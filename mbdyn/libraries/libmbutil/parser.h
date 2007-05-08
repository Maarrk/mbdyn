/* $Header$ */
/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2007
 *
 * Pierangelo Masarati	<masarati@aero.polimi.it>
 * Paolo Mantegazza	<mantegazza@aero.polimi.it>
 *
 * Dipartimento di Ingegneria Aerospaziale - Politecnico di Milano
 * via La Masa, 34 - 20156 Milano, Italy
 * http://www.aero.polimi.it
 *
 * Changing this copyright notice is forbidden.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License).
 * 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Parser per l'ingresso dati - parte generale */

/* Si compone di tre diverse strutture di scansione, 
 * piu' le strutture di memorizzazione di stringhe e variabili.
 * 
 * La prima struttura e' data dal LowParser, che riconosce gli elementi 
 * della sintassi. E' data da:
 * <statement_list>::=
 *   <statement> ; <statement_list>
 *   epsilon
 * <statement>::=
 *   <description>
 *   <description> : <arg_list>
 * <arg_list>::=
 *   <arg>
 *   <arg> , <arg_list>
 * <arg>::=
 *   <word>
 *   <number>
 * ecc. ecc.
 * 
 * La seconda struttura e' data dall'HighParser, che riconosce la sintassi 
 * vera e propria. In alternativa al LowParser, qualora sia atteso un valore
 * numerico esprimibile mediante un'espressione regolare 
 * (espressione matematica), e' possibile invocare la terza struttura,
 * il MathParser. Questo analizza espressioni anche complesse e multiple,
 * se correttamente racchiuse tra parentesi.
 * 
 * L'HighParser deve necessariamente riconoscere una parola chiave nel campo
 * <description>, mentre puo' trovare parole qualsiasi nel campo <arg>
 * qualora sia attesa una stringa.
 * 
 * Le parole chiave vengono fornite all'HighParser attraverso la KeyTable, 
 * ovvero una lista di parole impaccate (senza spazi). L'uso consigliato e':
 * 
 *   const char sKeyWords[] = { "keyword0",
 *                              "keyword1",
 *                              "...",
 *                              "keywordN"};
 * 
 *   enum KeyWords { KEYWORD0 = 0,
 *                   KEYWORD1,
 *                   ...,
 *                   KEYWORDN,
 *                   LASTKEYWORD};
 * 
 *   KeyTable K((int)LASTKEYWORD, sKeyWords);
 * 
 * Il MathParser usa una tabella di simboli, ovvero nomi (dotati di tipo) 
 * a cui e' associato un valore. La tabella e' esterna e quindi puo' essere 
 * conservata ed utilizzata in seguito conservando in memoria i nomi
 * definiti in precedenza.
 * 
 * A questo punto si puo' generare la tabella dei simboli:
 * 
 *   int iSymbolTableInitialSize = 10;
 *   Table T(iSymbolTableInitialSize);
 * 
 * Quindi si crea il MathParser:
 * 
 *   MathParser Math(T);
 * 
 * Infine si genera l'HighParser:
 * 
 *   HighParser HP(Math, K, StreamIn);
 * 
 * dove StreamIn e' l'istream da cui avviene la lettura.
 */
            

#ifndef PARSER_H
#define PARSER_H

#include <ac/iostream>
#include <ac/fstream>
#include <ac/f2c.h>

#include <string.h>
#include <ctype.h>

#include <stdlib.h>
#include <unistd.h>

#include "myassert.h"
#include "input.h"
#include "mathp.h"
#include "matvec3.h"
#include "matvec3n.h"
#include "matvec6.h"


/* Classi dichiarate */
class LowParser;
class KeyTable;
class HighParser;


const unsigned int iDefaultBufSize =
#ifdef BUFSIZ
	BUFSIZ
#else /* ! BUFSIZ */
	8192
#endif /* ! BUFSIZ */
;


/* LowParser - begin */

class LowParser {
	friend class HighParser;

public:
	enum Token {
		UNKNOWN,

		WORD,
		COMMA = ',',
		COLON = ':',
		SEMICOLON = ';',
		NUMBER,
		ENDOFFILE,

		LASTTOKEN
	};
   
private:
	HighParser& HP;
	enum Token CurrToken;
	char *sCurrWordBuf;
	unsigned iBufSize;
	doublereal dCurrNumber;

	void PackWords(InputStream& In);
   
public:
	LowParser(HighParser& hp);
	~LowParser(void);
	Token GetToken(InputStream& In);      
	doublereal dGetReal(void);
	integer iGetInt(void);   
	char* sGetWord(void);      
};

/* LowParser - end */


/* KeyTable - begin */

class KeyTable {
private:
	char* const* sKeyWords;
	const KeyTable *oldKey;
	HighParser& HP;
   
public:      
	KeyTable(HighParser& hp, const char* const sTable[]);   
	virtual ~KeyTable(void);
	int Find(const char* sToFind) const;
};

/* KeyTable - end */


/* HighParser - begin */

class HighParser {
public:   
	class ErrInvalidCallToGetDescription {};
	class ErrKeyWordExpected {};
	class ErrSemicolonExpected {};
	class ErrColonExpected {};
	class ErrMissingSeparator {};
	class ErrIntegerExpected {};
	class ErrRealExpected {};
	class ErrStringExpected {};
	class ErrIllegalDelimiter {};
   
public:      
	enum Token {
		UNKNOWN = -1,
		
		DESCRIPTION,
		FIRSTARG,
		ARG,
		LASTARG,
		NOARGS,
		WORD,	
		NUMBER,
		STRING,
		ENDOFFILE,

		LASTITEM
	};
   
	enum Delims {
		UNKNOWNDELIM = -1,
		
		PLAINBRACKETS,
		SQUAREBRACKETS,
		CURLYBRACKETS,
		SINGLEQUOTE,
		DOUBLEQUOTE,
		DEFAULTDELIM,

		LASTDELIM
	};
   
	const char ESCAPE_CHAR;
   
public:
	struct ErrOut {
		const char* sFileName;
		unsigned int iLineNumber;
	};

	struct WordSet {
		virtual ~WordSet(void) { NO_OP; };
		virtual bool IsWord(const std::string &s) const = 0;
	};
   
	enum {
		NONE		= 0x00U,
		EATSPACES	= 0x01U,
		ESCAPE		= 0x02U,
		LOWER		= 0x04U,
		UPPER		= 0x08U
	};

protected:   
	/* Parser di basso livello, per semplice lettura dei tipi piu' comuni */
	LowParser LowP;
     
	/* Stream in ingresso */
	InputStream* pIn;
	std::ifstream* pf;

	/* Buffer per le stringhe */
	char sStringBuf[iDefaultBufSize];
	char sStringBufWithSpaces[iDefaultBufSize];

	/* Parser delle espressioni matematiche, 
	 * usato per acquisire valori sicuramente numerici */
	MathParser& MathP;

	/* Tabella dei simboli da riconoscere; puo' essere cambiata */
	const KeyTable* KeyT;

	/* Token di basso ed alto livello */
	LowParser::Token CurrLowToken;
	Token CurrToken;

	virtual HighParser::Token FirstToken(void);
	virtual void NextToken(const char* sFuncName);

	int iGetDescription_int(const char* const s);
	void Set_int(void);
	void SetEnv_int(void);
	void Remark_int(void);
	virtual bool GetDescription_int(const char *s);
	virtual void Eof(void);

	virtual void
	SetDelims(enum Delims Del, char &cLdelim, char &cRdelim) const;

	int ParseWord(unsigned flags = HighParser::NONE);
	void PutbackWord(void);

public:   
	HighParser(MathParser& MP, InputStream& streamIn);
	virtual ~HighParser(void);
	/* Attacca una nuova KeyTable (e ritorna la vecchia) */
	virtual const KeyTable* PutKeyTable(const KeyTable& KT);
	/* Numero di linea corrente */   
	virtual int GetLineNumber(void) const;
	/* Numero di nome file e linea corrente */   
	virtual HighParser::ErrOut GetLineData(void) const;
	/* Restituisce il math parser */
	virtual MathParser& GetMathParser(void);
	/* "Chiude" i flussi */
	virtual void Close(void);
	/* verifica se il token successivo e' una description (ambiguo ...) */
	bool IsDescription(void);
	/* Legge una parola chiave */
	int GetDescription(void);
	/* si attende una descrizione */
	virtual void ExpectDescription(void);
	/* si attende una lista di argomenti */
	virtual void ExpectArg(void);
	/* 1 se trova la keyword sKeyWord */
	virtual bool IsKeyWord(const char* sKeyWord);
	/* numero della keyword trovata */
	virtual int IsKeyWord(void);
	/* 1 se e' atteso un argomento */
	virtual bool IsArg(void);
	/* 1 se e' atteso un argomento */
	virtual bool IsStringWithDelims(enum Delims Del = DEFAULTDELIM);
	/* se l'argomento successivo e' una parola in un WordSet, la ritorna */
	virtual const char *IsWord(const HighParser::WordSet& ws);
	/* Se ha letto un ";" lo rimette a posto */
	virtual void PutBackSemicolon(void);
	/* legge un intero con il mathpar */
	virtual integer GetInt(int iDefval = 0);
	/* legge un reale col mathpar */
	virtual doublereal GetReal(const doublereal& dDefval = 0.0);
	/* legge una string col mathpar */
	virtual std::string GetString(const std::string& sDefVal);
	/* legge una string col mathpar */
	virtual TypedValue GetValue(const TypedValue& v);
	/* legge una keyword */
	virtual int GetWord(void);
	/* legge una stringa */
	virtual const char* GetString(unsigned flags = HighParser::NONE);
	/* stringa delimitata */
	virtual const char* GetStringWithDelims(enum Delims Del = DEFAULTDELIM, bool escape = true); 
	/* vettore Vec3 */
	virtual Vec3 GetVec3(void);
	/* vettore Vec3 */
	virtual Vec3 GetVec3(const Vec3& vDef);
	/* matrice R mediante i due vettori */
	virtual Mat3x3 GetMatR2vec(void);
	/* matrice 3x3 simmetrica */
	virtual Mat3x3 GetMat3x3Sym(void);
	/* matrice 3x3 arbitraria */
	virtual Mat3x3 GetMat3x3(void);
	virtual Mat3x3 GetMat3x3(const Mat3x3& mDef);

	virtual Vec6 GetVec6(void);
	virtual Vec6 GetVec6(const Vec6& vDef);
	virtual Mat6x6 GetMat6x6(void);
	virtual Mat6x6 GetMat6x6(const Mat6x6& mDef);

	virtual inline doublereal Get(const doublereal& d) {
		return GetReal(doublereal(d));
	};
	virtual inline Vec3 Get(const Vec3& v) { return GetVec3(v); };
	virtual inline Mat3x3 Get(const Mat3x3& m) { return GetMat3x3(m); };
	virtual inline Vec6 Get(const Vec6& v) { return GetVec6(v); };
	virtual inline Mat6x6 Get(const Mat6x6& m) { return GetMat6x6(m); };

	virtual void GetMat6xN(Mat3xN& m1, Mat3xN& m2, integer iNumCols);
};

/* Le funzioni:
 *   ExpectDescription()
 *   ExpectArg()
 * informano il parser di cio' che e' atteso; di default il costruttore
 * setta ExpectDescription().
 * 
 * Le funzioni:
 *   GetDescription()
 *   IsKeyWord()
 *   GetWord()
 * restituiscono un intero che corrisponde alla posizione occupata nella
 * KeyTable dalle parole corrispondenti, oppure -1 se la parola non e'
 * trovata. Si noti che IsKeyWord(), in caso di esito negativo, ripristina 
 * l'istream. Tutte preparano poi il parser per la lettura successiva.
 * 
 * La funzione 
 *   IsKeyWord(const char*) 
 * restituisce 0 se non trova la parola e ripristina l'istream, altrimenti 
 * restituisce 1 e prepara il parser alla lettura successiva.
 * 
 * Le funzioni 
 *   GetInt(), 
 *   GetReal(), 
 *   GetString(), 
 *   GetStringWithDelims(enum Delims)
 * restituiscono i valori attesi e preparano il prser alla lettura successiva.
 */

/* HighParser - end */
   
extern std::ostream&
operator << (std::ostream& out, const HighParser::ErrOut& err);

#endif /* PARSER_H */


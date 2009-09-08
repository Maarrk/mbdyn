/* $Header$ */
/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2009
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

/* parser con include */

#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif /* HAVE_PWD_H */
#include <errno.h>

#include "parsinc.h"

const int PATHBUFSIZE = 1024;

/* IncludeParser - begin */

IncludeParser::IncludeParser(MathParser& MP, 
			     InputStream& streamIn,
			     const char *sInitialFile)
: HighParser(MP, streamIn),
sCurrPath(NULL),
sCurrFile(NULL)
{   
	ASSERT(sInitialFile != NULL);
#ifdef USE_INCLUDE_PARSER
   	char s[PATHBUFSIZE];
   	if (getcwd(s, PATHBUFSIZE) == NULL) {
		silent_cerr("Error in getcwd()" << std::endl);
      		throw ErrFileSystem(MBDYN_EXCEPT_ARGS);
   	}
	SAFESTRDUP(sCurrPath, s);
   	DEBUGCOUT("Current directory is <" << sCurrPath << '>' << std::endl);
   
   	SAFESTRDUP(sCurrFile, sInitialFile);
#else /* !USE_INCLUDE_PARSER */
   	NO_OP;
#endif /* !USE_INCLUDE_PARSER */
}


IncludeParser::~IncludeParser(void)
{   
   	IncludeParser::Close();
}
 

void IncludeParser::Close(void)
{
   	MyInput* pmi = NULL;
   	if (MyInStack.Pop(pmi)) {
      		ASSERT(pmi != NULL);
      		/* Nota: deve esserci solo l'ultimo file */
      		ASSERT(pf != NULL);
      		ASSERT(pIn != NULL);

#ifdef USE_INCLUDE_PARSER
      		ASSERT(sCurrPath != NULL);
      		ASSERT(sCurrFile != NULL);
#endif /* USE_INCLUDE_PARSER */

      		if (pf != NULL) {
	 		SAFEDELETE(pf);
      		}
      		if (pIn != NULL) {
	 		SAFEDELETE(pIn);
      		}

#ifdef USE_INCLUDE_PARSER
      		DEBUGCOUT("Leaving directory <" << sCurrPath 
			<< ">, file <" << sCurrFile << '>' << std::endl);
      		if (sCurrPath != NULL) {
	 		SAFEDELETEARR(sCurrPath);
	 		sCurrPath = NULL;
      		}
      		if (sCurrFile != NULL) {
	 		SAFEDELETEARR(sCurrFile);
	 		sCurrFile = NULL;
      		}
#endif /* USE_INCLUDE_PARSER */
      
      		pf = pmi->pfile;
      		pIn = pmi->pis;

#ifdef USE_INCLUDE_PARSER
      		sCurrPath = pmi->sPath;
      		sCurrFile = pmi->sFile;
      		DEBUGCOUT("Entering directory <" << sCurrPath 
			<< ">, file <" << sCurrFile << '>' << std::endl);
      		if (chdir(sCurrPath)) {
			silent_cerr("Error in chdir, path=" 
				<< sCurrPath << std::endl);
	 		throw ErrFileSystem(MBDYN_EXCEPT_ARGS);
      		}
#endif /* USE_INCLUDE_PARSER */

      		/* pmi must be non NULL */
      		SAFEDELETE(pmi);     
   	}
   
   	/* sCurrPath can be NULL if Close() has been already called */
#ifdef USE_INCLUDE_PARSER
   	if (sCurrPath != NULL) {
      		SAFEDELETEARR(sCurrPath);
      		sCurrPath = NULL;
   	}
   	if (sCurrFile != NULL) {
      		SAFEDELETEARR(sCurrFile);
      		sCurrFile = NULL;
   	}
#endif /* USE_INCLUDE_PARSER */
}

flag 
IncludeParser::fCheckStack(void)
{
   	MyInput* pmi = NULL;
   	if (MyInStack.Pop(pmi)) {
      		ASSERT(pmi != NULL);
      		/* 
       		 * Nota: se la stack e' piena, allora sia pf che pIn
		 * devono essere diversi da NULL; viceversa, se la stack
		 * e' vuota, pf deve essere NULL.
		 */
      		ASSERT(pf != NULL);
      		ASSERT(pIn != NULL);
#ifdef USE_INCLUDE_PARSER
      		ASSERT(sCurrPath != NULL);
      		ASSERT(sCurrFile != NULL);
#endif /* USE_INCLUDE_PARSER */
      
      		SAFEDELETE(pf); 
      		SAFEDELETE(pIn);
#ifdef USE_INCLUDE_PARSER
      		DEBUGCOUT("Leaving directory <" << sCurrPath 
			<< ">, file <" << sCurrFile << '>' << std::endl);
      		SAFEDELETEARR(sCurrPath);
      		sCurrPath = NULL;
      		SAFEDELETEARR(sCurrFile);
      		sCurrFile = NULL;
#endif /* USE_INCLUDE_PARSER */
      
      		pf = pmi->pfile;
      		pIn = pmi->pis;
#ifdef USE_INCLUDE_PARSER
      		sCurrPath = pmi->sPath;
      		sCurrFile = pmi->sFile;
      		DEBUGCOUT("Entering directory <" << sCurrPath 
			<< ">, file <" << sCurrFile << '>' << std::endl);
      		if (chdir(sCurrPath)) {
			silent_cerr("Error in chdir, path=" 
				<< sCurrPath << std::endl);
	 		throw ErrFileSystem(MBDYN_EXCEPT_ARGS);
      		}
#endif /* USE_INCLUDE_PARSER */
      
      		SAFEDELETE(pmi);
      		return flag(1);
   	} else {
      		return flag(0);
   	}
}

void 
IncludeParser::Include_int()
{
   	if (FirstToken() == UNKNOWN) {
		silent_cerr("Parser error in IncludeParser::Include_int(),"
			" colon expected at line " << GetLineData() 
			<< std::endl);
      		throw HighParser::ErrColonExpected(MBDYN_EXCEPT_ARGS);
   	}
   
   	const char* sfname = GetFileName();

	if (sfname != 0) {
		struct stat	s;

		if (stat(sfname, &s)) {
			int save_errno = errno;

			silent_cerr("Cannot stat file <" << sfname << "> "
				"at line " << GetLineData() << ": "
				<< save_errno 
				<< " (" << strerror(save_errno) << ")" 
				<< std::endl);
      			throw ErrFile(MBDYN_EXCEPT_ARGS);
		}

		if (!S_ISREG(s.st_mode)) {
			silent_cerr("File <" << sfname << "> "
				"at line " << GetLineData() << ": "
				"not a regular file?" << std::endl);
      			throw ErrFile(MBDYN_EXCEPT_ARGS);
		}

		if (!(s.st_mode & S_IRUSR)) {
			silent_cerr("File <" << sfname << "> "
				"at line " << GetLineData() << ": "
				"no read permissions?" << std::endl);
      			throw ErrFile(MBDYN_EXCEPT_ARGS);
		}
	}

	std::ifstream *pf_old = pf;
	InputStream *pIn_old = pIn;
	char *sOldPath = sCurrPath;
	char *sOldFile = sCurrFile;

   	pf = NULL;
   	pIn = NULL;

   	SAFENEWWITHCONSTRUCTOR(pf, std::ifstream, std::ifstream(sfname));
   	if (!(*pf)) {
#ifdef DEBUG
		char *buf = getcwd(NULL, 0);
		if (buf != NULL) {
			DEBUGCERR("Current directory <" << buf << ">" 
					<< std::endl);
			free(buf);
		}
#endif /* DEBUG */

		/* restore */
		pf = pf_old;
		pIn = pIn_old;
		sCurrPath = sOldPath;
		sCurrFile = sOldFile;
   
		silent_cerr("Invalid file <" << sfname << "> "
			"at line " << GetLineData() << std::endl);
      		throw ErrFile(MBDYN_EXCEPT_ARGS);
   	}
   
   	SAFENEWWITHCONSTRUCTOR(pIn, InputStream, InputStream(*pf));

   	/* Cambio di directory */
#ifdef USE_INCLUDE_PARSER
   	sCurrPath = NULL;
   	sCurrFile = NULL;
   	char* stmp = NULL;
   	SAFESTRDUP(stmp, sfname);
   	char* s = (char*)stmp+strlen(sfname);
   	while (--s >= stmp) {
      		if (s[0] == '/') {
	 		char c = s[1];
	 		s[1] = '\0';
	 		if (chdir(stmp)) {
				silent_cerr("Error in chdir, path=" 
					<< stmp << std::endl);
	    			throw ErrFileSystem(MBDYN_EXCEPT_ARGS);
	 		}
	 		char p[PATHBUFSIZE];
	 		if (getcwd(p, PATHBUFSIZE) == NULL) {
				silent_cerr("Error in getcwd()" << std::endl);
	    			SAFEDELETEARR(stmp);
	    			throw ErrFileSystem(MBDYN_EXCEPT_ARGS);
	 		}
			SAFESTRDUP(sCurrPath, p);
	 		DEBUGCOUT("Current directory is <" << sCurrPath 
				<< '>' << std::endl);
	 
	 		s[1] = c;
	 		break;
      		}
   	}
   	s++;
   	SAFESTRDUP(sCurrFile, s);
   	DEBUGCOUT("Opening file <" << sCurrFile << '>' << std::endl);
      
   	SAFEDELETEARR(stmp);
   
   	if (sCurrPath == NULL) {
      		char s[PATHBUFSIZE];
      		if (getcwd(s, PATHBUFSIZE) == NULL) {
			silent_cerr("Error in getcwd()" << std::endl);
	 		throw ErrFileSystem(MBDYN_EXCEPT_ARGS);
      		}
		SAFESTRDUP(sCurrPath, s);
      		DEBUGCOUT("Current directory is <" << sCurrPath 
			<< '>' << std::endl);
   	}
#endif /* USE_INCLUDE_PARSER */

      	MyInput* pmi = NULL;
#ifdef USE_INCLUDE_PARSER
   	SAFENEWWITHCONSTRUCTOR(pmi, 
			       MyInput,
			       MyInput(pf_old, pIn_old, sOldPath, sOldFile));
#else /* !USE_INCLUDE_PARSER */
   	SAFENEWWITHCONSTRUCTOR(pmi, MyInput, MyInput(pf_old, pIn_old));
#endif /* !USE_INCLUDE_PARSER */
 
   	MyInStack.Push(pmi);
 
   	/* FIXME: mettere un test se c'e' il punto e virgola? */
   	CurrToken = HighParser::DESCRIPTION;
}

void
IncludeParser::Eof(void)
{
	if(!fCheckStack()) {
		throw EndOfFile(MBDYN_EXCEPT_ARGS);
	}
}

bool
IncludeParser::GetDescription_int(const char *s)
{
   	if (!strcmp(s, "include")) {
      		Include_int();
      		return true;

#ifdef USE_INCLUDE_PARSER
	} else if (!strcmp(s, "chdir")) {
  	 	if (FirstToken() == UNKNOWN) {
			silent_cerr("Parser error "
				"in IncludeParser::Include_int(), "
				"colon expected at line " << GetLineData() 
				<< std::endl);
      			throw HighParser::ErrColonExpected(MBDYN_EXCEPT_ARGS);
   		}
   
   		const char* sfname = GetFileName();

      		if (chdir(sfname)) {
			silent_cerr("Error in chdir, path = " 
				<< sfname << std::endl);
	 		throw ErrFileSystem(MBDYN_EXCEPT_ARGS);
      		}
      		return true;

#endif /* USE_INCLUDE_PARSER */
		
   	}
	
	return HighParser::GetDescription_int(s);
}

/*
 * returns a dynamically allocated string with environment variables expanded
 */
char *
expand_environment(const char *in)
{
	char		*out = NULL;
#define MAXSUBST		10
	struct {
		unsigned	start;
		unsigned	end;
		const char	*value;
		unsigned	length;
	} 		subst[MAXSUBST];
	unsigned	cnt = 0, c;

	DEBUGCOUT(">> expand_environment: " << in << std::endl);

	subst[cnt].start = 0;
	subst[cnt].end = 0;
	subst[cnt].value = NULL;
	subst[cnt].length = 0;
	for (c = 0; in[c]; c++) {
		if (in[c] == '$') {
			if (cnt >= MAXSUBST - 2) {
				silent_cerr("too many substitutions in \""
						<< in << "\"" << std::endl);
				return NULL;
			}

			subst[cnt].end = c;
			if (in[c + 1] == '$') {
				c++;
				subst[cnt].start = c;
				subst[cnt].value = "";
				subst[cnt].length = 0;
				continue;
			}

			c++;
			unsigned namepos = c;
			if (in[c] == '{') {
				const char *end = std::strchr(&in[c], '}');

				if (end == NULL) {
					silent_cerr("missing trailing \"}\" "
							"in \"" << in << "\""
							<< std::endl);
					return NULL;
				}

				namepos++;
				unsigned l = end - &in[namepos];
				char buf[l + 1];
				memcpy(buf, &in[namepos], l);
				buf[l] = '\0';
				subst[cnt].value = getenv(buf);
				if (subst[cnt].value == NULL) {
					silent_cerr("unable to find "
							"environment "
							"variable \""
							<< buf << "\""
							<< std::endl);
					return NULL;
				}

				c = end - &in[0] + 1;

			} else {
				if (in[c] != '_' && !isalpha(in[c])) {
					silent_cerr("illegal leading char "
							"in environment "
							"variable name in \""
							<< in << "\""
							<< std::endl);
					return NULL;
				}

				for (c++; in[c]; c++) {
					if (in[c] != '_' && !isalnum(in[c])) {
						break;
					}
				}

				unsigned l = &in[c] - &in[namepos];
				char buf[l + 1];
				memcpy(buf, &in[namepos], l);
				buf[l] = '\0';

				subst[cnt].value = getenv(buf);
				if (subst[cnt].value == NULL) {
					silent_cerr("unable to find "
							"environment "
							"variable \""
							<< buf << "\""
							<< std::endl);
					return NULL;
				}
			}

			/* can't be NULL */
			subst[cnt].length = strlen(subst[cnt].value);

			cnt++;
			subst[cnt].start = c;

			/* because it's incremented again by "for" */
			c--;
		}
	}
	subst[cnt].end = c;
	subst[cnt].value = NULL;
	subst[cnt].length = 0;

	unsigned len = 0;
	for (c = 0; c < cnt; c++) {
		len += (subst[c].end - subst[c].start) + subst[c].length;
	}
	len += subst[c].end - subst[c].start;

	SAFENEWARR(out, char, len + 1);

	unsigned p = 0;
	for (c = 0; c < cnt; c++) {
		unsigned l = subst[c].end - subst[c].start;
		if (l > 0) {
			memcpy(&out[p], &in[subst[c].start], l);
			p += l;
		}
		if (subst[c].length > 0) {
			memcpy(&out[p], subst[c].value, subst[c].length);
			p += subst[c].length;
		}
	}
	unsigned l = subst[c].end - subst[c].start;
	if (l > 0) {
		memcpy(&out[p], &in[subst[c].start], l);
		p += l;
	}
	out[p] = '\0';

	DEBUGCOUT("<< expand_environment: " << out << std::endl);

	return out;
}

char *
resolve_filename(const char *filename_in)
{
	char	*res = NULL,
		*filename = NULL;;

	if (strchr(filename_in, '$')) {
		filename = expand_environment(filename_in);
		if (filename == NULL) {
			goto error_return;
		}
	} else {
		filename = (char *)filename_in;
	}
	
   	if (filename[0] == '~') {
      		filename++;
      		if (filename[0] == '/') {
	 		/* do environment stuff */
	 		char *home;
	 
	 		home = getenv("HOME");
	 		if (home == NULL) {	 	 
	    			goto error_return;
	 		}
	 
	 		char *s = NULL;
	 		int l, ll;
	 
	 		l = strlen(home);
			ll = l + strlen(filename) + 1;
	 		SAFENEWARR(s, char, ll);
	 
	 		strncpy(s, home, l);
	 		strcpy(s + l, filename);
	 
	 		res = s;
			goto error_return;

#if defined(HAVE_PWD_H)
      		} else {
	 		const char *p;
	 
	 		p = std::strchr(filename, '/');
	 		if (p == NULL) {
	    			goto error_return;
	 		} 
	 
	 		int l = p - filename;
	 
	 		char buf[l + 1];
	 		memcpy(buf, filename, l);
	 		buf[l] = '\0';

	 		/* do passwd stuff */
	 		struct passwd *pw;
	 
	 		pw = getpwnam(buf);
	 
	 		if (pw == NULL ) {
	    			goto error_return;
	 		}
	 
	 		l = strlen(pw->pw_dir);
			int ll = l + strlen(p) + 1;
	 		char *s = NULL;
	 		SAFENEWARR(s, char, ll);
	 		strncpy(s, pw->pw_dir, l);
	 		strcpy(s + l, p);
	 
	 		res = s;
			goto error_return;
#endif /* HAVE_PWD_H */
      		}
   	}

error_return:;
	if (filename != NULL) {
		if (res == NULL) {
			if (filename != filename_in) {
				res = filename;
			} else {
				SAFESTRDUP(res, filename_in);
			}
		} else {
			if (filename != filename_in) {
				SAFEDELETEARR(filename);
			}
		}
	}

	return res;
}

const char* 
IncludeParser::GetFileName(enum Delims Del)
{
   	const char *s = GetStringWithDelims(Del);
   	const char *stmp = resolve_filename(s);
	
   	if (stmp == NULL) {
      		return s;

   	} else {
      		if (strlen(stmp) >= iDefaultBufSize) {
	 		/* errore */
	 		strncpy(sStringBuf, stmp, iDefaultBufSize-1);
	 		sStringBuf[iDefaultBufSize-1] = '\0';
      		} else {
	 		strcpy(sStringBuf, stmp);
      		}
      		SAFEDELETEARR(stmp);
   	}
	
   	return sStringBuf;
}

IncludeParser::ErrOut
IncludeParser::GetLineData(void) const
{      
   	ErrOut LineData;
   	LineData.sFileName = sCurrFile;
   	LineData.iLineNumber = GetLineNumber();
   	return LineData;
}

/* IncludeParser - end */


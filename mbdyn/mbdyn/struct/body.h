/* $Header$ */
/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2008
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

/* elementi di massa, tipo: Elem::Type BODY */

#ifndef BODY_H
#define BODY_H

/* include per derivazione della classe */

#include "elem.h"
#include "strnode.h"
#include "gravity.h"

/* Body - begin */

class Body : 
virtual public Elem, public ElemGravityOwner, public InitialAssemblyElem {   
protected:
	const StructNode *pNode;
	doublereal dMass;
	Vec3 Xgc;
	Vec3 S0;
	Mat3x3 J0;
 
	mutable Vec3 S;
	mutable Mat3x3 J;

	/* momento statico */
	Vec3 GetS_int(void) const;

	/* momento d'inerzia */
	Mat3x3 GetJ_int(void) const;

	/* Scrive il contributo dell'elemento al file di restart */
	virtual std::ostream& Restart_int(std::ostream& out) const;
 
public:
	/* Costruttore definitivo (da mettere a punto) */
	Body(unsigned int uL, const StructNode *pNode,
		doublereal dMassTmp, const Vec3& XgcTmp, const Mat3x3& JTmp, 
		flag fOut);

	virtual ~Body(void);
 
	/* massa totale */
	doublereal dGetM(void) const {
		return dMass;
	};
 
	/* Tipo dell'elemento (usato solo per debug ecc.) */
	virtual Elem::Type GetElemType(void) const {
		return Elem::BODY; 
	};
 
	/* Numero gdl durante l'assemblaggio iniziale */
	virtual unsigned int iGetInitialNumDof(void) const { 
		return 0; 
	};

	virtual void AfterPredict(VectorHandler& X, VectorHandler& XP);
};

/* Body - end */

/* DynamicBody - begin */

class DynamicBody : 
virtual public Elem, public Body {
private:

	/* Assembla le due matrici necessarie per il calcolo degli
	 * autovalori e per lo jacobiano */  
	void AssMats(FullSubMatrixHandler& WorkMatA,
		FullSubMatrixHandler& WorkMatB,
		doublereal dCoef,
		bool bGravity,
		const Vec3& GravityAcceleration);

public:
	/* Costruttore definitivo (da mettere a punto) */
	DynamicBody(unsigned int uL, const DynamicStructNode* pNodeTmp, 
		doublereal dMassTmp, const Vec3& XgcTmp, const Mat3x3& JTmp, 
		flag fOut);

	virtual ~DynamicBody(void);
 
	/* Scrive il contributo dell'elemento al file di restart */
	virtual std::ostream& Restart(std::ostream& out) const;
 
	void WorkSpaceDim(integer* piNumRows, integer* piNumCols) const { 
		*piNumRows = 12; 
		*piNumCols = 6; 
	};
 
	virtual VariableSubMatrixHandler&
	AssJac(VariableSubMatrixHandler& WorkMat,
		doublereal dCoef,
		const VectorHandler& XCurr, 
		const VectorHandler& XPrimeCurr);

	void AssMats(VariableSubMatrixHandler& WorkMatA,
		VariableSubMatrixHandler& WorkMatB,
		const VectorHandler& XCurr,
		const VectorHandler& XPrimeCurr);
 
	virtual SubVectorHandler&
	AssRes(SubVectorHandler& WorkVec,
		doublereal dCoef,
		const VectorHandler& XCurr, 
		const VectorHandler& XPrimeCurr);
 
	/* Dimensione del workspace durante l'assemblaggio iniziale.
	 * Occorre tener conto del numero di dof che l'elemento definisce
	 * in questa fase e dei dof dei nodi che vengono utilizzati.
	 * Sono considerati dof indipendenti la posizione e la velocita'
	 * dei nodi */
	virtual void 
	InitialWorkSpaceDim(integer* piNumRows, integer* piNumCols) const { 
		*piNumRows = 12; 
		*piNumCols = 6; 
	};

	/* Contributo allo jacobiano durante l'assemblaggio iniziale */
	virtual VariableSubMatrixHandler& 
	InitialAssJac(VariableSubMatrixHandler& WorkMat,
		const VectorHandler& XCurr);

	/* Contributo al residuo durante l'assemblaggio iniziale */   
	virtual SubVectorHandler& 
	InitialAssRes(SubVectorHandler& WorkVec, const VectorHandler& XCurr);

	/* Usata per inizializzare la quantita' di moto */
	virtual void SetValue(DataManager *pDM,
		VectorHandler& X, VectorHandler& XP,
		SimulationEntity::Hints *ph = 0);

	/******** PER IL SOLUTORE PARALLELO *********/        
	/* Fornisce il tipo e la label dei nodi che sono connessi all'elemento
	 * utile per l'assemblaggio della matrice di connessione fra i dofs */
	virtual void 
	GetConnectedNodes(std::vector<const Node *>& connectedNodes) {
		connectedNodes.resize(1);
		connectedNodes[0] = pNode;
	};
	/**************************************************/
};

/* DynamicBody - end */

/* StaticBody - begin */

class StaticBody : 
virtual public Elem, public Body {   
private:
	const StructNode *pRefNode;
 
	/* Assembla le due matrici necessarie per il calcolo degli
	 * autovalori e per lo jacobiano */  
	bool AssMats(FullSubMatrixHandler& WorkMatA,
		FullSubMatrixHandler& WorkMatB,
		doublereal dCoef);

public:
	/* Costruttore definitivo (da mettere a punto) */
	StaticBody(unsigned int uL, const StaticStructNode* pNode,
		const StructNode* pRefNode,
		doublereal dMass, const Vec3& Xgc, const Mat3x3& J, 
		flag fOut);

	virtual ~StaticBody(void);
 
	/* Scrive il contributo dell'elemento al file di restart */
	virtual std::ostream& Restart(std::ostream& out) const;
 
	void WorkSpaceDim(integer* piNumRows, integer* piNumCols) const { 
		*piNumRows = 6; 
		*piNumCols = 6; 
	};
 
	virtual VariableSubMatrixHandler&
	AssJac(VariableSubMatrixHandler& WorkMat,
		doublereal dCoef,
		const VectorHandler& XCurr, 
		const VectorHandler& XPrimeCurr);

	void AssMats(VariableSubMatrixHandler& WorkMatA,
		VariableSubMatrixHandler& WorkMatB,
		const VectorHandler& XCurr,
		const VectorHandler& XPrimeCurr);
 
	virtual SubVectorHandler&
	AssRes(SubVectorHandler& WorkVec,
		doublereal dCoef,
		const VectorHandler& XCurr, 
		const VectorHandler& XPrimeCurr);
	
	/* Inverse Dynamics:*/
	virtual SubVectorHandler&
	AssRes(SubVectorHandler& WorkVec,
		const VectorHandler& /* XCurr */ ,
		const VectorHandler& /* XPrimeCurr */ ,
		const VectorHandler& /* XPrimePrimeCurr */ ,
		int iOrder = -1);

	/* Dimensione del workspace durante l'assemblaggio iniziale.
	 * Occorre tener conto del numero di dof che l'elemento definisce
	 * in questa fase e dei dof dei nodi che vengono utilizzati.
	 * Sono considerati dof indipendenti la posizione e la velocita'
	 * dei nodi */
	virtual void 
	InitialWorkSpaceDim(integer* piNumRows, integer* piNumCols) const { 
		*piNumRows = 6; 
		*piNumCols = 6; 
	};

	/* Contributo allo jacobiano durante l'assemblaggio iniziale */
	virtual VariableSubMatrixHandler& 
	InitialAssJac(VariableSubMatrixHandler& WorkMat,
		const VectorHandler& XCurr);

	/* Contributo al residuo durante l'assemblaggio iniziale */   
	virtual SubVectorHandler& 
	InitialAssRes(SubVectorHandler& WorkVec, const VectorHandler& XCurr);

	/* Usata per inizializzare la quantita' di moto */
	virtual void SetValue(DataManager *pDM,
		VectorHandler& X, VectorHandler& XP,
		SimulationEntity::Hints *ph = 0);

	/******** PER IL SOLUTORE PARALLELO *********/        
	/* Fornisce il tipo e la label dei nodi che sono connessi all'elemento
	 * utile per l'assemblaggio della matrice di connessione fra i dofs */
	virtual void 
	GetConnectedNodes(std::vector<const Node *>& connectedNodes) {
		connectedNodes.resize(1);
		connectedNodes[0] = pNode;
	};
	/**************************************************/
};

/* StaticBody - end */

class DataManager;
class MBDynParser;

extern Elem* ReadBody(DataManager* pDM, MBDynParser& HP, unsigned int uLabel);

#endif /* BODY_H */


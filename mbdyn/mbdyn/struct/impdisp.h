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

/* Deformable hinges */


#ifndef IMPDISP_H
#define IMPDISP_H

#include "joint.h"

/* ImposedDisplacementJoint - begin */

class ImposedDisplacementJoint :
virtual public Elem, public Joint, public DriveOwner {
private:

protected:
	const StructNode* pNode1;
	const StructNode* pNode2;
	mutable Vec3 f1;
	mutable Vec3 f2;

	Vec3 e1;
	doublereal e1xf1;

	Vec3 f2Ref;
	Vec3 dRef;
	Vec3 e1Ref;

	doublereal F;

	void AssMat(FullSubMatrixHandler& WM, doublereal dCoef);
	void AssVec(SubVectorHandler& WorkVec, doublereal dCoef);

public:
	/* Costruttore non banale */
	ImposedDisplacementJoint(unsigned int uL,
		const DofOwner* pDO,
		const DriveCaller* pDC,
		const StructNode* pN1,
		const StructNode* pN2,
		const Vec3& f1,
		const Vec3& f2,
		const Vec3& e1,
		flag fOut);

	/* Distruttore */
	virtual ~ImposedDisplacementJoint(void);

	/* Tipo di joint */
	virtual Joint::Type GetJointType(void) const {
		return IMPOSEDDISP;
	};

	/* Contributo al file di restart */
	virtual std::ostream& Restart(std::ostream& out) const;

	virtual void Output(OutputHandler& OH) const;

	void SetValue(DataManager *pDM,
			VectorHandler& X, VectorHandler& XP,
			SimulationEntity::Hints *ph = 0);

	virtual Hint *
	ParseHint(DataManager *pDM, const char *s) const;

	virtual unsigned int iGetNumDof(void) const {
		return 1;
	};

	virtual std::ostream& DescribeDof(std::ostream& out,
     		const char *prefix = "",
     		bool bInitial = false, int i = -1) const;

	virtual std::ostream& DescribeEq(std::ostream& out,
     		const char *prefix = "",
     		bool bInitial = false, int i = -1) const;

	virtual DofOrder::Order GetDofType(unsigned int i) const {
		ASSERT(i >= 0 && i <= 3);
		return DofOrder::ALGEBRAIC;
	};

	virtual void WorkSpaceDim(integer* piNumRows,
		integer* piNumCols) const
	{
		*piNumRows = 13;
		*piNumCols = 13;
	};

	/* assemblaggio jacobiano */
	virtual VariableSubMatrixHandler&
	AssJac(VariableSubMatrixHandler& WorkMat,
		doublereal dCoef,
		const VectorHandler& XCurr,
		const VectorHandler& XPrimeCurr);

	/* assemblaggio residuo */
	virtual SubVectorHandler&
	AssRes(SubVectorHandler& WorkVec,
		doublereal dCoef,
		const VectorHandler& XCurr,
		const VectorHandler& XPrimeCurr);

	/* Aggiorna le deformazioni ecc. */
	virtual void AfterPredict(VectorHandler& X, VectorHandler& XP);
	/* funzioni usate nell'assemblaggio iniziale */
	virtual unsigned int iGetInitialNumDof(void) const {
		return 2;
	};

	virtual void InitialWorkSpaceDim(integer* piNumRows,
		integer* piNumCols) const
	{
		*piNumRows = 26;
		*piNumCols = 26;
	};

	/* Contributo allo jacobiano durante l'assemblaggio iniziale */
	virtual VariableSubMatrixHandler&
	InitialAssJac(VariableSubMatrixHandler& WorkMat,
		const VectorHandler& XCurr);

	/* Contributo al residuo durante l'assemblaggio iniziale */
	virtual SubVectorHandler&
	InitialAssRes(SubVectorHandler& WorkVec,
		const VectorHandler& XCurr);

	/* Dati privati (aggiungere magari le reazioni vincolari) */
	virtual unsigned int iGetNumPrivData(void) const;

	unsigned int iGetPrivDataIdx(const char *s) const;

	virtual doublereal dGetPrivData(unsigned int i = 0) const;

	/* *******PER IL SOLUTORE PARALLELO******** */
	/* Fornisce il tipo e la label dei nodi che sono connessi all'elemento
	   utile per l'assemblaggio della matrice di connessione fra i dofs */
	virtual void GetConnectedNodes(std::vector<const Node *>& connectedNodes)
	{
		connectedNodes.resize(2);
		connectedNodes[0] = pNode1;
		connectedNodes[1] = pNode2;
	};
	/* ************************************************ */
};

/* ImposedDisplacementJoint - end */

/* ImposedDisplacementPinJoint - begin */

class ImposedDisplacementPinJoint :
virtual public Elem, public Joint, public DriveOwner {
private:

protected:
	const StructNode* pNode;
	mutable Vec3 f;
	mutable Vec3 x;

	Vec3 e;

	Vec3 fRef;
	Vec3 dRef;

	doublereal F;

	void AssMat(FullSubMatrixHandler& WM, doublereal dCoef);
	void AssVec(SubVectorHandler& WorkVec, doublereal dCoef);

public:
	/* Costruttore non banale */
	ImposedDisplacementPinJoint(unsigned int uL,
		const DofOwner* pDO,
		const DriveCaller* pDC,
		const StructNode* pN,
		const Vec3& f,
		const Vec3& x,
		const Vec3& e,
		flag fOut);

	/* Distruttore */
	virtual ~ImposedDisplacementPinJoint(void);

	/* Tipo di joint */
	virtual Joint::Type GetJointType(void) const {
		return IMPOSEDDISPPIN;
	};

	/* Contributo al file di restart */
	virtual std::ostream& Restart(std::ostream& out) const;

	virtual void Output(OutputHandler& OH) const;

	void SetValue(DataManager *pDM,
		VectorHandler& X, VectorHandler& XP,
		SimulationEntity::Hints *ph = 0);

	virtual Hint *
	ParseHint(DataManager *pDM, const char *s) const;

	virtual unsigned int iGetNumDof(void) const {
		return 1;
	};

	virtual std::ostream& DescribeDof(std::ostream& out,
     		const char *prefix = "",
     		bool bInitial = false, int i = -1) const;

	virtual std::ostream& DescribeEq(std::ostream& out,
     		const char *prefix = "",
     		bool bInitial = false, int i = -1) const;

	virtual DofOrder::Order GetDofType(unsigned int i) const {
		ASSERT(i >= 0 && i <= 3);
		return DofOrder::ALGEBRAIC;
	};

	virtual void WorkSpaceDim(integer* piNumRows,
		integer* piNumCols) const
	{
		*piNumRows = 7;
		*piNumCols = 7;
	};

	/* assemblaggio jacobiano */
	virtual VariableSubMatrixHandler&
	AssJac(VariableSubMatrixHandler& WorkMat,
		doublereal dCoef,
		const VectorHandler& XCurr,
		const VectorHandler& XPrimeCurr);

	/* assemblaggio residuo */
	virtual SubVectorHandler&
	AssRes(SubVectorHandler& WorkVec,
		doublereal dCoef,
		const VectorHandler& XCurr,
		const VectorHandler& XPrimeCurr);

	/* Aggiorna le deformazioni ecc. */
	virtual void AfterPredict(VectorHandler& X, VectorHandler& XP);
	/* funzioni usate nell'assemblaggio iniziale */
	virtual unsigned int iGetInitialNumDof(void) const {
		return 2;
	};

	virtual void InitialWorkSpaceDim(integer* piNumRows,
			integer* piNumCols) const
	{
		*piNumRows = 14;
		*piNumCols = 14;
	};

	/* Contributo allo jacobiano durante l'assemblaggio iniziale */
	virtual VariableSubMatrixHandler&
	InitialAssJac(VariableSubMatrixHandler& WorkMat,
		const VectorHandler& XCurr);

	/* Contributo al residuo durante l'assemblaggio iniziale */
	virtual SubVectorHandler&
	InitialAssRes(SubVectorHandler& WorkVec,
		const VectorHandler& XCurr);

	/* Dati privati (aggiungere magari le reazioni vincolari) */
	virtual unsigned int iGetNumPrivData(void) const;

	unsigned int iGetPrivDataIdx(const char *s) const;

	virtual doublereal dGetPrivData(unsigned int i = 0) const;

	/* *******PER IL SOLUTORE PARALLELO******** */
	/* Fornisce il tipo e la label dei nodi che sono connessi all'elemento
	   utile per l'assemblaggio della matrice di connessione fra i dofs */
	virtual void GetConnectedNodes(std::vector<const Node *>& connectedNodes)
	{
		connectedNodes.resize(1);
		connectedNodes[0] = pNode;
	};
	/* ************************************************ */
};

/* ImposedDisplacementPinJoint - end */

#endif /* IMPDISP_H */


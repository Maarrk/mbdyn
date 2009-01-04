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

#ifndef ROTTRIM_H
#define ROTTRIM_H

#include <rotor.h>
#include <genel.h>

class RotorTrim : virtual public Elem, public Genel {
protected:
	Rotor* pRotor;
	ScalarDifferentialNode* pvNodes[3];
	DriveOwner pvDrives[3];
	DriveOwner Trigger;
	
	doublereal dGamma;
	doublereal dP;
	
	doublereal dP2;
	doublereal dC;
	doublereal dC2;
	
	doublereal dTau0;
	doublereal dTau1;
	doublereal dKappa0;
	doublereal dKappa1;
	
public:
	RotorTrim(unsigned int uL,
		const DofOwner* pDO,
		Rotor* pRot, 
		ScalarDifferentialNode* pNode1,
		ScalarDifferentialNode* pNode2,
		ScalarDifferentialNode* pNode3,
		DriveCaller* pDrive1,
		DriveCaller* pDrive2,
		DriveCaller* pDrive3,
		const doublereal& dG,
		const doublereal& dp,
		const doublereal& dT0,
		const doublereal& dT1,
		const doublereal& dK0,
		const doublereal& dK1,
		DriveCaller *pTrigger,
		flag fOut);

	virtual ~RotorTrim(void);
	
	virtual unsigned int iGetNumDof(void) const;
	
	/* Scrive il contributo dell'elemento al file di restart */
	virtual std::ostream& Restart(std::ostream& out) const;

	/* Tipo di Genel */
	virtual Genel::Type GetGenelType(void) const { 
		return Genel::ROTORTRIM; 
	};
	
	/* Dimensioni del workspace */
	virtual void
	WorkSpaceDim(integer* piNumRows, integer* piNumCols) const;
	
	/* assemblaggio jacobiano */
	virtual VariableSubMatrixHandler& 
	AssJac(VariableSubMatrixHandler& WorkMat,
	       doublereal dCoef,
	       const VectorHandler& /* XCurr */ ,
	       const VectorHandler& /* XPrimeCurr */ );
	
	/* assemblaggio residuo */
	virtual SubVectorHandler&
	AssRes(SubVectorHandler& WorkVec,
	       doublereal dCoef,
	       const VectorHandler& /* XCurr */ ,
	       const VectorHandler& /* XPrimeCurr */ );

 	/* *******PER IL SOLUTORE PARALLELO******** */        
   	/*
	 * Fornisce il tipo e la label dei nodi che sono connessi all'elemento
	 * utile per l'assemblaggio della matrice di connessione fra i dofs
	 */
	virtual void
	GetConnectedNodes(std::vector<const Node *>& connectedNodes) {
		pRotor->GetConnectedNodes(connectedNodes);
		int NumNodes = connectedNodes.size();
		connectedNodes.resize(NumNodes + 3);
		for (int i = 0; i < 3; i++) {
			connectedNodes[NumNodes + i] = pvNodes[i];
		}
	};
	/* ************************************************ */
};

#endif /* ROTTRIM_H */


/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2003
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

#ifndef INSTRUMENTS_H
#define INSTRUMENTS_H

#ifdef USE_AERODYNAMIC_ELEMS

/* Elementi aerodinamici stazionari */

#include "aerodyn.h"

class AircraftInstruments : virtual public Elem, public AerodynamicElem {
public:
	enum Measure {
		AIRSPEED = 1,
		GROUNDSPEED,
		ALTITUDE,
		ATTITUDE,
		BANK,
		TURN,
		SLIP,
		VERTICALSPEED,
		AOA,
		LASTMEASURE
	};

protected:
	const StructNode* pNode;

	doublereal dMeasure[LASTMEASURE];

	void Update(void);
	
public:
	AircraftInstruments(unsigned int uLabel, const StructNode* pN,
			flag fOut);
	virtual ~AircraftInstruments(void);
	
	virtual inline void* pGet(void) const { 
		return (void*)this;
	};
	
	/* Scrive il contributo dell'elemento al file di restart */
	virtual std::ostream& Restart(std::ostream& out) const;
	
	/* Tipo dell'elemento (usato per debug ecc.) */
	virtual Elem::Type GetElemType(void) const {
		return Elem::AERODYNAMIC;
	};
	
	/* funzioni proprie */
	
	/*
	 * output; si assume che ogni tipo di elemento sappia, attraverso
	 * l'OutputHandler, dove scrivere il proprio output
	 */
	virtual void Output(OutputHandler& OH) const;
	
	/* Tipo di elemento aerodinamico */
	virtual AerodynamicElem::Type GetAerodynamicElemType(void) const {
		return AerodynamicElem::AIRCRAFTINSTRUMENTS;
	};
	
	/* Dimensioni del workspace */
	virtual void
	WorkSpaceDim(integer* piNumRows, integer* piNumCols) const;
	
	/* assemblaggio jacobiano */
	virtual VariableSubMatrixHandler& 
	AssJac(VariableSubMatrixHandler& WorkMat,
	       doublereal /* dCoef */ ,
	       const VectorHandler& /* XCurr */ ,
	       const VectorHandler& /* XPrimeCurr */ );
	
	/* assemblaggio residuo */
	virtual SubVectorHandler&
	AssRes(SubVectorHandler& WorkVec,
	       doublereal dCoef,
	       const VectorHandler& XCurr,
	       const VectorHandler& XPrimeCurr);
	
	/* Dati privati */
	virtual unsigned int iGetNumPrivData(void) const;
	virtual unsigned int iGetPrivDataIdx(const char *s) const;
	virtual doublereal dGetPrivData(unsigned int i) const;

	/* *******PER IL SOLUTORE PARALLELO******** */
	/*
	 * Fornisce il tipo e la label dei nodi che sono connessi all'elemento
	 * utile per l'assemblaggio della matrice di connessione fra i dofs
	 */
	virtual void
	GetConnectedNodes(int& NumNodes,
			  Node::Type* NdTyps,
			  unsigned int* NdLabels) {
		NumNodes = 1;
		NdTyps[0] = pNode->GetNodeType();
		NdLabels[0] = pNode->GetLabel();
	};
	
	/* ************************************************ */
};

class DataManager;
class MBDynParser;

extern Elem *
ReadAircraftInstruments(DataManager* pDM, MBDynParser& HP, unsigned int uLabel);

#endif /* USE_AERODYNAMIC_ELEMS */

#endif /* INSTRUMENTS_H */


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

/*****************************************************************************
 *                                                                           *
 *                            SOLUTION MANAGER                               *
 *                                                                           *
 *****************************************************************************/

/* Pierangelo Masarati */


#ifndef LS_H
#define LS_H

#include "ac/math.h"
#include "ac/iostream"
#include "ac/f2c.h"

#include <vector>

/* per il debugging */
#include "myassert.h"
#include "mynewmem.h"
#include "except.h"

/* LinearSolver - begin */

class LinearSolver {
protected:
	SolutionManager *pSM;
	mutable bool bHasBeenReset;
	doublereal *pdRhs;
	doublereal *pdSol;

public:
	LinearSolver(SolutionManager *pSM = NULL);
	virtual ~LinearSolver(void);

#ifdef DEBUG
	void IsValid(void) const;
#endif /* DEBUG */

	virtual void Reset(void);
	virtual void Solve(void) const = 0;

	bool bReset(void) const { return bHasBeenReset; };
	void SetSolutionManager(SolutionManager *pSM);
	doublereal *pdGetResVec(void) const;
	doublereal *pdGetSolVec(void) const;
	doublereal *pdSetResVec(doublereal* pd);
	doublereal *pdSetSolVec(doublereal* pd);

	virtual void MakeCompactForm(SparseMatrixHandler& mh,
			std::vector<doublereal>& Ax,
			std::vector<integer>& Ar,
			std::vector<integer>& Ac,
			std::vector<integer>& Ap) const;
};

/* LinearSolver - end */

#endif /* LS_H */


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


#ifndef SOLMAN_H
#define SOLMAN_H

#include "ac/math.h"
#include "ac/iostream"
#include "ac/f2c.h"

/* per il debugging */
#include "myassert.h"
#include "mynewmem.h"
#include "except.h"

#include "vh.h"
#include "mh.h"

/* Zero for sparse vector and matrix handlers */
extern const doublereal dZero;

/* classi virtuali dichiarate in questo file */
class MatrixHandler;    /* gestore matrice */
class SparseMatrixHandler;
class VectorHandler;    /* gestore vettore */
class SolutionManager;  /* gestore della soluzione */

/* classi usate in questo file */
class SubMatrixHandler;
class FullSubMatrixHandler;
class SparseSubMatrixHandler;
class VariableSubMatrixHandler;
class SubVectorHandler;
class Vec3;
class Mat3x3;
class LinearSolver;

/* SolutionDataManager - begin */

class SolutionDataManager {
public:
	virtual ~SolutionDataManager(void);

	struct ChangedEquationStructure {};
	
	/* Collega il DataManager ed il DriveHandler ai vettori soluzione */
	virtual void
	LinkToSolution(const VectorHandler& XCurr,
			const VectorHandler& XPrimeCurr) = 0;

	/* Assembla il residuo */
	virtual void AssRes(VectorHandler &ResHdl, doublereal dCoef) 
		throw(ChangedEquationStructure) = 0;
};

/* SolutionDataManager - end */


/* SolutionManager - begin */

class SolutionManager {
protected:
	LinearSolver *pLS;

public:
	SolutionManager(void);

	/* Distruttore: dealloca le matrici e distrugge gli oggetti propri */
	virtual ~SolutionManager(void);

	virtual void
	LinkToSolution(const VectorHandler& XCurr,
			const VectorHandler& XPrimeCurr);

#ifdef DEBUG
	virtual void IsValid(void) const = 0;
#endif /* DEBUG */

	/* Inizializzatore generico */
	virtual void MatrReset(void) = 0;

	/* Inizializzatore "speciale" */
	virtual void MatrInitialize(void);

	/* Risolve il sistema */
	virtual void Solve(void) = 0;
	virtual void SolveT(void);

	/* Rende disponibile l'handler per la matrice */
	virtual MatrixHandler* pMatHdl(void) const = 0;

	/* Rende disponibile l'handler per il termine noto */
	virtual VectorHandler* pResHdl(void) const = 0;

	/* Rende disponibile l'handler per la soluzione (e' lo stesso
	 * del termine noto, ma concettualmente sono separati) */
	virtual VectorHandler* pSolHdl(void) const = 0;

   	/* sposta il puntatore al vettore del residuo */
   	doublereal *pdSetResVec(doublereal* pd);
   
   	/* sposta il puntatore al vettore della soluzione */
   	doublereal *pdSetSolVec(doublereal* pd);
};

/* SolutionManager - end */

#endif /* SOLMAN_H */


/* $Header$ */
/*
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 2003-2008
 * 
 * This code is a partial merge of HmFe and MBDyn.
 *
 * Pierangelo Masarati  <masarati@aero.polimi.it>
 * Paolo Mantegazza     <mantegazza@aero.polimi.it>
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

#ifndef NAIVEMH_H
#define NAIVEMH_H

#include <vector>

#include "myassert.h"
#include "solman.h"

class NaiveSolver;
class MultiThreadDataManager;

/* Sparse Matrix */
class NaiveMatrixHandler : public MatrixHandler {
	friend void* sum_naive_matrices(void* arg);
	friend class NaiveSolver;
	friend class ParNaiveSolver;
	friend class MultiThreadDataManager;

protected:
	integer iSize;
	bool bOwnsMemory;
	doublereal **ppdRows;
	integer **ppiRows, **ppiCols;
	char **ppnonzero;
	integer *piNzr, *piNzc;

#ifdef DEBUG
	void IsValid(void) const {
		NO_OP;
	};
#endif /* DEBUG */

public:
	/* FIXME: always square? yes! */
	NaiveMatrixHandler(const integer n, NaiveMatrixHandler *const nmh = 0);

	virtual ~NaiveMatrixHandler(void);

	integer iGetNumRows(void) const {
		return iSize;
	};

	integer iGetNumCols(void) const {
		return iSize;
	};

	void Reset(void);

	/* Ridimensiona la matrice */
	virtual void Resize(integer, integer) {
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	};

	virtual inline const doublereal&
	operator () (integer iRow, integer iCol) const;

	virtual inline doublereal&
	operator () (integer iRow, integer iCol);

	/* Overload di += usato per l'assemblaggio delle matrici */
	virtual MatrixHandler& operator += (const SubMatrixHandler& SubMH);

	/* Overload di -= usato per l'assemblaggio delle matrici */
	virtual MatrixHandler& operator -= (const SubMatrixHandler& SubMH);

	/* Overload di += usato per l'assemblaggio delle matrici
	 * questi li vuole ma non so bene perche'; force per la doppia
	 * derivazione di VariableSubMatrixHandler */
	virtual MatrixHandler&
	operator += (const VariableSubMatrixHandler& SubMH);
	virtual MatrixHandler&
	operator -= (const VariableSubMatrixHandler& SubMH);
	
	void MakeCCStructure(std::vector<integer>& Ai,
		std::vector<integer>& Ap);

protected:
        /* Matrix Matrix product */
	virtual MatrixHandler&
	MatMatMul_base(void (MatrixHandler::*op)(integer iRow, integer iCol,
				const doublereal& dCoef),
			MatrixHandler& out, const MatrixHandler& in) const;
	virtual MatrixHandler&
	MatTMatMul_base(void (MatrixHandler::*op)(integer iRow, integer iCol,
				const doublereal& dCoef),
			MatrixHandler& out, const MatrixHandler& in) const;

	/* Matrix Vector product */
	virtual VectorHandler&
	MatVecMul_base(void (VectorHandler::*op)(integer iRow,
				const doublereal& dCoef),
			VectorHandler& out, const VectorHandler& in) const;
	virtual VectorHandler&
	MatTVecMul_base(void (VectorHandler::*op)(integer iRow,
				const doublereal& dCoef),
			VectorHandler& out, const VectorHandler& in) const;
};


const doublereal&
NaiveMatrixHandler::operator () (integer iRow, integer iCol) const
{
	--iRow;
	--iCol;
	if (ppnonzero[iRow][iCol]) {
		return ppdRows[iRow][iCol];
	}
	return ::dZero;
}

doublereal&
NaiveMatrixHandler::operator () (integer iRow, integer iCol)
{
	--iRow;
	--iCol;
	if (!(ppnonzero[iRow][iCol])) {
		ppnonzero[iRow][iCol] = 1;
		ppiRows[iCol][piNzr[iCol]] = iRow;
		ppiCols[iRow][piNzc[iRow]] = iCol;
		piNzr[iCol]++;
		piNzc[iRow]++;
		ppdRows[iRow][iCol] = 0.;
	}

	return ppdRows[iRow][iCol];
}

/* Sparse Matrix with unknowns permutation*/
class NaivePermMatrixHandler : public NaiveMatrixHandler {
protected:
	const integer* const perm;
	const integer* const invperm;

#ifdef DEBUG
	void IsValid(void) const {
		NO_OP;
	};
#endif /* DEBUG */

public:
	/* FIXME: always square? yes! */
	NaivePermMatrixHandler(integer iSize,
		const integer *const tperm,
		const integer *const invperm);

	NaivePermMatrixHandler(NaiveMatrixHandler*const nmh, 
		const integer *const tperm,
		const integer *const invperm);

	virtual ~NaivePermMatrixHandler(void);

	const integer* const pGetPerm(void) const;

	const integer* const pGetInvPerm(void) const;

	virtual inline const doublereal&
	operator () (integer iRow, integer iCol) const {
		/* FIXME: stupid 0/1 based arrays... */
		iCol = perm[iCol - 1] + 1;
		return NaiveMatrixHandler::operator()(iRow, iCol);
	};

	virtual inline doublereal&
	operator () (integer iRow, integer iCol) {
		/* FIXME: stupid 0/1 based arrays... */
		iCol = perm[iCol - 1] + 1;
		return NaiveMatrixHandler::operator()(iRow, iCol);
	};

protected:
        /* Matrix Matrix product */
	virtual MatrixHandler&
	MatMatMul_base(void (MatrixHandler::*op)(integer iRow, integer iCol,
				const doublereal& dCoef),
			MatrixHandler& out, const MatrixHandler& in) const;
	virtual MatrixHandler&
	MatTMatMul_base(void (MatrixHandler::*op)(integer iRow, integer iCol,
				const doublereal& dCoef),
			MatrixHandler& out, const MatrixHandler& in) const;

	/* Matrix Vector product */
	virtual VectorHandler&
	MatVecMul_base(void (VectorHandler::*op)(integer iRow,
				const doublereal& dCoef),
			VectorHandler& out, const VectorHandler& in) const;
	virtual VectorHandler&
	MatTVecMul_base(void (VectorHandler::*op)(integer iRow,
				const doublereal& dCoef),
			VectorHandler& out, const VectorHandler& in) const;
};


#endif /* NAIVEMH_H */


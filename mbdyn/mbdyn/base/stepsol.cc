/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2004
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
 
 /* 
  *
  * Copyright (C) 2004
  * Giuseppe Quaranta	<quaranta@aero.polimi.it>
  *
  * classi che impementano l'integrazione al passo 
  */
 
#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

 
#include <schurdataman.h> 
#include <external.h>
#include <solver.h>
#include <stepsol.h>

StepIntegrator::StepIntegrator(const integer MaxIt,
		const doublereal dT,
		const doublereal dSolutionTol,
		const integer stp,
		const integer sts) 
: pDM(NULL),
DofIterator(),
outputPred(false),
MaxIters(MaxIt),
dTol(dT),
dSolTol(dSolutionTol),
steps(stp),
unkstates(sts)
{
	NO_OP;
}

StepIntegrator::~StepIntegrator(void)
{
	NO_OP;
}

void
StepIntegrator::SetDataManager(DataManager* pDatMan)
{
	pDM = pDatMan;
	DofIterator = pDM->GetDofIterator();
}

integer
StepIntegrator::GetIntegratorNumPreviousStates(void) const
{
	return steps;
}

integer
StepIntegrator::GetIntegratorNumUnknownStates(void) const
{
	return unkstates;
}

void
StepIntegrator::OutputTypes(const bool fpred)
{
	outputPred  = fpred;
}

void
StepIntegrator::SetDriveHandler(const DriveHandler* /* pDH */ )
{
	NO_OP;
}

#include "stepsol.hc"

ImplicitStepIntegrator::ImplicitStepIntegrator(const integer MaxIt,
		const doublereal dT,
		const doublereal dSolutionTol,
		const integer stp,
		const integer sts,
		const bool bmod_res_test)
: StepIntegrator(MaxIt, dT, dSolutionTol, stp, sts),
bEvalProdCalledFirstTime(true),
pXCurr(0),
pXPrimeCurr(0),
bModResTest(bmod_res_test)
{
	NO_OP;
}

ImplicitStepIntegrator::~ImplicitStepIntegrator(void)
{
	NO_OP;
}

void
ImplicitStepIntegrator::EvalProd(doublereal Tau, const VectorHandler& f0,
	const VectorHandler& w, VectorHandler& z) const
{
	/* matrix-free product                                     
         *                                                      
         * J(XCurr) * w = -||w|| * (Res(XCurr + sigma * Tau * w/||w||) - f0) / (sigma * Tau)
         * 
         */
	if (bEvalProdCalledFirstTime) {
		XTau.Resize(w.iGetSize());
		SavedState.Resize(w.iGetSize());
		SavedDerState.Resize(w.iGetSize());
		bEvalProdCalledFirstTime = false;
	}

	SavedState = *pXCurr;
	SavedDerState = *pXPrimeCurr;

	/* if w = 0; J * w = 0 */ 
	ASSERT(pDM != NULL);
        
	doublereal nw = w.Norm();
        if (nw < DBL_EPSILON) {
                z.Reset();
                return;
        }
        doublereal sigma = pXCurr->InnerProd(w);
        sigma /=  nw;
        if (fabs(sigma) > DBL_EPSILON) {
                doublereal xx = (fabs( sigma) <= 1.) ? 1. : fabs(sigma);
                Tau = copysign(Tau*xx, sigma);
        }
        Tau /= nw;
#ifdef DEBUG_ITERATIVE
	std::cout << "Tau " << Tau << std::endl;
#endif /* DEBUG_ITERATIVE */
	
	XTau.Reset();
	z.Reset();
        XTau.ScalarMul(w, Tau);
	Update(&XTau);
#ifdef  USE_EXTERNAL
        External::SendFreeze();
#endif /* USE_EXTERNAL */
	Residual(&z);
	XTau.ScalarMul(XTau, -1.);

	/* riporta tutto nelle condizioni inziali */
#if 0
	Update(&XTau);
#endif
	*pXCurr = SavedState;
	*pXPrimeCurr = SavedDerState;
	pDM->Update();
	z -= f0;
	z.ScalarMul(z, -1./Tau);
}

/* scale factor for tests */
doublereal
ImplicitStepIntegrator::TestScale(const NonlinearSolverTest *pTest) const
{
	if (bModResTest) {
#ifdef USE_MPI
#warning "ImplicitStepIntegrator::TestScale() not available with Schur solution"
#endif /* USE_MPI */

		Dof CurrDof;
		doublereal dXPr = 0.;

		DofIterator.bGetFirst(CurrDof); 

	   	for (int iCntp1 = 1; iCntp1 <= pXPrimeCurr->iGetSize(); 
				iCntp1++, DofIterator.bGetNext(CurrDof)) {

			if (CurrDof.Order == DofOrder::DIFFERENTIAL) {
				doublereal d = pXPrimeCurr->dGetCoef(iCntp1);
				doublereal d2 = d*d;

				doublereal ds = pTest->dScaleCoef(iCntp1);
				doublereal ds2 = ds*ds;
				d2 *= ds2;

				dXPr += d2;
			}
			/* else if ALGEBRAIC: non aggiunge nulla */
		}

	   	return 1./(1.+dXPr);

	} else {
		return 1.;
	}
}


DerivativeSolver::DerivativeSolver(const doublereal Tl, 
		const doublereal dSolTl, 
		const doublereal dC,
		const integer iMaxIt,
		const bool bmod_res_test) 
: ImplicitStepIntegrator(iMaxIt, Tl, dSolTl, 1, 1, bmod_res_test),
dCoef(dC)
{
	NO_OP;
}

DerivativeSolver::~DerivativeSolver(void)
{
	NO_OP;
}

doublereal
DerivativeSolver::Advance(Solver* pS, 
		const doublereal TStep, 
		const doublereal /* dAph */, 
		const StepChange /* StType */,
		std::deque<MyVectorHandler*>& qX,
	 	std::deque<MyVectorHandler*>& qXPrime,
		MyVectorHandler*const pX,
 		MyVectorHandler*const pXPrime,
		integer& EffIter,
		doublereal& Err,
		doublereal& SolErr)
{
	/* no predizione */
	ASSERT(pDM != NULL);
	pXCurr = pX;

	pXPrimeCurr = pXPrime;

	pDM->LinkToSolution(*pXCurr, *pXPrimeCurr);
	Err = 0.;
	pS->pGetNonlinearSolver()->Solve(this, pS, MaxIters, dTol,
    			EffIter, Err, dSolTol, SolErr);

	/* if it gets here, it surely converged */
	pDM->AfterConvergence();

	return Err;
}

void
DerivativeSolver::Residual(VectorHandler* pRes) const
{
	ASSERT(pDM != NULL);
	pDM->AssRes(*pRes, dCoef);
}

void
DerivativeSolver::Jacobian(MatrixHandler* pJac) const
{
	ASSERT(pDM != NULL);
	pDM->AssJac(*pJac, dCoef);
}

void DerivativeSolver::UpdateDof(const int DCount,
		const DofOrder::Order Order,
		const VectorHandler* const pSol) const {
	doublereal d = pSol->dGetCoef(DCount);
	if (Order == DofOrder::DIFFERENTIAL) {
		pXPrimeCurr->IncCoef(DCount, d);
		
		/* Nota: b0Differential e b0Algebraic 
		 * possono essere distinti;
		 * in ogni caso sono calcolati 
		 * dalle funzioni di predizione
		 * e sono dati globali */
		pXCurr->IncCoef(DCount, dCoef*d);
	} else {
		pXCurr->IncCoef(DCount, d);
		pXPrimeCurr->IncCoef(DCount, dCoef*d);
	}	
}
		
void
DerivativeSolver::Update(const VectorHandler* pSol) const
{
	DEBUGCOUTFNAME("DerivativeSolver::Update");
	ASSERT(pDM != NULL);
	
	UpdateLoop(this, &DerivativeSolver::UpdateDof, pSol);
	pDM->DerivativesUpdate();
}

/* scale factor for tests */
doublereal
DerivativeSolver::TestScale(const NonlinearSolverTest *pTest) const
{
	return 1.;
}


StepNIntegrator::StepNIntegrator(const integer MaxIt,
		const doublereal dT,
		const doublereal dSolutionTol,
		const integer stp,
		const bool bmod_res_test)
: ImplicitStepIntegrator(MaxIt, dT, dSolutionTol, stp , 1, bmod_res_test),
db0Differential(0.),
db0Algebraic(0.)
{
	NO_OP;
}

StepNIntegrator::~StepNIntegrator(void)
{
	NO_OP;
}

void
StepNIntegrator::Residual(VectorHandler* pRes) const
{
	ASSERT(pDM != NULL);
	pDM->AssRes(*pRes, db0Differential);
}

void
StepNIntegrator::Jacobian(MatrixHandler* pJac) const
{
	ASSERT(pDM != NULL);
	pDM->AssJac(*pJac, db0Differential);
}

void StepNIntegrator::UpdateDof(const int DCount,
	const DofOrder::Order Order,
	const VectorHandler* const pSol) const {
	doublereal d = pSol->dGetCoef(DCount);
	if (Order == DofOrder::DIFFERENTIAL) {
		pXPrimeCurr->IncCoef(DCount, d);
		
		/* Nota: b0Differential e b0Algebraic 
		 * possono essere distinti;
		 * in ogni caso sono calcolati 
		 * dalle funzioni di predizione
		 * e sono dati globali */
		pXCurr->IncCoef(DCount, db0Differential*d);
	} else {
		pXCurr->IncCoef(DCount, d);
		pXPrimeCurr->IncCoef(DCount, db0Algebraic*d);
	}
}
	
void
StepNIntegrator::Update(const VectorHandler* pSol) const
{
	DEBUGCOUTFNAME("StepNIntegrator::Update");
	ASSERT(pDM != NULL);
	
	UpdateLoop(this, &StepNIntegrator::UpdateDof, pSol);
	pDM->Update();
}


/* StepNIntegrator - end */


/* Step1Integrator - begin */

Step1Integrator::Step1Integrator(const integer MaxIt,
		const doublereal dT,
		const doublereal dSolutionTol,
		const bool bmod_res_test) 
: StepNIntegrator(MaxIt, dT, dSolutionTol, 1, bmod_res_test),
pXPrev(NULL),
pXPrimePrev(NULL)
{
	NO_OP;
}

Step1Integrator::~Step1Integrator(void)
{
	NO_OP;
}

/* predizione valida per tutti i metodi del second'ordine 
  a patto di utilizzare le giuste funzioni implementate per 
  ciascun metodo
  */

void Step1Integrator::PredictDof(const int DCount,
	const DofOrder::Order Order,
	const VectorHandler* const pSol) const {
	if (Order == DofOrder::DIFFERENTIAL) {
		doublereal dXnm1 = pXPrev->dGetCoef(DCount);
		doublereal dXPnm1 = 
			pXPrimePrev->dGetCoef(DCount);
		doublereal dXPn = dPredDer(dXnm1, dXPnm1);
		doublereal dXn = dPredState(dXnm1, dXPn, dXPnm1);
		pXPrimeCurr->PutCoef(DCount, dXPn);
		pXCurr->PutCoef(DCount, dXn);
	
	} else if (Order == DofOrder::ALGEBRAIC) {
		doublereal dXnm1 = pXPrev->dGetCoef(DCount);
		doublereal dXInm1 = 
			pXPrimePrev->dGetCoef(DCount);
				                                
		doublereal dXn = dPredDerAlg(dXInm1, dXnm1);
		doublereal dXIn = dPredStateAlg(dXInm1, dXn, dXnm1);

		pXCurr->PutCoef(DCount, dXn);
		pXPrimeCurr->PutCoef(DCount, dXIn);
	
	} else {
		silent_cerr("Step1Integrator::"
			"PredictDof(): "
			"unknown order for local dof " 
			<< DCount << std::endl);
		throw ErrGeneric();
	}
};


void
Step1Integrator::Predict(void) 
{
   	DEBUGCOUTFNAME("Step1Integrator::Predict");
   	ASSERT(pDM != NULL);
	UpdateLoop(this, &Step1Integrator::PredictDof);
}

doublereal
Step1Integrator::Advance(Solver* pS, 
		const doublereal TStep, 
		const doublereal dAph, const StepChange StType,
		std::deque<MyVectorHandler*>& qX,
	 	std::deque<MyVectorHandler*>& qXPrime,
		MyVectorHandler*const pX,
 		MyVectorHandler*const pXPrime,
		integer& EffIter,
		doublereal& Err,
		doublereal& SolErr)
{
	ASSERT(pDM != NULL);
	pXCurr  = pX;
	pXPrev  = qX[0];

	pXPrimeCurr  = pXPrime;
	pXPrimePrev  = qXPrime[0];

	SetCoef(TStep, dAph, StType);	
	/* predizione */
	Predict();
	pDM->LinkToSolution(*pXCurr, *pXPrimeCurr);
      	pDM->AfterPredict();
	
#ifdef DEBUG
      	integer iNumDofs = pDM->iGetNumDofs();
	if (outputPred) {
		std::cout << "Dof:      XCurr  ,    XPrev  "
			",   XPrime  ,   XPPrev" << std::endl;
		for (int iTmpCnt = 1; iTmpCnt <= iNumDofs; iTmpCnt++) {
    			std::cout << std::setw(4) << iTmpCnt << ": ";
			std::cout << std::setw(12) << pX->dGetCoef(iTmpCnt);
			for (unsigned int ivec = 0; ivec < qX.size(); ivec++) {  
				std::cout << std::setw(12)
				<< (qX[ivec])->dGetCoef(iTmpCnt);
			} 
			std::cout << std::setw(12) << pXPrime->dGetCoef(iTmpCnt);
			for (unsigned int ivec = 0; ivec < qXPrime.size(); ivec++) {  
				std::cout << std::setw(12)
				<< (qXPrime[ivec])->dGetCoef(iTmpCnt);
			} 
			std::cout << std::endl;
 		}
	}
#endif /* DEBUG */

	Err = 0.;
	pS->pGetNonlinearSolver()->Solve(this, pS, MaxIters, dTol,
    			EffIter, Err, dSolTol, SolErr);
	
	/* if it gets here, it surely converged */
	pDM->AfterConvergence();

	return Err;
}

/* Step1Integrator - end */


/* Step2Integrator - begin */

Step2Integrator::Step2Integrator(const integer MaxIt,
		const doublereal dT,
		const doublereal dSolutionTol,
		const bool bmod_res_test) 
: StepNIntegrator(MaxIt, dT, dSolutionTol, 2, bmod_res_test),
pXPrev(NULL),
pXPrev2(NULL),
pXPrimePrev(NULL),
pXPrimePrev2(NULL)
{
	NO_OP;
}

Step2Integrator::~Step2Integrator(void)
{
	NO_OP;
}

/* predizione valida per tutti i metodi del second'ordine 
  a patto di utilizzare le giuste funzioni implementate per 
  ciascun metodo
  */
void Step2Integrator::PredictDof(const int DCount,
	const DofOrder::Order Order,
	const VectorHandler* const pSol) const {
	if (Order == DofOrder::DIFFERENTIAL) {
		doublereal dXnm1 = pXPrev->dGetCoef(DCount);
		doublereal dXnm2 = pXPrev2->dGetCoef(DCount);
		doublereal dXPnm1 = 
			pXPrimePrev->dGetCoef(DCount);

		doublereal dXPnm2 = 
			pXPrimePrev2->dGetCoef(DCount);
		doublereal dXPn = dPredDer(dXnm1, dXnm2, 
				dXPnm1, dXPnm2);
		doublereal dXn = dPredState(dXnm1, dXnm2, 
				dXPn, dXPnm1, dXPnm2);

		pXPrimeCurr->PutCoef(DCount, dXPn);
		pXCurr->PutCoef(DCount, dXn);
		
	} else if (Order == DofOrder::ALGEBRAIC) {
		doublereal dXnm1 = pXPrev->dGetCoef(DCount);
		doublereal dXnm2 = pXPrev2->dGetCoef(DCount);
		doublereal dXInm1 = 
			pXPrimePrev->dGetCoef(DCount);
						                                
		doublereal dXn = dPredDerAlg(dXInm1, 
				dXnm1, dXnm2);
		doublereal dXIn = dPredStateAlg(dXInm1, 
				dXn, dXnm1, dXnm2);

		pXCurr->PutCoef(DCount, dXn);
		pXPrimeCurr->PutCoef(DCount, dXIn);
			
	} else {
		silent_cerr("Step2Integrator::"
			"PredictDof(): "
			"unknown order for local dof " 
			<< DCount << std::endl);
		throw ErrGeneric();
	}
}

void
Step2Integrator::Predict(void) 
{
   	DEBUGCOUTFNAME("Step2Integrator::Predict");
   	ASSERT(pDM != NULL);
	UpdateLoop(this, &Step2Integrator::PredictDof);
}

doublereal
Step2Integrator::Advance(Solver* pS, 
		const doublereal TStep, 
		const doublereal dAph, const StepChange StType,
		std::deque<MyVectorHandler*>& qX,
	 	std::deque<MyVectorHandler*>& qXPrime,
		MyVectorHandler*const pX,
		MyVectorHandler*const pXPrime,
		integer& EffIter,
		doublereal& Err,
		doublereal& SolErr)
{
	ASSERT(pDM != NULL);
	pXCurr  = pX;
	pXPrev  = qX[0];
	pXPrev2 = qX[1]; 

	pXPrimeCurr  = pXPrime;
	pXPrimePrev  = qXPrime[0];
	pXPrimePrev2 = qXPrime[1]; 

	SetCoef(TStep, dAph, StType);

	/* predizione */

	Predict();

	pDM->LinkToSolution(*pXCurr, *pXPrimeCurr);

      	pDM->AfterPredict();


#ifdef DEBUG
      		integer iNumDofs = pDM->iGetNumDofs();
		if (outputPred) {
			std::cout << "Dof:      XCurr  ,    XPrev  ,   XPrev2  "
				",   XPrime  ,   XPPrev  ,   XPPrev2" << std::endl;
			for (int iTmpCnt = 1; iTmpCnt <= iNumDofs; iTmpCnt++) {
	    			std::cout << std::setw(4) << iTmpCnt << ": ";
				std::cout << std::setw(12) << pX->dGetCoef(iTmpCnt);
				for (unsigned int ivec = 0; ivec < qX.size(); ivec++) {  
					std::cout << std::setw(12)
					<< (qX[ivec])->dGetCoef(iTmpCnt);
				} 
				std::cout << std::setw(12) << pXPrime->dGetCoef(iTmpCnt);
				for (unsigned int ivec = 0; ivec < qXPrime.size(); ivec++) {  
					std::cout << std::setw(12)
					<< (qXPrime[ivec])->dGetCoef(iTmpCnt);
				} 
				std::cout << std::endl;
	 		}
      		}
#endif /* DEBUG */

	Err = 0.;        
	pS->pGetNonlinearSolver()->Solve(this, pS, MaxIters, dTol,
    			EffIter, Err, dSolTol, SolErr);
	
	/* if it gets here, it surely converged */
	pDM->AfterConvergence();

	return Err;
}

/* Step2Integrator - end */


/* CrankNicholson - begin */

CrankNicholsonIntegrator::CrankNicholsonIntegrator(const doublereal dTl, 
		const doublereal dSolTl, 
		const integer iMaxIt,
		const bool bmod_res_test)
: Step1Integrator(iMaxIt, dTl, dSolTl, bmod_res_test)  
{
	NO_OP;
}

CrankNicholsonIntegrator::~CrankNicholsonIntegrator(void)
{
	NO_OP;
}

void
CrankNicholsonIntegrator::SetCoef(doublereal dT,
		doublereal dAlpha,
		enum StepChange /* NewStep */)
{
	db0Differential = db0Algebraic = dT*dAlpha/2.;
}


doublereal 
CrankNicholsonIntegrator::dPredictDerivative(const doublereal& /* dXm1 */,
		const doublereal& dXPm1,
		DofOrder::Order o) const
{
	return dXPm1;
}
   
doublereal 
CrankNicholsonIntegrator::dPredictState(const doublereal& dXm1,
		const doublereal& dXP,
		const doublereal& dXPm1,
		DofOrder::Order o) const
{
	if (o == DofOrder::ALGEBRAIC) {
		return db0Differential*(dXP+dXPm1);     
	} /* else if (o == DofOrder::DIFFERENTIAL) */   
	return dXm1+db0Differential*(dXP+dXPm1);
}
   
/* Nota: usa predizione lineare per le derivate (massimo ordine possibile) */
doublereal 
CrankNicholsonIntegrator::dPredDer(const doublereal& /* dXm1 */ ,
	      const doublereal& dXPm1) const
{
	return dXPm1;
}
   
doublereal 
CrankNicholsonIntegrator::dPredState(const doublereal& dXm1,
		const doublereal& dXP,
		const doublereal& dXPm1) const
{
	return dXm1+db0Differential*(dXP+dXPm1);
}
   
doublereal 
CrankNicholsonIntegrator::dPredDerAlg(const doublereal& /* dXm1 */ ,
		const doublereal& dXPm1) const
{
	return dXPm1;
}
   
doublereal 
CrankNicholsonIntegrator::dPredStateAlg(const doublereal& /* dXm1 */ ,
		const doublereal& dXP,
		const doublereal& dXPm1) const
{
	return db0Differential*(dXP+dXPm1);
}

/* CrankNicholson - end */
  
/* Implicit Euler - begin */

ImplicitEulerIntegrator::ImplicitEulerIntegrator(const doublereal dTl, 
		const doublereal dSolTl, 
		const integer iMaxIt,
		const bool bmod_res_test)
: Step1Integrator(iMaxIt, dTl, dSolTl, bmod_res_test)  
{
	NO_OP;
}

ImplicitEulerIntegrator::~ImplicitEulerIntegrator(void)
{
	NO_OP;
}

void
ImplicitEulerIntegrator::SetCoef(doublereal dT,
		doublereal dAlpha,
		enum StepChange /* NewStep */)
{
	db0Differential = db0Algebraic = dT*dAlpha;
}


doublereal 
ImplicitEulerIntegrator::dPredictDerivative(const doublereal& /* dXm1 */,
		const doublereal& dXPm1,
		DofOrder::Order o) const
{
	return dXPm1;
}
   
doublereal 
ImplicitEulerIntegrator::dPredictState(const doublereal& dXm1,
		const doublereal& dXP,
		const doublereal& dXPm1,
		DofOrder::Order o) const
{
	if (o == DofOrder::ALGEBRAIC) {
		return db0Differential*dXP;     
	} /* else if (o == DofOrder::DIFFERENTIAL) */   
	return dXm1+db0Differential*dXP;
}
   
/* Nota: usa predizione lineare per le derivate (massimo ordine possibile) */
doublereal 
ImplicitEulerIntegrator::dPredDer(const doublereal& /* dXm1 */ ,
	      const doublereal& dXPm1) const
{
	return dXPm1;
}
   
doublereal 
ImplicitEulerIntegrator::dPredState(const doublereal& dXm1,
		const doublereal& dXP,
		const doublereal& dXPm1) const
{
	return dXm1+db0Differential*dXP;
}
   
doublereal 
ImplicitEulerIntegrator::dPredDerAlg(const doublereal& /* dXm1 */ ,
		const doublereal& dXPm1) const
{
	return dXPm1;
}
   
doublereal 
ImplicitEulerIntegrator::dPredStateAlg(const doublereal& /* dXm1 */ ,
		const doublereal& dXP,
		const doublereal& dXPm1) const
{
	return db0Differential*dXP;
}

/* Implicit Euler - end */
  
/* NostroMetodo - begin */

MultistepSolver::MultistepSolver(const doublereal Tl, 
		const doublereal dSolTl, 
		const integer iMaxIt,
		const DriveCaller* pRho,
		const DriveCaller* pAlgRho,
		const bool bmod_res_test)
:Step2Integrator(iMaxIt, Tl, dSolTl, bmod_res_test), 
Rho(pRho), AlgebraicRho(pAlgRho)
{
	ASSERT(pRho != NULL);
	ASSERT(pAlgRho != NULL);
}

MultistepSolver::~MultistepSolver(void)
{
	NO_OP;
}

void
MultistepSolver::SetDriveHandler(const DriveHandler* pDH)
{
	Rho.pGetDriveCaller()->SetDrvHdl(pDH);
	AlgebraicRho.pGetDriveCaller()->SetDrvHdl(pDH);
}

void
MultistepSolver::SetCoef(doublereal dT,
		doublereal dAlpha,
		enum StepChange /* NewStep */)
{
	doublereal dRho = Rho.dGet();
	doublereal dAlgebraicRho = AlgebraicRho.dGet();
   
	doublereal dDen = 2.*(1.+dAlpha)-(1.-dRho)*(1.-dRho);
	doublereal dBeta = dAlpha*((1.-dRho)*(1.-dRho)*(2.+dAlpha)
		+2.*(2.*dRho-1.)*(1.+dAlpha))/dDen;
	doublereal dDelta = .5*dAlpha*dAlpha*(1.-dRho)*(1.-dRho)/dDen;

	mp[0] = -6.*dAlpha*(1.+dAlpha);
	mp[1] = -mp[0];
	np[0] = 1.+4.*dAlpha+3.*dAlpha*dAlpha;
	np[1] = dAlpha*(2.+3.*dAlpha);
   
	a[0][DIFFERENTIAL] = 1.-dBeta;
	a[1][DIFFERENTIAL] = dBeta;
	b[0][DIFFERENTIAL] = dT*(dDelta/dAlpha+dAlpha/2);
	b[1][DIFFERENTIAL] = dT*(dBeta/2.+dAlpha/2.-dDelta/dAlpha*(1.+dAlpha));
	b[2][DIFFERENTIAL] = dT*(dBeta/2.+dDelta);
   
	DEBUGCOUT("Predict()" << std::endl
			<< "Alpha = " << dAlpha << std::endl
			<< "Differential coefficients:" << std::endl
			<< "beta  = " << dBeta << std::endl
			<< "delta = " << dDelta << std::endl
			<< "a1    = " << a[0][DIFFERENTIAL] << std::endl
			<< "a2    = " << a[1][DIFFERENTIAL] << std::endl
			<< "b0    = " << b[0][DIFFERENTIAL] << std::endl
			<< "b1    = " << b[1][DIFFERENTIAL] << std::endl
			<< "b2    = " << b[2][DIFFERENTIAL] << std::endl);
   
	/* Coefficienti del metodo - variabili algebriche */
	if (dAlgebraicRho != dRho) {
		dDen = 2.*(1.+dAlpha)-(1.-dAlgebraicRho)*(1.-dAlgebraicRho);
		dBeta = dAlpha*((1.-dAlgebraicRho)*(1.-dAlgebraicRho)*(2.+dAlpha)
				+2.*(2.*dAlgebraicRho-1.)*(1.+dAlpha))/dDen;      
		dDelta = .5*dAlpha*dAlpha*(1.-dAlgebraicRho)*(1.-dAlgebraicRho)/dDen;

		a[1][ALGEBRAIC] = dBeta;
		b[0][ALGEBRAIC] = dT*(dDelta/dAlpha+dAlpha/2.);
		b[1][ALGEBRAIC] = dT*(dBeta/2.+dAlpha/2.-dDelta/dAlpha*(1.+dAlpha));
		b[2][ALGEBRAIC] = dT*(dBeta/2.+dDelta);

	} else {
		a[1][ALGEBRAIC] = a[1][DIFFERENTIAL];
		b[0][ALGEBRAIC] = b[0][DIFFERENTIAL];
		b[1][ALGEBRAIC] = b[1][DIFFERENTIAL];
		b[2][ALGEBRAIC] = b[2][DIFFERENTIAL];
	}

	DEBUGCOUT("Algebraic coefficients:" << std::endl
			<< "beta  = " << dBeta << std::endl
			<< "delta = " << dDelta << std::endl
			<< "a2    = " << a[1][ALGEBRAIC] << std::endl
			<< "b0    = " << b[0][ALGEBRAIC] << std::endl
			<< "b1    = " << b[1][ALGEBRAIC] << std::endl
			<< "b2    = " << b[2][ALGEBRAIC] << std::endl);
      
	DEBUGCOUT("Asymptotic rho: " 
			<< -b[1][DIFFERENTIAL]/(2.*b[0][DIFFERENTIAL]) << std::endl
			<< "Discriminant: " 
			<< b[1][DIFFERENTIAL]*b[1][DIFFERENTIAL]-4.*b[2][DIFFERENTIAL]*b[0][DIFFERENTIAL] 
			<< std::endl
			<< "Asymptotic rho for algebraic variables: " 
			<< -b[1][ALGEBRAIC]/(2.*b[0][ALGEBRAIC]) << std::endl
			<< "Discriminant: " 
			<< b[1][ALGEBRAIC]*b[1][ALGEBRAIC]-4.*b[2][ALGEBRAIC]*b[0][ALGEBRAIC] 
			<< std::endl);

	/* Vengono modificati per la predizione, dopo che sono stati usati per
	 * costruire gli altri coefficienti */
	mp[0] /= dT;
	mp[1] /= dT;
   
	/* valori di ritorno */
	db0Differential = b[0][DIFFERENTIAL];
	db0Algebraic = b[0][ALGEBRAIC];
}


doublereal
MultistepSolver::dPredictDerivative(const doublereal& dXm1,
		const doublereal& dXm2,
		const doublereal& dXPm1,
		const doublereal& dXPm2,
		DofOrder::Order o) const
{
	if (o == DofOrder::ALGEBRAIC) {
		return np[0]*dXPm1+np[1]*dXPm2-mp[1]*dXm1;
	} /* else if (o == DofOrder::DIFFERENTIAL) */   
	return mp[0]*dXm1+mp[1]*dXm2+np[0]*dXPm1+np[1]*dXPm2;
}

doublereal
MultistepSolver::dPredictState(const doublereal& dXm1,
		const doublereal& dXm2,
		const doublereal& dXP,
		const doublereal& dXPm1,
		const doublereal& dXPm2,
		DofOrder::Order o) const
{
	if (o == DofOrder::ALGEBRAIC) {
		return b[0][ALGEBRAIC]*dXP+b[1][ALGEBRAIC]*dXPm1+b[2][ALGEBRAIC]*dXPm2
			-a[1][ALGEBRAIC]*dXm1;
	} /* else if (o == DofOrder::DIFFERENTIAL) */   
	return a[0][DIFFERENTIAL]*dXm1+a[1][DIFFERENTIAL]*dXm2
		+b[0][DIFFERENTIAL]*dXP+b[1][DIFFERENTIAL]*dXPm1+b[2][DIFFERENTIAL]*dXPm2;
}


/* Nota: usa predizione cubica per le derivate (massimo ordine possibile) */
doublereal 
MultistepSolver::dPredDer(const doublereal& dXm1,
		const doublereal& dXm2,
		const doublereal& dXPm1,
		const doublereal& dXPm2) const
{
	return mp[0]*dXm1+mp[1]*dXm2+np[0]*dXPm1+np[1]*dXPm2;
}
   
doublereal 
MultistepSolver::dPredState(const doublereal& dXm1,
		const doublereal& dXm2,
		const doublereal& dXP,
		const doublereal& dXPm1,
		const doublereal& dXPm2) const
{
	return a[0][DIFFERENTIAL]*dXm1+a[1][DIFFERENTIAL]*dXm2
		+b[0][DIFFERENTIAL]*dXP+b[1][DIFFERENTIAL]*dXPm1+b[2][DIFFERENTIAL]*dXPm2;
}

doublereal 
MultistepSolver::dPredDerAlg(const doublereal& dXm1,
		const doublereal& dXPm1,
		const doublereal& dXPm2) const
{
	return np[0]*dXPm1+np[1]*dXPm2-mp[1]*dXm1;
}

doublereal 
MultistepSolver::dPredStateAlg(const doublereal& dXm1,
		const doublereal& dXP,
		const doublereal& dXPm1,
		const doublereal& dXPm2) const
{
	return b[0][ALGEBRAIC]*dXP+b[1][ALGEBRAIC]*dXPm1+b[2][ALGEBRAIC]*dXPm2
		-a[1][ALGEBRAIC]*dXm1;
}

/* NostroMetodo - end */


/* Hope - begin */

HopeSolver::HopeSolver(const doublereal Tl, 
		const doublereal dSolTl, 
		const integer iMaxIt,
		const DriveCaller* pRho,
		const DriveCaller* pAlgRho,
		const bool bmod_res_test)
:Step2Integrator(iMaxIt, Tl, dSolTl, bmod_res_test), 
Rho(pRho), AlgebraicRho(pAlgRho), bStep(0)
{
	ASSERT(pRho != NULL);
	ASSERT(pAlgRho != NULL);
}

HopeSolver::~HopeSolver(void)
{
	NO_OP; 
}

void
HopeSolver::SetDriveHandler(const DriveHandler* pDH)
{
	Rho.pGetDriveCaller()->SetDrvHdl(pDH);
	AlgebraicRho.pGetDriveCaller()->SetDrvHdl(pDH);
}

void
HopeSolver::SetCoef(doublereal dT,
		doublereal dAlpha,
		enum StepChange NewStep)
{
#if 0
	if (dAlpha != 1.) {
		cerr << "HOPE time step integrator is not implemented yet in variable step form" << std::endl;
		throw ErrNotImplementedYet();
	}
#endif
 
	if (NewStep == NEWSTEP) {
		ASSERT(bStep == flag(0) || bStep == flag(1));
		bStep = 1-bStep;	/* Commuta il valore di bStep */
	}

	doublereal dTMod = dT*dAlpha;

	/* Differential coefficients */
	mp[0] = -6.*dAlpha*(1.+dAlpha);
	mp[1] = -mp[0];
	np[0] = 1.+4.*dAlpha+3.*dAlpha*dAlpha;
	np[1] = dAlpha*(2.+3.*dAlpha);
      
	if (bStep) {
		b[0][DIFFERENTIAL] = b[1][DIFFERENTIAL] 
			= b[0][ALGEBRAIC] = b[1][ALGEBRAIC] 
			= db0Algebraic = db0Differential = dTMod/2.;// dT/4.;

	} else {
		doublereal dRho = Rho.dGet();
		doublereal dALPHA = 4.*dRho/(3.+dRho);

		a[0][DIFFERENTIAL] = (4.-dALPHA)/3.;
		a[1][DIFFERENTIAL] = (dALPHA-1.)/3.;
		b[0][DIFFERENTIAL] = dTMod*(4.-dALPHA)/6.;// dT*(4.-dALPHA)/12.;
		b[1][DIFFERENTIAL] = dTMod*dALPHA/2.;// dT*dALPHA/4.;

		DEBUGCOUT("Predict()" << std::endl
				<< "Alpha = " << dAlpha << std::endl
				<< "Differential coefficients:" << std::endl
				<< "HOPE-Alpha = " << dALPHA << std::endl
				<< "a1    = " << a[0][DIFFERENTIAL] << std::endl
				<< "a2    = " << a[1][DIFFERENTIAL] << std::endl
				<< "b0    = " << b[0][DIFFERENTIAL] << std::endl
				<< "b1    = " << b[1][DIFFERENTIAL] << std::endl);

		/* Coefficienti del metodo - variabili algebriche */
		doublereal dAlgebraicRho = AlgebraicRho.dGet();   
		doublereal dAlgebraicALPHA = 4.*dAlgebraicRho/(3.+dAlgebraicRho);     

		if (dAlgebraicRho != dRho) {                 
			a[1][ALGEBRAIC] = (dAlgebraicALPHA-1.)/3.;
			b[0][ALGEBRAIC] = dTMod*(4.-dAlgebraicALPHA)/6.; // dT*(4.-dAlgebraicALPHA)/12.;
			b[1][ALGEBRAIC] = dTMod*dAlgebraicALPHA/2.; // dT*dAlgebraicALPHA/4.;

		} else {
			a[1][ALGEBRAIC] = a[1][DIFFERENTIAL];
			b[0][ALGEBRAIC] = b[0][DIFFERENTIAL];
			b[1][ALGEBRAIC] = b[1][DIFFERENTIAL];   
		}

		DEBUGCOUT("Algebraic coefficients:" << std::endl
				<< "HOPE-Alpha = " << dAlgebraicALPHA << std::endl
				<< "a2    = " << a[1][ALGEBRAIC] << std::endl
				<< "b0    = " << b[0][ALGEBRAIC] << std::endl
				<< "b1    = " << b[1][ALGEBRAIC] << std::endl);

		/* valori di ritorno */     
		db0Differential = b[0][DIFFERENTIAL];
		db0Algebraic = b[0][ALGEBRAIC];
	}
   
	/* Vengono modificati per la predizione, dopo che sono stati usati per
	 * costruire gli altri coefficienti */
	mp[0] /= dT;
	mp[1] /= dT;
}

doublereal
HopeSolver::dPredictDerivative(const doublereal& dXm1,
		const doublereal& dXm2,
		const doublereal& dXPm1,
		const doublereal& dXPm2,
		DofOrder::Order o) const
{
	if (o == DofOrder::ALGEBRAIC) {
		return np[0]*dXPm1+np[1]*dXPm2-mp[1]*dXm1;
	} /* else if (o == DofOrder::DIFFERENTIAL) */   
	return mp[0]*dXm1+mp[1]*dXm2+np[0]*dXPm1+np[1]*dXPm2;
}

doublereal
HopeSolver::dPredictState(const doublereal& dXm1,
		const doublereal& dXm2,
		const doublereal& dXP,
		const doublereal& dXPm1,
		const doublereal& /* dXPm2 */ ,
		DofOrder::Order o) const
{
	if (bStep) {
		if (o == DofOrder::ALGEBRAIC) {
			return b[0][ALGEBRAIC]*(dXP+dXPm1);
		} /* else if (o == DofOrder::DIFFERENTIAL) */
		return dXm1+b[0][ALGEBRAIC]*(dXP+dXPm1);

	} else {
		if (o == DofOrder::ALGEBRAIC) {
			return b[0][ALGEBRAIC]*dXP+b[1][ALGEBRAIC]*dXPm1
				-a[1][ALGEBRAIC]*dXm1;
		} /* else if (o == DofOrder::DIFFERENTIAL) */
		return a[0][DIFFERENTIAL]*dXm1+a[1][DIFFERENTIAL]*dXm2
			+b[0][DIFFERENTIAL]*dXP+b[1][DIFFERENTIAL]*dXPm1;
	}
}

/* Nota: usa predizione cubica per le derivate (massimo ordine possibile) */
doublereal 
HopeSolver::dPredDer(const doublereal& dXm1,
		const doublereal& dXm2,
		const doublereal& dXPm1,
		const doublereal& dXPm2) const
{
	return mp[0]*dXm1+mp[1]*dXm2+np[0]*dXPm1+np[1]*dXPm2;
}

doublereal 
HopeSolver::dPredState(const doublereal& dXm1,
			const doublereal& dXm2,
			const doublereal& dXP,
			const doublereal& dXPm1,
			const doublereal& /* dXPm2 */ ) const
{
	if (bStep) {
		return dXm1+b[0][ALGEBRAIC]*(dXP+dXPm1);

	} else {
		return a[0][DIFFERENTIAL]*dXm1+a[1][DIFFERENTIAL]*dXm2
			+b[0][DIFFERENTIAL]*dXP+b[1][DIFFERENTIAL]*dXPm1;
	}
}

doublereal 
HopeSolver::dPredDerAlg(const doublereal& dXm1,
			const doublereal& dXPm1,
			const doublereal& dXPm2) const
{
	return np[0]*dXPm1+np[1]*dXPm2-mp[1]*dXm1;
}

doublereal 
HopeSolver::dPredStateAlg(const doublereal& dXm1,
		const doublereal& dXP,
		const doublereal& dXPm1,
		const doublereal& /* dXPm2 */ ) const
{
	if (bStep) {
		return b[0][ALGEBRAIC]*(dXP+dXPm1);
	} else {
		return b[0][ALGEBRAIC]*dXP+b[1][ALGEBRAIC]*dXPm1
			-a[1][ALGEBRAIC]*dXm1;
	}
}

/* Hope - end */


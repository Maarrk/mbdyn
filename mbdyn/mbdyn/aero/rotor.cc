/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2006
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

/* Rotore */

#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#include <ac/math.h>

#ifdef USE_MPI
#include <mysleep.h>
const int mysleeptime = 300;

#ifdef MPI_PROFILING
extern "C" {
#include <mpe.h>
#include <stdio.h>
}
#endif /* MPI_PROFILING */
#include <mbcomm.h>
#endif /* USE_MPI */

#include <rotor.h>
#include <dataman.h>

/* Rotor - begin */

Rotor::Rotor(unsigned int uL, const DofOwner* pDO,
	     const StructNode* pC, const Mat3x3& rrot,
	     const StructNode* pR, const StructNode* pG, 
	     unsigned int iMaxIt, doublereal dTol, doublereal dE,
	     ResForceSet **ppres, flag fOut)
: Elem(uL, Elem::ROTOR, fOut), 
AerodynamicElem(uL, AerodynamicElem::ROTOR, fOut), 
ElemWithDofs(uL, Elem::ROTOR, pDO, fOut),
#ifdef USE_MPI
is_parallel(false),
pBlockLenght(NULL),
pDispl(NULL),
ReqV(MPI::REQUEST_NULL),
pRotDataType(NULL),
#endif /* USE_MPI */
pCraft(pC), pRotor(pR), pGround(pG),
dOmegaRef(0.), dRadius(0.), dArea(0.),
dUMean(0.), dUMeanRef(0.), dUMeanPrev(0.),
iMaxIter(iMaxIt),
iCurrIter(0),
dTolerance(dTol),
dEta(dE),
bUMeanRefConverged(false),
Weight(), dWeight(0.),
dHoverCorrection(1.), dForwardFlightCorrection(1.),
ppRes(ppres),
RRotTranspose(0.), RRot(rrot), RRot3(0.), 
VCraft(0.),
dPsi0(0.), dSinAlphad(1.), dCosAlphad(0.),
dMu(0.), dLambda(1.), dChi(0.),
dVelocity(0.), dOmega(0.),
iNumSteps(0)
{
	ASSERT(pC != NULL);
	ASSERT(pC->GetNodeType() == Node::STRUCTURAL);
	ASSERT(pR != NULL);
	ASSERT(pR->GetNodeType() == Node::STRUCTURAL);
	ASSERT(pG == NULL || pG->GetNodeType() == Node::STRUCTURAL);
      
	Vec3 R3C((pCraft->GetRCurr()*RRot).GetVec(3));
	Vec3 R3R((pRotor->GetRCurr()).GetVec(3));
	if (R3C.Dot(R3R) < 1. - DBL_EPSILON) {
		silent_cerr("warning, possible misalignment " << std::endl
			<< "of rotor StructNode(" << pRotor->GetLabel() << ") "
			"axis {" << R3R << "}" << std::endl
			<< "and craft StructNode(" << pCraft->GetLabel() << ") "
			"axis {" << R3C << "}" 
			<< "for Rotor(" << GetLabel() << ")"
			<< std::endl);
	}

#ifdef USE_MPI
	iForcesVecDim = 6;
	for (int i = 0; ppRes && ppRes[i]; i++) {
		iForcesVecDim += 6;
	}	        
	SAFENEWARR(pTmpVecR, doublereal, iForcesVecDim);
	SAFENEWARR(pTmpVecS, doublereal, iForcesVecDim);
	if (MPI::Is_initialized()) {
		is_parallel = true;
   		RotorComm = MBDynComm.Dup();
	}
#endif /* USE_MPI */

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_init(&induced_velocity_mutex, NULL);
	pthread_cond_init(&induced_velocity_cond, NULL);
	pthread_mutex_init(&forces_mutex, NULL);
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */
}

Rotor::~Rotor(void)
{
#ifdef USE_MPI
	SAFEDELETEARR(pTmpVecR);
	SAFEDELETEARR(pTmpVecS);
#endif /* USE_MPI */

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_destroy(&induced_velocity_mutex);
	pthread_cond_destroy(&induced_velocity_cond);
	pthread_mutex_destroy(&forces_mutex);
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */
}

unsigned int
Rotor::iGetNumPrivData(void) const {
	return 6;
}
   
unsigned int
Rotor::iGetPrivDataIdx(const char *s) const
{
	ASSERT(s != NULL);

	unsigned int idx = 0;

	switch (s[0]) {
	case 'M':
		idx += 3;
	case 'T':
		switch (s[1]) {
		case 'x':
			return idx + 1;

		case 'y':
			return idx + 2;

		case 'z':
			return idx + 3;
		}
	}

	return 0;
}

doublereal
Rotor::dGetPrivData(unsigned int i) const
{
	ASSERT(i > 0 && i <= 6);

	switch (i) {
	case 1:
	case 2:
	case 3:
		return Res.Force().dGet(i);

	case 4:
	case 5:
	case 6:
		return Res.Couple().dGet(i - 3);
	}

	throw ErrGeneric();
}
 
/* Tipo dell'elemento (usato per debug ecc.) */
Elem::Type Rotor::GetElemType(void) const
{
	return Elem::ROTOR;
}

void 
Rotor::AfterConvergence(const VectorHandler& /* X */ , 
		const VectorHandler& /* XP */ )
{
    /* non mi ricordo a cosa serve! */
    iNumSteps++;

    /* updates the umean at the previous step */
    dUMeanPrev = dUMean; 

    /*
     * FIXME: should go in AfterPredict ...
     * this way there's a one step delay in the weight value
     */
    if (Weight.pGetDriveCaller() != NULL) {
	    dWeight = Weight.dGet();
	    if (dWeight < 0.) {
		    silent_cout("Rotor(" << GetLabel()
				    << "): delay < 0.0; using 0.0"
				    << std::endl);
     		    dWeight = 0.;	    
	    } else if (dWeight > 1.) {
		    silent_cout("Rotor(" << GetLabel()
				    << "): delay > 1.0; using 1.0"
				    << std::endl);
     		    dWeight = 1.;
	    }
    }

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
    ASSERT(bDone);
    bDone = false;
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */
}

void
Rotor::Output(OutputHandler& OH) const
{
    	if (fToBeOutput()) {
#ifdef USE_MPI
		if (is_parallel && RotorComm.Get_size() > 1) { 
	    		if (RotorComm.Get_rank() == 0) {
				Vec3 TmpF(pTmpVecR);
				Vec3 TmpM(pTmpVecR+3);

				OH.Rotors() 
					<< std::setw(8) << GetLabel()	/* 1 */
					<< " " << RRotTranspose*TmpF /* 2-4 */
					<< " " << RRotTranspose*TmpM /* 5-7 */
					<< " " << dUMean 	/* 8 */
					<< " " << dVelocity	/* 9 */
					<< " " << atan2(dSinAlphad, dCosAlphad)	/* 10 */
					<< " " << dMu		/* 11 */
					<< " " << dLambda	/* 12 */
					<< " " << dChi		/* 13 */
					<< " " << dPsi0		/* 14 */
					<< " " << bUMeanRefConverged /* 15 */
					<< " " << iCurrIter	/* 16 */
					<< std::endl;

				for (int i = 0; ppRes && ppRes[i]; i++) {
					Vec3 TmpF(pTmpVecR+6+6*i);
					Vec3 TmpM(pTmpVecR+9+6*i);

					OH.Rotors()
						<< std::setw(8) << GetLabel() 
						<< ":" << ppRes[i]->GetLabel()
						<< " " << TmpF
						<< " " << TmpM
						<< std::endl;
				}
	    		}
		} else {
	    		OH.Rotors()
				<< std::setw(8) << GetLabel()	/* 1 */
	    			<< " " << RRotTranspose*Res.Force()  /* 2-4 */
				<< " " << RRotTranspose*Res.Couple() /* 5-7 */
	    			<< " " << dUMean		/* 8 */
	    			<< " " << dVelocity		/* 9 */
	    			<< " " << atan2(dSinAlphad, dCosAlphad)	/* 10 */
	    			<< " " << dMu			/* 11 */
	    			<< " " << dLambda		/* 12 */
				<< " " << dChi			/* 13 */
	    			<< " " << dPsi0			/* 14 */
				<< " " << bUMeanRefConverged	/* 15 */
				<< " " << iCurrIter		/* 16 */
	    			<< std::endl;

	    		for (int i = 0; ppRes && ppRes[i]; i++) {
				OH.Rotors()
					<< std::setw(8) << GetLabel() 
	    				<< ":" << ppRes[i]->GetLabel()
	    				<< " " << ppRes[i]->pRes->Force()
	    				<< " " << ppRes[i]->pRes->Couple()
	    				<< std::endl;
	    		}
		}

#else /* !USE_MPI */     
		OH.Rotors()
			<< std::setw(8) << GetLabel()	/* 1 */
			<< " " << RRotTranspose*Res.Force()	/* 2-4 */
			<< " " << RRotTranspose*Res.Couple()	/* 5-7 */
			<< " " << dUMean		/* 8 */
			<< " " << dVelocity		/* 9 */
			<< " " << atan2(dSinAlphad, dCosAlphad)	/* 10 */
			<< " " << dMu			/* 11 */
			<< " " << dLambda		/* 12 */
			<< " " << dChi			/* 13 */
			<< " " << dPsi0			/* 14 */
			<< " " << bUMeanRefConverged	/* 15 */
			<< " " << iCurrIter		/* 16 */
			<< std::endl;

		/* FIXME: check for parallel stuff ... */
		for (int i = 0; ppRes && ppRes[i]; i++) {
			OH.Rotors()
				<< std::setw(8) << GetLabel() 
				<< ":" << ppRes[i]->GetLabel()
				<< " " << ppRes[i]->pRes->Force()
				<< " " << ppRes[i]->pRes->Couple()
				<< std::endl;
		}
#endif /* !USE_MPI */
    	}
}

/* Calcola la posizione azimuthale di un punto generico.
 * X e' il punto di cui e' chiesta la posizione azimuthale, 
 *   nel sistema assoluto.
 * Gli viene sottratta la posizione del rotore (corpo attorno al quale avviene
 * la rotazione). Quindi il vettore risultante viene trasformato
 * nel riferimento del mozzo. 
 * Si trascura l'eventuale componente fuori del piano, dalle altre due si 
 * ricava l'angolo di rotazione relativa, a cui viene sommato l'angolo
 * che il corpo rotore forma con la direzione del vento relativo dPsi0, 
 * calcolata in precedenza.
 */
doublereal
Rotor::dGetPsi(const Vec3& X) const
{
	doublereal dr, dp;
	GetPos(X, dr, dp);
	return dp;
}

/* Calcola la distanza di un punto dall'asse di rotazione in coordinate 
 * adimensionali */
doublereal
Rotor::dGetPos(const Vec3& X) const
{
	doublereal dr, dp;
	GetPos(X, dr, dp);
	return dr;
}

void
Rotor::GetPos(const Vec3& X, doublereal& dr, doublereal& dp) const
{
	Vec3 XRel(RRotTranspose*(X-Res.Pole()));

	doublereal d1 = XRel.dGet(1);
	doublereal d2 = XRel.dGet(2);

	doublereal d = sqrt(d1*d1+d2*d2);

	ASSERT(dRadius > DBL_EPSILON);
	dr = d/dRadius;   

	dp = atan2(d2, d1) - dPsi0;
}

/* Calcola vari parametri geometrici
 * A partire dai corpi che identificano il velivolo ed il rotore
 */
void Rotor::InitParam(bool bComputeMeanInducedVelocity)
{
   ASSERT(pCraft != NULL);
   ASSERT(pRotor != NULL);
   
   /* Trasposta della matrice di rotazione del rotore */
   RRotTranspose = pCraft->GetRCurr()*RRot;
   RRot3 = RRotTranspose.GetVec(3);
   RRotTranspose = RRotTranspose.Transpose();
   
   /* Posizione del rotore */
   Res.PutPole(pRotor->GetXCurr());

   /* Velocita' angolare del rotore */
   dOmega = (pRotor->GetWCurr()-pCraft->GetWCurr()).Norm();
   
   /* Velocita' di traslazione del velivolo */
   VCraft = -pRotor->GetVCurr();
   Vec3 VTmp(0.);
   if (fGetAirVelocity(VTmp, pRotor->GetXCurr())) {
      VCraft += VTmp;
   }
  
   /* Velocita' nel sistema del velivolo (del disco?) decomposta */
   VTmp = RRotTranspose*VCraft;
   doublereal dV1 = VTmp.dGet(1);
   doublereal dV2 = VTmp.dGet(2);
   doublereal dV3 = VTmp.dGet(3);
   doublereal dVV = dV1*dV1+dV2*dV2;
   doublereal dV = sqrt(dVV);
       
   /* Angolo di azimuth 0 del rotore */
   dPsi0 = atan2(dV2, dV1);
   
   /* Angolo di influsso */
   dVelocity = sqrt(dV3*dV3+dVV);
   if (dVelocity > DBL_EPSILON) {
      dSinAlphad = -dV3/dVelocity;
      dCosAlphad = dV/dVelocity;
   } else {
      dSinAlphad = 1.;
      dCosAlphad = 0.;
   }

   if (!bComputeMeanInducedVelocity) {
      return;
   }

   /* Trazione nel sistema rotore */
   doublereal dT = RRot3*Res.Force();
   doublereal dRho = dGetAirDensity(GetXCurr());

   /* Parametri di influsso (usano il valore di dUMean al passo precedente) */
   doublereal dVTip = 0.;
   dMu = 0.;
   dLambda = 0.;
   dVTip = dOmega*dRadius;

   if (dVTip > DBL_EPSILON) {
      dMu = (dVelocity*dCosAlphad)/dVTip;
   }
   
   /* NOTE: dUMeanRef starts at the value it had previously */

   /* Ground effect */
   doublereal dGE = 1.;
   if (pGround) {
	   Vec3 p = pGround->GetRCurr().Transpose()*(pRotor->GetXCurr() - pGround->GetXCurr());
	   doublereal z = p.dGet(3)*(4./dRadius);

	   if (z < .25) {
		   if (z <= 0.) {
			   silent_cerr("warning, illegal negative "
				   "normalized altitude "
				   "z=" << z << std::endl);
		   }

		   z = .25;
	   }

	   /*
	    * According to I.C. Cheeseman & N.E. Bennett,
	    * "The Effect of Ground on a Helicopter Rotor
	    * in Forward Flight", NASA TR-3021, 1955:
	    *
	    * U = Uref * ( 1 - ( R / ( 4 * z ) )^2 )
	    * 
	    * We need to make R / ( 4 * z ) <= 1, so
	    * we must enforce z >= R / 4.
	    */
	   dGE -= 1./(z*z);
   }

   bUMeanRefConverged = false;
   if (dVTip > DBL_EPSILON) {
      for (iCurrIter = 0; iCurrIter < iMaxIter; iCurrIter++ ) {
         doublereal dUMeanRefOrig = dUMeanRef;
      
         dLambda = (dVelocity*dSinAlphad+dUMeanRef)/dVTip;
   
         /* Velocita' indotta media */
         doublereal dVRef = dOmega*dRadius*sqrt(dMu*dMu+dLambda*dLambda);
         doublereal dRef = 2.*dRho*dArea*dVRef;

         doublereal dDelta = dGE*dT/(dRef+1.) - dUMeanRefOrig;
         dUMeanRef = dEta*dDelta + dUMeanRefOrig;

         if (fabs(dDelta) <= dTolerance) {
	    bUMeanRefConverged = true;
	    break;
	 }
      }

      /* if no convergence, simply accept the current value
       * very forgiving choice, though */
#if 0
      if (iCurrIter == iMaxIter) {
	 silent_cerr("unable to compute mean induced velocity for Rotor(" 
		 << GetLabel() << ")" << std::endl);
	 throw ErrGeneric();
      }
#endif
   }

   if (dMu == 0. && dLambda == 0.) {
      dChi = 0.;
   } else {      
      dChi = atan2(dMu, dLambda);
   }

   /*
    * From Claudio Monteggia:
    *
    *                        Ct
    * Um = -------------------------------------
    *       sqrt( lambda^2 / KH^4 + mu^2 / KF^2)
    */
   doublereal dMuTmp = dMu/dForwardFlightCorrection;
   doublereal dLambdaTmp = dLambda/(dHoverCorrection*dHoverCorrection);
   doublereal dVRef = dOmega*dRadius*sqrt(dMuTmp*dMuTmp+dLambdaTmp*dLambdaTmp);
   doublereal d = 2.*dRho*dArea*dVRef;

   dUMean = (1.-dWeight)*dT/(d+1.)+dWeight*dUMeanPrev;
}

/* assemblaggio jacobiano (nullo per tutti tranne che per il DynamicInflow) */
VariableSubMatrixHandler& Rotor::AssJac(VariableSubMatrixHandler& WorkMat,
					doublereal /* dCoef */ ,
					const VectorHandler& /* XCurr */ ,
					const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering Rotor::AssJac()" << std::endl);
   WorkMat.SetNullMatrix();

   return WorkMat;	
}

std::ostream& Rotor::Restart(std::ostream& out) const
{
   return out << "  rotor: " << GetLabel() << ", " 
     << pCraft->GetLabel() << ", " << pRotor->GetLabel() 
     << ", induced velocity: ";
}

#ifdef USE_MPI
void Rotor::ExchangeTraction(flag fWhat)
{
#ifdef MPI_PROFILING
  MPE_Log_event(33, 0, "start RotorTrust Exchange ");
#endif /* MPI_PROFILING */
  /* Se il rotore � connesso ad una sola macchina non � necessario scambiare messaggi */
  if (is_parallel && RotorComm.Get_size() > 1){
    if (fWhat) {
      /* Scambia F e M */
      Res.Force().PutTo(pTmpVecS);
      Res.Couple().PutTo(pTmpVecS+3);
      for (int i = 0; ppRes && ppRes[i]; i++) {
	ppRes[i]->pRes->Force().PutTo(pTmpVecS+6+6*i);
	ppRes[i]->pRes->Couple().PutTo(pTmpVecS+9+6*i);
      }
      RotorComm.Allreduce(pTmpVecS, pTmpVecR, iForcesVecDim, MPI::DOUBLE, MPI::SUM);
      Res.PutForces(Vec3(pTmpVecR), Vec3(pTmpVecR+3));
      for (int i = 0; ppRes && ppRes[i]; i++) {
	ppRes[i]->pRes->PutForces(Vec3(pTmpVecR+6+6*i),  Vec3(pTmpVecR+9+6*i));
      }
    } else {
      RotorComm.Allreduce(Res.Force().pGetVec(), pTmpVecR, 3, 
		      MPI::DOUBLE, MPI::SUM);
      Res.PutForce(Vec3(pTmpVecR));
    }
  }
#ifdef MPI_PROFILING
  MPE_Log_event(34, 0, "end RotorTrust Exchange ");
#endif /* MPI_PROFILING */
}

void Rotor::InitializeRotorComm(MPI::Intracomm* Rot)
{
	ASSERT(is_parallel);
  	RotorComm = *Rot;
} 

void Rotor::ExchangeVelocity(void) 
{
#define ROTDATATYPELABEL	100
	if (is_parallel && RotorComm.Get_size() > 1){
		if (RotorComm.Get_rank() == 0) {
			for (int i = 1; i < RotorComm.Get_size(); i++) {
				RotorComm.Send(MPI::BOTTOM, 1, *pRotDataType,
						i, ROTDATATYPELABEL);
			}
		} else {
			ReqV = RotorComm.Irecv((void *)MPI::BOTTOM, 1,
					*pRotDataType, 0, ROTDATATYPELABEL);
		}
	}
}
#endif /* USE_MPI */
   
/* Somma alla trazione il contributo di forza di un elemento generico */
void 
Rotor::AddForce(unsigned int uL, 
		const Vec3& F, const Vec3& M, const Vec3& X)
{
	for (int i = 0; ppRes && ppRes[i]; i++) {
		if (ppRes[i]->is_in(uL)) {
			ppRes[i]->pRes->AddForces(F, M, X);
		}
	}
}

void 
Rotor::ResetForce(void)
{
	Res.Reset(pRotor->GetXCurr());
	for (int i = 0; ppRes && ppRes[i]; i++) {
		ppRes[i]->pRes->Reset();
	}
}

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
void
Rotor::Wait(void) const
{
	pthread_mutex_lock(&induced_velocity_mutex);
	if (!bDone) {
		pthread_cond_wait(&induced_velocity_cond,
				&induced_velocity_mutex);
	}
	pthread_mutex_unlock(&induced_velocity_mutex);
}

void
Rotor::Done(void) const
{
	pthread_mutex_lock(&induced_velocity_mutex);
	ASSERT(!bDone);
	bDone = true;
	pthread_cond_broadcast(&induced_velocity_cond);
	pthread_mutex_unlock(&induced_velocity_mutex);
}
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

/* Rotor - end */


/* NoRotor - begin */

NoRotor::NoRotor(unsigned int uLabel,
		 const DofOwner* pDO,
		 const StructNode* pCraft, 
		 const Mat3x3& rrot,
		 const StructNode* pRotor,
		 ResForceSet **ppres, 
		 doublereal dR,
		 flag fOut)
: Elem(uLabel, Elem::ROTOR, fOut), 
Rotor(uLabel, pDO, pCraft, rrot, pRotor, NULL, 0, 0., 0., ppres, fOut)
{
	dRadius = dR; /* puo' essere richiesto dal trim */
#ifdef USE_MPI
	if (is_parallel && fToBeOutput()) {
		SAFENEWARR(pBlockLenght, int, 3);
		SAFENEWARR(pDispl, MPI::Aint, 3);
		for (int i = 0; i < 3; i++) {
			pBlockLenght[i] = 1;
		}
		for (int i = 0; i < 3; i++) {	
			pDispl[i] = MPI::Get_address(&(Res.Pole().pGetVec()[i]));
		}
		SAFENEWWITHCONSTRUCTOR(pRotDataType, MPI::Datatype,
				MPI::Datatype(MPI::DOUBLE.Create_hindexed(3, pBlockLenght, pDispl)));
		pRotDataType->Commit();
	}
#endif /* USE_MPI */
}

NoRotor::~NoRotor(void)
{
#ifdef USE_MPI
	SAFEDELETEARR(pBlockLenght);
	SAFEDELETEARR(pDispl);
	SAFEDELETE(pRotDataType);
#endif /* USE_MPI */
}
   
/* assemblaggio residuo */
SubVectorHandler& NoRotor::AssRes(SubVectorHandler& WorkVec,
				  doublereal /* dCoef */ ,
				  const VectorHandler& /* XCurr */ ,
				  const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering NoRotor::AssRes()" << std::endl);

   flag out = fToBeOutput();
  
#ifdef USE_MPI
   if (out) {
      ExchangeTraction(out);
   }
   if (!is_parallel || RotorComm.Get_rank() == 0) {
#endif /* USE_MPI */   

      if (out) { 
         /* Calcola parametri vari */
         Rotor::InitParam(false);
    
#ifdef USE_MPI 
         ExchangeVelocity();
#endif /* USE_MPI */
      }

#ifdef USE_MPI 
   }
#endif /* USE_MPI */
   ResetForce();
   WorkVec.Resize(0);

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
   Done();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

   return WorkVec;
}

std::ostream& NoRotor::Restart(std::ostream& out) const
{
  return Rotor::Restart(out) << "no;" << std::endl;
}

/* Somma alla trazione il contributo di forza di un elemento generico */
void 
NoRotor::AddForce(unsigned int uL, 
		const Vec3& F, const Vec3& M, const Vec3& X)
{
	/*
	 * Non gli serve in quanto non calcola velocita' indotta.
	 * Solo se deve fare l'output lo calcola
	 */
#ifdef USE_MPI
	if (ReqV != MPI::REQUEST_NULL) {
		while (!ReqV.Test()) {
			MYSLEEP(mysleeptime);
		}
	}
#endif /* USE_MPI */

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_lock(&forces_mutex);
	Wait();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

	if (fToBeOutput()) {
		Res.AddForces(F, M, X);
		Rotor::AddForce(uL, F, M, X);
	}

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_unlock(&forces_mutex);
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */
}

/* Restituisce ad un elemento la velocita' indotta in base alla posizione
 * azimuthale */
Vec3 NoRotor::GetInducedVelocity(const Vec3& /* X */ ) const
{
	return Zero3;
}
  
/* NoRotor - end */


/* UniformRotor - begin */

UniformRotor::UniformRotor(unsigned int uLabel,
			   const DofOwner* pDO,
			   const StructNode* pCraft, 
			   const Mat3x3& rrot,
			   const StructNode* pRotor,
			   const StructNode* pGround,
			   ResForceSet **ppres, 
			   doublereal dOR,
			   doublereal dR, 
			   DriveCaller *pdW,
			   unsigned int iMaxIt,
			   doublereal dTol,
			   doublereal dE,
			   doublereal dCH,
			   doublereal dCFF,
			   flag fOut)
: Elem(uLabel, Elem::ROTOR, fOut), 
Rotor(uLabel, pDO, pCraft, rrot, pRotor, pGround, iMaxIt, dTol, dE, ppres, fOut)
{
	ASSERT(dOR > 0.);
	ASSERT(dR > 0.);
	ASSERT(pdW != NULL);
   
	dOmegaRef = dOR;
	dRadius = dR;
	dArea = M_PI*dRadius*dRadius;
	Weight.Set(pdW);
	dHoverCorrection = dCH;
	dForwardFlightCorrection = dCFF;

#ifdef USE_MPI
	if (is_parallel) {
		SAFENEWARR(pBlockLenght, int, 7);
		SAFENEWARR(pDispl, MPI::Aint, 7);
		for (int i = 0; i < 7; i++) {
			pBlockLenght[i] = 1;
		}
		for (int i = 0; i < 3; i++) {	
			pDispl[i] = MPI::Get_address(&(RRot3.pGetVec()[i]));
		}
		pDispl[3] = MPI::Get_address(&dUMeanPrev);
		for (int i = 4; i <= 6; i++) {	
			pDispl[i] = MPI::Get_address(&(Res.Pole().pGetVec()[i-4]));
		}
		SAFENEWWITHCONSTRUCTOR(pRotDataType, MPI::Datatype,
				MPI::Datatype(MPI::DOUBLE.Create_hindexed(7, pBlockLenght, pDispl)));
		pRotDataType->Commit();
	}
#endif /* USE_MPI */
}

UniformRotor::~UniformRotor(void)
{
#ifdef USE_MPI
	SAFEDELETEARR(pBlockLenght);
	SAFEDELETEARR(pDispl);
	SAFEDELETE(pRotDataType);
#endif /* USE_MPI */
}

/* assemblaggio residuo */
SubVectorHandler& UniformRotor::AssRes(SubVectorHandler& WorkVec,
				       doublereal /* dCoef */ ,
				       const VectorHandler& /* XCurr */ ,
				       const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering UniformRotor::AssRes()" << std::endl);  

#ifdef USE_MPI
   ExchangeTraction(fToBeOutput());
   if (!is_parallel || RotorComm.Get_rank() == 0) {
#endif /* USE_MPI */  


     /* Calcola parametri vari */
     Rotor::InitParam();   
     
#ifdef DEBUG   
     /* Prova:
	Vec3 XTmp(2.,2.,0.);
	doublereal dPsiTmp = dGetPsi(XTmp);
	doublereal dXTmp = dGetPos(XTmp);
	std::cout 
	<< "X rotore:  " << pRotor->GetXCurr() << std::endl
	<< "V rotore:  " << VCraft << std::endl
	<< "X punto:   " << XTmp << std::endl
	<< "Omega:     " << dOmega << std::endl
	<< "Velocita': " << dVelocity << std::endl
	<< "Psi0:      " << dPsi0 << std::endl
	<< "Psi punto: " << dPsiTmp << std::endl
	<< "Raggio:    " << dRadius << std::endl
	<< "r punto:   " << dXTmp << std::endl
	<< "mu:        " << dMu << std::endl
	<< "lambda:    " << dLambda << std::endl
	<< "cos(ad):   " << dCosAlphad << std::endl
	<< "sin(ad):   " << dSinAlphad << std::endl
	<< "UMean:     " << dUMean << std::endl;
     */        
#endif /* DEBUG */
   
#ifdef USE_MPI 
   }
   ExchangeVelocity();
#endif /* USE_MPI */   
   ResetForce();
   
   /* Non tocca il residuo */
   WorkVec.Resize(0);

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
   Done();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

   return WorkVec;  
}

std::ostream& UniformRotor::Restart(std::ostream& out) const
{
  return Rotor::Restart(out) << "uniform, " << dRadius << ", ", 
	  Weight.pGetDriveCaller()->Restart(out) 
	  << ", correction, " << dHoverCorrection
	  << ", " << dForwardFlightCorrection << ';' << std::endl;
}

/* Somma alla trazione il contributo di forza di un elemento generico */
void 
UniformRotor::AddForce(unsigned int uL,
		const Vec3& F, const Vec3& M, const Vec3& X)
{
#ifdef USE_MPI
	if (ReqV != MPI::REQUEST_NULL) {
		while (!ReqV.Test()) { 
			MYSLEEP(mysleeptime);
		}
	}
#endif /* USE_MPI */

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_lock(&forces_mutex);
	Wait();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

	/* Solo se deve fare l'output calcola anche il momento */
	if (fToBeOutput()) {      
		Res.AddForces(F, M, X);
		Rotor::AddForce(uL, F, M, X);
	} else {
		Res.AddForce(F);
	}

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_unlock(&forces_mutex);
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */
}

/* Restituisce ad un elemento la velocita' indotta in base alla posizione
 * azimuthale */
Vec3 UniformRotor::GetInducedVelocity(const Vec3& /* X */ ) const
{
#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	Wait();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

	return RRot3*dUMeanPrev;
};

/* UniformRotor - end */


/* GlauertRotor - begin */

GlauertRotor::GlauertRotor(unsigned int uLabel,
			   const DofOwner* pDO,
			   const StructNode* pCraft,
			   const Mat3x3& rrot,
			   const StructNode* pRotor,
			   const StructNode* pGround,
			   ResForceSet **ppres, 
			   doublereal dOR,
			   doublereal dR, 
			   DriveCaller *pdW,
			   unsigned int iMaxIt,
			   doublereal dTol,
			   doublereal dE,
			   doublereal dCH,
			   doublereal dCFF,
			   flag fOut)
: Elem(uLabel, Elem::ROTOR, fOut),
Rotor(uLabel, pDO, pCraft, rrot, pRotor, pGround, iMaxIt, dTol, dE, ppres, fOut)
{
	ASSERT(dOR > 0.);
	ASSERT(dR > 0.);
	ASSERT(pdW != NULL);
 
	dOmegaRef = dOR;
	dRadius = dR;
	dArea = M_PI*dRadius*dRadius;
	Weight.Set(pdW);
	dHoverCorrection = dCH;
	dForwardFlightCorrection = dCFF;

#ifdef USE_MPI
	if (is_parallel) {
		SAFENEWARR(pBlockLenght, int, 20);
		SAFENEWARR(pDispl, MPI::Aint, 20);
		for (int i = 0; i < 20; i++) {
			pBlockLenght[i] = 1;
		}
		for (int i = 0; i < 3; i++) {	
			pDispl[i] = MPI::Get_address(&(RRot3.pGetVec()[i]));
		}
		pDispl[3] = MPI::Get_address(&dUMeanPrev);
		pDispl[4] = MPI::Get_address(&dLambda);
		pDispl[5] = MPI::Get_address(&dMu);
		pDispl[6] = MPI::Get_address(&dChi);
		pDispl[7] = MPI::Get_address(&dPsi0);
		for (int i = 8; i <= 10; i++) {	
			pDispl[i] = MPI::Get_address(&(Res.Pole().pGetVec()[i-8]));
		}
		for (int i = 11; i < 20; i++) {	
			pDispl[i] = MPI::Get_address(&(RRotTranspose.pGetMat()[i-11]));
		}
		SAFENEWWITHCONSTRUCTOR(pRotDataType, MPI::Datatype,
				MPI::Datatype(MPI::DOUBLE.Create_hindexed(20, pBlockLenght, pDispl)));
		pRotDataType->Commit();
	} 
#endif /* USE_MPI */
}


GlauertRotor::~GlauertRotor(void)
{
#ifdef USE_MPI
	SAFEDELETEARR(pBlockLenght);
	SAFEDELETEARR(pDispl);
	SAFEDELETE(pRotDataType);
#endif /* USE_MPI */
}


/* assemblaggio residuo */
SubVectorHandler& GlauertRotor::AssRes(SubVectorHandler& WorkVec,
				       doublereal /* dCoef */ ,
				       const VectorHandler& /* XCurr */ , 
				       const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering GlauertRotor::AssRes()" << std::endl);  

#ifdef USE_MPI
   ExchangeTraction(fToBeOutput());
   if (!is_parallel || RotorComm.Get_rank() == 0) {
#endif /* USE_MPI */

     /* Calcola parametri vari */
     Rotor::InitParam();   

#ifdef USE_MPI 
   }
   ExchangeVelocity();
#endif /* USE_MPI */

   /* Non tocca il residuo */
   ResetForce();
   WorkVec.Resize(0);

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
   Done();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

   return WorkVec;  
}


std::ostream& GlauertRotor::Restart(std::ostream& out) const
{
   return Rotor::Restart(out) << "Glauert, " << dRadius << ", ",
	  Weight.pGetDriveCaller()->Restart(out) 
	  << ", correction, " << dHoverCorrection
	  << ", " << dForwardFlightCorrection << ';' << std::endl;
}


/* Somma alla trazione il contributo di forza di un elemento generico */
void 
GlauertRotor::AddForce(unsigned int uL,
		const Vec3& F, const Vec3& M, const Vec3& X)
{
#ifdef USE_MPI
	if (ReqV != MPI::REQUEST_NULL) {
		while (!ReqV.Test()) { 
			MYSLEEP(mysleeptime);
		}
	}
#endif /* USE_MPI */

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_lock(&forces_mutex);
	Wait();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

	/* Solo se deve fare l'output calcola anche il momento */
	if (fToBeOutput()) {      
		Res.AddForces(F, M, X);
		Rotor::AddForce(uL, F, M, X);
	} else {
		Res.AddForce(F);
	}

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_unlock(&forces_mutex);
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */
}


/*
 * Restituisce ad un elemento la velocita' indotta in base alla posizione
 * azimuthale
 */
Vec3 GlauertRotor::GetInducedVelocity(const Vec3& X) const
{   
	if (dUMeanPrev == 0.) {
		return Vec3(0.);
	}
   
#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	Wait();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

	if (fabs(dLambda) < 1.e-9) {
		return RRot3*dUMeanPrev;
	}
   
	doublereal dr, dp;
	GetPos(X, dr, dp);
	doublereal dd = 1.+4./3.*(1.-1.8*dMu*dMu)*tan(dChi/2.)*dr*cos(dp);
   
	return RRot3*(dd*dUMeanPrev);
};



/* GlauertRotor - end */


/* ManglerRotor - begin */

ManglerRotor::ManglerRotor(unsigned int uLabel,
			   const DofOwner* pDO,
			   const StructNode* pCraft, 
			   const Mat3x3& rrot,
			   const StructNode* pRotor,
			   const StructNode* pGround,
			   ResForceSet **ppres, 
			   doublereal dOR,
			   doublereal dR, 
			   DriveCaller *pdW,
			   unsigned int iMaxIt,
			   doublereal dTol,
			   doublereal dE,
			   doublereal dCH,
			   doublereal dCFF,
			   flag fOut)
: Elem(uLabel, Elem::ROTOR, fOut), 
Rotor(uLabel, pDO, pCraft, rrot, pRotor, pGround, iMaxIt, dTol, dE, ppres, fOut)
{
	ASSERT(dOR > 0.);
	ASSERT(dR > 0.);
	ASSERT(pdW != NULL);

	dOmegaRef = dOR;
	dRadius = dR;
	dArea = M_PI*dRadius*dRadius;
	Weight.Set(pdW);
	dHoverCorrection = dCH;
	dForwardFlightCorrection = dCFF;

#ifdef USE_MPI
	if (is_parallel) {
		SAFENEWARR(pBlockLenght, int, 18);
		SAFENEWARR(pDispl, MPI::Aint, 18);
		for (int i = 0; i < 18; i++) {
			pBlockLenght[i] = 1;
		}
		for (int i = 0; i < 3; i++) {	
			pDispl[i] = MPI::Get_address(&(RRot3.pGetVec()[i]));
		}
		pDispl[3] = MPI::Get_address(&dUMeanPrev);
		pDispl[4] = MPI::Get_address(&dSinAlphad);
		pDispl[5] = MPI::Get_address(&dPsi0);
		for (int i = 6; i <= 8; i++) {	
			pDispl[i] = MPI::Get_address(&(Res.Pole().pGetVec()[i-6]));
		}
		for (int i = 9; i < 18; i++) {
			pDispl[i] = MPI::Get_address(&(RRotTranspose.pGetMat()[i-9]));	  	
		}
		SAFENEWWITHCONSTRUCTOR(pRotDataType, MPI::Datatype,
				MPI::Datatype(MPI::DOUBLE.Create_hindexed(18, pBlockLenght, pDispl)));
		pRotDataType->Commit();
	}
#endif /* USE_MPI */
}


ManglerRotor::~ManglerRotor(void)
{
#ifdef USE_MPI
	SAFEDELETEARR(pBlockLenght);
	SAFEDELETEARR(pDispl);
	SAFEDELETE(pRotDataType);
#endif /* USE_MPI */
}


/* assemblaggio residuo */
SubVectorHandler& ManglerRotor::AssRes(SubVectorHandler& WorkVec,
				       doublereal /* dCoef */ ,
				       const VectorHandler& /* XCurr */ ,
				       const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering ManglerRotor::AssRes()" << std::endl);  

#ifdef USE_MPI
   ExchangeTraction(fToBeOutput());
   if (!is_parallel || RotorComm.Get_rank() == 0) {
#endif /* USE_MPI */

     /* Calcola parametri vari */
     Rotor::InitParam();   
     
#ifdef DEBUG   
     /* Prova: */
     Vec3 XTmp(2.,2.,0.);
     doublereal dPsiTmp = dGetPsi(XTmp);
     doublereal dXTmp = dGetPos(XTmp);
     Vec3 IndV = GetInducedVelocity(XTmp);
     std::cout 
       << "X rotore:  " << pRotor->GetXCurr() << std::endl
       << "V rotore:  " << VCraft << std::endl
       << "X punto:   " << XTmp << std::endl
       << "Omega:     " << dOmega << std::endl
       << "Velocita': " << dVelocity << std::endl
       << "Psi0:      " << dPsi0 << std::endl
       << "Psi punto: " << dPsiTmp << std::endl
       << "Raggio:    " << dRadius << std::endl
       << "r punto:   " << dXTmp << std::endl
       << "mu:        " << dMu << std::endl
       << "lambda:    " << dLambda << std::endl
       << "cos(ad):   " << dCosAlphad << std::endl
       << "sin(ad):   " << dSinAlphad << std::endl
       << "UMean:     " << dUMean << std::endl
       << "iv punto:  " << IndV << std::endl;
#endif /* DEBUG */

#ifdef USE_MPI 
   }
   ExchangeVelocity();
#endif /* USE_MPI */

   /* Non tocca il residuo */
   ResetForce();
   WorkVec.Resize(0);

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
   Done();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

   return WorkVec;  
}
   

std::ostream& ManglerRotor::Restart(std::ostream& out) const
{
   return Rotor::Restart(out) << "Mangler, " << dRadius << ", ",
	  Weight.pGetDriveCaller()->Restart(out) 
	  << ", correction, " << dHoverCorrection
	  << ", " << dForwardFlightCorrection << ';' << std::endl;
}


/* Somma alla trazione il contributo di forza di un elemento generico */
void 
ManglerRotor::AddForce(unsigned int uL,
		const Vec3& F, const Vec3& M, const Vec3& X)
{
#ifdef USE_MPI
	if (ReqV != MPI::REQUEST_NULL) {
		while (!ReqV.Test()) { 
			MYSLEEP(mysleeptime);
		}
	}
#endif /* USE_MPI */

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_lock(&forces_mutex);
	Wait();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

	/* Solo se deve fare l'output calcola anche il momento */
	if (fToBeOutput()) {      
		Res.AddForces(F, M, X);
		Rotor::AddForce(uL, F, M, X);
	} else {
		Res.AddForce(F);
	}

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_unlock(&forces_mutex);
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */
}


/* Restituisce ad un elemento la velocita' indotta in base alla posizione
 * azimuthale */
Vec3 ManglerRotor::GetInducedVelocity(const Vec3& X) const
{
	if (dUMeanPrev == 0.) {
		return Vec3(0.);
	}
   
#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	Wait();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

	doublereal dr, dp;
	GetPos(X, dr, dp);
   
	doublereal dr2 = dr*dr;
	doublereal dm2 = 1.-dr2;   
	doublereal dm = 0.;
	if (dm2 > 0.) {
		dm = sqrt(dm2);
	}
	doublereal da = 1.+dSinAlphad;
	if (fabs(da) > 1.e-9) {
		da = (1.-dSinAlphad)/da;
	}
	if (fabs(da) > 0.) {
		da = sqrt(da);
	}
   
	doublereal dd = 15./4.*dm*dr2;
   
	/* Primo coefficiente */
	doublereal dc = -15./256.*M_PI*(9.*dr2-4.)*dr*da;
	dd -= 4.*dc*cos(dp);
   
	/* Secondo coefficiente */
	dc = 45./256.*M_PI*pow(da*dr, 3);
	dd -= 4.*dc*cos(3.*dp);
   
	/* Coefficienti pari, da 2 a 10: */
	for (int i = 2; i <= 10; i += 2) {
		dc = pow(-1., i/2-1)*15./8.
			*((dm+i)/(i*i-1.)*(3.-9.*dr2+i*i)+3.*dm)/(i*i-9.)
			*pow(((1.-dm)/(1.+dm))*da, i/2.);
		dd -= 4.*dc*cos(i*dp);
	}
         
	return RRot3*(dd*dUMeanPrev);
};



/* ManglerRotor - end */




const doublereal dM11 = 8./(3.*M_PI);
const doublereal dM22 = -16./(45.*M_PI);
const doublereal dM33 = -16./(45.*M_PI);

/* DynamicInflowRotor - begin */

DynamicInflowRotor::DynamicInflowRotor(unsigned int uLabel,
				       const DofOwner* pDO,
				       const StructNode* pCraft, 
				       const Mat3x3& rrot,
				       const StructNode* pRotor,
	    			       const StructNode* pGround,
				       ResForceSet **ppres, 
				       doublereal dOR,
				       doublereal dR,
				       unsigned int iMaxIt,
				       doublereal dTol,
				       doublereal dE,
	    			       doublereal dCH,
	    			       doublereal dCFF,
				       doublereal dVConstTmp,
				       doublereal dVSineTmp,
				       doublereal dVCosineTmp,
				       flag fOut)
: Elem(uLabel, Elem::ROTOR, fOut),
Rotor(uLabel, pDO, pCraft, rrot, pRotor, pGround, iMaxIt, dTol, dE, ppres, fOut),
dVConst(dVConstTmp), dVSine(dVSineTmp), dVCosine(dVCosineTmp), 
dL11(0.), dL13(0.), dL22(0.), dL31(0.), dL33(0.)
{
	ASSERT(dOR > 0.);
	ASSERT(dR > 0.);
   
	dOmegaRef = dOR;
	dRadius = dR;
	dArea = M_PI*dRadius*dRadius;
 
	dHoverCorrection = dCH;
	dForwardFlightCorrection = dCFF;

	/* Significa che valuta la velocita' indotta media al passo corrente */
	dWeight = 0.;

#ifdef USE_MPI
	if (is_parallel) {
		SAFENEWARR(pBlockLenght, int, 20);
		SAFENEWARR(pDispl, MPI::Aint, 20);
		for (int i = 0; i < 20; i++) {
			pBlockLenght[i] = 1;
		}
		for (int i = 0; i < 3; i++) {	
			pDispl[i] = MPI::Get_address(RRot3.pGetVec()+i);
		}
		pDispl[3] = MPI::Get_address(&dVConst);
		pDispl[4] = MPI::Get_address(&dVSine);
		pDispl[5] = MPI::Get_address(&dVCosine);
		pDispl[6] = MPI::Get_address(&dOmega);
		pDispl[7] = MPI::Get_address(&dPsi0);
		for (int i = 8; i <= 10; i++) {	
			pDispl[i] = MPI::Get_address(Res.Pole().pGetVec()+i-8);
		}
		for (int i = 11; i < 20; i++) {	
			pDispl[i] = MPI::Get_address(RRotTranspose.pGetMat()+i-11);
		}
		SAFENEWWITHCONSTRUCTOR(pRotDataType, MPI::Datatype,
				MPI::Datatype(MPI::DOUBLE.Create_hindexed(20, pBlockLenght, pDispl)));
		pRotDataType->Commit();
	}
#endif /* USE_MPI */
}


DynamicInflowRotor::~DynamicInflowRotor(void)
{
#ifdef USE_MPI
	SAFEDELETEARR(pBlockLenght);
	SAFEDELETEARR(pDispl);
	SAFEDELETE(pRotDataType);
#endif /* USE_MPI */
}


void
DynamicInflowRotor::Output(OutputHandler& OH) const
{
     	/*
	 * FIXME: posso usare dei temporanei per il calcolo della trazione
	 * totale per l'output, cosi' evito il giro dei cast
	 */
	if (fToBeOutput()) {
#ifdef USE_MPI
		if (is_parallel && RotorComm.Get_size() > 1) {
			if (RotorComm.Get_rank() == 0) {
				Vec3 TmpF(pTmpVecR), TmpM(pTmpVecR+3);

				OH.Rotors() 
					<< std::setw(8) << GetLabel()	/* 1 */
					<< " " << RRotTranspose*TmpF /* 2-4 */
					<< " " << RRotTranspose*TmpM /* 5-7 */
					<< " " << dUMean	/* 8 */
					<< " " << dVelocity	/* 9 */
					<< " " << atan2(dSinAlphad, dCosAlphad)	/* 10 */
					<< " " << dMu		/* 11 */
					<< " " << dLambda	/* 12 */
					<< " " << dChi		/* 13 */
					<< " " << dPsi0		/* 14 */
					<< " " << bUMeanRefConverged /* 15 */
					<< " " << iCurrIter	/* 16 */
					<< " " << dVConst	/* 17 */
					<< " " << dVSine	/* 18 */
					<< " " << dVCosine	/* 19 */
					<< std::endl; 

				for (int i = 0; ppRes && ppRes[i]; i++) {
					Vec3 TmpF(pTmpVecR+6+6*i);
					Vec3 TmpM(pTmpVecR+9+6*i);
	
					OH.Rotors()
						<< std::setw(8) << GetLabel() 
						<< ":" << ppRes[i]->GetLabel()
						<< " " << TmpF
						<< " " << TmpM
						<< std::endl;
				}
			}
		} else {
			OH.Rotors()
				<< std::setw(8) << GetLabel()	/* 1 */
				<< " " << RRotTranspose*Res.Force()  /* 2-4 */
				<< " " << RRotTranspose*Res.Couple() /* 5-7 */
				<< " " << dUMean	/* 8 */
				<< " " << dVelocity	/* 9 */
				<< " " << atan2(dSinAlphad, dCosAlphad)	/* 10 */
				<< " " << dMu		/* 11 */
				<< " " << dLambda	/* 12 */
				<< " " << dChi		/* 13 */
				<< " " << dPsi0		/* 14 */
				<< " " << bUMeanRefConverged /* 15 */
				<< " " << iCurrIter	/* 16 */
				<< " " << dVConst	/* 17 */
				<< " " << dVSine	/* 18 */
				<< " " << dVCosine	/* 19 */
				<< std::endl;

			for (int i = 0; ppRes && ppRes[i]; i++) {
				OH.Rotors()
					<< std::setw(8) << GetLabel() 
	    				<< ":" << ppRes[i]->GetLabel()
					<< " " << ppRes[i]->pRes->Force()
					<< " " << ppRes[i]->pRes->Couple()
					<< std::endl;
			}
		}

#else /* !USE_MPI */
		OH.Rotors()
			<< std::setw(8) << GetLabel()	/* 1 */
			<< " " << RRotTranspose*Res.Force()	/* 2-4 */
			<< " " << RRotTranspose*Res.Couple()	/* 5-7 */
			<< " " << dUMean	/* 8 */
			<< " " << dVelocity	/* 9 */
			<< " " << atan2(dSinAlphad,dCosAlphad)	/* 10 */
			<< " " << dMu		/* 11 */
			<< " " << dLambda	/* 12 */
			<< " " << dChi		/* 13 */
			<< " " << dPsi0		/* 14 */
			<< " " << bUMeanRefConverged /* 15 */
			<< " " << iCurrIter	/* 16 */
			<< " " << dVConst	/* 17 */
			<< " " << dVSine	/* 18 */
			<< " " << dVCosine	/* 19 */
			<< std::endl;

		for (int i = 0; ppRes && ppRes[i]; i++) {
			OH.Rotors()
				<< std::setw(8) << GetLabel() 
    				<< ":" << ppRes[i]->GetLabel()
				<< " " << ppRes[i]->pRes->Force()
				<< " " << ppRes[i]->pRes->Couple()
				<< std::endl;
		}
#endif /* !USE_MPI */     
	}
}


/* assemblaggio jacobiano */
VariableSubMatrixHandler& 
DynamicInflowRotor::AssJac(VariableSubMatrixHandler& WorkMat,
		doublereal dCoef,
		const VectorHandler& /* XCurr */ ,
		const VectorHandler& /* XPrimeCurr */ )
{
	DEBUGCOUT("Entering DynamicInflowRotor::AssJac()" << std::endl);

	WorkMat.SetNullMatrix();

#ifdef USE_MPI 
	if (is_parallel && RotorComm.Get_rank() == 0) {
#endif /* USE_MPI */     
		SparseSubMatrixHandler& WM = WorkMat.SetSparse();
		integer iFirstIndex = iGetFirstIndex();

		WM.ResizeReset(5, 0);
   
		WM.PutItem(1, iFirstIndex+1, iFirstIndex+1, dM11+dCoef*dL11);
		WM.PutItem(2, iFirstIndex+3, iFirstIndex+1, dCoef*dL31);
		WM.PutItem(3, iFirstIndex+2, iFirstIndex+2, dM22+dCoef*dL22);
		WM.PutItem(4, iFirstIndex+1, iFirstIndex+3, dCoef*dL13);
		WM.PutItem(5, iFirstIndex+3, iFirstIndex+3, dM33+dCoef*dL33);

#ifdef USE_MPI
	}
#endif /* USE_MPI */

	return WorkMat;
}


/* assemblaggio residuo */
SubVectorHandler&
DynamicInflowRotor::AssRes(SubVectorHandler& WorkVec,
		doublereal /* dCoef */ ,
		const VectorHandler& XCurr, 
		const VectorHandler& XPrimeCurr)
{
     	DEBUGCOUT("Entering DynamicInflowRotor::AssRes()" << std::endl);
   
#ifdef USE_MPI
     	ExchangeTraction(flag(1));
   
     	if (!is_parallel || RotorComm.Get_rank() == 0) {
#endif /* USE_MPI */

	   	/* Calcola parametri vari */
	   	Rotor::InitParam();   
     
       	   	WorkVec.Resize(3);

	   	integer iFirstIndex = iGetFirstIndex();
     	
	   	WorkVec.PutRowIndex(1, iFirstIndex+1);
	   	WorkVec.PutRowIndex(2, iFirstIndex+2);
	   	WorkVec.PutRowIndex(3, iFirstIndex+3);
     
	   	dVConst = XCurr.dGetCoef(iFirstIndex+1);
	   	dVSine = XCurr.dGetCoef(iFirstIndex+2);
	   	dVCosine = XCurr.dGetCoef(iFirstIndex+3);
     
	   	doublereal dVConstPrime = XPrimeCurr.dGetCoef(iFirstIndex+1);
	   	doublereal dVSinePrime = XPrimeCurr.dGetCoef(iFirstIndex+2);
	   	doublereal dVCosinePrime = XPrimeCurr.dGetCoef(iFirstIndex+3);
     
		doublereal dCT = 0.;
		doublereal dCl = 0.;
		doublereal dCm = 0.;

	   	/*
		 * Attenzione: moltiplico tutte le equazioni per dOmega
		 * (ovvero, i coefficienti CT, CL e CM sono divisi
		 * per dOmega anziche' dOmega^2)
		 */
	   	doublereal dDim = dGetAirDensity(GetXCurr())*dArea*dOmega*(dRadius*dRadius);
	   	if (dDim > DBL_EPSILON) {
			/*
			 * From Claudio Monteggia:
			 *
			 *                        Ct
			 * Um = -------------------------------------
			 *       sqrt( lambda^2 / KH^4 + mu^2 / KF^2)
			 */
		 	doublereal dLambdaTmp
				= dLambda/(dHoverCorrection*dHoverCorrection);
		 	doublereal dMuTmp = dMu/dForwardFlightCorrection;
	
		 	doublereal dVT
				= sqrt(dLambdaTmp*dLambdaTmp+dMuTmp*dMuTmp);
		 	doublereal dVm = 0.;
		 	if (dVT > DBL_EPSILON) { 
		       		dVm = (dMuTmp*dMuTmp+dLambdaTmp*(dLambdaTmp+dVConst))/dVT;
		 	}
      
			/* 
			 * dUMean is just for output;
			 */	
		   	dUMean = dVConst*dOmega*dRadius;

		   	/* Trazione nel sistema rotore */
		   	doublereal dT = RRot3*Res.Force();
     
		   	/* Momento nel sistema rotore-vento */
		   	doublereal dCosP = cos(dPsi0);
		   	doublereal dSinP = sin(dPsi0);
		   	Mat3x3 RTmp( dCosP, -dSinP, 0., 
		      			dSinP, dCosP, 0.,
		      			0., 0., 1.);
			
		   	Vec3 M(RTmp*(RRotTranspose*Res.Couple()));

		 	/* Thrust, roll and pitch coefficients */
		 	dCT = dT/dDim;
		 	dDim *= dRadius;
		 	dCl = - M.dGet(1)/dDim;
		 	dCm = - M.dGet(2)/dDim;

			/* Matrix coefficients */
			/* FIXME: divide by 0? */
		 	doublereal dl11 = .5/dVT;
			/* FIXME: divide by 0? */
		 	doublereal d = 15./64.*M_PI*tan(dChi/2.);
			/* FIXME: divide by 0? */
		 	doublereal dl13 = d/dVm;
			/* FIXME: divide by 0? */
		 	doublereal dl31 = d/dVT;

		 	doublereal dCosChi2 = cos(dChi/2.);
		 	d = 2.*dCosChi2*dCosChi2;
			/* FIXME: divide by 0? */
		 	doublereal dl22 = -4./(d*dVm);
			/* FIXME: divide by 0? */
			doublereal dl33 = -4.*(d - 1)/(d*dVm);

			d = dl11*dl33 - dl31*dl13;
			/* FIXME: divide by 0? */
			dL11 = dOmega*dl33/d;
			dL31 = -dOmega*dl31/d;
			dL13 = -dOmega*dl13/d;
			dL33 = dOmega*dl11/d;
			dL22 = dOmega/dl22;

	   	} else {   
			dL11 = 0.;
			dL13 = 0.;
			dL22 = 0.;
			dL31 = 0.;
			dL33 = 0.;
	   	}

#ifdef DEBUG
	   	/* Prova: */
		static int i = -1;
		int iv[] = { 0, 1, 0, -1, 0 };
		if (++i == 4) {
			i = 0;
		}
	   	Vec3 XTmp(pRotor->GetXCurr()+pCraft->GetRCurr()*Vec3(dRadius*iv[i],dRadius*iv[i+1],0.));
	   	doublereal dPsiTmp, dXTmp;
	   	GetPos(XTmp, dXTmp, dPsiTmp);
	   	Vec3 IndV = GetInducedVelocity(XTmp);
	   	std::cout 
		 	<< "X rotore:  " << pRotor->GetXCurr() << std::endl
		 	<< "V rotore:  " << VCraft << std::endl
		 	<< "X punto:   " << XTmp << std::endl
		 	<< "Omega:     " << dOmega << std::endl
		 	<< "Velocita': " << dVelocity << std::endl
		 	<< "Psi0:      " << dPsi0 << " ("
				<< dPsi0*180./M_PI << " deg)" << std::endl
		 	<< "Psi punto: " << dPsiTmp << " ("
				<< dPsiTmp*180./M_PI << " deg)" << std::endl
		 	<< "Raggio:    " << dRadius << std::endl
		 	<< "r punto:   " << dXTmp << std::endl
		 	<< "mu:        " << dMu << std::endl
		 	<< "lambda:    " << dLambda << std::endl
		 	<< "cos(ad):   " << dCosAlphad << std::endl
		 	<< "sin(ad):   " << dSinAlphad << std::endl
		 	<< "UMean:     " << dUMean << std::endl
		 	<< "iv punto:  " << IndV << std::endl;
#endif /* DEBUG */

		WorkVec.PutCoef(1, dCT - dM11*dVConstPrime
				- dL11*dVConst - dL13*dVCosine);
	 	WorkVec.PutCoef(2, dCl - dM22*dVSinePrime
				- dL22*dVSine);
	 	WorkVec.PutCoef(3, dCm - dM33*dVCosinePrime
				- dL31*dVConst - dL33*dVCosine);

#ifdef USE_MPI 

     	} else {
	   	WorkVec.Resize(0);
     	}

	ExchangeVelocity();

#endif /* USE_MPI */

	/* Ora la trazione non serve piu' */
	ResetForce();

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	Done();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

     	return WorkVec;
}


/* Relativo ai ...WithDofs */
void
DynamicInflowRotor::SetInitialValue(VectorHandler& /* X */ ) const
{
	NO_OP;
}


/* Relativo ai ...WithDofs */
void
DynamicInflowRotor::SetValue(DataManager *pDM,
		VectorHandler& X, VectorHandler& XP,
		SimulationEntity::Hints *ph)
{
	integer iFirstIndex = iGetFirstIndex();

	for (int iCnt = 1; iCnt <= 3; iCnt++) {
		XP.PutCoef(iFirstIndex+iCnt, 0.);
	}   

	X.PutCoef(iFirstIndex+1, dVConst);
	X.PutCoef(iFirstIndex+2, dVSine);
	X.PutCoef(iFirstIndex+3, dVCosine);
}


/* Restart */
std::ostream& DynamicInflowRotor::Restart(std::ostream& out) const
{
   return Rotor::Restart(out) << "dynamic inflow, " << dRadius
	  << ", correction, " << dHoverCorrection
	  << ", " << dForwardFlightCorrection << ';' << std::endl;
}


/* Somma alla trazione il contributo di forza di un elemento generico */
void 
DynamicInflowRotor::AddForce(unsigned int uL,
		const Vec3& F, const Vec3& M, const Vec3& X)
{
	/*
	 * Gli serve la trazione ed il momento rispetto al rotore, 
	 * che si calcola da se'
	 */
#ifdef USE_MPI
	if (ReqV != MPI::REQUEST_NULL) {
		while (!ReqV.Test()) { 
			MYSLEEP(mysleeptime);
		}
	}
#endif /* USE_MPI */

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_lock(&forces_mutex);
	Wait();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

	Res.AddForces(F, M, X);
	if (fToBeOutput()) {
		Rotor::AddForce(uL, F, M, X);
	}

#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	pthread_mutex_unlock(&forces_mutex);
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */
}


/* Restituisce ad un elemento la velocita' indotta in base alla posizione
 * azimuthale */
Vec3
DynamicInflowRotor::GetInducedVelocity(const Vec3& X) const
{
#if defined(USE_MULTITHREAD) && defined(MBDYN_X_MT_ASSRES)
	Wait();
#endif /* USE_MULTITHREAD && MBDYN_X_MT_ASSRES */

	doublereal dr, dp;
	GetPos(X, dr, dp);
	
	return RRot3*((dVConst+dr*(dVCosine*cos(dp)+dVSine*sin(dp)))*dRadius*dOmega);
};

/* DynamicInflowRotor - end */


/* Legge un rotore */
Elem *
ReadRotor(DataManager* pDM,
		MBDynParser& HP, 
		const DofOwner* pDO, 
		unsigned int uLabel)
{
     	const char sFuncName[] = "ReadRotor()";
     	DEBUGCOUT("Entering " << sFuncName << std::endl);

	/* demote to pedantic; syntax changed a long ago... */
     	pedantic_cout("WARNING: the syntax changed; use a comma ',' "
     			"instead of a colon ':' after the keyword "
     			"\"induced velocity\"" << std::endl);
   
     	const char* sKeyWords[] = {
	  	"induced" "velocity",
			"no",
			"uniform",
			"glauert",
			"mangler",
			"dynamic" "inflow",
		NULL
     	};

     	/* enum delle parole chiave */
     	enum KeyWords {
	  	UNKNOWN = -1,
		INDUCEDVELOCITY = 0,
			NO,
			UNIFORM,
			GLAUERT,
			MANGLER,
			DYNAMICINFLOW,
		LASTKEYWORD 
     	};

     	/* tabella delle parole chiave */
     	KeyTable K(HP, sKeyWords);
   
     	/*     Velivolo */
     	unsigned int uNode = (unsigned int)HP.GetInt();
     	DEBUGCOUT("Craft Node: " << uNode << std::endl);
   
	/* verifica di esistenza del nodo */
     	StructNode* pCraft = pDM->pFindStructNode(uNode);
     	if (pCraft == NULL) {
	  	silent_cerr(std::endl << sFuncName
			<< " at line " << HP.GetLineData() 
			<< ": craft structural node " << uNode
			<< " not defined" << std::endl);
	  	throw DataManager::ErrGeneric();
     	}

     	Mat3x3 rrot(Eye3);
     	if (HP.IsKeyWord("hinge")) {
     		ReferenceFrame RF(pCraft);
     		rrot = HP.GetRotRel(RF);
     	}
   
     	uNode = (unsigned int)HP.GetInt();
     	DEBUGCOUT("Rotor Node: " << uNode << std::endl);
   
     	/* verifica di esistenza del nodo */
     	StructNode* pRotor = pDM->pFindStructNode(uNode);
     	if (pRotor == NULL) {
	  	silent_cerr(std::endl << sFuncName
			<< " at line " << HP.GetLineData() 
			<< ": rotor structural node " << uNode
			<< " not defined" << std::endl);
	  	throw DataManager::ErrGeneric();
     	}

	KeyWords InducedType = NO;
     	if (HP.IsArg() && HP.IsKeyWord("induced" "velocity")) {
      		InducedType = KeyWords(HP.GetWord());
     	}

     	Elem* pEl = NULL;
     	ResForceSet **ppres = NULL;
   
     	switch (InducedType) {
	case NO: {
		DEBUGCOUT("No induced velocity is considered" << std::endl);
       
	 	doublereal dR = 0.;
	 	if (HP.IsKeyWord("radius")) {
	      		dR = HP.GetReal();
	 	}

	 	ppres = ReadResSets(pDM, HP);
      
	 	flag fOut = pDM->fReadOutput(HP, Elem::ROTOR);
       
	 	SAFENEWWITHCONSTRUCTOR(pEl,
  				NoRotor,
  				NoRotor(uLabel, pDO, pCraft, rrot, pRotor, 
  					ppres, dR, fOut));
	 	break;
	}
      
    	case UNIFORM:
    	case GLAUERT:
    	case MANGLER:
    	case DYNAMICINFLOW: {
		doublereal dOR = HP.GetReal();
	 	DEBUGCOUT("Reference rotation speed: " << dOR << std::endl);
	 	if (dOR <= 0.) {
	      		silent_cerr("Illegal null or negative "
				"reference speed for rotor " << uLabel
				<< " at line " << HP.GetLineData()
				<< std::endl);
	      		throw DataManager::ErrGeneric();
	 	}
       
	 	doublereal dR = HP.GetReal();
	 	DEBUGCOUT("Radius: " << dR << std::endl);
	 	if (dR <= 0.) {
	      		silent_cerr("Illegal null or negative radius "
				"for rotor " << uLabel
				<< " at line " << HP.GetLineData()
				<< std::endl);
	      		throw DataManager::ErrGeneric();
	 	}
       
	 	doublereal dVConst = 0.;
	 	doublereal dVSine = 0.;
	 	doublereal dVCosine = 0.;
	 	DriveCaller *pdW = NULL;

		StructNode *pGround = NULL;
		if (HP.IsKeyWord("ground")) {
			uNode = (unsigned int)HP.GetInt();
     			DEBUGCOUT("Ground Node: " << uNode << std::endl);
   
			/* verifica di esistenza del nodo */
     			pGround = pDM->pFindStructNode(uNode);
     			if (pGround == NULL) {
	  			silent_cerr("ground structural node " << uNode
					<< " not defined at line " 
					<< HP.GetLineData()  << std::endl);
	  			throw DataManager::ErrGeneric();
     			}
		}

	 	if (InducedType == DYNAMICINFLOW) {
	      		if (HP.IsKeyWord("initial" "value")) {
		   		dVConst = HP.GetReal();
		   		dVSine = HP.GetReal();
		   		dVCosine = HP.GetReal();
	      		}
	  
	 	} else {   	      	   	      	  
	      		/*
			 * Legge il coefficiente di peso della velocita' 
			 * indotta ("weight" e' deprecato, si preferisce
			 * "delay") 
			 * 
			 * nota:
			 *
			 * U = U_n * ( 1 - dW ) + U_n-1 * dW
			 *
			 * quindi dW rappresenta il peso che si da'
			 * al valore al passo precedente; in questo modo
			 * si introduce un ritardo euristico (attenzione:
			 * il ritardo vero dipende dal passo temporale)
			 * che aiuta ad evitare problemi di convergenza.
			 * Se si desidera un ritardo "fisico", conviene
			 * provare il "Dynamic Inflow".
			 */
	      		if (HP.IsKeyWord("weight") || HP.IsKeyWord("delay")) {
		   		pdW = HP.GetDriveCaller();
	      		} else {
		   		SAFENEW(pdW, NullDriveCaller);
	      		}
	 	}

		/* max iterations when computing reference inflow velocity;
		 * after iMaxIter iterations, the current value is accepted
		 * regardless of convergence; thus, 1 reproduces original
		 * behavior */
#if 0
		int iMaxIter = INT_MAX;
#endif
		unsigned int iMaxIter = 1;
		if (HP.IsKeyWord("max" "iterations")) {
			int i = HP.GetInt();
			if (i <= 0) {
				silent_cerr("illegal max iterations " 
					<< i << " for Rotor(" 
					<< uLabel << ")");
				throw ErrGeneric();
			}
			iMaxIter = i;
		}

		/* tolerance when computing reference inflow velocity;
		 * when the difference in inflow velocity between two 
		 * iterations is less than tolerance in module, the
		 * cycle breaks */
		doublereal dTolerance = DBL_MAX;
		if (HP.IsKeyWord("tolerance")) {
			dTolerance = HP.GetReal();
			if (dTolerance <= 0.) {
				silent_cerr("illegal tolerance " 
					<< dTolerance << " for Rotor(" 
					<< uLabel << ")");
				throw ErrGeneric();
			}
		}

		/* increment factor when computing reference inflow velocity;
		 * only a fraction dEta of the difference between two iterations
		 * is applied */
		doublereal dEta = 1.;
		if (HP.IsKeyWord("eta")) {
			dEta = HP.GetReal();
			if (dEta <= 0.) {
				silent_cerr("illegal eta " 
					<< dEta << " for Rotor(" 
					<< uLabel << ")");
				throw ErrGeneric();
			}
		}

	 	/* Legge la correzione della velocita' indotta */
	 	doublereal dCH = 1.;
	 	doublereal dCFF = 1.;
	 	if (HP.IsKeyWord("correction")) {
	 		dCH = HP.GetReal();
	 		DEBUGCOUT("Hover correction: " << dCH << std::endl);
	 		if (dCH <= 0.) {
	 			silent_cerr("warning, illegal null "
					"or negative inflow correction "
					"for rotor " << uLabel
	 				<< ", switching to 1" << std::endl);
	 			dCH = 1.;
	 		}

	 		dCFF = HP.GetReal();
			DEBUGCOUT("Forward-flight correction: " << dCFF << std::endl);
	 		if (dCFF <= 0.) {
	 			silent_cerr("warning, illegal null "
					"or negative inflow correction "
					"for rotor " << uLabel
	 				<< ", switching to 1" << std::endl);
	 			dCFF = 1.;
	 		}
	 	}

	 	ppres = ReadResSets(pDM, HP);
	  
	 	flag fOut = pDM->fReadOutput(HP, Elem::ROTOR);
	  
      		switch (InducedType) {
	     	case UNIFORM:
	  		DEBUGCOUT("Uniform induced velocity" << std::endl);
			SAFENEWWITHCONSTRUCTOR(pEl, 
   					UniformRotor,
   					UniformRotor(uLabel, pDO, pCraft, rrot,
   						pRotor, pGround,
   						ppres, dOR, dR, pdW, 
						iMaxIter, dTolerance, dEta,
						dCH, dCFF,
   						fOut));
	  		break;

     		case GLAUERT:
	  		DEBUGCOUT("Glauert induced velocity" << std::endl);
	  		SAFENEWWITHCONSTRUCTOR(pEl,
   					GlauertRotor,
   					GlauertRotor(uLabel, pDO, pCraft, rrot,
   						pRotor, pGround,
   						ppres, dOR, dR, pdW, 
						iMaxIter, dTolerance, dEta,
						dCH, dCFF, 
   						fOut));
	  		break;

     		case MANGLER:
	  		DEBUGCOUT("Mangler induced velocity" << std::endl);

	  		SAFENEWWITHCONSTRUCTOR(pEl,
   					ManglerRotor,
   					ManglerRotor(uLabel, pDO, pCraft, rrot,
   						pRotor, pGround,
   						ppres, dOR, dR, pdW, 
						iMaxIter, dTolerance, dEta,
						dCH, dCFF, 
   						fOut));
	  		break;

     		case DYNAMICINFLOW:
	  		DEBUGCOUT("Dynamic inflow" << std::endl);

	  		SAFENEWWITHCONSTRUCTOR(pEl, 
       					DynamicInflowRotor,
       					DynamicInflowRotor(uLabel, pDO, 
						pCraft, rrot, pRotor, 
						pGround, ppres, 
						dOR, dR,
						iMaxIter, dTolerance, dEta,
						dCH, dCFF,
						dVConst, dVSine, dVCosine,
						fOut));
			break;
			
     		default:
			ASSERTMSG(0, "You shouldn't have reached this point");
			throw DataManager::ErrGeneric();	    
      		}
	 	break;
	}

	default:
		silent_cerr("unknown induced velocity type at line " 
	       		<< HP.GetLineData() << std::endl);
	 	throw DataManager::ErrGeneric();
	}
   
	/* Se non c'e' il punto e virgola finale */
	if (HP.IsArg()) {
		silent_cerr("semicolon expected at line "
			<< HP.GetLineData() << std::endl);
		throw DataManager::ErrGeneric();
	}

	ASSERT(pEl != NULL);
	return pEl;
} /* End of DataManager::ReadRotor() */


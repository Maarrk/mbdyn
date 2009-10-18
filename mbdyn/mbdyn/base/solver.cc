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

/*
 * Copyright 1999-2009 Giuseppe Quaranta <quaranta@aero.polimi.it>
 * Dipartimento di Ingegneria Aerospaziale - Politecnico di Milano
 *
 * This copyright statement applies to the MPI related code, which was
 * merged from files schur.h/schur.cc
 */

/*
 *
 * Copyright (C) 2003-2009
 * Giuseppe Quaranta	<quaranta@aero.polimi.it>
 *
 */

/* metodo per la soluzione del modello */

#ifdef HAVE_CONFIG_H
#include "mbconfig.h"           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

/* required for configure time macros with paths */
#include <cstring>
#include <limits>
#include "mbdefs.h"

#define RTAI_LOG

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include <cfloat>
#include <cmath>
#include <vector>
#include "ac/sys_sysinfo.h"

#include "solver.h"
#include "dataman.h"
#include "mtdataman.h"
#include "thirdorderstepsol.h"
#include "nr.h"
#include "bicg.h"
#include "gmres.h"
#include "solman.h"
#include "readlinsol.h"
#include "ls.h"
#include "naivewrap.h"

#include "solver_impl.h"

#include "ac/lapack.h"
#include "ac/arpack.h"
#include "eigjdqz.h"

const char sDefaultOutputFileName[] = "MBDyn";

#ifdef HAVE_SIGNAL
/*
 * MBDyn starts with mbdyn_keep_going == MBDYN_KEEP_GOING.
 *
 * A single CTRL^C sets it to MBDYN_STOP_AT_END_OF_TIME_STEP,
 * which results in exiting at the end of the time step,
 * after the output in case of success.
 *
 * A second CTRL^C sets it to MBDYN_STOP_AT_END_OF_ITERATION,
 * which results in exiting at the end of the current iteration,
 * after printing debug output if required.
 *
 * A further CTRL^C sets it to MBDYN_STOP_NOW and in throwing
 * an exception.
 */
enum {
	MBDYN_KEEP_GOING = 0,
	MBDYN_STOP_AT_END_OF_TIME_STEP = 1,
	MBDYN_STOP_AT_END_OF_ITERATION = 2,
	MBDYN_STOP_NOW = 3

};

volatile sig_atomic_t mbdyn_keep_going = MBDYN_KEEP_GOING;
__sighandler_t mbdyn_sh_term = SIG_DFL;
__sighandler_t mbdyn_sh_int = SIG_DFL;
__sighandler_t mbdyn_sh_hup = SIG_DFL;
__sighandler_t mbdyn_sh_pipe = SIG_DFL;

extern "C" void
mbdyn_really_exit_handler(int signum)
{
   	::mbdyn_keep_going = MBDYN_STOP_NOW;
   	switch (signum) {
    	case SIGTERM:
      		signal(signum, ::mbdyn_sh_term);
      		break;

    	case SIGINT:
      		signal(signum, ::mbdyn_sh_int);
      		break;

#ifdef SIGHUP
    	case SIGHUP:
      		signal(signum, ::mbdyn_sh_hup);
      		break;
#endif // SIGHUP

#ifdef SIGPIPE
    	case SIGPIPE:
      		signal(signum, ::mbdyn_sh_pipe);
      		break;
#endif // SIGPIPE
   	}

	throw ErrInterrupted(MBDYN_EXCEPT_ARGS);
}

extern "C" void
mbdyn_modify_last_iteration(int signum)
{
   	::mbdyn_keep_going = MBDYN_STOP_AT_END_OF_ITERATION;
      	signal(signum, mbdyn_really_exit_handler);
}

extern "C" void
mbdyn_modify_final_time_handler(int signum)
{
   	::mbdyn_keep_going = MBDYN_STOP_AT_END_OF_TIME_STEP;
      	signal(signum, mbdyn_modify_last_iteration);
}
#endif /* HAVE_SIGNAL */

extern "C" int
mbdyn_stop_at_end_of_iteration(void)
{
#ifdef HAVE_SIGNAL
	return ::mbdyn_keep_going >= MBDYN_STOP_AT_END_OF_ITERATION;
#else // ! HAVE_SIGNAL
	return 0;
#endif // ! HAVE_SIGNAL
}

extern "C" int
mbdyn_stop_at_end_of_time_step(void)
{
#ifdef HAVE_SIGNAL
	return ::mbdyn_keep_going >= MBDYN_STOP_AT_END_OF_TIME_STEP;
#else // ! HAVE_SIGNAL
	return 0;
#endif // ! HAVE_SIGNAL
}

extern "C" void
mbdyn_set_stop_at_end_of_iteration(void)
{
#ifdef HAVE_SIGNAL
	::mbdyn_keep_going = MBDYN_STOP_AT_END_OF_ITERATION;
#endif // HAVE_SIGNAL
}

extern "C" void
mbdyn_set_stop_at_end_of_time_step(void)
{
#ifdef HAVE_SIGNAL
	::mbdyn_keep_going = MBDYN_STOP_AT_END_OF_TIME_STEP;
#endif // HAVE_SIGNAL
}

extern "C" void
mbdyn_signal_init(void)
{
#ifdef HAVE_SIGNAL
	/*
	 * FIXME: don't do this if compiling with USE_RTAI
	 * Re FIXME: use sigaction() ...
	 */
	::mbdyn_sh_term = signal(SIGTERM, mbdyn_modify_final_time_handler);
	if (::mbdyn_sh_term == SIG_IGN) {
		signal(SIGTERM, SIG_IGN);
	}

	::mbdyn_sh_int = signal(SIGINT, mbdyn_modify_final_time_handler);
	if (::mbdyn_sh_int == SIG_IGN) {
		signal(SIGINT, SIG_IGN);
	}

#ifdef SIGHUP
	::mbdyn_sh_hup = signal(SIGHUP, mbdyn_modify_final_time_handler);
	if (::mbdyn_sh_hup == SIG_IGN) {
		signal(SIGHUP, SIG_IGN);
	}
#endif // SIGHUP

#ifdef SIGPIPE
	::mbdyn_sh_pipe = signal(SIGPIPE, mbdyn_modify_final_time_handler);
	if (::mbdyn_sh_pipe == SIG_IGN) {
		signal(SIGPIPE, SIG_IGN);
	}
#endif // SIGPIPE
#endif /* HAVE_SIGNAL */
}

int
mbdyn_reserve_stack(unsigned long size)
{
	int buf[size];

#ifdef HAVE_MEMSET
	memset(buf, 0, size*sizeof(int));
#else /* !HAVE_MEMSET */
	for (unsigned long i = 0; i < size; i++) {
		buf[i] = 0;
	}
#endif /* !HAVE_MEMSET */

#ifdef HAVE_MLOCKALL
	return mlockall(MCL_CURRENT | MCL_FUTURE);
#else /* !HAVE_MLOCKALL */
	return 0;
#endif /* !HAVE_MLOCKALL */
}

/* Costruttore: esegue la simulazione */
Solver::Solver(MBDynParser& HPar,
		const char* sInFName,
		const char* sOutFName,
		bool bPar)
:
#ifdef USE_MULTITHREAD
nThreads(0),
#endif /* USE_MULTITHREAD */
CurrStrategy(NOCHANGE),
sInputFileName(NULL),
sOutputFileName(NULL),
HP(HPar),
pStrategyChangeDrive(NULL),
EigAn(),
pRTSolver(0),
#ifdef __HACK_POD__
bPOD(0),
iPODStep(0),
iPODFrames(0),
#endif /*__HACK_POD__*/
iNumPreviousVectors(2),
iUnkStates(1),
pdWorkSpace(NULL),
qX(),
qXPrime(),
pX(NULL),
pXPrime(NULL),
dTime(0.),
dInitialTime(0.),
dFinalTime(0.),
dRefTimeStep(0.),
dInitialTimeStep(1.),
dMinTimeStep(::dDefaultMinTimeStep),
dMaxTimeStep(::dDefaultMaxTimeStep),
iFictitiousStepsNumber(::iDefaultFictitiousStepsNumber),
dFictitiousStepsRatio(::dDefaultFictitiousStepsRatio),
eAbortAfter(AFTER_UNKNOWN),
iStepsAfterReduction(0),
iStepsAfterRaise(0),
iWeightedPerformedIters(0),
bLastChance(false),
RegularType(INT_UNKNOWN),
FictitiousType(INT_UNKNOWN),
pDerivativeSteps(0),
pFirstFictitiousStep(0),
pFictitiousSteps(0),
pFirstRegularStep(0),
pRegularSteps(0),
pRhoRegular(NULL),
pRhoAlgebraicRegular(NULL),
pRhoFictitious(NULL),
pRhoAlgebraicFictitious(NULL),
dDerivativesCoef(::dDefaultDerivativesCoefficient),
ResTest(NonlinearSolverTest::NORM),
SolTest(NonlinearSolverTest::NONE),
bScale(false),
bTrueNewtonRaphson(true),
bKeepJac(false),
iIterationsBeforeAssembly(0),
NonlinearSolverType(NonlinearSolver::UNKNOWN),
MFSolverType(MatrixFreeSolver::UNKNOWN),
dIterTol(::dDefaultTol),
PcType(Preconditioner::FULLJACOBIANMATRIX),
iPrecondSteps(::iDefaultPreconditionerSteps),
iIterativeMaxSteps(::iDefaultPreconditionerSteps),
dIterertiveEtaMax(defaultIterativeEtaMax),
dIterertiveTau(defaultIterativeTau),
bHonorJacRequest(false),
/* for parallel solvers */
bParallel(bPar),
pSDM(NULL),
iNumLocDofs(0),
iNumIntDofs(0),
pLocDofs(NULL),
pIntDofs(NULL),
pDofs(NULL),
pLocalSM(NULL),
/* end of parallel solvers */
pDM(NULL),
iNumDofs(0),
pSM(NULL),
pNLS(NULL)
{
	DEBUGCOUTFNAME("Solver::Solver");

	ASSERT(sInFName != NULL);

	SAFESTRDUP(sInputFileName, sInFName);

	if (sOutFName != NULL) {
		SAFESTRDUP(sOutputFileName, sOutFName);
	}

	StrategyFactor.iMinIters = 1;
	StrategyFactor.iMaxIters = 0;
}

void
Solver::Run(void)
{
   	DEBUGCOUTFNAME("Solver::Run");

   	/* Legge i dati relativi al metodo di integrazione */
   	ReadData(HP);

#ifdef USE_MULTITHREAD
	/* check for thread potential */
	if (nThreads == 0) {
		int n = get_nprocs();

		if (n > 1) {
			silent_cout("no multithread requested "
					"with a potential of " << n
					<< " CPUs" << std::endl);
			nThreads = n;

		} else {
			nThreads = 1;
		}
	}
#endif /* USE_MULTITHREAD */

	if (pRTSolver) {
		pRTSolver->Setup();
	}

	/* Nome del file di output */
	if (sOutputFileName == 0) {
		if (sInputFileName != 0) {
			SAFESTRDUP(sOutputFileName, sInputFileName);

		} else {
			SAFESTRDUP(sOutputFileName, ::sDefaultOutputFileName);
		}

	} else {
		struct stat	s;

		if (stat(sOutputFileName, &s) != 0) {
			int	save_errno = errno;

			/* if does not exist, check path */
			if (save_errno != ENOENT) {
				char	*errmsg = strerror(save_errno);

				silent_cerr("stat(" << sOutputFileName << ") failed "
					"(" << save_errno << ": " << errmsg << ")" << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			char	*sOutputFilePath = 0;
			SAFESTRDUP(sOutputFilePath, sOutputFileName );

			char	*path = std::strrchr(sOutputFilePath, '/');
			if (path != NULL) {
				path[0] = '\0';

				if (stat(sOutputFilePath, &s) != 0) {
					save_errno = errno;
					char	*errmsg = strerror(save_errno);

					silent_cerr("stat(" << sOutputFileName << ") failed because "
							"stat(" << sOutputFilePath << ") failed "
						"(" << save_errno << ": " << errmsg << ")" << std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);

				} else if (!S_ISDIR(s.st_mode)) {
					silent_cerr("path to file \"" << sOutputFileName << "\" is invalid ("
							"\"" << sOutputFilePath << "\" is not a dir)" << std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
			}
			SAFEDELETEARR(sOutputFilePath);

		} else if (S_ISDIR(s.st_mode)) {
			unsigned 	lOld, lNew;
			char		*tmpOut = 0;
			const char	*tmpIn;

			if (sInputFileName) {
				tmpIn = std::strrchr(sInputFileName, '/');
				if (tmpIn == 0) {
					tmpIn = sInputFileName;
				}

			} else {
				tmpIn = ::sDefaultOutputFileName;
			}

			lOld = strlen(sOutputFileName);
			if (sOutputFileName[lOld - 1] == '/') {
				lOld--;
			}
			lNew = lOld + strlen(tmpIn) + 2;

			SAFENEWARR(tmpOut, char, lNew);
			memcpy(tmpOut, sOutputFileName, lOld);
			if (sOutputFileName[lOld - 1] != '/') {
				tmpOut[lOld] = '/';
				lOld++;
			}
			memcpy(&tmpOut[lOld], tmpIn, lNew - lOld);
			SAFEDELETEARR(sOutputFileName);
			sOutputFileName = tmpOut;
		}
	}

#ifdef USE_SCHUR
	int mpi_finalize = 0;

	int MyRank = 0;
	if (bParallel) {

		/*
		 * E' necessario poter determinare in questa routine
		 * quale e' il master in modo da far calcolare la soluzione
		 * solo su di esso
		 */
		MyRank = MBDynComm.Get_rank();
		/* chiama il gestore dei dati generali della simulazione */

		/*
		 * I file di output vengono stampati localmente
		 * da ogni processo aggiungendo al termine
		 * dell'OutputFileName il rank del processo
		 */
		ASSERT(MPI::COMM_WORLD.Get_size() > 1);
		int iRankLength = 1 + (int)log10(MPI::COMM_WORLD.Get_size() - 1);

		char* sNewOutName = NULL;
		int iOutLen = strlen(sOutputFileName)
			+ STRLENOF(".")
			+ iRankLength
			+ 1;

		SAFENEWARR(sNewOutName, char, iOutLen);
		snprintf(sNewOutName, iOutLen, "%s.%.*d",
				sOutputFileName, iRankLength, MyRank);
		SAFEDELETEARR(sOutputFileName);
		sOutputFileName = sNewOutName;

		DEBUGLCOUT(MYDEBUG_MEM, "creating parallel SchurDataManager"
				<< std::endl);

		SAFENEWWITHCONSTRUCTOR(pSDM,
			SchurDataManager,
			SchurDataManager(HP,
				OutputFlags,
				this,
				dInitialTime,
				sOutputFileName,
				sInputFileName,
				eAbortAfter == AFTER_INPUT));

		/* FIXME: who frees sNewOutname? */

		pDM = pSDM;

	} else
#endif // USE_SCHUR
	{
		/* chiama il gestore dei dati generali della simulazione */
#ifdef USE_MULTITHREAD
		if (nThreads > 1) {
			if (!(CurrLinearSolver.GetSolverFlags() & LinSol::SOLVER_FLAGS_ALLOWS_MT_ASS)) {
				/* conservative: dir may use too much memory */
				if (!CurrLinearSolver.AddSolverFlags(LinSol::SOLVER_FLAGS_ALLOWS_MT_ASS)) {
					bool b;

#if defined(USE_UMFPACK)
					b = CurrLinearSolver.SetSolver(LinSol::UMFPACK_SOLVER,
							LinSol::SOLVER_FLAGS_ALLOWS_MT_ASS);
#elif defined(USE_Y12)
					b = CurrLinearSolver.SetSolver(LinSol::Y12_SOLVER,
							LinSol::SOLVER_FLAGS_ALLOWS_MT_ASS);
#else
					b = false;
#endif
					if (!b) {
						silent_cerr("unable to select a CC-capable solver"
									<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}
				}
			}

			silent_cout("Creating multithread solver "
					"with " << nThreads << " threads "
					"and "
					<< CurrLinearSolver.GetSolverName()
					<< " linear solver"
					<< std::endl);

			SAFENEWWITHCONSTRUCTOR(pDM,
					MultiThreadDataManager,
					MultiThreadDataManager(HP,
						OutputFlags,
						this,
						dInitialTime,
						sOutputFileName,
						sInputFileName,
						eAbortAfter == AFTER_INPUT,
						nThreads));

		} else
#endif /* USE_MULTITHREAD */
		{
			DEBUGLCOUT(MYDEBUG_MEM, "creating DataManager"
					<< std::endl);

			silent_cout("Creating scalar solver "
					"with "
					<< CurrLinearSolver.GetSolverName()
					<< " linear solver"
					<< std::endl);

			SAFENEWWITHCONSTRUCTOR(pDM,
					DataManager,
					DataManager(HP,
						OutputFlags,
						this,
						dInitialTime,
						sOutputFileName,
						sInputFileName,
						eAbortAfter == AFTER_INPUT));
		}
	}

	// log symbol table
	pDM->GetLogFile() << HP.GetMathParser().GetSymbolTable();

	// close input stream
	HP.Close();

   	/* Si fa dare l'std::ostream al file di output per il log */
   	std::ostream& Out = pDM->GetOutFile();

   	if (eAbortAfter == AFTER_INPUT) {
      		/* Esce */
		pDM->Output(0, dTime, 0., true);
      		Out << "End of Input; no simulation or assembly is required."
			<< std::endl;
      		return;

   	} else if (eAbortAfter == AFTER_ASSEMBLY) {
      		/* Fa l'output dell'assemblaggio iniziale e poi esce */
      		pDM->Output(0, dTime, 0., true);
      		Out << "End of Initial Assembly; no simulation is required."
			<< std::endl;
      		return;
   	}

#ifdef USE_SCHUR
	/* Qui crea le partizioni: principale fra i processi, se parallelo  */
	if (bParallel) {
		pSDM->CreatePartition();
	}
#endif // USE_SCHUR

   	/* Si fa dare il DriveHandler e linka i drivers di rho ecc. */
   	const DriveHandler* pDH = pDM->pGetDrvHdl();
   	pRegularSteps->SetDriveHandler(pDH);
	if (iFictitiousStepsNumber) {
   		pFictitiousSteps->SetDriveHandler(pDH);
	}

   	/* Costruisce i vettori della soluzione ai vari passi */
   	DEBUGLCOUT(MYDEBUG_MEM, "creating solution vectors" << std::endl);

#ifdef USE_SCHUR
	if (bParallel) {
		iNumDofs = pSDM->HowManyDofs(SchurDataManager::TOTAL);
		pDofs = pSDM->pGetDofsList();

		iNumLocDofs = pSDM->HowManyDofs(SchurDataManager::LOCAL);
		pLocDofs = pSDM->GetDofsList(SchurDataManager::LOCAL);

		iNumIntDofs = pSDM->HowManyDofs(SchurDataManager::INTERNAL);
		pIntDofs = pSDM->GetDofsList(SchurDataManager::INTERNAL);

	} else
#endif // USE_SCHUR
	{
   		iNumDofs = pDM->iGetNumDofs();
	}

	/* relink those known drive callers that might need
	 * the data manager, but were verated ahead of it */
	if (pStrategyChangeDrive) {
		pStrategyChangeDrive->SetDrvHdl(pDM->pGetDrvHdl());
	}

   	ASSERT(iNumDofs > 0);

	integer iRSteps = pRegularSteps->GetIntegratorNumPreviousStates();
	integer iFSteps = 0;
	if (iFictitiousStepsNumber) {
		iFSteps = pFictitiousSteps->GetIntegratorNumPreviousStates();
	}
	iNumPreviousVectors = (iRSteps < iFSteps) ? iFSteps : iRSteps;

	integer iRUnkStates = pRegularSteps->GetIntegratorNumUnknownStates();
	integer iFUnkStates = 0;
	if (iFictitiousStepsNumber) {
		iFUnkStates = pFictitiousSteps->GetIntegratorNumUnknownStates();
	}
	iUnkStates = (iRUnkStates < iFUnkStates) ? iFUnkStates : iRUnkStates;

	/* allocate workspace for previous time steps */
	SAFENEWARR(pdWorkSpace, doublereal,
		2*(iNumPreviousVectors)*iNumDofs);
	/* allocate MyVectorHandlers for previous time steps: use workspace */
	for (int ivec = 0; ivec < iNumPreviousVectors; ivec++) {
   		SAFENEWWITHCONSTRUCTOR(pX,
			       	MyVectorHandler,
			       	MyVectorHandler(iNumDofs, pdWorkSpace+ivec*iNumDofs));
		qX.push_back(pX);
   		SAFENEWWITHCONSTRUCTOR(pXPrime,
			       	MyVectorHandler,
			       	MyVectorHandler(iNumDofs,
				pdWorkSpace+((iNumPreviousVectors)+ivec)*iNumDofs));
		qXPrime.push_back(pXPrime);
		pX = NULL;
		pXPrime = NULL;
	}
	/* allocate MyVectorHandlers for unknown time step(s): own memory */
	SAFENEWWITHCONSTRUCTOR(pX,
		       	MyVectorHandler,
		       	MyVectorHandler(iUnkStates*iNumDofs));
	SAFENEWWITHCONSTRUCTOR(pXPrime,
		       	MyVectorHandler,
		       	MyVectorHandler(iUnkStates*iNumDofs));


	/* Resetta i vettori */
   	for (int ivec = 0; ivec < iNumPreviousVectors; ivec++) {
		qX[ivec]->Reset();
		qXPrime[ivec]->Reset();
	}
	pX->Reset();
	pXPrime->Reset();

#ifdef __HACK_POD__
	std::ofstream PodOut;
	if (bPOD) {
		char *PODFileName = NULL;

		if (sOutputFileName == NULL) {
			SAFESTRDUP(PODFileName, "MBDyn.POD");

		} else {
			size_t l = strlen(sOutputFileName);
			SAFENEWARR(PODFileName, char, l + STRLENOF(".POD") + 1);

			memcpy(PODFileName, sOutputFileName, l);
			memcpy(PODFileName+l, ".POD", STRLENOF(".POD") + 1);
		}

		PodOut.open(PODFileName);
		if (!PodOut) {
			silent_cerr("unable to open file \"" << PODFileName
				<< "\"" << std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
		SAFEDELETEARR(PODFileName);

#ifdef __HACK_POD_BINARY__
		/* matrix size is coded at the beginning */
		PodOut.write((char *)&(pod.iFrames), sizeof(unsigned long));
		PodOut.write((char *)&iNumDofs, sizeof(unsigned long));
#endif /* __HACK_POD_BINARY__ */
	}
#endif /* __HACK_POD__ */


   	/*
	 * Immediately link DataManager to current solution
	 *
	 * this should work as long as the last unknown time step is put
	 * at the beginning of pX, pXPrime
	 */
   	pDM->LinkToSolution(*pX, *pXPrime);

	/* a questo punto si costruisce il nonlinear solver */
	pNLS = AllocateNonlinearSolver();

	MyVectorHandler Scale(iNumDofs);
	if (bScale) {
		/* collects scale factors from data manager */
		pDM->SetScale(Scale);
	}

	/*
	 * prepare tests for nonlinear solver;
	 *
	 * test on residual may allow pre-scaling;
	 * test on solution (difference between two iterations) does not
	 */
	NonlinearSolverTest *pResTest = NULL;
	if (bScale) {
		NonlinearSolverTestScale *pResTestScale = NULL;

		switch (ResTest) {
		case NonlinearSolverTest::NORM:
			SAFENEW(pResTestScale, NonlinearSolverTestScaleNorm);
			break;

		case NonlinearSolverTest::MINMAX:
			SAFENEW(pResTestScale, NonlinearSolverTestScaleMinMax);
			break;

		default:
			ASSERT(0);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}

		/* registers scale factors at nonlinear solver */
		pResTestScale->SetScale(&Scale);

		pResTest = pResTestScale;


	} else {
		switch (ResTest) {
		case NonlinearSolverTest::NONE:
			SAFENEW(pResTest, NonlinearSolverTestNone);
			break;

		case NonlinearSolverTest::NORM:
			SAFENEW(pResTest, NonlinearSolverTestNorm);
			break;

		case NonlinearSolverTest::MINMAX:
			SAFENEW(pResTest, NonlinearSolverTestMinMax);
			break;

		default:
			ASSERT(0);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
	}

	NonlinearSolverTest *pSolTest = NULL;
	switch (SolTest) {
	case NonlinearSolverTest::NONE:
		SAFENEW(pSolTest, NonlinearSolverTestNone);
		break;

	case NonlinearSolverTest::NORM:
		SAFENEW(pSolTest, NonlinearSolverTestNorm);
		break;

	case NonlinearSolverTest::MINMAX:
		SAFENEW(pSolTest, NonlinearSolverTestMinMax);
		break;

	default:
		ASSERT(0);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	/* registers tests in nonlinear solver */
	pNLS->SetTest(pResTest, pSolTest);

   	/*
	 * Dell'assemblaggio iniziale dei vincoli se ne occupa il DataManager
	 * in quanto e' lui il responsabile dei dati della simulazione,
	 * e quindi anche della loro coerenza. Inoltre e' lui a sapere
	 * quali equazioni sono di vincolo o meno.
	 */

	pDM->SetValue(*pX, *pXPrime);


	/*
	 * Prepare output
	 */
	pDM->OutputPrepare();

   	/*
	 * Dialoga con il DataManager per dargli il tempo iniziale
	 * e per farsi inizializzare i vettori di soluzione e derivata
	 */
	/* FIXME: the time is already set by DataManager, but FileDrivers
	 * have not been ServePending'd
	 */
	dTime = dInitialTime;
	pDM->SetTime(dTime, 0., 0);


   	if (EigAn.bAnalysis
		&& EigAn.OneAnalysis.dTime <= dTime
		&& !EigAn.OneAnalysis.bDone)
	{
	 	Eig();
	 	EigAn.OneAnalysis.bDone = true;
   	}

	integer iTotIter = 0;
	integer iStIter = 0;
	doublereal dTotErr = 0.;
	doublereal dTest = std::numeric_limits<double>::max();
	doublereal dSolTest = std::numeric_limits<double>::max();
	bool bSolConv = false;
	/* calcolo delle derivate */
	DEBUGLCOUT(MYDEBUG_DERIVATIVES, "derivatives solution step"
			<< std::endl);

	mbdyn_signal_init();

	/* settaggio degli output Types */
	unsigned OF = OutputFlags;
	if ( DEBUG_LEVEL_MATCH(MYDEBUG_RESIDUAL) ) {
		OF |= OUTPUT_RES;
	}
	if ( DEBUG_LEVEL_MATCH(MYDEBUG_JAC) ) {
		OF |= OUTPUT_JAC;
	}
	if ( DEBUG_LEVEL_MATCH(MYDEBUG_SOL) ) {
		OF |= OUTPUT_SOL;
	}
	pNLS->SetOutputFlags(OF);

	pDerivativeSteps->SetDataManager(pDM);
	pDerivativeSteps->OutputTypes(DEBUG_LEVEL_MATCH(MYDEBUG_PRED));
	if (iFictitiousStepsNumber) {
		pFirstFictitiousStep->SetDataManager(pDM);
		pFirstFictitiousStep->OutputTypes(DEBUG_LEVEL_MATCH(MYDEBUG_PRED));
		pFictitiousSteps->SetDataManager(pDM);
		pFictitiousSteps->OutputTypes(DEBUG_LEVEL_MATCH(MYDEBUG_PRED));
	}
	pFirstRegularStep->SetDataManager(pDM);
	pFirstRegularStep->OutputTypes(DEBUG_LEVEL_MATCH(MYDEBUG_PRED));
	pRegularSteps->SetDataManager(pDM);
	pRegularSteps->OutputTypes(DEBUG_LEVEL_MATCH(MYDEBUG_PRED));

#ifdef USE_EXTERNAL
	pNLS->SetExternal(External::EMPTY);
#endif /* USE_EXTERNAL */
   	/* Setup SolutionManager(s) */
	SetupSolmans(pDerivativeSteps->GetIntegratorNumUnknownStates());

	/* Derivative steps */
	try {

		dTest = pDerivativeSteps->Advance(this,
				0., 1., StepIntegrator::NEWSTEP,
			 	qX, qXPrime, pX, pXPrime,
				iStIter, dTest, dSolTest);
	}
	catch (NonlinearSolver::NoConvergence) {
		silent_cerr("Initial derivatives calculation " << iStIter
			<< " does not converge; aborting..." << std::endl
			<< "(hint: try playing with the \"derivatives coefficient\" value)" << std::endl);
	 	pDM->Output(0, dTime, 0., true);
	 	throw ErrMaxIterations(MBDYN_EXCEPT_ARGS);

	}
	catch (NonlinearSolver::ErrSimulationDiverged) {
		/*
		 * Mettere qui eventuali azioni speciali
		 * da intraprendere in caso di errore ...
		 */
		throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
	}
	catch (LinearSolver::ErrFactor err) {
		/*
		 * Mettere qui eventuali azioni speciali
		 * da intraprendere in caso di errore ...
		 */
		silent_cerr("Initial derivatives failed because no pivot element "
			"could be found for column " << err.iCol
			<< " (" << pDM->GetDofDescription(err.iCol) << "); "
			"aborting..." << std::endl);
		throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
	}
	catch (NonlinearSolver::ConvergenceOnSolution) {
		bSolConv = true;
	}
	catch (EndOfSimulation& eos) {
		silent_cerr("Simulation ended during the derivatives steps:\n" << eos.what() << "\n");
		return;
	}

	SAFEDELETE(pDerivativeSteps);
	pDerivativeSteps = 0;

#if 0
	/* don't sum up the derivatives error */
	dTotErr  += dTest;
#endif
	iTotIter += iStIter;

	if (outputMsg()) {
   		Out << "# Derivatives solution step at time " << dInitialTime
     			<< " performed in " << iStIter
     			<< " iterations with " << dTest
     			<< " error" << std::endl;
	}

   	DEBUGCOUT("Derivatives solution step has been performed successfully"
		  " in " << iStIter << " iterations" << std::endl);

#ifdef USE_EXTERNAL
	/* comunica che gli ultimi dati inviati sono la condizione iniziale */
	if (iFictitiousStepsNumber == 0) {
		External::SendInitial();
	}
#endif /* USE_EXTERNAL */

   	if (eAbortAfter == AFTER_DERIVATIVES) {
      		/*
		 * Fa l'output della soluzione delle derivate iniziali ed esce
		 */


      		pDM->Output(0, dTime, 0., true);
      		Out << "End of derivatives; no simulation is required."
			<< std::endl;
      		return;
   	} else if (mbdyn_stop_at_end_of_time_step()) {
      		/*
		 * Fa l'output della soluzione delle derivate iniziali ed esce
		 */
      		pDM->Output(0, dTime, 0., true);
      		Out << "Interrupted during derivatives computation." << std::endl;
      		throw ErrInterrupted(MBDYN_EXCEPT_ARGS);
   	}

	/* Dati comuni a passi fittizi e normali */
   	long lStep = 1;
   	doublereal dCurrTimeStep = 0.;

   	if (iFictitiousStepsNumber > 0) {

       		/* passi fittizi */

      		/*
		 * inizio integrazione: primo passo a predizione lineare
		 * con sottopassi di correzione delle accelerazioni
		 * e delle reazioni vincolari
		 */
      		pDM->BeforePredict(*pX, *pXPrime, *qX[0], *qXPrime[0]);
      		Flip();

      		dRefTimeStep = dInitialTimeStep*dFictitiousStepsRatio;
      		dCurrTimeStep = dRefTimeStep;
		/* FIXME: do we need to serve pending drives in dummy steps? */
      		pDM->SetTime(dTime + dCurrTimeStep, dCurrTimeStep, 0);

      		DEBUGLCOUT(MYDEBUG_FSTEPS, "Current time step: "
			   << dCurrTimeStep << std::endl);

	 	ASSERT(pFirstFictitiousStep != NULL);

   		/* Setup SolutionManager(s) */
		SetupSolmans(pFirstFictitiousStep->GetIntegratorNumUnknownStates());
		/* pFirstFictitiousStep */
		try {
	 		dTest = pFirstFictitiousStep->Advance(this,
					dRefTimeStep, 1.,
					StepIntegrator::NEWSTEP,
					qX, qXPrime, pX, pXPrime,
					iStIter, dTest, dSolTest);
		}
		catch (NonlinearSolver::NoConvergence) {
			silent_cerr("First dummy step does not converge; "
				"time step dt=" << dCurrTimeStep
				<< " cannot be reduced further; "
				"aborting..." << std::endl);
	 		pDM->Output(0, dTime, dCurrTimeStep, true);
	 		throw ErrMaxIterations(MBDYN_EXCEPT_ARGS);
		}
		catch (NonlinearSolver::ErrSimulationDiverged) {
			/*
		 	 * Mettere qui eventuali azioni speciali
		 	 * da intraprendere in caso di errore ...
		 	 */
			throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
		}
		catch (LinearSolver::ErrFactor err) {
			/*
			 * Mettere qui eventuali azioni speciali
			 * da intraprendere in caso di errore ...
			 */
			silent_cerr("First dummy step failed because no pivot element "
				"could be found for column " << err.iCol
				<< " (" << pDM->GetDofDescription(err.iCol) << "); "
				"aborting..." << std::endl);
			throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
		}
		catch (NonlinearSolver::ConvergenceOnSolution) {
			bSolConv = true;
		}
		catch (EndOfSimulation& eos) {
			silent_cerr("Simulation ended during the first dummy step:\n"
				<< eos.what() << "\n");
			return;
		}

		SAFEDELETE(pFirstFictitiousStep);
		pFirstFictitiousStep = 0;

      		dRefTimeStep = dCurrTimeStep;
      		dTime += dRefTimeStep;

#if 0
		/* don't sum up the derivatives error */
      		dTotErr += dTest;
#endif
      		iTotIter += iStIter;

      		if (mbdyn_stop_at_end_of_time_step()) {
	 		/*
			 * Fa l'output della soluzione delle derivate iniziali
			 * ed esce
			 */
#ifdef DEBUG_FICTITIOUS
	   		pDM->Output(0, dTime, dCurrTimeStep, true);
#endif /* DEBUG_FICTITIOUS */
	 		Out << "Interrupted during first dummy step." << std::endl;
	 		throw ErrInterrupted(MBDYN_EXCEPT_ARGS);
      		}

#ifdef DEBUG_FICTITIOUS
      		pDM->Output(0, dTime, dCurrTimeStep, true);
#endif /* DEBUG_FICTITIOUS */

       		/* Passi fittizi successivi */
		if (iFictitiousStepsNumber > 1) {
   			/* Setup SolutionManager(s) */
			SetupSolmans(pFictitiousSteps->GetIntegratorNumUnknownStates());
		}

      		for (int iSubStep = 2;
		     iSubStep <= iFictitiousStepsNumber;
		     iSubStep++) {
      			pDM->BeforePredict(*pX, *pXPrime,
				   	*qX[0], *qXPrime[0]);
	 		Flip();

	 		DEBUGLCOUT(MYDEBUG_FSTEPS, "Dummy step "
				   << iSubStep
				   << "; current time step: " << dCurrTimeStep
				   << std::endl);

	 		ASSERT(pFictitiousSteps!= NULL);
			try {
	 			pDM->SetTime(dTime + dCurrTimeStep, dCurrTimeStep, 0);
	 			dTest = pFictitiousSteps->Advance(this,
						dRefTimeStep,
						dCurrTimeStep/dRefTimeStep,
					 	StepIntegrator::NEWSTEP,
						qX, qXPrime, pX, pXPrime,
						iStIter, dTest, dSolTest);
			}
			catch (NonlinearSolver::NoConvergence) {
				silent_cerr("Dummy step " << iSubStep
					<< " does not converge; "
					"time step dt=" << dCurrTimeStep
					<< " cannot be reduced further; "
					"aborting..." << std::endl);
	 			pDM->Output(0, dTime, dCurrTimeStep, true);
	 			throw ErrMaxIterations(MBDYN_EXCEPT_ARGS);
			}

			catch (NonlinearSolver::ErrSimulationDiverged) {
				/*
		 		 * Mettere qui eventuali azioni speciali
		 		 * da intraprendere in caso di errore ...
		 		 */
				throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
			}
			catch (LinearSolver::ErrFactor err) {
				/*
				 * Mettere qui eventuali azioni speciali
				 * da intraprendere in caso di errore ...
				 */
				silent_cerr("Dummy step " << iSubStep
					<< " failed because no pivot element "
					"could be found for column " << err.iCol
					<< " (" << pDM->GetDofDescription(err.iCol) << "); "
					"aborting..." << std::endl);
				throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
			}
			catch (NonlinearSolver::ConvergenceOnSolution) {
				bSolConv = true;
			}
			catch (EndOfSimulation& eos) {
				silent_cerr("Simulation ended during the dummy steps:\n"
					<< eos.what() << "\n");
				return;
			}

#if 0
			/* don't sum up the derivatives error */
      			dTotErr += dTest;
#endif
      			iTotIter += iStIter;

#ifdef DEBUG
	 		if (DEBUG_LEVEL(MYDEBUG_FSTEPS)) {
	    			Out << "Step " << lStep
					<< " time " << dTime+dCurrTimeStep
					<< " step " << dCurrTimeStep
					<< " iterations " << iStIter
					<< " error " << dTest << std::endl;
	 		}
#endif /* DEBUG */

	 		DEBUGLCOUT(MYDEBUG_FSTEPS, "Substep " << iSubStep
				   << " of step " << lStep
				   << " has been successfully completed "
				   "in " << iStIter << " iterations"
				   << std::endl);

	 		if (mbdyn_stop_at_end_of_time_step()) {
				/* */
#ifdef DEBUG_FICTITIOUS
	    			pDM->Output(0, dTime, dCurrTimeStep);
#endif /* DEBUG_FICTITIOUS */
	    			Out << "Interrupted during dummy steps."
					<< std::endl;
				throw ErrInterrupted(MBDYN_EXCEPT_ARGS);
			}

			dTime += dRefTimeStep;
      		}
		if (outputMsg()) {
      			Out << "# Initial solution after dummy steps "
				"at time " << dTime
				<< " performed in " << iStIter
				<< " iterations with " << dTest
				<< " error" << std::endl;
		}

      		DEBUGLCOUT(MYDEBUG_FSTEPS,
			   "Fictitious steps have been successfully completed "
			   "in " << iStIter << " iterations" << std::endl);
#ifdef USE_EXTERNAL
	/* comunica che gli ultimi dati inviati sono la condizione iniziale */
		External::SendInitial();
#endif /* USE_EXTERNAL */
   	} /* Fine dei passi fittizi */

   	/* Output delle "condizioni iniziali" */
   	pDM->Output(0, dTime, dCurrTimeStep);

        if (outputMsg()) {
  	 	Out
			<< "# Key for lines starting with \"Step\":"
				<< std::endl
			<< "# Step Time TStep NIter ResErr SolErr SolConv"
				<< std::endl
			<< "Step " << 0
     			<< " " << dTime+dCurrTimeStep
     			<< " " << dCurrTimeStep
     			<< " " << iStIter
     			<< " " << dTest
			<< " " << dSolTest
			<< " " << bSolConv
			<< std::endl;
	}


   	if (eAbortAfter == AFTER_DUMMY_STEPS) {
      		Out << "End of dummy steps; no simulation is required."
			<< std::endl;
		return;

   	} else if (mbdyn_stop_at_end_of_time_step()) {
      		/* Fa l'output della soluzione ed esce */
      		Out << "Interrupted during dummy steps." << std::endl;
      		throw ErrInterrupted(MBDYN_EXCEPT_ARGS);
   	}

	/* primo passo regolare */

#ifdef USE_EXTERNAL
	/* il prossimo passo e' un regular */
	pNLS->SetExternal(External::REGULAR);
#endif /* USE_EXTERNAL */

   	lStep = 1; /* Resetto di nuovo lStep */

   	DEBUGCOUT("Step " << lStep << " has been successfully completed "
			"in " << iStIter << " iterations" << std::endl);


   	DEBUGCOUT("Current time step: " << dCurrTimeStep << std::endl);

      	pDM->BeforePredict(*pX, *pXPrime, *qX[0], *qXPrime[0]);

	Flip();
	dRefTimeStep = dInitialTimeStep;
   	dCurrTimeStep = dRefTimeStep;

	ASSERT(pFirstRegularStep!= NULL);
	StepIntegrator::StepChange CurrStep
			= StepIntegrator::NEWSTEP;

	/* Setup SolutionManager(s) */
	SetupSolmans(pFirstRegularStep->GetIntegratorNumUnknownStates(), true);
IfFirstStepIsToBeRepeated:
	try {
		pDM->SetTime(dTime + dCurrTimeStep, dCurrTimeStep, 1);
		dTest = pFirstRegularStep->Advance(this, dRefTimeStep,
				dCurrTimeStep/dRefTimeStep, CurrStep,
				qX, qXPrime, pX, pXPrime,
				iStIter, dTest, dSolTest);
	}
	catch (NonlinearSolver::NoConvergence) {
		if (dCurrTimeStep > dMinTimeStep) {
			/* Riduce il passo */
			CurrStep = StepIntegrator::REPEATSTEP;
			doublereal dOldCurrTimeStep = dCurrTimeStep;
			dCurrTimeStep = NewTimeStep(dOldCurrTimeStep,
						iStIter,
						CurrStep);
			if (dCurrTimeStep < dOldCurrTimeStep) {
				DEBUGCOUT("Changing time step"
					" from " << dOldCurrTimeStep
					<< " to " << dCurrTimeStep
					<< " during first step after "
					<< iStIter << " iterations"
					<< std::endl);
	    			goto IfFirstStepIsToBeRepeated;
			}
	 	}

	    	silent_cerr("Maximum iterations number "
			<< pRegularSteps->GetIntegratorMaxIters()
			<< " has been reached during "
			"first step (time=" << dTime << "); "
			<< "time step dt=" << dCurrTimeStep
			<< " cannot be reduced further; "
			"aborting..." << std::endl);
	    	pDM->Output(0, dTime, dCurrTimeStep, true);

		throw Solver::ErrMaxIterations(MBDYN_EXCEPT_ARGS);
      	}
	catch (NonlinearSolver::ErrSimulationDiverged) {
		/*
		 * Mettere qui eventuali azioni speciali
		 * da intraprendere in caso di errore ...
		 */

		throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
	}
	catch (LinearSolver::ErrFactor err) {
		/*
		 * Mettere qui eventuali azioni speciali
		 * da intraprendere in caso di errore ...
		 */
		silent_cerr("First step failed because no pivot element "
			"could be found for column " << err.iCol
			<< " (" << pDM->GetDofDescription(err.iCol) << "); "
			"aborting..." << std::endl);
		throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
	}
	catch (NonlinearSolver::ConvergenceOnSolution) {
		bSolConv = true;
	}
	catch (EndOfSimulation& eos) {
		silent_cerr("Simulation ended during the first regular step:\n"
			<< eos.what() << "\n");
		return;
	}

	SAFEDELETE(pFirstRegularStep);
	pFirstRegularStep = 0;

   	pDM->Output(lStep, dTime + dCurrTimeStep, dCurrTimeStep);

   	if (mbdyn_stop_at_end_of_time_step()) {
      		/* Fa l'output della soluzione al primo passo ed esce */
      		Out << "Interrupted during first step." << std::endl;
      		throw ErrInterrupted(MBDYN_EXCEPT_ARGS);
   	}

	if (outputMsg()) {
      		Out
			<< "Step " << lStep
			<< " " << dTime+dCurrTimeStep
			<< " " << dCurrTimeStep
			<< " " << iStIter
			<< " " << dTest
			<< " " << dSolTest
			<< " " << bSolConv
			<< std::endl;
	}

	bSolConv = false;

   	dRefTimeStep = dCurrTimeStep;
   	dTime += dRefTimeStep;

   	dTotErr += dTest;
   	iTotIter += iStIter;

   	if (EigAn.bAnalysis
		&& EigAn.OneAnalysis.dTime <= dTime
		&& !EigAn.OneAnalysis.bDone)
	{
	 	Eig();
		EigAn.OneAnalysis.bDone = true;
      	}

	if (pRTSolver) {
		pRTSolver->Init();
	}

    	/* Altri passi regolari */
	ASSERT(pRegularSteps != NULL);

	/* Setup SolutionManager(s) */
	SetupSolmans(pRegularSteps->GetIntegratorNumUnknownStates(), true);
      	while (true) {

		StepIntegrator::StepChange CurrStep
				= StepIntegrator::NEWSTEP;

      		if (pDM->EndOfSimulation() || dTime >= dFinalTime) {
			if (pRTSolver) {
				pRTSolver->StopCommanded();
			}
			silent_cout("End of simulation at time "
				<< dTime << " after "
				<< lStep << " steps;" << std::endl
				<< "output in file \"" << sOutputFileName << "\"" << std::endl
				<< "total iterations: " << iTotIter << std::endl
				<< "total Jacobian matrices: " << pNLS->TotalAssembledJacobian() << std::endl
				<< "total error: " << dTotErr << std::endl);

			if (pRTSolver) {
				pRTSolver->Log();
			}

			return;

		} else if (pRTSolver && pRTSolver->IsStopCommanded()) {
			silent_cout("Simulation is stopped by RTAI task" << std::endl
				<< "Simulation ended at time "
				<< dTime << " after "
				<< lStep << " steps;" << std::endl
				<< "total iterations: " << iTotIter << std::endl
				<< "total Jacobian matrices: " << pNLS->TotalAssembledJacobian() << std::endl
				<< "total error: " << dTotErr << std::endl);
			pRTSolver->Log();
			return;

      		} else if (mbdyn_stop_at_end_of_time_step()
#ifdef USE_MPI
				|| (MPI_Finalized(&mpi_finalize), mpi_finalize)
#endif /* USE_MPI */
				)
		{
			if (pRTSolver) {
				pRTSolver->StopCommanded();
			}

	 		silent_cout("Interrupted!" << std::endl
	   			<< "Simulation ended at time "
				<< dTime << " after "
				<< lStep << " steps;" << std::endl
				<< "total iterations: " << iTotIter << std::endl
				<< "total Jacobian matrices: " << pNLS->TotalAssembledJacobian() << std::endl
				<< "total error: " << dTotErr << std::endl);

			if (pRTSolver) {
				pRTSolver->Log();
			}

	 		throw ErrInterrupted(MBDYN_EXCEPT_ARGS);
      		}

      		lStep++;
      		pDM->BeforePredict(*pX, *pXPrime, *qX[0], *qXPrime[0]);

		Flip();

		if (pRTSolver) {
			pRTSolver->Wait();
		}

IfStepIsToBeRepeated:
		try {

			pDM->SetTime(dTime + dCurrTimeStep, dCurrTimeStep, lStep);
			dTest = pRegularSteps->Advance(this, dRefTimeStep,
					dCurrTimeStep/dRefTimeStep, CurrStep,
					qX, qXPrime, pX, pXPrime, iStIter,
					dTest, dSolTest);
		}
		catch (NonlinearSolver::NoConvergence) {
			if (dCurrTimeStep > dMinTimeStep) {
				/* Riduce il passo */
				CurrStep = StepIntegrator::REPEATSTEP;
				doublereal dOldCurrTimeStep = dCurrTimeStep;
				dCurrTimeStep = NewTimeStep(dOldCurrTimeStep,
						iStIter,
						CurrStep);
				if (dCurrTimeStep < dOldCurrTimeStep) {
					DEBUGCOUT("Changing time step"
						" from " << dOldCurrTimeStep
						<< " to " << dCurrTimeStep
						<< " during step "
						<< lStep << " after "
						<< iStIter << " iterations"
						<< std::endl);
					goto IfStepIsToBeRepeated;
				}
	    		}

			silent_cerr("Max iterations number "
				<< pRegularSteps->GetIntegratorMaxIters()
				<< " has been reached during"
				" step " << lStep << "; "
				"time step dt=" << dCurrTimeStep
				<< " cannot be reduced further; "
				"aborting..." << std::endl);
	       		throw ErrMaxIterations(MBDYN_EXCEPT_ARGS);
		}
		catch (NonlinearSolver::ErrSimulationDiverged) {
			if (dCurrTimeStep > dMinTimeStep) {
				/* Riduce il passo */
				CurrStep = StepIntegrator::REPEATSTEP;
				doublereal dOldCurrTimeStep = dCurrTimeStep;
				dCurrTimeStep = NewTimeStep(dOldCurrTimeStep,
						iStIter,
						CurrStep);
				if (dCurrTimeStep < dOldCurrTimeStep) {
					DEBUGCOUT("Changing time step"
						" from " << dOldCurrTimeStep
						<< " to " << dCurrTimeStep
						<< " during step "
						<< lStep << " after "
						<< iStIter << " iterations"
						<< std::endl);
					goto IfStepIsToBeRepeated;
				}
	    		}

			silent_cerr("Simulation diverged after "
				<< iStIter << " iterations, before "
				"reaching max iteration number "
				<< pRegularSteps->GetIntegratorMaxIters()
				<< " during step " << lStep << "; "
				"time step dt=" << dCurrTimeStep
				<< " cannot be reduced further; "
				"aborting..." << std::endl);
			throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
		}
		catch (LinearSolver::ErrFactor err) {
			/*
			 * Mettere qui eventuali azioni speciali
			 * da intraprendere in caso di errore ...
			 */
			silent_cerr("Simulation failed because no pivot element "
				"could be found for column " << err.iCol
				<< " (" << pDM->GetDofDescription(err.iCol) << ") "
				"after " << iStIter << " iterations "
				"during step " << lStep << "; "
				"aborting..." << std::endl);
			throw SimulationDiverged(MBDYN_EXCEPT_ARGS);
		}
		catch (NonlinearSolver::ConvergenceOnSolution) {
			bSolConv = true;
		}
		catch (EndOfSimulation& eos) {
			silent_cerr("Simulation ended during a regular step:\n"
				<< eos.what() << "\n");
#ifdef USE_MPI
			MBDynComm.Abort(0);
#endif /* USE_MPI */
			if (pRTSolver) {
				pRTSolver->StopCommanded();
			}

	 		silent_cout("Simulation ended at time "
				<< dTime << " after "
				<< lStep << " steps;" << std::endl
				<< "total iterations: " << iTotIter << std::endl
				<< "total Jacobian matrices: " << pNLS->TotalAssembledJacobian() << std::endl
				<< "total error: " << dTotErr << std::endl);

			if (pRTSolver) {
				pRTSolver->Log();
			}

	 		return;
      		}

	      	dTotErr += dTest;
      		iTotIter += iStIter;

      		pDM->Output(lStep, dTime + dCurrTimeStep, dCurrTimeStep);

		if (outputMsg()) {
      			Out << "Step " << lStep
				<< " " << dTime+dCurrTimeStep
				<< " " << dCurrTimeStep
				<< " " << iStIter
				<< " " << dTest
				<< " " << dSolTest
				<< " " << bSolConv
				<< std::endl;
		}

     	 	DEBUGCOUT("Step " << lStep
			<< " has been successfully completed "
			"in " << iStIter << " iterations" << std::endl);

	      	dRefTimeStep = dCurrTimeStep;
      		dTime += dRefTimeStep;

		bSolConv = false;

#ifdef __HACK_POD__
		if (bPOD && dTime >= pod.dTime) {
			if (++iPODStep == pod.iSteps) {
				/* output degli stati su di una riga */
#ifdef __HACK_POD_BINARY__
	       			PodOut.write((char *)&pX, iNumDofs*sizeof(doublereal));
	       			PodOut.write((char *)&pXPrime, iNumDofs*sizeof(doublereal));
#else /* !__HACK_POD_BINARY__ */
				PodOut << pX->dGetCoef(1);
				for (integer j = 1; j < iNumDofs; j++) {
					PodOut << "  " << pX->dGetCoef(j+1);
                       		}
                       		PodOut << std::endl;
#endif /* ! __HACK_POD_BINARY__ */
			}
                     	iPODFrames++;
                      	iPODStep = 0;
		}

		if (iPODFrames >= pod.iFrames){
			bPOD = false;
		}
#endif /*__HACK_POD__ */

      		if (EigAn.bAnalysis
			&& EigAn.OneAnalysis.dTime <= dTime
			&& !EigAn.OneAnalysis.bDone)
		{
			Eig();
			EigAn.OneAnalysis.bDone = true;
		}

      		/* Calcola il nuovo timestep */
      		dCurrTimeStep = NewTimeStep(dCurrTimeStep, iStIter, CurrStep);
		DEBUGCOUT("Current time step: " << dCurrTimeStep << std::endl);
   	} // while (true)  END OF ENDLESS-LOOP
}  // Solver::Run()

/* Distruttore */
Solver::~Solver(void)
{
   	DEBUGCOUTFNAME("Solver::~Solver");

   	if (sInputFileName != NULL) {
      		SAFEDELETEARR(sInputFileName);
   	}

   	if (sOutputFileName != NULL) {
      		SAFEDELETEARR(sOutputFileName);
   	}

	if (!qX.empty()) {
		for (int ivec = 0; ivec < iNumPreviousVectors; ivec++) {
   			if (qX[ivec] != NULL) {
				SAFEDELETE(qX[ivec]);
				SAFEDELETE(qXPrime[ivec]);
			}
		}
	}

	if (pX) {
		SAFEDELETE(pX);
	}
	if (pXPrime) {
		SAFEDELETE(pXPrime);
	}

   	if (pdWorkSpace != NULL) {
      		SAFEDELETEARR(pdWorkSpace);
   	}

   	if (pDM != NULL) {
      		SAFEDELETE(pDM);
	}

	if (pRTSolver) {
		SAFEDELETE(pRTSolver);
	}

   	if (pDerivativeSteps) {
   		SAFEDELETE(pDerivativeSteps);
	}

   	if (pFirstFictitiousStep) {
   		SAFEDELETE(pFirstFictitiousStep);
	}

	if (pFictitiousSteps) {
		SAFEDELETE(pFictitiousSteps);
	}

   	if (pFirstRegularStep) {
   		SAFEDELETE(pFirstRegularStep);
	}

	if (pRegularSteps) {
		SAFEDELETE(pRegularSteps);
	}

	if (pSM) {
		SAFEDELETE(pSM);
	}

	if (pNLS) {
		SAFEDELETE(pNLS);
	}
}

/* Nuovo delta t */
doublereal
Solver::NewTimeStep(doublereal dCurrTimeStep,
				 integer iPerformedIters,
				 StepIntegrator::StepChange Why)
{
   	DEBUGCOUTFNAME("Solver::NewTimeStep");

   	switch (CurrStrategy) {
    	case NOCHANGE:
       		return dCurrTimeStep;

	case CHANGE: {
		doublereal dNewStep = pStrategyChangeDrive->dGet(dTime);
		if (Why == StepIntegrator::REPEATSTEP
			&& dNewStep == dCurrTimeStep)
		{
			return dMinTimeStep/2.;
		}
		return std::max(std::min(dNewStep, dMaxTimeStep), dMinTimeStep);
		}

    	case FACTOR:
       		if (Why == StepIntegrator::REPEATSTEP) {
	  		if (dCurrTimeStep*StrategyFactor.dReductionFactor
	      			> dMinTimeStep)
			{
	     			if (bLastChance == true) {
					bLastChance = false;
	     			}
	     			iStepsAfterReduction = 0;
	     			return dCurrTimeStep*StrategyFactor.dReductionFactor;

	  		} else {
	     			if (bLastChance == false) {
					bLastChance = true;
					return dMinTimeStep;
	     			} else {
					/*
					 * Fuori viene intercettato
					 * il valore illegale
					 */
					return dCurrTimeStep*StrategyFactor.dReductionFactor;
	     			}
	  		}
       		}

       		if (Why == StepIntegrator::NEWSTEP) {
	  		iStepsAfterReduction++;
	  		iStepsAfterRaise++;

			iWeightedPerformedIters = (10*iPerformedIters + 9*iWeightedPerformedIters)/10;

// 			std::cerr << iPerformedIters << " " << StrategyFactor.iMaxIters << " "
// 				<< iPerformedIters << " " << StrategyFactor.iMinIters  << " "
// 				<< iStepsAfterReduction << " " << StrategyFactor.iStepsBeforeReduction << " "
// 				<< iStepsAfterRaise << " " << StrategyFactor.iStepsBeforeRaise << " "
// 				<< dCurrTimeStep << " " << dMaxTimeStep << "\n";
			if (iPerformedIters > StrategyFactor.iMaxIters) {
	     			iStepsAfterReduction = 0;
				bLastChance = false;
	     			return std::max(dCurrTimeStep*StrategyFactor.dReductionFactor, dMinTimeStep);

			} else if (iPerformedIters <= StrategyFactor.iMinIters
	      			&& iStepsAfterReduction > StrategyFactor.iStepsBeforeReduction
				&& iStepsAfterRaise > StrategyFactor.iStepsBeforeRaise
				&& dCurrTimeStep < dMaxTimeStep)
			{
	     			iStepsAfterRaise = 0;
				iWeightedPerformedIters = 0;
	     			return dCurrTimeStep*StrategyFactor.dRaiseFactor;
	  		}
	  		return dCurrTimeStep;
       		}
       		break;

    	default:
       		silent_cerr("You shouldn't have reached this point!" << std::endl);
       		throw Solver::ErrGeneric(MBDYN_EXCEPT_ARGS);
   	}

   	return dCurrTimeStep;
}

/*scrive il contributo al file di restart*/
std::ostream &
Solver::Restart(std::ostream& out,DataManager::eRestart type) const
{

	out << "begin: initial value;" << std::endl;
	switch(type) {
	case DataManager::ATEND:
		out << "  #  initial time: " << pDM->dGetTime() << ";"
			<< std::endl
			<< "  #  final time: " << dFinalTime << ";"
			<< std::endl
			<< "  #  time step: " << dInitialTimeStep << ";"
			<< std::endl;
		break;
	case DataManager::ITERATIONS:
	case DataManager::TIME:
	case DataManager::TIMES:
		out << "  initial time: " << pDM->dGetTime()<< ";" << std::endl
			<< "  final time: " << dFinalTime << ";" << std::endl
			<< "  time step: " << dInitialTimeStep << ";"
			<< std::endl;
		break;
	default:
		ASSERT(0);
	}

	out << "  method: ";
	switch(RegularType) {
	case INT_CRANKNICOLSON:
		out << "Crank Nicolson; " << std::endl;
		break;
	case INT_MS2:
		out << "ms, ";
		pRhoRegular->Restart(out) << ", ";
		pRhoAlgebraicRegular->Restart(out) << ";" << std::endl;
		break;
	case INT_HOPE:
		out << "hope, " << pRhoRegular->Restart(out) << ", "
			<< pRhoAlgebraicRegular->Restart(out) << ";"
			<< std::endl;
		break;

	case INT_THIRDORDER:
		out << "thirdorder, ";
		if (!pRhoRegular)
			out << "ad hoc;" << std::endl;
			else
			pRhoRegular->Restart(out) << ";" << std::endl;
		break;
	case INT_IMPLICITEULER:
		out << "implicit euler;" << std::endl;
		break;
	default:
		ASSERT(0);
	}

	out << "  max iterations: " << pRegularSteps->GetIntegratorMaxIters()
		<< ";" << std::endl
		<< "  tolerance: " << pRegularSteps->GetIntegratorDTol();
	switch(ResTest) {
	case NonlinearSolverTest::NORM:
		out << ", test, norm" ;
		break;
	case NonlinearSolverTest::MINMAX:
		out << ", test, minmax" ;
		break;
	case NonlinearSolverTest::NONE:
		NO_OP;
	default:
		silent_cerr("unhandled nonlinear solver test type" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	if (bScale) {
		out << ", scale"
			<< ", " << pRegularSteps->GetIntegratorDSolTol();
	}

	switch (SolTest) {
	case NonlinearSolverTest::NORM:
		out << ", test, norm" ;
		break;
	case NonlinearSolverTest::MINMAX:
		out << ", test, minmax" ;
		break;
	case NonlinearSolverTest::NONE:
		NO_OP;
	default:
		ASSERT(0);
	}
	out
		<< ";" << std::endl
		<< "  derivatives max iterations: " << pDerivativeSteps->GetIntegratorMaxIters() << ";" << std::endl
		<< "  derivatives tolerance: " << pDerivativeSteps->GetIntegratorDTol() << ";" << std::endl
		<< "  derivatives coefficient: " << dDerivativesCoef << ";" << std::endl;
	if (iFictitiousStepsNumber) {
		out
			<< "  fictitious steps max iterations: " << pFictitiousSteps->GetIntegratorMaxIters() << ";" << std::endl
			<< "  fictitious steps tolerance: " << pFictitiousSteps->GetIntegratorDTol() << ";" << std::endl;
	}
	out
		<< "  fictitious steps number: " << iFictitiousStepsNumber << ";" << std::endl
		<< "  fictitious steps ratio: " << dFictitiousStepsRatio << ";" << std::endl;
	switch (NonlinearSolverType) {
	case NonlinearSolver::MATRIXFREE:
		out << "  #  nonlinear solver: matrix free;" << std::endl;
		break;
	case NonlinearSolver::NEWTONRAPHSON:
	default :
		out << "  nonlinear solver: newton raphson";
		if (!bTrueNewtonRaphson) {
			out << ", modified, " << iIterationsBeforeAssembly;
			if (bKeepJac) {
				out << ", keep jacobian matrix";
			}
			if (bHonorJacRequest) {
				out << ", honor element requests";
			}
		}
		out << ";" << std::endl;
	}
	out << "  solver: ";
	RestartLinSol(out, CurrLinearSolver);
	out << "end: initial value;" << std::endl << std::endl;
	return out;
}

/* Dati dell'integratore */
void
Solver::ReadData(MBDynParser& HP)
{
   	DEBUGCOUTFNAME("MultiStepIntegrator::ReadData");

   	/* parole chiave */
   	const char* sKeyWords[] = {
      		"begin",
		"initial" "value",
		"multistep",		/* deprecated */
		"end",

		"initial" "time",
		"final" "time",
		"time" "step",
		"min" "time" "step",
		"max" "time" "step",
		"tolerance",
		"max" "iterations",
		"modify" "residual" "test",

		/* DEPRECATED */
		"fictitious" "steps" "number",
		"fictitious" "steps" "ratio",
		"fictitious" "steps" "tolerance",
		"fictitious" "steps" "max" "iterations",
		/* END OF DEPRECATED */

		"dummy" "steps" "number",
		"dummy" "steps" "ratio",
		"dummy" "steps" "tolerance",
		"dummy" "steps" "max" "iterations",

		"abort" "after",
			"input",
			"assembly",
			"derivatives",

			/* DEPRECATED */ "fictitious" "steps" /* END OF DEPRECATED */ ,
			"dummy" "steps",

		"output",
			"none",
			"iterations",
			"residual",
			"solution",
			/* DEPRECATED */ "jacobian" /* END OF DEPRECATED */ ,
			"jacobian" "matrix",
			"bailout",
			"messages",

		"method",
		/* DEPRECATED */ "fictitious" "steps" "method" /* END OF DEPRECATED */ ,
		"dummy" "steps" "method",

		"Crank" "Nicolson",
		/* DEPRECATED */ "Crank" "Nicholson" /* END OF DEPRECATED */ ,
			/* DEPRECATED */ "nostro" /* END OF DEPRECATED */ ,
			"ms",
			"hope",
			"bdf",
			"thirdorder",
			"implicit" "euler",

		"derivatives" "coefficient",
		"derivatives" "tolerance",
		"derivatives" "max" "iterations",

		/* DEPRECATED */
		"true",
		"modified",
		/* END OF DEPRECATED */

		"strategy",
			"factor",
			"no" "change",
			"change",

		"pod",
		"eigen" "analysis",	/* EXPERIMENTAL */

		/* DEPRECATED */
		"solver",
		"interface" "solver",
		/* END OF DEPRECATED */
		"linear" "solver",
		"interface" "linear" "solver",

		/* DEPRECATED */
		"preconditioner",
		/* END OF DEPRECATED */

		"nonlinear" "solver",
			"default",
			"newton" "raphson",
			"matrix" "free",
				"bicgstab",
				"gmres",
					/* DEPRECATED */ "full" "jacobian" /* END OF DEPRECATED */ ,
					"full" "jacobian" "matrix",

		/* RTAI stuff */
		"real" "time",

		/* multithread stuff */
		"threads",

		NULL
   	};

   	/* enum delle parole chiave */
   	enum KeyWords {
      		UNKNOWN = -1,
		BEGIN = 0,
		INITIAL_VALUE,
		MULTISTEP,
		END,

		INITIALTIME,
		FINALTIME,
		TIMESTEP,
		MINTIMESTEP,
		MAXTIMESTEP,
		TOLERANCE,
		MAXITERATIONS,
		MODIFY_RES_TEST,

		FICTITIOUSSTEPSNUMBER,
		FICTITIOUSSTEPSRATIO,
		FICTITIOUSSTEPSTOLERANCE,
		FICTITIOUSSTEPSMAXITERATIONS,

		DUMMYSTEPSNUMBER,
		DUMMYSTEPSRATIO,
		DUMMYSTEPSTOLERANCE,
		DUMMYSTEPSMAXITERATIONS,

		ABORTAFTER,
		INPUT,
		ASSEMBLY,
		DERIVATIVES,
		FICTITIOUSSTEPS,
		DUMMYSTEPS,

		OUTPUT,
			NONE,
			ITERATIONS,
			RESIDUAL,
			SOLUTION,
			JACOBIAN,
			JACOBIANMATRIX,
			BAILOUT,
			MESSAGES,

		METHOD,
		FICTITIOUSSTEPSMETHOD,
		DUMMYSTEPSMETHOD,
		CRANKNICOLSON,
		CRANKNICHOLSON,
		NOSTRO,
		MS,
		HOPE,
		BDF,
		THIRDORDER,
		IMPLICITEULER,

		DERIVATIVESCOEFFICIENT,
		DERIVATIVESTOLERANCE,
		DERIVATIVESMAXITERATIONS,

		/* DEPRECATED */
		NR_TRUE,
		MODIFIED,
		/* END OF DEPRECATED */

		STRATEGY,
		STRATEGYFACTOR,
		STRATEGYNOCHANGE,
		STRATEGYCHANGE,

		POD,
		EIGENANALYSIS,

		/* DEPRECATED */
		SOLVER,
		INTERFACESOLVER,
		/* END OF DEPRECATED */
		LINEARSOLVER,
		INTERFACELINEARSOLVER,

		/* DEPRECATED */
		PRECONDITIONER,
		/* END OF DEPRECATED */

		NONLINEARSOLVER,
			DEFAULT,
			NEWTONRAPHSON,
			MATRIXFREE,
				BICGSTAB,
				GMRES,
					FULLJACOBIAN,
					FULLJACOBIANMATRIX,

		/* RTAI stuff */
		REALTIME,

		THREADS,

		LASTKEYWORD
   	};

   	/* tabella delle parole chiave */
   	KeyTable K(HP, sKeyWords);

   	/* legge i dati della simulazione */
   	if (KeyWords(HP.GetDescription()) != BEGIN) {
      		silent_cerr("Error: <begin> expected at line "
			<< HP.GetLineData() << "; aborting..." << std::endl);
      		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
   	}

   	switch (KeyWords(HP.GetWord())) {
	case MULTISTEP:
		pedantic_cout("warning: \"begin: multistep\" is deprecated; "
			"use \"begin: initial value;\" instead." << std::endl);
	case INITIAL_VALUE:
		break;

	default:
      		silent_cerr("Error: \"begin: initial value;\" expected at line "
			<< HP.GetLineData() << "; aborting..." << std::endl);
      		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
   	}

   	bool bMethod(false);
   	bool bFictitiousStepsMethod(false);

	/* dati letti qui ma da passare alle classi
	 *	StepIntegration e NonlinearSolver
	 */

	doublereal dTol = ::dDefaultTol;
   	doublereal dSolutionTol = 0.;
   	integer iMaxIterations = ::iDefaultMaxIterations;
	bool bModResTest = false;

        /* Dati dei passi fittizi di trimmaggio iniziale */
   	doublereal dFictitiousStepsTolerance = ::dDefaultFictitiousStepsTolerance;
   	integer iFictitiousStepsMaxIterations = ::iDefaultMaxIterations;

   	/* Dati del passo iniziale di calcolo delle derivate */

	doublereal dDerivativesTol = ::dDefaultTol;
   	integer iDerivativesMaxIterations = ::iDefaultMaxIterations;

#ifdef USE_MULTITHREAD
	bool bSolverThreads(false);
	unsigned nSolverThreads = 0;
#endif /* USE_MULTITHREAD */


   	/* Ciclo infinito */
   	while (true) {
      		KeyWords CurrKeyWord = KeyWords(HP.GetDescription());

      		switch (CurrKeyWord) {
       		case INITIALTIME:
	  		dInitialTime = HP.GetReal();
	  		DEBUGLCOUT(MYDEBUG_INPUT, "Initial time is "
				   << dInitialTime << std::endl);
	  		break;

       		case FINALTIME:
			if (HP.IsKeyWord("forever")) {
				dFinalTime = std::numeric_limits<doublereal>::max();
			} else {
		  		dFinalTime = HP.GetReal();
			}
	  		DEBUGLCOUT(MYDEBUG_INPUT, "Final time is "
				   << dFinalTime << std::endl);

	  		if(dFinalTime <= dInitialTime) {
	     			silent_cerr("warning: final time " << dFinalTime
	       				<< " is less than initial time "
					<< dInitialTime << ';' << std::endl
	       				<< "this will cause the simulation"
					" to abort" << std::endl);
			}
	  		break;

       		case TIMESTEP:
	  		dInitialTimeStep = HP.GetReal();
	  		DEBUGLCOUT(MYDEBUG_INPUT, "Initial time step is "
				   << dInitialTimeStep << std::endl);

	  		if (dInitialTimeStep == 0.) {
	     			silent_cerr("warning, null initial time step"
					" is not allowed" << std::endl);
	  		} else if (dInitialTimeStep < 0.) {
	     			dInitialTimeStep = -dInitialTimeStep;
				silent_cerr("warning, negative initial time step"
					" is not allowed;" << std::endl
					<< "its modulus " << dInitialTimeStep
					<< " will be considered" << std::endl);
			}
			break;

       		case MINTIMESTEP:
	  		dMinTimeStep = HP.GetReal();
	  		DEBUGLCOUT(MYDEBUG_INPUT, "Min time step is "
				   << dMinTimeStep << std::endl);

	  		if (dMinTimeStep == 0.) {
	     			silent_cerr("warning, null min time step"
					" is not allowed" << std::endl);
	     			throw ErrGeneric(MBDYN_EXCEPT_ARGS);

			} else if (dMinTimeStep < 0.) {
				silent_cerr("negative min time step"
					" is not allowed" << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	  		}
	  		break;

       		case MAXTIMESTEP:
			if (HP.IsKeyWord("unlimited")) {
				dMaxTimeStep = 0.;
			} else {
	  			dMaxTimeStep = HP.GetReal();
			}
	  		DEBUGLCOUT(MYDEBUG_INPUT, "Max time step is "
				   << dMaxTimeStep << std::endl);

	  		if (dMaxTimeStep == 0.) {
				silent_cout("no max time step limit will be"
					" considered" << std::endl);

			} else if (dMaxTimeStep < 0.) {
				silent_cerr("negative max time step"
					" is not allowed" << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	  		}
	  		break;

       		case FICTITIOUSSTEPSNUMBER:
       		case DUMMYSTEPSNUMBER:
	  		iFictitiousStepsNumber = HP.GetInt();
			if (iFictitiousStepsNumber < 0) {
				iFictitiousStepsNumber =
					::iDefaultFictitiousStepsNumber;
				silent_cerr("negative dummy steps number"
					" is illegal" << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);

			} else if (iFictitiousStepsNumber == 1) {
				silent_cerr("warning, a single dummy step"
					" may be useless" << std::endl);
	  		}

	  		DEBUGLCOUT(MYDEBUG_INPUT, "Fictitious steps number: "
		     		   << iFictitiousStepsNumber << std::endl);
	  		break;

       		case FICTITIOUSSTEPSRATIO:
       		case DUMMYSTEPSRATIO:
	  		dFictitiousStepsRatio = HP.GetReal();
	  		if (dFictitiousStepsRatio < 0.) {
				silent_cerr("negative dummy steps ratio"
					" is illegal" << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

	  		if (dFictitiousStepsRatio > 1.) {
				silent_cerr("warning, dummy steps ratio"
					" is larger than one." << std::endl
					<< "Something like 1.e-3 should"
					" be safer ..." << std::endl);
	  		}

	  		DEBUGLCOUT(MYDEBUG_INPUT, "Fictitious steps ratio: "
		     		   << dFictitiousStepsRatio << std::endl);
	  		break;

       		case FICTITIOUSSTEPSTOLERANCE:
       		case DUMMYSTEPSTOLERANCE:
	  		dFictitiousStepsTolerance = HP.GetReal();
	  		if (dFictitiousStepsTolerance <= 0.) {
				silent_cerr("negative dummy steps"
					" tolerance is illegal" << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	  		}
			DEBUGLCOUT(MYDEBUG_INPUT,
				   "Fictitious steps tolerance: "
		     		   << dFictitiousStepsTolerance << std::endl);
	  		break;

       		case ABORTAFTER: {
	  		KeyWords WhenToAbort(KeyWords(HP.GetWord()));
	  		switch (WhenToAbort) {
	   		case INPUT:
	      			eAbortAfter = AFTER_INPUT;
	      			DEBUGLCOUT(MYDEBUG_INPUT,
			 		"Simulation will abort after"
					" data input" << std::endl);
	      			break;

	   		case ASSEMBLY:
	     			eAbortAfter = AFTER_ASSEMBLY;
	      			DEBUGLCOUT(MYDEBUG_INPUT,
			 		   "Simulation will abort after"
					   " initial assembly" << std::endl);
	      			break;

	   		case DERIVATIVES:
	      			eAbortAfter = AFTER_DERIVATIVES;
	      			DEBUGLCOUT(MYDEBUG_INPUT,
			 		   "Simulation will abort after"
					   " derivatives solution" << std::endl);
	      			break;

	   		case FICTITIOUSSTEPS:
	   		case DUMMYSTEPS:
	      			eAbortAfter = AFTER_DUMMY_STEPS;
	      			DEBUGLCOUT(MYDEBUG_INPUT,
			 		   "Simulation will abort after"
					   " dummy steps solution" << std::endl);
	      			break;

	   		default:
	      			silent_cerr("Don't know when to abort,"
					" so I'm going to abort now" << std::endl);
	      			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	  		}
	  		break;
       		}

		case OUTPUT: {
			unsigned OF = OUTPUT_DEFAULT;
			bool setOutput = false;

			while (HP.IsArg()) {
				KeyWords OutputFlag(KeyWords(HP.GetWord()));
				switch (OutputFlag) {
				case NONE:
					OF = OUTPUT_NONE;
					setOutput = true;
					break;

				case ITERATIONS:
					OF |= OUTPUT_ITERS;
					break;

				case RESIDUAL:
					OF |= OUTPUT_RES;
					break;

				case SOLUTION:
					OF |= OUTPUT_SOL;
					break;

				case JACOBIAN:
				case JACOBIANMATRIX:
					OF |= OUTPUT_JAC;
					break;

				case BAILOUT:
					OF |= OUTPUT_BAILOUT;
					break;

				case MESSAGES:
					OF |= OUTPUT_MSG;
					break;

				default:
					silent_cerr("Unknown output flag "
						"at line " << HP.GetLineData()
						<< "; ignored" << std::endl);
					break;
				}
			}

			if (setOutput) {
				SetOutputFlags(OF);
			} else {
				AddOutputFlags(OF);
			}

			break;
		}

       		case METHOD: {
	  		if (bMethod) {
	     			silent_cerr("error: multiple definition"
					" of integration method at line "
					<< HP.GetLineData() << std::endl);
	     			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	  		}
	  		bMethod = true;

	  		KeyWords KMethod = KeyWords(HP.GetWord());
	  		switch (KMethod) {
	   		case CRANKNICHOLSON:
				silent_cout("warning: \"crank nicolson\" is the correct spelling" << std::endl);
	   		case CRANKNICOLSON:
				RegularType = INT_CRANKNICOLSON;
	      			break;

			case BDF:
				/* default (order 2) */
				RegularType = INT_MS2;

				if (HP.IsKeyWord("order")) {
					int iOrder = HP.GetInt();

					switch (iOrder) {
					case 1:
						RegularType = INT_IMPLICITEULER;
						break;

					case 2:
						break;

					default:
						silent_cerr("unhandled BDF order " << iOrder << std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}
				}

				if (RegularType == INT_MS2) {
					SAFENEW(pRhoRegular, NullDriveCaller);
					SAFENEW(pRhoAlgebraicRegular, NullDriveCaller);
				}
		  		break;

	   		case NOSTRO:
				  silent_cerr("integration method \"nostro\" "
						  "is deprecated; use \"ms\" "
						  "instead at line "
						  << HP.GetLineData()
						  << std::endl);
	   		case MS:
	   		case HOPE:
	      			pRhoRegular = HP.GetDriveCaller(true);

	      			pRhoAlgebraicRegular = NULL;
				if (HP.IsArg()) {
					pRhoAlgebraicRegular = HP.GetDriveCaller(true);
				} else {
					pRhoAlgebraicRegular = pRhoRegular->pCopy();
				}

	      			switch (KMethod) {
	       			case NOSTRO:
	       			case MS:
					RegularType = INT_MS2;
		  			break;

	       			case HOPE:
					RegularType = INT_HOPE;
		  			break;

	       			default:
	          			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	      			}
	      			break;

			case THIRDORDER:
				if (HP.IsKeyWord("ad" "hoc")) {
					/* do nothing */ ;
				} else {
	      				pRhoRegular = HP.GetDriveCaller(true);
				}
				RegularType = INT_THIRDORDER;
				break;

			case IMPLICITEULER:
				RegularType = INT_IMPLICITEULER;
		  		break;

	   		default:
	      			silent_cerr("Unknown integration method at line "
					<< HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	  		}
	  		break;
       		}

		case FICTITIOUSSTEPSMETHOD:
		case DUMMYSTEPSMETHOD: {
			if (bFictitiousStepsMethod) {
				silent_cerr("error: multiple definition "
					"of dummy steps integration method "
					"at line " << HP.GetLineData()
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
			bFictitiousStepsMethod = true;

			KeyWords KMethod = KeyWords(HP.GetWord());
			switch (KMethod) {
			case CRANKNICHOLSON:
				silent_cout("warning: \"crank nicolson\" is the correct spelling" << std::endl);
			case CRANKNICOLSON:
				FictitiousType = INT_CRANKNICOLSON;
				break;

			case BDF:
				/* default (order 2) */
				FictitiousType = INT_MS2;

				if (HP.IsKeyWord("order")) {
					int iOrder = HP.GetInt();

					switch (iOrder) {
					case 1:
						FictitiousType = INT_IMPLICITEULER;
						break;

					case 2:
						break;

					default:
						silent_cerr("unhandled BDF order " << iOrder << std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}
				}

				if (FictitiousType == INT_MS2) {
					SAFENEW(pRhoFictitious, NullDriveCaller);
					SAFENEW(pRhoAlgebraicFictitious, NullDriveCaller);
				}
				break;

			case NOSTRO:
			case MS:
			case HOPE:
				pRhoFictitious = HP.GetDriveCaller(true);

				if (HP.IsArg()) {
					pRhoAlgebraicFictitious = HP.GetDriveCaller(true);
				} else {
					pRhoAlgebraicFictitious = pRhoFictitious->pCopy();
				}

				switch (KMethod) {
				case NOSTRO:
				case MS:
					FictitiousType = INT_MS2;
					break;

				case HOPE:
					FictitiousType = INT_HOPE;
					break;

	       			default:
	          			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
	      			break;

	   		case THIRDORDER:
				if (HP.IsKeyWord("ad" "hoc")) {
					/* do nothing */ ;
				} else {
					pRhoFictitious = HP.GetDriveCaller(true);
				}
				FictitiousType = INT_THIRDORDER;
				break;

	   		case IMPLICITEULER:
				FictitiousType = INT_IMPLICITEULER;
				break;

			default:
				silent_cerr("Unknown integration method at line " << HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
			break;
		}

		case TOLERANCE: {
			/*
			 * residual tolerance; can be the keyword "null",
			 * which means that the convergence test
			 * will be computed on the solution, or a number
			 */
			if (HP.IsKeyWord("null")) {
				dTol = 0.;

			} else {
				dTol = HP.GetReal();
				if (dTol < 0.) {
					dTol = ::dDefaultTol;
					silent_cerr("warning, residual tolerance "
						"< 0. is illegal; "
						"using default value " << dTol
						<< std::endl);
				}
			}

			/* safe default */
			if (dTol == 0.) {
				ResTest = NonlinearSolverTest::NONE;
			}

			if (HP.IsArg()) {
				if (HP.IsKeyWord("test")) {
					if (HP.IsKeyWord("norm")) {
						ResTest = NonlinearSolverTest::NORM;
					} else if (HP.IsKeyWord("minmax")) {
						ResTest = NonlinearSolverTest::MINMAX;
					} else if (HP.IsKeyWord("none")) {
						ResTest = NonlinearSolverTest::NONE;
					} else {
						silent_cerr("unknown test "
							"method at line "
							<< HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}

					if (HP.IsKeyWord("scale")) {
						if (ResTest == NonlinearSolverTest::NONE) {
							silent_cerr("it's a nonsense "
								"to scale a disabled test; "
								"\"scale\" ignored"
								<< std::endl);
							bScale = false;
						} else {
							bScale = true;
						}
					}
				}
			}

			if (HP.IsArg()) {
				if (!HP.IsKeyWord("null")) {
					dSolutionTol = HP.GetReal();
				}

				/* safe default */
				if (dSolutionTol != 0.) {
					SolTest = NonlinearSolverTest::NORM;
				}

				if (HP.IsArg()) {
					if (HP.IsKeyWord("test")) {
						if (HP.IsKeyWord("norm")) {
							SolTest = NonlinearSolverTest::NORM;
						} else if (HP.IsKeyWord("minmax")) {
							SolTest = NonlinearSolverTest::MINMAX;
						} else if (HP.IsKeyWord("none")) {
							SolTest = NonlinearSolverTest::NONE;
						} else {
							silent_cerr("unknown test "
								"method at line "
								<< HP.GetLineData()
								<< std::endl);
							throw ErrGeneric(MBDYN_EXCEPT_ARGS);
						}
					}
				}

			} else if (dTol == 0.) {
				silent_cerr("need solution tolerance "
					"with null residual tolerance"
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			if (dSolutionTol < 0.) {
				dSolutionTol = 0.;
				silent_cerr("warning, solution tolerance "
					"< 0. is illegal; "
					"solution test is disabled"
					<< std::endl);
			}

			if (dTol == 0. && dSolutionTol == 0.) {
				silent_cerr("both residual and solution "
					"tolerances are zero" << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			DEBUGLCOUT(MYDEBUG_INPUT, "tolerance = " << dTol
					<< ", " << dSolutionTol << std::endl);
			break;
		}


		case DERIVATIVESTOLERANCE: {
			dDerivativesTol = HP.GetReal();
			if (dDerivativesTol <= 0.) {
				dDerivativesTol = ::dDefaultTol;
				silent_cerr("warning, derivatives "
					"tolerance <= 0.0 is illegal; "
					"using default value "
					<< dDerivativesTol
					<< std::endl);
			}
			DEBUGLCOUT(MYDEBUG_INPUT,
					"Derivatives tolerance = "
					<< dDerivativesTol
					<< std::endl);
			break;
		}

		case MAXITERATIONS: {
			iMaxIterations = HP.GetInt();
			if (iMaxIterations < 1) {
				iMaxIterations = ::iDefaultMaxIterations;
				silent_cerr("warning, max iterations "
					"< 1 is illegal; using default value "
					<< iMaxIterations
					<< std::endl);
			}
			DEBUGLCOUT(MYDEBUG_INPUT,
					"Max iterations = "
					<< iMaxIterations << std::endl);
			break;
		}

		case MODIFY_RES_TEST:
			if (bParallel) {
				silent_cerr("\"modify residual test\" "
					"not supported by schur data manager "
					"at line " << HP.GetLineData()
					<< "; ignored" << std::endl);
			} else {
				bModResTest = true;
				DEBUGLCOUT(MYDEBUG_INPUT,
					"Modify residual test" << std::endl);
			}
			break;

		case DERIVATIVESMAXITERATIONS: {
			iDerivativesMaxIterations = HP.GetInt();
			if (iDerivativesMaxIterations < 1) {
				iDerivativesMaxIterations = ::iDefaultMaxIterations;
				silent_cerr("warning, derivatives "
					"max iterations < 1 is illegal; "
					"using default value "
					<< iDerivativesMaxIterations
					<< std::endl);
			}
			DEBUGLCOUT(MYDEBUG_INPUT, "Derivatives "
					"max iterations = "
					<< iDerivativesMaxIterations
					<< std::endl);
			break;
		}

		case FICTITIOUSSTEPSMAXITERATIONS:
		case DUMMYSTEPSMAXITERATIONS: {
			iFictitiousStepsMaxIterations = HP.GetInt();
			if (iFictitiousStepsMaxIterations < 1) {
				iFictitiousStepsMaxIterations = ::iDefaultMaxIterations;
				silent_cerr("warning, dummy steps "
					"max iterations < 1 is illegal; "
					"using default value "
					<< iFictitiousStepsMaxIterations
					<< std::endl);
			}
			DEBUGLCOUT(MYDEBUG_INPUT, "Fictitious steps "
					"max iterations = "
					<< iFictitiousStepsMaxIterations
					<< std::endl);
			break;
		}

		case DERIVATIVESCOEFFICIENT: {
			dDerivativesCoef = HP.GetReal();
			if (dDerivativesCoef <= 0.) {
				dDerivativesCoef = ::dDefaultDerivativesCoefficient;
				silent_cerr("warning, derivatives "
					"coefficient <= 0. is illegal; "
					"using default value "
					<< dDerivativesCoef
					<< std::endl);
			}
			DEBUGLCOUT(MYDEBUG_INPUT, "Derivatives coefficient = "
					<< dDerivativesCoef << std::endl);
			break;
		}

		case NEWTONRAPHSON: {
			pedantic_cout("Newton Raphson is deprecated; use "
					"\"nonlinear solver: newton raphson "
					"[ , modified, <steps> ]\" instead"
					<< std::endl);
			KeyWords NewRaph(KeyWords(HP.GetWord()));
			switch(NewRaph) {
			case MODIFIED:
				bTrueNewtonRaphson = 0;
				if (HP.IsArg()) {
					iIterationsBeforeAssembly = HP.GetInt();
		  		} else {
		       			iIterationsBeforeAssembly = ::iDefaultIterationsBeforeAssembly;
				}
				DEBUGLCOUT(MYDEBUG_INPUT, "Modified "
						"Newton-Raphson will be used; "
						"matrix will be assembled "
						"at most after "
						<< iIterationsBeforeAssembly
						<< " iterations" << std::endl);
				break;

			default:
				silent_cerr("warning: unknown case; "
					"using default" << std::endl);

			/* no break: fall-thru to next case */
			case NR_TRUE:
				bTrueNewtonRaphson = 1;
				iIterationsBeforeAssembly = 0;
				break;
			}
			break;
		}

		case END:
			switch (KeyWords(HP.GetWord())) {
			case MULTISTEP:
				pedantic_cout("\"end: multistep;\" is deprecated; "
					"use \"end: initial value;\" instead." << std::endl);
			case INITIAL_VALUE:
				break;

			default:
				silent_cerr("\"end: initial value;\" expected "
					"at line " << HP.GetLineData()
					<< "; aborting..." << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
			goto EndOfCycle;

		case STRATEGY: {
			switch (KeyWords(HP.GetWord())) {
			case STRATEGYFACTOR: {
				CurrStrategy = FACTOR;

				/*
				 * strategy: factor ,
				 *     <reduction factor> ,
				 *     <steps before reduction> ,
				 *     <raise factor> ,
				 *     <steps before raise> ,
				 *     <min iterations> ,
				 *     <max iterations> ;
				 */

				StrategyFactor.dReductionFactor = HP.GetReal();
				if (StrategyFactor.dReductionFactor >= 1.) {
					silent_cerr("warning, "
						"illegal reduction factor "
						"at line " << HP.GetLineData()
						<< "; default value 1. "
						"(no reduction) will be used"
						<< std::endl);
					StrategyFactor.dReductionFactor = 1.;
				}

				StrategyFactor.iStepsBeforeReduction = HP.GetInt();
				if (StrategyFactor.iStepsBeforeReduction <= 0) {
					silent_cerr("warning, "
						"illegal number of steps "
						"before reduction at line "
						<< HP.GetLineData()
						<< "; default value 1 will be "
						"used (it may be dangerous)"
						<< std::endl);
					StrategyFactor.iStepsBeforeReduction = 1;
				}

				StrategyFactor.dRaiseFactor = HP.GetReal();
				if (StrategyFactor.dRaiseFactor <= 1.) {
					silent_cerr("warning, "
						"illegal raise factor at line "
						<< HP.GetLineData()
						<< "; default value 1. "
						"(no raise) will be used"
						<< std::endl);
					StrategyFactor.dRaiseFactor = 1.;
				}

				StrategyFactor.iStepsBeforeRaise = HP.GetInt();
				if (StrategyFactor.iStepsBeforeRaise <= 0) {
					silent_cerr("warning, "
						"illegal number of steps "
						"before raise at line "
						<< HP.GetLineData()
						<< "; default value 1 will be "
						"used (it may be dangerous)"
						<< std::endl);
					StrategyFactor.iStepsBeforeRaise = 1;
				}

				StrategyFactor.iMinIters = HP.GetInt();
				if (StrategyFactor.iMinIters <= 0) {
					silent_cerr("warning, "
						"illegal minimum number "
						"of iterations at line "
						<< HP.GetLineData()
						<< "; default value 0 will be "
						"used (never raise)"
						<< std::endl);
					StrategyFactor.iMinIters = 1;
				}

				if (HP.IsArg()) {
					StrategyFactor.iMaxIters = HP.GetInt();
					if (StrategyFactor.iMaxIters <= 0) {
						silent_cerr("warning, "
							"illegal mmaximim number "
							"of iterations at line "
							<< HP.GetLineData()
							<< "; default value will be "
							"used"
							<< std::endl);
					}
				}

				DEBUGLCOUT(MYDEBUG_INPUT,
						"Time step control strategy: "
						"Factor" << std::endl
						<< "Reduction factor: "
						<< StrategyFactor.dReductionFactor
						<< "Steps before reduction: "
						<< StrategyFactor.iStepsBeforeReduction
						<< "Raise factor: "
						<< StrategyFactor.dRaiseFactor
						<< "Steps before raise: "
						<< StrategyFactor.iStepsBeforeRaise
						<< "Min iterations: "
						<< StrategyFactor.iMinIters
						<< "Max iterations: "
						<< StrategyFactor.iMaxIters
						<< std::endl);
				break;
			}

			case STRATEGYNOCHANGE: {
				CurrStrategy = NOCHANGE;
				break;
			}

			case STRATEGYCHANGE: {
				CurrStrategy = CHANGE;
				pStrategyChangeDrive = HP.GetDriveCaller(true);
				break;
			}

			default:
				silent_cerr("unknown time step control "
					"strategy at line "
					<< HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
			break;
		}

		case POD:
#ifdef __HACK_POD__
			pod.dTime = HP.GetReal();

			pod.iSteps = 1;
			if (HP.IsArg()) {
				pod.iSteps = HP.GetInt();
			}

			pod.iFrames = (unsigned int)(-1);
			if (HP.IsArg()) {
				pod.iFrames = HP.GetInt();
			}

			bPOD = true;
			DEBUGLCOUT(MYDEBUG_INPUT, "POD analysis will be "
					"performed since time " << pod.dTime
					<< " for " << pod.iFrames
					<< " frames  every " << pod.iSteps
					<< " steps" << std::endl);
#else/* !__HACK_POD__ */
			silent_cerr("line " << HP.GetLineData()
				<< ": POD analysis not supported (ignored)"
				<< std::endl);
			for (; HP.IsArg();) {
				(void)HP.GetReal();
			}
#endif /* !__HACK_POD__ */
			break;

		case EIGENANALYSIS:
#ifdef USE_EIG
			// read eigenanalysis time (to be changed)
			EigAn.OneAnalysis.dTime = HP.GetReal();

			// initialize EigAn
			EigAn.OneAnalysis.bDone = false;
			EigAn.bAnalysis = true;

			// permute is the default; use "balance, no" to disable
			EigAn.uFlags = EigenAnalysis::EIG_PERMUTE;

			while (HP.IsArg()) {
				if (HP.IsKeyWord("parameter")) {
					EigAn.dParam = HP.GetReal();

				} else if (HP.IsKeyWord("output" "matrices")) {
					EigAn.uFlags |= EigenAnalysis::EIG_OUTPUT_MATRICES;

				} else if (HP.IsKeyWord("output" "full" "matrices")) {
					EigAn.uFlags |= EigenAnalysis::EIG_OUTPUT_FULL_MATRICES;

				} else if (HP.IsKeyWord("output" "sparse" "matrices")) {
					EigAn.uFlags |= EigenAnalysis::EIG_OUTPUT_SPARSE_MATRICES;

				} else if (HP.IsKeyWord("output" "eigenvectors")) {
					EigAn.uFlags |= EigenAnalysis::EIG_OUTPUT_EIGENVECTORS;

				} else if (HP.IsKeyWord("upper" "frequency" "limit")) {
					EigAn.dUpperFreq = HP.GetReal();
					if (EigAn.dUpperFreq < 0.) {
						silent_cerr("invalid \"upper frequency limit\" "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}

				} else if (HP.IsKeyWord("lower" "frequency" "limit")) {
					EigAn.dLowerFreq = HP.GetReal();
					if (EigAn.dUpperFreq < 0.) {
						silent_cerr("invalid \"lower frequency limit\" "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}

				} else if (HP.IsKeyWord("use" "lapack")) {
					if (EigAn.uFlags & EigenAnalysis::EIG_USE_MASK) {
						silent_cerr("eigenanalysis routine already selected "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}
#ifdef USE_LAPACK
					EigAn.uFlags |= EigenAnalysis::EIG_USE_LAPACK;
#else // !USE_LAPACK
					silent_cerr("\"use lapack\" "
						"needs to configure --with-lapack "
						"at line " << HP.GetLineData()
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
#endif // !USE_LAPACK

				} else if (HP.IsKeyWord("use" "arpack")) {
					if (EigAn.uFlags & EigenAnalysis::EIG_USE_MASK) {
						silent_cerr("eigenanalysis routine already selected "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}
#ifdef USE_ARPACK
					EigAn.uFlags |= EigenAnalysis::EIG_USE_ARPACK;

					EigAn.arpack.iNEV = HP.GetInt();
					if (EigAn.arpack.iNEV <= 0) {
						silent_cerr("invalid number of eigenvalues "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}

					EigAn.arpack.iNCV = HP.GetInt();
					if (EigAn.arpack.iNCV <= 0
						|| EigAn.arpack.iNCV < EigAn.arpack.iNEV + 2)
					{
						silent_cerr("invalid number of Arnoldi vectors "
							"(must be >= NEV+2) "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}

					if (EigAn.arpack.iNCV < 2*EigAn.arpack.iNEV) {
						silent_cerr("warning, possibly incorrect number of Arnoldi vectors "
							"(should be > 2*NEV) "
							"at line " << HP.GetLineData()
							<< std::endl);
					}

					EigAn.arpack.dTOL = HP.GetReal();
					if (EigAn.arpack.dTOL < 0.) {
						silent_cerr("tolerance must be non-negative "
							"at line " << HP.GetLineData()
							<< std::endl);
						EigAn.arpack.dTOL = 0.;
					}
#else // !USE_ARPACK
					silent_cerr("\"use arpack\" "
						"needs to configure --with-arpack "
						"at line " << HP.GetLineData()
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
#endif // !USE_ARPACK

				} else if (HP.IsKeyWord("use" "jdqz")) {
					if (EigAn.uFlags & EigenAnalysis::EIG_USE_MASK) {
						silent_cerr("eigenanalysis routine already selected "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}
#ifdef USE_ARPACK
					EigAn.uFlags |= EigenAnalysis::EIG_USE_JDQZ;

					EigAn.jdqz.kmax = HP.GetInt();
					if (EigAn.jdqz.kmax <= 0) {
						silent_cerr("invalid number of eigenvalues "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}

					EigAn.jdqz.jmax = HP.GetInt();
					if (EigAn.jdqz.jmax < 20
						|| EigAn.jdqz.jmax < 2*EigAn.jdqz.kmax)
					{
						silent_cerr("invalid size of the search space "
							"(must be >= 20 && >= 2*kmax) "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}

					EigAn.jdqz.jmin = 2*EigAn.jdqz.kmax;

					EigAn.jdqz.eps = HP.GetReal();
					if (EigAn.jdqz.eps <= 0.) {
						silent_cerr("tolerance must be non-negative "
							"at line " << HP.GetLineData()
							<< std::endl);
						EigAn.jdqz.eps = std::numeric_limits<doublereal>::epsilon();
					}
#else // !USE_ARPACK
					silent_cerr("\"use jdqz\" "
						"needs to configure --with-jdqz "
						"at line " << HP.GetLineData()
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
#endif // !USE_ARPACK

				} else if (HP.IsKeyWord("balance")) {
					if (HP.IsKeyWord("no")) {
						EigAn.uFlags &= ~EigenAnalysis::EIG_BALANCE;

					} else if (HP.IsKeyWord("permute")) {
						EigAn.uFlags |= EigenAnalysis::EIG_PERMUTE;

					} else if (HP.IsKeyWord("scale")) {
						EigAn.uFlags |= EigenAnalysis::EIG_SCALE;

					} else if (HP.IsKeyWord("all")) {
						EigAn.uFlags |= EigenAnalysis::EIG_BALANCE;

					} else {
						silent_cerr("unknown balance option "
							"at line " << HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}

				} else {
					silent_cerr("unknown option "
						"at line " << HP.GetLineData()
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
			}

			// lower must be less than upper
			if (EigAn.dLowerFreq > EigAn.dUpperFreq) {
				silent_cerr("upper frequency limit " << EigAn.dUpperFreq
					<< " less than lower frequency limit " << EigAn.dLowerFreq
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			// if only upper is defined, make lower equal to -upper
			if (EigAn.dLowerFreq == -1.) {
				EigAn.dLowerFreq = -EigAn.dUpperFreq;
			}

			switch (EigAn.uFlags & EigenAnalysis::EIG_USE_MASK) {
			case EigenAnalysis::EIG_USE_LAPACK:
				if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_SPARSE_MATRICES) {
					silent_cerr("sparse matrices output "
						"incompatible with lapack "
						"at line " << HP.GetLineData()
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
				if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_MATRICES) {
					EigAn.uFlags |= EigenAnalysis::EIG_OUTPUT_FULL_MATRICES;
				}
				break;

			case EigenAnalysis::EIG_USE_ARPACK:
				if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_FULL_MATRICES) {
					silent_cerr("full matrices output "
						"incompatible with arpack "
						"at line " << HP.GetLineData()
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
				if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_MATRICES) {
					EigAn.uFlags |= EigenAnalysis::EIG_OUTPUT_SPARSE_MATRICES;
				}
				break;

			case EigenAnalysis::EIG_USE_JDQZ:
				if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_FULL_MATRICES) {
					silent_cerr("full matrices output "
						"incompatible with jdqz "
						"at line " << HP.GetLineData()
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
				if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_MATRICES) {
					EigAn.uFlags |= EigenAnalysis::EIG_OUTPUT_SPARSE_MATRICES;
				}
				break;

			default:
				break;
			}

			// if an eigenanalysis routine is selected
			// or sparse matrix output is not requested,
			// force direct eigensolution
			if ((EigAn.uFlags & EigenAnalysis::EIG_USE_MASK)
				|| !(EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_SPARSE_MATRICES))
			{
				EigAn.uFlags |= EigenAnalysis::EIG_SOLVE;
			}

			// if no eigenanalysis routine is selected,
			// force the use of LAPACK's
			if ((EigAn.uFlags & EigenAnalysis::EIG_SOLVE)
				&& !(EigAn.uFlags & EigenAnalysis::EIG_USE_MASK))
			{
				EigAn.uFlags |= EigenAnalysis::EIG_USE_LAPACK;
				if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_MATRICES) {
					EigAn.uFlags |= EigenAnalysis::EIG_OUTPUT_FULL_MATRICES;
				}
			}
#else // !USE_EIG
			HP.GetReal();
			if (HP.IsKeyWord("parameter")) {
				HP.GetReal();
			}

			while (HP.IsArg()) {
				if (HP.IsKeyWord("output" "matrices")) {
					NO_OP;

				} else if (HP.IsKeyWord("output" "full" "matrices")) {
					NO_OP;

				} else if (HP.IsKeyWord("output" "sparse" "matrices")) {
					NO_OP;

				} else if (HP.IsKeyWord("output" "eigenvectors")) {
					NO_OP;

				} else if (HP.IsKeyWord("use" "lapack")) {
					NO_OP;

				} else if (HP.IsKeyWord("use" "arpack")) {
					(void)HP.GetInt();
					(void)HP.GetInt();
					(void)HP.GetReal();

				} else if (HP.IsKeyWord("lower" "frequency" "limit")) {
					(void)HP.GetReal();

				} else if (HP.IsKeyWord("upper" "frequency" "limit")) {
					(void)HP.GetReal();

				} else {
					silent_cerr("unknown option "
						"in \"eigenanalysis\" statement "
						"at line " << HP.GetLineData()
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
			}

			silent_cerr(HP.GetLineData()
				<< ": eigenanalysis not supported (ignored)"
				<< std::endl);
#endif // !USE_EIG
			break;

		case SOLVER:
			silent_cerr("\"solver\" keyword at line "
					<< HP.GetLineData()
					<< " is deprecated; "
					"use \"linear solver\" instead"
					<< std::endl);
		case LINEARSOLVER:
			ReadLinSol(CurrLinearSolver, HP);
			break;

		case INTERFACESOLVER:
			silent_cerr("\"interface solver\" keyword at line "
					<< HP.GetLineData()
					<< " is deprecated; "
					"use \"interface linear solver\" "
					"instead" << std::endl);
		case INTERFACELINEARSOLVER:
			ReadLinSol(CurrIntSolver, HP, true);

#ifndef USE_MPI
			silent_cerr("Interface solver only allowed "
				"when compiled with MPI support" << std::endl);
#endif /* ! USE_MPI */
			break;

		case NONLINEARSOLVER:
			switch (KeyWords(HP.GetWord())) {
			case DEFAULT:
				NonlinearSolverType = NonlinearSolver::DEFAULT;
				break;

			case NEWTONRAPHSON:
				NonlinearSolverType = NonlinearSolver::NEWTONRAPHSON;
				break;

			case MATRIXFREE:
				NonlinearSolverType = NonlinearSolver::MATRIXFREE;
				break;

			default:
				silent_cerr("unknown nonlinear solver "
					"at line " << HP.GetLineData()
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			switch (NonlinearSolverType) {
			case NonlinearSolver::NEWTONRAPHSON:
    				bTrueNewtonRaphson = true;
				bKeepJac = false;
    				iIterationsBeforeAssembly = 0;

				if (HP.IsKeyWord("modified")) {
					bTrueNewtonRaphson = false;
					iIterationsBeforeAssembly = HP.GetInt();

					if (HP.IsKeyWord("keep" "jacobian")) {
						pedantic_cout("Use of deprecated \"keep jacobian\" "
							"at line " << HP.GetLineData() << std::endl);
						bKeepJac = true;

					} else if (HP.IsKeyWord("keep" "jacobian" "matrix")) {
						bKeepJac = true;
					}

					DEBUGLCOUT(MYDEBUG_INPUT, "modified "
							"Newton-Raphson "
							"will be used; "
							"matrix will be "
							"assembled at most "
							"after "
							<< iIterationsBeforeAssembly
							<< " iterations"
							<< std::endl);
					if (HP.IsKeyWord("honor" "element" "requests")) {
						bHonorJacRequest = true;
						DEBUGLCOUT(MYDEBUG_INPUT,
								"honor elements' "
								"request to update "
								"the preconditioner"
								<< std::endl);
					}
				}
				break;

			case NonlinearSolver::MATRIXFREE:
				switch (KeyWords(HP.GetWord())) {
				case DEFAULT:
					MFSolverType = MatrixFreeSolver::DEFAULT;
					break;


				case BICGSTAB:
					MFSolverType = MatrixFreeSolver::BICGSTAB;
					break;

				case GMRES:
					MFSolverType = MatrixFreeSolver::GMRES;
					break;

				default:
					silent_cerr("unknown iterative "
						"solver at line "
						<< HP.GetLineData()
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}

				if (HP.IsKeyWord("tolerance")) {
					dIterTol = HP.GetReal();
					DEBUGLCOUT(MYDEBUG_INPUT,"inner "
							"iterative solver "
							"tolerance: "
							<< dIterTol
							<< std::endl);
				}

				if (HP.IsKeyWord("steps")) {
					iIterativeMaxSteps = HP.GetInt();
					DEBUGLCOUT(MYDEBUG_INPUT, "maximum "
							"number of inner "
							"steps for iterative "
							"solver: "
							<< iIterativeMaxSteps
							<< std::endl);
				}

				if (HP.IsKeyWord("tau")) {
					dIterertiveTau = HP.GetReal();
					DEBUGLCOUT(MYDEBUG_INPUT,
							"tau scaling "
							"coefficient "
							"for iterative "
							"solver: "
							<< dIterertiveTau
							<< std::endl);
				}

				if (HP.IsKeyWord("eta")) {
					dIterertiveEtaMax = HP.GetReal();
					DEBUGLCOUT(MYDEBUG_INPUT, "maximum "
							"eta coefficient "
							"for iterative "
							"solver: "
	  						<< dIterertiveEtaMax
							<< std::endl);
				}

				if (HP.IsKeyWord("preconditioner")) {
					KeyWords KPrecond = KeyWords(HP.GetWord());
					switch (KPrecond) {
					case FULLJACOBIAN:
					case FULLJACOBIANMATRIX:
						PcType = Preconditioner::FULLJACOBIANMATRIX;
						if (HP.IsKeyWord("steps")) {
							iPrecondSteps = HP.GetInt();
							DEBUGLCOUT(MYDEBUG_INPUT,
									"number of steps "
									"before recomputing "
									"the preconditioner: "
									<< iPrecondSteps
									<< std::endl);
						}
						if (HP.IsKeyWord("honor" "element" "requests")) {
							bHonorJacRequest = true;
							DEBUGLCOUT(MYDEBUG_INPUT,
									"honor elements' "
									"request to update "
									"the preconditioner"
									<< std::endl);
						}
						break;

						/* add other preconditioners
						 * here */

					default:
						silent_cerr("unknown "
							"preconditioner "
							"at line "
							<< HP.GetLineData()
							<< std::endl);
						throw ErrGeneric(MBDYN_EXCEPT_ARGS);
					}
					break;
				}
				break;

			default:
				ASSERT(0);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
			break;

		case REALTIME:
			pRTSolver = ReadRTSolver(this, HP);
			break;

		case THREADS:
			if (HP.IsKeyWord("auto")) {
#ifdef USE_MULTITHREAD
				int n = get_nprocs();
				/* sanity checks ... */
				if (n <= 0) {
					silent_cerr("got " << n << " CPUs "
							"at line "
							<< HP.GetLineData()
							<< std::endl);
					nThreads = 1;
				} else {
					nThreads = n;
				}
#else /* ! USE_MULTITHREAD */
				silent_cerr("configure with "
						"--enable-multithread "
						"for multithreaded assembly"
						<< std::endl);
#endif /* ! USE_MULTITHREAD */

			} else if (HP.IsKeyWord("disable")) {
#ifdef USE_MULTITHREAD
				nThreads = 1;
#endif /* USE_MULTITHREAD */

			} else {
				bool bAssembly = false;
				bool bSolver = false;
				bool bAll = true;
				unsigned nt;

				if (HP.IsKeyWord("assembly")) {
					bAll = false;
					bAssembly = true;

				} else if (HP.IsKeyWord("solver")) {
					bAll = false;
					bSolver = true;
				}

				nt = HP.GetInt();

#ifdef USE_MULTITHREAD
				if (bAll || bAssembly) {
					nThreads = nt;
				}

				if (bAll || bSolver) {
					bSolverThreads = true;
					nSolverThreads = nt;
				}
#else /* ! USE_MULTITHREAD */
				silent_cerr("configure with "
						"--enable-multithread "
						"for multithreaded assembly"
						<< std::endl);
#endif /* ! USE_MULTITHREAD */
			}
			break;


		default:
			silent_cerr("unknown description at line "
				<< HP.GetLineData() << "; aborting..."
				<< std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
	}

EndOfCycle: /* esce dal ciclo di lettura */

   	switch (CurrStrategy) {
    	case FACTOR:
		if (StrategyFactor.iMaxIters <= StrategyFactor.iMinIters) {
			silent_cerr("warning, "
				<< "strategy maximum number "
				<< "of iterations "
				<< "is <= minimum: "
				<< StrategyFactor.iMaxIters << " <= "
				<< StrategyFactor.iMinIters << "; "
				<< "the maximum global iteration value "
				<< iMaxIterations << " "
				<< "will be used"
				<< std::endl);
			StrategyFactor.iMaxIters = iMaxIterations;
		}
		if (dMaxTimeStep == ::dDefaultMaxTimeStep) {
			silent_cerr("warning, "
				<< "maximum time step not set and strategy "
				<< "factor selected:\n"
				<< "the initial time step value will be used as "
				<< "maximum time step: "
				<< "max time step = "
				<< dInitialTimeStep
				<< std::endl);
			dMaxTimeStep = dInitialTimeStep;
		}
		if (dMinTimeStep == dDefaultMinTimeStep) {
			silent_cerr("warning, "
				<< "minimum time step not set and strategy "
				<< "factor selected:\n"
				<< "the initial time step value will be used as "
				<< "minimum time step: "
				<< "min time step = "
				<< dInitialTimeStep
				<< std::endl);
			dMinTimeStep = dInitialTimeStep;
		}
		if (dMinTimeStep == dMaxTimeStep) {
			silent_cerr("error, "
				<< "minimum and maximum time step are equal, but "
				<< "strategy factor has been selected:\n"
				<< "this is almost meaningless"
				<< std::endl);
		}
		if (dMinTimeStep > dMaxTimeStep) {
			silent_cerr("error, "
				<< "minimum and maximum time step are equal, but "
				<< "strategy factor has been selected:\n"
				<< "this is meaningless - bailing out"
				<< std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
		break;

	case CHANGE:
		if (dMinTimeStep > dMaxTimeStep) {
			silent_cerr("inconsistent min/max time step"
				<< std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
		break;

	default:
		if (dMinTimeStep != ::dDefaultMinTimeStep) {
			silent_cerr("\"min time step\" only allowed with variable time step (ignored)." <<std::endl);
		}
		dMinTimeStep = dInitialTimeStep;

		if (dMaxTimeStep != dDefaultMaxTimeStep) {
			silent_cerr("\"max time step\" only allowed with variable time step (ignored)." <<std::endl);
		}
		dMaxTimeStep = dInitialTimeStep;

		break;
	}

	if (dFinalTime < dInitialTime) {
		eAbortAfter = AFTER_ASSEMBLY;
	}

	if (dFinalTime == dInitialTime) {
		eAbortAfter = AFTER_DERIVATIVES;
	}

	/* Metodo di integrazione di default */
	if (!bMethod) {
		ASSERT(RegularType == INT_UNKNOWN);

		/* FIXME: maybe we should use a better value
		 * like 0.6; however, BDF should be conservative */
		SAFENEW(pRhoRegular, NullDriveCaller);

		/* DriveCaller per Rho asintotico per variabili algebriche */
		pRhoAlgebraicRegular = pRhoRegular->pCopy();

		RegularType = INT_MS2;
	}

	/* Metodo di integrazione di default */
	if (iFictitiousStepsNumber && !bFictitiousStepsMethod) {
		ASSERT(FictitiousType == INT_UNKNOWN);

		SAFENEW(pRhoFictitious, NullDriveCaller);

		/* DriveCaller per Rho asintotico per variabili algebriche */
		pRhoAlgebraicFictitious = pRhoFictitious->pCopy();

		FictitiousType = INT_MS2;
	}

	/* costruzione dello step solver derivative */
	SAFENEWWITHCONSTRUCTOR(pDerivativeSteps,
			DerivativeSolver,
			DerivativeSolver(dDerivativesTol,
				0.,
				dInitialTimeStep*dDerivativesCoef,
				iDerivativesMaxIterations,
				bModResTest));

	/* First step prediction must always be Crank-Nicolson for accuracy */
	if (iFictitiousStepsNumber) {
		SAFENEWWITHCONSTRUCTOR(pFirstFictitiousStep,
				CrankNicolsonIntegrator,
				CrankNicolsonIntegrator(dFictitiousStepsTolerance,
					0.,
					iFictitiousStepsMaxIterations,
					bModResTest));

		/* costruzione dello step solver fictitious */
		switch (FictitiousType) {
		case INT_CRANKNICOLSON:
			SAFENEWWITHCONSTRUCTOR(pFictitiousSteps,
					CrankNicolsonIntegrator,
					CrankNicolsonIntegrator(dFictitiousStepsTolerance,
						0.,
						iFictitiousStepsMaxIterations,
						bModResTest));
			break;

		case INT_MS2:
  			SAFENEWWITHCONSTRUCTOR(pFictitiousSteps,
					MultistepSolver,
					MultistepSolver(dFictitiousStepsTolerance,
						0.,
						iFictitiousStepsMaxIterations,
						pRhoFictitious,
						pRhoAlgebraicFictitious,
						bModResTest));
			break;

		case INT_HOPE:
			SAFENEWWITHCONSTRUCTOR(pFictitiousSteps,
					HopeSolver,
					HopeSolver(dFictitiousStepsTolerance,
						dSolutionTol,
						iFictitiousStepsMaxIterations,
						pRhoFictitious,
						pRhoAlgebraicFictitious,
						bModResTest));
			break;

		case INT_THIRDORDER:
			if (pRhoFictitious == 0) {
				SAFENEWWITHCONSTRUCTOR(pFictitiousSteps,
						AdHocThirdOrderIntegrator,
						AdHocThirdOrderIntegrator(dFictitiousStepsTolerance,
							dSolutionTol,
							iFictitiousStepsMaxIterations,
							bModResTest));
			} else {
				SAFENEWWITHCONSTRUCTOR(pFictitiousSteps,
						TunableThirdOrderIntegrator,
						TunableThirdOrderIntegrator(dFictitiousStepsTolerance,
							dSolutionTol,
							iFictitiousStepsMaxIterations,
							pRhoFictitious,
							bModResTest));
			}
			break;

		case INT_IMPLICITEULER:
			SAFENEWWITHCONSTRUCTOR(pFictitiousSteps,
					ImplicitEulerIntegrator,
					ImplicitEulerIntegrator(dFictitiousStepsTolerance,
						dSolutionTol, iFictitiousStepsMaxIterations,
						bModResTest));
			break;

		default:
			silent_cerr("unknown dummy steps integration method"
				<< std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			break;
		}
	}

	SAFENEWWITHCONSTRUCTOR(pFirstRegularStep,
			CrankNicolsonIntegrator,
			CrankNicolsonIntegrator(dTol,
				dSolutionTol,
				iMaxIterations,
				bModResTest));

	/* costruzione dello step solver per i passi normali */
	switch (RegularType) {
	case INT_CRANKNICOLSON:
		SAFENEWWITHCONSTRUCTOR(pRegularSteps,
			CrankNicolsonIntegrator,
			CrankNicolsonIntegrator(dTol,
				dSolutionTol,
				iMaxIterations,
				bModResTest));
		break;

	case INT_MS2:
  		SAFENEWWITHCONSTRUCTOR(pRegularSteps,
				MultistepSolver,
				MultistepSolver(dTol,
					dSolutionTol,
					iMaxIterations,
					pRhoRegular,
					pRhoAlgebraicRegular,
					bModResTest));
		break;

	case INT_HOPE:
		SAFENEWWITHCONSTRUCTOR(pRegularSteps,
				HopeSolver,
				HopeSolver(dTol,
					dSolutionTol,
					iMaxIterations,
					pRhoRegular,
					pRhoAlgebraicRegular,
					bModResTest));
		break;

	case INT_THIRDORDER:
		if (pRhoRegular == 0) {
			SAFENEWWITHCONSTRUCTOR(pRegularSteps,
					AdHocThirdOrderIntegrator,
					AdHocThirdOrderIntegrator(dTol,
						dSolutionTol,
						iMaxIterations,
						bModResTest));
		} else {
			SAFENEWWITHCONSTRUCTOR(pRegularSteps,
					TunableThirdOrderIntegrator,
					TunableThirdOrderIntegrator(dTol,
						dSolutionTol,
						iMaxIterations,
						pRhoRegular,
						bModResTest));
		}
		break;

	case INT_IMPLICITEULER:
		SAFENEWWITHCONSTRUCTOR(pRegularSteps,
				ImplicitEulerIntegrator,
				ImplicitEulerIntegrator(dTol,
					dSolutionTol,
					iMaxIterations,
					bModResTest));
		break;

	default:
		silent_cerr("Unknown integration method" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		break;
	}

#ifdef USE_MULTITHREAD
	if (bSolverThreads) {
		if (CurrLinearSolver.SetNumThreads(nSolverThreads)) {
			silent_cerr("linear solver "
					<< CurrLinearSolver.GetSolverName()
					<< " does not support "
					"threaded solution" << std::endl);
		}
	}
#endif /* USE_MULTITHREAD */
}

static int
do_eig(const doublereal& b, const doublereal& re,
	const doublereal& im, const doublereal& h,
	doublereal& sigma, doublereal& omega,
	doublereal& csi, doublereal& freq)
{
	// denominator
	doublereal d = re + b;
	d *= d;
	d += im*im;
	d *= h/2.;

	// real & imag
	sigma = (re*re - b*b + im*im)/d;
	omega = 2.*b*im/d;

	// frequency and damping factor
	if (im != 0.) {
		d = sigma*sigma + omega*omega;
		if (d > std::numeric_limits<doublereal>::epsilon()) {
			csi = -100*sigma/sqrt(d);

		} else {
			csi = 0.;
		}

		freq = omega/(2*M_PI);

	} else {
		if (std::abs(sigma) < std::numeric_limits<doublereal>::epsilon()) {
			csi = 0.;

		} else {
			csi = -100.*copysign(1, sigma);
		}

		freq = 0.;
	}

	return 0;
}

// Writes eigenvalues to the .out file in human-readable form
static void
output_eigenvalues(const VectorHandler *pBeta,
	const VectorHandler& R, const VectorHandler& I,
	const doublereal& dShiftR,
	DataManager* pDM,
	const Solver::EigenAnalysis *pEA,
	integer iLow, integer iHigh,
	std::vector<bool>& vOut)
{
	std::ostream& Out = pDM->GetOutFile();

	/* Output? */
	Out << "Mode n. " "  " "    Real    " "   " "    Imag    " "  " "    " "   Damp %   " "  Freq Hz" << std::endl;

	integer iNVec = R.iGetSize();

	for (int iCnt = 1; iCnt <= iNVec; iCnt++) {
		doublereal b = pBeta ? (*pBeta)(iCnt) : 1.;
		doublereal re = R(iCnt) + dShiftR;
		doublereal im = I(iCnt);
		doublereal sigma;
		doublereal omega;
		doublereal csi;
		doublereal freq;

		if (iCnt < iLow || iCnt > iHigh) {
			vOut[iCnt - 1] = false;
			continue;
		}

		const doublereal& h = pEA->dParam;
		do_eig(b, re, im, h, sigma, omega, csi, freq);

		if (freq < pEA->dLowerFreq || freq > pEA->dUpperFreq) {
			vOut[iCnt - 1] = false;
			continue;
		}

		vOut[iCnt - 1] = true;

		Out << std::setw(8) << iCnt << ": "
			<< std::setw(12) << sigma << " + " << std::setw(12) << omega << " j";

		if (fabs(csi) > std::numeric_limits<doublereal>::epsilon()) {
			Out << "    " << std::setw(12) << csi;
		} else {
			Out << "    " << std::setw(12) << 0.;
		}

		Out << "    " << std::setw(12) << freq;

		Out << std::endl;
	}
}

// Writes eigenvalues and eigenvectors
// in a form suitable for handling with octave/matlab
static void
output_eigenvectors(const VectorHandler *pBeta,
	const VectorHandler& R, const VectorHandler& I,
	const doublereal& dShiftR,
	const MatrixHandler *pVL, const MatrixHandler& VR,
	DataManager* pDM,
	const Solver::EigenAnalysis *pEA,
	const std::vector<bool>& vOut,
	std::ostream& o)
{
	static const char signs[] = { '-', '+' };
	int isign;

	integer iSize = VR.iGetNumRows();
	integer iNVec = VR.iGetNumCols();

	// alphar, alphai, beta
	o
		<< "% alphar, alphai, beta" << std::endl
		<< "alpha = [";

	for (integer r = 1; r <= iNVec; r++) {
		if (!vOut[r - 1]) {
			continue;
		}

		o
			<< std::setw(24) << R(r) + dShiftR
			<< std::setw(24) << I(r)
			<< std::setw(24) << (pBeta ? (*pBeta)(r) : 1.)
			<< ";" << std::endl;
	}
	o << "];" << std::endl;

	if (pVL) {
		// VL
		o
			<< "% left eigenvectors" << std::endl
			<< "VL = [" << std::endl;
		for (integer r = 1; r <= iSize; r++) {
			for (integer c = 1; c <= iNVec; c++) {
				if (!vOut[c - 1]) {
					continue;
				}

				if (I(c) != 0.) {
					ASSERT(c < iNVec);
					ASSERT(I(c) > 0.);

					doublereal re = (*pVL)(r, c);
					doublereal im = (*pVL)(r, c + 1);
					if (im < 0 ) {
						isign = 0;
						im = -im;

					} else {
						isign = 1;
					}
					o
						<< std::setw(24) << re << signs[isign] << "i*" << std::setw(24) << im;
					if (vOut[c]) {
						o
							<< std::setw(24) << re << signs[1-isign] << "i*" << std::setw(24) << im;
					}
					c++;

				} else {
					o
						<< std::setw(24) << (*pVL)(r, c);
				}
			}

			if (r < iSize) {
				o << ";" << std::endl;

			} else {
				o << "];" << std::endl;
			}
		}
	}

	// VR
	o
		<< "% right eigenvectors" << std::endl
		<< "VR = [" << std::endl;
	for (integer r = 1; r <= iSize; r++) {
		for (integer c = 1; c <= iNVec; c++) {
			if (!vOut[c - 1]) {
				continue;
			}

			if (I(c) != 0.) {
				ASSERT(c < iNVec);
				ASSERT(I(c) > 0.);

				doublereal re = VR(r, c);
				doublereal im = VR(r, c + 1);
				if (im < 0 ) {
					isign = 0;
					im = -im;

				} else {
					isign = 1;
				}
				o
					<< std::setw(24) << re << signs[isign] << "i*" << std::setw(24) << im;
				if (vOut[c]) {
					o
						<< std::setw(24) << re << signs[1-isign] << "i*" << std::setw(24) << im;
				}
				c++;

			} else {
				o
					<< std::setw(24) << VR(r, c);
			}
		}

		if (r < iSize) {
			o << ";" << std::endl;

		} else {
			o << "];" << std::endl;
		}
	}
}

#ifdef USE_LAPACK
// Computes eigenvalues and eigenvectors using LAPACK's
// generalized non-symmetric eigenanalysis
static void
eig_lapack(const MatrixHandler* pMatA, const MatrixHandler* pMatB,
	DataManager *pDM, Solver::EigenAnalysis *pEA, std::ostream& o)
{
	const FullMatrixHandler& MatA = dynamic_cast<const FullMatrixHandler &>(*pMatA);
	const FullMatrixHandler& MatB = dynamic_cast<const FullMatrixHandler &>(*pMatB);

	char sB[2] = "N";
	if ((pEA->uFlags & Solver::EigenAnalysis::EIG_BALANCE)
		== Solver::EigenAnalysis::EIG_BALANCE)
	{
		sB[0] = 'B';

	} else if (pEA->uFlags & Solver::EigenAnalysis::EIG_PERMUTE) {
		sB[0] = 'P';

	} else if (pEA->uFlags & Solver::EigenAnalysis::EIG_SCALE) {
		sB[0] = 'S';
	}

	char sL[2] = "V";
	char sR[2] = "V";
	char sS[2] = "N";

	// iNumDof is a member, set after dataman constr.
	integer iSize = MatA.iGetNumRows();

	// Minimum workspace size. To be improved.
	// NOTE: optimal iWorkSize is computed by dggev() and dggevx()
	// when called with iWorkSize = -1.
	// The computed value is stored in WorkVec[0].
	integer iWorkSize = -1;
	integer iMinWorkSize = -1;
	integer iInfo = 0;

	integer iILO = 1, iIHI = iSize;
	doublereal dABNRM = -1., dBBNRM = -1.;

	doublereal dDmy;
	integer	iDmy;
	logical lDmy;
	doublereal dWV;
	if (sB[0] == 'N') {
		iMinWorkSize = 8*iSize;

		__FC_DECL__(dggev)(
			sL,		// JOBVL
			sR,		// JOBVR
			&iSize,		// N
			&dDmy,		// A
			&iSize,		// LDA
			&dDmy,		// B
			&iSize,		// LDB
			&dDmy,		// ALPHAR
			&dDmy,		// ALPHAI
			&dDmy,		// BETA
			&dDmy,		// VL
			&iSize,		// LDVL
			&dDmy,		// VR
			&iSize,		// LDVR
			&dWV,		// WORK
			&iWorkSize,	// LWORK
			&iInfo);

	} else {
		iMinWorkSize = 6*iSize;

		__FC_DECL__(dggevx)(
			sB,		// BALANC
			sL,		// JOBVL
			sR,		// JOBVR
			sS,		// SENSE
			&iSize,		// N
			&dDmy,		// A
			&iSize,		// LDA
			&dDmy,		// B
			&iSize,		// LDB
			&dDmy,		// ALPHAR
			&dDmy,		// ALPHAI
			&dDmy,		// BETA
			&dDmy,		// VL
			&iSize,		// LDVL
			&dDmy,		// VR
			&iSize,		// LDVR
			&iILO,		// ILO
			&iIHI,		// IHI
			&dDmy,		// LSCALE
			&dDmy,		// RSCALE
			&dABNRM,	// ABNRM
			&dBBNRM,	// BBNRM
			&dDmy,		// RCONDE
			&dDmy,		// RCONDV
			&dWV,		// WORK
			&iWorkSize,	// LWORK
			&iDmy,		// IWORK
			&lDmy,		// BWORK
			&iInfo);
	}

	if (iInfo != 0) {
		silent_cerr("dggev[x]() query for worksize failed "
			"INFO=" << iInfo << std::endl);
		iInfo = 0;
	}

	iWorkSize = (integer)dWV;
	if (iWorkSize < iMinWorkSize) {
		silent_cerr("dggev[x]() asked for a worksize " << iWorkSize
			<< " less than the minimum, " << iMinWorkSize
			<< "; using the minimum" << std::endl);
		iWorkSize = iMinWorkSize;
	}

	// Workspaces
	// 	2 matrices iSize x iSize
	//	5 vectors iSize x 1
	//	1 vector iWorkSize x 1
	doublereal* pd = NULL;
	int iTmpSize = 2*(iSize*iSize) + 3*iSize + iWorkSize;
	if (sB[0] != 'N') {
		iTmpSize += 2*iSize;
	}
	SAFENEWARR(pd, doublereal, iTmpSize);
#if defined HAVE_MEMSET
	memset(pd, 0, iTmpSize*sizeof(doublereal));
#else // !HAVE_MEMSET
	for (int iCnt = iTmpSize; iCnt-- > 0; ) {
		pd[iCnt] = 0.;
	}
#endif // !HAVE_MEMSET

	// 2 pointer arrays iSize x 1 for the matrices
	doublereal** ppd = NULL;
	SAFENEWARR(ppd, doublereal*, 2*iSize);

	// Data Handlers
	doublereal* pdTmp = pd;
	doublereal** ppdTmp = ppd;

	FullMatrixHandler MatVL(pdTmp, ppdTmp, iSize*iSize, iSize, iSize);
	pdTmp += iSize*iSize;
	ppdTmp += iSize;

	FullMatrixHandler MatVR(pdTmp, ppdTmp, iSize*iSize, iSize, iSize);
	pdTmp += iSize*iSize;

	MyVectorHandler AlphaR(iSize, pdTmp);
	pdTmp += iSize;

	MyVectorHandler AlphaI(iSize, pdTmp);
	pdTmp += iSize;

	MyVectorHandler Beta(iSize, pdTmp);
	pdTmp += iSize;

	MyVectorHandler LScale;
	MyVectorHandler RScale;
	if (sB[0] != 'N') {
		LScale.Attach(iSize, pdTmp);
		pdTmp += iSize;

		RScale.Attach(iSize, pdTmp);
		pdTmp += iSize;
	}

	MyVectorHandler WorkVec(iWorkSize, pdTmp);

	// Eigenanalysis
	// NOTE: according to lapack's documentation, dgegv() is deprecated
	// in favour of dggev()... I find dgegv() a little bit faster (10%?)
	// for typical problems (N ~ 1000).
	if (sB[0] == 'N') {
		__FC_DECL__(dggev)(
			sL,			// JOBVL
			sR,			// JOBVR
			&iSize,			// N
			MatA.pdGetMat(),	// A
			&iSize,			// LDA
			MatB.pdGetMat(),	// B
			&iSize,			// LDB
			AlphaR.pdGetVec(),	// ALPHAR
			AlphaI.pdGetVec(),	// ALPHAI
			Beta.pdGetVec(),	// BETA
			MatVL.pdGetMat(),	// VL
			&iSize,			// LDVL
			MatVR.pdGetMat(),	// VR
			&iSize,			// LDVR
			WorkVec.pdGetVec(),	// WORK
			&iWorkSize,		// LWORK
			&iInfo);		// INFO

	} else {
		std::vector<integer> iIWORK(iSize + 6);

		__FC_DECL__(dggevx)(
			sB,			// BALANCE
			sL,			// JOBVL
			sR,			// JOBVR
			sS,			// SENSE
			&iSize,			// N
			MatA.pdGetMat(),	// A
			&iSize,			// LDA
			MatB.pdGetMat(),	// B
			&iSize,			// LDB
			AlphaR.pdGetVec(),	// ALPHAR
			AlphaI.pdGetVec(),	// ALPHAI
			Beta.pdGetVec(),	// BETA
			MatVL.pdGetMat(),	// VL
			&iSize,			// LDVL
			MatVR.pdGetMat(),	// VR
			&iSize,			// LDVR
			&iILO,			// ILO
			&iIHI,			// IHI
			LScale.pdGetVec(),	// LSCALE
			RScale.pdGetVec(),	// RSCALE
			&dABNRM,		// ABNRM
			&dBBNRM,		// BBNRM
			&dDmy,			// RCONDE
			&dDmy,			// RCONDV
			WorkVec.pdGetVec(),	// WORK
			&iWorkSize,		// LWORK
			&iIWORK[0],		// IWORK
			&lDmy,			// BWORK
			&iInfo);		// INFO
	}

	std::ostream& Out = pDM->GetOutFile();
	Out << "Info: " << iInfo << ", ";

	if (iInfo == 0) {
		// = 0:  successful exit
		Out << "success"
			<< " BALANC=\"" << sB << "\""
			<< " ILO=" << iILO
			<< " IHI=" << iIHI
			<< " ABNRM=" << dABNRM
			<< " BBNRM=" << dBBNRM
			<< std::endl;

	} else if (iInfo < 0) {
		const char *arg[] = {
			"JOBVL",
			"JOBVR",
			"N",
			"A",
			"LDA",
			"B",
			"LDB",
			"ALPHAR",
			"ALPHAI",
			"BETA",
			"VL",
			"LDVL",
			"VR",
			"LDVR",
			"WORK",
			"LWORK",
			"INFO",
			NULL
		};

		const char *argx[] = {
			"BALANCE",
			"JOBVL",
			"JOBVR",
			"SENSE",
			"N",
			"A",
			"LDA",
			"B",
			"LDB",
			"ALPHAR",
			"ALPHAI",
			"BETA",
			"VL",
			"LDVL",
			"VR",
			"LDVR",
			"ILO",
			"IHI",
			"LSCALE",
			"RSCALE",
			"ABNRM",
			"BBNRM",
			"RCONDE",
			"RCONDV",
			"WORK",
			"LWORK",
			"IWORK",
			"BWORK",
			"INFO",
			NULL
		};

		const char **argv = (sB[0] == 'N' ? arg : argx );

		// < 0:  if INFO = -i, the i-th argument had an illegal value.
		Out << "argument #" << -iInfo
			<< " (" << argv[-iInfo - 1] << ") "
			<< "was passed an illegal value" << std::endl;

	} else if (iInfo > 0 && iInfo <= iSize) {
		/* = 1,...,N:
		 * The QZ iteration failed.  No eigenvectors have been
		 * calculated, but ALPHAR(j), ALPHAI(j), and BETA(j)
		 * should be correct for j=INFO+1,...,N. */
		Out << "the QZ iteration failed, but eigenvalues "
			<< iInfo + 1 << "->" << iSize << "should be correct"
			<< std::endl;

	} else if (iInfo > iSize) {
		const char* const sErrs[] = {
			"DHGEQZ (other than QZ iteration failed in DHGEQZ)",
			"DTGEVC",
			NULL
		};

		Out << "error return from " << sErrs[iInfo - iSize - 1]
			<< std::endl;
	}

	std::vector<bool> vOut(iSize);
	output_eigenvalues(&Beta, AlphaR, AlphaI, 0., pDM, pEA, iILO, iIHI, vOut);

	if (pEA->uFlags & Solver::EigenAnalysis::EIG_OUTPUT_EIGENVECTORS) {
		output_eigenvectors(&Beta, AlphaR, AlphaI, 0.,
			&MatVL, MatVR, pDM, pEA, vOut, o);
	}

	SAFEDELETEARR(pd);
	SAFEDELETEARR(ppd);
}
#endif // USE_LAPACK

#ifdef USE_ARPACK
// Computes eigenvalues and eigenvectors using ARPACK's
// canonical non-symmetric eigenanalysis
static void
eig_arpack(const MatrixHandler* pMatA, SolutionManager* pSM,
	DataManager *pDM, Solver::EigenAnalysis *pEA, std::ostream& o)
{
	NaiveSparsePermSolutionManager<Colamd_ordering>& sm
		= dynamic_cast<NaiveSparsePermSolutionManager<Colamd_ordering> &>(*pSM);
	const NaiveMatrixHandler& MatA = dynamic_cast<const NaiveMatrixHandler &>(*pMatA);

	// shift
	doublereal SIGMAR = 0.;
	doublereal SIGMAI = 0.;

	// arpack-related vars
	integer IDO;		// 0 at first iteration; then set by dnaupd
	const char *BMAT;	// 'I' for standard problem
	integer N;		// size of problem
	const char *WHICH;	// "SM" to request smallest eigenvalues
	integer NEV;		// number of eigenvalues
	doublereal TOL;		// -1 to use machine precision
	std::vector<doublereal> RESID;	// residual vector (ignored if IDO==0)
	integer NCV;		// number of vectors in subspace
	std::vector<doublereal> V;	// Schur basis
	integer LDV;		// leading dimension of V (==N!)
	integer IPARAM[11] = { 0 };
	integer IPNTR[14] = { 0 };
	std::vector<doublereal> WORKD;
	std::vector<doublereal> WORKL;
	integer LWORKL;
	integer INFO;

	IDO = 0;
	BMAT = "I";
	N = MatA.iGetNumRows();
	WHICH = "SM";
	NEV = pEA->arpack.iNEV;
	TOL = pEA->arpack.dTOL;
	RESID.resize(N, 0.);
	NCV = pEA->arpack.iNCV;
	V.resize(N*NCV, 0.);
	LDV = N;
	IPARAM[0] = 1;
	IPARAM[2] = 300;		// configurable?
	IPARAM[3] = 1;
	IPARAM[6] = 1;			// mode 1: canonical problem
	WORKD.resize(3*N, 0.);
	LWORKL = 3*NCV*NCV + 6*NCV;
	WORKL.resize(LWORKL, 0.);
	INFO = 0;

	int cnt = 0;
	do {
		__FC_DECL__(dnaupd)(&IDO, &BMAT[0], &N, &WHICH[0], &NEV,
			&TOL, &RESID[0], &NCV, &V[0], &LDV, &IPARAM[0], &IPNTR[0],
			&WORKD[0], &WORKL[0], &LWORKL, &INFO);

#if 0
		std::cout << "cnt=" << cnt << ": IDO=" << IDO << ", INFO=" << INFO << std::endl;
#endif

		// compute Y = OP*X
		MyVectorHandler X(N, &WORKD[IPNTR[0] - 1]);
		MyVectorHandler Y(N, &WORKD[IPNTR[1] - 1]);

		/*
		 * NOTE: we are solving the problem

			MatB * X * Lambda = MatA * X

		 * and we want to focus on Ritz parameters Lambda
		 * as close as possible to (1., 0.), which maps
		 * to (0., 0.) in continuous time.
		 *
		 * We are casting the problem in the form

			X * Alpha = A * X

		 * by putting the problem in canonical form

			X * Lambda = MatB \ MatA * X

		 * and then subtracting a shift Sigma = (1., 0) after :

			X * Lambda - X = MatB \ MatA * X - X

			X * (Lambda - 1.) = (MatB \ MatA - I) * X

		 * so

			Alpha = Lambda - 1.

			A = MatB \ MatA - I

		 * and the sequence of operations for Y = A * X is

			X' = MatA * X
			X'' = MatB \ X'
			Y = X'' - X

		 * the eigenvalues need to be modified by adding 1.
		 */
		

		MatA.MatVecMul(*sm.pResHdl(), X);
		sm.Solve();
		*sm.pSolHdl() -= X;

		Y = *sm.pSolHdl();

#define CNT (100)
		cnt++;
		if (!(cnt % CNT)) {
			silent_cerr("\r" "cnt=" << cnt);
		}

		if (mbdyn_stop_at_end_of_iteration()) {
			silent_cerr((cnt >= CNT ? "\n" : "")
				<< "ARPACK: interrupted" << std::endl);
			return;
		}
	} while (IDO == 1 || IDO == -1);

	silent_cerr("\r" "cnt=" << cnt << std::endl);

	// NOTE: improve diagnostics
	if (INFO < 0) {
		silent_cerr("ARPACK error after " << cnt << " iterations; "
			"IDO=" << IDO << ", INFO=" << INFO << std::endl);
		return;
	}

	switch (INFO) {
	case 1:
		silent_cerr("Maximum number of iterations taken. "
			"All possible eigenvalues of OP have been found. IPARAM(5) "
			"returns the number of wanted converged Ritz values "
			"(currently = " << IPARAM[4] << "; requested NEV = " << NEV << ")."
			<< std::endl);
		break;

	case 2:
		silent_cerr("No longer an informational error. Deprecated starting "
			"with release 2 of ARPACK."
			<< std::endl);
		break;

	case 3:
		silent_cerr("No shifts could be applied during a cycle of the "
			"implicitly restarted Arnoldi iteration. One possibility "
			"is to increase the size of NCV (currently = " << NCV << ") "
			"relative to NEV (currently = " << NEV << "). "
			"See remark 4 in dnaupd(3)."
			<< std::endl);
		break;
	}

	std::ostream& Out = pDM->GetOutFile();
	Out << "INFO: " << INFO << std::endl;

#if 0
	for (int ii = 0; ii < 11; ii++) {
		std::cerr << "IPARAM(" << ii + 1 << ")=" << IPARAM[ii] << std::endl;
	}
#endif

	logical RVEC = true;
	const char *HOWMNY = "A";
	std::vector<logical> SELECT(NCV);
	std::vector<doublereal> DR(NEV + 1);
	std::vector<doublereal> DI(NEV + 1);
	std::vector<doublereal> Z(N*(NEV + 1));
	integer LDZ = N;
	std::vector<doublereal> WORKEV(3*NCV);

#if 0
	std::cerr << "dneupd:" << std::endl
		<< "RVEC = " << RVEC << std::endl
		<< "HOWMNY = " << HOWMNY << std::endl
		<< "SELECT(" << NCV << ")" << std::endl
		<< "DR(" << NEV + 1 << ")" << std::endl
		<< "DI(" << NEV + 1 << ")" << std::endl
		<< "Z(" << N << ", " << NEV + 1 << ")" << std::endl
		<< "LDZ = " << LDZ << std::endl
		<< "SIGMAR = " << SIGMAR << std::endl
		<< "SIGMAI = " << SIGMAI << std::endl
		<< "WORKEV(" << 3*NCV << ")" << std::endl
		<< "BMAT = " << BMAT << std::endl
		<< "N = " << N << std::endl
		<< "WHICH = " << WHICH << std::endl
		<< "NEV = " << NEV << std::endl
		<< "TOL = " << TOL << std::endl
		<< "RESID(" << N << ")" << std::endl
		<< "NCV = " << NCV << std::endl
		<< "V(" << N << ", " << NCV << ")" << std::endl
		<< "LDV = " << LDV << std::endl
		<< "IPARAM(" << sizeof(IPARAM)/sizeof(IPARAM[0]) << ")" << std::endl
		<< "IPNTR(" << sizeof(IPNTR)/sizeof(IPNTR[0]) << ")" << std::endl
		<< "WORKD(" << 3*N << ")" << std::endl
		<< "WORKL(" << LWORKL << ")" << std::endl
		<< "LWORKL = " << LWORKL << std::endl
		<< "INFO = " << INFO << std::endl;
#endif

	__FC_DECL__(dneupd)(&RVEC, &HOWMNY[0], &SELECT[0], &DR[0], &DI[0],
		&Z[0], &LDZ, &SIGMAR, &SIGMAI, &WORKEV[0],
		&BMAT[0], &N, &WHICH[0], &NEV,
		&TOL, &RESID[0], &NCV, &V[0], &LDV, &IPARAM[0], &IPNTR[0],
		&WORKD[0], &WORKL[0], &LWORKL, &INFO);

	int nconv = IPARAM[4];
	if (nconv > 0) {
		MyVectorHandler AlphaR(nconv, &DR[0]);
		MyVectorHandler AlphaI(nconv, &DI[0]);
		std::vector<bool> vOut(nconv);
		output_eigenvalues(0, AlphaR, AlphaI, 1., pDM, pEA, 1, nconv, vOut);

		if (pEA->uFlags & Solver::EigenAnalysis::EIG_OUTPUT_EIGENVECTORS) {
			std::vector<doublereal *> ZC(nconv);
			FullMatrixHandler VR(&Z[0], &ZC[0], N*nconv, N, nconv);
			output_eigenvectors(0, AlphaR, AlphaI, 1.,
				0, VR, pDM, pEA, vOut, o);
		}

	} else {
		silent_cerr("no converged Ritz coefficients" << std::endl);
	}
}
#endif // USE_ARPACK

#ifdef USE_JDQZ
// Computes eigenvalues and eigenvectors using ARPACK's
// canonical non-symmetric eigenanalysis
static void
eig_jdqz(const MatrixHandler *pMatA, const MatrixHandler *pMatB,
	DataManager *pDM, Solver::EigenAnalysis *pEA, std::ostream& o)
{
	const NaiveMatrixHandler& MatA = dynamic_cast<const NaiveMatrixHandler &>(*pMatA);
	const NaiveMatrixHandler& MatB = dynamic_cast<const NaiveMatrixHandler &>(*pMatB);

	MBJDQZ mbjdqz(MatA, MatB);
	mbjdqzp = &mbjdqz;

	/*

alpha, beta Obvious from equation (1)
wanted      Compute the converged eigenvectors (if wanted =
            .true.)
eivec       Converged eigenvectors if wanted = .true., else con-
            verged Schur vectors
n           The size of the problem
target      The value near which the eigenvalues are sought
eps         Tolerance of the eigensolutions, Ax-Bx /|/| < 
kmax        Number of wanted eigensolutions, on output: number of
            converged eigenpairs
jmax        Maximum size of the search space
jmin        Minimum size of the search space
method      Linear equation solver:
   1:       GMRESm , [2]
   2:       BiCGstab(), [3]
m           Maximum dimension of searchspace of GMRESm
l           Degree of GMRES-polynomial in Bi-CGstab()
mxmv        Maximum number of matrix-vector multiplications in
            GMRESm or BiCGstab()
maxstep     Maximum number of Jacobi-Davidson iterations
lock        Tracking parameter (section 2.5.1)
order       Selection criterion for Ritz values:
   0:       nearest to target
   -1:      smallest real part
   1:       largest real part
   -2:      smallest imaginary part
   2:       largest imaginary part
testspace   Determines how to expand the testspace W
   1:       w = "Standard Petrov" ×v (Section 3.1.1)
   2:       w = "Standard 'variable' Petrov" ×v (Section 3.1.2)
   3:       w = "Harmonic Petrov" ×v (Section 3.5.1)
zwork       Workspace
lwork       Size of workspace, >= 4+m+5jmax+3kmax if GMRESm
            is used, >= 10 + 6 + 5jmax + 3kmax if Bi-CGstab() is
            used.

*/

	std::vector<doublecomplex> alpha;
	std::vector<doublecomplex> beta;
	std::vector<doublecomplex> eivec;
	logical wanted = 1;
	integer n = pMatA->iGetNumRows();
	doublecomplex target = { 1., 0. };
	doublereal eps = pEA->jdqz.eps;
	integer kmax = pEA->jdqz.kmax;
	integer jmax = pEA->jdqz.jmax;
	integer jmin = pEA->jdqz.jmin;
	integer method = pEA->jdqz.method;
	integer m = pEA->jdqz.m;
	integer l = pEA->jdqz.l;
	integer mxmv = pEA->jdqz.mxmv;
	integer maxstep = pEA->jdqz.maxstep;
	doublereal lock = pEA->jdqz.lock;
	integer order = pEA->jdqz.order;
	integer testspace = pEA->jdqz.testspace;
	std::vector<doublecomplex> zwork;
	integer lwork;

	switch (method) {
	case Solver::EigenAnalysis::JDQZ::GMRES:
		lwork = 4 + m + 5*jmax + 3*kmax;
		break;

	case Solver::EigenAnalysis::JDQZ::BICGSTAB:
		lwork = 10 + 6*l + 5*jmax + 3*kmax;
		break;

	default:
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	alpha.resize(jmax);
	beta.resize(jmax);
	eivec.resize(n*kmax);
	zwork.resize(n*lwork);

	__FC_DECL__(jdqz)(
		&alpha[0],
		&beta[0],
		&eivec[0],
		&wanted,
		&n,
		&target,
		&eps,
		&kmax,
		&jmax,
		&jmin,
		&method,
		&m,
		&l,
		&mxmv,
		&maxstep,
		&lock,
		&order,
		&testspace,
		&zwork[0],
		&lwork);

	silent_cerr("\r" "cnt=" << mbjdqz.Cnt() << std::endl);

	if (kmax > 0) {
		int nconv = kmax;
		MyVectorHandler AlphaR(nconv);
		MyVectorHandler AlphaI(nconv);
		MyVectorHandler Beta(nconv);
		for (integer c = 0; c < nconv; c++) {
			if (beta[c].i != 0.) {
				doublereal d = std::sqrt(beta[c].r*beta[c].r + beta[c].i*beta[c].i);
				Beta(c + 1) = d;
				AlphaR(c + 1) = (alpha[c].r*beta[c].r + alpha[c].i*beta[c].i)/d;
				AlphaI(c + 1) = (alpha[c].i*beta[c].r - alpha[c].r*beta[c].i)/d;

			} else {
				Beta(c + 1) = beta[c].r;
				AlphaR(c + 1) = alpha[c].r;
				AlphaI(c + 1) = alpha[c].i;
			}
		}
		std::vector<bool> vOut(nconv);
		output_eigenvalues(0, AlphaR, AlphaI, 1., pDM, pEA, 1, nconv, vOut);
	
		if (pEA->uFlags & Solver::EigenAnalysis::EIG_OUTPUT_EIGENVECTORS) {
			FullMatrixHandler VR(n, nconv);
			doublecomplex *p = &eivec[0] - 1;
			for (integer c = 1; c <= nconv; c++) {
				
				if (AlphaI(c) == 0.) {
					for (integer r = 1; r <= n; r++) {
						VR(r, c) = p[r].r;
					}
						
				} else {
					for (integer r = 1; r <= n; r++) {
						VR(r, c) = p[r].r;
						VR(r, c + 1) = p[n + r].i;
					}
	
					p += n;
					c++;
				}
	
				p += n;
			}
	
			output_eigenvectors(&Beta, AlphaR, AlphaI, 1.,
				0, VR, pDM, pEA, vOut, o);
		}

	} else {
		silent_cerr("no converged eigenpairs" << std::endl);
	}
}
#endif // USE_JDQZ

// writes a full matrix in a form compatible with octave/matlab
static void
output_full_matrix(std::ostream& o,
	const MatrixHandler* pm,
	const std::string& comment,
	const std::string& name)
{
	const FullMatrixHandler& m = dynamic_cast<const FullMatrixHandler &>(*pm);

	o
		<< comment << std::endl
		<< name << " = [";

	integer nrows = m.iGetNumRows();
	integer ncols = m.iGetNumCols();

	for (integer r = 1; r <= nrows; r++) {
		for (integer c = 1; c <= ncols; c++) {
			o << std::setw(24) << m(r, c);
		}

		if (r == nrows) {
			o << "];" << std::endl;

		} else {
			o << ";" << std::endl;
		}
	}
}

// writes a sparse map matrix in a form compatible with octave/matlab
static void
output_sparse_matrix(std::ostream& o,
	const MatrixHandler* pm,
	const std::string& comment,
	const std::string& name)
{
	const SpMapMatrixHandler& m = dynamic_cast<const SpMapMatrixHandler &>(*pm);

	o
		<< comment << std::endl
		<< name << " = [";

	for (SpMapMatrixHandler::const_iterator i = m.begin();
		i != m.end(); ++i)
	{
		if (i->dCoef != 0.) {
			o << i->iRow + 1 << " " << i->iCol + 1 << " " << i->dCoef << ";" << std::endl;
		}
	}

	o << "];" << std::endl
		<< name << " = spconvert(" << name << ");" << std::endl;
}

// writes a naive sparse matrix in a form compatible with octave/matlab
static void
output_naive_matrix(std::ostream& o,
	const MatrixHandler* pm,
	const std::string& comment,
	const std::string& name)
{
	const NaiveMatrixHandler& m = dynamic_cast<const NaiveMatrixHandler &>(*pm);

	o
		<< comment << std::endl
		<< name << " = [";

	for (NaiveMatrixHandler::const_iterator i = m.begin();
		i != m.end(); ++i)
	{
		if (i->dCoef != 0.) {
			o << i->iRow + 1 << " " << i->iCol + 1 << " " << i->dCoef << ";" << std::endl;
		}
	}

	o << "];" << std::endl
		<< name << " = spconvert(" << name << ");" << std::endl;
}

// Driver for eigenanalysis
void
Solver::Eig(void)
{
	DEBUGCOUTFNAME("Solver::Eig");

	/*
	 * MatA, MatB: MatrixHandlers to eigenanalysis matrices
	 * MatVL, MatVR: MatrixHandlers to eigenvectors, if required
	 * AlphaR, AlphaI Beta: eigenvalues
	 * WorkVec:    Workspace
	 * iWorkSize:  Size of the workspace
	 */

	DEBUGCOUT("Solver::Eig(): performing eigenanalysis" << std::endl);

	integer iSize = iNumDofs;

	SolutionManager *pSM = 0;
	MatrixHandler *pMatA = 0;
	MatrixHandler *pMatB = 0;

	if (EigAn.uFlags & EigenAnalysis::EIG_USE_LAPACK) {
		SAFENEWWITHCONSTRUCTOR(pMatA, FullMatrixHandler,
			FullMatrixHandler(iSize));

		SAFENEWWITHCONSTRUCTOR(pMatB, FullMatrixHandler,
			FullMatrixHandler(iSize));

	} else if (EigAn.uFlags & EigenAnalysis::EIG_USE_ARPACK) {
		SAFENEWWITHCONSTRUCTOR(pSM, NaiveSparsePermSolutionManager<Colamd_ordering>,
			NaiveSparsePermSolutionManager<Colamd_ordering>(iSize));
		SAFENEWWITHCONSTRUCTOR(pMatA, NaiveMatrixHandler,
			NaiveMatrixHandler(iSize));
		pMatB = pSM->pMatHdl();

	} else if (EigAn.uFlags & EigenAnalysis::EIG_USE_JDQZ) {
		SAFENEWWITHCONSTRUCTOR(pMatA, NaiveMatrixHandler,
			NaiveMatrixHandler(iSize));
		SAFENEWWITHCONSTRUCTOR(pMatB, NaiveMatrixHandler,
			NaiveMatrixHandler(iSize));

	} else if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_SPARSE_MATRICES) {
		SAFENEWWITHCONSTRUCTOR(pMatA, SpMapMatrixHandler,
			SpMapMatrixHandler(iSize));
		SAFENEWWITHCONSTRUCTOR(pMatB, SpMapMatrixHandler,
			SpMapMatrixHandler(iSize));
	}

	pMatA->Reset();
	pMatB->Reset();

	// Matrices assembly (see eig.ps)
	doublereal h = EigAn.dParam;
	pDM->AssJac(*pMatA, -h/2.);
	pDM->AssJac(*pMatB, h/2.);

#ifdef DEBUG
	DEBUGCOUT(std::endl
		<< "Matrix A:" << std::endl << *pMatA << std::endl
		<< "Matrix B:" << std::endl << *pMatB << std::endl);
#endif /* DEBUG */

	std::ofstream o;
	if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT) {
		std::string tmpFileName;
		if (sOutputFileName == NULL) {
			tmpFileName = sInputFileName;

		} else {
			tmpFileName = sOutputFileName;
		}

		tmpFileName += ".m";
		o.open(tmpFileName.c_str());

		o.setf(std::ios::right | std::ios::scientific);
		o.precision(16);

		/* coefficient */
		o
			<< "% coefficient" << std::endl
			<< "dCoef = " << h/2. << ";" << std::endl;
	}

	if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_FULL_MATRICES) {
		/* first matrix */
		output_full_matrix(o, pMatB, "% F/xPrime + dCoef * F/x", "Aplus");

		/* second matrix */
		output_full_matrix(o, pMatA, "% F/xPrime - dCoef * F/x", "Aminus");

	} else if (EigAn.uFlags & EigenAnalysis::EIG_OUTPUT_SPARSE_MATRICES) {
		void (*om)(std::ostream&, const MatrixHandler *,
			const std::string&, const std::string&);

		if (dynamic_cast<const NaiveMatrixHandler *>(pMatB)) {
			om = output_naive_matrix;
	
		} else {
			om = output_sparse_matrix;
		}

		/* first matrix */
		om(o, pMatB, "% F/xPrime + dCoef * F/x", "Aplus");

		/* second matrix */
		om(o, pMatA, "% F/xPrime - dCoef * F/x", "Aminus");
	}

	switch (EigAn.uFlags & EigenAnalysis::EIG_USE_MASK) {
#ifdef USE_LAPACK
	case EigenAnalysis::EIG_USE_LAPACK:
		eig_lapack(pMatA, pMatB, pDM, &EigAn, o);
		break;
#endif // USE_LAPACK

#ifdef USE_ARPACK
	case EigenAnalysis::EIG_USE_ARPACK:
		eig_arpack(pMatA, pSM, pDM, &EigAn, o);
		break;
#endif // USE_ARPACK

#ifdef USE_JDQZ
	case EigenAnalysis::EIG_USE_JDQZ:
		eig_jdqz(pMatA, pMatB, pDM, &EigAn, o);
		break;
#endif // USE_JDQZ

	default:
		// can't get here!
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	if (o) {
		o.close();
	}

	if (pSM) {
		pMatB = 0;
		SAFEDELETE(pSM);
	}

	if (pMatA) {
		SAFEDELETE(pMatA);
	}

	if (pMatB) {
		SAFEDELETE(pMatB);
	}
}


SolutionManager *const
Solver::AllocateSolman(integer iNLD, integer iLWS)
{
	SolutionManager *pCurrSM = CurrLinearSolver.GetSolutionManager(iNLD, iLWS);

	/* special extra parameters if required */
	switch (CurrLinearSolver.GetSolver()) {
	case LinSol::UMFPACK_SOLVER:
#ifdef HAVE_UMFPACK_TIC_DISABLE
		if (pRTSolver) {
			/* disable profiling, to avoid times() system call
			 *
			 * This function has been introduced in Umfpack 4.1
			 * by our patch at
			 *
			 * http://mbdyn.aero.polimi.it/~masarati/Download/\
			 * 	mbdyn/umfpack-4.1-nosyscalls.patch
			 *
			 * but since Umfpack 4.3 is no longer required,
			 * provided the library is compiled with -DNO_TIMER
			 * to disable run-time syscalls to timing routines.
			 */
			umfpack_tic_disable();
		}
#endif // HAVE_UMFPACK_TIC_DISABLE
		break;

	default:
		break;
	}

	return pCurrSM;
};


SolutionManager *const
Solver::AllocateSchurSolman(integer iStates)
{
	SolutionManager *pSSM(NULL);

#ifdef USE_MPI
	switch (CurrIntSolver.GetSolver()) {
	case LinSol::LAPACK_SOLVER:
	case LinSol::MESCHACH_SOLVER:
	case LinSol::NAIVE_SOLVER:
	case LinSol::UMFPACK_SOLVER:
	case LinSol::Y12_SOLVER:
		break;

	default:
		silent_cerr("apparently solver "
				<< CurrIntSolver.GetSolverName()
				<< " is not allowed as interface solver "
				"for SchurSolutionManager" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	SAFENEWWITHCONSTRUCTOR(pSSM,
			SchurSolutionManager,
			SchurSolutionManager(iNumDofs, iStates, pLocDofs,
				iNumLocDofs,
				pIntDofs, iNumIntDofs,
				pLocalSM, CurrIntSolver));

#else /* !USE_MPI */
	silent_cerr("Configure --with-mpi to enable Schur solver" << std::endl);
	throw ErrGeneric(MBDYN_EXCEPT_ARGS);
#endif /* !USE_MPI */

	return pSSM;
};

NonlinearSolver *const
Solver::AllocateNonlinearSolver()
{
	NonlinearSolver *pNLS = NULL;

	switch (NonlinearSolverType) {
	case NonlinearSolver::MATRIXFREE:
		switch (MFSolverType) {
		case MatrixFreeSolver::BICGSTAB:
			SAFENEWWITHCONSTRUCTOR(pNLS,
					BiCGStab,
					BiCGStab(PcType,
						iPrecondSteps,
						dIterTol,
						iIterativeMaxSteps,
						dIterertiveEtaMax,
						dIterertiveTau,
						bHonorJacRequest));
			break;

		default:
			pedantic_cout("unknown matrix free solver type; "
					"using default" << std::endl);
			/* warning: should be unreachable */

		case MatrixFreeSolver::GMRES:
			SAFENEWWITHCONSTRUCTOR(pNLS,
					Gmres,
					Gmres(PcType,
						iPrecondSteps,
						dIterTol,
						iIterativeMaxSteps,
						dIterertiveEtaMax,
						dIterertiveTau,
						bHonorJacRequest));
			break;
		}
		break;

	default:
		pedantic_cout("unknown nonlinear solver type; using default"
				<< std::endl);

	case NonlinearSolver::NEWTONRAPHSON:
		SAFENEWWITHCONSTRUCTOR(pNLS,
				NewtonRaphsonSolver,
				NewtonRaphsonSolver(bTrueNewtonRaphson,
					bKeepJac,
					iIterationsBeforeAssembly,
					bHonorJacRequest));
		break;
	}
	return pNLS;
}

void
Solver::SetupSolmans(integer iStates, bool bCanBeParallel)
{
   	DEBUGLCOUT(MYDEBUG_MEM, "creating SolutionManager\n\tsize = "
		   << iNumDofs*iUnkStates <<
		   "\n\tnumdofs = " << iNumDofs
		   << "\n\tnumstates = " << iStates << std::endl);

	/* delete previous solmans */
	if (pSM != 0) {
		SAFEDELETE(pSM);
		pSM = 0;
	}
	if (pLocalSM != 0) {
		SAFEDELETE(pLocalSM);
		pLocalSM = 0;
	}

	integer iWorkSpaceSize = CurrLinearSolver.iGetWorkSpaceSize();
	integer iLWS = iWorkSpaceSize;
	integer iNLD = iNumDofs*iStates;
	if (bCanBeParallel && bParallel) {
		/* FIXME BEPPE! */
		iLWS = iWorkSpaceSize*iNumLocDofs/(iNumDofs*iNumDofs);
		/* FIXME: GIUSTO QUESTO? */
		iNLD = iNumLocDofs*iStates;
	}

	SolutionManager *pCurrSM = AllocateSolman(iNLD, iLWS);

	/*
	 * This is the LOCAL solver if instantiating a parallel
	 * integrator; otherwise it is the MAIN solver
	 */
	if (bCanBeParallel && bParallel) {
		pLocalSM = pCurrSM;

		/* Crea il solutore di Schur globale */
		pSM = AllocateSchurSolman(iStates);

	} else {
		pSM = pCurrSM;
	}
	/*
	 * FIXME: at present there MUST be a pSM
	 * (even for matrix-free nonlinear solvers)
	 */
	if (pSM == NULL) {
		silent_cerr("No linear solver defined" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}
}

clock_t
Solver::GetCPUTime(void) const
{
	return pDM->GetCPUTime();
}

void
Solver::PrintResidual(const VectorHandler& Res, integer iIterCnt) const
{
	pDM->PrintResidual(Res, iIterCnt);
}

void
Solver::PrintSolution(const VectorHandler& Sol, integer iIterCnt) const
{
	pDM->PrintSolution(Sol, iIterCnt);
}

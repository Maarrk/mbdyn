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

/* datamanager */

#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#ifdef USE_MULTITHREAD

extern "C" {
#include <time.h>
#ifdef HAVE_SYS_TIMES_H
#include <sys/times.h>
#endif /* HAVE_SYS_TIMES_H */
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif /* HAVE_SCHED_H */


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
}

#include "mtdataman.h"
#include "task2cpu.h"

static inline void
do_lock(integer &p)
{
	while (!mbdyn_compare_and_swap(p, 1, 0));
}
	
static inline void
do_unlock(integer &p)
{
	mbdyn_compare_and_swap(p, 0, 1);
}

static void
naivepsad(doublereal **ga, integer **gri, 
		integer *gnzr, integer **gci, integer *gnzc, char **gnz,
		doublereal **a, integer **ci, integer *nzc,
		integer from, integer to, integer *lock)
{

	for (integer r = from; r < to; r++) {
		integer nc = nzc[r];
			
		if (nc) {
			doublereal *pgar = ga[r];
			doublereal *par  = a[r];
			
			for (integer i = 0; i < nc; i++) {
				integer c = ci[r][i];
				
				if (gnz[r][c]) {
					pgar[c] += par[c];

				} else {
					pgar[c] = par[c];
					gci[r][gnzc[r]] = c;
					/* This can only be set to 1 from 0,
					 * so concurrency is harmless
					 */
					gnz[r][c] = 1;
					
					do_lock(lock[c]);

					gri[c][gnzr[c]] = r;
					gnzr[c]++;
										
					do_unlock(lock[c]);
					gnzc[r]++;
				}
			}
		}
	}
}


/* MultiThreadDataManager - begin */


/*
 * costruttore: inizializza l'oggetto, legge i dati e crea le strutture di
 * gestione di Dof, nodi, elementi e drivers.
 */

MultiThreadDataManager::MultiThreadDataManager(MBDynParser& HP, 
		unsigned OF,
		Solver* pS,
		doublereal dInitialTime,
		const char* sOutputFileName,
		const char* sInputFileName,
		bool bAbortAfterInput,
		unsigned nt)
:
DataManager(HP, OF, pS, dInitialTime, sOutputFileName, sInputFileName, bAbortAfterInput),
nThreads(nt),
AssMode(ASS_UNKNOWN),
CCReady(CC_NO),
thread_data(0),
op(MultiThreadDataManager::OP_UNKNOWN),
thread_count(0),
propagate_ErrMatrixRebuild(sig_atomic_t(false))
{
#if 0	/* no effects ... */
	struct sched_param	sp;
	int			policy = SCHED_FIFO;
	int			rc;

	rc = sched_getparam(0, &sp);
	if (rc != 0) {
		silent_cerr("sched_getparam() failed: " << errno << std::endl);
		throw ErrGeneric();
	}

	int pmin = sched_get_priority_min(policy);
	int pmax = sched_get_priority_max(policy);

	silent_cout("current priority is " << sp.sched_priority
			<< " {" << pmin << "," << pmax << "}" << std::endl);

	if (sp.sched_priority > pmax || sp.sched_priority < pmin) {
		sp.sched_priority = pmax;
	}

	rc = sched_setscheduler(0, policy, &sp);
	if (rc != 0) {
		silent_cerr("sched_setscheduler() unable "
				"to set SCHED_FIFO scheduling policy: "
				<< errno
				<< std::endl);
		throw ErrGeneric();
	}
#endif

	if (pthread_mutex_init(&thread_mutex, NULL)) {
		silent_cerr("MultiThreadDataManager::MultiThreadDataManager(): "
				"mutex init failed" << std::endl);
		throw ErrGeneric();
	}
		
	if (pthread_cond_init(&thread_cond, NULL)) {
		silent_cerr("MultiThreadDataManager::MultiThreadDataManager(): "
				"cond init failed" << std::endl);
		throw ErrGeneric();
	}

	ThreadSpawn();
}

MultiThreadDataManager::~MultiThreadDataManager(void)
{
	pthread_mutex_destroy(&thread_mutex);
	pthread_cond_destroy(&thread_cond);
}

clock_t
MultiThreadDataManager::ThreadDestroy(void)
{
	if (thread_data == 0) {
		return 0;
	}

	clock_t cputime = 0;

	op = MultiThreadDataManager::OP_EXIT;
	thread_count = nThreads - 1;

	for (unsigned i = 1; i < nThreads; i++) {
		void *retval = NULL;

		sem_post(&thread_data[i].sem);
		if (pthread_join(thread_data[i].thread, &retval)) {
			silent_cerr("pthread_join() failed on thread " << i
					<< std::endl);
			/* already shutting down ... */
		}

		cputime += thread_data[i].cputime;
	}

	if (thread_data[0].lock) {
		SAFEDELETEARR(thread_data[0].lock);
	}
	thread_cleanup(&thread_data[0]);

	SAFEDELETEARR(thread_data);
	thread_data = 0;

	return cputime;
}


void *
MultiThreadDataManager::thread(void *p)
{
	MultiThreadDataManager::ThreadData *arg
		= (MultiThreadDataManager::ThreadData *)p;

	silent_cout("MultiThreadDataManager: thread " << arg->threadNumber
			<< " [self=" << pthread_self()
			<< ",pid=" << getpid() << "]"
			<< " starting..." << std::endl);
	
	bool bKeepGoing = true;

	/* deal with signals ... */
	sigset_t newset /* , oldset */ ;
	sigemptyset(&newset);
	sigaddset(&newset, SIGTERM);
	sigaddset(&newset, SIGINT);
	sigaddset(&newset, SIGHUP);
	pthread_sigmask(SIG_BLOCK, &newset, /* &oldset */ NULL);

	(void)mbdyn_task2cpu(arg->threadNumber - 1);

	while (bKeepGoing) {
		/* stop here until told to start */
		/*
		 * NOTE: here
		 * - the requested operation must be set;
		 * - the appropriate operation args must be set
		 * - the thread_count must be set to nThreads - 1
		 */
		sem_wait(&arg->sem);

		DEBUGCOUT("thread " << arg->threadNumber << ": "
				"op " << arg->pDM->op << std::endl);

		/* select requested operation */
		switch (arg->pDM->op) {
		case MultiThreadDataManager::OP_ASSJAC_CC:
			//arg->pJacHdl->Reset();
			try {
				arg->pDM->DataManager::AssJac(*arg->pJacHdl,
						arg->dCoef,
						arg->ElemIter,
						*arg->pWorkMat);

			} catch (MatrixHandler::ErrRebuildMatrix) {
				silent_cerr("thread " << arg->threadNumber
						<< " caught ErrRebuildMatrix"
						<< std::endl);

				mbdyn_compare_and_swap(arg->pDM->propagate_ErrMatrixRebuild,
						sig_atomic_t(true), sig_atomic_t(false));

			} catch (...) {
				throw;
			}
			break;

		case MultiThreadDataManager::OP_ASSJAC_NAIVE:
#if 0
			arg->ppNaiveJacHdl[arg->threadNumber]->Reset();
#endif
			/* NOTE: Naive should never throw
			 * ErrRebuildMatrix ... */
			arg->pDM->DataManager::AssJac(*arg->ppNaiveJacHdl[arg->threadNumber],
					arg->dCoef,
					arg->ElemIter,
					*arg->pWorkMat);
			break;

		case MultiThreadDataManager::OP_SUM_NAIVE:
		{
			/* FIXME: if the naive matrix is permuted (colamd),
			 * this should not impact the parallel assembly,
			 * because all the matrices refer to the same 
			 * permutation vector */
			NaiveMatrixHandler* to = arg->ppNaiveJacHdl[0];
			integer nn = to->iGetNumRows();
			integer iFrom = (nn*(arg->threadNumber))/arg->pDM->nThreads;
			integer iTo = (nn*(arg->threadNumber + 1))/arg->pDM->nThreads;
			for (unsigned int matrix = 1; matrix < arg->pDM->nThreads; matrix++) {
				NaiveMatrixHandler* from = arg->ppNaiveJacHdl[matrix];
				naivepsad(to->ppdRows, 
						to->ppiRows, to->piNzr, 
						to->ppiCols, to->piNzc, to->ppnonzero,
						from->ppdRows, from->ppiCols, from->piNzc, 
      						iFrom, iTo, arg->lock);
			}
			break;
		}

#ifdef MBDYN_X_MT_ASSRES
		case MultiThreadDataManager::OP_ASSRES:
			arg->pResHdl->Reset();
			arg->pDM->DataManager::AssRes(*arg->pResHdl,
					arg->dCoef,
					arg->ElemIter,
					*arg->pWorkVec);
			break;
#endif /* MBDYN_X_MT_ASSRES */

		case MultiThreadDataManager::OP_EXIT:
			/* cleanup */
			thread_cleanup(arg);
			bKeepGoing = false;
			break;

		default:
			silent_cerr("MultiThreadDataManager: unhandled op"
					<< std::endl);
			throw ErrGeneric();
		}

		/* decrease counter and signal if last
		 * (mutex + cond) */
		arg->pDM->EndOfOp();
	}

	/* all threads are joined */
	pthread_exit(NULL);
}

void
MultiThreadDataManager::thread_cleanup(ThreadData *arg)
{
	/* cleanup */
	SAFEDELETE(arg->pWorkMatA);
	SAFEDELETE(arg->pWorkMatB);
	SAFEDELETE(arg->pWorkVec);
	if (arg->threadNumber > 0) {
		if (arg->pJacHdl) {
			SAFEDELETE(arg->pJacHdl);
		}
		SAFEDELETE(arg->pResHdl);

	} else {
		SAFEDELETEARR(arg->ppNaiveJacHdl);
	}
	sem_destroy(&arg->sem);

#ifdef HAVE_SYS_TIMES_H	
	/* Tempo di CPU impiegato */
	struct tms tmsbuf;
	times(&tmsbuf);

	pedantic_cout("thread " << arg->threadNumber << ":" << std::endl
		<< "\tutime:  " << tmsbuf.tms_utime << std::endl
		<< "\tstime:  " << tmsbuf.tms_stime << std::endl
		<< "\tcutime: " << tmsbuf.tms_cutime << std::endl
		<< "\tcstime: " << tmsbuf.tms_cstime << std::endl);
			
	arg->cputime = tmsbuf.tms_utime + tmsbuf.tms_cutime
		+ tmsbuf.tms_stime + tmsbuf.tms_cstime;
#endif /* HAVE_SYS_TIMES_H */
}

void
MultiThreadDataManager::EndOfOp(void)
{
	bool last;
	
	/* decrement the thread counter */
	pthread_mutex_lock(&thread_mutex);
	thread_count--;
	last = (thread_count == 0);

	/* if last thread, signal to restart */
	if (last) {
		pthread_cond_signal(&thread_cond);
		// pthread_cond_broadcast(&thread_cond);
	}

	pthread_mutex_unlock(&thread_mutex);
}

/* starts the helper threads */
void
MultiThreadDataManager::ThreadSpawn(void)
{
	ASSERT(nThreads > 1);

	SAFENEWARR(thread_data, MultiThreadDataManager::ThreadData, nThreads);
	
	for (unsigned i = 0; i < nThreads; i++) {
		/* callback data */
		thread_data[i].pDM = this;
		sem_init(&thread_data[i].sem, 0, 0);
		thread_data[i].threadNumber = i;
		thread_data[i].ElemIter.Init(&Elems[0], Elems.size());
		thread_data[i].lock = 0;

		/* SubMatrixHandlers */
		SAFENEWWITHCONSTRUCTOR(thread_data[i].pWorkMatA,
				VariableSubMatrixHandler,
				VariableSubMatrixHandler(iMaxWorkNumRows,
					iMaxWorkNumCols));

		SAFENEWWITHCONSTRUCTOR(thread_data[i].pWorkMatB,
				VariableSubMatrixHandler,
				VariableSubMatrixHandler(iMaxWorkNumRows,
					iMaxWorkNumCols));

		thread_data[i].pWorkMat = thread_data[i].pWorkMatA;

		SAFENEWWITHCONSTRUCTOR(thread_data[i].pWorkVec,
				MySubVectorHandler,
				MySubVectorHandler(iMaxWorkNumRows));

		/* set by AssJac when in CC form */
		thread_data[i].pJacHdl = 0;

		/* set by AssJac when in Naive form */
		thread_data[i].ppNaiveJacHdl = 0;

		/* set below */
		thread_data[i].pResHdl = 0;	

		/* to be sure... */
		thread_data[i].pMatA = 0;
		thread_data[i].pMatB = 0;

		if (i == 0) {
			continue;
		}

		SAFENEWWITHCONSTRUCTOR(thread_data[i].pResHdl,
				MyVectorHandler, MyVectorHandler(iTotDofs));

		/* create thread */
		if (pthread_create(&thread_data[i].thread, NULL, thread,
					&thread_data[i]) != 0) {
			silent_cerr("pthread_create() failed "
					"for thread " << i
					<< " of " << nThreads << std::endl);
			throw ErrGeneric();
		}
	}
	
	(void)mbdyn_task2cpu(nThreads - 1);
}

void
MultiThreadDataManager::AssJac(MatrixHandler& JacHdl, doublereal dCoef)
{
retry:;
	switch (AssMode) {
	case ASS_CC:
		CCAssJac(JacHdl, dCoef);
		break;

	case ASS_NAIVE:
		if (&JacHdl == thread_data[0].ppNaiveJacHdl[0]) {
			NaiveAssJac(JacHdl, dCoef);
			break;
		}

		for (unsigned i = 1; i < nThreads; i++) {
			if (thread_data[0].ppNaiveJacHdl[i]) {
				SAFEDELETE(thread_data[0].ppNaiveJacHdl[i]);
				thread_data[0].ppNaiveJacHdl[i] = 0;
			}
		}

		SAFEDELETEARR(thread_data[0].lock);
		thread_data[0].lock = 0;

		/* intentionally continue to next block */

	case ASS_UNKNOWN:
	{
		NaiveMatrixHandler *pNaiveJacHdl = dynamic_cast<NaiveMatrixHandler *>(&JacHdl);	
		if (pNaiveJacHdl) {
			AssMode = ASS_NAIVE;

			/* use JacHdl as matrix for the first thread,
			 * and create copies for the other threads;
			 * each thread sees the array of all the matrices,
			 * and uses only its own for element assembly,
			 * all for per-thread matrix summation */
			SAFENEWARR(thread_data[0].lock, integer, JacHdl.iGetNumRows());
			memset(thread_data[0].lock, 0, sizeof(integer)*JacHdl.iGetNumRows());

			SAFENEWARR(thread_data[0].ppNaiveJacHdl,
					NaiveMatrixHandler*, nThreads);
			thread_data[0].ppNaiveJacHdl[0] = pNaiveJacHdl;

			NaivePermMatrixHandler *pNaivePermJacHdl = dynamic_cast<NaivePermMatrixHandler *>(&JacHdl);	
			for (unsigned i = 1; i < nThreads; i++) {
				thread_data[i].lock = thread_data[0].lock;
				thread_data[i].ppNaiveJacHdl = thread_data[0].ppNaiveJacHdl;
				thread_data[0].ppNaiveJacHdl[i] = 0;

				if (pNaivePermJacHdl) {
					SAFENEWWITHCONSTRUCTOR(thread_data[0].ppNaiveJacHdl[i],
							NaivePermMatrixHandler,
							NaivePermMatrixHandler(JacHdl.iGetNumRows(),
								pNaivePermJacHdl->pGetPerm()));

				} else {
					SAFENEWWITHCONSTRUCTOR(thread_data[0].ppNaiveJacHdl[i],
							NaiveMatrixHandler,
							NaiveMatrixHandler(JacHdl.iGetNumRows()));
				}
			}
			goto retry;
		}

		AssMode = ASS_CC;
		goto retry;
	}

	default:
		silent_cerr("unable to detect jacobian matrix type "
				"for multithread assembly" << std::endl);
		throw ErrGeneric();
	}
}

void
MultiThreadDataManager::CCAssJac(MatrixHandler& JacHdl, doublereal dCoef)
{
	ASSERT(thread_data != NULL);

	propagate_ErrMatrixRebuild = sig_atomic_t(false);

	CompactSparseMatrixHandler *pMH
		= dynamic_cast<CompactSparseMatrixHandler *>(&JacHdl);

	while (false) {
retry:;
		CCReady = CC_NO;
		for (unsigned i = 1; i < nThreads; i++) {
			SAFEDELETE(thread_data[i].pJacHdl);
			thread_data[i].pJacHdl = 0;
		}
	}

	switch (CCReady) {
	case CC_NO:
		DEBUGCERR("CC_NO => CC_FIRST" << std::endl);

		ASSERT(dynamic_cast<SpMapMatrixHandler *>(&JacHdl));

		DataManager::AssJac(JacHdl, dCoef, ElemIter, *pWorkMat);
		CCReady = CC_FIRST;

		return;

	case CC_FIRST:
		if (pMH == 0) {
			goto retry;
		}

		DEBUGCERR("CC_FIRST => CC_YES" << std::endl);

		for (unsigned i = 1; i < nThreads; i++) {
			thread_data[i].pJacHdl = pMH->Copy();
		}

		CCReady = CC_YES;

		break;

	case CC_YES:
		if (pMH == 0) {
			goto retry;
		}

		DEBUGCERR("CC_YES" << std::endl);

		break;

	default:
		throw ErrGeneric();

	}

	thread_data[0].ElemIter.ResetAccessData();
	op = MultiThreadDataManager::OP_ASSJAC_CC;
	thread_count = nThreads - 1;

	for (unsigned i = 1; i < nThreads; i++) {
		thread_data[i].dCoef = dCoef;
	
		sem_post(&thread_data[i].sem);
	}

	try {
		DataManager::AssJac(JacHdl, dCoef, thread_data[0].ElemIter,
				*thread_data[0].pWorkMat);

	} catch (MatrixHandler::ErrRebuildMatrix) {
		silent_cerr("thread " << thread_data[0].threadNumber
				<< " caught ErrRebuildMatrix"
				<< std::endl);

		mbdyn_compare_and_swap(propagate_ErrMatrixRebuild,
				sig_atomic_t(true), sig_atomic_t(false));

	} catch (...) {
		throw;
	}

	pthread_mutex_lock(&thread_mutex);
	if (thread_count > 0) {
		pthread_cond_wait(&thread_cond, &thread_mutex);
	}
	pthread_mutex_unlock(&thread_mutex);

	if (propagate_ErrMatrixRebuild) {
		for (unsigned i = 1; i < nThreads; i++) {
			SAFEDELETE(thread_data[i].pJacHdl);
			thread_data[i].pJacHdl = 0;
		}
		CCReady = CC_NO;

		throw MatrixHandler::ErrRebuildMatrix();
	}

	for (unsigned i = 1; i < nThreads; i++) {
		pMH->AddUnchecked(*thread_data[i].pJacHdl);
	}
}

void
MultiThreadDataManager::NaiveAssJac(MatrixHandler& JacHdl, doublereal dCoef)
{
	ASSERT(thread_data != NULL);

	/* Assemble per-thread matrix */
	thread_data[0].ElemIter.ResetAccessData();
	op = MultiThreadDataManager::OP_ASSJAC_NAIVE;
	thread_count = nThreads - 1;

	for (unsigned i = 1; i < nThreads; i++) {
		thread_data[i].dCoef = dCoef;
	
		sem_post(&thread_data[i].sem);
	}

	/* FIXME Right now it's already done before calling AssJac;
	 * needs be moved here to improve parallel performances... */
#if 0
	thread_data[0].ppNaiveJacHdl[0]->Reset();
#endif
	DataManager::AssJac(*thread_data[0].ppNaiveJacHdl[0],
			dCoef,
			thread_data[0].ElemIter,
			*thread_data[0].pWorkMat);

	pthread_mutex_lock(&thread_mutex);
	if (thread_count > 0) {
		pthread_cond_wait(&thread_cond, &thread_mutex);
	}
	pthread_mutex_unlock(&thread_mutex);

	/* Sum per-thread matrices */
	op = MultiThreadDataManager::OP_SUM_NAIVE;
	thread_count = nThreads - 1;
	for (unsigned i = 1; i < nThreads; i++) {
		sem_post(&thread_data[i].sem);
	}

	NaiveMatrixHandler* to = thread_data[0].ppNaiveJacHdl[0];
	integer nn = to->iGetNumRows();
	integer iFrom = 0;
	integer iTo = nn/nThreads;
	for (unsigned matrix = 1; matrix < nThreads; matrix++) {
		NaiveMatrixHandler* from = thread_data[0].ppNaiveJacHdl[matrix];
		naivepsad(to->ppdRows, 
				to->ppiRows, to->piNzr, 
				to->ppiCols, to->piNzc, to->ppnonzero,
				from->ppdRows, from->ppiCols, from->piNzc, 
				iFrom, iTo, thread_data[0].lock);
	}

	pthread_mutex_lock(&thread_mutex);
	if (thread_count > 0) {
		pthread_cond_wait(&thread_cond, &thread_mutex);
	}
	
	pthread_mutex_unlock(&thread_mutex);
}

#ifdef MBDYN_X_MT_ASSRES
void
MultiThreadDataManager::AssRes(VectorHandler& ResHdl, doublereal dCoef)
	throw(ChangedEquationStructure)
{
	ASSERT(thread_data != NULL);

	thread_data[0].ElemIter.ResetAccessData();
	op = MultiThreadDataManager::OP_ASSRES;
	thread_count = nThreads - 1;

	for (unsigned i = 1; i < nThreads; i++) {
		thread_data[i].dCoef = dCoef;
	
		sem_post(&thread_data[i].sem);
	}

	DataManager::AssRes(ResHdl, dCoef, thread_data[0].ElemIter,
			*thread_data[0].pWorkVec);

	pthread_mutex_lock(&thread_mutex);
	if (thread_count > 0) {
		pthread_cond_wait(&thread_cond, &thread_mutex);
	}
	pthread_mutex_unlock(&thread_mutex);

	for (unsigned i = 1; i < nThreads; i++) {
		ResHdl += *thread_data[i].pResHdl;
	}
}
#endif /* MBDYN_X_MT_ASSRES */

clock_t
MultiThreadDataManager::GetCPUTime(void) const
{
	return ((MultiThreadDataManager *)this)->ThreadDestroy();
}

#endif /* USE_MULTITHREAD */


/* $Header$ */
/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2008
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

/* Legami costitutivi */

#ifndef CONSTLTP_H
#define CONSTLTP_H

#include "withlab.h"
#include "simentity.h"
#include "tpldrive.h"

/* Tipi di cerniere deformabili */
class ConstLawType {
public:
	enum Type {
		UNKNOWN = 0x0,
		
		ELASTIC = 0x1,
		VISCOUS = 0x2,
		VISCOELASTIC = (ELASTIC | VISCOUS),
	
		LASTCONSTLAWTYPE
	};
};

/* ConstitutiveLaw - begin */
template <class T, class Tder>
class ConstitutiveLaw : public WithLabel, public SimulationEntity {
public:
	class ErrNotAvailable {
	public:
		ErrNotAvailable(void) {
			silent_cerr("Constitutive law not available "
				"for this dimensionality"
				<< std::endl);
		};
		ErrNotAvailable(std::ostream& out) {
			out << "Constitutive law not available "
				"for this dimensionality"
				<< std::endl;
		};
		ErrNotAvailable(std::ostream& out, const char* const& s) {
			out << s << std::endl;
		};
	};

	typedef typename ConstitutiveLaw<T, Tder>::ErrNotAvailable Err;   
   
protected:
	T Epsilon;		/* strain */
	T EpsilonPrime;		/* strain rate */

	T F;			/* force */
	Tder FDE;		/* force strain derivative */
	Tder FDEPrime;		/* force strain rate derivative */

public:
	ConstitutiveLaw(void)
	: WithLabel(0),
	Epsilon(0.), EpsilonPrime(0.),
	F(0.), FDE(0.), FDEPrime(0.) {
		NO_OP;
	};

	virtual ~ConstitutiveLaw(void) {
		NO_OP;
	};

	virtual ConstLawType::Type GetConstLawType(void) const = 0;

	virtual ConstitutiveLaw<T, Tder>* pCopy(void) const = 0;
	virtual std::ostream& Restart(std::ostream& out) const {
		return out;
	};
	
	virtual void Update(const T& Eps, const T& EpsPrime = 0.) = 0;

	/* DEPRECATED */
	virtual void IncrementalUpdate(const T& DeltaEps,
			const T& EpsPrime = 0.) = 0;

	virtual void AfterConvergence(const T& Eps, const T& EpsPrime = 0.) {
		NO_OP;
	};
   
	virtual const T& GetF(void) const {
		return F;
	};

	virtual const Tder& GetFDE(void) const {
		return FDE;
	};

	virtual const Tder& GetFDEPrime(void) const {
		return FDEPrime;
	};

	/* simentity */
	virtual unsigned int iGetNumDof(void) const {
		return 0;
	};

	virtual std::ostream& DescribeDof(std::ostream& out,
			const char *prefix = "",
			bool bInitial = false, int i = -1) const {
		return out;
	};

	virtual std::ostream& DescribeEq(std::ostream& out,
			const char *prefix = "",
			bool bInitial = false, int i = -1) const {
		return out;
	};

	virtual DofOrder::Order GetDofType(unsigned int i) const {
		throw ErrGeneric();
	};
};

typedef ConstitutiveLaw<doublereal, doublereal> ConstitutiveLaw1D;
typedef ConstitutiveLaw<Vec3, Mat3x3> ConstitutiveLaw3D;
typedef ConstitutiveLaw<Vec6, Mat6x6> ConstitutiveLaw6D;

/* ConstitutiveLaw - end */


/* ConstitutiveLawOwner - begin */

template <class T, class Tder>
class ConstitutiveLawOwner : public SimulationEntity {
protected:
	mutable ConstitutiveLaw<T, Tder>* pConstLaw;
   
public:
	ConstitutiveLawOwner(const ConstitutiveLaw<T, Tder>* pCL)
	: pConstLaw((ConstitutiveLaw<T, Tder>*)pCL) { 
		ASSERT(pCL != NULL);
	};
   
	virtual ~ConstitutiveLawOwner(void) {
		ASSERT(pConstLaw != NULL);
		if (pConstLaw != NULL) {
			SAFEDELETE(pConstLaw);
		}	
	};

	inline ConstitutiveLaw<T, Tder>* pGetConstLaw(void) const {
		ASSERT(pConstLaw != NULL);
		return pConstLaw; 
	};

	inline void Update(const T& Eps, const T& EpsPrime = 0.) {
		ASSERT(pConstLaw != NULL);
		pConstLaw->Update(Eps, EpsPrime);
	};

	inline void IncrementalUpdate(const T& DeltaEps,
			const T& EpsPrime = 0.) {
		ASSERT(pConstLaw != NULL);
		pConstLaw->IncrementalUpdate(DeltaEps, EpsPrime);
	};

	inline void AfterConvergence(const T& Eps, const T& EpsPrime = 0.) {
		ASSERT(pConstLaw != NULL);
		pConstLaw->AfterConvergence(Eps, EpsPrime);
	};
   
	inline const T& GetF(void) const {
		ASSERT(pConstLaw != NULL);
		return pConstLaw->GetF();
	};

	inline const Tder& GetFDE(void) const {	
		ASSERT(pConstLaw != NULL);
		return pConstLaw->GetFDE();
	};
   
	inline const Tder& GetFDEPrime(void) const {
		ASSERT(pConstLaw != NULL);
		return pConstLaw->GetFDEPrime();
	};

	/* simentity */
	virtual unsigned int iGetNumDof(void) const {
		ASSERT(pConstLaw != NULL);
		return pConstLaw->iGetNumDof();
	};

	virtual std::ostream& DescribeDof(std::ostream& out,
			const char *prefix = "",
			bool bInitial = false, int i = -1) const {
		return out;
	};

	virtual std::ostream& DescribeEq(std::ostream& out,
			const char *prefix = "",
			bool bInitial = false, int i = -1) const {
		return out;
	};

	virtual DofOrder::Order GetDofType(unsigned int i) const {
		ASSERT(pConstLaw != NULL);
		return pConstLaw->GetDofType(i);
	};

	/*
	 * Metodi per l'estrazione di dati "privati".
	 * Si suppone che l'estrattore li sappia interpretare.
	 * Come default non ci sono dati privati estraibili
	 */
	virtual unsigned int iGetNumPrivData(void) const {
		return pConstLaw->iGetNumPrivData();
	};

	/*
	 * Maps a string (possibly with substrings) to a private data;
	 * returns a valid index ( > 0 && <= iGetNumPrivData()) or 0 
	 * in case of unrecognized data; error must be handled by caller
	 */
	virtual unsigned int iGetPrivDataIdx(const char *s) const {
		return pConstLaw->iGetPrivDataIdx(s);
	};

	/*
	 * Returns the current value of a private data
	 * with 0 < i <= iGetNumPrivData()
	 */
	virtual doublereal dGetPrivData(unsigned int i) const {
		return pConstLaw->dGetPrivData(i);
	};

	virtual std::ostream& OutputAppend(std::ostream& out) const {
		return pConstLaw->OutputAppend(out);
	};
};

typedef ConstitutiveLawOwner<doublereal, doublereal> ConstitutiveLaw1DOwner;
typedef ConstitutiveLawOwner<Vec3, Mat3x3> ConstitutiveLaw3DOwner;
typedef ConstitutiveLawOwner<Vec6, Mat6x6> ConstitutiveLaw6DOwner;

/* ConstitutiveLawOwner - end */

/* functions that read a constitutive law */
extern ConstitutiveLaw<doublereal, doublereal> *
ReadCL1D(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType);
extern ConstitutiveLaw<Vec3, Mat3x3> *
ReadCL3D(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType);
extern ConstitutiveLaw<Vec6, Mat6x6> *
ReadCL6D(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType);

/* prototype of the template functional object: reads a constitutive law */
template <class T, class Tder>
struct ConstitutiveLawRead {
	virtual ~ConstitutiveLawRead<T, Tder>( void ) { NO_OP; };
	virtual ConstitutiveLaw<T, Tder> *
	Read(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType) = 0;
};

/* constitutive law registration functions: call to register one */
extern bool
SetCL1D(const char *name, ConstitutiveLawRead<doublereal, doublereal> *rf);
extern bool
SetCL3D(const char *name, ConstitutiveLawRead<Vec3, Mat3x3> *rf);
extern bool
SetCL6D(const char *name, ConstitutiveLawRead<Vec6, Mat6x6> *rf);

/* create/destroy */
extern void InitCL(void);
extern void DestroyCL(void);

#endif /* CONSTLTP_H */


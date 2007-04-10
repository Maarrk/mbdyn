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
/*
 * ACKNOWLEDGEMENTS:
 * Support for output with NetCDF is based on a contribution
 * by Patrick Rix <patrick.rix@online.de>
 */

/* gestore dell'output */

#ifndef OUTPUT_H
#define OUTPUT_H

/* se #define DEBUG_COUT l'output avviene su cout anziche' nei files */

#include <ac/iostream>
#include <ac/iomanip>
#include <ac/fstream>

#include <myassert.h>
#include <except.h>
#include <solman.h>
#include <filename.h>

#ifdef USE_NETCDF
#include <netcdfcpp.h>  
#endif /* USE_NETCDF */

/* OutputHandler - begin */

class OutputHandler : public FileName {
public:
	enum OutFiles {
		UNKNOWN			= -1,
		FIRSTFILE		= 0,
		OUTPUT			= 0,	//  0
		STRNODES,
		ELECTRIC,
		ABSTRACT,
		INERTIA,
		JOINTS,				//  5
		FORCES,
		BEAMS,
		ROTORS,
		RESTART,
		RESTARTXSOL,			// 10
		AERODYNAMIC,
		HYDRAULIC,
		PRESNODES,
		LOADABLE,
		GENELS,				// 15
		PARTITION,
		ADAMSRES,
		ADAMSCMD,
		AEROMODALS,
		REFERENCEFRAMES,		// 20
		LOG,
		AIRPROPS,
		PARAMETERS,
		EXTERNALS,
		MODAL,				// 25
		NETCDF,

		LASTFILE			// 27
	};

private:
	long currentStep;

public:
	inline void SetCurrentStep(long Step) {
		currentStep = Step;
	};
	inline void IncCurrentStep(void) {
		currentStep++;
	};
       	inline long GetCurrentStep(void) const {
		return currentStep;
	};

private:

	// flag values
	enum {
		OUTPUT_NONE			= 0x00U,
		OUTPUT_USE_DEFAULT_PRECISION	= 0x01U,
		OUTPUT_USE_SCIENTIFIC		= 0x02U,

		OUTPUT_MAY_USE_TEXT		= 0x10U,
		OUTPUT_USE_TEXT			= 0x20U,
		OUTPUT_MAY_USE_NETCDF		= 0x40U,
		OUTPUT_USE_NETCDF		= 0x80U,

		LAST
	};

	/* Aggiungere qui i files che si desidera avere a disposizione */
	struct {
		std::ofstream*	pof;
		unsigned	flags;
	} OutData[LASTFILE];
		
	// NetCDF dimensions and global attributes related to the binary file
	enum {
		DIM_TIME = 0,
		DIM_V1,
		DIM_V2,
		DIM_V3,
		DIM_V4,
		DIM_V5,
		DIM_V6,
		DIM_V7,
		DIM_V8,
		DIM_V9,
		DIM_LAST
	};
#ifdef USE_NETCDF
	NcDim	*Dim[DIM_LAST];
	NcFile  *pBinFile;   /* ! one ! binary NetCDF data file */
#endif /* USE_NETCDF */

	/* handlers to streams */
	std::ofstream ofOutput;      		/*  0 */
	std::ofstream ofStrNodes;
	std::ofstream ofElectric;
	std::ofstream ofAbstract;
	std::ofstream ofInertia;
	std::ofstream ofJoints;      		/*  5 */
	std::ofstream ofForces;
	std::ofstream ofBeams;
	std::ofstream ofRotors;
	std::ofstream ofRestart;
	std::ofstream ofRestartXSol; 		/* 10 */
	std::ofstream ofAerodynamic;
	std::ofstream ofHydraulic;
	std::ofstream ofPresNodes;
	std::ofstream ofLoadable;
	std::ofstream ofGenels;			/* 15 */
	std::ofstream ofPartition;
	std::ofstream ofAdamsRes;
	std::ofstream ofAdamsCmd;
	std::ofstream ofAeroModals;
	std::ofstream ofReferenceFrames;	/* 20 */
	std::ofstream ofLog;
	std::ofstream ofAirProps;
	std::ofstream ofParameters;
	std::ofstream ofExternals;
	std::ofstream ofModal;

	int iCurrWidth;
	int iCurrPrecision;
	int nCurrRestartFile;

	// private because we know we're using valid out index
	bool IsOpen(int out) const;
	bool UseDefaultPrecision(int out) const;
	bool UseScientific(int out) const;

	bool UseText(int out) const;
	bool UseNetCDF(int out) const;

	// Pseudo-constructor
	void OutputHandler_int(void);

public:
	OutputHandler(void);

	OutputHandler(const char* sFName, int iExtNum = -1);

	void Init(const char* sFName, int iExtNum = -1);

	virtual ~OutputHandler(void);

	/* Aggiungere qui le funzioni che aprono i singoli stream */
	bool Open(const OutputHandler::OutFiles out);

	bool IsOpen(const OutputHandler::OutFiles out) const;
	bool UseDefaultPrecision(const OutputHandler::OutFiles out) const;
	bool UseScientific(const OutputHandler::OutFiles out) const;

	void SetText(const OutputHandler::OutFiles out);
	void ClearText(void);
	void ClearText(const OutputHandler::OutFiles out);
	bool UseText(const OutputHandler::OutFiles out) const;

	void SetNetCDF(const OutputHandler::OutFiles out);
	void ClearNetCDF(void);
	void ClearNetCDF(const OutputHandler::OutFiles out);
	bool UseNetCDF(const OutputHandler::OutFiles out) const;

	bool Close(const OutputHandler::OutFiles out);

	bool OutputOpen(void);
	bool RestartOpen(bool openResXSol = false);

	bool PartitionOpen(void);
	bool AdamsResOpen(void);
	bool AdamsCmdOpen(void);
	bool LogOpen(void);

	/* Aggiungere qui le funzioni che ritornano gli stream desiderati */
	inline std::ostream& Get(const OutputHandler::OutFiles f);

	inline std::ostream& Output(void) const;
	inline std::ostream& StrNodes(void) const;
	inline std::ostream& Electric(void) const;
	inline std::ostream& Abstract(void) const;
	inline std::ostream& Inertia(void) const;
	inline std::ostream& Joints(void) const;
	inline std::ostream& Forces(void) const;
	inline std::ostream& Beams(void) const;
	inline std::ostream& Rotors(void) const;
	inline std::ostream& Restart(void) const;
	inline std::ostream& RestartXSol(void) const;
	inline std::ostream& Aerodynamic(void) const;
	inline std::ostream& Hydraulic(void) const;
	inline std::ostream& PresNodes(void) const;
	inline std::ostream& Loadable(void) const;
	inline std::ostream& Genels(void) const;
	inline std::ostream& Partition(void) const;
	inline std::ostream& AdamsRes(void) const;
	inline std::ostream& AdamsCmd(void) const;
	inline std::ostream& AeroModals(void) const;
	inline std::ostream& ReferenceFrames(void) const;
	inline std::ostream& Log(void) const;
	inline std::ostream& AirProps(void) const;
	inline std::ostream& Parameters(void) const;
	inline std::ostream& Externals(void) const;
	inline std::ostream& Modal(void) const;

	inline int iW(void) const;
	inline int iP(void) const;

	void SetWidth(int iNewWidth);
	void SetPrecision(int iNewPrecision);

#ifdef USE_NETCDF
	inline NcFile* pGetBinFile(void) const;

	inline const NcDim* DimTime(void) const;
	inline const NcDim* DimV1(void) const;
	inline const NcDim* DimV2(void) const;
	inline const NcDim* DimV3(void) const;
	inline const NcDim* DimV4(void) const;
	inline const NcDim* DimV5(void) const;
	inline const NcDim* DimV6(void) const;
	inline const NcDim* DimV7(void) const;
	inline const NcDim* DimV8(void) const;
	inline const NcDim* DimV9(void) const;
#endif /* USE_NETCDF */
}; /* End class OutputHandler */

#ifdef USE_NETCDF
inline NcFile *
OutputHandler::pGetBinFile(void) const
{
	return pBinFile;
}

inline const NcDim *
OutputHandler::DimTime(void) const
{
	return Dim[DIM_TIME];
}

inline const NcDim *
OutputHandler::DimV1(void) const
{
	return Dim[DIM_V1];
}

inline const NcDim *
OutputHandler::DimV2(void) const
{
	return Dim[DIM_V2];
}

inline const NcDim *
OutputHandler::DimV3(void) const
{
	return Dim[DIM_V3];
}

inline const NcDim *
OutputHandler::DimV4(void) const
{
	return Dim[DIM_V4];
}

inline const NcDim *
OutputHandler::DimV5(void) const
{
	return Dim[DIM_V5];
}

inline const NcDim *
OutputHandler::DimV6(void) const
{
	return Dim[DIM_V6];
}

inline const NcDim *
OutputHandler::DimV7(void) const
{
	return Dim[DIM_V7];
}

inline const NcDim *
OutputHandler::DimV8(void) const
{
	return Dim[DIM_V8];
}

inline const NcDim *
OutputHandler::DimV9(void) const
{
	return Dim[DIM_V9];
}
#endif /* USE_NETCDF */

inline std::ostream&
OutputHandler::Get(const OutputHandler::OutFiles f)
{
	ASSERT(f > -1 && f < LASTFILE);
	ASSERT(IsOpen(f));
	return *(OutData[f].pof);
}

inline std::ostream&
OutputHandler::Output(void) const
{
#ifdef DEBUG_COUT
	return (std::ostream&)cout;
#else
	ASSERT(IsOpen(OUTPUT));
	return (std::ostream&)ofOutput;
#endif
}

inline std::ostream&
OutputHandler::StrNodes(void) const
{
	ASSERT(IsOpen(STRNODES));
	return (std::ostream&)ofStrNodes;
}

inline std::ostream&
OutputHandler::Electric(void) const
{
	ASSERT(IsOpen(ELECTRIC));
	return (std::ostream&)ofElectric;
}

inline std::ostream&
OutputHandler::Abstract(void) const
{
	ASSERT(IsOpen(ABSTRACT));
	return (std::ostream&)ofAbstract;
}

inline std::ostream&
OutputHandler::Inertia(void) const
{
	ASSERT(IsOpen(INERTIA));
	return (std::ostream&)ofInertia;
}

inline std::ostream&
OutputHandler::Joints(void) const
{
	ASSERT(IsOpen(JOINTS));
	return (std::ostream&)ofJoints;
}

inline std::ostream&
OutputHandler::Forces(void) const
{
	ASSERT(IsOpen(FORCES));
	return (std::ostream&)ofForces;
}

inline std::ostream&
OutputHandler::Beams(void) const
{
	ASSERT(IsOpen(BEAMS));
	return (std::ostream&)ofBeams;
}

inline std::ostream&
OutputHandler::Rotors(void) const
{
	ASSERT(IsOpen(ROTORS));
	return (std::ostream&)ofRotors;
}

inline std::ostream&
OutputHandler::Restart(void) const
{
	ASSERT(IsOpen(RESTART));
	return (std::ostream&)ofRestart;
}

inline std::ostream&
OutputHandler::RestartXSol(void) const
{
	ASSERT(IsOpen(RESTART));
	return (std::ostream&)ofRestartXSol;
}

inline std::ostream&
OutputHandler::Aerodynamic(void) const
{
	ASSERT(IsOpen(AERODYNAMIC));
	return (std::ostream&)ofAerodynamic;
}

inline std::ostream&
OutputHandler::Hydraulic(void) const
{
	ASSERT(IsOpen(HYDRAULIC));
	return (std::ostream&)ofHydraulic;
}

inline std::ostream&
OutputHandler::PresNodes(void) const
{
	ASSERT(IsOpen(PRESNODES));
	return (std::ostream&)ofPresNodes;
}

inline std::ostream&
OutputHandler::Loadable(void) const
{
	ASSERT(IsOpen(LOADABLE));
	return (std::ostream&)ofLoadable;
}

inline std::ostream&
OutputHandler::Genels(void) const
{
	ASSERT(IsOpen(GENELS));
	return (std::ostream&)ofGenels;
}

inline std::ostream&
OutputHandler::Partition(void) const
{
	ASSERT(IsOpen(PARTITION));
	return (std::ostream&)ofPartition;
}

inline std::ostream&
OutputHandler::AdamsRes(void) const
{
	ASSERT(IsOpen(ADAMSRES));
	return (std::ostream&)ofAdamsRes;
}

inline std::ostream&
OutputHandler::AdamsCmd(void) const
{
	ASSERT(IsOpen(ADAMSCMD));
	return (std::ostream&)ofAdamsCmd;
}

inline std::ostream&
OutputHandler::AeroModals(void) const
{
	ASSERT(IsOpen(AEROMODALS));
	return (std::ostream&)ofAeroModals;
}

inline std::ostream&
OutputHandler::ReferenceFrames(void) const
{
	ASSERT(IsOpen(REFERENCEFRAMES));
	return (std::ostream&)ofReferenceFrames;
}

inline std::ostream&
OutputHandler::Log(void) const
{
#ifdef DEBUG_COUT
	return (std::ostream&)cout;
#else
	ASSERT(IsOpen(LOG));
	return (std::ostream&)ofLog;
#endif
}

inline std::ostream&
OutputHandler::AirProps(void) const
{
	ASSERT(IsOpen(AIRPROPS));
	return (std::ostream&)ofAirProps;
}

inline std::ostream&
OutputHandler::Parameters(void) const
{
	ASSERT(IsOpen(PARAMETERS));
	return (std::ostream&)ofParameters;
}

inline std::ostream&
OutputHandler::Externals(void) const
{
	ASSERT(IsOpen(EXTERNALS));
	return (std::ostream&)ofExternals;
}

inline std::ostream&
OutputHandler::Modal(void) const
{
	ASSERT(IsOpen(MODAL));
	return (std::ostream&)ofModal;
}

inline int
OutputHandler::iW(void) const
{
	return iCurrWidth;
}

inline int
OutputHandler::iP(void) const
{
	return iCurrPrecision;
}

extern OutputHandler OutHdl;

/* OutputHandler - end */


/* ToBeOutput - begin */

const flag fDefaultOut = 1;

class ToBeOutput {
protected:
	flag fOutput;

public:
	ToBeOutput(flag fOut = fDefaultOut);
	virtual ~ToBeOutput(void);

	virtual void OutputPrepare(OutputHandler &OH);

	/* Regular output */
	virtual void Output(OutputHandler& OH) const;

	/* Output of perturbed solution (modes ...) */
	virtual void Output(OutputHandler& OH,
	const VectorHandler& X, const VectorHandler& XP) const;

	/* Output of modes in NASTRAN's pch/f06 format */
	enum {
		NASTRAN_FORMAT_FIXED	= 1,
		NASTRAN_FORMAT_FIXED16	= 2,
		NASTRAN_FORMAT_FREE	= 3
	};

#if 0
	/* TODO: NASTRAN-like output? */
	virtual void Output_pch(std::ostream &pch) const;
	virtual void Output_f06(std::ostream &f06, const VectorHandler& X) const;
	virtual void Output_f06(std::ostream &f06,
		const VectorHandler& Xr, const VectorHandler& Xi) const;
#endif

#if 0
	/* TODO: ADAMS-like output? */
	virtual void AdamsOutput(void) const;
#endif

	virtual flag fToBeOutput(void) const;
	virtual void SetOutputFlag(flag f = flag(1));
};

/* ToBeOutput - end */

#endif /* OUTPUT_H */


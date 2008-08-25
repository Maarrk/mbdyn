/* $Header$ */
/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 2007-2008
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

#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#include "dataman.h"
#include "extedge.h"
#include "except.h"

#include <fstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

/* ExtFileHandlerEDGE - begin */

ExtFileHandlerEDGE::ExtFileHandlerEDGE(std::string& fflagname,
	std::string& fdataname, int iSleepTime)
: fflagname(fflagname), fdataname(fdataname),
iSleepTime(iSleepTime), bReadForces(true)
{
	NO_OP;
}

ExtFileHandlerEDGE::~ExtFileHandlerEDGE(void)
{
	NO_OP;
}

ExtFileHandlerEDGE::EDGEcmd
ExtFileHandlerEDGE::CheckFlag(int& cnt)
{
	cnt++;

	inf.open(fflagname.c_str());

#ifdef USE_SLEEP
	if (iSleepTime > 0) {
		// WARNING: loops forever
		// add optional, configurable limit?
		for (; !inf; cnt++) {
			silent_cout("flag file \"" << fflagname.c_str() << "\" "
				"missing, try #" << cnt << "; "
				"sleeping " << iSleepTime << " s" << std::endl); 

			sleep(iSleepTime);

			inf.clear();
			inf.open(fdataname.c_str());
		}
	} else
#endif // USE_SLEEP
	{
		// error
		silent_cerr("flag file \"" << fflagname.c_str() << "\" "
			"missing" << std::endl); 
		throw ErrGeneric();
	}

	int cmd;
	inf >> cmd;

	inf.close();
	inf.clear();

	return EDGEcmd(cmd);
}


std::ostream&
ExtFileHandlerEDGE::Send_pre(bool bAfterConvergence)
{
	// open data file for writing
	int cnt = 0;
	if (CheckFlag(cnt) == EDGE_READ_READY) {
		outf.open(fdataname.c_str());
		if (!outf) {
			silent_cerr("unable to open data file "
				"\"" << fdataname.c_str() << "\" "
				"for output" << std::endl);
			throw ErrGeneric();
		}

		outf.setf(std::ios::scientific);

	} else {
		outf.setstate(std::ios_base::badbit);
	}

	return outf;
}

void
ExtFileHandlerEDGE::Send_post(bool bAfterConvergence)
{
	// close data file after writing
	if (outf.is_open()) {
		outf.close();
	}

	if (bAfterConvergence) {
		outf.open("update.ainp");
		outf << "UPDATE,N,0,0,1\n"
			"IBREAK,I,1,1,0\n"
			"5\n";
		outf.close();

		// re-set flag
		bReadForces = true;
	}

	outf.open(fflagname.c_str());
	outf << 3;
	outf.close();
	outf.clear();
}

std::istream&
ExtFileHandlerEDGE::Recv_pre(void)
{
	for (int cnt = 0; ;) {

		EDGEcmd cmd = CheckFlag(cnt);
		switch (cmd) {
		case EDGE_READ_READY:
			goto done;

		case EDGE_GOTO_NEXT_STEP:
			// FIXME: EDGE is done; do not read forces,
			// keep using old
			bReadForces = false;
			goto done;

		case EDGE_QUIT:
			silent_cout("EDGE requested end of simulation"
				<< std::endl);
			throw NoErr();

		default:
			break;
		}

		// WARNING: loops forever
		// add optional, configurable limit?
#ifdef USE_SLEEP
		if (iSleepTime > 0) {
			silent_cout("flag file \"" << fflagname.c_str() << "\": "
				"cmd=" << cmd << " try #" << cnt << "; "
				"sleeping " << iSleepTime << " s" << std::endl); 

			sleep(iSleepTime);
		} else
#endif // USE_SLEEP
		{
			silent_cout("flag file \"" << fflagname.c_str() << "\": "
				"cmd=" << cmd << " try #" << cnt << std::endl); 
		}
	}

done:;
	if (bReadForces) {
		inf.open(fdataname.c_str());
		if (!inf) {
			silent_cerr("unable to open data file "
				"\"" << fdataname.c_str() << "\" "
				"for input" << std::endl);
			throw ErrGeneric();
		}

	} else {
		inf.setstate(std::ios_base::badbit);
	}

	return inf;
}

void
ExtFileHandlerEDGE::Recv_post(void)
{
	if (inf.is_open()) {
		inf.close();
	}
}

/* ExtFileHandlerEDGE - end */

ExtFileHandlerBase *
ReadExtFileHandlerEDGE(DataManager* pDM,
	MBDynParser& HP, 
	unsigned int uLabel)
{
	ExtFileHandlerBase *pEFH = 0;

	const char *s = HP.GetFileName();
	if (s == 0) {
		silent_cerr("ExtForceEDGE(" << uLabel << "): "
			"unable to get flag file name "
			"at line " << HP.GetLineData() << std::endl);
		throw ErrGeneric();
	}
	std::string fflagname = s;

	s = HP.GetFileName();
	if (s == 0) {
		silent_cerr("ExtForceEDGE(" << uLabel << "): "
			"unable to get data file name "
			"at line " << HP.GetLineData() << std::endl);
		throw ErrGeneric();
	}
	std::string fdataname = s;

	int iSleepTime = 1;
	if (HP.IsKeyWord("sleep" "time")) {
		iSleepTime = HP.GetInt();
		if (iSleepTime <= 0 ) {
			silent_cerr("ExtForce(" << uLabel << "): "
				"invalid sleep time " << iSleepTime
				<< " at line " << HP.GetLineData()
				<< std::endl);
			throw ErrGeneric();
		}
	}

	SAFENEWWITHCONSTRUCTOR(pEFH, ExtFileHandlerEDGE,
		ExtFileHandlerEDGE(fflagname, fdataname, iSleepTime));

	return pEFH;
}

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

#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#include <simentity.h>

/* SimulationEntity - begin */

SimulationEntity::SimulationEntity(void)
{
	NO_OP;
}

SimulationEntity::~SimulationEntity(void)
{
	NO_OP;
}

flag 
SimulationEntity::fIsValidIndex(unsigned int i) const
{
	if (i >= 1 && i <= iGetNumDof()) {
		return flag(1);
	}
	return flag(0);
}

void 
SimulationEntity::SetValue(VectorHandler& /* X */ , 
		VectorHandler& /* XP */ ) const
{
	NO_OP;
}
	         
void 
SimulationEntity::BeforePredict(VectorHandler& /* X */ ,
		VectorHandler& /* XP */ ,
		VectorHandler& /* XPrev */ ,
		VectorHandler& /* XPPrev */ ) const
{
	NO_OP;
}
	
void 
SimulationEntity::AfterPredict(VectorHandler& /* X */ , 
		VectorHandler& /* XP */ )
{
	NO_OP;
}

void
SimulationEntity::Update(const VectorHandler& /* XCurr */ , 
		const VectorHandler& /* XPrimeCurr */ )
{
	NO_OP;
}

void 
SimulationEntity::AfterConvergence(const VectorHandler& /* X */ , 
		const VectorHandler& /* XP */ )
{
	NO_OP;
}

unsigned int
SimulationEntity::iGetNumPrivData(void) const 
{
	return 0;
}

unsigned int
SimulationEntity::iGetPrivDataIdx(const char *s) const 
{
	std::cerr << "no private data available" << std::endl;
	THROW(ErrGeneric());
}

doublereal
SimulationEntity::dGetPrivData(unsigned int /* i */ ) const
{
	std::cerr << "no private data available" << std::endl;
	THROW(ErrGeneric());
}

std::ostream&
SimulationEntity::OutputAppend(std::ostream& out) const
{
	return out;
}

/* SimulationEntity - end */


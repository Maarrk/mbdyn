/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2000
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
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
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
 * Copyright 1999-2000 Lamberto Puggelli <puggelli@tiscalinet.it>
 * Dipartimento di Ingegneria Aerospaziale - Politecnico di Milano
 */

#ifndef PRESNODE_H
#define PRESNODE_H

#include "node.h"

class PressureNode : public ScalarAlgebraicNode {
 public:
   PressureNode(unsigned int uL, const DofOwner* pDO, 
		doublereal dx, flag fOut) 
     : ScalarAlgebraicNode(uL, pDO, dx, fOut) {
	NO_OP;
     };
   
   virtual ~PressureNode(void) { 
      NO_OP;      
   };
   
   virtual Node::Type GetNodeType(void) const {
      return Node::HYDRAULIC;
   };
   
   ostream& Restart(ostream& out) const {
      return out << "Pressure Node: not implemented yet!" << endl;
   };
     
   void AfterPredict(VectorHandler&, VectorHandler&) {
      NO_OP;
   };
   
   void Update(const class VectorHandler& X, const class VectorHandler&) {
      dX = X.dGetCoef(iGetFirstIndex()+1);
   };
   
   void Output(OutputHandler& OH) const {
      OH.PresNodes() << setw(8) << GetLabel() << " "
	<< dX << endl;
   };
};

#endif

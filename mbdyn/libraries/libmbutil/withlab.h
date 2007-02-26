/* $Header$ */
/*
 * This library comes with MBDyn (C), a multibody analysis code.
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2007
 *
 * Pierangelo Masarati  <masarati@aero.polimi.it>
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

/* Classe da cui derivare gli oggetti dotati di label */


#ifndef WITHLAB_H
#define WITHLAB_H

#include "myassert.h"


/* WithLabel - begin */

class WithLabel {
protected:
	unsigned int uLabel;
	const char *sName;
	
	void DeleteName(void) {
		if (sName != NULL) {
			SAFEDELETEARR(sName);
			sName = NULL;
		}
	};
	
public:
   	WithLabel(unsigned int uL = 0, const char *sN = NULL) 
	: uLabel(uL), sName(sN) {
      		if (sN != NULL) {
			PutName(sN);
		}
   	};
	
   	virtual ~WithLabel(void) {
      		DeleteName();
   	};

        void PutLabel(unsigned int uL) {
		uLabel = uL;
	};
	
	void PutName(const char *sN) {
		DeleteName();
		SAFESTRDUP(sName, sN);
	};
   
   	unsigned int GetLabel(void) const {
      		return uLabel; 
   	};

	const char *GetName(void) const {
		return sName;
	};	
};
   
/* WithLabel - end */

#endif

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

/* Strutture e classi relative ai Dof */

#ifndef DOFOWN_H
#define DOFOWN_H


#include <myassert.h>
#include <myf2c.h>       /* per integer! */

extern const char* psDofOwnerNames[];   

/* ordine dei dof */
class DofOrder {
 public:
   enum Order {
      UNKNOWN = -1,
	ALGEBRAIC = 0,
	DIFFERENTIAL,
	
	LASTORDER
   };
};


/* strutture per i Dof */

class VectorHandler;

struct Dof {
   integer iIndex;
   DofOrder::Order Order;
};


struct DofOwner {
/* Tipi di DofOwner */
 public:
   enum Type {
      UNKNOWN = -1,
	STRUCTURALNODE = 0,
	ELECTRICNODE,
	ABSTRACTNODE,
	HYDRAULICNODE,
	
	JOINT,
	GENEL,
	ROTOR,
	UNSTEADYAERO,
	ELECTRICBULK,
	ELECTRIC,
	HYDRAULIC,	
	LOADABLE,
	
	LASTDOFTYPE
   };
   
   integer iFirstIndex;
   unsigned int iNumDofs;
};


/* Da questa classe derivano tutti gli elementi che possiedono Dof 
 * e che quindi sono esplicitamente costretti a dichiarare il metodo con cui 
 * inizializzanop i vettori della soluzione */
class DofOwnerOwner {
 private:
   const DofOwner* pDofOwner;
   
 public:   
   DofOwnerOwner(const DofOwner* pDO) : pDofOwner(pDO) { 
      ASSERT(pDofOwner != NULL);
   };
   
   inline const DofOwner* pGetDofOwner(void) const {
      ASSERT(pDofOwner != NULL);
      return pDofOwner; 
   };
   
   /* Restituisce l'indice (-1) del primo Dof del nodo. Per ipotesi, 
    * gli indici di eventuali altri Dof sono consecutivi.
    * Il primo Dof viene indirizzato nel modo seguente: 
    * - doublereal::X[Node::iGetFirstIndex()]   se si usa un array c,
    * - VectorHandler::dGetCoef(Node::iGetFirstIndex()+1) se si usa un handler
    * questa convenzione e' stata assunta per compatibilita' con le
    * porzioni di codice scritte in FORTRAN */
   inline integer iGetFirstIndex(void) const { 
      return pDofOwner->iFirstIndex; 
   };   
   
   virtual void SetInitialValue(VectorHandler& /* X */ ) const { 
      NO_OP; 
   };
   
   // virtual void SetValue(VectorHandler& X, VectorHandler& XP) const = 0;
};

#endif

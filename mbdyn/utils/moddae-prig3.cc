/*
 * MBDyn (C) is a multibody analysis code.
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2004
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

#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#include <ac/fstream>
#include <ac/iostream>
#include <math.h>

#include <myassert.h>
#include <solman.h>
#include "dae-intg.h"

struct private_data {
   doublereal l;
   doublereal g;
   doublereal thetaold;
   doublereal x[6];
   doublereal xP[6];
};

static int read(void** pp, const char* user_defined)
{
   *pp = (void*)new private_data;
   private_data* pd = (private_data*)*pp;
   
   if (user_defined != NULL) {
      // cerr << "opening file \"" << user_defined << "\"" << endl;
      std::ifstream in(user_defined);
      if (!in) {
	 std::cerr << "unable to open file \"" << user_defined << "\"" << std::endl;
	 exit(EXIT_FAILURE);
      }
      in >> pd->l >> pd->g >> pd->x[0] >> pd->x[1]
	      >> pd->x[2] >> pd->x[3] >> pd->xP[4] >> pd->xP[5];
   } else {
      pd->l = 1.;
      pd->g = 9.81;     
      pd->x[0] = 0.;
      pd->x[1] = 1.;
      pd->x[2] = 1.;
      pd->x[3] = 0.;
      pd->xP[4] = 0.;
      pd->xP[5] = 0.;
   }
   pd->x[4] = 0.;
   pd->x[5] = 0.;
   pd->thetaold = atan2(pd->x[0], -pd->x[1]);

   pd->xP[0] = pd->x[2];
   pd->xP[1] = pd->x[3];

   doublereal phi = sqrt(pd->x[0]*pd->x[0] + pd->x[1]*pd->x[1]) - pd->l;
   if (fabs(phi) > 1.e-9) {
      std::cerr << "constraint violated" << std::endl;
      exit(EXIT_FAILURE);
   }
   
   doublereal psi = (pd->x[0]*pd->xP[0] + pd->x[1]*pd->xP[1])/pd->l;
   if (fabs(psi) > 1.e-9) {
      std::cerr << "constraint derivative violated" << std::endl;
      exit(EXIT_FAILURE);
   }

   doublereal lambda = pd->l*(pd->x[2]*pd->x[2] + pd->x[3]*pd->x[3]
		   - pd->x[1]*pd->g)/pd->l;

   if (fabs(lambda - pd->xP[4]) > 1.e-9) {
	   std::cerr << "constraint reaction incorrect" << std::endl;
	   pd->xP[4] = lambda;
   }

   doublereal mu = 0.;
   /* FIXME: add computation of mu */
   pd->xP[5] = mu;
   
   pd->xP[2] = - (pd->x[0]*pd->xP[4])/pd->l;
   pd->xP[3] = - ((pd->x[1]*pd->xP[4])/pd->l + pd->g);

   std::cerr 
     << "l=" << pd->l << ", g=" << pd->g << std::endl
     << "x={" << pd->x[0] << "," << pd->x[1] << "," << pd->x[2]
     << "," << pd->x[3] << "," << pd->x[4] << "," << pd->x[5] 
     << "}" << std::endl
     << "xP={" << pd->xP[0] << "," << pd->xP[1] << "," << pd->xP[2]
     << "," << pd->xP[3] << "," << pd->xP[4] << "," << pd->xP[5]
     << "}" << std::endl;
   
   return 0;
}

static int size(void* p)
{
   /* private_data* pd = (private_data*)p; */
   return 6;
}

static int init(void* p, VectorHandler& X, VectorHandler& XP)
{
   private_data* pd = (private_data*)p;
   X.Reset();
   XP.Reset();
   for (int i = 1; i <= size(p); i++) {
      XP.PutCoef(i, pd->xP[i-1]); /* posiz. iniziale */
      X.PutCoef(i, pd->x[i-1]); /* posiz. iniziale */
   }
   return 0;
}

static int grad(void* p, MatrixHandler& J, MatrixHandler& JP, 
		const VectorHandler& X, const VectorHandler& XP,
		const doublereal& t)
{
   private_data* pd = (private_data*)p;
   
   doublereal x = X.dGetCoef(1);
   doublereal y = X.dGetCoef(2);
   doublereal u = X.dGetCoef(3);
   doublereal v = X.dGetCoef(4);
   doublereal lambda = XP.dGetCoef(5);
   doublereal mu = XP.dGetCoef(6);
   
   doublereal l = pd->l;
   
   J.PutCoef(1, 3, -1.);
   J.PutCoef(2, 4, -1.);
   J.PutCoef(3, 1, lambda/l);
   J.PutCoef(4, 2, lambda/l);
   J.PutCoef(3, 3, mu/l);
   J.PutCoef(4, 4, mu/l);
   J.PutCoef(5, 1, x/l);
   J.PutCoef(5, 2, y/l);
   J.PutCoef(6, 1, u/l);
   J.PutCoef(6, 2, v/l);
   J.PutCoef(6, 3, x/l);
   J.PutCoef(6, 4, y/l);

   for (int i = 1; i <= 4; i++) {
	   JP.PutCoef(i, i, 1.);
   }
   JP.PutCoef(1, 6, x/l);
   JP.PutCoef(2, 6, y/l);
   JP.PutCoef(3, 5, x/l);
   JP.PutCoef(4, 5, y/l);
   JP.PutCoef(3, 6, u/l);
   JP.PutCoef(4, 6, v/l);

   return 0;
}

static int func(void* p, VectorHandler& R, const VectorHandler& X, const VectorHandler& XP, const doublereal& t)
{
   private_data* pd = (private_data*)p;
   
   doublereal x = X.dGetCoef(1);
   doublereal y = X.dGetCoef(2);
   doublereal u = X.dGetCoef(3);
   doublereal v = X.dGetCoef(4);
   doublereal xP = XP.dGetCoef(1);
   doublereal yP = XP.dGetCoef(2);
   doublereal uP = XP.dGetCoef(3);
   doublereal vP = XP.dGetCoef(4);
   doublereal lambda = XP.dGetCoef(5);
   doublereal mu = XP.dGetCoef(6);
   
   doublereal l = pd->l;
   doublereal g = pd->g;

   R.PutCoef(1, u - xP - mu*x/l);
   R.PutCoef(2, v - yP - mu*y/l);
   R.PutCoef(3, - (uP + lambda*x/l + mu*u/l));
   R.PutCoef(4, - (vP + lambda*y/l + mu*v/l + g));
   R.PutCoef(5, l - sqrt(x*x + y*y));
   R.PutCoef(6, - (u*x + v*y)/l);

   return 0;
}

static std::ostream& out(void* p, std::ostream& o, 
	     const VectorHandler& X, const VectorHandler& XP)
{
   private_data* pd = (private_data*)p;

   doublereal x = X.dGetCoef(1);
   doublereal y = X.dGetCoef(2);
   doublereal xP = X.dGetCoef(3);
   doublereal yP = X.dGetCoef(4);
   doublereal lambda = XP.dGetCoef(5);
   doublereal mu = XP.dGetCoef(6);
  
   doublereal theta = atan2(x, -y);
   while (theta > pd->thetaold + M_PI_2) {
	   theta -= M_PI;
   }

   while (theta < pd->thetaold - M_PI_2) {
	   theta += M_PI;
   }

   pd->thetaold = theta;

   doublereal phi = 0.;
   if (fabs(x) > fabs(y)) {
	   phi = yP/x;
   } else {
	   phi = -xP/y;
   }
   doublereal g = pd->g;
   
   doublereal E = .5*(xP*xP+yP*yP)+g*y;
  
   
   return o
	   << theta			/*  3 */
	   << " " << phi		/*  4 */
	   << " " << XP.dGetCoef(1)	/*  5 */
	   << " " << XP.dGetCoef(2)	/*  6 */
	   << " " << x 			/*  7 */
	   << " " << y 			/*  8 */
	   << " " << xP			/*  9 */
	   << " " << yP			/* 10 */
	   << " " << E			/* 11 */
	   << " " << lambda		/* 12 */
	   << " " << mu;		/* 13 */
}

static int destroy(void** p)
{
   private_data* pd = (private_data*)(*p);
   delete pd;
   *p = NULL;
   return 0;
}

static struct funcs funcs_handler = {
	read,
	init,
	size,
	grad,
	func,
	out,
	destroy
};

/* de-mangle name */
extern "C" {
void *ff = &funcs_handler;
}


/* $Header$ */
/* 
 * HmFe (C) is a FEM analysis code. 
 *
 * Copyright (C) 1996-2008
 *
 * Marco Morandini  <morandini@aero.polimi.it>
 * Teodoro Merlini  <merlini@aero.polimi.it>
 *
 * Dipartimento di Ingegneria Aerospaziale - Politecnico di Milano
 * via La Masa, 34 - 20156 Milano, Italy
 * http://www.aero.polimi.it
 *
 * Changing this copyright notice is forbidden.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
/*
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2008
 *
 * This code is a partial merge of HmFe and MBDyn.
 *
 * Pierangelo Masarati  <masarati@aero.polimi.it>
 * Paolo Mantegazza     <mantegazza@aero.polimi.it>
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

#include <iostream>
#include <mathtyp.h>
#include <matvecexp.h>
#include <RotCoeff.hh>
#include <Rot.hh>

using namespace RotCoeff;

Mat3x3 RotManip::Rot(const Vec3 & phi) {
	doublereal coeff[COEFF_B];

	CoeffB(phi,phi,coeff);

	Mat3x3 Phi(1., phi*coeff[0]);		/* I + c[0] * phi x */
	Phi += Mat3x3(phi, phi*coeff[1]);	/* += c[1] * phi x phi x */

	return Phi;
};


Mat3x3 RotManip::DRot(const Vec3 & phi) {
	doublereal coeff[COEFF_C];

	CoeffC(phi,phi,coeff);

	Mat3x3 Ga(1., phi*coeff[1]);		/* I + c[0] * phi x */
	Ga += Mat3x3(phi, phi*coeff[2]);	/* += c[1] * phi x phi x */

	return Ga;
};


void RotManip::RotAndDRot(const Vec3 & phi, Mat3x3 & Phi, Mat3x3 & Ga) {
	doublereal coeff[COEFF_C];

	CoeffC(phi,phi,coeff);

	Phi = Mat3x3(1., phi*coeff[0]);
	Phi += Mat3x3(phi, phi*coeff[1]);

	Ga = Mat3x3(1., phi*coeff[1]);
	Ga += Mat3x3(phi, phi*coeff[2]);

	return;
};


Mat3x3 RotManip::DRot_IT(const Vec3 & phi) {
	doublereal coeff[COEFF_D], coeffs[COEFF_C_STAR];
	
	CoeffCStar(phi,phi,coeff,coeffs);

	Mat3x3 GaIT(1., phi*.5);
	GaIT += Mat3x3(phi, phi*coeffs[0]);

	return GaIT;
};

Mat3x3 RotManip::DRot_I(const Vec3 & phi) {
	doublereal coeff[COEFF_D], coeffs[COEFF_C_STAR];
	
	CoeffCStar(phi,phi,coeff,coeffs);

	Mat3x3 GaI(1., phi*(-.5));
	GaI += Mat3x3(phi, phi*coeffs[0]);

	return GaI;
};


void RotManip::RotAndDRot_IT(const Vec3 & phi, Mat3x3 & PhiIT, Mat3x3 & GaIT) {
	doublereal coeff[COEFF_D], coeffs[COEFF_C_STAR];

	CoeffCStar(phi,phi,coeff,coeffs);

	PhiIT = Mat3x3(1., phi*coeff[0]);
	PhiIT += Mat3x3(phi, phi*coeff[1]);

	GaIT = Mat3x3(1., phi*.5);
	GaIT += Mat3x3(phi, phi*coeffs[0]);

	return;
};

Vec3 RotManip::VecRot(const Mat3x3 & Phi) {
	doublereal phi, a, cosphi, sinphi;
	Vec3 unit;

	cosphi = .5*(Phi.Trace()-1.);
	if (cosphi > 0.) {
		unit = Phi.Ax();
		sinphi = unit.Norm();
		phi = atan2(sinphi, cosphi);
		CoeffA(phi, phi, &a);
		unit = unit/a;
	} else {
		Mat3x3 eet(Phi.Symm());
		eet -= Mat3x3(cosphi);
		eet /= (1.-cosphi);
		Int maxcol = 1;
		if ((eet.dGet(2, 2) > eet.dGet(1, 1))
				||(eet.dGet(3, 3) > eet.dGet(1, 1))) {
			maxcol = 2;
			if (eet.dGet(3, 3) > eet.dGet(2, 2)) {
				maxcol = 3;
			}
		}
		unit = (eet.GetVec(maxcol)/sqrt(eet.dGet(maxcol, maxcol)));
		sinphi = (Mat3x3(unit)*Phi).Trace()*(-.5);
		unit *= atan2(sinphi, cosphi);
	}
	return unit;
};

Mat3x3 RotManip::Elle
        (const Vec3 & phi,
        const Vec3 & a) {
    doublereal coeff[COEFF_E];
    CoeffE(phi,phi,coeff);
    
    Mat3x3 L(a*-coeff[1]);    
    L -= Mat3x3(phi,a*coeff[2]);
    L -= Mat3x3(phi.Cross(a*coeff[2]));
    L += (phi.Cross(a)).Tens(phi*coeff[3]);
    L += (Mat3x3(phi,phi)*a).Tens(phi*coeff[4]);

    return L;
};

MatExp RoTrManip::Elle
        (const VecExp & phi,
        const VecExp & a) {
    ScalExp coeff[COEFF_E];
    CoeffE(phi,phi.GetVec(),coeff);
    
    MatExp L(a*(-coeff[1]));
    L -= MatExp(phi,a*coeff[2]);
    L -= MatExp(phi.Cross(a*coeff[2]));
    L += (phi.Cross(a)).Tens(phi*coeff[3]);
    L += (MatExp(phi,phi)*a).Tens(phi*coeff[4]);

    return L;
};

MatExp RoTrManip::RoTr(const VecExp & phi) {
	ScalExp coeff[COEFF_B];

	CoeffB(phi,phi.GetVec(),coeff);

	MatExp Phi(1., phi*coeff[0]);	/* I + c[0] * phi x */
	Phi += MatExp(phi, phi*coeff[1]);	/* += c[1] * phi x phi x */

	return Phi;
};

MatExp RoTrManip::DRoTr(const VecExp & phi) {
	ScalExp coeff[COEFF_C];

	CoeffC(phi,phi.GetVec(),coeff);

	MatExp Ga(1., phi*coeff[1]);		/* I + c[0] * phi x */
	Ga += MatExp(phi, phi*coeff[2]);	/* += c[1] * phi x phi x */

	return Ga;
};

void RoTrManip::RoTrAndDRoTr(const VecExp & phi, MatExp & Phi, MatExp & Ga) {
	ScalExp coeff[COEFF_C];

	CoeffC(phi,phi.GetVec(),coeff);

	Phi = MatExp(1., phi*coeff[0]);
	Phi += MatExp(phi, phi*coeff[1]);

	Ga = MatExp(1., phi*coeff[1]);
	Ga += MatExp(phi, phi*coeff[2]);

	return;
};

MatExp RoTrManip::DRoTr_It(const VecExp & phi) {
	ScalExp coeff[COEFF_D], coeffs[COEFF_C_STAR];
	
	CoeffCStar(phi,phi.GetVec(),coeff,coeffs);

	MatExp GaIT(1., phi*.5);
	GaIT += MatExp(phi, phi*coeffs[0]);

	return GaIT;
};

MatExp RoTrManip::DRoTr_I(const VecExp & phi) {
	ScalExp coeff[COEFF_D], coeffs[COEFF_C_STAR];
	
	CoeffCStar(phi,phi.GetVec(),coeff,coeffs);

	MatExp GaIT(1., phi*-.5);
	GaIT += MatExp(phi, phi*coeffs[0]);

	return GaIT;
};

void RoTrManip::RoTrAndDRoTr_It(const VecExp & phi, 
				MatExp & PhiIT,
				MatExp & GaIT) {
	ScalExp coeff[COEFF_D], coeffs[COEFF_C_STAR];

	CoeffCStar(phi,phi.GetVec(),coeff,coeffs);

	PhiIT = MatExp(1., phi*coeff[0]);
	PhiIT += MatExp(phi, phi*coeff[1]);

	GaIT = MatExp(1., phi*.5);
	GaIT += MatExp(phi, phi*coeffs[0]);

	return;
};

VecExp RoTrManip::Helix(const MatExp & H)  {
	Vec3 phi(RotManip::VecRot(H.GetVec()));
	return VecExp(phi,
			RotManip::DRot_IT(phi).Transpose()*(
				(H.GetMom()*(H.GetVec().Transpose())).Ax())
			);
};


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

/* Continuano i vincoli di rotazione piani */

#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#include <planej.h>

/* PlaneHingeJoint - begin */

const unsigned int PlaneHingeJoint::NumSelfDof(5);
const unsigned int PlaneHingeJoint::NumDof(17);

/* Costruttore non banale */
PlaneHingeJoint::PlaneHingeJoint(unsigned int uL, const DofOwner* pDO,
		const StructNode* pN1, const StructNode* pN2,
		const Vec3& dTmp1, const Vec3& dTmp2,
		const Mat3x3& R1hTmp, const Mat3x3& R2hTmp,
		flag fOut, 
		const doublereal rr,
		const doublereal pref,
		BasicShapeCoefficient *const sh,
		BasicFriction *const f)
: Elem(uL, Elem::JOINT, fOut), 
Joint(uL, Joint::PLANEHINGE, pDO, fOut), 
pNode1(pN1), pNode2(pN2),
d1(dTmp1), R1h(R1hTmp), d2(dTmp2), R2h(R2hTmp), F(0.), M(0.), dTheta(0.),
Sh_c(sh), fc(f), preF(pref), r(rr)
{
	NO_OP;
}


/* Distruttore banale */
PlaneHingeJoint::~PlaneHingeJoint(void)
{
   NO_OP;
};

void
PlaneHingeJoint::SetValue(VectorHandler& X, VectorHandler& XP) const
{
	Mat3x3 RTmp((pNode1->GetRCurr()*R1h).Transpose()*(pNode2->GetRCurr()*R2h));
	Vec3 v(MatR2EulerAngles(RTmp));

	dTheta = v.dGet(3);

	if (fc) {
		fc->SetValue(X,XP,iGetFirstIndex()+NumSelfDof);
	}
}

void
PlaneHingeJoint::AfterConvergence(const VectorHandler& X, 
		const VectorHandler& XP)
{

	Mat3x3 RTmp(((pNode1->GetRCurr()*R1h).Transpose()
			*pNode1->GetRPrev()*R1h).Transpose()
			*((pNode2->GetRCurr()*R2h).Transpose()
			*pNode2->GetRPrev()*R2h));
	Vec3 v(MatR2EulerAngles(RTmp.Transpose()));

	dTheta += v.dGet(3);

	if (fc) {
		Mat3x3 R1(pNode1->GetRCurr());
		Mat3x3 R1hTmp(R1*R1h);
		Vec3 e3a(R1hTmp.GetVec(3));
		Vec3 Omega1(pNode1->GetWCurr());
		Vec3 Omega2(pNode2->GetWCurr());
		//relative velocity
		doublereal v = (Omega1-Omega2).Dot(e3a)*r;
		//reaction norm
		doublereal modF = std::max(F.Norm(), preF);;
		fc->AfterConvergence(modF,v,X,XP,iGetFirstIndex()+NumSelfDof);
	}
}


/* Contributo al file di restart */
std::ostream& PlaneHingeJoint::Restart(std::ostream& out) const
{
   Joint::Restart(out) << ", plane hinge, "
     << pNode1->GetLabel() << ", reference, node, ",
     d1.Write(out, ", ")
     << ", hinge, reference, node, 1, ", (R1h.GetVec(1)).Write(out, ", ")
     << ", 2, ", (R1h.GetVec(2)).Write(out, ", ") << ", "
     << pNode2->GetLabel() << ", reference, node, ",
     d2.Write(out, ", ")
     << ", hinge, reference, node, 1, ", (R2h.GetVec(1)).Write(out, ", ")
     << ", 2, ", (R2h.GetVec(2)).Write(out, ", ") << ';' << std::endl;
   
   return out;
}


/* Assemblaggio jacobiano */
VariableSubMatrixHandler& 
PlaneHingeJoint::AssJac(VariableSubMatrixHandler& WorkMat,
			    doublereal dCoef,
			    const VectorHandler& XCurr,
			    const VectorHandler& XPrimeCurr)
{
   DEBUGCOUT("Entering PlaneHingeJoint::AssJac()" << std::endl);
   
   /* Setta la sottomatrice come piena (e' un po' dispersivo, ma lo jacobiano 
    * e' complicato */					
   FullSubMatrixHandler& WM = WorkMat.SetFull();
   
   /* Ridimensiona la sottomatrice in base alle esigenze */
   integer iNumRows = 0;
   integer iNumCols = 0;
   WorkSpaceDim(&iNumRows, &iNumCols);
   WM.ResizeReset(iNumRows, iNumCols);
   
   /* Recupera gli indici delle varie incognite */
   integer iNode1FirstPosIndex = pNode1->iGetFirstPositionIndex();
   integer iNode1FirstMomIndex = pNode1->iGetFirstMomentumIndex();
   integer iNode2FirstPosIndex = pNode2->iGetFirstPositionIndex();
   integer iNode2FirstMomIndex = pNode2->iGetFirstMomentumIndex();
   integer iFirstReactionIndex = iGetFirstIndex();

   /* Setta gli indici delle equazioni */
   for (unsigned int iCnt = 1; iCnt <= 6; iCnt++) {	
      WM.PutRowIndex(iCnt, iNode1FirstMomIndex+iCnt);
      WM.PutColIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WM.PutRowIndex(6+iCnt, iNode2FirstMomIndex+iCnt);
      WM.PutColIndex(6+iCnt, iNode2FirstPosIndex+iCnt);
   }
   
   for (unsigned int iCnt = 1; iCnt <= iGetNumDof(); iCnt++) {	
      WM.PutRowIndex(12+iCnt, iFirstReactionIndex+iCnt);
      WM.PutColIndex(12+iCnt, iFirstReactionIndex+iCnt);
   }
   
   /* Recupera i dati che servono */
   Mat3x3 R1(pNode1->GetRRef());
   Mat3x3 R2(pNode2->GetRRef());   
   Vec3 d1Tmp(R1*d1);
   Vec3 d2Tmp(R2*d2);
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);
   
   /* Suppongo che le reazioni F, M siano gia' state aggiornate da AssRes;
    * ricordo che la forza F e' nel sistema globale, mentre la coppia M
    * e' nel sistema locale ed il terzo termine, M(3), e' nullo in quanto
    * diretto come l'asse attorno al quale la rotazione e' consentita */
   
      
   /* 
    * La cerniera piana ha le prime 3 equazioni uguali alla cerniera sferica;
    * inoltre ha due equazioni che affermano la coincidenza dell'asse 3 del
    * riferimento solidale con la cerniera visto dai due nodi.
    * 
    *      (R1 * R1h * e1)^T * (R2 * R2h * e3) = 0
    *      (R1 * R1h * e2)^T * (R2 * R2h * e3) = 0
    * 
    * A queste equazioni corrisponde una reazione di coppia agente attorno 
    * agli assi 1 e 2 del riferimento della cerniera. La coppia attorno 
    * all'asse 3 e' nulla per definizione. Quindi la coppia nel sistema 
    * globale e':
    *      -R1 * R1h * M       per il nodo 1,
    *       R2 * R2h * M       per il nodo 2
    * 
    * 
    *       xa   ga                   xb   gb                     F     M 
    * Qa |  0    0                     0    0                     I     0  | | xa |   | -F           |
    * Ga |  0    c*(F/\da/\-(Sa*M)/\)  0    0                     da/\  Sa | | ga |   | -da/\F-Sa*M |
    * Qb |  0    0                     0    0                    -I     0  | | xb | = |  F           |
    * Gb |  0    0                     0   -c*(F/\db/\-(Sb*M)/\) -db/\ -Sb | | gb |   |  db/\F+Sb*M |
    * F  | -c*I  c*da/\                c*I -c*db/\                0     0  | | F  |   |  xa+da-xb-db |
    * M1 |  0    c*Tb1^T*Ta3/\         0    c*Ta3^T*Tb1/\         0     0  | | M  |   |  Sb^T*Ta3    |
    * M2 |  0    c*Tb2^T*Ta3/\         0    c*Ta3^T*Tb2/\         0     0  | 
    * 
    * con da = R1*d01, db = R2*d02, c = dCoef,
    * Sa = R1*R1h*[e1,e2], Sb = R2*R2h*[e1, e2],
    * Ta3 = R1*R1h*e3, Tb1 = R2*R2h*e1, Tb2 = R2*R2h*e2
    */

   /* Contributo della forza alle equazioni di equilibrio dei due nodi */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(iCnt, 12+iCnt, 1.);
      WM.PutCoef(6+iCnt, 12+iCnt, -1.);
   }
   
   WM.Add(4, 13, Mat3x3(d1Tmp));
   WM.Add(10, 13, Mat3x3(-d2Tmp));   
   
   /* Moltiplica la forza ed il momento per il coefficiente
    * del metodo */
   Vec3 FTmp = F*dCoef;
   Vec3 MTmp = M*dCoef;

   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));
   MTmp = e2b*MTmp.dGet(1)-e1b*MTmp.dGet(2);
   
   Mat3x3 MWedgee3aWedge(MTmp, e3a);
   Mat3x3 e3aWedgeMWedge(e3a, MTmp);
   
   WM.Add(4, 4, Mat3x3(FTmp, d1Tmp)-MWedgee3aWedge);
   WM.Add(4, 10, e3aWedgeMWedge);
   
   WM.Add(10, 4, MWedgee3aWedge);   
   WM.Add(10, 10, Mat3x3(-FTmp, d2Tmp)-e3aWedgeMWedge);      
   
   /* Contributo del momento alle equazioni di equilibrio dei nodi */
   Vec3 Tmp1(e2b.Cross(e3a));
   Vec3 Tmp2(e3a.Cross(e1b));
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(3+iCnt, 16, d);
      WM.PutCoef(9+iCnt, 16, -d);
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(3+iCnt, 17, d);
      WM.PutCoef(9+iCnt, 17, -d);
   }         
   
   /* Modifica: divido le equazioni di vincolo per dCoef */
   
   /* Equazioni di vincolo degli spostamenti */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(12+iCnt, iCnt, -1.);
      WM.PutCoef(12+iCnt, 6+iCnt, 1.);
   }
   
   WM.Add(13, 4, Mat3x3(d1Tmp));
   WM.Add(13, 10, Mat3x3(-d2Tmp));
   
   /* Equazione di vincolo del momento
    * 
    * Attenzione: bisogna scrivere il vettore trasposto
    *   (Sb[1]^T*(Sa[3]/\))*dCoef
    * Questo pero' e' uguale a:
    *   (-Sa[3]/\*Sb[1])^T*dCoef,
    * che puo' essere ulteriormente semplificato:
    *   (Sa[3].Cross(Sb[1])*(-dCoef))^T;
    */
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(16, 3+iCnt, d);
      WM.PutCoef(16, 9+iCnt, -d);
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(17, 3+iCnt, -d);
      WM.PutCoef(17, 9+iCnt, d);
   }   

   if (fc) {
      //retrive
          //friction coef
      doublereal f = fc->fc();
          //shape function
      doublereal shc = Sh_c->Sh_c();
          //omega and omega rif
      Vec3 Omega1(pNode1->GetWCurr());
      Vec3 Omega2(pNode2->GetWCurr());
      Vec3 Omega1r(pNode1->GetWRef());
      Vec3 Omega2r(pNode2->GetWRef());   
      //compute 
          //relative velocity
      doublereal v = (Omega1-Omega2).Dot(e3a)*r;
          //reaction norm
      doublereal modF = std::max(F.Norm(), preF);
          //reaction moment
      //doublereal M3 = shc*modF*f;
      
      ExpandableRowVector dfc;
      ExpandableRowVector dF;
      ExpandableRowVector dv;
          //variation of reaction force
      dF.ReDim(3);
      if ((modF == 0.) or (F.Norm() > preF)) {
          dF.Set(0.,1,12+1);
          dF.Set(0.,2,12+2);
          dF.Set(0.,3,12+3);
      } else {
          dF.Set(F.dGet(1)/modF,1,12+1);
          dF.Set(F.dGet(2)/modF,2,12+2);
          dF.Set(F.dGet(3)/modF,3,12+3);
      }
          //variation of relative velocity
      dv.ReDim(6);
      
/* old (wrong?) relative velocity linearization */

//       dv.Set((e3a.dGet(1)*1.-( e3a.dGet(2)*Omega1r.dGet(3)-e3a.dGet(3)*Omega1r.dGet(2))*dCoef)*r,1,0+4);
//       dv.Set((e3a.dGet(2)*1.-(-e3a.dGet(1)*Omega1r.dGet(3)+e3a.dGet(3)*Omega1r.dGet(1))*dCoef)*r,2,0+5);
//       dv.Set((e3a.dGet(3)*1.-( e3a.dGet(1)*Omega1r.dGet(2)-e3a.dGet(2)*Omega1r.dGet(1))*dCoef)*r,3,0+6);
//       
//       dv.Set(-(e3a.dGet(1)*1.-( e3a.dGet(2)*Omega2r.dGet(3)-e3a.dGet(3)*Omega2r.dGet(2))*dCoef)*r,4,6+4);
//       dv.Set(-(e3a.dGet(2)*1.-(-e3a.dGet(1)*Omega2r.dGet(3)+e3a.dGet(3)*Omega2r.dGet(1))*dCoef)*r,5,6+5);
//       dv.Set(-(e3a.dGet(3)*1.-( e3a.dGet(1)*Omega2r.dGet(2)-e3a.dGet(2)*Omega2r.dGet(1))*dCoef)*r,6,6+6);

/* new (exact?) relative velocity linearization */
// 
//       ExpandableRowVector domega11, domega12, domega13;
//       ExpandableRowVector domega21, domega22, domega23;
//       domega11.ReDim(3); domega12.ReDim(3); domega13.ReDim(3);
//       domega21.ReDim(3); domega22.ReDim(3); domega23.ReDim(3);
//       
//       domega11.Set(1., 1, 0+4);
//           domega11.Set( Omega1r.dGet(3)*dCoef, 2, 0+5);
//           domega11.Set(-Omega1r.dGet(2)*dCoef, 3, 0+6);
//       domega21.Set(1., 1, 6+4);
//           domega21.Set( Omega2r.dGet(3)*dCoef, 2, 6+5);
//           domega21.Set(-Omega2r.dGet(2)*dCoef, 3, 6+6);
//       domega12.Set(1., 1, 0+5);
//           domega12.Set(-Omega1r.dGet(3)*dCoef, 2, 0+4);
//           domega12.Set( Omega1r.dGet(1)*dCoef, 3, 0+6);
//       domega22.Set(1., 1, 6+5);
//           domega22.Set(-Omega2r.dGet(3)*dCoef, 2, 6+4);
//           domega22.Set( Omega2r.dGet(1)*dCoef, 3, 6+6);
//       domega13.Set(1., 1, 0+6);
//           domega13.Set( Omega1r.dGet(2)*dCoef, 2, 0+4);
//           domega13.Set(-Omega1r.dGet(1)*dCoef, 3, 0+5);
//       domega23.Set(1., 1, 6+6);
//           domega23.Set( Omega2r.dGet(2)*dCoef, 2, 6+4);
//           domega23.Set(-Omega2r.dGet(1)*dCoef, 3, 6+5);
// 
//       Vec3 domega = Omega1-Omega2;
//       dv.Set((e3a.dGet(1)*1.-( 
//       		e3a.dGet(2)*(Omega1.dGet(3)-Omega2.dGet(3))-
// 		e3a.dGet(3)*(domega.dGet(2)))*dCoef)*r,1);
// 		dv.Link(1, &domega11);
//       dv.Set((e3a.dGet(2)*1.-(
//       		-e3a.dGet(1)*(Omega1.dGet(3)-Omega2.dGet(3))+
// 		e3a.dGet(3)*(domega.dGet(1)))*dCoef)*r,2);
// 		dv.Link(2, &domega12);
//       dv.Set((e3a.dGet(3)*1.-( 
//       		e3a.dGet(1)*(Omega1.dGet(2)-Omega2.dGet(2))-
// 		e3a.dGet(2)*(domega.dGet(1)))*dCoef)*r,3);
// 		dv.Link(3, &domega13);
// 
//       dv.Set(-(e3a.dGet(1)*1.)*r,4,6+4); dv.Link(4, &domega21);
//       dv.Set(-(e3a.dGet(2)*1.)*r,5,6+5); dv.Link(5, &domega22);
//       dv.Set(-(e3a.dGet(3)*1.)*r,6,6+6); dv.Link(6, &domega23);


/* new (approximate: assume constant triads orientations) 
 * relative velocity linearization 
*/

      dv.Set((e3a.dGet(1)*1.)*r,1, 0+4);
      dv.Set((e3a.dGet(2)*1.)*r,2, 0+5);
      dv.Set((e3a.dGet(3)*1.)*r,3, 0+6);
      
      dv.Set(-(e3a.dGet(1)*1.)*r,4, 6+4);
      dv.Set(-(e3a.dGet(2)*1.)*r,5, 6+5);
      dv.Set(-(e3a.dGet(3)*1.)*r,6, 6+6);


      //assemble friction states
      fc->AssJac(WM,dfc,12+NumSelfDof,iFirstReactionIndex+NumSelfDof,dCoef,modF,v,
      		XCurr,XPrimeCurr,dF,dv);
      ExpandableRowVector dM3;
      ExpandableRowVector dShc;
      //compute 
          //variation of shape function
      Sh_c->dSh_c(dShc,f,modF,v,dfc,dF,dv);
          //variation of moment component
      dM3.ReDim(3);
      dM3.Set(shc*f,1); dM3.Link(1,&dF);
      dM3.Set(modF*f,2); dM3.Link(2,&dShc);
      dM3.Set(shc*modF,3); dM3.Link(3,&dfc);
      //assemble first node
          //variation of moment component
      dM3.Sub(WM,0+4,e3a.dGet(1));
      dM3.Sub(WM,0+5,e3a.dGet(2));
      dM3.Sub(WM,0+6,e3a.dGet(3));
      //assemble second node
          //variation of moment component
      dM3.Add(WM,6+4,e3a.dGet(1));
      dM3.Add(WM,6+5,e3a.dGet(2));
      dM3.Add(WM,6+6,e3a.dGet(3));
   }
   
   return WorkMat;
}


/* Assemblaggio residuo */
SubVectorHandler& PlaneHingeJoint::AssRes(SubVectorHandler& WorkVec,
					  doublereal dCoef,
					  const VectorHandler& XCurr, 
					  const VectorHandler& XPrimeCurr)
{
   DEBUGCOUT("Entering PlaneHingeJoint::AssRes()" << std::endl);
      
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   WorkSpaceDim(&iNumRows, &iNumCols);
   WorkVec.ResizeReset(iNumRows);
 
   /* Indici */
   integer iNode1FirstMomIndex = pNode1->iGetFirstMomentumIndex();
   integer iNode2FirstMomIndex = pNode2->iGetFirstMomentumIndex();
   integer iFirstReactionIndex = iGetFirstIndex();
   
   /* Indici dei nodi */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {	
      WorkVec.PutRowIndex(iCnt, iNode1FirstMomIndex+iCnt);
      WorkVec.PutRowIndex(6+iCnt, iNode2FirstMomIndex+iCnt);
   }   
   
   /* Indici del vincolo */
   for (unsigned int iCnt = 1; iCnt <= iGetNumDof(); iCnt++) {
      WorkVec.PutRowIndex(12+iCnt, iFirstReactionIndex+iCnt);
   }

   /* Aggiorna i dati propri */
   F = Vec3(XCurr, iFirstReactionIndex+1);
   M = Vec3(XCurr.dGetCoef(iFirstReactionIndex+4),
	    XCurr.dGetCoef(iFirstReactionIndex+5),
	    0.);

   /*
    * FIXME: provare a mettere "modificatori" di forza/momento sui gdl
    * residui: attrito, rigidezze e smorzamenti, ecc.
    */
   
   /* Recupera i dati */
   Vec3 x1(pNode1->GetXCurr());
   Vec3 x2(pNode2->GetXCurr());
   Mat3x3 R1(pNode1->GetRCurr());
   Mat3x3 R2(pNode2->GetRCurr());
   
   /* Costruisce i dati propri nella configurazione corrente */
   Vec3 dTmp1(R1*d1);
   Vec3 dTmp2(R2*d2);
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);
   
   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));
   
   Vec3 MTmp(e2b.Cross(e3a)*M.dGet(1)+e3a.Cross(e1b)*M.dGet(2));
   
   /* Equazioni di equilibrio, nodo 1 */
   WorkVec.Sub(1, F);
   WorkVec.Add(4, F.Cross(dTmp1)-MTmp); /* Sfrutto  F/\d = -d/\F */
   
   /* Equazioni di equilibrio, nodo 2 */
   WorkVec.Add(7, F);
   WorkVec.Add(10, dTmp2.Cross(F)+MTmp);

   /* Modifica: divido le equazioni di vincolo per dCoef */
   if (dCoef != 0.) {
	
      /* Equazione di vincolo di posizione */
      WorkVec.Add(13, (x1+dTmp1-x2-dTmp2)/dCoef);
      
      /* Equazioni di vincolo di rotazione */
      Vec3 Tmp = R1hTmp.GetVec(3);
      WorkVec.PutCoef(16, Tmp.Dot(R2hTmp.GetVec(2))/dCoef);
      WorkVec.PutCoef(17, Tmp.Dot(R2hTmp.GetVec(1))/dCoef);

   }   
   if (fc) {
      Vec3 Omega1(pNode1->GetWCurr());
      Vec3 Omega2(pNode2->GetWCurr());
      doublereal v = (Omega1-Omega2).Dot(e3a)*r;
      doublereal modF = std::max(F.Norm(), preF);
      fc->AssRes(WorkVec,12+NumSelfDof,iFirstReactionIndex+NumSelfDof,modF,v,XCurr,XPrimeCurr);
      doublereal f = fc->fc();
      doublereal shc = Sh_c->Sh_c(f,modF,v);
      M3 = shc*modF*f;
      WorkVec.Sub(4,e3a*M3);
      WorkVec.Add(10,e3a*M3);
//!!!!!!!!!!!!!!
//      M += e3a*M3;
   }
   
   return WorkVec;
}

unsigned int PlaneHingeJoint::iGetNumDof(void) const {
   unsigned int i = NumSelfDof;
   if (fc) {
       i+=fc->iGetNumDof();
   } 
   return i;
};


DofOrder::Order
PlaneHingeJoint::GetDofType(unsigned int i) const {
   ASSERT(i >= 0 && i < iGetNumDof());
   if (i<NumSelfDof) {
       return DofOrder::ALGEBRAIC; 
   } else {
       return fc->GetDofType(i-NumSelfDof);
   }
};

DofOrder::Order
PlaneHingeJoint::GetEqType(unsigned int i) const
{
	ASSERTMSGBREAK(i < iGetNumDof(), 
		"INDEX ERROR in PlaneHingeJoint::GetEqType");
   if (i<NumSelfDof) {
       return DofOrder::ALGEBRAIC; 
   } else {
       return fc->GetEqType(i-NumSelfDof);
   }
}

/* Output (da mettere a punto) */
void PlaneHingeJoint::Output(OutputHandler& OH) const
{
   if (fToBeOutput()) {
      Mat3x3 R2Tmp(pNode2->GetRCurr()*R2h);
      Mat3x3 RTmp((pNode1->GetRCurr()*R1h).Transpose()*R2Tmp);
      Mat3x3 R2TmpT(R2Tmp.Transpose());
      
      std::ostream &of = Joint::Output(OH.Joints(), "PlaneHinge", GetLabel(),
		    R2TmpT*F, M, F, R2Tmp*M)
	<< " " << MatR2EulerAngles(RTmp)*dRaDegr
	  << " " << R2TmpT*(pNode2->GetWCurr()-pNode1->GetWCurr());
      if (fc) {
          of << " " << M3 << " " << fc->fc();
      }
      of << std::endl;
   }   
}


/* Contributo allo jacobiano durante l'assemblaggio iniziale */
VariableSubMatrixHandler& 
PlaneHingeJoint::InitialAssJac(VariableSubMatrixHandler& WorkMat,
			       const VectorHandler& XCurr)
{
   /* Per ora usa la matrice piena; eventualmente si puo' 
    * passare a quella sparsa quando si ottimizza */
   FullSubMatrixHandler& WM = WorkMat.SetFull();
   
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   InitialWorkSpaceDim(&iNumRows, &iNumCols);
   WM.ResizeReset(iNumRows, iNumCols);

   /* Equazioni: vedi joints.dvi */
   
   /*       equazioni ed incognite
    * F1                                     Delta_x1         0+1 =  1
    * M1                                     Delta_g1         3+1 =  4
    * FP1                                    Delta_xP1        6+1 =  7
    * MP1                                    Delta_w1         9+1 = 10
    * F2                                     Delta_x2        12+1 = 13
    * M2                                     Delta_g2        15+1 = 16
    * FP2                                    Delta_xP2       18+1 = 19
    * MP2                                    Delta_w2        21+1 = 22
    * vincolo spostamento                    Delta_F         24+1 = 25
    * vincolo rotazione                      Delta_M         27+1 = 28
    * derivata vincolo spostamento           Delta_FP        29+1 = 30
    * derivata vincolo rotazione             Delta_MP        32+1 = 33
    */
        
    
   /* Indici */
   integer iNode1FirstPosIndex = pNode1->iGetFirstPositionIndex();
   integer iNode1FirstVelIndex = iNode1FirstPosIndex+6;
   integer iNode2FirstPosIndex = pNode2->iGetFirstPositionIndex();
   integer iNode2FirstVelIndex = iNode2FirstPosIndex+6;
   integer iFirstReactionIndex = iGetFirstIndex();
   integer iReactionPrimeIndex = iFirstReactionIndex+5;
   
   /* Nota: le reazioni vincolari sono: 
    * Forza,       3 incognite, riferimento globale, 
    * Momento,     2 incognite, riferimento locale
    */

   /* Setta gli indici dei nodi */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {
      WM.PutRowIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WM.PutColIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WM.PutRowIndex(6+iCnt, iNode1FirstVelIndex+iCnt);
      WM.PutColIndex(6+iCnt, iNode1FirstVelIndex+iCnt);
      WM.PutRowIndex(12+iCnt, iNode2FirstPosIndex+iCnt);
      WM.PutColIndex(12+iCnt, iNode2FirstPosIndex+iCnt);
      WM.PutRowIndex(18+iCnt, iNode2FirstVelIndex+iCnt);
      WM.PutColIndex(18+iCnt, iNode2FirstVelIndex+iCnt);
   }
   
   /* Setta gli indici delle reazioni */
   for (int iCnt = 1; iCnt <= 10; iCnt++) {
      WM.PutRowIndex(24+iCnt, iFirstReactionIndex+iCnt);
      WM.PutColIndex(24+iCnt, iFirstReactionIndex+iCnt);	
   }   

   
   /* Recupera i dati */
   Mat3x3 R1(pNode1->GetRRef());
   Mat3x3 R2(pNode2->GetRRef());
   Vec3 Omega1(pNode1->GetWRef());
   Vec3 Omega2(pNode2->GetWRef());
   
   /* F ed M sono gia' state aggiornate da InitialAssRes */
   Vec3 FPrime(XCurr, iReactionPrimeIndex+1);
   Vec3 MPrime(XCurr.dGetCoef(iReactionPrimeIndex+4),
	       XCurr.dGetCoef(iReactionPrimeIndex+5),
	       0.);
   
   /* Matrici identita' */   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      /* Contributo di forza all'equazione della forza, nodo 1 */
      WM.PutCoef(iCnt, 24+iCnt, 1.);
      
      /* Contrib. di der. di forza all'eq. della der. della forza, nodo 1 */
      WM.PutCoef(6+iCnt, 29+iCnt, 1.);
      
      /* Contributo di forza all'equazione della forza, nodo 2 */
      WM.PutCoef(12+iCnt, 24+iCnt, -1.);
      
      /* Contrib. di der. di forza all'eq. della der. della forza, nodo 2 */
      WM.PutCoef(18+iCnt, 29+iCnt, -1.);
      
      /* Equazione di vincolo, nodo 1 */
      WM.PutCoef(24+iCnt, iCnt, -1.);
      
      /* Derivata dell'equazione di vincolo, nodo 1 */
      WM.PutCoef(29+iCnt, 6+iCnt, -1.);
      
      /* Equazione di vincolo, nodo 2 */
      WM.PutCoef(24+iCnt, 12+iCnt, 1.);
      
      /* Derivata dell'equazione di vincolo, nodo 2 */
      WM.PutCoef(29+iCnt, 18+iCnt, 1.);
   }
      
   /* Distanze e matrici di rotazione dai nodi alla cerniera 
    * nel sistema globale */
   Vec3 d1Tmp(R1*d1);
   Vec3 d2Tmp(R2*d2);
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);
   
   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));
   
   /* Ruota il momento e la sua derivata con le matrici della cerniera 
    * rispetto ai nodi */
   Vec3 MTmp(e2b*M.dGet(1)-e1b*M.dGet(2));
   Vec3 MPrimeTmp(e2b*MPrime.dGet(1)-e1b*MPrime.dGet(2));

   /* Matrici F/\d1/\, -F/\d2/\ */
   Mat3x3 FWedged1Wedge(F, d1Tmp);
   Mat3x3 FWedged2Wedge(F, -d2Tmp);
   
   /* Matrici (omega1/\d1)/\, -(omega2/\d2)/\ */
   Mat3x3 O1Wedged1Wedge(Omega1.Cross(d1Tmp));
   Mat3x3 O2Wedged2Wedge(d2Tmp.Cross(Omega2));
   
   Mat3x3 MDeltag1((Mat3x3(Omega2.Cross(MTmp)+MPrimeTmp)+
		    Mat3x3(MTmp, Omega1))*Mat3x3(e3a));
   Mat3x3 MDeltag2(Mat3x3(Omega1.Cross(e3a), MTmp)+
		   Mat3x3(e3a, MPrimeTmp)+
		   Mat3x3(e3a)*Mat3x3(Omega2, MTmp));

   /* Vettori temporanei */
   Vec3 Tmp1(e2b.Cross(e3a));   
   Vec3 Tmp2(e3a.Cross(e1b));
   
   /* Prodotto vettore tra il versore 3 della cerniera secondo il nodo 1
    * ed il versore 1 della cerniera secondo il nodo 2. A convergenza
    * devono essere ortogonali, quindi il loro prodotto vettore deve essere 
    * unitario */

   /* Error handling: il programma si ferma, pero' segnala dov'e' l'errore */
   if (Tmp1.Dot() <= DBL_EPSILON || Tmp2.Dot() <= DBL_EPSILON) {
      std::cerr << "Joint(" << GetLabel() << "): first node hinge axis "
	      "and second node hinge axis are (nearly) orthogonal"
	      << std::endl;
      THROW(Joint::ErrGeneric());
   }   
   
   Vec3 TmpPrime1(e2b.Cross(Omega1.Cross(e3a))-e3a.Cross(Omega2.Cross(e2b)));
   Vec3 TmpPrime2(e3a.Cross(Omega2.Cross(e1b))-e1b.Cross(Omega1.Cross(e3a)));
   
   /* Equazione di momento, nodo 1 */
   WM.Add(4, 4, FWedged1Wedge-Mat3x3(MTmp, e3a));
   WM.Add(4, 16, Mat3x3(e3a, MTmp));
   WM.Add(4, 25, Mat3x3(d1Tmp));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(3+iCnt, 28, Tmp1.dGet(iCnt));
      WM.PutCoef(3+iCnt, 29, Tmp2.dGet(iCnt));	
   }
   
   /* Equazione di momento, nodo 2 */
   WM.Add(16, 4, Mat3x3(MTmp, e3a));
   WM.Add(16, 16, FWedged2Wedge-Mat3x3(e3a, MTmp));
   WM.Add(16, 25, Mat3x3(-d2Tmp));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(15+iCnt, 28, -Tmp1.dGet(iCnt));
      WM.PutCoef(15+iCnt, 29, -Tmp2.dGet(iCnt));	
   }
   
   /* Derivata dell'equazione di momento, nodo 1 */
   WM.Add(10, 4, (Mat3x3(FPrime)+Mat3x3(F, Omega1))*Mat3x3(d1Tmp)-MDeltag1);
   WM.Add(10, 10, FWedged1Wedge-Mat3x3(MTmp, e3a));
   WM.Add(10, 16, MDeltag2);
   WM.Add(10, 22, Mat3x3(e3a, MTmp));
   WM.Add(10, 25, O1Wedged1Wedge);
   WM.Add(10, 30, Mat3x3(d1Tmp));
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(9+iCnt, 28, TmpPrime1.dGet(iCnt));
      WM.PutCoef(9+iCnt, 29, TmpPrime2.dGet(iCnt));
      WM.PutCoef(9+iCnt, 33, Tmp1.dGet(iCnt));
      WM.PutCoef(9+iCnt, 34, Tmp2.dGet(iCnt));	
   }
   
   /* Derivata dell'equazione di momento, nodo 2 */
   WM.Add(22, 4, MDeltag1);
   WM.Add(22, 10, Mat3x3(MTmp, e3a));
   WM.Add(22, 16, (Mat3x3(FPrime)+Mat3x3(F, Omega2))*Mat3x3(-d2Tmp)-MDeltag2);
   WM.Add(22, 22, FWedged2Wedge-Mat3x3(e3a, MTmp));
   WM.Add(22, 25, O2Wedged2Wedge);
   WM.Add(22, 30, Mat3x3(-d2Tmp));

   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(21+iCnt, 28, -TmpPrime1.dGet(iCnt));
      WM.PutCoef(21+iCnt, 29, -TmpPrime2.dGet(iCnt));	
      WM.PutCoef(21+iCnt, 33, -Tmp1.dGet(iCnt));
      WM.PutCoef(21+iCnt, 34, -Tmp2.dGet(iCnt));	
   }
   
   /* Equazione di vincolo di posizione */
   WM.Add(25, 4, Mat3x3(d1Tmp));
   WM.Add(25, 16, Mat3x3(-d2Tmp));
   
   /* Derivata dell'equazione di vincolo di posizione */
   WM.Add(30, 4, O1Wedged1Wedge);
   WM.Add(30, 10, Mat3x3(d1Tmp));
   WM.Add(30, 16, O2Wedged2Wedge);
   WM.Add(30, 22, Mat3x3(-d2Tmp));
   
   /* Equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
            
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(28, 3+iCnt, d);
      WM.PutCoef(28, 15+iCnt, -d);
      
      /* Queste sono per la derivata dell'equazione, sono qui solo per 
       * ottimizzazione */
      WM.PutCoef(33, 9+iCnt, d);
      WM.PutCoef(33, 21+iCnt, -d);
      
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(29, 3+iCnt, -d);
      WM.PutCoef(29, 15+iCnt, d);
      
      /* Queste sono per la derivata dell'equazione, sono qui solo per 
       * ottimizzazione */
      WM.PutCoef(34, 9+iCnt, -d);
      WM.PutCoef(34, 21+iCnt, d);
   }   
   
   /* Derivate delle equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
   Vec3 O1mO2(Omega1-Omega2);
   TmpPrime1 = e3a.Cross(O1mO2.Cross(e2b));   
   TmpPrime2 = e2b.Cross(e3a.Cross(O1mO2));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(33, 3+iCnt, TmpPrime1.dGet(iCnt));
      WM.PutCoef(33, 15+iCnt, TmpPrime2.dGet(iCnt));
   }
   
   TmpPrime1 = e3a.Cross(O1mO2.Cross(e1b));
   TmpPrime2 = e1b.Cross(e3a.Cross(O1mO2));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(34, 3+iCnt, TmpPrime1.dGet(iCnt));
      WM.PutCoef(34, 15+iCnt, TmpPrime2.dGet(iCnt));
   }   
   
   return WorkMat;
}


/* Contributo al residuo durante l'assemblaggio iniziale */   
SubVectorHandler& 
PlaneHingeJoint::InitialAssRes(SubVectorHandler& WorkVec,
			       const VectorHandler& XCurr)
{   
   DEBUGCOUT("Entering PlaneHingeJoint::InitialAssRes()" << std::endl);
   
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   InitialWorkSpaceDim(&iNumRows, &iNumCols);
   WorkVec.ResizeReset(iNumRows);

   /* Indici */
   integer iNode1FirstPosIndex = pNode1->iGetFirstPositionIndex();
   integer iNode1FirstVelIndex = iNode1FirstPosIndex+6;
   integer iNode2FirstPosIndex = pNode2->iGetFirstPositionIndex();
   integer iNode2FirstVelIndex = iNode2FirstPosIndex+6;
   integer iFirstReactionIndex = iGetFirstIndex();
   integer iReactionPrimeIndex = iFirstReactionIndex+5;
   
   /* Setta gli indici */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {	
      WorkVec.PutRowIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WorkVec.PutRowIndex(6+iCnt, iNode1FirstVelIndex+iCnt);
      WorkVec.PutRowIndex(12+iCnt, iNode2FirstPosIndex+iCnt);
      WorkVec.PutRowIndex(18+iCnt, iNode2FirstVelIndex+iCnt);
   }
   
   for (int iCnt = 1; iCnt <= 10; iCnt++) {
      WorkVec.PutRowIndex(24+iCnt, iFirstReactionIndex+iCnt);
   }

   /* Recupera i dati */
   Vec3 x1(pNode1->GetXCurr());
   Vec3 x2(pNode2->GetXCurr());
   Vec3 v1(pNode1->GetVCurr());
   Vec3 v2(pNode2->GetVCurr());
   Mat3x3 R1(pNode1->GetRCurr());
   Mat3x3 R2(pNode2->GetRCurr());
   Vec3 Omega1(pNode1->GetWCurr());
   Vec3 Omega2(pNode2->GetWCurr());
   
   /* Aggiorna F ed M, che restano anche per InitialAssJac */
   F = Vec3(XCurr, iFirstReactionIndex+1);
   M = Vec3(XCurr.dGetCoef(iFirstReactionIndex+4),
	    XCurr.dGetCoef(iFirstReactionIndex+5),
	    0.);
   Vec3 FPrime(XCurr, iReactionPrimeIndex+1);
   Vec3 MPrime(XCurr.dGetCoef(iReactionPrimeIndex+4),
	       XCurr.dGetCoef(iReactionPrimeIndex+5),
	       0.);
   
   /* Distanza nel sistema globale */
   Vec3 d1Tmp(R1*d1);
   Vec3 d2Tmp(R2*d2);
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);

   /* Vettori omega1/\d1, -omega2/\d2 */
   Vec3 O1Wedged1(Omega1.Cross(d1Tmp));
   Vec3 O2Wedged2(Omega2.Cross(d2Tmp));
   
   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));  
   
   /* Ruota il momento e la sua derivata con le matrici della cerniera 
    * rispetto ai nodi */
   Vec3 MTmp(e2b*M.dGet(1)-e1b*M.dGet(2));       
   Vec3 MPrimeTmp(e3a.Cross(MTmp.Cross(Omega2))+MTmp.Cross(Omega1.Cross(e3a))+
		  e2b.Cross(e3a)*MPrime.dGet(1)+e3a.Cross(e1b)*MPrime.dGet(2)); 
   
   /* Equazioni di equilibrio, nodo 1 */
   WorkVec.Add(1, -F);
   WorkVec.Add(4, F.Cross(d1Tmp)-MTmp.Cross(e3a)); /* Sfrutto il fatto che F/\d = -d/\F */
   
   /* Derivate delle equazioni di equilibrio, nodo 1 */
   WorkVec.Add(7, -FPrime);
   WorkVec.Add(10, FPrime.Cross(d1Tmp)-O1Wedged1.Cross(F)-MPrimeTmp);
   
   /* Equazioni di equilibrio, nodo 2 */
   WorkVec.Add(13, F);
   WorkVec.Add(16, d2Tmp.Cross(F)+MTmp.Cross(e3a)); 
   
   /* Derivate delle equazioni di equilibrio, nodo 2 */
   WorkVec.Add(19, FPrime);
   WorkVec.Add(22, d2Tmp.Cross(FPrime)+O2Wedged2.Cross(F)+MPrimeTmp);
   
   /* Equazione di vincolo di posizione */
   WorkVec.Add(25, x1+d1Tmp-x2-d2Tmp);
   
   /* Derivata dell'equazione di vincolo di posizione */
   WorkVec.Add(30, v1+O1Wedged1-v2-O2Wedged2);

   /* Equazioni di vincolo di rotazione */
   WorkVec.PutCoef(28, e2b.Dot(e3a));
   WorkVec.PutCoef(29, e1b.Dot(e3a));
   
   /* Derivate delle equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
   Vec3 Tmp((Omega1-Omega2).Cross(e3a));
   WorkVec.PutCoef(33, e2b.Dot(Tmp));
   WorkVec.PutCoef(34, e1b.Dot(Tmp));

   return WorkVec;
}


unsigned int
PlaneHingeJoint::iGetNumPrivData(void) const
{
	return 2;
}

unsigned int
PlaneHingeJoint::iGetPrivDataIdx(const char *s) const
{
	ASSERT(s != NULL);

	if (strcmp(s, "rz") == 0) {
		return 1;
	}

	if (strcmp(s, "wz") == 0) {
		return 2;
	}


	return 0;
}

doublereal PlaneHingeJoint::dGetPrivData(unsigned int i) const
{
   ASSERT(i >= 1 && i <= iGetNumPrivData());
   
   switch (i) {
    case 1: {
	return dTheta;
    }
      
    case 2: {
       Mat3x3 R2TmpT((pNode2->GetRCurr()*R2h).Transpose());
       Vec3 v(R2TmpT*(pNode2->GetWCurr()-pNode1->GetWCurr()));
       
       return v.dGet(3);
    }
      
    default:
      std::cerr << "Illegal private data" << std::endl;
      THROW(ErrGeneric());
   }
}

/* PlaneHingeJoint - end */


/* PlaneRotationJoint - begin */

/* Costruttore non banale */
PlaneRotationJoint::PlaneRotationJoint(unsigned int uL, const DofOwner* pDO,
				 const StructNode* pN1, const StructNode* pN2,
				 const Mat3x3& R1hTmp, const Mat3x3& R2hTmp,
				 flag fOut)
: Elem(uL, Elem::JOINT, fOut), 
Joint(uL, Joint::PLANEHINGE, pDO, fOut), 
pNode1(pN1), pNode2(pN2),
R1h(R1hTmp), R2h(R2hTmp), M(0.), dTheta(0.)
{
   NO_OP;
}


/* Distruttore banale */
PlaneRotationJoint::~PlaneRotationJoint(void)
{
   NO_OP;
};


void
PlaneRotationJoint::SetValue(VectorHandler& X, VectorHandler& XP) const
{
	Mat3x3 RTmp((pNode1->GetRCurr()*R1h).Transpose()*(pNode2->GetRCurr()*R2h));
	Vec3 v(MatR2EulerAngles(RTmp));

	dTheta = v.dGet(3);
}

void
PlaneRotationJoint::AfterConvergence(const VectorHandler& X, 
		const VectorHandler& XP)
{
	Mat3x3 RTmp(pNode1->GetRCurr().Transpose()*pNode1->GetRPrev()
			*pNode2->GetRPrev().Transpose()*pNode2->GetRCurr()*R2h);
	Vec3 v(MatR2EulerAngles(RTmp));

	dTheta += v.dGet(3);
}


/* Contributo al file di restart */
std::ostream& PlaneRotationJoint::Restart(std::ostream& out) const
{
   Joint::Restart(out) << ", plane hinge, "
     << pNode1->GetLabel() 
     << ", hinge, reference, node, 1, ", (R1h.GetVec(1)).Write(out, ", ")
     << ", 2, ", (R1h.GetVec(2)).Write(out, ", ") << ", "
     << pNode2->GetLabel() 
     << ", hinge, reference, node, 1, ", (R2h.GetVec(1)).Write(out, ", ")
     << ", 2, ", (R2h.GetVec(2)).Write(out, ", ") << ';' << std::endl;
   
   return out;
}


/* Assemblaggio jacobiano */
VariableSubMatrixHandler& 
PlaneRotationJoint::AssJac(VariableSubMatrixHandler& WorkMat,
			    doublereal dCoef,
			    const VectorHandler& /* XCurr */ ,
			    const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering PlaneRotationJoint::AssJac()" << std::endl);
   
   /* Setta la sottomatrice come piena (e' un po' dispersivo, ma lo jacobiano 
    * e' complicato */					
   FullSubMatrixHandler& WM = WorkMat.SetFull();
   
   /* Ridimensiona la sottomatrice in base alle esigenze */
   integer iNumRows = 0;
   integer iNumCols = 0;
   WorkSpaceDim(&iNumRows, &iNumCols);
   WM.ResizeReset(iNumRows, iNumCols);
   
   /* Recupera gli indici delle varie incognite */
   integer iNode1FirstPosIndex = pNode1->iGetFirstPositionIndex()+3;
   integer iNode1FirstMomIndex = pNode1->iGetFirstMomentumIndex()+3;
   integer iNode2FirstPosIndex = pNode2->iGetFirstPositionIndex()+3;
   integer iNode2FirstMomIndex = pNode2->iGetFirstMomentumIndex()+3;
   integer iFirstReactionIndex = iGetFirstIndex();

   /* Setta gli indici delle equazioni */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutRowIndex(iCnt, iNode1FirstMomIndex+iCnt);
      WM.PutColIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WM.PutRowIndex(3+iCnt, iNode2FirstMomIndex+iCnt);
      WM.PutColIndex(3+iCnt, iNode2FirstPosIndex+iCnt);
   }
   
   for (int iCnt = 1; iCnt <= 2; iCnt++) {	
      WM.PutRowIndex(6+iCnt, iFirstReactionIndex+iCnt);
      WM.PutColIndex(6+iCnt, iFirstReactionIndex+iCnt);
   }
   
   /* Recupera i dati che servono */
   const Mat3x3& R1(pNode1->GetRRef());
   const Mat3x3& R2(pNode2->GetRRef());   
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);
   
   /* Suppongo che le reazioni F, M siano gia' state aggiornate da AssRes;
    * ricordo che la forza F e' nel sistema globale, mentre la coppia M
    * e' nel sistema locale ed il terzo termine, M(3), e' nullo in quanto
    * diretto come l'asse attorno al quale la rotazione e' consentita */
   
      
   /* 
    * La cerniera piana ha le prime 3 equazioni uguali alla cerniera sferica;
    * inoltre ha due equazioni che affermano la coincidenza dell'asse 3 del
    * riferimento solidale con la cerniera visto dai due nodi.
    * 
    *      (R1 * R1h * e1)^T * (R2 * R2h * e3) = 0
    *      (R1 * R1h * e2)^T * (R2 * R2h * e3) = 0
    * 
    * A queste equazioni corrisponde una reazione di coppia agente attorno 
    * agli assi 1 e 2 del riferimento della cerniera. La coppia attorno 
    * all'asse 3 e' nulla per definizione. Quindi la coppia nel sistema 
    * globale e':
    *      -R1 * R1h * M       per il nodo 1,
    *       R2 * R2h * M       per il nodo 2
    * 
    * 
    *       xa   ga                   xb   gb                     F     M 
    * Qa |  0    0                     0    0                     I     0  | | xa |   | -F           |
    * Ga |  0    c*(F/\da/\-(Sa*M)/\)  0    0                     da/\  Sa | | ga |   | -da/\F-Sa*M |
    * Qb |  0    0                     0    0                    -I     0  | | xb | = |  F           |
    * Gb |  0    0                     0   -c*(F/\db/\-(Sb*M)/\) -db/\ -Sb | | gb |   |  db/\F+Sb*M |
    * F  | -c*I  c*da/\                c*I -c*db/\                0     0  | | F  |   |  xa+da-xb-db |
    * M1 |  0    c*Tb1^T*Ta3/\         0    c*Ta3^T*Tb1/\         0     0  | | M  |   |  Sb^T*Ta3    |
    * M2 |  0    c*Tb2^T*Ta3/\         0    c*Ta3^T*Tb2/\         0     0  | 
    * 
    * con da = R1*d01, db = R2*d02, c = dCoef,
    * Sa = R1*R1h*[e1,e2], Sb = R2*R2h*[e1, e2],
    * Ta3 = R1*R1h*e3, Tb1 = R2*R2h*e1, Tb2 = R2*R2h*e2
    */

   /* Moltiplica il momento per il coefficiente del metodo */
   Vec3 MTmp = M*dCoef;

   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));
   MTmp = e2b*MTmp.dGet(1)-e1b*MTmp.dGet(2);
   
   Mat3x3 MWedgee3aWedge(MTmp, e3a);
   Mat3x3 e3aWedgeMWedge(e3a, MTmp);
   
   WM.Sub(1, 1, MWedgee3aWedge);
   WM.Add(1, 4, e3aWedgeMWedge);
   
   WM.Add(4, 1, MWedgee3aWedge);   
   WM.Sub(4, 4, e3aWedgeMWedge);      
   
   /* Contributo del momento alle equazioni di equilibrio dei nodi */
   Vec3 Tmp1(e2b.Cross(e3a));
   Vec3 Tmp2(e3a.Cross(e1b));
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(iCnt, 7, d);
      WM.PutCoef(3+iCnt, 7, -d);
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(iCnt, 8, d);
      WM.PutCoef(3+iCnt, 8, -d);
   }         
   
   /* Modifica: divido le equazioni di vincolo per dCoef */
   
   /* Equazione di vincolo del momento
    * 
    * Attenzione: bisogna scrivere il vettore trasposto
    *   (Sb[1]^T*(Sa[3]/\))*dCoef
    * Questo pero' e' uguale a:
    *   (-Sa[3]/\*Sb[1])^T*dCoef,
    * che puo' essere ulteriormente semplificato:
    *   (Sa[3].Cross(Sb[1])*(-dCoef))^T;
    */
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(7, iCnt, d);
      WM.PutCoef(7, 3+iCnt, -d);
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(8, iCnt, -d);
      WM.PutCoef(8, 3+iCnt, d);
   }   
   
   return WorkMat;
}


/* Assemblaggio residuo */
SubVectorHandler& PlaneRotationJoint::AssRes(SubVectorHandler& WorkVec,
					  doublereal dCoef,
					  const VectorHandler& XCurr, 
					  const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering PlaneRotationJoint::AssRes()" << std::endl);
      
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   WorkSpaceDim(&iNumRows, &iNumCols);
   WorkVec.ResizeReset(iNumRows);
 
   /* Indici */
   integer iNode1FirstMomIndex = pNode1->iGetFirstMomentumIndex()+3;
   integer iNode2FirstMomIndex = pNode2->iGetFirstMomentumIndex()+3;
   integer iFirstReactionIndex = iGetFirstIndex();
   
   /* Indici dei nodi */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WorkVec.PutRowIndex(iCnt, iNode1FirstMomIndex+iCnt);
      WorkVec.PutRowIndex(3+iCnt, iNode2FirstMomIndex+iCnt);
   }   
   
   /* Indici del vincolo */
   for (int iCnt = 1; iCnt <= 2; iCnt++) {
      WorkVec.PutRowIndex(6+iCnt, iFirstReactionIndex+iCnt);
   }

   /* Aggiorna i dati propri */
   M = Vec3(XCurr.dGetCoef(iFirstReactionIndex+1),
	    XCurr.dGetCoef(iFirstReactionIndex+2),
	    0.);

   /*
    * FIXME: provare a mettere "modificatori" di forza/momento sui gdl
    * residui: attrito, rigidezze e smorzamenti, ecc.
    */
   
   /* Recupera i dati */
   const Mat3x3& R1(pNode1->GetRCurr());
   const Mat3x3& R2(pNode2->GetRCurr());
   
   /* Costruisce i dati propri nella configurazione corrente */
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);
   
   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));
   
   Vec3 MTmp(e2b.Cross(e3a)*M.dGet(1)+e3a.Cross(e1b)*M.dGet(2));
   
   /* Equazioni di equilibrio, nodo 1 */
   WorkVec.Sub(1, MTmp); /* Sfrutto  F/\d = -d/\F */
   
   /* Equazioni di equilibrio, nodo 2 */
   WorkVec.Add(4, MTmp);

   /* Modifica: divido le equazioni di vincolo per dCoef */
   if (dCoef != 0.) {
      
      /* Equazioni di vincolo di rotazione */
      Vec3 Tmp = R1hTmp.GetVec(3);
      WorkVec.PutCoef(7, Tmp.Dot(R2hTmp.GetVec(2))/dCoef);
      WorkVec.PutCoef(8, Tmp.Dot(R2hTmp.GetVec(1))/dCoef);
   }   
   
   return WorkVec;
}

DofOrder::Order
PlaneRotationJoint::GetEqType(unsigned int i) const
{
	ASSERTMSGBREAK(i < iGetNumDof(), 
		"INDEX ERROR in PlaneRotationJoint::GetEqType");
	return DofOrder::ALGEBRAIC;
}


/* Output (da mettere a punto) */
void PlaneRotationJoint::Output(OutputHandler& OH) const
{
   if (fToBeOutput()) {
      Mat3x3 R2Tmp(pNode2->GetRCurr()*R2h);
      Mat3x3 RTmp((pNode1->GetRCurr()*R1h).Transpose()*R2Tmp);
      Mat3x3 R2TmpT(R2Tmp.Transpose());
      
      Joint::Output(OH.Joints(), "PlaneHinge", GetLabel(),
		    Zero3, M, Zero3, R2Tmp*M)
	<< " " << Vec3(0., 0., dTheta)
	<< " " << R2TmpT*(pNode2->GetWCurr()-pNode1->GetWCurr()) << std::endl;
   }
}


/* Contributo allo jacobiano durante l'assemblaggio iniziale */
VariableSubMatrixHandler& 
PlaneRotationJoint::InitialAssJac(VariableSubMatrixHandler& WorkMat,
			       const VectorHandler& XCurr)
{
   /* Per ora usa la matrice piena; eventualmente si puo' 
    * passare a quella sparsa quando si ottimizza */
   FullSubMatrixHandler& WM = WorkMat.SetFull();
   
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   InitialWorkSpaceDim(&iNumRows, &iNumCols);
   WM.ResizeReset(iNumRows, iNumCols);

   /* Equazioni: vedi joints.dvi */
   
   /*       equazioni ed incognite
    * F1                                     Delta_x1         0+1 =  1
    * M1                                     Delta_g1         3+1 =  4
    * FP1                                    Delta_xP1        6+1 =  7
    * MP1                                    Delta_w1         9+1 = 10
    * F2                                     Delta_x2        12+1 = 13
    * M2                                     Delta_g2        15+1 = 16
    * FP2                                    Delta_xP2       18+1 = 19
    * MP2                                    Delta_w2        21+1 = 22
    * vincolo spostamento                    Delta_F         24+1 = 25
    * vincolo rotazione                      Delta_M         27+1 = 28
    * derivata vincolo spostamento           Delta_FP        29+1 = 30
    * derivata vincolo rotazione             Delta_MP        32+1 = 33
    */
        
    
   /* Indici */
   integer iNode1FirstPosIndex = pNode1->iGetFirstPositionIndex()+3;
   integer iNode1FirstVelIndex = iNode1FirstPosIndex+6+3;
   integer iNode2FirstPosIndex = pNode2->iGetFirstPositionIndex()+3;
   integer iNode2FirstVelIndex = iNode2FirstPosIndex+6+3;
   integer iFirstReactionIndex = iGetFirstIndex();
   integer iReactionPrimeIndex = iFirstReactionIndex+2;
   
   /* Nota: le reazioni vincolari sono: 
    * Forza,       3 incognite, riferimento globale, 
    * Momento,     2 incognite, riferimento locale
    */

   /* Setta gli indici dei nodi */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutRowIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WM.PutColIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WM.PutRowIndex(3+iCnt, iNode1FirstVelIndex+iCnt);
      WM.PutColIndex(3+iCnt, iNode1FirstVelIndex+iCnt);
      WM.PutRowIndex(6+iCnt, iNode2FirstPosIndex+iCnt);
      WM.PutColIndex(6+iCnt, iNode2FirstPosIndex+iCnt);
      WM.PutRowIndex(9+iCnt, iNode2FirstVelIndex+iCnt);
      WM.PutColIndex(9+iCnt, iNode2FirstVelIndex+iCnt);
   }
   
   /* Setta gli indici delle reazioni */
   for (int iCnt = 1; iCnt <= 4; iCnt++) {
      WM.PutRowIndex(12+iCnt, iFirstReactionIndex+iCnt);
      WM.PutColIndex(12+iCnt, iFirstReactionIndex+iCnt);	
   }   

   
   /* Recupera i dati */
   const Mat3x3& R1(pNode1->GetRRef());
   const Mat3x3& R2(pNode2->GetRRef());
   const Vec3& Omega1(pNode1->GetWRef());
   const Vec3& Omega2(pNode2->GetWRef());
   
   /* F ed M sono gia' state aggiornate da InitialAssRes */
   Vec3 MPrime(XCurr.dGetCoef(iReactionPrimeIndex+1),
	       XCurr.dGetCoef(iReactionPrimeIndex+2),
	       0.);
   
   /* Matrici identita' */   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      /* Contributo di forza all'equazione della forza, nodo 1 */
      WM.PutCoef(iCnt, 12+iCnt, 1.);
      
      /* Contrib. di der. di forza all'eq. della der. della forza, nodo 1 */
      WM.PutCoef(3+iCnt, 14+iCnt, 1.);
      
      /* Contributo di forza all'equazione della forza, nodo 2 */
      WM.PutCoef(6+iCnt, 12+iCnt, -1.);
      
      /* Contrib. di der. di forza all'eq. della der. della forza, nodo 2 */
      WM.PutCoef(9+iCnt, 14+iCnt, -1.);
   }
      
   /* Matrici di rotazione dai nodi alla cerniera nel sistema globale */
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);
   
   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));
   
   /* Ruota il momento e la sua derivata con le matrici della cerniera 
    * rispetto ai nodi */
   Vec3 MTmp(e2b*M.dGet(1)-e1b*M.dGet(2));
   Vec3 MPrimeTmp(e2b*MPrime.dGet(1)-e1b*MPrime.dGet(2));

   Mat3x3 MDeltag1((Mat3x3(Omega2.Cross(MTmp)+MPrimeTmp)+
		    Mat3x3(MTmp, Omega1))*Mat3x3(e3a));
   Mat3x3 MDeltag2(Mat3x3(Omega1.Cross(e3a), MTmp)+
		   Mat3x3(e3a, MPrimeTmp)+
		   Mat3x3(e3a)*Mat3x3(Omega2, MTmp));

   /* Vettori temporanei */
   Vec3 Tmp1(e2b.Cross(e3a));   
   Vec3 Tmp2(e3a.Cross(e1b));
   
   /* Prodotto vettore tra il versore 3 della cerniera secondo il nodo 1
    * ed il versore 1 della cerniera secondo il nodo 2. A convergenza
    * devono essere ortogonali, quindi il loro prodotto vettore deve essere 
    * unitario */

   /* Error handling: il programma si ferma, pero' segnala dov'e' l'errore */
   if (Tmp1.Dot() <= DBL_EPSILON || Tmp2.Dot() <= DBL_EPSILON) {
      std::cerr << "Joint(" << GetLabel() << "): first node hinge axis "
	      "and second node hinge axis are (nearly) orthogonal"
	      << std::endl;
      THROW(Joint::ErrGeneric());
   }   
   
   Vec3 TmpPrime1(e2b.Cross(Omega1.Cross(e3a))-e3a.Cross(Omega2.Cross(e2b)));
   Vec3 TmpPrime2(e3a.Cross(Omega2.Cross(e1b))-e1b.Cross(Omega1.Cross(e3a)));
   
   /* Equazione di momento, nodo 1 */
   WM.Sub(4, 4, Mat3x3(MTmp, e3a));
   WM.Add(4, 16, Mat3x3(e3a, MTmp));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(iCnt, 13, Tmp1.dGet(iCnt));
      WM.PutCoef(iCnt, 14, Tmp2.dGet(iCnt));	
   }
   
   /* Equazione di momento, nodo 2 */
   WM.Add(7, 1, Mat3x3(MTmp, e3a));
   WM.Sub(7, 7, Mat3x3(e3a, MTmp));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(6+iCnt, 13, -Tmp1.dGet(iCnt));
      WM.PutCoef(6+iCnt, 14, -Tmp2.dGet(iCnt));	
   }
   
   /* Derivata dell'equazione di momento, nodo 1 */
   WM.Sub(4, 1, MDeltag1);
   WM.Sub(4, 4, Mat3x3(MTmp, e3a));
   WM.Add(4, 7, MDeltag2);
   WM.Add(4, 10, Mat3x3(e3a, MTmp));
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(3+iCnt, 13, TmpPrime1.dGet(iCnt));
      WM.PutCoef(3+iCnt, 14, TmpPrime2.dGet(iCnt));
      WM.PutCoef(3+iCnt, 15, Tmp1.dGet(iCnt));
      WM.PutCoef(3+iCnt, 16, Tmp2.dGet(iCnt));	
   }
   
   /* Derivata dell'equazione di momento, nodo 2 */
   WM.Add(10, 1, MDeltag1);
   WM.Add(10, 4, Mat3x3(MTmp, e3a));
   WM.Sub(10, 7, MDeltag2);
   WM.Sub(10, 10, Mat3x3(e3a, MTmp));

   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(9+iCnt, 13, -TmpPrime1.dGet(iCnt));
      WM.PutCoef(9+iCnt, 14, -TmpPrime2.dGet(iCnt));	
      WM.PutCoef(9+iCnt, 15, -Tmp1.dGet(iCnt));
      WM.PutCoef(9+iCnt, 16, -Tmp2.dGet(iCnt));	
   }
   
   /* Equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
            
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(13, iCnt, d);
      WM.PutCoef(13, 6+iCnt, -d);
      
      /* Queste sono per la derivata dell'equazione, sono qui solo per 
       * ottimizzazione */
      WM.PutCoef(15, 3+iCnt, d);
      WM.PutCoef(15, 9+iCnt, -d);
      
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(14, iCnt, -d);
      WM.PutCoef(14, 6+iCnt, d);
      
      /* Queste sono per la derivata dell'equazione, sono qui solo per 
       * ottimizzazione */
      WM.PutCoef(16, 3+iCnt, -d);
      WM.PutCoef(16, 9+iCnt, d);
   }   
   
   /* Derivate delle equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
   Vec3 O1mO2(Omega1-Omega2);
   TmpPrime1 = e3a.Cross(O1mO2.Cross(e2b));   
   TmpPrime2 = e2b.Cross(e3a.Cross(O1mO2));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(15, iCnt, TmpPrime1.dGet(iCnt));
      WM.PutCoef(15, 6+iCnt, TmpPrime2.dGet(iCnt));
   }
   
   TmpPrime1 = e3a.Cross(O1mO2.Cross(e1b));
   TmpPrime2 = e1b.Cross(e3a.Cross(O1mO2));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(16, iCnt, TmpPrime1.dGet(iCnt));
      WM.PutCoef(16, 6+iCnt, TmpPrime2.dGet(iCnt));
   }   
   
   return WorkMat;
}


/* Contributo al residuo durante l'assemblaggio iniziale */   
SubVectorHandler& 
PlaneRotationJoint::InitialAssRes(SubVectorHandler& WorkVec,
			       const VectorHandler& XCurr)
{   
   DEBUGCOUT("Entering PlaneRotationJoint::InitialAssRes()" << std::endl);
   
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   InitialWorkSpaceDim(&iNumRows, &iNumCols);
   WorkVec.ResizeReset(iNumRows);

   /* Indici */
   integer iNode1FirstPosIndex = pNode1->iGetFirstPositionIndex()+3;
   integer iNode1FirstVelIndex = iNode1FirstPosIndex+6+3;
   integer iNode2FirstPosIndex = pNode2->iGetFirstPositionIndex()+3;
   integer iNode2FirstVelIndex = iNode2FirstPosIndex+6+3;
   integer iFirstReactionIndex = iGetFirstIndex();
   integer iReactionPrimeIndex = iFirstReactionIndex+2;
   
   /* Setta gli indici */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WorkVec.PutRowIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WorkVec.PutRowIndex(3+iCnt, iNode1FirstVelIndex+iCnt);
      WorkVec.PutRowIndex(6+iCnt, iNode2FirstPosIndex+iCnt);
      WorkVec.PutRowIndex(9+iCnt, iNode2FirstVelIndex+iCnt);
   }
   
   for (int iCnt = 1; iCnt <= 4; iCnt++) {
      WorkVec.PutRowIndex(12+iCnt, iFirstReactionIndex+iCnt);
   }

   /* Recupera i dati */
   const Mat3x3& R1(pNode1->GetRCurr());
   const Mat3x3& R2(pNode2->GetRCurr());
   const Vec3& Omega1(pNode1->GetWCurr());
   const Vec3& Omega2(pNode2->GetWCurr());
   
   /* Aggiorna F ed M, che restano anche per InitialAssJac */
   M = Vec3(XCurr.dGetCoef(iFirstReactionIndex+1),
	    XCurr.dGetCoef(iFirstReactionIndex+2),
	    0.);
   Vec3 MPrime(XCurr.dGetCoef(iReactionPrimeIndex+1),
	       XCurr.dGetCoef(iReactionPrimeIndex+2),
	       0.);
   
   /* Distanza nel sistema globale */
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);

   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));  
   
   /* Ruota il momento e la sua derivata con le matrici della cerniera 
    * rispetto ai nodi */
   Vec3 MTmp(e2b*M.dGet(1)-e1b*M.dGet(2));       
   Vec3 MPrimeTmp(e3a.Cross(MTmp.Cross(Omega2))+MTmp.Cross(Omega1.Cross(e3a))+
		  e2b.Cross(e3a)*MPrime.dGet(1)+e3a.Cross(e1b)*MPrime.dGet(2)); 
   
   /* Equazioni di equilibrio, nodo 1 */
   WorkVec.Sub(1, MTmp.Cross(e3a));
   
   /* Derivate delle equazioni di equilibrio, nodo 1 */
   WorkVec.Sub(4, MPrimeTmp);
   
   /* Equazioni di equilibrio, nodo 2 */
   WorkVec.Add(7, MTmp.Cross(e3a)); 
   
   /* Derivate delle equazioni di equilibrio, nodo 2 */
   WorkVec.Add(10, MPrimeTmp);
   
   /* Equazioni di vincolo di rotazione */
   WorkVec.PutCoef(13, e2b.Dot(e3a));
   WorkVec.PutCoef(14, e1b.Dot(e3a));
   
   /* Derivate delle equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
   Vec3 Tmp((Omega1-Omega2).Cross(e3a));
   WorkVec.PutCoef(15, e2b.Dot(Tmp));
   WorkVec.PutCoef(16, e1b.Dot(Tmp));

   return WorkVec;
}


unsigned int
PlaneRotationJoint::iGetNumPrivData(void) const
{
	return 2;
}

unsigned int
PlaneRotationJoint::iGetPrivDataIdx(const char *s) const
{
	ASSERT(s != NULL);

	if (strcmp(s, "rz") == 0) {
		return 1;
	}

	if (strcmp(s, "wz") == 0) {
		return 2;
	}

	return 0;
}

doublereal PlaneRotationJoint::dGetPrivData(unsigned int i) const
{
   ASSERT(i >= 1 && i <= iGetNumPrivData());
   
   switch (i) {
    case 1: {
	Mat3x3 RTmp(pNode1->GetRCurr().Transpose()*pNode1->GetRPrev()
			*pNode2->GetRPrev().Transpose()*pNode2->GetRCurr()*R2h);
	Vec3 v(MatR2EulerAngles(RTmp));

       return dTheta + v.dGet(3);
    }
      
    case 2: {
       Mat3x3 R2TmpT((pNode2->GetRCurr()*R2h).Transpose());
       Vec3 v(R2TmpT*(pNode2->GetWCurr()-pNode1->GetWCurr()));
       
       return v.dGet(3);
    }
      
    default:
      std::cerr << "Illegal private data" << std::endl;
      THROW(ErrGeneric());
   }
}

/* PlaneRotationJoint - end */


/* AxialRotationJoint - begin */

/* Costruttore non banale */
AxialRotationJoint::AxialRotationJoint(unsigned int uL, const DofOwner* pDO,
		const StructNode* pN1, 
		const StructNode* pN2,
		const Vec3& dTmp1, const Vec3& dTmp2,
		const Mat3x3& R1hTmp, 
		const Mat3x3& R2hTmp,
		const DriveCaller* pDC, flag fOut)
: Elem(uL, Elem::JOINT, fOut), 
Joint(uL, Joint::AXIALROTATION, pDO, fOut), 
DriveOwner(pDC), 
pNode1(pN1), pNode2(pN2), 
d1(dTmp1), R1h(R1hTmp), d2(dTmp2), R2h(R2hTmp), F(0.), M(0.), dTheta(0.)
{
	NO_OP;
}


/* Distruttore banale */
AxialRotationJoint::~AxialRotationJoint(void)
{
	NO_OP;
};


void
AxialRotationJoint::SetValue(VectorHandler& X, VectorHandler& XP) const
{
	Mat3x3 RTmp((pNode1->GetRCurr()*R1h).Transpose()*(pNode2->GetRCurr()*R2h));
	Vec3 v(MatR2EulerAngles(RTmp));

	dTheta = v.dGet(3);
}

void
AxialRotationJoint::AfterConvergence(const VectorHandler& X, 
		const VectorHandler& XP)
{
	Mat3x3 R1Tmp(((pNode1->GetRCurr()*R1h).Transpose()*pNode1->GetRPrev()*R1h).Transpose()
		*((pNode2->GetRCurr()*R2h).Transpose()*pNode2->GetRPrev()*R2h));
	Vec3 v1(MatR2EulerAngles(R1Tmp.Transpose()));

	dTheta += v1.dGet(3);
}


/* Contributo al file di restart */
std::ostream& AxialRotationJoint::Restart(std::ostream& out) const
{
   Joint::Restart(out) << ", axial rotation, "
     << pNode1->GetLabel() 
     << ", reference, node, ", d1.Write(out, ", ")
     << ", hinge, reference, node, 1, ", (R1h.GetVec(1)).Write(out, ", ")
     << ", 2, ", (R1h.GetVec(2)).Write(out, ", ") << ", "
     << pNode2->GetLabel() 
     << ", reference, node, ", d2.Write(out, ", ") 
     << ", hinge, reference, node, 1, ", (R2h.GetVec(1)).Write(out, ", ")
     << ", 2, ", (R2h.GetVec(2)).Write(out, ", ") << ", ";
   
   return pGetDriveCaller()->Restart(out) << ';' << std::endl;
}


/* Assemblaggio jacobiano */
VariableSubMatrixHandler& 
AxialRotationJoint::AssJac(VariableSubMatrixHandler& WorkMat,
			   doublereal dCoef,
			   const VectorHandler& /* XCurr */ ,
			   const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering AxialRotationJoint::AssJac()" << std::endl);
   
   /* Setta la sottomatrice come piena (e' un po' dispersivo, ma lo jacobiano 
    * e' complicato */					
   FullSubMatrixHandler& WM = WorkMat.SetFull();
   
   /* Ridimensiona la sottomatrice in base alle esigenze */
   integer iNumRows = 0;
   integer iNumCols = 0;
   WorkSpaceDim(&iNumRows, &iNumCols);
   WM.ResizeReset(iNumRows, iNumCols);
   
   /* Recupera gli indici delle varie incognite */
   integer iNode1FirstPosIndex = pNode1->iGetFirstPositionIndex();
   integer iNode1FirstMomIndex = pNode1->iGetFirstMomentumIndex();
   integer iNode2FirstPosIndex = pNode2->iGetFirstPositionIndex();
   integer iNode2FirstMomIndex = pNode2->iGetFirstMomentumIndex();
   integer iFirstReactionIndex = iGetFirstIndex();

   /* Recupera i dati che servono */
   Mat3x3 R1(pNode1->GetRRef());
   Mat3x3 R2(pNode2->GetRRef());   
   Vec3 Omega2(pNode2->GetWRef()); /* Serve dopo */
   Vec3 d1Tmp(R1*d1);
   Vec3 d2Tmp(R2*d2);
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);
   /* Suppongo che le reazioni F, M siano gia' state aggiornate da AssRes;
    * ricordo che la forza F e' nel sistema globale, mentre la coppia M
    * e' nel sistema locale ed il terzo termine, M(3), e' nullo in quanto
    * diretto come l'asse attorno al quale la rotazione e' consentita */
   
      
   /* 
    * La cerniera piana ha le prime 3 equazioni uguali alla cerniera sferica;
    * inoltre ha due equazioni che affermano la coincidenza dell'asse 3 del
    * riferimento solidale con la cerniera visto dai due nodi.
    * 
    *      (R1 * R1h * e1)^T * (R2 * R2h * e3) = 0
    *      (R1 * R1h * e2)^T * (R2 * R2h * e3) = 0
    * 
    * A queste equazioni corrisponde una reazione di coppia agente attorno 
    * agli assi 1 e 2 del riferimento della cerniera. La coppia attorno 
    * all'asse 3 e' nulla per definizione. Quindi la coppia nel sistema 
    * globale e':
    *      -R1 * R1h * M       per il nodo 1,
    *       R2 * R2h * M       per il nodo 2
    * 
    * 
    *       xa   ga                   xb   gb                     F     M 
    * Qa |  0    0                     0    0                     I     0  | | xa |   | -F           |
    * Ga |  0    c*(F/\da/\-(Sa*M)/\)  0    0                     da/\  Sa | | ga |   | -da/\F-Sa*M |
    * Qb |  0    0                     0    0                    -I     0  | | xb | = |  F           |
    * Gb |  0    0                     0   -c*(F/\db/\-(Sb*M)/\) -db/\ -Sb | | gb |   |  db/\F+Sb*M |
    * F  | -c*I  c*da/\                c*I -c*db/\                0     0  | | F  |   |  xa+da-xb-db |
    * M1 |  0    c*Tb1^T*Ta3/\         0    c*Ta3^T*Tb1/\         0     0  | | M  |   |  Sb^T*Ta3    |
    * M2 |  0    c*Tb2^T*Ta3/\         0    c*Ta3^T*Tb2/\         0     0  | 
    * 
    * con da = R1*d01, db = R2*d02, c = dCoef,
    * Sa = R1*R1h*[e1,e2], Sb = R2*R2h*[e1, e2],
    * Ta3 = R1*R1h*e3, Tb1 = R2*R2h*e1, Tb2 = R2*R2h*e2
    */

   /* Setta gli indici delle equazioni */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {	
      WM.PutRowIndex(iCnt, iNode1FirstMomIndex+iCnt);
      WM.PutColIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WM.PutRowIndex(6+iCnt, iNode2FirstMomIndex+iCnt);
      WM.PutColIndex(6+iCnt, iNode2FirstPosIndex+iCnt);
      WM.PutRowIndex(12+iCnt, iFirstReactionIndex+iCnt);
      WM.PutColIndex(12+iCnt, iFirstReactionIndex+iCnt);
   }
   
   /* Contributo della forza alle equazioni di equilibrio dei due nodi */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(iCnt, 12+iCnt, 1.);
      WM.PutCoef(6+iCnt, 12+iCnt, -1.);
   }
   
   WM.Add(4, 13, Mat3x3(d1Tmp));
   WM.Add(10, 13, Mat3x3(-d2Tmp));   
   
   /* Moltiplica la forza ed il momento per il coefficiente
    * del metodo */
   Vec3 FTmp = F*dCoef;
   Vec3 MTmp = M*dCoef;
   
   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));
   MTmp = e2b*MTmp.dGet(1)-e1b*MTmp.dGet(2);
   
   Mat3x3 MWedgee3aWedge(MTmp, e3a);
   Mat3x3 e3aWedgeMWedge(e3a, MTmp);
   
   WM.Add(4, 4, Mat3x3(FTmp, d1Tmp)-MWedgee3aWedge-Mat3x3(e3a*M.dGet(3)));
   WM.Add(4, 10, e3aWedgeMWedge);
   
   WM.Add(10, 4, MWedgee3aWedge);   
   WM.Add(10, 10, Mat3x3(-FTmp, d2Tmp)-e3aWedgeMWedge+
	  Mat3x3(e3a*M.dGet(3)));
   
   /* Contributo del momento alle equazioni di equilibrio dei nodi */
   Vec3 Tmp1(e2b.Cross(e3a));
   Vec3 Tmp2(e3a.Cross(e1b));
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(3+iCnt, 16, d);
      WM.PutCoef(9+iCnt, 16, -d);
      
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(3+iCnt, 17, d);
      WM.PutCoef(9+iCnt, 17, -d);
      
      d = e3a.dGet(iCnt);
      WM.PutCoef(3+iCnt, 18, d);	
      WM.PutCoef(9+iCnt, 18, -d);	
   }         
   
   /* Modifica: divido le equazioni di vincolo per dCoef */
   
   /* Equazioni di vincolo degli spostamenti */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(12+iCnt, iCnt, -1.);
      WM.PutCoef(12+iCnt, 6+iCnt, 1.);
   }
   
   WM.Add(13, 4, Mat3x3(d1Tmp));
   WM.Add(13, 10, Mat3x3(-d2Tmp));
   
   /* Equazione di vincolo del momento
    * 
    * Attenzione: bisogna scrivere il vettore trasposto
    *   (Sb[1]^T*(Sa[3]/\))*dCoef
    * Questo pero' e' uguale a:
    *   (-Sa[3]/\*Sb[1])^T*dCoef,
    * che puo' essere ulteriormente semplificato:
    *   (Sa[3].Cross(Sb[1])*(-dCoef))^T;
    */
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(16, 3+iCnt, d);
      WM.PutCoef(16, 9+iCnt, -d);
      
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(17, 3+iCnt, -d);
      WM.PutCoef(17, 9+iCnt, d);
   }
   
   /* Questa equazione non viene divisa per dCoef */
   
   /* Equazione di imposizione della velocita' di rotazione: 
    * (e3a+c(wb/\e3a))^T*(Delta_gPb-Delta_gPa) */
   Vec3 Tmp = e3a+(Omega2.Cross(e3a)*dCoef);
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      doublereal d = Tmp.dGet(iCnt);
      WM.PutCoef(18, 3+iCnt, -d);
      WM.PutCoef(18, 9+iCnt, d);
   }   
   
   return WorkMat;
}


/* Assemblaggio residuo */
SubVectorHandler& AxialRotationJoint::AssRes(SubVectorHandler& WorkVec,
					     doublereal dCoef,
					     const VectorHandler& XCurr,
					     const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering AxialRotationJoint::AssRes()" << std::endl);

   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   WorkSpaceDim(&iNumRows, &iNumCols);
   WorkVec.ResizeReset(iNumRows);
 
   /* Indici */
   integer iNode1FirstMomIndex = pNode1->iGetFirstMomentumIndex();
   integer iNode2FirstMomIndex = pNode2->iGetFirstMomentumIndex();
   integer iFirstReactionIndex = iGetFirstIndex();
   
   /* Indici dei nodi */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {	
      WorkVec.PutRowIndex(iCnt, iNode1FirstMomIndex+iCnt);
      WorkVec.PutRowIndex(6+iCnt, iNode2FirstMomIndex+iCnt);
      WorkVec.PutRowIndex(12+iCnt, iFirstReactionIndex+iCnt);
   }
   
   /* Aggiorna i dati propri */
   F = Vec3(XCurr, iFirstReactionIndex+1);
   M = Vec3(XCurr, iFirstReactionIndex+4);
   
   /* Recupera i dati */
   Vec3 x1(pNode1->GetXCurr());
   Vec3 x2(pNode2->GetXCurr());
   Mat3x3 R1(pNode1->GetRCurr());
   Mat3x3 R2(pNode2->GetRCurr());
   
   /* Costruisce i dati propri nella configurazione corrente */
   Vec3 dTmp1(R1*d1);
   Vec3 dTmp2(R2*d2);
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);
   
   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));
   
   Vec3 MTmp(e2b.Cross(e3a)*M.dGet(1)+e3a.Cross(e1b)*M.dGet(2));
   
   /* Equazioni di equilibrio, nodo 1 */
   WorkVec.Add(1, -F);
   WorkVec.Add(4, F.Cross(dTmp1)-MTmp-e3a*M.dGet(3)); /* Sfrutto  F/\d = -d/\F */
   
   /* Equazioni di equilibrio, nodo 2 */
   WorkVec.Add(7, F);
   WorkVec.Add(10, dTmp2.Cross(F)+MTmp+e3a*M.dGet(3));

   /* Modifica: divido le equazioni di vincolo per dCoef */
   if (dCoef != 0.) {
      
      /* Equazione di vincolo di posizione */
      WorkVec.Add(13, (x1+dTmp1-x2-dTmp2)/dCoef);
      
      /* Equazioni di vincolo di rotazione */
      Vec3 Tmp = Vec3(R1hTmp.GetVec(3));
      WorkVec.PutCoef(16, Tmp.Dot(R2hTmp.GetVec(2))/dCoef);
      WorkVec.PutCoef(17, Tmp.Dot(R2hTmp.GetVec(1))/dCoef);
   }   
   
   /* Questa equazione non viene divisa per dCoef */
   
   /* Equazione di vincolo di velocita' di rotazione */
   Vec3 Omega1(pNode1->GetWCurr());
   Vec3 Omega2(pNode2->GetWCurr());
   doublereal dOmega0 = pGetDriveCaller()->dGet();
   WorkVec.PutCoef(18, dOmega0-e3a.Dot(Omega2-Omega1));
   
   return WorkVec;
}

DofOrder::Order
AxialRotationJoint::GetEqType(unsigned int i) const
{
	ASSERTMSGBREAK(i < iGetNumDof(),
		"INDEX ERROR in AxialRotationJoint::GetEqType");
	if (i == 5) {
		return DofOrder::DIFFERENTIAL;
	}
	
	return DofOrder::ALGEBRAIC;
}


/* Output (da mettere a punto) */
void AxialRotationJoint::Output(OutputHandler& OH) const
{
   if (fToBeOutput()) {
      Mat3x3 R2Tmp(pNode2->GetRCurr()*R2h);
      Mat3x3 R2TmpT(R2Tmp.Transpose());
      Mat3x3 RTmp((pNode1->GetRCurr()*R1h).Transpose()*R2Tmp);
      
      Joint::Output(OH.Joints(), "AxialRotation", GetLabel(),
		    R2TmpT*F, M, F, R2Tmp*M) 
	<< " " << MatR2EulerAngles(RTmp)*dRaDegr << " " << dGet()
	<< " " << R2TmpT*(pNode2->GetWCurr()-pNode1->GetWCurr()) << std::endl;
   }
}


/* Contributo allo jacobiano durante l'assemblaggio iniziale */
VariableSubMatrixHandler& 
AxialRotationJoint::InitialAssJac(VariableSubMatrixHandler& WorkMat,
				  const VectorHandler& XCurr)
{
   /* Per ora usa la matrice piena; eventualmente si puo' 
    * passare a quella sparsa quando si ottimizza */
   FullSubMatrixHandler& WM = WorkMat.SetFull();
   
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   InitialWorkSpaceDim(&iNumRows, &iNumCols);
   WM.ResizeReset(iNumRows, iNumCols);

   /* Equazioni: vedi joints.dvi */
   
   /*       equazioni ed incognite
    * F1                                     Delta_x1         0+1 =  1
    * M1                                     Delta_g1         3+1 =  4
    * FP1                                    Delta_xP1        6+1 =  7
    * MP1                                    Delta_w1         9+1 = 10
    * F2                                     Delta_x2        12+1 = 13
    * M2                                     Delta_g2        15+1 = 16
    * FP2                                    Delta_xP2       18+1 = 19
    * MP2                                    Delta_w2        21+1 = 22
    * vincolo spostamento                    Delta_F         24+1 = 25
    * vincolo rotazione                      Delta_M         27+1 = 28
    * derivata vincolo spostamento           Delta_FP        29+1 = 30
    * derivata vincolo rotazione             Delta_MP        32+1 = 33
    * vincolo di velocita' di rotazione
    */
        
    
   /* Indici */
   integer iNode1FirstPosIndex = pNode1->iGetFirstPositionIndex();
   integer iNode1FirstVelIndex = iNode1FirstPosIndex+6;
   integer iNode2FirstPosIndex = pNode2->iGetFirstPositionIndex();
   integer iNode2FirstVelIndex = iNode2FirstPosIndex+6;
   integer iFirstReactionIndex = iGetFirstIndex();
   integer iReactionPrimeIndex = iFirstReactionIndex+5;
   
   /* Nota: le reazioni vincolari sono: 
    * Forza,       3 incognite, riferimento globale, 
    * Momento,     2 incognite, riferimento locale
    */

   /* Setta gli indici dei nodi */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {
      WM.PutRowIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WM.PutColIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WM.PutRowIndex(6+iCnt, iNode1FirstVelIndex+iCnt);
      WM.PutColIndex(6+iCnt, iNode1FirstVelIndex+iCnt);
      WM.PutRowIndex(12+iCnt, iNode2FirstPosIndex+iCnt);
      WM.PutColIndex(12+iCnt, iNode2FirstPosIndex+iCnt);
      WM.PutRowIndex(18+iCnt, iNode2FirstVelIndex+iCnt);
      WM.PutColIndex(18+iCnt, iNode2FirstVelIndex+iCnt);
   }
   
   /* Setta gli indici delle reazioni */
   for (int iCnt = 1; iCnt <= 5; iCnt++) {
      WM.PutRowIndex(24+iCnt, iFirstReactionIndex+iCnt);
      WM.PutColIndex(24+iCnt, iFirstReactionIndex+iCnt);	
   }   
   
   for (int iCnt = 1; iCnt <= 6; iCnt++) {
      WM.PutRowIndex(29+iCnt, iReactionPrimeIndex+iCnt);
      WM.PutColIndex(29+iCnt, iReactionPrimeIndex+iCnt);	
   }   
   
   /* Recupera i dati */
   Mat3x3 R1(pNode1->GetRRef());
   Mat3x3 R2(pNode2->GetRRef());
   Vec3 Omega1(pNode1->GetWRef());
   Vec3 Omega2(pNode2->GetWRef());   
   /* F ed M sono gia' state aggiornate da InitialAssRes */
   Vec3 FPrime(XCurr, iReactionPrimeIndex+1);
   Vec3 MPrime(XCurr, iReactionPrimeIndex+4);

   /* Matrici identita' */   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      /* Contributo di forza all'equazione della forza, nodo 1 */
      WM.PutCoef(iCnt, 24+iCnt, 1.);
      
      /* Contrib. di der. di forza all'eq. della der. della forza, nodo 1 */
      WM.PutCoef(6+iCnt, 29+iCnt, 1.);
      
      /* Contributo di forza all'equazione della forza, nodo 2 */
      WM.PutCoef(12+iCnt, 24+iCnt, -1.);
      
      /* Contrib. di der. di forza all'eq. della der. della forza, nodo 2 */
      WM.PutCoef(18+iCnt, 29+iCnt, -1.);
      
      /* Equazione di vincolo, nodo 1 */
      WM.PutCoef(24+iCnt, iCnt, -1.);
      
      /* Derivata dell'equazione di vincolo, nodo 1 */
      WM.PutCoef(29+iCnt, 6+iCnt, -1.);
      
      /* Equazione di vincolo, nodo 2 */
      WM.PutCoef(24+iCnt, 12+iCnt, 1.);
      
      /* Derivata dell'equazione di vincolo, nodo 2 */
      WM.PutCoef(29+iCnt, 18+iCnt, 1.);
   }
      
   /* Distanze e matrici di rotazione dai nodi alla cerniera 
    * nel sistema globale */
   Vec3 d1Tmp(R1*d1);
   Vec3 d2Tmp(R2*d2);
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);
   
   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));
   
   /* Ruota il momento e la sua derivata con le matrici della cerniera 
    * rispetto ai nodi */
   Vec3 MTmp(e2b*M.dGet(1)-e1b*M.dGet(2));
   Vec3 MPrimeTmp(e2b*MPrime.dGet(1)-e1b*MPrime.dGet(2));

   /* Matrici F/\d1/\, -F/\d2/\ */
   Mat3x3 FWedged1Wedge(F, d1Tmp);
   Mat3x3 FWedged2Wedge(F, -d2Tmp);
   
   /* Matrici (omega1/\d1)/\, -(omega2/\d2)/\ */
   Mat3x3 O1Wedged1Wedge(Omega1.Cross(d1Tmp));
   Mat3x3 O2Wedged2Wedge(d2Tmp.Cross(Omega2));
   
   Mat3x3 MDeltag1((Mat3x3(Omega2.Cross(MTmp)+MPrimeTmp)+
		    Mat3x3(MTmp, Omega1))*Mat3x3(e3a));
   Mat3x3 MDeltag2(Mat3x3(Omega1.Cross(e3a), MTmp)+
		   Mat3x3(e3a, MPrimeTmp)+
		   Mat3x3(e3a)*Mat3x3(Omega2, MTmp));

   /* Vettori temporanei */
   Vec3 Tmp1(e2b.Cross(e3a));   
   Vec3 Tmp2(e3a.Cross(e1b));
   
   /* Prodotto vettore tra il versore 3 della cerniera secondo il nodo 1
    * ed il versore 1 della cerniera secondo il nodo 2. A convergenza
    * devono essere ortogonali, quindi il loro prodotto vettore deve essere 
    * unitario */

   /* Error handling: il programma si ferma, pero' segnala dov'e' l'errore */
   if (Tmp1.Dot() <= DBL_EPSILON || Tmp2.Dot() <= DBL_EPSILON) {
      std::cerr << "Joint(" << GetLabel() << "): first node hinge axis "
	      "and second node hinge axis are (nearly) orthogonal" 
	      << std::endl;
      THROW(Joint::ErrGeneric());
   }   
   
   Vec3 TmpPrime1(e2b.Cross(Omega1.Cross(e3a))-e3a.Cross(Omega2.Cross(e2b)));
   Vec3 TmpPrime2(e3a.Cross(Omega2.Cross(e1b))-e1b.Cross(Omega1.Cross(e3a)));
   
   /* Equazione di momento, nodo 1 */
   WM.Add(4, 4, FWedged1Wedge-Mat3x3(MTmp, e3a));
   WM.Add(4, 16, Mat3x3(e3a, MTmp));
   WM.Add(4, 25, Mat3x3(d1Tmp));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(3+iCnt, 28, Tmp1.dGet(iCnt));
      WM.PutCoef(3+iCnt, 29, Tmp2.dGet(iCnt));
   }
   
   /* Equazione di momento, nodo 2 */
   WM.Add(4, 16, Mat3x3(MTmp, e3a));
   WM.Add(16, 16, FWedged2Wedge-Mat3x3(e3a, MTmp));
   WM.Add(16, 25, Mat3x3(-d2Tmp));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(15+iCnt, 28, -Tmp1.dGet(iCnt));
      WM.PutCoef(15+iCnt, 29, -Tmp2.dGet(iCnt));	
   }
   
   /* Derivata dell'equazione di momento, nodo 1 */
   WM.Add(10, 4, (Mat3x3(FPrime)+Mat3x3(F, Omega1))*Mat3x3(d1Tmp)-MDeltag1-
	  Mat3x3(e3a*MPrime.dGet(3)));
   WM.Add(10, 10, FWedged1Wedge-Mat3x3(MTmp, e3a)-Mat3x3(e3a*MPrime.dGet(3)));
   WM.Add(10, 16, MDeltag2);
   WM.Add(10, 22, Mat3x3(e3a, MTmp));
   WM.Add(10, 25, O1Wedged1Wedge);
   WM.Add(10, 30, Mat3x3(d1Tmp));
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(9+iCnt, 28, TmpPrime1.dGet(iCnt));
      WM.PutCoef(9+iCnt, 29, TmpPrime2.dGet(iCnt));
      WM.PutCoef(9+iCnt, 33, Tmp1.dGet(iCnt));
      WM.PutCoef(9+iCnt, 34, Tmp2.dGet(iCnt));	
      
      /* Contributo del momento attorno all'asse 3, dovuto alla velocita' 
       * imposta */
      WM.PutCoef(9+iCnt, 35, e3a.dGet(iCnt));	
   }
   
   /* Derivata dell'equazione di momento, nodo 2 */
   WM.Add(22, 4, MDeltag1+Mat3x3(e3a*MPrime.dGet(3)));
   WM.Add(22, 10, Mat3x3(MTmp, e3a)+Mat3x3(e3a*MPrime.dGet(3)));
   WM.Add(22, 16, (Mat3x3(FPrime)+Mat3x3(F, Omega2))*Mat3x3(-d2Tmp)-MDeltag2);
   WM.Add(22, 22, FWedged2Wedge-Mat3x3(e3a, MTmp));
   WM.Add(22, 25, O2Wedged2Wedge);
   WM.Add(22, 30, Mat3x3(-d2Tmp));

   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(21+iCnt, 28, -TmpPrime1.dGet(iCnt));
      WM.PutCoef(21+iCnt, 29, -TmpPrime2.dGet(iCnt));	
      WM.PutCoef(21+iCnt, 33, -Tmp1.dGet(iCnt));
      WM.PutCoef(21+iCnt, 34, -Tmp2.dGet(iCnt));	
      
      /* Contributo del momento attorno all'asse 3, dovuto alla velocita' 
       * imposta */
      WM.PutCoef(21+iCnt, 35, -e3a.dGet(iCnt));
   }
   
   /* Equazione di vincolo di posizione */
   WM.Add(25, 4, Mat3x3(d1Tmp));
   WM.Add(25, 16, Mat3x3(-d2Tmp));
   
   /* Derivata dell'equazione di vincolo di posizione */
   WM.Add(30, 4, O1Wedged1Wedge);
   WM.Add(30, 10, Mat3x3(d1Tmp));
   WM.Add(30, 16, O2Wedged2Wedge);
   WM.Add(30, 22, Mat3x3(-d2Tmp));
   
   /* Equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
            
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(28, 3+iCnt, d);
      WM.PutCoef(28, 15+iCnt, -d);
      
      /* Queste sono per la derivata dell'equazione, sono qui solo per 
       * ottimizzazione */
      WM.PutCoef(33, 9+iCnt, d);
      WM.PutCoef(33, 21+iCnt, -d);
      
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(29, 3+iCnt, -d);
      WM.PutCoef(29, 15+iCnt, d);
      
      /* Queste sono per la derivata dell'equazione, sono qui solo per 
       * ottimizzazione */
      WM.PutCoef(34, 9+iCnt, -d);
      WM.PutCoef(34, 21+iCnt, d);
   }   
   
   /* Derivate delle equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
   Vec3 O1mO2(Omega1-Omega2);
   TmpPrime1 = e3a.Cross(O1mO2.Cross(e2b));   
   TmpPrime2 = e2b.Cross(e3a.Cross(O1mO2));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(33, 3+iCnt, TmpPrime1.dGet(iCnt));
      WM.PutCoef(33, 15+iCnt, TmpPrime2.dGet(iCnt));
   }
   
   TmpPrime1 = e3a.Cross(O1mO2.Cross(e1b));
   TmpPrime2 = e1b.Cross(e3a.Cross(O1mO2));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(34, 3+iCnt, TmpPrime1.dGet(iCnt));
      WM.PutCoef(34, 15+iCnt, TmpPrime2.dGet(iCnt));
   }   
   
   /* Equazione di vincolo di velocita' di rotazione; viene posta qui perche'
    * a questo numero di equazione corrisponde il numero della 
    * relativa incognita */
   Vec3 Tmp = O1mO2.Cross(e3a);
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutCoef(35, 3+iCnt, Tmp.dGet(iCnt));
      doublereal d = e3a.dGet(iCnt);
      WM.PutCoef(35, 9+iCnt, -d);
      WM.PutCoef(35, 21+iCnt, d);
   }   
   
   return WorkMat;
}


/* Contributo al residuo durante l'assemblaggio iniziale */   
SubVectorHandler& 
AxialRotationJoint::InitialAssRes(SubVectorHandler& WorkVec,
				  const VectorHandler& XCurr)
{   
   DEBUGCOUT("Entering AxialRotationJoint::InitialAssRes()" << std::endl);
   
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   InitialWorkSpaceDim(&iNumRows, &iNumCols);
   WorkVec.ResizeReset(iNumRows);

   /* Indici */
   integer iNode1FirstPosIndex = pNode1->iGetFirstPositionIndex();
   integer iNode1FirstVelIndex = iNode1FirstPosIndex+6;
   integer iNode2FirstPosIndex = pNode2->iGetFirstPositionIndex();
   integer iNode2FirstVelIndex = iNode2FirstPosIndex+6;
   integer iFirstReactionIndex = iGetFirstIndex();
   integer iReactionPrimeIndex = iFirstReactionIndex+5;
   
   /* Setta gli indici */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {	
      WorkVec.PutRowIndex(iCnt, iNode1FirstPosIndex+iCnt);
      WorkVec.PutRowIndex(6+iCnt, iNode1FirstVelIndex+iCnt);
      WorkVec.PutRowIndex(12+iCnt, iNode2FirstPosIndex+iCnt);
      WorkVec.PutRowIndex(18+iCnt, iNode2FirstVelIndex+iCnt);
   }
   
   for (int iCnt = 1; iCnt <= 5; iCnt++) {
      WorkVec.PutRowIndex(24+iCnt, iFirstReactionIndex+iCnt);
   }

   for (int iCnt = 1; iCnt <= 6; iCnt++) {
      WorkVec.PutRowIndex(29+iCnt, iReactionPrimeIndex+iCnt);
   }

   /* Recupera i dati */
   Vec3 x1(pNode1->GetXCurr());
   Vec3 x2(pNode2->GetXCurr());
   Vec3 v1(pNode1->GetVCurr());
   Vec3 v2(pNode2->GetVCurr());
   Mat3x3 R1(pNode1->GetRCurr());
   Mat3x3 R2(pNode2->GetRCurr());
   Vec3 Omega1(pNode1->GetWCurr());
   Vec3 Omega2(pNode2->GetWCurr());
   
   /* Aggiorna F ed M, che restano anche per InitialAssJac */
   F = Vec3(XCurr, iFirstReactionIndex+1);
   M = Vec3(XCurr.dGetCoef(iFirstReactionIndex+4),
	    XCurr.dGetCoef(iFirstReactionIndex+5),
	    0.);
   Vec3 FPrime(XCurr, iReactionPrimeIndex+1);
   Vec3 MPrime(XCurr, iReactionPrimeIndex+4);
   
   /* Distanza nel sistema globale */
   Vec3 d1Tmp(R1*d1);
   Vec3 d2Tmp(R2*d2);
   Mat3x3 R1hTmp(R1*R1h);
   Mat3x3 R2hTmp(R2*R2h);

   /* Vettori omega1/\d1, -omega2/\d2 */
   Vec3 O1Wedged1(Omega1.Cross(d1Tmp));
   Vec3 O2Wedged2(Omega2.Cross(d2Tmp));
   
   Vec3 e3a(R1hTmp.GetVec(3));
   Vec3 e1b(R2hTmp.GetVec(1));
   Vec3 e2b(R2hTmp.GetVec(2));  
   
   /* Ruota il momento e la sua derivata con le matrici della cerniera 
    * rispetto ai nodi */
   Vec3 MTmp(e2b*M.dGet(1)-e1b*M.dGet(2));       
   Vec3 MPrimeTmp(e3a.Cross(MTmp.Cross(Omega2))+MTmp.Cross(Omega1.Cross(e3a))+
		  e2b.Cross(e3a)*MPrime.dGet(1)+e3a.Cross(e1b)*MPrime.dGet(2)); 
   
   /* Equazioni di equilibrio, nodo 1 */
   WorkVec.Add(1, -F);
   WorkVec.Add(4, F.Cross(d1Tmp)-MTmp.Cross(e3a)); /* Sfrutto il fatto che F/\d = -d/\F */
   
   /* Derivate delle equazioni di equilibrio, nodo 1 */
   WorkVec.Add(7, -FPrime);
   WorkVec.Add(10, FPrime.Cross(d1Tmp)-O1Wedged1.Cross(F)-MPrimeTmp-
	       e3a*MPrime.dGet(3));
   
   /* Equazioni di equilibrio, nodo 2 */
   WorkVec.Add(13, F);
   WorkVec.Add(16, d2Tmp.Cross(F)+MTmp.Cross(e3a));
   
   /* Derivate delle equazioni di equilibrio, nodo 2 */
   WorkVec.Add(19, FPrime);
   WorkVec.Add(22, d2Tmp.Cross(FPrime)+O2Wedged2.Cross(F)+MPrimeTmp+
	       e3a*MPrime.dGet(3));
   
   /* Equazione di vincolo di posizione */
   WorkVec.Add(25, x1+d1Tmp-x2-d2Tmp);
   
   /* Derivata dell'equazione di vincolo di posizione */
   WorkVec.Add(30, v1+O1Wedged1-v2-O2Wedged2);

   /* Equazioni di vincolo di rotazione */
   WorkVec.PutCoef(28, e2b.Dot(e3a));
   WorkVec.PutCoef(29, e1b.Dot(e3a));
   
   /* Derivate delle equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
   Vec3 Tmp((Omega1-Omega2).Cross(e3a));
   WorkVec.PutCoef(33, e2b.Dot(Tmp));
   WorkVec.PutCoef(34, e1b.Dot(Tmp));

   /* Equazione di vincolo di velocita' di rotazione */
   doublereal Omega0 = pGetDriveCaller()->dGet();
   WorkVec.PutCoef(35, Omega0-e3a.Dot(Omega2-Omega1));

   return WorkVec;
}

unsigned int
AxialRotationJoint::iGetNumPrivData(void) const
{
	return 8;
}

unsigned int
AxialRotationJoint::iGetPrivDataIdx(const char *s) const
{
	ASSERT(s != NULL);

	if (strcmp(s, "rz") == 0) {
		return 1;
	}

	if (strcmp(s, "wz") == 0) {
		return 2;
	}

	if (strcmp(s, "fx") == 0) {
		return 3;
	}

	if (strcmp(s, "fy") == 0) {
		return 4;
	}

	if (strcmp(s, "fz") == 0) {
		return 5;
	}

	if (strcmp(s, "mx") == 0) {
		return 6;
	}


	if (strcmp(s, "my") == 0) {
		return 7;
	}

	if (strcmp(s, "mz") == 0) {
		return 8;
	}

	return 0;
}

doublereal
AxialRotationJoint::dGetPrivData(unsigned int i) const
{
	ASSERT(i >= 1 && i <= iGetNumPrivData());
   
	switch (i) {
	case 1: {
		return dTheta;
	}
      
	case 2: 
		return dGet();
      
	case 3:
		return F.dGet(1);
      
	case 4:
		return F.dGet(2);
      
	case 5:
		return F.dGet(3);
      
	case 6:
		return M.dGet(1);
      
	case 7:
		return M.dGet(2);
      
	case 8:
		return M.dGet(3);
      
	}

	THROW(ErrGeneric());
}

/* AxialRotationJoint - end */


/* PlanePinJoint - begin */

/* Costruttore non banale */
PlanePinJoint::PlanePinJoint(unsigned int uL, const DofOwner* pDO,	       
			     const StructNode* pN,
			     const Vec3& X0Tmp, const Mat3x3& R0Tmp, 
			     const Vec3& dTmp, const Mat3x3& RhTmp,
			     flag fOut)
: Elem(uL, Elem::JOINT, fOut), 
Joint(uL, Joint::PLANEPIN, pDO, fOut), 
pNode(pN), 
X0(X0Tmp), R0(R0Tmp), d(dTmp), Rh(RhTmp),
F(0.), M(0.), dTheta(0.)
{
   NO_OP;
}


/* Distruttore banale */
PlanePinJoint::~PlanePinJoint(void)
{
   NO_OP;
};


void
PlanePinJoint::SetValue(VectorHandler& X, VectorHandler& XP) const
{
	Mat3x3 RTmp(R0.Transpose()*(pNode->GetRCurr()*Rh));
	Vec3 v(MatR2EulerAngles(RTmp));

	dTheta = v.dGet(3);
}

DofOrder::Order
PlanePinJoint::GetEqType(unsigned int i) const
{
	ASSERTMSGBREAK(i < iGetNumDof(), 
		"INDEX ERROR in PlanePinJoint::GetEqType");
   return DofOrder::ALGEBRAIC; 
}

void
PlanePinJoint::AfterConvergence(const VectorHandler& X, 
		const VectorHandler& XP)
{
	Mat3x3 RTmp(pNode->GetRPrev().Transpose()*pNode->GetRCurr()*Rh);
	Vec3 v(MatR2EulerAngles(RTmp));

	dTheta += v.dGet(3);
}


/* Contributo al file di restart */
std::ostream& PlanePinJoint::Restart(std::ostream& out) const
{
   Joint::Restart(out) << ", plane pin, "
     << pNode->GetLabel() 
     << ", reference, node, ", d.Write(out, ", ") 
     << ", hinge, reference, node, 1, ", 
     (Rh.GetVec(1)).Write(out, ", ") << ", 2, ", 
     (Rh.GetVec(2)).Write(out, ", ") 
     << ", reference, global, ", X0.Write(out, ", ") 
     << ", reference, global, 1, ",
     (R0.GetVec(1)).Write(out, ", ") << ", 2, ", 
     (R0.GetVec(2)).Write(out, ", ") << ';' << std::endl;
   
   return out;
}


/* Assemblaggio jacobiano */
VariableSubMatrixHandler& 
PlanePinJoint::AssJac(VariableSubMatrixHandler& WorkMat,
		      doublereal dCoef,
		      const VectorHandler& /* XCurr */ ,
		      const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering PlanePinJoint::AssJac()" << std::endl);
      
   SparseSubMatrixHandler& WM = WorkMat.SetSparse();
   WM.ResizeReset(39, 0);
   
   integer iFirstPositionIndex = pNode->iGetFirstPositionIndex();
   integer iFirstMomentumIndex = pNode->iGetFirstMomentumIndex();
   integer iFirstReactionIndex = iGetFirstIndex();

   Mat3x3 R(pNode->GetRRef());
   Vec3 dTmp(R*d);
   Mat3x3 RhTmp(R*Rh);
   
   
   /* 
    * L'equazione di vincolo afferma che il punto in cui si trova la
    * cerniera deve essere fissato:
    *      x + d = x0
    *      e3_0^Te1 = 0
    *      e3_0^Te2 = 0
    * 
    * con: d = R * d_0
    * 
    * La forza e' data dalla reazione vincolare F, nel sistema globale
    * La coppia dovuta all'eccentricita' e' data rispettivamente da:
    *     d /\ F
    *
    * 
    *       x      g         F
    * Q1 |  0      0             I   0    | | x |   | -F          |
    * G1 |  0      cF/\d1/\-M/\  d/\ e1e2 | | g |   | -d/\F-M     |
    * F  |  I      d/\           0   0    | | F |   |  (x+d-x0)/c |
    * M  |  0      e_0/\e1,e2    0   0    | | M |   |  e_0^Te1,e2 |
    * 
    * con d = R*d_0, c = dCoef
    */


   
   /* Moltiplica la forza ed il momento per il coefficiente
    * del metodo */

   Vec3 e3(R0.GetVec(3));
   Vec3 e1(RhTmp.GetVec(1));
   Vec3 e2(RhTmp.GetVec(2));
   Vec3 MTmp(e2*M.dGet(1)-e1*M.dGet(2));
            
   Vec3 Tmp1((e2).Cross(e3));
   Vec3 Tmp2((e3).Cross(e1));
   
   /* termini di reazione sul nodo (forza e momento) */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutItem(iCnt, iFirstMomentumIndex+iCnt, 
		  iFirstReactionIndex+iCnt, 1.);
      WM.PutItem(3+iCnt, 3+iFirstMomentumIndex+iCnt,
		  iFirstReactionIndex+4, Tmp1.dGet(iCnt));
      WM.PutItem(6+iCnt, 3+iFirstMomentumIndex+iCnt, 
		  iFirstReactionIndex+5, Tmp2.dGet(iCnt));
   }   
   
   WM.PutCross(10, iFirstMomentumIndex+3,
		iFirstReactionIndex, dTmp);
      
   
   /* Nota: F ed M, le reazioni vincolari, sono state aggiornate da AssRes */
   
   /* Termini diagonali del tipo: c*F/\d/\Delta_g 
    * nota: la forza e' gia' moltiplicata per dCoef */      
   WM.PutMat3x3(16, iFirstMomentumIndex+3, iFirstPositionIndex+3, 
		 Mat3x3(F*dCoef, dTmp)+Mat3x3(e3, MTmp*dCoef));

   /* Modifica: divido le equazioni di vincolo per dCoef */
   
   /* termini di vincolo dovuti al nodo 1 */
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      WM.PutItem(24+iCnt, iFirstReactionIndex+iCnt, 
		  iFirstPositionIndex+iCnt, -1.);
   }
   
   WM.PutCross(28, iFirstReactionIndex,
		iFirstPositionIndex+3, dTmp);
   
   for (int iCnt = 1; iCnt <= 3; iCnt ++) {
      WM.PutItem(33+iCnt, iFirstReactionIndex+4, 
		  iFirstPositionIndex+3+iCnt, Tmp1.dGet(iCnt));	
      WM.PutItem(36+iCnt, iFirstReactionIndex+5, 
		  iFirstPositionIndex+3+iCnt, -Tmp2.dGet(iCnt));	
   }
   
   return WorkMat;
}


/* Assemblaggio residuo */
SubVectorHandler& PlanePinJoint::AssRes(SubVectorHandler& WorkVec,
					      doublereal dCoef,
					      const VectorHandler& XCurr,
					      const VectorHandler& /* XPrimeCurr */ )
{
   DEBUGCOUT("Entering PlanePinJoint::AssRes()" << std::endl);
      
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   WorkSpaceDim(&iNumRows, &iNumCols);
   WorkVec.ResizeReset(iNumRows);
 
   integer iFirstMomentumIndex = pNode->iGetFirstMomentumIndex();
   integer iFirstReactionIndex = iGetFirstIndex();
   
   /* Indici dei nodi */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {
      WorkVec.PutRowIndex(iCnt, iFirstMomentumIndex+iCnt);
   }
     
   
   /* Indici del vincolo */
   for (int iCnt = 1; iCnt <= 5; iCnt++) {
      WorkVec.PutRowIndex(6+iCnt, iFirstReactionIndex+iCnt);   
   }
   
   F = Vec3(XCurr, iFirstReactionIndex+1);
   M = Vec3(XCurr.dGetCoef(iFirstReactionIndex+4),
	    XCurr.dGetCoef(iFirstReactionIndex+5),
	    0.);
   
   Vec3 x(pNode->GetXCurr());
   Mat3x3 R(pNode->GetRCurr());
   
   Vec3 dTmp(R*d);
   Mat3x3 RhTmp(R*Rh);
   
   Vec3 e3(R0.GetVec(3));
   Vec3 e1(RhTmp.GetVec(1));
   Vec3 e2(RhTmp.GetVec(2));
   
   WorkVec.Add(1, -F);
   WorkVec.Add(4, F.Cross(dTmp)-(e2*M.dGet(1)-e1*M.dGet(2)).Cross(e3)); /* Sfrutto il fatto che F/\d = -d/\F */
   
   /* Modifica: divido le equazioni di vincolo per dCoef */
   if (dCoef != 0.) {	
      WorkVec.Add(7, (x+dTmp-X0)/dCoef);
      
      WorkVec.PutCoef(10, e3.Dot(e2)/dCoef);
      WorkVec.PutCoef(11, e3.Dot(e1)/dCoef);
   }   
   
   return WorkVec;
}

/* Output (da mettere a punto) */
void PlanePinJoint::Output(OutputHandler& OH) const
{
   if (fToBeOutput()) {
      Mat3x3 RTmp(pNode->GetRCurr()*Rh);
      Mat3x3 RTmpT(RTmp.Transpose());
      Mat3x3 R0Tmp(R0.Transpose()*RTmp);
      
      Joint::Output(OH.Joints(), "PlanePin", GetLabel(),
		    RTmpT*F, M, F, RTmp*M) 
	<< " " << MatR2EulerAngles(R0Tmp)*dRaDegr
	<< " " << RTmpT*(pNode->GetWCurr()) << std::endl;      
   }
}


/* Contributo allo jacobiano durante l'assemblaggio iniziale */
VariableSubMatrixHandler& 
PlanePinJoint::InitialAssJac(VariableSubMatrixHandler& WorkMat,
				   const VectorHandler& XCurr)
{
   FullSubMatrixHandler& WM = WorkMat.SetFull();
   
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   InitialWorkSpaceDim(&iNumRows, &iNumCols);
   WM.ResizeReset(iNumRows, iNumCols);

   /* Equazioni: vedi joints.dvi */
    
   /* Indici */
   integer iFirstPositionIndex = pNode->iGetFirstPositionIndex();
   integer iFirstVelocityIndex = iFirstPositionIndex+6;
   integer iFirstReactionIndex = iGetFirstIndex();
   integer iReactionPrimeIndex = iFirstReactionIndex+5;

   /* Setto gli indici */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {
      WM.PutRowIndex(iCnt, iFirstPositionIndex+iCnt);
      WM.PutColIndex(iCnt, iFirstPositionIndex+iCnt);
      WM.PutRowIndex(6+iCnt, iFirstVelocityIndex+iCnt);
      WM.PutColIndex(6+iCnt, iFirstVelocityIndex+iCnt);
   }
   
   for (int iCnt = 1; iCnt <= 10; iCnt++) {
      WM.PutRowIndex(12+iCnt, iFirstReactionIndex+iCnt);
      WM.PutColIndex(12+iCnt, iFirstReactionIndex+iCnt);
   }   
   
   /* Matrici identita' */
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      /* Contributo di forza all'equazione della forza */
      WM.PutCoef(iCnt, 12+iCnt, 1.);
      
      /* Contrib. di der. di forza all'eq. della der. della forza */
      WM.PutCoef(6+iCnt, 17+iCnt, 1.);
      
      /* Equazione di vincolo */
      WM.PutCoef(12+iCnt, iCnt, -1.);
      
      /* Derivata dell'equazione di vincolo */
      WM.PutCoef(17+iCnt, 6+iCnt, -1.);
   }
   
   /* Recupera i dati */
   Mat3x3 R(pNode->GetRRef());
   Vec3 Omega(pNode->GetWRef());
   /* F, M sono state aggiornate da InitialAssRes */
   Vec3 FPrime(XCurr, iReactionPrimeIndex+1);
   Vec3 MPrime(XCurr.dGetCoef(iReactionPrimeIndex+4),
	       XCurr.dGetCoef(iReactionPrimeIndex+5),
	       0.);
   
   /* Distanza nel sistema globale */
   Vec3 dTmp(R*d);
   Mat3x3 RhTmp(R*Rh);

   Vec3 e3(R0.GetVec(3));
   Vec3 e1(RhTmp.GetVec(1));
   Vec3 e2(RhTmp.GetVec(2));

   /* Vettori temporanei */
   Vec3 Tmp1(e2.Cross(e3));   
   Vec3 Tmp2(e3.Cross(e1));
   
   /* Prodotto vettore tra il versore 3 della cerniera secondo il nodo 1
    * ed il versore 1 della cerniera secondo il nodo 2. A convergenza
    * devono essere ortogonali, quindi il loro prodotto vettore deve essere 
    * unitario */

   /* Error handling: il programma si ferma, pero' segnala dov'e' l'errore */
   if (Tmp1.Dot() <= DBL_EPSILON || Tmp2.Dot() <= DBL_EPSILON) {
      std::cerr << "Joint(" << GetLabel() << "): node hinge axis "
	      "and fixed point hinge axis are (nearly) orthogonal" 
	      << std::endl;
      THROW(Joint::ErrGeneric());
   }   
   
   Vec3 TmpPrime1(e3.Cross(e2.Cross(Omega)));
   Vec3 TmpPrime2(e3.Cross(Omega.Cross(e1)));   
   
   /* Ruota il momento e la sua derivata con le matrici della cerniera 
    * rispetto ai nodi */
   Vec3 MTmp(e2*M.dGet(1)-e1*M.dGet(2));
   Vec3 MPrimeTmp(e2*MPrime.dGet(1)-e1*MPrime.dGet(2));
         
   Mat3x3 MDeltag(Mat3x3(e3, MPrimeTmp)+Mat3x3(e3)*Mat3x3(Omega, MTmp));
   
   /* Matrici F/\d/\ */
   Mat3x3 FWedgedWedge(F, dTmp);
   
   /* Matrici (omega/\d)/\ */
   Mat3x3 OWedgedWedge(Omega.Cross(dTmp));
   
   /* Equazione di momento */
   WM.Add(4, 4, FWedgedWedge+Mat3x3(e3, MTmp));
   WM.Add(4, 13, Mat3x3(dTmp));
   
   /* Derivata dell'equazione di momento */
   WM.Add(10, 4, (Mat3x3(FPrime)+Mat3x3(F, Omega))*Mat3x3(dTmp)+MDeltag);
   WM.Add(10, 10, FWedgedWedge+Mat3x3(e3, MTmp));
   WM.Add(10, 13, OWedgedWedge);
   WM.Add(10, 18, Mat3x3(dTmp));
   
   for (int iCnt = 1; iCnt <= 3; iCnt++) {
      doublereal d = Tmp1.dGet(iCnt);
      WM.PutCoef(3+iCnt, 16, d);
      WM.PutCoef(9+iCnt, 21, d);
      
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(3+iCnt, 17, d);
      WM.PutCoef(9+iCnt, 22, d);
      
      WM.PutCoef(9+iCnt, 16, TmpPrime1.dGet(iCnt));
      WM.PutCoef(9+iCnt, 17, TmpPrime2.dGet(iCnt));
   }
   
   /* Equazione di vincolo */
   WM.Add(13, 4, Mat3x3(dTmp));
   
   /* Derivata dell'equazione di vincolo */
   WM.Add(18, 4, OWedgedWedge);
   WM.Add(18, 10, Mat3x3(dTmp));

   /* Equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */            
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      doublereal d = -Tmp1.dGet(iCnt);
      WM.PutCoef(16, 3+iCnt, d);
      
      /* Queste sono per la derivata dell'equazione, sono qui solo per 
       * ottimizzazione */
      WM.PutCoef(21, 9+iCnt, d);
      
      d = Tmp2.dGet(iCnt);
      WM.PutCoef(17, 3+iCnt, d);
      
      /* Queste sono per la derivata dell'equazione, sono qui solo per 
       * ottimizzazione */
      WM.PutCoef(22, 9+iCnt, d);
   }   
   
   /* Derivate delle equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
   TmpPrime2 = e2.Cross(Omega.Cross(e3));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(21, 3+iCnt, TmpPrime2.dGet(iCnt));
   }
   
   TmpPrime2 = e1.Cross(Omega.Cross(e3));
   for (int iCnt = 1; iCnt <= 3; iCnt++) {	
      WM.PutCoef(22, 3+iCnt, TmpPrime2.dGet(iCnt));
   }      
   
   return WorkMat;
}


/* Contributo al residuo durante l'assemblaggio iniziale */   
SubVectorHandler& 
PlanePinJoint::InitialAssRes(SubVectorHandler& WorkVec,
			const VectorHandler& XCurr)
{   
   DEBUGCOUT("Entering PlanePinJoint::InitialAssRes()" << std::endl);
   
   /* Dimensiona e resetta la matrice di lavoro */
   integer iNumRows = 0;
   integer iNumCols = 0;
   InitialWorkSpaceDim(&iNumRows, &iNumCols);
   WorkVec.ResizeReset(iNumRows);

   /* Indici */
   integer iFirstPositionIndex = pNode->iGetFirstPositionIndex();
   integer iFirstVelocityIndex = iFirstPositionIndex+6;
   integer iFirstReactionIndex = iGetFirstIndex();
   integer iReactionPrimeIndex = iFirstReactionIndex+5;
   
   /* Setta gli indici */
   for (int iCnt = 1; iCnt <= 6; iCnt++) {	
      WorkVec.PutRowIndex(iCnt, iFirstPositionIndex+iCnt);
      WorkVec.PutRowIndex(6+iCnt, iFirstVelocityIndex+iCnt);
   }
   
   for (int iCnt = 1; iCnt <= 10; iCnt++) {	
      WorkVec.PutRowIndex(12+iCnt, iFirstReactionIndex+iCnt);
   }
   
   /* Recupera i dati */
   Vec3 x(pNode->GetXCurr());
   Vec3 v(pNode->GetVCurr());
   Mat3x3 R(pNode->GetRCurr());
   Vec3 Omega(pNode->GetWCurr());
   
   Mat3x3 RhTmp(R*Rh);
   
   F = Vec3(XCurr, iFirstReactionIndex+1);
   M = Vec3(XCurr.dGetCoef(iFirstReactionIndex+4),
	    XCurr.dGetCoef(iFirstReactionIndex+5),
	    0.);
   Vec3 FPrime(XCurr, iReactionPrimeIndex+1);
   Vec3 MPrime(XCurr.dGetCoef(iReactionPrimeIndex+4),
	       XCurr.dGetCoef(iReactionPrimeIndex+5),
	       0.);
   
   /* Versori delle cerniere */
   Vec3 e3(R0.GetVec(3));
   Vec3 e1(RhTmp.GetVec(1));
   Vec3 e2(RhTmp.GetVec(2));

   /* Vettori temporanei */
   Vec3 Tmp1(e2.Cross(e3));   
   Vec3 Tmp2(e3.Cross(e1));
      
   Vec3 TmpPrime1(e3.Cross(e2.Cross(Omega)));
   Vec3 TmpPrime2(e3.Cross(Omega.Cross(e1)));   
   
   /* Distanza nel sistema globale */
   Vec3 dTmp(R*d);

   /* Vettori omega/\d */
   Vec3 OWedged(Omega.Cross(dTmp));
   
   /* Ruota il momento e la sua derivata con le matrici della cerniera 
    * rispetto ai nodi */
   Vec3 MTmp(e2*M.dGet(1)-e1*M.dGet(2));       
   Vec3 MPrimeTmp(e3.Cross(MTmp.Cross(Omega))+
		  e2.Cross(e3)*MPrime.dGet(1)+e3.Cross(e1)*MPrime.dGet(2)); 
   
   /* Equazioni di equilibrio */
   WorkVec.Add(1, -F);
   WorkVec.Add(4, F.Cross(dTmp)-MTmp.Cross(e3)); /* Sfrutto il fatto che F/\d = -d/\F */
   
   /* Derivate delle equazioni di equilibrio, nodo 1 */
   WorkVec.Add(7, -FPrime);
   WorkVec.Add(10, FPrime.Cross(dTmp)-OWedged.Cross(F)-MPrimeTmp);
   
   /* Equazione di vincolo di posizione */
   WorkVec.Add(13, x+dTmp-X0);
   
   /* Equazioni di vincolo di rotazione */
   WorkVec.PutCoef(16, e2.Dot(e3));
   WorkVec.PutCoef(17, e1.Dot(e3));

   /* Derivata dell'equazione di vincolo di posizione */
   WorkVec.Add(18, v+OWedged);
   
   /* Derivate delle equazioni di vincolo di rotazione: e1b~e3a, e2b~e3a */
   Vec3 Tmp(e3.Cross(Omega));
   WorkVec.PutCoef(21, e2.Dot(Tmp));
   WorkVec.PutCoef(22, e1.Dot(Tmp));
      
   return WorkVec;
}

unsigned int
PlanePinJoint::iGetNumPrivData(void) const
{
	return 2;
}

unsigned int
PlanePinJoint::iGetPrivDataIdx(const char *s) const
{
	ASSERT(s != NULL);

	if (strcmp(s, "rz") == 0) {
		return 1;
	}

	if (strcmp(s, "wz") == 0) {
		return 2;
	}

	return 0;
}

doublereal
PlanePinJoint::dGetPrivData(unsigned int i) const
{
   ASSERT(i >= 1 && i <= iGetNumPrivData());
   
   switch (i) {
    case 1: {
	Mat3x3 RTmp(pNode->GetRPrev().Transpose()*pNode->GetRCurr()*Rh);
	Vec3 v(MatR2EulerAngles(RTmp));

       return dTheta + v.dGet(3);
    }
      
    case 2: {
       Mat3x3 RTmpT((pNode->GetRCurr()*Rh).Transpose());
       Vec3 v(RTmpT*pNode->GetWCurr());
       
       return v.dGet(3);
    }
      
    default:
      std::cerr << "Illegal private data" << std::endl;
      THROW(ErrGeneric());
   }
}

/* PlanePinJoint - end */

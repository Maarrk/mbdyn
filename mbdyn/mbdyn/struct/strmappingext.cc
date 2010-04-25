/* $Header$ */
/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 2007-2010
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
#include "mbconfig.h"           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#include "dataman.h"
#include "strmappingext.h"
#include "modalmappingext.h"
#include "Rot.hh"

#include <fstream>
#include <cerrno>
#include <cstdlib>
#include <ctime>

/* StructMappingExtForce - begin */

/* Costruttore */
StructMappingExtForce::StructMappingExtForce(unsigned int uL,
	DataManager *pDM,
	StructNode *pRefNode,
	bool bUseReferenceNodeForces,
	bool bRotateReferenceNodeForces,
	std::vector<StructNode *>& nodes,
	std::vector<Vec3>& offsets,
	std::vector<unsigned>& labels,
	SpMapMatrixHandler *pH,
	std::vector<uint32_t>& mappedlabels,
	bool bLabels,
	bool bOutputAccelerations,
	unsigned uRRot,
	ExtFileHandlerBase *pEFH,
	bool bSendAfterPredict,
	int iCoupling,
	flag fOut)
: Elem(uL, fOut), 
ExtForce(uL, pDM, pEFH, bSendAfterPredict, iCoupling, fOut), 
pRefNode(pRefNode),
bUseReferenceNodeForces(bUseReferenceNodeForces),
bRotateReferenceNodeForces(bRotateReferenceNodeForces),
F0(0.), M0(0.),
F1(0.), M1(0.),
pH(pH),
uPoints(nodes.size()),
uMappedPoints(unsigned(pH->iGetNumRows())/3),
bLabels(bLabels),
bOutputAccelerations(bOutputAccelerations),
uRRot(uRRot),
m_qlabels(mappedlabels),
m_f(3*uPoints),
m_p(3*uMappedPoints),
m_x(3*uPoints),
m_xP(3*uPoints),
m_xPP(0),
m_q(3*uMappedPoints),
m_qP(3*uMappedPoints),
m_qPP(0)
{
	ASSERT(nodes.size() == offsets.size());
	ASSERT(uPoints == pH->iGetNumCols());
	ASSERT(uMappedPoints*3 == unsigned(pH->iGetNumRows()));

	switch (uRRot) {
	case MBC_ROT_THETA:
	case MBC_ROT_MAT:
	case MBC_ROT_EULER_123:
		break;

	default:
		silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
			"invalid reference node rotation type " << uRRot << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	StructNode *pNode = 0;
	unsigned uNodes = 0;
	std::vector<StructNode *>::const_iterator p;
	for (p = nodes.begin(); p != nodes.end(); p++) {
		if (*p != pNode) {
			pNode = *p;
			uNodes++;
		}
	}

	Nodes.resize(uNodes);
	p = nodes.begin();
	std::vector<StructNode *>::const_iterator pPrev = p;
	std::vector<NodeData>::iterator n = Nodes.begin();
	do {
		p++;
		if (*p != *pPrev) {
			n->pNode = *pPrev;
			n->Offsets.resize(p - pPrev);
			n->F = Zero3;
			n->M = Zero3;

			n++;
			pPrev = p;
		}
	} while (p != nodes.end());

	if (bLabels) {
		m_qlabels.resize(3*uMappedPoints);
	}

	unsigned uPts = 0;
	n = Nodes.begin();
	std::vector<Vec3>::const_iterator o = offsets.begin();
	std::vector<uint32_t>::iterator l = labels.begin();
	for (; o != offsets.end(); o++, uPts++) {
		if (uPts == n->Offsets.size()) {
			n++;
			uPts = 0;
		}

		// FIXME: pass labels
		n->Offsets[uPts].uLabel = unsigned(-1);
		n->Offsets[uPts].Offset = *o;
		n->Offsets[uPts].F = Zero3;

		if (bLabels) {
			n->Offsets[uPts].uLabel = *l;
			l++;
		}
	}

	if (bOutputAccelerations) {
		for (unsigned i = 0; i < nodes.size(); i++) {
			DynamicStructNode *pDSN = dynamic_cast<DynamicStructNode *>(Nodes[i].pNode);
			if (pDSN == 0) {
				silent_cerr("StructMappingExtForce"
					"(" << GetLabel() << "): "
					"StructNode(" << Nodes[i].pNode->GetLabel() << ") "
					"is not dynamic"
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			pDSN->ComputeAccelerations(true);
		}

		m_xPP.resize(3*uPoints);
		m_qPP.resize(3*uMappedPoints);
		
	}
}

StructMappingExtForce::~StructMappingExtForce(void)
{
	NO_OP;
}

bool
StructMappingExtForce::Prepare(ExtFileHandlerBase *pEFH)
{
	bool bResult = true;

	switch (pEFH->NegotiateRequest()) {
	case ExtFileHandlerBase::NEGOTIATE_NO:
		break;

	case ExtFileHandlerBase::NEGOTIATE_CLIENT: {
		std::ostream *outfp = pEFH->GetOutStream();
		if (outfp) {

#ifdef USE_SOCKET
		} else {
			char buf[sizeof(uint32_t) + sizeof(uint32_t)];
			uint32_t *uint32_ptr;

			uint32_ptr = (uint32_t *)&buf[0];
			uint32_ptr[0] = MBC_NODAL | MBC_U_ROT_REF_NODE(uRRot);
			if (pRefNode != 0) {
				uint32_ptr[0] |= MBC_REF_NODE;
			}

			if (bLabels) {
				uint32_ptr[0] |= MBC_LABELS;
			}

			if (bOutputAccelerations) {
				uint32_ptr[0] |= MBC_ACCELS;
			}

			uint32_ptr[1] = uPoints;

			ssize_t rc = send(pEFH->GetOutFileDes(),
				(const void *)buf, sizeof(buf),
				pEFH->GetSendFlags());
			if (rc == -1) {
				int save_errno = errno;
				char *err_msg = strerror(save_errno);
				silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
					"negotiation request send() failed "
					"(" << save_errno << ": " << err_msg << ")"
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);

			} else if (rc != sizeof(buf)) {
				silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
					"negotiation request send() failed "
					"(sent " << rc << " of " << sizeof(buf) << " bytes)"
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
#endif // USE_SOCKET
		}
		} break;

	case ExtFileHandlerBase::NEGOTIATE_SERVER: {
		unsigned uN;
		unsigned uNodal;
		bool bRef;
		unsigned uRR;
		unsigned uR;
		bool bA;
		bool bL;

		std::istream *infp = pEFH->GetInStream();
		if (infp) {

#ifdef USE_SOCKET
		} else {
			char buf[sizeof(uint32_t) + sizeof(uint32_t)];
			uint32_t *uint32_ptr;

			ssize_t rc = recv(pEFH->GetInFileDes(),
				(void *)buf, sizeof(buf),
				pEFH->GetRecvFlags());
			if (rc == -1) {
				int save_errno = errno;
				char *err_msg = strerror(save_errno);
				silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
					"negotiation response recv() failed "
					"(" << save_errno << ": " << err_msg << ")"
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);

			} else if (rc != sizeof(buf)) {
				silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
					"negotiation response recv() failed "
					"(got " << rc << " of " << sizeof(buf) << " bytes)"
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			uint32_ptr = (uint32_t *)&buf[0];
			uNodal = (uint32_ptr[0] & MBC_MODAL_NODAL_MASK);
			bRef = (uint32_ptr[0] & MBC_REF_NODE);
			uRR = (uint32_ptr[0] & MBC_REF_NODE_ROT_MASK);
			uR = (uint32_ptr[0] & MBC_ROT_MASK);
			bL = (uint32_ptr[0] & MBC_LABELS);
			bA = (uint32_ptr[0] & MBC_ACCELS);

			uN = uint32_ptr[1];
#endif // USE_SOCKET
		}

		if (uNodal != MBC_NODAL) {
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"negotiation response failed: expecting MBC_NODAL "
				"(=" << MBC_MODAL << "), got " << uNodal
				<< std::endl);
			bResult = false;
		}

		if ((pRefNode != 0 && !bRef) || (pRefNode == 0 && bRef)) {
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"negotiation response failed: reference node configuration mismatch "
				"(local=" << (pRefNode != 0 ? "yes" : "no") << ", remote=" << (bRef ? "yes" : "no") << ")"
				<< std::endl);
			bResult = false;
		}

		if (uR != MBC_ROT_NONE) {
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"negotiation response failed: orientation output mismatch "
				"(local=" << MBC_ROT_NONE << ", remote=" << uR << ")"
				<< std::endl);
			bResult = false;
		}

		if (uRR != MBC_U_ROT_REF_NODE(uRRot)) {
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"negotiation response failed: orientation output mismatch "
				"(local=" << MBC_U_ROT_REF_NODE(uRRot) << ", remote=" << uR << ")"
				<< std::endl);
			bResult = false;
		}

		if (bL != bLabels) {
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"negotiation response failed: labels output mismatch "
				"(local=" << (bLabels ? "yes" : "no") << ", remote=" << (bL ? "yes" : "no") << ")"
				<< std::endl);
			bResult = false;
		}

		if (bA != bOutputAccelerations) {
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"negotiation response failed: acceleration output mismatch "
				"(local=" << (bOutputAccelerations ? "yes" : "no") << ", remote=" << (bA ? "yes" : "no") << ")"
				<< std::endl);
			bResult = false;
		}

		if (uN != uPoints) {
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"negotiation response failed: node number mismatch "
				"(local=" << uPoints << ", remote=" << uN << ")"
				<< std::endl);
			bResult = false;
		}
		} break;

	default:
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	return bResult;
}

/*
 * Send output to companion software
 */
void
StructMappingExtForce::Send(ExtFileHandlerBase *pEFH, ExtFileHandlerBase::SendWhen when)
{
	std::ostream *outfp = pEFH->GetOutStream();
	if (outfp) {
		SendToStream(*outfp, when);

	} else {
		SendToFileDes(pEFH->GetOutFileDes(), when);
	}
}

void
StructMappingExtForce::SendToStream(std::ostream& outf, ExtFileHandlerBase::SendWhen when)
{
	if (pRefNode) {
		const Vec3& xRef = pRefNode->GetXCurr();
		const Mat3x3& RRef = pRefNode->GetRCurr();
		const Vec3& xpRef = pRefNode->GetVCurr();
		const Vec3& wRef = pRefNode->GetWCurr();
		const Vec3& xppRef = pRefNode->GetXPPCurr();
		const Vec3& wpRef = pRefNode->GetWPCurr();

		if (bLabels) {
			outf
				<< pRefNode->GetLabel()
				<< " ";
		}

		outf
			<< xRef
			<< " " << RRef
			<< " " << xpRef
			<< " " << wRef;

		if (bOutputAccelerations) {
			outf
				<< " " << xppRef
				<< " " << wpRef;
		}
		outf << std::endl;

		for (unsigned i = 0; i < Nodes.size(); i++) {
#if 0
			Vec3 f(Nodes[i]->GetRCurr()*Offsets[i]);
			Vec3 x(Nodes[i]->GetXCurr() + f);
			Vec3 Dx(x - xRef);
			Mat3x3 DR(RRef.MulTM(Nodes[i]->GetRCurr()));
			Vec3 v(Nodes[i]->GetVCurr() + Nodes[i]->GetWCurr().Cross(f));
			Vec3 Dv(v - xpRef - wRef.Cross(Dx));
			const Vec3& w(Nodes[i]->GetWCurr());

			// manipulate

			if (bLabels) {
				outf
					<< Nodes[i]->GetLabel()
					<< " ";
			}

			outf
				<< RRef.MulTV(Dx);

			switch (uRot) {
			case MBC_ROT_NONE:
				break;

			case MBC_ROT_MAT:
				outf
					<< " " << DR;
				break;

			case MBC_ROT_THETA:
				outf
					<< " " << RotManip::VecRot(DR);
				break;

			case MBC_ROT_EULER_123:
				outf
					<< " " << MatR2EulerAngles123(DR)*dRaDegr;
				break;
			}

			outf
				<< " " << RRef.MulTV(Dv);
			
			if (uRot != MBC_ROT_NONE) {
				outf
					<< " " << RRef.MulTV(w - wRef);
			}

			if (bOutputAccelerations) {
				const Vec3& xpp(Nodes[i]->GetXPPCurr());

				outf
					<< " " << RRef.MulTV(xpp - xppRef - wpRef.Cross(Dx)
							- wRef.Cross(wRef.Cross(Dx) + Dv*2));
				if (uRot != MBC_ROT_NONE) {
					const Vec3& wp(Nodes[i]->GetWPCurr());

					outf
						<< " " << RRef.MulTV(wp - wpRef - wRef.Cross(w));
				}
			}
			outf << std::endl;
#endif
		}

	} else {
		for (unsigned i = 0; i < Nodes.size(); i++) {
#if 0
			/*
				p = x + f
				R = R
				v = xp + w cross f
				w = w
				a = xpp + wp cross f + w cross w cross f
				wp = wp
			 */

			// Optimization of the above formulas
			const Mat3x3& R = Nodes[i]->GetRCurr();
			Vec3 f = R*Offsets[i];
			Vec3 x = Nodes[i]->GetXCurr() + f;
			const Vec3& w = Nodes[i]->GetWCurr();
			Vec3 wCrossf = w.Cross(f);
			Vec3 v = Nodes[i]->GetVCurr() + wCrossf;

			if (bLabels) {
				outf
					<< Nodes[i]->GetLabel()
					<< " ";
			}

			outf
				<< x;

			switch (uRot) {
			case MBC_ROT_NONE:
				break;

			case MBC_ROT_MAT:
				outf
					<< " " << R;
				break;

			case MBC_ROT_THETA:
				outf
					<< " " << RotManip::VecRot(R);
				break;

			case MBC_ROT_EULER_123:
				outf
					<< " " << MatR2EulerAngles123(R)*dRaDegr;
				break;
			}
			outf
				<< " " << v;

			if (uRot != MBC_ROT_NONE) {
				outf
					<< " " << w;
			}

			if (bOutputAccelerations) {
				const Vec3& wp = Nodes[i]->GetWPCurr();
				Vec3 a = Nodes[i]->GetXPPCurr() + wp.Cross(f) + w.Cross(wCrossf);

				outf
					<< " " << a;

				if (uRot != MBC_ROT_NONE) {
					outf
						<< " " << wp;
				}
			}

			outf << std::endl;
#endif
		}
	}
}

void
StructMappingExtForce::SendToFileDes(int outfd, ExtFileHandlerBase::SendWhen when)
{
#ifdef USE_SOCKET
	if (pRefNode) {
		const Vec3& xRef = pRefNode->GetXCurr();
		const Mat3x3& RRef = pRefNode->GetRCurr();
		const Vec3& xpRef = pRefNode->GetVCurr();
		const Vec3& wRef = pRefNode->GetWCurr();
		const Vec3& xppRef = pRefNode->GetXPPCurr();
		const Vec3& wpRef = pRefNode->GetWPCurr();

		if (bLabels) {
			uint32_t l = pRefNode->GetLabel();
			send(outfd, (void *)&l, sizeof(l), 0);
		}

		send(outfd, (void *)xRef.pGetVec(), 3*sizeof(doublereal), 0);
		switch (uRRot) {
		case MBC_ROT_MAT:
			send(outfd, (void *)RRef.pGetMat(), 9*sizeof(doublereal), 0);
			break;

		case MBC_ROT_THETA: {
			Vec3 Theta(RotManip::VecRot(RRef));
			send(outfd, (void *)Theta.pGetVec(), 3*sizeof(doublereal), 0);
			} break;

		case MBC_ROT_EULER_123: {
			Vec3 E(MatR2EulerAngles123(RRef)*dRaDegr);
			send(outfd, (void *)E.pGetVec(), 3*sizeof(doublereal), 0);
			} break;
		}
		send(outfd, (void *)xpRef.pGetVec(), 3*sizeof(doublereal), 0);
		send(outfd, (void *)wRef.pGetVec(), 3*sizeof(doublereal), 0);
		if (bOutputAccelerations) {
			send(outfd, (void *)xppRef.pGetVec(), 3*sizeof(doublereal), 0);
			send(outfd, (void *)wpRef.pGetVec(), 3*sizeof(doublereal), 0);
		}

		for (unsigned p3 = 0, n = 0; n < Nodes.size(); n++) {
			for (unsigned o = 0; o < Nodes[n].Offsets.size(); o++, p3 += 3) {
				Vec3 f(Nodes[n].pNode->GetRCurr()*Nodes[n].Offsets[o].Offset);
				Vec3 x(Nodes[n].pNode->GetXCurr() + f);
				Vec3 Dx(x - xRef);
				Vec3 v(Nodes[n].pNode->GetVCurr() + Nodes[n].pNode->GetWCurr().Cross(f));
				Vec3 Dv(v - xpRef - wRef.Cross(Dx));
				const Vec3& w(Nodes[n].pNode->GetWCurr());

				Vec3 xTilde(RRef.MulTV(Dx));
				m_x.Put(p3 + 1, xTilde);

				Vec3 vTilde(RRef.MulTV(Dv));
				m_xP.Put(p3 + 1, vTilde);

				if (bOutputAccelerations) {
					const Vec3& xpp = Nodes[n].pNode->GetXPPCurr();
					const Vec3& wp = Nodes[n].pNode->GetWPCurr();

					Vec3 xppTilde(RRef.MulTV(xpp - xppRef - wpRef.Cross(Dx)
						- wRef.Cross(wRef.Cross(Dx) + Dv*2)
						+ wp.Cross(f) + w.Cross(w.Cross(f))));
					m_xPP.Put(p3 + 1, xppTilde);
				}
			}
		}

	} else {
		for (unsigned p3 = 0, n = 0; n < Nodes.size(); n++) {
			for (unsigned o = 0; o < Nodes[n].Offsets.size(); o++, p3 +=3 ) {
				/*
					p = x + f
					R = R
					v = xp + w cross f
					w = w
					a = xpp + wp cross f + w cross w cross f
					wp = wp
				 */

				// Optimization of the above formulas
				const Mat3x3& R = Nodes[n].pNode->GetRCurr();
				Vec3 f = R*Nodes[n].Offsets[o].Offset;
				Vec3 x = Nodes[n].pNode->GetXCurr() + f;
				const Vec3& w = Nodes[n].pNode->GetWCurr();
				Vec3 wCrossf = w.Cross(f);
				Vec3 v = Nodes[n].pNode->GetVCurr() + wCrossf;

				m_x.Put(p3 + 1, x);
				m_xP.Put(p3 + 1, v);

				if (bOutputAccelerations) {
					const Vec3& wp = Nodes[n].pNode->GetWPCurr();
					Vec3 xpp = Nodes[n].pNode->GetXPPCurr() + wp.Cross(f) + w.Cross(wCrossf);

					m_xPP.Put(p3 + 1, xpp);
				}
			}
		}
	}

	if (bLabels) {
		send(outfd, &m_qlabels[0], sizeof(uint32_t)*m_qlabels.size(), 0);
	}

	pH->MatVecMul(m_q, m_x);
	pH->MatVecMul(m_qP, m_xP);

	send(outfd, &m_q[0], sizeof(double)*m_q.size(), 0);
	send(outfd, &m_qP[0], sizeof(double)*m_qP.size(), 0);

	if (bOutputAccelerations) {
		pH->MatVecMul(m_qPP, m_xPP);
		send(outfd, &m_qPP[0], sizeof(double)*m_qPP.size(), 0);
	}

#else // ! USE_SOCKET
	throw ErrGeneric(MBDYN_EXCEPT_ARGS);
#endif // ! USE_SOCKET
}

void
StructMappingExtForce::Recv(ExtFileHandlerBase *pEFH)
{
	std::istream *infp = pEFH->GetInStream();
	if (infp) {
		RecvFromStream(*infp);

	} else {
		RecvFromFileDes(pEFH->GetInFileDes());
	}
}

void
StructMappingExtForce::RecvFromStream(std::istream& inf)
{
#if 0
	if (pRefNode) {
		unsigned l;
		doublereal *f = F0.pGetVec(), *m = M0.pGetVec();

		if (bLabels) {
			inf >> l;
		}

		inf >> f[0] >> f[1] >> f[2];
		if (uRRot != MBC_ROT_NONE) {
			inf >> m[0] >> m[1] >> m[2];
		}
	}

	for (unsigned i = 0; i < Nodes.size(); i++) {
		/* assume unsigned int label */
		unsigned l;
		doublereal f[3], m[3];

		if (bLabels) {
			inf >> l;

			if (Nodes[i]->GetLabel() != l) {
				silent_cerr("StructMappingExtForce"
					"(" << GetLabel() << "): "
					"invalid " << i << "-th label " << l
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
		}

		inf
			>> f[0] >> f[1] >> f[2];
		if (uRot != MBC_ROT_NONE) {
			inf >> m[0] >> m[1] >> m[2];
		}

		if (!inf) {
			break;
		}

		F[i] = Vec3(f);
		if (uRot != MBC_ROT_NONE) {
			M[i] = Vec3(m);
		}
	}
#endif
}

void
StructMappingExtForce::RecvFromFileDes(int infd)
{
#ifdef USE_SOCKET
	if (pRefNode) {
		unsigned l;
		size_t ulen = 0;
		char buf[sizeof(uint32_t) + 6*sizeof(doublereal)];
		doublereal *f;
		ssize_t len;

		if (bLabels) {
			ulen = sizeof(uint32_t);
		}

		ulen += 6*sizeof(doublereal);

		len = recv(infd, (void *)buf, ulen, pEFH->GetRecvFlags());
		if (len == -1) {
			int save_errno = errno;
			char *err_msg = strerror(save_errno);
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"recv() failed (" << save_errno << ": "
				<< err_msg << ")" << std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);

		} else if (unsigned(len) != ulen) {
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"recv() failed " "(got " << len << " of "
				<< ulen << " bytes)" << std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}

		if (bLabels) {
			uint32_t *uint32_ptr = (uint32_t *)buf;
			l = uint32_ptr[0];
			f = (doublereal *)&uint32_ptr[1];

		} else {
			f = (doublereal *)buf;
		}

		F0 = Vec3(&f[0]);
		M0 = Vec3(&f[3]);
	}

	if (bLabels) {
		// Hack!
		ssize_t len = recv(infd, (void *)&m_p[0], sizeof(uint32_t)*m_p.size(),
			pEFH->GetRecvFlags());
		if (len == -1) {
			int save_errno = errno;
			char *err_msg = strerror(save_errno);
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"recv() failed (" << save_errno << ": "
				<< err_msg << ")" << std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);

		} else if (unsigned(len) != sizeof(double)*m_p.size()) {
			silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
				"recv() failed " "(got " << len << " of "
				<< sizeof(uint32_t)*m_p.size() << " bytes)" << std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}

		uint32_t *labels = (uint32_t *)&m_p[0];
		for (unsigned l = 0; l < m_qlabels.size(); l++) {
			if (labels[l] != m_qlabels[l]) {
				silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
					"label mismatch for point #" << l << "/" << m_qlabels.size()
					<< " local=" << m_qlabels[l] << " remote=" << labels[l] << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
		}
	}

	ssize_t len = recv(infd, (void *)&m_p[0], sizeof(double)*m_p.size(),
		pEFH->GetRecvFlags());
	if (len == -1) {
		int save_errno = errno;
		char *err_msg = strerror(save_errno);
		silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
			"recv() failed (" << save_errno << ": "
			<< err_msg << ")" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);

	} else if (unsigned(len) != sizeof(double)*m_p.size()) {
		silent_cerr("StructMappingExtForce(" << GetLabel() << "): "
			"recv() failed " "(got " << len << " of "
			<< sizeof(double)*m_p.size() << " bytes)" << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	pH->MatTVecMul(m_f, m_p);

	if (pRefNode) {
		for (unsigned p3 = 0, n = 0; n < Nodes.size(); n++) {
			Nodes[n].F = Zero3;
			Nodes[n].M = Zero3;
			for (unsigned o = 0; o < Nodes[n].Offsets.size(); o++, p3 += 3) {
				Nodes[n].Offsets[o].F = pRefNode->GetRCurr()*Vec3(m_f, p3 + 1);
				Nodes[n].F += Nodes[n].Offsets[o].F;
				Vec3 f(Nodes[n].pNode->GetRCurr()*Nodes[n].Offsets[o].Offset);
				Nodes[n].M += f.Cross(Nodes[n].Offsets[o].F);
			}
		}

	} else {
		for (unsigned p3 = 0, n = 0; n < Nodes.size(); n++) {
			Nodes[n].F = Zero3;
			Nodes[n].M = Zero3;
			for (unsigned o = 0; o < Nodes[n].Offsets.size(); o++, p3 += 3) {
				Nodes[n].Offsets[o].F = Vec3(m_f, p3 + 1);
				Nodes[n].F += Nodes[n].Offsets[o].F;
				Vec3 f(Nodes[n].pNode->GetRCurr()*Nodes[n].Offsets[o].Offset);
				Nodes[n].M += f.Cross(Nodes[n].Offsets[o].F);
			}
		}
	}
#else // ! USE_SOCKET
	throw ErrGeneric(MBDYN_EXCEPT_ARGS);
#endif // ! USE_SOCKET
}

SubVectorHandler&
StructMappingExtForce::AssRes(SubVectorHandler& WorkVec,
	doublereal dCoef,
	const VectorHandler& XCurr, 
	const VectorHandler& XPrimeCurr)
{
	ExtForce::Recv();

	if (pRefNode) {
		integer iSize = Nodes.size();
		if (bUseReferenceNodeForces) {
			iSize++;
		}

		WorkVec.ResizeReset(6*iSize);

		const Vec3& xRef = pRefNode->GetXCurr();
		const Mat3x3& RRef = pRefNode->GetRCurr();

		// manipulate
		if (bUseReferenceNodeForces) {
			if (bRotateReferenceNodeForces) {
				F1 = RRef*F0;
				M1 = RRef*M0;

			} else {
				F1 = F0;
				M1 = M0;
			}
		}

		for (unsigned n = 0; n < Nodes.size(); n++) {
			integer iFirstIndex = Nodes[n].pNode->iGetFirstMomentumIndex();
			for (int r = 1; r <= 6; r++) {
				WorkVec.PutRowIndex(n*6 + r, iFirstIndex + r);
			}

			WorkVec.Add(n*6 + 1, Nodes[n].F);
			WorkVec.Add(n*6 + 4, Nodes[n].M);

			if (bUseReferenceNodeForces) {
				F1 -= Nodes[n].F;
				M1 -= Nodes[n].M + (Nodes[n].pNode->GetXCurr() - xRef).Cross(Nodes[n].F);
			}
		}

		if (bUseReferenceNodeForces) {
			unsigned n = Nodes.size();
			integer iFirstIndex = pRefNode->iGetFirstMomentumIndex();
			for (int r = 1; r <= 6; r++) {
				WorkVec.PutRowIndex(n*6 + r, iFirstIndex + r);
			}

			WorkVec.Add(n*6 + 1, F1);
			WorkVec.Add(n*6 + 4, M1);
		}

	} else {
		WorkVec.ResizeReset(6*Nodes.size());

		for (unsigned n = 0; n < Nodes.size(); n++) {
			integer iFirstIndex = Nodes[n].pNode->iGetFirstMomentumIndex();
			for (int r = 1; r <= 6; r++) {
				WorkVec.PutRowIndex(n*6 + r, iFirstIndex + r);
			}

			WorkVec.Add(n*6 + 1, Nodes[n].F);
			WorkVec.Add(n*6 + 4, Nodes[n].M);
		}
	}

	return WorkVec;
}

void
StructMappingExtForce::Output(OutputHandler& OH) const
{
	std::ostream& out = OH.Forces();

	if (pRefNode) {
		out << GetLabel() << "#" << pRefNode->GetLabel()
			<< " " << F1
			<< " " << M1
			<< " " << F0
			<< " " << M0
			<< std::endl;
	}

	for (unsigned n = 0; n < Nodes.size(); n++) {
		out << GetLabel() << "@" << Nodes[n].pNode->GetLabel()
			<< " " << Nodes[n].F
			<< " " << Nodes[n].M
			<< std::endl;
	}
}
 
void
StructMappingExtForce::GetConnectedNodes(std::vector<const Node *>& connectedNodes) const
{
	unsigned n = Nodes.size();
	if (pRefNode) {
		n++;
	}
	connectedNodes.resize(n);

	for (n = 0; n < Nodes.size(); n++) {
		connectedNodes[n] = Nodes[n].pNode;
	}

	if (pRefNode) {
		connectedNodes[n] = pRefNode;
	}
}

Elem*
ReadStructMappingExtForce(DataManager* pDM, 
	MBDynParser& HP, 
	unsigned int uLabel)
{
	ExtFileHandlerBase *pEFH = 0;
	int iCoupling;

	bool bSendAfterPredict;
	ReadExtForce(pDM, HP, uLabel, pEFH, bSendAfterPredict, iCoupling);

	StructNode *pRefNode(0);
	if (HP.IsKeyWord("reference" "node")) {
		pRefNode = dynamic_cast<StructNode *>(pDM->ReadNode(HP, Node::STRUCTURAL));
		if (pRefNode == 0) {
			silent_cerr("StructMappingExtForce(" << uLabel << "): "
				"illegal reference node "
				"at line " << HP.GetLineData() << std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
	}

	bool bLabels(false);
	unsigned uRRot = MBC_ROT_MAT;
	bool bOutputAccelerations(false);
	bool bUseReferenceNodeForces(true);
	bool bRotateReferenceNodeForces(true);

	bool bGotLabels(false);
	bool bGotRot(false);
	bool bGotAccels(false);
	bool bGotUseRefForces(false);

	while (HP.IsArg()) {
		if (HP.IsKeyWord("no" "labels")) {
			silent_cerr("StructMappingExtForce(" << uLabel << "): "
				"use of \"no labels\" deprecated in favor of \"labels, { yes | no }\" at line "
				<< HP.GetLineData() << std::endl);

			if (bGotLabels) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"\"no labels\" already specified at line "
					<< HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			bLabels = false;
			bGotLabels = true;

		} else if (HP.IsKeyWord("labels")) {
			if (bGotLabels) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"\"labels\" already specified at line "
					<< HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			bLabels = HP.GetYesNo(bLabels);
			bGotLabels = true;

		} else if (HP.IsKeyWord("orientation")) {
			if (bGotRot) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"\"orientation\" already specified at line "
					<< HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			if (HP.IsKeyWord("none")) {
				uRRot = MBC_ROT_NONE;

			} else if (HP.IsKeyWord("orientation" "vector")) {
				uRRot = MBC_ROT_THETA;

			} else if (HP.IsKeyWord("orientation" "matrix")) {
				uRRot = MBC_ROT_MAT;

			} else if (HP.IsKeyWord("euler" "123")) {
				uRRot = MBC_ROT_EULER_123;

			} else {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"unknown \"orientation\" format at line "
					<< HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			bGotRot = true;	

		} else if (HP.IsKeyWord("accelerations")) {
			if (bGotAccels) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"\"accelerations\" already specified at line "
					<< HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			bOutputAccelerations = HP.GetYesNo(bOutputAccelerations);
			bGotAccels = true;

		} else if (HP.IsKeyWord("use" "reference" "node" "forces")) {
			if (pRefNode == 0) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"\"use reference node forces\" only meaningful when reference node is used at line "
					<< HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			if (bGotUseRefForces) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"\"use reference node forces\" already specified at line "
					<< HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
			
			bUseReferenceNodeForces = HP.GetYesNo(bUseReferenceNodeForces);
			bGotUseRefForces = true;

			if (bUseReferenceNodeForces && HP.IsKeyWord("rotate" "reference" "node" "forces")) {
				bRotateReferenceNodeForces = HP.GetYesNo(bRotateReferenceNodeForces);
			}

		} else {
			break;
		}
	}

	if (!HP.IsKeyWord("points" "number")) {
		silent_cerr("StructMappingExtForce(" << uLabel << "): "
			"\"points number\" keyword expected "
			"at line " << HP.GetLineData() << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	int nPoints = HP.GetInt();
	if (nPoints <= 0) {
		silent_cerr("StructMappingExtForce(" << uLabel << "): illegal points number " << nPoints <<
			" at line " << HP.GetLineData() << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	std::vector<StructNode *> Nodes(nPoints);
	std::vector<Vec3> Offsets(nPoints);
	std::vector<uint32_t> Labels;
	if (bLabels) {
		Labels.resize(nPoints);
	}

	std::map<unsigned, bool> Got;

	for (unsigned n = 0, p = 0; p < unsigned(nPoints); n++) {
		StructNode *pNode = dynamic_cast<StructNode*>(pDM->ReadNode(HP, Node::STRUCTURAL));
		unsigned uL(pNode->GetLabel());
		if (Got[uL]) {
			silent_cerr("StructMappingExtForce(" << uLabel << "): "
				"StructNode(" << uL << ") out of order "
				"at line " << HP.GetLineData() << std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
		Got[uL] = true;

		ReferenceFrame RF(pNode);
		while (HP.IsKeyWord("offset")) {
			if (p == unsigned(nPoints)) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"points number exceeds expected value " << nPoints
					<< " at line " << HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			if (bLabels) {
				int l = HP.GetInt();
				if (l < 0) {
					silent_cerr("StructMappingExtForce(" << uLabel << "): "
						"invalid (negative) point label " << l
						<< " at line " << HP.GetLineData() << std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
				Labels[p] = l;
			}

			Nodes[p] = pNode;
			Offsets[p] = HP.GetPosRel(RF);
			p++;
		}
	}

	if (HP.IsKeyWord("echo")) {
		const char *s = HP.GetFileName();
		if (s == NULL) {
			silent_cerr("StructMappingExtForce(" << uLabel << "): "
				"unable to parse echo file name "
				"at line " << HP.GetLineData()
				<< std::endl);
			throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}

		std::ofstream out(s);

		out.setf(std::ios::scientific);

		if (HP.IsKeyWord("precision")) {
			int iPrecision = HP.GetInt();
			if (iPrecision <= 0) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"invalid echo precision " << iPrecision
					<< " at line " << HP.GetLineData()
					<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}
			out.precision(iPrecision);
		}

		time_t t = std::time(NULL);
		char *user = std::getenv("USER");
		char *host = std::getenv("HOSTNAME");
		if (user == 0) user = "nobody";
		if (host == 0) host = "unknown";

		out
			<< "# Generated by MBDyn StructMappingExtForce(" << uLabel << ")" << std::endl
			<< "# " << user << "@" << host << std::endl
			<< "# " << std::ctime(&t)
			<< "# labels: " << (bLabels ? "on" : "off") << std::endl;
		if (pRefNode) {
			out << "# reference: " << pRefNode->GetLabel() << std::endl;
		}
		out
			<< "# points: " << nPoints << std::endl;

		if (pRefNode) {
			const Vec3& xRef = pRefNode->GetXCurr();
			const Mat3x3& RRef = pRefNode->GetRCurr();

			for (unsigned p = 0; p < unsigned(nPoints); p++) {
				if (bLabels) {
					out << Labels[p] << " ";
				}

				Vec3 x(RRef.MulTV(Nodes[p]->GetXCurr() + Nodes[p]->GetRCurr()*Offsets[p] - xRef));
				out << x << std::endl;
			}

		} else {
			for (unsigned p = 0; p < unsigned(nPoints); p++) {
				if (bLabels) {
					out << Labels[p] << " ";
				}

				Vec3 x(Nodes[p]->GetXCurr() + Nodes[p]->GetRCurr()*Offsets[p]);
				out << x << std::endl;
			}
		}

		if (HP.IsKeyWord("stop")) {
			throw NoErr(MBDYN_EXCEPT_ARGS);
		}
	}

	if (!HP.IsKeyWord("mapped" "points" "number")) {
		silent_cerr("StructMappingExtForce(" << uLabel << "): "
			"\"mapped points number\" keyword expected "
			"at line " << HP.GetLineData() << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	int nMappedPoints = HP.GetInt();
	if (nMappedPoints <= 0) {
		silent_cerr("StructMappingExtForce(" << uLabel << "): "
			"invalid mapped points number " << nMappedPoints
			<< " at line " << HP.GetLineData() << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	SpMapMatrixHandler *pH = ReadSparseMappingMatrix(HP, 3*nMappedPoints, 3*nPoints);

	std::vector<uint32_t> MappedLabels;
	if (bLabels) {
		MappedLabels.resize(nMappedPoints);
		if (HP.IsKeyWord("mapped" "labels" "file")) {
			const char *sFileName = HP.GetFileName();
			if (sFileName == 0) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"unable to read mapped labels file name "
					"at line " << HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			std::ifstream in(sFileName);
			if (!in) {
				silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"unable to open mapped labels file "
					"\"" << sFileName << "\" "
					"at line " << HP.GetLineData() << std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
			}

			char c = in.get();
			while (c == '#') {
				do {
					c = in.get();
				} while (c != '\n');
				c = in.get();
			}
			in.putback(c);

			for (unsigned l = 0; l < unsigned(nMappedPoints); l++) {
				int i;
				in >> i;
				if (!in) {
					silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"unable to read mapped label #" << l << "/" << nMappedPoints
						<< " from mapped labels file \"" << sFileName << "\"" << std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
				if (i < 0) {
					silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"invalid (negative) mapped label #" << l << "/" << nMappedPoints
						<< " from mapped labels file \"" << sFileName << "\"" << std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
				MappedLabels[l] = i;
			}

		} else {
			for (unsigned l = 0; l < unsigned(nMappedPoints); l++) {
				int i = HP.GetInt();
				if (i < 0) {
					silent_cerr("StructMappingExtForce(" << uLabel << "): "
						"invalid (negative) mapped label #" << l << "/" << nMappedPoints
						<< std::endl);
					throw ErrGeneric(MBDYN_EXCEPT_ARGS);
				}
				MappedLabels[l] = i;
			}
		}

		int duplicate = 0;
		for (unsigned l = 1; l < unsigned(nMappedPoints); l++) {
			for (unsigned c = 0; c < l; c++) {
				if (MappedLabels[l] == MappedLabels[c]) {
					duplicate++;
					silent_cerr("StructMappingExtForce(" << uLabel << "): "
					"duplicate mapped label " << MappedLabels[l] << ": #" << l << "==#" << c
						<< std::endl);
				}
			}
		}

		if (duplicate) {
			silent_cerr("StructMappingExtForce(" << uLabel << "): "
				<< duplicate << " duplicate mapped labels"
				<< std::endl);
				throw ErrGeneric(MBDYN_EXCEPT_ARGS);
		}
	}

	flag fOut = pDM->fReadOutput(HP, Elem::FORCE);
	Elem *pEl = 0;
	SAFENEWWITHCONSTRUCTOR(pEl, StructMappingExtForce,
		StructMappingExtForce(uLabel, pDM, pRefNode,
			bUseReferenceNodeForces, bRotateReferenceNodeForces,
			Nodes, Offsets, Labels,
			pH,
			MappedLabels,
			bLabels, bOutputAccelerations, uRRot,
			pEFH, bSendAfterPredict, iCoupling, fOut));

	return pEl;
}

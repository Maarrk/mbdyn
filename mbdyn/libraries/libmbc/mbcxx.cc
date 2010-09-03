/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2010
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

#ifdef USE_SOCKET

#include <stdlib.h>
#include "mbcxx.h"

MBCBase::Rot
MBCBase::GetRot(void) const
{
	return MBCBase::Rot(MBC_F_ROT(GetRigidPtr()));
}

bool
MBCBase::bRefNode(void) const
{
	return MBC_F_REF_NODE(GetRigidPtr());
}

MBCBase::Rot
MBCBase::GetRefNodeRot(void) const {
	return MBCBase::Rot(MBC_F_REF_NODE_ROT(GetRigidPtr()));
}

bool
MBCBase::bAccelerations(void) const
{
	return MBC_F_ACCELS(GetRigidPtr());
}

bool
MBCBase::bLabels(void) const
{
	return MBC_F_LABELS(GetRigidPtr());
}

void
MBCBase::SetTimeout(int t)
{
	GetBasePtr()->timeout = t;
}

void
MBCBase::SetVerbose(bool bv)
{
	GetBasePtr()->verbose = bv;
}

void
MBCBase::SetDataAndNext(bool bd)
{
	GetBasePtr()->data_and_next = bd;
}

bool
MBCBase::bVerbose(void) const
{
	return GetBasePtr()->verbose;
}

bool
MBCBase::bDataAndNext(void) const
{
	return GetBasePtr()->data_and_next;
}

MBCBase::Status
MBCBase::GetStatus(void) const
{
	return m_status;
}

void
MBCBase::SetStatus(MBCBase::Status s)
{
	m_status = s;
}

MBCBase::MBCBase(void)
: m_status(NOT_READY)
{
}

MBCBase::~MBCBase(void)
{
}

int
MBCBase::Init(const char *const path)
{
	if (m_status != NOT_READY) return -1;
	int rc = mbc_unix_init(GetBasePtr(), path);
	if (rc == 0) m_status = READY;
	return rc;
}

int
MBCBase::Init(const char *const host, short unsigned port)
{
	if (m_status != NOT_READY) return -1;
	int rc = mbc_inet_init(GetBasePtr(), host, port);
	if (rc == 0) m_status = READY;
	return rc;
}

uint32_t
MBCBase::GetRefNodeKinematicsLabel(void) const
{
	return MBC_R_K_LABEL(GetRigidPtr());
}

uint32_t
MBCBase::KinematicsLabel(void) const
{
	return MBC_R_K_LABEL(GetRigidPtr());
}

const double *const
MBCBase::GetRefNodeX(void) const
{
	return MBC_R_X(GetRigidPtr());
}

const double *const
MBCBase::GetRefNodeR(void) const
{
	return MBC_R_R(GetRigidPtr());
}

const double *const
MBCBase::GetRefNodeTheta(void) const
{
	return MBC_R_THETA(GetRigidPtr());
}

const double *const
MBCBase::GetRefNodeEuler123(void) const
{
	return MBC_R_EULER_123(GetRigidPtr());
}

const double *const
MBCBase::GetRefNodeXP(void) const
{
	return MBC_R_XP(GetRigidPtr());
}

const double *const
MBCBase::GetRefNodeOmega(void) const
{
	return MBC_R_OMEGA(GetRigidPtr());
}

const double *const
MBCBase::GetRefNodeXPP(void) const
{
	return MBC_R_XPP(GetRigidPtr());
}

const double *const
MBCBase::GetRefNodeOmegaP(void) const
{
	return MBC_R_OMEGAP(GetRigidPtr());
}

const double&
MBCBase::X(uint8_t idx) const
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_X(GetRigidPtr()))[idx - 1];
}

const double&
MBCBase::R(uint8_t ir, uint8_t ic) const
{
	if (ir < 1 || ir > 3 || ic < 1 || ic > 3) throw;
	return (MBC_R_R(GetRigidPtr()))[3*(ic - 1) + ir - 1];
}

const double&
MBCBase::Theta(uint8_t idx) const
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_THETA(GetRigidPtr()))[idx - 1];
}

const double&
MBCBase::Euler123(uint8_t idx) const
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_EULER_123(GetRigidPtr()))[idx - 1];
}

const double&
MBCBase::XP(uint8_t idx) const
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_XP(GetRigidPtr()))[idx - 1];
}

const double&
MBCBase::Omega(uint8_t idx) const
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_OMEGA(GetRigidPtr()))[idx - 1];
}

const double&
MBCBase::XPP(uint8_t idx) const
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_XPP(GetRigidPtr()))[idx - 1];
}

const double&
MBCBase::OmegaP(uint8_t idx) const
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_OMEGAP(GetRigidPtr()))[idx - 1];
}

uint32_t
MBCBase::GetRefNodeDynamicsLabel(void) const
{
	return MBC_R_D_LABEL(GetRigidPtr());
}

const uint32_t&
MBCBase::DynamicsLabel(void) const
{
	return MBC_R_D_LABEL(GetRigidPtr());
}

uint32_t&
MBCBase::DynamicsLabel(void)
{
	return MBC_R_D_LABEL(GetRigidPtr());
}

const double *
MBCBase::GetRefNodeF(void) const
{
	return MBC_R_F(GetRigidPtr());
}

const double *
MBCBase::GetRefNodeM(void) const
{
	return MBC_R_M(GetRigidPtr());
}

const double&
MBCBase::F(uint8_t idx) const
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_F(GetRigidPtr()))[idx - 1];
}

double&
MBCBase::F(uint8_t idx)
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_F(GetRigidPtr()))[idx - 1];
}

const double&
MBCBase::M(uint8_t idx) const
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_M(GetRigidPtr()))[idx - 1];
}

double&
MBCBase::M(uint8_t idx)
{
	if (idx < 1 || idx > 3) throw;
	return (MBC_R_M(GetRigidPtr()))[idx - 1];
}

mbc_t *
MBCNodal::GetBasePtr(void) const
{
	return (mbc_t*)&mbc;
}

mbc_rigid_stub_t *
MBCNodal::GetRigidPtr(void) const
{
	return (mbc_rigid_stub_t *)&mbc;
}

MBCNodal::MBCNodal(MBCBase::Rot rigid, unsigned nodes,
       	bool labels, MBCBase::Rot rot, bool accels)
{
	if (mbc_nodal_init(&mbc, rigid, nodes, labels,
		unsigned(rot) | MBC_U_ROT_REF_NODE(unsigned(rigid)), accels))
	{
		throw;
	}
}

MBCNodal::~MBCNodal(void)
{
	Close();
}

MBCBase::Type
MBCNodal::GetType(void) const
{
	return NODAL;
}

int
MBCNodal::Negotiate(void) const
{
	if (GetStatus() != READY) return -1;
	return mbc_nodal_negotiate_request(&mbc);
}

int
MBCNodal::PutForces(bool bConverged) const
{
	if (GetStatus() != READY) return -1;
	return mbc_nodal_put_forces(&mbc, bConverged);
}

int
MBCNodal::GetMotion(void) const
{
	if (GetStatus() != READY) return -1;
	return mbc_nodal_get_motion(&mbc);
}

int
MBCNodal::Close(void) const
{
	int rc = -1;
	if (GetStatus() == READY) {
		rc = mbc_nodal_destroy(&mbc);
		const_cast<MBCNodal *>(this)->SetStatus(CLOSED);
	}
	return rc;
}

uint32_t
MBCNodal::KinematicsLabel(void) const
{
	return MBCBase::KinematicsLabel();
}

const double&
MBCNodal::X(uint8_t idx) const
{
	return MBCBase::X(idx);
}

const double&
MBCNodal::R(uint8_t ir, uint8_t ic) const
{
	return MBCBase::R(ir, ic);
}

const double&
MBCNodal::Theta(uint8_t idx) const
{
	return MBCBase::Theta(idx);
}

const double&
MBCNodal::Euler123(uint8_t idx) const
{
	return MBCBase::Euler123(idx);
}

const double&
MBCNodal::XP(uint8_t idx) const
{
	return MBCBase::XP(idx);
}

const double&
MBCNodal::Omega(uint8_t idx) const
{
	return MBCBase::Omega(idx);
}

const double&
MBCNodal::XPP(uint8_t idx) const
{
	return MBCBase::XPP(idx);
}

const double&
MBCNodal::OmegaP(uint8_t idx) const
{
	return MBCBase::OmegaP(idx);
}

const uint32_t&
MBCNodal::DynamicsLabel(void) const
{
	return MBCBase::DynamicsLabel();
}

uint32_t&
MBCNodal::DynamicsLabel(void)
{
	return MBCBase::DynamicsLabel();
}

const double&
MBCNodal::F(uint8_t idx) const
{
	return MBCBase::F(idx);
}

double&
MBCNodal::F(uint8_t idx)
{
	return MBCBase::F(idx);
}

const double&
MBCNodal::M(uint8_t idx) const
{
	return MBCBase::M(idx);
}

double&
MBCNodal::M(uint8_t idx)
{
	return MBCBase::M(idx);
}

uint32_t
MBCNodal::GetNodes(void) const
{
	return mbc.nodes;
}

uint32_t *
MBCNodal::GetKinematicsLabel(void) const
{
	return MBC_N_K_LABELS(&mbc);
}

const double *const
MBCNodal::GetX(void) const
{
	return MBC_N_X(&mbc);
}

const double *const
MBCNodal::GetR(void) const
{
	return MBC_N_R(&mbc);
}

const double *const
MBCNodal::GetTheta(void) const
{
	return MBC_N_THETA(&mbc);
}

const double *const
MBCNodal::GetEuler123(void) const
{
	return MBC_N_EULER_123(&mbc);
}

const double *const
MBCNodal::GetXP(void) const
{
	return MBC_N_XP(&mbc);
}

const double *const
MBCNodal::GetOmega(void) const
{
	return MBC_N_OMEGA(&mbc);
}

const double *const
MBCNodal::GetXPP(void) const
{
	return MBC_N_XPP(&mbc);
}

const double *const
MBCNodal::GetOmegaP(void) const
{
	return MBC_N_OMEGAP(&mbc);
}

const double&
MBCNodal::X(uint32_t n, uint8_t idx) const
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_X(&mbc))[3*(n - 1) + idx - 1];
}

const double&
MBCNodal::R(uint32_t n, uint8_t ir, uint8_t ic) const
{
	if (ir < 1 || ir > 3 || ic < 1 || ic > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_R(&mbc))[9*(n - 1) + 3*(ic - 1) + ir - 1];
}

const double&
MBCNodal::Theta(uint32_t n, uint8_t idx) const
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_THETA(&mbc))[3*(n - 1) + idx - 1];
}

const double&
MBCNodal::Euler123(uint32_t n, uint8_t idx) const
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_EULER_123(&mbc))[3*(n - 1) + idx - 1];
}

const double&
MBCNodal::XP(uint32_t n, uint8_t idx) const
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_XP(&mbc))[3*(n - 1) + idx - 1];
}

const double&
MBCNodal::Omega(uint32_t n, uint8_t idx) const
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_OMEGA(&mbc))[3*(n - 1) + idx - 1];
}

const double&
MBCNodal::XPP(uint32_t n, uint8_t idx) const
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_XPP(&mbc))[3*(n - 1) + idx - 1];
}

const double&
MBCNodal::OmegaP(uint32_t n, uint8_t idx) const
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_OMEGAP(&mbc))[3*(n - 1) + idx - 1];
}

uint32_t
MBCNodal::GetKinematicsLabel(uint32_t n) const
{
	if (n < 1 || n > GetNodes()) throw;
	return (MBC_N_K_LABELS(&mbc))[n - 1];
}

uint32_t
MBCNodal::KinematicsLabel(uint32_t n) const
{
	if (n < 1 || n > GetNodes()) throw;
	return (MBC_N_K_LABELS(&mbc))[n - 1];
}

const double *const
MBCNodal::GetX(uint32_t n) const
{
	return &(MBC_N_X(&mbc)[3*(n - 1)]);
}

const double *const
MBCNodal::GetR(uint32_t n) const
{
	return &(MBC_N_R(&mbc)[9*(n - 1)]);
}

const double *const
MBCNodal::GetTheta(uint32_t n) const
{
	return &(MBC_N_THETA(&mbc)[3*(n - 1)]);
}

const double *const
MBCNodal::GetEuler123(uint32_t n) const
{
	return &(MBC_N_EULER_123(&mbc)[3*(n - 1)]);
}

const double *const
MBCNodal::GetXP(uint32_t n) const
{
	return &(MBC_N_XP(&mbc)[3*(n - 1)]);
}

const double *const
MBCNodal::GetOmega(uint32_t n) const
{
	return &(MBC_N_OMEGA(&mbc)[3*(n - 1)]);
}

const double *const
MBCNodal::GetXPP(uint32_t n) const
{
	return &(MBC_N_XPP(&mbc)[3*(n - 1)]);
}

const double *const
MBCNodal::GetOmegaP(uint32_t n) const
{
	return &(MBC_N_OMEGAP(&mbc)[3*(n - 1)]);
}

uint32_t *
MBCNodal::GetDynamicsLabel(void) const
{
	return MBC_N_D_LABELS(&mbc);
}

const uint32_t&
MBCNodal::DynamicsLabel(uint32_t n) const
{
	if (n < 1 || n > GetNodes()) throw;
	return (MBC_N_D_LABELS(&mbc))[n - 1];
}

uint32_t&
MBCNodal::DynamicsLabel(uint32_t n)
{
	if (n < 1 || n > GetNodes()) throw;
	return (MBC_N_D_LABELS(&mbc))[n - 1];
}

const double *
MBCNodal::GetF(void) const
{
	return MBC_N_F(&mbc);
}

const double *
MBCNodal::GetM(void) const
{
	return MBC_N_M(&mbc);
}

const double *
MBCNodal::GetF(uint32_t n) const
{
	if (n < 1 || n > GetNodes()) throw;
	return &(MBC_N_F(&mbc))[3*(n - 1)];
}

const double *
MBCNodal::GetM(uint32_t n) const
{
	if (n < 1 || n > GetNodes()) throw;
	return &(MBC_N_M(&mbc))[3*(n - 1)];
}

const double&
MBCNodal::F(uint32_t n, uint8_t idx) const
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_F(&mbc))[3*(n - 1) + idx - 1];
}

double&
MBCNodal::F(uint32_t n, uint8_t idx)
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_F(&mbc))[3*(n - 1) + idx - 1];
}

const double&
MBCNodal::M(uint32_t n, uint8_t idx) const
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_M(&mbc))[3*(n - 1) + idx - 1];
}

double&
MBCNodal::M(uint32_t n, uint8_t idx)
{
	if (idx < 1 || idx > 3 || n < 1 || n > GetNodes()) throw;
	return (MBC_N_M(&mbc))[3*(n - 1) + idx - 1];
}

mbc_t *
MBCModal::GetBasePtr(void) const
{
	return (mbc_t*)&mbc;
}

mbc_rigid_stub_t *
MBCModal::GetRigidPtr(void) const
{
	return (mbc_rigid_stub_t *)&mbc;
}

MBCModal::MBCModal(MBCBase::Rot rigid, unsigned modes)
{
	if (mbc_modal_init(&mbc, rigid, modes)) {
		throw;
	}
}

MBCModal::~MBCModal(void)
{
	Close();
}

MBCBase::Type
MBCModal::GetType(void) const
{
	return MODAL;
}

int
MBCModal::Negotiate(void) const
{
	if (GetStatus() != READY) return -1;
	return mbc_modal_negotiate_request(&mbc);
}

int
MBCModal::PutForces(bool bConverged) const
{
	if (GetStatus() != READY) return -1;
	return mbc_modal_put_forces(&mbc, bConverged);
}

int
MBCModal::GetMotion(void) const
{
	if (GetStatus() != READY) return -1;
	return mbc_modal_get_motion(&mbc);
}

int
MBCModal::Close(void) const
{
	int rc = -1;
	if (GetStatus() == READY) {
		rc = mbc_modal_destroy(&mbc);
		const_cast<MBCModal *>(this)->SetStatus(CLOSED);
	}
	return rc;
}

uint32_t
MBCModal::KinematicsLabel(void) const
{
	return MBCBase::KinematicsLabel();
}

const double&
MBCModal::X(uint8_t idx) const
{
	return MBCBase::X(idx);
}

const double&
MBCModal::R(uint8_t ir, uint8_t ic) const
{
	return MBCBase::R(ir, ic);
}

const double&
MBCModal::Theta(uint8_t idx) const
{
	return MBCBase::Theta(idx);
}

const double&
MBCModal::Euler123(uint8_t idx) const
{
	return MBCBase::Euler123(idx);
}

const double&
MBCModal::XP(uint8_t idx) const
{
	return MBCBase::XP(idx);
}

const double&
MBCModal::Omega(uint8_t idx) const
{
	return MBCBase::Omega(idx);
}

const double&
MBCModal::XPP(uint8_t idx) const
{
	return MBCBase::XPP(idx);
}

const double&
MBCModal::OmegaP(uint8_t idx) const
{
	return MBCBase::OmegaP(idx);
}

const uint32_t&
MBCModal::DynamicsLabel(void) const
{
	return MBCBase::DynamicsLabel();
}

uint32_t&
MBCModal::DynamicsLabel(void)
{
	return MBCBase::DynamicsLabel();
}

const double&
MBCModal::F(uint8_t idx) const
{
	return MBCBase::F(idx);
}

double&
MBCModal::F(uint8_t idx)
{
	return MBCBase::F(idx);
}

const double&
MBCModal::M(uint8_t idx) const
{
	return MBCBase::M(idx);
}

double&
MBCModal::M(uint8_t idx)
{
	return MBCBase::M(idx);
}

uint32_t
MBCModal::GetModes(void) const
{
	return mbc.modes;
}

const double *const
MBCModal::GetQ(void) const
{
	return MBC_Q(&mbc);
}

const double *const
MBCModal::GetQP(void) const
{
	return MBC_QP(&mbc);
}

const double&
MBCModal::Q(uint32_t m) const
{
	if (m < 1 || m > GetModes()) throw;
	return (MBC_Q(&mbc))[m - 1];
}

const double&
MBCModal::QP(uint32_t m) const
{
	if (m < 1 || m > GetModes()) throw;
	return (MBC_QP(&mbc))[m - 1];
}

const double *
MBCModal::GetP(void) const
{
	return MBC_P(&mbc);
}

const double&
MBCModal::P(uint32_t m) const
{
	if (m < 1 || m > GetModes()) throw;
	return (MBC_P(&mbc))[m - 1];
}

double&
MBCModal::P(uint32_t m)
{
	if (m < 1 || m > GetModes()) throw;
	return (MBC_P(&mbc))[m - 1];
}

#endif // USE_SOCKET

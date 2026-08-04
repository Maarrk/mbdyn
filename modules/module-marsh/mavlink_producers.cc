/* $Header$ */
/*
 * MBDyn (C) is a multibody analysis code.
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License).
 */

#include "mbconfig.h"

#include <cstring>
#include <map>

#include "mavlink_producers.h"

/* NodeStateProducer - begin */

NodeStateProducer::NodeStateProducer(DataManager *pDM, MBDynParser& HP)
: m_pNode(0),
m_tilde_f(::Zero3),
m_tilde_Rh(::Eye3)
{
	if (!HP.IsKeyWord("node")) {
		silent_cerr("module-marsh: \"node\" expected at line "
			<< HP.GetLineData() << std::endl);
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	m_pNode = pDM->ReadNode<const StructNode, Node::STRUCTURAL>(HP);
	if (!m_pNode->bComputeAccelerations()) {
		const_cast<StructNode *>(m_pNode)->ComputeAccelerations(true);
	}

	ReferenceFrame RF(m_pNode);
	if (HP.IsKeyWord("position")) {
		m_tilde_f = HP.GetPosRel(RF);
	}

	if (HP.IsKeyWord("orientation")) {
		m_tilde_Rh = HP.GetRotRel(RF);
	}
}

Mat3x3
NodeStateProducer::GetR(void) const
{
	return m_pNode->GetRCurr()*m_tilde_Rh;
}

Vec3
NodeStateProducer::GetBodyRate(void) const
{
	return GetR().MulTV(m_pNode->GetWCurr());
}

Vec3
NodeStateProducer::GetSpecificForce(const Vec3& gravity) const
{
	/* MAVLink SIM_STATE/MOTION_CUE_EXTRA accelerations are specific
	 * force (what an accelerometer reads): coordinate acceleration
	 * minus gravity, in body axes. A stationary body under gravity
	 * alone reports [0, 0, -9.80665] (NED-like z-down convention),
	 * matching the mps-adapter reference node's consumption-side
	 * convention. */
	return GetR().MulTV(m_pNode->GetXPPCurr() - gravity);
}

/* NodeStateProducer - end */

/* SimStateProducer - begin */

SimStateProducer::SimStateProducer(DataManager *pDM, MBDynParser& HP)
: NodeStateProducer(pDM, HP)
{
	NO_OP;
}

void
SimStateProducer::Pack(mavlink_message_t& msg, uint32_t /* time_boot_ms */,
	uint8_t sysid, uint8_t compid, const Vec3& gravity) const
{
	doublereal e0;
	Vec3 e;
	MatR2EulerParams(GetR(), e0, e);

	Vec3 rate(GetBodyRate());
	Vec3 accel(GetSpecificForce(gravity));

	mavlink_msg_sim_state_pack(sysid, compid, &msg,
		e0, e(1), e(2), e(3),
		0., 0., 0.,			/* roll/pitch/yaw: prefer quaternion */
		accel(1), accel(2), accel(3),
		rate(1), rate(2), rate(3),
		0., 0., 0.,			/* lat/lon/alt: not tracked */
		0., 0.,				/* std_dev_horz/vert */
		0., 0., 0.,			/* vn/ve/vd */
		0, 0);				/* lat_int/lon_int */
}

/* SimStateProducer - end */

/* MotionCueExtraProducer - begin */

MotionCueExtraProducer::MotionCueExtraProducer(DataManager *pDM, MBDynParser& HP)
: NodeStateProducer(pDM, HP)
{
	NO_OP;
}

void
MotionCueExtraProducer::Pack(mavlink_message_t& msg, uint32_t time_boot_ms,
	uint8_t sysid, uint8_t compid, const Vec3& gravity) const
{
	Vec3 rate(GetBodyRate());
	Vec3 accel(GetSpecificForce(gravity));

	mavlink_msg_motion_cue_extra_pack(sysid, compid, &msg,
		time_boot_ms,
		rate(1), rate(2), rate(3),
		accel(1), accel(2), accel(3));
}

/* MotionCueExtraProducer - end */

/* registry - begin */

typedef MavlinkProducer *(*ProducerFactory)(DataManager *, MBDynParser&);

template <class T>
static MavlinkProducer *
ReadProducer(DataManager *pDM, MBDynParser& HP)
{
	return new T(pDM, HP);
}

static const std::map<std::string, ProducerFactory> ProducerRegistry = {
	{ "SIM_STATE", &ReadProducer<SimStateProducer> },
	{ "MOTION_CUE_EXTRA", &ReadProducer<MotionCueExtraProducer> },
};

MavlinkProducer *
ReadMarshProducer(const std::string& name, DataManager *pDM, MBDynParser& HP)
{
	std::map<std::string, ProducerFactory>::const_iterator i = ProducerRegistry.find(name);
	if (i == ProducerRegistry.end()) {
		silent_cerr("module-marsh: unknown outbound message \"" << name
			<< "\" in \"send\" clause at line " << HP.GetLineData()
			<< std::endl);
		return 0;
	}

	return (*i->second)(pDM, HP);
}

/* registry - end */

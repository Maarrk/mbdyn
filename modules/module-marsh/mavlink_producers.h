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

#ifndef MODULE_MARSH_PRODUCERS_H
#define MODULE_MARSH_PRODUCERS_H

#include "mavlink_pubsub.h"

/*
 * Common binding for producers that read a single StructNode's motion:
 * node position/orientation offset, same pattern as modules/module-imu.
 * The orientation offset also serves as the MBDyn-axes -> MAVLink
 * body-axes (x-forward/y-right/z-down) frame conversion.
 */
class NodeStateProducer : public MavlinkProducer {
protected:
	const StructNode *m_pNode;
	Vec3 m_tilde_f;
	Mat3x3 m_tilde_Rh;

	Mat3x3 GetR(void) const;
	Vec3 GetBodyRate(void) const;
	/* specific force (accel including gravity, MAVLink SIM_STATE
	 * convention), body axes */
	Vec3 GetSpecificForce(const Vec3& gravity) const;

public:
	NodeStateProducer(DataManager *pDM, MBDynParser& HP);
	virtual ~NodeStateProducer(void) { };

	const StructNode *pGetNode(void) const override {
		return m_pNode;
	};
};

class SimStateProducer : public NodeStateProducer {
public:
	SimStateProducer(DataManager *pDM, MBDynParser& HP);
	void Pack(mavlink_message_t& msg, uint32_t time_boot_ms,
		uint8_t sysid, uint8_t compid, const Vec3& gravity) const override;
};

class MotionCueExtraProducer : public NodeStateProducer {
public:
	MotionCueExtraProducer(DataManager *pDM, MBDynParser& HP);
	void Pack(mavlink_message_t& msg, uint32_t time_boot_ms,
		uint8_t sysid, uint8_t compid, const Vec3& gravity) const override;
};

#endif /* MODULE_MARSH_PRODUCERS_H */

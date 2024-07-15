/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 Cadence Design Systems Inc.
 */

#ifndef __PHY_DP_H_
#define __PHY_DP_H_

#include <linux/types.h>

#define PHY_SUBMODE_DP	0
#define PHY_SUBMODE_EDP	1

/**
 * struct phy_configure_opts_dp - DisplayPort PHY configuration set
 *
 * This structure is used to represent the configuration state of a
 * DisplayPort phy.
 */
struct phy_configure_opts_dp {
	/**
	 * @link_rate:
	 *
	 * Link Rate, in Mb/s, of the main link.
	 *
	 * Allowed values: 1620, 2160, 2430, 2700, 3240, 4320, 5400, 8100 Mb/s
	 */
	unsigned int link_rate;

	/**
	 * @lanes:
	 *
	 * Number of active, consecutive, data lanes, starting from
	 * lane 0, used for the transmissions on main link.
	 *
	 * Allowed values: 1, 2, 4
	 */
	unsigned int lanes;

	/**
	 * @voltage:
	 *
	 * Voltage swing levels, as specified by DisplayPort specification,
	 * to be used by particular lanes. One value per lane.
	 * voltage[0] is for lane 0, voltage[1] is for lane 1, etc.
	 *
	 * Maximum value: 3
	 */
	unsigned int voltage[4];

	/**
	 * @pre:
	 *
	 * Pre-emphasis levels, as specified by DisplayPort specification, to be
	 * used by particular lanes. One value per lane.
	 *
	 * Maximum value: 3
	 */
	unsigned int pre[4];

	/**
	 * @ssc:
	 *
	 * Flag indicating, whether or not to enable spread-spectrum clocking.
	 *
	 */
	u8 ssc : 1;

	/**
	 * @set_rate:
	 *
	 * Flag indicating, whether or not reconfigure link rate and SSC to
	 * requested values.
	 *
	 */
	u8 set_rate : 1;

	/**
	 * @set_lanes:
	 *
	 * Flag indicating, whether or not reconfigure lane count to
	 * requested value.
	 *
	 */
	u8 set_lanes : 1;

	/**
	 * @set_voltages:
	 *
	 * Flag indicating, whether or not reconfigure voltage swing
	 * and pre-emphasis to requested values. Only lanes specified
	 * by "lanes" parameter will be affected.
	 *
	 */
	u8 set_voltages : 1;
};

#endif /* __PHY_DP_H_ */

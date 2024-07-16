/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020,2022 NXP
 */

#ifndef __PHY_LVDS_H_
#define __PHY_LVDS_H_

/**
 * struct phy_configure_opts_lvds - LVDS configuration set
 * @bits_per_lane_and_dclk_cycle:	Number of bits per lane per differential
 *					clock cycle.
 * @differential_clk_rate:		Clock rate, in Hertz, of the LVDS
 *					differential clock.
 * @lanes:				Number of active, consecutive,
 *					data lanes, starting from lane 0,
 *					used for the transmissions.
 * @is_slave:				Boolean, true if the phy is a slave
 *					which works together with a master
 *					phy to support dual link transmission,
 *					otherwise a regular phy or a master phy.
 *
 * This structure is used to represent the configuration state of a LVDS phy.
 */
struct phy_configure_opts_lvds {
	unsigned int	bits_per_lane_and_dclk_cycle;
	unsigned long	differential_clk_rate;
	unsigned int	lanes;
	bool		is_slave;
};

#endif /* __PHY_LVDS_H_ */

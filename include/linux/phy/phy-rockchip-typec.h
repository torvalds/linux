/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Lin Huang <hl@rock-chips.com>
 */

#ifndef __PHY_ROCKCHIP_TYPEC_H
#define __PHY_ROCKCHIP_TYPEC_H

#if IS_ENABLED(CONFIG_PHY_ROCKCHIP_TYPEC)
int tcphy_dp_set_phy_config(struct phy *phy, int link_rate, int lanes,
			    u8 swing, u8 pre_emp);
int tcphy_dp_set_lane_count(struct phy *phy, u8 lane_count);
int tcphy_dp_set_link_rate(struct phy *phy, int link_rate, bool ssc_on);
#else
static inline int tcphy_dp_set_phy_config(struct phy *phy, int link_rate,
					  int lanes, u8 swing, u8 pre_emp)
{
	return -ENODEV;
}

static inline int tcphy_dp_set_lane_count(struct phy *phy, u8 lane_count)
{
	return -ENODEV;
}

static inline int tcphy_dp_set_link_rate(struct phy *phy, int link_rate,
					 bool ssc_on)
{
	return -ENODEV;
}
#endif

#endif

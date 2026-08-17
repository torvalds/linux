/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright 2023-2026 NXP */

#ifndef __PHY_FSL_LYNX_H_
#define __PHY_FSL_LYNX_H_

enum lynx_lane_mode {
	LANE_MODE_UNKNOWN,
	LANE_MODE_1000BASEX_SGMII,
	LANE_MODE_2500BASEX,
	LANE_MODE_QSGMII,
	LANE_MODE_10G_QXGMII,
	LANE_MODE_10GBASER,
	LANE_MODE_USXGMII,
	LANE_MODE_25GBASER,
	LANE_MODE_MAX,
};

static inline bool lynx_lane_mode_uses_gmii_mac(enum lynx_lane_mode mode)
{
	switch (mode) {
	case LANE_MODE_1000BASEX_SGMII:
	case LANE_MODE_2500BASEX:
	case LANE_MODE_QSGMII:
	case LANE_MODE_10G_QXGMII:
		return true;
	default:
		return false;
	}
}

static inline bool lynx_lane_mode_uses_xgmii_mac(enum lynx_lane_mode mode)
{
	switch (mode) {
	case LANE_MODE_10GBASER:
	case LANE_MODE_USXGMII:
		return true;
	default:
		return false;
	}
}

#endif /* __PHY_FSL_LYNX_H_ */

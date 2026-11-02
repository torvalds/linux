/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2022,2024 NXP
 */

#ifndef __PHY_HDMI_H_
#define __PHY_HDMI_H_

#include <linux/types.h>

enum phy_hdmi_mode {
	PHY_HDMI_MODE_TMDS,
	PHY_HDMI_MODE_FRL,
};

/**
 * struct phy_configure_opts_hdmi - HDMI configuration set
 * @bpc: Bits per color channel.
 * @tmds_char_rate: HDMI TMDS Character Rate in Hertz.
 * @frl.rate_per_lane: HDMI FRL Rate per Lane in Gbps.
 * @frl.lanes: HDMI FRL lanes count.
 * @frl.ffe_level: Transmitter Feed Forward Equalizer Level.
 *	Optional, only meaningful when set_ffe_level flag is on.
 * @frl.set_ffe_level: Flag indicating whether or not to reconfigure ffe_level.
 *	All the other struct fields must be ignored when this is used.
 *
 * This structure is used to represent the configuration state of a HDMI phy.
 */
struct phy_configure_opts_hdmi {
	unsigned int bpc;
	union {
		unsigned long long tmds_char_rate;
		struct {
			u8 rate_per_lane;
			u8 lanes;
			u8 ffe_level;
			u8 set_ffe_level : 1;
		} frl;
	};
};

#endif /* __PHY_HDMI_H_ */

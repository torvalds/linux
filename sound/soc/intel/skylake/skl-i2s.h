/*
 *  skl-i2s.h - i2s blob mapping
 *
 *  Copyright (C) 2017 Intel Corp
 *  Author: Subhransu S. Prusty < subhransu.s.prusty@intel.com>
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#ifndef __SOUND_SOC_SKL_I2S_H
#define __SOUND_SOC_SKL_I2S_H

#define SKL_I2S_MAX_TIME_SLOTS		8
#define SKL_MCLK_DIV_CLK_SRC_MASK	GENMASK(17, 16)

#define SKL_MNDSS_DIV_CLK_SRC_MASK	GENMASK(21, 20)
#define SKL_SHIFT(x)			(ffs(x) - 1)
#define SKL_MCLK_DIV_RATIO_MASK		GENMASK(11, 0)

struct skl_i2s_config {
	u32 ssc0;
	u32 ssc1;
	u32 sscto;
	u32 sspsp;
	u32 sstsa;
	u32 ssrsa;
	u32 ssc2;
	u32 sspsp2;
	u32 ssc3;
	u32 ssioc;
} __packed;

struct skl_i2s_config_mclk {
	u32 mdivctrl;
	u32 mdivr;
};

/**
 * struct skl_i2s_config_blob_legacy - Structure defines I2S Gateway
 * configuration legacy blob
 *
 * @gtw_attr:		Gateway attribute for the I2S Gateway
 * @tdm_ts_group:	TDM slot mapping against channels in the Gateway.
 * @i2s_cfg:		I2S HW registers
 * @mclk:		MCLK clock source and divider values
 */
struct skl_i2s_config_blob_legacy {
	u32 gtw_attr;
	u32 tdm_ts_group[SKL_I2S_MAX_TIME_SLOTS];
	struct skl_i2s_config i2s_cfg;
	struct skl_i2s_config_mclk mclk;
};

#endif /* __SOUND_SOC_SKL_I2S_H */

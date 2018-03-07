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

#define is_legacy_blob(x) (x.signature != 0xEE)
#define ext_to_legacy_blob(i2s_config_blob_ext) \
	((struct skl_i2s_config_blob_legacy *) i2s_config_blob_ext)

#define get_clk_src(mclk, mask) \
		((mclk.mdivctrl & mask) >> SKL_SHIFT(mask))
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

struct skl_i2s_config_mclk_ext {
	u32 mdivctrl;
	u32 mdivr_count;
	u32 mdivr[0];
} __packed;

struct skl_i2s_config_blob_signature {
	u32 minor_ver : 8;
	u32 major_ver : 8;
	u32 resvdz : 8;
	u32 signature : 8;
} __packed;

struct skl_i2s_config_blob_header {
	struct skl_i2s_config_blob_signature sig;
	u32 size;
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

struct skl_i2s_config_blob_ext {
	u32 gtw_attr;
	struct skl_i2s_config_blob_header hdr;
	u32 tdm_ts_group[SKL_I2S_MAX_TIME_SLOTS];
	struct skl_i2s_config i2s_cfg;
	struct skl_i2s_config_mclk_ext mclk;
} __packed;
#endif /* __SOUND_SOC_SKL_I2S_H */

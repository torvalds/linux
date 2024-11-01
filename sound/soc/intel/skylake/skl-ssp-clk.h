/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  skl-ssp-clk.h - Skylake ssp clock information and ipc structure
 *
 *  Copyright (C) 2017 Intel Corp
 *  Author: Jaikrishna Nemallapudi <jaikrishnax.nemallapudi@intel.com>
 *  Author: Subhransu S. Prusty <subhransu.s.prusty@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef SOUND_SOC_SKL_SSP_CLK_H
#define SOUND_SOC_SKL_SSP_CLK_H

#define SKL_MAX_SSP		6
/* xtal/cardinal/pll, parent of ssp clocks and mclk */
#define SKL_MAX_CLK_SRC		3
#define SKL_MAX_SSP_CLK_TYPES	3 /* mclk, sclk, sclkfs */

#define SKL_MAX_CLK_CNT		(SKL_MAX_SSP * SKL_MAX_SSP_CLK_TYPES)

/* Max number of configurations supported for each clock */
#define SKL_MAX_CLK_RATES	10

#define SKL_SCLK_OFS		SKL_MAX_SSP
#define SKL_SCLKFS_OFS		(SKL_SCLK_OFS + SKL_MAX_SSP)

enum skl_clk_type {
	SKL_MCLK,
	SKL_SCLK,
	SKL_SCLK_FS,
};

enum skl_clk_src_type {
	SKL_XTAL,
	SKL_CARDINAL,
	SKL_PLL,
};

struct skl_clk_parent_src {
	u8 clk_id;
	const char *name;
	unsigned long rate;
	const char *parent_name;
};

struct skl_tlv_hdr {
	u32 type;
	u32 size;
};

struct skl_dmactrl_mclk_cfg {
	struct skl_tlv_hdr hdr;
	/* DMA Clk TLV params */
	u32 clk_warm_up:16;
	u32 mclk:1;
	u32 warm_up_over:1;
	u32 rsvd0:14;
	u32 clk_stop_delay:16;
	u32 keep_running:1;
	u32 clk_stop_over:1;
	u32 rsvd1:14;
};

struct skl_dmactrl_sclkfs_cfg {
	struct skl_tlv_hdr hdr;
	/* DMA SClk&FS  TLV params */
	u32 sampling_frequency;
	u32 bit_depth;
	u32 channel_map;
	u32 channel_config;
	u32 interleaving_style;
	u32 number_of_channels : 8;
	u32 valid_bit_depth : 8;
	u32 sample_type : 8;
	u32 reserved : 8;
};

union skl_clk_ctrl_ipc {
	struct skl_dmactrl_mclk_cfg mclk;
	struct skl_dmactrl_sclkfs_cfg sclk_fs;
};

struct skl_clk_rate_cfg_table {
	unsigned long rate;
	union skl_clk_ctrl_ipc dma_ctl_ipc;
	void *config;
};

/*
 * rate for mclk will be in rates[0]. For sclk and sclkfs, rates[] store
 * all possible clocks ssp can generate for that platform.
 */
struct skl_ssp_clk {
	const char *name;
	const char *parent_name;
	struct skl_clk_rate_cfg_table rate_cfg[SKL_MAX_CLK_RATES];
};

struct skl_clk_pdata {
	struct skl_clk_parent_src *parent_clks;
	int num_clks;
	struct skl_ssp_clk *ssp_clks;
	void *pvt_data;
};

#endif /* SOUND_SOC_SKL_SSP_CLK_H */

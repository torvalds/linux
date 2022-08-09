/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/sound/cs35l41.h -- Platform data for CS35L41
 *
 * Copyright (c) 2017-2021 Cirrus Logic Inc.
 *
 * Author: David Rhodes	<david.rhodes@cirrus.com>
 */

#ifndef __CS35L41_H
#define __CS35L41_H

enum cs35l41_clk_ids {
	CS35L41_CLKID_SCLK = 0,
	CS35L41_CLKID_LRCLK = 1,
	CS35L41_CLKID_MCLK = 4,
};

struct cs35l41_irq_cfg {
	bool irq_pol_inv;
	bool irq_out_en;
	int irq_src_sel;
};

struct cs35l41_platform_data {
	int bst_ind;
	int bst_ipk;
	int bst_cap;
	int dout_hiz;
	struct cs35l41_irq_cfg irq_config1;
	struct cs35l41_irq_cfg irq_config2;
};

#endif /* __CS35L41_H */

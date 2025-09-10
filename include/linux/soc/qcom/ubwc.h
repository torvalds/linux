/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018, The Linux Foundation
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QCOM_UBWC_H__
#define __QCOM_UBWC_H__

#include <linux/bits.h>
#include <linux/types.h>

struct qcom_ubwc_cfg_data {
	u32 ubwc_enc_version;
	/* Can be read from MDSS_BASE + 0x58 */
	u32 ubwc_dec_version;

	/**
	 * @ubwc_swizzle: Whether to enable level 1, 2 & 3 bank swizzling.
	 *
	 * UBWC 1.0 always enables all three levels.
	 * UBWC 2.0 removes level 1 bank swizzling, leaving levels 2 & 3.
	 * UBWC 4.0 adds the optional ability to disable levels 2 & 3.
	 */
	u32 ubwc_swizzle;
#define UBWC_SWIZZLE_ENABLE_LVL1	BIT(0)
#define UBWC_SWIZZLE_ENABLE_LVL2	BIT(1)
#define UBWC_SWIZZLE_ENABLE_LVL3	BIT(2)

	/**
	 * @highest_bank_bit: Highest Bank Bit
	 *
	 * The Highest Bank Bit value represents the bit of the highest
	 * DDR bank.  This should ideally use DRAM type detection.
	 */
	int highest_bank_bit;
	bool ubwc_bank_spread;

	/**
	 * @macrotile_mode: Macrotile Mode
	 *
	 * Whether to use 4-channel macrotiling mode or the newer
	 * 8-channel macrotiling mode introduced in UBWC 3.1. 0 is
	 * 4-channel and 1 is 8-channel.
	 */
	bool macrotile_mode;
};

#define UBWC_1_0 0x10000000
#define UBWC_2_0 0x20000000
#define UBWC_3_0 0x30000000
#define UBWC_4_0 0x40000000
#define UBWC_4_3 0x40030000
#define UBWC_5_0 0x50000000

#if IS_ENABLED(CONFIG_QCOM_UBWC_CONFIG)
const struct qcom_ubwc_cfg_data *qcom_ubwc_config_get_data(void);
#else
static inline const struct qcom_ubwc_cfg_data *qcom_ubwc_config_get_data(void)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif

static inline bool qcom_ubwc_get_ubwc_mode(const struct qcom_ubwc_cfg_data *cfg)
{
	bool ret = cfg->ubwc_enc_version == UBWC_1_0;

	if (ret && !(cfg->ubwc_swizzle & UBWC_SWIZZLE_ENABLE_LVL1))
		pr_err("UBWC config discrepancy - level 1 swizzling disabled on UBWC 1.0\n");

	return ret;
}

#endif /* __QCOM_UBWC_H__ */

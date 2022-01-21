/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014,2015, Linaro Ltd.
 */

#ifndef __SPM_H__
#define __SPM_H__

#include <linux/cpuidle.h>

#define MAX_PMIC_DATA		2
#define MAX_SEQ_DATA		64

enum pm_sleep_mode {
	PM_SLEEP_MODE_STBY,
	PM_SLEEP_MODE_RET,
	PM_SLEEP_MODE_SPC,
	PM_SLEEP_MODE_PC,
	PM_SLEEP_MODE_NR,
};

struct spm_reg_data {
	const u16 *reg_offset;
	u32 spm_cfg;
	u32 spm_dly;
	u32 pmic_dly;
	u32 pmic_data[MAX_PMIC_DATA];
	u32 avs_ctl;
	u32 avs_limit;
	u8 seq[MAX_SEQ_DATA];
	u8 start_index[PM_SLEEP_MODE_NR];
};

struct spm_driver_data {
	void __iomem *reg_base;
	const struct spm_reg_data *reg_data;
};

void spm_set_low_power_mode(struct spm_driver_data *drv,
			    enum pm_sleep_mode mode);

#endif /* __SPM_H__ */

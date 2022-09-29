/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _QCOM_LLCC_PMU_H
#define _QCOM_LLCC_PMU_H

#include <linux/kernel.h>

#define QCOM_LLCC_PMU_RD_EV	0x1000

#if IS_ENABLED(CONFIG_QCOM_LLCC_PMU)
int qcom_llcc_pmu_hw_type(u32 *type);
#else
static inline int qcom_llcc_pmu_hw_type(u32 *type)
{
	return 0;
}
#endif

#endif /* _QCOM_LLCC_PMU_H */

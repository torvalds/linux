/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_MPM_H__
#define __QCOM_MPM_H__

#include <linux/cpumask.h>

#if IS_ENABLED(CONFIG_MPM_LEGACY)
void msm_mpm_timer_write(uint64_t expiry);
int msm_mpm_enter_sleep(struct cpumask *cpumask);
#else
static inline int msm_mpm_enter_sleep(struct cpumask *cpumask)
{
	return -ENODEV;
}

static inline int msm_mpm_timer_write(uint64_t expiry)
{
}
#endif

#endif				/* __QCOM_MPM_H__ */

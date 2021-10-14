/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _LINUX_QCOM_CPUFREQ_HW_H
#define _LINUX_QCOM_CPUFREQ_HW_H

/*
 * We can take this out if we could move the OSM cycle
 * counter to WALT scheduler?
 */
#if IS_ENABLED(CONFIG_ARM_QCOM_CPUFREQ_HW)
extern u64 qcom_cpufreq_get_cpu_cycle_counter(int cpu);
#else
static inline u64 qcom_cpufreq_get_cpu_cycle_counter(int cpu)
{
	return U64_MAX;
}
#endif /*CONFIG_ARM_QCOM_CPUFREQ_HW*/
#endif /* _LINUX_QCOM_CPUFREQ_HW_H */

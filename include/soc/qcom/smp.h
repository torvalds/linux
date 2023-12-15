/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2010-2015, 2018-2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __QCOM_SMP_H
#define __QCOM_SMP_H

#ifdef CONFIG_SMP

#ifdef CONFIG_QGKI_LPM_IPI_CHECK
DECLARE_PER_CPU(bool, pending_ipi);

static inline bool is_IPI_pending(const struct cpumask *mask)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask) {
		if (per_cpu(pending_ipi, cpu))
			return true;
	}

	return false;
}
#else
static inline bool is_IPI_pending(const struct cpumask *mask)
{
	return false;
}
#endif /* CONFIG_QGKI_LPM_IPI_CHECK */

#else /* !SMP */

static inline bool is_IPI_pending(const struct cpumask *mask)
{
	return false;
}

#endif /* !SMP */

#endif /* __QCOM_SMP_H */

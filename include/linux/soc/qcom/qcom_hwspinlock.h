/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_HWSPINLOCK_H
#define __QCOM_HWSPINLOCK_H

struct hwspinlock;

#if IS_ENABLED(CONFIG_HWSPINLOCK_QCOM)

int qcom_hwspinlock_bust(struct hwspinlock *hwlock, unsigned int id);

#else /* !CONFIG_HWSPINLOCK_QCOM */

static inline int qcom_hwspinlock_bust(struct hwspinlock *hwlock, unsigned int id)
{
	return 0;
}

#endif /* CONFIG_HWSPINLOCK_QCOM */

#endif /* __QCOM_HWSPINLOCK_H */

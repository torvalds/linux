/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2014, 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DEVFREQ_ICC_H
#define _DEVFREQ_ICC_H

#include <linux/devfreq.h>

#ifdef CONFIG_QCOM_DEVFREQ_ICC
int devfreq_add_icc(struct device *dev);
int devfreq_remove_icc(struct device *dev);
int devfreq_suspend_icc(struct device *dev);
int devfreq_resume_icc(struct device *dev);
#else
static inline int devfreq_add_icc(struct device *dev)
{
	return 0;
}
static inline int devfreq_remove_icc(struct device *dev)
{
	return 0;
}
static inline int devfreq_suspend_icc(struct device *dev)
{
	return 0;
}
static inline int devfreq_resume_icc(struct device *dev)
{
	return 0;
}
#endif

#endif /* _DEVFREQ_ICC_H */

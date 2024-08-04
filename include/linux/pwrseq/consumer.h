/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#ifndef __POWER_SEQUENCING_CONSUMER_H__
#define __POWER_SEQUENCING_CONSUMER_H__

#include <linux/err.h>

struct device;
struct pwrseq_desc;

#if IS_ENABLED(CONFIG_POWER_SEQUENCING)

struct pwrseq_desc * __must_check
pwrseq_get(struct device *dev, const char *target);
void pwrseq_put(struct pwrseq_desc *desc);

struct pwrseq_desc * __must_check
devm_pwrseq_get(struct device *dev, const char *target);

int pwrseq_power_on(struct pwrseq_desc *desc);
int pwrseq_power_off(struct pwrseq_desc *desc);

#else /* CONFIG_POWER_SEQUENCING */

static inline struct pwrseq_desc * __must_check
pwrseq_get(struct device *dev, const char *target)
{
	return ERR_PTR(-ENOSYS);
}

static inline void pwrseq_put(struct pwrseq_desc *desc)
{
}

static inline struct pwrseq_desc * __must_check
devm_pwrseq_get(struct device *dev, const char *target)
{
	return ERR_PTR(-ENOSYS);
}

static inline int pwrseq_power_on(struct pwrseq_desc *desc)
{
	return -ENOSYS;
}

static inline int pwrseq_power_off(struct pwrseq_desc *desc)
{
	return -ENOSYS;
}

#endif /* CONFIG_POWER_SEQUENCING */

#endif /* __POWER_SEQUENCING_CONSUMER_H__ */

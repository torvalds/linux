/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for device driver Hi6421 PMIC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (C) 2011 Hisilicon.
 * Copyright (c) 2020-2021 Huawei Technologies Co., Ltd
 *
 * Guodong Xu <guodong.xu@linaro.org>
 */

#ifndef	__HISI_PMIC_H
#define	__HISI_PMIC_H

#include <linux/irqdomain.h>
#include <linux/regmap.h>

struct hi6421_spmi_pmic {
	struct resource				*res;
	struct device				*dev;
	void __iomem				*regs;
	spinlock_t				lock;
	struct irq_domain			*domain;
	int					irq;
	int					gpio;
	unsigned int				*irqs;
	struct regmap				*regmap;
};

#endif		/* __HISI_PMIC_H */

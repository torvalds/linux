/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for device driver Hi6421 PMIC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (C) 2011 Hisilicon.
 *
 * Guodong Xu <guodong.xu@linaro.org>
 */

#ifndef	__HISI_PMIC_H
#define	__HISI_PMIC_H

#include <linux/irqdomain.h>

#define HISI_REGS_ENA_PROTECT_TIME	(0)	/* in microseconds */
#define HISI_ECO_MODE_ENABLE		(1)
#define HISI_ECO_MODE_DISABLE		(0)

struct hi6421_spmi_pmic {
	struct resource				*res;
	struct device				*dev;
	void __iomem				*regs;
	spinlock_t				lock;
	struct irq_domain			*domain;
	int					irq;
	int					gpio;
	unsigned int				*irqs;
};

int hi6421_spmi_pmic_read(struct hi6421_spmi_pmic *pmic, int reg);
int hi6421_spmi_pmic_write(struct hi6421_spmi_pmic *pmic, int reg, u32 val);
int hi6421_spmi_pmic_rmw(struct hi6421_spmi_pmic *pmic, int reg,
			 u32 mask, u32 bits);

enum hi6421_spmi_pmic_irq_list {
	OTMP = 0,
	VBUS_CONNECT,
	VBUS_DISCONNECT,
	ALARMON_R,
	HOLD_6S,
	HOLD_1S,
	POWERKEY_UP,
	POWERKEY_DOWN,
	OCP_SCP_R,
	COUL_R,
	SIM0_HPD_R,
	SIM0_HPD_F,
	SIM1_HPD_R,
	SIM1_HPD_F,
	PMIC_IRQ_LIST_MAX,
};
#endif		/* __HISI_PMIC_H */

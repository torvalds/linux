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

typedef int (*pmic_ocp_callback)(char *);
int hisi_pmic_special_ocp_register(char *power_name, pmic_ocp_callback handler);

struct irq_mask_info {
	int start_addr;
	int array;
};

struct irq_info {
	int start_addr;
	int array;
};

struct bit_info {
	int addr;
	int bit;
};

struct write_lock {
	int addr;
	int val;
};

struct hisi_pmic {
	struct resource		*res;
	struct device		*dev;
	void __iomem		*regs;
	spinlock_t		lock;
	struct irq_domain	*domain;
	int			irq;
	int			gpio;
	unsigned int	*irqs;
	int			irqnum;
	int			irqarray;
	struct irq_mask_info irq_mask_addr;
	struct irq_info irq_addr;
	int			irqnum1;
	int			irqarray1;
	struct irq_mask_info irq_mask_addr1;
	struct irq_info irq_addr1;
	struct write_lock normal_lock;
	struct write_lock debug_lock;

	unsigned int g_extinterrupt_flag;
};

u32 hisi_pmic_read(struct hisi_pmic *pmic, int reg);
void hisi_pmic_write(struct hisi_pmic *pmic, int reg, u32 val);
void hisi_pmic_rmw(struct hisi_pmic *pmic, int reg, u32 mask, u32 bits);

enum pmic_irq_list {
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

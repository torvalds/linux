/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * Qualcomm PMIC irq 8xxx driver header file
 *
 */

#ifndef __MFD_PM8XXX_IRQ_H
#define __MFD_PM8XXX_IRQ_H

#include <linux/errno.h>
#include <linux/err.h>

struct pm8xxx_irq_core_data {
	u32		rev;
	int		nirqs;
};

struct pm8xxx_irq_platform_data {
	int				irq_base;
	struct pm8xxx_irq_core_data	irq_cdata;
	int				devirq;
	int				irq_trigger_flag;
};

struct pm_irq_chip;

#ifdef CONFIG_MFD_PM8XXX_IRQ
int pm8xxx_get_irq_stat(struct pm_irq_chip *chip, int irq);
struct pm_irq_chip *pm8xxx_irq_init(struct device *dev,
				const struct pm8xxx_irq_platform_data *pdata);
int pm8xxx_irq_exit(struct pm_irq_chip *chip);
#else
static inline int pm8xxx_get_irq_stat(struct pm_irq_chip *chip, int irq)
{
	return -ENXIO;
}
static inline struct pm_irq_chip *pm8xxx_irq_init(
				const struct device *dev,
				const struct pm8xxx_irq_platform_data *pdata)
{
	return ERR_PTR(-ENXIO);
}
static inline int pm8xxx_irq_exit(struct pm_irq_chip *chip)
{
	return -ENXIO;
}
#endif /* CONFIG_MFD_PM8XXX_IRQ */
#endif /* __MFD_PM8XXX_IRQ_H */

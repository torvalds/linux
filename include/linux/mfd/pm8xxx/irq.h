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

#endif /* __MFD_PM8XXX_IRQ_H */

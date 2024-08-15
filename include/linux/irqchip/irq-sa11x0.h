/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic IRQ handling for the SA11x0.
 *
 * Copyright (C) 2015 Dmitry Eremin-Solenikov
 * Copyright (C) 1999-2001 Nicolas Pitre
 */

#ifndef __INCLUDE_LINUX_IRQCHIP_IRQ_SA11x0_H
#define __INCLUDE_LINUX_IRQCHIP_IRQ_SA11x0_H

void __init sa11x0_init_irq_nodt(int irq_start, resource_size_t io_start);

#endif

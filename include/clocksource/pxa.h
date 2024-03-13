/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PXA clocksource, clockevents, and OST interrupt handlers.
 *
 * Copyright (C) 2014 Robert Jarzmik
 */

#ifndef _CLOCKSOURCE_PXA_H
#define _CLOCKSOURCE_PXA_H

extern void pxa_timer_nodt_init(int irq, void __iomem *base);

#endif

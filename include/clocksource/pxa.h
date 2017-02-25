/*
 * PXA clocksource, clockevents, and OST interrupt handlers.
 *
 * Copyright (C) 2014 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 */

#ifndef _CLOCKSOURCE_PXA_H
#define _CLOCKSOURCE_PXA_H

extern void pxa_timer_nodt_init(int irq, void __iomem *base);

#endif

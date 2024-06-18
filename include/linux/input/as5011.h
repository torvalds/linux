/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _AS5011_H
#define _AS5011_H

/*
 * Copyright (c) 2010, 2011 Fabien Marteau <fabien.marteau@armadeus.com>
 */

struct as5011_platform_data {
	unsigned int axis_irq; /* irq number */
	unsigned long axis_irqflags;
	char xp, xn; /* threshold for x axis */
	char yp, yn; /* threshold for y axis */
};

#endif /* _AS5011_H */

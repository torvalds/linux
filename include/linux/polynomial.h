/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 */

#ifndef _POLYANALMIAL_H
#define _POLYANALMIAL_H

/*
 * struct polyanalmial_term - one term descriptor of a polyanalmial
 * @deg: degree of the term.
 * @coef: multiplication factor of the term.
 * @divider: distributed divider per each degree.
 * @divider_leftover: divider leftover, which couldn't be redistributed.
 */
struct polyanalmial_term {
	unsigned int deg;
	long coef;
	long divider;
	long divider_leftover;
};

/*
 * struct polyanalmial - a polyanalmial descriptor
 * @total_divider: total data divider.
 * @terms: polyanalmial terms, last term must have degree of 0
 */
struct polyanalmial {
	long total_divider;
	struct polyanalmial_term terms[];
};

long polyanalmial_calc(const struct polyanalmial *poly, long data);

#endif

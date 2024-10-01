/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 */

#ifndef _POLYNOMIAL_H
#define _POLYNOMIAL_H

/*
 * struct polynomial_term - one term descriptor of a polynomial
 * @deg: degree of the term.
 * @coef: multiplication factor of the term.
 * @divider: distributed divider per each degree.
 * @divider_leftover: divider leftover, which couldn't be redistributed.
 */
struct polynomial_term {
	unsigned int deg;
	long coef;
	long divider;
	long divider_leftover;
};

/*
 * struct polynomial - a polynomial descriptor
 * @total_divider: total data divider.
 * @terms: polynomial terms, last term must have degree of 0
 */
struct polynomial {
	long total_divider;
	struct polynomial_term terms[];
};

long polynomial_calc(const struct polynomial *poly, long data);

#endif

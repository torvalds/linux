// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic polynomial calculation using integer coefficients.
 *
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Maxim Kaurkin <maxim.kaurkin@baikalelectronics.ru>
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/polynomial.h>

/*
 * Originally this was part of drivers/hwmon/bt1-pvt.c.
 * There the following conversion is used and should serve as an example here:
 *
 * The original translation formulae of the temperature (in degrees of Celsius)
 * to PVT data and vice-versa are following:
 *
 * N = 1.8322e-8*(T^4) + 2.343e-5*(T^3) + 8.7018e-3*(T^2) + 3.9269*(T^1) +
 *     1.7204e2
 * T = -1.6743e-11*(N^4) + 8.1542e-8*(N^3) + -1.8201e-4*(N^2) +
 *     3.1020e-1*(N^1) - 4.838e1
 *
 * where T = [-48.380, 147.438]C and N = [0, 1023].
 *
 * They must be accordingly altered to be suitable for the integer arithmetics.
 * The technique is called 'factor redistribution', which just makes sure the
 * multiplications and divisions are made so to have a result of the operations
 * within the integer numbers limit. In addition we need to translate the
 * formulae to accept millidegrees of Celsius. Here what they look like after
 * the alterations:
 *
 * N = (18322e-20*(T^4) + 2343e-13*(T^3) + 87018e-9*(T^2) + 39269e-3*T +
 *     17204e2) / 1e4
 * T = -16743e-12*(D^4) + 81542e-9*(D^3) - 182010e-6*(D^2) + 310200e-3*D -
 *     48380
 * where T = [-48380, 147438] mC and N = [0, 1023].
 *
 * static const struct polynomial poly_temp_to_N = {
 *         .total_divider = 10000,
 *         .terms = {
 *                 {4, 18322, 10000, 10000},
 *                 {3, 2343, 10000, 10},
 *                 {2, 87018, 10000, 10},
 *                 {1, 39269, 1000, 1},
 *                 {0, 1720400, 1, 1}
 *         }
 * };
 *
 * static const struct polynomial poly_N_to_temp = {
 *         .total_divider = 1,
 *         .terms = {
 *                 {4, -16743, 1000, 1},
 *                 {3, 81542, 1000, 1},
 *                 {2, -182010, 1000, 1},
 *                 {1, 310200, 1000, 1},
 *                 {0, -48380, 1, 1}
 *         }
 * };
 */

/**
 * polynomial_calc - calculate a polynomial using integer arithmetic
 *
 * @poly: pointer to the descriptor of the polynomial
 * @data: input value of the polynimal
 *
 * Calculate the result of a polynomial using only integer arithmetic. For
 * this to work without too much loss of precision the coefficients has to
 * be altered. This is called factor redistribution.
 *
 * Returns the result of the polynomial calculation.
 */
long polynomial_calc(const struct polynomial *poly, long data)
{
	const struct polynomial_term *term = poly->terms;
	long total_divider = poly->total_divider ?: 1;
	long tmp, ret = 0;
	int deg;

	/*
	 * Here is the polynomial calculation function, which performs the
	 * redistributed terms calculations. It's pretty straightforward.
	 * We walk over each degree term up to the free one, and perform
	 * the redistributed multiplication of the term coefficient, its
	 * divider (as for the rationale fraction representation), data
	 * power and the rational fraction divider leftover. Then all of
	 * this is collected in a total sum variable, which value is
	 * normalized by the total divider before being returned.
	 */
	do {
		tmp = term->coef;
		for (deg = 0; deg < term->deg; ++deg)
			tmp = mult_frac(tmp, data, term->divider);
		ret += tmp / term->divider_leftover;
	} while ((term++)->deg);

	return ret / total_divider;
}
EXPORT_SYMBOL_GPL(polynomial_calc);

MODULE_DESCRIPTION("Generic polynomial calculations");
MODULE_LICENSE("GPL");

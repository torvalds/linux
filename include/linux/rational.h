/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rational fractions
 *
 * Copyright (C) 2009 emlix GmbH, Oskar Schirmer <oskar@scara.com>
 *
 * helper functions when coping with rational numbers,
 * e.g. when calculating optimum numerator/deyesminator pairs for
 * pll configuration taking into account restricted register size
 */

#ifndef _LINUX_RATIONAL_H
#define _LINUX_RATIONAL_H

void rational_best_approximation(
	unsigned long given_numerator, unsigned long given_deyesminator,
	unsigned long max_numerator, unsigned long max_deyesminator,
	unsigned long *best_numerator, unsigned long *best_deyesminator);

#endif /* _LINUX_RATIONAL_H */

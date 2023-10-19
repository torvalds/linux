/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tle62x0.h - platform glue to Infineon TLE62x0 driver chips
 *
 * Copyright 2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
*/

struct tle62x0_pdata {
	unsigned int		init_state;
	unsigned int		gpio_count;
};

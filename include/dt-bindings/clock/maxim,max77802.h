/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014 Google, Inc
 *
 * Device Tree binding constants clocks for the Maxim 77802 PMIC.
 */

#ifndef _DT_BINDINGS_CLOCK_MAXIM_MAX77802_CLOCK_H
#define _DT_BINDINGS_CLOCK_MAXIM_MAX77802_CLOCK_H

/* Fixed rate clocks. */

#define MAX77802_CLK_32K_AP		0
#define MAX77802_CLK_32K_CP		1

/* Total number of clocks. */
#define MAX77802_CLKS_NUM		(MAX77802_CLK_32K_CP + 1)

#endif /* _DT_BINDINGS_CLOCK_MAXIM_MAX77802_CLOCK_H */

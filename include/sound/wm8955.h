/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Platform data for WM8955
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef __WM8955_PDATA_H__
#define __WM8955_PDATA_H__

struct wm8955_pdata {
	/* Configure LOUT2/ROUT2 to drive a speaker */
	unsigned int out2_speaker:1;

	/* Configure MONOIN+/- in differential mode */
	unsigned int monoin_diff:1;
};

#endif

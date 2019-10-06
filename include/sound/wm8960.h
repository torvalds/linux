/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * wm8960.h  --  WM8960 Soc Audio driver platform data
 */

#ifndef _WM8960_PDATA_H
#define _WM8960_PDATA_H

#define WM8960_DRES_400R 0
#define WM8960_DRES_200R 1
#define WM8960_DRES_600R 2
#define WM8960_DRES_150R 3
#define WM8960_DRES_MAX  3

struct wm8960_data {
	bool capless;  /* Headphone outputs configured in capless mode */

	bool shared_lrclk;  /* DAC and ADC LRCLKs are wired together */
};

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/wm1250-ev1.h - Platform data for WM1250-EV1
 *
 * Copyright 2011 Wolfson Microelectronics. PLC.
 */

#ifndef __LINUX_SND_WM1250_EV1_H
#define __LINUX_SND_WM1250_EV1_H

#define WM1250_EV1_NUM_GPIOS 5

#define WM1250_EV1_GPIO_CLK_ENA  0
#define WM1250_EV1_GPIO_CLK_SEL0 1
#define WM1250_EV1_GPIO_CLK_SEL1 2
#define WM1250_EV1_GPIO_OSR      3
#define WM1250_EV1_GPIO_MASTER   4


struct wm1250_ev1_pdata {
	int gpios[WM1250_EV1_NUM_GPIOS];
};

#endif

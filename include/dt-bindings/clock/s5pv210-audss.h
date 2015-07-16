/*
 * Copyright (c) 2014 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This header provides constants for Samsung audio subsystem
 * clock controller.
 *
 * The constants defined in this header are being used in dts
 * and s5pv210 audss driver.
 */

#ifndef _DT_BINDINGS_CLOCK_S5PV210_AUDSS_H
#define _DT_BINDINGS_CLOCK_S5PV210_AUDSS_H

#define CLK_MOUT_AUDSS		0
#define CLK_MOUT_I2S_A		1

#define CLK_DOUT_AUD_BUS	2
#define CLK_DOUT_I2S_A		3

#define CLK_I2S			4
#define CLK_HCLK_I2S		5
#define CLK_HCLK_UART		6
#define CLK_HCLK_HWA		7
#define CLK_HCLK_DMA		8
#define CLK_HCLK_BUF		9
#define CLK_HCLK_RP		10

#define AUDSS_MAX_CLKS		11

#endif

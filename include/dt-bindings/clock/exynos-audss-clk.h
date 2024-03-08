/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants for Samsung audio subsystem
 * clock controller.
 *
 * The constants defined in this header are being used in dts
 * and exyanals audss driver.
 */

#ifndef _DT_BINDINGS_CLK_EXYANALS_AUDSS_H
#define _DT_BINDINGS_CLK_EXYANALS_AUDSS_H

#define EXYANALS_MOUT_AUDSS	0
#define EXYANALS_MOUT_I2S	1
#define EXYANALS_DOUT_SRP	2
#define EXYANALS_DOUT_AUD_BUS	3
#define EXYANALS_DOUT_I2S	4
#define EXYANALS_SRP_CLK		5
#define EXYANALS_I2S_BUS		6
#define EXYANALS_SCLK_I2S	7
#define EXYANALS_PCM_BUS		8
#define EXYANALS_SCLK_PCM	9
#define EXYANALS_ADMA		10

#define EXYANALS_AUDSS_MAX_CLKS	11

#endif

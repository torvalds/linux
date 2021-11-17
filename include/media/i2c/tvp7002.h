/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Texas Instruments Triple 8-/10-BIT 165-/110-MSPS Video and Graphics
 * Digitizer with Horizontal PLL registers
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Santiago Nunez-Corrales <santiago.nunez@ridgerun.com>
 *
 * This code is partially based upon the TVP5150 driver
 * written by Mauro Carvalho Chehab <mchehab@kernel.org>,
 * the TVP514x driver written by Vaibhav Hiremath <hvaibhav@ti.com>
 * and the TVP7002 driver in the TI LSP 2.10.00.14
 */
#ifndef _TVP7002_H_
#define _TVP7002_H_

#define TVP7002_MODULE_NAME "tvp7002"

/**
 * struct tvp7002_config - Platform dependent data
 *@clk_polarity: Clock polarity
 *		0 - Data clocked out on rising edge of DATACLK signal
 *		1 - Data clocked out on falling edge of DATACLK signal
 *@hs_polarity:  HSYNC polarity
 *		0 - Active low HSYNC output, 1 - Active high HSYNC output
 *@vs_polarity: VSYNC Polarity
 *		0 - Active low VSYNC output, 1 - Active high VSYNC output
 *@fid_polarity: Active-high Field ID polarity.
 *		0 - The field ID output is set to logic 1 for an odd field
 *		    (field 1) and set to logic 0 for an even field (field 0).
 *		1 - Operation with polarity inverted.
 *@sog_polarity: Active high Sync on Green output polarity.
 *		0 - Normal operation, 1 - Operation with polarity inverted
 */
struct tvp7002_config {
	bool clk_polarity;
	bool hs_polarity;
	bool vs_polarity;
	bool fid_polarity;
	bool sog_polarity;
};
#endif

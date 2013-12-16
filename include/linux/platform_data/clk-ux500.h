/*
 * Clock definitions for ux500 platforms
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __CLK_UX500_H
#define __CLK_UX500_H

void u8500_of_clk_init(u32 clkrst1_base, u32 clkrst2_base, u32 clkrst3_base,
		       u32 clkrst5_base, u32 clkrst6_base);

void u8500_clk_init(u32 clkrst1_base, u32 clkrst2_base, u32 clkrst3_base,
		    u32 clkrst5_base, u32 clkrst6_base);
void u9540_clk_init(u32 clkrst1_base, u32 clkrst2_base, u32 clkrst3_base,
		    u32 clkrst5_base, u32 clkrst6_base);
void u8540_clk_init(u32 clkrst1_base, u32 clkrst2_base, u32 clkrst3_base,
		    u32 clkrst5_base, u32 clkrst6_base);

#endif /* __CLK_UX500_H */

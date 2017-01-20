/*
 * stm32fx-clock.h
 *
 * Copyright (C) 2016 STMicroelectronics
 * Author: Gabriel Fernandez for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

/*
 * List of clocks wich are not derived from system clock (SYSCLOCK)
 *
 * The index of these clocks is the secondary index of DT bindings
 * (see Documentatoin/devicetree/bindings/clock/st,stm32-rcc.txt)
 *
 * e.g:
	<assigned-clocks = <&rcc 1 CLK_LSE>;
*/

#ifndef _DT_BINDINGS_CLK_STMFX_H
#define _DT_BINDINGS_CLK_STMFX_H

#define SYSTICK			0
#define FCLK			1
#define CLK_LSI			2
#define CLK_LSE			3
#define CLK_HSE_RTC		4
#define CLK_RTC			5
#define PLL_VCO_I2S		6
#define PLL_VCO_SAI		7
#define CLK_LCD			8
#define CLK_I2S			9
#define CLK_SAI1		10
#define CLK_SAI2		11
#define CLK_I2SQ_PDIV		12
#define CLK_SAIQ_PDIV		13

#define END_PRIMARY_CLK		14

#endif

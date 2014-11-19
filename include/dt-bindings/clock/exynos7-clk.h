/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Naveen Krishna Ch <naveenkrishna.ch@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _DT_BINDINGS_CLOCK_EXYNOS7_H
#define _DT_BINDINGS_CLOCK_EXYNOS7_H

/* TOPC */
#define DOUT_ACLK_PERIS			1
#define DOUT_SCLK_BUS0_PLL		2
#define DOUT_SCLK_BUS1_PLL		3
#define DOUT_SCLK_CC_PLL		4
#define DOUT_SCLK_MFC_PLL		5
#define DOUT_ACLK_CCORE_133		6
#define TOPC_NR_CLK			7

/* TOP0 */
#define DOUT_ACLK_PERIC1		1
#define DOUT_ACLK_PERIC0		2
#define CLK_SCLK_UART0			3
#define CLK_SCLK_UART1			4
#define CLK_SCLK_UART2			5
#define CLK_SCLK_UART3			6
#define TOP0_NR_CLK			7

/* TOP1 */
#define DOUT_ACLK_FSYS1_200		1
#define DOUT_ACLK_FSYS0_200		2
#define DOUT_SCLK_MMC2			3
#define DOUT_SCLK_MMC1			4
#define DOUT_SCLK_MMC0			5
#define CLK_SCLK_MMC2			6
#define CLK_SCLK_MMC1			7
#define CLK_SCLK_MMC0			8
#define TOP1_NR_CLK			9

/* CCORE */
#define PCLK_RTC			1
#define CCORE_NR_CLK			2

/* PERIC0 */
#define PCLK_UART0			1
#define SCLK_UART0			2
#define PCLK_HSI2C0			3
#define PCLK_HSI2C1			4
#define PCLK_HSI2C4			5
#define PCLK_HSI2C5			6
#define PCLK_HSI2C9			7
#define PCLK_HSI2C10			8
#define PCLK_HSI2C11			9
#define PCLK_PWM			10
#define SCLK_PWM			11
#define PCLK_ADCIF			12
#define PERIC0_NR_CLK			13

/* PERIC1 */
#define PCLK_UART1			1
#define PCLK_UART2			2
#define PCLK_UART3			3
#define SCLK_UART1			4
#define SCLK_UART2			5
#define SCLK_UART3			6
#define PCLK_HSI2C2			7
#define PCLK_HSI2C3			8
#define PCLK_HSI2C6			9
#define PCLK_HSI2C7			10
#define PCLK_HSI2C8			11
#define PERIC1_NR_CLK			12

/* PERIS */
#define PCLK_CHIPID			1
#define SCLK_CHIPID			2
#define PCLK_WDT			3
#define PCLK_TMU			4
#define SCLK_TMU			5
#define PERIS_NR_CLK			6

/* FSYS0 */
#define ACLK_MMC2			1
#define FSYS0_NR_CLK			2

/* FSYS1 */
#define ACLK_MMC1			1
#define ACLK_MMC0			2
#define FSYS1_NR_CLK			3

#endif /* _DT_BINDINGS_CLOCK_EXYNOS7_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Naveen Krishna Ch <naveenkrishna.ch@gmail.com>
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
#define DOUT_ACLK_MSCL_532		7
#define ACLK_MSCL_532			8
#define DOUT_SCLK_AUD_PLL		9
#define FOUT_AUD_PLL			10
#define SCLK_AUD_PLL			11
#define SCLK_MFC_PLL_B			12
#define SCLK_MFC_PLL_A			13
#define SCLK_BUS1_PLL_B			14
#define SCLK_BUS1_PLL_A			15
#define SCLK_BUS0_PLL_B			16
#define SCLK_BUS0_PLL_A			17
#define SCLK_CC_PLL_B			18
#define SCLK_CC_PLL_A			19
#define ACLK_CCORE_133			20
#define ACLK_PERIS_66			21
#define TOPC_NR_CLK			22

/* TOP0 */
#define DOUT_ACLK_PERIC1		1
#define DOUT_ACLK_PERIC0		2
#define CLK_SCLK_UART0			3
#define CLK_SCLK_UART1			4
#define CLK_SCLK_UART2			5
#define CLK_SCLK_UART3			6
#define CLK_SCLK_SPI0			7
#define CLK_SCLK_SPI1			8
#define CLK_SCLK_SPI2			9
#define CLK_SCLK_SPI3			10
#define CLK_SCLK_SPI4			11
#define CLK_SCLK_SPDIF			12
#define CLK_SCLK_PCM1			13
#define CLK_SCLK_I2S1			14
#define CLK_ACLK_PERIC0_66		15
#define CLK_ACLK_PERIC1_66		16
#define TOP0_NR_CLK			17

/* TOP1 */
#define DOUT_ACLK_FSYS1_200		1
#define DOUT_ACLK_FSYS0_200		2
#define DOUT_SCLK_MMC2			3
#define DOUT_SCLK_MMC1			4
#define DOUT_SCLK_MMC0			5
#define CLK_SCLK_MMC2			6
#define CLK_SCLK_MMC1			7
#define CLK_SCLK_MMC0			8
#define CLK_ACLK_FSYS0_200		9
#define CLK_ACLK_FSYS1_200		10
#define CLK_SCLK_PHY_FSYS1		11
#define CLK_SCLK_PHY_FSYS1_26M		12
#define MOUT_SCLK_UFSUNIPRO20		13
#define DOUT_SCLK_UFSUNIPRO20		14
#define CLK_SCLK_UFSUNIPRO20		15
#define DOUT_SCLK_PHY_FSYS1		16
#define DOUT_SCLK_PHY_FSYS1_26M		17
#define TOP1_NR_CLK			18

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
#define PCLK_SPI0			12
#define PCLK_SPI1			13
#define PCLK_SPI2			14
#define PCLK_SPI3			15
#define PCLK_SPI4			16
#define SCLK_SPI0			17
#define SCLK_SPI1			18
#define SCLK_SPI2			19
#define SCLK_SPI3			20
#define SCLK_SPI4			21
#define PCLK_I2S1			22
#define PCLK_PCM1			23
#define PCLK_SPDIF			24
#define SCLK_I2S1			25
#define SCLK_PCM1			26
#define SCLK_SPDIF			27
#define PERIC1_NR_CLK			28

/* PERIS */
#define PCLK_CHIPID			1
#define SCLK_CHIPID			2
#define PCLK_WDT			3
#define PCLK_TMU			4
#define SCLK_TMU			5
#define PERIS_NR_CLK			6

/* FSYS0 */
#define ACLK_MMC2			1
#define ACLK_AXIUS_USBDRD30X_FSYS0X	2
#define ACLK_USBDRD300			3
#define SCLK_USBDRD300_SUSPENDCLK	4
#define SCLK_USBDRD300_REFCLK		5
#define PHYCLK_USBDRD300_UDRD30_PIPE_PCLK_USER		6
#define PHYCLK_USBDRD300_UDRD30_PHYCLK_USER		7
#define OSCCLK_PHY_CLKOUT_USB30_PHY		8
#define ACLK_PDMA0			9
#define ACLK_PDMA1			10
#define FSYS0_NR_CLK			11

/* FSYS1 */
#define ACLK_MMC1			1
#define ACLK_MMC0			2
#define PHYCLK_UFS20_TX0_SYMBOL		3
#define PHYCLK_UFS20_RX0_SYMBOL		4
#define PHYCLK_UFS20_RX1_SYMBOL		5
#define ACLK_UFS20_LINK			6
#define SCLK_UFSUNIPRO20_USER		7
#define PHYCLK_UFS20_RX1_SYMBOL_USER	8
#define PHYCLK_UFS20_RX0_SYMBOL_USER	9
#define PHYCLK_UFS20_TX0_SYMBOL_USER	10
#define OSCCLK_PHY_CLKOUT_EMBEDDED_COMBO_PHY	11
#define SCLK_COMBO_PHY_EMBEDDED_26M	12
#define DOUT_PCLK_FSYS1			13
#define PCLK_GPIO_FSYS1			14
#define MOUT_FSYS1_PHYCLK_SEL1		15
#define FSYS1_NR_CLK			16

/* MSCL */
#define USERMUX_ACLK_MSCL_532		1
#define DOUT_PCLK_MSCL			2
#define ACLK_MSCL_0			3
#define ACLK_MSCL_1			4
#define ACLK_JPEG			5
#define ACLK_G2D			6
#define ACLK_LH_ASYNC_SI_MSCL_0		7
#define ACLK_LH_ASYNC_SI_MSCL_1		8
#define ACLK_AXI2ACEL_BRIDGE		9
#define ACLK_XIU_MSCLX_0		10
#define ACLK_XIU_MSCLX_1		11
#define ACLK_QE_MSCL_0			12
#define ACLK_QE_MSCL_1			13
#define ACLK_QE_JPEG			14
#define ACLK_QE_G2D			15
#define ACLK_PPMU_MSCL_0		16
#define ACLK_PPMU_MSCL_1		17
#define ACLK_MSCLNP_133			18
#define ACLK_AHB2APB_MSCL0P		19
#define ACLK_AHB2APB_MSCL1P		20

#define PCLK_MSCL_0			21
#define PCLK_MSCL_1			22
#define PCLK_JPEG			23
#define PCLK_G2D			24
#define PCLK_QE_MSCL_0			25
#define PCLK_QE_MSCL_1			26
#define PCLK_QE_JPEG			27
#define PCLK_QE_G2D			28
#define PCLK_PPMU_MSCL_0		29
#define PCLK_PPMU_MSCL_1		30
#define PCLK_AXI2ACEL_BRIDGE		31
#define PCLK_PMU_MSCL			32
#define MSCL_NR_CLK			33

/* AUD */
#define SCLK_I2S			1
#define SCLK_PCM			2
#define PCLK_I2S			3
#define PCLK_PCM			4
#define ACLK_ADMA			5
#define AUD_NR_CLK			6
#endif /* _DT_BINDINGS_CLOCK_EXYNOS7_H */

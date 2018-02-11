/*
 * Copyright (c) 2015 Hisilicon Limited.
 *
 * Author: Bintian Wang <bintian.wang@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DT_BINDINGS_CLOCK_HI6220_H
#define __DT_BINDINGS_CLOCK_HI6220_H

/* clk in Hi6220 AO (always on) controller */
#define HI6220_NONE_CLOCK	0

/* fixed rate clocks */
#define HI6220_REF32K		1
#define HI6220_CLK_TCXO		2
#define HI6220_MMC1_PAD		3
#define HI6220_MMC2_PAD		4
#define HI6220_MMC0_PAD		5
#define HI6220_PLL_BBP		6
#define HI6220_PLL_GPU		7
#define HI6220_PLL1_DDR		8
#define HI6220_PLL_SYS		9
#define HI6220_PLL_SYS_MEDIA	10
#define HI6220_DDR_SRC		11
#define HI6220_PLL_MEDIA	12
#define HI6220_PLL_DDR		13

/* fixed factor clocks */
#define HI6220_300M		14
#define HI6220_150M		15
#define HI6220_PICOPHY_SRC	16
#define HI6220_MMC0_SRC_SEL	17
#define HI6220_MMC1_SRC_SEL	18
#define HI6220_MMC2_SRC_SEL	19
#define HI6220_VPU_CODEC	20
#define HI6220_MMC0_SMP		21
#define HI6220_MMC1_SMP		22
#define HI6220_MMC2_SMP		23

/* gate clocks */
#define HI6220_WDT0_PCLK	24
#define HI6220_WDT1_PCLK	25
#define HI6220_WDT2_PCLK	26
#define HI6220_TIMER0_PCLK	27
#define HI6220_TIMER1_PCLK	28
#define HI6220_TIMER2_PCLK	29
#define HI6220_TIMER3_PCLK	30
#define HI6220_TIMER4_PCLK	31
#define HI6220_TIMER5_PCLK	32
#define HI6220_TIMER6_PCLK	33
#define HI6220_TIMER7_PCLK	34
#define HI6220_TIMER8_PCLK	35
#define HI6220_UART0_PCLK	36
#define HI6220_RTC0_PCLK	37
#define HI6220_RTC1_PCLK	38
#define HI6220_AO_NR_CLKS	39

/* clk in Hi6220 systrl */
/* gate clock */
#define HI6220_MMC0_CLK		1
#define HI6220_MMC0_CIUCLK	2
#define HI6220_MMC1_CLK		3
#define HI6220_MMC1_CIUCLK	4
#define HI6220_MMC2_CLK		5
#define HI6220_MMC2_CIUCLK	6
#define HI6220_USBOTG_HCLK	7
#define HI6220_CLK_PICOPHY	8
#define HI6220_HIFI		9
#define HI6220_DACODEC_PCLK	10
#define HI6220_EDMAC_ACLK	11
#define HI6220_CS_ATB		12
#define HI6220_I2C0_CLK		13
#define HI6220_I2C1_CLK		14
#define HI6220_I2C2_CLK		15
#define HI6220_I2C3_CLK		16
#define HI6220_UART1_PCLK	17
#define HI6220_UART2_PCLK	18
#define HI6220_UART3_PCLK	19
#define HI6220_UART4_PCLK	20
#define HI6220_SPI_CLK		21
#define HI6220_TSENSOR_CLK	22
#define HI6220_MMU_CLK		23
#define HI6220_HIFI_SEL		24
#define HI6220_MMC0_SYSPLL	25
#define HI6220_MMC1_SYSPLL	26
#define HI6220_MMC2_SYSPLL	27
#define HI6220_MMC0_SEL		28
#define HI6220_MMC1_SEL		29
#define HI6220_BBPPLL_SEL	30
#define HI6220_MEDIA_PLL_SRC	31
#define HI6220_MMC2_SEL		32
#define HI6220_CS_ATB_SYSPLL	33

/* mux clocks */
#define HI6220_MMC0_SRC		34
#define HI6220_MMC0_SMP_IN	35
#define HI6220_MMC1_SRC		36
#define HI6220_MMC1_SMP_IN	37
#define HI6220_MMC2_SRC		38
#define HI6220_MMC2_SMP_IN	39
#define HI6220_HIFI_SRC		40
#define HI6220_UART1_SRC	41
#define HI6220_UART2_SRC	42
#define HI6220_UART3_SRC	43
#define HI6220_UART4_SRC	44
#define HI6220_MMC0_MUX0	45
#define HI6220_MMC1_MUX0	46
#define HI6220_MMC2_MUX0	47
#define HI6220_MMC0_MUX1	48
#define HI6220_MMC1_MUX1	49
#define HI6220_MMC2_MUX1	50

/* divider clocks */
#define HI6220_CLK_BUS		51
#define HI6220_MMC0_DIV		52
#define HI6220_MMC1_DIV		53
#define HI6220_MMC2_DIV		54
#define HI6220_HIFI_DIV		55
#define HI6220_BBPPLL0_DIV	56
#define HI6220_CS_DAPB		57
#define HI6220_CS_ATB_DIV	58

/* gate clock */
#define HI6220_DAPB_CLK		59

#define HI6220_SYS_NR_CLKS	60

/* clk in Hi6220 media controller */
/* gate clocks */
#define HI6220_DSI_PCLK		1
#define HI6220_G3D_PCLK		2
#define HI6220_ACLK_CODEC_VPU	3
#define HI6220_ISP_SCLK		4
#define HI6220_ADE_CORE		5
#define HI6220_MED_MMU		6
#define HI6220_CFG_CSI4PHY	7
#define HI6220_CFG_CSI2PHY	8
#define HI6220_ISP_SCLK_GATE	9
#define HI6220_ISP_SCLK_GATE1	10
#define HI6220_ADE_CORE_GATE	11
#define HI6220_CODEC_VPU_GATE	12
#define HI6220_MED_SYSPLL	13

/* mux clocks */
#define HI6220_1440_1200	14
#define HI6220_1000_1200	15
#define HI6220_1000_1440	16

/* divider clocks */
#define HI6220_CODEC_JPEG	17
#define HI6220_ISP_SCLK_SRC	18
#define HI6220_ISP_SCLK1	19
#define HI6220_ADE_CORE_SRC	20
#define HI6220_ADE_PIX_SRC	21
#define HI6220_G3D_CLK		22
#define HI6220_CODEC_VPU_SRC	23

#define HI6220_MEDIA_NR_CLKS	24

/* clk in Hi6220 power controller */
/* gate clocks */
#define HI6220_PLL_GPU_GATE	1
#define HI6220_PLL1_DDR_GATE	2
#define HI6220_PLL_DDR_GATE	3
#define HI6220_PLL_MEDIA_GATE	4
#define HI6220_PLL0_BBP_GATE	5

/* divider clocks */
#define HI6220_DDRC_SRC		6
#define HI6220_DDRC_AXI1	7

#define HI6220_POWER_NR_CLKS	8

/* clk in Hi6220 acpu sctrl */
#define HI6220_ACPU_SFT_AT_S		0

#endif

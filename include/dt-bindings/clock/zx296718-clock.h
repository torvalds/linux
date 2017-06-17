/*
 * Copyright (C) 2015 - 2016 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __DT_BINDINGS_CLOCK_ZX296718_H
#define __DT_BINDINGS_CLOCK_ZX296718_H

/* PLL */
#define ZX296718_PLL_CPU	1
#define ZX296718_PLL_MAC	2
#define ZX296718_PLL_MM0	3
#define ZX296718_PLL_MM1	4
#define ZX296718_PLL_VGA	5
#define ZX296718_PLL_DDR	6
#define ZX296718_PLL_AUDIO	7
#define ZX296718_PLL_HSIC	8
#define CPU_DBG_GATE		9
#define A72_GATE		10
#define CPU_PERI_GATE		11
#define A53_GATE		12
#define DDR1_GATE		13
#define DDR0_GATE		14
#define SD1_WCLK		15
#define SD1_AHB			16
#define SD0_WCLK		17
#define SD0_AHB			18
#define EMMC_WCLK		19
#define EMMC_NAND_AXI		20
#define NAND_WCLK		21
#define EMMC_NAND_AHB		22
#define LSP1_148M5		23
#define LSP1_99M		24
#define LSP1_24M		25
#define LSP0_74M25		26
#define LSP0_32K		27
#define LSP0_148M5		28
#define LSP0_99M		29
#define LSP0_24M		30
#define DEMUX_AXI		31
#define DEMUX_APB		32
#define DEMUX_148M5		33
#define DEMUX_108M		34
#define AUDIO_APB		35
#define AUDIO_99M		36
#define AUDIO_24M		37
#define AUDIO_16M384		38
#define AUDIO_32K		39
#define WDT_WCLK		40
#define TIMER_WCLK		41
#define VDE_ACLK		42
#define VCE_ACLK		43
#define HDE_ACLK		44
#define GPU_ACLK		45
#define SAPPU_ACLK		46
#define SAPPU_WCLK		47
#define VOU_ACLK		48
#define VOU_MAIN_WCLK		49
#define VOU_AUX_WCLK		50
#define VOU_PPU_WCLK		51
#define MIPI_CFG_CLK		52
#define VGA_I2C_WCLK		53
#define MIPI_REF_CLK		54
#define HDMI_OSC_CEC		55
#define HDMI_OSC_CLK		56
#define HDMI_XCLK		57
#define VIU_M0_ACLK		58
#define VIU_M1_ACLK		59
#define VIU_WCLK		60
#define VIU_JPEG_WCLK		61
#define VIU_CFG_CLK		62
#define TS_SYS_WCLK		63
#define TS_SYS_108M		64
#define USB20_HCLK		65
#define USB20_PHY_CLK		66
#define USB21_HCLK		67
#define USB21_PHY_CLK		68
#define GMAC_RMIICLK		69
#define GMAC_PCLK		70
#define GMAC_ACLK		71
#define GMAC_RFCLK		72
#define TEMPSENSOR_GATE		73

#define TOP_NR_CLKS		74


#define LSP0_TIMER3_PCLK	1
#define LSP0_TIMER3_WCLK	2
#define LSP0_TIMER4_PCLK	3
#define LSP0_TIMER4_WCLK	4
#define LSP0_TIMER5_PCLK	5
#define LSP0_TIMER5_WCLK	6
#define LSP0_UART3_PCLK		7
#define LSP0_UART3_WCLK		8
#define LSP0_UART1_PCLK		9
#define LSP0_UART1_WCLK		10
#define LSP0_UART2_PCLK		11
#define LSP0_UART2_WCLK		12
#define LSP0_SPIFC0_PCLK	13
#define LSP0_SPIFC0_WCLK	14
#define LSP0_I2C4_PCLK		15
#define LSP0_I2C4_WCLK		16
#define LSP0_I2C5_PCLK		17
#define LSP0_I2C5_WCLK		18
#define LSP0_SSP0_PCLK		19
#define LSP0_SSP0_WCLK		20
#define LSP0_SSP1_PCLK		21
#define LSP0_SSP1_WCLK		22
#define LSP0_USIM_PCLK		23
#define LSP0_USIM_WCLK		24
#define LSP0_GPIO_PCLK		25
#define LSP0_GPIO_WCLK		26
#define LSP0_I2C3_PCLK		27
#define LSP0_I2C3_WCLK		28

#define LSP0_NR_CLKS		29


#define LSP1_UART4_PCLK		1
#define LSP1_UART4_WCLK		2
#define LSP1_UART5_PCLK		3
#define LSP1_UART5_WCLK		4
#define LSP1_PWM_PCLK		5
#define LSP1_PWM_WCLK		6
#define LSP1_I2C2_PCLK		7
#define LSP1_I2C2_WCLK		8
#define LSP1_SSP2_PCLK		9
#define LSP1_SSP2_WCLK		10
#define LSP1_SSP3_PCLK		11
#define LSP1_SSP3_WCLK		12
#define LSP1_SSP4_PCLK		13
#define LSP1_SSP4_WCLK		14
#define LSP1_USIM1_PCLK		15
#define LSP1_USIM1_WCLK		16

#define LSP1_NR_CLKS		17


#define AUDIO_I2S0_WCLK		1
#define AUDIO_I2S0_PCLK		2
#define AUDIO_I2S1_WCLK		3
#define AUDIO_I2S1_PCLK		4
#define AUDIO_I2S2_WCLK		5
#define AUDIO_I2S2_PCLK		6
#define AUDIO_I2S3_WCLK		7
#define AUDIO_I2S3_PCLK		8
#define AUDIO_I2C0_WCLK		9
#define AUDIO_I2C0_PCLK		10
#define AUDIO_SPDIF0_WCLK	11
#define AUDIO_SPDIF0_PCLK	12
#define AUDIO_SPDIF1_WCLK	13
#define AUDIO_SPDIF1_PCLK	14
#define AUDIO_TIMER_WCLK	15
#define AUDIO_TIMER_PCLK	16
#define AUDIO_TDM_WCLK		17
#define AUDIO_TDM_PCLK		18
#define AUDIO_TS_PCLK		19
#define I2S0_WCLK_MUX		20
#define I2S1_WCLK_MUX		21
#define I2S2_WCLK_MUX		22
#define I2S3_WCLK_MUX		23

#define AUDIO_NR_CLKS		24

#endif

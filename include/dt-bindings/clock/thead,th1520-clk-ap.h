/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) 2023 Vivo Communication Technology Co. Ltd.
 * Authors: Yangtao Li <frank.li@vivo.com>
 */

#ifndef _DT_BINDINGS_CLK_TH1520_H_
#define _DT_BINDINGS_CLK_TH1520_H_

#define CLK_CPU_PLL0		0
#define CLK_CPU_PLL1		1
#define CLK_GMAC_PLL		2
#define CLK_VIDEO_PLL		3
#define CLK_DPU0_PLL		4
#define CLK_DPU1_PLL		5
#define CLK_TEE_PLL		6
#define CLK_C910_I0		7
#define CLK_C910		8
#define CLK_BROM		9
#define CLK_BMU			10
#define CLK_AHB2_CPUSYS_HCLK	11
#define CLK_APB3_CPUSYS_PCLK	12
#define CLK_AXI4_CPUSYS2_ACLK	13
#define CLK_AON2CPU_A2X		14
#define CLK_X2X_CPUSYS		15
#define CLK_AXI_ACLK		16
#define CLK_CPU2AON_X2H		17
#define CLK_PERI_AHB_HCLK	18
#define CLK_CPU2PERI_X2H	19
#define CLK_PERI_APB_PCLK	20
#define CLK_PERI2APB_PCLK	21
#define CLK_PERISYS_APB1_HCLK	22
#define CLK_PERISYS_APB2_HCLK	23
#define CLK_PERISYS_APB3_HCLK	24
#define CLK_PERISYS_APB4_HCLK	25
#define CLK_OSC12M		26
#define CLK_OUT1		27
#define CLK_OUT2		28
#define CLK_OUT3		29
#define CLK_OUT4		30
#define CLK_APB_PCLK		31
#define CLK_NPU			32
#define CLK_NPU_AXI		33
#define CLK_VI			34
#define CLK_VI_AHB		35
#define CLK_VO_AXI		36
#define CLK_VP_APB		37
#define CLK_VP_AXI		38
#define CLK_CPU2VP		39
#define CLK_VENC		40
#define CLK_DPU0		41
#define CLK_DPU1		42
#define CLK_EMMC_SDIO		43
#define CLK_GMAC1		44
#define CLK_PADCTRL1		45
#define CLK_DSMART		46
#define CLK_PADCTRL0		47
#define CLK_GMAC_AXI		48
#define CLK_GPIO3		49
#define CLK_GMAC0		50
#define CLK_PWM			51
#define CLK_QSPI0		52
#define CLK_QSPI1		53
#define CLK_SPI			54
#define CLK_UART0_PCLK		55
#define CLK_UART1_PCLK		56
#define CLK_UART2_PCLK		57
#define CLK_UART3_PCLK		58
#define CLK_UART4_PCLK		59
#define CLK_UART5_PCLK		60
#define CLK_GPIO0		61
#define CLK_GPIO1		62
#define CLK_GPIO2		63
#define CLK_I2C0		64
#define CLK_I2C1		65
#define CLK_I2C2		66
#define CLK_I2C3		67
#define CLK_I2C4		68
#define CLK_I2C5		69
#define CLK_SPINLOCK		70
#define CLK_DMA			71
#define CLK_MBOX0		72
#define CLK_MBOX1		73
#define CLK_MBOX2		74
#define CLK_MBOX3		75
#define CLK_WDT0		76
#define CLK_WDT1		77
#define CLK_TIMER0		78
#define CLK_TIMER1		79
#define CLK_SRAM0		80
#define CLK_SRAM1		81
#define CLK_SRAM2		82
#define CLK_SRAM3		83
#define CLK_PLL_GMAC_100M	84
#define CLK_UART_SCLK		85

/* VO clocks */
#define CLK_AXI4_VO_ACLK		0
#define CLK_GPU_MEM			1
#define CLK_GPU_CORE			2
#define CLK_GPU_CFG_ACLK		3
#define CLK_DPU_PIXELCLK0		4
#define CLK_DPU_PIXELCLK1		5
#define CLK_DPU_HCLK			6
#define CLK_DPU_ACLK			7
#define CLK_DPU_CCLK			8
#define CLK_HDMI_SFR			9
#define CLK_HDMI_PCLK			10
#define CLK_HDMI_CEC			11
#define CLK_MIPI_DSI0_PCLK		12
#define CLK_MIPI_DSI1_PCLK		13
#define CLK_MIPI_DSI0_CFG		14
#define CLK_MIPI_DSI1_CFG		15
#define CLK_MIPI_DSI0_REFCLK		16
#define CLK_MIPI_DSI1_REFCLK		17
#define CLK_HDMI_I2S			18
#define CLK_X2H_DPU1_ACLK		19
#define CLK_X2H_DPU_ACLK		20
#define CLK_AXI4_VO_PCLK		21
#define CLK_IOPMP_VOSYS_DPU_PCLK	22
#define CLK_IOPMP_VOSYS_DPU1_PCLK	23
#define CLK_IOPMP_VOSYS_GPU_PCLK	24
#define CLK_IOPMP_DPU1_ACLK		25
#define CLK_IOPMP_DPU_ACLK		26
#define CLK_IOPMP_GPU_ACLK		27
#define CLK_MIPIDSI0_PIXCLK		28
#define CLK_MIPIDSI1_PIXCLK		29
#define CLK_HDMI_PIXCLK			30

#endif

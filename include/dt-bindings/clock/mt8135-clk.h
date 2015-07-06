/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DT_BINDINGS_CLK_MT8135_H
#define _DT_BINDINGS_CLK_MT8135_H

/* TOPCKGEN */

#define CLK_TOP_DSI0_LNTC_DSICLK	1
#define CLK_TOP_HDMITX_CLKDIG_CTS	2
#define CLK_TOP_CLKPH_MCK		3
#define CLK_TOP_CPUM_TCK_IN		4
#define CLK_TOP_MAINPLL_806M		5
#define CLK_TOP_MAINPLL_537P3M		6
#define CLK_TOP_MAINPLL_322P4M		7
#define CLK_TOP_MAINPLL_230P3M		8
#define CLK_TOP_UNIVPLL_624M		9
#define CLK_TOP_UNIVPLL_416M		10
#define CLK_TOP_UNIVPLL_249P6M		11
#define CLK_TOP_UNIVPLL_178P3M		12
#define CLK_TOP_UNIVPLL_48M		13
#define CLK_TOP_MMPLL_D2		14
#define CLK_TOP_MMPLL_D3		15
#define CLK_TOP_MMPLL_D5		16
#define CLK_TOP_MMPLL_D7		17
#define CLK_TOP_MMPLL_D4		18
#define CLK_TOP_MMPLL_D6		19
#define CLK_TOP_SYSPLL_D2		20
#define CLK_TOP_SYSPLL_D4		21
#define CLK_TOP_SYSPLL_D6		22
#define CLK_TOP_SYSPLL_D8		23
#define CLK_TOP_SYSPLL_D10		24
#define CLK_TOP_SYSPLL_D12		25
#define CLK_TOP_SYSPLL_D16		26
#define CLK_TOP_SYSPLL_D24		27
#define CLK_TOP_SYSPLL_D3		28
#define CLK_TOP_SYSPLL_D2P5		29
#define CLK_TOP_SYSPLL_D5		30
#define CLK_TOP_SYSPLL_D3P5		31
#define CLK_TOP_UNIVPLL1_D2		32
#define CLK_TOP_UNIVPLL1_D4		33
#define CLK_TOP_UNIVPLL1_D6		34
#define CLK_TOP_UNIVPLL1_D8		35
#define CLK_TOP_UNIVPLL1_D10		36
#define CLK_TOP_UNIVPLL2_D2		37
#define CLK_TOP_UNIVPLL2_D4		38
#define CLK_TOP_UNIVPLL2_D6		39
#define CLK_TOP_UNIVPLL2_D8		40
#define CLK_TOP_UNIVPLL_D3		41
#define CLK_TOP_UNIVPLL_D5		42
#define CLK_TOP_UNIVPLL_D7		43
#define CLK_TOP_UNIVPLL_D10		44
#define CLK_TOP_UNIVPLL_D26		45
#define CLK_TOP_APLL			46
#define CLK_TOP_APLL_D4			47
#define CLK_TOP_APLL_D8			48
#define CLK_TOP_APLL_D16		49
#define CLK_TOP_APLL_D24		50
#define CLK_TOP_LVDSPLL_D2		51
#define CLK_TOP_LVDSPLL_D4		52
#define CLK_TOP_LVDSPLL_D8		53
#define CLK_TOP_LVDSTX_CLKDIG_CT	54
#define CLK_TOP_VPLL_DPIX		55
#define CLK_TOP_TVHDMI_H		56
#define CLK_TOP_HDMITX_CLKDIG_D2	57
#define CLK_TOP_HDMITX_CLKDIG_D3	58
#define CLK_TOP_TVHDMI_D2		59
#define CLK_TOP_TVHDMI_D4		60
#define CLK_TOP_MEMPLL_MCK_D4		61
#define CLK_TOP_AXI_SEL			62
#define CLK_TOP_SMI_SEL			63
#define CLK_TOP_MFG_SEL			64
#define CLK_TOP_IRDA_SEL		65
#define CLK_TOP_CAM_SEL			66
#define CLK_TOP_AUD_INTBUS_SEL		67
#define CLK_TOP_JPG_SEL			68
#define CLK_TOP_DISP_SEL		69
#define CLK_TOP_MSDC30_1_SEL		70
#define CLK_TOP_MSDC30_2_SEL		71
#define CLK_TOP_MSDC30_3_SEL		72
#define CLK_TOP_MSDC30_4_SEL		73
#define CLK_TOP_USB20_SEL		74
#define CLK_TOP_VENC_SEL		75
#define CLK_TOP_SPI_SEL			76
#define CLK_TOP_UART_SEL		77
#define CLK_TOP_MEM_SEL			78
#define CLK_TOP_CAMTG_SEL		79
#define CLK_TOP_AUDIO_SEL		80
#define CLK_TOP_FIX_SEL			81
#define CLK_TOP_VDEC_SEL		82
#define CLK_TOP_DDRPHYCFG_SEL		83
#define CLK_TOP_DPILVDS_SEL		84
#define CLK_TOP_PMICSPI_SEL		85
#define CLK_TOP_MSDC30_0_SEL		86
#define CLK_TOP_SMI_MFG_AS_SEL		87
#define CLK_TOP_GCPU_SEL		88
#define CLK_TOP_DPI1_SEL		89
#define CLK_TOP_CCI_SEL			90
#define CLK_TOP_APLL_SEL		91
#define CLK_TOP_HDMIPLL_SEL		92
#define CLK_TOP_NR_CLK			93

/* APMIXED_SYS */

#define CLK_APMIXED_ARMPLL1		1
#define CLK_APMIXED_ARMPLL2		2
#define CLK_APMIXED_MAINPLL		3
#define CLK_APMIXED_UNIVPLL		4
#define CLK_APMIXED_MMPLL		5
#define CLK_APMIXED_MSDCPLL		6
#define CLK_APMIXED_TVDPLL		7
#define CLK_APMIXED_LVDSPLL		8
#define CLK_APMIXED_AUDPLL		9
#define CLK_APMIXED_VDECPLL		10
#define CLK_APMIXED_NR_CLK		11

/* INFRA_SYS */

#define CLK_INFRA_PMIC_WRAP		1
#define CLK_INFRA_PMICSPI		2
#define CLK_INFRA_CCIF1_AP_CTRL		3
#define CLK_INFRA_CCIF0_AP_CTRL		4
#define CLK_INFRA_KP			5
#define CLK_INFRA_CPUM			6
#define CLK_INFRA_M4U			7
#define CLK_INFRA_MFGAXI		8
#define CLK_INFRA_DEVAPC		9
#define CLK_INFRA_AUDIO			10
#define CLK_INFRA_MFG_BUS		11
#define CLK_INFRA_SMI			12
#define CLK_INFRA_DBGCLK		13
#define CLK_INFRA_NR_CLK		14

/* PERI_SYS */

#define CLK_PERI_I2C5			1
#define CLK_PERI_I2C4			2
#define CLK_PERI_I2C3			3
#define CLK_PERI_I2C2			4
#define CLK_PERI_I2C1			5
#define CLK_PERI_I2C0			6
#define CLK_PERI_UART3			7
#define CLK_PERI_UART2			8
#define CLK_PERI_UART1			9
#define CLK_PERI_UART0			10
#define CLK_PERI_IRDA			11
#define CLK_PERI_NLI			12
#define CLK_PERI_MD_HIF			13
#define CLK_PERI_AP_HIF			14
#define CLK_PERI_MSDC30_3		15
#define CLK_PERI_MSDC30_2		16
#define CLK_PERI_MSDC30_1		17
#define CLK_PERI_MSDC20_2		18
#define CLK_PERI_MSDC20_1		19
#define CLK_PERI_AP_DMA			20
#define CLK_PERI_USB1			21
#define CLK_PERI_USB0			22
#define CLK_PERI_PWM			23
#define CLK_PERI_PWM7			24
#define CLK_PERI_PWM6			25
#define CLK_PERI_PWM5			26
#define CLK_PERI_PWM4			27
#define CLK_PERI_PWM3			28
#define CLK_PERI_PWM2			29
#define CLK_PERI_PWM1			30
#define CLK_PERI_THERM			31
#define CLK_PERI_NFI			32
#define CLK_PERI_USBSLV			33
#define CLK_PERI_USB1_MCU		34
#define CLK_PERI_USB0_MCU		35
#define CLK_PERI_GCPU			36
#define CLK_PERI_FHCTL			37
#define CLK_PERI_SPI1			38
#define CLK_PERI_AUXADC			39
#define CLK_PERI_PERI_PWRAP		40
#define CLK_PERI_I2C6			41
#define CLK_PERI_UART0_SEL		42
#define CLK_PERI_UART1_SEL		43
#define CLK_PERI_UART2_SEL		44
#define CLK_PERI_UART3_SEL		45
#define CLK_PERI_NR_CLK			46

#endif /* _DT_BINDINGS_CLK_MT8135_H */

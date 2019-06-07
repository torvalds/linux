/* SPDX-License-Identifier: GPL-2.0
 *
 * Device Tree binding constants for Actions Semi S700 Clock Management Unit
 *
 * Copyright (c) 2014 Actions Semi Inc.
 * Author: David Liu <liuwei@actions-semi.com>
 *
 * Author: Pathiban Nallathambi <pn@denx.de>
 * Author: Saravanan Sekar <sravanhome@gmail.com>
 */

#ifndef __DT_BINDINGS_CLOCK_S700_H
#define __DT_BINDINGS_CLOCK_S700_H

#define CLK_NONE			0

/* pll clocks */
#define CLK_CORE_PLL			1
#define CLK_DEV_PLL			2
#define CLK_DDR_PLL			3
#define CLK_NAND_PLL			4
#define CLK_DISPLAY_PLL			5
#define CLK_TVOUT_PLL			6
#define CLK_CVBS_PLL			7
#define CLK_AUDIO_PLL			8
#define CLK_ETHERNET_PLL		9

/* system clock */
#define CLK_CPU				10
#define CLK_DEV				11
#define CLK_AHB				12
#define CLK_APB				13
#define CLK_DMAC			14
#define CLK_NOC0_CLK_MUX		15
#define CLK_NOC1_CLK_MUX		16
#define CLK_HP_CLK_MUX			17
#define CLK_HP_CLK_DIV			18
#define CLK_NOC1_CLK_DIV		19
#define CLK_NOC0			20
#define CLK_NOC1			21
#define CLK_SENOR_SRC			22

/* peripheral device clock */
#define CLK_GPIO			23
#define CLK_TIMER			24
#define CLK_DSI				25
#define CLK_CSI				26
#define CLK_SI				27
#define CLK_DE				28
#define CLK_HDE				29
#define CLK_VDE				30
#define CLK_VCE				31
#define CLK_NAND			32
#define CLK_SD0				33
#define CLK_SD1				34
#define CLK_SD2				35

#define CLK_UART0			36
#define CLK_UART1			37
#define CLK_UART2			38
#define CLK_UART3			39
#define CLK_UART4			40
#define CLK_UART5			41
#define CLK_UART6			42

#define CLK_PWM0			43
#define CLK_PWM1			44
#define CLK_PWM2			45
#define CLK_PWM3			46
#define CLK_PWM4			47
#define CLK_PWM5			48
#define CLK_GPU3D			49

#define CLK_I2C0			50
#define CLK_I2C1			51
#define CLK_I2C2			52
#define CLK_I2C3			53

#define CLK_SPI0			54
#define CLK_SPI1			55
#define CLK_SPI2			56
#define CLK_SPI3			57

#define CLK_USB3_480MPLL0		58
#define CLK_USB3_480MPHY0		59
#define CLK_USB3_5GPHY			60
#define CLK_USB3_CCE			61
#define CLK_USB3_MAC			62

#define CLK_LCD				63
#define CLK_HDMI_AUDIO			64
#define CLK_I2SRX			65
#define CLK_I2STX			66

#define CLK_SENSOR0			67
#define CLK_SENSOR1			68

#define CLK_HDMI_DEV			69

#define CLK_ETHERNET			70
#define CLK_RMII_REF			71

#define CLK_USB2H0_PLLEN		72
#define CLK_USB2H0_PHY			73
#define CLK_USB2H0_CCE			74
#define CLK_USB2H1_PLLEN		75
#define CLK_USB2H1_PHY			76
#define CLK_USB2H1_CCE			77

#define CLK_TVOUT			78

#define CLK_THERMAL_SENSOR		79

#define CLK_IRC_SWITCH			80
#define CLK_PCM1			81
#define CLK_NR_CLKS			(CLK_PCM1 + 1)

#endif /* __DT_BINDINGS_CLOCK_S700_H */

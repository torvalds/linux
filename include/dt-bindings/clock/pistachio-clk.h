/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Google, Inc.
 */

#ifndef _DT_BINDINGS_CLOCK_PISTACHIO_H
#define _DT_BINDINGS_CLOCK_PISTACHIO_H

/* PLLs */
#define CLK_MIPS_PLL			0
#define CLK_AUDIO_PLL			1
#define CLK_RPU_V_PLL			2
#define CLK_RPU_L_PLL			3
#define CLK_SYS_PLL			4
#define CLK_WIFI_PLL			5
#define CLK_BT_PLL			6

/* Fixed-factor clocks */
#define CLK_WIFI_DIV4			16
#define CLK_WIFI_DIV8			17

/* Gate clocks */
#define CLK_MIPS			32
#define CLK_AUDIO_IN			33
#define CLK_AUDIO			34
#define CLK_I2S				35
#define CLK_SPDIF			36
#define CLK_AUDIO_DAC			37
#define CLK_RPU_V			38
#define CLK_RPU_L			39
#define CLK_RPU_SLEEP			40
#define CLK_WIFI_PLL_GATE		41
#define CLK_RPU_CORE			42
#define CLK_WIFI_ADC			43
#define CLK_WIFI_DAC			44
#define CLK_USB_PHY			45
#define CLK_ENET_IN			46
#define CLK_ENET			47
#define CLK_UART0			48
#define CLK_UART1			49
#define CLK_PERIPH_SYS			50
#define CLK_SPI0			51
#define CLK_SPI1			52
#define CLK_EVENT_TIMER			53
#define CLK_AUX_ADC_INTERNAL		54
#define CLK_AUX_ADC			55
#define CLK_SD_HOST			56
#define CLK_BT				57
#define CLK_BT_DIV4			58
#define CLK_BT_DIV8			59
#define CLK_BT_1MHZ			60

/* Divider clocks */
#define CLK_MIPS_INTERNAL_DIV		64
#define CLK_MIPS_DIV			65
#define CLK_AUDIO_DIV			66
#define CLK_I2S_DIV			67
#define CLK_SPDIF_DIV			68
#define CLK_AUDIO_DAC_DIV		69
#define CLK_RPU_V_DIV			70
#define CLK_RPU_L_DIV			71
#define CLK_RPU_SLEEP_DIV		72
#define CLK_RPU_CORE_DIV		73
#define CLK_USB_PHY_DIV			74
#define CLK_ENET_DIV			75
#define CLK_UART0_INTERNAL_DIV		76
#define CLK_UART0_DIV			77
#define CLK_UART1_INTERNAL_DIV		78
#define CLK_UART1_DIV			79
#define CLK_SYS_INTERNAL_DIV		80
#define CLK_SPI0_INTERNAL_DIV		81
#define CLK_SPI0_DIV			82
#define CLK_SPI1_INTERNAL_DIV		83
#define CLK_SPI1_DIV			84
#define CLK_EVENT_TIMER_INTERNAL_DIV	85
#define CLK_EVENT_TIMER_DIV		86
#define CLK_AUX_ADC_INTERNAL_DIV	87
#define CLK_AUX_ADC_DIV			88
#define CLK_SD_HOST_DIV			89
#define CLK_BT_DIV			90
#define CLK_BT_DIV4_DIV			91
#define CLK_BT_DIV8_DIV			92
#define CLK_BT_1MHZ_INTERNAL_DIV	93
#define CLK_BT_1MHZ_DIV			94

/* Mux clocks */
#define CLK_AUDIO_REF_MUX		96
#define CLK_MIPS_PLL_MUX		97
#define CLK_AUDIO_PLL_MUX		98
#define CLK_AUDIO_MUX			99
#define CLK_RPU_V_PLL_MUX		100
#define CLK_RPU_L_PLL_MUX		101
#define CLK_RPU_L_MUX			102
#define CLK_WIFI_PLL_MUX		103
#define CLK_WIFI_DIV4_MUX		104
#define CLK_WIFI_DIV8_MUX		105
#define CLK_RPU_CORE_MUX		106
#define CLK_SYS_PLL_MUX			107
#define CLK_ENET_MUX			108
#define CLK_EVENT_TIMER_MUX		109
#define CLK_SD_HOST_MUX			110
#define CLK_BT_PLL_MUX			111
#define CLK_DEBUG_MUX			112

#define CLK_NR_CLKS			113

/* Peripheral gate clocks */
#define PERIPH_CLK_SYS			0
#define PERIPH_CLK_SYS_BUS		1
#define PERIPH_CLK_DDR			2
#define PERIPH_CLK_ROM			3
#define PERIPH_CLK_COUNTER_FAST		4
#define PERIPH_CLK_COUNTER_SLOW		5
#define PERIPH_CLK_IR			6
#define PERIPH_CLK_WD			7
#define PERIPH_CLK_PDM			8
#define PERIPH_CLK_PWM			9
#define PERIPH_CLK_I2C0			10
#define PERIPH_CLK_I2C1			11
#define PERIPH_CLK_I2C2			12
#define PERIPH_CLK_I2C3			13

/* Peripheral divider clocks */
#define PERIPH_CLK_ROM_DIV		32
#define PERIPH_CLK_COUNTER_FAST_DIV	33
#define PERIPH_CLK_COUNTER_SLOW_PRE_DIV	34
#define PERIPH_CLK_COUNTER_SLOW_DIV	35
#define PERIPH_CLK_IR_PRE_DIV		36
#define PERIPH_CLK_IR_DIV		37
#define PERIPH_CLK_WD_PRE_DIV		38
#define PERIPH_CLK_WD_DIV		39
#define PERIPH_CLK_PDM_PRE_DIV		40
#define PERIPH_CLK_PDM_DIV		41
#define PERIPH_CLK_PWM_PRE_DIV		42
#define PERIPH_CLK_PWM_DIV		43
#define PERIPH_CLK_I2C0_PRE_DIV		44
#define PERIPH_CLK_I2C0_DIV		45
#define PERIPH_CLK_I2C1_PRE_DIV		46
#define PERIPH_CLK_I2C1_DIV		47
#define PERIPH_CLK_I2C2_PRE_DIV		48
#define PERIPH_CLK_I2C2_DIV		49
#define PERIPH_CLK_I2C3_PRE_DIV		50
#define PERIPH_CLK_I2C3_DIV		51

#define PERIPH_CLK_NR_CLKS		52

/* System gate clocks */
#define SYS_CLK_I2C0			0
#define SYS_CLK_I2C1			1
#define SYS_CLK_I2C2			2
#define SYS_CLK_I2C3			3
#define SYS_CLK_I2S_IN			4
#define SYS_CLK_PAUD_OUT		5
#define SYS_CLK_SPDIF_OUT		6
#define SYS_CLK_SPI0_MASTER		7
#define SYS_CLK_SPI0_SLAVE		8
#define SYS_CLK_PWM			9
#define SYS_CLK_UART0			10
#define SYS_CLK_UART1			11
#define SYS_CLK_SPI1			12
#define SYS_CLK_MDC			13
#define SYS_CLK_SD_HOST			14
#define SYS_CLK_ENET			15
#define SYS_CLK_IR			16
#define SYS_CLK_WD			17
#define SYS_CLK_TIMER			18
#define SYS_CLK_I2S_OUT			24
#define SYS_CLK_SPDIF_IN		25
#define SYS_CLK_EVENT_TIMER		26
#define SYS_CLK_HASH			27

#define SYS_CLK_NR_CLKS			28

/* Gates for external input clocks */
#define EXT_CLK_AUDIO_IN		0
#define EXT_CLK_ENET_IN			1

#define EXT_CLK_NR_CLKS			2

#endif /* _DT_BINDINGS_CLOCK_PISTACHIO_H */

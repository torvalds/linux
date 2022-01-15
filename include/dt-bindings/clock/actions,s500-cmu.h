/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Device Tree binding constants for Actions Semi S500 Clock Management Unit
 *
 * Copyright (c) 2014 Actions Semi Inc.
 * Copyright (c) 2018 LSI-TEC - Caninos Loucos
 */

#ifndef __DT_BINDINGS_CLOCK_S500_CMU_H
#define __DT_BINDINGS_CLOCK_S500_CMU_H

#define CLK_NONE		0

/* fixed rate clocks */
#define CLK_LOSC		1
#define CLK_HOSC		2

/* pll clocks */
#define CLK_CORE_PLL		3
#define CLK_DEV_PLL		4
#define CLK_DDR_PLL		5
#define CLK_NAND_PLL		6
#define CLK_DISPLAY_PLL		7
#define CLK_ETHERNET_PLL	8
#define CLK_AUDIO_PLL		9

/* system clock */
#define CLK_DEV			10
#define CLK_H			11
#define CLK_AHBPREDIV		12
#define CLK_AHB			13
#define CLK_DE			14
#define CLK_BISP		15
#define CLK_VCE			16
#define CLK_VDE			17

/* peripheral device clock */
#define CLK_TIMER		18
#define CLK_I2C0		19
#define CLK_I2C1		20
#define CLK_I2C2		21
#define CLK_I2C3		22
#define CLK_PWM0		23
#define CLK_PWM1		24
#define CLK_PWM2		25
#define CLK_PWM3		26
#define CLK_PWM4		27
#define CLK_PWM5		28
#define CLK_SD0			29
#define CLK_SD1			30
#define CLK_SD2			31
#define CLK_SENSOR0		32
#define CLK_SENSOR1		33
#define CLK_SPI0		34
#define CLK_SPI1		35
#define CLK_SPI2		36
#define CLK_SPI3		37
#define CLK_UART0		38
#define CLK_UART1		39
#define CLK_UART2		40
#define CLK_UART3		41
#define CLK_UART4		42
#define CLK_UART5		43
#define CLK_UART6		44
#define CLK_DE1			45
#define CLK_DE2			46
#define CLK_I2SRX		47
#define CLK_I2STX		48
#define CLK_HDMI_AUDIO		49
#define CLK_HDMI		50
#define CLK_SPDIF		51
#define CLK_NAND		52
#define CLK_ECC			53
#define CLK_RMII_REF		54
#define CLK_GPIO		55

/* additional clocks */
#define CLK_APB			56
#define CLK_DMAC		57
#define CLK_NIC			58
#define CLK_ETHERNET		59

#define CLK_NR_CLKS		(CLK_ETHERNET + 1)

#endif /* __DT_BINDINGS_CLOCK_S500_CMU_H */

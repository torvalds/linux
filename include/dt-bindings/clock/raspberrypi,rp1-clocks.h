/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) 2021 Raspberry Pi Ltd.
 */

#ifndef __DT_BINDINGS_CLOCK_RASPBERRYPI_RP1
#define __DT_BINDINGS_CLOCK_RASPBERRYPI_RP1

#define RP1_PLL_SYS_CORE		0
#define RP1_PLL_AUDIO_CORE		1
#define RP1_PLL_VIDEO_CORE		2

#define RP1_PLL_SYS			3
#define RP1_PLL_AUDIO			4
#define RP1_PLL_VIDEO			5

#define RP1_PLL_SYS_PRI_PH		6
#define RP1_PLL_SYS_SEC_PH		7
#define RP1_PLL_AUDIO_PRI_PH		8

#define RP1_PLL_SYS_SEC			9
#define RP1_PLL_AUDIO_SEC		10
#define RP1_PLL_VIDEO_SEC		11

#define RP1_CLK_SYS			12
#define RP1_CLK_SLOW_SYS		13
#define RP1_CLK_DMA			14
#define RP1_CLK_UART			15
#define RP1_CLK_ETH			16
#define RP1_CLK_PWM0			17
#define RP1_CLK_PWM1			18
#define RP1_CLK_AUDIO_IN		19
#define RP1_CLK_AUDIO_OUT		20
#define RP1_CLK_I2S			21
#define RP1_CLK_MIPI0_CFG		22
#define RP1_CLK_MIPI1_CFG		23
#define RP1_CLK_PCIE_AUX		24
#define RP1_CLK_USBH0_MICROFRAME	25
#define RP1_CLK_USBH1_MICROFRAME	26
#define RP1_CLK_USBH0_SUSPEND		27
#define RP1_CLK_USBH1_SUSPEND		28
#define RP1_CLK_ETH_TSU			29
#define RP1_CLK_ADC			30
#define RP1_CLK_SDIO_TIMER		31
#define RP1_CLK_SDIO_ALT_SRC		32
#define RP1_CLK_GP0			33
#define RP1_CLK_GP1			34
#define RP1_CLK_GP2			35
#define RP1_CLK_GP3			36
#define RP1_CLK_GP4			37
#define RP1_CLK_GP5			38
#define RP1_CLK_VEC			39
#define RP1_CLK_DPI			40
#define RP1_CLK_MIPI0_DPI		41
#define RP1_CLK_MIPI1_DPI		42

/* Extra PLL output channels - RP1B0 only */
#define RP1_PLL_VIDEO_PRI_PH		43
#define RP1_PLL_AUDIO_TERN		44

/* MIPI clocks managed by the DSI driver */
#define RP1_CLK_MIPI0_DSI_BYTECLOCK	45
#define RP1_CLK_MIPI1_DSI_BYTECLOCK	46

#endif

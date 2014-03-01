/*
 * Copyright (c) 2012-2013 Hisilicon Limited.
 * Copyright (c) 2012-2013 Linaro Limited.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
 *	   Xin Li <li.xin@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef __DTS_HI3620_CLOCK_H
#define __DTS_HI3620_CLOCK_H

#define HI3620_NONE_CLOCK	0

/* fixed rate & fixed factor clocks */
#define HI3620_OSC32K		1
#define HI3620_OSC26M		2
#define HI3620_PCLK		3
#define HI3620_PLL_ARM0		4
#define HI3620_PLL_ARM1		5
#define HI3620_PLL_PERI		6
#define HI3620_PLL_USB		7
#define HI3620_PLL_HDMI		8
#define HI3620_PLL_GPU		9
#define HI3620_RCLK_TCXO	10
#define HI3620_RCLK_CFGAXI	11
#define HI3620_RCLK_PICO	12

/* mux clocks */
#define HI3620_TIMER0_MUX	32
#define HI3620_TIMER1_MUX	33
#define HI3620_TIMER2_MUX	34
#define HI3620_TIMER3_MUX	35
#define HI3620_TIMER4_MUX	36
#define HI3620_TIMER5_MUX	37
#define HI3620_TIMER6_MUX	38
#define HI3620_TIMER7_MUX	39
#define HI3620_TIMER8_MUX	40
#define HI3620_TIMER9_MUX	41
#define HI3620_UART0_MUX	42
#define HI3620_UART1_MUX	43
#define HI3620_UART2_MUX	44
#define HI3620_UART3_MUX	45
#define HI3620_UART4_MUX	46
#define HI3620_SPI0_MUX		47
#define HI3620_SPI1_MUX		48
#define HI3620_SPI2_MUX		49
#define HI3620_SAXI_MUX		50
#define HI3620_PWM0_MUX		51
#define HI3620_PWM1_MUX		52
#define HI3620_SD_MUX		53
#define HI3620_MMC1_MUX		54
#define HI3620_MMC1_MUX2	55
#define HI3620_G2D_MUX		56
#define HI3620_VENC_MUX		57
#define HI3620_VDEC_MUX		58
#define HI3620_VPP_MUX		59
#define HI3620_EDC0_MUX		60
#define HI3620_LDI0_MUX		61
#define HI3620_EDC1_MUX		62
#define HI3620_LDI1_MUX		63
#define HI3620_RCLK_HSIC	64
#define HI3620_MMC2_MUX		65
#define HI3620_MMC3_MUX		66

/* divider clocks */
#define HI3620_SHAREAXI_DIV	128
#define HI3620_CFGAXI_DIV	129
#define HI3620_SD_DIV		130
#define HI3620_MMC1_DIV		131
#define HI3620_HSIC_DIV		132
#define HI3620_MMC2_DIV		133
#define HI3620_MMC3_DIV		134

/* gate clocks */
#define HI3620_TIMERCLK01	160
#define HI3620_TIMER_RCLK01	161
#define HI3620_TIMERCLK23	162
#define HI3620_TIMER_RCLK23	163
#define HI3620_TIMERCLK45	164
#define HI3620_TIMERCLK67	165
#define HI3620_TIMERCLK89	166
#define HI3620_RTCCLK		167
#define HI3620_KPC_CLK		168
#define HI3620_GPIOCLK0		169
#define HI3620_GPIOCLK1		170
#define HI3620_GPIOCLK2		171
#define HI3620_GPIOCLK3		172
#define HI3620_GPIOCLK4		173
#define HI3620_GPIOCLK5		174
#define HI3620_GPIOCLK6		175
#define HI3620_GPIOCLK7		176
#define HI3620_GPIOCLK8		177
#define HI3620_GPIOCLK9		178
#define HI3620_GPIOCLK10	179
#define HI3620_GPIOCLK11	180
#define HI3620_GPIOCLK12	181
#define HI3620_GPIOCLK13	182
#define HI3620_GPIOCLK14	183
#define HI3620_GPIOCLK15	184
#define HI3620_GPIOCLK16	185
#define HI3620_GPIOCLK17	186
#define HI3620_GPIOCLK18	187
#define HI3620_GPIOCLK19	188
#define HI3620_GPIOCLK20	189
#define HI3620_GPIOCLK21	190
#define HI3620_DPHY0_CLK	191
#define HI3620_DPHY1_CLK	192
#define HI3620_DPHY2_CLK	193
#define HI3620_USBPHY_CLK	194
#define HI3620_ACP_CLK		195
#define HI3620_PWMCLK0		196
#define HI3620_PWMCLK1		197
#define HI3620_UARTCLK0		198
#define HI3620_UARTCLK1		199
#define HI3620_UARTCLK2		200
#define HI3620_UARTCLK3		201
#define HI3620_UARTCLK4		202
#define HI3620_SPICLK0		203
#define HI3620_SPICLK1		204
#define HI3620_SPICLK2		205
#define HI3620_I2CCLK0		206
#define HI3620_I2CCLK1		207
#define HI3620_I2CCLK2		208
#define HI3620_I2CCLK3		209
#define HI3620_SCI_CLK		210
#define HI3620_DDRC_PER_CLK	211
#define HI3620_DMAC_CLK		212
#define HI3620_USB2DVC_CLK	213
#define HI3620_SD_CLK		214
#define HI3620_MMC_CLK1		215
#define HI3620_MMC_CLK2		216
#define HI3620_MMC_CLK3		217
#define HI3620_MCU_CLK		218

#define HI3620_NR_CLKS		219

#endif	/* __DTS_HI3620_CLOCK_H */

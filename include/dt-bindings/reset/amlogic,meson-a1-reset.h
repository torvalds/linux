/* SPDX-License-Identifier: (GPL-2.0+ OR MIT)
 *
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 * Author: Xingyu Chen <xingyu.chen@amlogic.com>
 *
 */

#ifndef _DT_BINDINGS_AMLOGIC_MESON_A1_RESET_H
#define _DT_BINDINGS_AMLOGIC_MESON_A1_RESET_H

/*	RESET0					*/
/*					0	*/
#define RESET_AM2AXI_VAD		1
/*					2-3	*/
#define RESET_PSRAM			4
#define RESET_PAD_CTRL			5
/*					6	*/
#define RESET_TEMP_SENSOR		7
#define RESET_AM2AXI_DEV		8
/*					9	*/
#define RESET_SPICC_A			10
#define RESET_MSR_CLK			11
#define RESET_AUDIO			12
#define RESET_ANALOG_CTRL		13
#define RESET_SAR_ADC			14
#define RESET_AUDIO_VAD			15
#define RESET_CEC			16
#define RESET_PWM_EF			17
#define RESET_PWM_CD			18
#define RESET_PWM_AB			19
/*					20	*/
#define RESET_IR_CTRL			21
#define RESET_I2C_S_A			22
/*					23	*/
#define RESET_I2C_M_D			24
#define RESET_I2C_M_C			25
#define RESET_I2C_M_B			26
#define RESET_I2C_M_A			27
#define RESET_I2C_PROD_AHB		28
#define RESET_I2C_PROD			29
/*					30-31	*/

/*	RESET1					*/
#define RESET_ACODEC			32
#define RESET_DMA			33
#define RESET_SD_EMMC_A			34
/*					35	*/
#define RESET_USBCTRL			36
/*					37	*/
#define RESET_USBPHY			38
/*					39-41	*/
#define RESET_RSA			42
#define RESET_DMC			43
/*					44	*/
#define RESET_IRQ_CTRL			45
/*					46	*/
#define RESET_NIC_VAD			47
#define RESET_NIC_AXI			48
#define RESET_RAMA			49
#define RESET_RAMB			50
/*					51-52	*/
#define RESET_ROM			53
#define RESET_SPIFC			54
#define RESET_GIC			55
#define RESET_UART_C			56
#define RESET_UART_B			57
#define RESET_UART_A			58
#define RESET_OSC_RING			59
/*					60-63	*/

/*	RESET2					*/
/*					64-95	*/

#endif

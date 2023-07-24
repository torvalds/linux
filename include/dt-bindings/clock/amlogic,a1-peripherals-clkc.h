/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 * Author: Jian Hu <jian.hu@amlogic.com>
 *
 * Copyright (c) 2023, SberDevices. All Rights Reserved.
 * Author: Dmitry Rokosov <ddrokosov@sberdevices.ru>
 */

#ifndef __A1_PERIPHERALS_CLKC_H
#define __A1_PERIPHERALS_CLKC_H

#define CLKID_FIXPLL_IN		1
#define CLKID_USB_PHY_IN	2
#define CLKID_USB_CTRL_IN	3
#define CLKID_HIFIPLL_IN	4
#define CLKID_SYSPLL_IN		5
#define CLKID_DDS_IN		6
#define CLKID_SYS		7
#define CLKID_CLKTREE		8
#define CLKID_RESET_CTRL	9
#define CLKID_ANALOG_CTRL	10
#define CLKID_PWR_CTRL		11
#define CLKID_PAD_CTRL		12
#define CLKID_SYS_CTRL		13
#define CLKID_TEMP_SENSOR	14
#define CLKID_AM2AXI_DIV	15
#define CLKID_SPICC_B		16
#define CLKID_SPICC_A		17
#define CLKID_MSR		18
#define CLKID_AUDIO		19
#define CLKID_JTAG_CTRL		20
#define CLKID_SARADC_EN		21
#define CLKID_PWM_EF		22
#define CLKID_PWM_CD		23
#define CLKID_PWM_AB		24
#define CLKID_CEC		25
#define CLKID_I2C_S		26
#define CLKID_IR_CTRL		27
#define CLKID_I2C_M_D		28
#define CLKID_I2C_M_C		29
#define CLKID_I2C_M_B		30
#define CLKID_I2C_M_A		31
#define CLKID_ACODEC		32
#define CLKID_OTP		33
#define CLKID_SD_EMMC_A		34
#define CLKID_USB_PHY		35
#define CLKID_USB_CTRL		36
#define CLKID_SYS_DSPB		37
#define CLKID_SYS_DSPA		38
#define CLKID_DMA		39
#define CLKID_IRQ_CTRL		40
#define CLKID_NIC		41
#define CLKID_GIC		42
#define CLKID_UART_C		43
#define CLKID_UART_B		44
#define CLKID_UART_A		45
#define CLKID_SYS_PSRAM		46
#define CLKID_RSA		47
#define CLKID_CORESIGHT		48
#define CLKID_AM2AXI_VAD	49
#define CLKID_AUDIO_VAD		50
#define CLKID_AXI_DMC		51
#define CLKID_AXI_PSRAM		52
#define CLKID_RAMB		53
#define CLKID_RAMA		54
#define CLKID_AXI_SPIFC		55
#define CLKID_AXI_NIC		56
#define CLKID_AXI_DMA		57
#define CLKID_CPU_CTRL		58
#define CLKID_ROM		59
#define CLKID_PROC_I2C		60
#define CLKID_DSPA_EN		63
#define CLKID_DSPA_EN_NIC	64
#define CLKID_DSPB_EN		65
#define CLKID_DSPB_EN_NIC	66
#define CLKID_RTC		67
#define CLKID_CECA_32K		68
#define CLKID_CECB_32K		69
#define CLKID_24M		70
#define CLKID_12M		71
#define CLKID_FCLK_DIV2_DIVN	72
#define CLKID_GEN		73
#define CLKID_SARADC		75
#define CLKID_PWM_A		76
#define CLKID_PWM_B		77
#define CLKID_PWM_C		78
#define CLKID_PWM_D		79
#define CLKID_PWM_E		80
#define CLKID_PWM_F		81
#define CLKID_SPICC		82
#define CLKID_TS		83
#define CLKID_SPIFC		84
#define CLKID_USB_BUS		85
#define CLKID_SD_EMMC		86
#define CLKID_PSRAM		87
#define CLKID_DMC		88
#define CLKID_DSPA_A_SEL	95
#define CLKID_DSPA_B_SEL	98
#define CLKID_DSPB_A_SEL	101
#define CLKID_DSPB_B_SEL	104
#define CLKID_CECB_32K_SEL_PRE	113
#define CLKID_CECB_32K_SEL	114
#define CLKID_CECA_32K_SEL_PRE	117
#define CLKID_CECA_32K_SEL	118
#define CLKID_GEN_SEL		121
#define CLKID_PWM_A_SEL		124
#define CLKID_PWM_B_SEL		126
#define CLKID_PWM_C_SEL		128
#define CLKID_PWM_D_SEL		130
#define CLKID_PWM_E_SEL		132
#define CLKID_PWM_F_SEL		134
#define CLKID_SD_EMMC_SEL2	147

#endif /* __A1_PERIPHERALS_CLKC_H */

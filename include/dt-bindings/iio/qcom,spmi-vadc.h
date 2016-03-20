/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DT_BINDINGS_QCOM_SPMI_VADC_H
#define _DT_BINDINGS_QCOM_SPMI_VADC_H

/* Voltage ADC channels */
#define VADC_USBIN				0x00
#define VADC_DCIN				0x01
#define VADC_VCHG_SNS				0x02
#define VADC_SPARE1_03				0x03
#define VADC_USB_ID_MV				0x04
#define VADC_VCOIN				0x05
#define VADC_VBAT_SNS				0x06
#define VADC_VSYS				0x07
#define VADC_DIE_TEMP				0x08
#define VADC_REF_625MV				0x09
#define VADC_REF_1250MV				0x0a
#define VADC_CHG_TEMP				0x0b
#define VADC_SPARE1				0x0c
#define VADC_SPARE2				0x0d
#define VADC_GND_REF				0x0e
#define VADC_VDD_VADC				0x0f

#define VADC_P_MUX1_1_1				0x10
#define VADC_P_MUX2_1_1				0x11
#define VADC_P_MUX3_1_1				0x12
#define VADC_P_MUX4_1_1				0x13
#define VADC_P_MUX5_1_1				0x14
#define VADC_P_MUX6_1_1				0x15
#define VADC_P_MUX7_1_1				0x16
#define VADC_P_MUX8_1_1				0x17
#define VADC_P_MUX9_1_1				0x18
#define VADC_P_MUX10_1_1			0x19
#define VADC_P_MUX11_1_1			0x1a
#define VADC_P_MUX12_1_1			0x1b
#define VADC_P_MUX13_1_1			0x1c
#define VADC_P_MUX14_1_1			0x1d
#define VADC_P_MUX15_1_1			0x1e
#define VADC_P_MUX16_1_1			0x1f

#define VADC_P_MUX1_1_3				0x20
#define VADC_P_MUX2_1_3				0x21
#define VADC_P_MUX3_1_3				0x22
#define VADC_P_MUX4_1_3				0x23
#define VADC_P_MUX5_1_3				0x24
#define VADC_P_MUX6_1_3				0x25
#define VADC_P_MUX7_1_3				0x26
#define VADC_P_MUX8_1_3				0x27
#define VADC_P_MUX9_1_3				0x28
#define VADC_P_MUX10_1_3			0x29
#define VADC_P_MUX11_1_3			0x2a
#define VADC_P_MUX12_1_3			0x2b
#define VADC_P_MUX13_1_3			0x2c
#define VADC_P_MUX14_1_3			0x2d
#define VADC_P_MUX15_1_3			0x2e
#define VADC_P_MUX16_1_3			0x2f

#define VADC_LR_MUX1_BAT_THERM			0x30
#define VADC_LR_MUX2_BAT_ID			0x31
#define VADC_LR_MUX3_XO_THERM			0x32
#define VADC_LR_MUX4_AMUX_THM1			0x33
#define VADC_LR_MUX5_AMUX_THM2			0x34
#define VADC_LR_MUX6_AMUX_THM3			0x35
#define VADC_LR_MUX7_HW_ID			0x36
#define VADC_LR_MUX8_AMUX_THM4			0x37
#define VADC_LR_MUX9_AMUX_THM5			0x38
#define VADC_LR_MUX10_USB_ID			0x39
#define VADC_AMUX_PU1				0x3a
#define VADC_AMUX_PU2				0x3b
#define VADC_LR_MUX3_BUF_XO_THERM		0x3c

#define VADC_LR_MUX1_PU1_BAT_THERM		0x70
#define VADC_LR_MUX2_PU1_BAT_ID			0x71
#define VADC_LR_MUX3_PU1_XO_THERM		0x72
#define VADC_LR_MUX4_PU1_AMUX_THM1		0x73
#define VADC_LR_MUX5_PU1_AMUX_THM2		0x74
#define VADC_LR_MUX6_PU1_AMUX_THM3		0x75
#define VADC_LR_MUX7_PU1_AMUX_HW_ID		0x76
#define VADC_LR_MUX8_PU1_AMUX_THM4		0x77
#define VADC_LR_MUX9_PU1_AMUX_THM5		0x78
#define VADC_LR_MUX10_PU1_AMUX_USB_ID		0x79
#define VADC_LR_MUX3_BUF_PU1_XO_THERM		0x7c

#define VADC_LR_MUX1_PU2_BAT_THERM		0xb0
#define VADC_LR_MUX2_PU2_BAT_ID			0xb1
#define VADC_LR_MUX3_PU2_XO_THERM		0xb2
#define VADC_LR_MUX4_PU2_AMUX_THM1		0xb3
#define VADC_LR_MUX5_PU2_AMUX_THM2		0xb4
#define VADC_LR_MUX6_PU2_AMUX_THM3		0xb5
#define VADC_LR_MUX7_PU2_AMUX_HW_ID		0xb6
#define VADC_LR_MUX8_PU2_AMUX_THM4		0xb7
#define VADC_LR_MUX9_PU2_AMUX_THM5		0xb8
#define VADC_LR_MUX10_PU2_AMUX_USB_ID		0xb9
#define VADC_LR_MUX3_BUF_PU2_XO_THERM		0xbc

#define VADC_LR_MUX1_PU1_PU2_BAT_THERM		0xf0
#define VADC_LR_MUX2_PU1_PU2_BAT_ID		0xf1
#define VADC_LR_MUX3_PU1_PU2_XO_THERM		0xf2
#define VADC_LR_MUX4_PU1_PU2_AMUX_THM1		0xf3
#define VADC_LR_MUX5_PU1_PU2_AMUX_THM2		0xf4
#define VADC_LR_MUX6_PU1_PU2_AMUX_THM3		0xf5
#define VADC_LR_MUX7_PU1_PU2_AMUX_HW_ID		0xf6
#define VADC_LR_MUX8_PU1_PU2_AMUX_THM4		0xf7
#define VADC_LR_MUX9_PU1_PU2_AMUX_THM5		0xf8
#define VADC_LR_MUX10_PU1_PU2_AMUX_USB_ID	0xf9
#define VADC_LR_MUX3_BUF_PU1_PU2_XO_THERM	0xfc

#endif /* _DT_BINDINGS_QCOM_SPMI_VADC_H */

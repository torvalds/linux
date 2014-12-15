/*
 * aml_m8_codec.h  --  AMLM8 Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _AML_M8_CODEC_H
#define _AML_M8_CODEC_H

#define APB_BASE					0x44000

#define AML_M8_PLAY_LRCLK_DIV 		0
#define AML_M8_PLAY_SCLK_DIV		1
#define AML_M8_REC_LRCLK_DIV		2
#define AML_M8_REC_SCLK_DIV			3

#define AMLM8_MAX_REG_NUM		0xFB

#define AMLM8_RESET				0x00
#define AMLM8_CLK_EXT_SELECT	0x01
#define AMLM8_I2S1_CONFIG_0		0x03
#define AMLM8_I2S1_CONFIG_1		0x04
#define AMLM8_I2S1_CONFIG_2		0x05
#define AMLM8_I2S1_CONFIG_3		0x06
#define AMLM8_I2S1_CONFIG_4		0x07

#define AMLM8_IREF				0x12
#define AMLM8_BP_PWR			0x13
#define AMLM8_STDBY_SLEEP		0x14
#define AMLM8_PD_0				0x15
#define AMLM8_PD_1				0x16

#define AMLM8_PD_3				0x18
#define AMLM8_PD_4				0x19

#define AMLM8_MUTE_0			0x1D
#define AMLM8_MUTE_2			0x1F
#define AMLM8_MUTE_4			0x21

#define AMLM8_REC_LEFT_VOL		0x24
#define AMLM8_REC_RIGHT_VOL		0x25
#define AMLM8_PGA_LEFT_VOL		0x26
#define AMLM8_PGA_RIGHT_VOL		0x27
#define AMLM8_LM_LEFT_VOL		0x34
#define AMLM8_LM_RIGHT_VOL		0x35
#define AMLM8_HS_LEFT_VOL		0x38
#define AMLM8_HS_RIGHT_VOL		0x39

#define AMLM8_PGA_SEL			0x47

#define AMLM8_INT_GEN			0x4F
#define AMLM8_INT_MASK_0		0x50
#define AMLM8_INT_MASK_1		0x51
#define AMLM8_INT_STATUS_0		0x52
#define AMLM8_INT_STATUS_1		0x53
#define AMLM8_INT_LEVLE_0		0x54
#define AMLM8_INT_LEVLE_1		0x55

#define AMLM8_LDR1_LEFT_SEL		0x59
#define AMLM8_LDR1_RIGHT_SEL	0x5A
#define AMLM8_LDR2_SEL			0x5D

#define AMLM8_REC_DMIX			0x7B
#define AMLM8_PB_DMIX			0x7C
#define AMLM8_I2S1_DMIX			0x7D

#define AMLM8_REC_PB_VOL		0x80
#define AMLM8_REC_I2S1_VOL		0x82
#define AMLM8_I2S1_PB_VOL		0x88
#define AMLM8_I2S1_I2S1_VOL		0x8A

#define AMLM8_POP_FREE_0		0xA2
#define AMLM8_NOTCH				0xAA
#define AMLM8_NOTCH0_0			0xAB
#define AMLM8_NOTCH0_1			0xAC
#define AMLM8_NOTCH0_2			0xAD
#define AMLM8_NOTCH0_3			0xAE
#define AMLM8_NOTCH1_0			0xAF
#define AMLM8_NOTCH1_1			0xB0
#define AMLM8_NOTCH1_2			0xB1
#define AMLM8_NOTCH1_3			0xB2
#define AMLM8_NOTCH2_0			0xB3
#define AMLM8_NOTCH2_1			0xB4
#define AMLM8_NOTCH2_2			0xB5
#define AMLM8_NOTCH2_3			0xB6
#define AMLM8_NOTCH3_0			0xB7
#define AMLM8_NOTCH3_1			0xB8
#define AMLM8_NOTCH3_2			0xB9
#define AMLM8_NOTCH3_3			0xBA

#define AMLM8_WIND_FILTER		0xBB

#define AMLM8_ALC_0				0xC8
#define AMLM8_ALC_1				0xC9
#define AMLM8_ALC_2				0xCA
#define AMLM8_ALC_3				0xCB
#define AMLM8_ALC_4				0xCC
#define AMLM8_ALC_5				0xCD

#define AMLM8_NOISE_GATE_0		0xCE
#define AMLM8_NOISE_GATE_1		0xCF

#define AMLM8_DTEST				0xD2
#define AMLM8_HP_0				0xD3
#define AMLM8_ANAS_RAMP			0xD4
#define AMLM8_DIGS_RAMP			0xD5
#define AMLM8_REFGEN_0			0xDC
#define AMLM8_REFGEN_1			0xDD

#define AMLM8_PGA_AUX_4			0xEC
#define AMLM8_PGA_AUX_5			0xED

#endif


void adac_wr_reg (unsigned long addr, unsigned long data);



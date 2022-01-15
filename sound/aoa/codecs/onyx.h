/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Apple Onboard Audio driver for Onyx codec (header)
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 */
#ifndef __SND_AOA_CODEC_ONYX_H
#define __SND_AOA_CODEC_ONYX_H
#include <linux/i2c.h>
#include <asm/pmac_low_i2c.h>
#include <asm/prom.h>

/* PCM3052 register definitions */

/* the attenuation registers take values from
 * -1 (0dB) to -127 (-63.0 dB) or others (muted) */
#define ONYX_REG_DAC_ATTEN_LEFT		65
#define FIRSTREGISTER			ONYX_REG_DAC_ATTEN_LEFT
#define ONYX_REG_DAC_ATTEN_RIGHT	66

#define ONYX_REG_CONTROL		67
#	define ONYX_MRST		(1<<7)
#	define ONYX_SRST		(1<<6)
#	define ONYX_ADPSV		(1<<5)
#	define ONYX_DAPSV		(1<<4)
#	define ONYX_SILICONVERSION	(1<<0)
/* all others reserved */

#define ONYX_REG_DAC_CONTROL		68
#	define ONYX_OVR1		(1<<6)
#	define ONYX_MUTE_RIGHT		(1<<1)
#	define ONYX_MUTE_LEFT		(1<<0)

#define ONYX_REG_DAC_DEEMPH		69
#	define ONYX_DIGDEEMPH_SHIFT	5
#	define ONYX_DIGDEEMPH_MASK	(3<<ONYX_DIGDEEMPH_SHIFT)
#	define ONYX_DIGDEEMPH_CTRL	(1<<4)

#define ONYX_REG_DAC_FILTER		70
#	define ONYX_ROLLOFF_FAST	(1<<5)
#	define ONYX_DAC_FILTER_ALWAYS	(1<<2)

#define	ONYX_REG_DAC_OUTPHASE		71
#	define ONYX_OUTPHASE_INVERTED	(1<<0)

#define ONYX_REG_ADC_CONTROL		72
#	define ONYX_ADC_INPUT_MIC	(1<<5)
/* 8 + input gain in dB, valid range for input gain is -4 .. 20 dB */
#	define ONYX_ADC_PGA_GAIN_MASK	0x1f

#define ONYX_REG_ADC_HPF_BYPASS		75
#	define ONYX_HPF_DISABLE		(1<<3)
#	define ONYX_ADC_HPF_ALWAYS	(1<<2)

#define ONYX_REG_DIG_INFO1		77
#	define ONYX_MASK_DIN_TO_BPZ	(1<<7)
/* bits 1-5 control channel bits 1-5 */
#	define ONYX_DIGOUT_DISABLE	(1<<0)

#define ONYX_REG_DIG_INFO2		78
/* controls channel bits 8-15 */

#define ONYX_REG_DIG_INFO3		79
/* control channel bits 24-29, high 2 bits reserved */

#define ONYX_REG_DIG_INFO4		80
#	define ONYX_VALIDL		(1<<7)
#	define ONYX_VALIDR		(1<<6)
#	define ONYX_SPDIF_ENABLE	(1<<5)
/* lower 4 bits control bits 32-35 of channel control and word length */
#	define ONYX_WORDLEN_MASK	(0xF)

#endif /* __SND_AOA_CODEC_ONYX_H */

// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC CX20721/CX20723 codec driver
//
// Copyright:	(C) 2017 Conexant Systems, Inc.
// Author:	Simon Ho, <Simon.ho@conexant.com>
//
// TODO: add support for TDM mode.
//

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include "cx2072x.h"

#define PLL_OUT_HZ_48	(1024 * 3 * 48000)
#define BITS_PER_SLOT	8

/* codec private data */
struct cx2072x_priv {
	struct regmap *regmap;
	struct clk *mclk;
	unsigned int mclk_rate;
	struct device *dev;
	struct snd_soc_component *codec;
	struct snd_soc_jack_gpio jack_gpio;
	struct mutex lock;
	unsigned int bclk_ratio;
	bool pll_changed;
	bool i2spcm_changed;
	int sample_size;
	int frame_size;
	int sample_rate;
	unsigned int dai_fmt;
	bool en_aec_ref;
};

/*
 * DAC/ADC Volume
 *
 * max : 74 : 0 dB
 *	 ( in 1 dB  step )
 * min : 0 : -74 dB
 */
static const DECLARE_TLV_DB_SCALE(adc_tlv, -7400, 100, 0);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -7400, 100, 0);
static const DECLARE_TLV_DB_SCALE(boost_tlv, 0, 1200, 0);

struct cx2072x_eq_ctrl {
	u8 ch;
	u8 band;
};

static const DECLARE_TLV_DB_RANGE(hpf_tlv,
	0, 0, TLV_DB_SCALE_ITEM(120, 0, 0),
	1, 63, TLV_DB_SCALE_ITEM(30, 30, 0)
);

/* Lookup table for PRE_DIV */
static const struct {
	unsigned int mclk;
	unsigned int div;
} mclk_pre_div[] = {
	{ 6144000, 1 },
	{ 12288000, 2 },
	{ 19200000, 3 },
	{ 26000000, 4 },
	{ 28224000, 5 },
	{ 36864000, 6 },
	{ 36864000, 7 },
	{ 48000000, 8 },
	{ 49152000, 8 },
};

/*
 * cx2072x register cache.
 */
static const struct reg_default cx2072x_reg_defaults[] = {
	{ CX2072X_AFG_POWER_STATE, 0x00000003 },
	{ CX2072X_UM_RESPONSE, 0x00000000 },
	{ CX2072X_GPIO_DATA, 0x00000000 },
	{ CX2072X_GPIO_ENABLE, 0x00000000 },
	{ CX2072X_GPIO_DIRECTION, 0x00000000 },
	{ CX2072X_GPIO_WAKE, 0x00000000 },
	{ CX2072X_GPIO_UM_ENABLE, 0x00000000 },
	{ CX2072X_GPIO_STICKY_MASK, 0x00000000 },
	{ CX2072X_DAC1_CONVERTER_FORMAT, 0x00000031 },
	{ CX2072X_DAC1_AMP_GAIN_RIGHT, 0x0000004a },
	{ CX2072X_DAC1_AMP_GAIN_LEFT, 0x0000004a },
	{ CX2072X_DAC1_POWER_STATE, 0x00000433 },
	{ CX2072X_DAC1_CONVERTER_STREAM_CHANNEL, 0x00000000 },
	{ CX2072X_DAC1_EAPD_ENABLE, 0x00000000 },
	{ CX2072X_DAC2_CONVERTER_FORMAT, 0x00000031 },
	{ CX2072X_DAC2_AMP_GAIN_RIGHT, 0x0000004a },
	{ CX2072X_DAC2_AMP_GAIN_LEFT, 0x0000004a },
	{ CX2072X_DAC2_POWER_STATE, 0x00000433 },
	{ CX2072X_DAC2_CONVERTER_STREAM_CHANNEL, 0x00000000 },
	{ CX2072X_ADC1_CONVERTER_FORMAT, 0x00000031 },
	{ CX2072X_ADC1_AMP_GAIN_RIGHT_0, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_LEFT_0, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_RIGHT_1, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_LEFT_1, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_RIGHT_2, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_LEFT_2, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_RIGHT_3, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_LEFT_3, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_RIGHT_4, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_LEFT_4, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_RIGHT_5, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_LEFT_5, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_RIGHT_6, 0x0000004a },
	{ CX2072X_ADC1_AMP_GAIN_LEFT_6, 0x0000004a },
	{ CX2072X_ADC1_CONNECTION_SELECT_CONTROL, 0x00000000 },
	{ CX2072X_ADC1_POWER_STATE, 0x00000433 },
	{ CX2072X_ADC1_CONVERTER_STREAM_CHANNEL, 0x00000000 },
	{ CX2072X_ADC2_CONVERTER_FORMAT, 0x00000031 },
	{ CX2072X_ADC2_AMP_GAIN_RIGHT_0, 0x0000004a },
	{ CX2072X_ADC2_AMP_GAIN_LEFT_0, 0x0000004a },
	{ CX2072X_ADC2_AMP_GAIN_RIGHT_1, 0x0000004a },
	{ CX2072X_ADC2_AMP_GAIN_LEFT_1, 0x0000004a },
	{ CX2072X_ADC2_AMP_GAIN_RIGHT_2, 0x0000004a },
	{ CX2072X_ADC2_AMP_GAIN_LEFT_2, 0x0000004a },
	{ CX2072X_ADC2_CONNECTION_SELECT_CONTROL, 0x00000000 },
	{ CX2072X_ADC2_POWER_STATE, 0x00000433 },
	{ CX2072X_ADC2_CONVERTER_STREAM_CHANNEL, 0x00000000 },
	{ CX2072X_PORTA_CONNECTION_SELECT_CTRL, 0x00000000 },
	{ CX2072X_PORTA_POWER_STATE, 0x00000433 },
	{ CX2072X_PORTA_PIN_CTRL, 0x000000c0 },
	{ CX2072X_PORTA_UNSOLICITED_RESPONSE, 0x00000000 },
	{ CX2072X_PORTA_PIN_SENSE, 0x00000000 },
	{ CX2072X_PORTA_EAPD_BTL, 0x00000002 },
	{ CX2072X_PORTB_POWER_STATE, 0x00000433 },
	{ CX2072X_PORTB_PIN_CTRL, 0x00000000 },
	{ CX2072X_PORTB_UNSOLICITED_RESPONSE, 0x00000000 },
	{ CX2072X_PORTB_PIN_SENSE, 0x00000000 },
	{ CX2072X_PORTB_EAPD_BTL, 0x00000002 },
	{ CX2072X_PORTB_GAIN_RIGHT, 0x00000000 },
	{ CX2072X_PORTB_GAIN_LEFT, 0x00000000 },
	{ CX2072X_PORTC_POWER_STATE, 0x00000433 },
	{ CX2072X_PORTC_PIN_CTRL, 0x00000000 },
	{ CX2072X_PORTC_GAIN_RIGHT, 0x00000000 },
	{ CX2072X_PORTC_GAIN_LEFT, 0x00000000 },
	{ CX2072X_PORTD_POWER_STATE, 0x00000433 },
	{ CX2072X_PORTD_PIN_CTRL, 0x00000020 },
	{ CX2072X_PORTD_UNSOLICITED_RESPONSE, 0x00000000 },
	{ CX2072X_PORTD_PIN_SENSE, 0x00000000 },
	{ CX2072X_PORTD_GAIN_RIGHT, 0x00000000 },
	{ CX2072X_PORTD_GAIN_LEFT, 0x00000000 },
	{ CX2072X_PORTE_CONNECTION_SELECT_CTRL, 0x00000000 },
	{ CX2072X_PORTE_POWER_STATE, 0x00000433 },
	{ CX2072X_PORTE_PIN_CTRL, 0x00000040 },
	{ CX2072X_PORTE_UNSOLICITED_RESPONSE, 0x00000000 },
	{ CX2072X_PORTE_PIN_SENSE, 0x00000000 },
	{ CX2072X_PORTE_EAPD_BTL, 0x00000002 },
	{ CX2072X_PORTE_GAIN_RIGHT, 0x00000000 },
	{ CX2072X_PORTE_GAIN_LEFT, 0x00000000 },
	{ CX2072X_PORTF_POWER_STATE, 0x00000433 },
	{ CX2072X_PORTF_PIN_CTRL, 0x00000000 },
	{ CX2072X_PORTF_UNSOLICITED_RESPONSE, 0x00000000 },
	{ CX2072X_PORTF_PIN_SENSE, 0x00000000 },
	{ CX2072X_PORTF_GAIN_RIGHT, 0x00000000 },
	{ CX2072X_PORTF_GAIN_LEFT, 0x00000000 },
	{ CX2072X_PORTG_POWER_STATE, 0x00000433 },
	{ CX2072X_PORTG_PIN_CTRL, 0x00000040 },
	{ CX2072X_PORTG_CONNECTION_SELECT_CTRL, 0x00000000 },
	{ CX2072X_PORTG_EAPD_BTL, 0x00000002 },
	{ CX2072X_PORTM_POWER_STATE, 0x00000433 },
	{ CX2072X_PORTM_PIN_CTRL, 0x00000000 },
	{ CX2072X_PORTM_CONNECTION_SELECT_CTRL, 0x00000000 },
	{ CX2072X_PORTM_EAPD_BTL, 0x00000002 },
	{ CX2072X_MIXER_POWER_STATE, 0x00000433 },
	{ CX2072X_MIXER_GAIN_RIGHT_0, 0x0000004a },
	{ CX2072X_MIXER_GAIN_LEFT_0, 0x0000004a },
	{ CX2072X_MIXER_GAIN_RIGHT_1, 0x0000004a },
	{ CX2072X_MIXER_GAIN_LEFT_1, 0x0000004a },
	{ CX2072X_SPKR_DRC_ENABLE_STEP, 0x040065a4 },
	{ CX2072X_SPKR_DRC_CONTROL, 0x007b0024 },
	{ CX2072X_SPKR_DRC_TEST, 0x00000000 },
	{ CX2072X_DIGITAL_BIOS_TEST0, 0x001f008a },
	{ CX2072X_DIGITAL_BIOS_TEST2, 0x00990026 },
	{ CX2072X_I2SPCM_CONTROL1, 0x00010001 },
	{ CX2072X_I2SPCM_CONTROL2, 0x00000000 },
	{ CX2072X_I2SPCM_CONTROL3, 0x00000000 },
	{ CX2072X_I2SPCM_CONTROL4, 0x00000000 },
	{ CX2072X_I2SPCM_CONTROL5, 0x00000000 },
	{ CX2072X_I2SPCM_CONTROL6, 0x00000000 },
	{ CX2072X_UM_INTERRUPT_CRTL_E, 0x00000000 },
	{ CX2072X_CODEC_TEST2, 0x00000000 },
	{ CX2072X_CODEC_TEST9, 0x00000004 },
	{ CX2072X_CODEC_TEST20, 0x00000600 },
	{ CX2072X_CODEC_TEST26, 0x00000208 },
	{ CX2072X_ANALOG_TEST4, 0x00000000 },
	{ CX2072X_ANALOG_TEST5, 0x00000000 },
	{ CX2072X_ANALOG_TEST6, 0x0000059a },
	{ CX2072X_ANALOG_TEST7, 0x000000a7 },
	{ CX2072X_ANALOG_TEST8, 0x00000017 },
	{ CX2072X_ANALOG_TEST9, 0x00000000 },
	{ CX2072X_ANALOG_TEST10, 0x00000285 },
	{ CX2072X_ANALOG_TEST11, 0x00000000 },
	{ CX2072X_ANALOG_TEST12, 0x00000000 },
	{ CX2072X_ANALOG_TEST13, 0x00000000 },
	{ CX2072X_DIGITAL_TEST1, 0x00000242 },
	{ CX2072X_DIGITAL_TEST11, 0x00000000 },
	{ CX2072X_DIGITAL_TEST12, 0x00000084 },
	{ CX2072X_DIGITAL_TEST15, 0x00000077 },
	{ CX2072X_DIGITAL_TEST16, 0x00000021 },
	{ CX2072X_DIGITAL_TEST17, 0x00000018 },
	{ CX2072X_DIGITAL_TEST18, 0x00000024 },
	{ CX2072X_DIGITAL_TEST19, 0x00000001 },
	{ CX2072X_DIGITAL_TEST20, 0x00000002 },
};

/*
 * register initialization
 */
static const struct reg_sequence cx2072x_reg_init[] = {
	{ CX2072X_ANALOG_TEST9,	0x080 },    /* DC offset Calibration */
	{ CX2072X_CODEC_TEST26,	0x65f },    /* Disable the PA */
	{ CX2072X_ANALOG_TEST10, 0x289 },   /* Set the speaker output gain */
	{ CX2072X_CODEC_TEST20,	0xf05 },
	{ CX2072X_CODEC_TESTXX,	0x380 },
	{ CX2072X_CODEC_TEST26,	0xb90 },
	{ CX2072X_CODEC_TEST9,	0x001 },    /* Enable 30 Hz High pass filter */
	{ CX2072X_ANALOG_TEST3,	0x300 },    /* Disable PCBEEP pad */
	{ CX2072X_CODEC_TEST24,	0x100 },    /* Disable SnM mode */
	{ CX2072X_PORTD_PIN_CTRL, 0x020 },  /* Enable PortD input */
	{ CX2072X_GPIO_ENABLE,	0x040 },    /* Enable GPIO7 pin for button */
	{ CX2072X_GPIO_UM_ENABLE, 0x040 },  /* Enable UM for GPIO7 */
	{ CX2072X_UM_RESPONSE,	0x080 },    /* Enable button response */
	{ CX2072X_DIGITAL_TEST12, 0x0c4 },  /* Enable headset button */
	{ CX2072X_DIGITAL_TEST0, 0x415 },   /* Power down class-D during idle */
	{ CX2072X_I2SPCM_CONTROL2, 0x00f }, /* Enable I2S TX */
	{ CX2072X_I2SPCM_CONTROL3, 0x00f }, /* Enable I2S RX */
};

static unsigned int cx2072x_register_size(unsigned int reg)
{
	switch (reg) {
	case CX2072X_VENDOR_ID:
	case CX2072X_REVISION_ID:
	case CX2072X_PORTA_PIN_SENSE:
	case CX2072X_PORTB_PIN_SENSE:
	case CX2072X_PORTD_PIN_SENSE:
	case CX2072X_PORTE_PIN_SENSE:
	case CX2072X_PORTF_PIN_SENSE:
	case CX2072X_I2SPCM_CONTROL1:
	case CX2072X_I2SPCM_CONTROL2:
	case CX2072X_I2SPCM_CONTROL3:
	case CX2072X_I2SPCM_CONTROL4:
	case CX2072X_I2SPCM_CONTROL5:
	case CX2072X_I2SPCM_CONTROL6:
	case CX2072X_UM_INTERRUPT_CRTL_E:
	case CX2072X_EQ_G_COEFF:
	case CX2072X_SPKR_DRC_CONTROL:
	case CX2072X_SPKR_DRC_TEST:
	case CX2072X_DIGITAL_BIOS_TEST0:
	case CX2072X_DIGITAL_BIOS_TEST2:
		return 4;
	case CX2072X_EQ_ENABLE_BYPASS:
	case CX2072X_EQ_B0_COEFF:
	case CX2072X_EQ_B1_COEFF:
	case CX2072X_EQ_B2_COEFF:
	case CX2072X_EQ_A1_COEFF:
	case CX2072X_EQ_A2_COEFF:
	case CX2072X_DAC1_CONVERTER_FORMAT:
	case CX2072X_DAC2_CONVERTER_FORMAT:
	case CX2072X_ADC1_CONVERTER_FORMAT:
	case CX2072X_ADC2_CONVERTER_FORMAT:
	case CX2072X_CODEC_TEST2:
	case CX2072X_CODEC_TEST9:
	case CX2072X_CODEC_TEST20:
	case CX2072X_CODEC_TEST26:
	case CX2072X_ANALOG_TEST3:
	case CX2072X_ANALOG_TEST4:
	case CX2072X_ANALOG_TEST5:
	case CX2072X_ANALOG_TEST6:
	case CX2072X_ANALOG_TEST7:
	case CX2072X_ANALOG_TEST8:
	case CX2072X_ANALOG_TEST9:
	case CX2072X_ANALOG_TEST10:
	case CX2072X_ANALOG_TEST11:
	case CX2072X_ANALOG_TEST12:
	case CX2072X_ANALOG_TEST13:
	case CX2072X_DIGITAL_TEST0:
	case CX2072X_DIGITAL_TEST1:
	case CX2072X_DIGITAL_TEST11:
	case CX2072X_DIGITAL_TEST12:
	case CX2072X_DIGITAL_TEST15:
	case CX2072X_DIGITAL_TEST16:
	case CX2072X_DIGITAL_TEST17:
	case CX2072X_DIGITAL_TEST18:
	case CX2072X_DIGITAL_TEST19:
	case CX2072X_DIGITAL_TEST20:
		return 2;
	default:
		return 1;
	}
}

static bool cx2072x_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CX2072X_VENDOR_ID:
	case CX2072X_REVISION_ID:
	case CX2072X_CURRENT_BCLK_FREQUENCY:
	case CX2072X_AFG_POWER_STATE:
	case CX2072X_UM_RESPONSE:
	case CX2072X_GPIO_DATA:
	case CX2072X_GPIO_ENABLE:
	case CX2072X_GPIO_DIRECTION:
	case CX2072X_GPIO_WAKE:
	case CX2072X_GPIO_UM_ENABLE:
	case CX2072X_GPIO_STICKY_MASK:
	case CX2072X_DAC1_CONVERTER_FORMAT:
	case CX2072X_DAC1_AMP_GAIN_RIGHT:
	case CX2072X_DAC1_AMP_GAIN_LEFT:
	case CX2072X_DAC1_POWER_STATE:
	case CX2072X_DAC1_CONVERTER_STREAM_CHANNEL:
	case CX2072X_DAC1_EAPD_ENABLE:
	case CX2072X_DAC2_CONVERTER_FORMAT:
	case CX2072X_DAC2_AMP_GAIN_RIGHT:
	case CX2072X_DAC2_AMP_GAIN_LEFT:
	case CX2072X_DAC2_POWER_STATE:
	case CX2072X_DAC2_CONVERTER_STREAM_CHANNEL:
	case CX2072X_ADC1_CONVERTER_FORMAT:
	case CX2072X_ADC1_AMP_GAIN_RIGHT_0:
	case CX2072X_ADC1_AMP_GAIN_LEFT_0:
	case CX2072X_ADC1_AMP_GAIN_RIGHT_1:
	case CX2072X_ADC1_AMP_GAIN_LEFT_1:
	case CX2072X_ADC1_AMP_GAIN_RIGHT_2:
	case CX2072X_ADC1_AMP_GAIN_LEFT_2:
	case CX2072X_ADC1_AMP_GAIN_RIGHT_3:
	case CX2072X_ADC1_AMP_GAIN_LEFT_3:
	case CX2072X_ADC1_AMP_GAIN_RIGHT_4:
	case CX2072X_ADC1_AMP_GAIN_LEFT_4:
	case CX2072X_ADC1_AMP_GAIN_RIGHT_5:
	case CX2072X_ADC1_AMP_GAIN_LEFT_5:
	case CX2072X_ADC1_AMP_GAIN_RIGHT_6:
	case CX2072X_ADC1_AMP_GAIN_LEFT_6:
	case CX2072X_ADC1_CONNECTION_SELECT_CONTROL:
	case CX2072X_ADC1_POWER_STATE:
	case CX2072X_ADC1_CONVERTER_STREAM_CHANNEL:
	case CX2072X_ADC2_CONVERTER_FORMAT:
	case CX2072X_ADC2_AMP_GAIN_RIGHT_0:
	case CX2072X_ADC2_AMP_GAIN_LEFT_0:
	case CX2072X_ADC2_AMP_GAIN_RIGHT_1:
	case CX2072X_ADC2_AMP_GAIN_LEFT_1:
	case CX2072X_ADC2_AMP_GAIN_RIGHT_2:
	case CX2072X_ADC2_AMP_GAIN_LEFT_2:
	case CX2072X_ADC2_CONNECTION_SELECT_CONTROL:
	case CX2072X_ADC2_POWER_STATE:
	case CX2072X_ADC2_CONVERTER_STREAM_CHANNEL:
	case CX2072X_PORTA_CONNECTION_SELECT_CTRL:
	case CX2072X_PORTA_POWER_STATE:
	case CX2072X_PORTA_PIN_CTRL:
	case CX2072X_PORTA_UNSOLICITED_RESPONSE:
	case CX2072X_PORTA_PIN_SENSE:
	case CX2072X_PORTA_EAPD_BTL:
	case CX2072X_PORTB_POWER_STATE:
	case CX2072X_PORTB_PIN_CTRL:
	case CX2072X_PORTB_UNSOLICITED_RESPONSE:
	case CX2072X_PORTB_PIN_SENSE:
	case CX2072X_PORTB_EAPD_BTL:
	case CX2072X_PORTB_GAIN_RIGHT:
	case CX2072X_PORTB_GAIN_LEFT:
	case CX2072X_PORTC_POWER_STATE:
	case CX2072X_PORTC_PIN_CTRL:
	case CX2072X_PORTC_GAIN_RIGHT:
	case CX2072X_PORTC_GAIN_LEFT:
	case CX2072X_PORTD_POWER_STATE:
	case CX2072X_PORTD_PIN_CTRL:
	case CX2072X_PORTD_UNSOLICITED_RESPONSE:
	case CX2072X_PORTD_PIN_SENSE:
	case CX2072X_PORTD_GAIN_RIGHT:
	case CX2072X_PORTD_GAIN_LEFT:
	case CX2072X_PORTE_CONNECTION_SELECT_CTRL:
	case CX2072X_PORTE_POWER_STATE:
	case CX2072X_PORTE_PIN_CTRL:
	case CX2072X_PORTE_UNSOLICITED_RESPONSE:
	case CX2072X_PORTE_PIN_SENSE:
	case CX2072X_PORTE_EAPD_BTL:
	case CX2072X_PORTE_GAIN_RIGHT:
	case CX2072X_PORTE_GAIN_LEFT:
	case CX2072X_PORTF_POWER_STATE:
	case CX2072X_PORTF_PIN_CTRL:
	case CX2072X_PORTF_UNSOLICITED_RESPONSE:
	case CX2072X_PORTF_PIN_SENSE:
	case CX2072X_PORTF_GAIN_RIGHT:
	case CX2072X_PORTF_GAIN_LEFT:
	case CX2072X_PORTG_POWER_STATE:
	case CX2072X_PORTG_PIN_CTRL:
	case CX2072X_PORTG_CONNECTION_SELECT_CTRL:
	case CX2072X_PORTG_EAPD_BTL:
	case CX2072X_PORTM_POWER_STATE:
	case CX2072X_PORTM_PIN_CTRL:
	case CX2072X_PORTM_CONNECTION_SELECT_CTRL:
	case CX2072X_PORTM_EAPD_BTL:
	case CX2072X_MIXER_POWER_STATE:
	case CX2072X_MIXER_GAIN_RIGHT_0:
	case CX2072X_MIXER_GAIN_LEFT_0:
	case CX2072X_MIXER_GAIN_RIGHT_1:
	case CX2072X_MIXER_GAIN_LEFT_1:
	case CX2072X_EQ_ENABLE_BYPASS:
	case CX2072X_EQ_B0_COEFF:
	case CX2072X_EQ_B1_COEFF:
	case CX2072X_EQ_B2_COEFF:
	case CX2072X_EQ_A1_COEFF:
	case CX2072X_EQ_A2_COEFF:
	case CX2072X_EQ_G_COEFF:
	case CX2072X_SPKR_DRC_ENABLE_STEP:
	case CX2072X_SPKR_DRC_CONTROL:
	case CX2072X_SPKR_DRC_TEST:
	case CX2072X_DIGITAL_BIOS_TEST0:
	case CX2072X_DIGITAL_BIOS_TEST2:
	case CX2072X_I2SPCM_CONTROL1:
	case CX2072X_I2SPCM_CONTROL2:
	case CX2072X_I2SPCM_CONTROL3:
	case CX2072X_I2SPCM_CONTROL4:
	case CX2072X_I2SPCM_CONTROL5:
	case CX2072X_I2SPCM_CONTROL6:
	case CX2072X_UM_INTERRUPT_CRTL_E:
	case CX2072X_CODEC_TEST2:
	case CX2072X_CODEC_TEST9:
	case CX2072X_CODEC_TEST20:
	case CX2072X_CODEC_TEST26:
	case CX2072X_ANALOG_TEST4:
	case CX2072X_ANALOG_TEST5:
	case CX2072X_ANALOG_TEST6:
	case CX2072X_ANALOG_TEST7:
	case CX2072X_ANALOG_TEST8:
	case CX2072X_ANALOG_TEST9:
	case CX2072X_ANALOG_TEST10:
	case CX2072X_ANALOG_TEST11:
	case CX2072X_ANALOG_TEST12:
	case CX2072X_ANALOG_TEST13:
	case CX2072X_DIGITAL_TEST0:
	case CX2072X_DIGITAL_TEST1:
	case CX2072X_DIGITAL_TEST11:
	case CX2072X_DIGITAL_TEST12:
	case CX2072X_DIGITAL_TEST15:
	case CX2072X_DIGITAL_TEST16:
	case CX2072X_DIGITAL_TEST17:
	case CX2072X_DIGITAL_TEST18:
	case CX2072X_DIGITAL_TEST19:
	case CX2072X_DIGITAL_TEST20:
		return true;
	default:
		return false;
	}
}

static bool cx2072x_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CX2072X_VENDOR_ID:
	case CX2072X_REVISION_ID:
	case CX2072X_UM_INTERRUPT_CRTL_E:
	case CX2072X_DIGITAL_TEST11:
	case CX2072X_PORTA_PIN_SENSE:
	case CX2072X_PORTB_PIN_SENSE:
	case CX2072X_PORTD_PIN_SENSE:
	case CX2072X_PORTE_PIN_SENSE:
	case CX2072X_PORTF_PIN_SENSE:
	case CX2072X_EQ_G_COEFF:
	case CX2072X_EQ_BAND:
		return true;
	default:
		return false;
	}
}

static int cx2072x_reg_raw_write(struct i2c_client *client,
				 unsigned int reg,
				 const void *val, size_t val_count)
{
	struct device *dev = &client->dev;
	u8 buf[2 + CX2072X_MAX_EQ_COEFF];
	int ret;

	if (WARN_ON(val_count + 2 > sizeof(buf)))
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	memcpy(buf + 2, val, val_count);

	ret = i2c_master_send(client, buf, val_count + 2);
	if (ret != val_count + 2) {
		dev_err(dev, "I2C write failed, ret = %d\n", ret);
		return ret < 0 ? ret : -EIO;
	}
	return 0;
}

static int cx2072x_reg_write(void *context, unsigned int reg,
			     unsigned int value)
{
	__le32 raw_value;
	unsigned int size;

	size = cx2072x_register_size(reg);

	if (reg == CX2072X_UM_INTERRUPT_CRTL_E) {
		/* Update the MSB byte only */
		reg += 3;
		size = 1;
		value >>= 24;
	}

	raw_value = cpu_to_le32(value);
	return cx2072x_reg_raw_write(context, reg, &raw_value, size);
}

static int cx2072x_reg_read(void *context, unsigned int reg,
			    unsigned int *value)
{
	struct i2c_client *client = context;
	struct device *dev = &client->dev;
	__le32 recv_buf = 0;
	struct i2c_msg msgs[2];
	unsigned int size;
	u8 send_buf[2];
	int ret;

	size = cx2072x_register_size(reg);

	send_buf[0] = reg >> 8;
	send_buf[1] = reg & 0xff;

	msgs[0].addr = client->addr;
	msgs[0].len = sizeof(send_buf);
	msgs[0].buf = send_buf;
	msgs[0].flags = 0;

	msgs[1].addr = client->addr;
	msgs[1].len = size;
	msgs[1].buf = (u8 *)&recv_buf;
	msgs[1].flags = I2C_M_RD;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(dev, "Failed to read register, ret = %d\n", ret);
		return ret < 0 ? ret : -EIO;
	}

	*value = le32_to_cpu(recv_buf);
	return 0;
}

/* get suggested pre_div valuce from mclk frequency */
static unsigned int get_div_from_mclk(unsigned int mclk)
{
	unsigned int div = 8;
	int i;

	for (i = 0; i < ARRAY_SIZE(mclk_pre_div); i++) {
		if (mclk <= mclk_pre_div[i].mclk) {
			div = mclk_pre_div[i].div;
			break;
		}
	}
	return div;
}

static int cx2072x_config_pll(struct cx2072x_priv *cx2072x)
{
	struct device *dev = cx2072x->dev;
	unsigned int pre_div;
	unsigned int pre_div_val;
	unsigned int pll_input;
	unsigned int pll_output;
	unsigned int int_div;
	unsigned int frac_div;
	u64 frac_num;
	unsigned int frac;
	unsigned int sample_rate = cx2072x->sample_rate;
	int pt_sample_per_sync = 2;
	int pt_clock_per_sample = 96;

	switch (sample_rate) {
	case 48000:
	case 32000:
	case 24000:
	case 16000:
		break;

	case 96000:
		pt_sample_per_sync = 1;
		pt_clock_per_sample = 48;
		break;

	case 192000:
		pt_sample_per_sync = 0;
		pt_clock_per_sample = 24;
		break;

	default:
		dev_err(dev, "Unsupported sample rate %d\n", sample_rate);
		return -EINVAL;
	}

	/* Configure PLL settings */
	pre_div = get_div_from_mclk(cx2072x->mclk_rate);
	pll_input = cx2072x->mclk_rate / pre_div;
	pll_output = sample_rate * 3072;
	int_div = pll_output / pll_input;
	frac_div = pll_output - (int_div * pll_input);

	if (frac_div) {
		frac_div *= 1000;
		frac_div /= pll_input;
		frac_num = (u64)(4000 + frac_div) * ((1 << 20) - 4);
		do_div(frac_num, 7);
		frac = ((u32)frac_num + 499) / 1000;
	}
	pre_div_val = (pre_div - 1) * 2;

	regmap_write(cx2072x->regmap, CX2072X_ANALOG_TEST4,
		     0x40 | (pre_div_val << 8));
	if (frac_div == 0) {
		/* Int mode */
		regmap_write(cx2072x->regmap, CX2072X_ANALOG_TEST7, 0x100);
	} else {
		/* frac mode */
		regmap_write(cx2072x->regmap, CX2072X_ANALOG_TEST6,
			     frac & 0xfff);
		regmap_write(cx2072x->regmap, CX2072X_ANALOG_TEST7,
			     (u8)(frac >> 12));
	}

	int_div--;
	regmap_write(cx2072x->regmap, CX2072X_ANALOG_TEST8, int_div);

	/* configure PLL tracking */
	if (frac_div == 0) {
		/* disable PLL tracking */
		regmap_write(cx2072x->regmap, CX2072X_DIGITAL_TEST16, 0x00);
	} else {
		/* configure and enable PLL tracking */
		regmap_write(cx2072x->regmap, CX2072X_DIGITAL_TEST16,
			     (pt_sample_per_sync << 4) & 0xf0);
		regmap_write(cx2072x->regmap, CX2072X_DIGITAL_TEST17,
			     pt_clock_per_sample);
		regmap_write(cx2072x->regmap, CX2072X_DIGITAL_TEST18,
			     pt_clock_per_sample * 3 / 2);
		regmap_write(cx2072x->regmap, CX2072X_DIGITAL_TEST19, 0x01);
		regmap_write(cx2072x->regmap, CX2072X_DIGITAL_TEST20, 0x02);
		regmap_update_bits(cx2072x->regmap, CX2072X_DIGITAL_TEST16,
				   0x01, 0x01);
	}

	return 0;
}

static int cx2072x_config_i2spcm(struct cx2072x_priv *cx2072x)
{
	struct device *dev = cx2072x->dev;
	unsigned int bclk_rate = 0;
	int is_i2s = 0;
	int has_one_bit_delay = 0;
	int is_frame_inv = 0;
	int is_bclk_inv = 0;
	int pulse_len;
	int frame_len = cx2072x->frame_size;
	int sample_size = cx2072x->sample_size;
	int i2s_right_slot;
	int i2s_right_pause_interval = 0;
	int i2s_right_pause_pos;
	int is_big_endian = 1;
	u64 div;
	unsigned int mod;
	union cx2072x_reg_i2spcm_ctrl_reg1 reg1;
	union cx2072x_reg_i2spcm_ctrl_reg2 reg2;
	union cx2072x_reg_i2spcm_ctrl_reg3 reg3;
	union cx2072x_reg_i2spcm_ctrl_reg4 reg4;
	union cx2072x_reg_i2spcm_ctrl_reg5 reg5;
	union cx2072x_reg_i2spcm_ctrl_reg6 reg6;
	union cx2072x_reg_digital_bios_test2 regdbt2;
	const unsigned int fmt = cx2072x->dai_fmt;

	if (frame_len <= 0) {
		dev_err(dev, "Incorrect frame len %d\n", frame_len);
		return -EINVAL;
	}

	if (sample_size <= 0) {
		dev_err(dev, "Incorrect sample size %d\n", sample_size);
		return -EINVAL;
	}

	dev_dbg(dev, "config_i2spcm set_dai_fmt- %08x\n", fmt);

	regdbt2.ulval = 0xac;

	/* set master/slave */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		reg2.r.tx_master = 1;
		reg3.r.rx_master = 1;
		dev_dbg(dev, "Sets Master mode\n");
		break;

	case SND_SOC_DAIFMT_CBS_CFS:
		reg2.r.tx_master = 0;
		reg3.r.rx_master = 0;
		dev_dbg(dev, "Sets Slave mode\n");
		break;

	default:
		dev_err(dev, "Unsupported DAI master mode\n");
		return -EINVAL;
	}

	/* set format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		is_i2s = 1;
		has_one_bit_delay = 1;
		pulse_len = frame_len / 2;
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
		is_i2s = 1;
		pulse_len = frame_len / 2;
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		is_i2s = 1;
		pulse_len = frame_len / 2;
		break;

	default:
		dev_err(dev, "Unsupported DAI format\n");
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		is_frame_inv = is_i2s;
		is_bclk_inv = is_i2s;
		break;

	case SND_SOC_DAIFMT_IB_IF:
		is_frame_inv = !is_i2s;
		is_bclk_inv = !is_i2s;
		break;

	case SND_SOC_DAIFMT_IB_NF:
		is_frame_inv = is_i2s;
		is_bclk_inv = !is_i2s;
		break;

	case SND_SOC_DAIFMT_NB_IF:
		is_frame_inv = !is_i2s;
		is_bclk_inv = is_i2s;
		break;

	default:
		dev_err(dev, "Unsupported DAI clock inversion\n");
		return -EINVAL;
	}

	reg1.r.rx_data_one_line = 1;
	reg1.r.tx_data_one_line = 1;

	if (is_i2s) {
		i2s_right_slot = (frame_len / 2) / BITS_PER_SLOT;
		i2s_right_pause_interval = (frame_len / 2) % BITS_PER_SLOT;
		i2s_right_pause_pos = i2s_right_slot * BITS_PER_SLOT;
	}

	reg1.r.rx_ws_pol = is_frame_inv;
	reg1.r.rx_ws_wid = pulse_len - 1;

	reg1.r.rx_frm_len = frame_len / BITS_PER_SLOT - 1;
	reg1.r.rx_sa_size = (sample_size / BITS_PER_SLOT) - 1;

	reg1.r.tx_ws_pol = reg1.r.rx_ws_pol;
	reg1.r.tx_ws_wid = pulse_len - 1;
	reg1.r.tx_frm_len = reg1.r.rx_frm_len;
	reg1.r.tx_sa_size = reg1.r.rx_sa_size;

	reg2.r.tx_endian_sel = !is_big_endian;
	reg2.r.tx_dstart_dly = has_one_bit_delay;
	if (cx2072x->en_aec_ref)
		reg2.r.tx_dstart_dly = 0;

	reg3.r.rx_endian_sel = !is_big_endian;
	reg3.r.rx_dstart_dly = has_one_bit_delay;

	reg4.ulval = 0;

	if (is_i2s) {
		reg2.r.tx_slot_1 = 0;
		reg2.r.tx_slot_2 = i2s_right_slot;
		reg3.r.rx_slot_1 = 0;
		if (cx2072x->en_aec_ref)
			reg3.r.rx_slot_2 = 0;
		else
			reg3.r.rx_slot_2 = i2s_right_slot;
		reg6.r.rx_pause_start_pos = i2s_right_pause_pos;
		reg6.r.rx_pause_cycles = i2s_right_pause_interval;
		reg6.r.tx_pause_start_pos = i2s_right_pause_pos;
		reg6.r.tx_pause_cycles = i2s_right_pause_interval;
	} else {
		dev_err(dev, "TDM mode is not implemented yet\n");
		return -EINVAL;
	}
	regdbt2.r.i2s_bclk_invert = is_bclk_inv;

	reg1.r.rx_data_one_line = 1;
	reg1.r.tx_data_one_line = 1;

	/* Configures the BCLK output */
	bclk_rate = cx2072x->sample_rate * frame_len;
	reg5.r.i2s_pcm_clk_div_chan_en = 0;

	/* Disables bclk output before setting new value */
	regmap_write(cx2072x->regmap, CX2072X_I2SPCM_CONTROL5, 0);

	if (reg2.r.tx_master) {
		/* Configures BCLK rate */
		div = PLL_OUT_HZ_48;
		mod = do_div(div, bclk_rate);
		if (mod) {
			dev_err(dev, "Unsupported BCLK %dHz\n", bclk_rate);
			return -EINVAL;
		}
		dev_dbg(dev, "enables BCLK %dHz output\n", bclk_rate);
		reg5.r.i2s_pcm_clk_div = (u32)div - 1;
		reg5.r.i2s_pcm_clk_div_chan_en = 1;
	}

	regmap_write(cx2072x->regmap, CX2072X_I2SPCM_CONTROL1, reg1.ulval);
	regmap_update_bits(cx2072x->regmap, CX2072X_I2SPCM_CONTROL2, 0xffffffc0,
			   reg2.ulval);
	regmap_update_bits(cx2072x->regmap, CX2072X_I2SPCM_CONTROL3, 0xffffffc0,
			   reg3.ulval);
	regmap_write(cx2072x->regmap, CX2072X_I2SPCM_CONTROL4, reg4.ulval);
	regmap_write(cx2072x->regmap, CX2072X_I2SPCM_CONTROL6, reg6.ulval);
	regmap_write(cx2072x->regmap, CX2072X_I2SPCM_CONTROL5, reg5.ulval);

	regmap_write(cx2072x->regmap, CX2072X_DIGITAL_BIOS_TEST2,
		     regdbt2.ulval);

	return 0;
}

static int afg_power_ev(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(cx2072x->regmap, CX2072X_DIGITAL_BIOS_TEST0,
				   0x00, 0x10);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(cx2072x->regmap, CX2072X_DIGITAL_BIOS_TEST0,
				   0x10, 0x10);
		break;
	}

	return 0;
}

static const struct snd_kcontrol_new cx2072x_snd_controls[] = {
	SOC_DOUBLE_R_TLV("PortD Boost Volume", CX2072X_PORTD_GAIN_LEFT,
			 CX2072X_PORTD_GAIN_RIGHT, 0, 3, 0, boost_tlv),
	SOC_DOUBLE_R_TLV("PortC Boost Volume", CX2072X_PORTC_GAIN_LEFT,
			 CX2072X_PORTC_GAIN_RIGHT, 0, 3, 0, boost_tlv),
	SOC_DOUBLE_R_TLV("PortB Boost Volume", CX2072X_PORTB_GAIN_LEFT,
			 CX2072X_PORTB_GAIN_RIGHT, 0, 3, 0, boost_tlv),
	SOC_DOUBLE_R_TLV("PortD ADC1 Volume", CX2072X_ADC1_AMP_GAIN_LEFT_1,
			 CX2072X_ADC1_AMP_GAIN_RIGHT_1, 0, 0x4a, 0, adc_tlv),
	SOC_DOUBLE_R_TLV("PortC ADC1 Volume", CX2072X_ADC1_AMP_GAIN_LEFT_2,
			 CX2072X_ADC1_AMP_GAIN_RIGHT_2, 0, 0x4a, 0, adc_tlv),
	SOC_DOUBLE_R_TLV("PortB ADC1 Volume", CX2072X_ADC1_AMP_GAIN_LEFT_0,
			 CX2072X_ADC1_AMP_GAIN_RIGHT_0, 0, 0x4a, 0, adc_tlv),
	SOC_DOUBLE_R_TLV("DAC1 Volume", CX2072X_DAC1_AMP_GAIN_LEFT,
			 CX2072X_DAC1_AMP_GAIN_RIGHT, 0, 0x4a, 0, dac_tlv),
	SOC_DOUBLE_R("DAC1 Switch", CX2072X_DAC1_AMP_GAIN_LEFT,
		     CX2072X_DAC1_AMP_GAIN_RIGHT, 7,  1, 0),
	SOC_DOUBLE_R_TLV("DAC2 Volume", CX2072X_DAC2_AMP_GAIN_LEFT,
			 CX2072X_DAC2_AMP_GAIN_RIGHT, 0, 0x4a, 0, dac_tlv),
	SOC_SINGLE_TLV("HPF Freq", CX2072X_CODEC_TEST9, 0, 0x3f, 0, hpf_tlv),
	SOC_DOUBLE("HPF Switch", CX2072X_CODEC_TEST9, 8, 9, 1, 1),
	SOC_SINGLE("PortA HP Amp Switch", CX2072X_PORTA_PIN_CTRL, 7, 1, 0),
};

static int cx2072x_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *codec = dai->component;
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);
	struct device *dev = codec->dev;
	const unsigned int sample_rate = params_rate(params);
	int sample_size, frame_size;

	/* Data sizes if not using TDM */
	sample_size = params_width(params);

	if (sample_size < 0)
		return sample_size;

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0)
		return frame_size;

	if (cx2072x->mclk_rate == 0) {
		dev_err(dev, "Master clock rate is not configured\n");
		return -EINVAL;
	}

	if (cx2072x->bclk_ratio)
		frame_size = cx2072x->bclk_ratio;

	switch (sample_rate) {
	case 48000:
	case 32000:
	case 24000:
	case 16000:
	case 96000:
	case 192000:
		break;

	default:
		dev_err(dev, "Unsupported sample rate %d\n", sample_rate);
		return -EINVAL;
	}

	dev_dbg(dev, "Sample size %d bits, frame = %d bits, rate = %d Hz\n",
		sample_size, frame_size, sample_rate);

	cx2072x->frame_size = frame_size;
	cx2072x->sample_size = sample_size;
	cx2072x->sample_rate = sample_rate;

	if (dai->id == CX2072X_DAI_DSP) {
		cx2072x->en_aec_ref = true;
		dev_dbg(cx2072x->dev, "enables aec reference\n");
		regmap_write(cx2072x->regmap,
			     CX2072X_ADC1_CONNECTION_SELECT_CONTROL, 3);
	}

	if (cx2072x->pll_changed) {
		cx2072x_config_pll(cx2072x);
		cx2072x->pll_changed = false;
	}

	if (cx2072x->i2spcm_changed) {
		cx2072x_config_i2spcm(cx2072x);
		cx2072x->i2spcm_changed = false;
	}

	return 0;
}

static int cx2072x_set_dai_bclk_ratio(struct snd_soc_dai *dai,
				      unsigned int ratio)
{
	struct snd_soc_component *codec = dai->component;
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);

	cx2072x->bclk_ratio = ratio;
	return 0;
}

static int cx2072x_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				  unsigned int freq, int dir)
{
	struct snd_soc_component *codec = dai->component;
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);

	if (clk_set_rate(cx2072x->mclk, freq)) {
		dev_err(codec->dev, "set clk rate failed\n");
		return -EINVAL;
	}

	cx2072x->mclk_rate = freq;
	return 0;
}

static int cx2072x_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *codec = dai->component;
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);
	struct device *dev = codec->dev;

	dev_dbg(dev, "set_dai_fmt- %08x\n", fmt);
	/* set master/slave */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_CBS_CFS:
		break;

	default:
		dev_err(dev, "Unsupported DAI master mode\n");
		return -EINVAL;
	}

	/* set format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		break;

	default:
		dev_err(dev, "Unsupported DAI format\n");
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
	case SND_SOC_DAIFMT_IB_IF:
	case SND_SOC_DAIFMT_IB_NF:
	case SND_SOC_DAIFMT_NB_IF:
		break;

	default:
		dev_err(dev, "Unsupported DAI clock inversion\n");
		return -EINVAL;
	}

	cx2072x->dai_fmt = fmt;
	return 0;
}

static const struct snd_kcontrol_new portaouten_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_PORTA_PIN_CTRL, 6, 1, 0);

static const struct snd_kcontrol_new porteouten_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_PORTE_PIN_CTRL, 6, 1, 0);

static const struct snd_kcontrol_new portgouten_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_PORTG_PIN_CTRL, 6, 1, 0);

static const struct snd_kcontrol_new portmouten_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_PORTM_PIN_CTRL, 6, 1, 0);

static const struct snd_kcontrol_new portbinen_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_PORTB_PIN_CTRL, 5, 1, 0);

static const struct snd_kcontrol_new portcinen_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_PORTC_PIN_CTRL, 5, 1, 0);

static const struct snd_kcontrol_new portdinen_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_PORTD_PIN_CTRL, 5, 1, 0);

static const struct snd_kcontrol_new porteinen_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_PORTE_PIN_CTRL, 5, 1, 0);

static const struct snd_kcontrol_new i2sadc1l_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_I2SPCM_CONTROL2, 0, 1, 0);

static const struct snd_kcontrol_new i2sadc1r_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_I2SPCM_CONTROL2, 1, 1, 0);

static const struct snd_kcontrol_new i2sadc2l_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_I2SPCM_CONTROL2, 2, 1, 0);

static const struct snd_kcontrol_new i2sadc2r_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_I2SPCM_CONTROL2, 3, 1, 0);

static const struct snd_kcontrol_new i2sdac1l_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_I2SPCM_CONTROL3, 0, 1, 0);

static const struct snd_kcontrol_new i2sdac1r_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_I2SPCM_CONTROL3, 1, 1, 0);

static const struct snd_kcontrol_new i2sdac2l_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_I2SPCM_CONTROL3, 2, 1, 0);

static const struct snd_kcontrol_new i2sdac2r_ctl =
	SOC_DAPM_SINGLE("Switch", CX2072X_I2SPCM_CONTROL3, 3, 1, 0);

static const char * const dac_enum_text[] = {
	"DAC1 Switch", "DAC2 Switch",
};

static const struct soc_enum porta_dac_enum =
SOC_ENUM_SINGLE(CX2072X_PORTA_CONNECTION_SELECT_CTRL, 0, 2, dac_enum_text);

static const struct snd_kcontrol_new porta_mux =
SOC_DAPM_ENUM("PortA Mux", porta_dac_enum);

static const struct soc_enum portg_dac_enum =
SOC_ENUM_SINGLE(CX2072X_PORTG_CONNECTION_SELECT_CTRL, 0, 2, dac_enum_text);

static const struct snd_kcontrol_new portg_mux =
SOC_DAPM_ENUM("PortG Mux", portg_dac_enum);

static const struct soc_enum porte_dac_enum =
SOC_ENUM_SINGLE(CX2072X_PORTE_CONNECTION_SELECT_CTRL, 0, 2, dac_enum_text);

static const struct snd_kcontrol_new porte_mux =
SOC_DAPM_ENUM("PortE Mux", porte_dac_enum);

static const struct soc_enum portm_dac_enum =
SOC_ENUM_SINGLE(CX2072X_PORTM_CONNECTION_SELECT_CTRL, 0, 2, dac_enum_text);

static const struct snd_kcontrol_new portm_mux =
SOC_DAPM_ENUM("PortM Mux", portm_dac_enum);

static const char * const adc1in_sel_text[] = {
	"PortB Switch", "PortD Switch", "PortC Switch", "Widget15 Switch",
	"PortE Switch", "PortF Switch", "PortH Switch"
};

static const struct soc_enum adc1in_sel_enum =
SOC_ENUM_SINGLE(CX2072X_ADC1_CONNECTION_SELECT_CONTROL, 0, 7, adc1in_sel_text);

static const struct snd_kcontrol_new adc1_mux =
SOC_DAPM_ENUM("ADC1 Mux", adc1in_sel_enum);

static const char * const adc2in_sel_text[] = {
	"PortC Switch", "Widget15 Switch", "PortH Switch"
};

static const struct soc_enum adc2in_sel_enum =
SOC_ENUM_SINGLE(CX2072X_ADC2_CONNECTION_SELECT_CONTROL, 0, 3, adc2in_sel_text);

static const struct snd_kcontrol_new adc2_mux =
SOC_DAPM_ENUM("ADC2 Mux", adc2in_sel_enum);

static const struct snd_kcontrol_new wid15_mix[] = {
	SOC_DAPM_SINGLE("DAC1L Switch", CX2072X_MIXER_GAIN_LEFT_0, 7, 1, 1),
	SOC_DAPM_SINGLE("DAC1R Switch", CX2072X_MIXER_GAIN_RIGHT_0, 7, 1, 1),
	SOC_DAPM_SINGLE("DAC2L Switch", CX2072X_MIXER_GAIN_LEFT_1, 7, 1, 1),
	SOC_DAPM_SINGLE("DAC2R Switch", CX2072X_MIXER_GAIN_RIGHT_1, 7, 1, 1),
};

#define CX2072X_DAPM_SUPPLY_S(wname, wsubseq, wreg, wshift, wmask,  won_val, \
	woff_val, wevent, wflags) \
	{.id = snd_soc_dapm_supply, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = wreg, .shift = wshift, .mask = wmask, \
	.on_val = won_val, .off_val = woff_val, \
	.subseq = wsubseq, .event = wevent, .event_flags = wflags}

#define CX2072X_DAPM_SWITCH(wname,  wreg, wshift, wmask,  won_val, woff_val, \
	wevent, wflags) \
	{.id = snd_soc_dapm_switch, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = wreg, .shift = wshift, .mask = wmask, \
	.on_val = won_val, .off_val = woff_val, \
	.event = wevent, .event_flags = wflags}

#define CX2072X_DAPM_SWITCH(wname,  wreg, wshift, wmask,  won_val, woff_val, \
	wevent, wflags) \
	{.id = snd_soc_dapm_switch, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = wreg, .shift = wshift, .mask = wmask, \
	.on_val = won_val, .off_val = woff_val, \
	.event = wevent, .event_flags = wflags}

#define CX2072X_DAPM_REG_E(wid, wname, wreg, wshift, wmask, won_val, woff_val, \
				wevent, wflags) \
	{.id = wid, .name = wname, .kcontrol_news = NULL, .num_kcontrols = 0, \
	.reg = wreg, .shift = wshift, .mask = wmask, \
	.on_val = won_val, .off_val = woff_val, \
	.event = wevent, .event_flags = wflags}

static const struct snd_soc_dapm_widget cx2072x_dapm_widgets[] = {
	/*Playback*/
	SND_SOC_DAPM_AIF_IN("In AIF", "Playback", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SWITCH("I2S DAC1L", SND_SOC_NOPM, 0, 0, &i2sdac1l_ctl),
	SND_SOC_DAPM_SWITCH("I2S DAC1R", SND_SOC_NOPM, 0, 0, &i2sdac1r_ctl),
	SND_SOC_DAPM_SWITCH("I2S DAC2L", SND_SOC_NOPM, 0, 0, &i2sdac2l_ctl),
	SND_SOC_DAPM_SWITCH("I2S DAC2R", SND_SOC_NOPM, 0, 0, &i2sdac2r_ctl),

	SND_SOC_DAPM_REG(snd_soc_dapm_dac, "DAC1", CX2072X_DAC1_POWER_STATE,
			 0, 0xfff, 0x00, 0x03),

	SND_SOC_DAPM_REG(snd_soc_dapm_dac, "DAC2", CX2072X_DAC2_POWER_STATE,
			 0, 0xfff, 0x00, 0x03),

	SND_SOC_DAPM_MUX("PortA Mux", SND_SOC_NOPM, 0, 0, &porta_mux),
	SND_SOC_DAPM_MUX("PortG Mux", SND_SOC_NOPM, 0, 0, &portg_mux),
	SND_SOC_DAPM_MUX("PortE Mux", SND_SOC_NOPM, 0, 0, &porte_mux),
	SND_SOC_DAPM_MUX("PortM Mux", SND_SOC_NOPM, 0, 0, &portm_mux),

	SND_SOC_DAPM_REG(snd_soc_dapm_supply, "PortA Power",
			 CX2072X_PORTA_POWER_STATE, 0, 0xfff, 0x00, 0x03),

	SND_SOC_DAPM_REG(snd_soc_dapm_supply, "PortM Power",
			 CX2072X_PORTM_POWER_STATE, 0, 0xfff, 0x00, 0x03),

	SND_SOC_DAPM_REG(snd_soc_dapm_supply, "PortG Power",
			 CX2072X_PORTG_POWER_STATE, 0, 0xfff, 0x00, 0x03),

	CX2072X_DAPM_SUPPLY_S("AFG Power", 0, CX2072X_AFG_POWER_STATE,
			      0, 0xfff, 0x00, 0x03, afg_power_ev,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SWITCH("PortA Out En", SND_SOC_NOPM, 0, 0,
			    &portaouten_ctl),
	SND_SOC_DAPM_SWITCH("PortE Out En", SND_SOC_NOPM, 0, 0,
			    &porteouten_ctl),
	SND_SOC_DAPM_SWITCH("PortG Out En", SND_SOC_NOPM, 0, 0,
			    &portgouten_ctl),
	SND_SOC_DAPM_SWITCH("PortM Out En", SND_SOC_NOPM, 0, 0,
			    &portmouten_ctl),

	SND_SOC_DAPM_OUTPUT("PORTA"),
	SND_SOC_DAPM_OUTPUT("PORTG"),
	SND_SOC_DAPM_OUTPUT("PORTE"),
	SND_SOC_DAPM_OUTPUT("PORTM"),
	SND_SOC_DAPM_OUTPUT("AEC REF"),

	/*Capture*/
	SND_SOC_DAPM_AIF_OUT("Out AIF", "Capture", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SWITCH("I2S ADC1L", SND_SOC_NOPM, 0, 0, &i2sadc1l_ctl),
	SND_SOC_DAPM_SWITCH("I2S ADC1R", SND_SOC_NOPM, 0, 0, &i2sadc1r_ctl),
	SND_SOC_DAPM_SWITCH("I2S ADC2L", SND_SOC_NOPM, 0, 0, &i2sadc2l_ctl),
	SND_SOC_DAPM_SWITCH("I2S ADC2R", SND_SOC_NOPM, 0, 0, &i2sadc2r_ctl),

	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC1", CX2072X_ADC1_POWER_STATE,
			 0, 0xff, 0x00, 0x03),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC2", CX2072X_ADC2_POWER_STATE,
			 0, 0xff, 0x00, 0x03),

	SND_SOC_DAPM_MUX("ADC1 Mux", SND_SOC_NOPM, 0, 0, &adc1_mux),
	SND_SOC_DAPM_MUX("ADC2 Mux", SND_SOC_NOPM, 0, 0, &adc2_mux),

	SND_SOC_DAPM_REG(snd_soc_dapm_supply, "PortB Power",
			 CX2072X_PORTB_POWER_STATE, 0, 0xfff, 0x00, 0x03),
	SND_SOC_DAPM_REG(snd_soc_dapm_supply, "PortC Power",
			 CX2072X_PORTC_POWER_STATE, 0, 0xfff, 0x00, 0x03),
	SND_SOC_DAPM_REG(snd_soc_dapm_supply, "PortD Power",
			 CX2072X_PORTD_POWER_STATE, 0, 0xfff, 0x00, 0x03),
	SND_SOC_DAPM_REG(snd_soc_dapm_supply, "PortE Power",
			 CX2072X_PORTE_POWER_STATE, 0, 0xfff, 0x00, 0x03),
	SND_SOC_DAPM_REG(snd_soc_dapm_supply, "Widget15 Power",
			 CX2072X_MIXER_POWER_STATE, 0, 0xfff, 0x00, 0x03),

	SND_SOC_DAPM_MIXER("Widget15 Mixer", SND_SOC_NOPM, 0, 0,
			   wid15_mix, ARRAY_SIZE(wid15_mix)),
	SND_SOC_DAPM_SWITCH("PortB In En", SND_SOC_NOPM, 0, 0, &portbinen_ctl),
	SND_SOC_DAPM_SWITCH("PortC In En", SND_SOC_NOPM, 0, 0, &portcinen_ctl),
	SND_SOC_DAPM_SWITCH("PortD In En", SND_SOC_NOPM, 0, 0, &portdinen_ctl),
	SND_SOC_DAPM_SWITCH("PortE In En", SND_SOC_NOPM, 0, 0, &porteinen_ctl),

	SND_SOC_DAPM_MICBIAS("Headset Bias", CX2072X_ANALOG_TEST11, 1, 0),
	SND_SOC_DAPM_MICBIAS("PortB Mic Bias", CX2072X_PORTB_PIN_CTRL, 2, 0),
	SND_SOC_DAPM_MICBIAS("PortD Mic Bias", CX2072X_PORTD_PIN_CTRL, 2, 0),
	SND_SOC_DAPM_MICBIAS("PortE Mic Bias", CX2072X_PORTE_PIN_CTRL, 2, 0),
	SND_SOC_DAPM_INPUT("PORTB"),
	SND_SOC_DAPM_INPUT("PORTC"),
	SND_SOC_DAPM_INPUT("PORTD"),
	SND_SOC_DAPM_INPUT("PORTEIN"),

};

static const struct snd_soc_dapm_route cx2072x_intercon[] = {
	/* Playback */
	{"In AIF", NULL, "AFG Power"},
	{"I2S DAC1L", "Switch", "In AIF"},
	{"I2S DAC1R", "Switch", "In AIF"},
	{"I2S DAC2L", "Switch", "In AIF"},
	{"I2S DAC2R", "Switch", "In AIF"},
	{"DAC1", NULL, "I2S DAC1L"},
	{"DAC1", NULL, "I2S DAC1R"},
	{"DAC2", NULL, "I2S DAC2L"},
	{"DAC2", NULL, "I2S DAC2R"},
	{"PortA Mux", "DAC1 Switch", "DAC1"},
	{"PortA Mux", "DAC2 Switch", "DAC2"},
	{"PortG Mux", "DAC1 Switch", "DAC1"},
	{"PortG Mux", "DAC2 Switch", "DAC2"},
	{"PortE Mux", "DAC1 Switch", "DAC1"},
	{"PortE Mux", "DAC2 Switch", "DAC2"},
	{"PortM Mux", "DAC1 Switch", "DAC1"},
	{"PortM Mux", "DAC2 Switch", "DAC2"},
	{"Widget15 Mixer", "DAC1L Switch", "DAC1"},
	{"Widget15 Mixer", "DAC1R Switch", "DAC2"},
	{"Widget15 Mixer", "DAC2L Switch", "DAC1"},
	{"Widget15 Mixer", "DAC2R Switch", "DAC2"},
	{"Widget15 Mixer", NULL, "Widget15 Power"},
	{"PortA Out En", "Switch", "PortA Mux"},
	{"PortG Out En", "Switch", "PortG Mux"},
	{"PortE Out En", "Switch", "PortE Mux"},
	{"PortM Out En", "Switch", "PortM Mux"},
	{"PortA Mux", NULL, "PortA Power"},
	{"PortG Mux", NULL, "PortG Power"},
	{"PortE Mux", NULL, "PortE Power"},
	{"PortM Mux", NULL, "PortM Power"},
	{"PortA Out En", NULL, "PortA Power"},
	{"PortG Out En", NULL, "PortG Power"},
	{"PortE Out En", NULL, "PortE Power"},
	{"PortM Out En", NULL, "PortM Power"},
	{"PORTA", NULL, "PortA Out En"},
	{"PORTG", NULL, "PortG Out En"},
	{"PORTE", NULL, "PortE Out En"},
	{"PORTM", NULL, "PortM Out En"},

	/* Capture */
	{"PORTD", NULL, "Headset Bias"},
	{"PortB In En", "Switch", "PORTB"},
	{"PortC In En", "Switch", "PORTC"},
	{"PortD In En", "Switch", "PORTD"},
	{"PortE In En", "Switch", "PORTEIN"},
	{"ADC1 Mux", "PortB Switch", "PortB In En"},
	{"ADC1 Mux", "PortC Switch", "PortC In En"},
	{"ADC1 Mux", "PortD Switch", "PortD In En"},
	{"ADC1 Mux", "PortE Switch", "PortE In En"},
	{"ADC1 Mux", "Widget15 Switch", "Widget15 Mixer"},
	{"ADC2 Mux", "PortC Switch", "PortC In En"},
	{"ADC2 Mux", "Widget15 Switch", "Widget15 Mixer"},
	{"ADC1", NULL, "ADC1 Mux"},
	{"ADC2", NULL, "ADC2 Mux"},
	{"I2S ADC1L", "Switch", "ADC1"},
	{"I2S ADC1R", "Switch", "ADC1"},
	{"I2S ADC2L", "Switch", "ADC2"},
	{"I2S ADC2R", "Switch", "ADC2"},
	{"Out AIF", NULL, "I2S ADC1L"},
	{"Out AIF", NULL, "I2S ADC1R"},
	{"Out AIF", NULL, "I2S ADC2L"},
	{"Out AIF", NULL, "I2S ADC2R"},
	{"Out AIF", NULL, "AFG Power"},
	{"AEC REF", NULL, "Out AIF"},
	{"PortB In En", NULL, "PortB Power"},
	{"PortC In En", NULL, "PortC Power"},
	{"PortD In En", NULL, "PortD Power"},
	{"PortE In En", NULL, "PortE Power"},
};

static int cx2072x_set_bias_level(struct snd_soc_component *codec,
				  enum snd_soc_bias_level level)
{
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);
	const enum snd_soc_bias_level old_level =
		snd_soc_component_get_bias_level(codec);

	if (level == SND_SOC_BIAS_STANDBY && old_level == SND_SOC_BIAS_OFF)
		regmap_write(cx2072x->regmap, CX2072X_AFG_POWER_STATE, 0);
	else if (level == SND_SOC_BIAS_OFF && old_level != SND_SOC_BIAS_OFF)
		regmap_write(cx2072x->regmap, CX2072X_AFG_POWER_STATE, 3);

	return 0;
}

/*
 * FIXME: the whole jack detection code below is pretty platform-specific;
 * it has lots of implicit assumptions about the pins, etc.
 * However, since we have no other code and reference, take this hard-coded
 * setup for now.  Once when we have different platform implementations,
 * this needs to be rewritten in a more generic form, or moving into the
 * platform data.
 */
static void cx2072x_enable_jack_detect(struct snd_soc_component *codec)
{
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(codec);

	/* No-sticky input type */
	regmap_write(cx2072x->regmap, CX2072X_GPIO_STICKY_MASK, 0x1f);

	/* Use GPOI0 as interrupt pin */
	regmap_write(cx2072x->regmap, CX2072X_UM_INTERRUPT_CRTL_E, 0x12 << 24);

	/* Enables unsolitited message on PortA */
	regmap_write(cx2072x->regmap, CX2072X_PORTA_UNSOLICITED_RESPONSE, 0x80);

	/* support both nokia and apple headset set. Monitor time = 275 ms */
	regmap_write(cx2072x->regmap, CX2072X_DIGITAL_TEST15, 0x73);

	/* Disable TIP detection */
	regmap_write(cx2072x->regmap, CX2072X_ANALOG_TEST12, 0x300);

	/* Switch MusicD3Live pin to GPIO */
	regmap_write(cx2072x->regmap, CX2072X_DIGITAL_TEST1, 0);

	snd_soc_dapm_mutex_lock(dapm);

	snd_soc_dapm_force_enable_pin_unlocked(dapm, "PORTD");
	snd_soc_dapm_force_enable_pin_unlocked(dapm, "Headset Bias");
	snd_soc_dapm_force_enable_pin_unlocked(dapm, "PortD Mic Bias");

	snd_soc_dapm_mutex_unlock(dapm);
}

static void cx2072x_disable_jack_detect(struct snd_soc_component *codec)
{
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);

	regmap_write(cx2072x->regmap, CX2072X_UM_INTERRUPT_CRTL_E, 0);
	regmap_write(cx2072x->regmap, CX2072X_PORTA_UNSOLICITED_RESPONSE, 0);
}

static int cx2072x_jack_status_check(void *data)
{
	struct snd_soc_component *codec = data;
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);
	unsigned int jack;
	unsigned int type = 0;
	int state = 0;

	mutex_lock(&cx2072x->lock);

	regmap_read(cx2072x->regmap, CX2072X_PORTA_PIN_SENSE, &jack);
	jack = jack >> 24;
	regmap_read(cx2072x->regmap, CX2072X_DIGITAL_TEST11, &type);

	if (jack == 0x80) {
		type = type >> 8;

		if (type & 0x8) {
			/* Apple headset */
			state |= SND_JACK_HEADSET;
			if (type & 0x2)
				state |= SND_JACK_BTN_0;
		} else if (type & 0x4) {
			/* Nokia headset */
			state |= SND_JACK_HEADPHONE;
		} else {
			/* Headphone */
			state |= SND_JACK_HEADPHONE;
		}
	}

	/* clear interrupt */
	regmap_write(cx2072x->regmap, CX2072X_UM_INTERRUPT_CRTL_E, 0x12 << 24);

	mutex_unlock(&cx2072x->lock);

	dev_dbg(codec->dev, "CX2072X_HSDETECT type=0x%X,Jack state = %x\n",
		type, state);
	return state;
}

static const struct snd_soc_jack_gpio cx2072x_jack_gpio = {
	.name = "headset",
	.report = SND_JACK_HEADSET | SND_JACK_BTN_0,
	.debounce_time = 150,
	.wake = true,
	.jack_status_check = cx2072x_jack_status_check,
};

static int cx2072x_set_jack(struct snd_soc_component *codec,
			    struct snd_soc_jack *jack, void *data)
{
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);
	int err;

	if (!jack) {
		cx2072x_disable_jack_detect(codec);
		return 0;
	}

	if (!cx2072x->jack_gpio.gpiod_dev) {
		cx2072x->jack_gpio = cx2072x_jack_gpio;
		cx2072x->jack_gpio.gpiod_dev = codec->dev;
		cx2072x->jack_gpio.data = codec;
		err = snd_soc_jack_add_gpios(jack, 1, &cx2072x->jack_gpio);
		if (err) {
			cx2072x->jack_gpio.gpiod_dev = NULL;
			return err;
		}
	}

	cx2072x_enable_jack_detect(codec);
	return 0;
}

static int cx2072x_probe(struct snd_soc_component *codec)
{
	struct cx2072x_priv *cx2072x = snd_soc_component_get_drvdata(codec);

	cx2072x->codec = codec;

	/*
	 * FIXME: below is, again, a very platform-specific init sequence,
	 * but we keep the code here just for simplicity.  It seems that all
	 * existing hardware implementations require this, so there is no very
	 * much reason to move this out of the codec driver to the platform
	 * data.
	 * But of course it's no "right" thing; if you are a good boy, don't
	 * read and follow the code like this!
	 */
	pm_runtime_get_sync(codec->dev);
	regmap_write(cx2072x->regmap, CX2072X_AFG_POWER_STATE, 0);

	regmap_multi_reg_write(cx2072x->regmap, cx2072x_reg_init,
			       ARRAY_SIZE(cx2072x_reg_init));

	/* configure PortC as input device */
	regmap_update_bits(cx2072x->regmap, CX2072X_PORTC_PIN_CTRL,
			   0x20, 0x20);

	regmap_update_bits(cx2072x->regmap, CX2072X_DIGITAL_BIOS_TEST2,
			   0x84, 0xff);

	regmap_write(cx2072x->regmap, CX2072X_AFG_POWER_STATE, 3);
	pm_runtime_put(codec->dev);

	return 0;
}

static const struct snd_soc_component_driver soc_codec_driver_cx2072x = {
	.probe = cx2072x_probe,
	.set_bias_level = cx2072x_set_bias_level,
	.set_jack = cx2072x_set_jack,
	.controls = cx2072x_snd_controls,
	.num_controls = ARRAY_SIZE(cx2072x_snd_controls),
	.dapm_widgets = cx2072x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cx2072x_dapm_widgets),
	.dapm_routes = cx2072x_intercon,
	.num_dapm_routes = ARRAY_SIZE(cx2072x_intercon),
};

/*
 * DAI ops
 */
static struct snd_soc_dai_ops cx2072x_dai_ops = {
	.set_sysclk = cx2072x_set_dai_sysclk,
	.set_fmt = cx2072x_set_dai_fmt,
	.hw_params = cx2072x_hw_params,
	.set_bclk_ratio = cx2072x_set_dai_bclk_ratio,
};

static int cx2072x_dsp_dai_probe(struct snd_soc_dai *dai)
{
	struct cx2072x_priv *cx2072x =
		snd_soc_component_get_drvdata(dai->component);

	cx2072x->en_aec_ref = true;
	return 0;
}

#define CX2072X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver soc_codec_cx2072x_dai[] = {
	{ /* playback and capture */
		.name = "cx2072x-hifi",
		.id	= CX2072X_DAI_HIFI,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CX2072X_RATES_DSP,
			.formats = CX2072X_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CX2072X_RATES_DSP,
			.formats = CX2072X_FORMATS,
		},
		.ops = &cx2072x_dai_ops,
		.symmetric_rate = 1,
	},
	{ /* plabayck only, return echo reference to Conexant DSP chip */
		.name = "cx2072x-dsp",
		.id	= CX2072X_DAI_DSP,
		.probe = cx2072x_dsp_dai_probe,
		.playback = {
			.stream_name = "DSP Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = CX2072X_RATES_DSP,
			.formats = CX2072X_FORMATS,
		},
		.ops = &cx2072x_dai_ops,
	},
	{ /* plabayck only, return echo reference through I2S TX */
		.name = "cx2072x-aec",
		.id	= 3,
		.capture = {
			.stream_name = "AEC Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = CX2072X_RATES_DSP,
			.formats = CX2072X_FORMATS,
		},
	},
};

static const struct regmap_config cx2072x_regmap = {
	.reg_bits = 16,
	.val_bits = 32,
	.max_register = CX2072X_REG_MAX,
	.reg_defaults = cx2072x_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cx2072x_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.readable_reg = cx2072x_readable_register,
	.volatile_reg = cx2072x_volatile_register,
	/* Needs custom read/write functions for various register lengths */
	.reg_read = cx2072x_reg_read,
	.reg_write = cx2072x_reg_write,
};

static int __maybe_unused cx2072x_runtime_suspend(struct device *dev)
{
	struct cx2072x_priv *cx2072x = dev_get_drvdata(dev);

	clk_disable_unprepare(cx2072x->mclk);
	return 0;
}

static int __maybe_unused cx2072x_runtime_resume(struct device *dev)
{
	struct cx2072x_priv *cx2072x = dev_get_drvdata(dev);

	return clk_prepare_enable(cx2072x->mclk);
}

static int cx2072x_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct cx2072x_priv *cx2072x;
	unsigned int ven_id, rev_id;
	int ret;

	cx2072x = devm_kzalloc(&i2c->dev, sizeof(struct cx2072x_priv),
			       GFP_KERNEL);
	if (!cx2072x)
		return -ENOMEM;

	cx2072x->regmap = devm_regmap_init(&i2c->dev, NULL, i2c,
					   &cx2072x_regmap);
	if (IS_ERR(cx2072x->regmap))
		return PTR_ERR(cx2072x->regmap);

	mutex_init(&cx2072x->lock);

	i2c_set_clientdata(i2c, cx2072x);

	cx2072x->dev = &i2c->dev;
	cx2072x->pll_changed = true;
	cx2072x->i2spcm_changed = true;
	cx2072x->bclk_ratio = 0;

	cx2072x->mclk = devm_clk_get(cx2072x->dev, "mclk");
	if (IS_ERR(cx2072x->mclk)) {
		dev_err(cx2072x->dev, "Failed to get MCLK\n");
		return PTR_ERR(cx2072x->mclk);
	}

	regmap_read(cx2072x->regmap, CX2072X_VENDOR_ID, &ven_id);
	regmap_read(cx2072x->regmap, CX2072X_REVISION_ID, &rev_id);

	dev_info(cx2072x->dev, "codec version: %08x,%08x\n", ven_id, rev_id);

	ret = devm_snd_soc_register_component(cx2072x->dev,
					      &soc_codec_driver_cx2072x,
					      soc_codec_cx2072x_dai,
					      ARRAY_SIZE(soc_codec_cx2072x_dai));
	if (ret < 0)
		return ret;

	pm_runtime_use_autosuspend(cx2072x->dev);
	pm_runtime_enable(cx2072x->dev);

	return 0;
}

static int cx2072x_i2c_remove(struct i2c_client *i2c)
{
	pm_runtime_disable(&i2c->dev);
	return 0;
}

static const struct i2c_device_id cx2072x_i2c_id[] = {
	{ "cx20721", 0 },
	{ "cx20723", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, cx2072x_i2c_id);

#ifdef CONFIG_ACPI
static struct acpi_device_id cx2072x_acpi_match[] = {
	{ "14F10720", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, cx2072x_acpi_match);
#endif

static const struct dev_pm_ops cx2072x_runtime_pm = {
	SET_RUNTIME_PM_OPS(cx2072x_runtime_suspend, cx2072x_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct i2c_driver cx2072x_i2c_driver = {
	.driver = {
		.name = "cx2072x",
		.acpi_match_table = ACPI_PTR(cx2072x_acpi_match),
		.pm = &cx2072x_runtime_pm,
	},
	.probe = cx2072x_i2c_probe,
	.remove = cx2072x_i2c_remove,
	.id_table = cx2072x_i2c_id,
};

module_i2c_driver(cx2072x_i2c_driver);

MODULE_DESCRIPTION("ASoC cx2072x Codec Driver");
MODULE_AUTHOR("Simon Ho <simon.ho@conexant.com>");
MODULE_LICENSE("GPL");

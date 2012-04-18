/*
 * ADAV80X Audio Codec driver supporting ADAV801, ADAV803
 *
 * Copyright 2011 Analog Devices Inc.
 * Author: Yi Li <yi.li@analog.com>
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>

#include "adav80x.h"

#define ADAV80X_PLAYBACK_CTRL	0x04
#define ADAV80X_AUX_IN_CTRL	0x05
#define ADAV80X_REC_CTRL	0x06
#define ADAV80X_AUX_OUT_CTRL	0x07
#define ADAV80X_DPATH_CTRL1	0x62
#define ADAV80X_DPATH_CTRL2	0x63
#define ADAV80X_DAC_CTRL1	0x64
#define ADAV80X_DAC_CTRL2	0x65
#define ADAV80X_DAC_CTRL3	0x66
#define ADAV80X_DAC_L_VOL	0x68
#define ADAV80X_DAC_R_VOL	0x69
#define ADAV80X_PGA_L_VOL	0x6c
#define ADAV80X_PGA_R_VOL	0x6d
#define ADAV80X_ADC_CTRL1	0x6e
#define ADAV80X_ADC_CTRL2	0x6f
#define ADAV80X_ADC_L_VOL	0x70
#define ADAV80X_ADC_R_VOL	0x71
#define ADAV80X_PLL_CTRL1	0x74
#define ADAV80X_PLL_CTRL2	0x75
#define ADAV80X_ICLK_CTRL1	0x76
#define ADAV80X_ICLK_CTRL2	0x77
#define ADAV80X_PLL_CLK_SRC	0x78
#define ADAV80X_PLL_OUTE	0x7a

#define ADAV80X_PLL_CLK_SRC_PLL_XIN(pll)	0x00
#define ADAV80X_PLL_CLK_SRC_PLL_MCLKI(pll)	(0x40 << (pll))
#define ADAV80X_PLL_CLK_SRC_PLL_MASK(pll)	(0x40 << (pll))

#define ADAV80X_ICLK_CTRL1_DAC_SRC(src)		((src) << 5)
#define ADAV80X_ICLK_CTRL1_ADC_SRC(src)		((src) << 2)
#define ADAV80X_ICLK_CTRL1_ICLK2_SRC(src)	(src)
#define ADAV80X_ICLK_CTRL2_ICLK1_SRC(src)	((src) << 3)

#define ADAV80X_PLL_CTRL1_PLLDIV		0x10
#define ADAV80X_PLL_CTRL1_PLLPD(pll)		(0x04 << (pll))
#define ADAV80X_PLL_CTRL1_XTLPD			0x02

#define ADAV80X_PLL_CTRL2_FIELD(pll, x)		((x) << ((pll) * 4))

#define ADAV80X_PLL_CTRL2_FS_48(pll)	ADAV80X_PLL_CTRL2_FIELD((pll), 0x00)
#define ADAV80X_PLL_CTRL2_FS_32(pll)	ADAV80X_PLL_CTRL2_FIELD((pll), 0x08)
#define ADAV80X_PLL_CTRL2_FS_44(pll)	ADAV80X_PLL_CTRL2_FIELD((pll), 0x0c)

#define ADAV80X_PLL_CTRL2_SEL(pll)	ADAV80X_PLL_CTRL2_FIELD((pll), 0x02)
#define ADAV80X_PLL_CTRL2_DOUB(pll)	ADAV80X_PLL_CTRL2_FIELD((pll), 0x01)
#define ADAV80X_PLL_CTRL2_PLL_MASK(pll) ADAV80X_PLL_CTRL2_FIELD((pll), 0x0f)

#define ADAV80X_ADC_CTRL1_MODULATOR_MASK	0x80
#define ADAV80X_ADC_CTRL1_MODULATOR_128FS	0x00
#define ADAV80X_ADC_CTRL1_MODULATOR_64FS	0x80

#define ADAV80X_DAC_CTRL1_PD			0x80

#define ADAV80X_DAC_CTRL2_DIV1			0x00
#define ADAV80X_DAC_CTRL2_DIV1_5		0x10
#define ADAV80X_DAC_CTRL2_DIV2			0x20
#define ADAV80X_DAC_CTRL2_DIV3			0x30
#define ADAV80X_DAC_CTRL2_DIV_MASK		0x30

#define ADAV80X_DAC_CTRL2_INTERPOL_256FS	0x00
#define ADAV80X_DAC_CTRL2_INTERPOL_128FS	0x40
#define ADAV80X_DAC_CTRL2_INTERPOL_64FS		0x80
#define ADAV80X_DAC_CTRL2_INTERPOL_MASK		0xc0

#define ADAV80X_DAC_CTRL2_DEEMPH_NONE		0x00
#define ADAV80X_DAC_CTRL2_DEEMPH_44		0x01
#define ADAV80X_DAC_CTRL2_DEEMPH_32		0x02
#define ADAV80X_DAC_CTRL2_DEEMPH_48		0x03
#define ADAV80X_DAC_CTRL2_DEEMPH_MASK		0x01

#define ADAV80X_CAPTURE_MODE_MASTER		0x20
#define ADAV80X_CAPTURE_WORD_LEN24		0x00
#define ADAV80X_CAPTURE_WORD_LEN20		0x04
#define ADAV80X_CAPTRUE_WORD_LEN18		0x08
#define ADAV80X_CAPTURE_WORD_LEN16		0x0c
#define ADAV80X_CAPTURE_WORD_LEN_MASK		0x0c

#define ADAV80X_CAPTURE_MODE_LEFT_J		0x00
#define ADAV80X_CAPTURE_MODE_I2S		0x01
#define ADAV80X_CAPTURE_MODE_RIGHT_J		0x03
#define ADAV80X_CAPTURE_MODE_MASK		0x03

#define ADAV80X_PLAYBACK_MODE_MASTER		0x10
#define ADAV80X_PLAYBACK_MODE_LEFT_J		0x00
#define ADAV80X_PLAYBACK_MODE_I2S		0x01
#define ADAV80X_PLAYBACK_MODE_RIGHT_J_24	0x04
#define ADAV80X_PLAYBACK_MODE_RIGHT_J_20	0x05
#define ADAV80X_PLAYBACK_MODE_RIGHT_J_18	0x06
#define ADAV80X_PLAYBACK_MODE_RIGHT_J_16	0x07
#define ADAV80X_PLAYBACK_MODE_MASK		0x07

#define ADAV80X_PLL_OUTE_SYSCLKPD(x)		BIT(2 - (x))

static u8 adav80x_default_regs[] = {
	0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x01, 0x80, 0x26, 0x00, 0x00,
	0x02, 0x40, 0x20, 0x00, 0x09, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd1, 0x92, 0xb1, 0x37,
	0x48, 0xd2, 0xfb, 0xca, 0xd2, 0x15, 0xe8, 0x29, 0xb9, 0x6a, 0xda, 0x2b,
	0xb7, 0xc0, 0x11, 0x65, 0x5c, 0xf6, 0xff, 0x8d, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa5, 0x00, 0x00,
	0x00, 0xe8, 0x46, 0xe1, 0x5b, 0xd3, 0x43, 0x77, 0x93, 0xa7, 0x44, 0xee,
	0x32, 0x12, 0xc0, 0x11, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x3f, 0x3f,
	0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x1d, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x52, 0x00,
};

struct adav80x {
	enum snd_soc_control_type control_type;

	enum adav80x_clk_src clk_src;
	unsigned int sysclk;
	enum adav80x_pll_src pll_src;

	unsigned int dai_fmt[2];
	unsigned int rate;
	bool deemph;
	bool sysclk_pd[3];
};

static const char *adav80x_mux_text[] = {
	"ADC",
	"Playback",
	"Aux Playback",
};

static const unsigned int adav80x_mux_values[] = {
	0, 2, 3,
};

#define ADAV80X_MUX_ENUM_DECL(name, reg, shift) \
	SOC_VALUE_ENUM_DOUBLE_DECL(name, reg, shift, 7, \
		ARRAY_SIZE(adav80x_mux_text), adav80x_mux_text, \
		adav80x_mux_values)

static ADAV80X_MUX_ENUM_DECL(adav80x_aux_capture_enum, ADAV80X_DPATH_CTRL1, 0);
static ADAV80X_MUX_ENUM_DECL(adav80x_capture_enum, ADAV80X_DPATH_CTRL1, 3);
static ADAV80X_MUX_ENUM_DECL(adav80x_dac_enum, ADAV80X_DPATH_CTRL2, 3);

static const struct snd_kcontrol_new adav80x_aux_capture_mux_ctrl =
	SOC_DAPM_VALUE_ENUM("Route", adav80x_aux_capture_enum);
static const struct snd_kcontrol_new adav80x_capture_mux_ctrl =
	SOC_DAPM_VALUE_ENUM("Route", adav80x_capture_enum);
static const struct snd_kcontrol_new adav80x_dac_mux_ctrl =
	SOC_DAPM_VALUE_ENUM("Route", adav80x_dac_enum);

#define ADAV80X_MUX(name, ctrl) \
	SND_SOC_DAPM_VALUE_MUX(name, SND_SOC_NOPM, 0, 0, ctrl)

static const struct snd_soc_dapm_widget adav80x_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", NULL, ADAV80X_DAC_CTRL1, 7, 1),
	SND_SOC_DAPM_ADC("ADC", NULL, ADAV80X_ADC_CTRL1, 5, 1),

	SND_SOC_DAPM_PGA("Right PGA", ADAV80X_ADC_CTRL1, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Left PGA", ADAV80X_ADC_CTRL1, 1, 1, NULL, 0),

	SND_SOC_DAPM_AIF_OUT("AIFOUT", "HiFi Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIFIN", "HiFi Playback", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_OUT("AIFAUXOUT", "Aux Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIFAUXIN", "Aux Playback", 0, SND_SOC_NOPM, 0, 0),

	ADAV80X_MUX("Aux Capture Select", &adav80x_aux_capture_mux_ctrl),
	ADAV80X_MUX("Capture Select", &adav80x_capture_mux_ctrl),
	ADAV80X_MUX("DAC Select", &adav80x_dac_mux_ctrl),

	SND_SOC_DAPM_INPUT("VINR"),
	SND_SOC_DAPM_INPUT("VINL"),
	SND_SOC_DAPM_OUTPUT("VOUTR"),
	SND_SOC_DAPM_OUTPUT("VOUTL"),

	SND_SOC_DAPM_SUPPLY("SYSCLK", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL1", ADAV80X_PLL_CTRL1, 2, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2", ADAV80X_PLL_CTRL1, 3, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("OSC", ADAV80X_PLL_CTRL1, 1, 1, NULL, 0),
};

static int adav80x_dapm_sysclk_check(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = source->codec;
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);
	const char *clk;

	switch (adav80x->clk_src) {
	case ADAV80X_CLK_PLL1:
		clk = "PLL1";
		break;
	case ADAV80X_CLK_PLL2:
		clk = "PLL2";
		break;
	case ADAV80X_CLK_XTAL:
		clk = "OSC";
		break;
	default:
		return 0;
	}

	return strcmp(source->name, clk) == 0;
}

static int adav80x_dapm_pll_check(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = source->codec;
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);

	return adav80x->pll_src == ADAV80X_PLL_SRC_XTAL;
}


static const struct snd_soc_dapm_route adav80x_dapm_routes[] = {
	{ "DAC Select", "ADC", "ADC" },
	{ "DAC Select", "Playback", "AIFIN" },
	{ "DAC Select", "Aux Playback", "AIFAUXIN" },
	{ "DAC", NULL,  "DAC Select" },

	{ "Capture Select", "ADC", "ADC" },
	{ "Capture Select", "Playback", "AIFIN" },
	{ "Capture Select", "Aux Playback", "AIFAUXIN" },
	{ "AIFOUT", NULL,  "Capture Select" },

	{ "Aux Capture Select", "ADC", "ADC" },
	{ "Aux Capture Select", "Playback", "AIFIN" },
	{ "Aux Capture Select", "Aux Playback", "AIFAUXIN" },
	{ "AIFAUXOUT", NULL,  "Aux Capture Select" },

	{ "VOUTR",  NULL, "DAC" },
	{ "VOUTL",  NULL, "DAC" },

	{ "Left PGA", NULL, "VINL" },
	{ "Right PGA", NULL, "VINR" },
	{ "ADC", NULL, "Left PGA" },
	{ "ADC", NULL, "Right PGA" },

	{ "SYSCLK", NULL, "PLL1", adav80x_dapm_sysclk_check },
	{ "SYSCLK", NULL, "PLL2", adav80x_dapm_sysclk_check },
	{ "SYSCLK", NULL, "OSC", adav80x_dapm_sysclk_check },
	{ "PLL1", NULL, "OSC", adav80x_dapm_pll_check },
	{ "PLL2", NULL, "OSC", adav80x_dapm_pll_check },

	{ "ADC", NULL, "SYSCLK" },
	{ "DAC", NULL, "SYSCLK" },
	{ "AIFOUT", NULL, "SYSCLK" },
	{ "AIFAUXOUT", NULL, "SYSCLK" },
	{ "AIFIN", NULL, "SYSCLK" },
	{ "AIFAUXIN", NULL, "SYSCLK" },
};

static int adav80x_set_deemph(struct snd_soc_codec *codec)
{
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);
	unsigned int val;

	if (adav80x->deemph) {
		switch (adav80x->rate) {
		case 32000:
			val = ADAV80X_DAC_CTRL2_DEEMPH_32;
			break;
		case 44100:
			val = ADAV80X_DAC_CTRL2_DEEMPH_44;
			break;
		case 48000:
		case 64000:
		case 88200:
		case 96000:
			val = ADAV80X_DAC_CTRL2_DEEMPH_48;
			break;
		default:
			val = ADAV80X_DAC_CTRL2_DEEMPH_NONE;
			break;
		}
	} else {
		val = ADAV80X_DAC_CTRL2_DEEMPH_NONE;
	}

	return snd_soc_update_bits(codec, ADAV80X_DAC_CTRL2,
		ADAV80X_DAC_CTRL2_DEEMPH_MASK, val);
}

static int adav80x_put_deemph(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);
	unsigned int deemph = ucontrol->value.enumerated.item[0];

	if (deemph > 1)
		return -EINVAL;

	adav80x->deemph = deemph;

	return adav80x_set_deemph(codec);
}

static int adav80x_get_deemph(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = adav80x->deemph;
	return 0;
};

static const DECLARE_TLV_DB_SCALE(adav80x_inpga_tlv, 0, 50, 0);
static const DECLARE_TLV_DB_MINMAX(adav80x_digital_tlv, -9563, 0);

static const struct snd_kcontrol_new adav80x_controls[] = {
	SOC_DOUBLE_R_TLV("Master Playback Volume", ADAV80X_DAC_L_VOL,
		ADAV80X_DAC_R_VOL, 0, 0xff, 0, adav80x_digital_tlv),
	SOC_DOUBLE_R_TLV("Master Capture Volume", ADAV80X_ADC_L_VOL,
			ADAV80X_ADC_R_VOL, 0, 0xff, 0, adav80x_digital_tlv),

	SOC_DOUBLE_R_TLV("PGA Capture Volume", ADAV80X_PGA_L_VOL,
			ADAV80X_PGA_R_VOL, 0, 0x30, 0, adav80x_inpga_tlv),

	SOC_DOUBLE("Master Playback Switch", ADAV80X_DAC_CTRL1, 0, 1, 1, 0),
	SOC_DOUBLE("Master Capture Switch", ADAV80X_ADC_CTRL1, 2, 3, 1, 1),

	SOC_SINGLE("ADC High Pass Filter Switch", ADAV80X_ADC_CTRL1, 6, 1, 0),

	SOC_SINGLE_BOOL_EXT("Playback De-emphasis Switch", 0,
			adav80x_get_deemph, adav80x_put_deemph),
};

static unsigned int adav80x_port_ctrl_regs[2][2] = {
	{ ADAV80X_REC_CTRL, ADAV80X_PLAYBACK_CTRL, },
	{ ADAV80X_AUX_OUT_CTRL, ADAV80X_AUX_IN_CTRL },
};

static int adav80x_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);
	unsigned int capture = 0x00;
	unsigned int playback = 0x00;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		capture |= ADAV80X_CAPTURE_MODE_MASTER;
		playback |= ADAV80X_PLAYBACK_MODE_MASTER;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		capture |= ADAV80X_CAPTURE_MODE_I2S;
		playback |= ADAV80X_PLAYBACK_MODE_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		capture |= ADAV80X_CAPTURE_MODE_LEFT_J;
		playback |= ADAV80X_PLAYBACK_MODE_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		capture |= ADAV80X_CAPTURE_MODE_RIGHT_J;
		playback |= ADAV80X_PLAYBACK_MODE_RIGHT_J_24;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, adav80x_port_ctrl_regs[dai->id][0],
		ADAV80X_CAPTURE_MODE_MASK | ADAV80X_CAPTURE_MODE_MASTER,
		capture);
	snd_soc_write(codec, adav80x_port_ctrl_regs[dai->id][1], playback);

	adav80x->dai_fmt[dai->id] = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	return 0;
}

static int adav80x_set_adc_clock(struct snd_soc_codec *codec,
		unsigned int sample_rate)
{
	unsigned int val;

	if (sample_rate <= 48000)
		val = ADAV80X_ADC_CTRL1_MODULATOR_128FS;
	else
		val = ADAV80X_ADC_CTRL1_MODULATOR_64FS;

	snd_soc_update_bits(codec, ADAV80X_ADC_CTRL1,
		ADAV80X_ADC_CTRL1_MODULATOR_MASK, val);

	return 0;
}

static int adav80x_set_dac_clock(struct snd_soc_codec *codec,
		unsigned int sample_rate)
{
	unsigned int val;

	if (sample_rate <= 48000)
		val = ADAV80X_DAC_CTRL2_DIV1 | ADAV80X_DAC_CTRL2_INTERPOL_256FS;
	else
		val = ADAV80X_DAC_CTRL2_DIV2 | ADAV80X_DAC_CTRL2_INTERPOL_128FS;

	snd_soc_update_bits(codec, ADAV80X_DAC_CTRL2,
		ADAV80X_DAC_CTRL2_DIV_MASK | ADAV80X_DAC_CTRL2_INTERPOL_MASK,
		val);

	return 0;
}

static int adav80x_set_capture_pcm_format(struct snd_soc_codec *codec,
		struct snd_soc_dai *dai, snd_pcm_format_t format)
{
	unsigned int val;

	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = ADAV80X_CAPTURE_WORD_LEN16;
		break;
	case SNDRV_PCM_FORMAT_S18_3LE:
		val = ADAV80X_CAPTRUE_WORD_LEN18;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val = ADAV80X_CAPTURE_WORD_LEN20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = ADAV80X_CAPTURE_WORD_LEN24;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, adav80x_port_ctrl_regs[dai->id][0],
		ADAV80X_CAPTURE_WORD_LEN_MASK, val);

	return 0;
}

static int adav80x_set_playback_pcm_format(struct snd_soc_codec *codec,
		struct snd_soc_dai *dai, snd_pcm_format_t format)
{
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);
	unsigned int val;

	if (adav80x->dai_fmt[dai->id] != SND_SOC_DAIFMT_RIGHT_J)
		return 0;

	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = ADAV80X_PLAYBACK_MODE_RIGHT_J_16;
		break;
	case SNDRV_PCM_FORMAT_S18_3LE:
		val = ADAV80X_PLAYBACK_MODE_RIGHT_J_18;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val = ADAV80X_PLAYBACK_MODE_RIGHT_J_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = ADAV80X_PLAYBACK_MODE_RIGHT_J_24;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, adav80x_port_ctrl_regs[dai->id][1],
		ADAV80X_PLAYBACK_MODE_MASK, val);

	return 0;
}

static int adav80x_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);
	unsigned int rate = params_rate(params);

	if (rate * 256 != adav80x->sysclk)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		adav80x_set_playback_pcm_format(codec, dai,
			params_format(params));
		adav80x_set_dac_clock(codec, rate);
	} else {
		adav80x_set_capture_pcm_format(codec, dai,
			params_format(params));
		adav80x_set_adc_clock(codec, rate);
	}
	adav80x->rate = rate;
	adav80x_set_deemph(codec);

	return 0;
}

static int adav80x_set_sysclk(struct snd_soc_codec *codec,
			      int clk_id, int source,
			      unsigned int freq, int dir)
{
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);

	if (dir == SND_SOC_CLOCK_IN) {
		switch (clk_id) {
		case ADAV80X_CLK_XIN:
		case ADAV80X_CLK_XTAL:
		case ADAV80X_CLK_MCLKI:
		case ADAV80X_CLK_PLL1:
		case ADAV80X_CLK_PLL2:
			break;
		default:
			return -EINVAL;
		}

		adav80x->sysclk = freq;

		if (adav80x->clk_src != clk_id) {
			unsigned int iclk_ctrl1, iclk_ctrl2;

			adav80x->clk_src = clk_id;
			if (clk_id == ADAV80X_CLK_XTAL)
				clk_id = ADAV80X_CLK_XIN;

			iclk_ctrl1 = ADAV80X_ICLK_CTRL1_DAC_SRC(clk_id) |
					ADAV80X_ICLK_CTRL1_ADC_SRC(clk_id) |
					ADAV80X_ICLK_CTRL1_ICLK2_SRC(clk_id);
			iclk_ctrl2 = ADAV80X_ICLK_CTRL2_ICLK1_SRC(clk_id);

			snd_soc_write(codec, ADAV80X_ICLK_CTRL1, iclk_ctrl1);
			snd_soc_write(codec, ADAV80X_ICLK_CTRL2, iclk_ctrl2);

			snd_soc_dapm_sync(&codec->dapm);
		}
	} else {
		unsigned int mask;

		switch (clk_id) {
		case ADAV80X_CLK_SYSCLK1:
		case ADAV80X_CLK_SYSCLK2:
		case ADAV80X_CLK_SYSCLK3:
			break;
		default:
			return -EINVAL;
		}

		clk_id -= ADAV80X_CLK_SYSCLK1;
		mask = ADAV80X_PLL_OUTE_SYSCLKPD(clk_id);

		if (freq == 0) {
			snd_soc_update_bits(codec, ADAV80X_PLL_OUTE, mask, mask);
			adav80x->sysclk_pd[clk_id] = true;
		} else {
			snd_soc_update_bits(codec, ADAV80X_PLL_OUTE, mask, 0);
			adav80x->sysclk_pd[clk_id] = false;
		}

		if (adav80x->sysclk_pd[0])
			snd_soc_dapm_disable_pin(&codec->dapm, "PLL1");
		else
			snd_soc_dapm_force_enable_pin(&codec->dapm, "PLL1");

		if (adav80x->sysclk_pd[1] || adav80x->sysclk_pd[2])
			snd_soc_dapm_disable_pin(&codec->dapm, "PLL2");
		else
			snd_soc_dapm_force_enable_pin(&codec->dapm, "PLL2");

		snd_soc_dapm_sync(&codec->dapm);
	}

	return 0;
}

static int adav80x_set_pll(struct snd_soc_codec *codec, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);
	unsigned int pll_ctrl1 = 0;
	unsigned int pll_ctrl2 = 0;
	unsigned int pll_src;

	switch (source) {
	case ADAV80X_PLL_SRC_XTAL:
	case ADAV80X_PLL_SRC_XIN:
	case ADAV80X_PLL_SRC_MCLKI:
		break;
	default:
		return -EINVAL;
	}

	if (!freq_out)
		return 0;

	switch (freq_in) {
	case 27000000:
		break;
	case 54000000:
		if (source == ADAV80X_PLL_SRC_XIN) {
			pll_ctrl1 |= ADAV80X_PLL_CTRL1_PLLDIV;
			break;
		}
	default:
		return -EINVAL;
	}

	if (freq_out > 12288000) {
		pll_ctrl2 |= ADAV80X_PLL_CTRL2_DOUB(pll_id);
		freq_out /= 2;
	}

	/* freq_out = sample_rate * 256 */
	switch (freq_out) {
	case 8192000:
		pll_ctrl2 |= ADAV80X_PLL_CTRL2_FS_32(pll_id);
		break;
	case 11289600:
		pll_ctrl2 |= ADAV80X_PLL_CTRL2_FS_44(pll_id);
		break;
	case 12288000:
		pll_ctrl2 |= ADAV80X_PLL_CTRL2_FS_48(pll_id);
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, ADAV80X_PLL_CTRL1, ADAV80X_PLL_CTRL1_PLLDIV,
		pll_ctrl1);
	snd_soc_update_bits(codec, ADAV80X_PLL_CTRL2,
			ADAV80X_PLL_CTRL2_PLL_MASK(pll_id), pll_ctrl2);

	if (source != adav80x->pll_src) {
		if (source == ADAV80X_PLL_SRC_MCLKI)
			pll_src = ADAV80X_PLL_CLK_SRC_PLL_MCLKI(pll_id);
		else
			pll_src = ADAV80X_PLL_CLK_SRC_PLL_XIN(pll_id);

		snd_soc_update_bits(codec, ADAV80X_PLL_CLK_SRC,
				ADAV80X_PLL_CLK_SRC_PLL_MASK(pll_id), pll_src);

		adav80x->pll_src = source;

		snd_soc_dapm_sync(&codec->dapm);
	}

	return 0;
}

static int adav80x_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	unsigned int mask = ADAV80X_DAC_CTRL1_PD;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_update_bits(codec, ADAV80X_DAC_CTRL1, mask, 0x00);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, ADAV80X_DAC_CTRL1, mask, mask);
		break;
	}

	codec->dapm.bias_level = level;
	return 0;
}

/* Enforce the same sample rate on all audio interfaces */
static int adav80x_dai_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);

	if (!codec->active || !adav80x->rate)
		return 0;

	return snd_pcm_hw_constraint_minmax(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, adav80x->rate, adav80x->rate);
}

static void adav80x_dai_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);

	if (!codec->active)
		adav80x->rate = 0;
}

static const struct snd_soc_dai_ops adav80x_dai_ops = {
	.set_fmt = adav80x_set_dai_fmt,
	.hw_params = adav80x_hw_params,
	.startup = adav80x_dai_startup,
	.shutdown = adav80x_dai_shutdown,
};

#define ADAV80X_PLAYBACK_RATES (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_64000 | SNDRV_PCM_RATE_88200 | \
	SNDRV_PCM_RATE_96000)

#define ADAV80X_CAPTURE_RATES (SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000)

#define ADAV80X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S18_3LE | \
	SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver adav80x_dais[] = {
	{
		.name = "adav80x-hifi",
		.id = 0,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ADAV80X_PLAYBACK_RATES,
			.formats = ADAV80X_FORMATS,
	},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ADAV80X_CAPTURE_RATES,
			.formats = ADAV80X_FORMATS,
		},
		.ops = &adav80x_dai_ops,
	},
	{
		.name = "adav80x-aux",
		.id = 1,
		.playback = {
			.stream_name = "Aux Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ADAV80X_PLAYBACK_RATES,
			.formats = ADAV80X_FORMATS,
		},
		.capture = {
			.stream_name = "Aux Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ADAV80X_CAPTURE_RATES,
			.formats = ADAV80X_FORMATS,
		},
		.ops = &adav80x_dai_ops,
	},
};

static int adav80x_probe(struct snd_soc_codec *codec)
{
	int ret;
	struct adav80x *adav80x = snd_soc_codec_get_drvdata(codec);

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, adav80x->control_type);
	if (ret) {
		dev_err(codec->dev, "failed to set cache I/O: %d\n", ret);
		return ret;
	}

	/* Force PLLs on for SYSCLK output */
	snd_soc_dapm_force_enable_pin(&codec->dapm, "PLL1");
	snd_soc_dapm_force_enable_pin(&codec->dapm, "PLL2");

	/* Power down S/PDIF receiver, since it is currently not supported */
	snd_soc_write(codec, ADAV80X_PLL_OUTE, 0x20);
	/* Disable DAC zero flag */
	snd_soc_write(codec, ADAV80X_DAC_CTRL3, 0x6);

	return adav80x_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
}

static int adav80x_suspend(struct snd_soc_codec *codec)
{
	return adav80x_set_bias_level(codec, SND_SOC_BIAS_OFF);
}

static int adav80x_resume(struct snd_soc_codec *codec)
{
	adav80x_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	codec->cache_sync = 1;
	snd_soc_cache_sync(codec);

	return 0;
}

static int adav80x_remove(struct snd_soc_codec *codec)
{
	return adav80x_set_bias_level(codec, SND_SOC_BIAS_OFF);
}

static struct snd_soc_codec_driver adav80x_codec_driver = {
	.probe = adav80x_probe,
	.remove = adav80x_remove,
	.suspend = adav80x_suspend,
	.resume = adav80x_resume,
	.set_bias_level = adav80x_set_bias_level,

	.set_pll = adav80x_set_pll,
	.set_sysclk = adav80x_set_sysclk,

	.reg_word_size = sizeof(u8),
	.reg_cache_size = ARRAY_SIZE(adav80x_default_regs),
	.reg_cache_default = adav80x_default_regs,

	.controls = adav80x_controls,
	.num_controls = ARRAY_SIZE(adav80x_controls),
	.dapm_widgets = adav80x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(adav80x_dapm_widgets),
	.dapm_routes = adav80x_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(adav80x_dapm_routes),
};

static int __devinit adav80x_bus_probe(struct device *dev,
		enum snd_soc_control_type control_type)
{
	struct adav80x *adav80x;
	int ret;

	adav80x = kzalloc(sizeof(*adav80x), GFP_KERNEL);
	if (!adav80x)
		return -ENOMEM;

	dev_set_drvdata(dev, adav80x);
	adav80x->control_type = control_type;

	ret = snd_soc_register_codec(dev, &adav80x_codec_driver,
		adav80x_dais, ARRAY_SIZE(adav80x_dais));
	if (ret)
		kfree(adav80x);

	return ret;
}

static int __devexit adav80x_bus_remove(struct device *dev)
{
	snd_soc_unregister_codec(dev);
	kfree(dev_get_drvdata(dev));
	return 0;
}

#if defined(CONFIG_SPI_MASTER)
static int __devinit adav80x_spi_probe(struct spi_device *spi)
{
	return adav80x_bus_probe(&spi->dev, SND_SOC_SPI);
}

static int __devexit adav80x_spi_remove(struct spi_device *spi)
{
	return adav80x_bus_remove(&spi->dev);
}

static struct spi_driver adav80x_spi_driver = {
	.driver = {
		.name	= "adav801",
		.owner	= THIS_MODULE,
	},
	.probe		= adav80x_spi_probe,
	.remove		= __devexit_p(adav80x_spi_remove),
};
#endif

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static const struct i2c_device_id adav80x_id[] = {
	{ "adav803", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adav80x_id);

static int __devinit adav80x_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	return adav80x_bus_probe(&client->dev, SND_SOC_I2C);
}

static int __devexit adav80x_i2c_remove(struct i2c_client *client)
{
	return adav80x_bus_remove(&client->dev);
}

static struct i2c_driver adav80x_i2c_driver = {
	.driver = {
		.name = "adav803",
		.owner = THIS_MODULE,
	},
	.probe = adav80x_i2c_probe,
	.remove = __devexit_p(adav80x_i2c_remove),
	.id_table = adav80x_id,
};
#endif

static int __init adav80x_init(void)
{
	int ret = 0;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&adav80x_i2c_driver);
	if (ret)
		return ret;
#endif

#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&adav80x_spi_driver);
#endif

	return ret;
}
module_init(adav80x_init);

static void __exit adav80x_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&adav80x_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&adav80x_spi_driver);
#endif
}
module_exit(adav80x_exit);

MODULE_DESCRIPTION("ASoC ADAV80x driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_AUTHOR("Yi Li <yi.li@analog.com>>");
MODULE_LICENSE("GPL");

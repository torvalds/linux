/*
 * Copyright (C) 2011 LAPIS Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "ml26124.h"

#define DVOL_CTL_DVMUTE_ON		BIT(4)	/* Digital volume MUTE On */
#define DVOL_CTL_DVMUTE_OFF		0	/* Digital volume MUTE Off */
#define ML26124_SAI_NO_DELAY	BIT(1)
#define ML26124_SAI_FRAME_SYNC	(BIT(5) | BIT(0)) /* For mono (Telecodec) */
#define ML26134_CACHESIZE 212
#define ML26124_VMID	BIT(1)
#define ML26124_RATES (SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 |\
		       SNDRV_PCM_RATE_48000)
#define ML26124_FORMATS (SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)
#define ML26124_NUM_REGISTER ML26134_CACHESIZE

struct ml26124_priv {
	u32 mclk;
	u32 rate;
	struct regmap *regmap;
	int clk_in;
	struct snd_pcm_substream *substream;
};

struct clk_coeff {
	u32 mclk;
	u32 rate;
	u8 pllnl;
	u8 pllnh;
	u8 pllml;
	u8 pllmh;
	u8 plldiv;
};

/* ML26124 configuration */
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7150, 50, 0);

static const DECLARE_TLV_DB_SCALE(alclvl, -2250, 150, 0);
static const DECLARE_TLV_DB_SCALE(mingain, -1200, 600, 0);
static const DECLARE_TLV_DB_SCALE(maxgain, -675, 600, 0);
static const DECLARE_TLV_DB_SCALE(boost_vol, -1200, 75, 0);
static const DECLARE_TLV_DB_SCALE(ngth, -7650, 150, 0);

static const char * const ml26124_companding[] = {"16bit PCM", "u-law",
						  "A-law"};

static const struct soc_enum ml26124_adc_companding_enum
	= SOC_ENUM_SINGLE(ML26124_SAI_TRANS_CTL, 6, 3, ml26124_companding);

static const struct soc_enum ml26124_dac_companding_enum
	= SOC_ENUM_SINGLE(ML26124_SAI_RCV_CTL, 6, 3, ml26124_companding);

static const struct snd_kcontrol_new ml26124_snd_controls[] = {
	SOC_SINGLE_TLV("Capture Digital Volume", ML26124_RECORD_DIG_VOL, 0,
			0xff, 1, digital_tlv),
	SOC_SINGLE_TLV("Playback Digital Volume", ML26124_PLBAK_DIG_VOL, 0,
			0xff, 1, digital_tlv),
	SOC_SINGLE_TLV("Digital Boost Volume", ML26124_DIGI_BOOST_VOL, 0,
			0x3f, 0, boost_vol),
	SOC_SINGLE_TLV("EQ Band0 Volume", ML26124_EQ_GAIN_BRAND0, 0,
			0xff, 1, digital_tlv),
	SOC_SINGLE_TLV("EQ Band1 Volume", ML26124_EQ_GAIN_BRAND1, 0,
			0xff, 1, digital_tlv),
	SOC_SINGLE_TLV("EQ Band2 Volume", ML26124_EQ_GAIN_BRAND2, 0,
			0xff, 1, digital_tlv),
	SOC_SINGLE_TLV("EQ Band3 Volume", ML26124_EQ_GAIN_BRAND3, 0,
			0xff, 1, digital_tlv),
	SOC_SINGLE_TLV("EQ Band4 Volume", ML26124_EQ_GAIN_BRAND4, 0,
			0xff, 1, digital_tlv),
	SOC_SINGLE_TLV("ALC Target Level", ML26124_ALC_TARGET_LEV, 0,
			0xf, 1, alclvl),
	SOC_SINGLE_TLV("ALC Min Input Volume", ML26124_ALC_MAXMIN_GAIN, 0,
			7, 0, mingain),
	SOC_SINGLE_TLV("ALC Max Input Volume", ML26124_ALC_MAXMIN_GAIN, 4,
			7, 1, maxgain),
	SOC_SINGLE_TLV("Playback Limiter Min Input Volume",
			ML26124_PL_MAXMIN_GAIN, 0, 7, 0, mingain),
	SOC_SINGLE_TLV("Playback Limiter Max Input Volume",
			ML26124_PL_MAXMIN_GAIN, 4, 7, 1, maxgain),
	SOC_SINGLE_TLV("Playback Boost Volume", ML26124_PLYBAK_BOST_VOL, 0,
			0x3f, 0, boost_vol),
	SOC_SINGLE("DC High Pass Filter Switch", ML26124_FILTER_EN, 0, 1, 0),
	SOC_SINGLE("Noise High Pass Filter Switch", ML26124_FILTER_EN, 1, 1, 0),
	SOC_SINGLE("ZC Switch", ML26124_PW_ZCCMP_PW_MNG, 1,
		    1, 0),
	SOC_SINGLE("EQ Band0 Switch", ML26124_FILTER_EN, 2, 1, 0),
	SOC_SINGLE("EQ Band1 Switch", ML26124_FILTER_EN, 3, 1, 0),
	SOC_SINGLE("EQ Band2 Switch", ML26124_FILTER_EN, 4, 1, 0),
	SOC_SINGLE("EQ Band3 Switch", ML26124_FILTER_EN, 5, 1, 0),
	SOC_SINGLE("EQ Band4 Switch", ML26124_FILTER_EN, 6, 1, 0),
	SOC_SINGLE("Play Limiter", ML26124_DVOL_CTL, 0, 1, 0),
	SOC_SINGLE("Capture Limiter", ML26124_DVOL_CTL, 1, 1, 0),
	SOC_SINGLE("Digital Volume Fade Switch", ML26124_DVOL_CTL, 3, 1, 0),
	SOC_SINGLE("Digital Switch", ML26124_DVOL_CTL, 4, 1, 0),
	SOC_ENUM("DAC Companding", ml26124_dac_companding_enum),
	SOC_ENUM("ADC Companding", ml26124_adc_companding_enum),
};

static const struct snd_kcontrol_new ml26124_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("DAC Switch", ML26124_SPK_AMP_OUT, 1, 1, 0),
	SOC_DAPM_SINGLE("Line in loopback Switch", ML26124_SPK_AMP_OUT, 3, 1,
			 0),
	SOC_DAPM_SINGLE("PGA Switch", ML26124_SPK_AMP_OUT, 5, 1, 0),
};

/* Input mux */
static const char * const ml26124_input_select[] = {"Analog MIC SingleEnded in",
				"Digital MIC in", "Analog MIC Differential in"};

static const struct soc_enum ml26124_insel_enum =
	SOC_ENUM_SINGLE(ML26124_MIC_IF_CTL, 0, 3, ml26124_input_select);

static const struct snd_kcontrol_new ml26124_input_mux_controls =
	SOC_DAPM_ENUM("Input Select", ml26124_insel_enum);

static const struct snd_kcontrol_new ml26124_line_control =
	SOC_DAPM_SINGLE("Switch", ML26124_PW_LOUT_PW_MNG, 1, 1, 0);

static const struct snd_soc_dapm_widget ml26124_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("MCLKEN", ML26124_CLK_EN, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLLEN", ML26124_CLK_EN, 1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLLOE", ML26124_CLK_EN, 2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS", ML26124_PW_REF_PW_MNG, 2, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Output Mixer", SND_SOC_NOPM, 0, 0,
			   &ml26124_output_mixer_controls[0],
			   ARRAY_SIZE(ml26124_output_mixer_controls)),
	SND_SOC_DAPM_DAC("DAC", "Playback", ML26124_PW_DAC_PW_MNG, 1, 0),
	SND_SOC_DAPM_ADC("ADC", "Capture", ML26124_PW_IN_PW_MNG, 1, 0),
	SND_SOC_DAPM_PGA("PGA", ML26124_PW_IN_PW_MNG, 3, 0, NULL, 0),
	SND_SOC_DAPM_MUX("Input Mux", SND_SOC_NOPM, 0, 0,
			  &ml26124_input_mux_controls),
	SND_SOC_DAPM_SWITCH("Line Out Enable", SND_SOC_NOPM, 0, 0,
			     &ml26124_line_control),
	SND_SOC_DAPM_INPUT("MDIN"),
	SND_SOC_DAPM_INPUT("MIN"),
	SND_SOC_DAPM_INPUT("LIN"),
	SND_SOC_DAPM_OUTPUT("SPOUT"),
	SND_SOC_DAPM_OUTPUT("LOUT"),
};

static const struct snd_soc_dapm_route ml26124_intercon[] = {
	/* Supply */
	{"DAC", NULL, "MCLKEN"},
	{"ADC", NULL, "MCLKEN"},
	{"DAC", NULL, "PLLEN"},
	{"ADC", NULL, "PLLEN"},
	{"DAC", NULL, "PLLOE"},
	{"ADC", NULL, "PLLOE"},

	/* output mixer */
	{"Output Mixer", "DAC Switch", "DAC"},
	{"Output Mixer", "Line in loopback Switch", "LIN"},

	/* outputs */
	{"LOUT", NULL, "Output Mixer"},
	{"SPOUT", NULL, "Output Mixer"},
	{"Line Out Enable", NULL, "LOUT"},

	/* input */
	{"ADC", NULL, "Input Mux"},
	{"Input Mux", "Analog MIC SingleEnded in", "PGA"},
	{"Input Mux", "Analog MIC Differential in", "PGA"},
	{"PGA", NULL, "MIN"},
};

/* PLLOutputFreq(Hz) = InputMclkFreq(Hz) * PLLM / (PLLN * PLLDIV) */
static const struct clk_coeff coeff_div[] = {
	{12288000, 16000, 0xc, 0x0, 0x20, 0x0, 0x4},
	{12288000, 32000, 0xc, 0x0, 0x20, 0x0, 0x4},
	{12288000, 48000, 0xc, 0x0, 0x30, 0x0, 0x4},
};

static struct reg_default ml26124_reg[] = {
	/* CLOCK control Register */
	{0x00, 0x00 },	/* Sampling Rate */
	{0x02, 0x00},	/* PLL NL */
	{0x04, 0x00},	/* PLLNH */
	{0x06, 0x00},	/* PLLML */
	{0x08, 0x00},	/* MLLMH */
	{0x0a, 0x00},	/* PLLDIV */
	{0x0c, 0x00},	/* Clock Enable */
	{0x0e, 0x00},	/* CLK Input/Output Control */

	/* System Control Register */
	{0x10, 0x00},	/* Software RESET */
	{0x12, 0x00},	/* Record/Playback Run */
	{0x14, 0x00},	/* Mic Input/Output control */

	/* Power Management Register */
	{0x20, 0x00},	/* Reference Power Management */
	{0x22, 0x00},	/* Input Power Management */
	{0x24, 0x00},	/* DAC Power Management */
	{0x26, 0x00},	/* SP-AMP Power Management */
	{0x28, 0x00},	/* LINEOUT Power Management */
	{0x2a, 0x00},	/* VIDEO Power Management */
	{0x2e, 0x00},	/* AC-CMP Power Management */

	/* Analog reference Control Register */
	{0x30, 0x04},	/* MICBIAS Voltage Control */

	/* Input/Output Amplifier Control Register */
	{0x32, 0x10},	/* MIC Input Volume */
	{0x38, 0x00},	/* Mic Boost Volume */
	{0x3a, 0x33},	/* Speaker AMP Volume */
	{0x48, 0x00},	/* AMP Volume Control Function Enable */
	{0x4a, 0x00},	/* Amplifier Volume Fader Control */

	/* Analog Path Control Register */
	{0x54, 0x00},	/* Speaker AMP Output Control */
	{0x5a, 0x00},	/* Mic IF Control */
	{0xe8, 0x01},	/* Mic Select Control */

	/* Audio Interface Control Register */
	{0x60, 0x00},	/* SAI-Trans Control */
	{0x62, 0x00},	/* SAI-Receive Control */
	{0x64, 0x00},	/* SAI Mode select */

	/* DSP Control Register */
	{0x66, 0x01},	/* Filter Func Enable */
	{0x68, 0x00},	/* Volume Control Func Enable */
	{0x6A, 0x00},	/* Mixer & Volume Control*/
	{0x6C, 0xff},	/* Record Digital Volume */
	{0x70, 0xff},	/* Playback Digital Volume */
	{0x72, 0x10},	/* Digital Boost Volume */
	{0x74, 0xe7},	/* EQ gain Band0 */
	{0x76, 0xe7},	/* EQ gain Band1 */
	{0x78, 0xe7},	/* EQ gain Band2 */
	{0x7A, 0xe7},	/* EQ gain Band3 */
	{0x7C, 0xe7},	/* EQ gain Band4 */
	{0x7E, 0x00},	/* HPF2 CutOff*/
	{0x80, 0x00},	/* EQ Band0 Coef0L */
	{0x82, 0x00},	/* EQ Band0 Coef0H */
	{0x84, 0x00},	/* EQ Band0 Coef0L */
	{0x86, 0x00},	/* EQ Band0 Coef0H */
	{0x88, 0x00},	/* EQ Band1 Coef0L */
	{0x8A, 0x00},	/* EQ Band1 Coef0H */
	{0x8C, 0x00},	/* EQ Band1 Coef0L */
	{0x8E, 0x00},	/* EQ Band1 Coef0H */
	{0x90, 0x00},	/* EQ Band2 Coef0L */
	{0x92, 0x00},	/* EQ Band2 Coef0H */
	{0x94, 0x00},	/* EQ Band2 Coef0L */
	{0x96, 0x00},	/* EQ Band2 Coef0H */
	{0x98, 0x00},	/* EQ Band3 Coef0L */
	{0x9A, 0x00},	/* EQ Band3 Coef0H */
	{0x9C, 0x00},	/* EQ Band3 Coef0L */
	{0x9E, 0x00},	/* EQ Band3 Coef0H */
	{0xA0, 0x00},	/* EQ Band4 Coef0L */
	{0xA2, 0x00},	/* EQ Band4 Coef0H */
	{0xA4, 0x00},	/* EQ Band4 Coef0L */
	{0xA6, 0x00},	/* EQ Band4 Coef0H */

	/* ALC Control Register */
	{0xb0, 0x00},	/* ALC Mode */
	{0xb2, 0x02},	/* ALC Attack Time */
	{0xb4, 0x03},	/* ALC Decay Time */
	{0xb6, 0x00},	/* ALC Hold Time */
	{0xb8, 0x0b},	/* ALC Target Level */
	{0xba, 0x70},	/* ALC Max/Min Gain */
	{0xbc, 0x00},	/* Noise Gate Threshold */
	{0xbe, 0x00},	/* ALC ZeroCross TimeOut */

	/* Playback Limiter Control Register */
	{0xc0, 0x04},	/* PL Attack Time */
	{0xc2, 0x05},	/* PL Decay Time */
	{0xc4, 0x0d},	/* PL Target Level */
	{0xc6, 0x70},	/* PL Max/Min Gain */
	{0xc8, 0x10},	/* Playback Boost Volume */
	{0xca, 0x00},	/* PL ZeroCross TimeOut */

	/* Video Amplifier Control Register */
	{0xd0, 0x01},	/* VIDEO AMP Gain Control */
	{0xd2, 0x01},	/* VIDEO AMP Setup 1 */
	{0xd4, 0x01},	/* VIDEO AMP Control2 */
};

/* Get sampling rate value of sampling rate setting register (0x0) */
static inline int get_srate(int rate)
{
	int srate;

	switch (rate) {
	case 16000:
		srate = 3;
		break;
	case 32000:
		srate = 6;
		break;
	case 48000:
		srate = 8;
		break;
	default:
		return -EINVAL;
	}
	return srate;
}

static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}
	return -EINVAL;
}

static int ml26124_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *hw_params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ml26124_priv *priv = snd_soc_codec_get_drvdata(codec);
	int i = get_coeff(priv->mclk, params_rate(hw_params));

	priv->substream = substream;
	priv->rate = params_rate(hw_params);

	if (priv->clk_in) {
		switch (priv->mclk / params_rate(hw_params)) {
		case 256:
			snd_soc_update_bits(codec, ML26124_CLK_CTL,
					    BIT(0) | BIT(1), 1);
			break;
		case 512:
			snd_soc_update_bits(codec, ML26124_CLK_CTL,
					    BIT(0) | BIT(1), 2);
			break;
		case 1024:
			snd_soc_update_bits(codec, ML26124_CLK_CTL,
					    BIT(0) | BIT(1), 3);
			break;
		default:
			dev_err(codec->dev, "Unsupported MCLKI\n");
			break;
		}
	} else {
		snd_soc_update_bits(codec, ML26124_CLK_CTL,
				    BIT(0) | BIT(1), 0);
	}

	switch (params_rate(hw_params)) {
	case 16000:
		snd_soc_update_bits(codec, ML26124_SMPLING_RATE, 0xf,
				    get_srate(params_rate(hw_params)));
		snd_soc_update_bits(codec, ML26124_PLLNL, 0xff,
				    coeff_div[i].pllnl);
		snd_soc_update_bits(codec, ML26124_PLLNH, 0x1,
				    coeff_div[i].pllnh);
		snd_soc_update_bits(codec, ML26124_PLLML, 0xff,
				    coeff_div[i].pllml);
		snd_soc_update_bits(codec, ML26124_PLLMH, 0x3f,
				    coeff_div[i].pllmh);
		snd_soc_update_bits(codec, ML26124_PLLDIV, 0x1f,
				    coeff_div[i].plldiv);
		break;
	case 32000:
		snd_soc_update_bits(codec, ML26124_SMPLING_RATE, 0xf,
				    get_srate(params_rate(hw_params)));
		snd_soc_update_bits(codec, ML26124_PLLNL, 0xff,
				    coeff_div[i].pllnl);
		snd_soc_update_bits(codec, ML26124_PLLNH, 0x1,
				    coeff_div[i].pllnh);
		snd_soc_update_bits(codec, ML26124_PLLML, 0xff,
				    coeff_div[i].pllml);
		snd_soc_update_bits(codec, ML26124_PLLMH, 0x3f,
				    coeff_div[i].pllmh);
		snd_soc_update_bits(codec, ML26124_PLLDIV, 0x1f,
				    coeff_div[i].plldiv);
		break;
	case 48000:
		snd_soc_update_bits(codec, ML26124_SMPLING_RATE, 0xf,
				    get_srate(params_rate(hw_params)));
		snd_soc_update_bits(codec, ML26124_PLLNL, 0xff,
				    coeff_div[i].pllnl);
		snd_soc_update_bits(codec, ML26124_PLLNH, 0x1,
				    coeff_div[i].pllnh);
		snd_soc_update_bits(codec, ML26124_PLLML, 0xff,
				    coeff_div[i].pllml);
		snd_soc_update_bits(codec, ML26124_PLLMH, 0x3f,
				    coeff_div[i].pllmh);
		snd_soc_update_bits(codec, ML26124_PLLDIV, 0x1f,
				    coeff_div[i].plldiv);
		break;
	default:
		pr_err("%s:this rate is no support for ml26124\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int ml26124_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ml26124_priv *priv = snd_soc_codec_get_drvdata(codec);

	switch (priv->substream->stream) {
	case SNDRV_PCM_STREAM_CAPTURE:
		snd_soc_update_bits(codec, ML26124_REC_PLYBAK_RUN, BIT(0), 1);
		break;
	case SNDRV_PCM_STREAM_PLAYBACK:
		snd_soc_update_bits(codec, ML26124_REC_PLYBAK_RUN, BIT(1), 2);
		break;
	}

	if (mute)
		snd_soc_update_bits(codec, ML26124_DVOL_CTL, BIT(4),
				    DVOL_CTL_DVMUTE_ON);
	else
		snd_soc_update_bits(codec, ML26124_DVOL_CTL, BIT(4),
				    DVOL_CTL_DVMUTE_OFF);

	return 0;
}

static int ml26124_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	unsigned char mode;
	struct snd_soc_codec *codec = codec_dai->codec;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		mode = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		mode = 0;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_update_bits(codec, ML26124_SAI_MODE_SEL, BIT(0), mode);

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ml26124_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct ml26124_priv *priv = snd_soc_codec_get_drvdata(codec);

	switch (clk_id) {
	case ML26124_USE_PLLOUT:
		priv->clk_in = ML26124_USE_PLLOUT;
		break;
	case ML26124_USE_MCLKI:
		priv->clk_in = ML26124_USE_MCLKI;
		break;
	default:
		return -EINVAL;
	}

	priv->mclk = freq;

	return 0;
}

static int ml26124_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	struct ml26124_priv *priv = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_update_bits(codec, ML26124_PW_SPAMP_PW_MNG,
				    ML26124_R26_MASK, ML26124_BLT_PREAMP_ON);
		msleep(100);
		snd_soc_update_bits(codec, ML26124_PW_SPAMP_PW_MNG,
				    ML26124_R26_MASK,
				    ML26124_MICBEN_ON | ML26124_BLT_ALL_ON);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* VMID ON */
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			snd_soc_update_bits(codec, ML26124_PW_REF_PW_MNG,
					    ML26124_VMID, ML26124_VMID);
			msleep(500);
			regcache_sync(priv->regmap);
		}
		break;
	case SND_SOC_BIAS_OFF:
		/* VMID OFF */
		snd_soc_update_bits(codec, ML26124_PW_REF_PW_MNG,
				    ML26124_VMID, 0);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static const struct snd_soc_dai_ops ml26124_dai_ops = {
	.hw_params	= ml26124_hw_params,
	.digital_mute	= ml26124_mute,
	.set_fmt	= ml26124_set_dai_fmt,
	.set_sysclk	= ml26124_set_dai_sysclk,
};

static struct snd_soc_dai_driver ml26124_dai = {
	.name = "ml26124-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = ML26124_RATES,
		.formats = ML26124_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = ML26124_RATES,
		.formats = ML26124_FORMATS,},
	.ops = &ml26124_dai_ops,
	.symmetric_rates = 1,
};

#ifdef CONFIG_PM
static int ml26124_suspend(struct snd_soc_codec *codec)
{
	ml26124_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int ml26124_resume(struct snd_soc_codec *codec)
{
	ml26124_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}
#else
#define ml26124_suspend NULL
#define ml26124_resume NULL
#endif

static int ml26124_probe(struct snd_soc_codec *codec)
{
	int ret;
	struct ml26124_priv *priv = snd_soc_codec_get_drvdata(codec);
	codec->control_data = priv->regmap;

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, SND_SOC_REGMAP);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	/* Software Reset */
	snd_soc_update_bits(codec, ML26124_SW_RST, 0x01, 1);
	snd_soc_update_bits(codec, ML26124_SW_RST, 0x01, 0);

	ml26124_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_ml26124 = {
	.probe =	ml26124_probe,
	.suspend =	ml26124_suspend,
	.resume =	ml26124_resume,
	.set_bias_level = ml26124_set_bias_level,
	.dapm_widgets = ml26124_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ml26124_dapm_widgets),
	.dapm_routes = ml26124_intercon,
	.num_dapm_routes = ARRAY_SIZE(ml26124_intercon),
	.controls = ml26124_snd_controls,
	.num_controls = ARRAY_SIZE(ml26124_snd_controls),
};

static const struct regmap_config ml26124_i2c_regmap = {
	.val_bits = 8,
	.reg_bits = 8,
	.max_register = ML26124_NUM_REGISTER,
	.reg_defaults = ml26124_reg,
	.num_reg_defaults = ARRAY_SIZE(ml26124_reg),
	.cache_type = REGCACHE_RBTREE,
	.write_flag_mask = 0x01,
};

static int ml26124_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct ml26124_priv *priv;
	int ret;

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(i2c, priv);

	priv->regmap = devm_regmap_init_i2c(i2c, &ml26124_i2c_regmap);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&i2c->dev, "regmap_init_i2c() failed: %d\n", ret);
		return ret;
	}

	return snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_ml26124, &ml26124_dai, 1);
}

static int ml26124_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id ml26124_i2c_id[] = {
	{ "ml26124", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ml26124_i2c_id);

static struct i2c_driver ml26124_i2c_driver = {
	.driver = {
		.name = "ml26124",
		.owner = THIS_MODULE,
	},
	.probe = ml26124_i2c_probe,
	.remove = ml26124_i2c_remove,
	.id_table = ml26124_i2c_id,
};

module_i2c_driver(ml26124_i2c_driver);

MODULE_AUTHOR("Tomoya MORINAGA <tomoya.rohm@gmail.com>");
MODULE_DESCRIPTION("LAPIS Semiconductor ML26124 ALSA SoC codec driver");
MODULE_LICENSE("GPL");

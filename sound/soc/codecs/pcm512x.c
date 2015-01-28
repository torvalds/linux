/*
 * Driver for the PCM512x CODECs
 *
 * Author:	Mark Brown <broonie@linaro.org>
 *		Copyright 2014 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include "pcm512x.h"

#define PCM512x_NUM_SUPPLIES 3
static const char * const pcm512x_supply_names[PCM512x_NUM_SUPPLIES] = {
	"AVDD",
	"DVDD",
	"CPVDD",
};

struct pcm512x_priv {
	struct regmap *regmap;
	struct clk *sclk;
	struct regulator_bulk_data supplies[PCM512x_NUM_SUPPLIES];
	struct notifier_block supply_nb[PCM512x_NUM_SUPPLIES];
	int fmt;
};

/*
 * We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define PCM512x_REGULATOR_EVENT(n) \
static int pcm512x_regulator_event_##n(struct notifier_block *nb, \
				      unsigned long event, void *data)    \
{ \
	struct pcm512x_priv *pcm512x = container_of(nb, struct pcm512x_priv, \
						    supply_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		regcache_mark_dirty(pcm512x->regmap);	\
		regcache_cache_only(pcm512x->regmap, true);	\
	} \
	return 0; \
}

PCM512x_REGULATOR_EVENT(0)
PCM512x_REGULATOR_EVENT(1)
PCM512x_REGULATOR_EVENT(2)

static const struct reg_default pcm512x_reg_defaults[] = {
	{ PCM512x_RESET,             0x00 },
	{ PCM512x_POWER,             0x00 },
	{ PCM512x_MUTE,              0x00 },
	{ PCM512x_DSP,               0x00 },
	{ PCM512x_PLL_REF,           0x00 },
	{ PCM512x_DAC_REF,           0x00 },
	{ PCM512x_DAC_ROUTING,       0x11 },
	{ PCM512x_DSP_PROGRAM,       0x01 },
	{ PCM512x_CLKDET,            0x00 },
	{ PCM512x_AUTO_MUTE,         0x00 },
	{ PCM512x_ERROR_DETECT,      0x00 },
	{ PCM512x_DIGITAL_VOLUME_1,  0x00 },
	{ PCM512x_DIGITAL_VOLUME_2,  0x30 },
	{ PCM512x_DIGITAL_VOLUME_3,  0x30 },
	{ PCM512x_DIGITAL_MUTE_1,    0x22 },
	{ PCM512x_DIGITAL_MUTE_2,    0x00 },
	{ PCM512x_DIGITAL_MUTE_3,    0x07 },
	{ PCM512x_OUTPUT_AMPLITUDE,  0x00 },
	{ PCM512x_ANALOG_GAIN_CTRL,  0x00 },
	{ PCM512x_UNDERVOLTAGE_PROT, 0x00 },
	{ PCM512x_ANALOG_MUTE_CTRL,  0x00 },
	{ PCM512x_ANALOG_GAIN_BOOST, 0x00 },
	{ PCM512x_VCOM_CTRL_1,       0x00 },
	{ PCM512x_VCOM_CTRL_2,       0x01 },
	{ PCM512x_BCLK_LRCLK_CFG,    0x00 },
	{ PCM512x_MASTER_MODE,       0x7c },
	{ PCM512x_SYNCHRONIZE,       0x10 },
	{ PCM512x_DSP_CLKDIV,        0x00 },
	{ PCM512x_DAC_CLKDIV,        0x00 },
	{ PCM512x_NCP_CLKDIV,        0x00 },
	{ PCM512x_OSR_CLKDIV,        0x00 },
	{ PCM512x_MASTER_CLKDIV_1,   0x00 },
	{ PCM512x_MASTER_CLKDIV_2,   0x00 },
	{ PCM512x_FS_SPEED_MODE,     0x00 },
	{ PCM512x_IDAC_1,            0x01 },
	{ PCM512x_IDAC_2,            0x00 },
};

static bool pcm512x_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PCM512x_RESET:
	case PCM512x_POWER:
	case PCM512x_MUTE:
	case PCM512x_PLL_EN:
	case PCM512x_SPI_MISO_FUNCTION:
	case PCM512x_DSP:
	case PCM512x_GPIO_EN:
	case PCM512x_BCLK_LRCLK_CFG:
	case PCM512x_DSP_GPIO_INPUT:
	case PCM512x_MASTER_MODE:
	case PCM512x_PLL_REF:
	case PCM512x_DAC_REF:
	case PCM512x_SYNCHRONIZE:
	case PCM512x_PLL_COEFF_0:
	case PCM512x_PLL_COEFF_1:
	case PCM512x_PLL_COEFF_2:
	case PCM512x_PLL_COEFF_3:
	case PCM512x_PLL_COEFF_4:
	case PCM512x_DSP_CLKDIV:
	case PCM512x_DAC_CLKDIV:
	case PCM512x_NCP_CLKDIV:
	case PCM512x_OSR_CLKDIV:
	case PCM512x_MASTER_CLKDIV_1:
	case PCM512x_MASTER_CLKDIV_2:
	case PCM512x_FS_SPEED_MODE:
	case PCM512x_IDAC_1:
	case PCM512x_IDAC_2:
	case PCM512x_ERROR_DETECT:
	case PCM512x_I2S_1:
	case PCM512x_I2S_2:
	case PCM512x_DAC_ROUTING:
	case PCM512x_DSP_PROGRAM:
	case PCM512x_CLKDET:
	case PCM512x_AUTO_MUTE:
	case PCM512x_DIGITAL_VOLUME_1:
	case PCM512x_DIGITAL_VOLUME_2:
	case PCM512x_DIGITAL_VOLUME_3:
	case PCM512x_DIGITAL_MUTE_1:
	case PCM512x_DIGITAL_MUTE_2:
	case PCM512x_DIGITAL_MUTE_3:
	case PCM512x_GPIO_OUTPUT_1:
	case PCM512x_GPIO_OUTPUT_2:
	case PCM512x_GPIO_OUTPUT_3:
	case PCM512x_GPIO_OUTPUT_4:
	case PCM512x_GPIO_OUTPUT_5:
	case PCM512x_GPIO_OUTPUT_6:
	case PCM512x_GPIO_CONTROL_1:
	case PCM512x_GPIO_CONTROL_2:
	case PCM512x_OVERFLOW:
	case PCM512x_RATE_DET_1:
	case PCM512x_RATE_DET_2:
	case PCM512x_RATE_DET_3:
	case PCM512x_RATE_DET_4:
	case PCM512x_ANALOG_MUTE_DET:
	case PCM512x_GPIN:
	case PCM512x_DIGITAL_MUTE_DET:
	case PCM512x_OUTPUT_AMPLITUDE:
	case PCM512x_ANALOG_GAIN_CTRL:
	case PCM512x_UNDERVOLTAGE_PROT:
	case PCM512x_ANALOG_MUTE_CTRL:
	case PCM512x_ANALOG_GAIN_BOOST:
	case PCM512x_VCOM_CTRL_1:
	case PCM512x_VCOM_CTRL_2:
	case PCM512x_CRAM_CTRL:
		return true;
	default:
		/* There are 256 raw register addresses */
		return reg < 0xff;
	}
}

static bool pcm512x_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PCM512x_PLL_EN:
	case PCM512x_OVERFLOW:
	case PCM512x_RATE_DET_1:
	case PCM512x_RATE_DET_2:
	case PCM512x_RATE_DET_3:
	case PCM512x_RATE_DET_4:
	case PCM512x_ANALOG_MUTE_DET:
	case PCM512x_GPIN:
	case PCM512x_DIGITAL_MUTE_DET:
	case PCM512x_CRAM_CTRL:
		return true;
	default:
		/* There are 256 raw register addresses */
		return reg < 0xff;
	}
}

static const DECLARE_TLV_DB_SCALE(digital_tlv, -10350, 50, 1);
static const DECLARE_TLV_DB_SCALE(analog_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(boost_tlv, 0, 80, 0);

static const char * const pcm512x_dsp_program_texts[] = {
	"FIR interpolation with de-emphasis",
	"Low latency IIR with de-emphasis",
	"Fixed process flow",
	"High attenuation with de-emphasis",
	"Ringing-less low latency FIR",
};

static const unsigned int pcm512x_dsp_program_values[] = {
	1,
	2,
	3,
	5,
	7,
};

static SOC_VALUE_ENUM_SINGLE_DECL(pcm512x_dsp_program,
				  PCM512x_DSP_PROGRAM, 0, 0x1f,
				  pcm512x_dsp_program_texts,
				  pcm512x_dsp_program_values);

static const char * const pcm512x_clk_missing_text[] = {
	"1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s"
};

static const struct soc_enum pcm512x_clk_missing =
	SOC_ENUM_SINGLE(PCM512x_CLKDET, 0,  8, pcm512x_clk_missing_text);

static const char * const pcm512x_autom_text[] = {
	"21ms", "106ms", "213ms", "533ms", "1.07s", "2.13s", "5.33s", "10.66s"
};

static const struct soc_enum pcm512x_autom_l =
	SOC_ENUM_SINGLE(PCM512x_AUTO_MUTE, PCM512x_ATML_SHIFT, 8,
			pcm512x_autom_text);

static const struct soc_enum pcm512x_autom_r =
	SOC_ENUM_SINGLE(PCM512x_AUTO_MUTE, PCM512x_ATMR_SHIFT, 8,
			pcm512x_autom_text);

static const char * const pcm512x_ramp_rate_text[] = {
	"1 sample/update", "2 samples/update", "4 samples/update",
	"Immediate"
};

static const struct soc_enum pcm512x_vndf =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNDF_SHIFT, 4,
			pcm512x_ramp_rate_text);

static const struct soc_enum pcm512x_vnuf =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNUF_SHIFT, 4,
			pcm512x_ramp_rate_text);

static const struct soc_enum pcm512x_vedf =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_2, PCM512x_VEDF_SHIFT, 4,
			pcm512x_ramp_rate_text);

static const char * const pcm512x_ramp_step_text[] = {
	"4dB/step", "2dB/step", "1dB/step", "0.5dB/step"
};

static const struct soc_enum pcm512x_vnds =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNDS_SHIFT, 4,
			pcm512x_ramp_step_text);

static const struct soc_enum pcm512x_vnus =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNUS_SHIFT, 4,
			pcm512x_ramp_step_text);

static const struct soc_enum pcm512x_veds =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_2, PCM512x_VEDS_SHIFT, 4,
			pcm512x_ramp_step_text);

static const struct snd_kcontrol_new pcm512x_controls[] = {
SOC_DOUBLE_R_TLV("Digital Playback Volume", PCM512x_DIGITAL_VOLUME_2,
		 PCM512x_DIGITAL_VOLUME_3, 0, 255, 1, digital_tlv),
SOC_DOUBLE_TLV("Playback Volume", PCM512x_ANALOG_GAIN_CTRL,
	       PCM512x_LAGN_SHIFT, PCM512x_RAGN_SHIFT, 1, 1, analog_tlv),
SOC_DOUBLE_TLV("Playback Boost Volume", PCM512x_ANALOG_GAIN_BOOST,
	       PCM512x_AGBL_SHIFT, PCM512x_AGBR_SHIFT, 1, 0, boost_tlv),
SOC_DOUBLE("Digital Playback Switch", PCM512x_MUTE, PCM512x_RQML_SHIFT,
	   PCM512x_RQMR_SHIFT, 1, 1),

SOC_SINGLE("Deemphasis Switch", PCM512x_DSP, PCM512x_DEMP_SHIFT, 1, 1),
SOC_ENUM("DSP Program", pcm512x_dsp_program),

SOC_ENUM("Clock Missing Period", pcm512x_clk_missing),
SOC_ENUM("Auto Mute Time Left", pcm512x_autom_l),
SOC_ENUM("Auto Mute Time Right", pcm512x_autom_r),
SOC_SINGLE("Auto Mute Mono Switch", PCM512x_DIGITAL_MUTE_3,
	   PCM512x_ACTL_SHIFT, 1, 0),
SOC_DOUBLE("Auto Mute Switch", PCM512x_DIGITAL_MUTE_3, PCM512x_AMLE_SHIFT,
	   PCM512x_AMRE_SHIFT, 1, 0),

SOC_ENUM("Volume Ramp Down Rate", pcm512x_vndf),
SOC_ENUM("Volume Ramp Down Step", pcm512x_vnds),
SOC_ENUM("Volume Ramp Up Rate", pcm512x_vnuf),
SOC_ENUM("Volume Ramp Up Step", pcm512x_vnus),
SOC_ENUM("Volume Ramp Down Emergency Rate", pcm512x_vedf),
SOC_ENUM("Volume Ramp Down Emergency Step", pcm512x_veds),
};

static const struct snd_soc_dapm_widget pcm512x_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DACL", NULL, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_DAC("DACR", NULL, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_OUTPUT("OUTL"),
SND_SOC_DAPM_OUTPUT("OUTR"),
};

static const struct snd_soc_dapm_route pcm512x_dapm_routes[] = {
	{ "DACL", NULL, "Playback" },
	{ "DACR", NULL, "Playback" },

	{ "OUTL", NULL, "DACL" },
	{ "OUTR", NULL, "DACR" },
};

static const u32 pcm512x_dai_rates[] = {
	8000, 11025, 16000, 22050, 32000, 44100, 48000, 64000,
	88200, 96000, 176400, 192000, 384000,
};

static const struct snd_pcm_hw_constraint_list constraints_slave = {
	.count = ARRAY_SIZE(pcm512x_dai_rates),
	.list  = pcm512x_dai_rates,
};

static int pcm512x_dai_startup_master(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm512x_priv *pcm512x = snd_soc_codec_get_drvdata(codec);
	struct device *dev = dai->dev;
	struct snd_pcm_hw_constraint_ratnums *constraints_no_pll;
	struct snd_ratnum *rats_no_pll;

	if (IS_ERR(pcm512x->sclk)) {
		dev_err(dev, "Need SCLK for master mode: %ld\n",
			PTR_ERR(pcm512x->sclk));
		return PTR_ERR(pcm512x->sclk);
	}

	constraints_no_pll = devm_kzalloc(dev, sizeof(*constraints_no_pll),
					  GFP_KERNEL);
	if (!constraints_no_pll)
		return -ENOMEM;
	constraints_no_pll->nrats = 1;
	rats_no_pll = devm_kzalloc(dev, sizeof(*rats_no_pll), GFP_KERNEL);
	if (!rats_no_pll)
		return -ENOMEM;
	constraints_no_pll->rats = rats_no_pll;
	rats_no_pll->num = clk_get_rate(pcm512x->sclk) / 64;
	rats_no_pll->den_min = 1;
	rats_no_pll->den_max = 128;
	rats_no_pll->den_step = 1;

	return snd_pcm_hw_constraint_ratnums(substream->runtime, 0,
					     SNDRV_PCM_HW_PARAM_RATE,
					     constraints_no_pll);
}

static int pcm512x_dai_startup_slave(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm512x_priv *pcm512x = snd_soc_codec_get_drvdata(codec);
	struct device *dev = dai->dev;
	struct regmap *regmap = pcm512x->regmap;

	if (IS_ERR(pcm512x->sclk)) {
		dev_info(dev, "No SCLK, using BCLK: %ld\n",
			 PTR_ERR(pcm512x->sclk));

		/* Disable reporting of missing SCLK as an error */
		regmap_update_bits(regmap, PCM512x_ERROR_DETECT,
				   PCM512x_IDCH, PCM512x_IDCH);

		/* Switch PLL input to BCLK */
		regmap_update_bits(regmap, PCM512x_PLL_REF,
				   PCM512x_SREF, PCM512x_SREF_BCK);
	}

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &constraints_slave);
}

static int pcm512x_dai_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm512x_priv *pcm512x = snd_soc_codec_get_drvdata(codec);

	switch (pcm512x->fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		return pcm512x_dai_startup_master(substream, dai);

	case SND_SOC_DAIFMT_CBS_CFS:
		return pcm512x_dai_startup_slave(substream, dai);

	default:
		return -EINVAL;
	}
}

static int pcm512x_set_bias_level(struct snd_soc_codec *codec,
				  enum snd_soc_bias_level level)
{
	struct pcm512x_priv *pcm512x = dev_get_drvdata(codec->dev);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
					 PCM512x_RQST, 0);
		if (ret != 0) {
			dev_err(codec->dev, "Failed to remove standby: %d\n",
				ret);
			return ret;
		}
		break;

	case SND_SOC_BIAS_OFF:
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
					 PCM512x_RQST, PCM512x_RQST);
		if (ret != 0) {
			dev_err(codec->dev, "Failed to request standby: %d\n",
				ret);
			return ret;
		}
		break;
	}

	codec->dapm.bias_level = level;

	return 0;
}

static int pcm512x_set_dividers(struct snd_soc_dai *dai,
				struct snd_pcm_hw_params *params)
{
	struct device *dev = dai->dev;
	struct snd_soc_codec *codec = dai->codec;
	struct pcm512x_priv *pcm512x = snd_soc_codec_get_drvdata(codec);
	unsigned long sck_rate;
	unsigned long mck_rate;
	unsigned long bclk_rate;
	unsigned long sample_rate;
	unsigned long osr_rate;
	int bclk_div;
	int lrclk_div;
	int dsp_div;
	int dac_div;
	unsigned long dac_rate;
	int ncp_div;
	int osr_div;
	unsigned long dac_mul;
	unsigned long sck_mul;
	int ret;
	int idac;
	int fssp;

	lrclk_div = snd_soc_params_to_frame_size(params);
	if (lrclk_div == 0) {
		dev_err(dev, "No LRCLK?\n");
		return -EINVAL;
	}

	sck_rate = clk_get_rate(pcm512x->sclk);
	bclk_div = params->rate_den * 64 / lrclk_div;
	bclk_rate = DIV_ROUND_CLOSEST(sck_rate, bclk_div);

	mck_rate = sck_rate;

	if (bclk_div > 128) {
		dev_err(dev, "Failed to find BCLK divider\n");
		return -EINVAL;
	}

	/* the actual rate */
	sample_rate = sck_rate / bclk_div / lrclk_div;
	osr_rate = 16 * sample_rate;

	/* run DSP no faster than 50 MHz */
	dsp_div = mck_rate > 50000000 ? 2 : 1;

	/* run DAC no faster than 6144000 Hz */
	dac_mul = 6144000 / osr_rate;
	sck_mul = sck_rate / osr_rate;
	for (; dac_mul; dac_mul--) {
		if (!(sck_mul % dac_mul))
			break;
	}
	if (!dac_mul) {
		dev_err(dev, "Failed to find DAC rate\n");
		return -EINVAL;
	}

	dac_rate = dac_mul * osr_rate;
	dev_dbg(dev, "dac_rate %lu sample_rate %lu\n", dac_rate, sample_rate);

	dac_div = DIV_ROUND_CLOSEST(sck_rate, dac_rate);
	if (dac_div > 128) {
		dev_err(dev, "Failed to find DAC divider\n");
		return -EINVAL;
	}

	ncp_div = DIV_ROUND_CLOSEST(sck_rate / dac_div, 1536000);
	if (ncp_div > 128 || sck_rate / dac_div / ncp_div > 2048000) {
		/* run NCP no faster than 2048000 Hz, but why? */
		ncp_div = DIV_ROUND_UP(sck_rate / dac_div, 2048000);
		if (ncp_div > 128) {
			dev_err(dev, "Failed to find NCP divider\n");
			return -EINVAL;
		}
	}

	osr_div = DIV_ROUND_CLOSEST(dac_rate, osr_rate);
	if (osr_div > 128) {
		dev_err(dev, "Failed to find OSR divider\n");
		return -EINVAL;
	}

	idac = mck_rate / (dsp_div * sample_rate);

	ret = regmap_write(pcm512x->regmap, PCM512x_DSP_CLKDIV, dsp_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write DSP divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_DAC_CLKDIV, dac_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write DAC divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_NCP_CLKDIV, ncp_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write NCP divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_OSR_CLKDIV, osr_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write OSR divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap,
			   PCM512x_MASTER_CLKDIV_1, bclk_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write BCLK divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap,
			   PCM512x_MASTER_CLKDIV_2, lrclk_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write LRCLK divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_IDAC_1, idac >> 8);
	if (ret != 0) {
		dev_err(dev, "Failed to write IDAC msb divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_IDAC_2, idac & 0xff);
	if (ret != 0) {
		dev_err(dev, "Failed to write IDAC lsb divider: %d\n", ret);
		return ret;
	}

	if (sample_rate <= 48000)
		fssp = PCM512x_FSSP_48KHZ;
	else if (sample_rate <= 96000)
		fssp = PCM512x_FSSP_96KHZ;
	else if (sample_rate <= 192000)
		fssp = PCM512x_FSSP_192KHZ;
	else
		fssp = PCM512x_FSSP_384KHZ;
	ret = regmap_update_bits(pcm512x->regmap, PCM512x_FS_SPEED_MODE,
				 PCM512x_FSSP, fssp);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set fs speed: %d\n", ret);
		return ret;
	}

	dev_dbg(codec->dev, "DSP divider %d\n", dsp_div);
	dev_dbg(codec->dev, "DAC divider %d\n", dac_div);
	dev_dbg(codec->dev, "NCP divider %d\n", ncp_div);
	dev_dbg(codec->dev, "OSR divider %d\n", osr_div);
	dev_dbg(codec->dev, "BCK divider %d\n", bclk_div);
	dev_dbg(codec->dev, "LRCK divider %d\n", lrclk_div);
	dev_dbg(codec->dev, "IDAC %d\n", idac);
	dev_dbg(codec->dev, "1<<FSSP %d\n", 1 << fssp);

	return 0;
}

static int pcm512x_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm512x_priv *pcm512x = snd_soc_codec_get_drvdata(codec);
	int alen;
	int ret;

	dev_dbg(codec->dev, "hw_params %u Hz, %u channels\n",
		params_rate(params),
		params_channels(params));

	switch (snd_pcm_format_width(params_format(params))) {
	case 16:
		alen = PCM512x_ALEN_16;
		break;
	case 20:
		alen = PCM512x_ALEN_20;
		break;
	case 24:
		alen = PCM512x_ALEN_24;
		break;
	case 32:
		alen = PCM512x_ALEN_32;
		break;
	default:
		dev_err(codec->dev, "Bad frame size: %d\n",
			snd_pcm_format_width(params_format(params)));
		return -EINVAL;
	}

	switch (pcm512x->fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		ret = regmap_update_bits(pcm512x->regmap,
					 PCM512x_BCLK_LRCLK_CFG,
					 PCM512x_BCKP
					 | PCM512x_BCKO | PCM512x_LRKO,
					 0);
		if (ret != 0) {
			dev_err(codec->dev,
				"Failed to enable slave mode: %d\n", ret);
			return ret;
		}

		ret = regmap_update_bits(pcm512x->regmap, PCM512x_ERROR_DETECT,
					 PCM512x_DCAS, 0);
		if (ret != 0) {
			dev_err(codec->dev,
				"Failed to enable clock divider autoset: %d\n",
				ret);
			return ret;
		}
		return 0;
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_I2S_1,
				 PCM512x_ALEN, alen);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set frame size: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_ERROR_DETECT,
				 PCM512x_IDFS | PCM512x_IDBK
				 | PCM512x_IDSK | PCM512x_IDCH
				 | PCM512x_IDCM | PCM512x_DCAS
				 | PCM512x_IPLK,
				 PCM512x_IDFS | PCM512x_IDBK
				 | PCM512x_IDSK | PCM512x_IDCH
				 | PCM512x_DCAS | PCM512x_IPLK);
	if (ret != 0) {
		dev_err(codec->dev,
			"Failed to ignore auto-clock failures: %d\n",
			ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_PLL_EN,
				 PCM512x_PLLE, 0);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to disable pll: %d\n", ret);
		return ret;
	}

	ret = pcm512x_set_dividers(dai, params);
	if (ret != 0)
		return ret;

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_DAC_REF,
				 PCM512x_SDAC, PCM512x_SDAC_SCK);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set sck as dacref: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_BCLK_LRCLK_CFG,
				 PCM512x_BCKP | PCM512x_BCKO | PCM512x_LRKO,
				 PCM512x_BCKO | PCM512x_LRKO);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable clock output: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_MASTER_MODE,
				 PCM512x_RLRK | PCM512x_RBCK,
				 PCM512x_RLRK | PCM512x_RBCK);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable master mode: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_SYNCHRONIZE,
				 PCM512x_RQSY, PCM512x_RQSY_HALT);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to halt clocks: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_SYNCHRONIZE,
				 PCM512x_RQSY, PCM512x_RQSY_RESUME);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to resume clocks: %d\n", ret);
		return ret;
	}

	return 0;
}

static int pcm512x_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm512x_priv *pcm512x = snd_soc_codec_get_drvdata(codec);

	pcm512x->fmt = fmt;

	return 0;
}

static const struct snd_soc_dai_ops pcm512x_dai_ops = {
	.startup = pcm512x_dai_startup,
	.hw_params = pcm512x_hw_params,
	.set_fmt = pcm512x_set_fmt,
};

static struct snd_soc_dai_driver pcm512x_dai = {
	.name = "pcm512x-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 8000,
		.rate_max = 384000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE
	},
	.ops = &pcm512x_dai_ops,
};

static struct snd_soc_codec_driver pcm512x_codec_driver = {
	.set_bias_level = pcm512x_set_bias_level,
	.idle_bias_off = true,

	.controls = pcm512x_controls,
	.num_controls = ARRAY_SIZE(pcm512x_controls),
	.dapm_widgets = pcm512x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(pcm512x_dapm_widgets),
	.dapm_routes = pcm512x_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(pcm512x_dapm_routes),
};

static const struct regmap_range_cfg pcm512x_range = {
	.name = "Pages", .range_min = PCM512x_VIRT_BASE,
	.range_max = PCM512x_MAX_REGISTER,
	.selector_reg = PCM512x_PAGE,
	.selector_mask = 0xff,
	.window_start = 0, .window_len = 0x100,
};

const struct regmap_config pcm512x_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = pcm512x_readable,
	.volatile_reg = pcm512x_volatile,

	.ranges = &pcm512x_range,
	.num_ranges = 1,

	.max_register = PCM512x_MAX_REGISTER,
	.reg_defaults = pcm512x_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(pcm512x_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(pcm512x_regmap);

int pcm512x_probe(struct device *dev, struct regmap *regmap)
{
	struct pcm512x_priv *pcm512x;
	int i, ret;

	pcm512x = devm_kzalloc(dev, sizeof(struct pcm512x_priv), GFP_KERNEL);
	if (!pcm512x)
		return -ENOMEM;

	dev_set_drvdata(dev, pcm512x);
	pcm512x->regmap = regmap;

	for (i = 0; i < ARRAY_SIZE(pcm512x->supplies); i++)
		pcm512x->supplies[i].supply = pcm512x_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(pcm512x->supplies),
				      pcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to get supplies: %d\n", ret);
		return ret;
	}

	pcm512x->supply_nb[0].notifier_call = pcm512x_regulator_event_0;
	pcm512x->supply_nb[1].notifier_call = pcm512x_regulator_event_1;
	pcm512x->supply_nb[2].notifier_call = pcm512x_regulator_event_2;

	for (i = 0; i < ARRAY_SIZE(pcm512x->supplies); i++) {
		ret = regulator_register_notifier(pcm512x->supplies[i].consumer,
						  &pcm512x->supply_nb[i]);
		if (ret != 0) {
			dev_err(dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(pcm512x->supplies),
				    pcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	/* Reset the device, verifying I/O in the process for I2C */
	ret = regmap_write(regmap, PCM512x_RESET,
			   PCM512x_RSTM | PCM512x_RSTR);
	if (ret != 0) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err;
	}

	ret = regmap_write(regmap, PCM512x_RESET, 0);
	if (ret != 0) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err;
	}

	pcm512x->sclk = devm_clk_get(dev, NULL);
	if (PTR_ERR(pcm512x->sclk) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (!IS_ERR(pcm512x->sclk)) {
		ret = clk_prepare_enable(pcm512x->sclk);
		if (ret != 0) {
			dev_err(dev, "Failed to enable SCLK: %d\n", ret);
			return ret;
		}
	}

	/* Default to standby mode */
	ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
				 PCM512x_RQST, PCM512x_RQST);
	if (ret != 0) {
		dev_err(dev, "Failed to request standby: %d\n",
			ret);
		goto err_clk;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = snd_soc_register_codec(dev, &pcm512x_codec_driver,
				    &pcm512x_dai, 1);
	if (ret != 0) {
		dev_err(dev, "Failed to register CODEC: %d\n", ret);
		goto err_pm;
	}

	return 0;

err_pm:
	pm_runtime_disable(dev);
err_clk:
	if (!IS_ERR(pcm512x->sclk))
		clk_disable_unprepare(pcm512x->sclk);
err:
	regulator_bulk_disable(ARRAY_SIZE(pcm512x->supplies),
				     pcm512x->supplies);
	return ret;
}
EXPORT_SYMBOL_GPL(pcm512x_probe);

void pcm512x_remove(struct device *dev)
{
	struct pcm512x_priv *pcm512x = dev_get_drvdata(dev);

	snd_soc_unregister_codec(dev);
	pm_runtime_disable(dev);
	if (!IS_ERR(pcm512x->sclk))
		clk_disable_unprepare(pcm512x->sclk);
	regulator_bulk_disable(ARRAY_SIZE(pcm512x->supplies),
			       pcm512x->supplies);
}
EXPORT_SYMBOL_GPL(pcm512x_remove);

#ifdef CONFIG_PM
static int pcm512x_suspend(struct device *dev)
{
	struct pcm512x_priv *pcm512x = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
				 PCM512x_RQPD, PCM512x_RQPD);
	if (ret != 0) {
		dev_err(dev, "Failed to request power down: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(pcm512x->supplies),
				     pcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to disable supplies: %d\n", ret);
		return ret;
	}

	if (!IS_ERR(pcm512x->sclk))
		clk_disable_unprepare(pcm512x->sclk);

	return 0;
}

static int pcm512x_resume(struct device *dev)
{
	struct pcm512x_priv *pcm512x = dev_get_drvdata(dev);
	int ret;

	if (!IS_ERR(pcm512x->sclk)) {
		ret = clk_prepare_enable(pcm512x->sclk);
		if (ret != 0) {
			dev_err(dev, "Failed to enable SCLK: %d\n", ret);
			return ret;
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(pcm512x->supplies),
				    pcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	regcache_cache_only(pcm512x->regmap, false);
	ret = regcache_sync(pcm512x->regmap);
	if (ret != 0) {
		dev_err(dev, "Failed to sync cache: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
				 PCM512x_RQPD, 0);
	if (ret != 0) {
		dev_err(dev, "Failed to remove power down: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

const struct dev_pm_ops pcm512x_pm_ops = {
	SET_RUNTIME_PM_OPS(pcm512x_suspend, pcm512x_resume, NULL)
};
EXPORT_SYMBOL_GPL(pcm512x_pm_ops);

MODULE_DESCRIPTION("ASoC PCM512x codec driver");
MODULE_AUTHOR("Mark Brown <broonie@linaro.org>");
MODULE_LICENSE("GPL v2");

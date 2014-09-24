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
	   PCM512x_AMLR_SHIFT, 1, 0),

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

static struct snd_soc_dai_driver pcm512x_dai = {
	.name = "pcm512x-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE
	},
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
	if (IS_ERR(pcm512x->sclk)) {
		if (PTR_ERR(pcm512x->sclk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		dev_info(dev, "No SCLK, using BCLK: %ld\n",
			 PTR_ERR(pcm512x->sclk));

		/* Disable reporting of missing SCLK as an error */
		regmap_update_bits(regmap, PCM512x_ERROR_DETECT,
				   PCM512x_IDCH, PCM512x_IDCH);

		/* Switch PLL input to BCLK */
		regmap_update_bits(regmap, PCM512x_PLL_REF,
				   PCM512x_SREF, PCM512x_SREF);
	} else {
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

#ifdef CONFIG_PM_RUNTIME
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

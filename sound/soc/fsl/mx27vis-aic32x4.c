// SPDX-License-Identifier: GPL-2.0+
//
// mx27vis-aic32x4.c
//
// Copyright 2011 Vista Silicon S.L.
//
// Author: Javier Martin <javier.martin@vista-silicon.com>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/platform_data/asoc-mx27vis.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <asm/mach-types.h>

#include "../codecs/tlv320aic32x4.h"
#include "imx-ssi.h"
#include "imx-audmux.h"

#define MX27VIS_AMP_GAIN	0
#define MX27VIS_AMP_MUTE	1

static int mx27vis_amp_gain;
static int mx27vis_amp_mute;
static int mx27vis_amp_gain0_gpio;
static int mx27vis_amp_gain1_gpio;
static int mx27vis_amp_mutel_gpio;
static int mx27vis_amp_muter_gpio;

static int mx27vis_aic32x4_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
				     25000000, SND_SOC_CLOCK_OUT);
	if (ret) {
		pr_err("%s: failed setting codec sysclk\n", __func__);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, IMX_SSP_SYS_CLK, 0,
				SND_SOC_CLOCK_IN);
	if (ret) {
		pr_err("can't set CPU system clock IMX_SSP_SYS_CLK\n");
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops mx27vis_aic32x4_snd_ops = {
	.hw_params	= mx27vis_aic32x4_hw_params,
};

static int mx27vis_amp_set(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value = ucontrol->value.integer.value[0];
	unsigned int reg = mc->reg;
	int max = mc->max;

	if (value > max)
		return -EINVAL;

	switch (reg) {
	case MX27VIS_AMP_GAIN:
		gpio_set_value(mx27vis_amp_gain0_gpio, value & 1);
		gpio_set_value(mx27vis_amp_gain1_gpio, value >> 1);
		mx27vis_amp_gain = value;
		break;
	case MX27VIS_AMP_MUTE:
		gpio_set_value(mx27vis_amp_mutel_gpio, value & 1);
		gpio_set_value(mx27vis_amp_muter_gpio, value >> 1);
		mx27vis_amp_mute = value;
		break;
	}
	return 0;
}

static int mx27vis_amp_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;

	switch (reg) {
	case MX27VIS_AMP_GAIN:
		ucontrol->value.integer.value[0] = mx27vis_amp_gain;
		break;
	case MX27VIS_AMP_MUTE:
		ucontrol->value.integer.value[0] = mx27vis_amp_mute;
		break;
	}
	return 0;
}

/* From 6dB to 24dB in steps of 6dB */
static const DECLARE_TLV_DB_SCALE(mx27vis_amp_tlv, 600, 600, 0);

static const struct snd_kcontrol_new mx27vis_aic32x4_controls[] = {
	SOC_DAPM_PIN_SWITCH("External Mic"),
	SOC_SINGLE_EXT_TLV("LO Ext Boost", MX27VIS_AMP_GAIN, 0, 3, 0,
		       mx27vis_amp_get, mx27vis_amp_set, mx27vis_amp_tlv),
	SOC_DOUBLE_EXT("LO Ext Mute Switch", MX27VIS_AMP_MUTE, 0, 1, 1, 0,
		       mx27vis_amp_get, mx27vis_amp_set),
};

static const struct snd_soc_dapm_widget aic32x4_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("External Mic", NULL),
};

static const struct snd_soc_dapm_route aic32x4_dapm_routes[] = {
	{"Mic Bias", NULL, "External Mic"},
	{"IN1_R", NULL, "Mic Bias"},
	{"IN2_R", NULL, "Mic Bias"},
	{"IN3_R", NULL, "Mic Bias"},
	{"IN1_L", NULL, "Mic Bias"},
	{"IN2_L", NULL, "Mic Bias"},
	{"IN3_L", NULL, "Mic Bias"},
};

SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_CPU("imx-ssi.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("tlv320aic32x4.0-0018",
				      "tlv320aic32x4-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("imx-ssi.0")));

static struct snd_soc_dai_link mx27vis_aic32x4_dai = {
	.name		= "tlv320aic32x4",
	.stream_name	= "TLV320AIC32X4",
	.dai_fmt	= SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF |
			  SND_SOC_DAIFMT_CBM_CFM,
	.ops		= &mx27vis_aic32x4_snd_ops,
	SND_SOC_DAILINK_REG(hifi),
};

static struct snd_soc_card mx27vis_aic32x4 = {
	.name		= "visstrim_m10-audio",
	.owner		= THIS_MODULE,
	.dai_link	= &mx27vis_aic32x4_dai,
	.num_links	= 1,
	.controls	= mx27vis_aic32x4_controls,
	.num_controls	= ARRAY_SIZE(mx27vis_aic32x4_controls),
	.dapm_widgets	= aic32x4_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aic32x4_dapm_widgets),
	.dapm_routes	= aic32x4_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(aic32x4_dapm_routes),
};

static int mx27vis_aic32x4_probe(struct platform_device *pdev)
{
	struct snd_mx27vis_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data supplied\n");
		return -EINVAL;
	}

	mx27vis_amp_gain0_gpio = pdata->amp_gain0_gpio;
	mx27vis_amp_gain1_gpio = pdata->amp_gain1_gpio;
	mx27vis_amp_mutel_gpio = pdata->amp_mutel_gpio;
	mx27vis_amp_muter_gpio = pdata->amp_muter_gpio;

	mx27vis_aic32x4.dev = &pdev->dev;
	ret = snd_soc_register_card(&mx27vis_aic32x4);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		return ret;
	}

	/* Connect SSI0 as clock slave to SSI1 external pins */
	imx_audmux_v1_configure_port(MX27_AUDMUX_HPCR1_SSI0,
			IMX_AUDMUX_V1_PCR_SYN |
			IMX_AUDMUX_V1_PCR_TFSDIR |
			IMX_AUDMUX_V1_PCR_TCLKDIR |
			IMX_AUDMUX_V1_PCR_TFCSEL(MX27_AUDMUX_PPCR1_SSI_PINS_1) |
			IMX_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_PPCR1_SSI_PINS_1)
	);
	imx_audmux_v1_configure_port(MX27_AUDMUX_PPCR1_SSI_PINS_1,
			IMX_AUDMUX_V1_PCR_SYN |
			IMX_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_HPCR1_SSI0)
	);

	return ret;
}

static int mx27vis_aic32x4_remove(struct platform_device *pdev)
{
	snd_soc_unregister_card(&mx27vis_aic32x4);

	return 0;
}

static struct platform_driver mx27vis_aic32x4_audio_driver = {
	.driver = {
		.name = "mx27vis",
	},
	.probe = mx27vis_aic32x4_probe,
	.remove = mx27vis_aic32x4_remove,
};

module_platform_driver(mx27vis_aic32x4_audio_driver);

MODULE_AUTHOR("Javier Martin <javier.martin@vista-silicon.com>");
MODULE_DESCRIPTION("ALSA SoC AIC32X4 mx27 visstrim");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mx27vis");

/*
 *  u1_mc1n2.c
 *
 *  Copyright (c) 2009 Samsung Electronics Co. Ltd
 *  Author: aitdark.park  <aitdark.park@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or  modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <mach/regs-clock.h>
#include <mach/pmu.h>
#include "../codecs/mc1n2/mc1n2.h"

static bool xclkout_enabled;

int mc1n2_set_mclk_source(bool on)
{
	if (on) {
		exynos4_pmu_xclkout_set(1, XCLKOUT_XUSBXTI);
		xclkout_enabled = true;
	} else {
		exynos4_pmu_xclkout_set(0, XCLKOUT_XUSBXTI);
		xclkout_enabled = false;
	}

	mdelay(10);

	return 0;
}
EXPORT_SYMBOL(mc1n2_set_mclk_source);

static int u1_hifi_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	/* Set the codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
				| SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* Set the cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
				| SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;
/* check later
	ret = snd_soc_dai_set_sysclk(cpu_dai, S3C_I2SV2_CLKSRC_CDCLK,
				0, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, S3C64XX_CLKSRC_MUX,
				0, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;
*/

	ret = snd_soc_dai_set_clkdiv(codec_dai, MC1N2_BCLK_MULT, MC1N2_LRCK_X32);

	if (ret < 0)
		return ret;

	return 0;
}

/* U1 Audio  extra controls */
#define U1_AUDIO_OFF	0
#define U1_HEADSET_OUT	1
#define U1_MIC_IN	2
#define U1_LINE_IN	3

static int u1_aud_mode;

static const char *u1_aud_scenario[] = {
	[U1_AUDIO_OFF] = "Off",
	[U1_HEADSET_OUT] = "Playback Headphones",
	[U1_MIC_IN] = "Capture Mic",
	[U1_LINE_IN] = "Capture Line",
};

static void u1_aud_ext_control(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	switch (u1_aud_mode) {
	case U1_HEADSET_OUT:
		snd_soc_dapm_enable_pin(dapm, "Headset Out");
		snd_soc_dapm_disable_pin(dapm, "Mic In");
		snd_soc_dapm_disable_pin(dapm, "Line In");
		break;
	case U1_MIC_IN:
		snd_soc_dapm_disable_pin(dapm, "Headset Out");
		snd_soc_dapm_enable_pin(dapm, "Mic In");
		snd_soc_dapm_disable_pin(dapm, "Line In");
		break;
	case U1_LINE_IN:
		snd_soc_dapm_disable_pin(dapm, "Headset Out");
		snd_soc_dapm_disable_pin(dapm, "Mic In");
		snd_soc_dapm_enable_pin(dapm, "Line In");
		break;
	case U1_AUDIO_OFF:
	default:
		snd_soc_dapm_disable_pin(dapm, "Headset Out");
		snd_soc_dapm_disable_pin(dapm, "Mic In");
		snd_soc_dapm_disable_pin(dapm, "Line In");
		break;
	}

	snd_soc_dapm_sync(dapm);
}

static int u1_mc1n2_getp(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = u1_aud_mode;
	return 0;
}

static int u1_mc1n2_setp(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (u1_aud_mode == ucontrol->value.integer.value[0])
		return 0;

	u1_aud_mode = ucontrol->value.integer.value[0];
	u1_aud_ext_control(codec);

	return 1;
}

static const struct snd_soc_dapm_widget u1_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headset Out", NULL),
	SND_SOC_DAPM_MIC("Mic In", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

static const struct soc_enum u1_aud_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(u1_aud_scenario),
			    u1_aud_scenario),
};

static const struct snd_kcontrol_new u1_aud_controls[] = {
	SOC_ENUM_EXT("U1 Audio Mode", u1_aud_enum[0],
		     u1_mc1n2_getp, u1_mc1n2_setp),
};

static const struct snd_soc_dapm_route u1_dapm_routes[] = {
	/* Connections to Headset */
	{"Headset Out", NULL, "HPOUT1L"},
	{"Headset Out", NULL, "HPOUT1R"},
	/* Connections to Mics */
	{"IN1LN", NULL, "Mic In"},
	{"IN1RN", NULL, "Mic In"},
	/* Connections to Line In */
	{"IN2LN", NULL, "Line In"},
	{"IN2RN", NULL, "Line In"},
};

static int u1_hifiaudio_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

/*
 * U1 MC1N2 DAI operations.
 */
static struct snd_soc_ops u1_hifi_ops = {
	.hw_params = u1_hifi_hw_params,
};

static struct snd_soc_dai_link u1_dai[] = {
#if defined(CONFIG_SND_SAMSUNG_LP) || defined(CONFIG_SND_SAMSUNG_ALP)
	{ /* Sec_Fifo DAI i/f */
		.name = "MC1N2 Sec_FIFO TX",
		.stream_name = "Sec_Dai",
		.cpu_dai_name = "samsung-i2s.4",
		.codec_dai_name = "mc1n2-da0i",
#ifndef CONFIG_SND_SOC_SAMSUNG_USE_DMA_WRAPPER
		.platform_name = "samsung-audio-idma",
#else
		.platform_name = "samsung-audio",
#endif
		.codec_name = "mc1n2.6-003a",
		.init = u1_hifiaudio_init,
		.ops = &u1_hifi_ops,
	},
#endif
	{ /* Primary DAI i/f */
		.name = "MC1N2 AIF1",
		.stream_name = "hifiaudio",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "mc1n2-da0i",
		.platform_name = "samsung-audio",
		.codec_name = "mc1n2.6-003a",
		.init = u1_hifiaudio_init,
		.ops = &u1_hifi_ops,
	}
};

static int u1_card_suspend(struct snd_soc_card *card)
{
	exynos4_sys_powerdown_xusbxti_control(xclkout_enabled ? 1 : 0);

	return 0;
}

static struct snd_soc_card u1_snd_card = {
	.name = "U1-YMU823",
	.dai_link = u1_dai,
	.num_links = ARRAY_SIZE(u1_dai),

	.suspend_post = u1_card_suspend,
};

/* setup codec data from mc1n2 codec driver */
extern void set_mc1n2_codec_data(struct mc1n2_setup *setup);

static struct platform_device *u1_snd_device;

static int __init u1_audio_init(void)
{
	int ret;

	mc1n2_set_mclk_source(1);

	u1_snd_device = platform_device_alloc("soc-audio", -1);
	if (!u1_snd_device)
		return -ENOMEM;

	platform_set_drvdata(u1_snd_device, &u1_snd_card);

	ret = platform_device_add(u1_snd_device);
	if (ret) {
		platform_device_put(u1_snd_device);
	}

	return ret;
}

static void __exit u1_audio_exit(void)
{
	platform_device_unregister(u1_snd_device);
}

module_init(u1_audio_init);

MODULE_AUTHOR("aitdark, aitdark.park@samsung.com");
MODULE_DESCRIPTION("ALSA SoC U1 MC1N2");
MODULE_LICENSE("GPL");

/*
 * neo1973_gta02_wm8753.c  --  SoC audio for Openmoko Freerunner(GTA02)
 *
 * Copyright 2007 Openmoko Inc
 * Author: Graeme Gregory <graeme@openmoko.org>
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory <linux@wolfsonmicro.com>
 * Copyright 2009 Wolfson Microelectronics
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>

#include <plat/regs-iis.h>

#include <mach/regs-clock.h>
#include <asm/io.h>
#include <mach/gta02.h>
#include "../codecs/wm8753.h"
#include "dma.h"
#include "s3c24xx-i2s.h"

static struct snd_soc_card neo1973_gta02;

static int neo1973_gta02_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0, bclk = 0;
	int ret = 0;
	unsigned long iis_clkrate;

	iis_clkrate = s3c24xx_i2s_get_clockrate();

	switch (params_rate(params)) {
	case 8000:
	case 16000:
		pll_out = 12288000;
		break;
	case 48000:
		bclk = WM8753_BCLK_DIV_4;
		pll_out = 12288000;
		break;
	case 96000:
		bclk = WM8753_BCLK_DIV_2;
		pll_out = 12288000;
		break;
	case 11025:
		bclk = WM8753_BCLK_DIV_16;
		pll_out = 11289600;
		break;
	case 22050:
		bclk = WM8753_BCLK_DIV_8;
		pll_out = 11289600;
		break;
	case 44100:
		bclk = WM8753_BCLK_DIV_4;
		pll_out = 11289600;
		break;
	case 88200:
		bclk = WM8753_BCLK_DIV_2;
		pll_out = 11289600;
		break;
	}

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8753_MCLK, pll_out,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set MCLK division for sample rate */
	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_MCLK,
		S3C2410_IISMOD_32FS);
	if (ret < 0)
		return ret;

	/* set codec BCLK division for sample rate */
	ret = snd_soc_dai_set_clkdiv(codec_dai,
					WM8753_BCLKDIV, bclk);
	if (ret < 0)
		return ret;

	/* set prescaler division for sample rate */
	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_PRESCALER,
		S3C24XX_PRESCALE(4, 4));
	if (ret < 0)
		return ret;

	/* codec PLL input is PCLK/4 */
	ret = snd_soc_dai_set_pll(codec_dai, WM8753_PLL1, 0,
		iis_clkrate / 4, pll_out);
	if (ret < 0)
		return ret;

	return 0;
}

static int neo1973_gta02_hifi_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	/* disable the PLL */
	return snd_soc_dai_set_pll(codec_dai, WM8753_PLL1, 0, 0, 0);
}

/*
 * Neo1973 WM8753 HiFi DAI opserations.
 */
static struct snd_soc_ops neo1973_gta02_hifi_ops = {
	.hw_params = neo1973_gta02_hifi_hw_params,
	.hw_free = neo1973_gta02_hifi_hw_free,
};

static int neo1973_gta02_voice_hw_params(
	struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int pcmdiv = 0;
	int ret = 0;
	unsigned long iis_clkrate;

	iis_clkrate = s3c24xx_i2s_get_clockrate();

	if (params_rate(params) != 8000)
		return -EINVAL;
	if (params_channels(params) != 1)
		return -EINVAL;

	pcmdiv = WM8753_PCM_DIV_6; /* 2.048 MHz */

	/* todo: gg check mode (DSP_B) against CSR datasheet */
	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_B |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8753_PCMCLK,
		12288000, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set codec PCM division for sample rate */
	ret = snd_soc_dai_set_clkdiv(codec_dai, WM8753_PCMDIV,
					pcmdiv);
	if (ret < 0)
		return ret;

	/* configure and enable PLL for 12.288MHz output */
	ret = snd_soc_dai_set_pll(codec_dai, WM8753_PLL2, 0,
		iis_clkrate / 4, 12288000);
	if (ret < 0)
		return ret;

	return 0;
}

static int neo1973_gta02_voice_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	/* disable the PLL */
	return snd_soc_dai_set_pll(codec_dai, WM8753_PLL2, 0, 0, 0);
}

static struct snd_soc_ops neo1973_gta02_voice_ops = {
	.hw_params = neo1973_gta02_voice_hw_params,
	.hw_free = neo1973_gta02_voice_hw_free,
};

#define LM4853_AMP 1
#define LM4853_SPK 2

static u8 lm4853_state;

/* This has no effect, it exists only to maintain compatibility with
 * existing ALSA state files.
 */
static int lm4853_set_state(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int val = ucontrol->value.integer.value[0];

	if (val)
		lm4853_state |= LM4853_AMP;
	else
		lm4853_state &= ~LM4853_AMP;

	return 0;
}

static int lm4853_get_state(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = lm4853_state & LM4853_AMP;

	return 0;
}

static int lm4853_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int val = ucontrol->value.integer.value[0];

	if (val) {
		lm4853_state |= LM4853_SPK;
		gpio_set_value(GTA02_GPIO_HP_IN, 0);
	} else {
		lm4853_state &= ~LM4853_SPK;
		gpio_set_value(GTA02_GPIO_HP_IN, 1);
	}

	return 0;
}

static int lm4853_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = (lm4853_state & LM4853_SPK) >> 1;

	return 0;
}

static int lm4853_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k,
			int event)
{
	gpio_set_value(GTA02_GPIO_AMP_SHUT, SND_SOC_DAPM_EVENT_OFF(event));

	return 0;
}

static const struct snd_soc_dapm_widget wm8753_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Stereo Out", lm4853_event),
	SND_SOC_DAPM_LINE("GSM Line Out", NULL),
	SND_SOC_DAPM_LINE("GSM Line In", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Handset Mic", NULL),
	SND_SOC_DAPM_SPK("Handset Spk", NULL),
};


/* example machine audio_mapnections */
static const struct snd_soc_dapm_route audio_map[] = {

	/* Connections to the lm4853 amp */
	{"Stereo Out", NULL, "LOUT1"},
	{"Stereo Out", NULL, "ROUT1"},

	/* Connections to the GSM Module */
	{"GSM Line Out", NULL, "MONO1"},
	{"GSM Line Out", NULL, "MONO2"},
	{"RXP", NULL, "GSM Line In"},
	{"RXN", NULL, "GSM Line In"},

	/* Connections to Headset */
	{"MIC1", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Headset Mic"},

	/* Call Mic */
	{"MIC2", NULL, "Mic Bias"},
	{"MIC2N", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Handset Mic"},

	/* Call Speaker */
	{"Handset Spk", NULL, "LOUT2"},
	{"Handset Spk", NULL, "ROUT2"},

	/* Connect the ALC pins */
	{"ACIN", NULL, "ACOP"},
};

static const struct snd_kcontrol_new wm8753_neo1973_gta02_controls[] = {
	SOC_DAPM_PIN_SWITCH("Stereo Out"),
	SOC_DAPM_PIN_SWITCH("GSM Line Out"),
	SOC_DAPM_PIN_SWITCH("GSM Line In"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Handset Mic"),
	SOC_DAPM_PIN_SWITCH("Handset Spk"),

	/* This has no effect, it exists only to maintain compatibility with
	 * existing ALSA state files.
	 */
	SOC_SINGLE_EXT("Amp State Switch", 6, 0, 1, 0,
		lm4853_get_state,
		lm4853_set_state),
	SOC_SINGLE_EXT("Amp Spk Switch", 7, 0, 1, 0,
		lm4853_get_spk,
		lm4853_set_spk),
};

/*
 * This is an example machine initialisation for a wm8753 connected to a
 * neo1973 GTA02.
 */
static int neo1973_gta02_wm8753_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int err;

	/* set up NC codec pins */
	snd_soc_dapm_nc_pin(dapm, "OUT3");
	snd_soc_dapm_nc_pin(dapm, "OUT4");
	snd_soc_dapm_nc_pin(dapm, "LINE1");
	snd_soc_dapm_nc_pin(dapm, "LINE2");

	/* Add neo1973 gta02 specific widgets */
	snd_soc_dapm_new_controls(dapm, wm8753_dapm_widgets,
				  ARRAY_SIZE(wm8753_dapm_widgets));

	/* add neo1973 gta02 specific controls */
	err = snd_soc_add_controls(codec, wm8753_neo1973_gta02_controls,
		ARRAY_SIZE(wm8753_neo1973_gta02_controls));

	if (err < 0)
		return err;

	/* set up neo1973 gta02 specific audio path audio_map */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	/* set endpoints to default off mode */
	snd_soc_dapm_disable_pin(dapm, "Stereo Out");
	snd_soc_dapm_disable_pin(dapm, "GSM Line Out");
	snd_soc_dapm_disable_pin(dapm, "GSM Line In");
	snd_soc_dapm_disable_pin(dapm, "Headset Mic");
	snd_soc_dapm_disable_pin(dapm, "Handset Mic");
	snd_soc_dapm_disable_pin(dapm, "Handset Spk");

	/* allow audio paths from the GSM modem to run during suspend */
	snd_soc_dapm_ignore_suspend(dapm, "Stereo Out");
	snd_soc_dapm_ignore_suspend(dapm, "GSM Line Out");
	snd_soc_dapm_ignore_suspend(dapm, "GSM Line In");
	snd_soc_dapm_ignore_suspend(dapm, "Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "Handset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "Handset Spk");

	snd_soc_dapm_sync(dapm);

	return 0;
}

/*
 * BT Codec DAI
 */
static struct snd_soc_dai_driver bt_dai = {
	.name = "bluetooth-dai",
	.playback = {
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
};

static struct snd_soc_dai_link neo1973_gta02_dai[] = {
{ /* Hifi Playback - for similatious use with voice below */
	.name = "WM8753",
	.stream_name = "WM8753 HiFi",
	.cpu_dai_name = "s3c24xx-i2s",
	.codec_dai_name = "wm8753-hifi",
	.init = neo1973_gta02_wm8753_init,
	.platform_name = "samsung-audio",
	.codec_name = "wm8753-codec.0-0x1a",
	.ops = &neo1973_gta02_hifi_ops,
},
{ /* Voice via BT */
	.name = "Bluetooth",
	.stream_name = "Voice",
	.cpu_dai_name = "bluetooth-dai",
	.codec_dai_name = "wm8753-voice",
	.ops = &neo1973_gta02_voice_ops,
	.codec_name = "wm8753-codec.0-0x1a",
	.platform_name = "samsung-audio",
},
};

static struct snd_soc_card neo1973_gta02 = {
	.name = "neo1973-gta02",
	.dai_link = neo1973_gta02_dai,
	.num_links = ARRAY_SIZE(neo1973_gta02_dai),
};

static struct platform_device *neo1973_gta02_snd_device;

static int __init neo1973_gta02_init(void)
{
	int ret;

	if (!machine_is_neo1973_gta02()) {
		printk(KERN_INFO
		       "Only GTA02 is supported by this ASoC driver\n");
		return -ENODEV;
	}

	neo1973_gta02_snd_device = platform_device_alloc("soc-audio", -1);
	if (!neo1973_gta02_snd_device)
		return -ENOMEM;

	/* register bluetooth DAI here */
	ret = snd_soc_register_dai(&neo1973_gta02_snd_device->dev, &bt_dai);
	if (ret) {
		platform_device_put(neo1973_gta02_snd_device);
		return ret;
	}

	platform_set_drvdata(neo1973_gta02_snd_device, &neo1973_gta02);
	ret = platform_device_add(neo1973_gta02_snd_device);

	if (ret) {
		platform_device_put(neo1973_gta02_snd_device);
		return ret;
	}

	/* Initialise GPIOs used by amp */
	ret = gpio_request(GTA02_GPIO_HP_IN, "GTA02_HP_IN");
	if (ret) {
		pr_err("gta02_wm8753: Failed to register GPIO %d\n", GTA02_GPIO_HP_IN);
		goto err_unregister_device;
	}

	ret = gpio_direction_output(GTA02_GPIO_HP_IN, 1);
	if (ret) {
		pr_err("gta02_wm8753: Failed to configure GPIO %d\n", GTA02_GPIO_HP_IN);
		goto err_free_gpio_hp_in;
	}

	ret = gpio_request(GTA02_GPIO_AMP_SHUT, "GTA02_AMP_SHUT");
	if (ret) {
		pr_err("gta02_wm8753: Failed to register GPIO %d\n", GTA02_GPIO_AMP_SHUT);
		goto err_free_gpio_hp_in;
	}

	ret = gpio_direction_output(GTA02_GPIO_AMP_SHUT, 1);
	if (ret) {
		pr_err("gta02_wm8753: Failed to configure GPIO %d\n", GTA02_GPIO_AMP_SHUT);
		goto err_free_gpio_amp_shut;
	}

	return 0;

err_free_gpio_amp_shut:
	gpio_free(GTA02_GPIO_AMP_SHUT);
err_free_gpio_hp_in:
	gpio_free(GTA02_GPIO_HP_IN);
err_unregister_device:
	platform_device_unregister(neo1973_gta02_snd_device);
	return ret;
}
module_init(neo1973_gta02_init);

static void __exit neo1973_gta02_exit(void)
{
	snd_soc_unregister_dai(&neo1973_gta02_snd_device->dev);
	platform_device_unregister(neo1973_gta02_snd_device);
	gpio_free(GTA02_GPIO_HP_IN);
	gpio_free(GTA02_GPIO_AMP_SHUT);
}
module_exit(neo1973_gta02_exit);

/* Module information */
MODULE_AUTHOR("Graeme Gregory, graeme@openmoko.org");
MODULE_DESCRIPTION("ALSA SoC WM8753 Neo1973 GTA02");
MODULE_LICENSE("GPL");

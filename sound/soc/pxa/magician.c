/*
 * SoC audio for HTC Magician
 *
 * Copyright (c) 2006 Philipp Zabel <philipp.zabel@gmail.com>
 *
 * based on spitz.c,
 * Authors: Liam Girdwood <lrg@slimlogic.co.uk>
 *          Richard Purdie <richard@openedhand.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/uda1380.h>

#include <mach/magician.h>
#include <asm/mach-types.h>
#include "../codecs/uda1380.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-i2s.h"
#include "pxa-ssp.h"

#define MAGICIAN_MIC       0
#define MAGICIAN_MIC_EXT   1

static int magician_hp_switch;
static int magician_spk_switch = 1;
static int magician_in_sel = MAGICIAN_MIC;

static void magician_ext_control(struct snd_soc_codec *codec)
{
	if (magician_spk_switch)
		snd_soc_dapm_enable_pin(codec, "Speaker");
	else
		snd_soc_dapm_disable_pin(codec, "Speaker");
	if (magician_hp_switch)
		snd_soc_dapm_enable_pin(codec, "Headphone Jack");
	else
		snd_soc_dapm_disable_pin(codec, "Headphone Jack");

	switch (magician_in_sel) {
	case MAGICIAN_MIC:
		snd_soc_dapm_disable_pin(codec, "Headset Mic");
		snd_soc_dapm_enable_pin(codec, "Call Mic");
		break;
	case MAGICIAN_MIC_EXT:
		snd_soc_dapm_disable_pin(codec, "Call Mic");
		snd_soc_dapm_enable_pin(codec, "Headset Mic");
		break;
	}

	snd_soc_dapm_sync(codec);
}

static int magician_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->socdev->card->codec;

	/* check the jack status at stream startup */
	magician_ext_control(codec);

	return 0;
}

/*
 * Magician uses SSP port for playback.
 */
static int magician_playback_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	unsigned int acps, acds, width, rate;
	unsigned int div4 = PXA_SSP_CLK_SCDB_4;
	int ret = 0;

	rate = params_rate(params);
	width = snd_pcm_format_physical_width(params_format(params));

	/*
	 * rate = SSPSCLK / (2 * width(16 or 32))
	 * SSPSCLK = (ACPS / ACDS) / SSPSCLKDIV(div4 or div1)
	 */
	switch (params_rate(params)) {
	case 8000:
		/* off by a factor of 2: bug in the PXA27x audio clock? */
		acps = 32842000;
		switch (width) {
		case 16:
			/* 513156 Hz ~= _2_ * 8000 Hz * 32 (+0.23%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_16;
			break;
		default: /* 32 */
			/* 1026312 Hz ~= _2_ * 8000 Hz * 64 (+0.23%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_8;
		}
		break;
	case 11025:
		acps = 5622000;
		switch (width) {
		case 16:
			/* 351375 Hz ~= 11025 Hz * 32 (-0.41%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_4;
			break;
		default: /* 32 */
			/* 702750 Hz ~= 11025 Hz * 64 (-0.41%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_2;
		}
		break;
	case 22050:
		acps = 5622000;
		switch (width) {
		case 16:
			/* 702750 Hz ~= 22050 Hz * 32 (-0.41%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_2;
			break;
		default: /* 32 */
			/* 1405500 Hz ~= 22050 Hz * 64 (-0.41%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_1;
		}
		break;
	case 44100:
		acps = 5622000;
		switch (width) {
		case 16:
			/* 1405500 Hz ~= 44100 Hz * 32 (-0.41%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_2;
			break;
		default: /* 32 */
			/* 2811000 Hz ~= 44100 Hz * 64 (-0.41%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_1;
		}
		break;
	case 48000:
		acps = 12235000;
		switch (width) {
		case 16:
			/* 1529375 Hz ~= 48000 Hz * 32 (-0.44%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_2;
			break;
		default: /* 32 */
			/* 3058750 Hz ~= 48000 Hz * 64 (-0.44%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_1;
		}
		break;
	case 96000:
	default:
		acps = 12235000;
		switch (width) {
		case 16:
			/* 3058750 Hz ~= 96000 Hz * 32 (-0.44%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_1;
			break;
		default: /* 32 */
			/* 6117500 Hz ~= 96000 Hz * 64 (-0.44%) */
			acds = PXA_SSP_CLK_AUDIO_DIV_2;
			div4 = PXA_SSP_CLK_SCDB_1;
			break;
		}
		break;
	}

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_MSB |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_NB_IF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, 1, 0, 1, width);
	if (ret < 0)
		return ret;

	/* set audio clock as clock source */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA_SSP_CLK_AUDIO, 0,
			SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	/* set the SSP audio system clock ACDS divider */
	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			PXA_SSP_AUDIO_DIV_ACDS, acds);
	if (ret < 0)
		return ret;

	/* set the SSP audio system clock SCDB divider4 */
	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			PXA_SSP_AUDIO_DIV_SCDB, div4);
	if (ret < 0)
		return ret;

	/* set SSP audio pll clock */
	ret = snd_soc_dai_set_pll(cpu_dai, 0, 0, acps);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * Magician uses I2S for capture.
 */
static int magician_capture_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret = 0;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai,
			SND_SOC_DAIFMT_MSB | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai,
			SND_SOC_DAIFMT_MSB | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set the I2S system clock as output */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA2XX_I2S_SYSCLK, 0,
			SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops magician_capture_ops = {
	.startup = magician_startup,
	.hw_params = magician_capture_hw_params,
};

static struct snd_soc_ops magician_playback_ops = {
	.startup = magician_startup,
	.hw_params = magician_playback_hw_params,
};

static int magician_get_hp(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = magician_hp_switch;
	return 0;
}

static int magician_set_hp(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (magician_hp_switch == ucontrol->value.integer.value[0])
		return 0;

	magician_hp_switch = ucontrol->value.integer.value[0];
	magician_ext_control(codec);
	return 1;
}

static int magician_get_spk(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = magician_spk_switch;
	return 0;
}

static int magician_set_spk(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (magician_spk_switch == ucontrol->value.integer.value[0])
		return 0;

	magician_spk_switch = ucontrol->value.integer.value[0];
	magician_ext_control(codec);
	return 1;
}

static int magician_get_input(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = magician_in_sel;
	return 0;
}

static int magician_set_input(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	if (magician_in_sel == ucontrol->value.integer.value[0])
		return 0;

	magician_in_sel = ucontrol->value.integer.value[0];

	switch (magician_in_sel) {
	case MAGICIAN_MIC:
		gpio_set_value(EGPIO_MAGICIAN_IN_SEL1, 1);
		break;
	case MAGICIAN_MIC_EXT:
		gpio_set_value(EGPIO_MAGICIAN_IN_SEL1, 0);
	}

	return 1;
}

static int magician_spk_power(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	gpio_set_value(EGPIO_MAGICIAN_SPK_POWER, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static int magician_hp_power(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	gpio_set_value(EGPIO_MAGICIAN_EP_POWER, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static int magician_mic_bias(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	gpio_set_value(EGPIO_MAGICIAN_MIC_POWER, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

/* magician machine dapm widgets */
static const struct snd_soc_dapm_widget uda1380_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", magician_hp_power),
	SND_SOC_DAPM_SPK("Speaker", magician_spk_power),
	SND_SOC_DAPM_MIC("Call Mic", magician_mic_bias),
	SND_SOC_DAPM_MIC("Headset Mic", magician_mic_bias),
};

/* magician machine audio_map */
static const struct snd_soc_dapm_route audio_map[] = {

	/* Headphone connected to VOUTL, VOUTR */
	{"Headphone Jack", NULL, "VOUTL"},
	{"Headphone Jack", NULL, "VOUTR"},

	/* Speaker connected to VOUTL, VOUTR */
	{"Speaker", NULL, "VOUTL"},
	{"Speaker", NULL, "VOUTR"},

	/* Mics are connected to VINM */
	{"VINM", NULL, "Headset Mic"},
	{"VINM", NULL, "Call Mic"},
};

static const char *input_select[] = {"Call Mic", "Headset Mic"};
static const struct soc_enum magician_in_sel_enum =
	SOC_ENUM_SINGLE_EXT(2, input_select);

static const struct snd_kcontrol_new uda1380_magician_controls[] = {
	SOC_SINGLE_BOOL_EXT("Headphone Switch",
			(unsigned long)&magician_hp_switch,
			magician_get_hp, magician_set_hp),
	SOC_SINGLE_BOOL_EXT("Speaker Switch",
			(unsigned long)&magician_spk_switch,
			magician_get_spk, magician_set_spk),
	SOC_ENUM_EXT("Input Select", magician_in_sel_enum,
			magician_get_input, magician_set_input),
};

/*
 * Logic for a uda1380 as connected on a HTC Magician
 */
static int magician_uda1380_init(struct snd_soc_codec *codec)
{
	int err;

	/* NC codec pins */
	snd_soc_dapm_nc_pin(codec, "VOUTLHP");
	snd_soc_dapm_nc_pin(codec, "VOUTRHP");

	/* FIXME: is anything connected here? */
	snd_soc_dapm_nc_pin(codec, "VINL");
	snd_soc_dapm_nc_pin(codec, "VINR");

	/* Add magician specific controls */
	err = snd_soc_add_controls(codec, uda1380_magician_controls,
				ARRAY_SIZE(uda1380_magician_controls));
	if (err < 0)
		return err;

	/* Add magician specific widgets */
	snd_soc_dapm_new_controls(codec, uda1380_dapm_widgets,
				  ARRAY_SIZE(uda1380_dapm_widgets));

	/* Set up magician specific audio path interconnects */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_sync(codec);
	return 0;
}

/* magician digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link magician_dai[] = {
{
	.name = "uda1380",
	.stream_name = "UDA1380 Playback",
	.cpu_dai = &pxa_ssp_dai[PXA_DAI_SSP1],
	.codec_dai = &uda1380_dai[UDA1380_DAI_PLAYBACK],
	.init = magician_uda1380_init,
	.ops = &magician_playback_ops,
},
{
	.name = "uda1380",
	.stream_name = "UDA1380 Capture",
	.cpu_dai = &pxa_i2s_dai,
	.codec_dai = &uda1380_dai[UDA1380_DAI_CAPTURE],
	.ops = &magician_capture_ops,
}
};

/* magician audio machine driver */
static struct snd_soc_card snd_soc_card_magician = {
	.name = "Magician",
	.dai_link = magician_dai,
	.num_links = ARRAY_SIZE(magician_dai),
	.platform = &pxa2xx_soc_platform,
};

/* magician audio subsystem */
static struct snd_soc_device magician_snd_devdata = {
	.card = &snd_soc_card_magician,
	.codec_dev = &soc_codec_dev_uda1380,
};

static struct platform_device *magician_snd_device;

/*
 * FIXME: move into magician board file once merged into the pxa tree
 */
static struct uda1380_platform_data uda1380_info = {
	.gpio_power = EGPIO_MAGICIAN_CODEC_POWER,
	.gpio_reset = EGPIO_MAGICIAN_CODEC_RESET,
	.dac_clk    = UDA1380_DAC_CLK_WSPLL,
};

static struct i2c_board_info i2c_board_info[] = {
	{
		I2C_BOARD_INFO("uda1380", 0x18),
		.platform_data = &uda1380_info,
	},
};

static int __init magician_init(void)
{
	int ret;
	struct i2c_adapter *adapter;
	struct i2c_client *client;

	if (!machine_is_magician())
		return -ENODEV;

	adapter = i2c_get_adapter(0);
	if (!adapter)
		return -ENODEV;
	client = i2c_new_device(adapter, i2c_board_info);
	i2c_put_adapter(adapter);
	if (!client)
		return -ENODEV;

	ret = gpio_request(EGPIO_MAGICIAN_SPK_POWER, "SPK_POWER");
	if (ret)
		goto err_request_spk;
	ret = gpio_request(EGPIO_MAGICIAN_EP_POWER, "EP_POWER");
	if (ret)
		goto err_request_ep;
	ret = gpio_request(EGPIO_MAGICIAN_MIC_POWER, "MIC_POWER");
	if (ret)
		goto err_request_mic;
	ret = gpio_request(EGPIO_MAGICIAN_IN_SEL0, "IN_SEL0");
	if (ret)
		goto err_request_in_sel0;
	ret = gpio_request(EGPIO_MAGICIAN_IN_SEL1, "IN_SEL1");
	if (ret)
		goto err_request_in_sel1;

	gpio_set_value(EGPIO_MAGICIAN_IN_SEL0, 0);

	magician_snd_device = platform_device_alloc("soc-audio", -1);
	if (!magician_snd_device) {
		ret = -ENOMEM;
		goto err_pdev;
	}

	platform_set_drvdata(magician_snd_device, &magician_snd_devdata);
	magician_snd_devdata.dev = &magician_snd_device->dev;
	ret = platform_device_add(magician_snd_device);
	if (ret) {
		platform_device_put(magician_snd_device);
		goto err_pdev;
	}

	return 0;

err_pdev:
	gpio_free(EGPIO_MAGICIAN_IN_SEL1);
err_request_in_sel1:
	gpio_free(EGPIO_MAGICIAN_IN_SEL0);
err_request_in_sel0:
	gpio_free(EGPIO_MAGICIAN_MIC_POWER);
err_request_mic:
	gpio_free(EGPIO_MAGICIAN_EP_POWER);
err_request_ep:
	gpio_free(EGPIO_MAGICIAN_SPK_POWER);
err_request_spk:
	return ret;
}

static void __exit magician_exit(void)
{
	platform_device_unregister(magician_snd_device);

	gpio_set_value(EGPIO_MAGICIAN_SPK_POWER, 0);
	gpio_set_value(EGPIO_MAGICIAN_EP_POWER, 0);
	gpio_set_value(EGPIO_MAGICIAN_MIC_POWER, 0);

	gpio_free(EGPIO_MAGICIAN_IN_SEL1);
	gpio_free(EGPIO_MAGICIAN_IN_SEL0);
	gpio_free(EGPIO_MAGICIAN_MIC_POWER);
	gpio_free(EGPIO_MAGICIAN_EP_POWER);
	gpio_free(EGPIO_MAGICIAN_SPK_POWER);
}

module_init(magician_init);
module_exit(magician_exit);

MODULE_AUTHOR("Philipp Zabel");
MODULE_DESCRIPTION("ALSA SoC Magician");
MODULE_LICENSE("GPL");

/*
 * neo1973_wm8753.c  --  SoC audio for Neo1973
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include <asm/mach-types.h>
#include <asm/hardware/scoop.h>
#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>
#include <mach/hardware.h>
#include <plat/audio.h>
#include <linux/io.h>
#include <mach/spi-gpio.h>

#include <plat/regs-iis.h>

#include "../codecs/wm8753.h"
#include "lm4857.h"
#include "s3c24xx-pcm.h"
#include "s3c24xx-i2s.h"

/* define the scenarios */
#define NEO_AUDIO_OFF			0
#define NEO_GSM_CALL_AUDIO_HANDSET	1
#define NEO_GSM_CALL_AUDIO_HEADSET	2
#define NEO_GSM_CALL_AUDIO_BLUETOOTH	3
#define NEO_STEREO_TO_SPEAKERS		4
#define NEO_STEREO_TO_HEADPHONES	5
#define NEO_CAPTURE_HANDSET		6
#define NEO_CAPTURE_HEADSET		7
#define NEO_CAPTURE_BLUETOOTH		8

static struct snd_soc_card neo1973;
static struct i2c_client *i2c;

static int neo1973_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	unsigned int pll_out = 0, bclk = 0;
	int ret = 0;
	unsigned long iis_clkrate;

	pr_debug("Entered %s\n", __func__);

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
	ret = snd_soc_dai_set_clkdiv(codec_dai, WM8753_BCLKDIV, bclk);
	if (ret < 0)
		return ret;

	/* set prescaler division for sample rate */
	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_PRESCALER,
		S3C24XX_PRESCALE(4, 4));
	if (ret < 0)
		return ret;

	/* codec PLL input is PCLK/4 */
	ret = snd_soc_dai_set_pll(codec_dai, WM8753_PLL1,
		iis_clkrate / 4, pll_out);
	if (ret < 0)
		return ret;

	return 0;
}

static int neo1973_hifi_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;

	pr_debug("Entered %s\n", __func__);

	/* disable the PLL */
	return snd_soc_dai_set_pll(codec_dai, WM8753_PLL1, 0, 0);
}

/*
 * Neo1973 WM8753 HiFi DAI opserations.
 */
static struct snd_soc_ops neo1973_hifi_ops = {
	.hw_params = neo1973_hifi_hw_params,
	.hw_free = neo1973_hifi_hw_free,
};

static int neo1973_voice_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	unsigned int pcmdiv = 0;
	int ret = 0;
	unsigned long iis_clkrate;

	pr_debug("Entered %s\n", __func__);

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
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8753_PCMCLK, 12288000,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set codec PCM division for sample rate */
	ret = snd_soc_dai_set_clkdiv(codec_dai, WM8753_PCMDIV, pcmdiv);
	if (ret < 0)
		return ret;

	/* configue and enable PLL for 12.288MHz output */
	ret = snd_soc_dai_set_pll(codec_dai, WM8753_PLL2,
		iis_clkrate / 4, 12288000);
	if (ret < 0)
		return ret;

	return 0;
}

static int neo1973_voice_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;

	pr_debug("Entered %s\n", __func__);

	/* disable the PLL */
	return snd_soc_dai_set_pll(codec_dai, WM8753_PLL2, 0, 0);
}

static struct snd_soc_ops neo1973_voice_ops = {
	.hw_params = neo1973_voice_hw_params,
	.hw_free = neo1973_voice_hw_free,
};

static int neo1973_scenario;

static int neo1973_get_scenario(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = neo1973_scenario;
	return 0;
}

static int set_scenario_endpoints(struct snd_soc_codec *codec, int scenario)
{
	pr_debug("Entered %s\n", __func__);

	switch (neo1973_scenario) {
	case NEO_AUDIO_OFF:
		snd_soc_dapm_disable_pin(codec, "Audio Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line In");
		snd_soc_dapm_disable_pin(codec, "Headset Mic");
		snd_soc_dapm_disable_pin(codec, "Call Mic");
		break;
	case NEO_GSM_CALL_AUDIO_HANDSET:
		snd_soc_dapm_enable_pin(codec, "Audio Out");
		snd_soc_dapm_enable_pin(codec, "GSM Line Out");
		snd_soc_dapm_enable_pin(codec, "GSM Line In");
		snd_soc_dapm_disable_pin(codec, "Headset Mic");
		snd_soc_dapm_enable_pin(codec, "Call Mic");
		break;
	case NEO_GSM_CALL_AUDIO_HEADSET:
		snd_soc_dapm_enable_pin(codec, "Audio Out");
		snd_soc_dapm_enable_pin(codec, "GSM Line Out");
		snd_soc_dapm_enable_pin(codec, "GSM Line In");
		snd_soc_dapm_enable_pin(codec, "Headset Mic");
		snd_soc_dapm_disable_pin(codec, "Call Mic");
		break;
	case NEO_GSM_CALL_AUDIO_BLUETOOTH:
		snd_soc_dapm_disable_pin(codec, "Audio Out");
		snd_soc_dapm_enable_pin(codec, "GSM Line Out");
		snd_soc_dapm_enable_pin(codec, "GSM Line In");
		snd_soc_dapm_disable_pin(codec, "Headset Mic");
		snd_soc_dapm_disable_pin(codec, "Call Mic");
		break;
	case NEO_STEREO_TO_SPEAKERS:
		snd_soc_dapm_enable_pin(codec, "Audio Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line In");
		snd_soc_dapm_disable_pin(codec, "Headset Mic");
		snd_soc_dapm_disable_pin(codec, "Call Mic");
		break;
	case NEO_STEREO_TO_HEADPHONES:
		snd_soc_dapm_enable_pin(codec, "Audio Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line In");
		snd_soc_dapm_disable_pin(codec, "Headset Mic");
		snd_soc_dapm_disable_pin(codec, "Call Mic");
		break;
	case NEO_CAPTURE_HANDSET:
		snd_soc_dapm_disable_pin(codec, "Audio Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line In");
		snd_soc_dapm_disable_pin(codec, "Headset Mic");
		snd_soc_dapm_enable_pin(codec, "Call Mic");
		break;
	case NEO_CAPTURE_HEADSET:
		snd_soc_dapm_disable_pin(codec, "Audio Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line In");
		snd_soc_dapm_enable_pin(codec, "Headset Mic");
		snd_soc_dapm_disable_pin(codec, "Call Mic");
		break;
	case NEO_CAPTURE_BLUETOOTH:
		snd_soc_dapm_disable_pin(codec, "Audio Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line In");
		snd_soc_dapm_disable_pin(codec, "Headset Mic");
		snd_soc_dapm_disable_pin(codec, "Call Mic");
		break;
	default:
		snd_soc_dapm_disable_pin(codec, "Audio Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line Out");
		snd_soc_dapm_disable_pin(codec, "GSM Line In");
		snd_soc_dapm_disable_pin(codec, "Headset Mic");
		snd_soc_dapm_disable_pin(codec, "Call Mic");
	}

	snd_soc_dapm_sync(codec);

	return 0;
}

static int neo1973_set_scenario(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	pr_debug("Entered %s\n", __func__);

	if (neo1973_scenario == ucontrol->value.integer.value[0])
		return 0;

	neo1973_scenario = ucontrol->value.integer.value[0];
	set_scenario_endpoints(codec, neo1973_scenario);
	return 1;
}

static u8 lm4857_regs[4] = {0x00, 0x40, 0x80, 0xC0};

static void lm4857_write_regs(void)
{
	pr_debug("Entered %s\n", __func__);

	if (i2c_master_send(i2c, lm4857_regs, 4) != 4)
		printk(KERN_ERR "lm4857: i2c write failed\n");
}

static int lm4857_get_reg(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int reg = kcontrol->private_value & 0xFF;
	int shift = (kcontrol->private_value >> 8) & 0x0F;
	int mask = (kcontrol->private_value >> 16) & 0xFF;

	pr_debug("Entered %s\n", __func__);

	ucontrol->value.integer.value[0] = (lm4857_regs[reg] >> shift) & mask;
	return 0;
}

static int lm4857_set_reg(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int reg = kcontrol->private_value & 0xFF;
	int shift = (kcontrol->private_value >> 8) & 0x0F;
	int mask = (kcontrol->private_value >> 16) & 0xFF;

	if (((lm4857_regs[reg] >> shift) & mask) ==
		ucontrol->value.integer.value[0])
		return 0;

	lm4857_regs[reg] &= ~(mask << shift);
	lm4857_regs[reg] |= ucontrol->value.integer.value[0] << shift;
	lm4857_write_regs();
	return 1;
}

static int lm4857_get_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	u8 value = lm4857_regs[LM4857_CTRL] & 0x0F;

	pr_debug("Entered %s\n", __func__);

	if (value)
		value -= 5;

	ucontrol->value.integer.value[0] = value;
	return 0;
}

static int lm4857_set_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	u8 value = ucontrol->value.integer.value[0];

	pr_debug("Entered %s\n", __func__);

	if (value)
		value += 5;

	if ((lm4857_regs[LM4857_CTRL] & 0x0F) == value)
		return 0;

	lm4857_regs[LM4857_CTRL] &= 0xF0;
	lm4857_regs[LM4857_CTRL] |= value;
	lm4857_write_regs();
	return 1;
}

static const struct snd_soc_dapm_widget wm8753_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Audio Out", NULL),
	SND_SOC_DAPM_LINE("GSM Line Out", NULL),
	SND_SOC_DAPM_LINE("GSM Line In", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Call Mic", NULL),
};


static const struct snd_soc_dapm_route dapm_routes[] = {

	/* Connections to the lm4857 amp */
	{"Audio Out", NULL, "LOUT1"},
	{"Audio Out", NULL, "ROUT1"},

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
	{"Mic Bias", NULL, "Call Mic"},

	/* Connect the ALC pins */
	{"ACIN", NULL, "ACOP"},
};

static const char *lm4857_mode[] = {
	"Off",
	"Call Speaker",
	"Stereo Speakers",
	"Stereo Speakers + Headphones",
	"Headphones"
};

static const struct soc_enum lm4857_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(lm4857_mode), lm4857_mode),
};

static const char *neo_scenarios[] = {
	"Off",
	"GSM Handset",
	"GSM Headset",
	"GSM Bluetooth",
	"Speakers",
	"Headphones",
	"Capture Handset",
	"Capture Headset",
	"Capture Bluetooth"
};

static const struct soc_enum neo_scenario_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(neo_scenarios), neo_scenarios),
};

static const DECLARE_TLV_DB_SCALE(stereo_tlv, -4050, 150, 0);
static const DECLARE_TLV_DB_SCALE(mono_tlv, -3450, 150, 0);

static const struct snd_kcontrol_new wm8753_neo1973_controls[] = {
	SOC_SINGLE_EXT_TLV("Amp Left Playback Volume", LM4857_LVOL, 0, 31, 0,
		lm4857_get_reg, lm4857_set_reg, stereo_tlv),
	SOC_SINGLE_EXT_TLV("Amp Right Playback Volume", LM4857_RVOL, 0, 31, 0,
		lm4857_get_reg, lm4857_set_reg, stereo_tlv),
	SOC_SINGLE_EXT_TLV("Amp Mono Playback Volume", LM4857_MVOL, 0, 31, 0,
		lm4857_get_reg, lm4857_set_reg, mono_tlv),
	SOC_ENUM_EXT("Amp Mode", lm4857_mode_enum[0],
		lm4857_get_mode, lm4857_set_mode),
	SOC_ENUM_EXT("Neo Mode", neo_scenario_enum[0],
		neo1973_get_scenario, neo1973_set_scenario),
	SOC_SINGLE_EXT("Amp Spk 3D Playback Switch", LM4857_LVOL, 5, 1, 0,
		lm4857_get_reg, lm4857_set_reg),
	SOC_SINGLE_EXT("Amp HP 3d Playback Switch", LM4857_RVOL, 5, 1, 0,
		lm4857_get_reg, lm4857_set_reg),
	SOC_SINGLE_EXT("Amp Fast Wakeup Playback Switch", LM4857_CTRL, 5, 1, 0,
		lm4857_get_reg, lm4857_set_reg),
	SOC_SINGLE_EXT("Amp Earpiece 6dB Playback Switch", LM4857_CTRL, 4, 1, 0,
		lm4857_get_reg, lm4857_set_reg),
};

/*
 * This is an example machine initialisation for a wm8753 connected to a
 * neo1973 II. It is missing logic to detect hp/mic insertions and logic
 * to re-route the audio in such an event.
 */
static int neo1973_wm8753_init(struct snd_soc_codec *codec)
{
	int err;

	pr_debug("Entered %s\n", __func__);

	/* set up NC codec pins */
	snd_soc_dapm_nc_pin(codec, "LOUT2");
	snd_soc_dapm_nc_pin(codec, "ROUT2");
	snd_soc_dapm_nc_pin(codec, "OUT3");
	snd_soc_dapm_nc_pin(codec, "OUT4");
	snd_soc_dapm_nc_pin(codec, "LINE1");
	snd_soc_dapm_nc_pin(codec, "LINE2");

	/* Add neo1973 specific widgets */
	snd_soc_dapm_new_controls(codec, wm8753_dapm_widgets,
				  ARRAY_SIZE(wm8753_dapm_widgets));

	/* set endpoints to default mode */
	set_scenario_endpoints(codec, NEO_AUDIO_OFF);

	/* add neo1973 specific controls */
	err = snd_soc_add_controls(codec, wm8753_neo1973_controls,
				ARRAY_SIZE(8753_neo1973_controls));
	if (err < 0)
		return err;

	/* set up neo1973 specific audio routes */
	err = snd_soc_dapm_add_routes(codec, dapm_routes,
				      ARRAY_SIZE(dapm_routes));

	snd_soc_dapm_sync(codec);
	return 0;
}

/*
 * BT Codec DAI
 */
static struct snd_soc_dai bt_dai = {
	.name = "Bluetooth",
	.id = 0,
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

static struct snd_soc_dai_link neo1973_dai[] = {
{ /* Hifi Playback - for similatious use with voice below */
	.name = "WM8753",
	.stream_name = "WM8753 HiFi",
	.cpu_dai = &s3c24xx_i2s_dai,
	.codec_dai = &wm8753_dai[WM8753_DAI_HIFI],
	.init = neo1973_wm8753_init,
	.ops = &neo1973_hifi_ops,
},
{ /* Voice via BT */
	.name = "Bluetooth",
	.stream_name = "Voice",
	.cpu_dai = &bt_dai,
	.codec_dai = &wm8753_dai[WM8753_DAI_VOICE],
	.ops = &neo1973_voice_ops,
},
};

static struct snd_soc_card neo1973 = {
	.name = "neo1973",
	.platform = &s3c24xx_soc_platform,
	.dai_link = neo1973_dai,
	.num_links = ARRAY_SIZE(neo1973_dai),
};

static struct snd_soc_device neo1973_snd_devdata = {
	.card = &neo1973,
	.codec_dev = &soc_codec_dev_wm8753,
};

static int lm4857_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	pr_debug("Entered %s\n", __func__);

	i2c = client;

	lm4857_write_regs();
	return 0;
}

static int lm4857_i2c_remove(struct i2c_client *client)
{
	pr_debug("Entered %s\n", __func__);

	i2c = NULL;

	return 0;
}

static u8 lm4857_state;

static int lm4857_suspend(struct i2c_client *dev, pm_message_t state)
{
	pr_debug("Entered %s\n", __func__);

	dev_dbg(&dev->dev, "lm4857_suspend\n");
	lm4857_state = lm4857_regs[LM4857_CTRL] & 0xf;
	if (lm4857_state) {
		lm4857_regs[LM4857_CTRL] &= 0xf0;
		lm4857_write_regs();
	}
	return 0;
}

static int lm4857_resume(struct i2c_client *dev)
{
	pr_debug("Entered %s\n", __func__);

	if (lm4857_state) {
		lm4857_regs[LM4857_CTRL] |= (lm4857_state & 0x0f);
		lm4857_write_regs();
	}
	return 0;
}

static void lm4857_shutdown(struct i2c_client *dev)
{
	pr_debug("Entered %s\n", __func__);

	dev_dbg(&dev->dev, "lm4857_shutdown\n");
	lm4857_regs[LM4857_CTRL] &= 0xf0;
	lm4857_write_regs();
}

static const struct i2c_device_id lm4857_i2c_id[] = {
	{ "neo1973_lm4857", 0 },
	{ }
};

static struct i2c_driver lm4857_i2c_driver = {
	.driver = {
		.name = "LM4857 I2C Amp",
		.owner = THIS_MODULE,
	},
	.suspend =        lm4857_suspend,
	.resume	=         lm4857_resume,
	.shutdown =       lm4857_shutdown,
	.probe =          lm4857_i2c_probe,
	.remove =         lm4857_i2c_remove,
	.id_table =       lm4857_i2c_id,
};

static struct platform_device *neo1973_snd_device;

static int __init neo1973_init(void)
{
	int ret;

	pr_debug("Entered %s\n", __func__);

	if (!machine_is_neo1973_gta01()) {
		printk(KERN_INFO
			"Only GTA01 hardware supported by ASoC driver\n");
		return -ENODEV;
	}

	neo1973_snd_device = platform_device_alloc("soc-audio", -1);
	if (!neo1973_snd_device)
		return -ENOMEM;

	platform_set_drvdata(neo1973_snd_device, &neo1973_snd_devdata);
	neo1973_snd_devdata.dev = &neo1973_snd_device->dev;
	ret = platform_device_add(neo1973_snd_device);

	if (ret) {
		platform_device_put(neo1973_snd_device);
		return ret;
	}

	ret = i2c_add_driver(&lm4857_i2c_driver);

	if (ret != 0)
		platform_device_unregister(neo1973_snd_device);

	return ret;
}

static void __exit neo1973_exit(void)
{
	pr_debug("Entered %s\n", __func__);

	i2c_del_driver(&lm4857_i2c_driver);
	platform_device_unregister(neo1973_snd_device);
}

module_init(neo1973_init);
module_exit(neo1973_exit);

/* Module information */
MODULE_AUTHOR("Graeme Gregory, graeme@openmoko.org, www.openmoko.org");
MODULE_DESCRIPTION("ALSA SoC WM8753 Neo1973");
MODULE_LICENSE("GPL");

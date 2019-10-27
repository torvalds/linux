// SPDX-License-Identifier: GPL-2.0+
//
// neo1973_wm8753.c - SoC audio for Openmoko Neo1973 and Freerunner devices
//
// Copyright 2007 Openmoko Inc
// Author: Graeme Gregory <graeme@openmoko.org>
// Copyright 2007 Wolfson Microelectronics PLC.
// Author: Graeme Gregory
//         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
// Copyright 2009 Wolfson Microelectronics

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <sound/soc.h>

#include <mach/gpio-samsung.h>
#include <asm/mach-types.h>
#include "regs-iis.h"

#include "../codecs/wm8753.h"
#include "s3c24xx-i2s.h"

static int neo1973_hifi_hw_params(struct snd_pcm_substream *substream,
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
	ret = snd_soc_dai_set_pll(codec_dai, WM8753_PLL1, 0,
		iis_clkrate / 4, pll_out);
	if (ret < 0)
		return ret;

	return 0;
}

static int neo1973_hifi_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	/* disable the PLL */
	return snd_soc_dai_set_pll(codec_dai, WM8753_PLL1, 0, 0, 0);
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

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8753_PCMCLK, 12288000,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set codec PCM division for sample rate */
	ret = snd_soc_dai_set_clkdiv(codec_dai, WM8753_PCMDIV, pcmdiv);
	if (ret < 0)
		return ret;

	/* configure and enable PLL for 12.288MHz output */
	ret = snd_soc_dai_set_pll(codec_dai, WM8753_PLL2, 0,
		iis_clkrate / 4, 12288000);
	if (ret < 0)
		return ret;

	return 0;
}

static int neo1973_voice_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	/* disable the PLL */
	return snd_soc_dai_set_pll(codec_dai, WM8753_PLL2, 0, 0, 0);
}

static struct snd_soc_ops neo1973_voice_ops = {
	.hw_params = neo1973_voice_hw_params,
	.hw_free = neo1973_voice_hw_free,
};

static int gta02_speaker_enabled;

static int lm4853_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	gta02_speaker_enabled = ucontrol->value.integer.value[0];

	gpio_set_value(S3C2410_GPJ(2), !gta02_speaker_enabled);

	return 0;
}

static int lm4853_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = gta02_speaker_enabled;
	return 0;
}

static int lm4853_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int event)
{
	gpio_set_value(S3C2410_GPJ(1), SND_SOC_DAPM_EVENT_OFF(event));

	return 0;
}

static const struct snd_soc_dapm_widget neo1973_wm8753_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("GSM Line Out", NULL),
	SND_SOC_DAPM_LINE("GSM Line In", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Handset Mic", NULL),
	SND_SOC_DAPM_SPK("Handset Spk", NULL),
	SND_SOC_DAPM_SPK("Stereo Out", lm4853_event),
};

static const struct snd_soc_dapm_route neo1973_wm8753_routes[] = {
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

	/* Connect the ALC pins */
	{"ACIN", NULL, "ACOP"},

	/* Connections to the amp */
	{"Stereo Out", NULL, "LOUT1"},
	{"Stereo Out", NULL, "ROUT1"},

	/* Call Speaker */
	{"Handset Spk", NULL, "LOUT2"},
	{"Handset Spk", NULL, "ROUT2"},
};

static const struct snd_kcontrol_new neo1973_wm8753_controls[] = {
	SOC_DAPM_PIN_SWITCH("GSM Line Out"),
	SOC_DAPM_PIN_SWITCH("GSM Line In"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Handset Mic"),
	SOC_DAPM_PIN_SWITCH("Handset Spk"),
	SOC_DAPM_PIN_SWITCH("Stereo Out"),

	SOC_SINGLE_BOOL_EXT("Amp Spk Switch", 0,
		lm4853_get_spk,
		lm4853_set_spk),
};

static int neo1973_wm8753_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;

	/* set endpoints to default off mode */
	snd_soc_dapm_disable_pin(&card->dapm, "GSM Line Out");
	snd_soc_dapm_disable_pin(&card->dapm, "GSM Line In");
	snd_soc_dapm_disable_pin(&card->dapm, "Headset Mic");
	snd_soc_dapm_disable_pin(&card->dapm, "Handset Mic");
	snd_soc_dapm_disable_pin(&card->dapm, "Stereo Out");
	snd_soc_dapm_disable_pin(&card->dapm, "Handset Spk");

	/* allow audio paths from the GSM modem to run during suspend */
	snd_soc_dapm_ignore_suspend(&card->dapm, "GSM Line Out");
	snd_soc_dapm_ignore_suspend(&card->dapm, "GSM Line In");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Headset Mic");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Handset Mic");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Stereo Out");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Handset Spk");

	return 0;
}

SND_SOC_DAILINK_DEFS(wm8753,
	DAILINK_COMP_ARRAY(COMP_CPU("s3c24xx-iis")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8753.0-001a", "wm8753-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("s3c24xx-iis")));

SND_SOC_DAILINK_DEFS(bluetooth,
	DAILINK_COMP_ARRAY(COMP_CPU("bt-sco-pcm")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8753.0-001a", "wm8753-voice")));

static struct snd_soc_dai_link neo1973_dai[] = {
{ /* Hifi Playback - for similatious use with voice below */
	.name = "WM8753",
	.stream_name = "WM8753 HiFi",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBM_CFM,
	.init = neo1973_wm8753_init,
	.ops = &neo1973_hifi_ops,
	SND_SOC_DAILINK_REG(wm8753),
},
{ /* Voice via BT */
	.name = "Bluetooth",
	.stream_name = "Voice",
	.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
	.ops = &neo1973_voice_ops,
	SND_SOC_DAILINK_REG(bluetooth),
},
};

static struct snd_soc_aux_dev neo1973_aux_devs[] = {
	{
		.dlc = COMP_AUX("dfbmcs320.0"),
	},
};

static struct snd_soc_codec_conf neo1973_codec_conf[] = {
	{
		.dev_name = "lm4857.0-007c",
		.name_prefix = "Amp",
	},
};

static const struct gpio neo1973_gta02_gpios[] = {
	{ S3C2410_GPJ(2), GPIOF_OUT_INIT_HIGH, "GTA02_HP_IN" },
	{ S3C2410_GPJ(1), GPIOF_OUT_INIT_HIGH, "GTA02_AMP_SHUT" },
};

static struct snd_soc_card neo1973 = {
	.name = "neo1973",
	.owner = THIS_MODULE,
	.dai_link = neo1973_dai,
	.num_links = ARRAY_SIZE(neo1973_dai),
	.aux_dev = neo1973_aux_devs,
	.num_aux_devs = ARRAY_SIZE(neo1973_aux_devs),
	.codec_conf = neo1973_codec_conf,
	.num_configs = ARRAY_SIZE(neo1973_codec_conf),

	.controls = neo1973_wm8753_controls,
	.num_controls = ARRAY_SIZE(neo1973_wm8753_controls),
	.dapm_widgets = neo1973_wm8753_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(neo1973_wm8753_dapm_widgets),
	.dapm_routes = neo1973_wm8753_routes,
	.num_dapm_routes = ARRAY_SIZE(neo1973_wm8753_routes),
	.fully_routed = true,
};

static struct platform_device *neo1973_snd_device;

static int __init neo1973_init(void)
{
	int ret;

	if (!machine_is_neo1973_gta02())
		return -ENODEV;

	if (machine_is_neo1973_gta02()) {
		neo1973.name = "neo1973gta02";
		neo1973.num_aux_devs = 1;

		ret = gpio_request_array(neo1973_gta02_gpios,
				ARRAY_SIZE(neo1973_gta02_gpios));
		if (ret)
			return ret;
	}

	neo1973_snd_device = platform_device_alloc("soc-audio", -1);
	if (!neo1973_snd_device) {
		ret = -ENOMEM;
		goto err_gpio_free;
	}

	platform_set_drvdata(neo1973_snd_device, &neo1973);
	ret = platform_device_add(neo1973_snd_device);

	if (ret)
		goto err_put_device;

	return 0;

err_put_device:
	platform_device_put(neo1973_snd_device);
err_gpio_free:
	if (machine_is_neo1973_gta02()) {
		gpio_free_array(neo1973_gta02_gpios,
				ARRAY_SIZE(neo1973_gta02_gpios));
	}
	return ret;
}
module_init(neo1973_init);

static void __exit neo1973_exit(void)
{
	platform_device_unregister(neo1973_snd_device);

	if (machine_is_neo1973_gta02()) {
		gpio_free_array(neo1973_gta02_gpios,
				ARRAY_SIZE(neo1973_gta02_gpios));
	}
}
module_exit(neo1973_exit);

/* Module information */
MODULE_AUTHOR("Graeme Gregory, graeme@openmoko.org, www.openmoko.org");
MODULE_DESCRIPTION("ALSA SoC WM8753 Neo1973 and Frerunner");
MODULE_LICENSE("GPL");

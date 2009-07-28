/*
 * cx20442.c  --  CX20442 ALSA Soc Audio driver
 *
 * Copyright 2009 Janusz Krzysztofik <jkrzyszt@tis.icnet.pl>
 *
 * Initially based on sound/soc/codecs/wm8400.c
 * Copyright 2008, 2009 Wolfson Microelectronics PLC.
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/soc-dapm.h>

#include "cx20442.h"


struct cx20442_priv {
	struct snd_soc_codec codec;
	u8 reg_cache[1];
};

#define CX20442_PM		0x0

#define CX20442_TELIN		0
#define CX20442_TELOUT		1
#define CX20442_MIC		2
#define CX20442_SPKOUT		3
#define CX20442_AGC		4

static const struct snd_soc_dapm_widget cx20442_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("TELOUT"),
	SND_SOC_DAPM_OUTPUT("SPKOUT"),
	SND_SOC_DAPM_OUTPUT("AGCOUT"),

	SND_SOC_DAPM_MIXER("SPKOUT Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA("TELOUT Amp", CX20442_PM, CX20442_TELOUT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SPKOUT Amp", CX20442_PM, CX20442_SPKOUT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SPKOUT AGC", CX20442_PM, CX20442_AGC, 0, NULL, 0),

	SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC", "Capture", SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MIXER("Input Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MICBIAS("TELIN Bias", CX20442_PM, CX20442_TELIN, 0),
	SND_SOC_DAPM_MICBIAS("MIC Bias", CX20442_PM, CX20442_MIC, 0),

	SND_SOC_DAPM_PGA("MIC AGC", CX20442_PM, CX20442_AGC, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("TELIN"),
	SND_SOC_DAPM_INPUT("MIC"),
	SND_SOC_DAPM_INPUT("AGCIN"),
};

static const struct snd_soc_dapm_route cx20442_audio_map[] = {
	{"TELOUT", NULL, "TELOUT Amp"},

	{"SPKOUT", NULL, "SPKOUT Mixer"},
	{"SPKOUT Mixer", NULL, "SPKOUT Amp"},

	{"TELOUT Amp", NULL, "DAC"},
	{"SPKOUT Amp", NULL, "DAC"},

	{"SPKOUT Mixer", NULL, "SPKOUT AGC"},
	{"SPKOUT AGC", NULL, "AGCIN"},

	{"AGCOUT", NULL, "MIC AGC"},
	{"MIC AGC", NULL, "MIC"},

	{"MIC Bias", NULL, "MIC"},
	{"Input Mixer", NULL, "MIC Bias"},

	{"TELIN Bias", NULL, "TELIN"},
	{"Input Mixer", NULL, "TELIN Bias"},

	{"ADC", NULL, "Input Mixer"},
};

static int cx20442_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, cx20442_dapm_widgets,
				  ARRAY_SIZE(cx20442_dapm_widgets));

	snd_soc_dapm_add_routes(codec, cx20442_audio_map,
				ARRAY_SIZE(cx20442_audio_map));

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

static unsigned int cx20442_read_reg_cache(struct snd_soc_codec *codec,
							unsigned int reg)
{
	u8 *reg_cache = codec->reg_cache;

	if (reg >= codec->reg_cache_size)
		return -EINVAL;

	return reg_cache[reg];
}

enum v253_vls {
	V253_VLS_NONE = 0,
	V253_VLS_T,
	V253_VLS_L,
	V253_VLS_LT,
	V253_VLS_S,
	V253_VLS_ST,
	V253_VLS_M,
	V253_VLS_MST,
	V253_VLS_S1,
	V253_VLS_S1T,
	V253_VLS_MS1T,
	V253_VLS_M1,
	V253_VLS_M1ST,
	V253_VLS_M1S1T,
	V253_VLS_H,
	V253_VLS_HT,
	V253_VLS_MS,
	V253_VLS_MS1,
	V253_VLS_M1S,
	V253_VLS_M1S1,
	V253_VLS_TEST,
};

static int cx20442_pm_to_v253_vls(u8 value)
{
	switch (value & ~(1 << CX20442_AGC)) {
	case 0:
		return V253_VLS_T;
	case (1 << CX20442_SPKOUT):
		return V253_VLS_S1;
	case (1 << CX20442_MIC):
		return V253_VLS_M1;
	case (1 << CX20442_SPKOUT) | (1 << CX20442_MIC):
		return V253_VLS_M1S1;
	case (1 << CX20442_TELOUT):
	case (1 << CX20442_TELIN):
	case (1 << CX20442_TELOUT) | (1 << CX20442_TELIN):
		return V253_VLS_L;
	case (1 << CX20442_TELOUT) | (1 << CX20442_MIC):
		return V253_VLS_NONE;
	}
	return -EINVAL;
}
static int cx20442_pm_to_v253_vsp(u8 value)
{
	switch (value & ~(1 << CX20442_AGC)) {
	case (1 << CX20442_SPKOUT):
	case (1 << CX20442_MIC):
	case (1 << CX20442_SPKOUT) | (1 << CX20442_MIC):
		return (bool)(value & (1 << CX20442_AGC));
	}
	return (value & (1 << CX20442_AGC)) ? -EINVAL : 0;
}

static int cx20442_write(struct snd_soc_codec *codec, unsigned int reg,
							unsigned int value)
{
	u8 *reg_cache = codec->reg_cache;
	int vls, vsp, old, len;
	char buf[18];

	if (reg >= codec->reg_cache_size)
		return -EINVAL;

	/* hw_write and control_data pointers required for talking to the modem
	 * are expected to be set by the machine driver's line discipline
	 * initialization code */
	if (!codec->hw_write || !codec->control_data)
		return -EIO;

	old = reg_cache[reg];
	reg_cache[reg] = value;

	vls = cx20442_pm_to_v253_vls(value);
	if (vls < 0)
		return vls;

	vsp = cx20442_pm_to_v253_vsp(value);
	if (vsp < 0)
		return vsp;

	if ((vls == V253_VLS_T) ||
			(vls == cx20442_pm_to_v253_vls(old))) {
		if (vsp == cx20442_pm_to_v253_vsp(old))
			return 0;
		len = snprintf(buf, ARRAY_SIZE(buf), "at+vsp=%d\r", vsp);
	} else if (vsp == cx20442_pm_to_v253_vsp(old))
		len = snprintf(buf, ARRAY_SIZE(buf), "at+vls=%d\r", vls);
	else
		len = snprintf(buf, ARRAY_SIZE(buf),
					"at+vls=%d;+vsp=%d\r", vls, vsp);

	if (unlikely(len > (ARRAY_SIZE(buf) - 1)))
		return -ENOMEM;

	if (codec->hw_write(codec->control_data, buf, len) != len)
		return -EIO;

	return 0;
}

struct snd_soc_dai cx20442_dai = {
	.name = "CX20442",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};
EXPORT_SYMBOL_GPL(cx20442_dai);

static struct snd_soc_codec *cx20442_codec;

static int cx20442_codec_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret;

	if (!cx20442_codec) {
		dev_err(&pdev->dev, "cx20442 not yet discovered\n");
		return -ENODEV;
	}
	codec = cx20442_codec;

	socdev->card->codec = codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to create pcms\n");
		goto pcm_err;
	}

	cx20442_add_widgets(codec);

	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register card\n");
		goto card_err;
	}

	return ret;

card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	return ret;
}

/* power down chip */
static int cx20442_codec_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device cx20442_codec_dev = {
	.probe = 	cx20442_codec_probe,
	.remove = 	cx20442_codec_remove,
};
EXPORT_SYMBOL_GPL(cx20442_codec_dev);

static int cx20442_register(struct cx20442_priv *cx20442)
{
	struct snd_soc_codec *codec = &cx20442->codec;
	int ret;

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->name = "CX20442";
	codec->owner = THIS_MODULE;
	codec->private_data = cx20442;

	codec->dai = &cx20442_dai;
	codec->num_dai = 1;

	codec->reg_cache = &cx20442->reg_cache;
	codec->reg_cache_size = ARRAY_SIZE(cx20442->reg_cache);
	codec->read = cx20442_read_reg_cache;
	codec->write = cx20442_write;

	codec->bias_level = SND_SOC_BIAS_OFF;

	cx20442_dai.dev = codec->dev;

	cx20442_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(&codec->dev, "Failed to register codec: %d\n", ret);
		goto err;
	}

	ret = snd_soc_register_dai(&cx20442_dai);
	if (ret != 0) {
		dev_err(&codec->dev, "Failed to register DAI: %d\n", ret);
		goto err_codec;
	}

	return 0;

err_codec:
	snd_soc_unregister_codec(codec);
err:
	cx20442_codec = NULL;
	kfree(cx20442);
	return ret;
}

static void cx20442_unregister(struct cx20442_priv *cx20442)
{
	snd_soc_unregister_dai(&cx20442_dai);
	snd_soc_unregister_codec(&cx20442->codec);

	cx20442_codec = NULL;
	kfree(cx20442);
}

static int cx20442_platform_probe(struct platform_device *pdev)
{
	struct cx20442_priv *cx20442;
	struct snd_soc_codec *codec;

	cx20442 = kzalloc(sizeof(struct cx20442_priv), GFP_KERNEL);
	if (cx20442 == NULL)
		return -ENOMEM;

	codec = &cx20442->codec;

	codec->control_data = NULL;
	codec->hw_write = NULL;
	codec->pop_time = 0;

	codec->dev = &pdev->dev;
	platform_set_drvdata(pdev, cx20442);

	return cx20442_register(cx20442);
}

static int __exit cx20442_platform_remove(struct platform_device *pdev)
{
	struct cx20442_priv *cx20442 = platform_get_drvdata(pdev);

	cx20442_unregister(cx20442);
	return 0;
}

static struct platform_driver cx20442_platform_driver = {
	.driver = {
		.name = "cx20442",
		.owner = THIS_MODULE,
		},
	.probe = cx20442_platform_probe,
	.remove = __exit_p(cx20442_platform_remove),
};

static int __init cx20442_init(void)
{
	return platform_driver_register(&cx20442_platform_driver);
}
module_init(cx20442_init);

static void __exit cx20442_exit(void)
{
	platform_driver_unregister(&cx20442_platform_driver);
}
module_exit(cx20442_exit);

MODULE_DESCRIPTION("ASoC CX20442-11 voice modem codec driver");
MODULE_AUTHOR("Janusz Krzysztofik");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cx20442");

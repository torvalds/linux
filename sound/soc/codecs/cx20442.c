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

#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "cx20442.h"


struct cx20442_priv {
	struct tty_struct *tty;
	struct regulator *por;
	u8 reg_cache;
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

static unsigned int cx20442_read_reg_cache(struct snd_soc_component *component,
					   unsigned int reg)
{
	struct cx20442_priv *cx20442 = snd_soc_component_get_drvdata(component);

	if (reg >= 1)
		return -EINVAL;

	return cx20442->reg_cache;
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
	case (1 << CX20442_MIC):
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

static int cx20442_write(struct snd_soc_component *component, unsigned int reg,
							unsigned int value)
{
	struct cx20442_priv *cx20442 = snd_soc_component_get_drvdata(component);
	int vls, vsp, old, len;
	char buf[18];

	if (reg >= 1)
		return -EINVAL;

	/* tty and write pointers required for talking to the modem
	 * are expected to be set by the line discipline initialization code */
	if (!cx20442->tty || !cx20442->tty->ops->write)
		return -EIO;

	old = cx20442->reg_cache;
	cx20442->reg_cache = value;

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

	dev_dbg(component->dev, "%s: %s\n", __func__, buf);
	if (cx20442->tty->ops->write(cx20442->tty, buf, len) != len)
		return -EIO;

	return 0;
}

/*
 * Line discpline related code
 *
 * Any of the callback functions below can be used in two ways:
 * 1) registerd by a machine driver as one of line discipline operations,
 * 2) called from a machine's provided line discipline callback function
 *    in case when extra machine specific code must be run as well.
 */

/* Modem init: echo off, digital speaker off, quiet off, voice mode */
static const char *v253_init = "ate0m0q0+fclass=8\r";

/* Line discipline .open() */
static int v253_open(struct tty_struct *tty)
{
	int ret, len = strlen(v253_init);

	/* Doesn't make sense without write callback */
	if (!tty->ops->write)
		return -EINVAL;

	/* Won't work if no codec pointer has been passed by a card driver */
	if (!tty->disc_data)
		return -ENODEV;

	tty->receive_room = 16;
	if (tty->ops->write(tty, v253_init, len) != len) {
		ret = -EIO;
		goto err;
	}
	/* Actual setup will be performed after the modem responds. */
	return 0;
err:
	tty->disc_data = NULL;
	return ret;
}

/* Line discipline .close() */
static void v253_close(struct tty_struct *tty)
{
	struct snd_soc_component *component = tty->disc_data;
	struct cx20442_priv *cx20442;

	tty->disc_data = NULL;

	if (!component)
		return;

	cx20442 = snd_soc_component_get_drvdata(component);

	/* Prevent the codec driver from further accessing the modem */
	cx20442->tty = NULL;
	component->card->pop_time = 0;
}

/* Line discipline .hangup() */
static int v253_hangup(struct tty_struct *tty)
{
	v253_close(tty);
	return 0;
}

/* Line discipline .receive_buf() */
static void v253_receive(struct tty_struct *tty,
				const unsigned char *cp, char *fp, int count)
{
	struct snd_soc_component *component = tty->disc_data;
	struct cx20442_priv *cx20442;

	if (!component)
		return;

	cx20442 = snd_soc_component_get_drvdata(component);

	if (!cx20442->tty) {
		/* First modem response, complete setup procedure */

		/* Set up codec driver access to modem controls */
		cx20442->tty = tty;
		component->card->pop_time = 1;
	}
}

/* Line discipline .write_wakeup() */
static void v253_wakeup(struct tty_struct *tty)
{
}

struct tty_ldisc_ops v253_ops = {
	.magic = TTY_LDISC_MAGIC,
	.name = "cx20442",
	.owner = THIS_MODULE,
	.open = v253_open,
	.close = v253_close,
	.hangup = v253_hangup,
	.receive_buf = v253_receive,
	.write_wakeup = v253_wakeup,
};
EXPORT_SYMBOL_GPL(v253_ops);


/*
 * Codec DAI
 */

static struct snd_soc_dai_driver cx20442_dai = {
	.name = "cx20442-voice",
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

static int cx20442_set_bias_level(struct snd_soc_component *component,
		enum snd_soc_bias_level level)
{
	struct cx20442_priv *cx20442 = snd_soc_component_get_drvdata(component);
	int err = 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (snd_soc_component_get_bias_level(component) != SND_SOC_BIAS_STANDBY)
			break;
		if (IS_ERR(cx20442->por))
			err = PTR_ERR(cx20442->por);
		else
			err = regulator_enable(cx20442->por);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) != SND_SOC_BIAS_PREPARE)
			break;
		if (IS_ERR(cx20442->por))
			err = PTR_ERR(cx20442->por);
		else
			err = regulator_disable(cx20442->por);
		break;
	default:
		break;
	}

	return err;
}

static int cx20442_component_probe(struct snd_soc_component *component)
{
	struct cx20442_priv *cx20442;

	cx20442 = kzalloc(sizeof(struct cx20442_priv), GFP_KERNEL);
	if (cx20442 == NULL)
		return -ENOMEM;

	cx20442->por = regulator_get(component->dev, "POR");
	if (IS_ERR(cx20442->por)) {
		int err = PTR_ERR(cx20442->por);

		dev_warn(component->dev, "failed to get POR supply (%d)", err);
		/*
		 * When running on a non-dt platform and requested regulator
		 * is not available, regulator_get() never returns
		 * -EPROBE_DEFER as it is not able to justify if the regulator
		 * may still appear later.  On the other hand, the board can
		 * still set full constraints flag at late_initcall in order
		 * to instruct regulator_get() to return a dummy one if
		 * sufficient.  Hence, if we get -ENODEV here, let's convert
		 * it to -EPROBE_DEFER and wait for the board to decide or
		 * let Deferred Probe infrastructure handle this error.
		 */
		if (err == -ENODEV)
			err = -EPROBE_DEFER;
		kfree(cx20442);
		return err;
	}

	cx20442->tty = NULL;

	snd_soc_component_set_drvdata(component, cx20442);
	component->card->pop_time = 0;

	return 0;
}

/* power down chip */
static void cx20442_component_remove(struct snd_soc_component *component)
{
	struct cx20442_priv *cx20442 = snd_soc_component_get_drvdata(component);

	if (cx20442->tty) {
		struct tty_struct *tty = cx20442->tty;
		tty_hangup(tty);
	}

	if (!IS_ERR(cx20442->por)) {
		/* should be already in STANDBY, hence disabled */
		regulator_put(cx20442->por);
	}

	snd_soc_component_set_drvdata(component, NULL);
	kfree(cx20442);
}

static const struct snd_soc_component_driver cx20442_component_dev = {
	.probe			= cx20442_component_probe,
	.remove			= cx20442_component_remove,
	.set_bias_level		= cx20442_set_bias_level,
	.read			= cx20442_read_reg_cache,
	.write			= cx20442_write,
	.dapm_widgets		= cx20442_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cx20442_dapm_widgets),
	.dapm_routes		= cx20442_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(cx20442_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int cx20442_platform_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
			&cx20442_component_dev, &cx20442_dai, 1);
}

static struct platform_driver cx20442_platform_driver = {
	.driver = {
		.name = "cx20442-codec",
		},
	.probe = cx20442_platform_probe,
};

module_platform_driver(cx20442_platform_driver);

MODULE_DESCRIPTION("ASoC CX20442-11 voice modem codec driver");
MODULE_AUTHOR("Janusz Krzysztofik");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cx20442-codec");

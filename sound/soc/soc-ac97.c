/*
 * soc-ac97.c  --  ALSA SoC Audio Layer AC97 support
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 * Copyright (C) 2010 Slimlogic Ltd.
 * Copyright (C) 2010 Texas Instruments Inc.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *         with code, comments and ideas from :-
 *         Richard Purdie <richard@openedhand.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <sound/ac97_codec.h>
#include <sound/soc.h>

struct snd_ac97_reset_cfg {
	struct pinctrl *pctl;
	struct pinctrl_state *pstate_reset;
	struct pinctrl_state *pstate_warm_reset;
	struct pinctrl_state *pstate_run;
	int gpio_sdata;
	int gpio_sync;
	int gpio_reset;
};

static struct snd_ac97_bus soc_ac97_bus = {
	.ops = NULL, /* Gets initialized in snd_soc_set_ac97_ops() */
};

/* unregister ac97 codec */
static int soc_ac97_dev_unregister(struct snd_soc_codec *codec)
{
	if (codec->ac97->dev.bus)
		device_del(&codec->ac97->dev);
	return 0;
}

/* register ac97 codec to bus */
static int soc_ac97_dev_register(struct snd_soc_codec *codec)
{
	int err;

	codec->ac97->dev.bus = &ac97_bus_type;
	codec->ac97->dev.parent = codec->component.card->dev;

	dev_set_name(&codec->ac97->dev, "%d-%d:%s",
		     codec->component.card->snd_card->number, 0,
		     codec->component.name);
	err = device_add(&codec->ac97->dev);
	if (err < 0) {
		dev_err(codec->dev, "ASoC: Can't register ac97 bus\n");
		codec->ac97->dev.bus = NULL;
		return err;
	}
	return 0;
}

static int soc_register_ac97_codec(struct snd_soc_codec *codec,
				   struct snd_soc_dai *codec_dai)
{
	int ret;

	/* Only instantiate AC97 if not already done by the adaptor
	 * for the generic AC97 subsystem.
	 */
	if (codec_dai->driver->ac97_control && !codec->ac97_registered) {
		/*
		 * It is possible that the AC97 device is already registered to
		 * the device subsystem. This happens when the device is created
		 * via snd_ac97_mixer(). Currently only SoC codec that does so
		 * is the generic AC97 glue but others migh emerge.
		 *
		 * In those cases we don't try to register the device again.
		 */
		if (!codec->ac97_created)
			return 0;

		ret = soc_ac97_dev_register(codec);
		if (ret < 0) {
			dev_err(codec->dev,
				"ASoC: AC97 device register failed: %d\n", ret);
			return ret;
		}

		codec->ac97_registered = 1;
	}
	return 0;
}

static void soc_unregister_ac97_codec(struct snd_soc_codec *codec)
{
	if (codec->ac97_registered) {
		soc_ac97_dev_unregister(codec);
		codec->ac97_registered = 0;
	}
}

static int soc_register_ac97_dai_link(struct snd_soc_pcm_runtime *rtd)
{
	int i, ret;

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[i];

		ret = soc_register_ac97_codec(codec_dai->codec, codec_dai);
		if (ret) {
			while (--i >= 0)
				soc_unregister_ac97_codec(codec_dai->codec);
			return ret;
		}
	}

	return 0;
}

static void soc_unregister_ac97_dai_link(struct snd_soc_pcm_runtime *rtd)
{
	int i;

	for (i = 0; i < rtd->num_codecs; i++)
		soc_unregister_ac97_codec(rtd->codec_dais[i]->codec);
}

static void soc_ac97_device_release(struct device *dev)
{
	kfree(to_ac97_t(dev));
}

/**
 * snd_soc_new_ac97_codec - initailise AC97 device
 * @codec: audio codec
 *
 * Initialises AC97 codec resources for use by ad-hoc devices only.
 */
int snd_soc_new_ac97_codec(struct snd_soc_codec *codec)
{
	codec->ac97 = kzalloc(sizeof(struct snd_ac97), GFP_KERNEL);
	if (codec->ac97 == NULL)
		return -ENOMEM;

	codec->ac97->bus = &soc_ac97_bus;
	codec->ac97->num = 0;
	codec->ac97->dev.release = soc_ac97_device_release;

	/*
	 * Mark the AC97 device to be created by us. This way we ensure that the
	 * device will be registered with the device subsystem later on.
	 */
	codec->ac97_created = 1;
	device_initialize(&codec->ac97->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_new_ac97_codec);

/**
 * snd_soc_free_ac97_codec - free AC97 codec device
 * @codec: audio codec
 *
 * Frees AC97 codec device resources.
 */
void snd_soc_free_ac97_codec(struct snd_soc_codec *codec)
{
	soc_unregister_ac97_codec(codec);
	codec->ac97->bus = NULL;
	put_device(&codec->ac97->dev);
	codec->ac97 = NULL;
	codec->ac97_created = 0;
}
EXPORT_SYMBOL_GPL(snd_soc_free_ac97_codec);

static struct snd_ac97_reset_cfg snd_ac97_rst_cfg;

static void snd_soc_ac97_warm_reset(struct snd_ac97 *ac97)
{
	struct pinctrl *pctl = snd_ac97_rst_cfg.pctl;

	pinctrl_select_state(pctl, snd_ac97_rst_cfg.pstate_warm_reset);

	gpio_direction_output(snd_ac97_rst_cfg.gpio_sync, 1);

	udelay(10);

	gpio_direction_output(snd_ac97_rst_cfg.gpio_sync, 0);

	pinctrl_select_state(pctl, snd_ac97_rst_cfg.pstate_run);
	msleep(2);
}

static void snd_soc_ac97_reset(struct snd_ac97 *ac97)
{
	struct pinctrl *pctl = snd_ac97_rst_cfg.pctl;

	pinctrl_select_state(pctl, snd_ac97_rst_cfg.pstate_reset);

	gpio_direction_output(snd_ac97_rst_cfg.gpio_sync, 0);
	gpio_direction_output(snd_ac97_rst_cfg.gpio_sdata, 0);
	gpio_direction_output(snd_ac97_rst_cfg.gpio_reset, 0);

	udelay(10);

	gpio_direction_output(snd_ac97_rst_cfg.gpio_reset, 1);

	pinctrl_select_state(pctl, snd_ac97_rst_cfg.pstate_run);
	msleep(2);
}

static int snd_soc_ac97_parse_pinctl(struct device *dev,
		struct snd_ac97_reset_cfg *cfg)
{
	struct pinctrl *p;
	struct pinctrl_state *state;
	int gpio;
	int ret;

	p = devm_pinctrl_get(dev);
	if (IS_ERR(p)) {
		dev_err(dev, "Failed to get pinctrl\n");
		return PTR_ERR(p);
	}
	cfg->pctl = p;

	state = pinctrl_lookup_state(p, "ac97-reset");
	if (IS_ERR(state)) {
		dev_err(dev, "Can't find pinctrl state ac97-reset\n");
		return PTR_ERR(state);
	}
	cfg->pstate_reset = state;

	state = pinctrl_lookup_state(p, "ac97-warm-reset");
	if (IS_ERR(state)) {
		dev_err(dev, "Can't find pinctrl state ac97-warm-reset\n");
		return PTR_ERR(state);
	}
	cfg->pstate_warm_reset = state;

	state = pinctrl_lookup_state(p, "ac97-running");
	if (IS_ERR(state)) {
		dev_err(dev, "Can't find pinctrl state ac97-running\n");
		return PTR_ERR(state);
	}
	cfg->pstate_run = state;

	gpio = of_get_named_gpio(dev->of_node, "ac97-gpios", 0);
	if (gpio < 0) {
		dev_err(dev, "Can't find ac97-sync gpio\n");
		return gpio;
	}
	ret = devm_gpio_request(dev, gpio, "AC97 link sync");
	if (ret) {
		dev_err(dev, "Failed requesting ac97-sync gpio\n");
		return ret;
	}
	cfg->gpio_sync = gpio;

	gpio = of_get_named_gpio(dev->of_node, "ac97-gpios", 1);
	if (gpio < 0) {
		dev_err(dev, "Can't find ac97-sdata gpio %d\n", gpio);
		return gpio;
	}
	ret = devm_gpio_request(dev, gpio, "AC97 link sdata");
	if (ret) {
		dev_err(dev, "Failed requesting ac97-sdata gpio\n");
		return ret;
	}
	cfg->gpio_sdata = gpio;

	gpio = of_get_named_gpio(dev->of_node, "ac97-gpios", 2);
	if (gpio < 0) {
		dev_err(dev, "Can't find ac97-reset gpio\n");
		return gpio;
	}
	ret = devm_gpio_request(dev, gpio, "AC97 link reset");
	if (ret) {
		dev_err(dev, "Failed requesting ac97-reset gpio\n");
		return ret;
	}
	cfg->gpio_reset = gpio;

	return 0;
}

struct snd_ac97_bus_ops *soc_ac97_ops;
EXPORT_SYMBOL_GPL(soc_ac97_ops);

int snd_soc_set_ac97_ops(struct snd_ac97_bus_ops *ops)
{
	if (ops == soc_ac97_ops)
		return 0;

	if (soc_ac97_ops && ops)
		return -EBUSY;

	soc_ac97_ops = ops;
	soc_ac97_bus.ops = ops;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_set_ac97_ops);

/**
 * snd_soc_set_ac97_ops_of_reset - Set ac97 ops with generic ac97 reset functions
 *
 * This function sets the reset and warm_reset properties of ops and parses
 * the device node of pdev to get pinctrl states and gpio numbers to use.
 */
int snd_soc_set_ac97_ops_of_reset(struct snd_ac97_bus_ops *ops,
		struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_ac97_reset_cfg cfg;
	int ret;

	ret = snd_soc_ac97_parse_pinctl(dev, &cfg);
	if (ret)
		return ret;

	ret = snd_soc_set_ac97_ops(ops);
	if (ret)
		return ret;

	ops->warm_reset = snd_soc_ac97_warm_reset;
	ops->reset = snd_soc_ac97_reset;

	snd_ac97_rst_cfg = cfg;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_set_ac97_ops_of_reset);

int snd_soc_ac97_register_dai_links(struct snd_soc_card *card)
{
	int i;
	int ret;

	/* register any AC97 codecs */
	for (i = 0; i < card->num_rtd; i++) {
		ret = soc_register_ac97_dai_link(&card->rtd[i]);
		if (ret < 0)
			goto err;
	}

	return 0;
err:
	dev_err(card->dev,
		"ASoC: failed to register AC97: %d\n", ret);
	while (--i >= 0)
		soc_unregister_ac97_dai_link(&card->rtd[i]);
	return ret;
}

void snd_soc_ac97_add_pdata(struct snd_soc_pcm_runtime *rtd)
{
	unsigned int i;

	/* add platform data for AC97 devices */
	for (i = 0; i < rtd->num_codecs; i++) {
		if (rtd->codec_dais[i]->driver->ac97_control)
			snd_ac97_dev_add_pdata(rtd->codec_dais[i]->codec->ac97,
					       rtd->cpu_dai->ac97_pdata);
	}
}

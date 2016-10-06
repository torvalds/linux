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
#include <linux/gpio/driver.h>
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

struct snd_ac97_gpio_priv {
#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif
	unsigned int gpios_set;
	struct snd_soc_codec *codec;
};

static struct snd_ac97_bus soc_ac97_bus = {
	.ops = NULL, /* Gets initialized in snd_soc_set_ac97_ops() */
};

static void soc_ac97_device_release(struct device *dev)
{
	kfree(to_ac97_t(dev));
}

#ifdef CONFIG_GPIOLIB
static inline struct snd_soc_codec *gpio_to_codec(struct gpio_chip *chip)
{
	struct snd_ac97_gpio_priv *gpio_priv = gpiochip_get_data(chip);

	return gpio_priv->codec;
}

static int snd_soc_ac97_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	if (offset >= AC97_NUM_GPIOS)
		return -EINVAL;

	return 0;
}

static int snd_soc_ac97_gpio_direction_in(struct gpio_chip *chip,
					  unsigned offset)
{
	struct snd_soc_codec *codec = gpio_to_codec(chip);

	dev_dbg(codec->dev, "set gpio %d to output\n", offset);
	return snd_soc_update_bits(codec, AC97_GPIO_CFG,
				   1 << offset, 1 << offset);
}

static int snd_soc_ac97_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct snd_soc_codec *codec = gpio_to_codec(chip);
	int ret;

	ret = snd_soc_read(codec, AC97_GPIO_STATUS);
	dev_dbg(codec->dev, "get gpio %d : %d\n", offset,
		ret < 0 ? ret : ret & (1 << offset));

	return ret < 0 ? ret : !!(ret & (1 << offset));
}

static void snd_soc_ac97_gpio_set(struct gpio_chip *chip, unsigned offset,
				  int value)
{
	struct snd_ac97_gpio_priv *gpio_priv = gpiochip_get_data(chip);
	struct snd_soc_codec *codec = gpio_to_codec(chip);

	gpio_priv->gpios_set &= ~(1 << offset);
	gpio_priv->gpios_set |= (!!value) << offset;
	snd_soc_write(codec, AC97_GPIO_STATUS, gpio_priv->gpios_set);
	dev_dbg(codec->dev, "set gpio %d to %d\n", offset, !!value);
}

static int snd_soc_ac97_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct snd_soc_codec *codec = gpio_to_codec(chip);

	dev_dbg(codec->dev, "set gpio %d to output\n", offset);
	snd_soc_ac97_gpio_set(chip, offset, value);
	return snd_soc_update_bits(codec, AC97_GPIO_CFG, 1 << offset, 0);
}

static const struct gpio_chip snd_soc_ac97_gpio_chip = {
	.label			= "snd_soc_ac97",
	.owner			= THIS_MODULE,
	.request		= snd_soc_ac97_gpio_request,
	.direction_input	= snd_soc_ac97_gpio_direction_in,
	.get			= snd_soc_ac97_gpio_get,
	.direction_output	= snd_soc_ac97_gpio_direction_out,
	.set			= snd_soc_ac97_gpio_set,
	.can_sleep		= 1,
};

static int snd_soc_ac97_init_gpio(struct snd_ac97 *ac97,
				  struct snd_soc_codec *codec)
{
	struct snd_ac97_gpio_priv *gpio_priv;
	int ret;

	gpio_priv = devm_kzalloc(codec->dev, sizeof(*gpio_priv), GFP_KERNEL);
	if (!gpio_priv)
		return -ENOMEM;
	ac97->gpio_priv = gpio_priv;
	gpio_priv->codec = codec;
	gpio_priv->gpio_chip = snd_soc_ac97_gpio_chip;
	gpio_priv->gpio_chip.ngpio = AC97_NUM_GPIOS;
	gpio_priv->gpio_chip.parent = codec->dev;
	gpio_priv->gpio_chip.base = -1;

	ret = gpiochip_add_data(&gpio_priv->gpio_chip, gpio_priv);
	if (ret != 0)
		dev_err(codec->dev, "Failed to add GPIOs: %d\n", ret);
	return ret;
}

static void snd_soc_ac97_free_gpio(struct snd_ac97 *ac97)
{
	gpiochip_remove(&ac97->gpio_priv->gpio_chip);
}
#else
static int snd_soc_ac97_init_gpio(struct snd_ac97 *ac97,
				  struct snd_soc_codec *codec)
{
	return 0;
}

static void snd_soc_ac97_free_gpio(struct snd_ac97 *ac97)
{
}
#endif

/**
 * snd_soc_alloc_ac97_codec() - Allocate new a AC'97 device
 * @codec: The CODEC for which to create the AC'97 device
 *
 * Allocated a new snd_ac97 device and intializes it, but does not yet register
 * it. The caller is responsible to either call device_add(&ac97->dev) to
 * register the device, or to call put_device(&ac97->dev) to free the device.
 *
 * Returns: A snd_ac97 device or a PTR_ERR in case of an error.
 */
struct snd_ac97 *snd_soc_alloc_ac97_codec(struct snd_soc_codec *codec)
{
	struct snd_ac97 *ac97;

	ac97 = kzalloc(sizeof(struct snd_ac97), GFP_KERNEL);
	if (ac97 == NULL)
		return ERR_PTR(-ENOMEM);

	ac97->bus = &soc_ac97_bus;
	ac97->num = 0;

	ac97->dev.bus = &ac97_bus_type;
	ac97->dev.parent = codec->component.card->dev;
	ac97->dev.release = soc_ac97_device_release;

	dev_set_name(&ac97->dev, "%d-%d:%s",
		     codec->component.card->snd_card->number, 0,
		     codec->component.name);

	device_initialize(&ac97->dev);

	return ac97;
}
EXPORT_SYMBOL(snd_soc_alloc_ac97_codec);

/**
 * snd_soc_new_ac97_codec - initailise AC97 device
 * @codec: audio codec
 * @id: The expected device ID
 * @id_mask: Mask that is applied to the device ID before comparing with @id
 *
 * Initialises AC97 codec resources for use by ad-hoc devices only.
 *
 * If @id is not 0 this function will reset the device, then read the ID from
 * the device and check if it matches the expected ID. If it doesn't match an
 * error will be returned and device will not be registered.
 *
 * Returns: A PTR_ERR() on failure or a valid snd_ac97 struct on success.
 */
struct snd_ac97 *snd_soc_new_ac97_codec(struct snd_soc_codec *codec,
	unsigned int id, unsigned int id_mask)
{
	struct snd_ac97 *ac97;
	int ret;

	ac97 = snd_soc_alloc_ac97_codec(codec);
	if (IS_ERR(ac97))
		return ac97;

	if (id) {
		ret = snd_ac97_reset(ac97, false, id, id_mask);
		if (ret < 0) {
			dev_err(codec->dev, "Failed to reset AC97 device: %d\n",
				ret);
			goto err_put_device;
		}
	}

	ret = device_add(&ac97->dev);
	if (ret)
		goto err_put_device;

	ret = snd_soc_ac97_init_gpio(ac97, codec);
	if (ret)
		goto err_put_device;

	return ac97;

err_put_device:
	put_device(&ac97->dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(snd_soc_new_ac97_codec);

/**
 * snd_soc_free_ac97_codec - free AC97 codec device
 * @codec: audio codec
 *
 * Frees AC97 codec device resources.
 */
void snd_soc_free_ac97_codec(struct snd_ac97 *ac97)
{
	snd_soc_ac97_free_gpio(ac97);
	device_del(&ac97->dev);
	ac97->bus = NULL;
	put_device(&ac97->dev);
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

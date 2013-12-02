/*
 * soc-devres.c  --  ALSA SoC Audio Layer devres functions
 *
 * Copyright (C) 2013 Linaro Ltd
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

static void devm_component_release(struct device *dev, void *res)
{
	snd_soc_unregister_component(*(struct device **)res);
}

/**
 * devm_snd_soc_register_component - resource managed component registration
 * @dev: Device used to manage component
 * @cmpnt_drv: Component driver
 * @dai_drv: DAI driver
 * @num_dai: Number of DAIs to register
 *
 * Register a component with automatic unregistration when the device is
 * unregistered.
 */
int devm_snd_soc_register_component(struct device *dev,
			 const struct snd_soc_component_driver *cmpnt_drv,
			 struct snd_soc_dai_driver *dai_drv, int num_dai)
{
	struct device **ptr;
	int ret;

	ptr = devres_alloc(devm_component_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = snd_soc_register_component(dev, cmpnt_drv, dai_drv, num_dai);
	if (ret == 0) {
		*ptr = dev;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_snd_soc_register_component);

static void devm_card_release(struct device *dev, void *res)
{
	snd_soc_unregister_card(*(struct snd_soc_card **)res);
}

/**
 * devm_snd_soc_register_card - resource managed card registration
 * @dev: Device used to manage card
 * @card: Card to register
 *
 * Register a card with automatic unregistration when the device is
 * unregistered.
 */
int devm_snd_soc_register_card(struct device *dev, struct snd_soc_card *card)
{
	struct device **ptr;
	int ret;

	ptr = devres_alloc(devm_card_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = snd_soc_register_card(card);
	if (ret == 0) {
		*ptr = dev;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_snd_soc_register_card);

#ifdef CONFIG_SND_SOC_GENERIC_DMAENGINE_PCM

static void devm_dmaengine_pcm_release(struct device *dev, void *res)
{
	snd_dmaengine_pcm_unregister(*(struct device **)res);
}

/**
 * devm_snd_dmaengine_pcm_register - resource managed dmaengine PCM registration
 * @dev: The parent device for the PCM device
 * @config: Platform specific PCM configuration
 * @flags: Platform specific quirks
 *
 * Register a dmaengine based PCM device with automatic unregistration when the
 * device is unregistered.
 */
int devm_snd_dmaengine_pcm_register(struct device *dev,
	const struct snd_dmaengine_pcm_config *config, unsigned int flags)
{
	struct device **ptr;
	int ret;

	ptr = devres_alloc(devm_dmaengine_pcm_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = snd_dmaengine_pcm_register(dev, config, flags);
	if (ret == 0) {
		*ptr = dev;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_snd_dmaengine_pcm_register);

#endif

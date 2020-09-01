// SPDX-License-Identifier: GPL-2.0+
//
// soc-devres.c  --  ALSA SoC Audio Layer devres functions
//
// Copyright (C) 2013 Linaro Ltd

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

static void devm_dai_release(struct device *dev, void *res)
{
	snd_soc_unregister_dai(*(struct snd_soc_dai **)res);
}

/**
 * devm_snd_soc_register_dai - resource-managed dai registration
 * @dev: Device used to manage component
 * @component: The component the DAIs are registered for
 * @dai_drv: DAI driver to use for the DAI
 * @legacy_dai_naming: if %true, use legacy single-name format;
 *	if %false, use multiple-name format;
 */
struct snd_soc_dai *devm_snd_soc_register_dai(struct device *dev,
					      struct snd_soc_component *component,
					      struct snd_soc_dai_driver *dai_drv,
					      bool legacy_dai_naming)
{
	struct snd_soc_dai **ptr;
	struct snd_soc_dai *dai;

	ptr = devres_alloc(devm_dai_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	dai = snd_soc_register_dai(component, dai_drv, legacy_dai_naming);
	if (dai) {
		*ptr = dai;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return dai;
}
EXPORT_SYMBOL_GPL(devm_snd_soc_register_dai);

static void devm_component_release(struct device *dev, void *res)
{
	const struct snd_soc_component_driver **cmpnt_drv = res;

	snd_soc_unregister_component_by_driver(dev, *cmpnt_drv);
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
	const struct snd_soc_component_driver **ptr;
	int ret;

	ptr = devres_alloc(devm_component_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = snd_soc_register_component(dev, cmpnt_drv, dai_drv, num_dai);
	if (ret == 0) {
		*ptr = cmpnt_drv;
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
	struct snd_soc_card **ptr;
	int ret;

	ptr = devres_alloc(devm_card_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = snd_soc_register_card(card);
	if (ret == 0) {
		*ptr = card;
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

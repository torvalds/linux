// SPDX-License-Identifier: GPL-2.0
//
// soc-component.c
//
// Copyright (C) 2019 Renesas Electronics Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
#include <linux/module.h>
#include <sound/soc.h>

/**
 * snd_soc_component_set_sysclk - configure COMPONENT system or master clock.
 * @component: COMPONENT
 * @clk_id: DAI specific clock ID
 * @source: Source for the clock
 * @freq: new clock frequency in Hz
 * @dir: new clock direction - input/output.
 *
 * Configures the CODEC master (MCLK) or system (SYSCLK) clocking.
 */
int snd_soc_component_set_sysclk(struct snd_soc_component *component,
				 int clk_id, int source, unsigned int freq,
				 int dir)
{
	if (component->driver->set_sysclk)
		return component->driver->set_sysclk(component, clk_id, source,
						     freq, dir);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_component_set_sysclk);

/*
 * snd_soc_component_set_pll - configure component PLL.
 * @component: COMPONENT
 * @pll_id: DAI specific PLL ID
 * @source: DAI specific source for the PLL
 * @freq_in: PLL input clock frequency in Hz
 * @freq_out: requested PLL output clock frequency in Hz
 *
 * Configures and enables PLL to generate output clock based on input clock.
 */
int snd_soc_component_set_pll(struct snd_soc_component *component, int pll_id,
			      int source, unsigned int freq_in,
			      unsigned int freq_out)
{
	if (component->driver->set_pll)
		return component->driver->set_pll(component, pll_id, source,
						  freq_in, freq_out);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_component_set_pll);

int snd_soc_component_enable_pin(struct snd_soc_component *component,
				 const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_enable_pin(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_enable_pin(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_enable_pin);

int snd_soc_component_enable_pin_unlocked(struct snd_soc_component *component,
					  const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_enable_pin_unlocked(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_enable_pin_unlocked(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_enable_pin_unlocked);

int snd_soc_component_disable_pin(struct snd_soc_component *component,
				  const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_disable_pin(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_disable_pin(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_disable_pin);

int snd_soc_component_disable_pin_unlocked(struct snd_soc_component *component,
					   const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_disable_pin_unlocked(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_disable_pin_unlocked(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_disable_pin_unlocked);

int snd_soc_component_nc_pin(struct snd_soc_component *component,
			     const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_nc_pin(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_nc_pin(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_nc_pin);

int snd_soc_component_nc_pin_unlocked(struct snd_soc_component *component,
				      const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_nc_pin_unlocked(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_nc_pin_unlocked(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_nc_pin_unlocked);

int snd_soc_component_get_pin_status(struct snd_soc_component *component,
				     const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_get_pin_status(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_get_pin_status(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_get_pin_status);

int snd_soc_component_force_enable_pin(struct snd_soc_component *component,
				       const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_force_enable_pin(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_force_enable_pin(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_force_enable_pin);

int snd_soc_component_force_enable_pin_unlocked(
	struct snd_soc_component *component,
	const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_force_enable_pin_unlocked(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_force_enable_pin_unlocked(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_force_enable_pin_unlocked);

/**
 * snd_soc_component_set_jack - configure component jack.
 * @component: COMPONENTs
 * @jack: structure to use for the jack
 * @data: can be used if codec driver need extra data for configuring jack
 *
 * Configures and enables jack detection function.
 */
int snd_soc_component_set_jack(struct snd_soc_component *component,
			       struct snd_soc_jack *jack, void *data)
{
	if (component->driver->set_jack)
		return component->driver->set_jack(component, jack, data);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_component_set_jack);

int snd_soc_component_module_get(struct snd_soc_component *component,
				 int upon_open)
{
	if (component->driver->module_get_upon_open == !!upon_open &&
	    !try_module_get(component->dev->driver->owner))
		return -ENODEV;

	return 0;
}

void snd_soc_component_module_put(struct snd_soc_component *component,
				  int upon_open)
{
	if (component->driver->module_get_upon_open == !!upon_open)
		module_put(component->dev->driver->owner);
}

int snd_soc_component_open(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	if (component->driver->ops &&
	    component->driver->ops->open)
		return component->driver->ops->open(substream);

	return 0;
}

int snd_soc_component_close(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream)
{
	if (component->driver->ops &&
	    component->driver->ops->close)
		return component->driver->ops->close(substream);

	return 0;
}

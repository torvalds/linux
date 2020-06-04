// SPDX-License-Identifier: GPL-2.0
//
// soc-component.c
//
// Copyright (C) 2019 Renesas Electronics Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
#include <linux/module.h>
#include <sound/soc.h>

int snd_soc_component_initialize(struct snd_soc_component *component,
				 const struct snd_soc_component_driver *driver,
				 struct device *dev, const char *name)
{
	INIT_LIST_HEAD(&component->dai_list);
	INIT_LIST_HEAD(&component->dobj_list);
	INIT_LIST_HEAD(&component->card_list);
	mutex_init(&component->io_mutex);

	component->name		= name;
	component->dev		= dev;
	component->driver	= driver;

	return 0;
}

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

void snd_soc_component_seq_notifier(struct snd_soc_component *component,
				    enum snd_soc_dapm_type type, int subseq)
{
	if (component->driver->seq_notifier)
		component->driver->seq_notifier(component, type, subseq);
}

int snd_soc_component_stream_event(struct snd_soc_component *component,
				   int event)
{
	if (component->driver->stream_event)
		return component->driver->stream_event(component, event);

	return 0;
}

int snd_soc_component_set_bias_level(struct snd_soc_component *component,
				     enum snd_soc_bias_level level)
{
	if (component->driver->set_bias_level)
		return component->driver->set_bias_level(component, level);

	return 0;
}

static int soc_component_pin(struct snd_soc_component *component,
			     const char *pin,
			     int (*pin_func)(struct snd_soc_dapm_context *dapm,
					     const char *pin))
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return pin_func(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = pin_func(dapm, full_name);
	kfree(full_name);

	return ret;
}

int snd_soc_component_enable_pin(struct snd_soc_component *component,
				 const char *pin)
{
	return soc_component_pin(component, pin, snd_soc_dapm_enable_pin);
}
EXPORT_SYMBOL_GPL(snd_soc_component_enable_pin);

int snd_soc_component_enable_pin_unlocked(struct snd_soc_component *component,
					  const char *pin)
{
	return soc_component_pin(component, pin, snd_soc_dapm_enable_pin_unlocked);
}
EXPORT_SYMBOL_GPL(snd_soc_component_enable_pin_unlocked);

int snd_soc_component_disable_pin(struct snd_soc_component *component,
				  const char *pin)
{
	return soc_component_pin(component, pin, snd_soc_dapm_disable_pin);
}
EXPORT_SYMBOL_GPL(snd_soc_component_disable_pin);

int snd_soc_component_disable_pin_unlocked(struct snd_soc_component *component,
					   const char *pin)
{
	return soc_component_pin(component, pin, snd_soc_dapm_disable_pin_unlocked);
}
EXPORT_SYMBOL_GPL(snd_soc_component_disable_pin_unlocked);

int snd_soc_component_nc_pin(struct snd_soc_component *component,
			     const char *pin)
{
	return soc_component_pin(component, pin, snd_soc_dapm_nc_pin);
}
EXPORT_SYMBOL_GPL(snd_soc_component_nc_pin);

int snd_soc_component_nc_pin_unlocked(struct snd_soc_component *component,
				      const char *pin)
{
	return soc_component_pin(component, pin, snd_soc_dapm_nc_pin_unlocked);
}
EXPORT_SYMBOL_GPL(snd_soc_component_nc_pin_unlocked);

int snd_soc_component_get_pin_status(struct snd_soc_component *component,
				     const char *pin)
{
	return soc_component_pin(component, pin, snd_soc_dapm_get_pin_status);
}
EXPORT_SYMBOL_GPL(snd_soc_component_get_pin_status);

int snd_soc_component_force_enable_pin(struct snd_soc_component *component,
				       const char *pin)
{
	return soc_component_pin(component, pin, snd_soc_dapm_force_enable_pin);
}
EXPORT_SYMBOL_GPL(snd_soc_component_force_enable_pin);

int snd_soc_component_force_enable_pin_unlocked(
	struct snd_soc_component *component,
	const char *pin)
{
	return soc_component_pin(component, pin, snd_soc_dapm_force_enable_pin_unlocked);
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
	if (component->driver->open)
		return component->driver->open(component, substream);
	return 0;
}

int snd_soc_component_close(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream)
{
	if (component->driver->close)
		return component->driver->close(component, substream);
	return 0;
}

int snd_soc_component_prepare(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	if (component->driver->prepare)
		return component->driver->prepare(component, substream);
	return 0;
}

int snd_soc_component_hw_params(struct snd_soc_component *component,
				struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	if (component->driver->hw_params)
		return component->driver->hw_params(component,
						    substream, params);
	return 0;
}

int snd_soc_component_hw_free(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	if (component->driver->hw_free)
		return component->driver->hw_free(component, substream);
	return 0;
}

int snd_soc_component_trigger(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream,
			      int cmd)
{
	if (component->driver->trigger)
		return component->driver->trigger(component, substream, cmd);
	return 0;
}

void snd_soc_component_suspend(struct snd_soc_component *component)
{
	if (component->driver->suspend)
		component->driver->suspend(component);
	component->suspended = 1;
}

void snd_soc_component_resume(struct snd_soc_component *component)
{
	if (component->driver->resume)
		component->driver->resume(component);
	component->suspended = 0;
}

int snd_soc_component_is_suspended(struct snd_soc_component *component)
{
	return component->suspended;
}

int snd_soc_component_probe(struct snd_soc_component *component)
{
	if (component->driver->probe)
		return component->driver->probe(component);

	return 0;
}

void snd_soc_component_remove(struct snd_soc_component *component)
{
	if (component->driver->remove)
		component->driver->remove(component);
}

int snd_soc_component_of_xlate_dai_id(struct snd_soc_component *component,
				      struct device_node *ep)
{
	if (component->driver->of_xlate_dai_id)
		return component->driver->of_xlate_dai_id(component, ep);

	return -ENOTSUPP;
}

int snd_soc_component_of_xlate_dai_name(struct snd_soc_component *component,
					struct of_phandle_args *args,
					const char **dai_name)
{
	if (component->driver->of_xlate_dai_name)
		return component->driver->of_xlate_dai_name(component,
						     args, dai_name);
	return -ENOTSUPP;
}

void snd_soc_component_setup_regmap(struct snd_soc_component *component)
{
	int val_bytes = regmap_get_val_bytes(component->regmap);

	/* Errors are legitimate for non-integer byte multiples */
	if (val_bytes > 0)
		component->val_bytes = val_bytes;
}

#ifdef CONFIG_REGMAP

/**
 * snd_soc_component_init_regmap() - Initialize regmap instance for the
 *                                   component
 * @component: The component for which to initialize the regmap instance
 * @regmap: The regmap instance that should be used by the component
 *
 * This function allows deferred assignment of the regmap instance that is
 * associated with the component. Only use this if the regmap instance is not
 * yet ready when the component is registered. The function must also be called
 * before the first IO attempt of the component.
 */
void snd_soc_component_init_regmap(struct snd_soc_component *component,
				   struct regmap *regmap)
{
	component->regmap = regmap;
	snd_soc_component_setup_regmap(component);
}
EXPORT_SYMBOL_GPL(snd_soc_component_init_regmap);

/**
 * snd_soc_component_exit_regmap() - De-initialize regmap instance for the
 *                                   component
 * @component: The component for which to de-initialize the regmap instance
 *
 * Calls regmap_exit() on the regmap instance associated to the component and
 * removes the regmap instance from the component.
 *
 * This function should only be used if snd_soc_component_init_regmap() was used
 * to initialize the regmap instance.
 */
void snd_soc_component_exit_regmap(struct snd_soc_component *component)
{
	regmap_exit(component->regmap);
	component->regmap = NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_component_exit_regmap);

#endif

int snd_soc_pcm_component_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component;
	int i;

	/* FIXME: use 1st pointer */
	for_each_rtd_components(rtd, i, component)
		if (component->driver->pointer)
			return component->driver->pointer(component, substream);

	return 0;
}

int snd_soc_pcm_component_ioctl(struct snd_pcm_substream *substream,
				unsigned int cmd, void *arg)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component;
	int i;

	/* FIXME: use 1st ioctl */
	for_each_rtd_components(rtd, i, component)
		if (component->driver->ioctl)
			return component->driver->ioctl(component, substream,
							cmd, arg);

	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

int snd_soc_pcm_component_sync_stop(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component;
	int i, ret;

	for_each_rtd_components(rtd, i, component) {
		if (component->driver->sync_stop) {
			ret = component->driver->sync_stop(component,
							   substream);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

int snd_soc_pcm_component_copy_user(struct snd_pcm_substream *substream,
				    int channel, unsigned long pos,
				    void __user *buf, unsigned long bytes)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component;
	int i;

	/* FIXME. it returns 1st copy now */
	for_each_rtd_components(rtd, i, component)
		if (component->driver->copy_user)
			return component->driver->copy_user(
				component, substream, channel, pos, buf, bytes);

	return -EINVAL;
}

struct page *snd_soc_pcm_component_page(struct snd_pcm_substream *substream,
					unsigned long offset)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component;
	struct page *page;
	int i;

	/* FIXME. it returns 1st page now */
	for_each_rtd_components(rtd, i, component) {
		if (component->driver->page) {
			page = component->driver->page(component,
						       substream, offset);
			if (page)
				return page;
		}
	}

	return NULL;
}

int snd_soc_pcm_component_mmap(struct snd_pcm_substream *substream,
			       struct vm_area_struct *vma)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component;
	int i;

	/* FIXME. it returns 1st mmap now */
	for_each_rtd_components(rtd, i, component)
		if (component->driver->mmap)
			return component->driver->mmap(component,
						       substream, vma);

	return -EINVAL;
}

int snd_soc_pcm_component_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component;
	int ret;
	int i;

	for_each_rtd_components(rtd, i, component) {
		if (component->driver->pcm_construct) {
			ret = component->driver->pcm_construct(component, rtd);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

void snd_soc_pcm_component_free(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component;
	int i;

	if (!rtd->pcm)
		return;

	for_each_rtd_components(rtd, i, component)
		if (component->driver->pcm_destruct)
			component->driver->pcm_destruct(component, rtd->pcm);
}

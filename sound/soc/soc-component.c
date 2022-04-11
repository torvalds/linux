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

int snd_soc_component_prepare(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	if (component->driver->ops &&
	    component->driver->ops->prepare)
		return component->driver->ops->prepare(substream);

	return 0;
}

int snd_soc_component_hw_params(struct snd_soc_component *component,
				struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	if (component->driver->ops &&
	    component->driver->ops->hw_params)
		return component->driver->ops->hw_params(substream, params);

	return 0;
}

int snd_soc_component_hw_free(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	if (component->driver->ops &&
	    component->driver->ops->hw_free)
		return component->driver->ops->hw_free(substream);

	return 0;
}

int snd_soc_component_trigger(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream,
			      int cmd)
{
	if (component->driver->ops &&
	    component->driver->ops->trigger)
		return component->driver->ops->trigger(substream, cmd);

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

int snd_soc_pcm_component_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component;
	struct snd_soc_rtdcom_list *rtdcom;

	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		/* FIXME: use 1st pointer */
		if (component->driver->ops &&
		    component->driver->ops->pointer)
			return component->driver->ops->pointer(substream);
	}

	return 0;
}

int snd_soc_pcm_component_ioctl(struct snd_pcm_substream *substream,
				unsigned int cmd, void *arg)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component;
	struct snd_soc_rtdcom_list *rtdcom;

	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		/* FIXME: use 1st ioctl */
		if (component->driver->ops &&
		    component->driver->ops->ioctl)
			return component->driver->ops->ioctl(substream,
							     cmd, arg);
	}

	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

int snd_soc_pcm_component_copy_user(struct snd_pcm_substream *substream,
				    int channel, unsigned long pos,
				    void __user *buf, unsigned long bytes)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_rtdcom_list *rtdcom;
	struct snd_soc_component *component;

	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		/* FIXME. it returns 1st copy now */
		if (component->driver->ops &&
		    component->driver->ops->copy_user)
			return component->driver->ops->copy_user(
				substream, channel, pos, buf, bytes);
	}

	return -EINVAL;
}

struct page *snd_soc_pcm_component_page(struct snd_pcm_substream *substream,
					unsigned long offset)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_rtdcom_list *rtdcom;
	struct snd_soc_component *component;
	struct page *page;

	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		/* FIXME. it returns 1st page now */
		if (component->driver->ops &&
		    component->driver->ops->page) {
			page = component->driver->ops->page(substream, offset);
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
	struct snd_soc_rtdcom_list *rtdcom;
	struct snd_soc_component *component;

	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		/* FIXME. it returns 1st mmap now */
		if (component->driver->ops &&
		    component->driver->ops->mmap)
			return component->driver->ops->mmap(substream, vma);
	}

	return -EINVAL;
}

int snd_soc_pcm_component_new(struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	struct snd_soc_rtdcom_list *rtdcom;
	struct snd_soc_component *component;
	int ret;

	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		if (component->driver->pcm_new) {
			ret = component->driver->pcm_new(rtd);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

void snd_soc_pcm_component_free(struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	struct snd_soc_rtdcom_list *rtdcom;
	struct snd_soc_component *component;

	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		if (component->driver->pcm_free)
			component->driver->pcm_free(pcm);
	}
}

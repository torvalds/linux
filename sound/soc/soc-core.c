// SPDX-License-Identifier: GPL-2.0+
//
// soc-core.c  --  ALSA SoC Audio Layer
//
// Copyright 2005 Wolfson Microelectronics PLC.
// Copyright 2005 Openedhand Ltd.
// Copyright (C) 2010 Slimlogic Ltd.
// Copyright (C) 2010 Texas Instruments Inc.
//
// Author: Liam Girdwood <lrg@slimlogic.co.uk>
//         with code, comments and ideas from :-
//         Richard Purdie <richard@openedhand.com>
//
//  TODO:
//   o Add hw rules to enforce rates, etc.
//   o More testing with other codecs/machines.
//   o Add more codecs and platforms to ensure good API coverage.
//   o Support TDM on PCM and I2S

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/dmi.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dpcm.h>
#include <sound/soc-topology.h>
#include <sound/initval.h>

#define CREATE_TRACE_POINTS
#include <trace/events/asoc.h>

#define NAME_SIZE	32

#ifdef CONFIG_DEBUG_FS
struct dentry *snd_soc_debugfs_root;
EXPORT_SYMBOL_GPL(snd_soc_debugfs_root);
#endif

static DEFINE_MUTEX(client_mutex);
static LIST_HEAD(component_list);
static LIST_HEAD(unbind_card_list);

#define for_each_component(component)			\
	list_for_each_entry(component, &component_list, list)

/*
 * This is a timeout to do a DAPM powerdown after a stream is closed().
 * It can be used to eliminate pops between different playback streams, e.g.
 * between two audio tracks.
 */
static int pmdown_time = 5000;
module_param(pmdown_time, int, 0);
MODULE_PARM_DESC(pmdown_time, "DAPM stream powerdown time (msecs)");

/*
 * If a DMI filed contain strings in this blacklist (e.g.
 * "Type2 - Board Manufacturer" or "Type1 - TBD by OEM"), it will be taken
 * as invalid and dropped when setting the card long name from DMI info.
 */
static const char * const dmi_blacklist[] = {
	"To be filled by OEM",
	"TBD by OEM",
	"Default String",
	"Board Manufacturer",
	"Board Vendor Name",
	"Board Product Name",
	NULL,	/* terminator */
};

static ssize_t pmdown_time_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct snd_soc_pcm_runtime *rtd = dev_get_drvdata(dev);

	return sprintf(buf, "%ld\n", rtd->pmdown_time);
}

static ssize_t pmdown_time_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct snd_soc_pcm_runtime *rtd = dev_get_drvdata(dev);
	int ret;

	ret = kstrtol(buf, 10, &rtd->pmdown_time);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR(pmdown_time, 0644, pmdown_time_show, pmdown_time_set);

static struct attribute *soc_dev_attrs[] = {
	&dev_attr_pmdown_time.attr,
	NULL
};

static umode_t soc_dev_attr_is_visible(struct kobject *kobj,
				       struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct snd_soc_pcm_runtime *rtd = dev_get_drvdata(dev);

	if (attr == &dev_attr_pmdown_time.attr)
		return attr->mode; /* always visible */
	return rtd->num_codecs ? attr->mode : 0; /* enabled only with codec */
}

static const struct attribute_group soc_dapm_dev_group = {
	.attrs = soc_dapm_dev_attrs,
	.is_visible = soc_dev_attr_is_visible,
};

static const struct attribute_group soc_dev_group = {
	.attrs = soc_dev_attrs,
	.is_visible = soc_dev_attr_is_visible,
};

static const struct attribute_group *soc_dev_attr_groups[] = {
	&soc_dapm_dev_group,
	&soc_dev_group,
	NULL
};

#ifdef CONFIG_DEBUG_FS
static void soc_init_component_debugfs(struct snd_soc_component *component)
{
	if (!component->card->debugfs_card_root)
		return;

	if (component->debugfs_prefix) {
		char *name;

		name = kasprintf(GFP_KERNEL, "%s:%s",
			component->debugfs_prefix, component->name);
		if (name) {
			component->debugfs_root = debugfs_create_dir(name,
				component->card->debugfs_card_root);
			kfree(name);
		}
	} else {
		component->debugfs_root = debugfs_create_dir(component->name,
				component->card->debugfs_card_root);
	}

	if (!component->debugfs_root) {
		dev_warn(component->dev,
			"ASoC: Failed to create component debugfs directory\n");
		return;
	}

	snd_soc_dapm_debugfs_init(snd_soc_component_get_dapm(component),
		component->debugfs_root);
}

static void soc_cleanup_component_debugfs(struct snd_soc_component *component)
{
	debugfs_remove_recursive(component->debugfs_root);
}

static int dai_list_show(struct seq_file *m, void *v)
{
	struct snd_soc_component *component;
	struct snd_soc_dai *dai;

	mutex_lock(&client_mutex);

	for_each_component(component)
		for_each_component_dais(component, dai)
			seq_printf(m, "%s\n", dai->name);

	mutex_unlock(&client_mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dai_list);

static int component_list_show(struct seq_file *m, void *v)
{
	struct snd_soc_component *component;

	mutex_lock(&client_mutex);

	for_each_component(component)
		seq_printf(m, "%s\n", component->name);

	mutex_unlock(&client_mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(component_list);

static void soc_init_card_debugfs(struct snd_soc_card *card)
{
	if (!snd_soc_debugfs_root)
		return;

	card->debugfs_card_root = debugfs_create_dir(card->name,
						     snd_soc_debugfs_root);
	if (!card->debugfs_card_root) {
		dev_warn(card->dev,
			 "ASoC: Failed to create card debugfs directory\n");
		return;
	}

	card->debugfs_pop_time = debugfs_create_u32("dapm_pop_time", 0644,
						    card->debugfs_card_root,
						    &card->pop_time);
	if (!card->debugfs_pop_time)
		dev_warn(card->dev,
			 "ASoC: Failed to create pop time debugfs file\n");
}

static void soc_cleanup_card_debugfs(struct snd_soc_card *card)
{
	debugfs_remove_recursive(card->debugfs_card_root);
}

static void snd_soc_debugfs_init(void)
{
	snd_soc_debugfs_root = debugfs_create_dir("asoc", NULL);
	if (IS_ERR_OR_NULL(snd_soc_debugfs_root)) {
		pr_warn("ASoC: Failed to create debugfs directory\n");
		snd_soc_debugfs_root = NULL;
		return;
	}

	if (!debugfs_create_file("dais", 0444, snd_soc_debugfs_root, NULL,
				 &dai_list_fops))
		pr_warn("ASoC: Failed to create DAI list debugfs file\n");

	if (!debugfs_create_file("components", 0444, snd_soc_debugfs_root, NULL,
				 &component_list_fops))
		pr_warn("ASoC: Failed to create component list debugfs file\n");
}

static void snd_soc_debugfs_exit(void)
{
	debugfs_remove_recursive(snd_soc_debugfs_root);
}

#else

static inline void soc_init_component_debugfs(
	struct snd_soc_component *component)
{
}

static inline void soc_cleanup_component_debugfs(
	struct snd_soc_component *component)
{
}

static inline void soc_init_card_debugfs(struct snd_soc_card *card)
{
}

static inline void soc_cleanup_card_debugfs(struct snd_soc_card *card)
{
}

static inline void snd_soc_debugfs_init(void)
{
}

static inline void snd_soc_debugfs_exit(void)
{
}

#endif

static int snd_soc_rtdcom_add(struct snd_soc_pcm_runtime *rtd,
			      struct snd_soc_component *component)
{
	struct snd_soc_rtdcom_list *rtdcom;
	struct snd_soc_rtdcom_list *new_rtdcom;

	for_each_rtdcom(rtd, rtdcom) {
		/* already connected */
		if (rtdcom->component == component)
			return 0;
	}

	new_rtdcom = kmalloc(sizeof(*new_rtdcom), GFP_KERNEL);
	if (!new_rtdcom)
		return -ENOMEM;

	new_rtdcom->component = component;
	INIT_LIST_HEAD(&new_rtdcom->list);

	list_add_tail(&new_rtdcom->list, &rtd->component_list);

	return 0;
}

static void snd_soc_rtdcom_del_all(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_rtdcom_list *rtdcom1, *rtdcom2;

	for_each_rtdcom_safe(rtd, rtdcom1, rtdcom2)
		kfree(rtdcom1);

	INIT_LIST_HEAD(&rtd->component_list);
}

struct snd_soc_component *snd_soc_rtdcom_lookup(struct snd_soc_pcm_runtime *rtd,
						const char *driver_name)
{
	struct snd_soc_rtdcom_list *rtdcom;

	if (!driver_name)
		return NULL;

	for_each_rtdcom(rtd, rtdcom) {
		const char *component_name = rtdcom->component->driver->name;

		if (!component_name)
			continue;

		if ((component_name == driver_name) ||
		    strcmp(component_name, driver_name) == 0)
			return rtdcom->component;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_rtdcom_lookup);

struct snd_pcm_substream *snd_soc_get_dai_substream(struct snd_soc_card *card,
		const char *dai_link, int stream)
{
	struct snd_soc_pcm_runtime *rtd;

	for_each_card_rtds(card, rtd) {
		if (rtd->dai_link->no_pcm &&
			!strcmp(rtd->dai_link->name, dai_link))
			return rtd->pcm->streams[stream].substream;
	}
	dev_dbg(card->dev, "ASoC: failed to find dai link %s\n", dai_link);
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_get_dai_substream);

static const struct snd_soc_ops null_snd_soc_ops;

static struct snd_soc_pcm_runtime *soc_new_pcm_runtime(
	struct snd_soc_card *card, struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;

	rtd = kzalloc(sizeof(struct snd_soc_pcm_runtime), GFP_KERNEL);
	if (!rtd)
		return NULL;

	INIT_LIST_HEAD(&rtd->component_list);
	rtd->card = card;
	rtd->dai_link = dai_link;
	if (!rtd->dai_link->ops)
		rtd->dai_link->ops = &null_snd_soc_ops;

	rtd->codec_dais = kcalloc(dai_link->num_codecs,
					sizeof(struct snd_soc_dai *),
					GFP_KERNEL);
	if (!rtd->codec_dais) {
		kfree(rtd);
		return NULL;
	}

	return rtd;
}

static void soc_free_pcm_runtime(struct snd_soc_pcm_runtime *rtd)
{
	kfree(rtd->codec_dais);
	snd_soc_rtdcom_del_all(rtd);
	kfree(rtd);
}

static void soc_add_pcm_runtime(struct snd_soc_card *card,
		struct snd_soc_pcm_runtime *rtd)
{
	list_add_tail(&rtd->list, &card->rtd_list);
	rtd->num = card->num_rtd;
	card->num_rtd++;
}

static void soc_remove_pcm_runtimes(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd, *_rtd;

	for_each_card_rtds_safe(card, rtd, _rtd) {
		list_del(&rtd->list);
		soc_free_pcm_runtime(rtd);
	}

	card->num_rtd = 0;
}

struct snd_soc_pcm_runtime *snd_soc_get_pcm_runtime(struct snd_soc_card *card,
		const char *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;

	for_each_card_rtds(card, rtd) {
		if (!strcmp(rtd->dai_link->name, dai_link))
			return rtd;
	}
	dev_dbg(card->dev, "ASoC: failed to find rtd %s\n", dai_link);
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_get_pcm_runtime);

static void snd_soc_flush_all_delayed_work(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;

	for_each_card_rtds(card, rtd)
		flush_delayed_work(&rtd->delayed_work);
}

static void codec2codec_close_delayed_work(struct work_struct *work)
{
	/*
	 * Currently nothing to do for c2c links
	 * Since c2c links are internal nodes in the DAPM graph and
	 * don't interface with the outside world or application layer
	 * we don't have to do any special handling on close.
	 */
}

#ifdef CONFIG_PM_SLEEP
/* powers down audio subsystem for suspend */
int snd_soc_suspend(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct snd_soc_component *component;
	struct snd_soc_pcm_runtime *rtd;
	int i;

	/* If the card is not initialized yet there is nothing to do */
	if (!card->instantiated)
		return 0;

	/*
	 * Due to the resume being scheduled into a workqueue we could
	 * suspend before that's finished - wait for it to complete.
	 */
	snd_power_wait(card->snd_card, SNDRV_CTL_POWER_D0);

	/* we're going to block userspace touching us until resume completes */
	snd_power_change_state(card->snd_card, SNDRV_CTL_POWER_D3hot);

	/* mute any active DACs */
	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *dai;

		if (rtd->dai_link->ignore_suspend)
			continue;

		for_each_rtd_codec_dai(rtd, i, dai) {
			struct snd_soc_dai_driver *drv = dai->driver;

			if (drv->ops->digital_mute && dai->playback_active)
				drv->ops->digital_mute(dai, 1);
		}
	}

	/* suspend all pcms */
	for_each_card_rtds(card, rtd) {
		if (rtd->dai_link->ignore_suspend)
			continue;

		snd_pcm_suspend_all(rtd->pcm);
	}

	if (card->suspend_pre)
		card->suspend_pre(card);

	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

		if (rtd->dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->suspend && !cpu_dai->driver->bus_control)
			cpu_dai->driver->suspend(cpu_dai);
	}

	/* close any waiting streams */
	snd_soc_flush_all_delayed_work(card);

	for_each_card_rtds(card, rtd) {

		if (rtd->dai_link->ignore_suspend)
			continue;

		snd_soc_dapm_stream_event(rtd,
					  SNDRV_PCM_STREAM_PLAYBACK,
					  SND_SOC_DAPM_STREAM_SUSPEND);

		snd_soc_dapm_stream_event(rtd,
					  SNDRV_PCM_STREAM_CAPTURE,
					  SND_SOC_DAPM_STREAM_SUSPEND);
	}

	/* Recheck all endpoints too, their state is affected by suspend */
	dapm_mark_endpoints_dirty(card);
	snd_soc_dapm_sync(&card->dapm);

	/* suspend all COMPONENTs */
	for_each_card_components(card, component) {
		struct snd_soc_dapm_context *dapm =
				snd_soc_component_get_dapm(component);

		/*
		 * If there are paths active then the COMPONENT will be held
		 * with bias _ON and should not be suspended.
		 */
		if (!component->suspended) {
			switch (snd_soc_dapm_get_bias_level(dapm)) {
			case SND_SOC_BIAS_STANDBY:
				/*
				 * If the COMPONENT is capable of idle
				 * bias off then being in STANDBY
				 * means it's doing something,
				 * otherwise fall through.
				 */
				if (dapm->idle_bias_off) {
					dev_dbg(component->dev,
						"ASoC: idle_bias_off CODEC on over suspend\n");
					break;
				}
				/* fall through */

			case SND_SOC_BIAS_OFF:
				if (component->driver->suspend)
					component->driver->suspend(component);
				component->suspended = 1;
				if (component->regmap)
					regcache_mark_dirty(component->regmap);
				/* deactivate pins to sleep state */
				pinctrl_pm_select_sleep_state(component->dev);
				break;
			default:
				dev_dbg(component->dev,
					"ASoC: COMPONENT is on over suspend\n");
				break;
			}
		}
	}

	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

		if (rtd->dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->suspend && cpu_dai->driver->bus_control)
			cpu_dai->driver->suspend(cpu_dai);

		/* deactivate pins to sleep state */
		pinctrl_pm_select_sleep_state(cpu_dai->dev);
	}

	if (card->suspend_post)
		card->suspend_post(card);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_suspend);

/*
 * deferred resume work, so resume can complete before we finished
 * setting our codec back up, which can be very slow on I2C
 */
static void soc_resume_deferred(struct work_struct *work)
{
	struct snd_soc_card *card =
			container_of(work, struct snd_soc_card,
				     deferred_resume_work);
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_component *component;
	int i;

	/*
	 * our power state is still SNDRV_CTL_POWER_D3hot from suspend time,
	 * so userspace apps are blocked from touching us
	 */

	dev_dbg(card->dev, "ASoC: starting resume work\n");

	/* Bring us up into D2 so that DAPM starts enabling things */
	snd_power_change_state(card->snd_card, SNDRV_CTL_POWER_D2);

	if (card->resume_pre)
		card->resume_pre(card);

	/* resume control bus DAIs */
	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

		if (rtd->dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->resume && cpu_dai->driver->bus_control)
			cpu_dai->driver->resume(cpu_dai);
	}

	for_each_card_components(card, component) {
		if (component->suspended) {
			if (component->driver->resume)
				component->driver->resume(component);
			component->suspended = 0;
		}
	}

	for_each_card_rtds(card, rtd) {

		if (rtd->dai_link->ignore_suspend)
			continue;

		snd_soc_dapm_stream_event(rtd,
					  SNDRV_PCM_STREAM_PLAYBACK,
					  SND_SOC_DAPM_STREAM_RESUME);

		snd_soc_dapm_stream_event(rtd,
					  SNDRV_PCM_STREAM_CAPTURE,
					  SND_SOC_DAPM_STREAM_RESUME);
	}

	/* unmute any active DACs */
	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *dai;

		if (rtd->dai_link->ignore_suspend)
			continue;

		for_each_rtd_codec_dai(rtd, i, dai) {
			struct snd_soc_dai_driver *drv = dai->driver;

			if (drv->ops->digital_mute && dai->playback_active)
				drv->ops->digital_mute(dai, 0);
		}
	}

	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

		if (rtd->dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->resume && !cpu_dai->driver->bus_control)
			cpu_dai->driver->resume(cpu_dai);
	}

	if (card->resume_post)
		card->resume_post(card);

	dev_dbg(card->dev, "ASoC: resume work completed\n");

	/* Recheck all endpoints too, their state is affected by suspend */
	dapm_mark_endpoints_dirty(card);
	snd_soc_dapm_sync(&card->dapm);

	/* userspace can access us now we are back as we were before */
	snd_power_change_state(card->snd_card, SNDRV_CTL_POWER_D0);
}

/* powers up audio subsystem after a suspend */
int snd_soc_resume(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	bool bus_control = false;
	struct snd_soc_pcm_runtime *rtd;

	/* If the card is not initialized yet there is nothing to do */
	if (!card->instantiated)
		return 0;

	/* activate pins from sleep state */
	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *codec_dai;
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
		int j;

		if (cpu_dai->active)
			pinctrl_pm_select_default_state(cpu_dai->dev);

		for_each_rtd_codec_dai(rtd, j, codec_dai) {
			if (codec_dai->active)
				pinctrl_pm_select_default_state(codec_dai->dev);
		}
	}

	/*
	 * DAIs that also act as the control bus master might have other drivers
	 * hanging off them so need to resume immediately. Other drivers don't
	 * have that problem and may take a substantial amount of time to resume
	 * due to I/O costs and anti-pop so handle them out of line.
	 */
	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

		bus_control |= cpu_dai->driver->bus_control;
	}
	if (bus_control) {
		dev_dbg(dev, "ASoC: Resuming control bus master immediately\n");
		soc_resume_deferred(&card->deferred_resume_work);
	} else {
		dev_dbg(dev, "ASoC: Scheduling resume work\n");
		if (!schedule_work(&card->deferred_resume_work))
			dev_err(dev, "ASoC: resume work item may be lost\n");
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_resume);
#else
#define snd_soc_suspend NULL
#define snd_soc_resume NULL
#endif

static const struct snd_soc_dai_ops null_dai_ops = {
};

static struct snd_soc_component *soc_find_component(
	const struct device_node *of_node, const char *name)
{
	struct snd_soc_component *component;
	struct device_node *component_of_node;

	lockdep_assert_held(&client_mutex);

	for_each_component(component) {
		if (of_node) {
			component_of_node = component->dev->of_node;
			if (!component_of_node && component->dev->parent)
				component_of_node = component->dev->parent->of_node;

			if (component_of_node == of_node)
				return component;
		} else if (name && strcmp(component->name, name) == 0) {
			return component;
		}
	}

	return NULL;
}

static int snd_soc_is_matching_component(
	const struct snd_soc_dai_link_component *dlc,
	struct snd_soc_component *component)
{
	struct device_node *component_of_node;

	component_of_node = component->dev->of_node;
	if (!component_of_node && component->dev->parent)
		component_of_node = component->dev->parent->of_node;

	if (dlc->of_node && component_of_node != dlc->of_node)
		return 0;
	if (dlc->name && strcmp(component->name, dlc->name))
		return 0;

	return 1;
}

/**
 * snd_soc_find_dai - Find a registered DAI
 *
 * @dlc: name of the DAI or the DAI driver and optional component info to match
 *
 * This function will search all registered components and their DAIs to
 * find the DAI of the same name. The component's of_node and name
 * should also match if being specified.
 *
 * Return: pointer of DAI, or NULL if not found.
 */
struct snd_soc_dai *snd_soc_find_dai(
	const struct snd_soc_dai_link_component *dlc)
{
	struct snd_soc_component *component;
	struct snd_soc_dai *dai;

	lockdep_assert_held(&client_mutex);

	/* Find CPU DAI from registered DAIs */
	for_each_component(component) {
		if (!snd_soc_is_matching_component(dlc, component))
			continue;
		for_each_component_dais(component, dai) {
			if (dlc->dai_name && strcmp(dai->name, dlc->dai_name)
			    && (!dai->driver->name
				|| strcmp(dai->driver->name, dlc->dai_name)))
				continue;

			return dai;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_find_dai);

/**
 * snd_soc_find_dai_link - Find a DAI link
 *
 * @card: soc card
 * @id: DAI link ID to match
 * @name: DAI link name to match, optional
 * @stream_name: DAI link stream name to match, optional
 *
 * This function will search all existing DAI links of the soc card to
 * find the link of the same ID. Since DAI links may not have their
 * unique ID, so name and stream name should also match if being
 * specified.
 *
 * Return: pointer of DAI link, or NULL if not found.
 */
struct snd_soc_dai_link *snd_soc_find_dai_link(struct snd_soc_card *card,
					       int id, const char *name,
					       const char *stream_name)
{
	struct snd_soc_dai_link *link, *_link;

	lockdep_assert_held(&client_mutex);

	for_each_card_links_safe(card, link, _link) {
		if (link->id != id)
			continue;

		if (name && (!link->name || strcmp(name, link->name)))
			continue;

		if (stream_name && (!link->stream_name
			|| strcmp(stream_name, link->stream_name)))
			continue;

		return link;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_find_dai_link);

static bool soc_is_dai_link_bound(struct snd_soc_card *card,
		struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;

	for_each_card_rtds(card, rtd) {
		if (rtd->dai_link == dai_link)
			return true;
	}

	return false;
}

static int soc_bind_dai_link(struct snd_soc_card *card,
	struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai_link_component *codecs;
	struct snd_soc_dai_link_component cpu_dai_component;
	struct snd_soc_component *component;
	struct snd_soc_dai **codec_dais;
	int i;

	if (dai_link->ignore)
		return 0;

	dev_dbg(card->dev, "ASoC: binding %s\n", dai_link->name);

	if (soc_is_dai_link_bound(card, dai_link)) {
		dev_dbg(card->dev, "ASoC: dai link %s already bound\n",
			dai_link->name);
		return 0;
	}

	rtd = soc_new_pcm_runtime(card, dai_link);
	if (!rtd)
		return -ENOMEM;

	cpu_dai_component.name = dai_link->cpu_name;
	cpu_dai_component.of_node = dai_link->cpu_of_node;
	cpu_dai_component.dai_name = dai_link->cpu_dai_name;
	rtd->cpu_dai = snd_soc_find_dai(&cpu_dai_component);
	if (!rtd->cpu_dai) {
		dev_info(card->dev, "ASoC: CPU DAI %s not registered\n",
			 dai_link->cpu_dai_name);
		goto _err_defer;
	}
	snd_soc_rtdcom_add(rtd, rtd->cpu_dai->component);

	rtd->num_codecs = dai_link->num_codecs;

	/* Find CODEC from registered CODECs */
	codec_dais = rtd->codec_dais;
	for_each_link_codecs(dai_link, i, codecs) {
		codec_dais[i] = snd_soc_find_dai(codecs);
		if (!codec_dais[i]) {
			dev_info(card->dev, "ASoC: CODEC DAI %s not registered\n",
				 codecs->dai_name);
			goto _err_defer;
		}
		snd_soc_rtdcom_add(rtd, codec_dais[i]->component);
	}

	/* Single codec links expect codec and codec_dai in runtime data */
	rtd->codec_dai = codec_dais[0];

	/* find one from the set of registered platforms */
	for_each_component(component) {
		if (!snd_soc_is_matching_component(dai_link->platforms,
						   component))
			continue;

		snd_soc_rtdcom_add(rtd, component);
	}

	soc_add_pcm_runtime(card, rtd);
	return 0;

_err_defer:
	soc_free_pcm_runtime(rtd);
	return -EPROBE_DEFER;
}

static void soc_cleanup_component(struct snd_soc_component *component)
{
	list_del(&component->card_list);
	snd_soc_dapm_free(snd_soc_component_get_dapm(component));
	soc_cleanup_component_debugfs(component);
	component->card = NULL;
	if (!component->driver->ignore_module_refcount)
		module_put(component->dev->driver->owner);
}

static void soc_remove_component(struct snd_soc_component *component)
{
	if (!component->card)
		return;

	if (component->driver->remove)
		component->driver->remove(component);

	soc_cleanup_component(component);
}

static void soc_remove_dai(struct snd_soc_dai *dai, int order)
{
	int err;

	if (!dai || !dai->probed || !dai->driver ||
	    dai->driver->remove_order != order)
		return;

	if (dai->driver->remove) {
		err = dai->driver->remove(dai);
		if (err < 0)
			dev_err(dai->dev,
				"ASoC: failed to remove %s: %d\n",
				dai->name, err);
	}
	dai->probed = 0;
}

static void soc_remove_link_dais(struct snd_soc_card *card,
		struct snd_soc_pcm_runtime *rtd, int order)
{
	int i;
	struct snd_soc_dai *codec_dai;

	/* unregister the rtd device */
	if (rtd->dev_registered) {
		device_unregister(rtd->dev);
		rtd->dev_registered = 0;
	}

	/* remove the CODEC DAI */
	for_each_rtd_codec_dai(rtd, i, codec_dai)
		soc_remove_dai(codec_dai, order);

	soc_remove_dai(rtd->cpu_dai, order);
}

static void soc_remove_link_components(struct snd_soc_card *card,
	struct snd_soc_pcm_runtime *rtd, int order)
{
	struct snd_soc_component *component;
	struct snd_soc_rtdcom_list *rtdcom;

	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		if (component->driver->remove_order == order)
			soc_remove_component(component);
	}
}

static void soc_remove_dai_links(struct snd_soc_card *card)
{
	int order;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai_link *link, *_link;

	for_each_comp_order(order) {
		for_each_card_rtds(card, rtd)
			soc_remove_link_dais(card, rtd, order);
	}

	for_each_comp_order(order) {
		for_each_card_rtds(card, rtd)
			soc_remove_link_components(card, rtd, order);
	}

	for_each_card_links_safe(card, link, _link) {
		if (link->dobj.type == SND_SOC_DOBJ_DAI_LINK)
			dev_warn(card->dev, "Topology forgot to remove link %s?\n",
				link->name);

		list_del(&link->list);
	}
}

static int snd_soc_init_platform(struct snd_soc_card *card,
				 struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_dai_link_component *platform = dai_link->platforms;

	/*
	 * REMOVE ME
	 *
	 * This is glue code for Legacy vs Modern dai_link.
	 * This function will be removed if all derivers are switched to
	 * modern style dai_link.
	 * Driver shouldn't use both legacy and modern style in the same time.
	 * see
	 *	soc.h :: struct snd_soc_dai_link
	 */
	/* convert Legacy platform link */
	if (!platform) {
		platform = devm_kzalloc(card->dev,
				sizeof(struct snd_soc_dai_link_component),
				GFP_KERNEL);
		if (!platform)
			return -ENOMEM;

		dai_link->platforms	  = platform;
		dai_link->num_platforms	  = 1;
		dai_link->legacy_platform = 1;
		platform->name		  = dai_link->platform_name;
		platform->of_node	  = dai_link->platform_of_node;
		platform->dai_name	  = NULL;
	}

	/* if there's no platform we match on the empty platform */
	if (!platform->name &&
	    !platform->of_node)
		platform->name = "snd-soc-dummy";

	return 0;
}

static void soc_cleanup_platform(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;
	/*
	 * FIXME
	 *
	 * this function should be removed with snd_soc_init_platform
	 */

	for_each_card_prelinks(card, i, link) {
		if (link->legacy_platform) {
			link->legacy_platform = 0;
			link->platforms       = NULL;
		}
	}
}

static int snd_soc_init_multicodec(struct snd_soc_card *card,
				   struct snd_soc_dai_link *dai_link)
{
	/*
	 * REMOVE ME
	 *
	 * This is glue code for Legacy vs Modern dai_link.
	 * This function will be removed if all derivers are switched to
	 * modern style dai_link.
	 * Driver shouldn't use both legacy and modern style in the same time.
	 * see
	 *	soc.h :: struct snd_soc_dai_link
	 */

	/* Legacy codec/codec_dai link is a single entry in multicodec */
	if (dai_link->codec_name || dai_link->codec_of_node ||
	    dai_link->codec_dai_name) {
		dai_link->num_codecs = 1;

		dai_link->codecs = devm_kzalloc(card->dev,
				sizeof(struct snd_soc_dai_link_component),
				GFP_KERNEL);
		if (!dai_link->codecs)
			return -ENOMEM;

		dai_link->codecs[0].name = dai_link->codec_name;
		dai_link->codecs[0].of_node = dai_link->codec_of_node;
		dai_link->codecs[0].dai_name = dai_link->codec_dai_name;
	}

	if (!dai_link->codecs) {
		dev_err(card->dev, "ASoC: DAI link has no CODECs\n");
		return -EINVAL;
	}

	return 0;
}

static int soc_init_dai_link(struct snd_soc_card *card,
			     struct snd_soc_dai_link *link)
{
	int i, ret;
	struct snd_soc_dai_link_component *codec;

	ret = snd_soc_init_platform(card, link);
	if (ret) {
		dev_err(card->dev, "ASoC: failed to init multiplatform\n");
		return ret;
	}

	ret = snd_soc_init_multicodec(card, link);
	if (ret) {
		dev_err(card->dev, "ASoC: failed to init multicodec\n");
		return ret;
	}

	for_each_link_codecs(link, i, codec) {
		/*
		 * Codec must be specified by 1 of name or OF node,
		 * not both or neither.
		 */
		if (!!codec->name ==
		    !!codec->of_node) {
			dev_err(card->dev, "ASoC: Neither/both codec name/of_node are set for %s\n",
				link->name);
			return -EINVAL;
		}
		/* Codec DAI name must be specified */
		if (!codec->dai_name) {
			dev_err(card->dev, "ASoC: codec_dai_name not set for %s\n",
				link->name);
			return -EINVAL;
		}
	}

	/* FIXME */
	if (link->num_platforms > 1) {
		dev_err(card->dev,
			"ASoC: multi platform is not yet supported %s\n",
			link->name);
		return -EINVAL;
	}

	/*
	 * Platform may be specified by either name or OF node, but
	 * can be left unspecified, and a dummy platform will be used.
	 */
	if (link->platforms->name && link->platforms->of_node) {
		dev_err(card->dev,
			"ASoC: Both platform name/of_node are set for %s\n",
			link->name);
		return -EINVAL;
	}

	/*
	 * Defer card registartion if platform dai component is not added to
	 * component list.
	 */
	if ((link->platforms->of_node || link->platforms->name) &&
	    !soc_find_component(link->platforms->of_node, link->platforms->name))
		return -EPROBE_DEFER;

	/*
	 * CPU device may be specified by either name or OF node, but
	 * can be left unspecified, and will be matched based on DAI
	 * name alone..
	 */
	if (link->cpu_name && link->cpu_of_node) {
		dev_err(card->dev,
			"ASoC: Neither/both cpu name/of_node are set for %s\n",
			link->name);
		return -EINVAL;
	}

	/*
	 * Defer card registartion if cpu dai component is not added to
	 * component list.
	 */
	if ((link->cpu_of_node || link->cpu_name) &&
	    !soc_find_component(link->cpu_of_node, link->cpu_name))
		return -EPROBE_DEFER;

	/*
	 * At least one of CPU DAI name or CPU device name/node must be
	 * specified
	 */
	if (!link->cpu_dai_name &&
	    !(link->cpu_name || link->cpu_of_node)) {
		dev_err(card->dev,
			"ASoC: Neither cpu_dai_name nor cpu_name/of_node are set for %s\n",
			link->name);
		return -EINVAL;
	}

	return 0;
}

void snd_soc_disconnect_sync(struct device *dev)
{
	struct snd_soc_component *component =
			snd_soc_lookup_component(dev, NULL);

	if (!component || !component->card)
		return;

	snd_card_disconnect_sync(component->card->snd_card);
}
EXPORT_SYMBOL_GPL(snd_soc_disconnect_sync);

/**
 * snd_soc_add_dai_link - Add a DAI link dynamically
 * @card: The ASoC card to which the DAI link is added
 * @dai_link: The new DAI link to add
 *
 * This function adds a DAI link to the ASoC card's link list.
 *
 * Note: Topology can use this API to add DAI links when probing the
 * topology component. And machine drivers can still define static
 * DAI links in dai_link array.
 */
int snd_soc_add_dai_link(struct snd_soc_card *card,
		struct snd_soc_dai_link *dai_link)
{
	if (dai_link->dobj.type
	    && dai_link->dobj.type != SND_SOC_DOBJ_DAI_LINK) {
		dev_err(card->dev, "Invalid dai link type %d\n",
			dai_link->dobj.type);
		return -EINVAL;
	}

	lockdep_assert_held(&client_mutex);
	/*
	 * Notify the machine driver for extra initialization
	 * on the link created by topology.
	 */
	if (dai_link->dobj.type && card->add_dai_link)
		card->add_dai_link(card, dai_link);

	list_add_tail(&dai_link->list, &card->dai_link_list);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_add_dai_link);

/**
 * snd_soc_remove_dai_link - Remove a DAI link from the list
 * @card: The ASoC card that owns the link
 * @dai_link: The DAI link to remove
 *
 * This function removes a DAI link from the ASoC card's link list.
 *
 * For DAI links previously added by topology, topology should
 * remove them by using the dobj embedded in the link.
 */
void snd_soc_remove_dai_link(struct snd_soc_card *card,
			     struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_dai_link *link, *_link;

	if (dai_link->dobj.type
	    && dai_link->dobj.type != SND_SOC_DOBJ_DAI_LINK) {
		dev_err(card->dev, "Invalid dai link type %d\n",
			dai_link->dobj.type);
		return;
	}

	lockdep_assert_held(&client_mutex);
	/*
	 * Notify the machine driver for extra destruction
	 * on the link created by topology.
	 */
	if (dai_link->dobj.type && card->remove_dai_link)
		card->remove_dai_link(card, dai_link);

	for_each_card_links_safe(card, link, _link) {
		if (link == dai_link) {
			list_del(&link->list);
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(snd_soc_remove_dai_link);

static void soc_set_of_name_prefix(struct snd_soc_component *component)
{
	struct device_node *component_of_node = component->dev->of_node;
	const char *str;
	int ret;

	if (!component_of_node && component->dev->parent)
		component_of_node = component->dev->parent->of_node;

	ret = of_property_read_string(component_of_node, "sound-name-prefix",
				      &str);
	if (!ret)
		component->name_prefix = str;
}

static void soc_set_name_prefix(struct snd_soc_card *card,
				struct snd_soc_component *component)
{
	int i;

	for (i = 0; i < card->num_configs && card->codec_conf; i++) {
		struct snd_soc_codec_conf *map = &card->codec_conf[i];
		struct device_node *component_of_node = component->dev->of_node;

		if (!component_of_node && component->dev->parent)
			component_of_node = component->dev->parent->of_node;

		if (map->of_node && component_of_node != map->of_node)
			continue;
		if (map->dev_name && strcmp(component->name, map->dev_name))
			continue;
		component->name_prefix = map->name_prefix;
		return;
	}

	/*
	 * If there is no configuration table or no match in the table,
	 * check if a prefix is provided in the node
	 */
	soc_set_of_name_prefix(component);
}

static int soc_probe_component(struct snd_soc_card *card,
	struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm =
			snd_soc_component_get_dapm(component);
	struct snd_soc_dai *dai;
	int ret;

	if (!strcmp(component->name, "snd-soc-dummy"))
		return 0;

	if (component->card) {
		if (component->card != card) {
			dev_err(component->dev,
				"Trying to bind component to card \"%s\" but is already bound to card \"%s\"\n",
				card->name, component->card->name);
			return -ENODEV;
		}
		return 0;
	}

	if (!component->driver->ignore_module_refcount &&
	    !try_module_get(component->dev->driver->owner))
		return -ENODEV;

	component->card = card;
	dapm->card = card;
	INIT_LIST_HEAD(&component->card_list);
	INIT_LIST_HEAD(&dapm->list);
	soc_set_name_prefix(card, component);

	soc_init_component_debugfs(component);

	if (component->driver->dapm_widgets) {
		ret = snd_soc_dapm_new_controls(dapm,
					component->driver->dapm_widgets,
					component->driver->num_dapm_widgets);

		if (ret != 0) {
			dev_err(component->dev,
				"Failed to create new controls %d\n", ret);
			goto err_probe;
		}
	}

	for_each_component_dais(component, dai) {
		ret = snd_soc_dapm_new_dai_widgets(dapm, dai);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to create DAI widgets %d\n", ret);
			goto err_probe;
		}
	}

	if (component->driver->probe) {
		ret = component->driver->probe(component);
		if (ret < 0) {
			dev_err(component->dev,
				"ASoC: failed to probe component %d\n", ret);
			goto err_probe;
		}

		WARN(dapm->idle_bias_off &&
			dapm->bias_level != SND_SOC_BIAS_OFF,
			"codec %s can not start from non-off bias with idle_bias_off==1\n",
			component->name);
	}

	/* machine specific init */
	if (component->init) {
		ret = component->init(component);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to do machine specific init %d\n", ret);
			goto err_probe;
		}
	}

	if (component->driver->controls)
		snd_soc_add_component_controls(component,
					       component->driver->controls,
					       component->driver->num_controls);
	if (component->driver->dapm_routes)
		snd_soc_dapm_add_routes(dapm,
					component->driver->dapm_routes,
					component->driver->num_dapm_routes);

	list_add(&dapm->list, &card->dapm_list);
	/* see for_each_card_components */
	list_add(&component->card_list, &card->component_dev_list);

err_probe:
	if (ret < 0)
		soc_cleanup_component(component);

	return ret;
}

static void rtd_release(struct device *dev)
{
	kfree(dev);
}

static int soc_post_component_init(struct snd_soc_pcm_runtime *rtd,
	const char *name)
{
	int ret = 0;

	/* register the rtd device */
	rtd->dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!rtd->dev)
		return -ENOMEM;
	device_initialize(rtd->dev);
	rtd->dev->parent = rtd->card->dev;
	rtd->dev->release = rtd_release;
	rtd->dev->groups = soc_dev_attr_groups;
	dev_set_name(rtd->dev, "%s", name);
	dev_set_drvdata(rtd->dev, rtd);
	mutex_init(&rtd->pcm_mutex);
	INIT_LIST_HEAD(&rtd->dpcm[SNDRV_PCM_STREAM_PLAYBACK].be_clients);
	INIT_LIST_HEAD(&rtd->dpcm[SNDRV_PCM_STREAM_CAPTURE].be_clients);
	INIT_LIST_HEAD(&rtd->dpcm[SNDRV_PCM_STREAM_PLAYBACK].fe_clients);
	INIT_LIST_HEAD(&rtd->dpcm[SNDRV_PCM_STREAM_CAPTURE].fe_clients);
	ret = device_add(rtd->dev);
	if (ret < 0) {
		/* calling put_device() here to free the rtd->dev */
		put_device(rtd->dev);
		dev_err(rtd->card->dev,
			"ASoC: failed to register runtime device: %d\n", ret);
		return ret;
	}
	rtd->dev_registered = 1;
	return 0;
}

static int soc_probe_link_components(struct snd_soc_card *card,
				     struct snd_soc_pcm_runtime *rtd, int order)
{
	struct snd_soc_component *component;
	struct snd_soc_rtdcom_list *rtdcom;
	int ret;

	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		if (component->driver->probe_order == order) {
			ret = soc_probe_component(card, component);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int soc_probe_dai(struct snd_soc_dai *dai, int order)
{
	if (dai->probed ||
	    dai->driver->probe_order != order)
		return 0;

	if (dai->driver->probe) {
		int ret = dai->driver->probe(dai);

		if (ret < 0) {
			dev_err(dai->dev, "ASoC: failed to probe DAI %s: %d\n",
				dai->name, ret);
			return ret;
		}
	}

	dai->probed = 1;

	return 0;
}

static int soc_link_dai_pcm_new(struct snd_soc_dai **dais, int num_dais,
				struct snd_soc_pcm_runtime *rtd)
{
	int i, ret = 0;

	for (i = 0; i < num_dais; ++i) {
		struct snd_soc_dai_driver *drv = dais[i]->driver;

		if (drv->pcm_new)
			ret = drv->pcm_new(rtd, dais[i]);
		if (ret < 0) {
			dev_err(dais[i]->dev,
				"ASoC: Failed to bind %s with pcm device\n",
				dais[i]->name);
			return ret;
		}
	}

	return 0;
}

static int soc_probe_link_dais(struct snd_soc_card *card,
		struct snd_soc_pcm_runtime *rtd, int order)
{
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_rtdcom_list *rtdcom;
	struct snd_soc_component *component;
	struct snd_soc_dai *codec_dai;
	int i, ret, num;

	dev_dbg(card->dev, "ASoC: probe %s dai link %d late %d\n",
			card->name, rtd->num, order);

	/* set default power off timeout */
	rtd->pmdown_time = pmdown_time;

	ret = soc_probe_dai(cpu_dai, order);
	if (ret)
		return ret;

	/* probe the CODEC DAI */
	for_each_rtd_codec_dai(rtd, i, codec_dai) {
		ret = soc_probe_dai(codec_dai, order);
		if (ret)
			return ret;
	}

	/* complete DAI probe during last probe */
	if (order != SND_SOC_COMP_ORDER_LAST)
		return 0;

	/* do machine specific initialization */
	if (dai_link->init) {
		ret = dai_link->init(rtd);
		if (ret < 0) {
			dev_err(card->dev, "ASoC: failed to init %s: %d\n",
				dai_link->name, ret);
			return ret;
		}
	}

	if (dai_link->dai_fmt)
		snd_soc_runtime_set_dai_fmt(rtd, dai_link->dai_fmt);

	ret = soc_post_component_init(rtd, dai_link->name);
	if (ret)
		return ret;

#ifdef CONFIG_DEBUG_FS
	/* add DPCM sysfs entries */
	if (dai_link->dynamic)
		soc_dpcm_debugfs_add(rtd);
#endif

	num = rtd->num;

	/*
	 * most drivers will register their PCMs using DAI link ordering but
	 * topology based drivers can use the DAI link id field to set PCM
	 * device number and then use rtd + a base offset of the BEs.
	 */
	for_each_rtdcom(rtd, rtdcom) {
		component = rtdcom->component;

		if (!component->driver->use_dai_pcm_id)
			continue;

		if (rtd->dai_link->no_pcm)
			num += component->driver->be_pcm_base;
		else
			num = rtd->dai_link->id;
	}

	if (cpu_dai->driver->compress_new) {
		/* create compress_device" */
		ret = cpu_dai->driver->compress_new(rtd, num);
		if (ret < 0) {
			dev_err(card->dev, "ASoC: can't create compress %s\n",
					 dai_link->stream_name);
			return ret;
		}
	} else if (!dai_link->params) {
		/* create the pcm */
		ret = soc_new_pcm(rtd, num);
		if (ret < 0) {
			dev_err(card->dev, "ASoC: can't create pcm %s :%d\n",
				dai_link->stream_name, ret);
			return ret;
		}
		ret = soc_link_dai_pcm_new(&cpu_dai, 1, rtd);
		if (ret < 0)
			return ret;
		ret = soc_link_dai_pcm_new(rtd->codec_dais,
					   rtd->num_codecs, rtd);
		if (ret < 0)
			return ret;
	} else {
		INIT_DELAYED_WORK(&rtd->delayed_work,
				  codec2codec_close_delayed_work);
	}

	return 0;
}

static int soc_bind_aux_dev(struct snd_soc_card *card, int num)
{
	struct snd_soc_aux_dev *aux_dev = &card->aux_dev[num];
	struct snd_soc_component *component;
	const char *name;
	struct device_node *codec_of_node;

	if (aux_dev->codec_of_node || aux_dev->codec_name) {
		/* codecs, usually analog devices */
		name = aux_dev->codec_name;
		codec_of_node = aux_dev->codec_of_node;
		component = soc_find_component(codec_of_node, name);
		if (!component) {
			if (codec_of_node)
				name = of_node_full_name(codec_of_node);
			goto err_defer;
		}
	} else if (aux_dev->name) {
		/* generic components */
		name = aux_dev->name;
		component = soc_find_component(NULL, name);
		if (!component)
			goto err_defer;
	} else {
		dev_err(card->dev, "ASoC: Invalid auxiliary device\n");
		return -EINVAL;
	}

	component->init = aux_dev->init;
	list_add(&component->card_aux_list, &card->aux_comp_list);

	return 0;

err_defer:
	dev_err(card->dev, "ASoC: %s not registered\n", name);
	return -EPROBE_DEFER;
}

static int soc_probe_aux_devices(struct snd_soc_card *card)
{
	struct snd_soc_component *comp;
	int order;
	int ret;

	for_each_comp_order(order) {
		list_for_each_entry(comp, &card->aux_comp_list, card_aux_list) {
			if (comp->driver->probe_order == order) {
				ret = soc_probe_component(card,	comp);
				if (ret < 0) {
					dev_err(card->dev,
						"ASoC: failed to probe aux component %s %d\n",
						comp->name, ret);
					return ret;
				}
			}
		}
	}

	return 0;
}

static void soc_remove_aux_devices(struct snd_soc_card *card)
{
	struct snd_soc_component *comp, *_comp;
	int order;

	for_each_comp_order(order) {
		list_for_each_entry_safe(comp, _comp,
			&card->aux_comp_list, card_aux_list) {

			if (comp->driver->remove_order == order) {
				soc_remove_component(comp);
				/* remove it from the card's aux_comp_list */
				list_del(&comp->card_aux_list);
			}
		}
	}
}

/**
 * snd_soc_runtime_set_dai_fmt() - Change DAI link format for a ASoC runtime
 * @rtd: The runtime for which the DAI link format should be changed
 * @dai_fmt: The new DAI link format
 *
 * This function updates the DAI link format for all DAIs connected to the DAI
 * link for the specified runtime.
 *
 * Note: For setups with a static format set the dai_fmt field in the
 * corresponding snd_dai_link struct instead of using this function.
 *
 * Returns 0 on success, otherwise a negative error code.
 */
int snd_soc_runtime_set_dai_fmt(struct snd_soc_pcm_runtime *rtd,
	unsigned int dai_fmt)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai;
	unsigned int i;
	int ret;

	for_each_rtd_codec_dai(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
		if (ret != 0 && ret != -ENOTSUPP) {
			dev_warn(codec_dai->dev,
				 "ASoC: Failed to set DAI format: %d\n", ret);
			return ret;
		}
	}

	/*
	 * Flip the polarity for the "CPU" end of a CODEC<->CODEC link
	 * the component which has non_legacy_dai_naming is Codec
	 */
	if (cpu_dai->component->driver->non_legacy_dai_naming) {
		unsigned int inv_dai_fmt;

		inv_dai_fmt = dai_fmt & ~SND_SOC_DAIFMT_MASTER_MASK;
		switch (dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBM_CFM:
			inv_dai_fmt |= SND_SOC_DAIFMT_CBS_CFS;
			break;
		case SND_SOC_DAIFMT_CBM_CFS:
			inv_dai_fmt |= SND_SOC_DAIFMT_CBS_CFM;
			break;
		case SND_SOC_DAIFMT_CBS_CFM:
			inv_dai_fmt |= SND_SOC_DAIFMT_CBM_CFS;
			break;
		case SND_SOC_DAIFMT_CBS_CFS:
			inv_dai_fmt |= SND_SOC_DAIFMT_CBM_CFM;
			break;
		}

		dai_fmt = inv_dai_fmt;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret != 0 && ret != -ENOTSUPP) {
		dev_warn(cpu_dai->dev,
			 "ASoC: Failed to set DAI format: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_runtime_set_dai_fmt);

#ifdef CONFIG_DMI
/*
 * Trim special characters, and replace '-' with '_' since '-' is used to
 * separate different DMI fields in the card long name. Only number and
 * alphabet characters and a few separator characters are kept.
 */
static void cleanup_dmi_name(char *name)
{
	int i, j = 0;

	for (i = 0; name[i]; i++) {
		if (isalnum(name[i]) || (name[i] == '.')
		    || (name[i] == '_'))
			name[j++] = name[i];
		else if (name[i] == '-')
			name[j++] = '_';
	}

	name[j] = '\0';
}

/*
 * Check if a DMI field is valid, i.e. not containing any string
 * in the black list.
 */
static int is_dmi_valid(const char *field)
{
	int i = 0;

	while (dmi_blacklist[i]) {
		if (strstr(field, dmi_blacklist[i]))
			return 0;
		i++;
	}

	return 1;
}

/**
 * snd_soc_set_dmi_name() - Register DMI names to card
 * @card: The card to register DMI names
 * @flavour: The flavour "differentiator" for the card amongst its peers.
 *
 * An Intel machine driver may be used by many different devices but are
 * difficult for userspace to differentiate, since machine drivers ususally
 * use their own name as the card short name and leave the card long name
 * blank. To differentiate such devices and fix bugs due to lack of
 * device-specific configurations, this function allows DMI info to be used
 * as the sound card long name, in the format of
 * "vendor-product-version-board"
 * (Character '-' is used to separate different DMI fields here).
 * This will help the user space to load the device-specific Use Case Manager
 * (UCM) configurations for the card.
 *
 * Possible card long names may be:
 * DellInc.-XPS139343-01-0310JH
 * ASUSTeKCOMPUTERINC.-T100TA-1.0-T100TA
 * Circuitco-MinnowboardMaxD0PLATFORM-D0-MinnowBoardMAX
 *
 * This function also supports flavoring the card longname to provide
 * the extra differentiation, like "vendor-product-version-board-flavor".
 *
 * We only keep number and alphabet characters and a few separator characters
 * in the card long name since UCM in the user space uses the card long names
 * as card configuration directory names and AudoConf cannot support special
 * charactors like SPACE.
 *
 * Returns 0 on success, otherwise a negative error code.
 */
int snd_soc_set_dmi_name(struct snd_soc_card *card, const char *flavour)
{
	const char *vendor, *product, *product_version, *board;
	size_t longname_buf_size = sizeof(card->snd_card->longname);
	size_t len;

	if (card->long_name)
		return 0; /* long name already set by driver or from DMI */

	/* make up dmi long name as: vendor.product.version.board */
	vendor = dmi_get_system_info(DMI_BOARD_VENDOR);
	if (!vendor || !is_dmi_valid(vendor)) {
		dev_warn(card->dev, "ASoC: no DMI vendor name!\n");
		return 0;
	}

	snprintf(card->dmi_longname, sizeof(card->snd_card->longname),
			 "%s", vendor);
	cleanup_dmi_name(card->dmi_longname);

	product = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (product && is_dmi_valid(product)) {
		len = strlen(card->dmi_longname);
		snprintf(card->dmi_longname + len,
			 longname_buf_size - len,
			 "-%s", product);

		len++;	/* skip the separator "-" */
		if (len < longname_buf_size)
			cleanup_dmi_name(card->dmi_longname + len);

		/*
		 * some vendors like Lenovo may only put a self-explanatory
		 * name in the product version field
		 */
		product_version = dmi_get_system_info(DMI_PRODUCT_VERSION);
		if (product_version && is_dmi_valid(product_version)) {
			len = strlen(card->dmi_longname);
			snprintf(card->dmi_longname + len,
				 longname_buf_size - len,
				 "-%s", product_version);

			len++;
			if (len < longname_buf_size)
				cleanup_dmi_name(card->dmi_longname + len);
		}
	}

	board = dmi_get_system_info(DMI_BOARD_NAME);
	if (board && is_dmi_valid(board)) {
		len = strlen(card->dmi_longname);
		snprintf(card->dmi_longname + len,
			 longname_buf_size - len,
			 "-%s", board);

		len++;
		if (len < longname_buf_size)
			cleanup_dmi_name(card->dmi_longname + len);
	} else if (!product) {
		/* fall back to using legacy name */
		dev_warn(card->dev, "ASoC: no DMI board/product name!\n");
		return 0;
	}

	/* Add flavour to dmi long name */
	if (flavour) {
		len = strlen(card->dmi_longname);
		snprintf(card->dmi_longname + len,
			 longname_buf_size - len,
			 "-%s", flavour);

		len++;
		if (len < longname_buf_size)
			cleanup_dmi_name(card->dmi_longname + len);
	}

	/* set the card long name */
	card->long_name = card->dmi_longname;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_set_dmi_name);
#endif /* CONFIG_DMI */

static void soc_check_tplg_fes(struct snd_soc_card *card)
{
	struct snd_soc_component *component;
	const struct snd_soc_component_driver *comp_drv;
	struct snd_soc_dai_link *dai_link;
	int i;

	for_each_component(component) {

		/* does this component override FEs ? */
		if (!component->driver->ignore_machine)
			continue;

		/* for this machine ? */
		if (strcmp(component->driver->ignore_machine,
			   card->dev->driver->name))
			continue;

		/* machine matches, so override the rtd data */
		for_each_card_prelinks(card, i, dai_link) {

			/* ignore this FE */
			if (dai_link->dynamic) {
				dai_link->ignore = true;
				continue;
			}

			dev_info(card->dev, "info: override FE DAI link %s\n",
				 card->dai_link[i].name);

			/* override platform component */
			if (snd_soc_init_platform(card, dai_link) < 0) {
				dev_err(card->dev, "init platform error");
				continue;
			}
			dai_link->platforms->name = component->name;

			/* convert non BE into BE */
			dai_link->no_pcm = 1;

			/* override any BE fixups */
			dai_link->be_hw_params_fixup =
				component->driver->be_hw_params_fixup;

			/*
			 * most BE links don't set stream name, so set it to
			 * dai link name if it's NULL to help bind widgets.
			 */
			if (!dai_link->stream_name)
				dai_link->stream_name = dai_link->name;
		}

		/* Inform userspace we are using alternate topology */
		if (component->driver->topology_name_prefix) {

			/* topology shortname created? */
			if (!card->topology_shortname_created) {
				comp_drv = component->driver;

				snprintf(card->topology_shortname, 32, "%s-%s",
					 comp_drv->topology_name_prefix,
					 card->name);
				card->topology_shortname_created = true;
			}

			/* use topology shortname */
			card->name = card->topology_shortname;
		}
	}
}

static int soc_cleanup_card_resources(struct snd_soc_card *card)
{
	/* free the ALSA card at first; this syncs with pending operations */
	if (card->snd_card)
		snd_card_free(card->snd_card);

	/* remove and free each DAI */
	soc_remove_dai_links(card);
	soc_remove_pcm_runtimes(card);
	soc_cleanup_platform(card);

	/* remove auxiliary devices */
	soc_remove_aux_devices(card);

	snd_soc_dapm_free(&card->dapm);
	soc_cleanup_card_debugfs(card);

	/* remove the card */
	if (card->remove)
		card->remove(card);

	return 0;
}

static int snd_soc_instantiate_card(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai_link *dai_link;
	int ret, i, order;

	mutex_lock(&client_mutex);
	mutex_lock_nested(&card->mutex, SND_SOC_CARD_CLASS_INIT);

	card->dapm.bias_level = SND_SOC_BIAS_OFF;
	card->dapm.dev = card->dev;
	card->dapm.card = card;
	list_add(&card->dapm.list, &card->dapm_list);

	/* check whether any platform is ignore machine FE and using topology */
	soc_check_tplg_fes(card);

	/* bind DAIs */
	for_each_card_prelinks(card, i, dai_link) {
		ret = soc_bind_dai_link(card, dai_link);
		if (ret != 0)
			goto probe_end;
	}

	/* bind aux_devs too */
	for (i = 0; i < card->num_aux_devs; i++) {
		ret = soc_bind_aux_dev(card, i);
		if (ret != 0)
			goto probe_end;
	}

	/* add predefined DAI links to the list */
	for_each_card_prelinks(card, i, dai_link)
		snd_soc_add_dai_link(card, dai_link);

	/* card bind complete so register a sound card */
	ret = snd_card_new(card->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			card->owner, 0, &card->snd_card);
	if (ret < 0) {
		dev_err(card->dev,
			"ASoC: can't create sound card for card %s: %d\n",
			card->name, ret);
		goto probe_end;
	}

	soc_init_card_debugfs(card);

#ifdef CONFIG_DEBUG_FS
	snd_soc_dapm_debugfs_init(&card->dapm, card->debugfs_card_root);
#endif

#ifdef CONFIG_PM_SLEEP
	/* deferred resume work */
	INIT_WORK(&card->deferred_resume_work, soc_resume_deferred);
#endif

	if (card->dapm_widgets)
		snd_soc_dapm_new_controls(&card->dapm, card->dapm_widgets,
					  card->num_dapm_widgets);

	if (card->of_dapm_widgets)
		snd_soc_dapm_new_controls(&card->dapm, card->of_dapm_widgets,
					  card->num_of_dapm_widgets);

	/* initialise the sound card only once */
	if (card->probe) {
		ret = card->probe(card);
		if (ret < 0)
			goto probe_end;
	}

	/* probe all components used by DAI links on this card */
	for_each_comp_order(order) {
		for_each_card_rtds(card, rtd) {
			ret = soc_probe_link_components(card, rtd, order);
			if (ret < 0) {
				dev_err(card->dev,
					"ASoC: failed to instantiate card %d\n",
					ret);
				goto probe_end;
			}
		}
	}

	/* probe auxiliary components */
	ret = soc_probe_aux_devices(card);
	if (ret < 0)
		goto probe_end;

	/*
	 * Find new DAI links added during probing components and bind them.
	 * Components with topology may bring new DAIs and DAI links.
	 */
	for_each_card_links(card, dai_link) {
		if (soc_is_dai_link_bound(card, dai_link))
			continue;

		ret = soc_init_dai_link(card, dai_link);
		if (ret)
			goto probe_end;
		ret = soc_bind_dai_link(card, dai_link);
		if (ret)
			goto probe_end;
	}

	/* probe all DAI links on this card */
	for_each_comp_order(order) {
		for_each_card_rtds(card, rtd) {
			ret = soc_probe_link_dais(card, rtd, order);
			if (ret < 0) {
				dev_err(card->dev,
					"ASoC: failed to instantiate card %d\n",
					ret);
				goto probe_end;
			}
		}
	}

	snd_soc_dapm_link_dai_widgets(card);
	snd_soc_dapm_connect_dai_link_widgets(card);

	if (card->controls)
		snd_soc_add_card_controls(card, card->controls,
					  card->num_controls);

	if (card->dapm_routes)
		snd_soc_dapm_add_routes(&card->dapm, card->dapm_routes,
					card->num_dapm_routes);

	if (card->of_dapm_routes)
		snd_soc_dapm_add_routes(&card->dapm, card->of_dapm_routes,
					card->num_of_dapm_routes);

	/* try to set some sane longname if DMI is available */
	snd_soc_set_dmi_name(card, NULL);

	snprintf(card->snd_card->shortname, sizeof(card->snd_card->shortname),
		 "%s", card->name);
	snprintf(card->snd_card->longname, sizeof(card->snd_card->longname),
		 "%s", card->long_name ? card->long_name : card->name);
	snprintf(card->snd_card->driver, sizeof(card->snd_card->driver),
		 "%s", card->driver_name ? card->driver_name : card->name);
	for (i = 0; i < ARRAY_SIZE(card->snd_card->driver); i++) {
		switch (card->snd_card->driver[i]) {
		case '_':
		case '-':
		case '\0':
			break;
		default:
			if (!isalnum(card->snd_card->driver[i]))
				card->snd_card->driver[i] = '_';
			break;
		}
	}

	if (card->late_probe) {
		ret = card->late_probe(card);
		if (ret < 0) {
			dev_err(card->dev, "ASoC: %s late_probe() failed: %d\n",
				card->name, ret);
			goto probe_end;
		}
	}

	snd_soc_dapm_new_widgets(card);

	ret = snd_card_register(card->snd_card);
	if (ret < 0) {
		dev_err(card->dev, "ASoC: failed to register soundcard %d\n",
				ret);
		goto probe_end;
	}

	card->instantiated = 1;
	dapm_mark_endpoints_dirty(card);
	snd_soc_dapm_sync(&card->dapm);

probe_end:
	if (ret < 0)
		soc_cleanup_card_resources(card);

	mutex_unlock(&card->mutex);
	mutex_unlock(&client_mutex);

	return ret;
}

/* probes a new socdev */
static int soc_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	/*
	 * no card, so machine driver should be registering card
	 * we should not be here in that case so ret error
	 */
	if (!card)
		return -EINVAL;

	dev_warn(&pdev->dev,
		 "ASoC: machine %s should use snd_soc_register_card()\n",
		 card->name);

	/* Bodge while we unpick instantiation */
	card->dev = &pdev->dev;

	return snd_soc_register_card(card);
}

/* removes a socdev */
static int soc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	return 0;
}

int snd_soc_poweroff(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct snd_soc_pcm_runtime *rtd;

	if (!card->instantiated)
		return 0;

	/*
	 * Flush out pmdown_time work - we actually do want to run it
	 * now, we're shutting down so no imminent restart.
	 */
	snd_soc_flush_all_delayed_work(card);

	snd_soc_dapm_shutdown(card);

	/* deactivate pins to sleep state */
	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
		struct snd_soc_dai *codec_dai;
		int i;

		pinctrl_pm_select_sleep_state(cpu_dai->dev);
		for_each_rtd_codec_dai(rtd, i, codec_dai) {
			pinctrl_pm_select_sleep_state(codec_dai->dev);
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_poweroff);

const struct dev_pm_ops snd_soc_pm_ops = {
	.suspend = snd_soc_suspend,
	.resume = snd_soc_resume,
	.freeze = snd_soc_suspend,
	.thaw = snd_soc_resume,
	.poweroff = snd_soc_poweroff,
	.restore = snd_soc_resume,
};
EXPORT_SYMBOL_GPL(snd_soc_pm_ops);

/* ASoC platform driver */
static struct platform_driver soc_driver = {
	.driver		= {
		.name		= "soc-audio",
		.pm		= &snd_soc_pm_ops,
	},
	.probe		= soc_probe,
	.remove		= soc_remove,
};

/**
 * snd_soc_cnew - create new control
 * @_template: control template
 * @data: control private data
 * @long_name: control long name
 * @prefix: control name prefix
 *
 * Create a new mixer control from a template control.
 *
 * Returns 0 for success, else error.
 */
struct snd_kcontrol *snd_soc_cnew(const struct snd_kcontrol_new *_template,
				  void *data, const char *long_name,
				  const char *prefix)
{
	struct snd_kcontrol_new template;
	struct snd_kcontrol *kcontrol;
	char *name = NULL;

	memcpy(&template, _template, sizeof(template));
	template.index = 0;

	if (!long_name)
		long_name = template.name;

	if (prefix) {
		name = kasprintf(GFP_KERNEL, "%s %s", prefix, long_name);
		if (!name)
			return NULL;

		template.name = name;
	} else {
		template.name = long_name;
	}

	kcontrol = snd_ctl_new1(&template, data);

	kfree(name);

	return kcontrol;
}
EXPORT_SYMBOL_GPL(snd_soc_cnew);

static int snd_soc_add_controls(struct snd_card *card, struct device *dev,
	const struct snd_kcontrol_new *controls, int num_controls,
	const char *prefix, void *data)
{
	int err, i;

	for (i = 0; i < num_controls; i++) {
		const struct snd_kcontrol_new *control = &controls[i];

		err = snd_ctl_add(card, snd_soc_cnew(control, data,
						     control->name, prefix));
		if (err < 0) {
			dev_err(dev, "ASoC: Failed to add %s: %d\n",
				control->name, err);
			return err;
		}
	}

	return 0;
}

struct snd_kcontrol *snd_soc_card_get_kcontrol(struct snd_soc_card *soc_card,
					       const char *name)
{
	struct snd_card *card = soc_card->snd_card;
	struct snd_kcontrol *kctl;

	if (unlikely(!name))
		return NULL;

	list_for_each_entry(kctl, &card->controls, list)
		if (!strncmp(kctl->id.name, name, sizeof(kctl->id.name)))
			return kctl;
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_card_get_kcontrol);

/**
 * snd_soc_add_component_controls - Add an array of controls to a component.
 *
 * @component: Component to add controls to
 * @controls: Array of controls to add
 * @num_controls: Number of elements in the array
 *
 * Return: 0 for success, else error.
 */
int snd_soc_add_component_controls(struct snd_soc_component *component,
	const struct snd_kcontrol_new *controls, unsigned int num_controls)
{
	struct snd_card *card = component->card->snd_card;

	return snd_soc_add_controls(card, component->dev, controls,
			num_controls, component->name_prefix, component);
}
EXPORT_SYMBOL_GPL(snd_soc_add_component_controls);

/**
 * snd_soc_add_card_controls - add an array of controls to a SoC card.
 * Convenience function to add a list of controls.
 *
 * @soc_card: SoC card to add controls to
 * @controls: array of controls to add
 * @num_controls: number of elements in the array
 *
 * Return 0 for success, else error.
 */
int snd_soc_add_card_controls(struct snd_soc_card *soc_card,
	const struct snd_kcontrol_new *controls, int num_controls)
{
	struct snd_card *card = soc_card->snd_card;

	return snd_soc_add_controls(card, soc_card->dev, controls, num_controls,
			NULL, soc_card);
}
EXPORT_SYMBOL_GPL(snd_soc_add_card_controls);

/**
 * snd_soc_add_dai_controls - add an array of controls to a DAI.
 * Convienience function to add a list of controls.
 *
 * @dai: DAI to add controls to
 * @controls: array of controls to add
 * @num_controls: number of elements in the array
 *
 * Return 0 for success, else error.
 */
int snd_soc_add_dai_controls(struct snd_soc_dai *dai,
	const struct snd_kcontrol_new *controls, int num_controls)
{
	struct snd_card *card = dai->component->card->snd_card;

	return snd_soc_add_controls(card, dai->dev, controls, num_controls,
			NULL, dai);
}
EXPORT_SYMBOL_GPL(snd_soc_add_dai_controls);

/**
 * snd_soc_dai_set_sysclk - configure DAI system or master clock.
 * @dai: DAI
 * @clk_id: DAI specific clock ID
 * @freq: new clock frequency in Hz
 * @dir: new clock direction - input/output.
 *
 * Configures the DAI master (MCLK) or system (SYSCLK) clocking.
 */
int snd_soc_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
	unsigned int freq, int dir)
{
	if (dai->driver->ops->set_sysclk)
		return dai->driver->ops->set_sysclk(dai, clk_id, freq, dir);

	return snd_soc_component_set_sysclk(dai->component, clk_id, 0,
					    freq, dir);
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_sysclk);

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

/**
 * snd_soc_dai_set_clkdiv - configure DAI clock dividers.
 * @dai: DAI
 * @div_id: DAI specific clock divider ID
 * @div: new clock divisor.
 *
 * Configures the clock dividers. This is used to derive the best DAI bit and
 * frame clocks from the system or master clock. It's best to set the DAI bit
 * and frame clocks as low as possible to save system power.
 */
int snd_soc_dai_set_clkdiv(struct snd_soc_dai *dai,
	int div_id, int div)
{
	if (dai->driver->ops->set_clkdiv)
		return dai->driver->ops->set_clkdiv(dai, div_id, div);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_clkdiv);

/**
 * snd_soc_dai_set_pll - configure DAI PLL.
 * @dai: DAI
 * @pll_id: DAI specific PLL ID
 * @source: DAI specific source for the PLL
 * @freq_in: PLL input clock frequency in Hz
 * @freq_out: requested PLL output clock frequency in Hz
 *
 * Configures and enables PLL to generate output clock based on input clock.
 */
int snd_soc_dai_set_pll(struct snd_soc_dai *dai, int pll_id, int source,
	unsigned int freq_in, unsigned int freq_out)
{
	if (dai->driver->ops->set_pll)
		return dai->driver->ops->set_pll(dai, pll_id, source,
					 freq_in, freq_out);

	return snd_soc_component_set_pll(dai->component, pll_id, source,
					 freq_in, freq_out);
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_pll);

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

/**
 * snd_soc_dai_set_bclk_ratio - configure BCLK to sample rate ratio.
 * @dai: DAI
 * @ratio: Ratio of BCLK to Sample rate.
 *
 * Configures the DAI for a preset BCLK to sample rate ratio.
 */
int snd_soc_dai_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	if (dai->driver->ops->set_bclk_ratio)
		return dai->driver->ops->set_bclk_ratio(dai, ratio);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_bclk_ratio);

/**
 * snd_soc_dai_set_fmt - configure DAI hardware audio format.
 * @dai: DAI
 * @fmt: SND_SOC_DAIFMT_* format value.
 *
 * Configures the DAI hardware format and clocking.
 */
int snd_soc_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	if (dai->driver->ops->set_fmt == NULL)
		return -ENOTSUPP;
	return dai->driver->ops->set_fmt(dai, fmt);
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_fmt);

/**
 * snd_soc_xlate_tdm_slot - generate tx/rx slot mask.
 * @slots: Number of slots in use.
 * @tx_mask: bitmask representing active TX slots.
 * @rx_mask: bitmask representing active RX slots.
 *
 * Generates the TDM tx and rx slot default masks for DAI.
 */
static int snd_soc_xlate_tdm_slot_mask(unsigned int slots,
				       unsigned int *tx_mask,
				       unsigned int *rx_mask)
{
	if (*tx_mask || *rx_mask)
		return 0;

	if (!slots)
		return -EINVAL;

	*tx_mask = (1 << slots) - 1;
	*rx_mask = (1 << slots) - 1;

	return 0;
}

/**
 * snd_soc_dai_set_tdm_slot() - Configures a DAI for TDM operation
 * @dai: The DAI to configure
 * @tx_mask: bitmask representing active TX slots.
 * @rx_mask: bitmask representing active RX slots.
 * @slots: Number of slots in use.
 * @slot_width: Width in bits for each slot.
 *
 * This function configures the specified DAI for TDM operation. @slot contains
 * the total number of slots of the TDM stream and @slot_with the width of each
 * slot in bit clock cycles. @tx_mask and @rx_mask are bitmasks specifying the
 * active slots of the TDM stream for the specified DAI, i.e. which slots the
 * DAI should write to or read from. If a bit is set the corresponding slot is
 * active, if a bit is cleared the corresponding slot is inactive. Bit 0 maps to
 * the first slot, bit 1 to the second slot and so on. The first active slot
 * maps to the first channel of the DAI, the second active slot to the second
 * channel and so on.
 *
 * TDM mode can be disabled by passing 0 for @slots. In this case @tx_mask,
 * @rx_mask and @slot_width will be ignored.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int snd_soc_dai_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	if (dai->driver->ops->xlate_tdm_slot_mask)
		dai->driver->ops->xlate_tdm_slot_mask(slots,
						&tx_mask, &rx_mask);
	else
		snd_soc_xlate_tdm_slot_mask(slots, &tx_mask, &rx_mask);

	dai->tx_mask = tx_mask;
	dai->rx_mask = rx_mask;

	if (dai->driver->ops->set_tdm_slot)
		return dai->driver->ops->set_tdm_slot(dai, tx_mask, rx_mask,
				slots, slot_width);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_tdm_slot);

/**
 * snd_soc_dai_set_channel_map - configure DAI audio channel map
 * @dai: DAI
 * @tx_num: how many TX channels
 * @tx_slot: pointer to an array which imply the TX slot number channel
 *           0~num-1 uses
 * @rx_num: how many RX channels
 * @rx_slot: pointer to an array which imply the RX slot number channel
 *           0~num-1 uses
 *
 * configure the relationship between channel number and TDM slot number.
 */
int snd_soc_dai_set_channel_map(struct snd_soc_dai *dai,
	unsigned int tx_num, unsigned int *tx_slot,
	unsigned int rx_num, unsigned int *rx_slot)
{
	if (dai->driver->ops->set_channel_map)
		return dai->driver->ops->set_channel_map(dai, tx_num, tx_slot,
			rx_num, rx_slot);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_channel_map);

/**
 * snd_soc_dai_get_channel_map - Get DAI audio channel map
 * @dai: DAI
 * @tx_num: how many TX channels
 * @tx_slot: pointer to an array which imply the TX slot number channel
 *           0~num-1 uses
 * @rx_num: how many RX channels
 * @rx_slot: pointer to an array which imply the RX slot number channel
 *           0~num-1 uses
 */
int snd_soc_dai_get_channel_map(struct snd_soc_dai *dai,
	unsigned int *tx_num, unsigned int *tx_slot,
	unsigned int *rx_num, unsigned int *rx_slot)
{
	if (dai->driver->ops->get_channel_map)
		return dai->driver->ops->get_channel_map(dai, tx_num, tx_slot,
			rx_num, rx_slot);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_get_channel_map);

/**
 * snd_soc_dai_set_tristate - configure DAI system or master clock.
 * @dai: DAI
 * @tristate: tristate enable
 *
 * Tristates the DAI so that others can use it.
 */
int snd_soc_dai_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	if (dai->driver->ops->set_tristate)
		return dai->driver->ops->set_tristate(dai, tristate);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_tristate);

/**
 * snd_soc_dai_digital_mute - configure DAI system or master clock.
 * @dai: DAI
 * @mute: mute enable
 * @direction: stream to mute
 *
 * Mutes the DAI DAC.
 */
int snd_soc_dai_digital_mute(struct snd_soc_dai *dai, int mute,
			     int direction)
{
	if (dai->driver->ops->mute_stream)
		return dai->driver->ops->mute_stream(dai, mute, direction);
	else if (direction == SNDRV_PCM_STREAM_PLAYBACK &&
		 dai->driver->ops->digital_mute)
		return dai->driver->ops->digital_mute(dai, mute);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_digital_mute);

static int snd_soc_bind_card(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	int ret;

	ret = snd_soc_instantiate_card(card);
	if (ret != 0)
		return ret;

	/* deactivate pins to sleep state */
	for_each_card_rtds(card, rtd) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
		struct snd_soc_dai *codec_dai;
		int j;

		for_each_rtd_codec_dai(rtd, j, codec_dai) {
			if (!codec_dai->active)
				pinctrl_pm_select_sleep_state(codec_dai->dev);
		}

		if (!cpu_dai->active)
			pinctrl_pm_select_sleep_state(cpu_dai->dev);
	}

	return ret;
}

/**
 * snd_soc_register_card - Register a card with the ASoC core
 *
 * @card: Card to register
 *
 */
int snd_soc_register_card(struct snd_soc_card *card)
{
	int i, ret;
	struct snd_soc_dai_link *link;

	if (!card->name || !card->dev)
		return -EINVAL;

	mutex_lock(&client_mutex);
	for_each_card_prelinks(card, i, link) {

		ret = soc_init_dai_link(card, link);
		if (ret) {
			dev_err(card->dev, "ASoC: failed to init link %s\n",
				link->name);
			mutex_unlock(&client_mutex);
			return ret;
		}
	}
	mutex_unlock(&client_mutex);

	dev_set_drvdata(card->dev, card);

	snd_soc_initialize_card_lists(card);

	INIT_LIST_HEAD(&card->dai_link_list);

	INIT_LIST_HEAD(&card->rtd_list);
	card->num_rtd = 0;

	INIT_LIST_HEAD(&card->dapm_dirty);
	INIT_LIST_HEAD(&card->dobj_list);
	card->instantiated = 0;
	mutex_init(&card->mutex);
	mutex_init(&card->dapm_mutex);

	return snd_soc_bind_card(card);
}
EXPORT_SYMBOL_GPL(snd_soc_register_card);

static void snd_soc_unbind_card(struct snd_soc_card *card, bool unregister)
{
	if (card->instantiated) {
		card->instantiated = false;
		snd_soc_dapm_shutdown(card);
		snd_soc_flush_all_delayed_work(card);
		soc_cleanup_card_resources(card);
		if (!unregister)
			list_add(&card->list, &unbind_card_list);
	} else {
		if (unregister)
			list_del(&card->list);
	}
}

/**
 * snd_soc_unregister_card - Unregister a card with the ASoC core
 *
 * @card: Card to unregister
 *
 */
int snd_soc_unregister_card(struct snd_soc_card *card)
{
	snd_soc_unbind_card(card, true);
	dev_dbg(card->dev, "ASoC: Unregistered card '%s'\n", card->name);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_card);

/*
 * Simplify DAI link configuration by removing ".-1" from device names
 * and sanitizing names.
 */
static char *fmt_single_name(struct device *dev, int *id)
{
	char *found, name[NAME_SIZE];
	int id1, id2;

	if (dev_name(dev) == NULL)
		return NULL;

	strlcpy(name, dev_name(dev), NAME_SIZE);

	/* are we a "%s.%d" name (platform and SPI components) */
	found = strstr(name, dev->driver->name);
	if (found) {
		/* get ID */
		if (sscanf(&found[strlen(dev->driver->name)], ".%d", id) == 1) {

			/* discard ID from name if ID == -1 */
			if (*id == -1)
				found[strlen(dev->driver->name)] = '\0';
		}

	} else {
		/* I2C component devices are named "bus-addr" */
		if (sscanf(name, "%x-%x", &id1, &id2) == 2) {
			char tmp[NAME_SIZE];

			/* create unique ID number from I2C addr and bus */
			*id = ((id1 & 0xffff) << 16) + id2;

			/* sanitize component name for DAI link creation */
			snprintf(tmp, NAME_SIZE, "%s.%s", dev->driver->name,
				 name);
			strlcpy(name, tmp, NAME_SIZE);
		} else
			*id = 0;
	}

	return kstrdup(name, GFP_KERNEL);
}

/*
 * Simplify DAI link naming for single devices with multiple DAIs by removing
 * any ".-1" and using the DAI name (instead of device name).
 */
static inline char *fmt_multiple_name(struct device *dev,
		struct snd_soc_dai_driver *dai_drv)
{
	if (dai_drv->name == NULL) {
		dev_err(dev,
			"ASoC: error - multiple DAI %s registered with no name\n",
			dev_name(dev));
		return NULL;
	}

	return kstrdup(dai_drv->name, GFP_KERNEL);
}

/**
 * snd_soc_unregister_dai - Unregister DAIs from the ASoC core
 *
 * @component: The component for which the DAIs should be unregistered
 */
static void snd_soc_unregister_dais(struct snd_soc_component *component)
{
	struct snd_soc_dai *dai, *_dai;

	for_each_component_dais_safe(component, dai, _dai) {
		dev_dbg(component->dev, "ASoC: Unregistered DAI '%s'\n",
			dai->name);
		list_del(&dai->list);
		kfree(dai->name);
		kfree(dai);
	}
}

/* Create a DAI and add it to the component's DAI list */
static struct snd_soc_dai *soc_add_dai(struct snd_soc_component *component,
	struct snd_soc_dai_driver *dai_drv,
	bool legacy_dai_naming)
{
	struct device *dev = component->dev;
	struct snd_soc_dai *dai;

	dev_dbg(dev, "ASoC: dynamically register DAI %s\n", dev_name(dev));

	dai = kzalloc(sizeof(struct snd_soc_dai), GFP_KERNEL);
	if (dai == NULL)
		return NULL;

	/*
	 * Back in the old days when we still had component-less DAIs,
	 * instead of having a static name, component-less DAIs would
	 * inherit the name of the parent device so it is possible to
	 * register multiple instances of the DAI. We still need to keep
	 * the same naming style even though those DAIs are not
	 * component-less anymore.
	 */
	if (legacy_dai_naming &&
	    (dai_drv->id == 0 || dai_drv->name == NULL)) {
		dai->name = fmt_single_name(dev, &dai->id);
	} else {
		dai->name = fmt_multiple_name(dev, dai_drv);
		if (dai_drv->id)
			dai->id = dai_drv->id;
		else
			dai->id = component->num_dai;
	}
	if (dai->name == NULL) {
		kfree(dai);
		return NULL;
	}

	dai->component = component;
	dai->dev = dev;
	dai->driver = dai_drv;
	if (!dai->driver->ops)
		dai->driver->ops = &null_dai_ops;

	/* see for_each_component_dais */
	list_add_tail(&dai->list, &component->dai_list);
	component->num_dai++;

	dev_dbg(dev, "ASoC: Registered DAI '%s'\n", dai->name);
	return dai;
}

/**
 * snd_soc_register_dais - Register a DAI with the ASoC core
 *
 * @component: The component the DAIs are registered for
 * @dai_drv: DAI driver to use for the DAIs
 * @count: Number of DAIs
 */
static int snd_soc_register_dais(struct snd_soc_component *component,
				 struct snd_soc_dai_driver *dai_drv,
				 size_t count)
{
	struct device *dev = component->dev;
	struct snd_soc_dai *dai;
	unsigned int i;
	int ret;

	dev_dbg(dev, "ASoC: dai register %s #%zu\n", dev_name(dev), count);

	for (i = 0; i < count; i++) {

		dai = soc_add_dai(component, dai_drv + i, count == 1 &&
				  !component->driver->non_legacy_dai_naming);
		if (dai == NULL) {
			ret = -ENOMEM;
			goto err;
		}
	}

	return 0;

err:
	snd_soc_unregister_dais(component);

	return ret;
}

/**
 * snd_soc_register_dai - Register a DAI dynamically & create its widgets
 *
 * @component: The component the DAIs are registered for
 * @dai_drv: DAI driver to use for the DAI
 *
 * Topology can use this API to register DAIs when probing a component.
 * These DAIs's widgets will be freed in the card cleanup and the DAIs
 * will be freed in the component cleanup.
 */
int snd_soc_register_dai(struct snd_soc_component *component,
	struct snd_soc_dai_driver *dai_drv)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct snd_soc_dai *dai;
	int ret;

	if (dai_drv->dobj.type != SND_SOC_DOBJ_PCM) {
		dev_err(component->dev, "Invalid dai type %d\n",
			dai_drv->dobj.type);
		return -EINVAL;
	}

	lockdep_assert_held(&client_mutex);
	dai = soc_add_dai(component, dai_drv, false);
	if (!dai)
		return -ENOMEM;

	/*
	 * Create the DAI widgets here. After adding DAIs, topology may
	 * also add routes that need these widgets as source or sink.
	 */
	ret = snd_soc_dapm_new_dai_widgets(dapm, dai);
	if (ret != 0) {
		dev_err(component->dev,
			"Failed to create DAI widgets %d\n", ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_register_dai);

static void snd_soc_component_seq_notifier(struct snd_soc_dapm_context *dapm,
	enum snd_soc_dapm_type type, int subseq)
{
	struct snd_soc_component *component = dapm->component;

	component->driver->seq_notifier(component, type, subseq);
}

static int snd_soc_component_stream_event(struct snd_soc_dapm_context *dapm,
	int event)
{
	struct snd_soc_component *component = dapm->component;

	return component->driver->stream_event(component, event);
}

static int snd_soc_component_set_bias_level(struct snd_soc_dapm_context *dapm,
					enum snd_soc_bias_level level)
{
	struct snd_soc_component *component = dapm->component;

	return component->driver->set_bias_level(component, level);
}

static int snd_soc_component_initialize(struct snd_soc_component *component,
	const struct snd_soc_component_driver *driver, struct device *dev)
{
	struct snd_soc_dapm_context *dapm;

	component->name = fmt_single_name(dev, &component->id);
	if (!component->name) {
		dev_err(dev, "ASoC: Failed to allocate name\n");
		return -ENOMEM;
	}

	component->dev = dev;
	component->driver = driver;

	dapm = snd_soc_component_get_dapm(component);
	dapm->dev = dev;
	dapm->component = component;
	dapm->bias_level = SND_SOC_BIAS_OFF;
	dapm->idle_bias_off = !driver->idle_bias_on;
	dapm->suspend_bias_off = driver->suspend_bias_off;
	if (driver->seq_notifier)
		dapm->seq_notifier = snd_soc_component_seq_notifier;
	if (driver->stream_event)
		dapm->stream_event = snd_soc_component_stream_event;
	if (driver->set_bias_level)
		dapm->set_bias_level = snd_soc_component_set_bias_level;

	INIT_LIST_HEAD(&component->dai_list);
	mutex_init(&component->io_mutex);

	return 0;
}

static void snd_soc_component_setup_regmap(struct snd_soc_component *component)
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

static void snd_soc_component_add(struct snd_soc_component *component)
{
	mutex_lock(&client_mutex);

	if (!component->driver->write && !component->driver->read) {
		if (!component->regmap)
			component->regmap = dev_get_regmap(component->dev,
							   NULL);
		if (component->regmap)
			snd_soc_component_setup_regmap(component);
	}

	/* see for_each_component */
	list_add(&component->list, &component_list);
	INIT_LIST_HEAD(&component->dobj_list);

	mutex_unlock(&client_mutex);
}

static void snd_soc_component_cleanup(struct snd_soc_component *component)
{
	snd_soc_unregister_dais(component);
	kfree(component->name);
}

static void snd_soc_component_del_unlocked(struct snd_soc_component *component)
{
	struct snd_soc_card *card = component->card;

	if (card)
		snd_soc_unbind_card(card, false);

	list_del(&component->list);
}

#define ENDIANNESS_MAP(name) \
	(SNDRV_PCM_FMTBIT_##name##LE | SNDRV_PCM_FMTBIT_##name##BE)
static u64 endianness_format_map[] = {
	ENDIANNESS_MAP(S16_),
	ENDIANNESS_MAP(U16_),
	ENDIANNESS_MAP(S24_),
	ENDIANNESS_MAP(U24_),
	ENDIANNESS_MAP(S32_),
	ENDIANNESS_MAP(U32_),
	ENDIANNESS_MAP(S24_3),
	ENDIANNESS_MAP(U24_3),
	ENDIANNESS_MAP(S20_3),
	ENDIANNESS_MAP(U20_3),
	ENDIANNESS_MAP(S18_3),
	ENDIANNESS_MAP(U18_3),
	ENDIANNESS_MAP(FLOAT_),
	ENDIANNESS_MAP(FLOAT64_),
	ENDIANNESS_MAP(IEC958_SUBFRAME_),
};

/*
 * Fix up the DAI formats for endianness: codecs don't actually see
 * the endianness of the data but we're using the CPU format
 * definitions which do need to include endianness so we ensure that
 * codec DAIs always have both big and little endian variants set.
 */
static void convert_endianness_formats(struct snd_soc_pcm_stream *stream)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(endianness_format_map); i++)
		if (stream->formats & endianness_format_map[i])
			stream->formats |= endianness_format_map[i];
}

static void snd_soc_try_rebind_card(void)
{
	struct snd_soc_card *card, *c;

	if (!list_empty(&unbind_card_list)) {
		list_for_each_entry_safe(card, c, &unbind_card_list, list) {
			if (!snd_soc_bind_card(card))
				list_del(&card->list);
		}
	}
}

int snd_soc_add_component(struct device *dev,
			struct snd_soc_component *component,
			const struct snd_soc_component_driver *component_driver,
			struct snd_soc_dai_driver *dai_drv,
			int num_dai)
{
	int ret;
	int i;

	ret = snd_soc_component_initialize(component, component_driver, dev);
	if (ret)
		goto err_free;

	if (component_driver->endianness) {
		for (i = 0; i < num_dai; i++) {
			convert_endianness_formats(&dai_drv[i].playback);
			convert_endianness_formats(&dai_drv[i].capture);
		}
	}

	ret = snd_soc_register_dais(component, dai_drv, num_dai);
	if (ret < 0) {
		dev_err(dev, "ASoC: Failed to register DAIs: %d\n", ret);
		goto err_cleanup;
	}

	snd_soc_component_add(component);
	snd_soc_try_rebind_card();

	return 0;

err_cleanup:
	snd_soc_component_cleanup(component);
err_free:
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_add_component);

int snd_soc_register_component(struct device *dev,
			const struct snd_soc_component_driver *component_driver,
			struct snd_soc_dai_driver *dai_drv,
			int num_dai)
{
	struct snd_soc_component *component;

	component = devm_kzalloc(dev, sizeof(*component), GFP_KERNEL);
	if (!component)
		return -ENOMEM;

	return snd_soc_add_component(dev, component, component_driver,
				     dai_drv, num_dai);
}
EXPORT_SYMBOL_GPL(snd_soc_register_component);

/**
 * snd_soc_unregister_component - Unregister all related component
 * from the ASoC core
 *
 * @dev: The device to unregister
 */
static int __snd_soc_unregister_component(struct device *dev)
{
	struct snd_soc_component *component;
	int found = 0;

	mutex_lock(&client_mutex);
	for_each_component(component) {
		if (dev != component->dev)
			continue;

		snd_soc_tplg_component_remove(component,
					      SND_SOC_TPLG_INDEX_ALL);
		snd_soc_component_del_unlocked(component);
		found = 1;
		break;
	}
	mutex_unlock(&client_mutex);

	if (found)
		snd_soc_component_cleanup(component);

	return found;
}

void snd_soc_unregister_component(struct device *dev)
{
	while (__snd_soc_unregister_component(dev))
		;
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_component);

struct snd_soc_component *snd_soc_lookup_component(struct device *dev,
						   const char *driver_name)
{
	struct snd_soc_component *component;
	struct snd_soc_component *ret;

	ret = NULL;
	mutex_lock(&client_mutex);
	for_each_component(component) {
		if (dev != component->dev)
			continue;

		if (driver_name &&
		    (driver_name != component->driver->name) &&
		    (strcmp(component->driver->name, driver_name) != 0))
			continue;

		ret = component;
		break;
	}
	mutex_unlock(&client_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_lookup_component);

/* Retrieve a card's name from device tree */
int snd_soc_of_parse_card_name(struct snd_soc_card *card,
			       const char *propname)
{
	struct device_node *np;
	int ret;

	if (!card->dev) {
		pr_err("card->dev is not set before calling %s\n", __func__);
		return -EINVAL;
	}

	np = card->dev->of_node;

	ret = of_property_read_string_index(np, propname, 0, &card->name);
	/*
	 * EINVAL means the property does not exist. This is fine providing
	 * card->name was previously set, which is checked later in
	 * snd_soc_register_card.
	 */
	if (ret < 0 && ret != -EINVAL) {
		dev_err(card->dev,
			"ASoC: Property '%s' could not be read: %d\n",
			propname, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_of_parse_card_name);

static const struct snd_soc_dapm_widget simple_widgets[] = {
	SND_SOC_DAPM_MIC("Microphone", NULL),
	SND_SOC_DAPM_LINE("Line", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

int snd_soc_of_parse_audio_simple_widgets(struct snd_soc_card *card,
					  const char *propname)
{
	struct device_node *np = card->dev->of_node;
	struct snd_soc_dapm_widget *widgets;
	const char *template, *wname;
	int i, j, num_widgets, ret;

	num_widgets = of_property_count_strings(np, propname);
	if (num_widgets < 0) {
		dev_err(card->dev,
			"ASoC: Property '%s' does not exist\n",	propname);
		return -EINVAL;
	}
	if (num_widgets & 1) {
		dev_err(card->dev,
			"ASoC: Property '%s' length is not even\n", propname);
		return -EINVAL;
	}

	num_widgets /= 2;
	if (!num_widgets) {
		dev_err(card->dev, "ASoC: Property '%s's length is zero\n",
			propname);
		return -EINVAL;
	}

	widgets = devm_kcalloc(card->dev, num_widgets, sizeof(*widgets),
			       GFP_KERNEL);
	if (!widgets) {
		dev_err(card->dev,
			"ASoC: Could not allocate memory for widgets\n");
		return -ENOMEM;
	}

	for (i = 0; i < num_widgets; i++) {
		ret = of_property_read_string_index(np, propname,
			2 * i, &template);
		if (ret) {
			dev_err(card->dev,
				"ASoC: Property '%s' index %d read error:%d\n",
				propname, 2 * i, ret);
			return -EINVAL;
		}

		for (j = 0; j < ARRAY_SIZE(simple_widgets); j++) {
			if (!strncmp(template, simple_widgets[j].name,
				     strlen(simple_widgets[j].name))) {
				widgets[i] = simple_widgets[j];
				break;
			}
		}

		if (j >= ARRAY_SIZE(simple_widgets)) {
			dev_err(card->dev,
				"ASoC: DAPM widget '%s' is not supported\n",
				template);
			return -EINVAL;
		}

		ret = of_property_read_string_index(np, propname,
						    (2 * i) + 1,
						    &wname);
		if (ret) {
			dev_err(card->dev,
				"ASoC: Property '%s' index %d read error:%d\n",
				propname, (2 * i) + 1, ret);
			return -EINVAL;
		}

		widgets[i].name = wname;
	}

	card->of_dapm_widgets = widgets;
	card->num_of_dapm_widgets = num_widgets;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_of_parse_audio_simple_widgets);

int snd_soc_of_get_slot_mask(struct device_node *np,
			     const char *prop_name,
			     unsigned int *mask)
{
	u32 val;
	const __be32 *of_slot_mask = of_get_property(np, prop_name, &val);
	int i;

	if (!of_slot_mask)
		return 0;
	val /= sizeof(u32);
	for (i = 0; i < val; i++)
		if (be32_to_cpup(&of_slot_mask[i]))
			*mask |= (1 << i);

	return val;
}
EXPORT_SYMBOL_GPL(snd_soc_of_get_slot_mask);

int snd_soc_of_parse_tdm_slot(struct device_node *np,
			      unsigned int *tx_mask,
			      unsigned int *rx_mask,
			      unsigned int *slots,
			      unsigned int *slot_width)
{
	u32 val;
	int ret;

	if (tx_mask)
		snd_soc_of_get_slot_mask(np, "dai-tdm-slot-tx-mask", tx_mask);
	if (rx_mask)
		snd_soc_of_get_slot_mask(np, "dai-tdm-slot-rx-mask", rx_mask);

	if (of_property_read_bool(np, "dai-tdm-slot-num")) {
		ret = of_property_read_u32(np, "dai-tdm-slot-num", &val);
		if (ret)
			return ret;

		if (slots)
			*slots = val;
	}

	if (of_property_read_bool(np, "dai-tdm-slot-width")) {
		ret = of_property_read_u32(np, "dai-tdm-slot-width", &val);
		if (ret)
			return ret;

		if (slot_width)
			*slot_width = val;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_of_parse_tdm_slot);

void snd_soc_of_parse_node_prefix(struct device_node *np,
				  struct snd_soc_codec_conf *codec_conf,
				  struct device_node *of_node,
				  const char *propname)
{
	const char *str;
	int ret;

	ret = of_property_read_string(np, propname, &str);
	if (ret < 0) {
		/* no prefix is not error */
		return;
	}

	codec_conf->of_node	= of_node;
	codec_conf->name_prefix	= str;
}
EXPORT_SYMBOL_GPL(snd_soc_of_parse_node_prefix);

int snd_soc_of_parse_audio_routing(struct snd_soc_card *card,
				   const char *propname)
{
	struct device_node *np = card->dev->of_node;
	int num_routes;
	struct snd_soc_dapm_route *routes;
	int i, ret;

	num_routes = of_property_count_strings(np, propname);
	if (num_routes < 0 || num_routes & 1) {
		dev_err(card->dev,
			"ASoC: Property '%s' does not exist or its length is not even\n",
			propname);
		return -EINVAL;
	}
	num_routes /= 2;
	if (!num_routes) {
		dev_err(card->dev, "ASoC: Property '%s's length is zero\n",
			propname);
		return -EINVAL;
	}

	routes = devm_kcalloc(card->dev, num_routes, sizeof(*routes),
			      GFP_KERNEL);
	if (!routes) {
		dev_err(card->dev,
			"ASoC: Could not allocate DAPM route table\n");
		return -EINVAL;
	}

	for (i = 0; i < num_routes; i++) {
		ret = of_property_read_string_index(np, propname,
			2 * i, &routes[i].sink);
		if (ret) {
			dev_err(card->dev,
				"ASoC: Property '%s' index %d could not be read: %d\n",
				propname, 2 * i, ret);
			return -EINVAL;
		}
		ret = of_property_read_string_index(np, propname,
			(2 * i) + 1, &routes[i].source);
		if (ret) {
			dev_err(card->dev,
				"ASoC: Property '%s' index %d could not be read: %d\n",
				propname, (2 * i) + 1, ret);
			return -EINVAL;
		}
	}

	card->num_of_dapm_routes = num_routes;
	card->of_dapm_routes = routes;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_of_parse_audio_routing);

unsigned int snd_soc_of_parse_daifmt(struct device_node *np,
				     const char *prefix,
				     struct device_node **bitclkmaster,
				     struct device_node **framemaster)
{
	int ret, i;
	char prop[128];
	unsigned int format = 0;
	int bit, frame;
	const char *str;
	struct {
		char *name;
		unsigned int val;
	} of_fmt_table[] = {
		{ "i2s",	SND_SOC_DAIFMT_I2S },
		{ "right_j",	SND_SOC_DAIFMT_RIGHT_J },
		{ "left_j",	SND_SOC_DAIFMT_LEFT_J },
		{ "dsp_a",	SND_SOC_DAIFMT_DSP_A },
		{ "dsp_b",	SND_SOC_DAIFMT_DSP_B },
		{ "ac97",	SND_SOC_DAIFMT_AC97 },
		{ "pdm",	SND_SOC_DAIFMT_PDM},
		{ "msb",	SND_SOC_DAIFMT_MSB },
		{ "lsb",	SND_SOC_DAIFMT_LSB },
	};

	if (!prefix)
		prefix = "";

	/*
	 * check "dai-format = xxx"
	 * or    "[prefix]format = xxx"
	 * SND_SOC_DAIFMT_FORMAT_MASK area
	 */
	ret = of_property_read_string(np, "dai-format", &str);
	if (ret < 0) {
		snprintf(prop, sizeof(prop), "%sformat", prefix);
		ret = of_property_read_string(np, prop, &str);
	}
	if (ret == 0) {
		for (i = 0; i < ARRAY_SIZE(of_fmt_table); i++) {
			if (strcmp(str, of_fmt_table[i].name) == 0) {
				format |= of_fmt_table[i].val;
				break;
			}
		}
	}

	/*
	 * check "[prefix]continuous-clock"
	 * SND_SOC_DAIFMT_CLOCK_MASK area
	 */
	snprintf(prop, sizeof(prop), "%scontinuous-clock", prefix);
	if (of_property_read_bool(np, prop))
		format |= SND_SOC_DAIFMT_CONT;
	else
		format |= SND_SOC_DAIFMT_GATED;

	/*
	 * check "[prefix]bitclock-inversion"
	 * check "[prefix]frame-inversion"
	 * SND_SOC_DAIFMT_INV_MASK area
	 */
	snprintf(prop, sizeof(prop), "%sbitclock-inversion", prefix);
	bit = !!of_get_property(np, prop, NULL);

	snprintf(prop, sizeof(prop), "%sframe-inversion", prefix);
	frame = !!of_get_property(np, prop, NULL);

	switch ((bit << 4) + frame) {
	case 0x11:
		format |= SND_SOC_DAIFMT_IB_IF;
		break;
	case 0x10:
		format |= SND_SOC_DAIFMT_IB_NF;
		break;
	case 0x01:
		format |= SND_SOC_DAIFMT_NB_IF;
		break;
	default:
		/* SND_SOC_DAIFMT_NB_NF is default */
		break;
	}

	/*
	 * check "[prefix]bitclock-master"
	 * check "[prefix]frame-master"
	 * SND_SOC_DAIFMT_MASTER_MASK area
	 */
	snprintf(prop, sizeof(prop), "%sbitclock-master", prefix);
	bit = !!of_get_property(np, prop, NULL);
	if (bit && bitclkmaster)
		*bitclkmaster = of_parse_phandle(np, prop, 0);

	snprintf(prop, sizeof(prop), "%sframe-master", prefix);
	frame = !!of_get_property(np, prop, NULL);
	if (frame && framemaster)
		*framemaster = of_parse_phandle(np, prop, 0);

	switch ((bit << 4) + frame) {
	case 0x11:
		format |= SND_SOC_DAIFMT_CBM_CFM;
		break;
	case 0x10:
		format |= SND_SOC_DAIFMT_CBM_CFS;
		break;
	case 0x01:
		format |= SND_SOC_DAIFMT_CBS_CFM;
		break;
	default:
		format |= SND_SOC_DAIFMT_CBS_CFS;
		break;
	}

	return format;
}
EXPORT_SYMBOL_GPL(snd_soc_of_parse_daifmt);

int snd_soc_get_dai_id(struct device_node *ep)
{
	struct snd_soc_component *pos;
	struct device_node *node;
	int ret;

	node = of_graph_get_port_parent(ep);

	/*
	 * For example HDMI case, HDMI has video/sound port,
	 * but ALSA SoC needs sound port number only.
	 * Thus counting HDMI DT port/endpoint doesn't work.
	 * Then, it should have .of_xlate_dai_id
	 */
	ret = -ENOTSUPP;
	mutex_lock(&client_mutex);
	for_each_component(pos) {
		struct device_node *component_of_node = pos->dev->of_node;

		if (!component_of_node && pos->dev->parent)
			component_of_node = pos->dev->parent->of_node;

		if (component_of_node != node)
			continue;

		if (pos->driver->of_xlate_dai_id)
			ret = pos->driver->of_xlate_dai_id(pos, ep);

		break;
	}
	mutex_unlock(&client_mutex);

	of_node_put(node);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_get_dai_id);

int snd_soc_get_dai_name(struct of_phandle_args *args,
				const char **dai_name)
{
	struct snd_soc_component *pos;
	struct device_node *component_of_node;
	int ret = -EPROBE_DEFER;

	mutex_lock(&client_mutex);
	for_each_component(pos) {
		component_of_node = pos->dev->of_node;
		if (!component_of_node && pos->dev->parent)
			component_of_node = pos->dev->parent->of_node;

		if (component_of_node != args->np)
			continue;

		if (pos->driver->of_xlate_dai_name) {
			ret = pos->driver->of_xlate_dai_name(pos,
							     args,
							     dai_name);
		} else {
			struct snd_soc_dai *dai;
			int id = -1;

			switch (args->args_count) {
			case 0:
				id = 0; /* same as dai_drv[0] */
				break;
			case 1:
				id = args->args[0];
				break;
			default:
				/* not supported */
				break;
			}

			if (id < 0 || id >= pos->num_dai) {
				ret = -EINVAL;
				continue;
			}

			ret = 0;

			/* find target DAI */
			for_each_component_dais(pos, dai) {
				if (id == 0)
					break;
				id--;
			}

			*dai_name = dai->driver->name;
			if (!*dai_name)
				*dai_name = pos->name;
		}

		break;
	}
	mutex_unlock(&client_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_get_dai_name);

int snd_soc_of_get_dai_name(struct device_node *of_node,
			    const char **dai_name)
{
	struct of_phandle_args args;
	int ret;

	ret = of_parse_phandle_with_args(of_node, "sound-dai",
					 "#sound-dai-cells", 0, &args);
	if (ret)
		return ret;

	ret = snd_soc_get_dai_name(&args, dai_name);

	of_node_put(args.np);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_of_get_dai_name);

/*
 * snd_soc_of_put_dai_link_codecs - Dereference device nodes in the codecs array
 * @dai_link: DAI link
 *
 * Dereference device nodes acquired by snd_soc_of_get_dai_link_codecs().
 */
void snd_soc_of_put_dai_link_codecs(struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_dai_link_component *component;
	int index;

	for_each_link_codecs(dai_link, index, component) {
		if (!component->of_node)
			break;
		of_node_put(component->of_node);
		component->of_node = NULL;
	}
}
EXPORT_SYMBOL_GPL(snd_soc_of_put_dai_link_codecs);

/*
 * snd_soc_of_get_dai_link_codecs - Parse a list of CODECs in the devicetree
 * @dev: Card device
 * @of_node: Device node
 * @dai_link: DAI link
 *
 * Builds an array of CODEC DAI components from the DAI link property
 * 'sound-dai'.
 * The array is set in the DAI link and the number of DAIs is set accordingly.
 * The device nodes in the array (of_node) must be dereferenced by calling
 * snd_soc_of_put_dai_link_codecs() on @dai_link.
 *
 * Returns 0 for success
 */
int snd_soc_of_get_dai_link_codecs(struct device *dev,
				   struct device_node *of_node,
				   struct snd_soc_dai_link *dai_link)
{
	struct of_phandle_args args;
	struct snd_soc_dai_link_component *component;
	char *name;
	int index, num_codecs, ret;

	/* Count the number of CODECs */
	name = "sound-dai";
	num_codecs = of_count_phandle_with_args(of_node, name,
						"#sound-dai-cells");
	if (num_codecs <= 0) {
		if (num_codecs == -ENOENT)
			dev_err(dev, "No 'sound-dai' property\n");
		else
			dev_err(dev, "Bad phandle in 'sound-dai'\n");
		return num_codecs;
	}
	component = devm_kcalloc(dev,
				 num_codecs, sizeof(*component),
				 GFP_KERNEL);
	if (!component)
		return -ENOMEM;
	dai_link->codecs = component;
	dai_link->num_codecs = num_codecs;

	/* Parse the list */
	for_each_link_codecs(dai_link, index, component) {
		ret = of_parse_phandle_with_args(of_node, name,
						 "#sound-dai-cells",
						 index, &args);
		if (ret)
			goto err;
		component->of_node = args.np;
		ret = snd_soc_get_dai_name(&args, &component->dai_name);
		if (ret < 0)
			goto err;
	}
	return 0;
err:
	snd_soc_of_put_dai_link_codecs(dai_link);
	dai_link->codecs = NULL;
	dai_link->num_codecs = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_of_get_dai_link_codecs);

static int __init snd_soc_init(void)
{
	snd_soc_debugfs_init();
	snd_soc_util_init();

	return platform_driver_register(&soc_driver);
}
module_init(snd_soc_init);

static void __exit snd_soc_exit(void)
{
	snd_soc_util_exit();
	snd_soc_debugfs_exit();

	platform_driver_unregister(&soc_driver);
}
module_exit(snd_soc_exit);

/* Module information */
MODULE_AUTHOR("Liam Girdwood, lrg@slimlogic.co.uk");
MODULE_DESCRIPTION("ALSA SoC Core");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:soc-audio");

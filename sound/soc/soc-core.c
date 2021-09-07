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
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dpcm.h>
#include <sound/soc-topology.h>
#include <sound/soc-link.h>
#include <sound/initval.h>

#define CREATE_TRACE_POINTS
#include <trace/events/asoc.h>

static DEFINE_MUTEX(client_mutex);
static LIST_HEAD(component_list);
static LIST_HEAD(unbind_card_list);

#define for_each_component(component)			\
	list_for_each_entry(component, &component_list, list)

/*
 * This is used if driver don't need to have CPU/Codec/Platform
 * dai_link. see soc.h
 */
struct snd_soc_dai_link_component null_dailink_component[0];
EXPORT_SYMBOL_GPL(null_dailink_component);

/*
 * This is a timeout to do a DAPM powerdown after a stream is closed().
 * It can be used to eliminate pops between different playback streams, e.g.
 * between two audio tracks.
 */
static int pmdown_time = 5000;
module_param(pmdown_time, int, 0);
MODULE_PARM_DESC(pmdown_time, "DAPM stream powerdown time (msecs)");

static ssize_t pmdown_time_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct snd_soc_pcm_runtime *rtd = dev_get_drvdata(dev);

	return sprintf(buf, "%ld\n", rtd->pmdown_time);
}

static ssize_t pmdown_time_store(struct device *dev,
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

static DEVICE_ATTR_RW(pmdown_time);

static struct attribute *soc_dev_attrs[] = {
	&dev_attr_pmdown_time.attr,
	NULL
};

static umode_t soc_dev_attr_is_visible(struct kobject *kobj,
				       struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct snd_soc_pcm_runtime *rtd = dev_get_drvdata(dev);

	if (!rtd)
		return 0;

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
struct dentry *snd_soc_debugfs_root;
EXPORT_SYMBOL_GPL(snd_soc_debugfs_root);

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

	snd_soc_dapm_debugfs_init(snd_soc_component_get_dapm(component),
		component->debugfs_root);
}

static void soc_cleanup_component_debugfs(struct snd_soc_component *component)
{
	if (!component->debugfs_root)
		return;
	debugfs_remove_recursive(component->debugfs_root);
	component->debugfs_root = NULL;
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
	card->debugfs_card_root = debugfs_create_dir(card->name,
						     snd_soc_debugfs_root);

	debugfs_create_u32("dapm_pop_time", 0644, card->debugfs_card_root,
			   &card->pop_time);

	snd_soc_dapm_debugfs_init(&card->dapm, card->debugfs_card_root);
}

static void soc_cleanup_card_debugfs(struct snd_soc_card *card)
{
	debugfs_remove_recursive(card->debugfs_card_root);
	card->debugfs_card_root = NULL;
}

static void snd_soc_debugfs_init(void)
{
	snd_soc_debugfs_root = debugfs_create_dir("asoc", NULL);

	debugfs_create_file("dais", 0444, snd_soc_debugfs_root, NULL,
			    &dai_list_fops);

	debugfs_create_file("components", 0444, snd_soc_debugfs_root, NULL,
			    &component_list_fops);
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

static int snd_soc_rtd_add_component(struct snd_soc_pcm_runtime *rtd,
				     struct snd_soc_component *component)
{
	struct snd_soc_component *comp;
	int i;

	for_each_rtd_components(rtd, i, comp) {
		/* already connected */
		if (comp == component)
			return 0;
	}

	/* see for_each_rtd_components */
	rtd->components[rtd->num_components] = component;
	rtd->num_components++;

	return 0;
}

struct snd_soc_component *snd_soc_rtdcom_lookup(struct snd_soc_pcm_runtime *rtd,
						const char *driver_name)
{
	struct snd_soc_component *component;
	int i;

	if (!driver_name)
		return NULL;

	/*
	 * NOTE
	 *
	 * snd_soc_rtdcom_lookup() will find component from rtd by using
	 * specified driver name.
	 * But, if many components which have same driver name are connected
	 * to 1 rtd, this function will return 1st found component.
	 */
	for_each_rtd_components(rtd, i, component) {
		const char *component_name = component->driver->name;

		if (!component_name)
			continue;

		if ((component_name == driver_name) ||
		    strcmp(component_name, driver_name) == 0)
			return component;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_rtdcom_lookup);

struct snd_soc_component
*snd_soc_lookup_component_nolocked(struct device *dev, const char *driver_name)
{
	struct snd_soc_component *component;
	struct snd_soc_component *found_component;

	found_component = NULL;
	for_each_component(component) {
		if ((dev == component->dev) &&
		    (!driver_name ||
		     (driver_name == component->driver->name) ||
		     (strcmp(component->driver->name, driver_name) == 0))) {
			found_component = component;
			break;
		}
	}

	return found_component;
}
EXPORT_SYMBOL_GPL(snd_soc_lookup_component_nolocked);

struct snd_soc_component *snd_soc_lookup_component(struct device *dev,
						   const char *driver_name)
{
	struct snd_soc_component *component;

	mutex_lock(&client_mutex);
	component = snd_soc_lookup_component_nolocked(dev, driver_name);
	mutex_unlock(&client_mutex);

	return component;
}
EXPORT_SYMBOL_GPL(snd_soc_lookup_component);

struct snd_soc_pcm_runtime
*snd_soc_get_pcm_runtime(struct snd_soc_card *card,
			 struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;

	for_each_card_rtds(card, rtd) {
		if (rtd->dai_link == dai_link)
			return rtd;
	}
	dev_dbg(card->dev, "ASoC: failed to find rtd %s\n", dai_link->name);
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_get_pcm_runtime);

/*
 * Power down the audio subsystem pmdown_time msecs after close is called.
 * This is to ensure there are no pops or clicks in between any music tracks
 * due to DAPM power cycling.
 */
void snd_soc_close_delayed_work(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int playback = SNDRV_PCM_STREAM_PLAYBACK;

	mutex_lock_nested(&rtd->card->pcm_mutex, rtd->card->pcm_subclass);

	dev_dbg(rtd->dev,
		"ASoC: pop wq checking: %s status: %s waiting: %s\n",
		codec_dai->driver->playback.stream_name,
		snd_soc_dai_stream_active(codec_dai, playback) ?
		"active" : "inactive",
		rtd->pop_wait ? "yes" : "no");

	/* are we waiting on this codec DAI stream */
	if (rtd->pop_wait == 1) {
		rtd->pop_wait = 0;
		snd_soc_dapm_stream_event(rtd, playback,
					  SND_SOC_DAPM_STREAM_STOP);
	}

	mutex_unlock(&rtd->card->pcm_mutex);
}
EXPORT_SYMBOL_GPL(snd_soc_close_delayed_work);

static void soc_release_rtd_dev(struct device *dev)
{
	/* "dev" means "rtd->dev" */
	kfree(dev);
}

static void soc_free_pcm_runtime(struct snd_soc_pcm_runtime *rtd)
{
	if (!rtd)
		return;

	list_del(&rtd->list);

	if (delayed_work_pending(&rtd->delayed_work))
		flush_delayed_work(&rtd->delayed_work);
	snd_soc_pcm_component_free(rtd);

	/*
	 * we don't need to call kfree() for rtd->dev
	 * see
	 *	soc_release_rtd_dev()
	 *
	 * We don't need rtd->dev NULL check, because
	 * it is alloced *before* rtd.
	 * see
	 *	soc_new_pcm_runtime()
	 *
	 * We don't need to mind freeing for rtd,
	 * because it was created from dev (= rtd->dev)
	 * see
	 *	soc_new_pcm_runtime()
	 *
	 *		rtd = devm_kzalloc(dev, ...);
	 *		rtd->dev = dev
	 */
	device_unregister(rtd->dev);
}

static void close_delayed_work(struct work_struct *work) {
	struct snd_soc_pcm_runtime *rtd =
			container_of(work, struct snd_soc_pcm_runtime,
				     delayed_work.work);

	if (rtd->close_delayed_work_func)
		rtd->close_delayed_work_func(rtd);
}

static struct snd_soc_pcm_runtime *soc_new_pcm_runtime(
	struct snd_soc_card *card, struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_component *component;
	struct device *dev;
	int ret;
	int stream;

	/*
	 * for rtd->dev
	 */
	dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->parent	= card->dev;
	dev->release	= soc_release_rtd_dev;

	dev_set_name(dev, "%s", dai_link->name);

	ret = device_register(dev);
	if (ret < 0) {
		put_device(dev); /* soc_release_rtd_dev */
		return NULL;
	}

	/*
	 * for rtd
	 */
	rtd = devm_kzalloc(dev,
			   sizeof(*rtd) +
			   sizeof(*component) * (dai_link->num_cpus +
						 dai_link->num_codecs +
						 dai_link->num_platforms),
			   GFP_KERNEL);
	if (!rtd) {
		device_unregister(dev);
		return NULL;
	}

	rtd->dev = dev;
	INIT_LIST_HEAD(&rtd->list);
	for_each_pcm_streams(stream) {
		INIT_LIST_HEAD(&rtd->dpcm[stream].be_clients);
		INIT_LIST_HEAD(&rtd->dpcm[stream].fe_clients);
	}
	dev_set_drvdata(dev, rtd);
	INIT_DELAYED_WORK(&rtd->delayed_work, close_delayed_work);

	/*
	 * for rtd->dais
	 */
	rtd->dais = devm_kcalloc(dev, dai_link->num_cpus + dai_link->num_codecs,
					sizeof(struct snd_soc_dai *),
					GFP_KERNEL);
	if (!rtd->dais)
		goto free_rtd;

	/*
	 * dais = [][][][][][][][][][][][][][][][][][]
	 *	  ^cpu_dais         ^codec_dais
	 *	  |--- num_cpus ---|--- num_codecs --|
	 * see
	 *	asoc_rtd_to_cpu()
	 *	asoc_rtd_to_codec()
	 */
	rtd->num_cpus	= dai_link->num_cpus;
	rtd->num_codecs	= dai_link->num_codecs;
	rtd->card	= card;
	rtd->dai_link	= dai_link;
	rtd->num	= card->num_rtd++;

	/* see for_each_card_rtds */
	list_add_tail(&rtd->list, &card->rtd_list);

	ret = device_add_groups(dev, soc_dev_attr_groups);
	if (ret < 0)
		goto free_rtd;

	return rtd;

free_rtd:
	soc_free_pcm_runtime(rtd);
	return NULL;
}

static void snd_soc_flush_all_delayed_work(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;

	for_each_card_rtds(card, rtd)
		flush_delayed_work(&rtd->delayed_work);
}

#ifdef CONFIG_PM_SLEEP
static void soc_playback_digital_mute(struct snd_soc_card *card, int mute)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *dai;
	int playback = SNDRV_PCM_STREAM_PLAYBACK;
	int i;

	for_each_card_rtds(card, rtd) {

		if (rtd->dai_link->ignore_suspend)
			continue;

		for_each_rtd_dais(rtd, i, dai) {
			if (snd_soc_dai_stream_active(dai, playback))
				snd_soc_dai_digital_mute(dai, mute, playback);
		}
	}
}

static void soc_dapm_suspend_resume(struct snd_soc_card *card, int event)
{
	struct snd_soc_pcm_runtime *rtd;
	int stream;

	for_each_card_rtds(card, rtd) {

		if (rtd->dai_link->ignore_suspend)
			continue;

		for_each_pcm_streams(stream)
			snd_soc_dapm_stream_event(rtd, stream, event);
	}
}

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
	snd_power_wait(card->snd_card);

	/* we're going to block userspace touching us until resume completes */
	snd_power_change_state(card->snd_card, SNDRV_CTL_POWER_D3hot);

	/* mute any active DACs */
	soc_playback_digital_mute(card, 1);

	/* suspend all pcms */
	for_each_card_rtds(card, rtd) {
		if (rtd->dai_link->ignore_suspend)
			continue;

		snd_pcm_suspend_all(rtd->pcm);
	}

	snd_soc_card_suspend_pre(card);

	/* close any waiting streams */
	snd_soc_flush_all_delayed_work(card);

	soc_dapm_suspend_resume(card, SND_SOC_DAPM_STREAM_SUSPEND);

	/* Recheck all endpoints too, their state is affected by suspend */
	dapm_mark_endpoints_dirty(card);
	snd_soc_dapm_sync(&card->dapm);

	/* suspend all COMPONENTs */
	for_each_card_rtds(card, rtd) {

		if (rtd->dai_link->ignore_suspend)
			continue;

		for_each_rtd_components(rtd, i, component) {
			struct snd_soc_dapm_context *dapm =
				snd_soc_component_get_dapm(component);

			/*
			 * ignore if component was already suspended
			 */
			if (snd_soc_component_is_suspended(component))
				continue;

			/*
			 * If there are paths active then the COMPONENT will be
			 * held with bias _ON and should not be suspended.
			 */
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
				fallthrough;

			case SND_SOC_BIAS_OFF:
				snd_soc_component_suspend(component);
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

	snd_soc_card_suspend_post(card);

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
	struct snd_soc_component *component;

	/*
	 * our power state is still SNDRV_CTL_POWER_D3hot from suspend time,
	 * so userspace apps are blocked from touching us
	 */

	dev_dbg(card->dev, "ASoC: starting resume work\n");

	/* Bring us up into D2 so that DAPM starts enabling things */
	snd_power_change_state(card->snd_card, SNDRV_CTL_POWER_D2);

	snd_soc_card_resume_pre(card);

	for_each_card_components(card, component) {
		if (snd_soc_component_is_suspended(component))
			snd_soc_component_resume(component);
	}

	soc_dapm_suspend_resume(card, SND_SOC_DAPM_STREAM_RESUME);

	/* unmute any active DACs */
	soc_playback_digital_mute(card, 0);

	snd_soc_card_resume_post(card);

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
	struct snd_soc_component *component;

	/* If the card is not initialized yet there is nothing to do */
	if (!card->instantiated)
		return 0;

	/* activate pins from sleep state */
	for_each_card_components(card, component)
		if (snd_soc_component_active(component))
			pinctrl_pm_select_default_state(component->dev);

	dev_dbg(dev, "ASoC: Scheduling resume work\n");
	if (!schedule_work(&card->deferred_resume_work))
		dev_err(dev, "ASoC: resume work item may be lost\n");

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_resume);

static void soc_resume_init(struct snd_soc_card *card)
{
	/* deferred resume work */
	INIT_WORK(&card->deferred_resume_work, soc_resume_deferred);
}
#else
#define snd_soc_suspend NULL
#define snd_soc_resume NULL
static inline void soc_resume_init(struct snd_soc_card *card)
{
}
#endif

static struct device_node
*soc_component_to_node(struct snd_soc_component *component)
{
	struct device_node *of_node;

	of_node = component->dev->of_node;
	if (!of_node && component->dev->parent)
		of_node = component->dev->parent->of_node;

	return of_node;
}

static int snd_soc_is_matching_component(
	const struct snd_soc_dai_link_component *dlc,
	struct snd_soc_component *component)
{
	struct device_node *component_of_node;

	if (!dlc)
		return 0;

	component_of_node = soc_component_to_node(component);

	if (dlc->of_node && component_of_node != dlc->of_node)
		return 0;
	if (dlc->name && strcmp(component->name, dlc->name))
		return 0;

	return 1;
}

static struct snd_soc_component *soc_find_component(
	const struct snd_soc_dai_link_component *dlc)
{
	struct snd_soc_component *component;

	lockdep_assert_held(&client_mutex);

	/*
	 * NOTE
	 *
	 * It returns *1st* found component, but some driver
	 * has few components by same of_node/name
	 * ex)
	 *	CPU component and generic DMAEngine component
	 */
	for_each_component(component)
		if (snd_soc_is_matching_component(dlc, component))
			return component;

	return NULL;
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

struct snd_soc_dai *snd_soc_find_dai_with_mutex(
	const struct snd_soc_dai_link_component *dlc)
{
	struct snd_soc_dai *dai;

	mutex_lock(&client_mutex);
	dai = snd_soc_find_dai(dlc);
	mutex_unlock(&client_mutex);

	return dai;
}
EXPORT_SYMBOL_GPL(snd_soc_find_dai_with_mutex);

static int soc_dai_link_sanity_check(struct snd_soc_card *card,
				     struct snd_soc_dai_link *link)
{
	int i;
	struct snd_soc_dai_link_component *cpu, *codec, *platform;

	for_each_link_codecs(link, i, codec) {
		/*
		 * Codec must be specified by 1 of name or OF node,
		 * not both or neither.
		 */
		if (!!codec->name == !!codec->of_node) {
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

		/*
		 * Defer card registration if codec component is not added to
		 * component list.
		 */
		if (!soc_find_component(codec)) {
			dev_dbg(card->dev,
				"ASoC: codec component %s not found for link %s\n",
				codec->name, link->name);
			return -EPROBE_DEFER;
		}
	}

	for_each_link_platforms(link, i, platform) {
		/*
		 * Platform may be specified by either name or OF node, but it
		 * can be left unspecified, then no components will be inserted
		 * in the rtdcom list
		 */
		if (!!platform->name == !!platform->of_node) {
			dev_err(card->dev,
				"ASoC: Neither/both platform name/of_node are set for %s\n",
				link->name);
			return -EINVAL;
		}

		/*
		 * Defer card registration if platform component is not added to
		 * component list.
		 */
		if (!soc_find_component(platform)) {
			dev_dbg(card->dev,
				"ASoC: platform component %s not found for link %s\n",
				platform->name, link->name);
			return -EPROBE_DEFER;
		}
	}

	for_each_link_cpus(link, i, cpu) {
		/*
		 * CPU device may be specified by either name or OF node, but
		 * can be left unspecified, and will be matched based on DAI
		 * name alone..
		 */
		if (cpu->name && cpu->of_node) {
			dev_err(card->dev,
				"ASoC: Neither/both cpu name/of_node are set for %s\n",
				link->name);
			return -EINVAL;
		}

		/*
		 * Defer card registration if cpu dai component is not added to
		 * component list.
		 */
		if ((cpu->of_node || cpu->name) &&
		    !soc_find_component(cpu)) {
			dev_dbg(card->dev,
				"ASoC: cpu component %s not found for link %s\n",
				cpu->name, link->name);
			return -EPROBE_DEFER;
		}

		/*
		 * At least one of CPU DAI name or CPU device name/node must be
		 * specified
		 */
		if (!cpu->dai_name &&
		    !(cpu->name || cpu->of_node)) {
			dev_err(card->dev,
				"ASoC: Neither cpu_dai_name nor cpu_name/of_node are set for %s\n",
				link->name);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * snd_soc_remove_pcm_runtime - Remove a pcm_runtime from card
 * @card: The ASoC card to which the pcm_runtime has
 * @rtd: The pcm_runtime to remove
 *
 * This function removes a pcm_runtime from the ASoC card.
 */
void snd_soc_remove_pcm_runtime(struct snd_soc_card *card,
				struct snd_soc_pcm_runtime *rtd)
{
	lockdep_assert_held(&client_mutex);

	/* release machine specific resources */
	snd_soc_link_exit(rtd);

	/*
	 * Notify the machine driver for extra destruction
	 */
	snd_soc_card_remove_dai_link(card, rtd->dai_link);

	soc_free_pcm_runtime(rtd);
}
EXPORT_SYMBOL_GPL(snd_soc_remove_pcm_runtime);

/**
 * snd_soc_add_pcm_runtime - Add a pcm_runtime dynamically via dai_link
 * @card: The ASoC card to which the pcm_runtime is added
 * @dai_link: The DAI link to find pcm_runtime
 *
 * This function adds a pcm_runtime ASoC card by using dai_link.
 *
 * Note: Topology can use this API to add pcm_runtime when probing the
 * topology component. And machine drivers can still define static
 * DAI links in dai_link array.
 */
int snd_soc_add_pcm_runtime(struct snd_soc_card *card,
			    struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai_link_component *codec, *platform, *cpu;
	struct snd_soc_component *component;
	int i, ret;

	lockdep_assert_held(&client_mutex);

	/*
	 * Notify the machine driver for extra initialization
	 */
	ret = snd_soc_card_add_dai_link(card, dai_link);
	if (ret < 0)
		return ret;

	if (dai_link->ignore)
		return 0;

	dev_dbg(card->dev, "ASoC: binding %s\n", dai_link->name);

	ret = soc_dai_link_sanity_check(card, dai_link);
	if (ret < 0)
		return ret;

	rtd = soc_new_pcm_runtime(card, dai_link);
	if (!rtd)
		return -ENOMEM;

	for_each_link_cpus(dai_link, i, cpu) {
		asoc_rtd_to_cpu(rtd, i) = snd_soc_find_dai(cpu);
		if (!asoc_rtd_to_cpu(rtd, i)) {
			dev_info(card->dev, "ASoC: CPU DAI %s not registered\n",
				 cpu->dai_name);
			goto _err_defer;
		}
		snd_soc_rtd_add_component(rtd, asoc_rtd_to_cpu(rtd, i)->component);
	}

	/* Find CODEC from registered CODECs */
	for_each_link_codecs(dai_link, i, codec) {
		asoc_rtd_to_codec(rtd, i) = snd_soc_find_dai(codec);
		if (!asoc_rtd_to_codec(rtd, i)) {
			dev_info(card->dev, "ASoC: CODEC DAI %s not registered\n",
				 codec->dai_name);
			goto _err_defer;
		}

		snd_soc_rtd_add_component(rtd, asoc_rtd_to_codec(rtd, i)->component);
	}

	/* Find PLATFORM from registered PLATFORMs */
	for_each_link_platforms(dai_link, i, platform) {
		for_each_component(component) {
			if (!snd_soc_is_matching_component(platform, component))
				continue;

			snd_soc_rtd_add_component(rtd, component);
		}
	}

	return 0;

_err_defer:
	snd_soc_remove_pcm_runtime(card, rtd);
	return -EPROBE_DEFER;
}
EXPORT_SYMBOL_GPL(snd_soc_add_pcm_runtime);

static void snd_soc_runtime_get_dai_fmt(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_dai *dai, *not_used;
	struct device *dev = rtd->dev;
	u64 pos, possible_fmt;
	unsigned int mask = 0, dai_fmt = 0;
	int i, j, priority, pri, until;

	/*
	 * Get selectable format from each DAIs.
	 *
	 ****************************
	 *            NOTE
	 * Using .auto_selectable_formats is not mandatory,
	 * we can select format manually from Sound Card.
	 * When use it, driver should list well tested format only.
	 ****************************
	 *
	 * ex)
	 *	auto_selectable_formats (= SND_SOC_POSSIBLE_xxx)
	 *		 (A)	 (B)	 (C)
	 *	DAI0_: { 0x000F, 0x00F0, 0x0F00 };
	 *	DAI1 : { 0xF000, 0x0F00 };
	 *		 (X)	 (Y)
	 *
	 * "until" will be 3 in this case (MAX array size from DAI0 and DAI1)
	 * Here is dev_dbg() message and comments
	 *
	 * priority = 1
	 * DAI0: (pri, fmt) = (1, 000000000000000F) // 1st check (A) DAI1 is not selected
	 * DAI1: (pri, fmt) = (0, 0000000000000000) //               Necessary Waste
	 * DAI0: (pri, fmt) = (1, 000000000000000F) // 2nd check (A)
	 * DAI1: (pri, fmt) = (1, 000000000000F000) //           (X)
	 * priority = 2
	 * DAI0: (pri, fmt) = (2, 00000000000000FF) // 3rd check (A) + (B)
	 * DAI1: (pri, fmt) = (1, 000000000000F000) //           (X)
	 * DAI0: (pri, fmt) = (2, 00000000000000FF) // 4th check (A) + (B)
	 * DAI1: (pri, fmt) = (2, 000000000000FF00) //           (X) + (Y)
	 * priority = 3
	 * DAI0: (pri, fmt) = (3, 0000000000000FFF) // 5th check (A) + (B) + (C)
	 * DAI1: (pri, fmt) = (2, 000000000000FF00) //           (X) + (Y)
	 * found auto selected format: 0000000000000F00
	 */
	until = snd_soc_dai_get_fmt_max_priority(rtd);
	for (priority = 1; priority <= until; priority++) {

		dev_dbg(dev, "priority = %d\n", priority);
		for_each_rtd_dais(rtd, j, not_used) {

			possible_fmt = ULLONG_MAX;
			for_each_rtd_dais(rtd, i, dai) {
				u64 fmt = 0;

				pri = (j >= i) ? priority : priority - 1;
				fmt = snd_soc_dai_get_fmt(dai, pri);
				dev_dbg(dev, "%s: (pri, fmt) = (%d, %016llX)\n", dai->name, pri, fmt);
				possible_fmt &= fmt;
			}
			if (possible_fmt)
				goto found;
		}
	}
	/* Not Found */
	return;
found:
	dev_dbg(dev, "found auto selected format: %016llX\n", possible_fmt);

	/*
	 * convert POSSIBLE_DAIFMT to DAIFMT
	 *
	 * Some basic/default settings on each is defined as 0.
	 * see
	 *	SND_SOC_DAIFMT_NB_NF
	 *	SND_SOC_DAIFMT_GATED
	 *
	 * SND_SOC_DAIFMT_xxx_MASK can't notice it if Sound Card specify
	 * these value, and will be overwrite to auto selected value.
	 *
	 * To avoid such issue, loop from 63 to 0 here.
	 * Small number of SND_SOC_POSSIBLE_xxx will be Hi priority.
	 * Basic/Default settings of each part and aboves are defined
	 * as Hi priority (= small number) of SND_SOC_POSSIBLE_xxx.
	 */
	for (i = 63; i >= 0; i--) {
		pos = 1ULL << i;
		switch (possible_fmt & pos) {
		/*
		 * for format
		 */
		case SND_SOC_POSSIBLE_DAIFMT_I2S:
		case SND_SOC_POSSIBLE_DAIFMT_RIGHT_J:
		case SND_SOC_POSSIBLE_DAIFMT_LEFT_J:
		case SND_SOC_POSSIBLE_DAIFMT_DSP_A:
		case SND_SOC_POSSIBLE_DAIFMT_DSP_B:
		case SND_SOC_POSSIBLE_DAIFMT_AC97:
		case SND_SOC_POSSIBLE_DAIFMT_PDM:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_FORMAT_MASK) | i;
			break;
		/*
		 * for clock
		 */
		case SND_SOC_POSSIBLE_DAIFMT_CONT:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_CLOCK_MASK) | SND_SOC_DAIFMT_CONT;
			break;
		case SND_SOC_POSSIBLE_DAIFMT_GATED:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_CLOCK_MASK) | SND_SOC_DAIFMT_GATED;
			break;
		/*
		 * for clock invert
		 */
		case SND_SOC_POSSIBLE_DAIFMT_NB_NF:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_INV_MASK) | SND_SOC_DAIFMT_NB_NF;
			break;
		case SND_SOC_POSSIBLE_DAIFMT_NB_IF:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_INV_MASK) | SND_SOC_DAIFMT_NB_IF;
			break;
		case SND_SOC_POSSIBLE_DAIFMT_IB_NF:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_INV_MASK) | SND_SOC_DAIFMT_IB_NF;
			break;
		case SND_SOC_POSSIBLE_DAIFMT_IB_IF:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_INV_MASK) | SND_SOC_DAIFMT_IB_IF;
			break;
		/*
		 * for clock provider / consumer
		 */
		case SND_SOC_POSSIBLE_DAIFMT_CBP_CFP:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) | SND_SOC_DAIFMT_CBP_CFP;
			break;
		case SND_SOC_POSSIBLE_DAIFMT_CBC_CFP:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) | SND_SOC_DAIFMT_CBC_CFP;
			break;
		case SND_SOC_POSSIBLE_DAIFMT_CBP_CFC:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) | SND_SOC_DAIFMT_CBP_CFC;
			break;
		case SND_SOC_POSSIBLE_DAIFMT_CBC_CFC:
			dai_fmt = (dai_fmt & ~SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) | SND_SOC_DAIFMT_CBC_CFC;
			break;
		}
	}

	/*
	 * Some driver might have very complex limitation.
	 * In such case, user want to auto-select non-limitation part,
	 * and want to manually specify complex part.
	 *
	 * Or for example, if both CPU and Codec can be clock provider,
	 * but because of its quality, user want to specify it manually.
	 *
	 * Use manually specified settings if sound card did.
	 */
	if (!(dai_link->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK))
		mask |= SND_SOC_DAIFMT_FORMAT_MASK;
	if (!(dai_link->dai_fmt & SND_SOC_DAIFMT_CLOCK_MASK))
		mask |= SND_SOC_DAIFMT_CLOCK_MASK;
	if (!(dai_link->dai_fmt & SND_SOC_DAIFMT_INV_MASK))
		mask |= SND_SOC_DAIFMT_INV_MASK;
	if (!(dai_link->dai_fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK))
		mask |= SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK;

	dai_link->dai_fmt |= (dai_fmt & mask);
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
	struct snd_soc_dai *cpu_dai;
	struct snd_soc_dai *codec_dai;
	unsigned int inv_dai_fmt;
	unsigned int i;
	int ret;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
		if (ret != 0 && ret != -ENOTSUPP)
			return ret;
	}

	/*
	 * Flip the polarity for the "CPU" end of a CODEC<->CODEC link
	 * the component which has non_legacy_dai_naming is Codec
	 */
	inv_dai_fmt = snd_soc_daifmt_clock_provider_fliped(dai_fmt);

	for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
		unsigned int fmt = dai_fmt;

		if (cpu_dai->component->driver->non_legacy_dai_naming)
			fmt = inv_dai_fmt;

		ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
		if (ret != 0 && ret != -ENOTSUPP)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_runtime_set_dai_fmt);

static int soc_init_pcm_runtime(struct snd_soc_card *card,
				struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_component *component;
	int ret, num, i;

	/* set default power off timeout */
	rtd->pmdown_time = pmdown_time;

	/* do machine specific initialization */
	ret = snd_soc_link_init(rtd);
	if (ret < 0)
		return ret;

	snd_soc_runtime_get_dai_fmt(rtd);
	if (dai_link->dai_fmt) {
		ret = snd_soc_runtime_set_dai_fmt(rtd, dai_link->dai_fmt);
		if (ret)
			return ret;
	}

	/* add DPCM sysfs entries */
	soc_dpcm_debugfs_add(rtd);

	num = rtd->num;

	/*
	 * most drivers will register their PCMs using DAI link ordering but
	 * topology based drivers can use the DAI link id field to set PCM
	 * device number and then use rtd + a base offset of the BEs.
	 */
	for_each_rtd_components(rtd, i, component) {
		if (!component->driver->use_dai_pcm_id)
			continue;

		if (rtd->dai_link->no_pcm)
			num += component->driver->be_pcm_base;
		else
			num = rtd->dai_link->id;
	}

	/* create compress_device if possible */
	ret = snd_soc_dai_compress_new(cpu_dai, rtd, num);
	if (ret != -ENOTSUPP)
		return ret;

	/* create the pcm */
	ret = soc_new_pcm(rtd, num);
	if (ret < 0) {
		dev_err(card->dev, "ASoC: can't create pcm %s :%d\n",
			dai_link->stream_name, ret);
		return ret;
	}

	return snd_soc_pcm_dai_new(rtd);
}

static void soc_set_name_prefix(struct snd_soc_card *card,
				struct snd_soc_component *component)
{
	struct device_node *of_node = soc_component_to_node(component);
	const char *str;
	int ret, i;

	for (i = 0; i < card->num_configs; i++) {
		struct snd_soc_codec_conf *map = &card->codec_conf[i];

		if (snd_soc_is_matching_component(&map->dlc, component) &&
		    map->name_prefix) {
			component->name_prefix = map->name_prefix;
			return;
		}
	}

	/*
	 * If there is no configuration table or no match in the table,
	 * check if a prefix is provided in the node
	 */
	ret = of_property_read_string(of_node, "sound-name-prefix", &str);
	if (ret < 0)
		return;

	component->name_prefix = str;
}

static void soc_remove_component(struct snd_soc_component *component,
				 int probed)
{

	if (!component->card)
		return;

	if (probed)
		snd_soc_component_remove(component);

	/* For framework level robustness */
	snd_soc_component_set_jack(component, NULL, NULL);

	list_del_init(&component->card_list);
	snd_soc_dapm_free(snd_soc_component_get_dapm(component));
	soc_cleanup_component_debugfs(component);
	component->card = NULL;
	snd_soc_component_module_put_when_remove(component);
}

static int soc_probe_component(struct snd_soc_card *card,
			       struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct snd_soc_dai *dai;
	int probed = 0;
	int ret;

	if (snd_soc_component_is_dummy(component))
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

	ret = snd_soc_component_module_get_when_probe(component);
	if (ret < 0)
		return ret;

	component->card = card;
	soc_set_name_prefix(card, component);

	soc_init_component_debugfs(component);

	snd_soc_dapm_init(dapm, card, component);

	ret = snd_soc_dapm_new_controls(dapm,
					component->driver->dapm_widgets,
					component->driver->num_dapm_widgets);

	if (ret != 0) {
		dev_err(component->dev,
			"Failed to create new controls %d\n", ret);
		goto err_probe;
	}

	for_each_component_dais(component, dai) {
		ret = snd_soc_dapm_new_dai_widgets(dapm, dai);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to create DAI widgets %d\n", ret);
			goto err_probe;
		}
	}

	ret = snd_soc_component_probe(component);
	if (ret < 0)
		goto err_probe;

	WARN(dapm->idle_bias_off &&
	     dapm->bias_level != SND_SOC_BIAS_OFF,
	     "codec %s can not start from non-off bias with idle_bias_off==1\n",
	     component->name);
	probed = 1;

	/*
	 * machine specific init
	 * see
	 *	snd_soc_component_set_aux()
	 */
	ret = snd_soc_component_init(component);
	if (ret < 0)
		goto err_probe;

	ret = snd_soc_add_component_controls(component,
					     component->driver->controls,
					     component->driver->num_controls);
	if (ret < 0)
		goto err_probe;

	ret = snd_soc_dapm_add_routes(dapm,
				      component->driver->dapm_routes,
				      component->driver->num_dapm_routes);
	if (ret < 0) {
		if (card->disable_route_checks) {
			dev_info(card->dev,
				 "%s: disable_route_checks set, ignoring errors on add_routes\n",
				 __func__);
		} else {
			dev_err(card->dev,
				"%s: snd_soc_dapm_add_routes failed: %d\n",
				__func__, ret);
			goto err_probe;
		}
	}

	/* see for_each_card_components */
	list_add(&component->card_list, &card->component_dev_list);

err_probe:
	if (ret < 0)
		soc_remove_component(component, probed);

	return ret;
}

static void soc_remove_link_dais(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	int order;

	for_each_comp_order(order) {
		for_each_card_rtds(card, rtd) {
			/* remove all rtd connected DAIs in good order */
			snd_soc_pcm_dai_remove(rtd, order);
		}
	}
}

static int soc_probe_link_dais(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	int order, ret;

	for_each_comp_order(order) {
		for_each_card_rtds(card, rtd) {

			dev_dbg(card->dev,
				"ASoC: probe %s dai link %d late %d\n",
				card->name, rtd->num, order);

			/* probe all rtd connected DAIs in good order */
			ret = snd_soc_pcm_dai_probe(rtd, order);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void soc_remove_link_components(struct snd_soc_card *card)
{
	struct snd_soc_component *component;
	struct snd_soc_pcm_runtime *rtd;
	int i, order;

	for_each_comp_order(order) {
		for_each_card_rtds(card, rtd) {
			for_each_rtd_components(rtd, i, component) {
				if (component->driver->remove_order != order)
					continue;

				soc_remove_component(component, 1);
			}
		}
	}
}

static int soc_probe_link_components(struct snd_soc_card *card)
{
	struct snd_soc_component *component;
	struct snd_soc_pcm_runtime *rtd;
	int i, ret, order;

	for_each_comp_order(order) {
		for_each_card_rtds(card, rtd) {
			for_each_rtd_components(rtd, i, component) {
				if (component->driver->probe_order != order)
					continue;

				ret = soc_probe_component(card, component);
				if (ret < 0)
					return ret;
			}
		}
	}

	return 0;
}

static void soc_unbind_aux_dev(struct snd_soc_card *card)
{
	struct snd_soc_component *component, *_component;

	for_each_card_auxs_safe(card, component, _component) {
		/* for snd_soc_component_init() */
		snd_soc_component_set_aux(component, NULL);
		list_del(&component->card_aux_list);
	}
}

static int soc_bind_aux_dev(struct snd_soc_card *card)
{
	struct snd_soc_component *component;
	struct snd_soc_aux_dev *aux;
	int i;

	for_each_card_pre_auxs(card, i, aux) {
		/* codecs, usually analog devices */
		component = soc_find_component(&aux->dlc);
		if (!component)
			return -EPROBE_DEFER;

		/* for snd_soc_component_init() */
		snd_soc_component_set_aux(component, aux);
		/* see for_each_card_auxs */
		list_add(&component->card_aux_list, &card->aux_comp_list);
	}
	return 0;
}

static int soc_probe_aux_devices(struct snd_soc_card *card)
{
	struct snd_soc_component *component;
	int order;
	int ret;

	for_each_comp_order(order) {
		for_each_card_auxs(card, component) {
			if (component->driver->probe_order != order)
				continue;

			ret = soc_probe_component(card,	component);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static void soc_remove_aux_devices(struct snd_soc_card *card)
{
	struct snd_soc_component *comp, *_comp;
	int order;

	for_each_comp_order(order) {
		for_each_card_auxs_safe(card, comp, _comp) {
			if (comp->driver->remove_order == order)
				soc_remove_component(comp, 1);
		}
	}
}

#ifdef CONFIG_DMI
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

/*
 * Append a string to card->dmi_longname with character cleanups.
 */
static void append_dmi_string(struct snd_soc_card *card, const char *str)
{
	char *dst = card->dmi_longname;
	size_t dst_len = sizeof(card->dmi_longname);
	size_t len;

	len = strlen(dst);
	snprintf(dst + len, dst_len - len, "-%s", str);

	len++;	/* skip the separator "-" */
	if (len < dst_len)
		cleanup_dmi_name(dst + len);
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
	const char *vendor, *product, *board;

	if (card->long_name)
		return 0; /* long name already set by driver or from DMI */

	if (!dmi_available)
		return 0;

	/* make up dmi long name as: vendor-product-version-board */
	vendor = dmi_get_system_info(DMI_BOARD_VENDOR);
	if (!vendor || !is_dmi_valid(vendor)) {
		dev_warn(card->dev, "ASoC: no DMI vendor name!\n");
		return 0;
	}

	snprintf(card->dmi_longname, sizeof(card->dmi_longname), "%s", vendor);
	cleanup_dmi_name(card->dmi_longname);

	product = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (product && is_dmi_valid(product)) {
		const char *product_version = dmi_get_system_info(DMI_PRODUCT_VERSION);

		append_dmi_string(card, product);

		/*
		 * some vendors like Lenovo may only put a self-explanatory
		 * name in the product version field
		 */
		if (product_version && is_dmi_valid(product_version))
			append_dmi_string(card, product_version);
	}

	board = dmi_get_system_info(DMI_BOARD_NAME);
	if (board && is_dmi_valid(board)) {
		if (!product || strcasecmp(board, product))
			append_dmi_string(card, board);
	} else if (!product) {
		/* fall back to using legacy name */
		dev_warn(card->dev, "ASoC: no DMI board/product name!\n");
		return 0;
	}

	/* Add flavour to dmi long name */
	if (flavour)
		append_dmi_string(card, flavour);

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

		/* does this component override BEs ? */
		if (!component->driver->ignore_machine)
			continue;

		/* for this machine ? */
		if (!strcmp(component->driver->ignore_machine,
			    card->dev->driver->name))
			goto match;
		if (strcmp(component->driver->ignore_machine,
			   dev_name(card->dev)))
			continue;
match:
		/* machine matches, so override the rtd data */
		for_each_card_prelinks(card, i, dai_link) {

			/* ignore this FE */
			if (dai_link->dynamic) {
				dai_link->ignore = true;
				continue;
			}

			dev_dbg(card->dev, "info: override BE DAI link %s\n",
				card->dai_link[i].name);

			/* override platform component */
			if (!dai_link->platforms) {
				dev_err(card->dev, "init platform error");
				continue;
			}

			if (component->dev->of_node)
				dai_link->platforms->of_node = component->dev->of_node;
			else
				dai_link->platforms->name = component->name;

			/* convert non BE into BE */
			if (!dai_link->no_pcm) {
				dai_link->no_pcm = 1;

				if (dai_link->dpcm_playback)
					dev_warn(card->dev,
						 "invalid configuration, dailink %s has flags no_pcm=0 and dpcm_playback=1\n",
						 dai_link->name);
				if (dai_link->dpcm_capture)
					dev_warn(card->dev,
						 "invalid configuration, dailink %s has flags no_pcm=0 and dpcm_capture=1\n",
						 dai_link->name);

				/* convert normal link into DPCM one */
				if (!(dai_link->dpcm_playback ||
				      dai_link->dpcm_capture)) {
					dai_link->dpcm_playback = !dai_link->capture_only;
					dai_link->dpcm_capture = !dai_link->playback_only;
				}
			}

			/*
			 * override any BE fixups
			 * see
			 *	snd_soc_link_be_hw_params_fixup()
			 */
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

#define soc_setup_card_name(name, name1, name2, norm)		\
	__soc_setup_card_name(name, sizeof(name), name1, name2, norm)
static void __soc_setup_card_name(char *name, int len,
				  const char *name1, const char *name2,
				  int normalization)
{
	int i;

	snprintf(name, len, "%s", name1 ? name1 : name2);

	if (!normalization)
		return;

	/*
	 * Name normalization
	 *
	 * The driver name is somewhat special, as it's used as a key for
	 * searches in the user-space.
	 *
	 * ex)
	 *	"abcd??efg" -> "abcd__efg"
	 */
	for (i = 0; i < len; i++) {
		switch (name[i]) {
		case '_':
		case '-':
		case '\0':
			break;
		default:
			if (!isalnum(name[i]))
				name[i] = '_';
			break;
		}
	}
}

static void soc_cleanup_card_resources(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd, *n;

	if (card->snd_card)
		snd_card_disconnect_sync(card->snd_card);

	snd_soc_dapm_shutdown(card);

	/* remove and free each DAI */
	soc_remove_link_dais(card);
	soc_remove_link_components(card);

	for_each_card_rtds_safe(card, rtd, n)
		snd_soc_remove_pcm_runtime(card, rtd);

	/* remove auxiliary devices */
	soc_remove_aux_devices(card);
	soc_unbind_aux_dev(card);

	snd_soc_dapm_free(&card->dapm);
	soc_cleanup_card_debugfs(card);

	/* remove the card */
	snd_soc_card_remove(card);

	if (card->snd_card) {
		snd_card_free(card->snd_card);
		card->snd_card = NULL;
	}
}

static void snd_soc_unbind_card(struct snd_soc_card *card, bool unregister)
{
	if (card->instantiated) {
		card->instantiated = false;
		snd_soc_flush_all_delayed_work(card);

		soc_cleanup_card_resources(card);
		if (!unregister)
			list_add(&card->list, &unbind_card_list);
	} else {
		if (unregister)
			list_del(&card->list);
	}
}

static int snd_soc_bind_card(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_component *component;
	struct snd_soc_dai_link *dai_link;
	int ret, i;

	mutex_lock(&client_mutex);
	mutex_lock_nested(&card->mutex, SND_SOC_CARD_CLASS_INIT);

	snd_soc_dapm_init(&card->dapm, card, NULL);

	/* check whether any platform is ignore machine FE and using topology */
	soc_check_tplg_fes(card);

	/* bind aux_devs too */
	ret = soc_bind_aux_dev(card);
	if (ret < 0)
		goto probe_end;

	/* add predefined DAI links to the list */
	card->num_rtd = 0;
	for_each_card_prelinks(card, i, dai_link) {
		ret = snd_soc_add_pcm_runtime(card, dai_link);
		if (ret < 0)
			goto probe_end;
	}

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

	soc_resume_init(card);

	ret = snd_soc_dapm_new_controls(&card->dapm, card->dapm_widgets,
					card->num_dapm_widgets);
	if (ret < 0)
		goto probe_end;

	ret = snd_soc_dapm_new_controls(&card->dapm, card->of_dapm_widgets,
					card->num_of_dapm_widgets);
	if (ret < 0)
		goto probe_end;

	/* initialise the sound card only once */
	ret = snd_soc_card_probe(card);
	if (ret < 0)
		goto probe_end;

	/* probe all components used by DAI links on this card */
	ret = soc_probe_link_components(card);
	if (ret < 0) {
		dev_err(card->dev,
			"ASoC: failed to instantiate card %d\n", ret);
		goto probe_end;
	}

	/* probe auxiliary components */
	ret = soc_probe_aux_devices(card);
	if (ret < 0) {
		dev_err(card->dev,
			"ASoC: failed to probe aux component %d\n", ret);
		goto probe_end;
	}

	/* probe all DAI links on this card */
	ret = soc_probe_link_dais(card);
	if (ret < 0) {
		dev_err(card->dev,
			"ASoC: failed to instantiate card %d\n", ret);
		goto probe_end;
	}

	for_each_card_rtds(card, rtd) {
		ret = soc_init_pcm_runtime(card, rtd);
		if (ret < 0)
			goto probe_end;
	}

	snd_soc_dapm_link_dai_widgets(card);
	snd_soc_dapm_connect_dai_link_widgets(card);

	ret = snd_soc_add_card_controls(card, card->controls,
					card->num_controls);
	if (ret < 0)
		goto probe_end;

	ret = snd_soc_dapm_add_routes(&card->dapm, card->dapm_routes,
				      card->num_dapm_routes);
	if (ret < 0) {
		if (card->disable_route_checks) {
			dev_info(card->dev,
				 "%s: disable_route_checks set, ignoring errors on add_routes\n",
				 __func__);
		} else {
			dev_err(card->dev,
				 "%s: snd_soc_dapm_add_routes failed: %d\n",
				 __func__, ret);
			goto probe_end;
		}
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, card->of_dapm_routes,
				      card->num_of_dapm_routes);
	if (ret < 0)
		goto probe_end;

	/* try to set some sane longname if DMI is available */
	snd_soc_set_dmi_name(card, NULL);

	soc_setup_card_name(card->snd_card->shortname,
			    card->name, NULL, 0);
	soc_setup_card_name(card->snd_card->longname,
			    card->long_name, card->name, 0);
	soc_setup_card_name(card->snd_card->driver,
			    card->driver_name, card->name, 1);

	if (card->components) {
		/* the current implementation of snd_component_add() accepts */
		/* multiple components in the string separated by space, */
		/* but the string collision (identical string) check might */
		/* not work correctly */
		ret = snd_component_add(card->snd_card, card->components);
		if (ret < 0) {
			dev_err(card->dev, "ASoC: %s snd_component_add() failed: %d\n",
				card->name, ret);
			goto probe_end;
		}
	}

	ret = snd_soc_card_late_probe(card);
	if (ret < 0)
		goto probe_end;

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

	/* deactivate pins to sleep state */
	for_each_card_components(card, component)
		if (!snd_soc_component_active(component))
			pinctrl_pm_select_sleep_state(component->dev);

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

	return devm_snd_soc_register_card(&pdev->dev, card);
}

int snd_soc_poweroff(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct snd_soc_component *component;

	if (!card->instantiated)
		return 0;

	/*
	 * Flush out pmdown_time work - we actually do want to run it
	 * now, we're shutting down so no imminent restart.
	 */
	snd_soc_flush_all_delayed_work(card);

	snd_soc_dapm_shutdown(card);

	/* deactivate pins to sleep state */
	for_each_card_components(card, component)
		pinctrl_pm_select_sleep_state(component->dev);

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
	int i;

	for (i = 0; i < num_controls; i++) {
		const struct snd_kcontrol_new *control = &controls[i];
		int err = snd_ctl_add(card, snd_soc_cnew(control, data,
							 control->name, prefix));
		if (err < 0) {
			dev_err(dev, "ASoC: Failed to add %s: %d\n",
				control->name, err);
			return err;
		}
	}

	return 0;
}

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
 * snd_soc_register_card - Register a card with the ASoC core
 *
 * @card: Card to register
 *
 */
int snd_soc_register_card(struct snd_soc_card *card)
{
	if (!card->name || !card->dev)
		return -EINVAL;

	dev_set_drvdata(card->dev, card);

	INIT_LIST_HEAD(&card->widgets);
	INIT_LIST_HEAD(&card->paths);
	INIT_LIST_HEAD(&card->dapm_list);
	INIT_LIST_HEAD(&card->aux_comp_list);
	INIT_LIST_HEAD(&card->component_dev_list);
	INIT_LIST_HEAD(&card->list);
	INIT_LIST_HEAD(&card->rtd_list);
	INIT_LIST_HEAD(&card->dapm_dirty);
	INIT_LIST_HEAD(&card->dobj_list);

	card->instantiated = 0;
	mutex_init(&card->mutex);
	mutex_init(&card->dapm_mutex);
	mutex_init(&card->pcm_mutex);
	spin_lock_init(&card->dpcm_lock);

	return snd_soc_bind_card(card);
}
EXPORT_SYMBOL_GPL(snd_soc_register_card);

/**
 * snd_soc_unregister_card - Unregister a card with the ASoC core
 *
 * @card: Card to unregister
 *
 */
int snd_soc_unregister_card(struct snd_soc_card *card)
{
	mutex_lock(&client_mutex);
	snd_soc_unbind_card(card, true);
	mutex_unlock(&client_mutex);
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
	const char *devname = dev_name(dev);
	char *found, *name;
	unsigned int id1, id2;

	if (devname == NULL)
		return NULL;

	name = devm_kstrdup(dev, devname, GFP_KERNEL);
	if (!name)
		return NULL;

	/* are we a "%s.%d" name (platform and SPI components) */
	found = strstr(name, dev->driver->name);
	if (found) {
		/* get ID */
		if (sscanf(&found[strlen(dev->driver->name)], ".%d", id) == 1) {

			/* discard ID from name if ID == -1 */
			if (*id == -1)
				found[strlen(dev->driver->name)] = '\0';
		}

	/* I2C component devices are named "bus-addr" */
	} else if (sscanf(name, "%x-%x", &id1, &id2) == 2) {

		/* create unique ID number from I2C addr and bus */
		*id = ((id1 & 0xffff) << 16) + id2;

		devm_kfree(dev, name);

		/* sanitize component name for DAI link creation */
		name = devm_kasprintf(dev, GFP_KERNEL, "%s.%s", dev->driver->name, devname);
	} else {
		*id = 0;
	}

	return name;
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

	return devm_kstrdup(dev, dai_drv->name, GFP_KERNEL);
}

void snd_soc_unregister_dai(struct snd_soc_dai *dai)
{
	dev_dbg(dai->dev, "ASoC: Unregistered DAI '%s'\n", dai->name);
	list_del(&dai->list);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_dai);

/**
 * snd_soc_register_dai - Register a DAI dynamically & create its widgets
 *
 * @component: The component the DAIs are registered for
 * @dai_drv: DAI driver to use for the DAI
 * @legacy_dai_naming: if %true, use legacy single-name format;
 * 	if %false, use multiple-name format;
 *
 * Topology can use this API to register DAIs when probing a component.
 * These DAIs's widgets will be freed in the card cleanup and the DAIs
 * will be freed in the component cleanup.
 */
struct snd_soc_dai *snd_soc_register_dai(struct snd_soc_component *component,
					 struct snd_soc_dai_driver *dai_drv,
					 bool legacy_dai_naming)
{
	struct device *dev = component->dev;
	struct snd_soc_dai *dai;

	dev_dbg(dev, "ASoC: dynamically register DAI %s\n", dev_name(dev));

	lockdep_assert_held(&client_mutex);

	dai = devm_kzalloc(dev, sizeof(*dai), GFP_KERNEL);
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
	if (!dai->name)
		return NULL;

	dai->component = component;
	dai->dev = dev;
	dai->driver = dai_drv;

	/* see for_each_component_dais */
	list_add_tail(&dai->list, &component->dai_list);
	component->num_dai++;

	dev_dbg(dev, "ASoC: Registered DAI '%s'\n", dai->name);
	return dai;
}

/**
 * snd_soc_unregister_dais - Unregister DAIs from the ASoC core
 *
 * @component: The component for which the DAIs should be unregistered
 */
static void snd_soc_unregister_dais(struct snd_soc_component *component)
{
	struct snd_soc_dai *dai, *_dai;

	for_each_component_dais_safe(component, dai, _dai)
		snd_soc_unregister_dai(dai);
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
	struct snd_soc_dai *dai;
	unsigned int i;
	int ret;

	for (i = 0; i < count; i++) {
		dai = snd_soc_register_dai(component, dai_drv + i, count == 1 &&
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

	list_for_each_entry_safe(card, c, &unbind_card_list, list)
		if (!snd_soc_bind_card(card))
			list_del(&card->list);
}

static void snd_soc_del_component_unlocked(struct snd_soc_component *component)
{
	struct snd_soc_card *card = component->card;

	snd_soc_unregister_dais(component);

	if (card)
		snd_soc_unbind_card(card, false);

	list_del(&component->list);
}

int snd_soc_component_initialize(struct snd_soc_component *component,
				 const struct snd_soc_component_driver *driver,
				 struct device *dev)
{
	INIT_LIST_HEAD(&component->dai_list);
	INIT_LIST_HEAD(&component->dobj_list);
	INIT_LIST_HEAD(&component->card_list);
	mutex_init(&component->io_mutex);

	component->name = fmt_single_name(dev, &component->id);
	if (!component->name) {
		dev_err(dev, "ASoC: Failed to allocate name\n");
		return -ENOMEM;
	}

	component->dev		= dev;
	component->driver	= driver;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_component_initialize);

int snd_soc_add_component(struct snd_soc_component *component,
			  struct snd_soc_dai_driver *dai_drv,
			  int num_dai)
{
	int ret;
	int i;

	mutex_lock(&client_mutex);

	if (component->driver->endianness) {
		for (i = 0; i < num_dai; i++) {
			convert_endianness_formats(&dai_drv[i].playback);
			convert_endianness_formats(&dai_drv[i].capture);
		}
	}

	ret = snd_soc_register_dais(component, dai_drv, num_dai);
	if (ret < 0) {
		dev_err(component->dev, "ASoC: Failed to register DAIs: %d\n",
			ret);
		goto err_cleanup;
	}

	if (!component->driver->write && !component->driver->read) {
		if (!component->regmap)
			component->regmap = dev_get_regmap(component->dev,
							   NULL);
		if (component->regmap)
			snd_soc_component_setup_regmap(component);
	}

	/* see for_each_component */
	list_add(&component->list, &component_list);

err_cleanup:
	if (ret < 0)
		snd_soc_del_component_unlocked(component);

	mutex_unlock(&client_mutex);

	if (ret == 0)
		snd_soc_try_rebind_card();

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_add_component);

int snd_soc_register_component(struct device *dev,
			const struct snd_soc_component_driver *component_driver,
			struct snd_soc_dai_driver *dai_drv,
			int num_dai)
{
	struct snd_soc_component *component;
	int ret;

	component = devm_kzalloc(dev, sizeof(*component), GFP_KERNEL);
	if (!component)
		return -ENOMEM;

	ret = snd_soc_component_initialize(component, component_driver, dev);
	if (ret < 0)
		return ret;

	return snd_soc_add_component(component, dai_drv, num_dai);
}
EXPORT_SYMBOL_GPL(snd_soc_register_component);

/**
 * snd_soc_unregister_component_by_driver - Unregister component using a given driver
 * from the ASoC core
 *
 * @dev: The device to unregister
 * @component_driver: The component driver to unregister
 */
void snd_soc_unregister_component_by_driver(struct device *dev,
					    const struct snd_soc_component_driver *component_driver)
{
	struct snd_soc_component *component;

	if (!component_driver)
		return;

	mutex_lock(&client_mutex);
	component = snd_soc_lookup_component_nolocked(dev, component_driver->name);
	if (!component)
		goto out;

	snd_soc_del_component_unlocked(component);

out:
	mutex_unlock(&client_mutex);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_component_by_driver);

/**
 * snd_soc_unregister_component - Unregister all related component
 * from the ASoC core
 *
 * @dev: The device to unregister
 */
void snd_soc_unregister_component(struct device *dev)
{
	mutex_lock(&client_mutex);
	while (1) {
		struct snd_soc_component *component = snd_soc_lookup_component_nolocked(dev, NULL);

		if (!component)
			break;

		snd_soc_del_component_unlocked(component);
	}
	mutex_unlock(&client_mutex);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_component);

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
	int i, j, num_widgets;

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
		int ret = of_property_read_string_index(np, propname,
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

	codec_conf->dlc.of_node	= of_node;
	codec_conf->name_prefix	= str;
}
EXPORT_SYMBOL_GPL(snd_soc_of_parse_node_prefix);

int snd_soc_of_parse_audio_routing(struct snd_soc_card *card,
				   const char *propname)
{
	struct device_node *np = card->dev->of_node;
	int num_routes;
	struct snd_soc_dapm_route *routes;
	int i;

	num_routes = of_property_count_strings(np, propname);
	if (num_routes < 0 || num_routes & 1) {
		dev_err(card->dev,
			"ASoC: Property '%s' does not exist or its length is not even\n",
			propname);
		return -EINVAL;
	}
	num_routes /= 2;

	routes = devm_kcalloc(card->dev, num_routes, sizeof(*routes),
			      GFP_KERNEL);
	if (!routes) {
		dev_err(card->dev,
			"ASoC: Could not allocate DAPM route table\n");
		return -ENOMEM;
	}

	for (i = 0; i < num_routes; i++) {
		int ret = of_property_read_string_index(np, propname,
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

int snd_soc_of_parse_aux_devs(struct snd_soc_card *card, const char *propname)
{
	struct device_node *node = card->dev->of_node;
	struct snd_soc_aux_dev *aux;
	int num, i;

	num = of_count_phandle_with_args(node, propname, NULL);
	if (num == -ENOENT) {
		return 0;
	} else if (num < 0) {
		dev_err(card->dev, "ASOC: Property '%s' could not be read: %d\n",
			propname, num);
		return num;
	}

	aux = devm_kcalloc(card->dev, num, sizeof(*aux), GFP_KERNEL);
	if (!aux)
		return -ENOMEM;
	card->aux_dev = aux;
	card->num_aux_devs = num;

	for_each_card_pre_auxs(card, i, aux) {
		aux->dlc.of_node = of_parse_phandle(node, propname, i);
		if (!aux->dlc.of_node)
			return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_of_parse_aux_devs);

unsigned int snd_soc_daifmt_clock_provider_fliped(unsigned int dai_fmt)
{
	unsigned int inv_dai_fmt = dai_fmt & ~SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK;

	switch (dai_fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		inv_dai_fmt |= SND_SOC_DAIFMT_CBC_CFC;
		break;
	case SND_SOC_DAIFMT_CBP_CFC:
		inv_dai_fmt |= SND_SOC_DAIFMT_CBC_CFP;
		break;
	case SND_SOC_DAIFMT_CBC_CFP:
		inv_dai_fmt |= SND_SOC_DAIFMT_CBP_CFC;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		inv_dai_fmt |= SND_SOC_DAIFMT_CBP_CFP;
		break;
	}

	return inv_dai_fmt;
}
EXPORT_SYMBOL_GPL(snd_soc_daifmt_clock_provider_fliped);

unsigned int snd_soc_daifmt_clock_provider_from_bitmap(unsigned int bit_frame)
{
	/*
	 * bit_frame is return value from
	 *	snd_soc_daifmt_parse_clock_provider_raw()
	 */

	/* Codec base */
	switch (bit_frame) {
	case 0x11:
		return SND_SOC_DAIFMT_CBP_CFP;
	case 0x10:
		return SND_SOC_DAIFMT_CBP_CFC;
	case 0x01:
		return SND_SOC_DAIFMT_CBC_CFP;
	default:
		return SND_SOC_DAIFMT_CBC_CFC;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_daifmt_clock_provider_from_bitmap);

unsigned int snd_soc_daifmt_parse_format(struct device_node *np,
					 const char *prefix)
{
	int ret;
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
		int i;

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

	return format;
}
EXPORT_SYMBOL_GPL(snd_soc_daifmt_parse_format);

unsigned int snd_soc_daifmt_parse_clock_provider_raw(struct device_node *np,
						     const char *prefix,
						     struct device_node **bitclkmaster,
						     struct device_node **framemaster)
{
	char prop[128];
	unsigned int bit, frame;

	if (!prefix)
		prefix = "";

	/*
	 * check "[prefix]bitclock-master"
	 * check "[prefix]frame-master"
	 */
	snprintf(prop, sizeof(prop), "%sbitclock-master", prefix);
	bit = !!of_get_property(np, prop, NULL);
	if (bit && bitclkmaster)
		*bitclkmaster = of_parse_phandle(np, prop, 0);

	snprintf(prop, sizeof(prop), "%sframe-master", prefix);
	frame = !!of_get_property(np, prop, NULL);
	if (frame && framemaster)
		*framemaster = of_parse_phandle(np, prop, 0);

	/*
	 * return bitmap.
	 * It will be parameter of
	 *	snd_soc_daifmt_clock_provider_from_bitmap()
	 */
	return (bit << 4) + frame;
}
EXPORT_SYMBOL_GPL(snd_soc_daifmt_parse_clock_provider_raw);

int snd_soc_get_dai_id(struct device_node *ep)
{
	struct snd_soc_component *component;
	struct snd_soc_dai_link_component dlc;
	int ret;

	dlc.of_node	= of_graph_get_port_parent(ep);
	dlc.name	= NULL;
	/*
	 * For example HDMI case, HDMI has video/sound port,
	 * but ALSA SoC needs sound port number only.
	 * Thus counting HDMI DT port/endpoint doesn't work.
	 * Then, it should have .of_xlate_dai_id
	 */
	ret = -ENOTSUPP;
	mutex_lock(&client_mutex);
	component = soc_find_component(&dlc);
	if (component)
		ret = snd_soc_component_of_xlate_dai_id(component, ep);
	mutex_unlock(&client_mutex);

	of_node_put(dlc.of_node);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_get_dai_id);

/**
 * snd_soc_info_multi_ext - external single mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a single external mixer control.
 * that accepts multiple input.
 *
 * Returns 0 for success.
 */
int snd_soc_info_multi_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_multi_mixer_control *mc =
		(struct soc_multi_mixer_control *)kcontrol->private_value;
	int platform_max;

	if (!mc->platform_max)
		mc->platform_max = mc->max;
	platform_max = mc->platform_max;

	if (platform_max == 1 && !strnstr(kcontrol->id.name, " Volume", 30))
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = mc->count;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = platform_max;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_multi_ext);

int snd_soc_get_dai_name(const struct of_phandle_args *args,
				const char **dai_name)
{
	struct snd_soc_component *pos;
	int ret = -EPROBE_DEFER;

	mutex_lock(&client_mutex);
	for_each_component(pos) {
		struct device_node *component_of_node = soc_component_to_node(pos);

		if (component_of_node != args->np)
			continue;

		ret = snd_soc_component_of_xlate_dai_name(pos, args, dai_name);
		if (ret == -ENOTSUPP) {
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
		} else if (ret) {
			/*
			 * if another error than ENOTSUPP is returned go on and
			 * check if another component is provided with the same
			 * node. This may happen if a device provides several
			 * components
			 */
			continue;
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

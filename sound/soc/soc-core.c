/*
 * soc-core.c  --  ALSA SoC Audio Layer
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
 *
 *  TODO:
 *   o Add hw rules to enforce rates, etc.
 *   o More testing with other codecs/machines.
 *   o Add more codecs and platforms to ensure good API coverage.
 *   o Support TDM on PCM and I2S
 */

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
static LIST_HEAD(platform_list);
static LIST_HEAD(codec_list);
static LIST_HEAD(component_list);

/*
 * This is a timeout to do a DAPM powerdown after a stream is closed().
 * It can be used to eliminate pops between different playback streams, e.g.
 * between two audio tracks.
 */
static int pmdown_time = 5000;
module_param(pmdown_time, int, 0);
MODULE_PARM_DESC(pmdown_time, "DAPM stream powerdown time (msecs)");

/* returns the minimum number of bytes needed to represent
 * a particular given value */
static int min_bytes_needed(unsigned long val)
{
	int c = 0;
	int i;

	for (i = (sizeof val * 8) - 1; i >= 0; --i, ++c)
		if (val & (1UL << i))
			break;
	c = (sizeof val * 8) - c;
	if (!c || (c % 8))
		c = (c + 8) / 8;
	else
		c /= 8;
	return c;
}

/* fill buf which is 'len' bytes with a formatted
 * string of the form 'reg: value\n' */
static int format_register_str(struct snd_soc_codec *codec,
			       unsigned int reg, char *buf, size_t len)
{
	int wordsize = min_bytes_needed(codec->driver->reg_cache_size) * 2;
	int regsize = codec->driver->reg_word_size * 2;
	int ret;

	/* +2 for ': ' and + 1 for '\n' */
	if (wordsize + regsize + 2 + 1 != len)
		return -EINVAL;

	sprintf(buf, "%.*x: ", wordsize, reg);
	buf += wordsize + 2;

	ret = snd_soc_read(codec, reg);
	if (ret < 0)
		memset(buf, 'X', regsize);
	else
		sprintf(buf, "%.*x", regsize, ret);
	buf[regsize] = '\n';
	/* no NUL-termination needed */
	return 0;
}

/* codec register dump */
static ssize_t soc_codec_reg_show(struct snd_soc_codec *codec, char *buf,
				  size_t count, loff_t pos)
{
	int i, step = 1;
	int wordsize, regsize;
	int len;
	size_t total = 0;
	loff_t p = 0;

	wordsize = min_bytes_needed(codec->driver->reg_cache_size) * 2;
	regsize = codec->driver->reg_word_size * 2;

	len = wordsize + regsize + 2 + 1;

	if (!codec->driver->reg_cache_size)
		return 0;

	if (codec->driver->reg_cache_step)
		step = codec->driver->reg_cache_step;

	for (i = 0; i < codec->driver->reg_cache_size; i += step) {
		/* only support larger than PAGE_SIZE bytes debugfs
		 * entries for the default case */
		if (p >= pos) {
			if (total + len >= count - 1)
				break;
			format_register_str(codec, i, buf + total, len);
			total += len;
		}
		p += len;
	}

	total = min(total, count - 1);

	return total;
}

static ssize_t codec_reg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_pcm_runtime *rtd = dev_get_drvdata(dev);

	return soc_codec_reg_show(rtd->codec, buf, PAGE_SIZE, 0);
}

static DEVICE_ATTR(codec_reg, 0444, codec_reg_show, NULL);

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
	&dev_attr_codec_reg.attr,
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
	return rtd->codec ? attr->mode : 0; /* enabled only with codec */
}

static const struct attribute_group soc_dapm_dev_group = {
	.attrs = soc_dapm_dev_attrs,
	.is_visible = soc_dev_attr_is_visible,
};

static const struct attribute_group soc_dev_roup = {
	.attrs = soc_dev_attrs,
	.is_visible = soc_dev_attr_is_visible,
};

static const struct attribute_group *soc_dev_attr_groups[] = {
	&soc_dapm_dev_group,
	&soc_dev_roup,
	NULL
};

#ifdef CONFIG_DEBUG_FS
static ssize_t codec_reg_read_file(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	ssize_t ret;
	struct snd_soc_codec *codec = file->private_data;
	char *buf;

	if (*ppos < 0 || !count)
		return -EINVAL;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = soc_codec_reg_show(codec, buf, count, *ppos);
	if (ret >= 0) {
		if (copy_to_user(user_buf, buf, ret)) {
			kfree(buf);
			return -EFAULT;
		}
		*ppos += ret;
	}

	kfree(buf);
	return ret;
}

static ssize_t codec_reg_write_file(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	size_t buf_size;
	char *start = buf;
	unsigned long reg, value;
	struct snd_soc_codec *codec = file->private_data;
	int ret;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;
	reg = simple_strtoul(start, &start, 16);
	while (*start == ' ')
		start++;
	ret = kstrtoul(start, 16, &value);
	if (ret)
		return ret;

	/* Userspace has been fiddling around behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_NOW_UNRELIABLE);

	snd_soc_write(codec, reg, value);
	return buf_size;
}

static const struct file_operations codec_reg_fops = {
	.open = simple_open,
	.read = codec_reg_read_file,
	.write = codec_reg_write_file,
	.llseek = default_llseek,
};

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

	if (component->init_debugfs)
		component->init_debugfs(component);
}

static void soc_cleanup_component_debugfs(struct snd_soc_component *component)
{
	debugfs_remove_recursive(component->debugfs_root);
}

static void soc_init_codec_debugfs(struct snd_soc_component *component)
{
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);

	codec->debugfs_reg = debugfs_create_file("codec_reg", 0644,
						 codec->component.debugfs_root,
						 codec, &codec_reg_fops);
	if (!codec->debugfs_reg)
		dev_warn(codec->dev,
			"ASoC: Failed to create codec register debugfs file\n");
}

static ssize_t codec_list_read_file(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	ssize_t len, ret = 0;
	struct snd_soc_codec *codec;

	if (!buf)
		return -ENOMEM;

	mutex_lock(&client_mutex);

	list_for_each_entry(codec, &codec_list, list) {
		len = snprintf(buf + ret, PAGE_SIZE - ret, "%s\n",
			       codec->component.name);
		if (len >= 0)
			ret += len;
		if (ret > PAGE_SIZE) {
			ret = PAGE_SIZE;
			break;
		}
	}

	mutex_unlock(&client_mutex);

	if (ret >= 0)
		ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return ret;
}

static const struct file_operations codec_list_fops = {
	.read = codec_list_read_file,
	.llseek = default_llseek,/* read accesses f_pos */
};

static ssize_t dai_list_read_file(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	ssize_t len, ret = 0;
	struct snd_soc_component *component;
	struct snd_soc_dai *dai;

	if (!buf)
		return -ENOMEM;

	mutex_lock(&client_mutex);

	list_for_each_entry(component, &component_list, list) {
		list_for_each_entry(dai, &component->dai_list, list) {
			len = snprintf(buf + ret, PAGE_SIZE - ret, "%s\n",
				dai->name);
			if (len >= 0)
				ret += len;
			if (ret > PAGE_SIZE) {
				ret = PAGE_SIZE;
				break;
			}
		}
	}

	mutex_unlock(&client_mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return ret;
}

static const struct file_operations dai_list_fops = {
	.read = dai_list_read_file,
	.llseek = default_llseek,/* read accesses f_pos */
};

static ssize_t platform_list_read_file(struct file *file,
				       char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	ssize_t len, ret = 0;
	struct snd_soc_platform *platform;

	if (!buf)
		return -ENOMEM;

	mutex_lock(&client_mutex);

	list_for_each_entry(platform, &platform_list, list) {
		len = snprintf(buf + ret, PAGE_SIZE - ret, "%s\n",
			       platform->component.name);
		if (len >= 0)
			ret += len;
		if (ret > PAGE_SIZE) {
			ret = PAGE_SIZE;
			break;
		}
	}

	mutex_unlock(&client_mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return ret;
}

static const struct file_operations platform_list_fops = {
	.read = platform_list_read_file,
	.llseek = default_llseek,/* read accesses f_pos */
};

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
	if (IS_ERR(snd_soc_debugfs_root) || !snd_soc_debugfs_root) {
		pr_warn("ASoC: Failed to create debugfs directory\n");
		snd_soc_debugfs_root = NULL;
		return;
	}

	if (!debugfs_create_file("codecs", 0444, snd_soc_debugfs_root, NULL,
				 &codec_list_fops))
		pr_warn("ASoC: Failed to create CODEC list debugfs file\n");

	if (!debugfs_create_file("dais", 0444, snd_soc_debugfs_root, NULL,
				 &dai_list_fops))
		pr_warn("ASoC: Failed to create DAI list debugfs file\n");

	if (!debugfs_create_file("platforms", 0444, snd_soc_debugfs_root, NULL,
				 &platform_list_fops))
		pr_warn("ASoC: Failed to create platform list debugfs file\n");
}

static void snd_soc_debugfs_exit(void)
{
	debugfs_remove_recursive(snd_soc_debugfs_root);
}

#else

#define soc_init_codec_debugfs NULL

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

struct snd_pcm_substream *snd_soc_get_dai_substream(struct snd_soc_card *card,
		const char *dai_link, int stream)
{
	struct snd_soc_pcm_runtime *rtd;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		if (rtd->dai_link->no_pcm &&
			!strcmp(rtd->dai_link->name, dai_link))
			return rtd->pcm->streams[stream].substream;
	}
	dev_dbg(card->dev, "ASoC: failed to find dai link %s\n", dai_link);
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_get_dai_substream);

static struct snd_soc_pcm_runtime *soc_new_pcm_runtime(
	struct snd_soc_card *card, struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;

	rtd = kzalloc(sizeof(struct snd_soc_pcm_runtime), GFP_KERNEL);
	if (!rtd)
		return NULL;

	rtd->card = card;
	rtd->dai_link = dai_link;
	rtd->codec_dais = kzalloc(sizeof(struct snd_soc_dai *) *
					dai_link->num_codecs,
					GFP_KERNEL);
	if (!rtd->codec_dais) {
		kfree(rtd);
		return NULL;
	}

	return rtd;
}

static void soc_free_pcm_runtime(struct snd_soc_pcm_runtime *rtd)
{
	if (rtd && rtd->codec_dais)
		kfree(rtd->codec_dais);
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

	list_for_each_entry_safe(rtd, _rtd, &card->rtd_list, list) {
		list_del(&rtd->list);
		soc_free_pcm_runtime(rtd);
	}

	card->num_rtd = 0;
}

struct snd_soc_pcm_runtime *snd_soc_get_pcm_runtime(struct snd_soc_card *card,
		const char *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		if (!strcmp(rtd->dai_link->name, dai_link))
			return rtd;
	}
	dev_dbg(card->dev, "ASoC: failed to find rtd %s\n", dai_link);
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_get_pcm_runtime);

static void codec2codec_close_delayed_work(struct work_struct *work)
{
	/* Currently nothing to do for c2c links
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
	struct snd_soc_codec *codec;
	struct snd_soc_pcm_runtime *rtd;
	int i;

	/* If the card is not initialized yet there is nothing to do */
	if (!card->instantiated)
		return 0;

	/* Due to the resume being scheduled into a workqueue we could
	* suspend before that's finished - wait for it to complete.
	 */
	snd_power_lock(card->snd_card);
	snd_power_wait(card->snd_card, SNDRV_CTL_POWER_D0);
	snd_power_unlock(card->snd_card);

	/* we're going to block userspace touching us until resume completes */
	snd_power_change_state(card->snd_card, SNDRV_CTL_POWER_D3hot);

	/* mute any active DACs */
	list_for_each_entry(rtd, &card->rtd_list, list) {

		if (rtd->dai_link->ignore_suspend)
			continue;

		for (i = 0; i < rtd->num_codecs; i++) {
			struct snd_soc_dai *dai = rtd->codec_dais[i];
			struct snd_soc_dai_driver *drv = dai->driver;

			if (drv->ops->digital_mute && dai->playback_active)
				drv->ops->digital_mute(dai, 1);
		}
	}

	/* suspend all pcms */
	list_for_each_entry(rtd, &card->rtd_list, list) {
		if (rtd->dai_link->ignore_suspend)
			continue;

		snd_pcm_suspend_all(rtd->pcm);
	}

	if (card->suspend_pre)
		card->suspend_pre(card);

	list_for_each_entry(rtd, &card->rtd_list, list) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

		if (rtd->dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->suspend && !cpu_dai->driver->bus_control)
			cpu_dai->driver->suspend(cpu_dai);
	}

	/* close any waiting streams */
	list_for_each_entry(rtd, &card->rtd_list, list)
		flush_delayed_work(&rtd->delayed_work);

	list_for_each_entry(rtd, &card->rtd_list, list) {

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

	/* suspend all CODECs */
	list_for_each_entry(codec, &card->codec_dev_list, card_list) {
		struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);

		/* If there are paths active then the CODEC will be held with
		 * bias _ON and should not be suspended. */
		if (!codec->suspended) {
			switch (snd_soc_dapm_get_bias_level(dapm)) {
			case SND_SOC_BIAS_STANDBY:
				/*
				 * If the CODEC is capable of idle
				 * bias off then being in STANDBY
				 * means it's doing something,
				 * otherwise fall through.
				 */
				if (dapm->idle_bias_off) {
					dev_dbg(codec->dev,
						"ASoC: idle_bias_off CODEC on over suspend\n");
					break;
				}

			case SND_SOC_BIAS_OFF:
				if (codec->driver->suspend)
					codec->driver->suspend(codec);
				codec->suspended = 1;
				if (codec->component.regmap)
					regcache_mark_dirty(codec->component.regmap);
				/* deactivate pins to sleep state */
				pinctrl_pm_select_sleep_state(codec->dev);
				break;
			default:
				dev_dbg(codec->dev,
					"ASoC: CODEC is on over suspend\n");
				break;
			}
		}
	}

	list_for_each_entry(rtd, &card->rtd_list, list) {
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

/* deferred resume work, so resume can complete before we finished
 * setting our codec back up, which can be very slow on I2C
 */
static void soc_resume_deferred(struct work_struct *work)
{
	struct snd_soc_card *card =
			container_of(work, struct snd_soc_card, deferred_resume_work);
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_codec *codec;
	int i;

	/* our power state is still SNDRV_CTL_POWER_D3hot from suspend time,
	 * so userspace apps are blocked from touching us
	 */

	dev_dbg(card->dev, "ASoC: starting resume work\n");

	/* Bring us up into D2 so that DAPM starts enabling things */
	snd_power_change_state(card->snd_card, SNDRV_CTL_POWER_D2);

	if (card->resume_pre)
		card->resume_pre(card);

	/* resume control bus DAIs */
	list_for_each_entry(rtd, &card->rtd_list, list) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

		if (rtd->dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->resume && cpu_dai->driver->bus_control)
			cpu_dai->driver->resume(cpu_dai);
	}

	list_for_each_entry(codec, &card->codec_dev_list, card_list) {
		if (codec->suspended) {
			if (codec->driver->resume)
				codec->driver->resume(codec);
			codec->suspended = 0;
		}
	}

	list_for_each_entry(rtd, &card->rtd_list, list) {

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
	list_for_each_entry(rtd, &card->rtd_list, list) {

		if (rtd->dai_link->ignore_suspend)
			continue;

		for (i = 0; i < rtd->num_codecs; i++) {
			struct snd_soc_dai *dai = rtd->codec_dais[i];
			struct snd_soc_dai_driver *drv = dai->driver;

			if (drv->ops->digital_mute && dai->playback_active)
				drv->ops->digital_mute(dai, 0);
		}
	}

	list_for_each_entry(rtd, &card->rtd_list, list) {
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
	list_for_each_entry(rtd, &card->rtd_list, list) {
		struct snd_soc_dai **codec_dais = rtd->codec_dais;
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
		int j;

		if (cpu_dai->active)
			pinctrl_pm_select_default_state(cpu_dai->dev);

		for (j = 0; j < rtd->num_codecs; j++) {
			struct snd_soc_dai *codec_dai = codec_dais[j];
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
	list_for_each_entry(rtd, &card->rtd_list, list) {
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

	lockdep_assert_held(&client_mutex);

	list_for_each_entry(component, &component_list, list) {
		if (of_node) {
			if (component->dev->of_node == of_node)
				return component;
		} else if (strcmp(component->name, name) == 0) {
			return component;
		}
	}

	return NULL;
}

/**
 * snd_soc_find_dai - Find a registered DAI
 *
 * @dlc: name of the DAI and optional component info to match
 *
 * This function will search all regsitered components and their DAIs to
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
	struct device_node *component_of_node;

	lockdep_assert_held(&client_mutex);

	/* Find CPU DAI from registered DAIs*/
	list_for_each_entry(component, &component_list, list) {
		component_of_node = component->dev->of_node;
		if (!component_of_node && component->dev->parent)
			component_of_node = component->dev->parent->of_node;

		if (dlc->of_node && component_of_node != dlc->of_node)
			continue;
		if (dlc->name && strcmp(component->name, dlc->name))
			continue;
		list_for_each_entry(dai, &component->dai_list, list) {
			if (dlc->dai_name && strcmp(dai->name, dlc->dai_name))
				continue;

			return dai;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_find_dai);

static bool soc_is_dai_link_bound(struct snd_soc_card *card,
		struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		if (rtd->dai_link == dai_link)
			return true;
	}

	return false;
}

static int soc_bind_dai_link(struct snd_soc_card *card,
	struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai_link_component *codecs = dai_link->codecs;
	struct snd_soc_dai_link_component cpu_dai_component;
	struct snd_soc_dai **codec_dais;
	struct snd_soc_platform *platform;
	const char *platform_name;
	int i;

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
		dev_err(card->dev, "ASoC: CPU DAI %s not registered\n",
			dai_link->cpu_dai_name);
		goto _err_defer;
	}

	rtd->num_codecs = dai_link->num_codecs;

	/* Find CODEC from registered CODECs */
	codec_dais = rtd->codec_dais;
	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dais[i] = snd_soc_find_dai(&codecs[i]);
		if (!codec_dais[i]) {
			dev_err(card->dev, "ASoC: CODEC DAI %s not registered\n",
				codecs[i].dai_name);
			goto _err_defer;
		}
	}

	/* Single codec links expect codec and codec_dai in runtime data */
	rtd->codec_dai = codec_dais[0];
	rtd->codec = rtd->codec_dai->codec;

	/* if there's no platform we match on the empty platform */
	platform_name = dai_link->platform_name;
	if (!platform_name && !dai_link->platform_of_node)
		platform_name = "snd-soc-dummy";

	/* find one from the set of registered platforms */
	list_for_each_entry(platform, &platform_list, list) {
		if (dai_link->platform_of_node) {
			if (platform->dev->of_node !=
			    dai_link->platform_of_node)
				continue;
		} else {
			if (strcmp(platform->component.name, platform_name))
				continue;
		}

		rtd->platform = platform;
	}
	if (!rtd->platform) {
		dev_err(card->dev, "ASoC: platform %s not registered\n",
			dai_link->platform_name);
		goto _err_defer;
	}

	soc_add_pcm_runtime(card, rtd);
	return 0;

_err_defer:
	soc_free_pcm_runtime(rtd);
	return  -EPROBE_DEFER;
}

static void soc_remove_component(struct snd_soc_component *component)
{
	if (!component->card)
		return;

	/* This is a HACK and will be removed soon */
	if (component->codec)
		list_del(&component->codec->card_list);

	if (component->remove)
		component->remove(component);

	snd_soc_dapm_free(snd_soc_component_get_dapm(component));

	soc_cleanup_component_debugfs(component);
	component->card = NULL;
	module_put(component->dev->driver->owner);
}

static void soc_remove_dai(struct snd_soc_dai *dai, int order)
{
	int err;

	if (dai && dai->probed &&
			dai->driver->remove_order == order) {
		if (dai->driver->remove) {
			err = dai->driver->remove(dai);
			if (err < 0)
				dev_err(dai->dev,
					"ASoC: failed to remove %s: %d\n",
					dai->name, err);
		}
		dai->probed = 0;
	}
}

static void soc_remove_link_dais(struct snd_soc_card *card,
		struct snd_soc_pcm_runtime *rtd, int order)
{
	int i;

	/* unregister the rtd device */
	if (rtd->dev_registered) {
		device_unregister(rtd->dev);
		rtd->dev_registered = 0;
	}

	/* remove the CODEC DAI */
	for (i = 0; i < rtd->num_codecs; i++)
		soc_remove_dai(rtd->codec_dais[i], order);

	soc_remove_dai(rtd->cpu_dai, order);
}

static void soc_remove_link_components(struct snd_soc_card *card,
	struct snd_soc_pcm_runtime *rtd, int order)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_component *component;
	int i;

	/* remove the platform */
	if (platform && platform->component.driver->remove_order == order)
		soc_remove_component(&platform->component);

	/* remove the CODEC-side CODEC */
	for (i = 0; i < rtd->num_codecs; i++) {
		component = rtd->codec_dais[i]->component;
		if (component->driver->remove_order == order)
			soc_remove_component(component);
	}

	/* remove any CPU-side CODEC */
	if (cpu_dai) {
		if (cpu_dai->component->driver->remove_order == order)
			soc_remove_component(cpu_dai->component);
	}
}

static void soc_remove_dai_links(struct snd_soc_card *card)
{
	int order;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai_link *link, *_link;

	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
			order++) {
		list_for_each_entry(rtd, &card->rtd_list, list)
			soc_remove_link_dais(card, rtd, order);
	}

	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
			order++) {
		list_for_each_entry(rtd, &card->rtd_list, list)
			soc_remove_link_components(card, rtd, order);
	}

	list_for_each_entry_safe(link, _link, &card->dai_link_list, list) {
		if (link->dobj.type == SND_SOC_DOBJ_DAI_LINK)
			dev_warn(card->dev, "Topology forgot to remove link %s?\n",
				link->name);

		list_del(&link->list);
		card->num_dai_links--;
	}
}

static int snd_soc_init_multicodec(struct snd_soc_card *card,
				   struct snd_soc_dai_link *dai_link)
{
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

	ret = snd_soc_init_multicodec(card, link);
	if (ret) {
		dev_err(card->dev, "ASoC: failed to init multicodec\n");
		return ret;
	}

	for (i = 0; i < link->num_codecs; i++) {
		/*
		 * Codec must be specified by 1 of name or OF node,
		 * not both or neither.
		 */
		if (!!link->codecs[i].name ==
		    !!link->codecs[i].of_node) {
			dev_err(card->dev, "ASoC: Neither/both codec name/of_node are set for %s\n",
				link->name);
			return -EINVAL;
		}
		/* Codec DAI name must be specified */
		if (!link->codecs[i].dai_name) {
			dev_err(card->dev, "ASoC: codec_dai_name not set for %s\n",
				link->name);
			return -EINVAL;
		}
	}

	/*
	 * Platform may be specified by either name or OF node, but
	 * can be left unspecified, and a dummy platform will be used.
	 */
	if (link->platform_name && link->platform_of_node) {
		dev_err(card->dev,
			"ASoC: Both platform name/of_node are set for %s\n",
			link->name);
		return -EINVAL;
	}

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
	/* Notify the machine driver for extra initialization
	 * on the link created by topology.
	 */
	if (dai_link->dobj.type && card->add_dai_link)
		card->add_dai_link(card, dai_link);

	list_add_tail(&dai_link->list, &card->dai_link_list);
	card->num_dai_links++;

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
	/* Notify the machine driver for extra destruction
	 * on the link created by topology.
	 */
	if (dai_link->dobj.type && card->remove_dai_link)
		card->remove_dai_link(card, dai_link);

	list_for_each_entry_safe(link, _link, &card->dai_link_list, list) {
		if (link == dai_link) {
			list_del(&link->list);
			card->num_dai_links--;
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(snd_soc_remove_dai_link);

static void soc_set_name_prefix(struct snd_soc_card *card,
				struct snd_soc_component *component)
{
	int i;

	if (card->codec_conf == NULL)
		return;

	for (i = 0; i < card->num_configs; i++) {
		struct snd_soc_codec_conf *map = &card->codec_conf[i];
		if (map->of_node && component->dev->of_node != map->of_node)
			continue;
		if (map->dev_name && strcmp(component->name, map->dev_name))
			continue;
		component->name_prefix = map->name_prefix;
		break;
	}
}

static int soc_probe_component(struct snd_soc_card *card,
	struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
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

	if (!try_module_get(component->dev->driver->owner))
		return -ENODEV;

	component->card = card;
	dapm->card = card;
	soc_set_name_prefix(card, component);

	soc_init_component_debugfs(component);

	if (component->dapm_widgets) {
		ret = snd_soc_dapm_new_controls(dapm, component->dapm_widgets,
			component->num_dapm_widgets);

		if (ret != 0) {
			dev_err(component->dev,
				"Failed to create new controls %d\n", ret);
			goto err_probe;
		}
	}

	list_for_each_entry(dai, &component->dai_list, list) {
		ret = snd_soc_dapm_new_dai_widgets(dapm, dai);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to create DAI widgets %d\n", ret);
			goto err_probe;
		}
	}

	if (component->probe) {
		ret = component->probe(component);
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

	if (component->controls)
		snd_soc_add_component_controls(component, component->controls,
				     component->num_controls);
	if (component->dapm_routes)
		snd_soc_dapm_add_routes(dapm, component->dapm_routes,
					component->num_dapm_routes);

	list_add(&dapm->list, &card->dapm_list);

	/* This is a HACK and will be removed soon */
	if (component->codec)
		list_add(&component->codec->card_list, &card->codec_dev_list);

	return 0;

err_probe:
	soc_cleanup_component_debugfs(component);
	component->card = NULL;
	module_put(component->dev->driver->owner);

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
			struct snd_soc_pcm_runtime *rtd,
				     int order)
{
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_component *component;
	int i, ret;

	/* probe the CPU-side component, if it is a CODEC */
	component = rtd->cpu_dai->component;
	if (component->driver->probe_order == order) {
		ret = soc_probe_component(card, component);
		if (ret < 0)
			return ret;
	}

	/* probe the CODEC-side components */
	for (i = 0; i < rtd->num_codecs; i++) {
		component = rtd->codec_dais[i]->component;
		if (component->driver->probe_order == order) {
			ret = soc_probe_component(card, component);
			if (ret < 0)
				return ret;
		}
	}

	/* probe the platform */
	if (platform->component.driver->probe_order == order) {
		ret = soc_probe_component(card, &platform->component);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int soc_probe_dai(struct snd_soc_dai *dai, int order)
{
	int ret;

	if (!dai->probed && dai->driver->probe_order == order) {
		if (dai->driver->probe) {
			ret = dai->driver->probe(dai);
			if (ret < 0) {
				dev_err(dai->dev,
					"ASoC: failed to probe DAI %s: %d\n",
					dai->name, ret);
				return ret;
			}
		}

		dai->probed = 1;
	}

	return 0;
}

static int soc_link_dai_widgets(struct snd_soc_card *card,
				struct snd_soc_dai_link *dai_link,
				struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dapm_widget *sink, *source;
	int ret;

	if (rtd->num_codecs > 1)
		dev_warn(card->dev, "ASoC: Multiple codecs not supported yet\n");

	/* link the DAI widgets */
	sink = codec_dai->playback_widget;
	source = cpu_dai->capture_widget;
	if (sink && source) {
		ret = snd_soc_dapm_new_pcm(card, dai_link->params,
					   dai_link->num_params,
					   source, sink);
		if (ret != 0) {
			dev_err(card->dev, "ASoC: Can't link %s to %s: %d\n",
				sink->name, source->name, ret);
			return ret;
		}
	}

	sink = cpu_dai->playback_widget;
	source = codec_dai->capture_widget;
	if (sink && source) {
		ret = snd_soc_dapm_new_pcm(card, dai_link->params,
					   dai_link->num_params,
					   source, sink);
		if (ret != 0) {
			dev_err(card->dev, "ASoC: Can't link %s to %s: %d\n",
				sink->name, source->name, ret);
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
	int i, ret;

	dev_dbg(card->dev, "ASoC: probe %s dai link %d late %d\n",
			card->name, rtd->num, order);

	/* set default power off timeout */
	rtd->pmdown_time = pmdown_time;

	ret = soc_probe_dai(cpu_dai, order);
	if (ret)
		return ret;

	/* probe the CODEC DAI */
	for (i = 0; i < rtd->num_codecs; i++) {
		ret = soc_probe_dai(rtd->codec_dais[i], order);
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

	if (cpu_dai->driver->compress_new) {
		/*create compress_device"*/
		ret = cpu_dai->driver->compress_new(rtd, rtd->num);
		if (ret < 0) {
			dev_err(card->dev, "ASoC: can't create compress %s\n",
					 dai_link->stream_name);
			return ret;
		}
	} else {

		if (!dai_link->params) {
			/* create the pcm */
			ret = soc_new_pcm(rtd, rtd->num);
			if (ret < 0) {
				dev_err(card->dev, "ASoC: can't create pcm %s :%d\n",
				       dai_link->stream_name, ret);
				return ret;
			}
		} else {
			INIT_DELAYED_WORK(&rtd->delayed_work,
						codec2codec_close_delayed_work);

			/* link the DAI widgets */
			ret = soc_link_dai_widgets(card, dai_link, rtd);
			if (ret)
				return ret;
		}
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
	list_add(&component->list_aux, &card->aux_comp_list);
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

	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
		order++) {
		list_for_each_entry(comp, &card->aux_comp_list, list_aux) {
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

	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
		order++) {
		list_for_each_entry_safe(comp, _comp,
			&card->aux_comp_list, list_aux) {
			if (comp->driver->remove_order == order) {
				soc_remove_component(comp);
				/* remove it from the card's aux_comp_list */
				list_del(&comp->list_aux);
			}
		}
	}
}

static int snd_soc_init_codec_cache(struct snd_soc_codec *codec)
{
	int ret;

	if (codec->cache_init)
		return 0;

	ret = snd_soc_cache_init(codec);
	if (ret < 0) {
		dev_err(codec->dev,
			"ASoC: Failed to set cache compression type: %d\n",
			ret);
		return ret;
	}
	codec->cache_init = 1;
	return 0;
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
	struct snd_soc_dai **codec_dais = rtd->codec_dais;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int i;
	int ret;

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_dai *codec_dai = codec_dais[i];

		ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
		if (ret != 0 && ret != -ENOTSUPP) {
			dev_warn(codec_dai->dev,
				 "ASoC: Failed to set DAI format: %d\n", ret);
			return ret;
		}
	}

	/* Flip the polarity for the "CPU" end of a CODEC<->CODEC link */
	if (cpu_dai->codec) {
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

static int snd_soc_instantiate_card(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai_link *dai_link;
	int ret, i, order;

	mutex_lock(&client_mutex);
	mutex_lock_nested(&card->mutex, SND_SOC_CARD_CLASS_INIT);

	/* bind DAIs */
	for (i = 0; i < card->num_links; i++) {
		ret = soc_bind_dai_link(card, &card->dai_link[i]);
		if (ret != 0)
			goto base_error;
	}

	/* bind aux_devs too */
	for (i = 0; i < card->num_aux_devs; i++) {
		ret = soc_bind_aux_dev(card, i);
		if (ret != 0)
			goto base_error;
	}

	/* add predefined DAI links to the list */
	for (i = 0; i < card->num_links; i++)
		snd_soc_add_dai_link(card, card->dai_link+i);

	/* initialize the register cache for each available codec */
	list_for_each_entry(codec, &codec_list, list) {
		if (codec->cache_init)
			continue;
		ret = snd_soc_init_codec_cache(codec);
		if (ret < 0)
			goto base_error;
	}

	/* card bind complete so register a sound card */
	ret = snd_card_new(card->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			card->owner, 0, &card->snd_card);
	if (ret < 0) {
		dev_err(card->dev,
			"ASoC: can't create sound card for card %s: %d\n",
			card->name, ret);
		goto base_error;
	}

	soc_init_card_debugfs(card);

	card->dapm.bias_level = SND_SOC_BIAS_OFF;
	card->dapm.dev = card->dev;
	card->dapm.card = card;
	list_add(&card->dapm.list, &card->dapm_list);

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
			goto card_probe_error;
	}

	/* probe all components used by DAI links on this card */
	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
			order++) {
		list_for_each_entry(rtd, &card->rtd_list, list) {
			ret = soc_probe_link_components(card, rtd, order);
			if (ret < 0) {
				dev_err(card->dev,
					"ASoC: failed to instantiate card %d\n",
					ret);
				goto probe_dai_err;
			}
		}
	}

	/* probe auxiliary components */
	ret = soc_probe_aux_devices(card);
	if (ret < 0)
		goto probe_dai_err;

	/* Find new DAI links added during probing components and bind them.
	 * Components with topology may bring new DAIs and DAI links.
	 */
	list_for_each_entry(dai_link, &card->dai_link_list, list) {
		if (soc_is_dai_link_bound(card, dai_link))
			continue;

		ret = soc_init_dai_link(card, dai_link);
		if (ret)
			goto probe_dai_err;
		ret = soc_bind_dai_link(card, dai_link);
		if (ret)
			goto probe_dai_err;
	}

	/* probe all DAI links on this card */
	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
			order++) {
		list_for_each_entry(rtd, &card->rtd_list, list) {
			ret = soc_probe_link_dais(card, rtd, order);
			if (ret < 0) {
				dev_err(card->dev,
					"ASoC: failed to instantiate card %d\n",
					ret);
				goto probe_dai_err;
			}
		}
	}

	snd_soc_dapm_link_dai_widgets(card);
	snd_soc_dapm_connect_dai_link_widgets(card);

	if (card->controls)
		snd_soc_add_card_controls(card, card->controls, card->num_controls);

	if (card->dapm_routes)
		snd_soc_dapm_add_routes(&card->dapm, card->dapm_routes,
					card->num_dapm_routes);

	if (card->of_dapm_routes)
		snd_soc_dapm_add_routes(&card->dapm, card->of_dapm_routes,
					card->num_of_dapm_routes);

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
			goto probe_aux_dev_err;
		}
	}

	snd_soc_dapm_new_widgets(card);

	ret = snd_card_register(card->snd_card);
	if (ret < 0) {
		dev_err(card->dev, "ASoC: failed to register soundcard %d\n",
				ret);
		goto probe_aux_dev_err;
	}

	card->instantiated = 1;
	snd_soc_dapm_sync(&card->dapm);
	mutex_unlock(&card->mutex);
	mutex_unlock(&client_mutex);

	return 0;

probe_aux_dev_err:
	soc_remove_aux_devices(card);

probe_dai_err:
	soc_remove_dai_links(card);

card_probe_error:
	if (card->remove)
		card->remove(card);

	snd_soc_dapm_free(&card->dapm);
	soc_cleanup_card_debugfs(card);
	snd_card_free(card->snd_card);

base_error:
	soc_remove_pcm_runtimes(card);
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

static int soc_cleanup_card_resources(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;

	/* make sure any delayed work runs */
	list_for_each_entry(rtd, &card->rtd_list, list)
		flush_delayed_work(&rtd->delayed_work);

	/* remove and free each DAI */
	soc_remove_dai_links(card);
	soc_remove_pcm_runtimes(card);

	/* remove auxiliary devices */
	soc_remove_aux_devices(card);

	snd_soc_dapm_free(&card->dapm);
	soc_cleanup_card_debugfs(card);

	/* remove the card */
	if (card->remove)
		card->remove(card);

	snd_card_free(card->snd_card);
	return 0;

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

	/* Flush out pmdown_time work - we actually do want to run it
	 * now, we're shutting down so no imminent restart. */
	list_for_each_entry(rtd, &card->rtd_list, list)
		flush_delayed_work(&rtd->delayed_work);

	snd_soc_dapm_shutdown(card);

	/* deactivate pins to sleep state */
	list_for_each_entry(rtd, &card->rtd_list, list) {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
		int i;

		pinctrl_pm_select_sleep_state(cpu_dai->dev);
		for (i = 0; i < rtd->num_codecs; i++) {
			struct snd_soc_dai *codec_dai = rtd->codec_dais[i];
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
 * snd_soc_add_codec_controls - add an array of controls to a codec.
 * Convenience function to add a list of controls. Many codecs were
 * duplicating this code.
 *
 * @codec: codec to add controls to
 * @controls: array of controls to add
 * @num_controls: number of elements in the array
 *
 * Return 0 for success, else error.
 */
int snd_soc_add_codec_controls(struct snd_soc_codec *codec,
	const struct snd_kcontrol_new *controls, unsigned int num_controls)
{
	return snd_soc_add_component_controls(&codec->component, controls,
		num_controls);
}
EXPORT_SYMBOL_GPL(snd_soc_add_codec_controls);

/**
 * snd_soc_add_platform_controls - add an array of controls to a platform.
 * Convenience function to add a list of controls.
 *
 * @platform: platform to add controls to
 * @controls: array of controls to add
 * @num_controls: number of elements in the array
 *
 * Return 0 for success, else error.
 */
int snd_soc_add_platform_controls(struct snd_soc_platform *platform,
	const struct snd_kcontrol_new *controls, unsigned int num_controls)
{
	return snd_soc_add_component_controls(&platform->component, controls,
		num_controls);
}
EXPORT_SYMBOL_GPL(snd_soc_add_platform_controls);

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
	if (dai->driver && dai->driver->ops->set_sysclk)
		return dai->driver->ops->set_sysclk(dai, clk_id, freq, dir);
	else if (dai->codec && dai->codec->driver->set_sysclk)
		return dai->codec->driver->set_sysclk(dai->codec, clk_id, 0,
						      freq, dir);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_sysclk);

/**
 * snd_soc_codec_set_sysclk - configure CODEC system or master clock.
 * @codec: CODEC
 * @clk_id: DAI specific clock ID
 * @source: Source for the clock
 * @freq: new clock frequency in Hz
 * @dir: new clock direction - input/output.
 *
 * Configures the CODEC master (MCLK) or system (SYSCLK) clocking.
 */
int snd_soc_codec_set_sysclk(struct snd_soc_codec *codec, int clk_id,
			     int source, unsigned int freq, int dir)
{
	if (codec->driver->set_sysclk)
		return codec->driver->set_sysclk(codec, clk_id, source,
						 freq, dir);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_sysclk);

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
	if (dai->driver && dai->driver->ops->set_clkdiv)
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
	if (dai->driver && dai->driver->ops->set_pll)
		return dai->driver->ops->set_pll(dai, pll_id, source,
					 freq_in, freq_out);
	else if (dai->codec && dai->codec->driver->set_pll)
		return dai->codec->driver->set_pll(dai->codec, pll_id, source,
						   freq_in, freq_out);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_pll);

/*
 * snd_soc_codec_set_pll - configure codec PLL.
 * @codec: CODEC
 * @pll_id: DAI specific PLL ID
 * @source: DAI specific source for the PLL
 * @freq_in: PLL input clock frequency in Hz
 * @freq_out: requested PLL output clock frequency in Hz
 *
 * Configures and enables PLL to generate output clock based on input clock.
 */
int snd_soc_codec_set_pll(struct snd_soc_codec *codec, int pll_id, int source,
			  unsigned int freq_in, unsigned int freq_out)
{
	if (codec->driver->set_pll)
		return codec->driver->set_pll(codec, pll_id, source,
					      freq_in, freq_out);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_pll);

/**
 * snd_soc_dai_set_bclk_ratio - configure BCLK to sample rate ratio.
 * @dai: DAI
 * @ratio: Ratio of BCLK to Sample rate.
 *
 * Configures the DAI for a preset BCLK to sample rate ratio.
 */
int snd_soc_dai_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	if (dai->driver && dai->driver->ops->set_bclk_ratio)
		return dai->driver->ops->set_bclk_ratio(dai, ratio);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_bclk_ratio);

/**
 * snd_soc_dai_set_fmt - configure DAI hardware audio format.
 * @dai: DAI
 * @fmt: SND_SOC_DAIFMT_ format value.
 *
 * Configures the DAI hardware format and clocking.
 */
int snd_soc_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	if (dai->driver == NULL)
		return -EINVAL;
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
	if (dai->driver && dai->driver->ops->xlate_tdm_slot_mask)
		dai->driver->ops->xlate_tdm_slot_mask(slots,
						&tx_mask, &rx_mask);
	else
		snd_soc_xlate_tdm_slot_mask(slots, &tx_mask, &rx_mask);

	dai->tx_mask = tx_mask;
	dai->rx_mask = rx_mask;

	if (dai->driver && dai->driver->ops->set_tdm_slot)
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
	if (dai->driver && dai->driver->ops->set_channel_map)
		return dai->driver->ops->set_channel_map(dai, tx_num, tx_slot,
			rx_num, rx_slot);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_channel_map);

/**
 * snd_soc_dai_set_tristate - configure DAI system or master clock.
 * @dai: DAI
 * @tristate: tristate enable
 *
 * Tristates the DAI so that others can use it.
 */
int snd_soc_dai_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	if (dai->driver && dai->driver->ops->set_tristate)
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
	if (!dai->driver)
		return -ENOTSUPP;

	if (dai->driver->ops->mute_stream)
		return dai->driver->ops->mute_stream(dai, mute, direction);
	else if (direction == SNDRV_PCM_STREAM_PLAYBACK &&
		 dai->driver->ops->digital_mute)
		return dai->driver->ops->digital_mute(dai, mute);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_digital_mute);

/**
 * snd_soc_register_card - Register a card with the ASoC core
 *
 * @card: Card to register
 *
 */
int snd_soc_register_card(struct snd_soc_card *card)
{
	int i, ret;
	struct snd_soc_pcm_runtime *rtd;

	if (!card->name || !card->dev)
		return -EINVAL;

	for (i = 0; i < card->num_links; i++) {
		struct snd_soc_dai_link *link = &card->dai_link[i];

		ret = soc_init_dai_link(card, link);
		if (ret) {
			dev_err(card->dev, "ASoC: failed to init link %s\n",
				link->name);
			return ret;
		}
	}

	dev_set_drvdata(card->dev, card);

	snd_soc_initialize_card_lists(card);

	INIT_LIST_HEAD(&card->dai_link_list);
	card->num_dai_links = 0;

	INIT_LIST_HEAD(&card->rtd_list);
	card->num_rtd = 0;

	INIT_LIST_HEAD(&card->dapm_dirty);
	INIT_LIST_HEAD(&card->dobj_list);
	card->instantiated = 0;
	mutex_init(&card->mutex);
	mutex_init(&card->dapm_mutex);

	ret = snd_soc_instantiate_card(card);
	if (ret != 0)
		return ret;

	/* deactivate pins to sleep state */
	list_for_each_entry(rtd, &card->rtd_list, list)  {
		struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
		int j;

		for (j = 0; j < rtd->num_codecs; j++) {
			struct snd_soc_dai *codec_dai = rtd->codec_dais[j];
			if (!codec_dai->active)
				pinctrl_pm_select_sleep_state(codec_dai->dev);
		}

		if (!cpu_dai->active)
			pinctrl_pm_select_sleep_state(cpu_dai->dev);
	}

	return ret;
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
	if (card->instantiated) {
		card->instantiated = false;
		snd_soc_dapm_shutdown(card);
		soc_cleanup_card_resources(card);
		dev_dbg(card->dev, "ASoC: Unregistered card '%s'\n", card->name);
	}

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
		/* I2C component devices are named "bus-addr"  */
		if (sscanf(name, "%x-%x", &id1, &id2) == 2) {
			char tmp[NAME_SIZE];

			/* create unique ID number from I2C addr and bus */
			*id = ((id1 & 0xffff) << 16) + id2;

			/* sanitize component name for DAI link creation */
			snprintf(tmp, NAME_SIZE, "%s.%s", dev->driver->name, name);
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

	list_for_each_entry_safe(dai, _dai, &component->dai_list, list) {
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

	list_add(&dai->list, &component->dai_list);
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
 * @legacy_dai_naming: Use the legacy naming scheme and let the DAI inherit the
 *                     parent's name.
 */
static int snd_soc_register_dais(struct snd_soc_component *component,
	struct snd_soc_dai_driver *dai_drv, size_t count,
	bool legacy_dai_naming)
{
	struct device *dev = component->dev;
	struct snd_soc_dai *dai;
	unsigned int i;
	int ret;

	dev_dbg(dev, "ASoC: dai register %s #%Zu\n", dev_name(dev), count);

	component->dai_drv = dai_drv;

	for (i = 0; i < count; i++) {

		dai = soc_add_dai(component, dai_drv + i,
				count == 1 && legacy_dai_naming);
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

	/* Create the DAI widgets here. After adding DAIs, topology may
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
	component->probe = component->driver->probe;
	component->remove = component->driver->remove;

	dapm = &component->dapm;
	dapm->dev = dev;
	dapm->component = component;
	dapm->bias_level = SND_SOC_BIAS_OFF;
	dapm->idle_bias_off = true;
	if (driver->seq_notifier)
		dapm->seq_notifier = snd_soc_component_seq_notifier;
	if (driver->stream_event)
		dapm->stream_event = snd_soc_component_stream_event;

	component->controls = driver->controls;
	component->num_controls = driver->num_controls;
	component->dapm_widgets = driver->dapm_widgets;
	component->num_dapm_widgets = driver->num_dapm_widgets;
	component->dapm_routes = driver->dapm_routes;
	component->num_dapm_routes = driver->num_dapm_routes;

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
 * snd_soc_component_init_regmap() - Initialize regmap instance for the component
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
 * snd_soc_component_exit_regmap() - De-initialize regmap instance for the component
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

static void snd_soc_component_add_unlocked(struct snd_soc_component *component)
{
	if (!component->write && !component->read) {
		if (!component->regmap)
			component->regmap = dev_get_regmap(component->dev, NULL);
		if (component->regmap)
			snd_soc_component_setup_regmap(component);
	}

	list_add(&component->list, &component_list);
	INIT_LIST_HEAD(&component->dobj_list);
}

static void snd_soc_component_add(struct snd_soc_component *component)
{
	mutex_lock(&client_mutex);
	snd_soc_component_add_unlocked(component);
	mutex_unlock(&client_mutex);
}

static void snd_soc_component_cleanup(struct snd_soc_component *component)
{
	snd_soc_unregister_dais(component);
	kfree(component->name);
}

static void snd_soc_component_del_unlocked(struct snd_soc_component *component)
{
	list_del(&component->list);
}

int snd_soc_register_component(struct device *dev,
			       const struct snd_soc_component_driver *cmpnt_drv,
			       struct snd_soc_dai_driver *dai_drv,
			       int num_dai)
{
	struct snd_soc_component *cmpnt;
	int ret;

	cmpnt = kzalloc(sizeof(*cmpnt), GFP_KERNEL);
	if (!cmpnt) {
		dev_err(dev, "ASoC: Failed to allocate memory\n");
		return -ENOMEM;
	}

	ret = snd_soc_component_initialize(cmpnt, cmpnt_drv, dev);
	if (ret)
		goto err_free;

	cmpnt->ignore_pmdown_time = true;
	cmpnt->registered_as_component = true;

	ret = snd_soc_register_dais(cmpnt, dai_drv, num_dai, true);
	if (ret < 0) {
		dev_err(dev, "ASoC: Failed to register DAIs: %d\n", ret);
		goto err_cleanup;
	}

	snd_soc_component_add(cmpnt);

	return 0;

err_cleanup:
	snd_soc_component_cleanup(cmpnt);
err_free:
	kfree(cmpnt);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_register_component);

/**
 * snd_soc_unregister_component - Unregister a component from the ASoC core
 *
 * @dev: The device to unregister
 */
void snd_soc_unregister_component(struct device *dev)
{
	struct snd_soc_component *cmpnt;

	mutex_lock(&client_mutex);
	list_for_each_entry(cmpnt, &component_list, list) {
		if (dev == cmpnt->dev && cmpnt->registered_as_component)
			goto found;
	}
	mutex_unlock(&client_mutex);
	return;

found:
	snd_soc_tplg_component_remove(cmpnt, SND_SOC_TPLG_INDEX_ALL);
	snd_soc_component_del_unlocked(cmpnt);
	mutex_unlock(&client_mutex);
	snd_soc_component_cleanup(cmpnt);
	kfree(cmpnt);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_component);

static int snd_soc_platform_drv_probe(struct snd_soc_component *component)
{
	struct snd_soc_platform *platform = snd_soc_component_to_platform(component);

	return platform->driver->probe(platform);
}

static void snd_soc_platform_drv_remove(struct snd_soc_component *component)
{
	struct snd_soc_platform *platform = snd_soc_component_to_platform(component);

	platform->driver->remove(platform);
}

/**
 * snd_soc_add_platform - Add a platform to the ASoC core
 * @dev: The parent device for the platform
 * @platform: The platform to add
 * @platform_drv: The driver for the platform
 */
int snd_soc_add_platform(struct device *dev, struct snd_soc_platform *platform,
		const struct snd_soc_platform_driver *platform_drv)
{
	int ret;

	ret = snd_soc_component_initialize(&platform->component,
			&platform_drv->component_driver, dev);
	if (ret)
		return ret;

	platform->dev = dev;
	platform->driver = platform_drv;

	if (platform_drv->probe)
		platform->component.probe = snd_soc_platform_drv_probe;
	if (platform_drv->remove)
		platform->component.remove = snd_soc_platform_drv_remove;

#ifdef CONFIG_DEBUG_FS
	platform->component.debugfs_prefix = "platform";
#endif

	mutex_lock(&client_mutex);
	snd_soc_component_add_unlocked(&platform->component);
	list_add(&platform->list, &platform_list);
	mutex_unlock(&client_mutex);

	dev_dbg(dev, "ASoC: Registered platform '%s'\n",
		platform->component.name);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_add_platform);

/**
 * snd_soc_register_platform - Register a platform with the ASoC core
 *
 * @dev: The device for the platform
 * @platform_drv: The driver for the platform
 */
int snd_soc_register_platform(struct device *dev,
		const struct snd_soc_platform_driver *platform_drv)
{
	struct snd_soc_platform *platform;
	int ret;

	dev_dbg(dev, "ASoC: platform register %s\n", dev_name(dev));

	platform = kzalloc(sizeof(struct snd_soc_platform), GFP_KERNEL);
	if (platform == NULL)
		return -ENOMEM;

	ret = snd_soc_add_platform(dev, platform, platform_drv);
	if (ret)
		kfree(platform);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_register_platform);

/**
 * snd_soc_remove_platform - Remove a platform from the ASoC core
 * @platform: the platform to remove
 */
void snd_soc_remove_platform(struct snd_soc_platform *platform)
{

	mutex_lock(&client_mutex);
	list_del(&platform->list);
	snd_soc_component_del_unlocked(&platform->component);
	mutex_unlock(&client_mutex);

	dev_dbg(platform->dev, "ASoC: Unregistered platform '%s'\n",
		platform->component.name);

	snd_soc_component_cleanup(&platform->component);
}
EXPORT_SYMBOL_GPL(snd_soc_remove_platform);

struct snd_soc_platform *snd_soc_lookup_platform(struct device *dev)
{
	struct snd_soc_platform *platform;

	mutex_lock(&client_mutex);
	list_for_each_entry(platform, &platform_list, list) {
		if (dev == platform->dev) {
			mutex_unlock(&client_mutex);
			return platform;
		}
	}
	mutex_unlock(&client_mutex);

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_lookup_platform);

/**
 * snd_soc_unregister_platform - Unregister a platform from the ASoC core
 *
 * @dev: platform to unregister
 */
void snd_soc_unregister_platform(struct device *dev)
{
	struct snd_soc_platform *platform;

	platform = snd_soc_lookup_platform(dev);
	if (!platform)
		return;

	snd_soc_remove_platform(platform);
	kfree(platform);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_platform);

static u64 codec_format_map[] = {
	SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE,
	SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_U16_BE,
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE,
	SNDRV_PCM_FMTBIT_U24_LE | SNDRV_PCM_FMTBIT_U24_BE,
	SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE,
	SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_U32_BE,
	SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_U24_3BE,
	SNDRV_PCM_FMTBIT_U24_3LE | SNDRV_PCM_FMTBIT_U24_3BE,
	SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE,
	SNDRV_PCM_FMTBIT_U20_3LE | SNDRV_PCM_FMTBIT_U20_3BE,
	SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S18_3BE,
	SNDRV_PCM_FMTBIT_U18_3LE | SNDRV_PCM_FMTBIT_U18_3BE,
	SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE,
	SNDRV_PCM_FMTBIT_FLOAT64_LE | SNDRV_PCM_FMTBIT_FLOAT64_BE,
	SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE
	| SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE,
};

/* Fix up the DAI formats for endianness: codecs don't actually see
 * the endianness of the data but we're using the CPU format
 * definitions which do need to include endianness so we ensure that
 * codec DAIs always have both big and little endian variants set.
 */
static void fixup_codec_formats(struct snd_soc_pcm_stream *stream)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(codec_format_map); i++)
		if (stream->formats & codec_format_map[i])
			stream->formats |= codec_format_map[i];
}

static int snd_soc_codec_drv_probe(struct snd_soc_component *component)
{
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);

	return codec->driver->probe(codec);
}

static void snd_soc_codec_drv_remove(struct snd_soc_component *component)
{
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);

	codec->driver->remove(codec);
}

static int snd_soc_codec_drv_write(struct snd_soc_component *component,
	unsigned int reg, unsigned int val)
{
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);

	return codec->driver->write(codec, reg, val);
}

static int snd_soc_codec_drv_read(struct snd_soc_component *component,
	unsigned int reg, unsigned int *val)
{
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);

	*val = codec->driver->read(codec, reg);

	return 0;
}

static int snd_soc_codec_set_bias_level(struct snd_soc_dapm_context *dapm,
	enum snd_soc_bias_level level)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(dapm);

	return codec->driver->set_bias_level(codec, level);
}

/**
 * snd_soc_register_codec - Register a codec with the ASoC core
 *
 * @dev: The parent device for this codec
 * @codec_drv: Codec driver
 * @dai_drv: The associated DAI driver
 * @num_dai: Number of DAIs
 */
int snd_soc_register_codec(struct device *dev,
			   const struct snd_soc_codec_driver *codec_drv,
			   struct snd_soc_dai_driver *dai_drv,
			   int num_dai)
{
	struct snd_soc_dapm_context *dapm;
	struct snd_soc_codec *codec;
	struct snd_soc_dai *dai;
	int ret, i;

	dev_dbg(dev, "codec register %s\n", dev_name(dev));

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	codec->component.codec = codec;

	ret = snd_soc_component_initialize(&codec->component,
			&codec_drv->component_driver, dev);
	if (ret)
		goto err_free;

	if (codec_drv->probe)
		codec->component.probe = snd_soc_codec_drv_probe;
	if (codec_drv->remove)
		codec->component.remove = snd_soc_codec_drv_remove;
	if (codec_drv->write)
		codec->component.write = snd_soc_codec_drv_write;
	if (codec_drv->read)
		codec->component.read = snd_soc_codec_drv_read;
	codec->component.ignore_pmdown_time = codec_drv->ignore_pmdown_time;

	dapm = snd_soc_codec_get_dapm(codec);
	dapm->idle_bias_off = codec_drv->idle_bias_off;
	dapm->suspend_bias_off = codec_drv->suspend_bias_off;
	if (codec_drv->seq_notifier)
		dapm->seq_notifier = codec_drv->seq_notifier;
	if (codec_drv->set_bias_level)
		dapm->set_bias_level = snd_soc_codec_set_bias_level;
	codec->dev = dev;
	codec->driver = codec_drv;
	codec->component.val_bytes = codec_drv->reg_word_size;

#ifdef CONFIG_DEBUG_FS
	codec->component.init_debugfs = soc_init_codec_debugfs;
	codec->component.debugfs_prefix = "codec";
#endif

	if (codec_drv->get_regmap)
		codec->component.regmap = codec_drv->get_regmap(dev);

	for (i = 0; i < num_dai; i++) {
		fixup_codec_formats(&dai_drv[i].playback);
		fixup_codec_formats(&dai_drv[i].capture);
	}

	ret = snd_soc_register_dais(&codec->component, dai_drv, num_dai, false);
	if (ret < 0) {
		dev_err(dev, "ASoC: Failed to register DAIs: %d\n", ret);
		goto err_cleanup;
	}

	list_for_each_entry(dai, &codec->component.dai_list, list)
		dai->codec = codec;

	mutex_lock(&client_mutex);
	snd_soc_component_add_unlocked(&codec->component);
	list_add(&codec->list, &codec_list);
	mutex_unlock(&client_mutex);

	dev_dbg(codec->dev, "ASoC: Registered codec '%s'\n",
		codec->component.name);
	return 0;

err_cleanup:
	snd_soc_component_cleanup(&codec->component);
err_free:
	kfree(codec);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_register_codec);

/**
 * snd_soc_unregister_codec - Unregister a codec from the ASoC core
 *
 * @dev: codec to unregister
 */
void snd_soc_unregister_codec(struct device *dev)
{
	struct snd_soc_codec *codec;

	mutex_lock(&client_mutex);
	list_for_each_entry(codec, &codec_list, list) {
		if (dev == codec->dev)
			goto found;
	}
	mutex_unlock(&client_mutex);
	return;

found:
	list_del(&codec->list);
	snd_soc_component_del_unlocked(&codec->component);
	mutex_unlock(&client_mutex);

	dev_dbg(codec->dev, "ASoC: Unregistered codec '%s'\n",
			codec->component.name);

	snd_soc_component_cleanup(&codec->component);
	snd_soc_cache_exit(codec);
	kfree(codec);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_codec);

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

static int snd_soc_of_get_slot_mask(struct device_node *np,
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

void snd_soc_of_parse_audio_prefix(struct snd_soc_card *card,
				   struct snd_soc_codec_conf *codec_conf,
				   struct device_node *of_node,
				   const char *propname)
{
	struct device_node *np = card->dev->of_node;
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
EXPORT_SYMBOL_GPL(snd_soc_of_parse_audio_prefix);

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

	routes = devm_kzalloc(card->dev, num_routes * sizeof(*routes),
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
	 * check "[prefix]format = xxx"
	 * SND_SOC_DAIFMT_FORMAT_MASK area
	 */
	snprintf(prop, sizeof(prop), "%sformat", prefix);
	ret = of_property_read_string(np, prop, &str);
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

static int snd_soc_get_dai_name(struct of_phandle_args *args,
				const char **dai_name)
{
	struct snd_soc_component *pos;
	struct device_node *component_of_node;
	int ret = -EPROBE_DEFER;

	mutex_lock(&client_mutex);
	list_for_each_entry(pos, &component_list, list) {
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

			*dai_name = pos->dai_drv[id].name;
			if (!*dai_name)
				*dai_name = pos->name;
		}

		break;
	}
	mutex_unlock(&client_mutex);
	return ret;
}

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
 * snd_soc_of_get_dai_link_codecs - Parse a list of CODECs in the devicetree
 * @dev: Card device
 * @of_node: Device node
 * @dai_link: DAI link
 *
 * Builds an array of CODEC DAI components from the DAI link property
 * 'sound-dai'.
 * The array is set in the DAI link and the number of DAIs is set accordingly.
 * The device nodes in the array (of_node) must be dereferenced by the caller.
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
	component = devm_kzalloc(dev,
				 sizeof *component * num_codecs,
				 GFP_KERNEL);
	if (!component)
		return -ENOMEM;
	dai_link->codecs = component;
	dai_link->num_codecs = num_codecs;

	/* Parse the list */
	for (index = 0, component = dai_link->codecs;
	     index < dai_link->num_codecs;
	     index++, component++) {
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
	for (index = 0, component = dai_link->codecs;
	     index < dai_link->num_codecs;
	     index++, component++) {
		if (!component->of_node)
			break;
		of_node_put(component->of_node);
		component->of_node = NULL;
	}
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

#ifdef CONFIG_DEBUG_FS
#endif
	platform_driver_unregister(&soc_driver);
}
module_exit(snd_soc_exit);

/* Module information */
MODULE_AUTHOR("Liam Girdwood, lrg@slimlogic.co.uk");
MODULE_DESCRIPTION("ALSA SoC Core");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:soc-audio");

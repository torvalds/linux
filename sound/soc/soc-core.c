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
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <sound/ac97_codec.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dpcm.h>
#include <sound/initval.h>

#define CREATE_TRACE_POINTS
#include <trace/events/asoc.h>

#define NAME_SIZE	32

static DECLARE_WAIT_QUEUE_HEAD(soc_pm_waitq);

#ifdef CONFIG_DEBUG_FS
struct dentry *snd_soc_debugfs_root;
EXPORT_SYMBOL_GPL(snd_soc_debugfs_root);
#endif

static DEFINE_MUTEX(client_mutex);
static LIST_HEAD(dai_list);
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
	char tmpbuf[len + 1];
	char regbuf[regsize + 1];

	/* since tmpbuf is allocated on the stack, warn the callers if they
	 * try to abuse this function */
	WARN_ON(len > 63);

	/* +2 for ': ' and + 1 for '\n' */
	if (wordsize + regsize + 2 + 1 != len)
		return -EINVAL;

	ret = snd_soc_read(codec, reg);
	if (ret < 0) {
		memset(regbuf, 'X', regsize);
		regbuf[regsize] = '\0';
	} else {
		snprintf(regbuf, regsize + 1, "%.*x", regsize, ret);
	}

	/* prepare the buffer */
	snprintf(tmpbuf, len + 1, "%.*x: %s\n", wordsize, reg, regbuf);
	/* copy it back to the caller without the '\0' */
	memcpy(buf, tmpbuf, len);

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
		if (!snd_soc_codec_readable_register(codec, i))
			continue;
		if (codec->driver->display_register) {
			count += codec->driver->display_register(codec, buf + count,
							 PAGE_SIZE - count, i);
		} else {
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

	ret = strict_strtol(buf, 10, &rtd->pmdown_time);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR(pmdown_time, 0644, pmdown_time_show, pmdown_time_set);

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

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;
	reg = simple_strtoul(start, &start, 16);
	while (*start == ' ')
		start++;
	if (strict_strtoul(start, 16, &value))
		return -EINVAL;

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

static void soc_init_codec_debugfs(struct snd_soc_codec *codec)
{
	struct dentry *debugfs_card_root = codec->card->debugfs_card_root;

	codec->debugfs_codec_root = debugfs_create_dir(codec->name,
						       debugfs_card_root);
	if (!codec->debugfs_codec_root) {
		dev_warn(codec->dev, "ASoC: Failed to create codec debugfs"
			" directory\n");
		return;
	}

	debugfs_create_bool("cache_sync", 0444, codec->debugfs_codec_root,
			    &codec->cache_sync);
	debugfs_create_bool("cache_only", 0444, codec->debugfs_codec_root,
			    &codec->cache_only);

	codec->debugfs_reg = debugfs_create_file("codec_reg", 0644,
						 codec->debugfs_codec_root,
						 codec, &codec_reg_fops);
	if (!codec->debugfs_reg)
		dev_warn(codec->dev, "ASoC: Failed to create codec register"
			" debugfs file\n");

	snd_soc_dapm_debugfs_init(&codec->dapm, codec->debugfs_codec_root);
}

static void soc_cleanup_codec_debugfs(struct snd_soc_codec *codec)
{
	debugfs_remove_recursive(codec->debugfs_codec_root);
}

static void soc_init_platform_debugfs(struct snd_soc_platform *platform)
{
	struct dentry *debugfs_card_root = platform->card->debugfs_card_root;

	platform->debugfs_platform_root = debugfs_create_dir(platform->name,
						       debugfs_card_root);
	if (!platform->debugfs_platform_root) {
		dev_warn(platform->dev,
			"ASoC: Failed to create platform debugfs directory\n");
		return;
	}

	snd_soc_dapm_debugfs_init(&platform->dapm,
		platform->debugfs_platform_root);
}

static void soc_cleanup_platform_debugfs(struct snd_soc_platform *platform)
{
	debugfs_remove_recursive(platform->debugfs_platform_root);
}

static ssize_t codec_list_read_file(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	ssize_t len, ret = 0;
	struct snd_soc_codec *codec;

	if (!buf)
		return -ENOMEM;

	list_for_each_entry(codec, &codec_list, list) {
		len = snprintf(buf + ret, PAGE_SIZE - ret, "%s\n",
			       codec->name);
		if (len >= 0)
			ret += len;
		if (ret > PAGE_SIZE) {
			ret = PAGE_SIZE;
			break;
		}
	}

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
	struct snd_soc_dai *dai;

	if (!buf)
		return -ENOMEM;

	list_for_each_entry(dai, &dai_list, list) {
		len = snprintf(buf + ret, PAGE_SIZE - ret, "%s\n", dai->name);
		if (len >= 0)
			ret += len;
		if (ret > PAGE_SIZE) {
			ret = PAGE_SIZE;
			break;
		}
	}

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

	list_for_each_entry(platform, &platform_list, list) {
		len = snprintf(buf + ret, PAGE_SIZE - ret, "%s\n",
			       platform->name);
		if (len >= 0)
			ret += len;
		if (ret > PAGE_SIZE) {
			ret = PAGE_SIZE;
			break;
		}
	}

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

#else

static inline void soc_init_codec_debugfs(struct snd_soc_codec *codec)
{
}

static inline void soc_cleanup_codec_debugfs(struct snd_soc_codec *codec)
{
}

static inline void soc_init_platform_debugfs(struct snd_soc_platform *platform)
{
}

static inline void soc_cleanup_platform_debugfs(struct snd_soc_platform *platform)
{
}

static inline void soc_init_card_debugfs(struct snd_soc_card *card)
{
}

static inline void soc_cleanup_card_debugfs(struct snd_soc_card *card)
{
}
#endif

struct snd_pcm_substream *snd_soc_get_dai_substream(struct snd_soc_card *card,
		const char *dai_link, int stream)
{
	int i;

	for (i = 0; i < card->num_links; i++) {
		if (card->rtd[i].dai_link->no_pcm &&
			!strcmp(card->rtd[i].dai_link->name, dai_link))
			return card->rtd[i].pcm->streams[stream].substream;
	}
	dev_dbg(card->dev, "ASoC: failed to find dai link %s\n", dai_link);
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_get_dai_substream);

struct snd_soc_pcm_runtime *snd_soc_get_pcm_runtime(struct snd_soc_card *card,
		const char *dai_link)
{
	int i;

	for (i = 0; i < card->num_links; i++) {
		if (!strcmp(card->rtd[i].dai_link->name, dai_link))
			return &card->rtd[i];
	}
	dev_dbg(card->dev, "ASoC: failed to find rtd %s\n", dai_link);
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_get_pcm_runtime);

#ifdef CONFIG_SND_SOC_AC97_BUS
/* unregister ac97 codec */
static int soc_ac97_dev_unregister(struct snd_soc_codec *codec)
{
	if (codec->ac97->dev.bus)
		device_unregister(&codec->ac97->dev);
	return 0;
}

/* stop no dev release warning */
static void soc_ac97_device_release(struct device *dev){}

/* register ac97 codec to bus */
static int soc_ac97_dev_register(struct snd_soc_codec *codec)
{
	int err;

	codec->ac97->dev.bus = &ac97_bus_type;
	codec->ac97->dev.parent = codec->card->dev;
	codec->ac97->dev.release = soc_ac97_device_release;

	dev_set_name(&codec->ac97->dev, "%d-%d:%s",
		     codec->card->snd_card->number, 0, codec->name);
	err = device_register(&codec->ac97->dev);
	if (err < 0) {
		dev_err(codec->dev, "ASoC: Can't register ac97 bus\n");
		codec->ac97->dev.bus = NULL;
		return err;
	}
	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
/* powers down audio subsystem for suspend */
int snd_soc_suspend(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct snd_soc_codec *codec;
	int i;

	/* If the initialization of this soc device failed, there is no codec
	 * associated with it. Just bail out in this case.
	 */
	if (list_empty(&card->codec_dev_list))
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
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai *dai = card->rtd[i].codec_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		if (drv->ops->digital_mute && dai->playback_active)
			drv->ops->digital_mute(dai, 1);
	}

	/* suspend all pcms */
	for (i = 0; i < card->num_rtd; i++) {
		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		snd_pcm_suspend_all(card->rtd[i].pcm);
	}

	if (card->suspend_pre)
		card->suspend_pre(card);

	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai *cpu_dai = card->rtd[i].cpu_dai;
		struct snd_soc_platform *platform = card->rtd[i].platform;

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->suspend && !cpu_dai->driver->ac97_control)
			cpu_dai->driver->suspend(cpu_dai);
		if (platform->driver->suspend && !platform->suspended) {
			platform->driver->suspend(cpu_dai);
			platform->suspended = 1;
		}
	}

	/* close any waiting streams and save state */
	for (i = 0; i < card->num_rtd; i++) {
		flush_delayed_work(&card->rtd[i].delayed_work);
		card->rtd[i].codec->dapm.suspend_bias_level = card->rtd[i].codec->dapm.bias_level;
	}

	for (i = 0; i < card->num_rtd; i++) {

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		snd_soc_dapm_stream_event(&card->rtd[i],
					  SNDRV_PCM_STREAM_PLAYBACK,
					  SND_SOC_DAPM_STREAM_SUSPEND);

		snd_soc_dapm_stream_event(&card->rtd[i],
					  SNDRV_PCM_STREAM_CAPTURE,
					  SND_SOC_DAPM_STREAM_SUSPEND);
	}

	/* Recheck all analogue paths too */
	dapm_mark_io_dirty(&card->dapm);
	snd_soc_dapm_sync(&card->dapm);

	/* suspend all CODECs */
	list_for_each_entry(codec, &card->codec_dev_list, card_list) {
		/* If there are paths active then the CODEC will be held with
		 * bias _ON and should not be suspended. */
		if (!codec->suspended && codec->driver->suspend) {
			switch (codec->dapm.bias_level) {
			case SND_SOC_BIAS_STANDBY:
				/*
				 * If the CODEC is capable of idle
				 * bias off then being in STANDBY
				 * means it's doing something,
				 * otherwise fall through.
				 */
				if (codec->dapm.idle_bias_off) {
					dev_dbg(codec->dev,
						"ASoC: idle_bias_off CODEC on"
						" over suspend\n");
					break;
				}
			case SND_SOC_BIAS_OFF:
				codec->driver->suspend(codec);
				codec->suspended = 1;
				codec->cache_sync = 1;
				if (codec->using_regmap)
					regcache_mark_dirty(codec->control_data);
				break;
			default:
				dev_dbg(codec->dev, "ASoC: CODEC is on"
					" over suspend\n");
				break;
			}
		}
	}

	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai *cpu_dai = card->rtd[i].cpu_dai;

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->suspend && cpu_dai->driver->ac97_control)
			cpu_dai->driver->suspend(cpu_dai);
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

	/* resume AC97 DAIs */
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai *cpu_dai = card->rtd[i].cpu_dai;

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->resume && cpu_dai->driver->ac97_control)
			cpu_dai->driver->resume(cpu_dai);
	}

	list_for_each_entry(codec, &card->codec_dev_list, card_list) {
		/* If the CODEC was idle over suspend then it will have been
		 * left with bias OFF or STANDBY and suspended so we must now
		 * resume.  Otherwise the suspend was suppressed.
		 */
		if (codec->driver->resume && codec->suspended) {
			switch (codec->dapm.bias_level) {
			case SND_SOC_BIAS_STANDBY:
			case SND_SOC_BIAS_OFF:
				codec->driver->resume(codec);
				codec->suspended = 0;
				break;
			default:
				dev_dbg(codec->dev, "ASoC: CODEC was on over"
					" suspend\n");
				break;
			}
		}
	}

	for (i = 0; i < card->num_rtd; i++) {

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		snd_soc_dapm_stream_event(&card->rtd[i],
					  SNDRV_PCM_STREAM_PLAYBACK,
					  SND_SOC_DAPM_STREAM_RESUME);

		snd_soc_dapm_stream_event(&card->rtd[i],
					  SNDRV_PCM_STREAM_CAPTURE,
					  SND_SOC_DAPM_STREAM_RESUME);
	}

	/* unmute any active DACs */
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai *dai = card->rtd[i].codec_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		if (drv->ops->digital_mute && dai->playback_active)
			drv->ops->digital_mute(dai, 0);
	}

	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai *cpu_dai = card->rtd[i].cpu_dai;
		struct snd_soc_platform *platform = card->rtd[i].platform;

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		if (cpu_dai->driver->resume && !cpu_dai->driver->ac97_control)
			cpu_dai->driver->resume(cpu_dai);
		if (platform->driver->resume && platform->suspended) {
			platform->driver->resume(cpu_dai);
			platform->suspended = 0;
		}
	}

	if (card->resume_post)
		card->resume_post(card);

	dev_dbg(card->dev, "ASoC: resume work completed\n");

	/* userspace can access us now we are back as we were before */
	snd_power_change_state(card->snd_card, SNDRV_CTL_POWER_D0);

	/* Recheck all analogue paths too */
	dapm_mark_io_dirty(&card->dapm);
	snd_soc_dapm_sync(&card->dapm);
}

/* powers up audio subsystem after a suspend */
int snd_soc_resume(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	int i, ac97_control = 0;

	/* If the initialization of this soc device failed, there is no codec
	 * associated with it. Just bail out in this case.
	 */
	if (list_empty(&card->codec_dev_list))
		return 0;

	/* AC97 devices might have other drivers hanging off them so
	 * need to resume immediately.  Other drivers don't have that
	 * problem and may take a substantial amount of time to resume
	 * due to I/O costs and anti-pop so handle them out of line.
	 */
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai *cpu_dai = card->rtd[i].cpu_dai;
		ac97_control |= cpu_dai->driver->ac97_control;
	}
	if (ac97_control) {
		dev_dbg(dev, "ASoC: Resuming AC97 immediately\n");
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

static int soc_bind_dai_link(struct snd_soc_card *card, int num)
{
	struct snd_soc_dai_link *dai_link = &card->dai_link[num];
	struct snd_soc_pcm_runtime *rtd = &card->rtd[num];
	struct snd_soc_codec *codec;
	struct snd_soc_platform *platform;
	struct snd_soc_dai *codec_dai, *cpu_dai;
	const char *platform_name;

	dev_dbg(card->dev, "ASoC: binding %s at idx %d\n", dai_link->name, num);

	/* Find CPU DAI from registered DAIs*/
	list_for_each_entry(cpu_dai, &dai_list, list) {
		if (dai_link->cpu_of_node &&
		    (cpu_dai->dev->of_node != dai_link->cpu_of_node))
			continue;
		if (dai_link->cpu_name &&
		    strcmp(dev_name(cpu_dai->dev), dai_link->cpu_name))
			continue;
		if (dai_link->cpu_dai_name &&
		    strcmp(cpu_dai->name, dai_link->cpu_dai_name))
			continue;

		rtd->cpu_dai = cpu_dai;
	}

	if (!rtd->cpu_dai) {
		dev_err(card->dev, "ASoC: CPU DAI %s not registered\n",
			dai_link->cpu_dai_name);
		return -EPROBE_DEFER;
	}

	/* Find CODEC from registered CODECs */
	list_for_each_entry(codec, &codec_list, list) {
		if (dai_link->codec_of_node) {
			if (codec->dev->of_node != dai_link->codec_of_node)
				continue;
		} else {
			if (strcmp(codec->name, dai_link->codec_name))
				continue;
		}

		rtd->codec = codec;

		/*
		 * CODEC found, so find CODEC DAI from registered DAIs from
		 * this CODEC
		 */
		list_for_each_entry(codec_dai, &dai_list, list) {
			if (codec->dev == codec_dai->dev &&
				!strcmp(codec_dai->name,
					dai_link->codec_dai_name)) {

				rtd->codec_dai = codec_dai;
			}
		}

		if (!rtd->codec_dai) {
			dev_err(card->dev, "ASoC: CODEC DAI %s not registered\n",
				dai_link->codec_dai_name);
			return -EPROBE_DEFER;
		}
	}

	if (!rtd->codec) {
		dev_err(card->dev, "ASoC: CODEC %s not registered\n",
			dai_link->codec_name);
		return -EPROBE_DEFER;
	}

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
			if (strcmp(platform->name, platform_name))
				continue;
		}

		rtd->platform = platform;
	}
	if (!rtd->platform) {
		dev_err(card->dev, "ASoC: platform %s not registered\n",
			dai_link->platform_name);
		return -EPROBE_DEFER;
	}

	card->num_rtd++;

	return 0;
}

static int soc_remove_platform(struct snd_soc_platform *platform)
{
	int ret;

	if (platform->driver->remove) {
		ret = platform->driver->remove(platform);
		if (ret < 0)
			dev_err(platform->dev, "ASoC: failed to remove %d\n",
				ret);
	}

	/* Make sure all DAPM widgets are freed */
	snd_soc_dapm_free(&platform->dapm);

	soc_cleanup_platform_debugfs(platform);
	platform->probed = 0;
	list_del(&platform->card_list);
	module_put(platform->dev->driver->owner);

	return 0;
}

static void soc_remove_codec(struct snd_soc_codec *codec)
{
	int err;

	if (codec->driver->remove) {
		err = codec->driver->remove(codec);
		if (err < 0)
			dev_err(codec->dev, "ASoC: failed to remove %d\n", err);
	}

	/* Make sure all DAPM widgets are freed */
	snd_soc_dapm_free(&codec->dapm);

	soc_cleanup_codec_debugfs(codec);
	codec->probed = 0;
	list_del(&codec->card_list);
	module_put(codec->dev->driver->owner);
}

static void soc_remove_link_dais(struct snd_soc_card *card, int num, int order)
{
	struct snd_soc_pcm_runtime *rtd = &card->rtd[num];
	struct snd_soc_dai *codec_dai = rtd->codec_dai, *cpu_dai = rtd->cpu_dai;
	int err;

	/* unregister the rtd device */
	if (rtd->dev_registered) {
		device_remove_file(rtd->dev, &dev_attr_pmdown_time);
		device_remove_file(rtd->dev, &dev_attr_codec_reg);
		device_unregister(rtd->dev);
		rtd->dev_registered = 0;
	}

	/* remove the CODEC DAI */
	if (codec_dai && codec_dai->probed &&
			codec_dai->driver->remove_order == order) {
		if (codec_dai->driver->remove) {
			err = codec_dai->driver->remove(codec_dai);
			if (err < 0)
				dev_err(codec_dai->dev,
					"ASoC: failed to remove %s: %d\n",
					codec_dai->name, err);
		}
		codec_dai->probed = 0;
		list_del(&codec_dai->card_list);
	}

	/* remove the cpu_dai */
	if (cpu_dai && cpu_dai->probed &&
			cpu_dai->driver->remove_order == order) {
		if (cpu_dai->driver->remove) {
			err = cpu_dai->driver->remove(cpu_dai);
			if (err < 0)
				dev_err(cpu_dai->dev,
					"ASoC: failed to remove %s: %d\n",
					cpu_dai->name, err);
		}
		cpu_dai->probed = 0;
		list_del(&cpu_dai->card_list);

		if (!cpu_dai->codec) {
			snd_soc_dapm_free(&cpu_dai->dapm);
			module_put(cpu_dai->dev->driver->owner);
		}
	}
}

static void soc_remove_link_components(struct snd_soc_card *card, int num,
				       int order)
{
	struct snd_soc_pcm_runtime *rtd = &card->rtd[num];
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_codec *codec;

	/* remove the platform */
	if (platform && platform->probed &&
	    platform->driver->remove_order == order) {
		soc_remove_platform(platform);
	}

	/* remove the CODEC-side CODEC */
	if (codec_dai) {
		codec = codec_dai->codec;
		if (codec && codec->probed &&
		    codec->driver->remove_order == order)
			soc_remove_codec(codec);
	}

	/* remove any CPU-side CODEC */
	if (cpu_dai) {
		codec = cpu_dai->codec;
		if (codec && codec->probed &&
		    codec->driver->remove_order == order)
			soc_remove_codec(codec);
	}
}

static void soc_remove_dai_links(struct snd_soc_card *card)
{
	int dai, order;

	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
			order++) {
		for (dai = 0; dai < card->num_rtd; dai++)
			soc_remove_link_dais(card, dai, order);
	}

	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
			order++) {
		for (dai = 0; dai < card->num_rtd; dai++)
			soc_remove_link_components(card, dai, order);
	}

	card->num_rtd = 0;
}

static void soc_set_name_prefix(struct snd_soc_card *card,
				struct snd_soc_codec *codec)
{
	int i;

	if (card->codec_conf == NULL)
		return;

	for (i = 0; i < card->num_configs; i++) {
		struct snd_soc_codec_conf *map = &card->codec_conf[i];
		if (map->dev_name && !strcmp(codec->name, map->dev_name)) {
			codec->name_prefix = map->name_prefix;
			break;
		}
	}
}

static int soc_probe_codec(struct snd_soc_card *card,
			   struct snd_soc_codec *codec)
{
	int ret = 0;
	const struct snd_soc_codec_driver *driver = codec->driver;
	struct snd_soc_dai *dai;

	codec->card = card;
	codec->dapm.card = card;
	soc_set_name_prefix(card, codec);

	if (!try_module_get(codec->dev->driver->owner))
		return -ENODEV;

	soc_init_codec_debugfs(codec);

	if (driver->dapm_widgets)
		snd_soc_dapm_new_controls(&codec->dapm, driver->dapm_widgets,
					  driver->num_dapm_widgets);

	/* Create DAPM widgets for each DAI stream */
	list_for_each_entry(dai, &dai_list, list) {
		if (dai->dev != codec->dev)
			continue;

		snd_soc_dapm_new_dai_widgets(&codec->dapm, dai);
	}

	codec->dapm.idle_bias_off = driver->idle_bias_off;

	if (driver->probe) {
		ret = driver->probe(codec);
		if (ret < 0) {
			dev_err(codec->dev,
				"ASoC: failed to probe CODEC %d\n", ret);
			goto err_probe;
		}
		WARN(codec->dapm.idle_bias_off &&
			codec->dapm.bias_level != SND_SOC_BIAS_OFF,
			"codec %s can not start from non-off bias"
			" with idle_bias_off==1\n", codec->name);
	}

	/* If the driver didn't set I/O up try regmap */
	if (!codec->write && dev_get_regmap(codec->dev, NULL))
		snd_soc_codec_set_cache_io(codec, 0, 0, SND_SOC_REGMAP);

	if (driver->controls)
		snd_soc_add_codec_controls(codec, driver->controls,
				     driver->num_controls);
	if (driver->dapm_routes)
		snd_soc_dapm_add_routes(&codec->dapm, driver->dapm_routes,
					driver->num_dapm_routes);

	/* mark codec as probed and add to card codec list */
	codec->probed = 1;
	list_add(&codec->card_list, &card->codec_dev_list);
	list_add(&codec->dapm.list, &card->dapm_list);

	return 0;

err_probe:
	soc_cleanup_codec_debugfs(codec);
	module_put(codec->dev->driver->owner);

	return ret;
}

static int soc_probe_platform(struct snd_soc_card *card,
			   struct snd_soc_platform *platform)
{
	int ret = 0;
	const struct snd_soc_platform_driver *driver = platform->driver;
	struct snd_soc_dai *dai;

	platform->card = card;
	platform->dapm.card = card;

	if (!try_module_get(platform->dev->driver->owner))
		return -ENODEV;

	soc_init_platform_debugfs(platform);

	if (driver->dapm_widgets)
		snd_soc_dapm_new_controls(&platform->dapm,
			driver->dapm_widgets, driver->num_dapm_widgets);

	/* Create DAPM widgets for each DAI stream */
	list_for_each_entry(dai, &dai_list, list) {
		if (dai->dev != platform->dev)
			continue;

		snd_soc_dapm_new_dai_widgets(&platform->dapm, dai);
	}

	platform->dapm.idle_bias_off = 1;

	if (driver->probe) {
		ret = driver->probe(platform);
		if (ret < 0) {
			dev_err(platform->dev,
				"ASoC: failed to probe platform %d\n", ret);
			goto err_probe;
		}
	}

	if (driver->controls)
		snd_soc_add_platform_controls(platform, driver->controls,
				     driver->num_controls);
	if (driver->dapm_routes)
		snd_soc_dapm_add_routes(&platform->dapm, driver->dapm_routes,
					driver->num_dapm_routes);

	/* mark platform as probed and add to card platform list */
	platform->probed = 1;
	list_add(&platform->card_list, &card->platform_dev_list);
	list_add(&platform->dapm.list, &card->dapm_list);

	return 0;

err_probe:
	soc_cleanup_platform_debugfs(platform);
	module_put(platform->dev->driver->owner);

	return ret;
}

static void rtd_release(struct device *dev)
{
	kfree(dev);
}

static int soc_post_component_init(struct snd_soc_card *card,
				   struct snd_soc_codec *codec,
				   int num, int dailess)
{
	struct snd_soc_dai_link *dai_link = NULL;
	struct snd_soc_aux_dev *aux_dev = NULL;
	struct snd_soc_pcm_runtime *rtd;
	const char *temp, *name;
	int ret = 0;

	if (!dailess) {
		dai_link = &card->dai_link[num];
		rtd = &card->rtd[num];
		name = dai_link->name;
	} else {
		aux_dev = &card->aux_dev[num];
		rtd = &card->rtd_aux[num];
		name = aux_dev->name;
	}
	rtd->card = card;

	/* Make sure all DAPM widgets are instantiated */
	snd_soc_dapm_new_widgets(&codec->dapm);

	/* machine controls, routes and widgets are not prefixed */
	temp = codec->name_prefix;
	codec->name_prefix = NULL;

	/* do machine specific initialization */
	if (!dailess && dai_link->init)
		ret = dai_link->init(rtd);
	else if (dailess && aux_dev->init)
		ret = aux_dev->init(&codec->dapm);
	if (ret < 0) {
		dev_err(card->dev, "ASoC: failed to init %s: %d\n", name, ret);
		return ret;
	}
	codec->name_prefix = temp;

	/* register the rtd device */
	rtd->codec = codec;

	rtd->dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!rtd->dev)
		return -ENOMEM;
	device_initialize(rtd->dev);
	rtd->dev->parent = card->dev;
	rtd->dev->release = rtd_release;
	rtd->dev->init_name = name;
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
		dev_err(card->dev,
			"ASoC: failed to register runtime device: %d\n", ret);
		return ret;
	}
	rtd->dev_registered = 1;

	/* add DAPM sysfs entries for this codec */
	ret = snd_soc_dapm_sys_add(rtd->dev);
	if (ret < 0)
		dev_err(codec->dev,
			"ASoC: failed to add codec dapm sysfs entries: %d\n", ret);

	/* add codec sysfs entries */
	ret = device_create_file(rtd->dev, &dev_attr_codec_reg);
	if (ret < 0)
		dev_err(codec->dev,
			"ASoC: failed to add codec sysfs files: %d\n", ret);

#ifdef CONFIG_DEBUG_FS
	/* add DPCM sysfs entries */
	if (!dailess && !dai_link->dynamic)
		goto out;

	ret = soc_dpcm_debugfs_add(rtd);
	if (ret < 0)
		dev_err(rtd->dev, "ASoC: failed to add dpcm sysfs entries: %d\n", ret);

out:
#endif
	return 0;
}

static int soc_probe_link_components(struct snd_soc_card *card, int num,
				     int order)
{
	struct snd_soc_pcm_runtime *rtd = &card->rtd[num];
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_platform *platform = rtd->platform;
	int ret;

	/* probe the CPU-side component, if it is a CODEC */
	if (cpu_dai->codec &&
	    !cpu_dai->codec->probed &&
	    cpu_dai->codec->driver->probe_order == order) {
		ret = soc_probe_codec(card, cpu_dai->codec);
		if (ret < 0)
			return ret;
	}

	/* probe the CODEC-side component */
	if (!codec_dai->codec->probed &&
	    codec_dai->codec->driver->probe_order == order) {
		ret = soc_probe_codec(card, codec_dai->codec);
		if (ret < 0)
			return ret;
	}

	/* probe the platform */
	if (!platform->probed &&
	    platform->driver->probe_order == order) {
		ret = soc_probe_platform(card, platform);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int soc_probe_link_dais(struct snd_soc_card *card, int num, int order)
{
	struct snd_soc_dai_link *dai_link = &card->dai_link[num];
	struct snd_soc_pcm_runtime *rtd = &card->rtd[num];
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dapm_widget *play_w, *capture_w;
	int ret;

	dev_dbg(card->dev, "ASoC: probe %s dai link %d late %d\n",
			card->name, num, order);

	/* config components */
	cpu_dai->platform = platform;
	codec_dai->card = card;
	cpu_dai->card = card;

	/* set default power off timeout */
	rtd->pmdown_time = pmdown_time;

	/* probe the cpu_dai */
	if (!cpu_dai->probed &&
			cpu_dai->driver->probe_order == order) {
		if (!cpu_dai->codec) {
			cpu_dai->dapm.card = card;
			if (!try_module_get(cpu_dai->dev->driver->owner))
				return -ENODEV;

			list_add(&cpu_dai->dapm.list, &card->dapm_list);
			snd_soc_dapm_new_dai_widgets(&cpu_dai->dapm, cpu_dai);
		}

		if (cpu_dai->driver->probe) {
			ret = cpu_dai->driver->probe(cpu_dai);
			if (ret < 0) {
				dev_err(cpu_dai->dev,
					"ASoC: failed to probe CPU DAI %s: %d\n",
					cpu_dai->name, ret);
				module_put(cpu_dai->dev->driver->owner);
				return ret;
			}
		}
		cpu_dai->probed = 1;
		/* mark cpu_dai as probed and add to card dai list */
		list_add(&cpu_dai->card_list, &card->dai_dev_list);
	}

	/* probe the CODEC DAI */
	if (!codec_dai->probed && codec_dai->driver->probe_order == order) {
		if (codec_dai->driver->probe) {
			ret = codec_dai->driver->probe(codec_dai);
			if (ret < 0) {
				dev_err(codec_dai->dev,
					"ASoC: failed to probe CODEC DAI %s: %d\n",
					codec_dai->name, ret);
				return ret;
			}
		}

		/* mark codec_dai as probed and add to card dai list */
		codec_dai->probed = 1;
		list_add(&codec_dai->card_list, &card->dai_dev_list);
	}

	/* complete DAI probe during last probe */
	if (order != SND_SOC_COMP_ORDER_LAST)
		return 0;

	ret = soc_post_component_init(card, codec, num, 0);
	if (ret)
		return ret;

	ret = device_create_file(rtd->dev, &dev_attr_pmdown_time);
	if (ret < 0)
		dev_warn(rtd->dev, "ASoC: failed to add pmdown_time sysfs: %d\n",
			ret);

	if (cpu_dai->driver->compress_dai) {
		/*create compress_device"*/
		ret = soc_new_compress(rtd, num);
		if (ret < 0) {
			dev_err(card->dev, "ASoC: can't create compress %s\n",
					 dai_link->stream_name);
			return ret;
		}
	} else {

		if (!dai_link->params) {
			/* create the pcm */
			ret = soc_new_pcm(rtd, num);
			if (ret < 0) {
				dev_err(card->dev, "ASoC: can't create pcm %s :%d\n",
				       dai_link->stream_name, ret);
				return ret;
			}
		} else {
			/* link the DAI widgets */
			play_w = codec_dai->playback_widget;
			capture_w = cpu_dai->capture_widget;
			if (play_w && capture_w) {
				ret = snd_soc_dapm_new_pcm(card, dai_link->params,
						   capture_w, play_w);
				if (ret != 0) {
					dev_err(card->dev, "ASoC: Can't link %s to %s: %d\n",
						play_w->name, capture_w->name, ret);
					return ret;
				}
			}

			play_w = cpu_dai->playback_widget;
			capture_w = codec_dai->capture_widget;
			if (play_w && capture_w) {
				ret = snd_soc_dapm_new_pcm(card, dai_link->params,
						   capture_w, play_w);
				if (ret != 0) {
					dev_err(card->dev, "ASoC: Can't link %s to %s: %d\n",
						play_w->name, capture_w->name, ret);
					return ret;
				}
			}
		}
	}

	/* add platform data for AC97 devices */
	if (rtd->codec_dai->driver->ac97_control)
		snd_ac97_dev_add_pdata(codec->ac97, rtd->cpu_dai->ac97_pdata);

	return 0;
}

#ifdef CONFIG_SND_SOC_AC97_BUS
static int soc_register_ac97_dai_link(struct snd_soc_pcm_runtime *rtd)
{
	int ret;

	/* Only instantiate AC97 if not already done by the adaptor
	 * for the generic AC97 subsystem.
	 */
	if (rtd->codec_dai->driver->ac97_control && !rtd->codec->ac97_registered) {
		/*
		 * It is possible that the AC97 device is already registered to
		 * the device subsystem. This happens when the device is created
		 * via snd_ac97_mixer(). Currently only SoC codec that does so
		 * is the generic AC97 glue but others migh emerge.
		 *
		 * In those cases we don't try to register the device again.
		 */
		if (!rtd->codec->ac97_created)
			return 0;

		ret = soc_ac97_dev_register(rtd->codec);
		if (ret < 0) {
			dev_err(rtd->codec->dev,
				"ASoC: AC97 device register failed: %d\n", ret);
			return ret;
		}

		rtd->codec->ac97_registered = 1;
	}
	return 0;
}

static void soc_unregister_ac97_dai_link(struct snd_soc_codec *codec)
{
	if (codec->ac97_registered) {
		soc_ac97_dev_unregister(codec);
		codec->ac97_registered = 0;
	}
}
#endif

static int soc_check_aux_dev(struct snd_soc_card *card, int num)
{
	struct snd_soc_aux_dev *aux_dev = &card->aux_dev[num];
	struct snd_soc_codec *codec;

	/* find CODEC from registered CODECs*/
	list_for_each_entry(codec, &codec_list, list) {
		if (!strcmp(codec->name, aux_dev->codec_name))
			return 0;
	}

	dev_err(card->dev, "ASoC: %s not registered\n", aux_dev->codec_name);

	return -EPROBE_DEFER;
}

static int soc_probe_aux_dev(struct snd_soc_card *card, int num)
{
	struct snd_soc_aux_dev *aux_dev = &card->aux_dev[num];
	struct snd_soc_codec *codec;
	int ret = -ENODEV;

	/* find CODEC from registered CODECs*/
	list_for_each_entry(codec, &codec_list, list) {
		if (!strcmp(codec->name, aux_dev->codec_name)) {
			if (codec->probed) {
				dev_err(codec->dev,
					"ASoC: codec already probed");
				ret = -EBUSY;
				goto out;
			}
			goto found;
		}
	}
	/* codec not found */
	dev_err(card->dev, "ASoC: codec %s not found", aux_dev->codec_name);
	return -EPROBE_DEFER;

found:
	ret = soc_probe_codec(card, codec);
	if (ret < 0)
		return ret;

	ret = soc_post_component_init(card, codec, num, 1);

out:
	return ret;
}

static void soc_remove_aux_dev(struct snd_soc_card *card, int num)
{
	struct snd_soc_pcm_runtime *rtd = &card->rtd_aux[num];
	struct snd_soc_codec *codec = rtd->codec;

	/* unregister the rtd device */
	if (rtd->dev_registered) {
		device_remove_file(rtd->dev, &dev_attr_codec_reg);
		device_unregister(rtd->dev);
		rtd->dev_registered = 0;
	}

	if (codec && codec->probed)
		soc_remove_codec(codec);
}

static int snd_soc_init_codec_cache(struct snd_soc_codec *codec,
				    enum snd_soc_compress_type compress_type)
{
	int ret;

	if (codec->cache_init)
		return 0;

	/* override the compress_type if necessary */
	if (compress_type && codec->compress_type != compress_type)
		codec->compress_type = compress_type;
	ret = snd_soc_cache_init(codec);
	if (ret < 0) {
		dev_err(codec->dev, "ASoC: Failed to set cache compression"
			" type: %d\n", ret);
		return ret;
	}
	codec->cache_init = 1;
	return 0;
}

static int snd_soc_instantiate_card(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec;
	struct snd_soc_codec_conf *codec_conf;
	enum snd_soc_compress_type compress_type;
	struct snd_soc_dai_link *dai_link;
	int ret, i, order, dai_fmt;

	mutex_lock_nested(&card->mutex, SND_SOC_CARD_CLASS_INIT);

	/* bind DAIs */
	for (i = 0; i < card->num_links; i++) {
		ret = soc_bind_dai_link(card, i);
		if (ret != 0)
			goto base_error;
	}

	/* check aux_devs too */
	for (i = 0; i < card->num_aux_devs; i++) {
		ret = soc_check_aux_dev(card, i);
		if (ret != 0)
			goto base_error;
	}

	/* initialize the register cache for each available codec */
	list_for_each_entry(codec, &codec_list, list) {
		if (codec->cache_init)
			continue;
		/* by default we don't override the compress_type */
		compress_type = 0;
		/* check to see if we need to override the compress_type */
		for (i = 0; i < card->num_configs; ++i) {
			codec_conf = &card->codec_conf[i];
			if (!strcmp(codec->name, codec_conf->dev_name)) {
				compress_type = codec_conf->compress_type;
				if (compress_type && compress_type
				    != codec->compress_type)
					break;
			}
		}
		ret = snd_soc_init_codec_cache(codec, compress_type);
		if (ret < 0)
			goto base_error;
	}

	/* card bind complete so register a sound card */
	ret = snd_card_create(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			card->owner, 0, &card->snd_card);
	if (ret < 0) {
		dev_err(card->dev, "ASoC: can't create sound card for"
			" card %s: %d\n", card->name, ret);
		goto base_error;
	}
	card->snd_card->dev = card->dev;

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

	/* initialise the sound card only once */
	if (card->probe) {
		ret = card->probe(card);
		if (ret < 0)
			goto card_probe_error;
	}

	/* probe all components used by DAI links on this card */
	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
			order++) {
		for (i = 0; i < card->num_links; i++) {
			ret = soc_probe_link_components(card, i, order);
			if (ret < 0) {
				dev_err(card->dev,
					"ASoC: failed to instantiate card %d\n",
					ret);
				goto probe_dai_err;
			}
		}
	}

	/* probe all DAI links on this card */
	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST;
			order++) {
		for (i = 0; i < card->num_links; i++) {
			ret = soc_probe_link_dais(card, i, order);
			if (ret < 0) {
				dev_err(card->dev,
					"ASoC: failed to instantiate card %d\n",
					ret);
				goto probe_dai_err;
			}
		}
	}

	for (i = 0; i < card->num_aux_devs; i++) {
		ret = soc_probe_aux_dev(card, i);
		if (ret < 0) {
			dev_err(card->dev,
				"ASoC: failed to add auxiliary devices %d\n",
				ret);
			goto probe_aux_dev_err;
		}
	}

	snd_soc_dapm_link_dai_widgets(card);

	if (card->controls)
		snd_soc_add_card_controls(card, card->controls, card->num_controls);

	if (card->dapm_routes)
		snd_soc_dapm_add_routes(&card->dapm, card->dapm_routes,
					card->num_dapm_routes);

	snd_soc_dapm_new_widgets(&card->dapm);

	for (i = 0; i < card->num_links; i++) {
		dai_link = &card->dai_link[i];
		dai_fmt = dai_link->dai_fmt;

		if (dai_fmt) {
			ret = snd_soc_dai_set_fmt(card->rtd[i].codec_dai,
						  dai_fmt);
			if (ret != 0 && ret != -ENOTSUPP)
				dev_warn(card->rtd[i].codec_dai->dev,
					 "ASoC: Failed to set DAI format: %d\n",
					 ret);
		}

		/* If this is a regular CPU link there will be a platform */
		if (dai_fmt &&
		    (dai_link->platform_name || dai_link->platform_of_node)) {
			ret = snd_soc_dai_set_fmt(card->rtd[i].cpu_dai,
						  dai_fmt);
			if (ret != 0 && ret != -ENOTSUPP)
				dev_warn(card->rtd[i].cpu_dai->dev,
					 "ASoC: Failed to set DAI format: %d\n",
					 ret);
		} else if (dai_fmt) {
			/* Flip the polarity for the "CPU" end */
			dai_fmt &= ~SND_SOC_DAIFMT_MASTER_MASK;
			switch (dai_link->dai_fmt &
				SND_SOC_DAIFMT_MASTER_MASK) {
			case SND_SOC_DAIFMT_CBM_CFM:
				dai_fmt |= SND_SOC_DAIFMT_CBS_CFS;
				break;
			case SND_SOC_DAIFMT_CBM_CFS:
				dai_fmt |= SND_SOC_DAIFMT_CBS_CFM;
				break;
			case SND_SOC_DAIFMT_CBS_CFM:
				dai_fmt |= SND_SOC_DAIFMT_CBM_CFS;
				break;
			case SND_SOC_DAIFMT_CBS_CFS:
				dai_fmt |= SND_SOC_DAIFMT_CBM_CFM;
				break;
			}

			ret = snd_soc_dai_set_fmt(card->rtd[i].cpu_dai,
						  dai_fmt);
			if (ret != 0 && ret != -ENOTSUPP)
				dev_warn(card->rtd[i].cpu_dai->dev,
					 "ASoC: Failed to set DAI format: %d\n",
					 ret);
		}
	}

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

	snd_soc_dapm_new_widgets(&card->dapm);

	if (card->fully_routed)
		list_for_each_entry(codec, &card->codec_dev_list, card_list)
			snd_soc_dapm_auto_nc_codec_pins(codec);

	ret = snd_card_register(card->snd_card);
	if (ret < 0) {
		dev_err(card->dev, "ASoC: failed to register soundcard %d\n",
				ret);
		goto probe_aux_dev_err;
	}

#ifdef CONFIG_SND_SOC_AC97_BUS
	/* register any AC97 codecs */
	for (i = 0; i < card->num_rtd; i++) {
		ret = soc_register_ac97_dai_link(&card->rtd[i]);
		if (ret < 0) {
			dev_err(card->dev, "ASoC: failed to register AC97:"
				" %d\n", ret);
			while (--i >= 0)
				soc_unregister_ac97_dai_link(card->rtd[i].codec);
			goto probe_aux_dev_err;
		}
	}
#endif

	card->instantiated = 1;
	snd_soc_dapm_sync(&card->dapm);
	mutex_unlock(&card->mutex);

	return 0;

probe_aux_dev_err:
	for (i = 0; i < card->num_aux_devs; i++)
		soc_remove_aux_dev(card, i);

probe_dai_err:
	soc_remove_dai_links(card);

card_probe_error:
	if (card->remove)
		card->remove(card);

	snd_card_free(card->snd_card);

base_error:
	mutex_unlock(&card->mutex);

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
	int i;

	/* make sure any delayed work runs */
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_pcm_runtime *rtd = &card->rtd[i];
		flush_delayed_work(&rtd->delayed_work);
	}

	/* remove auxiliary devices */
	for (i = 0; i < card->num_aux_devs; i++)
		soc_remove_aux_dev(card, i);

	/* remove and free each DAI */
	soc_remove_dai_links(card);

	soc_cleanup_card_debugfs(card);

	/* remove the card */
	if (card->remove)
		card->remove(card);

	snd_soc_dapm_free(&card->dapm);

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
	int i;

	if (!card->instantiated)
		return 0;

	/* Flush out pmdown_time work - we actually do want to run it
	 * now, we're shutting down so no imminent restart. */
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_pcm_runtime *rtd = &card->rtd[i];
		flush_delayed_work(&rtd->delayed_work);
	}

	snd_soc_dapm_shutdown(card);

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
		.owner		= THIS_MODULE,
		.pm		= &snd_soc_pm_ops,
	},
	.probe		= soc_probe,
	.remove		= soc_remove,
};

/**
 * snd_soc_codec_volatile_register: Report if a register is volatile.
 *
 * @codec: CODEC to query.
 * @reg: Register to query.
 *
 * Boolean function indiciating if a CODEC register is volatile.
 */
int snd_soc_codec_volatile_register(struct snd_soc_codec *codec,
				    unsigned int reg)
{
	if (codec->volatile_register)
		return codec->volatile_register(codec, reg);
	else
		return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_volatile_register);

/**
 * snd_soc_codec_readable_register: Report if a register is readable.
 *
 * @codec: CODEC to query.
 * @reg: Register to query.
 *
 * Boolean function indicating if a CODEC register is readable.
 */
int snd_soc_codec_readable_register(struct snd_soc_codec *codec,
				    unsigned int reg)
{
	if (codec->readable_register)
		return codec->readable_register(codec, reg);
	else
		return 1;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_readable_register);

/**
 * snd_soc_codec_writable_register: Report if a register is writable.
 *
 * @codec: CODEC to query.
 * @reg: Register to query.
 *
 * Boolean function indicating if a CODEC register is writable.
 */
int snd_soc_codec_writable_register(struct snd_soc_codec *codec,
				    unsigned int reg)
{
	if (codec->writable_register)
		return codec->writable_register(codec, reg);
	else
		return 1;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_writable_register);

int snd_soc_platform_read(struct snd_soc_platform *platform,
					unsigned int reg)
{
	unsigned int ret;

	if (!platform->driver->read) {
		dev_err(platform->dev, "ASoC: platform has no read back\n");
		return -1;
	}

	ret = platform->driver->read(platform, reg);
	dev_dbg(platform->dev, "read %x => %x\n", reg, ret);
	trace_snd_soc_preg_read(platform, reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_platform_read);

int snd_soc_platform_write(struct snd_soc_platform *platform,
					 unsigned int reg, unsigned int val)
{
	if (!platform->driver->write) {
		dev_err(platform->dev, "ASoC: platform has no write back\n");
		return -1;
	}

	dev_dbg(platform->dev, "write %x = %x\n", reg, val);
	trace_snd_soc_preg_write(platform, reg, val);
	return platform->driver->write(platform, reg, val);
}
EXPORT_SYMBOL_GPL(snd_soc_platform_write);

/**
 * snd_soc_new_ac97_codec - initailise AC97 device
 * @codec: audio codec
 * @ops: AC97 bus operations
 * @num: AC97 codec number
 *
 * Initialises AC97 codec resources for use by ad-hoc devices only.
 */
int snd_soc_new_ac97_codec(struct snd_soc_codec *codec,
	struct snd_ac97_bus_ops *ops, int num)
{
	mutex_lock(&codec->mutex);

	codec->ac97 = kzalloc(sizeof(struct snd_ac97), GFP_KERNEL);
	if (codec->ac97 == NULL) {
		mutex_unlock(&codec->mutex);
		return -ENOMEM;
	}

	codec->ac97->bus = kzalloc(sizeof(struct snd_ac97_bus), GFP_KERNEL);
	if (codec->ac97->bus == NULL) {
		kfree(codec->ac97);
		codec->ac97 = NULL;
		mutex_unlock(&codec->mutex);
		return -ENOMEM;
	}

	codec->ac97->bus->ops = ops;
	codec->ac97->num = num;

	/*
	 * Mark the AC97 device to be created by us. This way we ensure that the
	 * device will be registered with the device subsystem later on.
	 */
	codec->ac97_created = 1;

	mutex_unlock(&codec->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_new_ac97_codec);

struct snd_ac97_bus_ops *soc_ac97_ops;

int snd_soc_set_ac97_ops(struct snd_ac97_bus_ops *ops)
{
	if (ops == soc_ac97_ops)
		return 0;

	if (soc_ac97_ops && ops)
		return -EBUSY;

	soc_ac97_ops = ops;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_set_ac97_ops);

/**
 * snd_soc_free_ac97_codec - free AC97 codec device
 * @codec: audio codec
 *
 * Frees AC97 codec device resources.
 */
void snd_soc_free_ac97_codec(struct snd_soc_codec *codec)
{
	mutex_lock(&codec->mutex);
#ifdef CONFIG_SND_SOC_AC97_BUS
	soc_unregister_ac97_dai_link(codec);
#endif
	kfree(codec->ac97->bus);
	kfree(codec->ac97);
	codec->ac97 = NULL;
	codec->ac97_created = 0;
	mutex_unlock(&codec->mutex);
}
EXPORT_SYMBOL_GPL(snd_soc_free_ac97_codec);

unsigned int snd_soc_read(struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned int ret;

	ret = codec->read(codec, reg);
	dev_dbg(codec->dev, "read %x => %x\n", reg, ret);
	trace_snd_soc_reg_read(codec, reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_read);

unsigned int snd_soc_write(struct snd_soc_codec *codec,
			   unsigned int reg, unsigned int val)
{
	dev_dbg(codec->dev, "write %x = %x\n", reg, val);
	trace_snd_soc_reg_write(codec, reg, val);
	return codec->write(codec, reg, val);
}
EXPORT_SYMBOL_GPL(snd_soc_write);

unsigned int snd_soc_bulk_write_raw(struct snd_soc_codec *codec,
				    unsigned int reg, const void *data, size_t len)
{
	return codec->bulk_write_raw(codec, reg, data, len);
}
EXPORT_SYMBOL_GPL(snd_soc_bulk_write_raw);

/**
 * snd_soc_update_bits - update codec register bits
 * @codec: audio codec
 * @reg: codec register
 * @mask: register mask
 * @value: new value
 *
 * Writes new register value.
 *
 * Returns 1 for change, 0 for no change, or negative error code.
 */
int snd_soc_update_bits(struct snd_soc_codec *codec, unsigned short reg,
				unsigned int mask, unsigned int value)
{
	bool change;
	unsigned int old, new;
	int ret;

	if (codec->using_regmap) {
		ret = regmap_update_bits_check(codec->control_data, reg,
					       mask, value, &change);
	} else {
		ret = snd_soc_read(codec, reg);
		if (ret < 0)
			return ret;

		old = ret;
		new = (old & ~mask) | (value & mask);
		change = old != new;
		if (change)
			ret = snd_soc_write(codec, reg, new);
	}

	if (ret < 0)
		return ret;

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_update_bits);

/**
 * snd_soc_update_bits_locked - update codec register bits
 * @codec: audio codec
 * @reg: codec register
 * @mask: register mask
 * @value: new value
 *
 * Writes new register value, and takes the codec mutex.
 *
 * Returns 1 for change else 0.
 */
int snd_soc_update_bits_locked(struct snd_soc_codec *codec,
			       unsigned short reg, unsigned int mask,
			       unsigned int value)
{
	int change;

	mutex_lock(&codec->mutex);
	change = snd_soc_update_bits(codec, reg, mask, value);
	mutex_unlock(&codec->mutex);

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_update_bits_locked);

/**
 * snd_soc_test_bits - test register for change
 * @codec: audio codec
 * @reg: codec register
 * @mask: register mask
 * @value: new value
 *
 * Tests a register with a new value and checks if the new value is
 * different from the old value.
 *
 * Returns 1 for change else 0.
 */
int snd_soc_test_bits(struct snd_soc_codec *codec, unsigned short reg,
				unsigned int mask, unsigned int value)
{
	int change;
	unsigned int old, new;

	old = snd_soc_read(codec, reg);
	new = (old & ~mask) | value;
	change = old != new;

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_test_bits);

/**
 * snd_soc_set_runtime_hwparams - set the runtime hardware parameters
 * @substream: the pcm substream
 * @hw: the hardware parameters
 *
 * Sets the substream runtime hardware parameters.
 */
int snd_soc_set_runtime_hwparams(struct snd_pcm_substream *substream,
	const struct snd_pcm_hardware *hw)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	runtime->hw.info = hw->info;
	runtime->hw.formats = hw->formats;
	runtime->hw.period_bytes_min = hw->period_bytes_min;
	runtime->hw.period_bytes_max = hw->period_bytes_max;
	runtime->hw.periods_min = hw->periods_min;
	runtime->hw.periods_max = hw->periods_max;
	runtime->hw.buffer_bytes_max = hw->buffer_bytes_max;
	runtime->hw.fifo_size = hw->fifo_size;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_set_runtime_hwparams);

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
	int name_len;

	memcpy(&template, _template, sizeof(template));
	template.index = 0;

	if (!long_name)
		long_name = template.name;

	if (prefix) {
		name_len = strlen(long_name) + strlen(prefix) + 2;
		name = kmalloc(name_len, GFP_KERNEL);
		if (!name)
			return NULL;

		snprintf(name, name_len, "%s %s", prefix, long_name);

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
	const struct snd_kcontrol_new *controls, int num_controls)
{
	struct snd_card *card = codec->card->snd_card;

	return snd_soc_add_controls(card, codec->dev, controls, num_controls,
			codec->name_prefix, codec);
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
	const struct snd_kcontrol_new *controls, int num_controls)
{
	struct snd_card *card = platform->card->snd_card;

	return snd_soc_add_controls(card, platform->dev, controls, num_controls,
			NULL, platform);
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
	struct snd_card *card = dai->card->snd_card;

	return snd_soc_add_controls(card, dai->dev, controls, num_controls,
			NULL, dai);
}
EXPORT_SYMBOL_GPL(snd_soc_add_dai_controls);

/**
 * snd_soc_info_enum_double - enumerated double mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a double enumerated
 * mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_info_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = e->shift_l == e->shift_r ? 1 : 2;
	uinfo->value.enumerated.items = e->max;

	if (uinfo->value.enumerated.item > e->max - 1)
		uinfo->value.enumerated.item = e->max - 1;
	strcpy(uinfo->value.enumerated.name,
		e->texts[uinfo->value.enumerated.item]);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_enum_double);

/**
 * snd_soc_get_enum_double - enumerated double mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a double enumerated mixer.
 *
 * Returns 0 for success.
 */
int snd_soc_get_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val;

	val = snd_soc_read(codec, e->reg);
	ucontrol->value.enumerated.item[0]
		= (val >> e->shift_l) & e->mask;
	if (e->shift_l != e->shift_r)
		ucontrol->value.enumerated.item[1] =
			(val >> e->shift_r) & e->mask;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_enum_double);

/**
 * snd_soc_put_enum_double - enumerated double mixer put callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a double enumerated mixer.
 *
 * Returns 0 for success.
 */
int snd_soc_put_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val;
	unsigned int mask;

	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = e->mask << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->max - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= e->mask << e->shift_r;
	}

	return snd_soc_update_bits_locked(codec, e->reg, mask, val);
}
EXPORT_SYMBOL_GPL(snd_soc_put_enum_double);

/**
 * snd_soc_get_value_enum_double - semi enumerated double mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a double semi enumerated mixer.
 *
 * Semi enumerated mixer: the enumerated items are referred as values. Can be
 * used for handling bitfield coded enumeration for example.
 *
 * Returns 0 for success.
 */
int snd_soc_get_value_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg_val, val, mux;

	reg_val = snd_soc_read(codec, e->reg);
	val = (reg_val >> e->shift_l) & e->mask;
	for (mux = 0; mux < e->max; mux++) {
		if (val == e->values[mux])
			break;
	}
	ucontrol->value.enumerated.item[0] = mux;
	if (e->shift_l != e->shift_r) {
		val = (reg_val >> e->shift_r) & e->mask;
		for (mux = 0; mux < e->max; mux++) {
			if (val == e->values[mux])
				break;
		}
		ucontrol->value.enumerated.item[1] = mux;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_value_enum_double);

/**
 * snd_soc_put_value_enum_double - semi enumerated double mixer put callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a double semi enumerated mixer.
 *
 * Semi enumerated mixer: the enumerated items are referred as values. Can be
 * used for handling bitfield coded enumeration for example.
 *
 * Returns 0 for success.
 */
int snd_soc_put_value_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val;
	unsigned int mask;

	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	val = e->values[ucontrol->value.enumerated.item[0]] << e->shift_l;
	mask = e->mask << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->max - 1)
			return -EINVAL;
		val |= e->values[ucontrol->value.enumerated.item[1]] << e->shift_r;
		mask |= e->mask << e->shift_r;
	}

	return snd_soc_update_bits_locked(codec, e->reg, mask, val);
}
EXPORT_SYMBOL_GPL(snd_soc_put_value_enum_double);

/**
 * snd_soc_info_enum_ext - external enumerated single mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about an external enumerated
 * single mixer.
 *
 * Returns 0 for success.
 */
int snd_soc_info_enum_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = e->max;

	if (uinfo->value.enumerated.item > e->max - 1)
		uinfo->value.enumerated.item = e->max - 1;
	strcpy(uinfo->value.enumerated.name,
		e->texts[uinfo->value.enumerated.item]);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_enum_ext);

/**
 * snd_soc_info_volsw_ext - external single mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a single external mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	int max = kcontrol->private_value;

	if (max == 1 && !strstr(kcontrol->id.name, " Volume"))
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = max;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw_ext);

/**
 * snd_soc_info_volsw - single mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a single mixer control, or a double
 * mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int platform_max;

	if (!mc->platform_max)
		mc->platform_max = mc->max;
	platform_max = mc->platform_max;

	if (platform_max == 1 && !strstr(kcontrol->id.name, " Volume"))
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = snd_soc_volsw_is_stereo(mc) ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = platform_max;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw);

/**
 * snd_soc_get_volsw - single mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a single mixer control, or a double mixer
 * control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;

	ucontrol->value.integer.value[0] =
		(snd_soc_read(codec, reg) >> shift) & mask;
	if (invert)
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];

	if (snd_soc_volsw_is_stereo(mc)) {
		if (reg == reg2)
			ucontrol->value.integer.value[1] =
				(snd_soc_read(codec, reg) >> rshift) & mask;
		else
			ucontrol->value.integer.value[1] =
				(snd_soc_read(codec, reg2) >> shift) & mask;
		if (invert)
			ucontrol->value.integer.value[1] =
				max - ucontrol->value.integer.value[1];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw);

/**
 * snd_soc_put_volsw - single mixer put callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a single mixer control, or a double mixer
 * control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	int err;
	bool type_2r = 0;
	unsigned int val2 = 0;
	unsigned int val, val_mask;

	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = max - val;
	val_mask = mask << shift;
	val = val << shift;
	if (snd_soc_volsw_is_stereo(mc)) {
		val2 = (ucontrol->value.integer.value[1] & mask);
		if (invert)
			val2 = max - val2;
		if (reg == reg2) {
			val_mask |= mask << rshift;
			val |= val2 << rshift;
		} else {
			val2 = val2 << shift;
			type_2r = 1;
		}
	}
	err = snd_soc_update_bits_locked(codec, reg, val_mask, val);
	if (err < 0)
		return err;

	if (type_2r)
		err = snd_soc_update_bits_locked(codec, reg2, val_mask, val2);

	return err;
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw);

/**
 * snd_soc_get_volsw_sx - single mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a single mixer control, or a double mixer
 * control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw_sx(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;

	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	int min = mc->min;
	int mask = (1 << (fls(min + max) - 1)) - 1;

	ucontrol->value.integer.value[0] =
	    ((snd_soc_read(codec, reg) >> shift) - min) & mask;

	if (snd_soc_volsw_is_stereo(mc))
		ucontrol->value.integer.value[1] =
			((snd_soc_read(codec, reg2) >> rshift) - min) & mask;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw_sx);

/**
 * snd_soc_put_volsw_sx - double mixer set callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to set the value of a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw_sx(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;

	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	int min = mc->min;
	int mask = (1 << (fls(min + max) - 1)) - 1;
	int err = 0;
	unsigned short val, val_mask, val2 = 0;

	val_mask = mask << shift;
	val = (ucontrol->value.integer.value[0] + min) & mask;
	val = val << shift;

	err = snd_soc_update_bits_locked(codec, reg, val_mask, val);
	if (err < 0)
		return err;

	if (snd_soc_volsw_is_stereo(mc)) {
		val_mask = mask << rshift;
		val2 = (ucontrol->value.integer.value[1] + min) & mask;
		val2 = val2 << rshift;

		if (snd_soc_update_bits_locked(codec, reg2, val_mask, val2))
			return err;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw_sx);

/**
 * snd_soc_info_volsw_s8 - signed mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a signed mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw_s8(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int platform_max;
	int min = mc->min;

	if (!mc->platform_max)
		mc->platform_max = mc->max;
	platform_max = mc->platform_max;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = platform_max - min;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw_s8);

/**
 * snd_soc_get_volsw_s8 - signed mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a signed mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw_s8(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	int min = mc->min;
	int val = snd_soc_read(codec, reg);

	ucontrol->value.integer.value[0] =
		((signed char)(val & 0xff))-min;
	ucontrol->value.integer.value[1] =
		((signed char)((val >> 8) & 0xff))-min;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw_s8);

/**
 * snd_soc_put_volsw_sgn - signed mixer put callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a signed mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw_s8(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	int min = mc->min;
	unsigned int val;

	val = (ucontrol->value.integer.value[0]+min) & 0xff;
	val |= ((ucontrol->value.integer.value[1]+min) & 0xff) << 8;

	return snd_soc_update_bits_locked(codec, reg, 0xffff, val);
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw_s8);

/**
 * snd_soc_info_volsw_range - single mixer info callback with range.
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information, within a range, about a single
 * mixer control.
 *
 * returns 0 for success.
 */
int snd_soc_info_volsw_range(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int platform_max;
	int min = mc->min;

	if (!mc->platform_max)
		mc->platform_max = mc->max;
	platform_max = mc->platform_max;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = snd_soc_volsw_is_stereo(mc) ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = platform_max - min;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw_range);

/**
 * snd_soc_put_volsw_range - single mixer put value callback with range.
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value, within a range, for a single mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw_range(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int rreg = mc->rreg;
	unsigned int shift = mc->shift;
	int min = mc->min;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val, val_mask;
	int ret;

	val = ((ucontrol->value.integer.value[0] + min) & mask);
	if (invert)
		val = max - val;
	val_mask = mask << shift;
	val = val << shift;

	ret = snd_soc_update_bits_locked(codec, reg, val_mask, val);
	if (ret < 0)
		return ret;

	if (snd_soc_volsw_is_stereo(mc)) {
		val = ((ucontrol->value.integer.value[1] + min) & mask);
		if (invert)
			val = max - val;
		val_mask = mask << shift;
		val = val << shift;

		ret = snd_soc_update_bits_locked(codec, rreg, val_mask, val);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw_range);

/**
 * snd_soc_get_volsw_range - single mixer get callback with range
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value, within a range, of a single mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw_range(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int rreg = mc->rreg;
	unsigned int shift = mc->shift;
	int min = mc->min;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;

	ucontrol->value.integer.value[0] =
		(snd_soc_read(codec, reg) >> shift) & mask;
	if (invert)
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];
	ucontrol->value.integer.value[0] =
		ucontrol->value.integer.value[0] - min;

	if (snd_soc_volsw_is_stereo(mc)) {
		ucontrol->value.integer.value[1] =
			(snd_soc_read(codec, rreg) >> shift) & mask;
		if (invert)
			ucontrol->value.integer.value[1] =
				max - ucontrol->value.integer.value[1];
		ucontrol->value.integer.value[1] =
			ucontrol->value.integer.value[1] - min;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw_range);

/**
 * snd_soc_limit_volume - Set new limit to an existing volume control.
 *
 * @codec: where to look for the control
 * @name: Name of the control
 * @max: new maximum limit
 *
 * Return 0 for success, else error.
 */
int snd_soc_limit_volume(struct snd_soc_codec *codec,
	const char *name, int max)
{
	struct snd_card *card = codec->card->snd_card;
	struct snd_kcontrol *kctl;
	struct soc_mixer_control *mc;
	int found = 0;
	int ret = -EINVAL;

	/* Sanity check for name and max */
	if (unlikely(!name || max <= 0))
		return -EINVAL;

	list_for_each_entry(kctl, &card->controls, list) {
		if (!strncmp(kctl->id.name, name, sizeof(kctl->id.name))) {
			found = 1;
			break;
		}
	}
	if (found) {
		mc = (struct soc_mixer_control *)kctl->private_value;
		if (max <= mc->max) {
			mc->platform_max = max;
			ret = 0;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_limit_volume);

int snd_soc_bytes_info(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_bytes *params = (void *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = params->num_regs * codec->val_bytes;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_bytes_info);

int snd_soc_bytes_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int ret;

	if (codec->using_regmap)
		ret = regmap_raw_read(codec->control_data, params->base,
				      ucontrol->value.bytes.data,
				      params->num_regs * codec->val_bytes);
	else
		ret = -EINVAL;

	/* Hide any masked bytes to ensure consistent data reporting */
	if (ret == 0 && params->mask) {
		switch (codec->val_bytes) {
		case 1:
			ucontrol->value.bytes.data[0] &= ~params->mask;
			break;
		case 2:
			((u16 *)(&ucontrol->value.bytes.data))[0]
				&= ~params->mask;
			break;
		case 4:
			((u32 *)(&ucontrol->value.bytes.data))[0]
				&= ~params->mask;
			break;
		default:
			return -EINVAL;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_bytes_get);

int snd_soc_bytes_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int ret, len;
	unsigned int val;
	void *data;

	if (!codec->using_regmap)
		return -EINVAL;

	len = params->num_regs * codec->val_bytes;

	data = kmemdup(ucontrol->value.bytes.data, len, GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;

	/*
	 * If we've got a mask then we need to preserve the register
	 * bits.  We shouldn't modify the incoming data so take a
	 * copy.
	 */
	if (params->mask) {
		ret = regmap_read(codec->control_data, params->base, &val);
		if (ret != 0)
			goto out;

		val &= params->mask;

		switch (codec->val_bytes) {
		case 1:
			((u8 *)data)[0] &= ~params->mask;
			((u8 *)data)[0] |= val;
			break;
		case 2:
			((u16 *)data)[0] &= cpu_to_be16(~params->mask);
			((u16 *)data)[0] |= cpu_to_be16(val);
			break;
		case 4:
			((u32 *)data)[0] &= cpu_to_be32(~params->mask);
			((u32 *)data)[0] |= cpu_to_be32(val);
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
	}

	ret = regmap_raw_write(codec->control_data, params->base,
			       data, len);

out:
	kfree(data);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_bytes_put);

/**
 * snd_soc_info_xr_sx - signed multi register info callback
 * @kcontrol: mreg control
 * @uinfo: control element information
 *
 * Callback to provide information of a control that can
 * span multiple codec registers which together
 * forms a single signed value in a MSB/LSB manner.
 *
 * Returns 0 for success.
 */
int snd_soc_info_xr_sx(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = mc->min;
	uinfo->value.integer.max = mc->max;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_xr_sx);

/**
 * snd_soc_get_xr_sx - signed multi register get callback
 * @kcontrol: mreg control
 * @ucontrol: control element information
 *
 * Callback to get the value of a control that can span
 * multiple codec registers which together forms a single
 * signed value in a MSB/LSB manner. The control supports
 * specifying total no of bits used to allow for bitfields
 * across the multiple codec registers.
 *
 * Returns 0 for success.
 */
int snd_soc_get_xr_sx(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int regbase = mc->regbase;
	unsigned int regcount = mc->regcount;
	unsigned int regwshift = codec->driver->reg_word_size * BITS_PER_BYTE;
	unsigned int regwmask = (1<<regwshift)-1;
	unsigned int invert = mc->invert;
	unsigned long mask = (1UL<<mc->nbits)-1;
	long min = mc->min;
	long max = mc->max;
	long val = 0;
	unsigned long regval;
	unsigned int i;

	for (i = 0; i < regcount; i++) {
		regval = snd_soc_read(codec, regbase+i) & regwmask;
		val |= regval << (regwshift*(regcount-i-1));
	}
	val &= mask;
	if (min < 0 && val > max)
		val |= ~mask;
	if (invert)
		val = max - val;
	ucontrol->value.integer.value[0] = val;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_xr_sx);

/**
 * snd_soc_put_xr_sx - signed multi register get callback
 * @kcontrol: mreg control
 * @ucontrol: control element information
 *
 * Callback to set the value of a control that can span
 * multiple codec registers which together forms a single
 * signed value in a MSB/LSB manner. The control supports
 * specifying total no of bits used to allow for bitfields
 * across the multiple codec registers.
 *
 * Returns 0 for success.
 */
int snd_soc_put_xr_sx(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int regbase = mc->regbase;
	unsigned int regcount = mc->regcount;
	unsigned int regwshift = codec->driver->reg_word_size * BITS_PER_BYTE;
	unsigned int regwmask = (1<<regwshift)-1;
	unsigned int invert = mc->invert;
	unsigned long mask = (1UL<<mc->nbits)-1;
	long max = mc->max;
	long val = ucontrol->value.integer.value[0];
	unsigned int i, regval, regmask;
	int err;

	if (invert)
		val = max - val;
	val &= mask;
	for (i = 0; i < regcount; i++) {
		regval = (val >> (regwshift*(regcount-i-1))) & regwmask;
		regmask = (mask >> (regwshift*(regcount-i-1))) & regwmask;
		err = snd_soc_update_bits_locked(codec, regbase+i,
				regmask, regval);
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_put_xr_sx);

/**
 * snd_soc_get_strobe - strobe get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback get the value of a strobe mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_get_strobe(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int mask = 1 << shift;
	unsigned int invert = mc->invert != 0;
	unsigned int val = snd_soc_read(codec, reg) & mask;

	if (shift != 0 && val != 0)
		val = val >> shift;
	ucontrol->value.enumerated.item[0] = val ^ invert;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_strobe);

/**
 * snd_soc_put_strobe - strobe put callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback strobe a register bit to high then low (or the inverse)
 * in one pass of a single mixer enum control.
 *
 * Returns 1 for success.
 */
int snd_soc_put_strobe(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int mask = 1 << shift;
	unsigned int invert = mc->invert != 0;
	unsigned int strobe = ucontrol->value.enumerated.item[0] != 0;
	unsigned int val1 = (strobe ^ invert) ? mask : 0;
	unsigned int val2 = (strobe ^ invert) ? 0 : mask;
	int err;

	err = snd_soc_update_bits_locked(codec, reg, mask, val1);
	if (err < 0)
		return err;

	err = snd_soc_update_bits_locked(codec, reg, mask, val2);
	return err;
}
EXPORT_SYMBOL_GPL(snd_soc_put_strobe);

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
		return -EINVAL;
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
		return -EINVAL;
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
 * snd_soc_dai_set_tdm_slot - configure DAI TDM.
 * @dai: DAI
 * @tx_mask: bitmask representing active TX slots.
 * @rx_mask: bitmask representing active RX slots.
 * @slots: Number of slots in use.
 * @slot_width: Width in bits for each slot.
 *
 * Configures a DAI for TDM operation. Both mask and slots are codec and DAI
 * specific.
 */
int snd_soc_dai_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	if (dai->driver && dai->driver->ops->set_tdm_slot)
		return dai->driver->ops->set_tdm_slot(dai, tx_mask, rx_mask,
				slots, slot_width);
	else
		return -EINVAL;
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

	if (!card->name || !card->dev)
		return -EINVAL;

	for (i = 0; i < card->num_links; i++) {
		struct snd_soc_dai_link *link = &card->dai_link[i];

		/*
		 * Codec must be specified by 1 of name or OF node,
		 * not both or neither.
		 */
		if (!!link->codec_name == !!link->codec_of_node) {
			dev_err(card->dev, "ASoC: Neither/both codec"
				" name/of_node are set for %s\n", link->name);
			return -EINVAL;
		}
		/* Codec DAI name must be specified */
		if (!link->codec_dai_name) {
			dev_err(card->dev, "ASoC: codec_dai_name not"
				" set for %s\n", link->name);
			return -EINVAL;
		}

		/*
		 * Platform may be specified by either name or OF node, but
		 * can be left unspecified, and a dummy platform will be used.
		 */
		if (link->platform_name && link->platform_of_node) {
			dev_err(card->dev, "ASoC: Both platform name/of_node"
				" are set for %s\n", link->name);
			return -EINVAL;
		}

		/*
		 * CPU device may be specified by either name or OF node, but
		 * can be left unspecified, and will be matched based on DAI
		 * name alone..
		 */
		if (link->cpu_name && link->cpu_of_node) {
			dev_err(card->dev, "ASoC: Neither/both "
				"cpu name/of_node are set for %s\n",link->name);
			return -EINVAL;
		}
		/*
		 * At least one of CPU DAI name or CPU device name/node must be
		 * specified
		 */
		if (!link->cpu_dai_name &&
		    !(link->cpu_name || link->cpu_of_node)) {
			dev_err(card->dev, "ASoC: Neither cpu_dai_name nor "
				"cpu_name/of_node are set for %s\n", link->name);
			return -EINVAL;
		}
	}

	dev_set_drvdata(card->dev, card);

	snd_soc_initialize_card_lists(card);

	soc_init_card_debugfs(card);

	card->rtd = devm_kzalloc(card->dev,
				 sizeof(struct snd_soc_pcm_runtime) *
				 (card->num_links + card->num_aux_devs),
				 GFP_KERNEL);
	if (card->rtd == NULL)
		return -ENOMEM;
	card->num_rtd = 0;
	card->rtd_aux = &card->rtd[card->num_links];

	for (i = 0; i < card->num_links; i++)
		card->rtd[i].dai_link = &card->dai_link[i];

	INIT_LIST_HEAD(&card->list);
	INIT_LIST_HEAD(&card->dapm_dirty);
	card->instantiated = 0;
	mutex_init(&card->mutex);
	mutex_init(&card->dapm_mutex);

	ret = snd_soc_instantiate_card(card);
	if (ret != 0)
		soc_cleanup_card_debugfs(card);

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
	if (card->instantiated)
		soc_cleanup_card_resources(card);
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
		dev_err(dev, "ASoC: error - multiple DAI %s registered with"
				" no name\n", dev_name(dev));
		return NULL;
	}

	return kstrdup(dai_drv->name, GFP_KERNEL);
}

/**
 * snd_soc_register_dai - Register a DAI with the ASoC core
 *
 * @dai: DAI to register
 */
static int snd_soc_register_dai(struct device *dev,
		struct snd_soc_dai_driver *dai_drv)
{
	struct snd_soc_codec *codec;
	struct snd_soc_dai *dai;

	dev_dbg(dev, "ASoC: dai register %s\n", dev_name(dev));

	dai = kzalloc(sizeof(struct snd_soc_dai), GFP_KERNEL);
	if (dai == NULL)
		return -ENOMEM;

	/* create DAI component name */
	dai->name = fmt_single_name(dev, &dai->id);
	if (dai->name == NULL) {
		kfree(dai);
		return -ENOMEM;
	}

	dai->dev = dev;
	dai->driver = dai_drv;
	dai->dapm.dev = dev;
	if (!dai->driver->ops)
		dai->driver->ops = &null_dai_ops;

	mutex_lock(&client_mutex);

	list_for_each_entry(codec, &codec_list, list) {
		if (codec->dev == dev) {
			dev_dbg(dev, "ASoC: Mapped DAI %s to CODEC %s\n",
				dai->name, codec->name);
			dai->codec = codec;
			break;
		}
	}

	if (!dai->codec)
		dai->dapm.idle_bias_off = 1;

	list_add(&dai->list, &dai_list);

	mutex_unlock(&client_mutex);

	dev_dbg(dev, "ASoC: Registered DAI '%s'\n", dai->name);

	return 0;
}

/**
 * snd_soc_unregister_dai - Unregister a DAI from the ASoC core
 *
 * @dai: DAI to unregister
 */
static void snd_soc_unregister_dai(struct device *dev)
{
	struct snd_soc_dai *dai;

	list_for_each_entry(dai, &dai_list, list) {
		if (dev == dai->dev)
			goto found;
	}
	return;

found:
	mutex_lock(&client_mutex);
	list_del(&dai->list);
	mutex_unlock(&client_mutex);

	dev_dbg(dev, "ASoC: Unregistered DAI '%s'\n", dai->name);
	kfree(dai->name);
	kfree(dai);
}

/**
 * snd_soc_register_dais - Register multiple DAIs with the ASoC core
 *
 * @dai: Array of DAIs to register
 * @count: Number of DAIs
 */
static int snd_soc_register_dais(struct device *dev,
		struct snd_soc_dai_driver *dai_drv, size_t count)
{
	struct snd_soc_codec *codec;
	struct snd_soc_dai *dai;
	int i, ret = 0;

	dev_dbg(dev, "ASoC: dai register %s #%Zu\n", dev_name(dev), count);

	for (i = 0; i < count; i++) {

		dai = kzalloc(sizeof(struct snd_soc_dai), GFP_KERNEL);
		if (dai == NULL) {
			ret = -ENOMEM;
			goto err;
		}

		/* create DAI component name */
		dai->name = fmt_multiple_name(dev, &dai_drv[i]);
		if (dai->name == NULL) {
			kfree(dai);
			ret = -EINVAL;
			goto err;
		}

		dai->dev = dev;
		dai->driver = &dai_drv[i];
		if (dai->driver->id)
			dai->id = dai->driver->id;
		else
			dai->id = i;
		dai->dapm.dev = dev;
		if (!dai->driver->ops)
			dai->driver->ops = &null_dai_ops;

		mutex_lock(&client_mutex);

		list_for_each_entry(codec, &codec_list, list) {
			if (codec->dev == dev) {
				dev_dbg(dev, "ASoC: Mapped DAI %s to "
					"CODEC %s\n", dai->name, codec->name);
				dai->codec = codec;
				break;
			}
		}

		if (!dai->codec)
			dai->dapm.idle_bias_off = 1;

		list_add(&dai->list, &dai_list);

		mutex_unlock(&client_mutex);

		dev_dbg(dai->dev, "ASoC: Registered DAI '%s'\n", dai->name);
	}

	return 0;

err:
	for (i--; i >= 0; i--)
		snd_soc_unregister_dai(dev);

	return ret;
}

/**
 * snd_soc_unregister_dais - Unregister multiple DAIs from the ASoC core
 *
 * @dai: Array of DAIs to unregister
 * @count: Number of DAIs
 */
static void snd_soc_unregister_dais(struct device *dev, size_t count)
{
	int i;

	for (i = 0; i < count; i++)
		snd_soc_unregister_dai(dev);
}

/**
 * snd_soc_add_platform - Add a platform to the ASoC core
 * @dev: The parent device for the platform
 * @platform: The platform to add
 * @platform_driver: The driver for the platform
 */
int snd_soc_add_platform(struct device *dev, struct snd_soc_platform *platform,
		const struct snd_soc_platform_driver *platform_drv)
{
	/* create platform component name */
	platform->name = fmt_single_name(dev, &platform->id);
	if (platform->name == NULL) {
		kfree(platform);
		return -ENOMEM;
	}

	platform->dev = dev;
	platform->driver = platform_drv;
	platform->dapm.dev = dev;
	platform->dapm.platform = platform;
	platform->dapm.stream_event = platform_drv->stream_event;
	mutex_init(&platform->mutex);

	mutex_lock(&client_mutex);
	list_add(&platform->list, &platform_list);
	mutex_unlock(&client_mutex);

	dev_dbg(dev, "ASoC: Registered platform '%s'\n", platform->name);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_add_platform);

/**
 * snd_soc_register_platform - Register a platform with the ASoC core
 *
 * @platform: platform to register
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
	mutex_unlock(&client_mutex);

	dev_dbg(platform->dev, "ASoC: Unregistered platform '%s'\n",
		platform->name);
	kfree(platform->name);
}
EXPORT_SYMBOL_GPL(snd_soc_remove_platform);

struct snd_soc_platform *snd_soc_lookup_platform(struct device *dev)
{
	struct snd_soc_platform *platform;

	list_for_each_entry(platform, &platform_list, list) {
		if (dev == platform->dev)
			return platform;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_lookup_platform);

/**
 * snd_soc_unregister_platform - Unregister a platform from the ASoC core
 *
 * @platform: platform to unregister
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

/**
 * snd_soc_register_codec - Register a codec with the ASoC core
 *
 * @codec: codec to register
 */
int snd_soc_register_codec(struct device *dev,
			   const struct snd_soc_codec_driver *codec_drv,
			   struct snd_soc_dai_driver *dai_drv,
			   int num_dai)
{
	size_t reg_size;
	struct snd_soc_codec *codec;
	int ret, i;

	dev_dbg(dev, "codec register %s\n", dev_name(dev));

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	/* create CODEC component name */
	codec->name = fmt_single_name(dev, &codec->id);
	if (codec->name == NULL) {
		ret = -ENOMEM;
		goto fail_codec;
	}

	if (codec_drv->compress_type)
		codec->compress_type = codec_drv->compress_type;
	else
		codec->compress_type = SND_SOC_FLAT_COMPRESSION;

	codec->write = codec_drv->write;
	codec->read = codec_drv->read;
	codec->volatile_register = codec_drv->volatile_register;
	codec->readable_register = codec_drv->readable_register;
	codec->writable_register = codec_drv->writable_register;
	codec->ignore_pmdown_time = codec_drv->ignore_pmdown_time;
	codec->dapm.bias_level = SND_SOC_BIAS_OFF;
	codec->dapm.dev = dev;
	codec->dapm.codec = codec;
	codec->dapm.seq_notifier = codec_drv->seq_notifier;
	codec->dapm.stream_event = codec_drv->stream_event;
	codec->dev = dev;
	codec->driver = codec_drv;
	codec->num_dai = num_dai;
	mutex_init(&codec->mutex);

	/* allocate CODEC register cache */
	if (codec_drv->reg_cache_size && codec_drv->reg_word_size) {
		reg_size = codec_drv->reg_cache_size * codec_drv->reg_word_size;
		codec->reg_size = reg_size;
		/* it is necessary to make a copy of the default register cache
		 * because in the case of using a compression type that requires
		 * the default register cache to be marked as the
		 * kernel might have freed the array by the time we initialize
		 * the cache.
		 */
		if (codec_drv->reg_cache_default) {
			codec->reg_def_copy = kmemdup(codec_drv->reg_cache_default,
						      reg_size, GFP_KERNEL);
			if (!codec->reg_def_copy) {
				ret = -ENOMEM;
				goto fail_codec_name;
			}
		}
	}

	if (codec_drv->reg_access_size && codec_drv->reg_access_default) {
		if (!codec->volatile_register)
			codec->volatile_register = snd_soc_default_volatile_register;
		if (!codec->readable_register)
			codec->readable_register = snd_soc_default_readable_register;
		if (!codec->writable_register)
			codec->writable_register = snd_soc_default_writable_register;
	}

	for (i = 0; i < num_dai; i++) {
		fixup_codec_formats(&dai_drv[i].playback);
		fixup_codec_formats(&dai_drv[i].capture);
	}

	mutex_lock(&client_mutex);
	list_add(&codec->list, &codec_list);
	mutex_unlock(&client_mutex);

	/* register any DAIs */
	ret = snd_soc_register_dais(dev, dai_drv, num_dai);
	if (ret < 0) {
		dev_err(codec->dev, "ASoC: Failed to regster DAIs: %d\n", ret);
		goto fail_codec_name;
	}

	dev_dbg(codec->dev, "ASoC: Registered codec '%s'\n", codec->name);
	return 0;

fail_codec_name:
	mutex_lock(&client_mutex);
	list_del(&codec->list);
	mutex_unlock(&client_mutex);

	kfree(codec->name);
fail_codec:
	kfree(codec);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_register_codec);

/**
 * snd_soc_unregister_codec - Unregister a codec from the ASoC core
 *
 * @codec: codec to unregister
 */
void snd_soc_unregister_codec(struct device *dev)
{
	struct snd_soc_codec *codec;

	list_for_each_entry(codec, &codec_list, list) {
		if (dev == codec->dev)
			goto found;
	}
	return;

found:
	snd_soc_unregister_dais(dev, codec->num_dai);

	mutex_lock(&client_mutex);
	list_del(&codec->list);
	mutex_unlock(&client_mutex);

	dev_dbg(codec->dev, "ASoC: Unregistered codec '%s'\n", codec->name);

	snd_soc_cache_exit(codec);
	kfree(codec->reg_def_copy);
	kfree(codec->name);
	kfree(codec);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_codec);


/**
 * snd_soc_register_component - Register a component with the ASoC core
 *
 */
int snd_soc_register_component(struct device *dev,
			 const struct snd_soc_component_driver *cmpnt_drv,
			 struct snd_soc_dai_driver *dai_drv,
			 int num_dai)
{
	struct snd_soc_component *cmpnt;
	int ret;

	dev_dbg(dev, "component register %s\n", dev_name(dev));

	cmpnt = devm_kzalloc(dev, sizeof(*cmpnt), GFP_KERNEL);
	if (!cmpnt) {
		dev_err(dev, "ASoC: Failed to allocate memory\n");
		return -ENOMEM;
	}

	cmpnt->name = fmt_single_name(dev, &cmpnt->id);
	if (!cmpnt->name) {
		dev_err(dev, "ASoC: Failed to simplifying name\n");
		return -ENOMEM;
	}

	cmpnt->dev	= dev;
	cmpnt->driver	= cmpnt_drv;
	cmpnt->num_dai	= num_dai;

	/*
	 * snd_soc_register_dai()  uses fmt_single_name(), and
	 * snd_soc_register_dais() uses fmt_multiple_name()
	 * for dai->name which is used for name based matching
	 */
	if (1 == num_dai)
		ret = snd_soc_register_dai(dev, dai_drv);
	else
		ret = snd_soc_register_dais(dev, dai_drv, num_dai);
	if (ret < 0) {
		dev_err(dev, "ASoC: Failed to regster DAIs: %d\n", ret);
		goto error_component_name;
	}

	mutex_lock(&client_mutex);
	list_add(&cmpnt->list, &component_list);
	mutex_unlock(&client_mutex);

	dev_dbg(cmpnt->dev, "ASoC: Registered component '%s'\n", cmpnt->name);

	return ret;

error_component_name:
	kfree(cmpnt->name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_register_component);

/**
 * snd_soc_unregister_component - Unregister a component from the ASoC core
 *
 */
void snd_soc_unregister_component(struct device *dev)
{
	struct snd_soc_component *cmpnt;

	list_for_each_entry(cmpnt, &component_list, list) {
		if (dev == cmpnt->dev)
			goto found;
	}
	return;

found:
	snd_soc_unregister_dais(dev, cmpnt->num_dai);

	mutex_lock(&client_mutex);
	list_del(&cmpnt->list);
	mutex_unlock(&client_mutex);

	dev_dbg(dev, "ASoC: Unregistered component '%s'\n", cmpnt->name);
	kfree(cmpnt->name);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_component);

/* Retrieve a card's name from device tree */
int snd_soc_of_parse_card_name(struct snd_soc_card *card,
			       const char *propname)
{
	struct device_node *np = card->dev->of_node;
	int ret;

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

int snd_soc_of_parse_audio_routing(struct snd_soc_card *card,
				   const char *propname)
{
	struct device_node *np = card->dev->of_node;
	int num_routes;
	struct snd_soc_dapm_route *routes;
	int i, ret;

	num_routes = of_property_count_strings(np, propname);
	if (num_routes < 0 || num_routes & 1) {
		dev_err(card->dev, "ASoC: Property '%s' does not exist or its"
			" length is not even\n", propname);
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

	card->num_dapm_routes = num_routes;
	card->dapm_routes = routes;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_of_parse_audio_routing);

unsigned int snd_soc_of_parse_daifmt(struct device_node *np,
				     const char *prefix)
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
	if (of_get_property(np, prop, NULL))
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

	snprintf(prop, sizeof(prop), "%sframe-master", prefix);
	frame = !!of_get_property(np, prop, NULL);

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

static int __init snd_soc_init(void)
{
#ifdef CONFIG_DEBUG_FS
	snd_soc_debugfs_root = debugfs_create_dir("asoc", NULL);
	if (IS_ERR(snd_soc_debugfs_root) || !snd_soc_debugfs_root) {
		pr_warn("ASoC: Failed to create debugfs directory\n");
		snd_soc_debugfs_root = NULL;
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
#endif

	snd_soc_util_init();

	return platform_driver_register(&soc_driver);
}
module_init(snd_soc_init);

static void __exit snd_soc_exit(void)
{
	snd_soc_util_exit();

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(snd_soc_debugfs_root);
#endif
	platform_driver_unregister(&soc_driver);
}
module_exit(snd_soc_exit);

/* Module information */
MODULE_AUTHOR("Liam Girdwood, lrg@slimlogic.co.uk");
MODULE_DESCRIPTION("ALSA SoC Core");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:soc-audio");

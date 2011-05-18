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
#include <linux/slab.h>
#include <sound/ac97_codec.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#define CREATE_TRACE_POINTS
#include <trace/events/asoc.h>

#define NAME_SIZE	32

static DEFINE_MUTEX(pcm_mutex);
static DECLARE_WAIT_QUEUE_HEAD(soc_pm_waitq);

#ifdef CONFIG_DEBUG_FS
struct dentry *snd_soc_debugfs_root;
EXPORT_SYMBOL_GPL(snd_soc_debugfs_root);
#endif

static DEFINE_MUTEX(client_mutex);
static LIST_HEAD(card_list);
static LIST_HEAD(dai_list);
static LIST_HEAD(platform_list);
static LIST_HEAD(codec_list);

static int soc_new_pcm(struct snd_soc_pcm_runtime *rtd, int num);

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

	ret = snd_soc_read(codec , reg);
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
		if (codec->readable_register && !codec->readable_register(codec, i))
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
	struct snd_soc_pcm_runtime *rtd =
			container_of(dev, struct snd_soc_pcm_runtime, dev);

	return soc_codec_reg_show(rtd->codec, buf, PAGE_SIZE, 0);
}

static DEVICE_ATTR(codec_reg, 0444, codec_reg_show, NULL);

static ssize_t pmdown_time_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct snd_soc_pcm_runtime *rtd =
			container_of(dev, struct snd_soc_pcm_runtime, dev);

	return sprintf(buf, "%ld\n", rtd->pmdown_time);
}

static ssize_t pmdown_time_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct snd_soc_pcm_runtime *rtd =
			container_of(dev, struct snd_soc_pcm_runtime, dev);
	int ret;

	ret = strict_strtol(buf, 10, &rtd->pmdown_time);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR(pmdown_time, 0644, pmdown_time_show, pmdown_time_set);

#ifdef CONFIG_DEBUG_FS
static int codec_reg_open_file(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

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
	int buf_size;
	char *start = buf;
	unsigned long reg, value;
	int step = 1;
	struct snd_soc_codec *codec = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	if (codec->driver->reg_cache_step)
		step = codec->driver->reg_cache_step;

	while (*start == ' ')
		start++;
	reg = simple_strtoul(start, &start, 16);
	while (*start == ' ')
		start++;
	if (strict_strtoul(start, 16, &value))
		return -EINVAL;

	/* Userspace has been fiddling around behind the kernel's back */
	add_taint(TAINT_USER);

	snd_soc_write(codec, reg, value);
	return buf_size;
}

static const struct file_operations codec_reg_fops = {
	.open = codec_reg_open_file,
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
		printk(KERN_WARNING
		       "ASoC: Failed to create codec debugfs directory\n");
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
		printk(KERN_WARNING
		       "ASoC: Failed to create codec register debugfs file\n");

	codec->dapm.debugfs_dapm = debugfs_create_dir("dapm",
						 codec->debugfs_codec_root);
	if (!codec->dapm.debugfs_dapm)
		printk(KERN_WARNING
		       "Failed to create DAPM debugfs directory\n");

	snd_soc_dapm_debugfs_init(&codec->dapm);
}

static void soc_cleanup_codec_debugfs(struct snd_soc_codec *codec)
{
	debugfs_remove_recursive(codec->debugfs_codec_root);
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
			 "ASoC: Failed to create codec debugfs directory\n");
		return;
	}

	card->debugfs_pop_time = debugfs_create_u32("dapm_pop_time", 0644,
						    card->debugfs_card_root,
						    &card->pop_time);
	if (!card->debugfs_pop_time)
		dev_warn(card->dev,
		       "Failed to create pop time debugfs file\n");
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

static inline void soc_init_card_debugfs(struct snd_soc_card *card)
{
}

static inline void soc_cleanup_card_debugfs(struct snd_soc_card *card)
{
}
#endif

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
		snd_printk(KERN_ERR "Can't register ac97 bus\n");
		codec->ac97->dev.bus = NULL;
		return err;
	}
	return 0;
}
#endif

static int soc_pcm_apply_symmetry(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	if (!codec_dai->driver->symmetric_rates &&
	    !cpu_dai->driver->symmetric_rates &&
	    !rtd->dai_link->symmetric_rates)
		return 0;

	/* This can happen if multiple streams are starting simultaneously -
	 * the second can need to get its constraints before the first has
	 * picked a rate.  Complain and allow the application to carry on.
	 */
	if (!rtd->rate) {
		dev_warn(&rtd->dev,
			 "Not enforcing symmetric_rates due to race\n");
		return 0;
	}

	dev_dbg(&rtd->dev, "Symmetry forces %dHz rate\n", rtd->rate);

	ret = snd_pcm_hw_constraint_minmax(substream->runtime,
					   SNDRV_PCM_HW_PARAM_RATE,
					   rtd->rate, rtd->rate);
	if (ret < 0) {
		dev_err(&rtd->dev,
			"Unable to apply rate symmetry constraint: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * Called by ALSA when a PCM substream is opened, the runtime->hw record is
 * then initialized and any private data can be allocated. This also calls
 * startup for the cpu DAI, platform, machine and codec DAI.
 */
static int soc_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai_driver *cpu_dai_drv = cpu_dai->driver;
	struct snd_soc_dai_driver *codec_dai_drv = codec_dai->driver;
	int ret = 0;

	mutex_lock(&pcm_mutex);

	/* startup the audio subsystem */
	if (cpu_dai->driver->ops->startup) {
		ret = cpu_dai->driver->ops->startup(substream, cpu_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't open interface %s\n",
				cpu_dai->name);
			goto out;
		}
	}

	if (platform->driver->ops->open) {
		ret = platform->driver->ops->open(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't open platform %s\n", platform->name);
			goto platform_err;
		}
	}

	if (codec_dai->driver->ops->startup) {
		ret = codec_dai->driver->ops->startup(substream, codec_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't open codec %s\n",
				codec_dai->name);
			goto codec_dai_err;
		}
	}

	if (rtd->dai_link->ops && rtd->dai_link->ops->startup) {
		ret = rtd->dai_link->ops->startup(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: %s startup failed\n", rtd->dai_link->name);
			goto machine_err;
		}
	}

	/* Check that the codec and cpu DAIs are compatible */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		runtime->hw.rate_min =
			max(codec_dai_drv->playback.rate_min,
			    cpu_dai_drv->playback.rate_min);
		runtime->hw.rate_max =
			min(codec_dai_drv->playback.rate_max,
			    cpu_dai_drv->playback.rate_max);
		runtime->hw.channels_min =
			max(codec_dai_drv->playback.channels_min,
				cpu_dai_drv->playback.channels_min);
		runtime->hw.channels_max =
			min(codec_dai_drv->playback.channels_max,
				cpu_dai_drv->playback.channels_max);
		runtime->hw.formats =
			codec_dai_drv->playback.formats & cpu_dai_drv->playback.formats;
		runtime->hw.rates =
			codec_dai_drv->playback.rates & cpu_dai_drv->playback.rates;
		if (codec_dai_drv->playback.rates
			   & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))
			runtime->hw.rates |= cpu_dai_drv->playback.rates;
		if (cpu_dai_drv->playback.rates
			   & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))
			runtime->hw.rates |= codec_dai_drv->playback.rates;
	} else {
		runtime->hw.rate_min =
			max(codec_dai_drv->capture.rate_min,
			    cpu_dai_drv->capture.rate_min);
		runtime->hw.rate_max =
			min(codec_dai_drv->capture.rate_max,
			    cpu_dai_drv->capture.rate_max);
		runtime->hw.channels_min =
			max(codec_dai_drv->capture.channels_min,
				cpu_dai_drv->capture.channels_min);
		runtime->hw.channels_max =
			min(codec_dai_drv->capture.channels_max,
				cpu_dai_drv->capture.channels_max);
		runtime->hw.formats =
			codec_dai_drv->capture.formats & cpu_dai_drv->capture.formats;
		runtime->hw.rates =
			codec_dai_drv->capture.rates & cpu_dai_drv->capture.rates;
		if (codec_dai_drv->capture.rates
			   & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))
			runtime->hw.rates |= cpu_dai_drv->capture.rates;
		if (cpu_dai_drv->capture.rates
			   & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))
			runtime->hw.rates |= codec_dai_drv->capture.rates;
	}

	ret = -EINVAL;
	snd_pcm_limit_hw_rates(runtime);
	if (!runtime->hw.rates) {
		printk(KERN_ERR "asoc: %s <-> %s No matching rates\n",
			codec_dai->name, cpu_dai->name);
		goto config_err;
	}
	if (!runtime->hw.formats) {
		printk(KERN_ERR "asoc: %s <-> %s No matching formats\n",
			codec_dai->name, cpu_dai->name);
		goto config_err;
	}
	if (!runtime->hw.channels_min || !runtime->hw.channels_max ||
	    runtime->hw.channels_min > runtime->hw.channels_max) {
		printk(KERN_ERR "asoc: %s <-> %s No matching channels\n",
				codec_dai->name, cpu_dai->name);
		goto config_err;
	}

	/* Symmetry only applies if we've already got an active stream. */
	if (cpu_dai->active || codec_dai->active) {
		ret = soc_pcm_apply_symmetry(substream);
		if (ret != 0)
			goto config_err;
	}

	pr_debug("asoc: %s <-> %s info:\n",
			codec_dai->name, cpu_dai->name);
	pr_debug("asoc: rate mask 0x%x\n", runtime->hw.rates);
	pr_debug("asoc: min ch %d max ch %d\n", runtime->hw.channels_min,
		 runtime->hw.channels_max);
	pr_debug("asoc: min rate %d max rate %d\n", runtime->hw.rate_min,
		 runtime->hw.rate_max);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		cpu_dai->playback_active++;
		codec_dai->playback_active++;
	} else {
		cpu_dai->capture_active++;
		codec_dai->capture_active++;
	}
	cpu_dai->active++;
	codec_dai->active++;
	rtd->codec->active++;
	mutex_unlock(&pcm_mutex);
	return 0;

config_err:
	if (rtd->dai_link->ops && rtd->dai_link->ops->shutdown)
		rtd->dai_link->ops->shutdown(substream);

machine_err:
	if (codec_dai->driver->ops->shutdown)
		codec_dai->driver->ops->shutdown(substream, codec_dai);

codec_dai_err:
	if (platform->driver->ops->close)
		platform->driver->ops->close(substream);

platform_err:
	if (cpu_dai->driver->ops->shutdown)
		cpu_dai->driver->ops->shutdown(substream, cpu_dai);
out:
	mutex_unlock(&pcm_mutex);
	return ret;
}

/*
 * Power down the audio subsystem pmdown_time msecs after close is called.
 * This is to ensure there are no pops or clicks in between any music tracks
 * due to DAPM power cycling.
 */
static void close_delayed_work(struct work_struct *work)
{
	struct snd_soc_pcm_runtime *rtd =
			container_of(work, struct snd_soc_pcm_runtime, delayed_work.work);
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	mutex_lock(&pcm_mutex);

	pr_debug("pop wq checking: %s status: %s waiting: %s\n",
		 codec_dai->driver->playback.stream_name,
		 codec_dai->playback_active ? "active" : "inactive",
		 codec_dai->pop_wait ? "yes" : "no");

	/* are we waiting on this codec DAI stream */
	if (codec_dai->pop_wait == 1) {
		codec_dai->pop_wait = 0;
		snd_soc_dapm_stream_event(rtd,
			codec_dai->driver->playback.stream_name,
			SND_SOC_DAPM_STREAM_STOP);
	}

	mutex_unlock(&pcm_mutex);
}

/*
 * Called by ALSA when a PCM substream is closed. Private data can be
 * freed here. The cpu DAI, codec DAI, machine and platform are also
 * shutdown.
 */
static int soc_codec_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;

	mutex_lock(&pcm_mutex);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		cpu_dai->playback_active--;
		codec_dai->playback_active--;
	} else {
		cpu_dai->capture_active--;
		codec_dai->capture_active--;
	}

	cpu_dai->active--;
	codec_dai->active--;
	codec->active--;

	/* Muting the DAC suppresses artifacts caused during digital
	 * shutdown, for example from stopping clocks.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_dai_digital_mute(codec_dai, 1);

	if (cpu_dai->driver->ops->shutdown)
		cpu_dai->driver->ops->shutdown(substream, cpu_dai);

	if (codec_dai->driver->ops->shutdown)
		codec_dai->driver->ops->shutdown(substream, codec_dai);

	if (rtd->dai_link->ops && rtd->dai_link->ops->shutdown)
		rtd->dai_link->ops->shutdown(substream);

	if (platform->driver->ops->close)
		platform->driver->ops->close(substream);
	cpu_dai->runtime = NULL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* start delayed pop wq here for playback streams */
		codec_dai->pop_wait = 1;
		schedule_delayed_work(&rtd->delayed_work,
			msecs_to_jiffies(rtd->pmdown_time));
	} else {
		/* capture streams can be powered down now */
		snd_soc_dapm_stream_event(rtd,
			codec_dai->driver->capture.stream_name,
			SND_SOC_DAPM_STREAM_STOP);
	}

	mutex_unlock(&pcm_mutex);
	return 0;
}

/*
 * Called by ALSA when the PCM substream is prepared, can set format, sample
 * rate, etc.  This function is non atomic and can be called multiple times,
 * it can refer to the runtime info.
 */
static int soc_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret = 0;

	mutex_lock(&pcm_mutex);

	if (rtd->dai_link->ops && rtd->dai_link->ops->prepare) {
		ret = rtd->dai_link->ops->prepare(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: machine prepare error\n");
			goto out;
		}
	}

	if (platform->driver->ops->prepare) {
		ret = platform->driver->ops->prepare(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: platform prepare error\n");
			goto out;
		}
	}

	if (codec_dai->driver->ops->prepare) {
		ret = codec_dai->driver->ops->prepare(substream, codec_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: codec DAI prepare error\n");
			goto out;
		}
	}

	if (cpu_dai->driver->ops->prepare) {
		ret = cpu_dai->driver->ops->prepare(substream, cpu_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: cpu DAI prepare error\n");
			goto out;
		}
	}

	/* cancel any delayed stream shutdown that is pending */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    codec_dai->pop_wait) {
		codec_dai->pop_wait = 0;
		cancel_delayed_work(&rtd->delayed_work);
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_dapm_stream_event(rtd,
					  codec_dai->driver->playback.stream_name,
					  SND_SOC_DAPM_STREAM_START);
	else
		snd_soc_dapm_stream_event(rtd,
					  codec_dai->driver->capture.stream_name,
					  SND_SOC_DAPM_STREAM_START);

	snd_soc_dai_digital_mute(codec_dai, 0);

out:
	mutex_unlock(&pcm_mutex);
	return ret;
}

/*
 * Called by ALSA when the hardware params are set by application. This
 * function can also be called multiple times and can allocate buffers
 * (using snd_pcm_lib_* ). It's non-atomic.
 */
static int soc_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret = 0;

	mutex_lock(&pcm_mutex);

	if (rtd->dai_link->ops && rtd->dai_link->ops->hw_params) {
		ret = rtd->dai_link->ops->hw_params(substream, params);
		if (ret < 0) {
			printk(KERN_ERR "asoc: machine hw_params failed\n");
			goto out;
		}
	}

	if (codec_dai->driver->ops->hw_params) {
		ret = codec_dai->driver->ops->hw_params(substream, params, codec_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't set codec %s hw params\n",
				codec_dai->name);
			goto codec_err;
		}
	}

	if (cpu_dai->driver->ops->hw_params) {
		ret = cpu_dai->driver->ops->hw_params(substream, params, cpu_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: interface %s hw params failed\n",
				cpu_dai->name);
			goto interface_err;
		}
	}

	if (platform->driver->ops->hw_params) {
		ret = platform->driver->ops->hw_params(substream, params);
		if (ret < 0) {
			printk(KERN_ERR "asoc: platform %s hw params failed\n",
				platform->name);
			goto platform_err;
		}
	}

	rtd->rate = params_rate(params);

out:
	mutex_unlock(&pcm_mutex);
	return ret;

platform_err:
	if (cpu_dai->driver->ops->hw_free)
		cpu_dai->driver->ops->hw_free(substream, cpu_dai);

interface_err:
	if (codec_dai->driver->ops->hw_free)
		codec_dai->driver->ops->hw_free(substream, codec_dai);

codec_err:
	if (rtd->dai_link->ops && rtd->dai_link->ops->hw_free)
		rtd->dai_link->ops->hw_free(substream);

	mutex_unlock(&pcm_mutex);
	return ret;
}

/*
 * Frees resources allocated by hw_params, can be called multiple times
 */
static int soc_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;

	mutex_lock(&pcm_mutex);

	/* apply codec digital mute */
	if (!codec->active)
		snd_soc_dai_digital_mute(codec_dai, 1);

	/* free any machine hw params */
	if (rtd->dai_link->ops && rtd->dai_link->ops->hw_free)
		rtd->dai_link->ops->hw_free(substream);

	/* free any DMA resources */
	if (platform->driver->ops->hw_free)
		platform->driver->ops->hw_free(substream);

	/* now free hw params for the DAIs  */
	if (codec_dai->driver->ops->hw_free)
		codec_dai->driver->ops->hw_free(substream, codec_dai);

	if (cpu_dai->driver->ops->hw_free)
		cpu_dai->driver->ops->hw_free(substream, cpu_dai);

	mutex_unlock(&pcm_mutex);
	return 0;
}

static int soc_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	if (codec_dai->driver->ops->trigger) {
		ret = codec_dai->driver->ops->trigger(substream, cmd, codec_dai);
		if (ret < 0)
			return ret;
	}

	if (platform->driver->ops->trigger) {
		ret = platform->driver->ops->trigger(substream, cmd);
		if (ret < 0)
			return ret;
	}

	if (cpu_dai->driver->ops->trigger) {
		ret = cpu_dai->driver->ops->trigger(substream, cmd, cpu_dai);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/*
 * soc level wrapper for pointer callback
 * If cpu_dai, codec_dai, platform driver has the delay callback, than
 * the runtime->delay will be updated accordingly.
 */
static snd_pcm_uframes_t soc_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t offset = 0;
	snd_pcm_sframes_t delay = 0;

	if (platform->driver->ops->pointer)
		offset = platform->driver->ops->pointer(substream);

	if (cpu_dai->driver->ops->delay)
		delay += cpu_dai->driver->ops->delay(substream, cpu_dai);

	if (codec_dai->driver->ops->delay)
		delay += codec_dai->driver->ops->delay(substream, codec_dai);

	if (platform->driver->delay)
		delay += platform->driver->delay(substream, codec_dai);

	runtime->delay = delay;

	return offset;
}

/* ASoC PCM operations */
static struct snd_pcm_ops soc_pcm_ops = {
	.open		= soc_pcm_open,
	.close		= soc_codec_close,
	.hw_params	= soc_pcm_hw_params,
	.hw_free	= soc_pcm_hw_free,
	.prepare	= soc_pcm_prepare,
	.trigger	= soc_pcm_trigger,
	.pointer	= soc_pcm_pointer,
};

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
		flush_delayed_work_sync(&card->rtd[i].delayed_work);
		card->rtd[i].codec->dapm.suspend_bias_level = card->rtd[i].codec->dapm.bias_level;
	}

	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai_driver *driver = card->rtd[i].codec_dai->driver;

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		if (driver->playback.stream_name != NULL)
			snd_soc_dapm_stream_event(&card->rtd[i], driver->playback.stream_name,
				SND_SOC_DAPM_STREAM_SUSPEND);

		if (driver->capture.stream_name != NULL)
			snd_soc_dapm_stream_event(&card->rtd[i], driver->capture.stream_name,
				SND_SOC_DAPM_STREAM_SUSPEND);
	}

	/* suspend all CODECs */
	list_for_each_entry(codec, &card->codec_dev_list, card_list) {
		/* If there are paths active then the CODEC will be held with
		 * bias _ON and should not be suspended. */
		if (!codec->suspended && codec->driver->suspend) {
			switch (codec->dapm.bias_level) {
			case SND_SOC_BIAS_STANDBY:
			case SND_SOC_BIAS_OFF:
				codec->driver->suspend(codec, PMSG_SUSPEND);
				codec->suspended = 1;
				break;
			default:
				dev_dbg(codec->dev, "CODEC is on over suspend\n");
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

	dev_dbg(card->dev, "starting resume work\n");

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
				dev_dbg(codec->dev, "CODEC was on over suspend\n");
				break;
			}
		}
	}

	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai_driver *driver = card->rtd[i].codec_dai->driver;

		if (card->rtd[i].dai_link->ignore_suspend)
			continue;

		if (driver->playback.stream_name != NULL)
			snd_soc_dapm_stream_event(&card->rtd[i], driver->playback.stream_name,
				SND_SOC_DAPM_STREAM_RESUME);

		if (driver->capture.stream_name != NULL)
			snd_soc_dapm_stream_event(&card->rtd[i], driver->capture.stream_name,
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

	dev_dbg(card->dev, "resume work completed\n");

	/* userspace can access us now we are back as we were before */
	snd_power_change_state(card->snd_card, SNDRV_CTL_POWER_D0);
}

/* powers up audio subsystem after a suspend */
int snd_soc_resume(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	int i;

	/* AC97 devices might have other drivers hanging off them so
	 * need to resume immediately.  Other drivers don't have that
	 * problem and may take a substantial amount of time to resume
	 * due to I/O costs and anti-pop so handle them out of line.
	 */
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dai *cpu_dai = card->rtd[i].cpu_dai;
		if (cpu_dai->driver->ac97_control) {
			dev_dbg(dev, "Resuming AC97 immediately\n");
			soc_resume_deferred(&card->deferred_resume_work);
		} else {
			dev_dbg(dev, "Scheduling resume work\n");
			if (!schedule_work(&card->deferred_resume_work))
				dev_err(dev, "resume work item may be lost\n");
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_resume);
#else
#define snd_soc_suspend NULL
#define snd_soc_resume NULL
#endif

static struct snd_soc_dai_ops null_dai_ops = {
};

static int soc_bind_dai_link(struct snd_soc_card *card, int num)
{
	struct snd_soc_dai_link *dai_link = &card->dai_link[num];
	struct snd_soc_pcm_runtime *rtd = &card->rtd[num];
	struct snd_soc_codec *codec;
	struct snd_soc_platform *platform;
	struct snd_soc_dai *codec_dai, *cpu_dai;

	if (rtd->complete)
		return 1;
	dev_dbg(card->dev, "binding %s at idx %d\n", dai_link->name, num);

	/* do we already have the CPU DAI for this link ? */
	if (rtd->cpu_dai) {
		goto find_codec;
	}
	/* no, then find CPU DAI from registered DAIs*/
	list_for_each_entry(cpu_dai, &dai_list, list) {
		if (!strcmp(cpu_dai->name, dai_link->cpu_dai_name)) {

			if (!try_module_get(cpu_dai->dev->driver->owner))
				return -ENODEV;

			rtd->cpu_dai = cpu_dai;
			goto find_codec;
		}
	}
	dev_dbg(card->dev, "CPU DAI %s not registered\n",
			dai_link->cpu_dai_name);

find_codec:
	/* do we already have the CODEC for this link ? */
	if (rtd->codec) {
		goto find_platform;
	}

	/* no, then find CODEC from registered CODECs*/
	list_for_each_entry(codec, &codec_list, list) {
		if (!strcmp(codec->name, dai_link->codec_name)) {
			rtd->codec = codec;

			/* CODEC found, so find CODEC DAI from registered DAIs from this CODEC*/
			list_for_each_entry(codec_dai, &dai_list, list) {
				if (codec->dev == codec_dai->dev &&
						!strcmp(codec_dai->name, dai_link->codec_dai_name)) {
					rtd->codec_dai = codec_dai;
					goto find_platform;
				}
			}
			dev_dbg(card->dev, "CODEC DAI %s not registered\n",
					dai_link->codec_dai_name);

			goto find_platform;
		}
	}
	dev_dbg(card->dev, "CODEC %s not registered\n",
			dai_link->codec_name);

find_platform:
	/* do we already have the CODEC DAI for this link ? */
	if (rtd->platform) {
		goto out;
	}
	/* no, then find CPU DAI from registered DAIs*/
	list_for_each_entry(platform, &platform_list, list) {
		if (!strcmp(platform->name, dai_link->platform_name)) {
			rtd->platform = platform;
			goto out;
		}
	}

	dev_dbg(card->dev, "platform %s not registered\n",
			dai_link->platform_name);
	return 0;

out:
	/* mark rtd as complete if we found all 4 of our client devices */
	if (rtd->codec && rtd->codec_dai && rtd->platform && rtd->cpu_dai) {
		rtd->complete = 1;
		card->num_rtd++;
	}
	return 1;
}

static void soc_remove_codec(struct snd_soc_codec *codec)
{
	int err;

	if (codec->driver->remove) {
		err = codec->driver->remove(codec);
		if (err < 0)
			dev_err(codec->dev,
				"asoc: failed to remove %s: %d\n",
				codec->name, err);
	}

	/* Make sure all DAPM widgets are freed */
	snd_soc_dapm_free(&codec->dapm);

	soc_cleanup_codec_debugfs(codec);
	codec->probed = 0;
	list_del(&codec->card_list);
	module_put(codec->dev->driver->owner);
}

static void soc_remove_dai_link(struct snd_soc_card *card, int num)
{
	struct snd_soc_pcm_runtime *rtd = &card->rtd[num];
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *codec_dai = rtd->codec_dai, *cpu_dai = rtd->cpu_dai;
	int err;

	/* unregister the rtd device */
	if (rtd->dev_registered) {
		device_remove_file(&rtd->dev, &dev_attr_pmdown_time);
		device_remove_file(&rtd->dev, &dev_attr_codec_reg);
		device_unregister(&rtd->dev);
		rtd->dev_registered = 0;
	}

	/* remove the CODEC DAI */
	if (codec_dai && codec_dai->probed) {
		if (codec_dai->driver->remove) {
			err = codec_dai->driver->remove(codec_dai);
			if (err < 0)
				printk(KERN_ERR "asoc: failed to remove %s\n", codec_dai->name);
		}
		codec_dai->probed = 0;
		list_del(&codec_dai->card_list);
	}

	/* remove the platform */
	if (platform && platform->probed) {
		if (platform->driver->remove) {
			err = platform->driver->remove(platform);
			if (err < 0)
				printk(KERN_ERR "asoc: failed to remove %s\n", platform->name);
		}
		platform->probed = 0;
		list_del(&platform->card_list);
		module_put(platform->dev->driver->owner);
	}

	/* remove the CODEC */
	if (codec && codec->probed)
		soc_remove_codec(codec);

	/* remove the cpu_dai */
	if (cpu_dai && cpu_dai->probed) {
		if (cpu_dai->driver->remove) {
			err = cpu_dai->driver->remove(cpu_dai);
			if (err < 0)
				printk(KERN_ERR "asoc: failed to remove %s\n", cpu_dai->name);
		}
		cpu_dai->probed = 0;
		list_del(&cpu_dai->card_list);
		module_put(cpu_dai->dev->driver->owner);
	}
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

	codec->card = card;
	codec->dapm.card = card;
	soc_set_name_prefix(card, codec);

	if (!try_module_get(codec->dev->driver->owner))
		return -ENODEV;

	if (driver->probe) {
		ret = driver->probe(codec);
		if (ret < 0) {
			dev_err(codec->dev,
				"asoc: failed to probe CODEC %s: %d\n",
				codec->name, ret);
			goto err_probe;
		}
	}

	if (driver->dapm_widgets)
		snd_soc_dapm_new_controls(&codec->dapm, driver->dapm_widgets,
					  driver->num_dapm_widgets);
	if (driver->dapm_routes)
		snd_soc_dapm_add_routes(&codec->dapm, driver->dapm_routes,
					driver->num_dapm_routes);

	soc_init_codec_debugfs(codec);

	/* mark codec as probed and add to card codec list */
	codec->probed = 1;
	list_add(&codec->card_list, &card->codec_dev_list);
	list_add(&codec->dapm.list, &card->dapm_list);

	return 0;

err_probe:
	module_put(codec->dev->driver->owner);

	return ret;
}

static void rtd_release(struct device *dev) {}

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

	/* machine controls, routes and widgets are not prefixed */
	temp = codec->name_prefix;
	codec->name_prefix = NULL;

	/* do machine specific initialization */
	if (!dailess && dai_link->init)
		ret = dai_link->init(rtd);
	else if (dailess && aux_dev->init)
		ret = aux_dev->init(&codec->dapm);
	if (ret < 0) {
		dev_err(card->dev, "asoc: failed to init %s: %d\n", name, ret);
		return ret;
	}
	codec->name_prefix = temp;

	/* Make sure all DAPM widgets are instantiated */
	snd_soc_dapm_new_widgets(&codec->dapm);

	/* register the rtd device */
	rtd->codec = codec;
	rtd->dev.parent = card->dev;
	rtd->dev.release = rtd_release;
	rtd->dev.init_name = name;
	ret = device_register(&rtd->dev);
	if (ret < 0) {
		dev_err(card->dev,
			"asoc: failed to register runtime device: %d\n", ret);
		return ret;
	}
	rtd->dev_registered = 1;

	/* add DAPM sysfs entries for this codec */
	ret = snd_soc_dapm_sys_add(&rtd->dev);
	if (ret < 0)
		dev_err(codec->dev,
			"asoc: failed to add codec dapm sysfs entries: %d\n",
			ret);

	/* add codec sysfs entries */
	ret = device_create_file(&rtd->dev, &dev_attr_codec_reg);
	if (ret < 0)
		dev_err(codec->dev,
			"asoc: failed to add codec sysfs files: %d\n", ret);

	return 0;
}

static int soc_probe_dai_link(struct snd_soc_card *card, int num)
{
	struct snd_soc_dai_link *dai_link = &card->dai_link[num];
	struct snd_soc_pcm_runtime *rtd = &card->rtd[num];
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *codec_dai = rtd->codec_dai, *cpu_dai = rtd->cpu_dai;
	int ret;

	dev_dbg(card->dev, "probe %s dai link %d\n", card->name, num);

	/* config components */
	codec_dai->codec = codec;
	cpu_dai->platform = platform;
	codec_dai->card = card;
	cpu_dai->card = card;

	/* set default power off timeout */
	rtd->pmdown_time = pmdown_time;

	/* probe the cpu_dai */
	if (!cpu_dai->probed) {
		if (cpu_dai->driver->probe) {
			ret = cpu_dai->driver->probe(cpu_dai);
			if (ret < 0) {
				printk(KERN_ERR "asoc: failed to probe CPU DAI %s\n",
						cpu_dai->name);
				return ret;
			}
		}
		cpu_dai->probed = 1;
		/* mark cpu_dai as probed and add to card cpu_dai list */
		list_add(&cpu_dai->card_list, &card->dai_dev_list);
	}

	/* probe the CODEC */
	if (!codec->probed) {
		ret = soc_probe_codec(card, codec);
		if (ret < 0)
			return ret;
	}

	/* probe the platform */
	if (!platform->probed) {
		if (!try_module_get(platform->dev->driver->owner))
			return -ENODEV;

		if (platform->driver->probe) {
			ret = platform->driver->probe(platform);
			if (ret < 0) {
				printk(KERN_ERR "asoc: failed to probe platform %s\n",
						platform->name);
				module_put(platform->dev->driver->owner);
				return ret;
			}
		}
		/* mark platform as probed and add to card platform list */
		platform->probed = 1;
		list_add(&platform->card_list, &card->platform_dev_list);
	}

	/* probe the CODEC DAI */
	if (!codec_dai->probed) {
		if (codec_dai->driver->probe) {
			ret = codec_dai->driver->probe(codec_dai);
			if (ret < 0) {
				printk(KERN_ERR "asoc: failed to probe CODEC DAI %s\n",
						codec_dai->name);
				return ret;
			}
		}

		/* mark cpu_dai as probed and add to card cpu_dai list */
		codec_dai->probed = 1;
		list_add(&codec_dai->card_list, &card->dai_dev_list);
	}

	/* DAPM dai link stream work */
	INIT_DELAYED_WORK(&rtd->delayed_work, close_delayed_work);

	ret = soc_post_component_init(card, codec, num, 0);
	if (ret)
		return ret;

	ret = device_create_file(&rtd->dev, &dev_attr_pmdown_time);
	if (ret < 0)
		printk(KERN_WARNING "asoc: failed to add pmdown_time sysfs\n");

	/* create the pcm */
	ret = soc_new_pcm(rtd, num);
	if (ret < 0) {
		printk(KERN_ERR "asoc: can't create pcm %s\n", dai_link->stream_name);
		return ret;
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
			printk(KERN_ERR "asoc: AC97 device register failed\n");
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
					"asoc: codec already probed");
				ret = -EBUSY;
				goto out;
			}
			goto found;
		}
	}
	/* codec not found */
	dev_err(card->dev, "asoc: codec %s not found", aux_dev->codec_name);
	goto out;

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
		device_remove_file(&rtd->dev, &dev_attr_codec_reg);
		device_unregister(&rtd->dev);
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
		dev_err(codec->dev, "Failed to set cache compression type: %d\n",
			ret);
		return ret;
	}
	codec->cache_init = 1;
	return 0;
}

static void snd_soc_instantiate_card(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec;
	struct snd_soc_codec_conf *codec_conf;
	enum snd_soc_compress_type compress_type;
	int ret, i;

	mutex_lock(&card->mutex);

	if (card->instantiated) {
		mutex_unlock(&card->mutex);
		return;
	}

	/* bind DAIs */
	for (i = 0; i < card->num_links; i++)
		soc_bind_dai_link(card, i);

	/* bind completed ? */
	if (card->num_rtd != card->num_links) {
		mutex_unlock(&card->mutex);
		return;
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
		if (ret < 0) {
			mutex_unlock(&card->mutex);
			return;
		}
	}

	/* card bind complete so register a sound card */
	ret = snd_card_create(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			card->owner, 0, &card->snd_card);
	if (ret < 0) {
		printk(KERN_ERR "asoc: can't create sound card for card %s\n",
			card->name);
		mutex_unlock(&card->mutex);
		return;
	}
	card->snd_card->dev = card->dev;

	card->dapm.bias_level = SND_SOC_BIAS_OFF;
	card->dapm.dev = card->dev;
	card->dapm.card = card;
	list_add(&card->dapm.list, &card->dapm_list);

#ifdef CONFIG_PM_SLEEP
	/* deferred resume work */
	INIT_WORK(&card->deferred_resume_work, soc_resume_deferred);
#endif

	/* initialise the sound card only once */
	if (card->probe) {
		ret = card->probe(card);
		if (ret < 0)
			goto card_probe_error;
	}

	for (i = 0; i < card->num_links; i++) {
		ret = soc_probe_dai_link(card, i);
		if (ret < 0) {
			pr_err("asoc: failed to instantiate card %s: %d\n",
			       card->name, ret);
			goto probe_dai_err;
		}
	}

	for (i = 0; i < card->num_aux_devs; i++) {
		ret = soc_probe_aux_dev(card, i);
		if (ret < 0) {
			pr_err("asoc: failed to add auxiliary devices %s: %d\n",
			       card->name, ret);
			goto probe_aux_dev_err;
		}
	}

	if (card->dapm_widgets)
		snd_soc_dapm_new_controls(&card->dapm, card->dapm_widgets,
					  card->num_dapm_widgets);
	if (card->dapm_routes)
		snd_soc_dapm_add_routes(&card->dapm, card->dapm_routes,
					card->num_dapm_routes);

#ifdef CONFIG_DEBUG_FS
	card->dapm.debugfs_dapm = debugfs_create_dir("dapm",
						     card->debugfs_card_root);
	if (!card->dapm.debugfs_dapm)
		printk(KERN_WARNING
		       "Failed to create card DAPM debugfs directory\n");

	snd_soc_dapm_debugfs_init(&card->dapm);
#endif

	snprintf(card->snd_card->shortname, sizeof(card->snd_card->shortname),
		 "%s",  card->name);
	snprintf(card->snd_card->longname, sizeof(card->snd_card->longname),
		 "%s", card->name);

	if (card->late_probe) {
		ret = card->late_probe(card);
		if (ret < 0) {
			dev_err(card->dev, "%s late_probe() failed: %d\n",
				card->name, ret);
			goto probe_aux_dev_err;
		}
	}

	ret = snd_card_register(card->snd_card);
	if (ret < 0) {
		printk(KERN_ERR "asoc: failed to register soundcard for %s\n", card->name);
		goto probe_aux_dev_err;
	}

#ifdef CONFIG_SND_SOC_AC97_BUS
	/* register any AC97 codecs */
	for (i = 0; i < card->num_rtd; i++) {
		ret = soc_register_ac97_dai_link(&card->rtd[i]);
		if (ret < 0) {
			printk(KERN_ERR "asoc: failed to register AC97 %s\n", card->name);
			while (--i >= 0)
				soc_unregister_ac97_dai_link(card->rtd[i].codec);
			goto probe_aux_dev_err;
		}
	}
#endif

	card->instantiated = 1;
	mutex_unlock(&card->mutex);
	return;

probe_aux_dev_err:
	for (i = 0; i < card->num_aux_devs; i++)
		soc_remove_aux_dev(card, i);

probe_dai_err:
	for (i = 0; i < card->num_links; i++)
		soc_remove_dai_link(card, i);

card_probe_error:
	if (card->remove)
		card->remove(card);

	snd_card_free(card->snd_card);

	mutex_unlock(&card->mutex);
}

/*
 * Attempt to initialise any uninitialised cards.  Must be called with
 * client_mutex.
 */
static void snd_soc_instantiate_cards(void)
{
	struct snd_soc_card *card;
	list_for_each_entry(card, &card_list, list)
		snd_soc_instantiate_card(card);
}

/* probes a new socdev */
static int soc_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	int ret = 0;

	/*
	 * no card, so machine driver should be registering card
	 * we should not be here in that case so ret error
	 */
	if (!card)
		return -EINVAL;

	/* Bodge while we unpick instantiation */
	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to register card\n");
		return ret;
	}

	return 0;
}

static int soc_cleanup_card_resources(struct snd_soc_card *card)
{
	int i;

	/* make sure any delayed work runs */
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_pcm_runtime *rtd = &card->rtd[i];
		flush_delayed_work_sync(&rtd->delayed_work);
	}

	/* remove auxiliary devices */
	for (i = 0; i < card->num_aux_devs; i++)
		soc_remove_aux_dev(card, i);

	/* remove and free each DAI */
	for (i = 0; i < card->num_rtd; i++)
		soc_remove_dai_link(card, i);

	soc_cleanup_card_debugfs(card);

	/* remove the card */
	if (card->remove)
		card->remove(card);

	kfree(card->rtd);
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
		flush_delayed_work_sync(&rtd->delayed_work);
	}

	snd_soc_dapm_shutdown(card);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_poweroff);

const struct dev_pm_ops snd_soc_pm_ops = {
	.suspend = snd_soc_suspend,
	.resume = snd_soc_resume,
	.poweroff = snd_soc_poweroff,
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

/* create a new pcm */
static int soc_new_pcm(struct snd_soc_pcm_runtime *rtd, int num)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_pcm *pcm;
	char new_name[64];
	int ret = 0, playback = 0, capture = 0;

	/* check client and interface hw capabilities */
	snprintf(new_name, sizeof(new_name), "%s %s-%d",
			rtd->dai_link->stream_name, codec_dai->name, num);

	if (codec_dai->driver->playback.channels_min)
		playback = 1;
	if (codec_dai->driver->capture.channels_min)
		capture = 1;

	dev_dbg(rtd->card->dev, "registered pcm #%d %s\n",num,new_name);
	ret = snd_pcm_new(rtd->card->snd_card, new_name,
			num, playback, capture, &pcm);
	if (ret < 0) {
		printk(KERN_ERR "asoc: can't create pcm for codec %s\n", codec->name);
		return ret;
	}

	rtd->pcm = pcm;
	pcm->private_data = rtd;
	soc_pcm_ops.mmap = platform->driver->ops->mmap;
	soc_pcm_ops.pointer = platform->driver->ops->pointer;
	soc_pcm_ops.ioctl = platform->driver->ops->ioctl;
	soc_pcm_ops.copy = platform->driver->ops->copy;
	soc_pcm_ops.silence = platform->driver->ops->silence;
	soc_pcm_ops.ack = platform->driver->ops->ack;
	soc_pcm_ops.page = platform->driver->ops->page;

	if (playback)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &soc_pcm_ops);

	if (capture)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &soc_pcm_ops);

	ret = platform->driver->pcm_new(rtd->card->snd_card, codec_dai, pcm);
	if (ret < 0) {
		printk(KERN_ERR "asoc: platform pcm constructor failed\n");
		return ret;
	}

	pcm->private_free = platform->driver->pcm_free;
	printk(KERN_INFO "asoc: %s <-> %s mapping ok\n", codec_dai->name,
		cpu_dai->name);
	return ret;
}

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
	int change;
	unsigned int old, new;
	int ret;

	ret = snd_soc_read(codec, reg);
	if (ret < 0)
		return ret;

	old = ret;
	new = (old & ~mask) | value;
	change = old != new;
	if (change) {
		ret = snd_soc_write(codec, reg, new);
		if (ret < 0)
			return ret;
	}

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
				  void *data, char *long_name,
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
		name = kmalloc(name_len, GFP_ATOMIC);
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

/**
 * snd_soc_add_controls - add an array of controls to a codec.
 * Convienience function to add a list of controls. Many codecs were
 * duplicating this code.
 *
 * @codec: codec to add controls to
 * @controls: array of controls to add
 * @num_controls: number of elements in the array
 *
 * Return 0 for success, else error.
 */
int snd_soc_add_controls(struct snd_soc_codec *codec,
	const struct snd_kcontrol_new *controls, int num_controls)
{
	struct snd_card *card = codec->card->snd_card;
	int err, i;

	for (i = 0; i < num_controls; i++) {
		const struct snd_kcontrol_new *control = &controls[i];
		err = snd_ctl_add(card, snd_soc_cnew(control, codec,
						     control->name,
						     codec->name_prefix));
		if (err < 0) {
			dev_err(codec->dev, "%s: Failed to add %s: %d\n",
				codec->name, control->name, err);
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_add_controls);

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
	unsigned int val, bitmask;

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
	val = snd_soc_read(codec, e->reg);
	ucontrol->value.enumerated.item[0]
		= (val >> e->shift_l) & (bitmask - 1);
	if (e->shift_l != e->shift_r)
		ucontrol->value.enumerated.item[1] =
			(val >> e->shift_r) & (bitmask - 1);

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
	unsigned int mask, bitmask;

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->max - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= (bitmask - 1) << e->shift_r;
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
 * Callback to provide information about a single mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int platform_max;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;

	if (!mc->platform_max)
		mc->platform_max = mc->max;
	platform_max = mc->platform_max;

	if (platform_max == 1 && !strstr(kcontrol->id.name, " Volume"))
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = shift == rshift ? 1 : 2;
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
 * Callback to get the value of a single mixer control.
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
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;

	ucontrol->value.integer.value[0] =
		(snd_soc_read(codec, reg) >> shift) & mask;
	if (shift != rshift)
		ucontrol->value.integer.value[1] =
			(snd_soc_read(codec, reg) >> rshift) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];
		if (shift != rshift)
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
 * Callback to set the value of a single mixer control.
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
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val, val2, val_mask;

	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = max - val;
	val_mask = mask << shift;
	val = val << shift;
	if (shift != rshift) {
		val2 = (ucontrol->value.integer.value[1] & mask);
		if (invert)
			val2 = max - val2;
		val_mask |= mask << rshift;
		val |= val2 << rshift;
	}
	return snd_soc_update_bits_locked(codec, reg, val_mask, val);
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw);

/**
 * snd_soc_info_volsw_2r - double mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a double mixer control that
 * spans 2 codec registers.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw_2r(struct snd_kcontrol *kcontrol,
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

	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = platform_max;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw_2r);

/**
 * snd_soc_get_volsw_2r - double mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;

	ucontrol->value.integer.value[0] =
		(snd_soc_read(codec, reg) >> shift) & mask;
	ucontrol->value.integer.value[1] =
		(snd_soc_read(codec, reg2) >> shift) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] =
			max - ucontrol->value.integer.value[1];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw_2r);

/**
 * snd_soc_put_volsw_2r - double mixer set callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	int err;
	unsigned int val, val2, val_mask;

	val_mask = mask << shift;
	val = (ucontrol->value.integer.value[0] & mask);
	val2 = (ucontrol->value.integer.value[1] & mask);

	if (invert) {
		val = max - val;
		val2 = max - val2;
	}

	val = val << shift;
	val2 = val2 << shift;

	err = snd_soc_update_bits_locked(codec, reg, val_mask, val);
	if (err < 0)
		return err;

	err = snd_soc_update_bits_locked(codec, reg2, val_mask, val2);
	return err;
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw_2r);

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

/**
 * snd_soc_info_volsw_2r_sx - double with tlv and variable data size
 *  mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw_2r_sx(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int max = mc->max;
	int min = mc->min;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = max-min;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw_2r_sx);

/**
 * snd_soc_get_volsw_2r_sx - double with tlv and variable data size
 *  mixer get callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw_2r_sx(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int mask = (1<<mc->shift)-1;
	int min = mc->min;
	int val = snd_soc_read(codec, mc->reg) & mask;
	int valr = snd_soc_read(codec, mc->rreg) & mask;

	ucontrol->value.integer.value[0] = ((val & 0xff)-min) & mask;
	ucontrol->value.integer.value[1] = ((valr & 0xff)-min) & mask;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw_2r_sx);

/**
 * snd_soc_put_volsw_2r_sx - double with tlv and variable data size
 *  mixer put callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw_2r_sx(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int mask = (1<<mc->shift)-1;
	int min = mc->min;
	int ret;
	unsigned int val, valr, oval, ovalr;

	val = ((ucontrol->value.integer.value[0]+min) & 0xff);
	val &= mask;
	valr = ((ucontrol->value.integer.value[1]+min) & 0xff);
	valr &= mask;

	oval = snd_soc_read(codec, mc->reg) & mask;
	ovalr = snd_soc_read(codec, mc->rreg) & mask;

	ret = 0;
	if (oval != val) {
		ret = snd_soc_write(codec, mc->reg, val);
		if (ret < 0)
			return ret;
	}
	if (ovalr != valr) {
		ret = snd_soc_write(codec, mc->rreg, valr);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw_2r_sx);

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
		return dai->codec->driver->set_sysclk(dai->codec, clk_id,
						      freq, dir);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_sysclk);

/**
 * snd_soc_codec_set_sysclk - configure CODEC system or master clock.
 * @codec: CODEC
 * @clk_id: DAI specific clock ID
 * @freq: new clock frequency in Hz
 * @dir: new clock direction - input/output.
 *
 * Configures the CODEC master (MCLK) or system (SYSCLK) clocking.
 */
int snd_soc_codec_set_sysclk(struct snd_soc_codec *codec, int clk_id,
	unsigned int freq, int dir)
{
	if (codec->driver->set_sysclk)
		return codec->driver->set_sysclk(codec, clk_id, freq, dir);
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
	if (dai->driver && dai->driver->ops->set_fmt)
		return dai->driver->ops->set_fmt(dai, fmt);
	else
		return -EINVAL;
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
 *
 * Mutes the DAI DAC.
 */
int snd_soc_dai_digital_mute(struct snd_soc_dai *dai, int mute)
{
	if (dai->driver && dai->driver->ops->digital_mute)
		return dai->driver->ops->digital_mute(dai, mute);
	else
		return -EINVAL;
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
	int i;

	if (!card->name || !card->dev)
		return -EINVAL;

	dev_set_drvdata(card->dev, card);

	snd_soc_initialize_card_lists(card);

	soc_init_card_debugfs(card);

	card->rtd = kzalloc(sizeof(struct snd_soc_pcm_runtime) *
			    (card->num_links + card->num_aux_devs),
			    GFP_KERNEL);
	if (card->rtd == NULL)
		return -ENOMEM;
	card->rtd_aux = &card->rtd[card->num_links];

	for (i = 0; i < card->num_links; i++)
		card->rtd[i].dai_link = &card->dai_link[i];

	INIT_LIST_HEAD(&card->list);
	card->instantiated = 0;
	mutex_init(&card->mutex);

	mutex_lock(&client_mutex);
	list_add(&card->list, &card_list);
	snd_soc_instantiate_cards();
	mutex_unlock(&client_mutex);

	dev_dbg(card->dev, "Registered card '%s'\n", card->name);

	return 0;
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
	mutex_lock(&client_mutex);
	list_del(&card->list);
	mutex_unlock(&client_mutex);
	dev_dbg(card->dev, "Unregistered card '%s'\n", card->name);

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
		printk(KERN_ERR "asoc: error - multiple DAI %s registered with no name\n",
				dev_name(dev));
		return NULL;
	}

	return kstrdup(dai_drv->name, GFP_KERNEL);
}

/**
 * snd_soc_register_dai - Register a DAI with the ASoC core
 *
 * @dai: DAI to register
 */
int snd_soc_register_dai(struct device *dev,
		struct snd_soc_dai_driver *dai_drv)
{
	struct snd_soc_dai *dai;

	dev_dbg(dev, "dai register %s\n", dev_name(dev));

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
	if (!dai->driver->ops)
		dai->driver->ops = &null_dai_ops;

	mutex_lock(&client_mutex);
	list_add(&dai->list, &dai_list);
	snd_soc_instantiate_cards();
	mutex_unlock(&client_mutex);

	pr_debug("Registered DAI '%s'\n", dai->name);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_register_dai);

/**
 * snd_soc_unregister_dai - Unregister a DAI from the ASoC core
 *
 * @dai: DAI to unregister
 */
void snd_soc_unregister_dai(struct device *dev)
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

	pr_debug("Unregistered DAI '%s'\n", dai->name);
	kfree(dai->name);
	kfree(dai);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_dai);

/**
 * snd_soc_register_dais - Register multiple DAIs with the ASoC core
 *
 * @dai: Array of DAIs to register
 * @count: Number of DAIs
 */
int snd_soc_register_dais(struct device *dev,
		struct snd_soc_dai_driver *dai_drv, size_t count)
{
	struct snd_soc_dai *dai;
	int i, ret = 0;

	dev_dbg(dev, "dai register %s #%Zu\n", dev_name(dev), count);

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
		if (!dai->driver->ops)
			dai->driver->ops = &null_dai_ops;

		mutex_lock(&client_mutex);
		list_add(&dai->list, &dai_list);
		mutex_unlock(&client_mutex);

		pr_debug("Registered DAI '%s'\n", dai->name);
	}

	mutex_lock(&client_mutex);
	snd_soc_instantiate_cards();
	mutex_unlock(&client_mutex);
	return 0;

err:
	for (i--; i >= 0; i--)
		snd_soc_unregister_dai(dev);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_register_dais);

/**
 * snd_soc_unregister_dais - Unregister multiple DAIs from the ASoC core
 *
 * @dai: Array of DAIs to unregister
 * @count: Number of DAIs
 */
void snd_soc_unregister_dais(struct device *dev, size_t count)
{
	int i;

	for (i = 0; i < count; i++)
		snd_soc_unregister_dai(dev);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_dais);

/**
 * snd_soc_register_platform - Register a platform with the ASoC core
 *
 * @platform: platform to register
 */
int snd_soc_register_platform(struct device *dev,
		struct snd_soc_platform_driver *platform_drv)
{
	struct snd_soc_platform *platform;

	dev_dbg(dev, "platform register %s\n", dev_name(dev));

	platform = kzalloc(sizeof(struct snd_soc_platform), GFP_KERNEL);
	if (platform == NULL)
			return -ENOMEM;

	/* create platform component name */
	platform->name = fmt_single_name(dev, &platform->id);
	if (platform->name == NULL) {
		kfree(platform);
		return -ENOMEM;
	}

	platform->dev = dev;
	platform->driver = platform_drv;

	mutex_lock(&client_mutex);
	list_add(&platform->list, &platform_list);
	snd_soc_instantiate_cards();
	mutex_unlock(&client_mutex);

	pr_debug("Registered platform '%s'\n", platform->name);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_register_platform);

/**
 * snd_soc_unregister_platform - Unregister a platform from the ASoC core
 *
 * @platform: platform to unregister
 */
void snd_soc_unregister_platform(struct device *dev)
{
	struct snd_soc_platform *platform;

	list_for_each_entry(platform, &platform_list, list) {
		if (dev == platform->dev)
			goto found;
	}
	return;

found:
	mutex_lock(&client_mutex);
	list_del(&platform->list);
	mutex_unlock(&client_mutex);

	pr_debug("Unregistered platform '%s'\n", platform->name);
	kfree(platform->name);
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
		kfree(codec);
		return -ENOMEM;
	}

	if (codec_drv->compress_type)
		codec->compress_type = codec_drv->compress_type;
	else
		codec->compress_type = SND_SOC_FLAT_COMPRESSION;

	codec->write = codec_drv->write;
	codec->read = codec_drv->read;
	codec->volatile_register = codec_drv->volatile_register;
	codec->readable_register = codec_drv->readable_register;
	codec->dapm.bias_level = SND_SOC_BIAS_OFF;
	codec->dapm.dev = dev;
	codec->dapm.codec = codec;
	codec->dapm.seq_notifier = codec_drv->seq_notifier;
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
		 * the default register cache to be marked as __devinitconst the
		 * kernel might have freed the array by the time we initialize
		 * the cache.
		 */
		if (codec_drv->reg_cache_default) {
			codec->reg_def_copy = kmemdup(codec_drv->reg_cache_default,
						      reg_size, GFP_KERNEL);
			if (!codec->reg_def_copy) {
				ret = -ENOMEM;
				goto fail;
			}
		}
	}

	if (codec_drv->reg_access_size && codec_drv->reg_access_default) {
		if (!codec->volatile_register)
			codec->volatile_register = snd_soc_default_volatile_register;
		if (!codec->readable_register)
			codec->readable_register = snd_soc_default_readable_register;
	}

	for (i = 0; i < num_dai; i++) {
		fixup_codec_formats(&dai_drv[i].playback);
		fixup_codec_formats(&dai_drv[i].capture);
	}

	/* register any DAIs */
	if (num_dai) {
		ret = snd_soc_register_dais(dev, dai_drv, num_dai);
		if (ret < 0)
			goto fail;
	}

	mutex_lock(&client_mutex);
	list_add(&codec->list, &codec_list);
	snd_soc_instantiate_cards();
	mutex_unlock(&client_mutex);

	pr_debug("Registered codec '%s'\n", codec->name);
	return 0;

fail:
	kfree(codec->reg_def_copy);
	codec->reg_def_copy = NULL;
	kfree(codec->name);
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
	int i;

	list_for_each_entry(codec, &codec_list, list) {
		if (dev == codec->dev)
			goto found;
	}
	return;

found:
	if (codec->num_dai)
		for (i = 0; i < codec->num_dai; i++)
			snd_soc_unregister_dai(dev);

	mutex_lock(&client_mutex);
	list_del(&codec->list);
	mutex_unlock(&client_mutex);

	pr_debug("Unregistered codec '%s'\n", codec->name);

	snd_soc_cache_exit(codec);
	kfree(codec->reg_def_copy);
	kfree(codec->name);
	kfree(codec);
}
EXPORT_SYMBOL_GPL(snd_soc_unregister_codec);

static int __init snd_soc_init(void)
{
#ifdef CONFIG_DEBUG_FS
	snd_soc_debugfs_root = debugfs_create_dir("asoc", NULL);
	if (IS_ERR(snd_soc_debugfs_root) || !snd_soc_debugfs_root) {
		printk(KERN_WARNING
		       "ASoC: Failed to create debugfs directory\n");
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

	return platform_driver_register(&soc_driver);
}
module_init(snd_soc_init);

static void __exit snd_soc_exit(void)
{
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

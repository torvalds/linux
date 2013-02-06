/*
 * soc-compress.c  --  ALSA SoC Compress
 *
 * Copyright (C) 2012 Intel Corp.
 *
 * Authors: Namarta Kohli <namartax.kohli@intel.com>
 *          Ramesh Babu K V <ramesh.babu@linux.intel.com>
 *          Vinod Koul <vinod.koul@linux.intel.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/compress_params.h>
#include <sound/compress_driver.h>
#include <sound/soc.h>
#include <sound/initval.h>

static int soc_compr_open(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (platform->driver->compr_ops && platform->driver->compr_ops->open) {
		ret = platform->driver->compr_ops->open(cstream);
		if (ret < 0) {
			pr_err("compress asoc: can't open platform %s\n", platform->name);
			goto out;
		}
	}

	if (rtd->dai_link->compr_ops && rtd->dai_link->compr_ops->startup) {
		ret = rtd->dai_link->compr_ops->startup(cstream);
		if (ret < 0) {
			pr_err("compress asoc: %s startup failed\n", rtd->dai_link->name);
			goto machine_err;
		}
	}

	if (cstream->direction == SND_COMPRESS_PLAYBACK) {
		cpu_dai->playback_active++;
		codec_dai->playback_active++;
	} else {
		cpu_dai->capture_active++;
		codec_dai->capture_active++;
	}

	cpu_dai->active++;
	codec_dai->active++;
	rtd->codec->active++;

	mutex_unlock(&rtd->pcm_mutex);

	return 0;

machine_err:
	if (platform->driver->compr_ops && platform->driver->compr_ops->free)
		platform->driver->compr_ops->free(cstream);
out:
	mutex_unlock(&rtd->pcm_mutex);
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

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	dev_dbg(rtd->dev, "ASoC: pop wq checking: %s status: %s waiting: %s\n",
		 codec_dai->driver->playback.stream_name,
		 codec_dai->playback_active ? "active" : "inactive",
		 rtd->pop_wait ? "yes" : "no");

	/* are we waiting on this codec DAI stream */
	if (rtd->pop_wait == 1) {
		rtd->pop_wait = 0;
		snd_soc_dapm_stream_event(rtd, SNDRV_PCM_STREAM_PLAYBACK,
					  SND_SOC_DAPM_STREAM_STOP);
	}

	mutex_unlock(&rtd->pcm_mutex);
}

static int soc_compr_free(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (cstream->direction == SND_COMPRESS_PLAYBACK) {
		cpu_dai->playback_active--;
		codec_dai->playback_active--;
		snd_soc_dai_digital_mute(codec_dai, 1);
	} else {
		cpu_dai->capture_active--;
		codec_dai->capture_active--;
	}

	cpu_dai->active--;
	codec_dai->active--;
	codec->active--;

	if (!cpu_dai->active)
		cpu_dai->rate = 0;

	if (!codec_dai->active)
		codec_dai->rate = 0;


	if (rtd->dai_link->compr_ops && rtd->dai_link->compr_ops->shutdown)
		rtd->dai_link->compr_ops->shutdown(cstream);

	if (platform->driver->compr_ops && platform->driver->compr_ops->free)
		platform->driver->compr_ops->free(cstream);
	cpu_dai->runtime = NULL;

	if (cstream->direction == SND_COMPRESS_PLAYBACK) {
		if (!rtd->pmdown_time || codec->ignore_pmdown_time ||
		    rtd->dai_link->ignore_pmdown_time) {
			snd_soc_dapm_stream_event(rtd,
					SNDRV_PCM_STREAM_PLAYBACK,
					SND_SOC_DAPM_STREAM_STOP);
		} else {
			rtd->pop_wait = 1;
			schedule_delayed_work(&rtd->delayed_work,
				msecs_to_jiffies(rtd->pmdown_time));
		}
	} else {
		/* capture streams can be powered down now */
		snd_soc_dapm_stream_event(rtd,
			SNDRV_PCM_STREAM_CAPTURE,
			SND_SOC_DAPM_STREAM_STOP);
	}

	mutex_unlock(&rtd->pcm_mutex);
	return 0;
}

static int soc_compr_trigger(struct snd_compr_stream *cstream, int cmd)
{

	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (platform->driver->compr_ops && platform->driver->compr_ops->trigger) {
		ret = platform->driver->compr_ops->trigger(cstream, cmd);
		if (ret < 0)
			goto out;
	}

	if (cstream->direction == SND_COMPRESS_PLAYBACK) {
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
			snd_soc_dai_digital_mute(codec_dai, 0);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
			snd_soc_dai_digital_mute(codec_dai, 1);
			break;
		}
	}

out:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static int soc_compr_set_params(struct snd_compr_stream *cstream,
					struct snd_compr_params *params)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	/* first we call set_params for the platform driver
	 * this should configure the soc side
	 * if the machine has compressed ops then we call that as well
	 * expectation is that platform and machine will configure everything
	 * for this compress path, like configuring pcm port for codec
	 */
	if (platform->driver->compr_ops && platform->driver->compr_ops->set_params) {
		ret = platform->driver->compr_ops->set_params(cstream, params);
		if (ret < 0)
			goto out;
	}

	if (rtd->dai_link->compr_ops && rtd->dai_link->compr_ops->set_params) {
		ret = rtd->dai_link->compr_ops->set_params(cstream);
		if (ret < 0)
			goto out;
	}

	snd_soc_dapm_stream_event(rtd, SNDRV_PCM_STREAM_PLAYBACK,
				SND_SOC_DAPM_STREAM_START);

out:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static int soc_compr_get_params(struct snd_compr_stream *cstream,
					struct snd_codec *params)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (platform->driver->compr_ops && platform->driver->compr_ops->get_params)
		ret = platform->driver->compr_ops->get_params(cstream, params);

	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static int soc_compr_get_caps(struct snd_compr_stream *cstream,
				struct snd_compr_caps *caps)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (platform->driver->compr_ops && platform->driver->compr_ops->get_caps)
		ret = platform->driver->compr_ops->get_caps(cstream, caps);

	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static int soc_compr_get_codec_caps(struct snd_compr_stream *cstream,
				struct snd_compr_codec_caps *codec)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (platform->driver->compr_ops && platform->driver->compr_ops->get_codec_caps)
		ret = platform->driver->compr_ops->get_codec_caps(cstream, codec);

	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static int soc_compr_ack(struct snd_compr_stream *cstream, size_t bytes)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (platform->driver->compr_ops && platform->driver->compr_ops->ack)
		ret = platform->driver->compr_ops->ack(cstream, bytes);

	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static int soc_compr_pointer(struct snd_compr_stream *cstream,
			struct snd_compr_tstamp *tstamp)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (platform->driver->compr_ops && platform->driver->compr_ops->pointer)
		 platform->driver->compr_ops->pointer(cstream, tstamp);

	mutex_unlock(&rtd->pcm_mutex);
	return 0;
}

static int soc_compr_copy(struct snd_compr_stream *cstream,
			  const char __user *buf, size_t count)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (platform->driver->compr_ops && platform->driver->compr_ops->copy)
		ret = platform->driver->compr_ops->copy(cstream, buf, count);

	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

/* ASoC Compress operations */
static struct snd_compr_ops soc_compr_ops = {
	.open		= soc_compr_open,
	.free		= soc_compr_free,
	.set_params	= soc_compr_set_params,
	.get_params	= soc_compr_get_params,
	.trigger	= soc_compr_trigger,
	.pointer	= soc_compr_pointer,
	.ack		= soc_compr_ack,
	.get_caps	= soc_compr_get_caps,
	.get_codec_caps = soc_compr_get_codec_caps
};

/* create a new compress */
int soc_new_compress(struct snd_soc_pcm_runtime *rtd, int num)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_compr *compr;
	char new_name[64];
	int ret = 0, direction = 0;

	/* check client and interface hw capabilities */
	snprintf(new_name, sizeof(new_name), "%s %s-%d",
			rtd->dai_link->stream_name, codec_dai->name, num);
	direction = SND_COMPRESS_PLAYBACK;
	compr = kzalloc(sizeof(*compr), GFP_KERNEL);
	if (compr == NULL) {
		snd_printk(KERN_ERR "Cannot allocate compr\n");
		return -ENOMEM;
	}

	compr->ops = devm_kzalloc(rtd->card->dev, sizeof(soc_compr_ops),
				  GFP_KERNEL);
	if (compr->ops == NULL) {
		dev_err(rtd->card->dev, "Cannot allocate compressed ops\n");
		ret = -ENOMEM;
		goto compr_err;
	}
	memcpy(compr->ops, &soc_compr_ops, sizeof(soc_compr_ops));

	/* Add copy callback for not memory mapped DSPs */
	if (platform->driver->compr_ops && platform->driver->compr_ops->copy)
		compr->ops->copy = soc_compr_copy;

	mutex_init(&compr->lock);
	ret = snd_compress_new(rtd->card->snd_card, num, direction, compr);
	if (ret < 0) {
		pr_err("compress asoc: can't create compress for codec %s\n",
			codec->name);
		goto compr_err;
	}

	/* DAPM dai link stream work */
	INIT_DELAYED_WORK(&rtd->delayed_work, close_delayed_work);

	rtd->compr = compr;
	compr->private_data = rtd;

	printk(KERN_INFO "compress asoc: %s <-> %s mapping ok\n", codec_dai->name,
		cpu_dai->name);
	return ret;

compr_err:
	kfree(compr);
	return ret;
}

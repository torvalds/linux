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
#include <sound/soc-dpcm.h>

static int soc_compr_open(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
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

	snd_soc_runtime_activate(rtd, cstream->direction);

	mutex_unlock(&rtd->pcm_mutex);

	return 0;

machine_err:
	if (platform->driver->compr_ops && platform->driver->compr_ops->free)
		platform->driver->compr_ops->free(cstream);
out:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static int soc_compr_open_fe(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *fe = cstream->private_data;
	struct snd_pcm_substream *fe_substream = fe->pcm->streams[0].substream;
	struct snd_soc_platform *platform = fe->platform;
	struct snd_soc_dpcm *dpcm;
	struct snd_soc_dapm_widget_list *list;
	int stream;
	int ret = 0;

	if (cstream->direction == SND_COMPRESS_PLAYBACK)
		stream = SNDRV_PCM_STREAM_PLAYBACK;
	else
		stream = SNDRV_PCM_STREAM_CAPTURE;

	mutex_lock_nested(&fe->card->mutex, SND_SOC_CARD_CLASS_RUNTIME);

	if (platform->driver->compr_ops && platform->driver->compr_ops->open) {
		ret = platform->driver->compr_ops->open(cstream);
		if (ret < 0) {
			pr_err("compress asoc: can't open platform %s\n", platform->name);
			goto out;
		}
	}

	if (fe->dai_link->compr_ops && fe->dai_link->compr_ops->startup) {
		ret = fe->dai_link->compr_ops->startup(cstream);
		if (ret < 0) {
			pr_err("compress asoc: %s startup failed\n", fe->dai_link->name);
			goto machine_err;
		}
	}

	fe->dpcm[stream].runtime = fe_substream->runtime;

	if (dpcm_path_get(fe, stream, &list) <= 0) {
		dev_dbg(fe->dev, "ASoC: %s no valid %s route\n",
			fe->dai_link->name, stream ? "capture" : "playback");
	}

	/* calculate valid and active FE <-> BE dpcms */
	dpcm_process_paths(fe, stream, &list, 1);

	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_FE;

	ret = dpcm_be_dai_startup(fe, stream);
	if (ret < 0) {
		/* clean up all links */
		list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be)
			dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;

		dpcm_be_disconnect(fe, stream);
		fe->dpcm[stream].runtime = NULL;
		goto fe_err;
	}

	dpcm_clear_pending_state(fe, stream);
	dpcm_path_put(&list);

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_OPEN;
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;

	snd_soc_runtime_activate(fe, stream);

	mutex_unlock(&fe->card->mutex);

	return 0;

fe_err:
	if (fe->dai_link->compr_ops && fe->dai_link->compr_ops->shutdown)
		fe->dai_link->compr_ops->shutdown(cstream);
machine_err:
	if (platform->driver->compr_ops && platform->driver->compr_ops->free)
		platform->driver->compr_ops->free(cstream);
out:
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;
	mutex_unlock(&fe->card->mutex);
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
	int stream;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (cstream->direction == SND_COMPRESS_PLAYBACK)
		stream = SNDRV_PCM_STREAM_PLAYBACK;
	else
		stream = SNDRV_PCM_STREAM_CAPTURE;

	snd_soc_runtime_deactivate(rtd, stream);

	snd_soc_dai_digital_mute(codec_dai, 1, cstream->direction);

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
		if (snd_soc_runtime_ignore_pmdown_time(rtd)) {
			snd_soc_dapm_stream_event(rtd,
					SNDRV_PCM_STREAM_PLAYBACK,
					SND_SOC_DAPM_STREAM_STOP);
		} else {
			rtd->pop_wait = 1;
			queue_delayed_work(system_power_efficient_wq,
					   &rtd->delayed_work,
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

static int soc_compr_free_fe(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *fe = cstream->private_data;
	struct snd_soc_platform *platform = fe->platform;
	struct snd_soc_dpcm *dpcm;
	int stream, ret;

	mutex_lock_nested(&fe->card->mutex, SND_SOC_CARD_CLASS_RUNTIME);

	if (cstream->direction == SND_COMPRESS_PLAYBACK)
		stream = SNDRV_PCM_STREAM_PLAYBACK;
	else
		stream = SNDRV_PCM_STREAM_CAPTURE;

	snd_soc_runtime_deactivate(fe, stream);

	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_FE;

	ret = dpcm_be_dai_hw_free(fe, stream);
	if (ret < 0)
		dev_err(fe->dev, "compressed hw_free failed %d\n", ret);

	ret = dpcm_be_dai_shutdown(fe, stream);

	/* mark FE's links ready to prune */
	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be)
		dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_STOP);
	else
		dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_STOP);

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;

	dpcm_be_disconnect(fe, stream);

	fe->dpcm[stream].runtime = NULL;

	if (fe->dai_link->compr_ops && fe->dai_link->compr_ops->shutdown)
		fe->dai_link->compr_ops->shutdown(cstream);

	if (platform->driver->compr_ops && platform->driver->compr_ops->free)
		platform->driver->compr_ops->free(cstream);

	mutex_unlock(&fe->card->mutex);
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

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_soc_dai_digital_mute(codec_dai, 0, cstream->direction);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_soc_dai_digital_mute(codec_dai, 1, cstream->direction);
		break;
	}

out:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static int soc_compr_trigger_fe(struct snd_compr_stream *cstream, int cmd)
{
	struct snd_soc_pcm_runtime *fe = cstream->private_data;
	struct snd_soc_platform *platform = fe->platform;
	int ret = 0, stream;

	if (cmd == SND_COMPR_TRIGGER_PARTIAL_DRAIN ||
		cmd == SND_COMPR_TRIGGER_DRAIN) {

		if (platform->driver->compr_ops &&
			platform->driver->compr_ops->trigger)
		return platform->driver->compr_ops->trigger(cstream, cmd);
	}

	if (cstream->direction == SND_COMPRESS_PLAYBACK)
		stream = SNDRV_PCM_STREAM_PLAYBACK;
	else
		stream = SNDRV_PCM_STREAM_CAPTURE;


	mutex_lock_nested(&fe->card->mutex, SND_SOC_CARD_CLASS_RUNTIME);

	if (platform->driver->compr_ops && platform->driver->compr_ops->trigger) {
		ret = platform->driver->compr_ops->trigger(cstream, cmd);
		if (ret < 0)
			goto out;
	}

	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_FE;

	ret = dpcm_be_dai_trigger(fe, stream, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		fe->dpcm[stream].state = SND_SOC_DPCM_STATE_START;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		fe->dpcm[stream].state = SND_SOC_DPCM_STATE_STOP;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		fe->dpcm[stream].state = SND_SOC_DPCM_STATE_PAUSED;
		break;
	}

out:
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;
	mutex_unlock(&fe->card->mutex);
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
			goto err;
	}

	if (rtd->dai_link->compr_ops && rtd->dai_link->compr_ops->set_params) {
		ret = rtd->dai_link->compr_ops->set_params(cstream);
		if (ret < 0)
			goto err;
	}

	if (cstream->direction == SND_COMPRESS_PLAYBACK)
		snd_soc_dapm_stream_event(rtd, SNDRV_PCM_STREAM_PLAYBACK,
					SND_SOC_DAPM_STREAM_START);
	else
		snd_soc_dapm_stream_event(rtd, SNDRV_PCM_STREAM_CAPTURE,
					SND_SOC_DAPM_STREAM_START);

	/* cancel any delayed stream shutdown that is pending */
	rtd->pop_wait = 0;
	mutex_unlock(&rtd->pcm_mutex);

	cancel_delayed_work_sync(&rtd->delayed_work);

	return ret;

err:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static int soc_compr_set_params_fe(struct snd_compr_stream *cstream,
					struct snd_compr_params *params)
{
	struct snd_soc_pcm_runtime *fe = cstream->private_data;
	struct snd_pcm_substream *fe_substream = fe->pcm->streams[0].substream;
	struct snd_soc_platform *platform = fe->platform;
	int ret = 0, stream;

	if (cstream->direction == SND_COMPRESS_PLAYBACK)
		stream = SNDRV_PCM_STREAM_PLAYBACK;
	else
		stream = SNDRV_PCM_STREAM_CAPTURE;

	mutex_lock_nested(&fe->card->mutex, SND_SOC_CARD_CLASS_RUNTIME);

	if (platform->driver->compr_ops && platform->driver->compr_ops->set_params) {
		ret = platform->driver->compr_ops->set_params(cstream, params);
		if (ret < 0)
			goto out;
	}

	if (fe->dai_link->compr_ops && fe->dai_link->compr_ops->set_params) {
		ret = fe->dai_link->compr_ops->set_params(cstream);
		if (ret < 0)
			goto out;
	}

	/*
	 * Create an empty hw_params for the BE as the machine driver must
	 * fix this up to match DSP decoder and ASRC configuration.
	 * I.e. machine driver fixup for compressed BE is mandatory.
	 */
	memset(&fe->dpcm[fe_substream->stream].hw_params, 0,
		sizeof(struct snd_pcm_hw_params));

	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_FE;

	ret = dpcm_be_dai_hw_params(fe, stream);
	if (ret < 0)
		goto out;

	ret = dpcm_be_dai_prepare(fe, stream);
	if (ret < 0)
		goto out;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_START);
	else
		dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_START);

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_PREPARE;

out:
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;
	mutex_unlock(&fe->card->mutex);
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
			  char __user *buf, size_t count)
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

static int soc_compr_set_metadata(struct snd_compr_stream *cstream,
				struct snd_compr_metadata *metadata)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	int ret = 0;

	if (platform->driver->compr_ops && platform->driver->compr_ops->set_metadata)
		ret = platform->driver->compr_ops->set_metadata(cstream, metadata);

	return ret;
}

static int soc_compr_get_metadata(struct snd_compr_stream *cstream,
				struct snd_compr_metadata *metadata)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	int ret = 0;

	if (platform->driver->compr_ops && platform->driver->compr_ops->get_metadata)
		ret = platform->driver->compr_ops->get_metadata(cstream, metadata);

	return ret;
}

/* ASoC Compress operations */
static struct snd_compr_ops soc_compr_ops = {
	.open		= soc_compr_open,
	.free		= soc_compr_free,
	.set_params	= soc_compr_set_params,
	.set_metadata   = soc_compr_set_metadata,
	.get_metadata	= soc_compr_get_metadata,
	.get_params	= soc_compr_get_params,
	.trigger	= soc_compr_trigger,
	.pointer	= soc_compr_pointer,
	.ack		= soc_compr_ack,
	.get_caps	= soc_compr_get_caps,
	.get_codec_caps = soc_compr_get_codec_caps
};

/* ASoC Dynamic Compress operations */
static struct snd_compr_ops soc_compr_dyn_ops = {
	.open		= soc_compr_open_fe,
	.free		= soc_compr_free_fe,
	.set_params	= soc_compr_set_params_fe,
	.get_params	= soc_compr_get_params,
	.set_metadata   = soc_compr_set_metadata,
	.get_metadata	= soc_compr_get_metadata,
	.trigger	= soc_compr_trigger_fe,
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
	struct snd_pcm *be_pcm;
	char new_name[64];
	int ret = 0, direction = 0;

	/* check client and interface hw capabilities */
	snprintf(new_name, sizeof(new_name), "%s %s-%d",
			rtd->dai_link->stream_name, codec_dai->name, num);

	if (codec_dai->driver->playback.channels_min)
		direction = SND_COMPRESS_PLAYBACK;
	else if (codec_dai->driver->capture.channels_min)
		direction = SND_COMPRESS_CAPTURE;
	else
		return -EINVAL;

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

	if (rtd->dai_link->dynamic) {
		snprintf(new_name, sizeof(new_name), "(%s)",
			rtd->dai_link->stream_name);

		ret = snd_pcm_new_internal(rtd->card->snd_card, new_name, num,
				1, 0, &be_pcm);
		if (ret < 0) {
			dev_err(rtd->card->dev, "ASoC: can't create compressed for %s\n",
				rtd->dai_link->name);
			goto compr_err;
		}

		rtd->pcm = be_pcm;
		rtd->fe_compr = 1;
		be_pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream->private_data = rtd;
		be_pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream->private_data = rtd;
		memcpy(compr->ops, &soc_compr_dyn_ops, sizeof(soc_compr_dyn_ops));
	} else
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

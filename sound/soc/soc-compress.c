// SPDX-License-Identifier: GPL-2.0+
//
// soc-compress.c  --  ALSA SoC Compress
//
// Copyright (C) 2012 Intel Corp.
//
// Authors: Namarta Kohli <namartax.kohli@intel.com>
//          Ramesh Babu K V <ramesh.babu@linux.intel.com>
//          Vinod Koul <vinod.koul@linux.intel.com>

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
#include <sound/soc-link.h>

static int snd_soc_compr_components_open(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_component *component;
	int ret = 0;
	int i;

	for_each_rtd_components(rtd, i, component) {
		ret = snd_soc_component_module_get_when_open(component, cstream);
		if (ret < 0)
			break;

		ret = snd_soc_component_compr_open(component, cstream);
		if (ret < 0)
			break;
	}

	return ret;
}

static void snd_soc_compr_components_free(struct snd_compr_stream *cstream,
					  int rollback)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_component *component;
	int i;

	for_each_rtd_components(rtd, i, component) {
		snd_soc_component_compr_free(component, cstream, rollback);
		snd_soc_component_module_put_when_close(component, cstream, rollback);
	}
}

static int soc_compr_clean(struct snd_compr_stream *cstream, int rollback)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int stream = cstream->direction; /* SND_COMPRESS_xxx is same as SNDRV_PCM_STREAM_xxx */

	snd_soc_dpcm_mutex_lock(rtd);

	if (!rollback)
		snd_soc_runtime_deactivate(rtd, stream);

	snd_soc_dai_digital_mute(codec_dai, 1, stream);

	if (!snd_soc_dai_active(cpu_dai))
		cpu_dai->rate = 0;

	if (!snd_soc_dai_active(codec_dai))
		codec_dai->rate = 0;

	snd_soc_link_compr_shutdown(cstream, rollback);

	snd_soc_compr_components_free(cstream, rollback);

	snd_soc_dai_compr_shutdown(cpu_dai, cstream, rollback);

	if (!rollback)
		snd_soc_dapm_stream_stop(rtd, stream);

	snd_soc_dpcm_mutex_unlock(rtd);

	snd_soc_pcm_component_pm_runtime_put(rtd, cstream, rollback);

	return 0;
}

static int soc_compr_free(struct snd_compr_stream *cstream)
{
	return soc_compr_clean(cstream, 0);
}

static int soc_compr_open(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int stream = cstream->direction; /* SND_COMPRESS_xxx is same as SNDRV_PCM_STREAM_xxx */
	int ret;

	ret = snd_soc_pcm_component_pm_runtime_get(rtd, cstream);
	if (ret < 0)
		goto err_no_lock;

	snd_soc_dpcm_mutex_lock(rtd);

	ret = snd_soc_dai_compr_startup(cpu_dai, cstream);
	if (ret < 0)
		goto err;

	ret = snd_soc_compr_components_open(cstream);
	if (ret < 0)
		goto err;

	ret = snd_soc_link_compr_startup(cstream);
	if (ret < 0)
		goto err;

	snd_soc_runtime_activate(rtd, stream);
err:
	snd_soc_dpcm_mutex_unlock(rtd);
err_no_lock:
	if (ret < 0)
		soc_compr_clean(cstream, 1);

	return ret;
}

static int soc_compr_open_fe(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *fe = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(fe, 0);
	struct snd_soc_dpcm *dpcm;
	struct snd_soc_dapm_widget_list *list;
	int stream = cstream->direction; /* SND_COMPRESS_xxx is same as SNDRV_PCM_STREAM_xxx */
	int ret;

	snd_soc_card_mutex_lock(fe->card);

	ret = dpcm_path_get(fe, stream, &list);
	if (ret < 0)
		goto be_err;

	snd_soc_dpcm_mutex_lock(fe);

	/* calculate valid and active FE <-> BE dpcms */
	dpcm_process_paths(fe, stream, &list, 1);

	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_FE;

	ret = dpcm_be_dai_startup(fe, stream);
	if (ret < 0) {
		/* clean up all links */
		for_each_dpcm_be(fe, stream, dpcm)
			dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;

		dpcm_be_disconnect(fe, stream);
		goto out;
	}

	ret = snd_soc_dai_compr_startup(cpu_dai, cstream);
	if (ret < 0)
		goto out;

	ret = snd_soc_compr_components_open(cstream);
	if (ret < 0)
		goto open_err;

	ret = snd_soc_link_compr_startup(cstream);
	if (ret < 0)
		goto machine_err;

	dpcm_clear_pending_state(fe, stream);
	dpcm_path_put(&list);

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_OPEN;
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;

	snd_soc_runtime_activate(fe, stream);
	snd_soc_dpcm_mutex_unlock(fe);

	snd_soc_card_mutex_unlock(fe->card);

	return 0;

machine_err:
	snd_soc_compr_components_free(cstream, 1);
open_err:
	snd_soc_dai_compr_shutdown(cpu_dai, cstream, 1);
out:
	dpcm_path_put(&list);
	snd_soc_dpcm_mutex_unlock(fe);
be_err:
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;
	snd_soc_card_mutex_unlock(fe->card);
	return ret;
}

static int soc_compr_free_fe(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *fe = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(fe, 0);
	struct snd_soc_dpcm *dpcm;
	int stream = cstream->direction; /* SND_COMPRESS_xxx is same as SNDRV_PCM_STREAM_xxx */

	snd_soc_card_mutex_lock(fe->card);

	snd_soc_dpcm_mutex_lock(fe);
	snd_soc_runtime_deactivate(fe, stream);

	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_FE;

	dpcm_be_dai_hw_free(fe, stream);

	dpcm_be_dai_shutdown(fe, stream);

	/* mark FE's links ready to prune */
	for_each_dpcm_be(fe, stream, dpcm)
		dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;

	dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_STOP);

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;

	dpcm_be_disconnect(fe, stream);

	snd_soc_dpcm_mutex_unlock(fe);

	snd_soc_link_compr_shutdown(cstream, 0);

	snd_soc_compr_components_free(cstream, 0);

	snd_soc_dai_compr_shutdown(cpu_dai, cstream, 0);

	snd_soc_card_mutex_unlock(fe->card);
	return 0;
}

static int soc_compr_trigger(struct snd_compr_stream *cstream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int stream = cstream->direction; /* SND_COMPRESS_xxx is same as SNDRV_PCM_STREAM_xxx */
	int ret;

	snd_soc_dpcm_mutex_lock(rtd);

	ret = snd_soc_component_compr_trigger(cstream, cmd);
	if (ret < 0)
		goto out;

	ret = snd_soc_dai_compr_trigger(cpu_dai, cstream, cmd);
	if (ret < 0)
		goto out;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_soc_dai_digital_mute(codec_dai, 0, stream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_soc_dai_digital_mute(codec_dai, 1, stream);
		break;
	}

out:
	snd_soc_dpcm_mutex_unlock(rtd);
	return ret;
}

static int soc_compr_trigger_fe(struct snd_compr_stream *cstream, int cmd)
{
	struct snd_soc_pcm_runtime *fe = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(fe, 0);
	int stream = cstream->direction; /* SND_COMPRESS_xxx is same as SNDRV_PCM_STREAM_xxx */
	int ret;

	if (cmd == SND_COMPR_TRIGGER_PARTIAL_DRAIN ||
	    cmd == SND_COMPR_TRIGGER_DRAIN)
		return snd_soc_component_compr_trigger(cstream, cmd);

	snd_soc_card_mutex_lock(fe->card);

	ret = snd_soc_dai_compr_trigger(cpu_dai, cstream, cmd);
	if (ret < 0)
		goto out;

	ret = snd_soc_component_compr_trigger(cstream, cmd);
	if (ret < 0)
		goto out;

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
	snd_soc_card_mutex_unlock(fe->card);
	return ret;
}

static int soc_compr_set_params(struct snd_compr_stream *cstream,
				struct snd_compr_params *params)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int stream = cstream->direction; /* SND_COMPRESS_xxx is same as SNDRV_PCM_STREAM_xxx */
	int ret;

	snd_soc_dpcm_mutex_lock(rtd);

	/*
	 * First we call set_params for the CPU DAI, then the component
	 * driver this should configure the SoC side. If the machine has
	 * compressed ops then we call that as well. The expectation is
	 * that these callbacks will configure everything for this compress
	 * path, like configuring a PCM port for a CODEC.
	 */
	ret = snd_soc_dai_compr_set_params(cpu_dai, cstream, params);
	if (ret < 0)
		goto err;

	ret = snd_soc_component_compr_set_params(cstream, params);
	if (ret < 0)
		goto err;

	ret = snd_soc_link_compr_set_params(cstream);
	if (ret < 0)
		goto err;

	snd_soc_dapm_stream_event(rtd, stream, SND_SOC_DAPM_STREAM_START);

	/* cancel any delayed stream shutdown that is pending */
	rtd->pop_wait = 0;
	snd_soc_dpcm_mutex_unlock(rtd);

	cancel_delayed_work_sync(&rtd->delayed_work);

	return 0;

err:
	snd_soc_dpcm_mutex_unlock(rtd);
	return ret;
}

static int soc_compr_set_params_fe(struct snd_compr_stream *cstream,
				   struct snd_compr_params *params)
{
	struct snd_soc_pcm_runtime *fe = cstream->private_data;
	struct snd_pcm_substream *fe_substream =
		 fe->pcm->streams[cstream->direction].substream;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(fe, 0);
	int stream = cstream->direction; /* SND_COMPRESS_xxx is same as SNDRV_PCM_STREAM_xxx */
	int ret;

	snd_soc_card_mutex_lock(fe->card);

	/*
	 * Create an empty hw_params for the BE as the machine driver must
	 * fix this up to match DSP decoder and ASRC configuration.
	 * I.e. machine driver fixup for compressed BE is mandatory.
	 */
	memset(&fe->dpcm[fe_substream->stream].hw_params, 0,
		sizeof(struct snd_pcm_hw_params));

	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_FE;

	snd_soc_dpcm_mutex_lock(fe);
	ret = dpcm_be_dai_hw_params(fe, stream);
	snd_soc_dpcm_mutex_unlock(fe);
	if (ret < 0)
		goto out;

	snd_soc_dpcm_mutex_lock(fe);
	ret = dpcm_be_dai_prepare(fe, stream);
	snd_soc_dpcm_mutex_unlock(fe);
	if (ret < 0)
		goto out;

	ret = snd_soc_dai_compr_set_params(cpu_dai, cstream, params);
	if (ret < 0)
		goto out;

	ret = snd_soc_component_compr_set_params(cstream, params);
	if (ret < 0)
		goto out;

	ret = snd_soc_link_compr_set_params(cstream);
	if (ret < 0)
		goto out;
	snd_soc_dpcm_mutex_lock(fe);
	dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_START);
	snd_soc_dpcm_mutex_unlock(fe);
	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_PREPARE;

out:
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;
	snd_soc_card_mutex_unlock(fe->card);
	return ret;
}

static int soc_compr_get_params(struct snd_compr_stream *cstream,
				struct snd_codec *params)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret = 0;

	snd_soc_dpcm_mutex_lock(rtd);

	ret = snd_soc_dai_compr_get_params(cpu_dai, cstream, params);
	if (ret < 0)
		goto err;

	ret = snd_soc_component_compr_get_params(cstream, params);
err:
	snd_soc_dpcm_mutex_unlock(rtd);
	return ret;
}

static int soc_compr_ack(struct snd_compr_stream *cstream, size_t bytes)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret;

	snd_soc_dpcm_mutex_lock(rtd);

	ret = snd_soc_dai_compr_ack(cpu_dai, cstream, bytes);
	if (ret < 0)
		goto err;

	ret = snd_soc_component_compr_ack(cstream, bytes);
err:
	snd_soc_dpcm_mutex_unlock(rtd);
	return ret;
}

static int soc_compr_pointer(struct snd_compr_stream *cstream,
			     struct snd_compr_tstamp *tstamp)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	int ret;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);

	snd_soc_dpcm_mutex_lock(rtd);

	ret = snd_soc_dai_compr_pointer(cpu_dai, cstream, tstamp);
	if (ret < 0)
		goto out;

	ret = snd_soc_component_compr_pointer(cstream, tstamp);
out:
	snd_soc_dpcm_mutex_unlock(rtd);
	return ret;
}

static int soc_compr_set_metadata(struct snd_compr_stream *cstream,
				  struct snd_compr_metadata *metadata)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret;

	ret = snd_soc_dai_compr_set_metadata(cpu_dai, cstream, metadata);
	if (ret < 0)
		return ret;

	return snd_soc_component_compr_set_metadata(cstream, metadata);
}

static int soc_compr_get_metadata(struct snd_compr_stream *cstream,
				  struct snd_compr_metadata *metadata)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret;

	ret = snd_soc_dai_compr_get_metadata(cpu_dai, cstream, metadata);
	if (ret < 0)
		return ret;

	return snd_soc_component_compr_get_metadata(cstream, metadata);
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
	.get_caps	= snd_soc_component_compr_get_caps,
	.get_codec_caps = snd_soc_component_compr_get_codec_caps,
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
	.get_caps	= snd_soc_component_compr_get_caps,
	.get_codec_caps = snd_soc_component_compr_get_codec_caps,
};

/**
 * snd_soc_new_compress - create a new compress.
 *
 * @rtd: The runtime for which we will create compress
 * @num: the device index number (zero based - shared with normal PCMs)
 *
 * Return: 0 for success, else error.
 */
int snd_soc_new_compress(struct snd_soc_pcm_runtime *rtd, int num)
{
	struct snd_soc_component *component;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_compr *compr;
	struct snd_pcm *be_pcm;
	char new_name[64];
	int ret = 0, direction = 0;
	int playback = 0, capture = 0;
	int i;

	/*
	 * make sure these are same value,
	 * and then use these as equally
	 */
	BUILD_BUG_ON((int)SNDRV_PCM_STREAM_PLAYBACK != (int)SND_COMPRESS_PLAYBACK);
	BUILD_BUG_ON((int)SNDRV_PCM_STREAM_CAPTURE  != (int)SND_COMPRESS_CAPTURE);

	if (rtd->dai_link->num_cpus > 1 ||
	    rtd->dai_link->num_codecs > 1) {
		dev_err(rtd->card->dev,
			"Compress ASoC: Multi CPU/Codec not supported\n");
		return -EINVAL;
	}

	if (!codec_dai) {
		dev_err(rtd->card->dev, "Missing codec\n");
		return -EINVAL;
	}

	/* check client and interface hw capabilities */
	if (snd_soc_dai_stream_valid(codec_dai, SNDRV_PCM_STREAM_PLAYBACK) &&
	    snd_soc_dai_stream_valid(cpu_dai,   SNDRV_PCM_STREAM_PLAYBACK))
		playback = 1;
	if (snd_soc_dai_stream_valid(codec_dai, SNDRV_PCM_STREAM_CAPTURE) &&
	    snd_soc_dai_stream_valid(cpu_dai,   SNDRV_PCM_STREAM_CAPTURE))
		capture = 1;

	/*
	 * Compress devices are unidirectional so only one of the directions
	 * should be set, check for that (xor)
	 */
	if (playback + capture != 1) {
		dev_err(rtd->card->dev,
			"Compress ASoC: Invalid direction for P %d, C %d\n",
			playback, capture);
		return -EINVAL;
	}

	if (playback)
		direction = SND_COMPRESS_PLAYBACK;
	else
		direction = SND_COMPRESS_CAPTURE;

	compr = devm_kzalloc(rtd->card->dev, sizeof(*compr), GFP_KERNEL);
	if (!compr)
		return -ENOMEM;

	compr->ops = devm_kzalloc(rtd->card->dev, sizeof(soc_compr_ops),
				  GFP_KERNEL);
	if (!compr->ops)
		return -ENOMEM;

	if (rtd->dai_link->dynamic) {
		snprintf(new_name, sizeof(new_name), "(%s)",
			rtd->dai_link->stream_name);

		ret = snd_pcm_new_internal(rtd->card->snd_card, new_name, num,
				rtd->dai_link->dpcm_playback,
				rtd->dai_link->dpcm_capture, &be_pcm);
		if (ret < 0) {
			dev_err(rtd->card->dev,
				"Compress ASoC: can't create compressed for %s: %d\n",
				rtd->dai_link->name, ret);
			return ret;
		}

		/* inherit atomicity from DAI link */
		be_pcm->nonatomic = rtd->dai_link->nonatomic;

		rtd->pcm = be_pcm;
		rtd->fe_compr = 1;
		if (rtd->dai_link->dpcm_playback)
			be_pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream->private_data = rtd;
		if (rtd->dai_link->dpcm_capture)
			be_pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream->private_data = rtd;
		memcpy(compr->ops, &soc_compr_dyn_ops, sizeof(soc_compr_dyn_ops));
	} else {
		snprintf(new_name, sizeof(new_name), "%s %s-%d",
			rtd->dai_link->stream_name, codec_dai->name, num);

		memcpy(compr->ops, &soc_compr_ops, sizeof(soc_compr_ops));
	}

	for_each_rtd_components(rtd, i, component) {
		if (!component->driver->compress_ops ||
		    !component->driver->compress_ops->copy)
			continue;

		compr->ops->copy = snd_soc_component_compr_copy;
		break;
	}

	ret = snd_compress_new(rtd->card->snd_card, num, direction,
				new_name, compr);
	if (ret < 0) {
		component = snd_soc_rtd_to_codec(rtd, 0)->component;
		dev_err(component->dev,
			"Compress ASoC: can't create compress for codec %s: %d\n",
			component->name, ret);
		return ret;
	}

	/* DAPM dai link stream work */
	rtd->close_delayed_work_func = snd_soc_close_delayed_work;

	rtd->compr = compr;
	compr->private_data = rtd;

	dev_dbg(rtd->card->dev, "Compress ASoC: %s <-> %s mapping ok\n",
		codec_dai->name, cpu_dai->name);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_new_compress);

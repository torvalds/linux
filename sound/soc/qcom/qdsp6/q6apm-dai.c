// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/spinlock.h>
#include <sound/pcm.h>
#include <asm/dma.h>
#include <linux/dma-mapping.h>
#include <sound/pcm_params.h>
#include "q6apm.h"

#define DRV_NAME "q6apm-dai"

#define PLAYBACK_MIN_NUM_PERIODS	2
#define PLAYBACK_MAX_NUM_PERIODS	8
#define PLAYBACK_MAX_PERIOD_SIZE	65536
#define PLAYBACK_MIN_PERIOD_SIZE	128
#define CAPTURE_MIN_NUM_PERIODS		2
#define CAPTURE_MAX_NUM_PERIODS		8
#define CAPTURE_MAX_PERIOD_SIZE		65536
#define CAPTURE_MIN_PERIOD_SIZE		6144
#define BUFFER_BYTES_MAX (PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE)
#define BUFFER_BYTES_MIN (PLAYBACK_MIN_NUM_PERIODS * PLAYBACK_MIN_PERIOD_SIZE)
#define COMPR_PLAYBACK_MAX_FRAGMENT_SIZE (128 * 1024)
#define COMPR_PLAYBACK_MAX_NUM_FRAGMENTS (16 * 4)
#define COMPR_PLAYBACK_MIN_FRAGMENT_SIZE (8 * 1024)
#define COMPR_PLAYBACK_MIN_NUM_FRAGMENTS (4)
#define SID_MASK_DEFAULT	0xF

static const struct snd_compr_codec_caps q6apm_compr_caps = {
	.num_descriptors = 1,
	.descriptor[0].max_ch = 2,
	.descriptor[0].sample_rates = {	8000, 11025, 12000, 16000, 22050,
					24000, 32000, 44100, 48000, 88200,
					96000, 176400, 192000 },
	.descriptor[0].num_sample_rates = 13,
	.descriptor[0].bit_rate[0] = 320,
	.descriptor[0].bit_rate[1] = 128,
	.descriptor[0].num_bitrates = 2,
	.descriptor[0].profiles = 0,
	.descriptor[0].modes = SND_AUDIOCHANMODE_MP3_STEREO,
	.descriptor[0].formats = 0,
};

enum stream_state {
	Q6APM_STREAM_IDLE = 0,
	Q6APM_STREAM_STOPPED,
	Q6APM_STREAM_RUNNING,
};

struct q6apm_dai_rtd {
	struct snd_pcm_substream *substream;
	struct snd_compr_stream *cstream;
	struct snd_codec codec;
	struct snd_compr_params codec_param;
	struct snd_dma_buffer dma_buffer;
	phys_addr_t phys;
	unsigned int pcm_size;
	unsigned int pcm_count;
	unsigned int periods;
	unsigned int bytes_sent;
	unsigned int bytes_received;
	unsigned int copied_total;
	uint16_t bits_per_sample;
	snd_pcm_uframes_t queue_ptr;
	bool next_track;
	enum stream_state state;
	struct q6apm_graph *graph;
	spinlock_t lock;
	bool notify_on_drain;
};

struct q6apm_dai_data {
	long long sid;
};

static const struct snd_pcm_hardware q6apm_dai_hardware_capture = {
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_BATCH),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE),
	.rates =                SNDRV_PCM_RATE_8000_48000,
	.rate_min =             8000,
	.rate_max =             48000,
	.channels_min =         2,
	.channels_max =         4,
	.buffer_bytes_max =     CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min =	CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max =     CAPTURE_MAX_PERIOD_SIZE,
	.periods_min =          CAPTURE_MIN_NUM_PERIODS,
	.periods_max =          CAPTURE_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

static const struct snd_pcm_hardware q6apm_dai_hardware_playback = {
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_BATCH),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE),
	.rates =                SNDRV_PCM_RATE_8000_192000,
	.rate_min =             8000,
	.rate_max =             192000,
	.channels_min =         2,
	.channels_max =         8,
	.buffer_bytes_max =     (PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE),
	.period_bytes_min =	PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max =     PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min =          PLAYBACK_MIN_NUM_PERIODS,
	.periods_max =          PLAYBACK_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

static void event_handler(uint32_t opcode, uint32_t token, void *payload, void *priv)
{
	struct q6apm_dai_rtd *prtd = priv;
	struct snd_pcm_substream *substream = prtd->substream;

	switch (opcode) {
	case APM_CLIENT_EVENT_CMD_EOS_DONE:
		prtd->state = Q6APM_STREAM_STOPPED;
		break;
	case APM_CLIENT_EVENT_DATA_WRITE_DONE:
		snd_pcm_period_elapsed(substream);

		break;
	case APM_CLIENT_EVENT_DATA_READ_DONE:
		snd_pcm_period_elapsed(substream);
		if (prtd->state == Q6APM_STREAM_RUNNING)
			q6apm_read(prtd->graph);

		break;
	default:
		break;
	}
}

static void event_handler_compr(uint32_t opcode, uint32_t token,
				void *payload, void *priv)
{
	struct q6apm_dai_rtd *prtd = priv;
	struct snd_compr_stream *substream = prtd->cstream;
	unsigned long flags;
	uint32_t wflags = 0;
	uint64_t avail;
	uint32_t bytes_written, bytes_to_write;
	bool is_last_buffer = false;

	switch (opcode) {
	case APM_CLIENT_EVENT_CMD_EOS_DONE:
		spin_lock_irqsave(&prtd->lock, flags);
		if (prtd->notify_on_drain) {
			snd_compr_drain_notify(prtd->cstream);
			prtd->notify_on_drain = false;
		} else {
			prtd->state = Q6APM_STREAM_STOPPED;
		}
		spin_unlock_irqrestore(&prtd->lock, flags);
		break;
	case APM_CLIENT_EVENT_DATA_WRITE_DONE:
		spin_lock_irqsave(&prtd->lock, flags);
		bytes_written = token >> APM_WRITE_TOKEN_LEN_SHIFT;
		prtd->copied_total += bytes_written;
		snd_compr_fragment_elapsed(substream);

		if (prtd->state != Q6APM_STREAM_RUNNING) {
			spin_unlock_irqrestore(&prtd->lock, flags);
			break;
		}

		avail = prtd->bytes_received - prtd->bytes_sent;

		if (avail > prtd->pcm_count) {
			bytes_to_write = prtd->pcm_count;
		} else {
			if (substream->partial_drain || prtd->notify_on_drain)
				is_last_buffer = true;
			bytes_to_write = avail;
		}

		if (bytes_to_write) {
			if (substream->partial_drain && is_last_buffer)
				wflags |= APM_LAST_BUFFER_FLAG;

			q6apm_write_async(prtd->graph,
						bytes_to_write, 0, 0, wflags);

			prtd->bytes_sent += bytes_to_write;

			if (prtd->notify_on_drain && is_last_buffer)
				audioreach_shared_memory_send_eos(prtd->graph);
		}

		spin_unlock_irqrestore(&prtd->lock, flags);
		break;
	default:
		break;
	}
}

static int q6apm_dai_prepare(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	struct audioreach_module_config cfg;
	struct device *dev = component->dev;
	struct q6apm_dai_data *pdata;
	int ret;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata)
		return -EINVAL;

	if (!prtd || !prtd->graph) {
		dev_err(dev, "%s: private data null or audio client freed\n", __func__);
		return -EINVAL;
	}

	cfg.direction = substream->stream;
	cfg.sample_rate = runtime->rate;
	cfg.num_channels = runtime->channels;
	cfg.bit_width = prtd->bits_per_sample;
	cfg.fmt = SND_AUDIOCODEC_PCM;
	audioreach_set_default_channel_mapping(cfg.channel_map, runtime->channels);

	if (prtd->state) {
		/* clear the previous setup if any  */
		q6apm_graph_stop(prtd->graph);
		q6apm_unmap_memory_regions(prtd->graph, substream->stream);
	}

	prtd->pcm_count = snd_pcm_lib_period_bytes(substream);
	/* rate and channels are sent to audio driver */
	ret = q6apm_graph_media_format_shmem(prtd->graph, &cfg);
	if (ret < 0) {
		dev_err(dev, "%s: q6apm_open_write failed\n", __func__);
		return ret;
	}

	ret = q6apm_graph_media_format_pcm(prtd->graph, &cfg);
	if (ret < 0)
		dev_err(dev, "%s: CMD Format block failed\n", __func__);

	ret = q6apm_map_memory_regions(prtd->graph, substream->stream, prtd->phys,
				       (prtd->pcm_size / prtd->periods), prtd->periods);

	if (ret < 0) {
		dev_err(dev, "Audio Start: Buffer Allocation failed rc = %d\n",	ret);
		return -ENOMEM;
	}

	ret = q6apm_graph_prepare(prtd->graph);
	if (ret) {
		dev_err(dev, "Failed to prepare Graph %d\n", ret);
		return ret;
	}

	ret = q6apm_graph_start(prtd->graph);
	if (ret) {
		dev_err(dev, "Failed to Start Graph %d\n", ret);
		return ret;
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		int i;
		/* Queue the buffers for Capture ONLY after graph is started */
		for (i = 0; i < runtime->periods; i++)
			q6apm_read(prtd->graph);

	}

	/* Now that graph as been prepared and started update the internal state accordingly */
	prtd->state = Q6APM_STREAM_RUNNING;

	return 0;
}

static int q6apm_dai_ack(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	int i, ret = 0, avail_periods;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		avail_periods = (runtime->control->appl_ptr - prtd->queue_ptr)/runtime->period_size;
		for (i = 0; i < avail_periods; i++) {
			ret = q6apm_write_async(prtd->graph, prtd->pcm_count, 0, 0, NO_TIMESTAMP);
			if (ret < 0) {
				dev_err(component->dev, "Error queuing playback buffer %d\n", ret);
				return ret;
			}
			prtd->queue_ptr += runtime->period_size;
		}
	}

	return ret;
}

static int q6apm_dai_trigger(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		/* TODO support be handled via SoftPause Module */
		prtd->state = Q6APM_STREAM_STOPPED;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int q6apm_dai_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_prtd, 0);
	struct device *dev = component->dev;
	struct q6apm_dai_data *pdata;
	struct q6apm_dai_rtd *prtd;
	int graph_id, ret;

	graph_id = cpu_dai->driver->id;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata) {
		dev_err(dev, "Drv data not found ..\n");
		return -EINVAL;
	}

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);
	prtd->substream = substream;
	prtd->graph = q6apm_graph_open(dev, event_handler, prtd, graph_id);
	if (IS_ERR(prtd->graph)) {
		dev_err(dev, "%s: Could not allocate memory\n", __func__);
		ret = PTR_ERR(prtd->graph);
		goto err;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = q6apm_dai_hardware_playback;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		runtime->hw = q6apm_dai_hardware_capture;

	/* Ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(dev, "snd_pcm_hw_constraint_integer failed\n");
		goto err;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
						   BUFFER_BYTES_MIN, BUFFER_BYTES_MAX);
		if (ret < 0) {
			dev_err(dev, "constraint for buffer bytes min max ret = %d\n", ret);
			goto err;
		}
	}

	/* setup 10ms latency to accommodate DSP restrictions */
	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 480);
	if (ret < 0) {
		dev_err(dev, "constraint for period bytes step ret = %d\n", ret);
		goto err;
	}

	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 480);
	if (ret < 0) {
		dev_err(dev, "constraint for buffer bytes step ret = %d\n", ret);
		goto err;
	}

	runtime->private_data = prtd;
	runtime->dma_bytes = BUFFER_BYTES_MAX;
	if (pdata->sid < 0)
		prtd->phys = substream->dma_buffer.addr;
	else
		prtd->phys = substream->dma_buffer.addr | (pdata->sid << 32);

	return 0;
err:
	kfree(prtd);

	return ret;
}

static int q6apm_dai_close(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;

	if (prtd->state) { /* only stop graph that is started */
		q6apm_graph_stop(prtd->graph);
		q6apm_unmap_memory_regions(prtd->graph, substream->stream);
	}

	q6apm_graph_close(prtd->graph);
	prtd->graph = NULL;
	kfree(prtd);
	runtime->private_data = NULL;

	return 0;
}

static snd_pcm_uframes_t q6apm_dai_pointer(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	snd_pcm_uframes_t ptr;

	ptr = q6apm_get_hw_pointer(prtd->graph, substream->stream) * runtime->period_size;
	if (ptr)
		return ptr - 1;

	return 0;
}

static int q6apm_dai_hw_params(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;

	prtd->pcm_size = params_buffer_bytes(params);
	prtd->periods = params_periods(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		prtd->bits_per_sample = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		prtd->bits_per_sample = 24;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int q6apm_dai_pcm_new(struct snd_soc_component *component, struct snd_soc_pcm_runtime *rtd)
{
	int size = BUFFER_BYTES_MAX;

	return snd_pcm_set_fixed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV, component->dev, size);
}

static int q6apm_dai_compr_open(struct snd_soc_component *component,
				struct snd_compr_stream *stream)
{
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd;
	struct q6apm_dai_data *pdata;
	struct device *dev = component->dev;
	int ret, size;
	int graph_id;

	graph_id = cpu_dai->driver->id;
	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata)
		return -EINVAL;

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	prtd->cstream = stream;
	prtd->graph = q6apm_graph_open(dev, event_handler_compr, prtd, graph_id);
	if (IS_ERR(prtd->graph)) {
		ret = PTR_ERR(prtd->graph);
		kfree(prtd);
		return ret;
	}

	runtime->private_data = prtd;
	runtime->dma_bytes = BUFFER_BYTES_MAX;
	size = COMPR_PLAYBACK_MAX_FRAGMENT_SIZE * COMPR_PLAYBACK_MAX_NUM_FRAGMENTS;
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dev, size, &prtd->dma_buffer);
	if (ret)
		return ret;

	if (pdata->sid < 0)
		prtd->phys = prtd->dma_buffer.addr;
	else
		prtd->phys = prtd->dma_buffer.addr | (pdata->sid << 32);

	snd_compr_set_runtime_buffer(stream, &prtd->dma_buffer);
	spin_lock_init(&prtd->lock);

	q6apm_enable_compress_module(dev, prtd->graph, true);
	return 0;
}

static int q6apm_dai_compr_free(struct snd_soc_component *component,
				struct snd_compr_stream *stream)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;

	q6apm_graph_stop(prtd->graph);
	q6apm_unmap_memory_regions(prtd->graph, SNDRV_PCM_STREAM_PLAYBACK);
	q6apm_graph_close(prtd->graph);
	snd_dma_free_pages(&prtd->dma_buffer);
	prtd->graph = NULL;
	kfree(prtd);
	runtime->private_data = NULL;

	return 0;
}

static int q6apm_dai_compr_get_caps(struct snd_soc_component *component,
				    struct snd_compr_stream *stream,
				    struct snd_compr_caps *caps)
{
	caps->direction = SND_COMPRESS_PLAYBACK;
	caps->min_fragment_size = COMPR_PLAYBACK_MIN_FRAGMENT_SIZE;
	caps->max_fragment_size = COMPR_PLAYBACK_MAX_FRAGMENT_SIZE;
	caps->min_fragments = COMPR_PLAYBACK_MIN_NUM_FRAGMENTS;
	caps->max_fragments = COMPR_PLAYBACK_MAX_NUM_FRAGMENTS;
	caps->num_codecs = 3;
	caps->codecs[0] = SND_AUDIOCODEC_MP3;
	caps->codecs[1] = SND_AUDIOCODEC_AAC;
	caps->codecs[2] = SND_AUDIOCODEC_FLAC;

	return 0;
}

static int q6apm_dai_compr_get_codec_caps(struct snd_soc_component *component,
					  struct snd_compr_stream *stream,
					  struct snd_compr_codec_caps *codec)
{
	switch (codec->codec) {
	case SND_AUDIOCODEC_MP3:
		*codec = q6apm_compr_caps;
		break;
	default:
		break;
	}

	return 0;
}

static int q6apm_dai_compr_pointer(struct snd_soc_component *component,
				   struct snd_compr_stream *stream,
				   struct snd_compr_tstamp *tstamp)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	unsigned long flags;

	spin_lock_irqsave(&prtd->lock, flags);
	tstamp->copied_total = prtd->copied_total;
	tstamp->byte_offset = prtd->copied_total % prtd->pcm_size;
	spin_unlock_irqrestore(&prtd->lock, flags);

	return 0;
}

static int q6apm_dai_compr_trigger(struct snd_soc_component *component,
			    struct snd_compr_stream *stream, int cmd)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = q6apm_write_async(prtd->graph, prtd->pcm_count, 0, 0, NO_TIMESTAMP);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	case SND_COMPR_TRIGGER_NEXT_TRACK:
		prtd->next_track = true;
		break;
	case SND_COMPR_TRIGGER_DRAIN:
	case SND_COMPR_TRIGGER_PARTIAL_DRAIN:
		prtd->notify_on_drain = true;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int q6apm_dai_compr_ack(struct snd_soc_component *component, struct snd_compr_stream *stream,
			size_t count)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	unsigned long flags;

	spin_lock_irqsave(&prtd->lock, flags);
	prtd->bytes_received += count;
	spin_unlock_irqrestore(&prtd->lock, flags);

	return count;
}

static int q6apm_dai_compr_set_params(struct snd_soc_component *component,
				      struct snd_compr_stream *stream,
				      struct snd_compr_params *params)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	struct q6apm_dai_data *pdata;
	struct audioreach_module_config cfg;
	struct snd_codec *codec = &params->codec;
	int dir = stream->direction;
	int ret;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata)
		return -EINVAL;

	prtd->periods = runtime->fragments;
	prtd->pcm_count = runtime->fragment_size;
	prtd->pcm_size = runtime->fragments * runtime->fragment_size;
	prtd->bits_per_sample = 16;

	if (prtd->next_track != true) {
		memcpy(&prtd->codec, codec, sizeof(*codec));

		ret = q6apm_set_real_module_id(component->dev, prtd->graph, codec->id);
		if (ret)
			return ret;

		cfg.direction = dir;
		cfg.sample_rate = codec->sample_rate;
		cfg.num_channels = 2;
		cfg.bit_width = prtd->bits_per_sample;
		cfg.fmt = codec->id;
		audioreach_set_default_channel_mapping(cfg.channel_map,
						       cfg.num_channels);
		memcpy(&cfg.codec, codec, sizeof(*codec));

		ret = q6apm_graph_media_format_shmem(prtd->graph, &cfg);
		if (ret < 0)
			return ret;

		ret = q6apm_graph_media_format_pcm(prtd->graph, &cfg);
		if (ret)
			return ret;

		ret = q6apm_map_memory_regions(prtd->graph, SNDRV_PCM_STREAM_PLAYBACK,
					       prtd->phys, (prtd->pcm_size / prtd->periods),
					       prtd->periods);
		if (ret < 0)
			return -ENOMEM;

		ret = q6apm_graph_prepare(prtd->graph);
		if (ret)
			return ret;

		ret = q6apm_graph_start(prtd->graph);
		if (ret)
			return ret;

	} else {
		cfg.direction = dir;
		cfg.sample_rate = codec->sample_rate;
		cfg.num_channels = 2;
		cfg.bit_width = prtd->bits_per_sample;
		cfg.fmt = codec->id;
		memcpy(&cfg.codec, codec, sizeof(*codec));

		ret = audioreach_compr_set_param(prtd->graph,  &cfg);
		if (ret < 0)
			return ret;
	}
	prtd->state = Q6APM_STREAM_RUNNING;

	return 0;
}

static int q6apm_dai_compr_set_metadata(struct snd_soc_component *component,
					struct snd_compr_stream *stream,
					struct snd_compr_metadata *metadata)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	int ret = 0;

	switch (metadata->key) {
	case SNDRV_COMPRESS_ENCODER_PADDING:
		q6apm_remove_trailing_silence(component->dev, prtd->graph,
					      metadata->value[0]);
		break;
	case SNDRV_COMPRESS_ENCODER_DELAY:
		q6apm_remove_initial_silence(component->dev, prtd->graph,
					     metadata->value[0]);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int q6apm_dai_compr_mmap(struct snd_soc_component *component,
				struct snd_compr_stream *stream,
				struct vm_area_struct *vma)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	struct device *dev = component->dev;

	return dma_mmap_coherent(dev, vma, prtd->dma_buffer.area, prtd->dma_buffer.addr,
				 prtd->dma_buffer.bytes);
}

static int q6apm_compr_copy(struct snd_soc_component *component,
			    struct snd_compr_stream *stream, char __user *buf,
			    size_t count)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	void *dstn;
	unsigned long flags;
	size_t copy;
	u32 wflags = 0;
	u32 app_pointer;
	u32 bytes_received;
	uint32_t bytes_to_write;
	int avail, bytes_in_flight = 0;

	bytes_received = prtd->bytes_received;

	/**
	 * Make sure that next track data pointer is aligned at 32 bit boundary
	 * This is a Mandatory requirement from DSP data buffers alignment
	 */
	if (prtd->next_track)
		bytes_received = ALIGN(prtd->bytes_received, prtd->pcm_count);

	app_pointer = bytes_received/prtd->pcm_size;
	app_pointer = bytes_received -  (app_pointer * prtd->pcm_size);
	dstn = prtd->dma_buffer.area + app_pointer;

	if (count < prtd->pcm_size - app_pointer) {
		if (copy_from_user(dstn, buf, count))
			return -EFAULT;
	} else {
		copy = prtd->pcm_size - app_pointer;
		if (copy_from_user(dstn, buf, copy))
			return -EFAULT;
		if (copy_from_user(prtd->dma_buffer.area, buf + copy, count - copy))
			return -EFAULT;
	}

	spin_lock_irqsave(&prtd->lock, flags);
	bytes_in_flight = prtd->bytes_received - prtd->copied_total;

	if (prtd->next_track) {
		prtd->next_track = false;
		prtd->copied_total = ALIGN(prtd->copied_total, prtd->pcm_count);
		prtd->bytes_sent = ALIGN(prtd->bytes_sent, prtd->pcm_count);
	}

	prtd->bytes_received = bytes_received + count;

	/* Kick off the data to dsp if its starving!! */
	if (prtd->state == Q6APM_STREAM_RUNNING && (bytes_in_flight == 0)) {
		bytes_to_write = prtd->pcm_count;
		avail = prtd->bytes_received - prtd->bytes_sent;

		if (avail < prtd->pcm_count)
			bytes_to_write = avail;

		q6apm_write_async(prtd->graph, bytes_to_write, 0, 0, wflags);
		prtd->bytes_sent += bytes_to_write;
	}

	spin_unlock_irqrestore(&prtd->lock, flags);

	return count;
}

static const struct snd_compress_ops q6apm_dai_compress_ops = {
	.open		= q6apm_dai_compr_open,
	.free		= q6apm_dai_compr_free,
	.get_caps	= q6apm_dai_compr_get_caps,
	.get_codec_caps	= q6apm_dai_compr_get_codec_caps,
	.pointer	= q6apm_dai_compr_pointer,
	.trigger	= q6apm_dai_compr_trigger,
	.ack		= q6apm_dai_compr_ack,
	.set_params	= q6apm_dai_compr_set_params,
	.set_metadata	= q6apm_dai_compr_set_metadata,
	.mmap		= q6apm_dai_compr_mmap,
	.copy		= q6apm_compr_copy,
};

static const struct snd_soc_component_driver q6apm_fe_dai_component = {
	.name		= DRV_NAME,
	.open		= q6apm_dai_open,
	.close		= q6apm_dai_close,
	.prepare	= q6apm_dai_prepare,
	.pcm_construct	= q6apm_dai_pcm_new,
	.hw_params	= q6apm_dai_hw_params,
	.pointer	= q6apm_dai_pointer,
	.trigger	= q6apm_dai_trigger,
	.ack		= q6apm_dai_ack,
	.compress_ops	= &q6apm_dai_compress_ops,
	.use_dai_pcm_id = true,
};

static int q6apm_dai_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct q6apm_dai_data *pdata;
	struct of_phandle_args args;
	int rc;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	rc = of_parse_phandle_with_fixed_args(node, "iommus", 1, 0, &args);
	if (rc < 0)
		pdata->sid = -1;
	else
		pdata->sid = args.args[0] & SID_MASK_DEFAULT;

	dev_set_drvdata(dev, pdata);

	return devm_snd_soc_register_component(dev, &q6apm_fe_dai_component, NULL, 0);
}

#ifdef CONFIG_OF
static const struct of_device_id q6apm_dai_device_id[] = {
	{ .compatible = "qcom,q6apm-dais" },
	{},
};
MODULE_DEVICE_TABLE(of, q6apm_dai_device_id);
#endif

static struct platform_driver q6apm_dai_platform_driver = {
	.driver = {
		.name = "q6apm-dai",
		.of_match_table = of_match_ptr(q6apm_dai_device_id),
	},
	.probe = q6apm_dai_probe,
};
module_platform_driver(q6apm_dai_platform_driver);

MODULE_DESCRIPTION("Q6APM dai driver");
MODULE_LICENSE("GPL");

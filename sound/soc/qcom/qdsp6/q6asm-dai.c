// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <linux/spinlock.h>
#include <sound/compress_driver.h>
#include <asm/dma.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <sound/pcm_params.h>
#include "q6asm.h"
#include "q6routing.h"
#include "q6dsp-errno.h"

#define DRV_NAME	"q6asm-fe-dai"

#define PLAYBACK_MIN_NUM_PERIODS    2
#define PLAYBACK_MAX_NUM_PERIODS   8
#define PLAYBACK_MAX_PERIOD_SIZE    65536
#define PLAYBACK_MIN_PERIOD_SIZE    128
#define CAPTURE_MIN_NUM_PERIODS     2
#define CAPTURE_MAX_NUM_PERIODS     8
#define CAPTURE_MAX_PERIOD_SIZE     4096
#define CAPTURE_MIN_PERIOD_SIZE     320
#define SID_MASK_DEFAULT	0xF

/* Default values used if user space does not set */
#define COMPR_PLAYBACK_MIN_FRAGMENT_SIZE (8 * 1024)
#define COMPR_PLAYBACK_MAX_FRAGMENT_SIZE (128 * 1024)
#define COMPR_PLAYBACK_MIN_NUM_FRAGMENTS (4)
#define COMPR_PLAYBACK_MAX_NUM_FRAGMENTS (16 * 4)

#define ALAC_CH_LAYOUT_MONO   ((101 << 16) | 1)
#define ALAC_CH_LAYOUT_STEREO ((101 << 16) | 2)

enum stream_state {
	Q6ASM_STREAM_IDLE = 0,
	Q6ASM_STREAM_STOPPED,
	Q6ASM_STREAM_RUNNING,
};

struct q6asm_dai_rtd {
	struct snd_pcm_substream *substream;
	struct snd_compr_stream *cstream;
	struct snd_codec codec;
	struct snd_dma_buffer dma_buffer;
	spinlock_t lock;
	phys_addr_t phys;
	unsigned int pcm_size;
	unsigned int pcm_count;
	unsigned int pcm_irq_pos;       /* IRQ position */
	unsigned int periods;
	unsigned int bytes_sent;
	unsigned int bytes_received;
	unsigned int copied_total;
	uint16_t bits_per_sample;
	uint16_t source; /* Encoding source bit mask */
	struct audio_client *audio_client;
	uint32_t next_track_stream_id;
	bool next_track;
	uint32_t stream_id;
	uint16_t session_id;
	enum stream_state state;
	uint32_t initial_samples_drop;
	uint32_t trailing_samples_drop;
	bool notify_on_drain;
};

struct q6asm_dai_data {
	struct snd_soc_dai_driver *dais;
	int num_dais;
	long long int sid;
};

static const struct snd_pcm_hardware q6asm_dai_hardware_capture = {
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_BATCH |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE),
	.rates =                SNDRV_PCM_RATE_8000_48000,
	.rate_min =             8000,
	.rate_max =             48000,
	.channels_min =         1,
	.channels_max =         4,
	.buffer_bytes_max =     CAPTURE_MAX_NUM_PERIODS *
				CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min =	CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max =     CAPTURE_MAX_PERIOD_SIZE,
	.periods_min =          CAPTURE_MIN_NUM_PERIODS,
	.periods_max =          CAPTURE_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

static struct snd_pcm_hardware q6asm_dai_hardware_playback = {
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_BATCH |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE),
	.rates =                SNDRV_PCM_RATE_8000_192000,
	.rate_min =             8000,
	.rate_max =             192000,
	.channels_min =         1,
	.channels_max =         8,
	.buffer_bytes_max =     (PLAYBACK_MAX_NUM_PERIODS *
				PLAYBACK_MAX_PERIOD_SIZE),
	.period_bytes_min =	PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max =     PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min =          PLAYBACK_MIN_NUM_PERIODS,
	.periods_max =          PLAYBACK_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

#define Q6ASM_FEDAI_DRIVER(num) { \
		.playback = {						\
			.stream_name = "MultiMedia"#num" Playback",	\
			.rates = (SNDRV_PCM_RATE_8000_192000|		\
					SNDRV_PCM_RATE_KNOT),		\
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |		\
					SNDRV_PCM_FMTBIT_S24_LE),	\
			.channels_min = 1,				\
			.channels_max = 8,				\
			.rate_min =     8000,				\
			.rate_max =	192000,				\
		},							\
		.capture = {						\
			.stream_name = "MultiMedia"#num" Capture",	\
			.rates = (SNDRV_PCM_RATE_8000_48000|		\
					SNDRV_PCM_RATE_KNOT),		\
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |		\
				    SNDRV_PCM_FMTBIT_S24_LE),		\
			.channels_min = 1,				\
			.channels_max = 4,				\
			.rate_min =     8000,				\
			.rate_max =	48000,				\
		},							\
		.name = "MultiMedia"#num,				\
		.id = MSM_FRONTEND_DAI_MULTIMEDIA##num,			\
	}

/* Conventional and unconventional sample rate supported */
static unsigned int supported_sample_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
	88200, 96000, 176400, 192000
};

static struct snd_pcm_hw_constraint_list constraints_sample_rates = {
	.count = ARRAY_SIZE(supported_sample_rates),
	.list = supported_sample_rates,
	.mask = 0,
};

static const struct snd_compr_codec_caps q6asm_compr_caps = {
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

static void event_handler(uint32_t opcode, uint32_t token,
			  void *payload, void *priv)
{
	struct q6asm_dai_rtd *prtd = priv;
	struct snd_pcm_substream *substream = prtd->substream;

	switch (opcode) {
	case ASM_CLIENT_EVENT_CMD_RUN_DONE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			q6asm_write_async(prtd->audio_client, prtd->stream_id,
				   prtd->pcm_count, 0, 0, 0);
		break;
	case ASM_CLIENT_EVENT_CMD_EOS_DONE:
		prtd->state = Q6ASM_STREAM_STOPPED;
		break;
	case ASM_CLIENT_EVENT_DATA_WRITE_DONE: {
		prtd->pcm_irq_pos += prtd->pcm_count;
		snd_pcm_period_elapsed(substream);
		if (prtd->state == Q6ASM_STREAM_RUNNING)
			q6asm_write_async(prtd->audio_client, prtd->stream_id,
					   prtd->pcm_count, 0, 0, 0);

		break;
		}
	case ASM_CLIENT_EVENT_DATA_READ_DONE:
		prtd->pcm_irq_pos += prtd->pcm_count;
		snd_pcm_period_elapsed(substream);
		if (prtd->state == Q6ASM_STREAM_RUNNING)
			q6asm_read(prtd->audio_client, prtd->stream_id);

		break;
	default:
		break;
	}
}

static int q6asm_dai_prepare(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = asoc_substream_to_rtd(substream);
	struct q6asm_dai_rtd *prtd = runtime->private_data;
	struct q6asm_dai_data *pdata;
	struct device *dev = component->dev;
	int ret, i;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata)
		return -EINVAL;

	if (!prtd || !prtd->audio_client) {
		dev_err(dev, "%s: private data null or audio client freed\n",
			__func__);
		return -EINVAL;
	}

	prtd->pcm_count = snd_pcm_lib_period_bytes(substream);
	prtd->pcm_irq_pos = 0;
	/* rate and channels are sent to audio driver */
	if (prtd->state) {
		/* clear the previous setup if any  */
		q6asm_cmd(prtd->audio_client, prtd->stream_id, CMD_CLOSE);
		q6asm_unmap_memory_regions(substream->stream,
					   prtd->audio_client);
		q6routing_stream_close(soc_prtd->dai_link->id,
					 substream->stream);
	}

	ret = q6asm_map_memory_regions(substream->stream, prtd->audio_client,
				       prtd->phys,
				       (prtd->pcm_size / prtd->periods),
				       prtd->periods);

	if (ret < 0) {
		dev_err(dev, "Audio Start: Buffer Allocation failed rc = %d\n",
							ret);
		return -ENOMEM;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = q6asm_open_write(prtd->audio_client, prtd->stream_id,
				       FORMAT_LINEAR_PCM,
				       0, prtd->bits_per_sample, false);
	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		ret = q6asm_open_read(prtd->audio_client, prtd->stream_id,
				      FORMAT_LINEAR_PCM,
				      prtd->bits_per_sample);
	}

	if (ret < 0) {
		dev_err(dev, "%s: q6asm_open_write failed\n", __func__);
		goto open_err;
	}

	prtd->session_id = q6asm_get_session_id(prtd->audio_client);
	ret = q6routing_stream_open(soc_prtd->dai_link->id, LEGACY_PCM_MODE,
			      prtd->session_id, substream->stream);
	if (ret) {
		dev_err(dev, "%s: stream reg failed ret:%d\n", __func__, ret);
		goto routing_err;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = q6asm_media_format_block_multi_ch_pcm(
				prtd->audio_client, prtd->stream_id,
				runtime->rate, runtime->channels, NULL,
				prtd->bits_per_sample);
	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		ret = q6asm_enc_cfg_blk_pcm_format_support(prtd->audio_client,
							   prtd->stream_id,
							   runtime->rate,
							   runtime->channels,
							   prtd->bits_per_sample);

		/* Queue the buffers */
		for (i = 0; i < runtime->periods; i++)
			q6asm_read(prtd->audio_client, prtd->stream_id);

	}
	if (ret < 0)
		dev_info(dev, "%s: CMD Format block failed\n", __func__);
	else
		prtd->state = Q6ASM_STREAM_RUNNING;

	return ret;

routing_err:
	q6asm_cmd(prtd->audio_client, prtd->stream_id,  CMD_CLOSE);
open_err:
	q6asm_unmap_memory_regions(substream->stream, prtd->audio_client);
	q6asm_audio_client_free(prtd->audio_client);
	prtd->audio_client = NULL;

	return ret;
}

static int q6asm_dai_trigger(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = q6asm_run_nowait(prtd->audio_client, prtd->stream_id,
				       0, 0, 0);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		prtd->state = Q6ASM_STREAM_STOPPED;
		ret = q6asm_cmd_nowait(prtd->audio_client, prtd->stream_id,
				       CMD_EOS);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = q6asm_cmd_nowait(prtd->audio_client, prtd->stream_id,
				       CMD_PAUSE);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int q6asm_dai_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(soc_prtd, 0);
	struct q6asm_dai_rtd *prtd;
	struct q6asm_dai_data *pdata;
	struct device *dev = component->dev;
	int ret = 0;
	int stream_id;

	stream_id = cpu_dai->driver->id;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata) {
		dev_err(dev, "Drv data not found ..\n");
		return -EINVAL;
	}

	prtd = kzalloc(sizeof(struct q6asm_dai_rtd), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	prtd->substream = substream;
	prtd->audio_client = q6asm_audio_client_alloc(dev,
				(q6asm_cb)event_handler, prtd, stream_id,
				LEGACY_PCM_MODE);
	if (IS_ERR(prtd->audio_client)) {
		dev_info(dev, "%s: Could not allocate memory\n", __func__);
		ret = PTR_ERR(prtd->audio_client);
		kfree(prtd);
		return ret;
	}

	/* DSP expects stream id from 1 */
	prtd->stream_id = 1;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = q6asm_dai_hardware_playback;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		runtime->hw = q6asm_dai_hardware_capture;

	ret = snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				&constraints_sample_rates);
	if (ret < 0)
		dev_info(dev, "snd_pcm_hw_constraint_list failed\n");
	/* Ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		dev_info(dev, "snd_pcm_hw_constraint_integer failed\n");

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = snd_pcm_hw_constraint_minmax(runtime,
			SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
			PLAYBACK_MIN_NUM_PERIODS * PLAYBACK_MIN_PERIOD_SIZE,
			PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE);
		if (ret < 0) {
			dev_err(dev, "constraint for buffer bytes min max ret = %d\n",
				ret);
		}
	}

	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (ret < 0) {
		dev_err(dev, "constraint for period bytes step ret = %d\n",
								ret);
	}
	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (ret < 0) {
		dev_err(dev, "constraint for buffer bytes step ret = %d\n",
								ret);
	}

	runtime->private_data = prtd;

	snd_soc_set_runtime_hwparams(substream, &q6asm_dai_hardware_playback);

	runtime->dma_bytes = q6asm_dai_hardware_playback.buffer_bytes_max;


	if (pdata->sid < 0)
		prtd->phys = substream->dma_buffer.addr;
	else
		prtd->phys = substream->dma_buffer.addr | (pdata->sid << 32);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int q6asm_dai_close(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = asoc_substream_to_rtd(substream);
	struct q6asm_dai_rtd *prtd = runtime->private_data;

	if (prtd->audio_client) {
		if (prtd->state)
			q6asm_cmd(prtd->audio_client, prtd->stream_id,
				  CMD_CLOSE);

		q6asm_unmap_memory_regions(substream->stream,
					   prtd->audio_client);
		q6asm_audio_client_free(prtd->audio_client);
		prtd->audio_client = NULL;
	}
	q6routing_stream_close(soc_prtd->dai_link->id,
						substream->stream);
	kfree(prtd);
	return 0;
}

static snd_pcm_uframes_t q6asm_dai_pointer(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream)
{

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;

	if (prtd->pcm_irq_pos >= prtd->pcm_size)
		prtd->pcm_irq_pos = 0;

	return bytes_to_frames(runtime, (prtd->pcm_irq_pos));
}

static int q6asm_dai_mmap(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream,
			  struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = component->dev;

	return dma_mmap_coherent(dev, vma,
			runtime->dma_area, runtime->dma_addr,
			runtime->dma_bytes);
}

static int q6asm_dai_hw_params(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;

	prtd->pcm_size = params_buffer_bytes(params);
	prtd->periods = params_periods(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		prtd->bits_per_sample = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		prtd->bits_per_sample = 24;
		break;
	}

	return 0;
}

static void compress_event_handler(uint32_t opcode, uint32_t token,
				   void *payload, void *priv)
{
	struct q6asm_dai_rtd *prtd = priv;
	struct snd_compr_stream *substream = prtd->cstream;
	unsigned long flags;
	u32 wflags = 0;
	uint64_t avail;
	uint32_t bytes_written, bytes_to_write;
	bool is_last_buffer = false;

	switch (opcode) {
	case ASM_CLIENT_EVENT_CMD_RUN_DONE:
		spin_lock_irqsave(&prtd->lock, flags);
		if (!prtd->bytes_sent) {
			q6asm_stream_remove_initial_silence(prtd->audio_client,
						    prtd->stream_id,
						    prtd->initial_samples_drop);

			q6asm_write_async(prtd->audio_client, prtd->stream_id,
					  prtd->pcm_count, 0, 0, 0);
			prtd->bytes_sent += prtd->pcm_count;
		}

		spin_unlock_irqrestore(&prtd->lock, flags);
		break;

	case ASM_CLIENT_EVENT_CMD_EOS_DONE:
		spin_lock_irqsave(&prtd->lock, flags);
		if (prtd->notify_on_drain) {
			if (substream->partial_drain) {
				/*
				 * Close old stream and make it stale, switch
				 * the active stream now!
				 */
				q6asm_cmd_nowait(prtd->audio_client,
						 prtd->stream_id,
						 CMD_CLOSE);
				/*
				 * vaild stream ids start from 1, So we are
				 * toggling this between 1 and 2.
				 */
				prtd->stream_id = (prtd->stream_id == 1 ? 2 : 1);
			}

			snd_compr_drain_notify(prtd->cstream);
			prtd->notify_on_drain = false;

		} else {
			prtd->state = Q6ASM_STREAM_STOPPED;
		}
		spin_unlock_irqrestore(&prtd->lock, flags);
		break;

	case ASM_CLIENT_EVENT_DATA_WRITE_DONE:
		spin_lock_irqsave(&prtd->lock, flags);

		bytes_written = token >> ASM_WRITE_TOKEN_LEN_SHIFT;
		prtd->copied_total += bytes_written;
		snd_compr_fragment_elapsed(substream);

		if (prtd->state != Q6ASM_STREAM_RUNNING) {
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
			if (substream->partial_drain && is_last_buffer) {
				wflags |= ASM_LAST_BUFFER_FLAG;
				q6asm_stream_remove_trailing_silence(prtd->audio_client,
						     prtd->stream_id,
						     prtd->trailing_samples_drop);
			}

			q6asm_write_async(prtd->audio_client, prtd->stream_id,
					  bytes_to_write, 0, 0, wflags);

			prtd->bytes_sent += bytes_to_write;
		}

		if (prtd->notify_on_drain && is_last_buffer)
			q6asm_cmd_nowait(prtd->audio_client,
					 prtd->stream_id, CMD_EOS);

		spin_unlock_irqrestore(&prtd->lock, flags);
		break;

	default:
		break;
	}
}

static int q6asm_dai_compr_open(struct snd_soc_component *component,
				struct snd_compr_stream *stream)
{
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	struct snd_compr_runtime *runtime = stream->runtime;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct q6asm_dai_data *pdata;
	struct device *dev = component->dev;
	struct q6asm_dai_rtd *prtd;
	int stream_id, size, ret;

	stream_id = cpu_dai->driver->id;
	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata) {
		dev_err(dev, "Drv data not found ..\n");
		return -EINVAL;
	}

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (!prtd)
		return -ENOMEM;

	/* DSP expects stream id from 1 */
	prtd->stream_id = 1;

	prtd->cstream = stream;
	prtd->audio_client = q6asm_audio_client_alloc(dev,
					(q6asm_cb)compress_event_handler,
					prtd, stream_id, LEGACY_PCM_MODE);
	if (IS_ERR(prtd->audio_client)) {
		dev_err(dev, "Could not allocate memory\n");
		ret = PTR_ERR(prtd->audio_client);
		goto free_prtd;
	}

	size = COMPR_PLAYBACK_MAX_FRAGMENT_SIZE *
			COMPR_PLAYBACK_MAX_NUM_FRAGMENTS;
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dev, size,
				  &prtd->dma_buffer);
	if (ret) {
		dev_err(dev, "Cannot allocate buffer(s)\n");
		goto free_client;
	}

	if (pdata->sid < 0)
		prtd->phys = prtd->dma_buffer.addr;
	else
		prtd->phys = prtd->dma_buffer.addr | (pdata->sid << 32);

	snd_compr_set_runtime_buffer(stream, &prtd->dma_buffer);
	spin_lock_init(&prtd->lock);
	runtime->private_data = prtd;

	return 0;

free_client:
	q6asm_audio_client_free(prtd->audio_client);
free_prtd:
	kfree(prtd);

	return ret;
}

static int q6asm_dai_compr_free(struct snd_soc_component *component,
				struct snd_compr_stream *stream)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = stream->private_data;

	if (prtd->audio_client) {
		if (prtd->state) {
			q6asm_cmd(prtd->audio_client, prtd->stream_id,
				  CMD_CLOSE);
			if (prtd->next_track_stream_id) {
				q6asm_cmd(prtd->audio_client,
					  prtd->next_track_stream_id,
					  CMD_CLOSE);
			}
		}

		snd_dma_free_pages(&prtd->dma_buffer);
		q6asm_unmap_memory_regions(stream->direction,
					   prtd->audio_client);
		q6asm_audio_client_free(prtd->audio_client);
		prtd->audio_client = NULL;
	}
	q6routing_stream_close(rtd->dai_link->id, stream->direction);
	kfree(prtd);

	return 0;
}

static int __q6asm_dai_compr_set_codec_params(struct snd_soc_component *component,
					      struct snd_compr_stream *stream,
					      struct snd_codec *codec,
					      int stream_id)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;
	struct q6asm_flac_cfg flac_cfg;
	struct q6asm_wma_cfg wma_cfg;
	struct q6asm_alac_cfg alac_cfg;
	struct q6asm_ape_cfg ape_cfg;
	unsigned int wma_v9 = 0;
	struct device *dev = component->dev;
	int ret;
	union snd_codec_options *codec_options;
	struct snd_dec_flac *flac;
	struct snd_dec_wma *wma;
	struct snd_dec_alac *alac;
	struct snd_dec_ape *ape;

	codec_options = &(prtd->codec.options);

	memcpy(&prtd->codec, codec, sizeof(*codec));

	switch (codec->id) {
	case SND_AUDIOCODEC_FLAC:

		memset(&flac_cfg, 0x0, sizeof(struct q6asm_flac_cfg));
		flac = &codec_options->flac_d;

		flac_cfg.ch_cfg = codec->ch_in;
		flac_cfg.sample_rate = codec->sample_rate;
		flac_cfg.stream_info_present = 1;
		flac_cfg.sample_size = flac->sample_size;
		flac_cfg.min_blk_size = flac->min_blk_size;
		flac_cfg.max_blk_size = flac->max_blk_size;
		flac_cfg.max_frame_size = flac->max_frame_size;
		flac_cfg.min_frame_size = flac->min_frame_size;

		ret = q6asm_stream_media_format_block_flac(prtd->audio_client,
							   stream_id,
							   &flac_cfg);
		if (ret < 0) {
			dev_err(dev, "FLAC CMD Format block failed:%d\n", ret);
			return -EIO;
		}
		break;

	case SND_AUDIOCODEC_WMA:
		wma = &codec_options->wma_d;

		memset(&wma_cfg, 0x0, sizeof(struct q6asm_wma_cfg));

		wma_cfg.sample_rate =  codec->sample_rate;
		wma_cfg.num_channels = codec->ch_in;
		wma_cfg.bytes_per_sec = codec->bit_rate / 8;
		wma_cfg.block_align = codec->align;
		wma_cfg.bits_per_sample = prtd->bits_per_sample;
		wma_cfg.enc_options = wma->encoder_option;
		wma_cfg.adv_enc_options = wma->adv_encoder_option;
		wma_cfg.adv_enc_options2 = wma->adv_encoder_option2;

		if (wma_cfg.num_channels == 1)
			wma_cfg.channel_mask = 4; /* Mono Center */
		else if (wma_cfg.num_channels == 2)
			wma_cfg.channel_mask = 3; /* Stereo FL/FR */
		else
			return -EINVAL;

		/* check the codec profile */
		switch (codec->profile) {
		case SND_AUDIOPROFILE_WMA9:
			wma_cfg.fmtag = 0x161;
			wma_v9 = 1;
			break;

		case SND_AUDIOPROFILE_WMA10:
			wma_cfg.fmtag = 0x166;
			break;

		case SND_AUDIOPROFILE_WMA9_PRO:
			wma_cfg.fmtag = 0x162;
			break;

		case SND_AUDIOPROFILE_WMA9_LOSSLESS:
			wma_cfg.fmtag = 0x163;
			break;

		case SND_AUDIOPROFILE_WMA10_LOSSLESS:
			wma_cfg.fmtag = 0x167;
			break;

		default:
			dev_err(dev, "Unknown WMA profile:%x\n",
				codec->profile);
			return -EIO;
		}

		if (wma_v9)
			ret = q6asm_stream_media_format_block_wma_v9(
					prtd->audio_client, stream_id,
					&wma_cfg);
		else
			ret = q6asm_stream_media_format_block_wma_v10(
					prtd->audio_client, stream_id,
					&wma_cfg);
		if (ret < 0) {
			dev_err(dev, "WMA9 CMD failed:%d\n", ret);
			return -EIO;
		}
		break;

	case SND_AUDIOCODEC_ALAC:
		memset(&alac_cfg, 0x0, sizeof(alac_cfg));
		alac = &codec_options->alac_d;

		alac_cfg.sample_rate = codec->sample_rate;
		alac_cfg.avg_bit_rate = codec->bit_rate;
		alac_cfg.bit_depth = prtd->bits_per_sample;
		alac_cfg.num_channels = codec->ch_in;

		alac_cfg.frame_length = alac->frame_length;
		alac_cfg.pb = alac->pb;
		alac_cfg.mb = alac->mb;
		alac_cfg.kb = alac->kb;
		alac_cfg.max_run = alac->max_run;
		alac_cfg.compatible_version = alac->compatible_version;
		alac_cfg.max_frame_bytes = alac->max_frame_bytes;

		switch (codec->ch_in) {
		case 1:
			alac_cfg.channel_layout_tag = ALAC_CH_LAYOUT_MONO;
			break;
		case 2:
			alac_cfg.channel_layout_tag = ALAC_CH_LAYOUT_STEREO;
			break;
		}
		ret = q6asm_stream_media_format_block_alac(prtd->audio_client,
							   stream_id,
							   &alac_cfg);
		if (ret < 0) {
			dev_err(dev, "ALAC CMD Format block failed:%d\n", ret);
			return -EIO;
		}
		break;

	case SND_AUDIOCODEC_APE:
		memset(&ape_cfg, 0x0, sizeof(ape_cfg));
		ape = &codec_options->ape_d;

		ape_cfg.sample_rate = codec->sample_rate;
		ape_cfg.num_channels = codec->ch_in;
		ape_cfg.bits_per_sample = prtd->bits_per_sample;

		ape_cfg.compatible_version = ape->compatible_version;
		ape_cfg.compression_level = ape->compression_level;
		ape_cfg.format_flags = ape->format_flags;
		ape_cfg.blocks_per_frame = ape->blocks_per_frame;
		ape_cfg.final_frame_blocks = ape->final_frame_blocks;
		ape_cfg.total_frames = ape->total_frames;
		ape_cfg.seek_table_present = ape->seek_table_present;

		ret = q6asm_stream_media_format_block_ape(prtd->audio_client,
							  stream_id,
							  &ape_cfg);
		if (ret < 0) {
			dev_err(dev, "APE CMD Format block failed:%d\n", ret);
			return -EIO;
		}
		break;

	default:
		break;
	}

	return 0;
}

static int q6asm_dai_compr_set_params(struct snd_soc_component *component,
				      struct snd_compr_stream *stream,
				      struct snd_compr_params *params)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	int dir = stream->direction;
	struct q6asm_dai_data *pdata;
	struct device *dev = component->dev;
	int ret;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata)
		return -EINVAL;

	if (!prtd || !prtd->audio_client) {
		dev_err(dev, "private data null or audio client freed\n");
		return -EINVAL;
	}

	prtd->periods = runtime->fragments;
	prtd->pcm_count = runtime->fragment_size;
	prtd->pcm_size = runtime->fragments * runtime->fragment_size;
	prtd->bits_per_sample = 16;

	if (dir == SND_COMPRESS_PLAYBACK) {
		ret = q6asm_open_write(prtd->audio_client, prtd->stream_id, params->codec.id,
				params->codec.profile, prtd->bits_per_sample,
				true);

		if (ret < 0) {
			dev_err(dev, "q6asm_open_write failed\n");
			q6asm_audio_client_free(prtd->audio_client);
			prtd->audio_client = NULL;
			return ret;
		}
	}

	prtd->session_id = q6asm_get_session_id(prtd->audio_client);
	ret = q6routing_stream_open(rtd->dai_link->id, LEGACY_PCM_MODE,
			      prtd->session_id, dir);
	if (ret) {
		dev_err(dev, "Stream reg failed ret:%d\n", ret);
		return ret;
	}

	ret = __q6asm_dai_compr_set_codec_params(component, stream,
						 &params->codec,
						 prtd->stream_id);
	if (ret) {
		dev_err(dev, "codec param setup failed ret:%d\n", ret);
		return ret;
	}

	ret = q6asm_map_memory_regions(dir, prtd->audio_client, prtd->phys,
				       (prtd->pcm_size / prtd->periods),
				       prtd->periods);

	if (ret < 0) {
		dev_err(dev, "Buffer Mapping failed ret:%d\n", ret);
		return -ENOMEM;
	}

	prtd->state = Q6ASM_STREAM_RUNNING;

	return 0;
}

static int q6asm_dai_compr_set_metadata(struct snd_soc_component *component,
					struct snd_compr_stream *stream,
					struct snd_compr_metadata *metadata)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;
	int ret = 0;

	switch (metadata->key) {
	case SNDRV_COMPRESS_ENCODER_PADDING:
		prtd->trailing_samples_drop = metadata->value[0];
		break;
	case SNDRV_COMPRESS_ENCODER_DELAY:
		prtd->initial_samples_drop = metadata->value[0];
		if (prtd->next_track_stream_id) {
			ret = q6asm_open_write(prtd->audio_client,
					       prtd->next_track_stream_id,
					       prtd->codec.id,
					       prtd->codec.profile,
					       prtd->bits_per_sample,
				       true);
			if (ret < 0) {
				dev_err(component->dev, "q6asm_open_write failed\n");
				return ret;
			}
			ret = __q6asm_dai_compr_set_codec_params(component, stream,
								 &prtd->codec,
								 prtd->next_track_stream_id);
			if (ret < 0) {
				dev_err(component->dev, "q6asm_open_write failed\n");
				return ret;
			}

			ret = q6asm_stream_remove_initial_silence(prtd->audio_client,
						    prtd->next_track_stream_id,
						    prtd->initial_samples_drop);
			prtd->next_track_stream_id = 0;

		}

		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int q6asm_dai_compr_trigger(struct snd_soc_component *component,
				   struct snd_compr_stream *stream, int cmd)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = q6asm_run_nowait(prtd->audio_client, prtd->stream_id,
				       0, 0, 0);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		prtd->state = Q6ASM_STREAM_STOPPED;
		ret = q6asm_cmd_nowait(prtd->audio_client, prtd->stream_id,
				       CMD_EOS);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = q6asm_cmd_nowait(prtd->audio_client, prtd->stream_id,
				       CMD_PAUSE);
		break;
	case SND_COMPR_TRIGGER_NEXT_TRACK:
		prtd->next_track = true;
		prtd->next_track_stream_id = (prtd->stream_id == 1 ? 2 : 1);
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

static int q6asm_dai_compr_pointer(struct snd_soc_component *component,
				   struct snd_compr_stream *stream,
				   struct snd_compr_tstamp *tstamp)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;
	unsigned long flags;

	spin_lock_irqsave(&prtd->lock, flags);

	tstamp->copied_total = prtd->copied_total;
	tstamp->byte_offset = prtd->copied_total % prtd->pcm_size;

	spin_unlock_irqrestore(&prtd->lock, flags);

	return 0;
}

static int q6asm_compr_copy(struct snd_soc_component *component,
			    struct snd_compr_stream *stream, char __user *buf,
			    size_t count)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;
	unsigned long flags;
	u32 wflags = 0;
	int avail, bytes_in_flight = 0;
	void *dstn;
	size_t copy;
	u32 app_pointer;
	u32 bytes_received;

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
		if (copy_from_user(prtd->dma_buffer.area, buf + copy,
				   count - copy))
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
	if (prtd->state == Q6ASM_STREAM_RUNNING && (bytes_in_flight == 0)) {
		uint32_t bytes_to_write = prtd->pcm_count;

		avail = prtd->bytes_received - prtd->bytes_sent;

		if (avail < prtd->pcm_count)
			bytes_to_write = avail;

		q6asm_write_async(prtd->audio_client, prtd->stream_id,
				  bytes_to_write, 0, 0, wflags);
		prtd->bytes_sent += bytes_to_write;
	}

	spin_unlock_irqrestore(&prtd->lock, flags);

	return count;
}

static int q6asm_dai_compr_mmap(struct snd_soc_component *component,
				struct snd_compr_stream *stream,
				struct vm_area_struct *vma)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6asm_dai_rtd *prtd = runtime->private_data;
	struct device *dev = component->dev;

	return dma_mmap_coherent(dev, vma,
			prtd->dma_buffer.area, prtd->dma_buffer.addr,
			prtd->dma_buffer.bytes);
}

static int q6asm_dai_compr_get_caps(struct snd_soc_component *component,
				    struct snd_compr_stream *stream,
				    struct snd_compr_caps *caps)
{
	caps->direction = SND_COMPRESS_PLAYBACK;
	caps->min_fragment_size = COMPR_PLAYBACK_MIN_FRAGMENT_SIZE;
	caps->max_fragment_size = COMPR_PLAYBACK_MAX_FRAGMENT_SIZE;
	caps->min_fragments = COMPR_PLAYBACK_MIN_NUM_FRAGMENTS;
	caps->max_fragments = COMPR_PLAYBACK_MAX_NUM_FRAGMENTS;
	caps->num_codecs = 5;
	caps->codecs[0] = SND_AUDIOCODEC_MP3;
	caps->codecs[1] = SND_AUDIOCODEC_FLAC;
	caps->codecs[2] = SND_AUDIOCODEC_WMA;
	caps->codecs[3] = SND_AUDIOCODEC_ALAC;
	caps->codecs[4] = SND_AUDIOCODEC_APE;

	return 0;
}

static int q6asm_dai_compr_get_codec_caps(struct snd_soc_component *component,
					  struct snd_compr_stream *stream,
					  struct snd_compr_codec_caps *codec)
{
	switch (codec->codec) {
	case SND_AUDIOCODEC_MP3:
		*codec = q6asm_compr_caps;
		break;
	default:
		break;
	}

	return 0;
}

static struct snd_compress_ops q6asm_dai_compress_ops = {
	.open		= q6asm_dai_compr_open,
	.free		= q6asm_dai_compr_free,
	.set_params	= q6asm_dai_compr_set_params,
	.set_metadata	= q6asm_dai_compr_set_metadata,
	.pointer	= q6asm_dai_compr_pointer,
	.trigger	= q6asm_dai_compr_trigger,
	.get_caps	= q6asm_dai_compr_get_caps,
	.get_codec_caps	= q6asm_dai_compr_get_codec_caps,
	.mmap		= q6asm_dai_compr_mmap,
	.copy		= q6asm_compr_copy,
};

static int q6asm_dai_pcm_new(struct snd_soc_component *component,
			     struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm_substream *psubstream, *csubstream;
	struct snd_pcm *pcm = rtd->pcm;
	struct device *dev;
	int size, ret;

	dev = component->dev;
	size = q6asm_dai_hardware_playback.buffer_bytes_max;
	psubstream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	if (psubstream) {
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dev, size,
					  &psubstream->dma_buffer);
		if (ret) {
			dev_err(dev, "Cannot allocate buffer(s)\n");
			return ret;
		}
	}

	csubstream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	if (csubstream) {
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dev, size,
					  &csubstream->dma_buffer);
		if (ret) {
			dev_err(dev, "Cannot allocate buffer(s)\n");
			if (psubstream)
				snd_dma_free_pages(&psubstream->dma_buffer);
			return ret;
		}
	}

	return 0;
}

static void q6asm_dai_pcm_free(struct snd_soc_component *component,
			       struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	int i;

	for (i = 0; i < ARRAY_SIZE(pcm->streams); i++) {
		substream = pcm->streams[i].substream;
		if (substream) {
			snd_dma_free_pages(&substream->dma_buffer);
			substream->dma_buffer.area = NULL;
			substream->dma_buffer.addr = 0;
		}
	}
}

static const struct snd_soc_dapm_widget q6asm_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("MM_DL1", "MultiMedia1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL2", "MultiMedia2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL3", "MultiMedia3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL4", "MultiMedia4 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL5", "MultiMedia5 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL6", "MultiMedia6 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL7", "MultiMedia7 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL8", "MultiMedia8 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL1", "MultiMedia1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL2", "MultiMedia2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL3", "MultiMedia3 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL4", "MultiMedia4 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL5", "MultiMedia5 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL6", "MultiMedia6 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL7", "MultiMedia7 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL8", "MultiMedia8 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_component_driver q6asm_fe_dai_component = {
	.name		= DRV_NAME,
	.open		= q6asm_dai_open,
	.hw_params	= q6asm_dai_hw_params,
	.close		= q6asm_dai_close,
	.prepare	= q6asm_dai_prepare,
	.trigger	= q6asm_dai_trigger,
	.pointer	= q6asm_dai_pointer,
	.mmap		= q6asm_dai_mmap,
	.pcm_construct	= q6asm_dai_pcm_new,
	.pcm_destruct	= q6asm_dai_pcm_free,
	.compress_ops	= &q6asm_dai_compress_ops,
	.dapm_widgets	= q6asm_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(q6asm_dapm_widgets),
};

static struct snd_soc_dai_driver q6asm_fe_dais_template[] = {
	Q6ASM_FEDAI_DRIVER(1),
	Q6ASM_FEDAI_DRIVER(2),
	Q6ASM_FEDAI_DRIVER(3),
	Q6ASM_FEDAI_DRIVER(4),
	Q6ASM_FEDAI_DRIVER(5),
	Q6ASM_FEDAI_DRIVER(6),
	Q6ASM_FEDAI_DRIVER(7),
	Q6ASM_FEDAI_DRIVER(8),
};

static int of_q6asm_parse_dai_data(struct device *dev,
				    struct q6asm_dai_data *pdata)
{
	struct snd_soc_dai_driver *dai_drv;
	struct snd_soc_pcm_stream empty_stream;
	struct device_node *node;
	int ret, id, dir, idx = 0;


	pdata->num_dais = of_get_child_count(dev->of_node);
	if (!pdata->num_dais) {
		dev_err(dev, "No dais found in DT\n");
		return -EINVAL;
	}

	pdata->dais = devm_kcalloc(dev, pdata->num_dais, sizeof(*dai_drv),
				   GFP_KERNEL);
	if (!pdata->dais)
		return -ENOMEM;

	memset(&empty_stream, 0, sizeof(empty_stream));

	for_each_child_of_node(dev->of_node, node) {
		ret = of_property_read_u32(node, "reg", &id);
		if (ret || id >= MAX_SESSIONS || id < 0) {
			dev_err(dev, "valid dai id not found:%d\n", ret);
			continue;
		}

		dai_drv = &pdata->dais[idx++];
		*dai_drv = q6asm_fe_dais_template[id];

		ret = of_property_read_u32(node, "direction", &dir);
		if (ret)
			continue;

		if (dir == Q6ASM_DAI_RX)
			dai_drv->capture = empty_stream;
		else if (dir == Q6ASM_DAI_TX)
			dai_drv->playback = empty_stream;

		if (of_property_read_bool(node, "is-compress-dai"))
			dai_drv->compress_new = snd_soc_new_compress;
	}

	return 0;
}

static int q6asm_dai_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct of_phandle_args args;
	struct q6asm_dai_data *pdata;
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

	rc = of_q6asm_parse_dai_data(dev, pdata);
	if (rc)
		return rc;

	return devm_snd_soc_register_component(dev, &q6asm_fe_dai_component,
					       pdata->dais, pdata->num_dais);
}

#ifdef CONFIG_OF
static const struct of_device_id q6asm_dai_device_id[] = {
	{ .compatible = "qcom,q6asm-dais" },
	{},
};
MODULE_DEVICE_TABLE(of, q6asm_dai_device_id);
#endif

static struct platform_driver q6asm_dai_platform_driver = {
	.driver = {
		.name = "q6asm-dai",
		.of_match_table = of_match_ptr(q6asm_dai_device_id),
	},
	.probe = q6asm_dai_probe,
};
module_platform_driver(q6asm_dai_platform_driver);

MODULE_DESCRIPTION("Q6ASM dai driver");
MODULE_LICENSE("GPL v2");

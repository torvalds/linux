// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <asm/dma.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <sound/pcm_params.h>
#include "q6apm.h"

#define DRV_NAME "q6apm-dai"

#define PLAYBACK_MIN_NUM_PERIODS	2
#define PLAYBACK_MAX_NUM_PERIODS	8
#define PLAYBACK_MAX_PERIOD_SIZE	65536
#define PLAYBACK_MIN_PERIOD_SIZE	128
#define CAPTURE_MIN_NUM_PERIODS		2
#define CAPTURE_MAX_NUM_PERIODS		8
#define CAPTURE_MAX_PERIOD_SIZE		4096
#define CAPTURE_MIN_PERIOD_SIZE		320
#define BUFFER_BYTES_MAX (PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE)
#define BUFFER_BYTES_MIN (PLAYBACK_MIN_NUM_PERIODS * PLAYBACK_MIN_PERIOD_SIZE)
#define SID_MASK_DEFAULT	0xF

enum stream_state {
	Q6APM_STREAM_IDLE = 0,
	Q6APM_STREAM_STOPPED,
	Q6APM_STREAM_RUNNING,
};

struct q6apm_dai_rtd {
	struct snd_pcm_substream *substream;
	struct snd_compr_stream *cstream;
	struct snd_compr_params codec_param;
	struct snd_dma_buffer dma_buffer;
	phys_addr_t phys;
	unsigned int pcm_size;
	unsigned int pcm_count;
	unsigned int pos;       /* Buffer position */
	unsigned int periods;
	unsigned int bytes_sent;
	unsigned int bytes_received;
	unsigned int copied_total;
	uint16_t bits_per_sample;
	uint16_t source; /* Encoding source bit mask */
	uint16_t session_id;
	enum stream_state state;
	struct q6apm_graph *graph;
};

struct q6apm_dai_data {
	long long sid;
};

static struct snd_pcm_hardware q6apm_dai_hardware_capture = {
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
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

static struct snd_pcm_hardware q6apm_dai_hardware_playback = {
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
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

static void event_handler(uint32_t opcode, uint32_t token, uint32_t *payload, void *priv)
{
	struct q6apm_dai_rtd *prtd = priv;
	struct snd_pcm_substream *substream = prtd->substream;

	switch (opcode) {
	case APM_CLIENT_EVENT_CMD_EOS_DONE:
		prtd->state = Q6APM_STREAM_STOPPED;
		break;
	case APM_CLIENT_EVENT_DATA_WRITE_DONE:
		prtd->pos += prtd->pcm_count;
		snd_pcm_period_elapsed(substream);
		if (prtd->state == Q6APM_STREAM_RUNNING)
			q6apm_write_async(prtd->graph, prtd->pcm_count, 0, 0, 0);

		break;
	case APM_CLIENT_EVENT_DATA_READ_DONE:
		prtd->pos += prtd->pcm_count;
		snd_pcm_period_elapsed(substream);
		if (prtd->state == Q6APM_STREAM_RUNNING)
			q6apm_read(prtd->graph);

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

	prtd->pcm_count = snd_pcm_lib_period_bytes(substream);
	prtd->pos = 0;
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
		 /* start writing buffers for playback only as we already queued capture buffers */
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			ret = q6apm_write_async(prtd->graph, prtd->pcm_count, 0, 0, 0);
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
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(soc_prtd, 0);
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

	prtd->substream = substream;
	prtd->graph = q6apm_graph_open(dev, (q6apm_cb)event_handler, prtd, graph_id);
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

	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (ret < 0) {
		dev_err(dev, "constraint for period bytes step ret = %d\n", ret);
		goto err;
	}

	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
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

	if (prtd->pos == prtd->pcm_size)
		prtd->pos = 0;

	return bytes_to_frames(runtime, prtd->pos);
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

static const struct snd_soc_component_driver q6apm_fe_dai_component = {
	.name		= DRV_NAME,
	.open		= q6apm_dai_open,
	.close		= q6apm_dai_close,
	.prepare	= q6apm_dai_prepare,
	.pcm_construct	= q6apm_dai_pcm_new,
	.hw_params	= q6apm_dai_hw_params,
	.pointer	= q6apm_dai_pointer,
	.trigger	= q6apm_dai_trigger,
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

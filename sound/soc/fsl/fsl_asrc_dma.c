/*
 * Freescale ASRC ALSA SoC Platform (DMA) driver
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * Author: Nicolin Chen <nicoleotsuka@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_data/dma-imx.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "fsl_asrc.h"

#define FSL_ASRC_DMABUF_SIZE	(256 * 1024)

static struct snd_pcm_hardware snd_imx_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.buffer_bytes_max = FSL_ASRC_DMABUF_SIZE,
	.period_bytes_min = 128,
	.period_bytes_max = 65535, /* Limited by SDMA engine */
	.periods_min = 2,
	.periods_max = 255,
	.fifo_size = 0,
};

static bool filter(struct dma_chan *chan, void *param)
{
	if (!imx_dma_is_general_purpose(chan))
		return false;

	chan->private = param;

	return true;
}

static void fsl_asrc_dma_complete(void *arg)
{
	struct snd_pcm_substream *substream = arg;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;

	pair->pos += snd_pcm_lib_period_bytes(substream);
	if (pair->pos >= snd_pcm_lib_buffer_bytes(substream))
		pair->pos = 0;

	snd_pcm_period_elapsed(substream);
}

static int fsl_asrc_dma_prepare_and_submit(struct snd_pcm_substream *substream)
{
	u8 dir = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? OUT : IN;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;
	struct device *dev = rtd->platform->dev;
	unsigned long flags = DMA_CTRL_ACK;

	/* Prepare and submit Front-End DMA channel */
	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	pair->pos = 0;
	pair->desc[!dir] = dmaengine_prep_dma_cyclic(
			pair->dma_chan[!dir], runtime->dma_addr,
			snd_pcm_lib_buffer_bytes(substream),
			snd_pcm_lib_period_bytes(substream),
			dir == OUT ? DMA_TO_DEVICE : DMA_FROM_DEVICE, flags);
	if (!pair->desc[!dir]) {
		dev_err(dev, "failed to prepare slave DMA for Front-End\n");
		return -ENOMEM;
	}

	pair->desc[!dir]->callback = fsl_asrc_dma_complete;
	pair->desc[!dir]->callback_param = substream;

	dmaengine_submit(pair->desc[!dir]);

	/* Prepare and submit Back-End DMA channel */
	pair->desc[dir] = dmaengine_prep_dma_cyclic(
			pair->dma_chan[dir], 0xffff, 64, 64, DMA_DEV_TO_DEV, 0);
	if (!pair->desc[dir]) {
		dev_err(dev, "failed to prepare slave DMA for Back-End\n");
		return -ENOMEM;
	}

	dmaengine_submit(pair->desc[dir]);

	return 0;
}

static int fsl_asrc_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = fsl_asrc_dma_prepare_and_submit(substream);
		if (ret)
			return ret;
		dma_async_issue_pending(pair->dma_chan[IN]);
		dma_async_issue_pending(pair->dma_chan[OUT]);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_terminate_all(pair->dma_chan[OUT]);
		dmaengine_terminate_all(pair->dma_chan[IN]);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fsl_asrc_dma_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	enum dma_slave_buswidth buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_dmaengine_dai_dma_data *dma_params_fe = NULL;
	struct snd_dmaengine_dai_dma_data *dma_params_be = NULL;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;
	struct fsl_asrc *asrc_priv = pair->asrc_priv;
	struct dma_slave_config config_fe, config_be;
	enum asrc_pair_index index = pair->index;
	struct device *dev = rtd->platform->dev;
	int stream = substream->stream;
	struct imx_dma_data *tmp_data;
	struct snd_soc_dpcm *dpcm;
	struct dma_chan *tmp_chan;
	struct device *dev_be;
	u8 dir = tx ? OUT : IN;
	dma_cap_mask_t mask;
	int ret;

	/* Fetch the Back-End dma_data from DPCM */
	list_for_each_entry(dpcm, &rtd->dpcm[stream].be_clients, list_be) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *substream_be;
		struct snd_soc_dai *dai = be->cpu_dai;

		if (dpcm->fe != rtd)
			continue;

		substream_be = snd_soc_dpcm_get_substream(be, stream);
		dma_params_be = snd_soc_dai_get_dma_data(dai, substream_be);
		dev_be = dai->dev;
		break;
	}

	if (!dma_params_be) {
		dev_err(dev, "failed to get the substream of Back-End\n");
		return -EINVAL;
	}

	/* Override dma_data of the Front-End and config its dmaengine */
	dma_params_fe = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	dma_params_fe->addr = asrc_priv->paddr + REG_ASRDx(!dir, index);
	dma_params_fe->maxburst = dma_params_be->maxburst;

	pair->dma_chan[!dir] = fsl_asrc_get_dma_channel(pair, !dir);
	if (!pair->dma_chan[!dir]) {
		dev_err(dev, "failed to request DMA channel\n");
		return -EINVAL;
	}

	memset(&config_fe, 0, sizeof(config_fe));
	ret = snd_dmaengine_pcm_prepare_slave_config(substream, params, &config_fe);
	if (ret) {
		dev_err(dev, "failed to prepare DMA config for Front-End\n");
		return ret;
	}

	ret = dmaengine_slave_config(pair->dma_chan[!dir], &config_fe);
	if (ret) {
		dev_err(dev, "failed to config DMA channel for Front-End\n");
		return ret;
	}

	/* Request and config DMA channel for Back-End */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_CYCLIC, mask);

	/* Get DMA request of Back-End */
	tmp_chan = dma_request_slave_channel(dev_be, tx ? "tx" : "rx");
	tmp_data = tmp_chan->private;
	pair->dma_data.dma_request = tmp_data->dma_request;
	dma_release_channel(tmp_chan);

	/* Get DMA request of Front-End */
	tmp_chan = fsl_asrc_get_dma_channel(pair, dir);
	tmp_data = tmp_chan->private;
	pair->dma_data.dma_request2 = tmp_data->dma_request;
	pair->dma_data.peripheral_type = tmp_data->peripheral_type;
	pair->dma_data.priority = tmp_data->priority;
	dma_release_channel(tmp_chan);

	pair->dma_chan[dir] = dma_request_channel(mask, filter, &pair->dma_data);
	if (!pair->dma_chan[dir]) {
		dev_err(dev, "failed to request DMA channel for Back-End\n");
		return -EINVAL;
	}

	if (asrc_priv->asrc_width == 16)
		buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
	else
		buswidth = DMA_SLAVE_BUSWIDTH_4_BYTES;

	config_be.direction = DMA_DEV_TO_DEV;
	config_be.src_addr_width = buswidth;
	config_be.src_maxburst = dma_params_be->maxburst;
	config_be.dst_addr_width = buswidth;
	config_be.dst_maxburst = dma_params_be->maxburst;

	if (tx) {
		config_be.src_addr = asrc_priv->paddr + REG_ASRDO(index);
		config_be.dst_addr = dma_params_be->addr;
	} else {
		config_be.dst_addr = asrc_priv->paddr + REG_ASRDI(index);
		config_be.src_addr = dma_params_be->addr;
	}

	ret = dmaengine_slave_config(pair->dma_chan[dir], &config_be);
	if (ret) {
		dev_err(dev, "failed to config DMA channel for Back-End\n");
		return ret;
	}

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int fsl_asrc_dma_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;

	snd_pcm_set_runtime_buffer(substream, NULL);

	if (pair->dma_chan[IN])
		dma_release_channel(pair->dma_chan[IN]);

	if (pair->dma_chan[OUT])
		dma_release_channel(pair->dma_chan[OUT]);

	pair->dma_chan[IN] = NULL;
	pair->dma_chan[OUT] = NULL;

	return 0;
}

static int fsl_asrc_dma_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = rtd->platform->dev;
	struct fsl_asrc *asrc_priv = dev_get_drvdata(dev);
	struct fsl_asrc_pair *pair;

	pair = kzalloc(sizeof(struct fsl_asrc_pair), GFP_KERNEL);
	if (!pair) {
		dev_err(dev, "failed to allocate pair\n");
		return -ENOMEM;
	}

	pair->asrc_priv = asrc_priv;

	runtime->private_data = pair;

	snd_pcm_hw_constraint_integer(substream->runtime,
				      SNDRV_PCM_HW_PARAM_PERIODS);
	snd_soc_set_runtime_hwparams(substream, &snd_imx_hardware);

	return 0;
}

static int fsl_asrc_dma_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;
	struct fsl_asrc *asrc_priv;

	if (!pair)
		return 0;

	asrc_priv = pair->asrc_priv;

	if (asrc_priv->pair[pair->index] == pair)
		asrc_priv->pair[pair->index] = NULL;

	kfree(pair);

	return 0;
}

static snd_pcm_uframes_t fsl_asrc_dma_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;

	return bytes_to_frames(substream->runtime, pair->pos);
}

static struct snd_pcm_ops fsl_asrc_dma_pcm_ops = {
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= fsl_asrc_dma_hw_params,
	.hw_free	= fsl_asrc_dma_hw_free,
	.trigger	= fsl_asrc_dma_trigger,
	.open		= fsl_asrc_dma_startup,
	.close		= fsl_asrc_dma_shutdown,
	.pointer	= fsl_asrc_dma_pcm_pointer,
};

static int fsl_asrc_dma_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm_substream *substream;
	struct snd_pcm *pcm = rtd->pcm;
	int ret, i;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(card->dev, "failed to set DMA mask\n");
		return ret;
	}

	for (i = SNDRV_PCM_STREAM_PLAYBACK; i <= SNDRV_PCM_STREAM_LAST; i++) {
		substream = pcm->streams[i].substream;
		if (!substream)
			continue;

		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, pcm->card->dev,
				FSL_ASRC_DMABUF_SIZE, &substream->dma_buffer);
		if (ret) {
			dev_err(card->dev, "failed to allocate DMA buffer\n");
			goto err;
		}
	}

	return 0;

err:
	if (--i == 0 && pcm->streams[i].substream)
		snd_dma_free_pages(&pcm->streams[i].substream->dma_buffer);

	return ret;
}

static void fsl_asrc_dma_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	int i;

	for (i = SNDRV_PCM_STREAM_PLAYBACK; i <= SNDRV_PCM_STREAM_LAST; i++) {
		substream = pcm->streams[i].substream;
		if (!substream)
			continue;

		snd_dma_free_pages(&substream->dma_buffer);
		substream->dma_buffer.area = NULL;
		substream->dma_buffer.addr = 0;
	}
}

struct snd_soc_platform_driver fsl_asrc_platform = {
	.ops		= &fsl_asrc_dma_pcm_ops,
	.pcm_new	= fsl_asrc_dma_pcm_new,
	.pcm_free	= fsl_asrc_dma_pcm_free,
};
EXPORT_SYMBOL_GPL(fsl_asrc_platform);

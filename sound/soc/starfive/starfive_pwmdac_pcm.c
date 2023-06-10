// SPDX-License-Identifier: GPL-2.0
/*
 * PWMDAC PCM driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */
  
#include <linux/io.h>
#include <linux/rcupdate.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "pwmdac.h"

#define BUFFER_BYTES_MAX	(3 * 2 * 8 * PERIOD_BYTES_MIN)
#define PERIOD_BYTES_MIN	4096
#define PERIODS_MIN		2

static unsigned int sf_pwmdac_pcm_tx_8(struct sf_pwmdac_dev *dev, 
		struct snd_pcm_runtime *runtime, unsigned int tx_ptr, 
		bool *period_elapsed) 
{ 
	const u8 (*p)[2] = (void *)runtime->dma_area; 
	unsigned int period_pos = tx_ptr % runtime->period_size; 
	int i; 
	u32 basedat = 0;
	
	for (i = 0; i < dev->fifo_th; i++) { 
		basedat = (p[tx_ptr][0]<<8)|(p[tx_ptr][1] << 24);
		iowrite32(basedat,dev->pwmdac_base + PWMDAC_WDATA); 
		period_pos++; 
		if (++tx_ptr >= runtime->buffer_size) 
			tx_ptr = 0; 
	} 
	
	*period_elapsed = period_pos >= runtime->period_size; 
	
	return tx_ptr; 
}
		

static unsigned int sf_pwmdac_pcm_tx_16(struct sf_pwmdac_dev *dev, 
		struct snd_pcm_runtime *runtime, unsigned int tx_ptr, 
		bool *period_elapsed) 
{ 
	const u16 (*p)[2] = (void *)runtime->dma_area; 
	unsigned int period_pos = tx_ptr % runtime->period_size; 
	int i; 
	u32 basedat = 0;
	
	for (i = 0; i < dev->fifo_th; i++) { 
		basedat = (p[tx_ptr][0])|(p[tx_ptr][1] << 16);
		iowrite32(basedat,dev->pwmdac_base + PWMDAC_WDATA); 
		period_pos++; 
		if (++tx_ptr >= runtime->buffer_size) 
			tx_ptr = 0; 
	} 
	
	*period_elapsed = period_pos >= runtime->period_size; 
	return tx_ptr; 
}
		

static const struct snd_pcm_hardware sf_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.rates = SNDRV_PCM_RATE_16000,
	.rate_min = 16000,
	.rate_max = 16000,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = BUFFER_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = BUFFER_BYTES_MAX / PERIODS_MIN,
	.periods_min = PERIODS_MIN,
	.periods_max = BUFFER_BYTES_MAX / PERIOD_BYTES_MIN,
	.fifo_size = 2,
};

static void sf_pcm_transfer(struct sf_pwmdac_dev *dev, bool push)
{
	struct snd_pcm_substream *substream;
	bool active, period_elapsed;

	rcu_read_lock();
	if (push)
		substream = rcu_dereference(dev->tx_substream);

	active = substream && snd_pcm_running(substream);
	if (active) {
		unsigned int ptr;
		unsigned int new_ptr;

		if (push) {
			ptr = READ_ONCE(dev->tx_ptr);
			new_ptr = dev->tx_fn(dev, substream->runtime, ptr,
					&period_elapsed);
			cmpxchg(&dev->tx_ptr, ptr, new_ptr);
		}
		
		if (period_elapsed)
			snd_pcm_period_elapsed(substream);
	}
	rcu_read_unlock();
}

void sf_pwmdac_pcm_push_tx(struct sf_pwmdac_dev *dev)
{
	sf_pcm_transfer(dev, true);
}


static int sf_pcm_open(struct snd_soc_component *component,
		    struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sf_pwmdac_dev *dev = snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));

	snd_soc_set_runtime_hwparams(substream, &sf_pcm_hardware);
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	runtime->private_data = dev;

	return 0;
}


static int sf_pcm_close(struct snd_soc_component *component,
		    struct snd_pcm_substream *substream)
{
	synchronize_rcu();
	return 0;
}

static int sf_pcm_hw_params(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sf_pwmdac_dev *dev = runtime->private_data;
	int ret;


	switch (params_channels(hw_params)) {
	case 2:
		break;
	default:
		dev_err(dev->dev, "invalid channels number\n");
		return -EINVAL;
	}

	switch (params_format(hw_params)) {
	case SNDRV_PCM_FORMAT_U8:
	case SNDRV_PCM_FORMAT_S8:
		dev->tx_fn = sf_pwmdac_pcm_tx_8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		dev->tx_fn = sf_pwmdac_pcm_tx_16;
		break;
	default:
		dev_err(dev->dev, "invalid format\n");
		return -EINVAL;
	}

		return 0;
}


static int sf_pcm_trigger(struct snd_soc_component *component,
		    struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sf_pwmdac_dev *dev = runtime->private_data;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			WRITE_ONCE(dev->tx_ptr, 0);
			rcu_assign_pointer(dev->tx_substream, substream);
		} 
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			rcu_assign_pointer(dev->tx_substream, NULL);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static snd_pcm_uframes_t sf_pcm_pointer(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sf_pwmdac_dev *dev = runtime->private_data;
	snd_pcm_uframes_t pos;
	
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		pos = READ_ONCE(dev->tx_ptr);

	return pos < runtime->buffer_size ? pos : 0;
}

static int sf_pcm_new(struct snd_soc_component *component,
		      struct snd_soc_pcm_runtime *rtd)
{
	size_t size = sf_pcm_hardware.buffer_bytes_max;
	
	snd_pcm_set_managed_buffer_all(rtd->pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			NULL, size, size);
	return 0;
}

static const struct snd_soc_component_driver dw_pcm_component = {
	.open		= sf_pcm_open,
	.close		= sf_pcm_close,
	.hw_params	= sf_pcm_hw_params,
	.trigger	= sf_pcm_trigger,
	.pointer	= sf_pcm_pointer,
	.pcm_construct	= sf_pcm_new,
};

int sf_pwmdac_pcm_register(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev, &dw_pcm_component,
					       NULL, 0);
}

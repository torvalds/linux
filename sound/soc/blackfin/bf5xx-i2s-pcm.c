/*
 * File:         sound/soc/blackfin/bf5xx-i2s-pcm.c
 * Author:       Cliff Cai <Cliff.Cai@analog.com>
 *
 * Created:      Tue June 06 2008
 * Description:  DMA driver for i2s codec
 *
 * Modified:
 *               Copyright 2008 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/dma.h>

#include "bf5xx-sport.h"
#include "bf5xx-i2s-pcm.h"

static void bf5xx_dma_irq(void *data)
{
	struct snd_pcm_substream *pcm = data;
	snd_pcm_period_elapsed(pcm);
}

static const struct snd_pcm_hardware bf5xx_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_MMAP_VALID |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.period_bytes_min	= 32,
	.period_bytes_max	= 0x10000,
	.periods_min		= 1,
	.periods_max		= PAGE_SIZE/32,
	.buffer_bytes_max	= 0x20000, /* 128 kbytes */
	.fifo_size		= 16,
};

static int bf5xx_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int buffer_size = params_buffer_bytes(params);
	struct bf5xx_i2s_pcm_data *dma_data;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	if (dma_data->tdm_mode)
		buffer_size = buffer_size / params_channels(params) * 8;

	return snd_pcm_lib_malloc_pages(substream, buffer_size);
}

static int bf5xx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);

	return 0;
}

static int bf5xx_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sport_device *sport = runtime->private_data;
	int period_bytes = frames_to_bytes(runtime, runtime->period_size);
	struct bf5xx_i2s_pcm_data *dma_data;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	if (dma_data->tdm_mode)
		period_bytes = period_bytes / runtime->channels * 8;

	pr_debug("%s enter\n", __func__);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		sport_set_tx_callback(sport, bf5xx_dma_irq, substream);
		sport_config_tx_dma(sport, runtime->dma_area,
			runtime->periods, period_bytes);
	} else {
		sport_set_rx_callback(sport, bf5xx_dma_irq, substream);
		sport_config_rx_dma(sport, runtime->dma_area,
			runtime->periods, period_bytes);
	}

	return 0;
}

static int bf5xx_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sport_device *sport = runtime->private_data;
	int ret = 0;

	pr_debug("%s enter\n", __func__);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sport_tx_start(sport);
		else
			sport_rx_start(sport);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sport_tx_stop(sport);
		else
			sport_rx_stop(sport);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static snd_pcm_uframes_t bf5xx_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sport_device *sport = runtime->private_data;
	unsigned int diff;
	snd_pcm_uframes_t frames;
	struct bf5xx_i2s_pcm_data *dma_data;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	pr_debug("%s enter\n", __func__);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		diff = sport_curr_offset_tx(sport);
	} else {
		diff = sport_curr_offset_rx(sport);
	}

	/*
	 * TX at least can report one frame beyond the end of the
	 * buffer if we hit the wraparound case - clamp to within the
	 * buffer as the ALSA APIs require.
	 */
	if (diff == snd_pcm_lib_buffer_bytes(substream))
		diff = 0;

	frames = bytes_to_frames(substream->runtime, diff);
	if (dma_data->tdm_mode)
		frames = frames * runtime->channels / 8;

	return frames;
}

static int bf5xx_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(cpu_dai);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	struct bf5xx_i2s_pcm_data *dma_data;
	int ret;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	pr_debug("%s enter\n", __func__);

	snd_soc_set_runtime_hwparams(substream, &bf5xx_pcm_hardware);
	if (dma_data->tdm_mode)
		runtime->hw.buffer_bytes_max /= 4;
	else
		runtime->hw.info |= SNDRV_PCM_INFO_MMAP;

	ret = snd_pcm_hw_constraint_integer(runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		goto out;

	if (sport_handle != NULL) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sport_handle->tx_buf = buf->area;
		else
			sport_handle->rx_buf = buf->area;

		runtime->private_data = sport_handle;
	} else {
		pr_err("sport_handle is NULL\n");
		return -1;
	}
	return 0;

 out:
	return ret;
}

static int bf5xx_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	size_t size = vma->vm_end - vma->vm_start;
	vma->vm_start = (unsigned long)runtime->dma_area;
	vma->vm_end = vma->vm_start + size;
	vma->vm_flags |=  VM_SHARED;

	return 0 ;
}

static int bf5xx_pcm_copy(struct snd_pcm_substream *substream,
			  int channel, unsigned long pos,
			  void *buf, unsigned long count)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int sample_size = runtime->sample_bits / 8;
	struct bf5xx_i2s_pcm_data *dma_data;
	unsigned int i;
	void *src, *dst;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	if (dma_data->tdm_mode) {
		pos = bytes_to_frames(runtime, pos);
		count = bytes_to_frames(runtime, count);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			src = buf;
			dst = runtime->dma_area;
			dst += pos * sample_size * 8;

			while (count--) {
				for (i = 0; i < runtime->channels; i++) {
					memcpy(dst + dma_data->map[i] *
						sample_size, src, sample_size);
					src += sample_size;
				}
				dst += 8 * sample_size;
			}
		} else {
			src = runtime->dma_area;
			src += pos * sample_size * 8;
			dst = buf;

			while (count--) {
				for (i = 0; i < runtime->channels; i++) {
					memcpy(dst, src + dma_data->map[i] *
						sample_size, sample_size);
					dst += sample_size;
				}
				src += 8 * sample_size;
			}
		}
	} else {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			src = buf;
			dst = runtime->dma_area;
			dst += pos;
		} else {
			src = runtime->dma_area;
			src += pos;
			dst = buf;
		}

		memcpy(dst, src, count);
	}

	return 0;
}

static int bf5xx_pcm_copy_user(struct snd_pcm_substream *substream,
			       int channel, unsigned long pos,
			       void __user *buf, unsigned long count)
{
	return bf5xx_pcm_copy(substream, channel, pos, (void *)buf, count);
}

static int bf5xx_pcm_silence(struct snd_pcm_substream *substream,
			     int channel, unsigned long pos,
			     unsigned long count)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int sample_size = runtime->sample_bits / 8;
	void *buf = runtime->dma_area;
	struct bf5xx_i2s_pcm_data *dma_data;
	unsigned int offset, samples;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	if (dma_data->tdm_mode) {
		offset = bytes_to_frames(runtime, pos) * 8 * sample_size;
		samples = bytes_to_frames(runtime, count) * 8;
	} else {
		offset = pos;
		samples = bytes_to_samples(runtime, count);
	}

	snd_pcm_format_set_silence(runtime->format, buf + offset, samples);

	return 0;
}

static const struct snd_pcm_ops bf5xx_pcm_i2s_ops = {
	.open		= bf5xx_pcm_open,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= bf5xx_pcm_hw_params,
	.hw_free	= bf5xx_pcm_hw_free,
	.prepare	= bf5xx_pcm_prepare,
	.trigger	= bf5xx_pcm_trigger,
	.pointer	= bf5xx_pcm_pointer,
	.mmap		= bf5xx_pcm_mmap,
	.copy_user	= bf5xx_pcm_copy_user,
	.copy_kernel	= bf5xx_pcm_copy,
	.fill_silence	= bf5xx_pcm_silence,
};

static int bf5xx_pcm_i2s_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	size_t size = bf5xx_pcm_hardware.buffer_bytes_max;
	int ret;

	pr_debug("%s enter\n", __func__);
	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	return snd_pcm_lib_preallocate_pages_for_all(rtd->pcm,
				SNDRV_DMA_TYPE_DEV, card->dev, size, size);
}

static struct snd_soc_component_driver bf5xx_i2s_soc_component = {
	.ops		= &bf5xx_pcm_i2s_ops,
	.pcm_new	= bf5xx_pcm_i2s_new,
};

static int bfin_i2s_soc_platform_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
					&bf5xx_i2s_soc_component, NULL, 0);
}

static struct platform_driver bfin_i2s_pcm_driver = {
	.driver = {
		.name = "bfin-i2s-pcm-audio",
	},

	.probe = bfin_i2s_soc_platform_probe,
};

module_platform_driver(bfin_i2s_pcm_driver);

MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("ADI Blackfin I2S PCM DMA module");
MODULE_LICENSE("GPL");

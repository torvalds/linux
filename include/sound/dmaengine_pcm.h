/*
 *  Copyright (C) 2012, Analog Devices Inc.
 *	Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __SOUND_DMAENGINE_PCM_H__
#define __SOUND_DMAENGINE_PCM_H__

#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/dmaengine.h>

/**
 * snd_pcm_substream_to_dma_direction - Get dma_transfer_direction for a PCM
 *   substream
 * @substream: PCM substream
 */
static inline enum dma_transfer_direction
snd_pcm_substream_to_dma_direction(const struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return DMA_MEM_TO_DEV;
	else
		return DMA_DEV_TO_MEM;
}

int snd_hwparams_to_dma_slave_config(const struct snd_pcm_substream *substream,
	const struct snd_pcm_hw_params *params, struct dma_slave_config *slave_config);
int snd_dmaengine_pcm_trigger(struct snd_pcm_substream *substream, int cmd);
snd_pcm_uframes_t snd_dmaengine_pcm_pointer(struct snd_pcm_substream *substream);
snd_pcm_uframes_t snd_dmaengine_pcm_pointer_no_residue(struct snd_pcm_substream *substream);

int snd_dmaengine_pcm_open(struct snd_pcm_substream *substream,
	struct dma_chan *chan);
int snd_dmaengine_pcm_close(struct snd_pcm_substream *substream);

int snd_dmaengine_pcm_open_request_chan(struct snd_pcm_substream *substream,
	dma_filter_fn filter_fn, void *filter_data);
int snd_dmaengine_pcm_close_release_chan(struct snd_pcm_substream *substream);

struct dma_chan *snd_dmaengine_pcm_request_channel(dma_filter_fn filter_fn,
	void *filter_data);
struct dma_chan *snd_dmaengine_pcm_get_chan(struct snd_pcm_substream *substream);

/*
 * The DAI supports packed transfers, eg 2 16-bit samples in a 32-bit word.
 * If this flag is set the dmaengine driver won't put any restriction on
 * the supported sample formats and set the DMA transfer size to undefined.
 * The DAI driver is responsible to disable any unsupported formats in it's
 * configuration and catch corner cases that are not already handled in
 * the ALSA core.
 */
#define SND_DMAENGINE_PCM_DAI_FLAG_PACK BIT(0)

/**
 * struct snd_dmaengine_dai_dma_data - DAI DMA configuration data
 * @addr: Address of the DAI data source or destination register.
 * @addr_width: Width of the DAI data source or destination register.
 * @maxburst: Maximum number of words(note: words, as in units of the
 * src_addr_width member, not bytes) that can be send to or received from the
 * DAI in one burst.
 * @slave_id: Slave requester id for the DMA channel.
 * @filter_data: Custom DMA channel filter data, this will usually be used when
 * requesting the DMA channel.
 * @chan_name: Custom channel name to use when requesting DMA channel.
 * @fifo_size: FIFO size of the DAI controller in bytes
 * @flags: PCM_DAI flags, only SND_DMAENGINE_PCM_DAI_FLAG_PACK for now
 */
struct snd_dmaengine_dai_dma_data {
	dma_addr_t addr;
	enum dma_slave_buswidth addr_width;
	u32 maxburst;
	unsigned int slave_id;
	void *filter_data;
	const char *chan_name;
	unsigned int fifo_size;
	unsigned int flags;
};

void snd_dmaengine_pcm_set_config_from_dai_data(
	const struct snd_pcm_substream *substream,
	const struct snd_dmaengine_dai_dma_data *dma_data,
	struct dma_slave_config *config);


/*
 * Try to request the DMA channel using compat_request_channel or
 * compat_filter_fn if it couldn't be requested through devicetree.
 */
#define SND_DMAENGINE_PCM_FLAG_COMPAT BIT(0)
/*
 * Don't try to request the DMA channels through devicetree. This flag only
 * makes sense if SND_DMAENGINE_PCM_FLAG_COMPAT is set as well.
 */
#define SND_DMAENGINE_PCM_FLAG_NO_DT BIT(1)
/*
 * The PCM is half duplex and the DMA channel is shared between capture and
 * playback.
 */
#define SND_DMAENGINE_PCM_FLAG_HALF_DUPLEX BIT(3)
/*
 * The PCM streams have custom channel names specified.
 */
#define SND_DMAENGINE_PCM_FLAG_CUSTOM_CHANNEL_NAME BIT(4)

/**
 * struct snd_dmaengine_pcm_config - Configuration data for dmaengine based PCM
 * @prepare_slave_config: Callback used to fill in the DMA slave_config for a
 *   PCM substream. Will be called from the PCM drivers hwparams callback.
 * @compat_request_channel: Callback to request a DMA channel for platforms
 *   which do not use devicetree.
 * @process: Callback used to apply processing on samples transferred from/to
 *   user space.
 * @compat_filter_fn: Will be used as the filter function when requesting a
 *  channel for platforms which do not use devicetree. The filter parameter
 *  will be the DAI's DMA data.
 * @dma_dev: If set, request DMA channel on this device rather than the DAI
 *  device.
 * @chan_names: If set, these custom DMA channel names will be requested at
 *  registration time.
 * @pcm_hardware: snd_pcm_hardware struct to be used for the PCM.
 * @prealloc_buffer_size: Size of the preallocated audio buffer.
 *
 * Note: If both compat_request_channel and compat_filter_fn are set
 * compat_request_channel will be used to request the channel and
 * compat_filter_fn will be ignored. Otherwise the channel will be requested
 * using dma_request_channel with compat_filter_fn as the filter function.
 */
struct snd_dmaengine_pcm_config {
	int (*prepare_slave_config)(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct dma_slave_config *slave_config);
	struct dma_chan *(*compat_request_channel)(
			struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_substream *substream);
	int (*process)(struct snd_pcm_substream *substream,
		       int channel, unsigned long hwoff,
		       void *buf, unsigned long bytes);
	dma_filter_fn compat_filter_fn;
	struct device *dma_dev;
	const char *chan_names[SNDRV_PCM_STREAM_LAST + 1];

	const struct snd_pcm_hardware *pcm_hardware;
	unsigned int prealloc_buffer_size;
};

int snd_dmaengine_pcm_register(struct device *dev,
	const struct snd_dmaengine_pcm_config *config,
	unsigned int flags);
void snd_dmaengine_pcm_unregister(struct device *dev);

int devm_snd_dmaengine_pcm_register(struct device *dev,
	const struct snd_dmaengine_pcm_config *config,
	unsigned int flags);

int snd_dmaengine_pcm_prepare_slave_config(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct dma_slave_config *slave_config);

#define SND_DMAENGINE_PCM_DRV_NAME "snd_dmaengine_pcm"

#endif

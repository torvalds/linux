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
	dma_filter_fn filter_fn, void *filter_data);
int snd_dmaengine_pcm_close(struct snd_pcm_substream *substream);

struct dma_chan *snd_dmaengine_pcm_get_chan(struct snd_pcm_substream *substream);

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
 */
struct snd_dmaengine_dai_dma_data {
	dma_addr_t addr;
	enum dma_slave_buswidth addr_width;
	u32 maxburst;
	unsigned int slave_id;
	void *filter_data;
};

void snd_dmaengine_pcm_set_config_from_dai_data(
	const struct snd_pcm_substream *substream,
	const struct snd_dmaengine_dai_dma_data *dma_data,
	struct dma_slave_config *config);

#endif

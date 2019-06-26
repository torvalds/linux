// SPDX-License-Identifier: GPL-2.0

#ifndef __SPRD_PCM_DMA_H
#define __SPRD_PCM_DMA_H

#define SPRD_PCM_CHANNEL_MAX	2

struct sprd_pcm_dma_params {
	dma_addr_t dev_phys[SPRD_PCM_CHANNEL_MAX];
	u32 datawidth[SPRD_PCM_CHANNEL_MAX];
	u32 fragment_len[SPRD_PCM_CHANNEL_MAX];
	const char *chan_name[SPRD_PCM_CHANNEL_MAX];
};

#endif /* __SPRD_PCM_DMA_H */

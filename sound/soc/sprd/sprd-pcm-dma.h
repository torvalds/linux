// SPDX-License-Identifier: GPL-2.0

#ifndef __SPRD_PCM_DMA_H
#define __SPRD_PCM_DMA_H

#define DRV_NAME		"sprd_pcm_dma"
#define SPRD_PCM_CHANNEL_MAX	2

extern const struct snd_compress_ops sprd_platform_compress_ops;

struct sprd_pcm_dma_params {
	dma_addr_t dev_phys[SPRD_PCM_CHANNEL_MAX];
	u32 datawidth[SPRD_PCM_CHANNEL_MAX];
	u32 fragment_len[SPRD_PCM_CHANNEL_MAX];
	const char *chan_name[SPRD_PCM_CHANNEL_MAX];
};

struct sprd_compr_playinfo {
	int total_time;
	int current_time;
	int total_data_length;
	u64 current_data_offset;
};

struct sprd_compr_params {
	u32 direction;
	u32 rate;
	u32 sample_rate;
	u32 channels;
	u32 format;
	u32 period;
	u32 periods;
	u32 info_phys;
	u32 info_size;
};

struct sprd_compr_callback {
	void (*drain_notify)(void *data);
	void *drain_data;
};

struct sprd_compr_ops {
	int (*open)(int str_id, struct sprd_compr_callback *cb);
	int (*close)(int str_id);
	int (*start)(int str_id);
	int (*stop)(int str_id);
	int (*pause)(int str_id);
	int (*pause_release)(int str_id);
	int (*drain)(u64 received_total);
	int (*set_params)(int str_id, struct sprd_compr_params *params);
};

struct sprd_compr_data {
	struct sprd_compr_ops *ops;
	struct sprd_pcm_dma_params *dma_params;
};

#endif /* __SPRD_PCM_DMA_H */

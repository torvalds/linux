/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Rockchip DLP (Digital Loopback) driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 *
 */

#ifndef _ROCKCHIP_DLP_H
#define _ROCKCHIP_DLP_H

#include "rockchip_dlp_pcm.h"

#define DLP_MAX_DRDS	8

/* MUST: enum dlp_mode should be match to dlp_text */
enum dlp_mode {
	DLP_MODE_DISABLED,
	DLP_MODE_2CH_1LP_1MIC,		/* replace cap-ch-0   with play-ch-0 */
	DLP_MODE_2CH_1MIC_1LP,		/* replace cap-ch-1   with play-ch-1 */
	DLP_MODE_2CH_1MIC_1LP_MIX,	/* replace cap-ch-1   with play-ch-all-mix */
	DLP_MODE_2CH_2LP,		/* replace cap-ch-0~1 with play-ch-0~1 */
	DLP_MODE_4CH_2MIC_2LP,		/* replace cap-ch-2~3 with play-ch-0~1 */
	DLP_MODE_4CH_2MIC_1LP_MIX,	/* replace cap-ch-3   with play-ch-all-mix */
	DLP_MODE_4CH_4LP,		/* replace cap-ch-0~3 with play-ch-0~3 */
	DLP_MODE_6CH_4MIC_2LP,		/* replace cap-ch-4~5 with play-ch-0~1 */
	DLP_MODE_6CH_4MIC_1LP_MIX,	/* replace cap-ch-4   with play-ch-all-mix */
	DLP_MODE_6CH_6LP,		/* replace cap-ch-0~5 with play-ch-0~5 */
	DLP_MODE_8CH_6MIC_2LP,		/* replace cap-ch-6~7 with play-ch-0~1 */
	DLP_MODE_8CH_6MIC_1LP_MIX,	/* replace cap-ch-6   with play-ch-all-mix */
	DLP_MODE_8CH_8LP,		/* replace cap-ch-0~7 with play-ch-0~7 */
	DLP_MODE_10CH_8MIC_2LP,		/* replace cap-ch-8~9 with play-ch-0~1 */
	DLP_MODE_10CH_8MIC_1LP_MIX,	/* replace cap-ch-8   with play-ch-all-mix */
	DLP_MODE_16CH_8MIC_8LP,		/* replace cap-ch-8~f with play-ch-8~f */
};

struct dlp;

struct dlp_runtime_data {
	struct dlp *parent;
	struct kref refcount;
	struct list_head node;
	char *buf;
	snd_pcm_uframes_t buf_sz;
	snd_pcm_uframes_t last_buf_sz;
	snd_pcm_uframes_t period_sz;
	int64_t hw_ptr;
	int64_t hw_ptr_delta; /* play-ptr - cap-ptr */
	atomic64_t period_elapsed;
	atomic_t stop;
	unsigned int frame_bytes;
	unsigned int channels;
	unsigned int buf_ofs;
	int stream;
};

struct dlp {
	struct device *dev;
	struct list_head drd_avl_list;
	struct list_head drd_rdy_list;
	struct list_head drd_ref_list;
	struct dlp_runtime_data drds[DLP_MAX_DRDS];
	struct dlp_runtime_data *drd_pb_shadow;
	struct snd_soc_component component;
	const struct snd_dlp_config *config;
	enum dlp_mode mode;
	int drd_avl_count;
	atomic_t active;
	spinlock_t lock;
};

typedef snd_pcm_uframes_t (*dma_pointer_f)(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream);

static inline struct dlp *soc_component_to_dlp(struct snd_soc_component *p)
{
	return container_of(p, struct dlp, component);
}

static inline struct dlp_runtime_data *substream_to_drd(
	const struct snd_pcm_substream *substream)
{
	if (!substream->runtime)
		return NULL;

	return substream->runtime->private_data;
}

static inline ssize_t dlp_channels_to_bytes(struct dlp_runtime_data *drd,
					    int channels)
{
	return (drd->frame_bytes / drd->channels) * channels;
}

static inline ssize_t dlp_frames_to_bytes(struct dlp_runtime_data *drd,
					  snd_pcm_sframes_t size)
{
	return size * drd->frame_bytes;
}

static inline snd_pcm_sframes_t dlp_bytes_to_frames(struct dlp_runtime_data *drd,
						    ssize_t size)
{
	return size / drd->frame_bytes;
}

void dlp_dma_complete(struct dlp *dlp, struct dlp_runtime_data *drd);
int dlp_open(struct dlp *dlp, struct dlp_runtime_data *drd,
	     struct snd_pcm_substream *substream);
int dlp_close(struct dlp *dlp, struct dlp_runtime_data *drd,
	      struct snd_pcm_substream *substream);
int dlp_hw_params(struct snd_soc_component *component,
		  struct snd_pcm_substream *substream,
		  struct snd_pcm_hw_params *params);
int dlp_start(struct snd_soc_component *component,
	      struct snd_pcm_substream *substream,
	      struct device *dev,
	      dma_pointer_f dma_pointer);
void dlp_stop(struct snd_soc_component *component,
	      struct snd_pcm_substream *substream,
	      dma_pointer_f dma_pointer);
int dlp_copy_user(struct snd_soc_component *component,
		  struct snd_pcm_substream *substream,
		  int channel, unsigned long hwoff,
		  void __user *buf, unsigned long bytes);
int dlp_prepare(struct snd_soc_component *component,
		struct snd_pcm_substream *substream);
int dlp_probe(struct snd_soc_component *component);
int dlp_register(struct dlp *dlp, struct device *dev,
		 const struct snd_soc_component_driver *driver,
		 const struct snd_dlp_config *config);

#endif

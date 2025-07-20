/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2025 Intel Corporation.
 */

#ifndef __HDA_SDW_BPT_H
#define __HDA_SDW_BPT_H

#include <linux/device.h>

struct hdac_ext_stream;
struct snd_dma_buffer;

#if IS_ENABLED(CONFIG_SND_SOF_SOF_HDA_SDW_BPT)
int hda_sdw_bpt_open(struct device *dev, int link_id, struct hdac_ext_stream **bpt_tx_stream,
		     struct snd_dma_buffer *dmab_tx_bdl, u32 bpt_tx_num_bytes,
		     u32 tx_dma_bandwidth, struct hdac_ext_stream **bpt_rx_stream,
		     struct snd_dma_buffer *dmab_rx_bdl, u32 bpt_rx_num_bytes,
		     u32 rx_dma_bandwidth);

int hda_sdw_bpt_send_async(struct device *dev, struct hdac_ext_stream *bpt_tx_stream,
			   struct hdac_ext_stream *bpt_rx_stream);

int hda_sdw_bpt_wait(struct device *dev, struct hdac_ext_stream *bpt_tx_stream,
		     struct hdac_ext_stream *bpt_rx_stream);

int hda_sdw_bpt_close(struct device *dev, struct hdac_ext_stream *bpt_tx_stream,
		      struct snd_dma_buffer *dmab_tx_bdl, struct hdac_ext_stream *bpt_rx_stream,
		      struct snd_dma_buffer *dmab_rx_bdl);
#else
static inline int hda_sdw_bpt_open(struct device *dev, int link_id,
				   struct hdac_ext_stream **bpt_tx_stream,
				   struct snd_dma_buffer *dmab_tx_bdl, u32 bpt_tx_num_bytes,
				   u32 tx_dma_bandwidth, struct hdac_ext_stream **bpt_rx_stream,
				   struct snd_dma_buffer *dmab_rx_bdl, u32 bpt_rx_num_bytes,
				   u32 rx_dma_bandwidth)
{
	WARN_ONCE(1, "SoundWire BPT is disabled");
	return -EOPNOTSUPP;
}

static inline int hda_sdw_bpt_send_async(struct device *dev, struct hdac_ext_stream *bpt_tx_stream,
					 struct hdac_ext_stream *bpt_rx_stream)
{
	WARN_ONCE(1, "SoundWire BPT is disabled");
	return -EOPNOTSUPP;
}

static inline int hda_sdw_bpt_wait(struct device *dev, struct hdac_ext_stream *bpt_tx_stream,
				   struct hdac_ext_stream *bpt_rx_stream)
{
	WARN_ONCE(1, "SoundWire BPT is disabled");
	return -EOPNOTSUPP;
}

static inline int hda_sdw_bpt_close(struct device *dev, struct hdac_ext_stream *bpt_tx_stream,
				    struct snd_dma_buffer *dmab_tx_bdl,
				    struct hdac_ext_stream *bpt_rx_stream,
				    struct snd_dma_buffer *dmab_rx_bdl)
{
	WARN_ONCE(1, "SoundWire BPT is disabled");
	return -EOPNOTSUPP;
}
#endif

#endif /* __HDA_SDW_BPT_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 NXP
 *
 */

#ifndef _FSL_ASRC_COMMON_H
#define _FSL_ASRC_COMMON_H

/* directions */
#define IN	0
#define OUT	1

enum asrc_pair_index {
	ASRC_INVALID_PAIR = -1,
	ASRC_PAIR_A = 0,
	ASRC_PAIR_B = 1,
	ASRC_PAIR_C = 2,
	ASRC_PAIR_D = 3,
};

#define PAIR_CTX_NUM  0x4

/**
 * fsl_asrc_pair: ASRC Pair common data
 *
 * @asrc: pointer to its parent module
 * @error: error record
 * @index: pair index (ASRC_PAIR_A, ASRC_PAIR_B, ASRC_PAIR_C)
 * @channels: occupied channel number
 * @desc: input and output dma descriptors
 * @dma_chan: inputer and output DMA channels
 * @dma_data: private dma data
 * @pos: hardware pointer position
 * @req_dma_chan: flag to release dev_to_dev chan
 * @private: pair private area
 */
struct fsl_asrc_pair {
	struct fsl_asrc *asrc;
	unsigned int error;

	enum asrc_pair_index index;
	unsigned int channels;

	struct dma_async_tx_descriptor *desc[2];
	struct dma_chan *dma_chan[2];
	struct imx_dma_data dma_data;
	unsigned int pos;
	bool req_dma_chan;

	void *private;
};

/**
 * fsl_asrc: ASRC common data
 *
 * @dma_params_rx: DMA parameters for receive channel
 * @dma_params_tx: DMA parameters for transmit channel
 * @pdev: platform device pointer
 * @regmap: regmap handler
 * @paddr: physical address to the base address of registers
 * @mem_clk: clock source to access register
 * @ipg_clk: clock source to drive peripheral
 * @spba_clk: SPBA clock (optional, depending on SoC design)
 * @lock: spin lock for resource protection
 * @pair: pair pointers
 * @channel_avail: non-occupied channel numbers
 * @asrc_rate: default sample rate for ASoC Back-Ends
 * @asrc_format: default sample format for ASoC Back-Ends
 * @use_edma: edma is used
 * @get_dma_channel: function pointer
 * @request_pair: function pointer
 * @release_pair: function pointer
 * @get_fifo_addr: function pointer
 * @pair_priv_size: size of pair private struct.
 * @private: private data structure
 */
struct fsl_asrc {
	struct snd_dmaengine_dai_dma_data dma_params_rx;
	struct snd_dmaengine_dai_dma_data dma_params_tx;
	struct platform_device *pdev;
	struct regmap *regmap;
	unsigned long paddr;
	struct clk *mem_clk;
	struct clk *ipg_clk;
	struct clk *spba_clk;
	spinlock_t lock;      /* spin lock for resource protection */

	struct fsl_asrc_pair *pair[PAIR_CTX_NUM];
	unsigned int channel_avail;

	int asrc_rate;
	snd_pcm_format_t asrc_format;
	bool use_edma;

	struct dma_chan *(*get_dma_channel)(struct fsl_asrc_pair *pair, bool dir);
	int (*request_pair)(int channels, struct fsl_asrc_pair *pair);
	void (*release_pair)(struct fsl_asrc_pair *pair);
	int (*get_fifo_addr)(u8 dir, enum asrc_pair_index index);
	size_t pair_priv_size;

	void *private;
};

#define DRV_NAME "fsl-asrc-dai"
extern struct snd_soc_component_driver fsl_asrc_component;

#endif /* _FSL_ASRC_COMMON_H */

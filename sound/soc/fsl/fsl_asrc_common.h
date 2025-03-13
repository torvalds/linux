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
 * struct fsl_asrc_m2m_cap - capability data
 * @fmt_in: input sample format
 * @fmt_out: output sample format
 * @chan_min: minimum channel number
 * @chan_max: maximum channel number
 * @rate_in: minimum rate
 * @rate_out: maximum rete
 */
struct fsl_asrc_m2m_cap {
	u64 fmt_in;
	u64 fmt_out;
	int chan_min;
	int chan_max;
	const unsigned int *rate_in;
	int rate_in_count;
	const unsigned int *rate_out;
	int rate_out_count;
};

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
 * @complete: dma task complete
 * @sample_format: format of m2m
 * @rate: rate of m2m
 * @buf_len: buffer length of m2m
 * @dma_buffer: buffer pointers
 * @first_convert: start of conversion
 * @ratio_mod_flag: flag for new ratio modifier
 * @ratio_mod: ratio modification
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

	/* used for m2m */
	struct completion complete[2];
	snd_pcm_format_t sample_format[2];
	unsigned int rate[2];
	unsigned int buf_len[2];
	struct snd_dma_buffer dma_buffer[2];
	unsigned int first_convert;
	bool ratio_mod_flag;
	unsigned int ratio_mod;
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
 * @card: compress sound card
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
 * @m2m_get_cap: function pointer
 * @m2m_prepare: function pointer
 * @m2m_start: function pointer
 * @m2m_unprepare: function pointer
 * @m2m_stop: function pointer
 * @m2m_calc_out_len: function pointer
 * @m2m_get_maxburst: function pointer
 * @m2m_pair_suspend: function pointer
 * @m2m_pair_resume: function pointer
 * @m2m_set_ratio_mod: function pointer
 * @get_output_fifo_size: function pointer
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
	struct snd_card *card;
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
	int (*m2m_get_cap)(struct fsl_asrc_m2m_cap *cap);

	int (*m2m_prepare)(struct fsl_asrc_pair *pair);
	int (*m2m_start)(struct fsl_asrc_pair *pair);
	int (*m2m_unprepare)(struct fsl_asrc_pair *pair);
	int (*m2m_stop)(struct fsl_asrc_pair *pair);

	int (*m2m_calc_out_len)(struct fsl_asrc_pair *pair, int input_buffer_length);
	int (*m2m_get_maxburst)(u8 dir, struct fsl_asrc_pair *pair);
	int (*m2m_pair_suspend)(struct fsl_asrc_pair *pair);
	int (*m2m_pair_resume)(struct fsl_asrc_pair *pair);
	int (*m2m_set_ratio_mod)(struct fsl_asrc_pair *pair, int val);

	unsigned int (*get_output_fifo_size)(struct fsl_asrc_pair *pair);
	size_t pair_priv_size;

	void *private;
};

#define DRV_NAME "fsl-asrc-dai"
extern struct snd_soc_component_driver fsl_asrc_component;

int fsl_asrc_m2m_init(struct fsl_asrc *asrc);
void fsl_asrc_m2m_exit(struct fsl_asrc *asrc);
int fsl_asrc_m2m_resume(struct fsl_asrc *asrc);
int fsl_asrc_m2m_suspend(struct fsl_asrc *asrc);

#endif /* _FSL_ASRC_COMMON_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef SND_SOC_QCOM_QAIF_H
#define SND_SOC_QCOM_QAIF_H

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <sound/pcm.h>
#include <dt-bindings/sound/qcom,q6dsp-lpass-ports.h>

struct snd_pcm_substream;
struct snd_soc_dai_driver;
struct snd_soc_dai_ops;
struct platform_device;

#define QAIF_CIF_DMA_INTF_ONE_CHANNEL		0x01
#define QAIF_CIF_DMA_INTF_TWO_CHANNEL		0x03
#define QAIF_CIF_DMA_INTF_FOUR_CHANNEL		0x0f
#define QAIF_CIF_DMA_INTF_SIX_CHANNEL		0x3f
#define QAIF_CIF_DMA_INTF_EIGHT_CHANNEL		0xff

#define QAIF_AIF_SLOT_WIDTH(bits)		((bits) - 1)
#define QAIF_AIF_SAMPLE_WIDTH(bits)		((bits) - 1)

/* Watermark level 5 encoded as N-1: hardware field value = watermark - 1 */
#define QAIF_DMACTL_WM_5			4

#define QAIF_DMA_BYTES_TO_WORDS_SHIFT		3
#define QAIF_DMA_CLOCK_FREQ			38400000

#define QAIF_MAX_AIF_CFG_CNT			4
#define QAIF_MAX_LANES				8

#define QAIF_MAX_MI2S_PORTS			8
#define QAIF_MI2S_SLOTS				2
#define QAIF_MAX_AIF_DMA_IDX			8
#define QAIF_MAX_CIF_DMA_IDX			8

/* SID map register CSID field (WRDMA_SID/RDDMA_SID), bits [4:0] */
#define QAIF_CSID_MASK				GENMASK(4, 0)

static inline bool qaif_is_aif_mi2s_port(unsigned int dai_id)
{
	return (dai_id >= AIF_MI2S_RX_0 && dai_id <= AIF_MI2S_RX_12) ||
	       (dai_id >= AIF_MI2S_TX_0 && dai_id <= AIF_MI2S_TX_12);
}

static inline bool qaif_is_aif_tdm_port(unsigned int dai_id)
{
	return (dai_id >= AIF_TDM_RX_0 && dai_id <= AIF_TDM_RX_12) ||
	       (dai_id >= AIF_TDM_TX_0 && dai_id <= AIF_TDM_TX_12);
}

static inline bool qaif_is_aif_port(unsigned int dai_id)
{
	return qaif_is_aif_mi2s_port(dai_id) || qaif_is_aif_tdm_port(dai_id);
}

static inline bool qaif_is_cif_port(unsigned int dai_id)
{
	return (dai_id >= WSA_CODEC_DMA_RX_0 && dai_id <= RX_CODEC_DMA_RX_7);
}

enum qxm_sel {
	QAIF_QXM0 = 0,
	QAIF_QXM1 = 1,
};

enum qaif_irq_type {
	QAIF_AIF_IRQ = 0,
	QAIF_CIF_IRQ = 1,
	QAIF_IRQ_TYPE_MAX,
};

enum qaif_dma_type {
	QAIF_AIF_DMA = 0,
	QAIF_CIF_DMA = 1,
};

struct qaif_aif_config {
	u32 sync_mode;
	u32 sync_src;
	bool invert_sync;
	u32 sync_delay;
	u32 slot_width;
	u32 slot_en_rx_mask;
	u32 slot_en_tx_mask;
	bool loopback_en;
	bool ctrl_data_oe;
	u32 lane_en_mask;
	u32 lane_dir_mask;
	bool full_cycle_en;
	u32 slot_num;
};

struct qaif_pcm_data {
	/* Flat slot in aif_substream[] (or cif_substream[]) used to track
	 * this stream. Direction-encoded: playback uses the raw DMA channel
	 * number (0..num_rddma-1), capture adds wrdma_start so playback and
	 * capture of the same DAI occupy distinct slots.
	 * Set by alloc_stream_dma_idx(); bounded by QAIF_MAX_AIF/CIF_DMA_IDX.
	 */
	int stream_dma_idx;
	/* Hardware channel number (0-based within its DMA bank) passed to
	 * register address helpers (e.g. QAIF_RDDMA_CFG_REG(v, chan)).
	 * Direction-agnostic: the register helpers select RDDMA or WRDMA bank
	 * based on stream direction, so both directions of the same DAI share
	 * the same value. Set by get_dma_idx().
	 */
	int dma_reg_idx;
};

/* Named 0-based DMA channel indices used as the dma_idx of a
 * qaif_dmaidx_dai_map entry, so board tables reference channels by name.
 */
enum qaif_dma_idx {
	QAIF_DMA_IDX0 = 0,
	QAIF_DMA_IDX1,
	QAIF_DMA_IDX2,
	QAIF_DMA_IDX3,
	QAIF_DMA_IDX4,
	QAIF_DMA_IDX5,
	QAIF_DMA_IDX6,
	QAIF_DMA_IDX7,
};

/* Associates a DAI ID with its physical DMA channel index on a given
 * board/variant. @dai_id is the DAI to match; @dma_idx (a qaif_dma_idx value)
 * is the 0-based hardware channel that get_dma_idx() returns for it.
 */
struct qaif_dmaidx_dai_map {
	unsigned int dai_id;
	int dma_idx;
};

struct qaif_drv_data {
	struct clk *mi2s_bit_clk[QAIF_MAX_MI2S_PORTS];
	struct clk_bulk_data *clks;
	int num_clks;
	struct clk *aud_dma_clk;
	struct clk *aud_dma_mem_clk;
	void __iomem *audio_qaif;
	struct regmap *audio_qaif_map;
	int audio_qaif_irq;
	struct mutex stream_lock; /* serializes stream open/close */
	bool qaif_hw_configured;
	const struct qaif_variant *variant;
	struct qaif_aif_config aif_intf_cfg[QAIF_MAX_AIF_CFG_CNT];
	unsigned long aif_dma_idx_bit_map;
	unsigned long cif_dma_idx_bit_map;
	struct snd_pcm_substream *aif_substream[QAIF_MAX_AIF_DMA_IDX];
	struct snd_pcm_substream *cif_substream[QAIF_MAX_CIF_DMA_IDX];
	u32 smmu_csid_bits;		/* SMMU stream ID bits from DT iommus cell */
};

enum qaif_summary_irq_bitmask {
	QAIF_SUMMARY_BITMASK_AIF_PERIOD_RDDMA		= BIT(0),
	QAIF_SUMMARY_BITMASK_AIF_UNDERFLOW_RDDMA	= BIT(1),
	QAIF_SUMMARY_BITMASK_AIF_ERR_RSP_RDDMA		= BIT(2),
	QAIF_SUMMARY_BITMASK_AIF_PERIOD_WRDMA		= BIT(3),
	QAIF_SUMMARY_BITMASK_AIF_OVERFLOW_WRDMA		= BIT(4),
	QAIF_SUMMARY_BITMASK_AIF_ERR_RSP_WRDMA		= BIT(5),
	QAIF_SUMMARY_BITMASK_CIF_PERIOD_RDDMA		= BIT(18),
	QAIF_SUMMARY_BITMASK_CIF_UNDERFLOW_RDDMA	= BIT(19),
	QAIF_SUMMARY_BITMASK_CIF_ERR_RSP_RDDMA		= BIT(20),
	QAIF_SUMMARY_BITMASK_CIF_PERIOD_WRDMA		= BIT(24),
	QAIF_SUMMARY_BITMASK_CIF_OVERFLOW_WRDMA		= BIT(25),
	QAIF_SUMMARY_BITMASK_CIF_ERR_RSP_WRDMA		= BIT(26),
};

enum qaif_client_status_register_bitmask_info {
	QAIF_BITMASK_AIF_RDDMA_WRDMA	= GENMASK(5, 0),
	QAIF_BITMASK_CIF_RDDMA_WRDMA	= GENMASK(26, 18),
};

struct qaif_irq_map {
	u32 client_id;
	u32 mask;
	/*
	 * Called from the hard IRQ handler (asoc_platform_qaif_irq()) with
	 * @status holding the summary interrupt status. Must not sleep.
	 */
	irqreturn_t (*client_irq_handler)(struct qaif_drv_data *drvdata, u32 status);
};

enum qaif_dma_dir {
	QAIF_DMA_RDDMA = 0,
	QAIF_DMA_WRDMA = 1,
};

enum qaif_irq {
	QAIF_IRQ_PERIOD = 0,
	QAIF_IRQ_OVERFLOW,
	QAIF_IRQ_UNDERFLOW,
	QAIF_IRQ_ERROR,
};

enum qaif_irq_op {
	QAIF_IRQ_CLEAR = 0,
	QAIF_IRQ_ENABLE,
	QAIF_IRQ_DISABLE,
};

enum qaif_client_info {
	QAIF_CLIENT_ID_AIF_DMA = 0,
	QAIF_CLIENT_ID_CIF_DMA,
};

struct qaif_variant {
	u32 ee;				/* Hardware EE index assigned to this driver instance */

	u32 num_rddma;
	u32 num_wrdma;
	u32 wrdma_start;
	u32 num_codec_rddma;
	u32 num_codec_wrdma;
	u32 codec_wrdma_start;
	u32 num_intf;

	u32 rddma_reg_base;
	u32 rddma_stride;
	u32 codec_rddma_reg_base;
	u32 codec_rddma_stride;
	u32 wrdma_reg_base;
	u32 wrdma_stride;
	u32 codec_wrdma_reg_base;
	u32 codec_wrdma_stride;

	u32 rddma_irq_reg_base;
	u32 rddma_irq_stride;
	u32 codec_rddma_irq_reg_base;
	u32 codec_rddma_irq_stride;
	u32 wrdma_irq_reg_base;
	u32 wrdma_irq_stride;
	u32 codec_wrdma_irq_reg_base;
	u32 codec_wrdma_irq_stride;

	/* QSB master port selector; only QAIF_QXM0 is supported today */
	enum qxm_sel qxm_type;
	/* SHRAM sizes in 64-bit words */
	u32 rddma_shram_len;
	/* SHRAM start offsets (in 64-bit words), indexed by enum qaif_dma_type */
	u32 rddma_shram_start_addr[2];
	u32 wrdma_shram_len;
	u32 wrdma_shram_start_addr[2];

	const char * const *clk_name;
	int num_clks;
	struct snd_soc_dai_driver *dai_driver;
	int num_dai;
	const char * const *dai_bit_clk_names;
	/*
	 * Optional per-AIF configuration tables, indexed by AIF interface
	 * index (0..num_intf-1). NULL if the variant does not use them.
	 */
	const bool *aif_full_cycle_en;
	const bool *aif_ctrl_data_oe;
	const bool *aif_loopback_en;

	/* Returns a direction-encoded flat slot index for use in
	 * aif_substream[]/cif_substream[] and the dma_idx_bit_map.
	 * Playback returns the raw channel number; capture adds wrdma_start (or
	 * codec_wrdma_start) so both directions of the same DAI get distinct
	 * slots. Sets the corresponding bit atomically; returns -EBUSY if
	 * already open, -EINVAL if @dai_id or the computed index is out of range.
	 */
	int (*alloc_stream_dma_idx)(struct qaif_drv_data *data, int direction,
				    unsigned int dai_id);
	/* Clears the bit set by alloc_stream_dma_idx. */
	int (*free_stream_dma_idx)(struct qaif_drv_data *data, int chan,
				   unsigned int dai_id);
	/* Returns the hardware channel number (0-based within its DMA bank)
	 * for @dai_id, direction-agnostic. Used as dma_reg_idx.
	 */
	int (*get_dma_idx)(unsigned int dai_id);
};

extern const struct snd_soc_dai_ops asoc_qcom_qaif_cif_dai_ops;
extern const struct snd_soc_dai_ops asoc_qcom_qaif_aif_cpu_dai_ops;

int asoc_qcom_qaif_cpu_platform_probe(struct platform_device *pdev);
int asoc_qcom_qaif_platform_register(struct platform_device *pdev);
extern const struct dev_pm_ops asoc_qcom_qaif_pm_ops;

#endif /* SND_SOC_QCOM_QAIF_H */

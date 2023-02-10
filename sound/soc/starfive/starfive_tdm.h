/* SPDX-License-Identifier: GPL-2.0
 *
 * TDM driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef __SND_SOC_STARFIVE_TDM_H
#define __SND_SOC_STARFIVE_TDM_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/types.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <linux/dmaengine.h>
#include <linux/types.h>

#define TDM_PCMGBCR			0x00
	#define PCMGBCR_MASK		0x1e
	#define PCMGBCR_ENABLE		BIT(0)
	#define PCMGBCR_TRITXEN		BIT(4)
	#define CLKPOL_BIT		5
	#define TRITXEN_BIT		4
	#define ELM_BIT			3
	#define SYNCM_BIT		2
	#define MS_BIT			1
#define TDM_PCMTXCR			0x04
	#define PCMTXCR_TXEN		BIT(0)
	#define IFL_BIT			11
	#define WL_BIT			8
	#define SSCALE_BIT		4
	#define SL_BIT			2
	#define LRJ_BIT			1
#define TDM_PCMRXCR			0x08
	#define PCMRXCR_RXEN		BIT(0)
	#define PCMRXCR_RXSL_MASK	0xc
	#define PCMRXCR_RXSL_16BIT	0x4
	#define PCMRXCR_RXSL_32BIT	0x8
	#define PCMRXCR_SCALE_MASK	0xf0
	#define PCMRXCR_SCALE_1CH	0x10
#define TDM_PCMDIV			0x0c

/*  DMA registers */
#define TDM_FIFO			0x170c0000
#define TDM_FIFO_DEPTH			32

#define ONE_CHANNEL_SUPPORT		1
#define TWO_CHANNEL_SUPPORT		2
#define FOUR_CHANNEL_SUPPORT		4
#define SIX_CHANNEL_SUPPORT		6
#define EIGHT_CHANNEL_SUPPORT		8

enum TDM_MASTER_SLAVE_MODE {
	TDM_AS_MASTER = 0,
	TDM_AS_SLAVE,
};

enum TDM_CLKPOL {
	/* tx raising and rx falling */
	TDM_TX_RASING_RX_FALLING = 0,
	/* tx falling and rx raising */
	TDM_TX_FALLING_RX_RASING,
};

enum TDM_FRAME_MODE {
	SHORT_EARLY = 0,
	SHORT_LATER,
	LONG,
};

enum TDM_ELM {
	/* only work while SYNCM=0 */
	TDM_ELM_LATE = 0,
	TDM_ELM_EARLY,
};

enum TDM_SYNCM {
	/* short frame sync */
	TDM_SYNCM_SHORT = 0,
	/* long frame sync */
	TDM_SYNCM_LONG,
};

enum TDM_IFL {
	/* FIFO to send or received : half-1/2, Quarter-1/4 */
	TDM_FIFO_HALF = 0,
	TDM_FIFO_QUARTER,
};

enum TDM_WL {
	/* send or received word length */
	TDM_8BIT_WORD_LEN = 0,
	TDM_16BIT_WORD_LEN,
	TDM_20BIT_WORD_LEN,
	TDM_24BIT_WORD_LEN,
	TDM_32BIT_WORD_LEN,
};

enum TDM_SL {
	/* send or received slot length */
	TDM_8BIT_SLOT_LEN = 0,
	TDM_16BIT_SLOT_LEN,
	TDM_32BIT_SLOT_LEN,
};

enum TDM_LRJ {
	/* left-justify or right-justify */
	TDM_RIGHT_JUSTIFY = 0,
	TDM_LEFT_JUSTIFT,
};

typedef struct tdm_chan_cfg {
	enum TDM_IFL ifl;
	enum TDM_WL  wl;
	unsigned char sscale;
	enum TDM_SL  sl;
	enum TDM_LRJ lrj;
	unsigned char enable;
} tdm_chan_cfg_t;

struct sf_tdm_dev {
	void __iomem *tdm_base;
	struct device *dev;
	struct clk *clk_tdm_ahb;
	struct clk *clk_tdm_apb;
	struct clk *clk_tdm_internal;
	struct clk *clk_tdm_ext;
	struct clk *clk_tdm;
	struct clk *clk_mclk_inner;
	struct reset_control *resets;
	int active;
	
	enum TDM_CLKPOL clkpolity;
	enum TDM_ELM	elm;
	enum TDM_SYNCM	syncm;
	enum TDM_MASTER_SLAVE_MODE ms_mode;
	enum TDM_FRAME_MODE frame_mode;
	unsigned char	tritxen;
	
	tdm_chan_cfg_t tx;
	tdm_chan_cfg_t rx;
	
	u16 syncdiv;
	u32 samplerate;
	u32 pcmclk;

	/* data related to DMA transfers b/w tdm and DMAC */
	struct snd_dmaengine_dai_dma_data play_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	u32 saved_reg_value[4];
};

#endif	/* __SND_SOC_STARFIVE_TDM_H */

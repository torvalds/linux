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
#define TDM_PCMTXCR			0x04
#define TDM_PCMRXCR			0x08
#define TDM_PCMDIV			0x0c

/*  DMA registers */
#define TDM_RXDMA			0xc0
#define TDM_TXDMA			0xd0

#define TDM_FIFO_DEPTH			16

#define TDM_MAX_CHANNEL_NUM		8
#define TDM_MIN_CHANNEL_NUM		2

#define TWO_CHANNEL_SUPPORT		2	
#define FOUR_CHANNEL_SUPPORT		4
#define SIX_CHANNEL_SUPPORT		6
#define EIGHT_CHANNEL_SUPPORT		8

enum TDM_MODE {
	TDM_AS_MASTER = 0,
	TDM_AS_SLAVE,
};

enum TDM_CLKPOL {
	/* tx raising and rx falling */
	TDM_TX_RASING_RX_FALLING = 0,
	/* tx raising and rx falling */
	TDM_TX_FALLING_RX_RASING,
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
	struct clk *clk_ahb0;
	struct clk *clk_tdm_ahb;
	struct clk *clk_apb0;
	struct clk *clk_tdm_apb;
	struct clk *clk_tdm_intl;
	struct reset_control *rst_ahb;
	struct reset_control *rst_apb;
	struct reset_control *rst_tdm;
	int active;
	
	enum TDM_CLKPOL clkpolity;
	enum TDM_ELM	elm;
	enum TDM_SYNCM	syncm;
	enum TDM_MODE	mode;
	unsigned char	tritxen;
	
	tdm_chan_cfg_t tx;
	tdm_chan_cfg_t rx;
	
	u16 syncdiv;
	u32 samplerate;
	u32 pcmclk;

	/* data related to DMA transfers b/w tdm and DMAC */
	struct snd_dmaengine_dai_dma_data play_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
};

#endif	/* __SND_SOC_STARFIVE_TDM_H */

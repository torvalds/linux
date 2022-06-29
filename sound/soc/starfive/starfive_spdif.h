//SPDX-License-Identifier: GPL-2.0
/*
 * SPDIF driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#ifndef __SND_SOC_STARFIVE_SPDIF_H
#define __SND_SOC_STARFIVE_SPDIF_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/types.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <linux/dmaengine.h>
#include <linux/types.h>

#define SPDIF_CTRL			0x0
#define SPDIF_INT_REG			0x4
#define SPDIF_FIFO_CTRL			0x8
#define SPDIF_STAT_REG			0xC

#define SPDIF_FIFO_ADDR			0x100
#define DMAC_SPDIF_POLLING_LEN		256

/* ctrl: sampled on the rising clock edge */
#define	SPDIF_TSAMPLERATE	0	/* [SRATEW-1:0] */
#define SPDIF_SFR_ENABLE	(1<<8)	/* 0:SFR reg reset to defualt value; auto set back to '1' after reset */
#define SPDIF_ENABLE		(1<<9)	/* 0:reset of SPDIF block, SRF bits are unchanged; 1:enables SPDIF module */
#define SPDIF_FIFO_ENABLE	(1<<10)	/* 0:FIFO pointers are reset to zero,threshold levels for FIFO are unchaned; auto set back to '1' */
#define SPDIF_CLK_ENABLE	(1<<11)	/* 1:blocked and the modules are in power save mode; 0:block feeds the modules */
#define SPDIF_TR_MODE		(1<<12)	/* 0:rx; 1:tx */
#define SPDIF_PARITCHECK	(1<<13)	/* 0:party bit rx in a sub-frame is repeated on the parity; 1:check on a parity error */
#define SPDIF_PARITYGEN		(1<<14)	/* 0:parity bit from FIFO is transmitted in sub-frame;1:parity bit generated inside the core and added to a transmitted sub-frame */
#define SPDIF_VALIDITYCHECK	(1<<15)	/* 0:validity bit in frame isn't checked and all frame are written; 1:validity bit rx is checked */
#define SPDIF_CHANNEL_MODE	(1<<16)	/* 0:two-channel; 1:single-channel */
#define SPDIF_DUPLICATE		(1<<17)	/* only tx -single-channel mode; 0:secondary channel; 1: left(primary) channel */
#define SPDIF_SETPREAMBB	(1<<18)	/* only tx; 0:first preamble B after reset tx valid sub-frame; 1:first preamble B is tx after preambleddel(INT_REG) */
#define SPDIF_USE_FIFO_IF	(1<<19)	/* 0:FIFO disabled ,APB accese FIFO; 1:FIFO enable, APB access to FIFO disable; */
#define SPDIF_PARITY_MASK	(1<<21)
#define SPDIF_UNDERR_MASK	(1<<22)
#define SPDIF_OVRERR_MASK	(1<<23)
#define SPDIF_EMPTY_MASK	(1<<24)
#define	SPDIF_AEMPTY_MASK	(1<<25)
#define SPDIF_FULL_MASK		(1<<26)
#define SPDIF_AFULL_MASK	(1<<27)
#define SPDIF_SYNCERR_MASK	(1<<28)
#define SPDIF_LOCK_MASK		(1<<29)
#define SPDIF_BEGIN_MASK	(1<<30)
#define SPDIF_INTEREQ_MAKS	(1<<31)

#define SPDIF_MASK_ENABLE	(SPDIF_PARITY_MASK | SPDIF_UNDERR_MASK | \
				 SPDIF_OVRERR_MASK | SPDIF_EMPTY_MASK | \
				 SPDIF_AEMPTY_MASK | SPDIF_FULL_MASK | \
				 SPDIF_AFULL_MASK | SPDIF_SYNCERR_MASK | \
				 SPDIF_LOCK_MASK | SPDIF_BEGIN_MASK | \
				 SPDIF_INTEREQ_MAKS)

#define SPDIF_MASK_FIFO     (SPDIF_EMPTY_MASK | SPDIF_AEMPTY_MASK | \
			     SPDIF_FULL_MASK | SPDIF_AFULL_MASK)

/* INT_REG */
#define SPDIF_RSAMPLERATE	0	/* [SRATEW-1:0] */
#define SPDIF_PREAMBLEDEL	8	/* [PDELAYW+7:8] first B delay */
#define SPDIF_PARITYO		(1<<21)	/* 0:clear parity error */
#define SPDIF_TDATA_UNDERR	(1<<22)	/* tx data underrun error;0:clear */
#define SPDIF_RDATA_OVRERR	(1<<23)	/* rx data overrun error; 0:clear */
#define SPDIF_FIFO_EMPTY	(1<<24)	/* empty; 0:clear */
#define SPDIF_FIOF_AEMPTY	(1<<25)	/* almost empty; 0:clear */
#define SPDIF_FIFO_FULL		(1<<26)	/* FIFO full; 0:clear */
#define SPDIF_FIFO_AFULL	(1<<27)	/* FIFO almost full; 0:clear */
#define SPDIF_SYNCERR		(1<<28)	/* sync error; 0:clear */
#define SPDIF_LOCK		(1<<29)	/* sync; 0:clear */
#define SPDIF_BLOCK_BEGIN	(1<<30)	/* new start block rx data */

#define SPDIF_INT_REG_BIT	(SPDIF_PARITYO | SPDIF_TDATA_UNDERR | \
				 SPDIF_RDATA_OVRERR | SPDIF_FIFO_EMPTY | \
				 SPDIF_FIOF_AEMPTY | SPDIF_FIFO_FULL | \
				 SPDIF_FIFO_AFULL | SPDIF_SYNCERR | \
				 SPDIF_LOCK | SPDIF_BLOCK_BEGIN)
						 
#define SPDIF_ERROR_INT_STATUS	(SPDIF_PARITYO | \
				 SPDIF_TDATA_UNDERR | SPDIF_RDATA_OVRERR)
#define SPDIF_FIFO_INT_STATUS	(SPDIF_FIFO_EMPTY | SPDIF_FIOF_AEMPTY | \
				 SPDIF_FIFO_FULL | SPDIF_FIFO_AFULL)

#define SPDIF_INT_PARITY_ERROR	(-1)
#define SPDIF_INT_TDATA_UNDERR	(-2)
#define SPDIF_INT_RDATA_OVRERR	(-3)
#define SPDIF_INT_FIFO_EMPTY	1
#define SPDIF_INT_FIFO_AEMPTY	2
#define SPDIF_INT_FIFO_FULL	3
#define SPDIF_INT_FIFO_AFULL	4
#define SPDIF_INT_SYNCERR	(-4)
#define SPDIF_INT_LOCK		5	/* reciever has become synchronized with input data stream */
#define SPDIF_INT_BLOCK_BEGIN	6	/* start a new block in recieve data, written into FIFO */

/* FIFO_CTRL */
#define SPDIF_AEMPTY_THRESHOLD	0	/* [depth-1:0] */
#define SPDIF_AFULL_THRESHOLD	16	/* [depth+15:16] */

/* STAT_REG */
#define SPDIF_FIFO_LEVEL	(1<<0)
#define SPDIF_PARITY_FLAG	(1<<21)	/* 1:error; 0:repeated */
#define SPDIF_UNDERR_FLAG	(1<<22)	/* 1:error */
#define SPDIF_OVRERR_FLAG	(1<<23)	/* 1:error */
#define SPDIF_EMPTY_FLAG	(1<<24)	/* 1:fifo empty */
#define SPDIF_AEMPTY_FLAG	(1<<25)	/* 1:fifo almost empty */
#define SPDIF_FULL_FLAG		(1<<26)	/* 1:fifo full */
#define SPDIF_AFULL_FLAG	(1<<27)	/* 1:fifo almost full */
#define SPDIF_SYNCERR_FLAG	(1<<28)	/* 1:rx sync error */
#define SPDIF_LOCK_FLAG		(1<<29)	/* 1:RX sync */
#define SPDIF_BEGIN_FLAG	(1<<30)	/* 1:start a new block */
#define SPDIF_RIGHT_LEFT	(1<<31)	/* 1:left channel received and tx into FIFO; 0:right channel received and tx into FIFO */

#define BIT8TO20MASK 	0x1FFF
#define ALLBITMASK		0xFFFFFFFF

#define SPDIF_STAT		(SPDIF_PARITY_FLAG | SPDIF_UNDERR_FLAG | \
				 SPDIF_OVRERR_FLAG | SPDIF_EMPTY_FLAG | \
				 SPDIF_AEMPTY_FLAG | SPDIF_FULL_FLAG | \
				 SPDIF_AFULL_FLAG | SPDIF_SYNCERR_FLAG | \
				 SPDIF_LOCK_FLAG | SPDIF_BEGIN_FLAG | \
				 SPDIF_RIGHT_LEFT)
struct sf_spdif_dev {
	void __iomem *spdif_base;
	struct regmap *regmap;
	struct device *dev;
	u32 fifo_th;
	int active;

	/* data related to DMA transfers b/w i2s and DMAC */
	struct snd_dmaengine_dai_dma_data play_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;

	bool use_pio;
	struct snd_pcm_substream __rcu *tx_substream;
	struct snd_pcm_substream __rcu *rx_substream;

	unsigned int (*tx_fn)(struct sf_spdif_dev *dev,
			struct snd_pcm_runtime *runtime, unsigned int tx_ptr,
			bool *period_elapsed, snd_pcm_format_t format);
	unsigned int (*rx_fn)(struct sf_spdif_dev *dev,
			struct snd_pcm_runtime *runtime, unsigned int rx_ptr,
			bool *period_elapsed, snd_pcm_format_t format);

	snd_pcm_format_t format;
	unsigned int tx_ptr;
	unsigned int rx_ptr;
	struct clk* spdif_apb;
	struct clk* spdif_core;
	struct clk* apb0_clk;
	struct clk* audio_root;
	struct clk* mclk_inner;
	struct reset_control *rst_apb;

	struct snd_dmaengine_dai_dma_data dma_data;
};

#if IS_ENABLED(CONFIG_SND_STARFIVE_SPDIF_PCM)
void sf_spdif_pcm_push_tx(struct sf_spdif_dev *dev);
void sf_spdif_pcm_pop_rx(struct sf_spdif_dev *dev);
int sf_spdif_pcm_register(struct platform_device *pdev);
#else
void sf_spdif_pcm_push_tx(struct sf_spdif_dev *dev) { }
void sf_spdif_pcm_pop_rx(struct sf_spdif_dev *dev) { }
int sf_spdif_pcm_register(struct platform_device *pdev)
{
	return -EINVAL;
}
#endif


#endif	/* __SND_SOC_STARFIVE_SPDIF_H */

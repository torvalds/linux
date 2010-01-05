/*
 * mxc-ssi.c  --  SSI driver for Freescale IMX
 *
 * Copyright 2006 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood
 *         liam.girdwood@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  Based on mxc-alsa-mc13783 (C) 2006 Freescale.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * TODO:
 *   Need to rework SSI register defs when new defs go into mainline.
 *   Add support for TDM and FIFO 1.
 *   Add support for i.mx3x DMA interface.
 *
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <mach/dma-mx1-mx2.h>
#include <asm/mach-types.h>

#include "mxc-ssi.h"
#include "mx1_mx2-pcm.h"

#define SSI1_PORT	0
#define SSI2_PORT	1

static int ssi_active[2] = {0, 0};

/* DMA information for mx1_mx2 platforms */
static struct mx1_mx2_pcm_dma_params imx_ssi1_pcm_stereo_out0 = {
	.name			= "SSI1 PCM Stereo out 0",
	.transfer_type = DMA_MODE_WRITE,
	.per_address = SSI1_BASE_ADDR + STX0,
	.event_id = DMA_REQ_SSI1_TX0,
	.watermark_level = TXFIFO_WATERMARK,
	.per_config = IMX_DMA_MEMSIZE_16 | IMX_DMA_TYPE_FIFO,
	.mem_config = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
};

static struct mx1_mx2_pcm_dma_params imx_ssi1_pcm_stereo_out1 = {
	.name			= "SSI1 PCM Stereo out 1",
	.transfer_type = DMA_MODE_WRITE,
	.per_address = SSI1_BASE_ADDR + STX1,
	.event_id = DMA_REQ_SSI1_TX1,
	.watermark_level = TXFIFO_WATERMARK,
	.per_config = IMX_DMA_MEMSIZE_16 | IMX_DMA_TYPE_FIFO,
	.mem_config = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
};

static struct mx1_mx2_pcm_dma_params imx_ssi1_pcm_stereo_in0 = {
	.name			= "SSI1 PCM Stereo in 0",
	.transfer_type = DMA_MODE_READ,
	.per_address = SSI1_BASE_ADDR + SRX0,
	.event_id = DMA_REQ_SSI1_RX0,
	.watermark_level = RXFIFO_WATERMARK,
	.per_config = IMX_DMA_MEMSIZE_16 | IMX_DMA_TYPE_FIFO,
	.mem_config = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
};

static struct mx1_mx2_pcm_dma_params imx_ssi1_pcm_stereo_in1 = {
	.name			= "SSI1 PCM Stereo in 1",
	.transfer_type = DMA_MODE_READ,
	.per_address = SSI1_BASE_ADDR + SRX1,
	.event_id = DMA_REQ_SSI1_RX1,
	.watermark_level = RXFIFO_WATERMARK,
	.per_config = IMX_DMA_MEMSIZE_16 | IMX_DMA_TYPE_FIFO,
	.mem_config = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
};

static struct mx1_mx2_pcm_dma_params imx_ssi2_pcm_stereo_out0 = {
	.name			= "SSI2 PCM Stereo out 0",
	.transfer_type = DMA_MODE_WRITE,
	.per_address = SSI2_BASE_ADDR + STX0,
	.event_id = DMA_REQ_SSI2_TX0,
	.watermark_level = TXFIFO_WATERMARK,
	.per_config = IMX_DMA_MEMSIZE_16 | IMX_DMA_TYPE_FIFO,
	.mem_config = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
};

static struct mx1_mx2_pcm_dma_params imx_ssi2_pcm_stereo_out1 = {
	.name			= "SSI2 PCM Stereo out 1",
	.transfer_type = DMA_MODE_WRITE,
	.per_address = SSI2_BASE_ADDR + STX1,
	.event_id = DMA_REQ_SSI2_TX1,
	.watermark_level = TXFIFO_WATERMARK,
	.per_config = IMX_DMA_MEMSIZE_16 | IMX_DMA_TYPE_FIFO,
	.mem_config = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
};

static struct mx1_mx2_pcm_dma_params imx_ssi2_pcm_stereo_in0 = {
	.name			= "SSI2 PCM Stereo in 0",
	.transfer_type = DMA_MODE_READ,
	.per_address = SSI2_BASE_ADDR + SRX0,
	.event_id = DMA_REQ_SSI2_RX0,
	.watermark_level = RXFIFO_WATERMARK,
	.per_config = IMX_DMA_MEMSIZE_16 | IMX_DMA_TYPE_FIFO,
	.mem_config = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
};

static struct mx1_mx2_pcm_dma_params imx_ssi2_pcm_stereo_in1 = {
	.name			= "SSI2 PCM Stereo in 1",
	.transfer_type = DMA_MODE_READ,
	.per_address = SSI2_BASE_ADDR + SRX1,
	.event_id = DMA_REQ_SSI2_RX1,
	.watermark_level = RXFIFO_WATERMARK,
	.per_config = IMX_DMA_MEMSIZE_16 | IMX_DMA_TYPE_FIFO,
	.mem_config = IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
};

static struct clk *ssi_clk0, *ssi_clk1;

int get_ssi_clk(int ssi, struct device *dev)
{
	switch (ssi) {
	case 0:
		ssi_clk0 = clk_get(dev, "ssi1");
		if (IS_ERR(ssi_clk0))
			return PTR_ERR(ssi_clk0);
		return 0;
	case 1:
		ssi_clk1 = clk_get(dev, "ssi2");
		if (IS_ERR(ssi_clk1))
			return PTR_ERR(ssi_clk1);
		return 0;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(get_ssi_clk);

void put_ssi_clk(int ssi)
{
	switch (ssi) {
	case 0:
		clk_put(ssi_clk0);
		ssi_clk0 = NULL;
		break;
	case 1:
		clk_put(ssi_clk1);
		ssi_clk1 = NULL;
		break;
	}
}
EXPORT_SYMBOL(put_ssi_clk);

/*
 * SSI system clock configuration.
 * Should only be called when port is inactive (i.e. SSIEN = 0).
 */
static int imx_ssi_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
	int clk_id, unsigned int freq, int dir)
{
	u32 scr;

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		scr = SSI1_SCR;
		pr_debug("%s: SCR for SSI1 is %x\n", __func__, scr);
	} else {
		scr = SSI2_SCR;
		pr_debug("%s: SCR for SSI2 is %x\n", __func__, scr);
	}

	if (scr & SSI_SCR_SSIEN) {
		printk(KERN_WARNING "Warning ssi already enabled\n");
		return 0;
	}

	switch (clk_id) {
	case IMX_SSP_SYS_CLK:
		if (dir == SND_SOC_CLOCK_OUT) {
			scr |= SSI_SCR_SYS_CLK_EN;
			pr_debug("%s: clk of is output\n", __func__);
		} else {
			scr &= ~SSI_SCR_SYS_CLK_EN;
			pr_debug("%s: clk of is input\n", __func__);
		}
		break;
	default:
		return -EINVAL;
	}

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		pr_debug("%s: writeback of SSI1_SCR\n", __func__);
		SSI1_SCR = scr;
	} else {
		pr_debug("%s: writeback of SSI2_SCR\n", __func__);
		SSI2_SCR = scr;
	}

	return 0;
}

/*
 * SSI Clock dividers
 * Should only be called when port is inactive (i.e. SSIEN = 0).
 */
static int imx_ssi_set_dai_clkdiv(struct snd_soc_dai *cpu_dai,
	int div_id, int div)
{
	u32 stccr, srccr;

	pr_debug("%s\n", __func__);
	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		if (SSI1_SCR & SSI_SCR_SSIEN)
			return 0;
		srccr = SSI1_STCCR;
		stccr = SSI1_STCCR;
	} else {
		if (SSI2_SCR & SSI_SCR_SSIEN)
			return 0;
		srccr = SSI2_STCCR;
		stccr = SSI2_STCCR;
	}

	switch (div_id) {
	case IMX_SSI_TX_DIV_2:
		stccr &= ~SSI_STCCR_DIV2;
		stccr |= div;
		break;
	case IMX_SSI_TX_DIV_PSR:
		stccr &= ~SSI_STCCR_PSR;
		stccr |= div;
		break;
	case IMX_SSI_TX_DIV_PM:
		stccr &= ~0xff;
		stccr |= SSI_STCCR_PM(div);
		break;
	case IMX_SSI_RX_DIV_2:
		stccr &= ~SSI_STCCR_DIV2;
		stccr |= div;
		break;
	case IMX_SSI_RX_DIV_PSR:
		stccr &= ~SSI_STCCR_PSR;
		stccr |= div;
		break;
	case IMX_SSI_RX_DIV_PM:
		stccr &= ~0xff;
		stccr |= SSI_STCCR_PM(div);
		break;
	default:
		return -EINVAL;
	}

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		SSI1_STCCR = stccr;
		SSI1_SRCCR = srccr;
	} else {
		SSI2_STCCR = stccr;
		SSI2_SRCCR = srccr;
	}
	return 0;
}

/*
 * SSI Network Mode or TDM slots configuration.
 * Should only be called when port is inactive (i.e. SSIEN = 0).
 */
static int imx_ssi_set_dai_tdm_slot(struct snd_soc_dai *cpu_dai,
	unsigned int mask, int slots)
{
	u32 stmsk, srmsk, stccr;

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		if (SSI1_SCR & SSI_SCR_SSIEN) {
			printk(KERN_WARNING "Warning ssi already enabled\n");
			return 0;
		}
		stccr = SSI1_STCCR;
	} else {
		if (SSI2_SCR & SSI_SCR_SSIEN) {
			printk(KERN_WARNING "Warning ssi already enabled\n");
			return 0;
		}
		stccr = SSI2_STCCR;
	}

	stmsk = srmsk = mask;
	stccr &= ~SSI_STCCR_DC_MASK;
	stccr |= SSI_STCCR_DC(slots - 1);

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		SSI1_STMSK = stmsk;
		SSI1_SRMSK = srmsk;
		SSI1_SRCCR = SSI1_STCCR = stccr;
	} else {
		SSI2_STMSK = stmsk;
		SSI2_SRMSK = srmsk;
		SSI2_SRCCR = SSI2_STCCR = stccr;
	}

	return 0;
}

/*
 * SSI DAI format configuration.
 * Should only be called when port is inactive (i.e. SSIEN = 0).
 * Note: We don't use the I2S modes but instead manually configure the
 * SSI for I2S.
 */
static int imx_ssi_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	u32 stcr = 0, srcr = 0, scr;

	/*
	 * This is done to avoid this function to modify
	 * previous set values in stcr
	 */
	stcr = SSI1_STCR;

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2)
		scr = SSI1_SCR & ~(SSI_SCR_SYN | SSI_SCR_NET);
	else
		scr = SSI2_SCR & ~(SSI_SCR_SYN | SSI_SCR_NET);

	if (scr & SSI_SCR_SSIEN) {
		printk(KERN_WARNING "Warning ssi already enabled\n");
		return 0;
	}

	/* DAI mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* data on rising edge of bclk, frame low 1clk before data */
		stcr |= SSI_STCR_TFSI | SSI_STCR_TEFS | SSI_STCR_TXBIT0;
		srcr |= SSI_SRCR_RFSI | SSI_SRCR_REFS | SSI_SRCR_RXBIT0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/* data on rising edge of bclk, frame high with data */
		stcr |= SSI_STCR_TXBIT0;
		srcr |= SSI_SRCR_RXBIT0;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		/* data on rising edge of bclk, frame high with data */
		stcr |= SSI_STCR_TFSL;
		srcr |= SSI_SRCR_RFSL;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/* data on rising edge of bclk, frame high 1clk before data */
		stcr |= SSI_STCR_TFSL | SSI_STCR_TEFS;
		srcr |= SSI_SRCR_RFSL | SSI_SRCR_REFS;
		break;
	}

	/* DAI clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		stcr |= SSI_STCR_TFSI;
		stcr &= ~SSI_STCR_TSCKP;
		srcr |= SSI_SRCR_RFSI;
		srcr &= ~SSI_SRCR_RSCKP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		stcr &= ~(SSI_STCR_TSCKP | SSI_STCR_TFSI);
		srcr &= ~(SSI_SRCR_RSCKP | SSI_SRCR_RFSI);
		break;
	case SND_SOC_DAIFMT_NB_IF:
		stcr |= SSI_STCR_TFSI | SSI_STCR_TSCKP;
		srcr |= SSI_SRCR_RFSI | SSI_SRCR_RSCKP;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		stcr &= ~SSI_STCR_TFSI;
		stcr |= SSI_STCR_TSCKP;
		srcr &= ~SSI_SRCR_RFSI;
		srcr |= SSI_SRCR_RSCKP;
		break;
	}

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		stcr |= SSI_STCR_TFDIR | SSI_STCR_TXDIR;
		srcr |= SSI_SRCR_RFDIR | SSI_SRCR_RXDIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		stcr |= SSI_STCR_TFDIR;
		srcr |= SSI_SRCR_RFDIR;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		stcr |= SSI_STCR_TXDIR;
		srcr |= SSI_SRCR_RXDIR;
		break;
	}

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		SSI1_STCR = stcr;
		SSI1_SRCR = srcr;
		SSI1_SCR = scr;
	} else {
		SSI2_STCR = stcr;
		SSI2_SRCR = srcr;
		SSI2_SCR = scr;
	}

	return 0;
}

static int imx_ssi_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* set up TX DMA params */
		switch (cpu_dai->id) {
		case IMX_DAI_SSI0:
			cpu_dai->dma_data = &imx_ssi1_pcm_stereo_out0;
			break;
		case IMX_DAI_SSI1:
			cpu_dai->dma_data = &imx_ssi1_pcm_stereo_out1;
			break;
		case IMX_DAI_SSI2:
			cpu_dai->dma_data = &imx_ssi2_pcm_stereo_out0;
			break;
		case IMX_DAI_SSI3:
			cpu_dai->dma_data = &imx_ssi2_pcm_stereo_out1;
		}
		pr_debug("%s: (playback)\n", __func__);
	} else {
		/* set up RX DMA params */
		switch (cpu_dai->id) {
		case IMX_DAI_SSI0:
			cpu_dai->dma_data = &imx_ssi1_pcm_stereo_in0;
			break;
		case IMX_DAI_SSI1:
			cpu_dai->dma_data = &imx_ssi1_pcm_stereo_in1;
			break;
		case IMX_DAI_SSI2:
			cpu_dai->dma_data = &imx_ssi2_pcm_stereo_in0;
			break;
		case IMX_DAI_SSI3:
			cpu_dai->dma_data = &imx_ssi2_pcm_stereo_in1;
		}
		pr_debug("%s: (capture)\n", __func__);
	}

	/*
	 * we cant really change any SSI values after SSI is enabled
	 * need to fix in software for max flexibility - lrg
	 */
	if (cpu_dai->active) {
		printk(KERN_WARNING "Warning ssi already enabled\n");
		return 0;
	}

	/* reset the SSI port - Sect 45.4.4 */
	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {

		if (!ssi_clk0)
			return -EINVAL;

		if (ssi_active[SSI1_PORT]++) {
			pr_debug("%s: exit before reset\n", __func__);
			return 0;
		}

		/* SSI1 Reset */
		SSI1_SCR = 0;

		SSI1_SFCSR = SSI_SFCSR_RFWM1(RXFIFO_WATERMARK) |
			SSI_SFCSR_RFWM0(RXFIFO_WATERMARK) |
			SSI_SFCSR_TFWM1(TXFIFO_WATERMARK) |
			SSI_SFCSR_TFWM0(TXFIFO_WATERMARK);
	} else {

		if (!ssi_clk1)
			return -EINVAL;

		if (ssi_active[SSI2_PORT]++) {
			pr_debug("%s: exit before reset\n", __func__);
			return 0;
		}

		/* SSI2 Reset */
		SSI2_SCR = 0;

		SSI2_SFCSR = SSI_SFCSR_RFWM1(RXFIFO_WATERMARK) |
			SSI_SFCSR_RFWM0(RXFIFO_WATERMARK) |
			SSI_SFCSR_TFWM1(TXFIFO_WATERMARK) |
			SSI_SFCSR_TFWM0(TXFIFO_WATERMARK);
	}

	return 0;
}

int imx_ssi_hw_tx_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	u32 stccr, stcr, sier;

	pr_debug("%s\n", __func__);

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		stccr = SSI1_STCCR & ~SSI_STCCR_WL_MASK;
		stcr = SSI1_STCR;
		sier = SSI1_SIER;
	} else {
		stccr = SSI2_STCCR & ~SSI_STCCR_WL_MASK;
		stcr = SSI2_STCR;
		sier = SSI2_SIER;
	}

	/* DAI data (word) size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		stccr |= SSI_STCCR_WL(16);
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		stccr |= SSI_STCCR_WL(20);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		stccr |= SSI_STCCR_WL(24);
		break;
	}

	/* enable interrupts */
	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2)
		stcr |= SSI_STCR_TFEN0;
	else
		stcr |= SSI_STCR_TFEN1;
	sier |= SSI_SIER_TDMAE;

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		SSI1_STCR = stcr;
		SSI1_STCCR = stccr;
		SSI1_SIER = sier;
	} else {
		SSI2_STCR = stcr;
		SSI2_STCCR = stccr;
		SSI2_SIER = sier;
	}

	return 0;
}

int imx_ssi_hw_rx_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	u32 srccr, srcr, sier;

	pr_debug("%s\n", __func__);

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		srccr = SSI1_SRCCR & ~SSI_SRCCR_WL_MASK;
		srcr = SSI1_SRCR;
		sier = SSI1_SIER;
	} else {
		srccr = SSI2_SRCCR & ~SSI_SRCCR_WL_MASK;
		srcr = SSI2_SRCR;
		sier = SSI2_SIER;
	}

	/* DAI data (word) size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		srccr |= SSI_SRCCR_WL(16);
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		srccr |= SSI_SRCCR_WL(20);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		srccr |= SSI_SRCCR_WL(24);
		break;
	}

	/* enable interrupts */
	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2)
		srcr |= SSI_SRCR_RFEN0;
	else
		srcr |= SSI_SRCR_RFEN1;
	sier |= SSI_SIER_RDMAE;

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		SSI1_SRCR = srcr;
		SSI1_SRCCR = srccr;
		SSI1_SIER = sier;
	} else {
		SSI2_SRCR = srcr;
		SSI2_SRCCR = srccr;
		SSI2_SIER = sier;
	}

	return 0;
}

/*
 * Should only be called when port is inactive (i.e. SSIEN = 0),
 * although can be called multiple times by upper layers.
 */
int imx_ssi_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	int ret;

	/* cant change any parameters when SSI is running */
	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		if (SSI1_SCR & SSI_SCR_SSIEN) {
			printk(KERN_WARNING "Warning ssi already enabled\n");
			return 0;
		}
	} else {
		if (SSI2_SCR & SSI_SCR_SSIEN) {
			printk(KERN_WARNING "Warning ssi already enabled\n");
			return 0;
		}
	}

	/*
	 * Configure both tx and rx params with the same settings. This is
	 * really a harware restriction because SSI must be disabled until
	 * we can change those values. If there is an active audio stream in
	 * one direction, enabling the other direction with different
	 * settings would mean disturbing the running one.
	 */
	ret = imx_ssi_hw_tx_params(substream, params);
	if (ret < 0)
		return ret;
	return imx_ssi_hw_rx_params(substream, params);
}

int imx_ssi_prepare(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret;

	pr_debug("%s\n", __func__);

	/* Enable clks here to follow SSI recommended init sequence */
	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2) {
		ret = clk_enable(ssi_clk0);
		if (ret < 0)
			printk(KERN_ERR "Unable to enable ssi_clk0\n");
	} else {
		ret = clk_enable(ssi_clk1);
		if (ret < 0)
			printk(KERN_ERR "Unable to enable ssi_clk1\n");
	}

	return 0;
}

static int imx_ssi_trigger(struct snd_pcm_substream *substream, int cmd,
			struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	u32 scr;

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2)
		scr = SSI1_SCR;
	else
		scr = SSI2_SCR;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			scr |= SSI_SCR_TE | SSI_SCR_SSIEN;
		else
			scr |= SSI_SCR_RE | SSI_SCR_SSIEN;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			scr &= ~SSI_SCR_TE;
		else
			scr &= ~SSI_SCR_RE;
		break;
	default:
		return -EINVAL;
	}

	if (cpu_dai->id == IMX_DAI_SSI0 || cpu_dai->id == IMX_DAI_SSI2)
		SSI1_SCR = scr;
	else
		SSI2_SCR = scr;

	return 0;
}

static void imx_ssi_shutdown(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	/* shutdown SSI if neither Tx or Rx is active */
	if (!cpu_dai->active) {

		if (cpu_dai->id == IMX_DAI_SSI0 ||
			cpu_dai->id == IMX_DAI_SSI2) {

			if (--ssi_active[SSI1_PORT] > 1)
				return;

			SSI1_SCR = 0;
			clk_disable(ssi_clk0);
		} else {
			if (--ssi_active[SSI2_PORT])
				return;
			SSI2_SCR = 0;
			clk_disable(ssi_clk1);
		}
	}
}

#ifdef CONFIG_PM
static int imx_ssi_suspend(struct platform_device *dev,
	struct snd_soc_dai *dai)
{
	return 0;
}

static int imx_ssi_resume(struct platform_device *pdev,
	struct snd_soc_dai *dai)
{
	return 0;
}

#else
#define imx_ssi_suspend	NULL
#define imx_ssi_resume	NULL
#endif

#define IMX_SSI_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | \
	SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
	SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | \
	SNDRV_PCM_RATE_96000)

#define IMX_SSI_BITS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops imx_ssi_pcm_dai_ops = {
	.startup = imx_ssi_startup,
	.shutdown = imx_ssi_shutdown,
	.trigger = imx_ssi_trigger,
	.prepare = imx_ssi_prepare,
	.hw_params = imx_ssi_hw_params,
	.set_sysclk = imx_ssi_set_dai_sysclk,
	.set_clkdiv = imx_ssi_set_dai_clkdiv,
	.set_fmt = imx_ssi_set_dai_fmt,
	.set_tdm_slot = imx_ssi_set_dai_tdm_slot,
};

struct snd_soc_dai imx_ssi_pcm_dai[] = {
{
	.name = "imx-i2s-1-0",
	.id = IMX_DAI_SSI0,
	.suspend = imx_ssi_suspend,
	.resume = imx_ssi_resume,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.formats = IMX_SSI_BITS,
		.rates = IMX_SSI_RATES,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.formats = IMX_SSI_BITS,
		.rates = IMX_SSI_RATES,},
	.ops = &imx_ssi_pcm_dai_ops,
},
{
	.name = "imx-i2s-2-0",
	.id = IMX_DAI_SSI1,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.formats = IMX_SSI_BITS,
		.rates = IMX_SSI_RATES,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.formats = IMX_SSI_BITS,
		.rates = IMX_SSI_RATES,},
	.ops = &imx_ssi_pcm_dai_ops,
},
{
	.name = "imx-i2s-1-1",
	.id = IMX_DAI_SSI2,
	.suspend = imx_ssi_suspend,
	.resume = imx_ssi_resume,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.formats = IMX_SSI_BITS,
		.rates = IMX_SSI_RATES,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.formats = IMX_SSI_BITS,
		.rates = IMX_SSI_RATES,},
	.ops = &imx_ssi_pcm_dai_ops,
},
{
	.name = "imx-i2s-2-1",
	.id = IMX_DAI_SSI3,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.formats = IMX_SSI_BITS,
		.rates = IMX_SSI_RATES,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.formats = IMX_SSI_BITS,
		.rates = IMX_SSI_RATES,},
	.ops = &imx_ssi_pcm_dai_ops,
},
};
EXPORT_SYMBOL_GPL(imx_ssi_pcm_dai);

static int __init imx_ssi_init(void)
{
	return snd_soc_register_dais(imx_ssi_pcm_dai,
				ARRAY_SIZE(imx_ssi_pcm_dai));
}

static void __exit imx_ssi_exit(void)
{
	snd_soc_unregister_dais(imx_ssi_pcm_dai,
				ARRAY_SIZE(imx_ssi_pcm_dai));
}

module_init(imx_ssi_init);
module_exit(imx_ssi_exit);
MODULE_AUTHOR("Liam Girdwood, liam.girdwood@wolfsonmicro.com");
MODULE_DESCRIPTION("i.MX ASoC I2S driver");
MODULE_LICENSE("GPL");

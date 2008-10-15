/* sound/soc/at32/at32-ssc.c
 * ASoC platform driver for AT32 using SSC as DAI
 *
 * Copyright (C) 2008 Long Range Systems
 *    Geoffrey Wossum <gwossum@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Note that this is basically a port of the sound/soc/at91-ssc.c to
 * the AVR32 kernel.  Thanks to Frank Mandarino for that code.
 */

/* #define DEBUG */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/atmel_pdc.h>
#include <linux/atmel-ssc.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "at32-pcm.h"
#include "at32-ssc.h"



/*-------------------------------------------------------------------------*\
 * Constants
\*-------------------------------------------------------------------------*/
#define NUM_SSC_DEVICES		3

/*
 * SSC direction masks
 */
#define SSC_DIR_MASK_UNUSED	0
#define SSC_DIR_MASK_PLAYBACK	1
#define SSC_DIR_MASK_CAPTURE	2

/*
 * SSC register values that Atmel left out of <linux/atmel-ssc.h>.  These
 * are expected to be used with SSC_BF
 */
/* START bit field values */
#define SSC_START_CONTINUOUS	0
#define SSC_START_TX_RX		1
#define SSC_START_LOW_RF	2
#define SSC_START_HIGH_RF	3
#define SSC_START_FALLING_RF	4
#define SSC_START_RISING_RF	5
#define SSC_START_LEVEL_RF	6
#define SSC_START_EDGE_RF	7
#define SSS_START_COMPARE_0	8

/* CKI bit field values */
#define SSC_CKI_FALLING		0
#define SSC_CKI_RISING		1

/* CKO bit field values */
#define SSC_CKO_NONE		0
#define SSC_CKO_CONTINUOUS	1
#define SSC_CKO_TRANSFER	2

/* CKS bit field values */
#define SSC_CKS_DIV		0
#define SSC_CKS_CLOCK		1
#define SSC_CKS_PIN		2

/* FSEDGE bit field values */
#define SSC_FSEDGE_POSITIVE	0
#define SSC_FSEDGE_NEGATIVE	1

/* FSOS bit field values */
#define SSC_FSOS_NONE		0
#define SSC_FSOS_NEGATIVE	1
#define SSC_FSOS_POSITIVE	2
#define SSC_FSOS_LOW		3
#define SSC_FSOS_HIGH		4
#define SSC_FSOS_TOGGLE		5

#define START_DELAY		1



/*-------------------------------------------------------------------------*\
 * Module data
\*-------------------------------------------------------------------------*/
/*
 * SSC PDC registered required by the PCM DMA engine
 */
static struct at32_pdc_regs pdc_tx_reg = {
	.xpr = SSC_PDC_TPR,
	.xcr = SSC_PDC_TCR,
	.xnpr = SSC_PDC_TNPR,
	.xncr = SSC_PDC_TNCR,
};



static struct at32_pdc_regs pdc_rx_reg = {
	.xpr = SSC_PDC_RPR,
	.xcr = SSC_PDC_RCR,
	.xnpr = SSC_PDC_RNPR,
	.xncr = SSC_PDC_RNCR,
};



/*
 * SSC and PDC status bits for transmit and receive
 */
static struct at32_ssc_mask ssc_tx_mask = {
	.ssc_enable = SSC_BIT(CR_TXEN),
	.ssc_disable = SSC_BIT(CR_TXDIS),
	.ssc_endx = SSC_BIT(SR_ENDTX),
	.ssc_endbuf = SSC_BIT(SR_TXBUFE),
	.pdc_enable = SSC_BIT(PDC_PTCR_TXTEN),
	.pdc_disable = SSC_BIT(PDC_PTCR_TXTDIS),
};



static struct at32_ssc_mask ssc_rx_mask = {
	.ssc_enable = SSC_BIT(CR_RXEN),
	.ssc_disable = SSC_BIT(CR_RXDIS),
	.ssc_endx = SSC_BIT(SR_ENDRX),
	.ssc_endbuf = SSC_BIT(SR_RXBUFF),
	.pdc_enable = SSC_BIT(PDC_PTCR_RXTEN),
	.pdc_disable = SSC_BIT(PDC_PTCR_RXTDIS),
};



/*
 * DMA parameters for each SSC
 */
static struct at32_pcm_dma_params ssc_dma_params[NUM_SSC_DEVICES][2] = {
	{
	 {
	  .name = "SSC0 PCM out",
	  .pdc = &pdc_tx_reg,
	  .mask = &ssc_tx_mask,
	  },
	 {
	  .name = "SSC0 PCM in",
	  .pdc = &pdc_rx_reg,
	  .mask = &ssc_rx_mask,
	  },
	 },
	{
	 {
	  .name = "SSC1 PCM out",
	  .pdc = &pdc_tx_reg,
	  .mask = &ssc_tx_mask,
	  },
	 {
	  .name = "SSC1 PCM in",
	  .pdc = &pdc_rx_reg,
	  .mask = &ssc_rx_mask,
	  },
	 },
	{
	 {
	  .name = "SSC2 PCM out",
	  .pdc = &pdc_tx_reg,
	  .mask = &ssc_tx_mask,
	  },
	 {
	  .name = "SSC2 PCM in",
	  .pdc = &pdc_rx_reg,
	  .mask = &ssc_rx_mask,
	  },
	 },
};



static struct at32_ssc_info ssc_info[NUM_SSC_DEVICES] = {
	{
	 .name = "ssc0",
	 .lock = __SPIN_LOCK_UNLOCKED(ssc_info[0].lock),
	 .dir_mask = SSC_DIR_MASK_UNUSED,
	 .initialized = 0,
	 },
	{
	 .name = "ssc1",
	 .lock = __SPIN_LOCK_UNLOCKED(ssc_info[1].lock),
	 .dir_mask = SSC_DIR_MASK_UNUSED,
	 .initialized = 0,
	 },
	{
	 .name = "ssc2",
	 .lock = __SPIN_LOCK_UNLOCKED(ssc_info[2].lock),
	 .dir_mask = SSC_DIR_MASK_UNUSED,
	 .initialized = 0,
	 },
};




/*-------------------------------------------------------------------------*\
 * ISR
\*-------------------------------------------------------------------------*/
/*
 * SSC interrupt handler.  Passes PDC interrupts to the DMA interrupt
 * handler in the PCM driver.
 */
static irqreturn_t at32_ssc_interrupt(int irq, void *dev_id)
{
	struct at32_ssc_info *ssc_p = dev_id;
	struct at32_pcm_dma_params *dma_params;
	u32 ssc_sr;
	u32 ssc_substream_mask;
	int i;

	ssc_sr = (ssc_readl(ssc_p->ssc->regs, SR) &
		  ssc_readl(ssc_p->ssc->regs, IMR));

	/*
	 * Loop through substreams attached to this SSC.  If a DMA-related
	 * interrupt occured on that substream, call the DMA interrupt
	 * handler function, if one has been registered in the dma_param
	 * structure by the PCM driver.
	 */
	for (i = 0; i < ARRAY_SIZE(ssc_p->dma_params); i++) {
		dma_params = ssc_p->dma_params[i];

		if ((dma_params != NULL) &&
		    (dma_params->dma_intr_handler != NULL)) {
			ssc_substream_mask = (dma_params->mask->ssc_endx |
					      dma_params->mask->ssc_endbuf);
			if (ssc_sr & ssc_substream_mask) {
				dma_params->dma_intr_handler(ssc_sr,
							     dma_params->
							     substream);
			}
		}
	}


	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*\
 * DAI functions
\*-------------------------------------------------------------------------*/
/*
 * Startup.  Only that one substream allowed in each direction.
 */
static int at32_ssc_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct at32_ssc_info *ssc_p = &ssc_info[rtd->dai->cpu_dai->id];
	int dir_mask;

	dir_mask = ((substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		    SSC_DIR_MASK_PLAYBACK : SSC_DIR_MASK_CAPTURE);

	spin_lock_irq(&ssc_p->lock);
	if (ssc_p->dir_mask & dir_mask) {
		spin_unlock_irq(&ssc_p->lock);
		return -EBUSY;
	}
	ssc_p->dir_mask |= dir_mask;
	spin_unlock_irq(&ssc_p->lock);

	return 0;
}



/*
 * Shutdown.  Clear DMA parameters and shutdown the SSC if there
 * are no other substreams open.
 */
static void at32_ssc_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct at32_ssc_info *ssc_p = &ssc_info[rtd->dai->cpu_dai->id];
	struct at32_pcm_dma_params *dma_params;
	int dir_mask;

	dma_params = ssc_p->dma_params[substream->stream];

	if (dma_params != NULL) {
		ssc_writel(dma_params->ssc->regs, CR,
			   dma_params->mask->ssc_disable);
		pr_debug("%s disabled SSC_SR=0x%08x\n",
			 (substream->stream ? "receiver" : "transmit"),
			 ssc_readl(ssc_p->ssc->regs, SR));

		dma_params->ssc = NULL;
		dma_params->substream = NULL;
		ssc_p->dma_params[substream->stream] = NULL;
	}


	dir_mask = 1 << substream->stream;
	spin_lock_irq(&ssc_p->lock);
	ssc_p->dir_mask &= ~dir_mask;
	if (!ssc_p->dir_mask) {
		/* Shutdown the SSC clock */
		pr_debug("at32-ssc: Stopping user %d clock\n",
			 ssc_p->ssc->user);
		clk_disable(ssc_p->ssc->clk);

		if (ssc_p->initialized) {
			free_irq(ssc_p->ssc->irq, ssc_p);
			ssc_p->initialized = 0;
		}

		/* Reset the SSC */
		ssc_writel(ssc_p->ssc->regs, CR, SSC_BIT(CR_SWRST));

		/* clear the SSC dividers */
		ssc_p->cmr_div = 0;
		ssc_p->tcmr_period = 0;
		ssc_p->rcmr_period = 0;
	}
	spin_unlock_irq(&ssc_p->lock);
}



/*
 * Set the SSC system clock rate
 */
static int at32_ssc_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
				   int clk_id, unsigned int freq, int dir)
{
	/* TODO: What the heck do I do here? */
	return 0;
}



/*
 * Record DAI format for use by hw_params()
 */
static int at32_ssc_set_dai_fmt(struct snd_soc_dai *cpu_dai,
				unsigned int fmt)
{
	struct at32_ssc_info *ssc_p = &ssc_info[cpu_dai->id];

	ssc_p->daifmt = fmt;
	return 0;
}



/*
 * Record SSC clock dividers for use in hw_params()
 */
static int at32_ssc_set_dai_clkdiv(struct snd_soc_dai *cpu_dai,
				   int div_id, int div)
{
	struct at32_ssc_info *ssc_p = &ssc_info[cpu_dai->id];

	switch (div_id) {
	case AT32_SSC_CMR_DIV:
		/*
		 * The same master clock divider is used for both
		 * transmit and receive, so if a value has already
		 * been set, it must match this value
		 */
		if (ssc_p->cmr_div == 0)
			ssc_p->cmr_div = div;
		else if (div != ssc_p->cmr_div)
			return -EBUSY;
		break;

	case AT32_SSC_TCMR_PERIOD:
		ssc_p->tcmr_period = div;
		break;

	case AT32_SSC_RCMR_PERIOD:
		ssc_p->rcmr_period = div;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}



/*
 * Configure the SSC
 */
static int at32_ssc_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int id = rtd->dai->cpu_dai->id;
	struct at32_ssc_info *ssc_p = &ssc_info[id];
	struct at32_pcm_dma_params *dma_params;
	int channels, bits;
	u32 tfmr, rfmr, tcmr, rcmr;
	int start_event;
	int ret;


	/*
	 * Currently, there is only one set of dma_params for each direction.
	 * If more are added, this code will have to be changed to select
	 * the proper set
	 */
	dma_params = &ssc_dma_params[id][substream->stream];
	dma_params->ssc = ssc_p->ssc;
	dma_params->substream = substream;

	ssc_p->dma_params[substream->stream] = dma_params;


	/*
	 * The cpu_dai->dma_data field is only used to communicate the
	 * appropriate DMA parameters to the PCM driver's hw_params()
	 * function.  It should not be used for other purposes as it
	 * is common to all substreams.
	 */
	rtd->dai->cpu_dai->dma_data = dma_params;

	channels = params_channels(params);


	/*
	 * Determine sample size in bits and the PDC increment
	 */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		bits = 8;
		dma_params->pdc_xfer_size = 1;
		break;

	case SNDRV_PCM_FORMAT_S16:
		bits = 16;
		dma_params->pdc_xfer_size = 2;
		break;

	case SNDRV_PCM_FORMAT_S24:
		bits = 24;
		dma_params->pdc_xfer_size = 4;
		break;

	case SNDRV_PCM_FORMAT_S32:
		bits = 32;
		dma_params->pdc_xfer_size = 4;
		break;

	default:
		pr_warning("at32-ssc: Unsupported PCM format %d",
			   params_format(params));
		return -EINVAL;
	}
	pr_debug("at32-ssc: bits = %d, pdc_xfer_size = %d, channels = %d\n",
		 bits, dma_params->pdc_xfer_size, channels);


	/*
	 * The SSC only supports up to 16-bit samples in I2S format, due
	 * to the size of the Frame Mode Register FSLEN field.
	 */
	if ((ssc_p->daifmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_I2S)
		if (bits > 16) {
			pr_warning("at32-ssc: "
				   "sample size %d is too large for I2S\n",
				   bits);
			return -EINVAL;
		}


	/*
	 * Compute the SSC register settings
	 */
	switch (ssc_p->daifmt & (SND_SOC_DAIFMT_FORMAT_MASK |
				 SND_SOC_DAIFMT_MASTER_MASK)) {
	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS:
		/*
		 * I2S format, SSC provides BCLK and LRS clocks.
		 *
		 * The SSC transmit and receive clocks are generated from the
		 * MCK divider, and the BCLK signal is output on the SSC TK line
		 */
		pr_debug("at32-ssc: SSC mode is I2S BCLK / FRAME master\n");
		rcmr = (SSC_BF(RCMR_PERIOD, ssc_p->rcmr_period) |
			SSC_BF(RCMR_STTDLY, START_DELAY) |
			SSC_BF(RCMR_START, SSC_START_FALLING_RF) |
			SSC_BF(RCMR_CKI, SSC_CKI_RISING) |
			SSC_BF(RCMR_CKO, SSC_CKO_NONE) |
			SSC_BF(RCMR_CKS, SSC_CKS_DIV));

		rfmr = (SSC_BF(RFMR_FSEDGE, SSC_FSEDGE_POSITIVE) |
			SSC_BF(RFMR_FSOS, SSC_FSOS_NEGATIVE) |
			SSC_BF(RFMR_FSLEN, bits - 1) |
			SSC_BF(RFMR_DATNB, channels - 1) |
			SSC_BIT(RFMR_MSBF) | SSC_BF(RFMR_DATLEN, bits - 1));

		tcmr = (SSC_BF(TCMR_PERIOD, ssc_p->tcmr_period) |
			SSC_BF(TCMR_STTDLY, START_DELAY) |
			SSC_BF(TCMR_START, SSC_START_FALLING_RF) |
			SSC_BF(TCMR_CKI, SSC_CKI_FALLING) |
			SSC_BF(TCMR_CKO, SSC_CKO_CONTINUOUS) |
			SSC_BF(TCMR_CKS, SSC_CKS_DIV));

		tfmr = (SSC_BF(TFMR_FSEDGE, SSC_FSEDGE_POSITIVE) |
			SSC_BF(TFMR_FSOS, SSC_FSOS_NEGATIVE) |
			SSC_BF(TFMR_FSLEN, bits - 1) |
			SSC_BF(TFMR_DATNB, channels - 1) | SSC_BIT(TFMR_MSBF) |
			SSC_BF(TFMR_DATLEN, bits - 1));
		break;


	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM:
		/*
		 * I2S format, CODEC supplies BCLK and LRC clock.
		 *
		 * The SSC transmit clock is obtained from the BCLK signal
		 * on the TK line, and the SSC receive clock is generated from
		 * the transmit clock.
		 *
		 * For single channel data, one sample is transferred on the
		 * falling edge of the LRC clock.  For two channel data, one
		 * sample is transferred on both edges of the LRC clock.
		 */
		pr_debug("at32-ssc: SSC mode is I2S BCLK / FRAME slave\n");
		start_event = ((channels == 1) ?
			       SSC_START_FALLING_RF : SSC_START_EDGE_RF);

		rcmr = (SSC_BF(RCMR_STTDLY, START_DELAY) |
			SSC_BF(RCMR_START, start_event) |
			SSC_BF(RCMR_CKI, SSC_CKI_RISING) |
			SSC_BF(RCMR_CKO, SSC_CKO_NONE) |
			SSC_BF(RCMR_CKS, SSC_CKS_CLOCK));

		rfmr = (SSC_BF(RFMR_FSEDGE, SSC_FSEDGE_POSITIVE) |
			SSC_BF(RFMR_FSOS, SSC_FSOS_NONE) |
			SSC_BIT(RFMR_MSBF) | SSC_BF(RFMR_DATLEN, bits - 1));

		tcmr = (SSC_BF(TCMR_STTDLY, START_DELAY) |
			SSC_BF(TCMR_START, start_event) |
			SSC_BF(TCMR_CKI, SSC_CKI_FALLING) |
			SSC_BF(TCMR_CKO, SSC_CKO_NONE) |
			SSC_BF(TCMR_CKS, SSC_CKS_PIN));

		tfmr = (SSC_BF(TFMR_FSEDGE, SSC_FSEDGE_POSITIVE) |
			SSC_BF(TFMR_FSOS, SSC_FSOS_NONE) |
			SSC_BIT(TFMR_MSBF) | SSC_BF(TFMR_DATLEN, bits - 1));
		break;


	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBS_CFS:
		/*
		 * DSP/PCM Mode A format, SSC provides BCLK and LRC clocks.
		 *
		 * The SSC transmit and receive clocks are generated from the
		 * MCK divider, and the BCLK signal is output on the SSC TK line
		 */
		pr_debug("at32-ssc: SSC mode is DSP A BCLK / FRAME master\n");
		rcmr = (SSC_BF(RCMR_PERIOD, ssc_p->rcmr_period) |
			SSC_BF(RCMR_STTDLY, 1) |
			SSC_BF(RCMR_START, SSC_START_RISING_RF) |
			SSC_BF(RCMR_CKI, SSC_CKI_RISING) |
			SSC_BF(RCMR_CKO, SSC_CKO_NONE) |
			SSC_BF(RCMR_CKS, SSC_CKS_DIV));

		rfmr = (SSC_BF(RFMR_FSEDGE, SSC_FSEDGE_POSITIVE) |
			SSC_BF(RFMR_FSOS, SSC_FSOS_POSITIVE) |
			SSC_BF(RFMR_DATNB, channels - 1) |
			SSC_BIT(RFMR_MSBF) | SSC_BF(RFMR_DATLEN, bits - 1));

		tcmr = (SSC_BF(TCMR_PERIOD, ssc_p->tcmr_period) |
			SSC_BF(TCMR_STTDLY, 1) |
			SSC_BF(TCMR_START, SSC_START_RISING_RF) |
			SSC_BF(TCMR_CKI, SSC_CKI_RISING) |
			SSC_BF(TCMR_CKO, SSC_CKO_CONTINUOUS) |
			SSC_BF(TCMR_CKS, SSC_CKS_DIV));

		tfmr = (SSC_BF(TFMR_FSEDGE, SSC_FSEDGE_POSITIVE) |
			SSC_BF(TFMR_FSOS, SSC_FSOS_POSITIVE) |
			SSC_BF(TFMR_DATNB, channels - 1) |
			SSC_BIT(TFMR_MSBF) | SSC_BF(TFMR_DATLEN, bits - 1));
		break;


	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBM_CFM:
	default:
		pr_warning("at32-ssc: unsupported DAI format 0x%x\n",
			   ssc_p->daifmt);
		return -EINVAL;
		break;
	}
	pr_debug("at32-ssc: RCMR=%08x RFMR=%08x TCMR=%08x TFMR=%08x\n",
		 rcmr, rfmr, tcmr, tfmr);


	if (!ssc_p->initialized) {
		/* enable peripheral clock */
		pr_debug("at32-ssc: Starting clock\n");
		clk_enable(ssc_p->ssc->clk);

		/* Reset the SSC and its PDC registers */
		ssc_writel(ssc_p->ssc->regs, CR, SSC_BIT(CR_SWRST));

		ssc_writel(ssc_p->ssc->regs, PDC_RPR, 0);
		ssc_writel(ssc_p->ssc->regs, PDC_RCR, 0);
		ssc_writel(ssc_p->ssc->regs, PDC_RNPR, 0);
		ssc_writel(ssc_p->ssc->regs, PDC_RNCR, 0);

		ssc_writel(ssc_p->ssc->regs, PDC_TPR, 0);
		ssc_writel(ssc_p->ssc->regs, PDC_TCR, 0);
		ssc_writel(ssc_p->ssc->regs, PDC_TNPR, 0);
		ssc_writel(ssc_p->ssc->regs, PDC_TNCR, 0);

		ret = request_irq(ssc_p->ssc->irq, at32_ssc_interrupt, 0,
				  ssc_p->name, ssc_p);
		if (ret < 0) {
			pr_warning("at32-ssc: request irq failed (%d)\n", ret);
			pr_debug("at32-ssc: Stopping clock\n");
			clk_disable(ssc_p->ssc->clk);
			return ret;
		}

		ssc_p->initialized = 1;
	}

	/* Set SSC clock mode register */
	ssc_writel(ssc_p->ssc->regs, CMR, ssc_p->cmr_div);

	/* set receive clock mode and format */
	ssc_writel(ssc_p->ssc->regs, RCMR, rcmr);
	ssc_writel(ssc_p->ssc->regs, RFMR, rfmr);

	/* set transmit clock mode and format */
	ssc_writel(ssc_p->ssc->regs, TCMR, tcmr);
	ssc_writel(ssc_p->ssc->regs, TFMR, tfmr);

	pr_debug("at32-ssc: SSC initialized\n");
	return 0;
}



static int at32_ssc_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct at32_ssc_info *ssc_p = &ssc_info[rtd->dai->cpu_dai->id];
	struct at32_pcm_dma_params *dma_params;

	dma_params = ssc_p->dma_params[substream->stream];

	ssc_writel(dma_params->ssc->regs, CR, dma_params->mask->ssc_enable);

	return 0;
}



#ifdef CONFIG_PM
static int at32_ssc_suspend(struct platform_device *pdev,
			    struct snd_soc_dai *cpu_dai)
{
	struct at32_ssc_info *ssc_p;

	if (!cpu_dai->active)
		return 0;

	ssc_p = &ssc_info[cpu_dai->id];

	/* Save the status register before disabling transmit and receive */
	ssc_p->ssc_state.ssc_sr = ssc_readl(ssc_p->ssc->regs, SR);
	ssc_writel(ssc_p->ssc->regs, CR, SSC_BIT(CR_TXDIS) | SSC_BIT(CR_RXDIS));

	/* Save the current interrupt mask, then disable unmasked interrupts */
	ssc_p->ssc_state.ssc_imr = ssc_readl(ssc_p->ssc->regs, IMR);
	ssc_writel(ssc_p->ssc->regs, IDR, ssc_p->ssc_state.ssc_imr);

	ssc_p->ssc_state.ssc_cmr = ssc_readl(ssc_p->ssc->regs, CMR);
	ssc_p->ssc_state.ssc_rcmr = ssc_readl(ssc_p->ssc->regs, RCMR);
	ssc_p->ssc_state.ssc_rfmr = ssc_readl(ssc_p->ssc->regs, RFMR);
	ssc_p->ssc_state.ssc_tcmr = ssc_readl(ssc_p->ssc->regs, TCMR);
	ssc_p->ssc_state.ssc_tfmr = ssc_readl(ssc_p->ssc->regs, TFMR);

	return 0;
}



static int at32_ssc_resume(struct platform_device *pdev,
			   struct snd_soc_dai *cpu_dai)
{
	struct at32_ssc_info *ssc_p;
	u32 cr;

	if (!cpu_dai->active)
		return 0;

	ssc_p = &ssc_info[cpu_dai->id];

	/* restore SSC register settings */
	ssc_writel(ssc_p->ssc->regs, TFMR, ssc_p->ssc_state.ssc_tfmr);
	ssc_writel(ssc_p->ssc->regs, TCMR, ssc_p->ssc_state.ssc_tcmr);
	ssc_writel(ssc_p->ssc->regs, RFMR, ssc_p->ssc_state.ssc_rfmr);
	ssc_writel(ssc_p->ssc->regs, RCMR, ssc_p->ssc_state.ssc_rcmr);
	ssc_writel(ssc_p->ssc->regs, CMR, ssc_p->ssc_state.ssc_cmr);

	/* re-enable interrupts */
	ssc_writel(ssc_p->ssc->regs, IER, ssc_p->ssc_state.ssc_imr);

	/* Re-enable recieve and transmit as appropriate */
	cr = 0;
	cr |=
	    (ssc_p->ssc_state.ssc_sr & SSC_BIT(SR_RXEN)) ? SSC_BIT(CR_RXEN) : 0;
	cr |=
	    (ssc_p->ssc_state.ssc_sr & SSC_BIT(SR_TXEN)) ? SSC_BIT(CR_TXEN) : 0;
	ssc_writel(ssc_p->ssc->regs, CR, cr);

	return 0;
}
#else /* CONFIG_PM */
#  define at32_ssc_suspend	NULL
#  define at32_ssc_resume	NULL
#endif /* CONFIG_PM */


#define AT32_SSC_RATES \
    (SNDRV_PCM_RATE_8000  | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
     SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
     SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)


#define AT32_SSC_FORMATS \
    (SNDRV_PCM_FMTBIT_S8  | SNDRV_PCM_FMTBIT_S16 | \
     SNDRV_PCM_FMTBIT_S24 | SNDRV_PCM_FMTBIT_S32)


struct snd_soc_dai at32_ssc_dai[NUM_SSC_DEVICES] = {
	{
	 .name = "at32-ssc0",
	 .id = 0,
	 .type = SND_SOC_DAI_PCM,
	 .suspend = at32_ssc_suspend,
	 .resume = at32_ssc_resume,
	 .playback = {
		      .channels_min = 1,
		      .channels_max = 2,
		      .rates = AT32_SSC_RATES,
		      .formats = AT32_SSC_FORMATS,
		      },
	 .capture = {
		     .channels_min = 1,
		     .channels_max = 2,
		     .rates = AT32_SSC_RATES,
		     .formats = AT32_SSC_FORMATS,
		     },
	 .ops = {
		 .startup = at32_ssc_startup,
		 .shutdown = at32_ssc_shutdown,
		 .prepare = at32_ssc_prepare,
		 .hw_params = at32_ssc_hw_params,
		 },
	 .dai_ops = {
		     .set_sysclk = at32_ssc_set_dai_sysclk,
		     .set_fmt = at32_ssc_set_dai_fmt,
		     .set_clkdiv = at32_ssc_set_dai_clkdiv,
		     },
	 .private_data = &ssc_info[0],
	 },
	{
	 .name = "at32-ssc1",
	 .id = 1,
	 .type = SND_SOC_DAI_PCM,
	 .suspend = at32_ssc_suspend,
	 .resume = at32_ssc_resume,
	 .playback = {
		      .channels_min = 1,
		      .channels_max = 2,
		      .rates = AT32_SSC_RATES,
		      .formats = AT32_SSC_FORMATS,
		      },
	 .capture = {
		     .channels_min = 1,
		     .channels_max = 2,
		     .rates = AT32_SSC_RATES,
		     .formats = AT32_SSC_FORMATS,
		     },
	 .ops = {
		 .startup = at32_ssc_startup,
		 .shutdown = at32_ssc_shutdown,
		 .prepare = at32_ssc_prepare,
		 .hw_params = at32_ssc_hw_params,
		 },
	 .dai_ops = {
		     .set_sysclk = at32_ssc_set_dai_sysclk,
		     .set_fmt = at32_ssc_set_dai_fmt,
		     .set_clkdiv = at32_ssc_set_dai_clkdiv,
		     },
	 .private_data = &ssc_info[1],
	 },
	{
	 .name = "at32-ssc2",
	 .id = 2,
	 .type = SND_SOC_DAI_PCM,
	 .suspend = at32_ssc_suspend,
	 .resume = at32_ssc_resume,
	 .playback = {
		      .channels_min = 1,
		      .channels_max = 2,
		      .rates = AT32_SSC_RATES,
		      .formats = AT32_SSC_FORMATS,
		      },
	 .capture = {
		     .channels_min = 1,
		     .channels_max = 2,
		     .rates = AT32_SSC_RATES,
		     .formats = AT32_SSC_FORMATS,
		     },
	 .ops = {
		 .startup = at32_ssc_startup,
		 .shutdown = at32_ssc_shutdown,
		 .prepare = at32_ssc_prepare,
		 .hw_params = at32_ssc_hw_params,
		 },
	 .dai_ops = {
		     .set_sysclk = at32_ssc_set_dai_sysclk,
		     .set_fmt = at32_ssc_set_dai_fmt,
		     .set_clkdiv = at32_ssc_set_dai_clkdiv,
		     },
	 .private_data = &ssc_info[2],
	 },
};
EXPORT_SYMBOL_GPL(at32_ssc_dai);


MODULE_AUTHOR("Geoffrey Wossum <gwossum@acm.org>");
MODULE_DESCRIPTION("AT32 SSC ASoC Interface");
MODULE_LICENSE("GPL");

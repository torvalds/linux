/*
 * atmel_ssc_dai.c  --  ALSA SoC ATMEL SSC Audio Layer Platform driver
 *
 * Copyright (C) 2005 SAN People
 * Copyright (C) 2008 Atmel
 *
 * Author: Sedji Gaouaou <sedji.gaouaou@atmel.com>
 *         ATMEL CORP.
 *
 * Based on at91-ssc.c by
 * Frank Mandarino <fmandarino@endrelia.com>
 * Based on pxa2xx Platform drivers by
 * Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/atmel_pdc.h>

#include <linux/atmel-ssc.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "atmel-pcm.h"
#include "atmel_ssc_dai.h"


#define NUM_SSC_DEVICES		3

/*
 * SSC PDC registers required by the PCM DMA engine.
 */
static struct atmel_pdc_regs pdc_tx_reg = {
	.xpr		= ATMEL_PDC_TPR,
	.xcr		= ATMEL_PDC_TCR,
	.xnpr		= ATMEL_PDC_TNPR,
	.xncr		= ATMEL_PDC_TNCR,
};

static struct atmel_pdc_regs pdc_rx_reg = {
	.xpr		= ATMEL_PDC_RPR,
	.xcr		= ATMEL_PDC_RCR,
	.xnpr		= ATMEL_PDC_RNPR,
	.xncr		= ATMEL_PDC_RNCR,
};

/*
 * SSC & PDC status bits for transmit and receive.
 */
static struct atmel_ssc_mask ssc_tx_mask = {
	.ssc_enable	= SSC_BIT(CR_TXEN),
	.ssc_disable	= SSC_BIT(CR_TXDIS),
	.ssc_endx	= SSC_BIT(SR_ENDTX),
	.ssc_endbuf	= SSC_BIT(SR_TXBUFE),
	.pdc_enable	= ATMEL_PDC_TXTEN,
	.pdc_disable	= ATMEL_PDC_TXTDIS,
};

static struct atmel_ssc_mask ssc_rx_mask = {
	.ssc_enable	= SSC_BIT(CR_RXEN),
	.ssc_disable	= SSC_BIT(CR_RXDIS),
	.ssc_endx	= SSC_BIT(SR_ENDRX),
	.ssc_endbuf	= SSC_BIT(SR_RXBUFF),
	.pdc_enable	= ATMEL_PDC_RXTEN,
	.pdc_disable	= ATMEL_PDC_RXTDIS,
};


/*
 * DMA parameters.
 */
static struct atmel_pcm_dma_params ssc_dma_params[NUM_SSC_DEVICES][2] = {
	{{
	.name		= "SSC0 PCM out",
	.pdc		= &pdc_tx_reg,
	.mask		= &ssc_tx_mask,
	},
	{
	.name		= "SSC0 PCM in",
	.pdc		= &pdc_rx_reg,
	.mask		= &ssc_rx_mask,
	} },
	{{
	.name		= "SSC1 PCM out",
	.pdc		= &pdc_tx_reg,
	.mask		= &ssc_tx_mask,
	},
	{
	.name		= "SSC1 PCM in",
	.pdc		= &pdc_rx_reg,
	.mask		= &ssc_rx_mask,
	} },
	{{
	.name		= "SSC2 PCM out",
	.pdc		= &pdc_tx_reg,
	.mask		= &ssc_tx_mask,
	},
	{
	.name		= "SSC2 PCM in",
	.pdc		= &pdc_rx_reg,
	.mask		= &ssc_rx_mask,
	} },
};


static struct atmel_ssc_info ssc_info[NUM_SSC_DEVICES] = {
	{
	.name		= "ssc0",
	.lock		= __SPIN_LOCK_UNLOCKED(ssc_info[0].lock),
	.dir_mask	= SSC_DIR_MASK_UNUSED,
	.initialized	= 0,
	},
	{
	.name		= "ssc1",
	.lock		= __SPIN_LOCK_UNLOCKED(ssc_info[1].lock),
	.dir_mask	= SSC_DIR_MASK_UNUSED,
	.initialized	= 0,
	},
	{
	.name		= "ssc2",
	.lock		= __SPIN_LOCK_UNLOCKED(ssc_info[2].lock),
	.dir_mask	= SSC_DIR_MASK_UNUSED,
	.initialized	= 0,
	},
};


/*
 * SSC interrupt handler.  Passes PDC interrupts to the DMA
 * interrupt handler in the PCM driver.
 */
static irqreturn_t atmel_ssc_interrupt(int irq, void *dev_id)
{
	struct atmel_ssc_info *ssc_p = dev_id;
	struct atmel_pcm_dma_params *dma_params;
	u32 ssc_sr;
	u32 ssc_substream_mask;
	int i;

	ssc_sr = (unsigned long)ssc_readl(ssc_p->ssc->regs, SR)
			& (unsigned long)ssc_readl(ssc_p->ssc->regs, IMR);

	/*
	 * Loop through the substreams attached to this SSC.  If
	 * a DMA-related interrupt occurred on that substream, call
	 * the DMA interrupt handler function, if one has been
	 * registered in the dma_params structure by the PCM driver.
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
static int atmel_ssc_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct atmel_ssc_info *ssc_p = &ssc_info[dai->id];
	int dir_mask;

	pr_debug("atmel_ssc_startup: SSC_SR=0x%u\n",
		ssc_readl(ssc_p->ssc->regs, SR));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir_mask = SSC_DIR_MASK_PLAYBACK;
	else
		dir_mask = SSC_DIR_MASK_CAPTURE;

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
static void atmel_ssc_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct atmel_ssc_info *ssc_p = &ssc_info[dai->id];
	struct atmel_pcm_dma_params *dma_params;
	int dir, dir_mask;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = 0;
	else
		dir = 1;

	dma_params = ssc_p->dma_params[dir];

	if (dma_params != NULL) {
		ssc_writel(ssc_p->ssc->regs, CR, dma_params->mask->ssc_disable);
		pr_debug("atmel_ssc_shutdown: %s disabled SSC_SR=0x%08x\n",
			(dir ? "receive" : "transmit"),
			ssc_readl(ssc_p->ssc->regs, SR));

		dma_params->ssc = NULL;
		dma_params->substream = NULL;
		ssc_p->dma_params[dir] = NULL;
	}

	dir_mask = 1 << dir;

	spin_lock_irq(&ssc_p->lock);
	ssc_p->dir_mask &= ~dir_mask;
	if (!ssc_p->dir_mask) {
		if (ssc_p->initialized) {
			/* Shutdown the SSC clock. */
			pr_debug("atmel_ssc_dau: Stopping clock\n");
			clk_disable(ssc_p->ssc->clk);

			free_irq(ssc_p->ssc->irq, ssc_p);
			ssc_p->initialized = 0;
		}

		/* Reset the SSC */
		ssc_writel(ssc_p->ssc->regs, CR, SSC_BIT(CR_SWRST));
		/* Clear the SSC dividers */
		ssc_p->cmr_div = ssc_p->tcmr_period = ssc_p->rcmr_period = 0;
	}
	spin_unlock_irq(&ssc_p->lock);
}


/*
 * Record the DAI format for use in hw_params().
 */
static int atmel_ssc_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	struct atmel_ssc_info *ssc_p = &ssc_info[cpu_dai->id];

	ssc_p->daifmt = fmt;
	return 0;
}

/*
 * Record SSC clock dividers for use in hw_params().
 */
static int atmel_ssc_set_dai_clkdiv(struct snd_soc_dai *cpu_dai,
	int div_id, int div)
{
	struct atmel_ssc_info *ssc_p = &ssc_info[cpu_dai->id];

	switch (div_id) {
	case ATMEL_SSC_CMR_DIV:
		/*
		 * The same master clock divider is used for both
		 * transmit and receive, so if a value has already
		 * been set, it must match this value.
		 */
		if (ssc_p->cmr_div == 0)
			ssc_p->cmr_div = div;
		else
			if (div != ssc_p->cmr_div)
				return -EBUSY;
		break;

	case ATMEL_SSC_TCMR_PERIOD:
		ssc_p->tcmr_period = div;
		break;

	case ATMEL_SSC_RCMR_PERIOD:
		ssc_p->rcmr_period = div;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Configure the SSC.
 */
static int atmel_ssc_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	int id = dai->id;
	struct atmel_ssc_info *ssc_p = &ssc_info[id];
	struct atmel_pcm_dma_params *dma_params;
	int dir, channels, bits;
	u32 tfmr, rfmr, tcmr, rcmr;
	int start_event;
	int ret;

	/*
	 * Currently, there is only one set of dma params for
	 * each direction.  If more are added, this code will
	 * have to be changed to select the proper set.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = 0;
	else
		dir = 1;

	dma_params = &ssc_dma_params[id][dir];
	dma_params->ssc = ssc_p->ssc;
	dma_params->substream = substream;

	ssc_p->dma_params[dir] = dma_params;

	/*
	 * The snd_soc_pcm_stream->dma_data field is only used to communicate
	 * the appropriate DMA parameters to the pcm driver hw_params()
	 * function.  It should not be used for other purposes
	 * as it is common to all substreams.
	 */
	snd_soc_dai_set_dma_data(rtd->cpu_dai, substream, dma_params);

	channels = params_channels(params);

	/*
	 * Determine sample size in bits and the PDC increment.
	 */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		bits = 8;
		dma_params->pdc_xfer_size = 1;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		bits = 16;
		dma_params->pdc_xfer_size = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		bits = 24;
		dma_params->pdc_xfer_size = 4;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		bits = 32;
		dma_params->pdc_xfer_size = 4;
		break;
	default:
		printk(KERN_WARNING "atmel_ssc_dai: unsupported PCM format");
		return -EINVAL;
	}

	/*
	 * The SSC only supports up to 16-bit samples in I2S format, due
	 * to the size of the Frame Mode Register FSLEN field.
	 */
	if ((ssc_p->daifmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_I2S
		&& bits > 16) {
		printk(KERN_WARNING
				"atmel_ssc_dai: sample size %d "
				"is too large for I2S\n", bits);
		return -EINVAL;
	}

	/*
	 * Compute SSC register settings.
	 */
	switch (ssc_p->daifmt
		& (SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_MASTER_MASK)) {

	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS:
		/*
		 * I2S format, SSC provides BCLK and LRC clocks.
		 *
		 * The SSC transmit and receive clocks are generated
		 * from the MCK divider, and the BCLK signal
		 * is output on the SSC TK line.
		 */
		rcmr =	  SSC_BF(RCMR_PERIOD, ssc_p->rcmr_period)
			| SSC_BF(RCMR_STTDLY, START_DELAY)
			| SSC_BF(RCMR_START, SSC_START_FALLING_RF)
			| SSC_BF(RCMR_CKI, SSC_CKI_RISING)
			| SSC_BF(RCMR_CKO, SSC_CKO_NONE)
			| SSC_BF(RCMR_CKS, SSC_CKS_DIV);

		rfmr =	  SSC_BF(RFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
			| SSC_BF(RFMR_FSOS, SSC_FSOS_NEGATIVE)
			| SSC_BF(RFMR_FSLEN, (bits - 1))
			| SSC_BF(RFMR_DATNB, (channels - 1))
			| SSC_BIT(RFMR_MSBF)
			| SSC_BF(RFMR_LOOP, 0)
			| SSC_BF(RFMR_DATLEN, (bits - 1));

		tcmr =	  SSC_BF(TCMR_PERIOD, ssc_p->tcmr_period)
			| SSC_BF(TCMR_STTDLY, START_DELAY)
			| SSC_BF(TCMR_START, SSC_START_FALLING_RF)
			| SSC_BF(TCMR_CKI, SSC_CKI_FALLING)
			| SSC_BF(TCMR_CKO, SSC_CKO_CONTINUOUS)
			| SSC_BF(TCMR_CKS, SSC_CKS_DIV);

		tfmr =	  SSC_BF(TFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
			| SSC_BF(TFMR_FSDEN, 0)
			| SSC_BF(TFMR_FSOS, SSC_FSOS_NEGATIVE)
			| SSC_BF(TFMR_FSLEN, (bits - 1))
			| SSC_BF(TFMR_DATNB, (channels - 1))
			| SSC_BIT(TFMR_MSBF)
			| SSC_BF(TFMR_DATDEF, 0)
			| SSC_BF(TFMR_DATLEN, (bits - 1));
		break;

	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM:
		/*
		 * I2S format, CODEC supplies BCLK and LRC clocks.
		 *
		 * The SSC transmit clock is obtained from the BCLK signal on
		 * on the TK line, and the SSC receive clock is
		 * generated from the transmit clock.
		 *
		 *  For single channel data, one sample is transferred
		 * on the falling edge of the LRC clock.
		 * For two channel data, one sample is
		 * transferred on both edges of the LRC clock.
		 */
		start_event = ((channels == 1)
				? SSC_START_FALLING_RF
				: SSC_START_EDGE_RF);

		rcmr =	  SSC_BF(RCMR_PERIOD, 0)
			| SSC_BF(RCMR_STTDLY, START_DELAY)
			| SSC_BF(RCMR_START, start_event)
			| SSC_BF(RCMR_CKI, SSC_CKI_RISING)
			| SSC_BF(RCMR_CKO, SSC_CKO_NONE)
			| SSC_BF(RCMR_CKS, SSC_CKS_CLOCK);

		rfmr =	  SSC_BF(RFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
			| SSC_BF(RFMR_FSOS, SSC_FSOS_NONE)
			| SSC_BF(RFMR_FSLEN, 0)
			| SSC_BF(RFMR_DATNB, 0)
			| SSC_BIT(RFMR_MSBF)
			| SSC_BF(RFMR_LOOP, 0)
			| SSC_BF(RFMR_DATLEN, (bits - 1));

		tcmr =	  SSC_BF(TCMR_PERIOD, 0)
			| SSC_BF(TCMR_STTDLY, START_DELAY)
			| SSC_BF(TCMR_START, start_event)
			| SSC_BF(TCMR_CKI, SSC_CKI_FALLING)
			| SSC_BF(TCMR_CKO, SSC_CKO_NONE)
			| SSC_BF(TCMR_CKS, SSC_CKS_PIN);

		tfmr =	  SSC_BF(TFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
			| SSC_BF(TFMR_FSDEN, 0)
			| SSC_BF(TFMR_FSOS, SSC_FSOS_NONE)
			| SSC_BF(TFMR_FSLEN, 0)
			| SSC_BF(TFMR_DATNB, 0)
			| SSC_BIT(TFMR_MSBF)
			| SSC_BF(TFMR_DATDEF, 0)
			| SSC_BF(TFMR_DATLEN, (bits - 1));
		break;

	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBS_CFS:
		/*
		 * DSP/PCM Mode A format, SSC provides BCLK and LRC clocks.
		 *
		 * The SSC transmit and receive clocks are generated from the
		 * MCK divider, and the BCLK signal is output
		 * on the SSC TK line.
		 */
		rcmr =	  SSC_BF(RCMR_PERIOD, ssc_p->rcmr_period)
			| SSC_BF(RCMR_STTDLY, 1)
			| SSC_BF(RCMR_START, SSC_START_RISING_RF)
			| SSC_BF(RCMR_CKI, SSC_CKI_RISING)
			| SSC_BF(RCMR_CKO, SSC_CKO_NONE)
			| SSC_BF(RCMR_CKS, SSC_CKS_DIV);

		rfmr =	  SSC_BF(RFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
			| SSC_BF(RFMR_FSOS, SSC_FSOS_POSITIVE)
			| SSC_BF(RFMR_FSLEN, 0)
			| SSC_BF(RFMR_DATNB, (channels - 1))
			| SSC_BIT(RFMR_MSBF)
			| SSC_BF(RFMR_LOOP, 0)
			| SSC_BF(RFMR_DATLEN, (bits - 1));

		tcmr =	  SSC_BF(TCMR_PERIOD, ssc_p->tcmr_period)
			| SSC_BF(TCMR_STTDLY, 1)
			| SSC_BF(TCMR_START, SSC_START_RISING_RF)
			| SSC_BF(TCMR_CKI, SSC_CKI_RISING)
			| SSC_BF(TCMR_CKO, SSC_CKO_CONTINUOUS)
			| SSC_BF(TCMR_CKS, SSC_CKS_DIV);

		tfmr =	  SSC_BF(TFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
			| SSC_BF(TFMR_FSDEN, 0)
			| SSC_BF(TFMR_FSOS, SSC_FSOS_POSITIVE)
			| SSC_BF(TFMR_FSLEN, 0)
			| SSC_BF(TFMR_DATNB, (channels - 1))
			| SSC_BIT(TFMR_MSBF)
			| SSC_BF(TFMR_DATDEF, 0)
			| SSC_BF(TFMR_DATLEN, (bits - 1));
		break;

	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBM_CFM:
		/*
		 * DSP/PCM Mode A format, CODEC supplies BCLK and LRC clocks.
		 *
		 * The SSC transmit clock is obtained from the BCLK signal on
		 * on the TK line, and the SSC receive clock is
		 * generated from the transmit clock.
		 *
		 * Data is transferred on first BCLK after LRC pulse rising
		 * edge.If stereo, the right channel data is contiguous with
		 * the left channel data.
		 */
		rcmr =	  SSC_BF(RCMR_PERIOD, 0)
			| SSC_BF(RCMR_STTDLY, START_DELAY)
			| SSC_BF(RCMR_START, SSC_START_RISING_RF)
			| SSC_BF(RCMR_CKI, SSC_CKI_RISING)
			| SSC_BF(RCMR_CKO, SSC_CKO_NONE)
			| SSC_BF(RCMR_CKS, SSC_CKS_PIN);

		rfmr =	  SSC_BF(RFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
			| SSC_BF(RFMR_FSOS, SSC_FSOS_NONE)
			| SSC_BF(RFMR_FSLEN, 0)
			| SSC_BF(RFMR_DATNB, (channels - 1))
			| SSC_BIT(RFMR_MSBF)
			| SSC_BF(RFMR_LOOP, 0)
			| SSC_BF(RFMR_DATLEN, (bits - 1));

		tcmr =	  SSC_BF(TCMR_PERIOD, 0)
			| SSC_BF(TCMR_STTDLY, START_DELAY)
			| SSC_BF(TCMR_START, SSC_START_RISING_RF)
			| SSC_BF(TCMR_CKI, SSC_CKI_FALLING)
			| SSC_BF(TCMR_CKO, SSC_CKO_NONE)
			| SSC_BF(TCMR_CKS, SSC_CKS_PIN);

		tfmr =	  SSC_BF(TFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
			| SSC_BF(TFMR_FSDEN, 0)
			| SSC_BF(TFMR_FSOS, SSC_FSOS_NONE)
			| SSC_BF(TFMR_FSLEN, 0)
			| SSC_BF(TFMR_DATNB, (channels - 1))
			| SSC_BIT(TFMR_MSBF)
			| SSC_BF(TFMR_DATDEF, 0)
			| SSC_BF(TFMR_DATLEN, (bits - 1));
		break;

	default:
		printk(KERN_WARNING "atmel_ssc_dai: unsupported DAI format 0x%x\n",
			ssc_p->daifmt);
		return -EINVAL;
	}
	pr_debug("atmel_ssc_hw_params: "
			"RCMR=%08x RFMR=%08x TCMR=%08x TFMR=%08x\n",
			rcmr, rfmr, tcmr, tfmr);

	if (!ssc_p->initialized) {

		/* Enable PMC peripheral clock for this SSC */
		pr_debug("atmel_ssc_dai: Starting clock\n");
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

		ret = request_irq(ssc_p->ssc->irq, atmel_ssc_interrupt, 0,
				ssc_p->name, ssc_p);
		if (ret < 0) {
			printk(KERN_WARNING
					"atmel_ssc_dai: request_irq failure\n");
			pr_debug("Atmel_ssc_dai: Stoping clock\n");
			clk_disable(ssc_p->ssc->clk);
			return ret;
		}

		ssc_p->initialized = 1;
	}

	/* set SSC clock mode register */
	ssc_writel(ssc_p->ssc->regs, CMR, ssc_p->cmr_div);

	/* set receive clock mode and format */
	ssc_writel(ssc_p->ssc->regs, RCMR, rcmr);
	ssc_writel(ssc_p->ssc->regs, RFMR, rfmr);

	/* set transmit clock mode and format */
	ssc_writel(ssc_p->ssc->regs, TCMR, tcmr);
	ssc_writel(ssc_p->ssc->regs, TFMR, tfmr);

	pr_debug("atmel_ssc_dai,hw_params: SSC initialized\n");
	return 0;
}


static int atmel_ssc_prepare(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct atmel_ssc_info *ssc_p = &ssc_info[dai->id];
	struct atmel_pcm_dma_params *dma_params;
	int dir;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = 0;
	else
		dir = 1;

	dma_params = ssc_p->dma_params[dir];

	ssc_writel(ssc_p->ssc->regs, CR, dma_params->mask->ssc_enable);

	pr_debug("%s enabled SSC_SR=0x%08x\n",
			dir ? "receive" : "transmit",
			ssc_readl(ssc_p->ssc->regs, SR));
	return 0;
}


#ifdef CONFIG_PM
static int atmel_ssc_suspend(struct snd_soc_dai *cpu_dai)
{
	struct atmel_ssc_info *ssc_p;

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



static int atmel_ssc_resume(struct snd_soc_dai *cpu_dai)
{
	struct atmel_ssc_info *ssc_p;
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

	/* Re-enable receive and transmit as appropriate */
	cr = 0;
	cr |=
	    (ssc_p->ssc_state.ssc_sr & SSC_BIT(SR_RXEN)) ? SSC_BIT(CR_RXEN) : 0;
	cr |=
	    (ssc_p->ssc_state.ssc_sr & SSC_BIT(SR_TXEN)) ? SSC_BIT(CR_TXEN) : 0;
	ssc_writel(ssc_p->ssc->regs, CR, cr);

	return 0;
}
#else /* CONFIG_PM */
#  define atmel_ssc_suspend	NULL
#  define atmel_ssc_resume	NULL
#endif /* CONFIG_PM */

#define ATMEL_SSC_RATES (SNDRV_PCM_RATE_8000_96000)

#define ATMEL_SSC_FORMATS (SNDRV_PCM_FMTBIT_S8     | SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops atmel_ssc_dai_ops = {
	.startup	= atmel_ssc_startup,
	.shutdown	= atmel_ssc_shutdown,
	.prepare	= atmel_ssc_prepare,
	.hw_params	= atmel_ssc_hw_params,
	.set_fmt	= atmel_ssc_set_dai_fmt,
	.set_clkdiv	= atmel_ssc_set_dai_clkdiv,
};

static struct snd_soc_dai_driver atmel_ssc_dai = {
		.suspend = atmel_ssc_suspend,
		.resume = atmel_ssc_resume,
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = ATMEL_SSC_RATES,
			.formats = ATMEL_SSC_FORMATS,},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = ATMEL_SSC_RATES,
			.formats = ATMEL_SSC_FORMATS,},
		.ops = &atmel_ssc_dai_ops,
};

static int asoc_ssc_init(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ssc_device *ssc = platform_get_drvdata(pdev);
	int ret;

	ret = snd_soc_register_dai(dev, &atmel_ssc_dai);
	if (ret) {
		dev_err(dev, "Could not register DAI: %d\n", ret);
		goto err;
	}

	if (ssc->pdata->use_dma)
		ret = atmel_pcm_dma_platform_register(dev);
	else
		ret = atmel_pcm_pdc_platform_register(dev);

	if (ret) {
		dev_err(dev, "Could not register PCM: %d\n", ret);
		goto err_unregister_dai;
	};

	return 0;

err_unregister_dai:
	snd_soc_unregister_dai(dev);
err:
	return ret;
}

static void asoc_ssc_exit(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ssc_device *ssc = platform_get_drvdata(pdev);

	if (ssc->pdata->use_dma)
		atmel_pcm_dma_platform_unregister(dev);
	else
		atmel_pcm_pdc_platform_unregister(dev);

	snd_soc_unregister_dai(dev);
}

/**
 * atmel_ssc_set_audio - Allocate the specified SSC for audio use.
 */
int atmel_ssc_set_audio(int ssc_id)
{
	struct ssc_device *ssc;
	int ret;

	/* If we can grab the SSC briefly to parent the DAI device off it */
	ssc = ssc_request(ssc_id);
	if (IS_ERR(ssc)) {
		pr_err("Unable to parent ASoC SSC DAI on SSC: %ld\n",
			PTR_ERR(ssc));
		return PTR_ERR(ssc);
	} else {
		ssc_info[ssc_id].ssc = ssc;
	}

	ret = asoc_ssc_init(&ssc->pdev->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(atmel_ssc_set_audio);

void atmel_ssc_put_audio(int ssc_id)
{
	struct ssc_device *ssc = ssc_info[ssc_id].ssc;

	asoc_ssc_exit(&ssc->pdev->dev);
	ssc_free(ssc);
}
EXPORT_SYMBOL_GPL(atmel_ssc_put_audio);

/* Module information */
MODULE_AUTHOR("Sedji Gaouaou, sedji.gaouaou@atmel.com, www.atmel.com");
MODULE_DESCRIPTION("ATMEL SSC ASoC Interface");
MODULE_LICENSE("GPL");

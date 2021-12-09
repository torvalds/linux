// SPDX-License-Identifier: GPL-2.0-or-later
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
	.ssc_error	= SSC_BIT(SR_OVRUN),
	.pdc_enable	= ATMEL_PDC_TXTEN,
	.pdc_disable	= ATMEL_PDC_TXTDIS,
};

static struct atmel_ssc_mask ssc_rx_mask = {
	.ssc_enable	= SSC_BIT(CR_RXEN),
	.ssc_disable	= SSC_BIT(CR_RXDIS),
	.ssc_endx	= SSC_BIT(SR_ENDRX),
	.ssc_endbuf	= SSC_BIT(SR_RXBUFF),
	.ssc_error	= SSC_BIT(SR_OVRUN),
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
	.dir_mask	= SSC_DIR_MASK_UNUSED,
	.initialized	= 0,
	},
	{
	.name		= "ssc1",
	.dir_mask	= SSC_DIR_MASK_UNUSED,
	.initialized	= 0,
	},
	{
	.name		= "ssc2",
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

/*
 * When the bit clock is input, limit the maximum rate according to the
 * Serial Clock Ratio Considerations section from the SSC documentation:
 *
 *   The Transmitter and the Receiver can be programmed to operate
 *   with the clock signals provided on either the TK or RK pins.
 *   This allows the SSC to support many slave-mode data transfers.
 *   In this case, the maximum clock speed allowed on the RK pin is:
 *   - Peripheral clock divided by 2 if Receiver Frame Synchro is input
 *   - Peripheral clock divided by 3 if Receiver Frame Synchro is output
 *   In addition, the maximum clock speed allowed on the TK pin is:
 *   - Peripheral clock divided by 6 if Transmit Frame Synchro is input
 *   - Peripheral clock divided by 2 if Transmit Frame Synchro is output
 *
 * When the bit clock is output, limit the rate according to the
 * SSC divider restrictions.
 */
static int atmel_ssc_hw_rule_rate(struct snd_pcm_hw_params *params,
				  struct snd_pcm_hw_rule *rule)
{
	struct atmel_ssc_info *ssc_p = rule->private;
	struct ssc_device *ssc = ssc_p->ssc;
	struct snd_interval *i = hw_param_interval(params, rule->var);
	struct snd_interval t;
	struct snd_ratnum r = {
		.den_min = 1,
		.den_max = 4095,
		.den_step = 1,
	};
	unsigned int num = 0, den = 0;
	int frame_size;
	int mck_div = 2;
	int ret;

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0)
		return frame_size;

	switch (ssc_p->daifmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFC:
		if ((ssc_p->dir_mask & SSC_DIR_MASK_CAPTURE)
		    && ssc->clk_from_rk_pin)
			/* Receiver Frame Synchro (i.e. capture)
			 * is output (format is _CFS) and the RK pin
			 * is used for input (format is _CBM_).
			 */
			mck_div = 3;
		break;

	case SND_SOC_DAIFMT_CBP_CFP:
		if ((ssc_p->dir_mask & SSC_DIR_MASK_PLAYBACK)
		    && !ssc->clk_from_rk_pin)
			/* Transmit Frame Synchro (i.e. playback)
			 * is input (format is _CFM) and the TK pin
			 * is used for input (format _CBM_ but not
			 * using the RK pin).
			 */
			mck_div = 6;
		break;
	}

	switch (ssc_p->daifmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		r.num = ssc_p->mck_rate / mck_div / frame_size;

		ret = snd_interval_ratnum(i, 1, &r, &num, &den);
		if (ret >= 0 && den && rule->var == SNDRV_PCM_HW_PARAM_RATE) {
			params->rate_num = num;
			params->rate_den = den;
		}
		break;

	case SND_SOC_DAIFMT_CBP_CFC:
	case SND_SOC_DAIFMT_CBP_CFP:
		t.min = 8000;
		t.max = ssc_p->mck_rate / mck_div / frame_size;
		t.openmin = t.openmax = 0;
		t.integer = 0;
		ret = snd_interval_refine(i, &t);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
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
	struct platform_device *pdev = to_platform_device(dai->dev);
	struct atmel_ssc_info *ssc_p = &ssc_info[pdev->id];
	struct atmel_pcm_dma_params *dma_params;
	int dir, dir_mask;
	int ret;

	pr_debug("atmel_ssc_startup: SSC_SR=0x%x\n",
		ssc_readl(ssc_p->ssc->regs, SR));

	/* Enable PMC peripheral clock for this SSC */
	pr_debug("atmel_ssc_dai: Starting clock\n");
	clk_enable(ssc_p->ssc->clk);
	ssc_p->mck_rate = clk_get_rate(ssc_p->ssc->clk);

	/* Reset the SSC unless initialized to keep it in a clean state */
	if (!ssc_p->initialized)
		ssc_writel(ssc_p->ssc->regs, CR, SSC_BIT(CR_SWRST));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dir = 0;
		dir_mask = SSC_DIR_MASK_PLAYBACK;
	} else {
		dir = 1;
		dir_mask = SSC_DIR_MASK_CAPTURE;
	}

	ret = snd_pcm_hw_rule_add(substream->runtime, 0,
				  SNDRV_PCM_HW_PARAM_RATE,
				  atmel_ssc_hw_rule_rate,
				  ssc_p,
				  SNDRV_PCM_HW_PARAM_FRAME_BITS,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to specify rate rule: %d\n", ret);
		return ret;
	}

	dma_params = &ssc_dma_params[pdev->id][dir];
	dma_params->ssc = ssc_p->ssc;
	dma_params->substream = substream;

	ssc_p->dma_params[dir] = dma_params;

	snd_soc_dai_set_dma_data(dai, substream, dma_params);

	if (ssc_p->dir_mask & dir_mask)
		return -EBUSY;

	ssc_p->dir_mask |= dir_mask;

	return 0;
}

/*
 * Shutdown.  Clear DMA parameters and shutdown the SSC if there
 * are no other substreams open.
 */
static void atmel_ssc_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct platform_device *pdev = to_platform_device(dai->dev);
	struct atmel_ssc_info *ssc_p = &ssc_info[pdev->id];
	struct atmel_pcm_dma_params *dma_params;
	int dir, dir_mask;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = 0;
	else
		dir = 1;

	dma_params = ssc_p->dma_params[dir];

	if (dma_params != NULL) {
		dma_params->ssc = NULL;
		dma_params->substream = NULL;
		ssc_p->dma_params[dir] = NULL;
	}

	dir_mask = 1 << dir;

	ssc_p->dir_mask &= ~dir_mask;
	if (!ssc_p->dir_mask) {
		if (ssc_p->initialized) {
			free_irq(ssc_p->ssc->irq, ssc_p);
			ssc_p->initialized = 0;
		}

		/* Reset the SSC */
		ssc_writel(ssc_p->ssc->regs, CR, SSC_BIT(CR_SWRST));
		/* Clear the SSC dividers */
		ssc_p->cmr_div = ssc_p->tcmr_period = ssc_p->rcmr_period = 0;
		ssc_p->forced_divider = 0;
	}

	/* Shutdown the SSC clock. */
	pr_debug("atmel_ssc_dai: Stopping clock\n");
	clk_disable(ssc_p->ssc->clk);
}


/*
 * Record the DAI format for use in hw_params().
 */
static int atmel_ssc_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	struct platform_device *pdev = to_platform_device(cpu_dai->dev);
	struct atmel_ssc_info *ssc_p = &ssc_info[pdev->id];

	ssc_p->daifmt = fmt;
	return 0;
}

/*
 * Record SSC clock dividers for use in hw_params().
 */
static int atmel_ssc_set_dai_clkdiv(struct snd_soc_dai *cpu_dai,
	int div_id, int div)
{
	struct platform_device *pdev = to_platform_device(cpu_dai->dev);
	struct atmel_ssc_info *ssc_p = &ssc_info[pdev->id];

	switch (div_id) {
	case ATMEL_SSC_CMR_DIV:
		/*
		 * The same master clock divider is used for both
		 * transmit and receive, so if a value has already
		 * been set, it must match this value.
		 */
		if (ssc_p->dir_mask !=
			(SSC_DIR_MASK_PLAYBACK | SSC_DIR_MASK_CAPTURE))
			ssc_p->cmr_div = div;
		else if (ssc_p->cmr_div == 0)
			ssc_p->cmr_div = div;
		else
			if (div != ssc_p->cmr_div)
				return -EBUSY;
		ssc_p->forced_divider |= BIT(ATMEL_SSC_CMR_DIV);
		break;

	case ATMEL_SSC_TCMR_PERIOD:
		ssc_p->tcmr_period = div;
		ssc_p->forced_divider |= BIT(ATMEL_SSC_TCMR_PERIOD);
		break;

	case ATMEL_SSC_RCMR_PERIOD:
		ssc_p->rcmr_period = div;
		ssc_p->forced_divider |= BIT(ATMEL_SSC_RCMR_PERIOD);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* Is the cpu-dai master of the frame clock? */
static int atmel_ssc_cfs(struct atmel_ssc_info *ssc_p)
{
	switch (ssc_p->daifmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFC:
	case SND_SOC_DAIFMT_CBC_CFC:
		return 1;
	}
	return 0;
}

/* Is the cpu-dai master of the bit clock? */
static int atmel_ssc_cbs(struct atmel_ssc_info *ssc_p)
{
	switch (ssc_p->daifmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFP:
	case SND_SOC_DAIFMT_CBC_CFC:
		return 1;
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
	struct platform_device *pdev = to_platform_device(dai->dev);
	int id = pdev->id;
	struct atmel_ssc_info *ssc_p = &ssc_info[id];
	struct ssc_device *ssc = ssc_p->ssc;
	struct atmel_pcm_dma_params *dma_params;
	int dir, channels, bits;
	u32 tfmr, rfmr, tcmr, rcmr;
	int ret;
	int fslen, fslen_ext, fs_osync, fs_edge;
	u32 cmr_div;
	u32 tcmr_period;
	u32 rcmr_period;

	/*
	 * Currently, there is only one set of dma params for
	 * each direction.  If more are added, this code will
	 * have to be changed to select the proper set.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = 0;
	else
		dir = 1;

	/*
	 * If the cpu dai should provide BCLK, but noone has provided the
	 * divider needed for that to work, fall back to something sensible.
	 */
	cmr_div = ssc_p->cmr_div;
	if (!(ssc_p->forced_divider & BIT(ATMEL_SSC_CMR_DIV)) &&
	    atmel_ssc_cbs(ssc_p)) {
		int bclk_rate = snd_soc_params_to_bclk(params);

		if (bclk_rate < 0) {
			dev_err(dai->dev, "unable to calculate cmr_div: %d\n",
				bclk_rate);
			return bclk_rate;
		}

		cmr_div = DIV_ROUND_CLOSEST(ssc_p->mck_rate, 2 * bclk_rate);
	}

	/*
	 * If the cpu dai should provide LRCLK, but noone has provided the
	 * dividers needed for that to work, fall back to something sensible.
	 */
	tcmr_period = ssc_p->tcmr_period;
	rcmr_period = ssc_p->rcmr_period;
	if (atmel_ssc_cfs(ssc_p)) {
		int frame_size = snd_soc_params_to_frame_size(params);

		if (frame_size < 0) {
			dev_err(dai->dev,
				"unable to calculate tx/rx cmr_period: %d\n",
				frame_size);
			return frame_size;
		}

		if (!(ssc_p->forced_divider & BIT(ATMEL_SSC_TCMR_PERIOD)))
			tcmr_period = frame_size / 2 - 1;
		if (!(ssc_p->forced_divider & BIT(ATMEL_SSC_RCMR_PERIOD)))
			rcmr_period = frame_size / 2 - 1;
	}

	dma_params = ssc_p->dma_params[dir];

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
	 * Compute SSC register settings.
	 */

	fslen_ext = (bits - 1) / 16;
	fslen = (bits - 1) % 16;

	switch (ssc_p->daifmt & SND_SOC_DAIFMT_FORMAT_MASK) {

	case SND_SOC_DAIFMT_LEFT_J:
		fs_osync = SSC_FSOS_POSITIVE;
		fs_edge = SSC_START_RISING_RF;

		rcmr =	  SSC_BF(RCMR_STTDLY, 0);
		tcmr =	  SSC_BF(TCMR_STTDLY, 0);

		break;

	case SND_SOC_DAIFMT_I2S:
		fs_osync = SSC_FSOS_NEGATIVE;
		fs_edge = SSC_START_FALLING_RF;

		rcmr =	  SSC_BF(RCMR_STTDLY, 1);
		tcmr =	  SSC_BF(TCMR_STTDLY, 1);

		break;

	case SND_SOC_DAIFMT_DSP_A:
		/*
		 * DSP/PCM Mode A format
		 *
		 * Data is transferred on first BCLK after LRC pulse rising
		 * edge.If stereo, the right channel data is contiguous with
		 * the left channel data.
		 */
		fs_osync = SSC_FSOS_POSITIVE;
		fs_edge = SSC_START_RISING_RF;
		fslen = fslen_ext = 0;

		rcmr =	  SSC_BF(RCMR_STTDLY, 1);
		tcmr =	  SSC_BF(TCMR_STTDLY, 1);

		break;

	default:
		printk(KERN_WARNING "atmel_ssc_dai: unsupported DAI format 0x%x\n",
			ssc_p->daifmt);
		return -EINVAL;
	}

	if (!atmel_ssc_cfs(ssc_p)) {
		fslen = fslen_ext = 0;
		rcmr_period = tcmr_period = 0;
		fs_osync = SSC_FSOS_NONE;
	}

	rcmr |=	  SSC_BF(RCMR_START, fs_edge);
	tcmr |=	  SSC_BF(TCMR_START, fs_edge);

	if (atmel_ssc_cbs(ssc_p)) {
		/*
		 * SSC provides BCLK
		 *
		 * The SSC transmit and receive clocks are generated from the
		 * MCK divider, and the BCLK signal is output
		 * on the SSC TK line.
		 */
		rcmr |=	  SSC_BF(RCMR_CKS, SSC_CKS_DIV)
			| SSC_BF(RCMR_CKO, SSC_CKO_NONE);

		tcmr |=	  SSC_BF(TCMR_CKS, SSC_CKS_DIV)
			| SSC_BF(TCMR_CKO, SSC_CKO_CONTINUOUS);
	} else {
		rcmr |=	  SSC_BF(RCMR_CKS, ssc->clk_from_rk_pin ?
					SSC_CKS_PIN : SSC_CKS_CLOCK)
			| SSC_BF(RCMR_CKO, SSC_CKO_NONE);

		tcmr |=	  SSC_BF(TCMR_CKS, ssc->clk_from_rk_pin ?
					SSC_CKS_CLOCK : SSC_CKS_PIN)
			| SSC_BF(TCMR_CKO, SSC_CKO_NONE);
	}

	rcmr |=	  SSC_BF(RCMR_PERIOD, rcmr_period)
		| SSC_BF(RCMR_CKI, SSC_CKI_RISING);

	tcmr |=   SSC_BF(TCMR_PERIOD, tcmr_period)
		| SSC_BF(TCMR_CKI, SSC_CKI_FALLING);

	rfmr =    SSC_BF(RFMR_FSLEN_EXT, fslen_ext)
		| SSC_BF(RFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
		| SSC_BF(RFMR_FSOS, fs_osync)
		| SSC_BF(RFMR_FSLEN, fslen)
		| SSC_BF(RFMR_DATNB, (channels - 1))
		| SSC_BIT(RFMR_MSBF)
		| SSC_BF(RFMR_LOOP, 0)
		| SSC_BF(RFMR_DATLEN, (bits - 1));

	tfmr =    SSC_BF(TFMR_FSLEN_EXT, fslen_ext)
		| SSC_BF(TFMR_FSEDGE, SSC_FSEDGE_POSITIVE)
		| SSC_BF(TFMR_FSDEN, 0)
		| SSC_BF(TFMR_FSOS, fs_osync)
		| SSC_BF(TFMR_FSLEN, fslen)
		| SSC_BF(TFMR_DATNB, (channels - 1))
		| SSC_BIT(TFMR_MSBF)
		| SSC_BF(TFMR_DATDEF, 0)
		| SSC_BF(TFMR_DATLEN, (bits - 1));

	if (fslen_ext && !ssc->pdata->has_fslen_ext) {
		dev_err(dai->dev, "sample size %d is too large for SSC device\n",
			bits);
		return -EINVAL;
	}

	pr_debug("atmel_ssc_hw_params: "
			"RCMR=%08x RFMR=%08x TCMR=%08x TFMR=%08x\n",
			rcmr, rfmr, tcmr, tfmr);

	if (!ssc_p->initialized) {
		if (!ssc_p->ssc->pdata->use_dma) {
			ssc_writel(ssc_p->ssc->regs, PDC_RPR, 0);
			ssc_writel(ssc_p->ssc->regs, PDC_RCR, 0);
			ssc_writel(ssc_p->ssc->regs, PDC_RNPR, 0);
			ssc_writel(ssc_p->ssc->regs, PDC_RNCR, 0);

			ssc_writel(ssc_p->ssc->regs, PDC_TPR, 0);
			ssc_writel(ssc_p->ssc->regs, PDC_TCR, 0);
			ssc_writel(ssc_p->ssc->regs, PDC_TNPR, 0);
			ssc_writel(ssc_p->ssc->regs, PDC_TNCR, 0);
		}

		ret = request_irq(ssc_p->ssc->irq, atmel_ssc_interrupt, 0,
				ssc_p->name, ssc_p);
		if (ret < 0) {
			printk(KERN_WARNING
					"atmel_ssc_dai: request_irq failure\n");
			pr_debug("Atmel_ssc_dai: Stopping clock\n");
			clk_disable(ssc_p->ssc->clk);
			return ret;
		}

		ssc_p->initialized = 1;
	}

	/* set SSC clock mode register */
	ssc_writel(ssc_p->ssc->regs, CMR, cmr_div);

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
	struct platform_device *pdev = to_platform_device(dai->dev);
	struct atmel_ssc_info *ssc_p = &ssc_info[pdev->id];
	struct atmel_pcm_dma_params *dma_params;
	int dir;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = 0;
	else
		dir = 1;

	dma_params = ssc_p->dma_params[dir];

	ssc_writel(ssc_p->ssc->regs, CR, dma_params->mask->ssc_disable);
	ssc_writel(ssc_p->ssc->regs, IDR, dma_params->mask->ssc_error);

	pr_debug("%s enabled SSC_SR=0x%08x\n",
			dir ? "receive" : "transmit",
			ssc_readl(ssc_p->ssc->regs, SR));
	return 0;
}

static int atmel_ssc_trigger(struct snd_pcm_substream *substream,
			     int cmd, struct snd_soc_dai *dai)
{
	struct platform_device *pdev = to_platform_device(dai->dev);
	struct atmel_ssc_info *ssc_p = &ssc_info[pdev->id];
	struct atmel_pcm_dma_params *dma_params;
	int dir;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = 0;
	else
		dir = 1;

	dma_params = ssc_p->dma_params[dir];

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ssc_writel(ssc_p->ssc->regs, CR, dma_params->mask->ssc_enable);
		break;
	default:
		ssc_writel(ssc_p->ssc->regs, CR, dma_params->mask->ssc_disable);
		break;
	}

	return 0;
}

#ifdef CONFIG_PM
static int atmel_ssc_suspend(struct snd_soc_component *component)
{
	struct atmel_ssc_info *ssc_p;
	struct platform_device *pdev = to_platform_device(component->dev);

	if (!snd_soc_component_active(component))
		return 0;

	ssc_p = &ssc_info[pdev->id];

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

static int atmel_ssc_resume(struct snd_soc_component *component)
{
	struct atmel_ssc_info *ssc_p;
	struct platform_device *pdev = to_platform_device(component->dev);
	u32 cr;

	if (!snd_soc_component_active(component))
		return 0;

	ssc_p = &ssc_info[pdev->id];

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

#define ATMEL_SSC_FORMATS (SNDRV_PCM_FMTBIT_S8     | SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops atmel_ssc_dai_ops = {
	.startup	= atmel_ssc_startup,
	.shutdown	= atmel_ssc_shutdown,
	.prepare	= atmel_ssc_prepare,
	.trigger	= atmel_ssc_trigger,
	.hw_params	= atmel_ssc_hw_params,
	.set_fmt	= atmel_ssc_set_dai_fmt,
	.set_clkdiv	= atmel_ssc_set_dai_clkdiv,
};

static struct snd_soc_dai_driver atmel_ssc_dai = {
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min = 8000,
			.rate_max = 384000,
			.formats = ATMEL_SSC_FORMATS,},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min = 8000,
			.rate_max = 384000,
			.formats = ATMEL_SSC_FORMATS,},
		.ops = &atmel_ssc_dai_ops,
};

static const struct snd_soc_component_driver atmel_ssc_component = {
	.name		= "atmel-ssc",
	.suspend	= atmel_ssc_suspend,
	.resume		= atmel_ssc_resume,
};

static int asoc_ssc_init(struct device *dev)
{
	struct ssc_device *ssc = dev_get_drvdata(dev);
	int ret;

	ret = devm_snd_soc_register_component(dev, &atmel_ssc_component,
					 &atmel_ssc_dai, 1);
	if (ret) {
		dev_err(dev, "Could not register DAI: %d\n", ret);
		return ret;
	}

	if (ssc->pdata->use_dma)
		ret = atmel_pcm_dma_platform_register(dev);
	else
		ret = atmel_pcm_pdc_platform_register(dev);

	if (ret) {
		dev_err(dev, "Could not register PCM: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * atmel_ssc_set_audio - Allocate the specified SSC for audio use.
 * @ssc_id: SSD ID in [0, NUM_SSC_DEVICES[
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

	ssc_free(ssc);
}
EXPORT_SYMBOL_GPL(atmel_ssc_put_audio);

/* Module information */
MODULE_AUTHOR("Sedji Gaouaou, sedji.gaouaou@atmel.com, www.atmel.com");
MODULE_DESCRIPTION("ATMEL SSC ASoC Interface");
MODULE_LICENSE("GPL");

/*
 * at91-i2s.c  --  ALSA SoC I2S Audio Layer Platform driver
 *
 * Author: Frank Mandarino <fmandarino@endrelia.com>
 *         Endrelia Technologies Inc.
 *
 * Based on pxa2xx Platform drivers by
 * Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/atmel_pdc.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <asm/arch/hardware.h>
#include <asm/arch/at91_pmc.h>
#include <asm/arch/at91_ssc.h>

#include "at91-pcm.h"
#include "at91-i2s.h"

#if 0
#define	DBG(x...)	printk(KERN_DEBUG "at91-i2s:" x)
#else
#define	DBG(x...)
#endif

#if defined(CONFIG_ARCH_AT91SAM9260)
#define NUM_SSC_DEVICES		1
#else
#define NUM_SSC_DEVICES		3
#endif


/*
 * SSC PDC registers required by the PCM DMA engine.
 */
static struct at91_pdc_regs pdc_tx_reg = {
	.xpr		= ATMEL_PDC_TPR,
	.xcr		= ATMEL_PDC_TCR,
	.xnpr		= ATMEL_PDC_TNPR,
	.xncr		= ATMEL_PDC_TNCR,
};

static struct at91_pdc_regs pdc_rx_reg = {
	.xpr		= ATMEL_PDC_RPR,
	.xcr		= ATMEL_PDC_RCR,
	.xnpr		= ATMEL_PDC_RNPR,
	.xncr		= ATMEL_PDC_RNCR,
};

/*
 * SSC & PDC status bits for transmit and receive.
 */
static struct at91_ssc_mask ssc_tx_mask = {
	.ssc_enable	= AT91_SSC_TXEN,
	.ssc_disable	= AT91_SSC_TXDIS,
	.ssc_endx	= AT91_SSC_ENDTX,
	.ssc_endbuf	= AT91_SSC_TXBUFE,
	.pdc_enable	= ATMEL_PDC_TXTEN,
	.pdc_disable	= ATMEL_PDC_TXTDIS,
};

static struct at91_ssc_mask ssc_rx_mask = {
	.ssc_enable	= AT91_SSC_RXEN,
	.ssc_disable	= AT91_SSC_RXDIS,
	.ssc_endx	= AT91_SSC_ENDRX,
	.ssc_endbuf	= AT91_SSC_RXBUFF,
	.pdc_enable	= ATMEL_PDC_RXTEN,
	.pdc_disable	= ATMEL_PDC_RXTDIS,
};


/*
 * DMA parameters.
 */
static struct at91_pcm_dma_params ssc_dma_params[NUM_SSC_DEVICES][2] = {
	{{
	.name		= "SSC0/I2S PCM Stereo out",
	.pdc		= &pdc_tx_reg,
	.mask		= &ssc_tx_mask,
	},
	{
	.name		= "SSC0/I2S PCM Stereo in",
	.pdc		= &pdc_rx_reg,
	.mask		= &ssc_rx_mask,
	}},
#if NUM_SSC_DEVICES == 3
	{{
	.name		= "SSC1/I2S PCM Stereo out",
	.pdc		= &pdc_tx_reg,
	.mask		= &ssc_tx_mask,
	},
	{
	.name		= "SSC1/I2S PCM Stereo in",
	.pdc		= &pdc_rx_reg,
	.mask		= &ssc_rx_mask,
	}},
	{{
	.name		= "SSC2/I2S PCM Stereo out",
	.pdc		= &pdc_tx_reg,
	.mask		= &ssc_tx_mask,
	},
	{
	.name		= "SSC1/I2S PCM Stereo in",
	.pdc		= &pdc_rx_reg,
	.mask		= &ssc_rx_mask,
	}},
#endif
};

struct at91_ssc_state {
	u32	ssc_cmr;
	u32	ssc_rcmr;
	u32	ssc_rfmr;
	u32	ssc_tcmr;
	u32	ssc_tfmr;
	u32	ssc_sr;
	u32	ssc_imr;
};

static struct at91_ssc_info {
	char		*name;
	struct at91_ssc_periph ssc;
	spinlock_t 	lock;		/* lock for dir_mask */
	unsigned short	dir_mask;	/* 0=unused, 1=playback, 2=capture */
	unsigned short	initialized;	/* 1=SSC has been initialized */
	unsigned short	daifmt;
	unsigned short	cmr_div;
	unsigned short	tcmr_period;
	unsigned short	rcmr_period;
	struct at91_pcm_dma_params *dma_params[2];
	struct at91_ssc_state ssc_state;

} ssc_info[NUM_SSC_DEVICES] = {
	{
	.name		= "ssc0",
	.lock		= SPIN_LOCK_UNLOCKED,
	.dir_mask	= 0,
	.initialized	= 0,
	},
#if NUM_SSC_DEVICES == 3
	{
	.name		= "ssc1",
	.lock		= SPIN_LOCK_UNLOCKED,
	.dir_mask	= 0,
	.initialized	= 0,
	},
	{
	.name		= "ssc2",
	.lock		= SPIN_LOCK_UNLOCKED,
	.dir_mask	= 0,
	.initialized	= 0,
	},
#endif
};

static unsigned int at91_i2s_sysclk;

/*
 * SSC interrupt handler.  Passes PDC interrupts to the DMA
 * interrupt handler in the PCM driver.
 */
static irqreturn_t at91_i2s_interrupt(int irq, void *dev_id)
{
	struct at91_ssc_info *ssc_p = dev_id;
	struct at91_pcm_dma_params *dma_params;
	u32 ssc_sr;
	int i;

	ssc_sr = at91_ssc_read(ssc_p->ssc.base + AT91_SSC_SR)
			& at91_ssc_read(ssc_p->ssc.base + AT91_SSC_IMR);

	/*
	 * Loop through the substreams attached to this SSC.  If
	 * a DMA-related interrupt occurred on that substream, call
	 * the DMA interrupt handler function, if one has been
	 * registered in the dma_params structure by the PCM driver.
	 */
	for (i = 0; i < ARRAY_SIZE(ssc_p->dma_params); i++) {
		dma_params = ssc_p->dma_params[i];

		if (dma_params != NULL && dma_params->dma_intr_handler != NULL &&
			(ssc_sr &
			(dma_params->mask->ssc_endx | dma_params->mask->ssc_endbuf)))

			dma_params->dma_intr_handler(ssc_sr, dma_params->substream);
	}

	return IRQ_HANDLED;
}

/*
 * Startup.  Only that one substream allowed in each direction.
 */
static int at91_i2s_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct at91_ssc_info *ssc_p = &ssc_info[rtd->dai->cpu_dai->id];
	int dir_mask;

	DBG("i2s_startup: SSC_SR=0x%08lx\n",
			at91_ssc_read(ssc_p->ssc.base + AT91_SSC_SR));
	dir_mask = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0x1 : 0x2;

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
static void at91_i2s_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct at91_ssc_info *ssc_p = &ssc_info[rtd->dai->cpu_dai->id];
	struct at91_pcm_dma_params *dma_params;
	int dir, dir_mask;

	dir = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0 : 1;
	dma_params = ssc_p->dma_params[dir];

	if (dma_params != NULL) {
		at91_ssc_write(dma_params->ssc_base + AT91_SSC_CR,
				dma_params->mask->ssc_disable);
		DBG("%s disabled SSC_SR=0x%08lx\n", (dir ? "receive" : "transmit"),
			at91_ssc_read(ssc_p->ssc.base + AT91_SSC_SR));

		dma_params->ssc_base = NULL;
		dma_params->substream = NULL;
		ssc_p->dma_params[dir] = NULL;
	}

	dir_mask = 1 << dir;

	spin_lock_irq(&ssc_p->lock);
	ssc_p->dir_mask &= ~dir_mask;
	if (!ssc_p->dir_mask) {
		/* Shutdown the SSC clock. */
		DBG("Stopping pid %d clock\n", ssc_p->ssc.pid);
		at91_sys_write(AT91_PMC_PCDR, 1<<ssc_p->ssc.pid);

		if (ssc_p->initialized) {
			free_irq(ssc_p->ssc.pid, ssc_p);
			ssc_p->initialized = 0;
		}

		/* Reset the SSC */
		at91_ssc_write(ssc_p->ssc.base + AT91_SSC_CR, AT91_SSC_SWRST);

		/* Clear the SSC dividers */
		ssc_p->cmr_div = ssc_p->tcmr_period = ssc_p->rcmr_period = 0;
	}
	spin_unlock_irq(&ssc_p->lock);
}

/*
 * Record the SSC system clock rate.
 */
static int at91_i2s_set_dai_sysclk(struct snd_soc_cpu_dai *cpu_dai,
		int clk_id, unsigned int freq, int dir)
{
	/*
	 * The only clock supplied to the SSC is the AT91 master clock,
	 * which is only used if the SSC is generating BCLK and/or
	 * LRC clocks.
	 */
	switch (clk_id) {
	case AT91_SYSCLK_MCK:
		at91_i2s_sysclk = freq;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Record the DAI format for use in hw_params().
 */
static int at91_i2s_set_dai_fmt(struct snd_soc_cpu_dai *cpu_dai,
		unsigned int fmt)
{
	struct at91_ssc_info *ssc_p = &ssc_info[cpu_dai->id];

	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_I2S)
		return -EINVAL;

	ssc_p->daifmt = fmt;
	return 0;
}

/*
 * Record SSC clock dividers for use in hw_params().
 */
static int at91_i2s_set_dai_clkdiv(struct snd_soc_cpu_dai *cpu_dai,
	int div_id, int div)
{
	struct at91_ssc_info *ssc_p = &ssc_info[cpu_dai->id];

	switch (div_id) {
	case AT91SSC_CMR_DIV:
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

	case AT91SSC_TCMR_PERIOD:
		ssc_p->tcmr_period = div;
		break;

	case AT91SSC_RCMR_PERIOD:
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
static int at91_i2s_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int id = rtd->dai->cpu_dai->id;
	struct at91_ssc_info *ssc_p = &ssc_info[id];
	struct at91_pcm_dma_params *dma_params;
	int dir, channels, bits;
	u32 tfmr, rfmr, tcmr, rcmr;
	int start_event;
	int ret;

	/*
	 * Currently, there is only one set of dma params for
	 * each direction.  If more are added, this code will
	 * have to be changed to select the proper set.
	 */
	dir = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0 : 1;

	dma_params = &ssc_dma_params[id][dir];
	dma_params->ssc_base = ssc_p->ssc.base;
	dma_params->substream = substream;

	ssc_p->dma_params[dir] = dma_params;

	/*
	 * The cpu_dai->dma_data field is only used to communicate the
	 * appropriate DMA parameters to the pcm driver hw_params()
	 * function.  It should not be used for other purposes
	 * as it is common to all substreams.
	 */
	rtd->dai->cpu_dai->dma_data = dma_params;

	channels = params_channels(params);

	/*
	 * The SSC only supports up to 16-bit samples in I2S format, due
	 * to the size of the Frame Mode Register FSLEN field.  Also, I2S
	 * implies signed data.
	 */
	bits = 16;
	dma_params->pdc_xfer_size = 2;

	/*
	 * Compute SSC register settings.
	 */
	switch (ssc_p->daifmt) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/*
		 * SSC provides BCLK and LRC clocks.
		 *
		 * The SSC transmit and receive clocks are generated from the
		 * MCK divider, and the BCLK signal is output on the SSC TK line.
		 */
		rcmr =	  (( ssc_p->rcmr_period		<< 24) & AT91_SSC_PERIOD)
			| (( 1				<< 16) & AT91_SSC_STTDLY)
			| (( AT91_SSC_START_FALLING_RF	     ) & AT91_SSC_START)
			| (( AT91_SSC_CK_RISING		     ) & AT91_SSC_CKI)
			| (( AT91_SSC_CKO_NONE		     ) & AT91_SSC_CKO)
			| (( AT91_SSC_CKS_DIV		     ) & AT91_SSC_CKS);

		rfmr =	  (( AT91_SSC_FSEDGE_POSITIVE	     ) & AT91_SSC_FSEDGE)
			| (( AT91_SSC_FSOS_NEGATIVE	     ) & AT91_SSC_FSOS)
			| (((bits - 1)			<< 16) & AT91_SSC_FSLEN)
			| (((channels - 1)		<<  8) & AT91_SSC_DATNB)
			| (( 1				<<  7) & AT91_SSC_MSBF)
			| (( 0				<<  5) & AT91_SSC_LOOP)
			| (((bits - 1)			<<  0) & AT91_SSC_DATALEN);

		tcmr =	  (( ssc_p->tcmr_period		<< 24) & AT91_SSC_PERIOD)
			| (( 1				<< 16) & AT91_SSC_STTDLY)
			| (( AT91_SSC_START_FALLING_RF       ) & AT91_SSC_START)
			| (( AT91_SSC_CKI_FALLING	     ) & AT91_SSC_CKI)
			| (( AT91_SSC_CKO_CONTINUOUS	     ) & AT91_SSC_CKO)
			| (( AT91_SSC_CKS_DIV		     ) & AT91_SSC_CKS);

		tfmr =	  (( AT91_SSC_FSEDGE_POSITIVE	     ) & AT91_SSC_FSEDGE)
			| (( 0				<< 23) & AT91_SSC_FSDEN)
			| (( AT91_SSC_FSOS_NEGATIVE	     ) & AT91_SSC_FSOS)
			| (((bits - 1)			<< 16) & AT91_SSC_FSLEN)
			| (((channels - 1)		<<  8) & AT91_SSC_DATNB)
			| (( 1				<<  7) & AT91_SSC_MSBF)
			| (( 0				<<  5) & AT91_SSC_DATDEF)
			| (((bits - 1)			<<  0) & AT91_SSC_DATALEN);
		break;

	case SND_SOC_DAIFMT_CBM_CFM:

		/*
		 * CODEC supplies BCLK and LRC clocks.
		 *
		 * The SSC transmit clock is obtained from the BCLK signal on
		 * on the TK line, and the SSC receive clock is generated from the
		 * transmit clock.
		 *
		 * For single channel data, one sample is transferred on the falling
		 * edge of the LRC clock.  For two channel data, one sample is
		 * transferred on both edges of the LRC clock.
		 */
		start_event = channels == 1
				? AT91_SSC_START_FALLING_RF
				: AT91_SSC_START_EDGE_RF;

		rcmr =	  (( 0				<< 24) & AT91_SSC_PERIOD)
			| (( 1				<< 16) & AT91_SSC_STTDLY)
			| (( start_event		     ) & AT91_SSC_START)
			| (( AT91_SSC_CK_RISING		     ) & AT91_SSC_CKI)
			| (( AT91_SSC_CKO_NONE		     ) & AT91_SSC_CKO)
			| (( AT91_SSC_CKS_CLOCK		     ) & AT91_SSC_CKS);

		rfmr =	  (( AT91_SSC_FSEDGE_POSITIVE	     ) & AT91_SSC_FSEDGE)
			| (( AT91_SSC_FSOS_NONE		     ) & AT91_SSC_FSOS)
			| (( 0				<< 16) & AT91_SSC_FSLEN)
			| (( 0				<<  8) & AT91_SSC_DATNB)
			| (( 1				<<  7) & AT91_SSC_MSBF)
			| (( 0				<<  5) & AT91_SSC_LOOP)
			| (((bits - 1)			<<  0) & AT91_SSC_DATALEN);

		tcmr =	  (( 0				<< 24) & AT91_SSC_PERIOD)
			| (( 1				<< 16) & AT91_SSC_STTDLY)
			| (( start_event		     ) & AT91_SSC_START)
			| (( AT91_SSC_CKI_FALLING	     ) & AT91_SSC_CKI)
			| (( AT91_SSC_CKO_NONE		     ) & AT91_SSC_CKO)
			| (( AT91_SSC_CKS_PIN		     ) & AT91_SSC_CKS);

		tfmr =	  (( AT91_SSC_FSEDGE_POSITIVE	     ) & AT91_SSC_FSEDGE)
			| (( 0				<< 23) & AT91_SSC_FSDEN)
			| (( AT91_SSC_FSOS_NONE		     ) & AT91_SSC_FSOS)
			| (( 0				<< 16) & AT91_SSC_FSLEN)
			| (( 0				<<  8) & AT91_SSC_DATNB)
			| (( 1				<<  7) & AT91_SSC_MSBF)
			| (( 0				<<  5) & AT91_SSC_DATDEF)
			| (((bits - 1)			<<  0) & AT91_SSC_DATALEN);
		break;

	case SND_SOC_DAIFMT_CBS_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
	default:
		printk(KERN_WARNING "at91-i2s: unsupported DAI format 0x%x.\n",
			ssc_p->daifmt);
		return -EINVAL;
		break;
	}
	DBG("RCMR=%08x RFMR=%08x TCMR=%08x TFMR=%08x\n", rcmr, rfmr, tcmr, tfmr);

	if (!ssc_p->initialized) {

		/* Enable PMC peripheral clock for this SSC */
		DBG("Starting pid %d clock\n", ssc_p->ssc.pid);
		at91_sys_write(AT91_PMC_PCER, 1<<ssc_p->ssc.pid);

		/* Reset the SSC and its PDC registers */
		at91_ssc_write(ssc_p->ssc.base + AT91_SSC_CR, AT91_SSC_SWRST);

		at91_ssc_write(ssc_p->ssc.base + ATMEL_PDC_RPR, 0);
		at91_ssc_write(ssc_p->ssc.base + ATMEL_PDC_RCR, 0);
		at91_ssc_write(ssc_p->ssc.base + ATMEL_PDC_RNPR, 0);
		at91_ssc_write(ssc_p->ssc.base + ATMEL_PDC_RNCR, 0);
		at91_ssc_write(ssc_p->ssc.base + ATMEL_PDC_TPR, 0);
		at91_ssc_write(ssc_p->ssc.base + ATMEL_PDC_TCR, 0);
		at91_ssc_write(ssc_p->ssc.base + ATMEL_PDC_TNPR, 0);
		at91_ssc_write(ssc_p->ssc.base + ATMEL_PDC_TNCR, 0);

		if ((ret = request_irq(ssc_p->ssc.pid, at91_i2s_interrupt,
					0, ssc_p->name, ssc_p)) < 0) {
			printk(KERN_WARNING "at91-i2s: request_irq failure\n");

			DBG("Stopping pid %d clock\n", ssc_p->ssc.pid);
			at91_sys_write(AT91_PMC_PCER, 1<<ssc_p->ssc.pid);
			return ret;
		}

		ssc_p->initialized = 1;
	}

	/* set SSC clock mode register */
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_CMR, ssc_p->cmr_div);

	/* set receive clock mode and format */
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_RCMR, rcmr);
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_RFMR, rfmr);

	/* set transmit clock mode and format */
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_TCMR, tcmr);
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_TFMR, tfmr);

	DBG("hw_params: SSC initialized\n");
	return 0;
}


static int at91_i2s_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct at91_ssc_info *ssc_p = &ssc_info[rtd->dai->cpu_dai->id];
	struct at91_pcm_dma_params *dma_params;
	int dir;

	dir = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0 : 1;
	dma_params = ssc_p->dma_params[dir];

	at91_ssc_write(dma_params->ssc_base + AT91_SSC_CR,
			dma_params->mask->ssc_enable);

	DBG("%s enabled SSC_SR=0x%08lx\n", dir ? "receive" : "transmit",
		at91_ssc_read(dma_params->ssc_base + AT91_SSC_SR));
	return 0;
}


#ifdef CONFIG_PM
static int at91_i2s_suspend(struct platform_device *pdev,
	struct snd_soc_cpu_dai *cpu_dai)
{
	struct at91_ssc_info *ssc_p;

	if(!cpu_dai->active)
		return 0;

	ssc_p = &ssc_info[cpu_dai->id];

	/* Save the status register before disabling transmit and receive. */
	ssc_p->ssc_state.ssc_sr = at91_ssc_read(ssc_p->ssc.base + AT91_SSC_SR);
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_CR,
			AT91_SSC_TXDIS | AT91_SSC_RXDIS);

	/* Save the current interrupt mask, then disable unmasked interrupts. */
	ssc_p->ssc_state.ssc_imr = at91_ssc_read(ssc_p->ssc.base + AT91_SSC_IMR);
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_IDR, ssc_p->ssc_state.ssc_imr);

	ssc_p->ssc_state.ssc_cmr  = at91_ssc_read(ssc_p->ssc.base + AT91_SSC_CMR);
	ssc_p->ssc_state.ssc_rcmr = at91_ssc_read(ssc_p->ssc.base + AT91_SSC_RCMR);
	ssc_p->ssc_state.ssc_rfmr = at91_ssc_read(ssc_p->ssc.base + AT91_SSC_RFMR);
	ssc_p->ssc_state.ssc_tcmr = at91_ssc_read(ssc_p->ssc.base + AT91_SSC_TCMR);
	ssc_p->ssc_state.ssc_tfmr = at91_ssc_read(ssc_p->ssc.base + AT91_SSC_TFMR);

	return 0;
}

static int at91_i2s_resume(struct platform_device *pdev,
	struct snd_soc_cpu_dai *cpu_dai)
{
	struct at91_ssc_info *ssc_p;

	if(!cpu_dai->active)
		return 0;

	ssc_p = &ssc_info[cpu_dai->id];

	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_TFMR, ssc_p->ssc_state.ssc_tfmr);
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_TCMR, ssc_p->ssc_state.ssc_tcmr);
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_RFMR, ssc_p->ssc_state.ssc_rfmr);
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_RCMR, ssc_p->ssc_state.ssc_rcmr);
	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_CMR,  ssc_p->ssc_state.ssc_cmr);

	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_IER,  ssc_p->ssc_state.ssc_imr);

	at91_ssc_write(ssc_p->ssc.base + AT91_SSC_CR,
		((ssc_p->ssc_state.ssc_sr & AT91_SSC_RXENA) ? AT91_SSC_RXEN : 0) |
		((ssc_p->ssc_state.ssc_sr & AT91_SSC_TXENA) ? AT91_SSC_TXEN : 0));

	return 0;
}

#else
#define at91_i2s_suspend	NULL
#define at91_i2s_resume		NULL
#endif

#define AT91_I2S_RATES (SNDRV_PCM_RATE_8000  | SNDRV_PCM_RATE_11025 |\
			SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
			SNDRV_PCM_RATE_96000)

struct snd_soc_cpu_dai at91_i2s_dai[NUM_SSC_DEVICES] = {
	{	.name = "at91_ssc0/i2s",
		.id = 0,
		.type = SND_SOC_DAI_I2S,
		.suspend = at91_i2s_suspend,
		.resume = at91_i2s_resume,
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = AT91_I2S_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = AT91_I2S_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,},
		.ops = {
			.startup = at91_i2s_startup,
			.shutdown = at91_i2s_shutdown,
			.prepare = at91_i2s_prepare,
			.hw_params = at91_i2s_hw_params,},
		.dai_ops = {
			.set_sysclk = at91_i2s_set_dai_sysclk,
			.set_fmt = at91_i2s_set_dai_fmt,
			.set_clkdiv = at91_i2s_set_dai_clkdiv,},
		.private_data = &ssc_info[0].ssc,
	},
#if NUM_SSC_DEVICES == 3
	{	.name = "at91_ssc1/i2s",
		.id = 1,
		.type = SND_SOC_DAI_I2S,
		.suspend = at91_i2s_suspend,
		.resume = at91_i2s_resume,
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = AT91_I2S_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = AT91_I2S_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,},
		.ops = {
			.startup = at91_i2s_startup,
			.shutdown = at91_i2s_shutdown,
			.prepare = at91_i2s_prepare,
			.hw_params = at91_i2s_hw_params,},
		.dai_ops = {
			.set_sysclk = at91_i2s_set_dai_sysclk,
			.set_fmt = at91_i2s_set_dai_fmt,
			.set_clkdiv = at91_i2s_set_dai_clkdiv,},
		.private_data = &ssc_info[1].ssc,
	},
	{	.name = "at91_ssc2/i2s",
		.id = 2,
		.type = SND_SOC_DAI_I2S,
		.suspend = at91_i2s_suspend,
		.resume = at91_i2s_resume,
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = AT91_I2S_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = AT91_I2S_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,},
		.ops = {
			.startup = at91_i2s_startup,
			.shutdown = at91_i2s_shutdown,
			.prepare = at91_i2s_prepare,
			.hw_params = at91_i2s_hw_params,},
		.dai_ops = {
			.set_sysclk = at91_i2s_set_dai_sysclk,
			.set_fmt = at91_i2s_set_dai_fmt,
			.set_clkdiv = at91_i2s_set_dai_clkdiv,},
		.private_data = &ssc_info[2].ssc,
	},
#endif
};

EXPORT_SYMBOL_GPL(at91_i2s_dai);

/* Module information */
MODULE_AUTHOR("Frank Mandarino, fmandarino@endrelia.com, www.endrelia.com");
MODULE_DESCRIPTION("AT91 I2S ASoC Interface");
MODULE_LICENSE("GPL");

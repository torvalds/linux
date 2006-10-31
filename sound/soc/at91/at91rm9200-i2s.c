/*
 * at91rm9200-i2s.c  --  ALSA Soc Audio Layer Platform driver and DMA engine
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
 *  Revision history
 *     3rd Mar 2006   Initial version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <asm/arch/at91rm9200.h>
#include <asm/arch/at91rm9200_ssc.h>
#include <asm/arch/at91rm9200_pdc.h>
#include <asm/arch/hardware.h>

#include "at91rm9200-pcm.h"

#if 0
#define	DBG(x...)	printk(KERN_DEBUG "at91rm9200-i2s:" x)
#else
#define	DBG(x...)
#endif

#define AT91RM9200_I2S_DAIFMT \
	(SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_NB_NF)

#define AT91RM9200_I2S_DIR \
	(SND_SOC_DAIDIR_PLAYBACK | SND_SOC_DAIDIR_CAPTURE)

/* priv is (SSC_CMR.DIV << 16 | SSC_TCMR.PERIOD ) */
static struct snd_soc_dai_mode at91rm9200_i2s[] = {

	/* 8k: BCLK = (MCLK/10) = (60MHz/50) = 1.2MHz */
	{
		.fmt = AT91RM9200_I2S_DAIFMT,
		.pcmfmt = SNDRV_PCM_FMTBIT_S16_LE,
		.pcmrate = SNDRV_PCM_RATE_8000,
		.pcmdir = AT91RM9200_I2S_DIR,
		.flags = SND_SOC_DAI_BFS_DIV,
		.fs = 1500,
		.bfs = SND_SOC_FSBD(10),
		.priv = (25 << 16 | 74),
	},

	/* 16k: BCLK = (MCLK/3) ~= (60MHz/14) = 4.285714MHz */
	{
		.fmt = AT91RM9200_I2S_DAIFMT,
		.pcmfmt = SNDRV_PCM_FMTBIT_S16_LE,
		.pcmrate = SNDRV_PCM_RATE_16000,
		.pcmdir = AT91RM9200_I2S_DIR,
		.flags = SND_SOC_DAI_BFS_DIV,
		.fs = 750,
		.bfs = SND_SOC_FSBD(3),
		.priv = (7 << 16 | 133),
	},

	/* 32k: BCLK = (MCLK/3) ~= (60MHz/14) = 4.285714MHz */
	{
		.fmt = AT91RM9200_I2S_DAIFMT,
		.pcmfmt = SNDRV_PCM_FMTBIT_S16_LE,
		.pcmrate = SNDRV_PCM_RATE_32000,
		.pcmdir = AT91RM9200_I2S_DIR,
		.flags = SND_SOC_DAI_BFS_DIV,
		.fs = 375,
		.bfs = SND_SOC_FSBD(3),
		.priv = (7 << 16 | 66),
	},

	/* 48k: BCLK = (MCLK/5) ~= (60MHz/26) = 2.3076923MHz */
	{
		.fmt = AT91RM9200_I2S_DAIFMT,
		.pcmfmt = SNDRV_PCM_FMTBIT_S16_LE,
		.pcmrate = SNDRV_PCM_RATE_48000,
		.pcmdir = AT91RM9200_I2S_DIR,
		.flags = SND_SOC_DAI_BFS_DIV,
		.fs = 250,
		.bfs SND_SOC_FSBD(5),
		.priv = (13 << 16 | 23),
	},
};


/*
 * SSC registers required by the PCM DMA engine.
 */
static struct at91rm9200_ssc_regs ssc_reg[3] = {
	{
	.cr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_SSC_CR),
	.ier	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_SSC_IER),
	.idr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_SSC_IDR),
	},
	{
	.cr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_SSC_CR),
	.ier	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_SSC_IER),
	.idr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_SSC_IDR),
	},
	{
	.cr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_SSC_CR),
	.ier	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_SSC_IER),
	.idr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_SSC_IDR),
	},
};

static struct at91rm9200_pdc_regs pdc_tx_reg[3] = {
	{
	.xpr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_TPR),
	.xcr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_TCR),
	.xnpr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_TNPR),
	.xncr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_TNCR),
	.ptcr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_PTCR),
	},
	{
	.xpr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_TPR),
	.xcr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_TCR),
	.xnpr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_TNPR),
	.xncr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_TNCR),
	.ptcr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_PTCR),
	},
	{
	.xpr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_TPR),
	.xcr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_TCR),
	.xnpr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_TNPR),
	.xncr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_TNCR),
	.ptcr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_PTCR),
	},
};

static struct at91rm9200_pdc_regs pdc_rx_reg[3] = {
	{
	.xpr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_RPR),
	.xcr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_RCR),
	.xnpr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_RNPR),
	.xncr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_RNCR),
	.ptcr	= (void __iomem *) (AT91_VA_BASE_SSC0 + AT91_PDC_PTCR),
	},
	{
	.xpr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_RPR),
	.xcr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_RCR),
	.xnpr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_RNPR),
	.xncr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_RNCR),
	.ptcr	= (void __iomem *) (AT91_VA_BASE_SSC1 + AT91_PDC_PTCR),
	},
	{
	.xpr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_RPR),
	.xcr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_RCR),
	.xnpr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_RNPR),
	.xncr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_RNCR),
	.ptcr	= (void __iomem *) (AT91_VA_BASE_SSC2 + AT91_PDC_PTCR),
	},
};

/*
 * SSC & PDC status bits for transmit and receive.
 */
static struct at91rm9200_ssc_mask ssc_tx_mask = {
	.ssc_enable	= AT91_SSC_TXEN,
	.ssc_disable	= AT91_SSC_TXDIS,
	.ssc_endx	= AT91_SSC_ENDTX,
	.ssc_endbuf	= AT91_SSC_TXBUFE,
	.pdc_enable	= AT91_PDC_TXTEN,
	.pdc_disable	= AT91_PDC_TXTDIS,
};

static struct at91rm9200_ssc_mask ssc_rx_mask = {
	.ssc_enable	= AT91_SSC_RXEN,
	.ssc_disable	= AT91_SSC_RXDIS,
	.ssc_endx	= AT91_SSC_ENDRX,
	.ssc_endbuf	= AT91_SSC_RXBUFF,
	.pdc_enable	= AT91_PDC_RXTEN,
	.pdc_disable	= AT91_PDC_RXTDIS,
};

/*
 * A MUTEX is used to protect an SSC initialzed flag which allows
 * the substream hw_params() call to initialize the SSC only if
 * there are no other substreams open.  If there are other
 * substreams open, the hw_param() call can only check that
 * it is using the same format and rate.
 */
static DECLARE_MUTEX(ssc0_mutex);
static DECLARE_MUTEX(ssc1_mutex);
static DECLARE_MUTEX(ssc2_mutex);

/*
 * DMA parameters.
 */
static at91rm9200_pcm_dma_params_t ssc_dma_params[3][2] = {
	{{
	.name		= "SSC0/I2S PCM Stereo out",
	.ssc		= &ssc_reg[0],
	.pdc		= &pdc_tx_reg[0],
	.mask		= &ssc_tx_mask,
	},
	{
	.name		= "SSC0/I2S PCM Stereo in",
	.ssc		= &ssc_reg[0],
	.pdc		= &pdc_rx_reg[0],
	.mask		= &ssc_rx_mask,
	}},
	{{
	.name		= "SSC1/I2S PCM Stereo out",
	.ssc		= &ssc_reg[1],
	.pdc		= &pdc_tx_reg[1],
	.mask		= &ssc_tx_mask,
	},
	{
	.name		= "SSC1/I2S PCM Stereo in",
	.ssc		= &ssc_reg[1],
	.pdc		= &pdc_rx_reg[1],
	.mask		= &ssc_rx_mask,
	}},
	{{
	.name		= "SSC2/I2S PCM Stereo out",
	.ssc		= &ssc_reg[2],
	.pdc		= &pdc_tx_reg[2],
	.mask		= &ssc_tx_mask,
	},
	{
	.name		= "SSC1/I2S PCM Stereo in",
	.ssc		= &ssc_reg[2],
	.pdc		= &pdc_rx_reg[2],
	.mask		= &ssc_rx_mask,
	}},
};


struct at91rm9200_ssc_state {
	u32	ssc_cmr;
	u32	ssc_rcmr;
	u32	ssc_rfmr;
	u32	ssc_tcmr;
	u32	ssc_tfmr;
	u32	ssc_sr;
	u32	ssc_imr;
};

static struct at91rm9200_ssc_info {
	char		*name;
	void __iomem    *ssc_base;
	u32		pid;
	spinlock_t 	lock;		/* lock for dir_mask */
	int		dir_mask;	/* 0=unused, 1=playback, 2=capture */
	struct semaphore *mutex;
	int		initialized;
	int		pcmfmt;
	int		rate;
	at91rm9200_pcm_dma_params_t *dma_params[2];
	struct at91rm9200_ssc_state ssc_state;

} ssc_info[3] = {
	{
	.name		= "ssc0",
	.ssc_base	= (void __iomem *) AT91_VA_BASE_SSC0,
	.pid		= AT91_ID_SSC0,
	.lock		= SPIN_LOCK_UNLOCKED,
	.dir_mask	= 0,
	.mutex		= &ssc0_mutex,
	.initialized	= 0,
	},
	{
	.name		= "ssc1",
	.ssc_base	= (void __iomem *) AT91_VA_BASE_SSC1,
	.pid		= AT91_ID_SSC1,
	.lock		= SPIN_LOCK_UNLOCKED,
	.dir_mask	= 0,
	.mutex		= &ssc1_mutex,
	.initialized	= 0,
	},
	{
	.name		= "ssc2",
	.ssc_base	= (void __iomem *) AT91_VA_BASE_SSC2,
	.pid		= AT91_ID_SSC2,
	.lock		= SPIN_LOCK_UNLOCKED,
	.dir_mask	= 0,
	.mutex		= &ssc2_mutex,
	.initialized	= 0,
	},
};


static irqreturn_t at91rm9200_i2s_interrupt(int irq, void *dev_id)
{
	struct at91rm9200_ssc_info *ssc_p = dev_id;
	at91rm9200_pcm_dma_params_t *dma_params;
	u32 ssc_sr;
	int i;

	ssc_sr = at91_ssc_read(ssc_p->ssc_base + AT91_SSC_SR)
			& at91_ssc_read(ssc_p->ssc_base + AT91_SSC_IMR);

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

static int at91rm9200_i2s_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct at91rm9200_ssc_info *ssc_p = &ssc_info[rtd->cpu_dai->id];
	int dir_mask;

	DBG("i2s_startup: SSC_SR=0x%08lx\n",
			at91_ssc_read(ssc_p->ssc_base + AT91_SSC_SR));
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

static void at91rm9200_i2s_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct at91rm9200_ssc_info *ssc_p = &ssc_info[rtd->cpu_dai->id];
	at91rm9200_pcm_dma_params_t *dma_params = rtd->cpu_dai->dma_data;
	int dir, dir_mask;

	dir = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0 : 1;

	if (dma_params != NULL) {
		at91_ssc_write(dma_params->ssc->cr, dma_params->mask->ssc_disable);
		DBG("%s disabled SSC_SR=0x%08lx\n", (dir ? "receive" : "transmit"),
			at91_ssc_read(ssc_p->ssc_base + AT91_SSC_SR));

		dma_params->substream = NULL;
		ssc_p->dma_params[dir] = NULL;
	}

	dir_mask = 1 << dir;

	spin_lock_irq(&ssc_p->lock);
	ssc_p->dir_mask &= ~dir_mask;
	if (!ssc_p->dir_mask) {
		/* Shutdown the SSC clock. */
		DBG("Stopping pid %d clock\n", ssc_p->pid);
		at91_sys_write(AT91_PMC_PCDR, 1<<ssc_p->pid);

		if (ssc_p->initialized)
			free_irq(ssc_p->pid, ssc_p);

		/* Reset the SSC */
		at91_ssc_write(ssc_p->ssc_base + AT91_SSC_CR, AT91_SSC_SWRST);

		/* Force a re-init on the next hw_params() call. */
		ssc_p->initialized = 0;
	}
	spin_unlock_irq(&ssc_p->lock);
}

#ifdef CONFIG_PM
static int at91rm9200_i2s_suspend(struct platform_device *pdev,
	struct snd_soc_cpu_dai *dai)
{
	struct at91rm9200_ssc_info *ssc_p;

	if(!dai->active)
		return 0;

	ssc_p = &ssc_info[dai->id];

	/* Save the status register before disabling transmit and receive. */
	ssc_p->state->ssc_sr = at91_ssc_read(ssc_p->ssc_base + AT91_SSC_SR);
	at91_ssc_write(ssc_p->ssc_base +
		AT91_SSC_CR, AT91_SSC_TXDIS | AT91_SSC_RXDIS);

	/* Save the current interrupt mask, then disable unmasked interrupts. */
	ssc_p->state->ssc_imr = at91_ssc_read(ssc_p->ssc_base + AT91_SSC_IMR);
	at91_ssc_write(ssc_p->ssc_base + AT91_SSC_IDR, ssc_p->state->ssc_imr);

	ssc_p->state->ssc_cmr  = at91_ssc_read(ssc_p->ssc_base + AT91_SSC_CMR);
	ssc_p->state->ssc_rcmr = at91_ssc_read(ssc_p->ssc_base + AT91_SSC_RCMR);
	ssc_p->state->ssc_rfmr = at91_ssc_read(ssc_p->ssc_base + AT91_SSC_RCMR);
	ssc_p->state->ssc_tcmr = at91_ssc_read(ssc_p->ssc_base + AT91_SSC_RCMR);
	ssc_p->state->ssc_tfmr = at91_ssc_read(ssc_p->ssc_base + AT91_SSC_RCMR);

	return 0;
}

static int at91rm9200_i2s_resume(struct platform_device *pdev,
	struct snd_soc_cpu_dai *dai)
{
	struct at91rm9200_ssc_info *ssc_p;
	u32 cr_mask;

	if(!dai->active)
		return 0;

	ssc_p = &ssc_info[dai->id];

	at91_ssc_write(ssc_p->ssc_base + AT91_SSC_RCMR, ssc_p->state->ssc_tfmr);
	at91_ssc_write(ssc_p->ssc_base + AT91_SSC_RCMR, ssc_p->state->ssc_tcmr);
	at91_ssc_write(ssc_p->ssc_base + AT91_SSC_RCMR, ssc_p->state->ssc_rfmr);
	at91_ssc_write(ssc_p->ssc_base + AT91_SSC_RCMR, ssc_p->state->ssc_rcmr);
	at91_ssc_write(ssc_p->ssc_base + AT91_SSC_CMR,  ssc_p->state->ssc_cmr);

	at91_ssc_write(ssc_p->ssc_base + AT91_SSC_IER,  ssc_p->state->ssc_imr);

	at91_ssc_write(ssc_p->ssc_base + AT91_SSC_CR,
		((ssc_p->state->ssc_sr & AT91_SSC_RXENA) ? AT91_SSC_RXEN : 0) |
		((ssc_p->state->ssc_sr & AT91_SSC_TXENA) ? AT91_SSC_TXEN : 0));

	return 0;
}

#else
#define at91rm9200_i2s_suspend	NULL
#define at91rm9200_i2s_resume	NULL
#endif

static unsigned int at91rm9200_i2s_config_sysclk(
	struct snd_soc_cpu_dai *iface, struct snd_soc_clock_info *info,
	unsigned int clk)
{
	/* Currently, there is only support for USB (12Mhz) mode */
	if (clk != 12000000)
		return 0;
	return 12000000;
}

static int at91rm9200_i2s_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int id = rtd->cpu_dai->id;
	struct at91rm9200_ssc_info *ssc_p = &ssc_info[id];
	at91rm9200_pcm_dma_params_t *dma_params;
	unsigned int pcmfmt, rate;
	int dir, channels, bits;
	struct clk *mck_clk;
	unsigned long bclk;
	u32 div, period, tfmr, rfmr, tcmr, rcmr;
	int ret;

	/*
	 * Currently, there is only one set of dma params for
	 * each direction.  If more are added, this code will
	 * have to be changed to select the proper set.
	 */
	dir = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0 : 1;

	dma_params = &ssc_dma_params[id][dir];
	dma_params->substream = substream;

	ssc_p->dma_params[dir] = dma_params;
	rtd->cpu_dai->dma_data = dma_params;

	rate = params_rate(params);
	channels = params_channels(params);

	pcmfmt = rtd->cpu_dai->dai_runtime.pcmfmt;
	switch (pcmfmt) {
		case SNDRV_PCM_FMTBIT_S16_LE:
			/* likely this is all we'll ever support, but ... */
			bits = 16;
			dma_params->pdc_xfer_size = 2;
			break;
		default:
			printk(KERN_WARNING "at91rm9200-i2s: unsupported format %x\n",
				pcmfmt);
			return -EINVAL;
	}

	/* Don't allow both SSC substreams to initialize at the same time. */
	down(ssc_p->mutex);

	/*
	 * If this SSC is alreadly initialized, then this substream must use
	 * the same format and rate.
	 */
	if (ssc_p->initialized) {
		if (pcmfmt != ssc_p->pcmfmt || rate != ssc_p->rate) {
			printk(KERN_WARNING "at91rm9200-i2s: "
				"incompatible substream in other direction\n");
			up(ssc_p->mutex);
			return -EINVAL;
		}
	} else {
		/* Enable PMC peripheral clock for this SSC */
		DBG("Starting pid %d clock\n", ssc_p->pid);
		at91_sys_write(AT91_PMC_PCER, 1<<ssc_p->pid);

		/* Reset the SSC */
		at91_ssc_write(ssc_p->ssc_base + AT91_SSC_CR, AT91_SSC_SWRST);

		at91_ssc_write(ssc_p->ssc_base + AT91_PDC_RPR, 0);
		at91_ssc_write(ssc_p->ssc_base + AT91_PDC_RCR, 0);
		at91_ssc_write(ssc_p->ssc_base + AT91_PDC_RNPR, 0);
		at91_ssc_write(ssc_p->ssc_base + AT91_PDC_RNCR, 0);
		at91_ssc_write(ssc_p->ssc_base + AT91_PDC_TPR, 0);
		at91_ssc_write(ssc_p->ssc_base + AT91_PDC_TCR, 0);
		at91_ssc_write(ssc_p->ssc_base + AT91_PDC_TNPR, 0);
		at91_ssc_write(ssc_p->ssc_base + AT91_PDC_TNCR, 0);

		mck_clk = clk_get(NULL, "mck");

		div = rtd->cpu_dai->dai_runtime.priv >> 16;
		period = rtd->cpu_dai->dai_runtime.priv & 0xffff;
		bclk = 60000000 / (2 * div);

		DBG("mck %ld fsbd %d bfs %d bfs_real %d bclk %ld div %d period %d\n",
			clk_get_rate(mck_clk),
			SND_SOC_FSBD(6),
			rtd->cpu_dai->dai_runtime.bfs,
			SND_SOC_FSBD_REAL(rtd->cpu_dai->dai_runtime.bfs),
			bclk,
			div,
			period);

		clk_put(mck_clk);

		at91_ssc_write(ssc_p->ssc_base + AT91_SSC_CMR, div);

		/*
		 * Setup the TFMR and RFMR for the proper data format.
		 */
		tfmr =
		  (( AT91_SSC_FSEDGE_POSITIVE	     ) & AT91_SSC_FSEDGE)
		| (( 0				<< 23) & AT91_SSC_FSDEN)
		| (( AT91_SSC_FSOS_NEGATIVE	     ) & AT91_SSC_FSOS)
		| (((bits - 1)			<< 16) & AT91_SSC_FSLEN)
		| (((channels - 1)		<<  8) & AT91_SSC_DATNB)
		| (( 1				<<  7) & AT91_SSC_MSBF)
		| (( 0				<<  5) & AT91_SSC_DATDEF)
		| (((bits - 1)			<<  0) & AT91_SSC_DATALEN);
		DBG("SSC_TFMR=0x%08x\n", tfmr);
		at91_ssc_write(ssc_p->ssc_base + AT91_SSC_TFMR, tfmr);

		rfmr =
		  (( AT91_SSC_FSEDGE_POSITIVE	     ) & AT91_SSC_FSEDGE)
		| (( AT91_SSC_FSOS_NONE		     ) & AT91_SSC_FSOS)
		| (( 0				<< 16) & AT91_SSC_FSLEN)
		| (((channels - 1)		<<  8) & AT91_SSC_DATNB)
		| (( 1				<<  7) & AT91_SSC_MSBF)
		| (( 0				<<  5) & AT91_SSC_LOOP)
		| (((bits - 1)			<<  0) & AT91_SSC_DATALEN);

		DBG("SSC_RFMR=0x%08x\n", rfmr);
		at91_ssc_write(ssc_p->ssc_base + AT91_SSC_RFMR, rfmr);

		/*
		 * Setup the TCMR and RCMR to generate the proper BCLK
		 * and LRC signals.
		 */
		tcmr =
		  (( period			<< 24) & AT91_SSC_PERIOD)
		| (( 1				<< 16) & AT91_SSC_STTDLY)
		| (( AT91_SSC_START_FALLING_RF       ) & AT91_SSC_START)
		| (( AT91_SSC_CKI_FALLING	     ) & AT91_SSC_CKI)
		| (( AT91_SSC_CKO_CONTINUOUS	     ) & AT91_SSC_CKO)
		| (( AT91_SSC_CKS_DIV		     ) & AT91_SSC_CKS);

		DBG("SSC_TCMR=0x%08x\n", tcmr);
		at91_ssc_write(ssc_p->ssc_base + AT91_SSC_TCMR, tcmr);

		rcmr =
		  (( 0				<< 24) & AT91_SSC_PERIOD)
		| (( 1				<< 16) & AT91_SSC_STTDLY)
		| (( AT91_SSC_START_TX_RX	     ) & AT91_SSC_START)
		| (( AT91_SSC_CK_RISING		     ) & AT91_SSC_CKI)
		| (( AT91_SSC_CKO_NONE		     ) & AT91_SSC_CKO)
		| (( AT91_SSC_CKS_CLOCK		     ) & AT91_SSC_CKS);

		DBG("SSC_RCMR=0x%08x\n", rcmr);
		at91_ssc_write(ssc_p->ssc_base + AT91_SSC_RCMR, rcmr);

		if ((ret = request_irq(ssc_p->pid, at91rm9200_i2s_interrupt,
					0, ssc_p->name, ssc_p)) < 0) {
			printk(KERN_WARNING "at91rm9200-i2s: request_irq failure\n");
			return ret;
		}

		/*
		 * Save the current substream parameters in order to check
		 * that the substream in the opposite direction uses the
		 * same parameters.
		 */
		ssc_p->pcmfmt = pcmfmt;
		ssc_p->rate = rate;
		ssc_p->initialized = 1;

		DBG("hw_params: SSC initialized\n");
	}

	up(ssc_p->mutex);

	return 0;
}


static int at91rm9200_i2s_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	at91rm9200_pcm_dma_params_t *dma_params = rtd->cpu_dai->dma_data;

	at91_ssc_write(dma_params->ssc->cr, dma_params->mask->ssc_enable);

	DBG("%s enabled SSC_SR=0x%08lx\n",
	substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "transmit" : "receive",
	at91_ssc_read(ssc_info[rtd->cpu_dai->id].ssc_base + AT91_SSC_SR));
	return 0;
}


struct snd_soc_cpu_dai at91rm9200_i2s_dai[] = {
	{	.name = "at91rm9200-ssc0/i2s",
		.id = 0,
		.type = SND_SOC_DAI_I2S,
		.suspend = at91rm9200_i2s_suspend,
		.resume = at91rm9200_i2s_resume,
		.config_sysclk = at91rm9200_i2s_config_sysclk,
		.playback = {
			.channels_min = 1,
			.channels_max = 2,},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,},
		.ops = {
			.startup = at91rm9200_i2s_startup,
			.shutdown = at91rm9200_i2s_shutdown,
			.prepare = at91rm9200_i2s_prepare,
			.hw_params = at91rm9200_i2s_hw_params,},
		.caps = {
			.mode = &at91rm9200_i2s[0],
			.num_modes = ARRAY_SIZE(at91rm9200_i2s),},
	},
	{	.name = "at91rm9200-ssc1/i2s",
		.id = 1,
		.type = SND_SOC_DAI_I2S,
		.suspend = at91rm9200_i2s_suspend,
		.resume = at91rm9200_i2s_resume,
		.config_sysclk = at91rm9200_i2s_config_sysclk,
		.playback = {
			.channels_min = 1,
			.channels_max = 2,},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,},
		.ops = {
			.startup = at91rm9200_i2s_startup,
			.shutdown = at91rm9200_i2s_shutdown,
			.prepare = at91rm9200_i2s_prepare,
			.hw_params = at91rm9200_i2s_hw_params,},
		.caps = {
			.mode = &at91rm9200_i2s[0],
			.num_modes = ARRAY_SIZE(at91rm9200_i2s),},
	},
	{	.name = "at91rm9200-ssc2/i2s",
		.id = 2,
		.type = SND_SOC_DAI_I2S,
		.suspend = at91rm9200_i2s_suspend,
		.resume = at91rm9200_i2s_resume,
		.config_sysclk = at91rm9200_i2s_config_sysclk,
		.playback = {
			.channels_min = 1,
			.channels_max = 2,},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,},
		.ops = {
			.startup = at91rm9200_i2s_startup,
			.shutdown = at91rm9200_i2s_shutdown,
			.prepare = at91rm9200_i2s_prepare,
			.hw_params = at91rm9200_i2s_hw_params,},
		.caps = {
			.mode = &at91rm9200_i2s[0],
			.num_modes = ARRAY_SIZE(at91rm9200_i2s),},
	},
};

EXPORT_SYMBOL_GPL(at91rm9200_i2s_dai);

/* Module information */
MODULE_AUTHOR("Frank Mandarino, fmandarino@endrelia.com, www.endrelia.com");
MODULE_DESCRIPTION("AT91RM9200 I2S ASoC Interface");
MODULE_LICENSE("GPL");

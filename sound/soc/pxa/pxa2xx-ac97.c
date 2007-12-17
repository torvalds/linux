/*
 * linux/sound/pxa2xx-ac97.c -- AC97 support for the Intel PXA2xx chip.
 *
 * Author:	Nicolas Pitre
 * Created:	Dec 02, 2004
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <asm/irq.h>
#include <linux/mutex.h>
#include <asm/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/audio.h>

#include "pxa2xx-pcm.h"
#include "pxa2xx-ac97.h"

static DEFINE_MUTEX(car_mutex);
static DECLARE_WAIT_QUEUE_HEAD(gsr_wq);
static volatile long gsr_bits;

/*
 * Beware PXA27x bugs:
 *
 *   o Slot 12 read from modem space will hang controller.
 *   o CDONE, SDONE interrupt fails after any slot 12 IO.
 *
 * We therefore have an hybrid approach for waiting on SDONE (interrupt or
 * 1 jiffy timeout if interrupt never comes).
 */

static unsigned short pxa2xx_ac97_read(struct snd_ac97 *ac97,
	unsigned short reg)
{
	unsigned short val = -1;
	volatile u32 *reg_addr;

	mutex_lock(&car_mutex);

	/* set up primary or secondary codec/modem space */
#ifdef CONFIG_PXA27x
	reg_addr = ac97->num ? &SAC_REG_BASE : &PAC_REG_BASE;
#else
	if (reg == AC97_GPIO_STATUS)
		reg_addr = ac97->num ? &SMC_REG_BASE : &PMC_REG_BASE;
	else
		reg_addr = ac97->num ? &SAC_REG_BASE : &PAC_REG_BASE;
#endif
	reg_addr += (reg >> 1);

#ifndef CONFIG_PXA27x
	if (reg == AC97_GPIO_STATUS) {
		/* read from controller cache */
		val = *reg_addr;
		goto out;
	}
#endif

	/* start read access across the ac97 link */
	GSR = GSR_CDONE | GSR_SDONE;
	gsr_bits = 0;
	val = *reg_addr;

	wait_event_timeout(gsr_wq, (GSR | gsr_bits) & GSR_SDONE, 1);
	if (!((GSR | gsr_bits) & GSR_SDONE)) {
		printk(KERN_ERR "%s: read error (ac97_reg=%x GSR=%#lx)\n",
				__FUNCTION__, reg, GSR | gsr_bits);
		val = -1;
		goto out;
	}

	/* valid data now */
	GSR = GSR_CDONE | GSR_SDONE;
	gsr_bits = 0;
	val = *reg_addr;
	/* but we've just started another cycle... */
	wait_event_timeout(gsr_wq, (GSR | gsr_bits) & GSR_SDONE, 1);

out:	mutex_unlock(&car_mutex);
	return val;
}

static void pxa2xx_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
	unsigned short val)
{
	volatile u32 *reg_addr;

	mutex_lock(&car_mutex);

	/* set up primary or secondary codec/modem space */
#ifdef CONFIG_PXA27x
	reg_addr = ac97->num ? &SAC_REG_BASE : &PAC_REG_BASE;
#else
	if (reg == AC97_GPIO_STATUS)
		reg_addr = ac97->num ? &SMC_REG_BASE : &PMC_REG_BASE;
	else
		reg_addr = ac97->num ? &SAC_REG_BASE : &PAC_REG_BASE;
#endif
	reg_addr += (reg >> 1);

	GSR = GSR_CDONE | GSR_SDONE;
	gsr_bits = 0;
	*reg_addr = val;
	wait_event_timeout(gsr_wq, (GSR | gsr_bits) & GSR_CDONE, 1);
	if (!((GSR | gsr_bits) & GSR_CDONE))
		printk(KERN_ERR "%s: write error (ac97_reg=%x GSR=%#lx)\n",
				__FUNCTION__, reg, GSR | gsr_bits);

	mutex_unlock(&car_mutex);
}

static void pxa2xx_ac97_warm_reset(struct snd_ac97 *ac97)
{
	gsr_bits = 0;

#ifdef CONFIG_PXA27x
	/* warm reset broken on Bulverde,
	   so manually keep AC97 reset high */
	pxa_gpio_mode(113 | GPIO_OUT | GPIO_DFLT_HIGH);
	udelay(10);
	GCR |= GCR_WARM_RST;
	pxa_gpio_mode(113 | GPIO_ALT_FN_2_OUT);
	udelay(500);
#else
	GCR |= GCR_WARM_RST | GCR_PRIRDY_IEN | GCR_SECRDY_IEN;
	wait_event_timeout(gsr_wq, gsr_bits & (GSR_PCR | GSR_SCR), 1);
#endif

	if (!((GSR | gsr_bits) & (GSR_PCR | GSR_SCR)))
		printk(KERN_INFO "%s: warm reset timeout (GSR=%#lx)\n",
				 __FUNCTION__, gsr_bits);

	GCR &= ~(GCR_PRIRDY_IEN|GCR_SECRDY_IEN);
	GCR |= GCR_SDONE_IE|GCR_CDONE_IE;
}

static void pxa2xx_ac97_cold_reset(struct snd_ac97 *ac97)
{
	GCR &=  GCR_COLD_RST;  /* clear everything but nCRST */
	GCR &= ~GCR_COLD_RST;  /* then assert nCRST */

	gsr_bits = 0;
#ifdef CONFIG_PXA27x
	/* PXA27x Developers Manual section 13.5.2.2.1 */
	pxa_set_cken(CKEN_AC97CONF, 1);
	udelay(5);
	pxa_set_cken(CKEN_AC97CONF, 0);
	GCR = GCR_COLD_RST;
	udelay(50);
#else
	GCR = GCR_COLD_RST;
	GCR |= GCR_CDONE_IE|GCR_SDONE_IE;
	wait_event_timeout(gsr_wq, gsr_bits & (GSR_PCR | GSR_SCR), 1);
#endif

	if (!((GSR | gsr_bits) & (GSR_PCR | GSR_SCR)))
		printk(KERN_INFO "%s: cold reset timeout (GSR=%#lx)\n",
				 __FUNCTION__, gsr_bits);

	GCR &= ~(GCR_PRIRDY_IEN|GCR_SECRDY_IEN);
	GCR |= GCR_SDONE_IE|GCR_CDONE_IE;
}

static irqreturn_t pxa2xx_ac97_irq(int irq, void *dev_id)
{
	long status;

	status = GSR;
	if (status) {
		GSR = status;
		gsr_bits |= status;
		wake_up(&gsr_wq);

#ifdef CONFIG_PXA27x
		/* Although we don't use those we still need to clear them
		   since they tend to spuriously trigger when MMC is used
		   (hardware bug? go figure)... */
		MISR = MISR_EOC;
		PISR = PISR_EOC;
		MCSR = MCSR_EOC;
#endif

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

struct snd_ac97_bus_ops soc_ac97_ops = {
	.read	= pxa2xx_ac97_read,
	.write	= pxa2xx_ac97_write,
	.warm_reset	= pxa2xx_ac97_warm_reset,
	.reset	= pxa2xx_ac97_cold_reset,
};

static struct pxa2xx_pcm_dma_params pxa2xx_ac97_pcm_stereo_out = {
	.name			= "AC97 PCM Stereo out",
	.dev_addr		= __PREG(PCDR),
	.drcmr			= &DRCMRTXPCDR,
	.dcmd			= DCMD_INCSRCADDR | DCMD_FLOWTRG |
				  DCMD_BURST32 | DCMD_WIDTH4,
};

static struct pxa2xx_pcm_dma_params pxa2xx_ac97_pcm_stereo_in = {
	.name			= "AC97 PCM Stereo in",
	.dev_addr		= __PREG(PCDR),
	.drcmr			= &DRCMRRXPCDR,
	.dcmd			= DCMD_INCTRGADDR | DCMD_FLOWSRC |
				  DCMD_BURST32 | DCMD_WIDTH4,
};

static struct pxa2xx_pcm_dma_params pxa2xx_ac97_pcm_aux_mono_out = {
	.name			= "AC97 Aux PCM (Slot 5) Mono out",
	.dev_addr		= __PREG(MODR),
	.drcmr			= &DRCMRTXMODR,
	.dcmd			= DCMD_INCSRCADDR | DCMD_FLOWTRG |
				  DCMD_BURST16 | DCMD_WIDTH2,
};

static struct pxa2xx_pcm_dma_params pxa2xx_ac97_pcm_aux_mono_in = {
	.name			= "AC97 Aux PCM (Slot 5) Mono in",
	.dev_addr		= __PREG(MODR),
	.drcmr			= &DRCMRRXMODR,
	.dcmd			= DCMD_INCTRGADDR | DCMD_FLOWSRC |
				  DCMD_BURST16 | DCMD_WIDTH2,
};

static struct pxa2xx_pcm_dma_params pxa2xx_ac97_pcm_mic_mono_in = {
	.name			= "AC97 Mic PCM (Slot 6) Mono in",
	.dev_addr		= __PREG(MCDR),
	.drcmr			= &DRCMRRXMCDR,
	.dcmd			= DCMD_INCTRGADDR | DCMD_FLOWSRC |
				  DCMD_BURST16 | DCMD_WIDTH2,
};

#ifdef CONFIG_PM
static int pxa2xx_ac97_suspend(struct platform_device *pdev,
	struct snd_soc_cpu_dai *dai)
{
	GCR |= GCR_ACLINK_OFF;
	pxa_set_cken(CKEN_AC97, 0);
	return 0;
}

static int pxa2xx_ac97_resume(struct platform_device *pdev,
	struct snd_soc_cpu_dai *dai)
{
	pxa_gpio_mode(GPIO31_SYNC_AC97_MD);
	pxa_gpio_mode(GPIO30_SDATA_OUT_AC97_MD);
	pxa_gpio_mode(GPIO28_BITCLK_AC97_MD);
	pxa_gpio_mode(GPIO29_SDATA_IN_AC97_MD);
#ifdef CONFIG_PXA27x
	/* Use GPIO 113 as AC97 Reset on Bulverde */
	pxa_gpio_mode(113 | GPIO_ALT_FN_2_OUT);
#endif
	pxa_set_cken(CKEN_AC97, 1);
	return 0;
}

#else
#define pxa2xx_ac97_suspend	NULL
#define pxa2xx_ac97_resume	NULL
#endif

static int pxa2xx_ac97_probe(struct platform_device *pdev)
{
	int ret;

	ret = request_irq(IRQ_AC97, pxa2xx_ac97_irq, IRQF_DISABLED, "AC97", NULL);
	if (ret < 0)
		goto err;

	pxa_gpio_mode(GPIO31_SYNC_AC97_MD);
	pxa_gpio_mode(GPIO30_SDATA_OUT_AC97_MD);
	pxa_gpio_mode(GPIO28_BITCLK_AC97_MD);
	pxa_gpio_mode(GPIO29_SDATA_IN_AC97_MD);
#ifdef CONFIG_PXA27x
	/* Use GPIO 113 as AC97 Reset on Bulverde */
	pxa_gpio_mode(113 | GPIO_ALT_FN_2_OUT);
#endif
	pxa_set_cken(CKEN_AC97, 1);
	return 0;

 err:
	if (CKEN & (1 << CKEN_AC97)) {
		GCR |= GCR_ACLINK_OFF;
		free_irq(IRQ_AC97, NULL);
		pxa_set_cken(CKEN_AC97, 0);
	}
	return ret;
}

static void pxa2xx_ac97_remove(struct platform_device *pdev)
{
	GCR |= GCR_ACLINK_OFF;
	free_irq(IRQ_AC97, NULL);
	pxa_set_cken(CKEN_AC97, 0);
}

static int pxa2xx_ac97_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_cpu_dai *cpu_dai = rtd->dai->cpu_dai;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cpu_dai->dma_data = &pxa2xx_ac97_pcm_stereo_out;
	else
		cpu_dai->dma_data = &pxa2xx_ac97_pcm_stereo_in;

	return 0;
}

static int pxa2xx_ac97_hw_aux_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_cpu_dai *cpu_dai = rtd->dai->cpu_dai;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cpu_dai->dma_data = &pxa2xx_ac97_pcm_aux_mono_out;
	else
		cpu_dai->dma_data = &pxa2xx_ac97_pcm_aux_mono_in;

	return 0;
}

static int pxa2xx_ac97_hw_mic_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_cpu_dai *cpu_dai = rtd->dai->cpu_dai;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return -ENODEV;
	else
		cpu_dai->dma_data = &pxa2xx_ac97_pcm_mic_mono_in;

	return 0;
}

#define PXA2XX_AC97_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 | \
		SNDRV_PCM_RATE_48000)

/*
 * There is only 1 physical AC97 interface for pxa2xx, but it
 * has extra fifo's that can be used for aux DACs and ADCs.
 */
struct snd_soc_cpu_dai pxa_ac97_dai[] = {
{
	.name = "pxa2xx-ac97",
	.id = 0,
	.type = SND_SOC_DAI_AC97,
	.probe = pxa2xx_ac97_probe,
	.remove = pxa2xx_ac97_remove,
	.suspend = pxa2xx_ac97_suspend,
	.resume = pxa2xx_ac97_resume,
	.playback = {
		.stream_name = "AC97 Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = PXA2XX_AC97_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.stream_name = "AC97 Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = PXA2XX_AC97_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = {
		.hw_params = pxa2xx_ac97_hw_params,},
},
{
	.name = "pxa2xx-ac97-aux",
	.id = 1,
	.type = SND_SOC_DAI_AC97,
	.playback = {
		.stream_name = "AC97 Aux Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = PXA2XX_AC97_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.stream_name = "AC97 Aux Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = PXA2XX_AC97_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = {
		.hw_params = pxa2xx_ac97_hw_aux_params,},
},
{
	.name = "pxa2xx-ac97-mic",
	.id = 2,
	.type = SND_SOC_DAI_AC97,
	.capture = {
		.stream_name = "AC97 Mic Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = PXA2XX_AC97_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = {
		.hw_params = pxa2xx_ac97_hw_mic_params,},
},
};

EXPORT_SYMBOL_GPL(pxa_ac97_dai);
EXPORT_SYMBOL_GPL(soc_ac97_ops);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("AC97 driver for the Intel PXA2xx chip");
MODULE_LICENSE("GPL");

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
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>

#include <asm/irq.h>
#include <asm/semaphore.h>
#include <asm/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/audio.h>

#include "pxa2xx-pcm.h"


static DECLARE_MUTEX(car_mutex);
static DECLARE_WAIT_QUEUE_HEAD(gsr_wq);
static volatile long gsr_bits;

static unsigned short pxa2xx_ac97_read(ac97_t *ac97, unsigned short reg)
{
	unsigned short val = -1;
	volatile u32 *reg_addr;

	down(&car_mutex);
	if (CAR & CAR_CAIP) {
		printk(KERN_CRIT"%s: CAR_CAIP already set\n", __FUNCTION__);
		goto out;
	}

	/* set up primary or secondary codec space */
	reg_addr = (ac97->num & 1) ? &SAC_REG_BASE : &PAC_REG_BASE;
	reg_addr += (reg >> 1);

	/* start read access across the ac97 link */
	gsr_bits = 0;
	val = *reg_addr;
	if (reg == AC97_GPIO_STATUS)
		goto out;
	wait_event_timeout(gsr_wq, gsr_bits & GSR_SDONE, 1);
	if (!gsr_bits & GSR_SDONE) {
		printk(KERN_ERR "%s: read error (ac97_reg=%d GSR=%#lx)\n",
				__FUNCTION__, reg, gsr_bits);
		val = -1;
		goto out;
	}

	/* valid data now */
	gsr_bits = 0;
	val = *reg_addr;			
	/* but we've just started another cycle... */
	wait_event_timeout(gsr_wq, gsr_bits & GSR_SDONE, 1);

out:	up(&car_mutex);
	return val;
}

static void pxa2xx_ac97_write(ac97_t *ac97, unsigned short reg, unsigned short val)
{
	volatile u32 *reg_addr;

	down(&car_mutex);

	if (CAR & CAR_CAIP) {
		printk(KERN_CRIT "%s: CAR_CAIP already set\n", __FUNCTION__);
		goto out;
	}

	/* set up primary or secondary codec space */
	reg_addr = (ac97->num & 1) ? &SAC_REG_BASE : &PAC_REG_BASE;
	reg_addr += (reg >> 1);
	gsr_bits = 0;
	*reg_addr = val;
	wait_event_timeout(gsr_wq, gsr_bits & GSR_CDONE, 1);
	if (!gsr_bits & GSR_SDONE)
		printk(KERN_ERR "%s: write error (ac97_reg=%d GSR=%#lx)\n",
				__FUNCTION__, reg, gsr_bits);

out:	up(&car_mutex);
}

static void pxa2xx_ac97_reset(ac97_t *ac97)
{
	/* First, try cold reset */
	GCR &=  GCR_COLD_RST;  /* clear everything but nCRST */
	GCR &= ~GCR_COLD_RST;  /* then assert nCRST */

	gsr_bits = 0;
#ifdef CONFIG_PXA27x
	/* PXA27x Developers Manual section 13.5.2.2.1 */
	pxa_set_cken(1 << 31, 1);
	udelay(5);
	pxa_set_cken(1 << 31, 0);
	GCR = GCR_COLD_RST;
	udelay(50);
#else
	GCR = GCR_COLD_RST;
	GCR |= GCR_CDONE_IE|GCR_SDONE_IE;
	wait_event_timeout(gsr_wq, gsr_bits & (GSR_PCR | GSR_SCR), 1);
#endif

	if (!((GSR | gsr_bits) & (GSR_PCR | GSR_SCR))) {
		printk(KERN_INFO "%s: cold reset timeout (GSR=%#lx)\n",
				 __FUNCTION__, gsr_bits);

		/* let's try warm reset */
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
		GCR |= GCR_WARM_RST|GCR_PRIRDY_IEN|GCR_SECRDY_IEN;
		wait_event_timeout(gsr_wq, gsr_bits & (GSR_PCR | GSR_SCR), 1);
#endif			

		if (!((GSR | gsr_bits) & (GSR_PCR | GSR_SCR)))
			printk(KERN_INFO "%s: warm reset timeout (GSR=%#lx)\n",
					 __FUNCTION__, gsr_bits);
	}

	GCR &= ~(GCR_PRIRDY_IEN|GCR_SECRDY_IEN);
	GCR |= GCR_SDONE_IE|GCR_CDONE_IE;
}

static irqreturn_t pxa2xx_ac97_irq(int irq, void *dev_id, struct pt_regs *regs)
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

static ac97_bus_ops_t pxa2xx_ac97_ops = {
	.read	= pxa2xx_ac97_read,
	.write	= pxa2xx_ac97_write,
	.reset	= pxa2xx_ac97_reset,
};

static pxa2xx_pcm_dma_params_t pxa2xx_ac97_pcm_out = {
	.name			= "AC97 PCM out",
	.dev_addr		= __PREG(PCDR),
	.drcmr			= &DRCMRTXPCDR,
	.dcmd			= DCMD_INCSRCADDR | DCMD_FLOWTRG |
				  DCMD_BURST32 | DCMD_WIDTH4,
};

static pxa2xx_pcm_dma_params_t pxa2xx_ac97_pcm_in = {
	.name			= "AC97 PCM in",
	.dev_addr		= __PREG(PCDR),
	.drcmr			= &DRCMRRXPCDR,
	.dcmd			= DCMD_INCTRGADDR | DCMD_FLOWSRC |
				  DCMD_BURST32 | DCMD_WIDTH4,
};

static snd_pcm_t *pxa2xx_ac97_pcm;
static ac97_t *pxa2xx_ac97_ac97;

static int pxa2xx_ac97_pcm_startup(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	pxa2xx_audio_ops_t *platform_ops;
	int r;

	runtime->hw.channels_min = 2;
	runtime->hw.channels_max = 2;

	r = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
	    AC97_RATES_FRONT_DAC : AC97_RATES_ADC;
	runtime->hw.rates = pxa2xx_ac97_ac97->rates[r];
	snd_pcm_limit_hw_rates(runtime);

       	platform_ops = substream->pcm->card->dev->platform_data;
	if (platform_ops && platform_ops->startup)
		return platform_ops->startup(substream, platform_ops->priv);
	else
		return 0;
}

static void pxa2xx_ac97_pcm_shutdown(snd_pcm_substream_t *substream)
{
	pxa2xx_audio_ops_t *platform_ops;

       	platform_ops = substream->pcm->card->dev->platform_data;
	if (platform_ops && platform_ops->shutdown)
		platform_ops->shutdown(substream, platform_ops->priv);
}

static int pxa2xx_ac97_pcm_prepare(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int reg = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		  AC97_PCM_FRONT_DAC_RATE : AC97_PCM_LR_ADC_RATE;
	return snd_ac97_set_rate(pxa2xx_ac97_ac97, reg, runtime->rate);
}

static pxa2xx_pcm_client_t pxa2xx_ac97_pcm_client = {
	.playback_params	= &pxa2xx_ac97_pcm_out,
	.capture_params		= &pxa2xx_ac97_pcm_in,
	.startup		= pxa2xx_ac97_pcm_startup,
	.shutdown		= pxa2xx_ac97_pcm_shutdown,
	.prepare		= pxa2xx_ac97_pcm_prepare,
};

#ifdef CONFIG_PM

static int pxa2xx_ac97_do_suspend(snd_card_t *card, pm_message_t state)
{
	if (card->power_state != SNDRV_CTL_POWER_D3cold) {
		pxa2xx_audio_ops_t *platform_ops = card->dev->platform_data;
		snd_pcm_suspend_all(pxa2xx_ac97_pcm);
		snd_ac97_suspend(pxa2xx_ac97_ac97);
		snd_power_change_state(card, SNDRV_CTL_POWER_D3cold);
		if (platform_ops && platform_ops->suspend)
			platform_ops->suspend(platform_ops->priv);
		GCR |= GCR_ACLINK_OFF;
		pxa_set_cken(CKEN2_AC97, 0);
	}

	return 0;
}

static int pxa2xx_ac97_do_resume(snd_card_t *card)
{
	if (card->power_state != SNDRV_CTL_POWER_D0) {
		pxa2xx_audio_ops_t *platform_ops = card->dev->platform_data;
		pxa_set_cken(CKEN2_AC97, 1);
		if (platform_ops && platform_ops->resume)
			platform_ops->resume(platform_ops->priv);
		snd_ac97_resume(pxa2xx_ac97_ac97);
		snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	}

	return 0;
}

static int pxa2xx_ac97_suspend(struct device *_dev, pm_message_t state, u32 level)
{
	snd_card_t *card = dev_get_drvdata(_dev);
	int ret = 0;

	if (card && level == SUSPEND_DISABLE)
		ret = pxa2xx_ac97_do_suspend(card, PMSG_SUSPEND);

	return ret;
}

static int pxa2xx_ac97_resume(struct device *_dev, u32 level)
{
	snd_card_t *card = dev_get_drvdata(_dev);
	int ret = 0;

	if (card && level == RESUME_ENABLE)
		ret = pxa2xx_ac97_do_resume(card);

	return ret;
}

#else
#define pxa2xx_ac97_suspend	NULL
#define pxa2xx_ac97_resume	NULL
#endif

static int pxa2xx_ac97_probe(struct device *dev)
{
	snd_card_t *card;
	ac97_bus_t *ac97_bus;
	ac97_template_t ac97_template;
	int ret;

	ret = -ENOMEM;
	card = snd_card_new(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			    THIS_MODULE, 0);
	if (!card)
		goto err;

	card->dev = dev;
	strncpy(card->driver, dev->driver->name, sizeof(card->driver));

	ret = pxa2xx_pcm_new(card, &pxa2xx_ac97_pcm_client, &pxa2xx_ac97_pcm);
	if (ret)
		goto err;

	ret = request_irq(IRQ_AC97, pxa2xx_ac97_irq, 0, "AC97", NULL);
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
	pxa_set_cken(CKEN2_AC97, 1);

	ret = snd_ac97_bus(card, 0, &pxa2xx_ac97_ops, NULL, &ac97_bus);
	if (ret)
		goto err;
	memset(&ac97_template, 0, sizeof(ac97_template));
	ret = snd_ac97_mixer(ac97_bus, &ac97_template, &pxa2xx_ac97_ac97);
	if (ret)
		goto err;

	snprintf(card->shortname, sizeof(card->shortname),
		 "%s", snd_ac97_get_short_name(pxa2xx_ac97_ac97));
	snprintf(card->longname, sizeof(card->longname),
		 "%s (%s)", dev->driver->name, card->mixername);

	snd_card_set_pm_callback(card, pxa2xx_ac97_do_suspend,
				 pxa2xx_ac97_do_resume, NULL);
	ret = snd_card_register(card);
	if (ret == 0) {
		dev_set_drvdata(dev, card);
		return 0;
	}

 err:
	if (card)
		snd_card_free(card);
	if (CKEN & CKEN2_AC97) {
		GCR |= GCR_ACLINK_OFF;
		free_irq(IRQ_AC97, NULL);
		pxa_set_cken(CKEN2_AC97, 0);
	}
	return ret;
}

static int pxa2xx_ac97_remove(struct device *dev)
{
	snd_card_t *card = dev_get_drvdata(dev);

	if (card) {
		snd_card_free(card);
		dev_set_drvdata(dev, NULL);
		GCR |= GCR_ACLINK_OFF;
		free_irq(IRQ_AC97, NULL);
		pxa_set_cken(CKEN2_AC97, 0);
	}

	return 0;
}

static struct device_driver pxa2xx_ac97_driver = {
	.name		= "pxa2xx-ac97",
	.bus		= &platform_bus_type,
	.probe		= pxa2xx_ac97_probe,
	.remove		= pxa2xx_ac97_remove,
	.suspend	= pxa2xx_ac97_suspend,
	.resume		= pxa2xx_ac97_resume,
};

static int __init pxa2xx_ac97_init(void)
{
	return driver_register(&pxa2xx_ac97_driver);
}

static void __exit pxa2xx_ac97_exit(void)
{
	driver_unregister(&pxa2xx_ac97_driver);
}

module_init(pxa2xx_ac97_init);
module_exit(pxa2xx_ac97_exit);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("AC97 driver for the Intel PXA2xx chip");
MODULE_LICENSE("GPL");

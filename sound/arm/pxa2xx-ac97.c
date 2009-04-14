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

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/pxa2xx-lib.h>

#include <mach/regs-ac97.h>
#include <mach/audio.h>

#include "pxa2xx-pcm.h"

static void pxa2xx_ac97_reset(struct snd_ac97 *ac97)
{
	if (!pxa2xx_ac97_try_cold_reset(ac97)) {
		pxa2xx_ac97_try_warm_reset(ac97);
	}

	pxa2xx_ac97_finish_reset(ac97);
}

static struct snd_ac97_bus_ops pxa2xx_ac97_ops = {
	.read	= pxa2xx_ac97_read,
	.write	= pxa2xx_ac97_write,
	.reset	= pxa2xx_ac97_reset,
};

static struct pxa2xx_pcm_dma_params pxa2xx_ac97_pcm_out = {
	.name			= "AC97 PCM out",
	.dev_addr		= __PREG(PCDR),
	.drcmr			= &DRCMR(12),
	.dcmd			= DCMD_INCSRCADDR | DCMD_FLOWTRG |
				  DCMD_BURST32 | DCMD_WIDTH4,
};

static struct pxa2xx_pcm_dma_params pxa2xx_ac97_pcm_in = {
	.name			= "AC97 PCM in",
	.dev_addr		= __PREG(PCDR),
	.drcmr			= &DRCMR(11),
	.dcmd			= DCMD_INCTRGADDR | DCMD_FLOWSRC |
				  DCMD_BURST32 | DCMD_WIDTH4,
};

static struct snd_pcm *pxa2xx_ac97_pcm;
static struct snd_ac97 *pxa2xx_ac97_ac97;

static int pxa2xx_ac97_pcm_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
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

static void pxa2xx_ac97_pcm_shutdown(struct snd_pcm_substream *substream)
{
	pxa2xx_audio_ops_t *platform_ops;

       	platform_ops = substream->pcm->card->dev->platform_data;
	if (platform_ops && platform_ops->shutdown)
		platform_ops->shutdown(substream, platform_ops->priv);
}

static int pxa2xx_ac97_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int reg = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		  AC97_PCM_FRONT_DAC_RATE : AC97_PCM_LR_ADC_RATE;
	return snd_ac97_set_rate(pxa2xx_ac97_ac97, reg, runtime->rate);
}

static struct pxa2xx_pcm_client pxa2xx_ac97_pcm_client = {
	.playback_params	= &pxa2xx_ac97_pcm_out,
	.capture_params		= &pxa2xx_ac97_pcm_in,
	.startup		= pxa2xx_ac97_pcm_startup,
	.shutdown		= pxa2xx_ac97_pcm_shutdown,
	.prepare		= pxa2xx_ac97_pcm_prepare,
};

#ifdef CONFIG_PM

static int pxa2xx_ac97_do_suspend(struct snd_card *card, pm_message_t state)
{
	pxa2xx_audio_ops_t *platform_ops = card->dev->platform_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3cold);
	snd_pcm_suspend_all(pxa2xx_ac97_pcm);
	snd_ac97_suspend(pxa2xx_ac97_ac97);
	if (platform_ops && platform_ops->suspend)
		platform_ops->suspend(platform_ops->priv);

	return pxa2xx_ac97_hw_suspend();
}

static int pxa2xx_ac97_do_resume(struct snd_card *card)
{
	pxa2xx_audio_ops_t *platform_ops = card->dev->platform_data;
	int rc;

	rc = pxa2xx_ac97_hw_resume();
	if (rc)
		return rc;

	if (platform_ops && platform_ops->resume)
		platform_ops->resume(platform_ops->priv);
	snd_ac97_resume(pxa2xx_ac97_ac97);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);

	return 0;
}

static int pxa2xx_ac97_suspend(struct platform_device *dev, pm_message_t state)
{
	struct snd_card *card = platform_get_drvdata(dev);
	int ret = 0;

	if (card)
		ret = pxa2xx_ac97_do_suspend(card, PMSG_SUSPEND);

	return ret;
}

static int pxa2xx_ac97_resume(struct platform_device *dev)
{
	struct snd_card *card = platform_get_drvdata(dev);
	int ret = 0;

	if (card)
		ret = pxa2xx_ac97_do_resume(card);

	return ret;
}

#else
#define pxa2xx_ac97_suspend	NULL
#define pxa2xx_ac97_resume	NULL
#endif

static int __devinit pxa2xx_ac97_probe(struct platform_device *dev)
{
	struct snd_card *card;
	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97_template ac97_template;
	int ret;

	ret = snd_card_create(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			      THIS_MODULE, 0, &card);
	if (ret < 0)
		goto err;

	card->dev = &dev->dev;
	strncpy(card->driver, dev->dev.driver->name, sizeof(card->driver));

	ret = pxa2xx_pcm_new(card, &pxa2xx_ac97_pcm_client, &pxa2xx_ac97_pcm);
	if (ret)
		goto err;

	ret = pxa2xx_ac97_hw_probe(dev);
	if (ret)
		goto err;

	ret = snd_ac97_bus(card, 0, &pxa2xx_ac97_ops, NULL, &ac97_bus);
	if (ret)
		goto err_remove;
	memset(&ac97_template, 0, sizeof(ac97_template));
	ret = snd_ac97_mixer(ac97_bus, &ac97_template, &pxa2xx_ac97_ac97);
	if (ret)
		goto err_remove;

	snprintf(card->shortname, sizeof(card->shortname),
		 "%s", snd_ac97_get_short_name(pxa2xx_ac97_ac97));
	snprintf(card->longname, sizeof(card->longname),
		 "%s (%s)", dev->dev.driver->name, card->mixername);

	snd_card_set_dev(card, &dev->dev);
	ret = snd_card_register(card);
	if (ret == 0) {
		platform_set_drvdata(dev, card);
		return 0;
	}

err_remove:
	pxa2xx_ac97_hw_remove(dev);
err:
	if (card)
		snd_card_free(card);
	return ret;
}

static int __devexit pxa2xx_ac97_remove(struct platform_device *dev)
{
	struct snd_card *card = platform_get_drvdata(dev);

	if (card) {
		snd_card_free(card);
		platform_set_drvdata(dev, NULL);
		pxa2xx_ac97_hw_remove(dev);
	}

	return 0;
}

static struct platform_driver pxa2xx_ac97_driver = {
	.probe		= pxa2xx_ac97_probe,
	.remove		= __devexit_p(pxa2xx_ac97_remove),
	.suspend	= pxa2xx_ac97_suspend,
	.resume		= pxa2xx_ac97_resume,
	.driver		= {
		.name	= "pxa2xx-ac97",
		.owner	= THIS_MODULE,
	},
};

static int __init pxa2xx_ac97_init(void)
{
	return platform_driver_register(&pxa2xx_ac97_driver);
}

static void __exit pxa2xx_ac97_exit(void)
{
	platform_driver_unregister(&pxa2xx_ac97_driver);
}

module_init(pxa2xx_ac97_init);
module_exit(pxa2xx_ac97_exit);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("AC97 driver for the Intel PXA2xx chip");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa2xx-ac97");

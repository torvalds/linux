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
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/pxa2xx-lib.h>
#include <sound/dmaengine_pcm.h>

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

static unsigned long pxa2xx_ac97_pcm_out_req = 12;
static struct snd_dmaengine_dai_dma_data pxa2xx_ac97_pcm_out = {
	.addr		= __PREG(PCDR),
	.addr_width	= DMA_SLAVE_BUSWIDTH_4_BYTES,
	.maxburst	= 32,
	.filter_data	= &pxa2xx_ac97_pcm_out_req,
};

static unsigned long pxa2xx_ac97_pcm_in_req = 11;
static struct snd_dmaengine_dai_dma_data pxa2xx_ac97_pcm_in = {
	.addr		= __PREG(PCDR),
	.addr_width	= DMA_SLAVE_BUSWIDTH_4_BYTES,
	.maxburst	= 32,
	.filter_data	= &pxa2xx_ac97_pcm_in_req,
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

#ifdef CONFIG_PM_SLEEP

static int pxa2xx_ac97_do_suspend(struct snd_card *card)
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

static int pxa2xx_ac97_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	int ret = 0;

	if (card)
		ret = pxa2xx_ac97_do_suspend(card);

	return ret;
}

static int pxa2xx_ac97_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	int ret = 0;

	if (card)
		ret = pxa2xx_ac97_do_resume(card);

	return ret;
}

static SIMPLE_DEV_PM_OPS(pxa2xx_ac97_pm_ops, pxa2xx_ac97_suspend, pxa2xx_ac97_resume);
#endif

static int pxa2xx_ac97_probe(struct platform_device *dev)
{
	struct snd_card *card;
	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97_template ac97_template;
	int ret;
	pxa2xx_audio_ops_t *pdata = dev->dev.platform_data;

	if (dev->id >= 0) {
		dev_err(&dev->dev, "PXA2xx has only one AC97 port.\n");
		ret = -ENXIO;
		goto err_dev;
	}

	ret = snd_card_new(&dev->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			   THIS_MODULE, 0, &card);
	if (ret < 0)
		goto err;

	strlcpy(card->driver, dev->dev.driver->name, sizeof(card->driver));

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

	if (pdata && pdata->codec_pdata[0])
		snd_ac97_dev_add_pdata(ac97_bus->codec[0], pdata->codec_pdata[0]);
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
err_dev:
	return ret;
}

static int pxa2xx_ac97_remove(struct platform_device *dev)
{
	struct snd_card *card = platform_get_drvdata(dev);

	if (card) {
		snd_card_free(card);
		pxa2xx_ac97_hw_remove(dev);
	}

	return 0;
}

static struct platform_driver pxa2xx_ac97_driver = {
	.probe		= pxa2xx_ac97_probe,
	.remove		= pxa2xx_ac97_remove,
	.driver		= {
		.name	= "pxa2xx-ac97",
#ifdef CONFIG_PM_SLEEP
		.pm	= &pxa2xx_ac97_pm_ops,
#endif
	},
};

module_platform_driver(pxa2xx_ac97_driver);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("AC97 driver for the Intel PXA2xx chip");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa2xx-ac97");

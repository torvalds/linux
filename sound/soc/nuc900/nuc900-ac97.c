/*
 * Copyright (c) 2009-2010 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <linux/clk.h>

#include <mach/mfp.h>

#include "nuc900-audio.h"

static DEFINE_MUTEX(ac97_mutex);
struct nuc900_audio *nuc900_ac97_data;

static int nuc900_checkready(void)
{
	struct nuc900_audio *nuc900_audio = nuc900_ac97_data;

	if (!(AUDIO_READ(nuc900_audio->mmio + ACTL_ACIS0) & CODEC_READY))
		return -EPERM;

	return 0;
}

/* AC97 controller reads codec register */
static unsigned short nuc900_ac97_read(struct snd_ac97 *ac97,
					unsigned short reg)
{
	struct nuc900_audio *nuc900_audio = nuc900_ac97_data;
	unsigned long timeout = 0x10000, val;

	mutex_lock(&ac97_mutex);

	val = nuc900_checkready();
	if (val) {
		dev_err(nuc900_audio->dev, "AC97 codec is not ready\n");
		goto out;
	}

	/* set the R_WB bit and write register index */
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACOS1, R_WB | reg);

	/* set the valid frame bit and valid slots */
	val = AUDIO_READ(nuc900_audio->mmio + ACTL_ACOS0);
	val |= (VALID_FRAME | SLOT1_VALID);
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACOS0, val);

	udelay(100);

	/* polling the AC_R_FINISH */
	while (!(AUDIO_READ(nuc900_audio->mmio + ACTL_ACCON) & AC_R_FINISH)
								&& timeout--)
		mdelay(1);

	if (!timeout) {
		dev_err(nuc900_audio->dev, "AC97 read register time out !\n");
		val = -EPERM;
		goto out;
	}

	val = AUDIO_READ(nuc900_audio->mmio + ACTL_ACOS0) ;
	val &= ~SLOT1_VALID;
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACOS0, val);

	if (AUDIO_READ(nuc900_audio->mmio + ACTL_ACIS1) >> 2 != reg) {
		dev_err(nuc900_audio->dev,
				"R_INDEX of REG_ACTL_ACIS1 not match!\n");
	}

	udelay(100);
	val = (AUDIO_READ(nuc900_audio->mmio + ACTL_ACIS2) & 0xFFFF);

out:
	mutex_unlock(&ac97_mutex);
	return val;
}

/* AC97 controller writes to codec register */
static void nuc900_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
				unsigned short val)
{
	struct nuc900_audio *nuc900_audio = nuc900_ac97_data;
	unsigned long tmp, timeout = 0x10000;

	mutex_lock(&ac97_mutex);

	tmp = nuc900_checkready();
	if (tmp)
		dev_err(nuc900_audio->dev, "AC97 codec is not ready\n");

	/* clear the R_WB bit and write register index */
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACOS1, reg);

	/* write register value */
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACOS2, val);

	/* set the valid frame bit and valid slots */
	tmp = AUDIO_READ(nuc900_audio->mmio + ACTL_ACOS0);
	tmp |= SLOT1_VALID | SLOT2_VALID | VALID_FRAME;
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACOS0, tmp);

	udelay(100);

	/* polling the AC_W_FINISH */
	while ((AUDIO_READ(nuc900_audio->mmio + ACTL_ACCON) & AC_W_FINISH)
								&& timeout--)
		mdelay(1);

	if (!timeout)
		dev_err(nuc900_audio->dev, "AC97 write register time out !\n");

	tmp = AUDIO_READ(nuc900_audio->mmio + ACTL_ACOS0);
	tmp &= ~(SLOT1_VALID | SLOT2_VALID);
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACOS0, tmp);

	mutex_unlock(&ac97_mutex);

}

static void nuc900_ac97_warm_reset(struct snd_ac97 *ac97)
{
	struct nuc900_audio *nuc900_audio = nuc900_ac97_data;
	unsigned long val;

	mutex_lock(&ac97_mutex);

	/* warm reset AC 97 */
	val = AUDIO_READ(nuc900_audio->mmio + ACTL_ACCON);
	val |= AC_W_RES;
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACCON, val);

	udelay(100);

	val = nuc900_checkready();
	if (val)
		dev_err(nuc900_audio->dev, "AC97 codec is not ready\n");

	mutex_unlock(&ac97_mutex);
}

static void nuc900_ac97_cold_reset(struct snd_ac97 *ac97)
{
	struct nuc900_audio *nuc900_audio = nuc900_ac97_data;
	unsigned long val;

	mutex_lock(&ac97_mutex);

	/* reset Audio Controller */
	val = AUDIO_READ(nuc900_audio->mmio + ACTL_RESET);
	val |= ACTL_RESET_BIT;
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_RESET, val);

	val = AUDIO_READ(nuc900_audio->mmio + ACTL_RESET);
	val &= (~ACTL_RESET_BIT);
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_RESET, val);

	/* reset AC-link interface */

	val = AUDIO_READ(nuc900_audio->mmio + ACTL_RESET);
	val |= AC_RESET;
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_RESET, val);

	val = AUDIO_READ(nuc900_audio->mmio + ACTL_RESET);
	val &= ~AC_RESET;
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_RESET, val);

	/* cold reset AC 97 */
	val = AUDIO_READ(nuc900_audio->mmio + ACTL_ACCON);
	val |= AC_C_RES;
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACCON, val);

	val = AUDIO_READ(nuc900_audio->mmio + ACTL_ACCON);
	val &= (~AC_C_RES);
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACCON, val);

	udelay(100);

	mutex_unlock(&ac97_mutex);

}

/* AC97 controller operations */
struct snd_ac97_bus_ops soc_ac97_ops = {
	.read		= nuc900_ac97_read,
	.write		= nuc900_ac97_write,
	.reset		= nuc900_ac97_cold_reset,
	.warm_reset	= nuc900_ac97_warm_reset,
}
EXPORT_SYMBOL_GPL(soc_ac97_ops);

static int nuc900_ac97_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct nuc900_audio *nuc900_audio = nuc900_ac97_data;
	int ret;
	unsigned long val, tmp;

	ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		val = AUDIO_READ(nuc900_audio->mmio + ACTL_RESET);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			tmp = AUDIO_READ(nuc900_audio->mmio + ACTL_ACOS0);
			tmp |= (SLOT3_VALID | SLOT4_VALID | VALID_FRAME);
			AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACOS0, tmp);

			tmp = AUDIO_READ(nuc900_audio->mmio + ACTL_PSR);
			tmp |= (P_DMA_END_IRQ | P_DMA_MIDDLE_IRQ);
			AUDIO_WRITE(nuc900_audio->mmio + ACTL_PSR, tmp);
			val |= AC_PLAY;
		} else {
			tmp = AUDIO_READ(nuc900_audio->mmio + ACTL_RSR);
			tmp |= (R_DMA_END_IRQ | R_DMA_MIDDLE_IRQ);

			AUDIO_WRITE(nuc900_audio->mmio + ACTL_RSR, tmp);
			val |= AC_RECORD;
		}

		AUDIO_WRITE(nuc900_audio->mmio + ACTL_RESET, val);

		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		val = AUDIO_READ(nuc900_audio->mmio + ACTL_RESET);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			tmp = AUDIO_READ(nuc900_audio->mmio + ACTL_ACOS0);
			tmp &= ~(SLOT3_VALID | SLOT4_VALID);
			AUDIO_WRITE(nuc900_audio->mmio + ACTL_ACOS0, tmp);

			AUDIO_WRITE(nuc900_audio->mmio + ACTL_PSR, RESET_PRSR);
			val &= ~AC_PLAY;
		} else {
			AUDIO_WRITE(nuc900_audio->mmio + ACTL_RSR, RESET_PRSR);
			val &= ~AC_RECORD;
		}

		AUDIO_WRITE(nuc900_audio->mmio + ACTL_RESET, val);

		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int nuc900_ac97_probe(struct snd_soc_dai *dai)
{
	struct nuc900_audio *nuc900_audio = nuc900_ac97_data;
	unsigned long val;

	mutex_lock(&ac97_mutex);

	/* enable unit clock */
	clk_enable(nuc900_audio->clk);

	/* enable audio controller and AC-link interface */
	val = AUDIO_READ(nuc900_audio->mmio + ACTL_CON);
	val |= (IIS_AC_PIN_SEL | ACLINK_EN);
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_CON, val);

	mutex_unlock(&ac97_mutex);

	return 0;
}

static int nuc900_ac97_remove(struct snd_soc_dai *dai)
{
	struct nuc900_audio *nuc900_audio = nuc900_ac97_data;

	clk_disable(nuc900_audio->clk);
	return 0;
}

static const struct snd_soc_dai_ops nuc900_ac97_dai_ops = {
	.trigger	= nuc900_ac97_trigger,
};

static struct snd_soc_dai_driver nuc900_ac97_dai = {
	.probe			= nuc900_ac97_probe,
	.remove			= nuc900_ac97_remove,
	.ac97_control		= 1,
	.playback = {
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.capture = {
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops = &nuc900_ac97_dai_ops,
};

static const struct snd_soc_component_driver nuc900_ac97_component = {
	.name		= "nuc900-ac97",
};

static int nuc900_ac97_drvprobe(struct platform_device *pdev)
{
	struct nuc900_audio *nuc900_audio;
	int ret;

	if (nuc900_ac97_data)
		return -EBUSY;

	nuc900_audio = kzalloc(sizeof(struct nuc900_audio), GFP_KERNEL);
	if (!nuc900_audio)
		return -ENOMEM;

	spin_lock_init(&nuc900_audio->lock);

	nuc900_audio->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!nuc900_audio->res) {
		ret = -ENODEV;
		goto out0;
	}

	if (!request_mem_region(nuc900_audio->res->start,
			resource_size(nuc900_audio->res), pdev->name)) {
		ret = -EBUSY;
		goto out0;
	}

	nuc900_audio->mmio = ioremap(nuc900_audio->res->start,
					resource_size(nuc900_audio->res));
	if (!nuc900_audio->mmio) {
		ret = -ENOMEM;
		goto out1;
	}

	nuc900_audio->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(nuc900_audio->clk)) {
		ret = PTR_ERR(nuc900_audio->clk);
		goto out2;
	}

	nuc900_audio->irq_num = platform_get_irq(pdev, 0);
	if (!nuc900_audio->irq_num) {
		ret = -EBUSY;
		goto out3;
	}

	nuc900_ac97_data = nuc900_audio;

	ret = snd_soc_register_component(&pdev->dev, &nuc900_ac97_component,
					 &nuc900_ac97_dai, 1);
	if (ret)
		goto out3;

	/* enbale ac97 multifunction pin */
	mfp_set_groupg(nuc900_audio->dev, NULL);

	return 0;

out3:
	clk_put(nuc900_audio->clk);
out2:
	iounmap(nuc900_audio->mmio);
out1:
	release_mem_region(nuc900_audio->res->start,
					resource_size(nuc900_audio->res));
out0:
	kfree(nuc900_audio);
	return ret;
}

static int nuc900_ac97_drvremove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	clk_put(nuc900_ac97_data->clk);
	iounmap(nuc900_ac97_data->mmio);
	release_mem_region(nuc900_ac97_data->res->start,
				resource_size(nuc900_ac97_data->res));

	kfree(nuc900_ac97_data);
	nuc900_ac97_data = NULL;

	return 0;
}

static struct platform_driver nuc900_ac97_driver = {
	.driver	= {
		.name	= "nuc900-ac97",
		.owner	= THIS_MODULE,
	},
	.probe		= nuc900_ac97_drvprobe,
	.remove		= nuc900_ac97_drvremove,
};

module_platform_driver(nuc900_ac97_driver);

MODULE_AUTHOR("Wan ZongShun <mcuos.com@gmail.com>");
MODULE_DESCRIPTION("NUC900 AC97 SoC driver!");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nuc900-ac97");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Power management for audio on multifunction CS5535 companion device
 * Copyright (C) Jaya Kumar
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <sound/asoundef.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include "cs5535audio.h"

static void snd_cs5535audio_stop_hardware(struct cs5535audio *cs5535au)
{
	/* 
	we depend on snd_ac97_suspend to tell the
	AC97 codec to shutdown. the amd spec suggests
	that the LNK_SHUTDOWN be done at the same time
	that the codec power-down is issued. instead,
	we do it just after rather than at the same 
	time. excluding codec specific build_ops->suspend
	ac97 powerdown hits:
	0x8000 EAPD 
	0x4000 Headphone amplifier 
	0x0300 ADC & DAC 
	0x0400 Analog Mixer powerdown (Vref on) 
	I am not sure if this is the best that we can do.
	The remainder to be investigated are:
	- analog mixer (vref off) 0x0800
	- AC-link powerdown 0x1000
	- codec internal clock 0x2000
	*/

	/* set LNK_SHUTDOWN to shutdown AC link */
	cs_writel(cs5535au, ACC_CODEC_CNTL, ACC_CODEC_CNTL_LNK_SHUTDOWN);

}

static int __maybe_unused snd_cs5535audio_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct cs5535audio *cs5535au = card->private_data;
	int i;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_ac97_suspend(cs5535au->ac97);
	for (i = 0; i < NUM_CS5535AUDIO_DMAS; i++) {
		struct cs5535audio_dma *dma = &cs5535au->dmas[i];
		if (dma && dma->substream)
			dma->saved_prd = dma->ops->read_prd(cs5535au);
	}
	/* save important regs, then disable aclink in hw */
	snd_cs5535audio_stop_hardware(cs5535au);
	return 0;
}

static int __maybe_unused snd_cs5535audio_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct cs5535audio *cs5535au = card->private_data;
	u32 tmp;
	int timeout;
	int i;

	/* set LNK_WRM_RST to reset AC link */
	cs_writel(cs5535au, ACC_CODEC_CNTL, ACC_CODEC_CNTL_LNK_WRM_RST);

	timeout = 50;
	do {
		tmp = cs_readl(cs5535au, ACC_CODEC_STATUS);
		if (tmp & PRM_RDY_STS)
			break;
		udelay(1);
	} while (--timeout);

	if (!timeout)
		dev_err(cs5535au->card->dev, "Failure getting AC Link ready\n");

	/* set up rate regs, dma. actual initiation is done in trig */
	for (i = 0; i < NUM_CS5535AUDIO_DMAS; i++) {
		struct cs5535audio_dma *dma = &cs5535au->dmas[i];
		if (dma && dma->substream) {
			dma->substream->ops->prepare(dma->substream);
			dma->ops->setup_prd(cs5535au, dma->saved_prd);
		}
	}

	/* we depend on ac97 to perform the codec power up */
	snd_ac97_resume(cs5535au->ac97);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);

	return 0;
}

SIMPLE_DEV_PM_OPS(snd_cs5535audio_pm, snd_cs5535audio_suspend, snd_cs5535audio_resume);

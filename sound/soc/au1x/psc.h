/*
 * Au12x0/Au1550 PSC ALSA ASoC audio support.
 *
 * (c) 2007-2008 MSC Vertriebsges.m.b.H.,
 *	Manuel Lauss <manuel.lauss@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOTE: all of these drivers can only work with a SINGLE instance
 *	 of a PSC. Multiple independent audio devices are impossible
 *	 with ASoC v1.
 */

#ifndef _AU1X_PCM_H
#define _AU1X_PCM_H

extern struct snd_soc_dai au1xpsc_ac97_dai;
extern struct snd_soc_dai au1xpsc_i2s_dai;
extern struct snd_soc_platform au1xpsc_soc_platform;
extern struct snd_ac97_bus_ops soc_ac97_ops;

/* DBDMA helpers */
extern struct platform_device *au1xpsc_pcm_add(struct platform_device *pdev);
extern void au1xpsc_pcm_destroy(struct platform_device *dmapd);

struct au1xpsc_audio_data {
	void __iomem *mmio;

	unsigned long cfg;
	unsigned long rate;

	unsigned long pm[2];
	struct mutex lock;
	struct platform_device *dmapd;
};

#define PCM_TX	0
#define PCM_RX	1

#define SUBSTREAM_TYPE(substream) \
	((substream)->stream == SNDRV_PCM_STREAM_PLAYBACK ? PCM_TX : PCM_RX)

/* easy access macros */
#define PSC_CTRL(x)	((unsigned long)((x)->mmio) + PSC_CTRL_OFFSET)
#define PSC_SEL(x)	((unsigned long)((x)->mmio) + PSC_SEL_OFFSET)
#define I2S_STAT(x)	((unsigned long)((x)->mmio) + PSC_I2SSTAT_OFFSET)
#define I2S_CFG(x)	((unsigned long)((x)->mmio) + PSC_I2SCFG_OFFSET)
#define I2S_PCR(x)	((unsigned long)((x)->mmio) + PSC_I2SPCR_OFFSET)
#define AC97_CFG(x)	((unsigned long)((x)->mmio) + PSC_AC97CFG_OFFSET)
#define AC97_CDC(x)	((unsigned long)((x)->mmio) + PSC_AC97CDC_OFFSET)
#define AC97_EVNT(x)	((unsigned long)((x)->mmio) + PSC_AC97EVNT_OFFSET)
#define AC97_PCR(x)	((unsigned long)((x)->mmio) + PSC_AC97PCR_OFFSET)
#define AC97_RST(x)	((unsigned long)((x)->mmio) + PSC_AC97RST_OFFSET)
#define AC97_STAT(x)	((unsigned long)((x)->mmio) + PSC_AC97STAT_OFFSET)

#endif

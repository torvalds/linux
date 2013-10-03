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
 * Au1xxx-PSC I2S glue.
 *
 * NOTE: so far only PSC slave mode (bit- and frameclock) is supported.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_psc.h>

#include "psc.h"

/* supported I2S DAI hardware formats */
#define AU1XPSC_I2S_DAIFMT \
	(SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_LEFT_J |	\
	 SND_SOC_DAIFMT_NB_NF)

/* supported I2S direction */
#define AU1XPSC_I2S_DIR \
	(SND_SOC_DAIDIR_PLAYBACK | SND_SOC_DAIDIR_CAPTURE)

#define AU1XPSC_I2S_RATES \
	SNDRV_PCM_RATE_8000_192000

#define AU1XPSC_I2S_FMTS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

#define I2SSTAT_BUSY(stype)	\
	((stype) == SNDRV_PCM_STREAM_PLAYBACK ? PSC_I2SSTAT_TB : PSC_I2SSTAT_RB)
#define I2SPCR_START(stype)	\
	((stype) == SNDRV_PCM_STREAM_PLAYBACK ? PSC_I2SPCR_TS : PSC_I2SPCR_RS)
#define I2SPCR_STOP(stype)	\
	((stype) == SNDRV_PCM_STREAM_PLAYBACK ? PSC_I2SPCR_TP : PSC_I2SPCR_RP)
#define I2SPCR_CLRFIFO(stype)	\
	((stype) == SNDRV_PCM_STREAM_PLAYBACK ? PSC_I2SPCR_TC : PSC_I2SPCR_RC)


static int au1xpsc_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
			       unsigned int fmt)
{
	struct au1xpsc_audio_data *pscdata = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long ct;
	int ret;

	ret = -EINVAL;

	ct = pscdata->cfg;

	ct &= ~(PSC_I2SCFG_XM | PSC_I2SCFG_MLJ);	/* left-justified */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ct |= PSC_I2SCFG_XM;	/* enable I2S mode */
		break;
	case SND_SOC_DAIFMT_MSB:
		break;
	case SND_SOC_DAIFMT_LSB:
		ct |= PSC_I2SCFG_MLJ;	/* LSB (right-) justified */
		break;
	default:
		goto out;
	}

	ct &= ~(PSC_I2SCFG_BI | PSC_I2SCFG_WI);		/* IB-IF */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		ct |= PSC_I2SCFG_BI | PSC_I2SCFG_WI;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		ct |= PSC_I2SCFG_BI;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ct |= PSC_I2SCFG_WI;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		break;
	default:
		goto out;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:	/* CODEC master */
		ct |= PSC_I2SCFG_MS;	/* PSC I2S slave mode */
		break;
	case SND_SOC_DAIFMT_CBS_CFS:	/* CODEC slave */
		ct &= ~PSC_I2SCFG_MS;	/* PSC I2S Master mode */
		break;
	default:
		goto out;
	}

	pscdata->cfg = ct;
	ret = 0;
out:
	return ret;
}

static int au1xpsc_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct au1xpsc_audio_data *pscdata = snd_soc_dai_get_drvdata(dai);

	int cfgbits;
	unsigned long stat;

	/* check if the PSC is already streaming data */
	stat = au_readl(I2S_STAT(pscdata));
	if (stat & (PSC_I2SSTAT_TB | PSC_I2SSTAT_RB)) {
		/* reject parameters not currently set up in hardware */
		cfgbits = au_readl(I2S_CFG(pscdata));
		if ((PSC_I2SCFG_GET_LEN(cfgbits) != params->msbits) ||
		    (params_rate(params) != pscdata->rate))
			return -EINVAL;
	} else {
		/* set sample bitdepth */
		pscdata->cfg &= ~(0x1f << 4);
		pscdata->cfg |= PSC_I2SCFG_SET_LEN(params->msbits);
		/* remember current rate for other stream */
		pscdata->rate = params_rate(params);
	}
	return 0;
}

/* Configure PSC late:  on my devel systems the codec  is I2S master and
 * supplies the i2sbitclock __AND__ i2sMclk (!) to the PSC unit.  ASoC
 * uses aggressive PM and  switches the codec off  when it is not in use
 * which also means the PSC unit doesn't get any clocks and is therefore
 * dead. That's why this chunk here gets called from the trigger callback
 * because I can be reasonably certain the codec is driving the clocks.
 */
static int au1xpsc_i2s_configure(struct au1xpsc_audio_data *pscdata)
{
	unsigned long tmo;

	/* bring PSC out of sleep, and configure I2S unit */
	au_writel(PSC_CTRL_ENABLE, PSC_CTRL(pscdata));
	au_sync();

	tmo = 1000000;
	while (!(au_readl(I2S_STAT(pscdata)) & PSC_I2SSTAT_SR) && tmo)
		tmo--;

	if (!tmo)
		goto psc_err;

	au_writel(0, I2S_CFG(pscdata));
	au_sync();
	au_writel(pscdata->cfg | PSC_I2SCFG_DE_ENABLE, I2S_CFG(pscdata));
	au_sync();

	/* wait for I2S controller to become ready */
	tmo = 1000000;
	while (!(au_readl(I2S_STAT(pscdata)) & PSC_I2SSTAT_DR) && tmo)
		tmo--;

	if (tmo)
		return 0;

psc_err:
	au_writel(0, I2S_CFG(pscdata));
	au_writel(PSC_CTRL_SUSPEND, PSC_CTRL(pscdata));
	au_sync();
	return -ETIMEDOUT;
}

static int au1xpsc_i2s_start(struct au1xpsc_audio_data *pscdata, int stype)
{
	unsigned long tmo, stat;
	int ret;

	ret = 0;

	/* if both TX and RX are idle, configure the PSC  */
	stat = au_readl(I2S_STAT(pscdata));
	if (!(stat & (PSC_I2SSTAT_TB | PSC_I2SSTAT_RB))) {
		ret = au1xpsc_i2s_configure(pscdata);
		if (ret)
			goto out;
	}

	au_writel(I2SPCR_CLRFIFO(stype), I2S_PCR(pscdata));
	au_sync();
	au_writel(I2SPCR_START(stype), I2S_PCR(pscdata));
	au_sync();

	/* wait for start confirmation */
	tmo = 1000000;
	while (!(au_readl(I2S_STAT(pscdata)) & I2SSTAT_BUSY(stype)) && tmo)
		tmo--;

	if (!tmo) {
		au_writel(I2SPCR_STOP(stype), I2S_PCR(pscdata));
		au_sync();
		ret = -ETIMEDOUT;
	}
out:
	return ret;
}

static int au1xpsc_i2s_stop(struct au1xpsc_audio_data *pscdata, int stype)
{
	unsigned long tmo, stat;

	au_writel(I2SPCR_STOP(stype), I2S_PCR(pscdata));
	au_sync();

	/* wait for stop confirmation */
	tmo = 1000000;
	while ((au_readl(I2S_STAT(pscdata)) & I2SSTAT_BUSY(stype)) && tmo)
		tmo--;

	/* if both TX and RX are idle, disable PSC */
	stat = au_readl(I2S_STAT(pscdata));
	if (!(stat & (PSC_I2SSTAT_TB | PSC_I2SSTAT_RB))) {
		au_writel(0, I2S_CFG(pscdata));
		au_sync();
		au_writel(PSC_CTRL_SUSPEND, PSC_CTRL(pscdata));
		au_sync();
	}
	return 0;
}

static int au1xpsc_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	struct au1xpsc_audio_data *pscdata = snd_soc_dai_get_drvdata(dai);
	int ret, stype = substream->stream;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = au1xpsc_i2s_start(pscdata, stype);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		ret = au1xpsc_i2s_stop(pscdata, stype);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int au1xpsc_i2s_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct au1xpsc_audio_data *pscdata = snd_soc_dai_get_drvdata(dai);
	snd_soc_dai_set_dma_data(dai, substream, &pscdata->dmaids[0]);
	return 0;
}

static const struct snd_soc_dai_ops au1xpsc_i2s_dai_ops = {
	.startup	= au1xpsc_i2s_startup,
	.trigger	= au1xpsc_i2s_trigger,
	.hw_params	= au1xpsc_i2s_hw_params,
	.set_fmt	= au1xpsc_i2s_set_fmt,
};

static const struct snd_soc_dai_driver au1xpsc_i2s_dai_template = {
	.playback = {
		.rates		= AU1XPSC_I2S_RATES,
		.formats	= AU1XPSC_I2S_FMTS,
		.channels_min	= 2,
		.channels_max	= 8,	/* 2 without external help */
	},
	.capture = {
		.rates		= AU1XPSC_I2S_RATES,
		.formats	= AU1XPSC_I2S_FMTS,
		.channels_min	= 2,
		.channels_max	= 8,	/* 2 without external help */
	},
	.ops = &au1xpsc_i2s_dai_ops,
};

static const struct snd_soc_component_driver au1xpsc_i2s_component = {
	.name		= "au1xpsc-i2s",
};

static int au1xpsc_i2s_drvprobe(struct platform_device *pdev)
{
	struct resource *iores, *dmares;
	unsigned long sel;
	int ret;
	struct au1xpsc_audio_data *wd;

	wd = devm_kzalloc(&pdev->dev, sizeof(struct au1xpsc_audio_data),
			  GFP_KERNEL);
	if (!wd)
		return -ENOMEM;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -ENODEV;

	ret = -EBUSY;
	if (!devm_request_mem_region(&pdev->dev, iores->start,
				     resource_size(iores),
				     pdev->name))
		return -EBUSY;

	wd->mmio = devm_ioremap(&pdev->dev, iores->start,
				resource_size(iores));
	if (!wd->mmio)
		return -EBUSY;

	dmares = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!dmares)
		return -EBUSY;
	wd->dmaids[SNDRV_PCM_STREAM_PLAYBACK] = dmares->start;

	dmares = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!dmares)
		return -EBUSY;
	wd->dmaids[SNDRV_PCM_STREAM_CAPTURE] = dmares->start;

	/* preserve PSC clock source set up by platform (dev.platform_data
	 * is already occupied by soc layer)
	 */
	sel = au_readl(PSC_SEL(wd)) & PSC_SEL_CLK_MASK;
	au_writel(PSC_CTRL_DISABLE, PSC_CTRL(wd));
	au_sync();
	au_writel(PSC_SEL_PS_I2SMODE | sel, PSC_SEL(wd));
	au_writel(0, I2S_CFG(wd));
	au_sync();

	/* preconfigure: set max rx/tx fifo depths */
	wd->cfg |= PSC_I2SCFG_RT_FIFO8 | PSC_I2SCFG_TT_FIFO8;

	/* don't wait for I2S core to become ready now; clocks may not
	 * be running yet; depending on clock input for PSC a wait might
	 * time out.
	 */

	/* name the DAI like this device instance ("au1xpsc-i2s.PSCINDEX") */
	memcpy(&wd->dai_drv, &au1xpsc_i2s_dai_template,
	       sizeof(struct snd_soc_dai_driver));
	wd->dai_drv.name = dev_name(&pdev->dev);

	platform_set_drvdata(pdev, wd);

	return snd_soc_register_component(&pdev->dev, &au1xpsc_i2s_component,
					  &wd->dai_drv, 1);
}

static int au1xpsc_i2s_drvremove(struct platform_device *pdev)
{
	struct au1xpsc_audio_data *wd = platform_get_drvdata(pdev);

	snd_soc_unregister_component(&pdev->dev);

	au_writel(0, I2S_CFG(wd));
	au_sync();
	au_writel(PSC_CTRL_DISABLE, PSC_CTRL(wd));
	au_sync();

	return 0;
}

#ifdef CONFIG_PM
static int au1xpsc_i2s_drvsuspend(struct device *dev)
{
	struct au1xpsc_audio_data *wd = dev_get_drvdata(dev);

	/* save interesting register and disable PSC */
	wd->pm[0] = au_readl(PSC_SEL(wd));

	au_writel(0, I2S_CFG(wd));
	au_sync();
	au_writel(PSC_CTRL_DISABLE, PSC_CTRL(wd));
	au_sync();

	return 0;
}

static int au1xpsc_i2s_drvresume(struct device *dev)
{
	struct au1xpsc_audio_data *wd = dev_get_drvdata(dev);

	/* select I2S mode and PSC clock */
	au_writel(PSC_CTRL_DISABLE, PSC_CTRL(wd));
	au_sync();
	au_writel(0, PSC_SEL(wd));
	au_sync();
	au_writel(wd->pm[0], PSC_SEL(wd));
	au_sync();

	return 0;
}

static struct dev_pm_ops au1xpsci2s_pmops = {
	.suspend	= au1xpsc_i2s_drvsuspend,
	.resume		= au1xpsc_i2s_drvresume,
};

#define AU1XPSCI2S_PMOPS &au1xpsci2s_pmops

#else

#define AU1XPSCI2S_PMOPS NULL

#endif

static struct platform_driver au1xpsc_i2s_driver = {
	.driver		= {
		.name	= "au1xpsc_i2s",
		.owner	= THIS_MODULE,
		.pm	= AU1XPSCI2S_PMOPS,
	},
	.probe		= au1xpsc_i2s_drvprobe,
	.remove		= au1xpsc_i2s_drvremove,
};

module_platform_driver(au1xpsc_i2s_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Au12x0/Au1550 PSC I2S ALSA ASoC audio driver");
MODULE_AUTHOR("Manuel Lauss");

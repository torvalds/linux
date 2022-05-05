// SPDX-License-Identifier: GPL-2.0
/*
 * TDM driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/reset.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include "starfive_tdm.h"

#define AUDIOC_CLK 	(12288000)

static inline u32 sf_tdm_readl(struct sf_tdm_dev *tdm, u16 reg)
{
	return readl_relaxed(tdm->tdm_base + reg);
}

static inline void sf_tdm_writel(struct sf_tdm_dev *tdm, u16 reg, u32 val)
{
	writel_relaxed(val, tdm->tdm_base + reg);
}

static void sf_tdm_start(struct sf_tdm_dev *tdm, struct snd_pcm_substream *substream)
{
	u32 data;
	
	data = sf_tdm_readl(tdm, TDM_PCMGBCR);
	sf_tdm_writel(tdm, TDM_PCMGBCR, data | 0x1 | (0x1<<4));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		sf_tdm_writel(tdm, TDM_PCMTXCR, sf_tdm_readl(tdm, TDM_PCMTXCR) | 0x1);
	else
		sf_tdm_writel(tdm, TDM_PCMRXCR, sf_tdm_readl(tdm, TDM_PCMRXCR) | 0x1);
}

static void sf_tdm_stop(struct sf_tdm_dev*tdm, struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		sf_tdm_writel(tdm, TDM_PCMTXCR, sf_tdm_readl(tdm, TDM_PCMTXCR) & 0xffe);
	else
		sf_tdm_writel(tdm, TDM_PCMRXCR, sf_tdm_readl(tdm, TDM_PCMRXCR) & 0xffe);

	sf_tdm_writel(tdm, TDM_PCMGBCR, sf_tdm_readl(tdm, TDM_PCMGBCR) & 0x1e);
}

static int sf_tdm_syncdiv(struct sf_tdm_dev *tdm)
{
	u32 sl, sscale, syncdiv;

	sl = (tdm->rx.sl >= tdm->tx.sl) ? tdm->rx.sl:tdm->tx.sl;
	sscale = (tdm->rx.sscale >= tdm->tx.sscale) ? tdm->rx.sscale:tdm->tx.sscale;
	syncdiv = tdm->pcmclk / tdm->samplerate - 1;

	if ((syncdiv + 1) < (sl * sscale)) {
		pr_info("set syncdiv failed !\n");
		return -1;
	}

	if ((tdm->syncm == TDM_SYNCM_LONG) && ((tdm->rx.sscale <= 1) || (tdm->tx.sscale <= 1))) {
		if ((syncdiv + 1) <= sl) {
			pr_info("set syncdiv failed !  it must be (syncdiv+1) > max[txsl, rxsl]\n");
			return -1;
		}
	}

	sf_tdm_writel(tdm, TDM_PCMDIV, syncdiv);
	return 0;
}

static void sf_tdm_contrl(struct sf_tdm_dev *tdm)
{
	u32 data;

	data = (tdm->clkpolity << 5) | (tdm->elm << 3) | (tdm->syncm << 2) | (tdm->mode << 1);
	sf_tdm_writel(tdm, TDM_PCMGBCR, data);
}

static void sf_tdm_config(struct sf_tdm_dev *tdm, struct snd_pcm_substream *substream)
{
	u32 datarx, datatx;
	
	sf_tdm_stop(tdm, substream);
	sf_tdm_contrl(tdm);
	sf_tdm_syncdiv(tdm);

	datarx = (tdm->rx.ifl << 11) | (tdm->rx.wl << 8) | (tdm->rx.sscale << 4) |
		(tdm->rx.sl << 2) | (tdm->rx.lrj << 1);

	datatx = (tdm->tx.ifl << 11) | (tdm->tx.wl << 8) | (tdm->tx.sscale << 4) |
		(tdm->tx.sl << 2) | (tdm->tx.lrj << 1);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		sf_tdm_writel(tdm, TDM_PCMTXCR, datatx);
	else
		sf_tdm_writel(tdm, TDM_PCMRXCR, datarx);

	sf_tdm_start(tdm, substream);
}


#define sf_tdm_suspend	NULL
#define sf_tdm_resume	NULL

static const struct snd_soc_component_driver sf_tdm_component = {
	.name		= "sf-tdm",
	.suspend	= sf_tdm_suspend,
	.resume		= sf_tdm_resume,
};

static int sf_tdm_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	struct sf_tdm_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);
	struct snd_dmaengine_dai_dma_data *dma_data = NULL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &dev->play_dma_data;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		dma_data = &dev->capture_dma_data;

	snd_soc_dai_set_dma_data(cpu_dai, substream, (void *)dma_data);

	return 0;
}

static void sf_tdm_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}


static int sf_tdm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sf_tdm_dev *dev = snd_soc_dai_get_drvdata(dai);
	int chan_wl, chan_sl, chan_nr;

	switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S8:
				chan_wl = TDM_8BIT_WORD_LEN;
				chan_sl = TDM_8BIT_SLOT_LEN;
			break;
		
		case SNDRV_PCM_FORMAT_S16_LE:
				chan_wl = TDM_16BIT_WORD_LEN;
				chan_sl = TDM_16BIT_SLOT_LEN;
			break;

		case SNDRV_PCM_FORMAT_S24_LE:
				chan_wl = TDM_24BIT_WORD_LEN;
				chan_sl = TDM_32BIT_SLOT_LEN;
			break;

		case SNDRV_PCM_FORMAT_S32_LE:
				chan_wl = TDM_32BIT_WORD_LEN;
				chan_sl = TDM_32BIT_SLOT_LEN;
			break;

		default:
			dev_err(dev->dev, "tdm: unsupported PCM fmt");
			return -EINVAL;
	}

	chan_nr = params_channels(params);
	switch (chan_nr) {
		case TWO_CHANNEL_SUPPORT:
		case FOUR_CHANNEL_SUPPORT:
		case SIX_CHANNEL_SUPPORT:
		case EIGHT_CHANNEL_SUPPORT:
			break;
		default:
			dev_err(dev->dev, "channel not supported\n");
			return -EINVAL;
	}
	
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dev->tx.wl = chan_wl;
		dev->tx.sl = chan_sl;
		dev->tx.sscale = chan_nr;
	} else {
		dev->rx.wl = chan_wl;
		dev->rx.sl = chan_sl;
		dev->rx.sscale = chan_nr;
	}

	dev->samplerate = params_rate(params);
	if (!dev->mode) {
		sf_tdm_syncdiv(dev);
	}
	
	sf_tdm_config(dev, substream);

	return 0;
}

static int sf_tdm_prepare(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	return 0;
}

static int sf_tdm_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct sf_tdm_dev *dev = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev->active++;
		sf_tdm_start(dev, substream);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dev->active--;
		sf_tdm_stop(dev, substream);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int sf_tdm_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct sf_tdm_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		dev->mode = TDM_AS_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		dev->mode = TDM_AS_MASTER;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		ret = -EINVAL;
		break;
	default:
		dev_dbg(dev->dev, "tdm : Invalid master/slave format\n");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const struct snd_soc_dai_ops sf_tdm_dai_ops = {
	.startup	= sf_tdm_startup,
	.shutdown	= sf_tdm_shutdown,
	.hw_params	= sf_tdm_hw_params,
	.prepare	= sf_tdm_prepare,
	.trigger	= sf_tdm_trigger,
	.set_fmt	= sf_tdm_set_fmt,
};

static int tdm_configure_dai(struct sf_tdm_dev *dev,
				   struct snd_soc_dai_driver *sf_tdm_dai,
				   unsigned int rates)
{
	sf_tdm_dai->playback.channels_min = TDM_MIN_CHANNEL_NUM;
	sf_tdm_dai->playback.channels_max = TDM_MAX_CHANNEL_NUM;
	sf_tdm_dai->playback.formats = SNDRV_PCM_FMTBIT_S8|SNDRV_PCM_FMTBIT_S16_LE|SNDRV_PCM_FMTBIT_S24_LE|SNDRV_PCM_FMTBIT_S32_LE;
	sf_tdm_dai->playback.rates = rates;

	sf_tdm_dai->capture.channels_min = TDM_MIN_CHANNEL_NUM;
	sf_tdm_dai->capture.channels_max = TDM_MAX_CHANNEL_NUM;
	sf_tdm_dai->capture.formats = SNDRV_PCM_FMTBIT_S8|SNDRV_PCM_FMTBIT_S16_LE|SNDRV_PCM_FMTBIT_S24_LE|SNDRV_PCM_FMTBIT_S32_LE;
	sf_tdm_dai->capture.rates = rates;

	return 0;
}

static int sf_tdm_dai_probe(struct snd_soc_dai *dai)
{
	struct sf_tdm_dev *dev = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &dev->play_dma_data, &dev->capture_dma_data);
	return 0;
}

static int sf_tdm_clock_init(struct platform_device *pdev, struct sf_tdm_dev *dev)
{
	int ret;

	dev->rst_ahb = devm_reset_control_get_exclusive(&pdev->dev, "tdm_ahb");
	if (IS_ERR(dev->rst_ahb)) {
		dev_err(&pdev->dev, "Failed to get tdm_ahb reset control\n");
		return PTR_ERR(dev->rst_ahb);
	}

	dev->rst_apb = devm_reset_control_get_exclusive(&pdev->dev, "tdm_apb");
	if (IS_ERR(dev->rst_apb)) {
		dev_err(&pdev->dev, "Failed to get tdm_apb reset control\n");
		return PTR_ERR(dev->rst_apb);
	}

	dev->rst_tdm = devm_reset_control_get_exclusive(&pdev->dev, "tdm_rst");
	if (IS_ERR(dev->rst_tdm)) {
		dev_err(&pdev->dev, "Failed to get tdm_rst reset control\n");
		return PTR_ERR(dev->rst_tdm);
	}

	dev->clk_ahb0 = devm_clk_get(&pdev->dev, "clk_ahb0");
	if (IS_ERR(dev->clk_ahb0)) {
		dev_err(&pdev->dev, "Failed to get clk_ahb0");
		return PTR_ERR(dev->clk_ahb0);
	}

	dev->clk_tdm_ahb = devm_clk_get(&pdev->dev, "clk_tdm_ahb");
	if (IS_ERR(dev->clk_tdm_ahb)) {
		dev_err(&pdev->dev, "Failed to get clk_tdm_ahb");
		return PTR_ERR(dev->clk_tdm_ahb);
	}

	dev->clk_apb0 = devm_clk_get(&pdev->dev, "clk_apb0");
	if (IS_ERR(dev->clk_apb0)) {
		dev_err(&pdev->dev, "Failed to get clk_apb0");
		return PTR_ERR(dev->clk_apb0);
	}

	dev->clk_tdm_apb = devm_clk_get(&pdev->dev, "clk_tdm_apb");
	if (IS_ERR(dev->clk_tdm_apb)) {
		dev_err(&pdev->dev, "Failed to get clk_tdm_apb");
		return PTR_ERR(dev->clk_tdm_ahb);
	}

	dev->clk_tdm_intl = devm_clk_get(&pdev->dev, "clk_tdm_intl");
	if (IS_ERR(dev->clk_tdm_intl)) {
		dev_err(&pdev->dev, "Failed to get clk_tdm_intl");
		return PTR_ERR(dev->clk_tdm_intl);
	}

	ret = clk_prepare_enable(dev->clk_ahb0);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare enable clk_ahb0\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(dev->clk_tdm_ahb);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare enable clk_tdm_ahb\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(dev->clk_apb0);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare enable clk_apb0\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(dev->clk_tdm_apb);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare enable clk_tdm_apb\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(dev->clk_tdm_intl);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare enable clk_tdm_intl\n");
		goto err_clk_disable;
	}

	ret = reset_control_deassert(dev->rst_ahb);
	if (ret) {
		dev_err(&pdev->dev, "failed to deassert rst_ahb\n");
		goto err_clk_disable;
	}

	ret = reset_control_deassert(dev->rst_apb);
	if (ret) {
		dev_err(&pdev->dev, "failed to deassert rst_apb\n");
		goto err_clk_disable;
	}

	ret = reset_control_deassert(dev->rst_tdm);
	if (ret) {
		dev_err(&pdev->dev, "failed to deassert rst_tdm\n");
		goto err_clk_disable;
	}

	return 0;

err_clk_disable:
	return ret;
}

static int sf_tdm_probe(struct platform_device *pdev)
{
	struct sf_tdm_dev *dev;
	struct resource *res;
	int ret;
	struct snd_soc_dai_driver *sf_tdm_dai;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	sf_tdm_dai = devm_kzalloc(&pdev->dev, sizeof(*sf_tdm_dai), GFP_KERNEL);
	if (!sf_tdm_dai)
		return -ENOMEM;

	sf_tdm_dai->ops = &sf_tdm_dai_ops;
	sf_tdm_dai->probe = sf_tdm_dai_probe;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->tdm_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->tdm_base))
		return PTR_ERR(dev->tdm_base);

	dev->dev = &pdev->dev;

	ret = tdm_configure_dai(dev, sf_tdm_dai, SNDRV_PCM_RATE_8000_192000);
	if (ret < 0)
		return ret;

	dev->clkpolity = TDM_TX_RASING_RX_FALLING;
	dev->tritxen = 1;
	dev->elm = TDM_ELM_LATE;
	dev->syncm = TDM_SYNCM_SHORT;
	dev->mode = TDM_AS_MASTER;
	dev->rx.ifl = TDM_FIFO_HALF;
	dev->tx.ifl = TDM_FIFO_HALF;
	dev->rx.sscale = 1;
	dev->tx.sscale = 1;
	dev->rx.lrj = TDM_LEFT_JUSTIFT;
	dev->tx.lrj = TDM_LEFT_JUSTIFT;
	 
	dev->samplerate = 16000;
	dev->pcmclk = 4096000;

	dev->play_dma_data.addr = res->start + TDM_TXDMA;
	dev->play_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dev->play_dma_data.fifo_size = TDM_FIFO_DEPTH/2;
	dev->play_dma_data.maxburst = 16;

	dev->capture_dma_data.addr = res->start  + TDM_RXDMA;
	dev->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dev->capture_dma_data.fifo_size = TDM_FIFO_DEPTH/2;
	dev->capture_dma_data.maxburst = 16;
	
	ret = sf_tdm_clock_init(pdev, dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable audio-tdm clock\n");
		return ret;
	}

	ret = sf_tdm_clock_init(pdev, dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable audio-tdm clock\n");
		return ret;
	}

	dev_set_drvdata(&pdev->dev, dev);
	ret = devm_snd_soc_register_component(&pdev->dev, &sf_tdm_component,
					 sf_tdm_dai, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "not able to register dai\n");
		return ret;
	}
	
	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL,	0);
	if (ret) {
		dev_err(&pdev->dev, "could not register pcm: %d\n",
				ret);
		return ret;
	}
	
	return 0;
}


static int sf_tdm_dev_remove(struct platform_device *pdev)
{
	return 0;
}
static const struct of_device_id sf_tdm_of_match[] = {
	{.compatible = "starfive,sf-tdm",}, 
	{}
};
MODULE_DEVICE_TABLE(of, sf_tdm_of_match);

static struct platform_driver sf_tdm_driver = {

	.driver = {
		.name = "sf-tdm",
		.of_match_table = sf_tdm_of_match,
	},
	.probe = sf_tdm_probe,
	.remove = sf_tdm_dev_remove,
};
module_platform_driver(sf_tdm_driver);

MODULE_AUTHOR("Walker Chen <walker.chen@starfivetech.com>");
MODULE_DESCRIPTION("starfive TDM Controller Driver");
MODULE_LICENSE("GPL v2");

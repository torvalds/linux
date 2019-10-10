// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SiRF USP in I2S/DSP mode
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#include "sirf-usp.h"

struct sirf_usp {
	struct regmap *regmap;
	struct clk *clk;
	u32 mode1_reg;
	u32 mode2_reg;
	int daifmt_format;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
};

static void sirf_usp_tx_enable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_FIFO_OP,
		USP_TX_FIFO_RESET, USP_TX_FIFO_RESET);
	regmap_write(usp->regmap, USP_TX_FIFO_OP, 0);

	regmap_update_bits(usp->regmap, USP_TX_FIFO_OP,
		USP_TX_FIFO_START, USP_TX_FIFO_START);

	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_TX_ENA, USP_TX_ENA);
}

static void sirf_usp_tx_disable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_TX_ENA, ~USP_TX_ENA);
	/* FIFO stop */
	regmap_write(usp->regmap, USP_TX_FIFO_OP, 0);
}

static void sirf_usp_rx_enable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_RX_FIFO_OP,
		USP_RX_FIFO_RESET, USP_RX_FIFO_RESET);
	regmap_write(usp->regmap, USP_RX_FIFO_OP, 0);

	regmap_update_bits(usp->regmap, USP_RX_FIFO_OP,
		USP_RX_FIFO_START, USP_RX_FIFO_START);

	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_RX_ENA, USP_RX_ENA);
}

static void sirf_usp_rx_disable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_RX_ENA, ~USP_RX_ENA);
	/* FIFO stop */
	regmap_write(usp->regmap, USP_RX_FIFO_OP, 0);
}

static int sirf_usp_pcm_dai_probe(struct snd_soc_dai *dai)
{
	struct sirf_usp *usp = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &usp->playback_dma_data,
			&usp->capture_dma_data);
	return 0;
}

static int sirf_usp_pcm_set_dai_fmt(struct snd_soc_dai *dai,
		unsigned int fmt)
{
	struct sirf_usp *usp = snd_soc_dai_get_drvdata(dai);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		dev_err(dai->dev, "Only CBM and CFM supported\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_DSP_A:
		usp->daifmt_format = (fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		break;
	default:
		dev_err(dai->dev, "Only I2S and DSP_A format supported\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		usp->daifmt_format |= (fmt & SND_SOC_DAIFMT_INV_MASK);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void sirf_usp_i2s_init(struct sirf_usp *usp)
{
	/* Configure RISC mode */
	regmap_update_bits(usp->regmap, USP_RISC_DSP_MODE,
		USP_RISC_DSP_SEL, ~USP_RISC_DSP_SEL);

	/*
	 * Configure DMA IO Length register
	 * Set no limit, USP can receive data continuously until it is diabled
	 */
	regmap_write(usp->regmap, USP_TX_DMA_IO_LEN, 0);
	regmap_write(usp->regmap, USP_RX_DMA_IO_LEN, 0);

	/* Configure Mode2 register */
	regmap_write(usp->regmap, USP_MODE2, (1 << USP_RXD_DELAY_LEN_OFFSET) |
		(0 << USP_TXD_DELAY_LEN_OFFSET) |
		USP_TFS_CLK_SLAVE_MODE | USP_RFS_CLK_SLAVE_MODE);

	/* Configure Mode1 register */
	regmap_write(usp->regmap, USP_MODE1,
		USP_SYNC_MODE | USP_EN | USP_TXD_ACT_EDGE_FALLING |
		USP_RFS_ACT_LEVEL_LOGIC1 | USP_TFS_ACT_LEVEL_LOGIC1 |
		USP_TX_UFLOW_REPEAT_ZERO | USP_CLOCK_MODE_SLAVE);

	/* Configure RX DMA IO Control register */
	regmap_write(usp->regmap, USP_RX_DMA_IO_CTRL, 0);

	/* Congiure RX FIFO Control register */
	regmap_write(usp->regmap, USP_RX_FIFO_CTRL,
		(USP_RX_FIFO_THRESHOLD << USP_RX_FIFO_THD_OFFSET) |
		(USP_TX_RX_FIFO_WIDTH_DWORD << USP_RX_FIFO_WIDTH_OFFSET));

	/* Congiure RX FIFO Level Check register */
	regmap_write(usp->regmap, USP_RX_FIFO_LEVEL_CHK,
		RX_FIFO_SC(0x04) | RX_FIFO_LC(0x0E) | RX_FIFO_HC(0x1B));

	/* Configure TX DMA IO Control register*/
	regmap_write(usp->regmap, USP_TX_DMA_IO_CTRL, 0);

	/* Configure TX FIFO Control register */
	regmap_write(usp->regmap, USP_TX_FIFO_CTRL,
		(USP_TX_FIFO_THRESHOLD << USP_TX_FIFO_THD_OFFSET) |
		(USP_TX_RX_FIFO_WIDTH_DWORD << USP_TX_FIFO_WIDTH_OFFSET));
	/* Congiure TX FIFO Level Check register */
	regmap_write(usp->regmap, USP_TX_FIFO_LEVEL_CHK,
		TX_FIFO_SC(0x1B) | TX_FIFO_LC(0x0E) | TX_FIFO_HC(0x04));
}

static int sirf_usp_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sirf_usp *usp = snd_soc_dai_get_drvdata(dai);
	u32 data_len, frame_len, shifter_len;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_len = 16;
		frame_len = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_len = 24;
		frame_len = 32;
		break;
	case SNDRV_PCM_FORMAT_S24_3LE:
		data_len = 24;
		frame_len = 24;
		break;
	default:
		dev_err(dai->dev, "Format unsupported\n");
		return -EINVAL;
	}

	shifter_len = data_len;

	switch (usp->daifmt_format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		regmap_update_bits(usp->regmap, USP_RX_FRAME_CTRL,
			USP_I2S_SYNC_CHG, USP_I2S_SYNC_CHG);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		regmap_update_bits(usp->regmap, USP_RX_FRAME_CTRL,
			USP_I2S_SYNC_CHG, 0);
		frame_len = data_len * params_channels(params);
		data_len = frame_len;
		break;
	default:
		dev_err(dai->dev, "Only support I2S and DSP_A mode\n");
		return -EINVAL;
	}

	switch (usp->daifmt_format & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		regmap_update_bits(usp->regmap, USP_MODE1,
			USP_RXD_ACT_EDGE_FALLING | USP_TXD_ACT_EDGE_FALLING,
			USP_RXD_ACT_EDGE_FALLING);
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_update_bits(usp->regmap, USP_TX_FRAME_CTRL,
			USP_TXC_DATA_LEN_MASK | USP_TXC_FRAME_LEN_MASK
			| USP_TXC_SHIFTER_LEN_MASK | USP_TXC_SLAVE_CLK_SAMPLE,
			((data_len - 1) << USP_TXC_DATA_LEN_OFFSET)
			| ((frame_len - 1) << USP_TXC_FRAME_LEN_OFFSET)
			| ((shifter_len - 1) << USP_TXC_SHIFTER_LEN_OFFSET)
			| USP_TXC_SLAVE_CLK_SAMPLE);
	else
		regmap_update_bits(usp->regmap, USP_RX_FRAME_CTRL,
			USP_RXC_DATA_LEN_MASK | USP_RXC_FRAME_LEN_MASK
			| USP_RXC_SHIFTER_LEN_MASK | USP_SINGLE_SYNC_MODE,
			((data_len - 1) << USP_RXC_DATA_LEN_OFFSET)
			| ((frame_len - 1) << USP_RXC_FRAME_LEN_OFFSET)
			| ((shifter_len - 1) << USP_RXC_SHIFTER_LEN_OFFSET)
			| USP_SINGLE_SYNC_MODE);

	return 0;
}

static int sirf_usp_pcm_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct sirf_usp *usp = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sirf_usp_tx_enable(usp);
		else
			sirf_usp_rx_enable(usp);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sirf_usp_tx_disable(usp);
		else
			sirf_usp_rx_disable(usp);
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops sirf_usp_pcm_dai_ops = {
	.trigger = sirf_usp_pcm_trigger,
	.set_fmt = sirf_usp_pcm_set_dai_fmt,
	.hw_params = sirf_usp_pcm_hw_params,
};

static struct snd_soc_dai_driver sirf_usp_pcm_dai = {
	.probe = sirf_usp_pcm_dai_probe,
	.name = "sirf-usp-pcm",
	.id = 0,
	.playback = {
		.stream_name = "SiRF USP PCM Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S24_3LE,
	},
	.capture = {
		.stream_name = "SiRF USP PCM Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S24_3LE,
	},
	.ops = &sirf_usp_pcm_dai_ops,
};

static int sirf_usp_pcm_runtime_suspend(struct device *dev)
{
	struct sirf_usp *usp = dev_get_drvdata(dev);

	clk_disable_unprepare(usp->clk);
	return 0;
}

static int sirf_usp_pcm_runtime_resume(struct device *dev)
{
	struct sirf_usp *usp = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(usp->clk);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}
	sirf_usp_i2s_init(usp);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sirf_usp_pcm_suspend(struct device *dev)
{
	struct sirf_usp *usp = dev_get_drvdata(dev);

	if (!pm_runtime_status_suspended(dev)) {
		regmap_read(usp->regmap, USP_MODE1, &usp->mode1_reg);
		regmap_read(usp->regmap, USP_MODE2, &usp->mode2_reg);
		sirf_usp_pcm_runtime_suspend(dev);
	}
	return 0;
}

static int sirf_usp_pcm_resume(struct device *dev)
{
	struct sirf_usp *usp = dev_get_drvdata(dev);
	int ret;

	if (!pm_runtime_status_suspended(dev)) {
		ret = sirf_usp_pcm_runtime_resume(dev);
		if (ret)
			return ret;
		regmap_write(usp->regmap, USP_MODE1, usp->mode1_reg);
		regmap_write(usp->regmap, USP_MODE2, usp->mode2_reg);
	}
	return 0;
}
#endif

static const struct snd_soc_component_driver sirf_usp_component = {
	.name		= "sirf-usp",
};

static const struct regmap_config sirf_usp_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = USP_RX_FIFO_DATA,
	.cache_type = REGCACHE_NONE,
};

static int sirf_usp_pcm_probe(struct platform_device *pdev)
{
	int ret;
	struct sirf_usp *usp;
	void __iomem *base;

	usp = devm_kzalloc(&pdev->dev, sizeof(struct sirf_usp),
			GFP_KERNEL);
	if (!usp)
		return -ENOMEM;

	platform_set_drvdata(pdev, usp);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);
	usp->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &sirf_usp_regmap_config);
	if (IS_ERR(usp->regmap))
		return PTR_ERR(usp->regmap);

	usp->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(usp->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		return PTR_ERR(usp->clk);
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = sirf_usp_pcm_runtime_resume(&pdev->dev);
		if (ret)
			return ret;
	}

	ret = devm_snd_soc_register_component(&pdev->dev, &sirf_usp_component,
		&sirf_usp_pcm_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio SoC dai failed.\n");
		return ret;
	}
	return devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
}

static int sirf_usp_pcm_remove(struct platform_device *pdev)
{
	if (!pm_runtime_enabled(&pdev->dev))
		sirf_usp_pcm_runtime_suspend(&pdev->dev);
	else
		pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id sirf_usp_pcm_of_match[] = {
	{ .compatible = "sirf,prima2-usp-pcm", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_usp_pcm_of_match);

static const struct dev_pm_ops sirf_usp_pcm_pm_ops = {
	SET_RUNTIME_PM_OPS(sirf_usp_pcm_runtime_suspend,
		sirf_usp_pcm_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(sirf_usp_pcm_suspend, sirf_usp_pcm_resume)
};

static struct platform_driver sirf_usp_pcm_driver = {
	.driver = {
		.name = "sirf-usp-pcm",
		.of_match_table = sirf_usp_pcm_of_match,
		.pm = &sirf_usp_pcm_pm_ops,
	},
	.probe = sirf_usp_pcm_probe,
	.remove = sirf_usp_pcm_remove,
};

module_platform_driver(sirf_usp_pcm_driver);

MODULE_DESCRIPTION("SiRF SoC USP PCM bus driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");

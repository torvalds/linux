// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Linaro
 *
 * Author: Jun Nie <jun.nie@linaro.org>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>

#define ZX_I2S_PROCESS_CTRL	0x04
#define ZX_I2S_TIMING_CTRL	0x08
#define	ZX_I2S_FIFO_CTRL	0x0C
#define	ZX_I2S_FIFO_STATUS	0x10
#define ZX_I2S_INT_EN		0x14
#define ZX_I2S_INT_STATUS	0x18
#define ZX_I2S_DATA		0x1C
#define ZX_I2S_FRAME_CNTR	0x20

#define I2S_DEAGULT_FIFO_THRES	(0x10)
#define I2S_MAX_FIFO_THRES	(0x20)

#define ZX_I2S_PROCESS_TX_EN	(1 << 0)
#define ZX_I2S_PROCESS_TX_DIS	(0 << 0)
#define ZX_I2S_PROCESS_RX_EN	(1 << 1)
#define ZX_I2S_PROCESS_RX_DIS	(0 << 1)
#define ZX_I2S_PROCESS_I2S_EN	(1 << 2)
#define ZX_I2S_PROCESS_I2S_DIS	(0 << 2)

#define ZX_I2S_TIMING_MAST		(1 << 0)
#define ZX_I2S_TIMING_SLAVE		(0 << 0)
#define ZX_I2S_TIMING_MS_MASK		(1 << 0)
#define ZX_I2S_TIMING_LOOP		(1 << 1)
#define ZX_I2S_TIMING_NOR		(0 << 1)
#define ZX_I2S_TIMING_LOOP_MASK		(1 << 1)
#define ZX_I2S_TIMING_PTNR		(1 << 2)
#define ZX_I2S_TIMING_NTPR		(0 << 2)
#define ZX_I2S_TIMING_PHASE_MASK	(1 << 2)
#define ZX_I2S_TIMING_TDM		(1 << 3)
#define ZX_I2S_TIMING_I2S		(0 << 3)
#define ZX_I2S_TIMING_TIMING_MASK	(1 << 3)
#define ZX_I2S_TIMING_LONG_SYNC		(1 << 4)
#define ZX_I2S_TIMING_SHORT_SYNC	(0 << 4)
#define ZX_I2S_TIMING_SYNC_MASK		(1 << 4)
#define ZX_I2S_TIMING_TEAK_EN		(1 << 5)
#define ZX_I2S_TIMING_TEAK_DIS		(0 << 5)
#define ZX_I2S_TIMING_TEAK_MASK		(1 << 5)
#define ZX_I2S_TIMING_STD_I2S		(0 << 6)
#define ZX_I2S_TIMING_MSB_JUSTIF	(1 << 6)
#define ZX_I2S_TIMING_LSB_JUSTIF	(2 << 6)
#define ZX_I2S_TIMING_ALIGN_MASK	(3 << 6)
#define ZX_I2S_TIMING_CHN_MASK		(7 << 8)
#define ZX_I2S_TIMING_CHN(x)		((x - 1) << 8)
#define ZX_I2S_TIMING_LANE_MASK		(3 << 11)
#define ZX_I2S_TIMING_LANE(x)		((x - 1) << 11)
#define ZX_I2S_TIMING_TSCFG_MASK	(7 << 13)
#define ZX_I2S_TIMING_TSCFG(x)		(x << 13)
#define ZX_I2S_TIMING_TS_WIDTH_MASK	(0x1f << 16)
#define ZX_I2S_TIMING_TS_WIDTH(x)	((x - 1) << 16)
#define ZX_I2S_TIMING_DATA_SIZE_MASK	(0x1f << 21)
#define ZX_I2S_TIMING_DATA_SIZE(x)	((x - 1) << 21)
#define ZX_I2S_TIMING_CFG_ERR_MASK	(1 << 31)

#define ZX_I2S_FIFO_CTRL_TX_RST		(1 << 0)
#define ZX_I2S_FIFO_CTRL_TX_RST_MASK	(1 << 0)
#define ZX_I2S_FIFO_CTRL_RX_RST		(1 << 1)
#define ZX_I2S_FIFO_CTRL_RX_RST_MASK	(1 << 1)
#define ZX_I2S_FIFO_CTRL_TX_DMA_EN	(1 << 4)
#define ZX_I2S_FIFO_CTRL_TX_DMA_DIS	(0 << 4)
#define ZX_I2S_FIFO_CTRL_TX_DMA_MASK	(1 << 4)
#define ZX_I2S_FIFO_CTRL_RX_DMA_EN	(1 << 5)
#define ZX_I2S_FIFO_CTRL_RX_DMA_DIS	(0 << 5)
#define ZX_I2S_FIFO_CTRL_RX_DMA_MASK	(1 << 5)
#define ZX_I2S_FIFO_CTRL_TX_THRES_MASK	(0x1F << 8)
#define ZX_I2S_FIFO_CTRL_RX_THRES_MASK	(0x1F << 16)

#define CLK_RAT (32 * 4)

struct zx_i2s_info {
	struct snd_dmaengine_dai_dma_data	dma_playback;
	struct snd_dmaengine_dai_dma_data	dma_capture;
	struct clk				*dai_wclk;
	struct clk				*dai_pclk;
	void __iomem				*reg_base;
	int					master;
	resource_size_t				mapbase;
};

static void zx_i2s_tx_en(void __iomem *base, bool on)
{
	unsigned long val;

	val = readl_relaxed(base + ZX_I2S_PROCESS_CTRL);
	if (on)
		val |= ZX_I2S_PROCESS_TX_EN | ZX_I2S_PROCESS_I2S_EN;
	else
		val &= ~(ZX_I2S_PROCESS_TX_EN | ZX_I2S_PROCESS_I2S_EN);
	writel_relaxed(val, base + ZX_I2S_PROCESS_CTRL);
}

static void zx_i2s_rx_en(void __iomem *base, bool on)
{
	unsigned long val;

	val = readl_relaxed(base + ZX_I2S_PROCESS_CTRL);
	if (on)
		val |= ZX_I2S_PROCESS_RX_EN | ZX_I2S_PROCESS_I2S_EN;
	else
		val &= ~(ZX_I2S_PROCESS_RX_EN | ZX_I2S_PROCESS_I2S_EN);
	writel_relaxed(val, base + ZX_I2S_PROCESS_CTRL);
}

static void zx_i2s_tx_dma_en(void __iomem *base, bool on)
{
	unsigned long val;

	val = readl_relaxed(base + ZX_I2S_FIFO_CTRL);
	val |= ZX_I2S_FIFO_CTRL_TX_RST | (I2S_DEAGULT_FIFO_THRES << 8);
	if (on)
		val |= ZX_I2S_FIFO_CTRL_TX_DMA_EN;
	else
		val &= ~ZX_I2S_FIFO_CTRL_TX_DMA_EN;
	writel_relaxed(val, base + ZX_I2S_FIFO_CTRL);
}

static void zx_i2s_rx_dma_en(void __iomem *base, bool on)
{
	unsigned long val;

	val = readl_relaxed(base + ZX_I2S_FIFO_CTRL);
	val |= ZX_I2S_FIFO_CTRL_RX_RST | (I2S_DEAGULT_FIFO_THRES << 16);
	if (on)
		val |= ZX_I2S_FIFO_CTRL_RX_DMA_EN;
	else
		val &= ~ZX_I2S_FIFO_CTRL_RX_DMA_EN;
	writel_relaxed(val, base + ZX_I2S_FIFO_CTRL);
}

#define ZX_I2S_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	 SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000| \
	 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

#define ZX_I2S_FMTBIT \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)

static int zx_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct zx_i2s_info *zx_i2s = dev_get_drvdata(dai->dev);

	snd_soc_dai_set_drvdata(dai, zx_i2s);
	zx_i2s->dma_playback.addr = zx_i2s->mapbase + ZX_I2S_DATA;
	zx_i2s->dma_playback.maxburst = 16;
	zx_i2s->dma_capture.addr = zx_i2s->mapbase + ZX_I2S_DATA;
	zx_i2s->dma_capture.maxburst = 16;
	snd_soc_dai_init_dma_data(dai, &zx_i2s->dma_playback,
				  &zx_i2s->dma_capture);
	return 0;
}

static int zx_i2s_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct zx_i2s_info *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long val;

	val = readl_relaxed(i2s->reg_base + ZX_I2S_TIMING_CTRL);
	val &= ~(ZX_I2S_TIMING_TIMING_MASK | ZX_I2S_TIMING_ALIGN_MASK |
			ZX_I2S_TIMING_TEAK_MASK | ZX_I2S_TIMING_SYNC_MASK |
			ZX_I2S_TIMING_MS_MASK);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		val |= (ZX_I2S_TIMING_I2S | ZX_I2S_TIMING_STD_I2S);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val |= (ZX_I2S_TIMING_I2S | ZX_I2S_TIMING_MSB_JUSTIF);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val |= (ZX_I2S_TIMING_I2S | ZX_I2S_TIMING_LSB_JUSTIF);
		break;
	default:
		dev_err(cpu_dai->dev, "Unknown i2s timing\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		/* Codec is master, and I2S is slave. */
		i2s->master = 0;
		val |= ZX_I2S_TIMING_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		/* Codec is slave, and I2S is master. */
		i2s->master = 1;
		val |= ZX_I2S_TIMING_MAST;
		break;
	default:
		dev_err(cpu_dai->dev, "Unknown master/slave format\n");
		return -EINVAL;
	}

	writel_relaxed(val, i2s->reg_base + ZX_I2S_TIMING_CTRL);
	return 0;
}

static int zx_i2s_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *socdai)
{
	struct zx_i2s_info *i2s = snd_soc_dai_get_drvdata(socdai);
	struct snd_dmaengine_dai_dma_data *dma_data;
	unsigned int lane, ch_num, len, ret = 0;
	unsigned int ts_width = 32;
	unsigned long val;
	unsigned long chn_cfg;

	dma_data = snd_soc_dai_get_dma_data(socdai, substream);
	dma_data->addr_width = ts_width >> 3;

	val = readl_relaxed(i2s->reg_base + ZX_I2S_TIMING_CTRL);
	val &= ~(ZX_I2S_TIMING_TS_WIDTH_MASK | ZX_I2S_TIMING_DATA_SIZE_MASK |
		ZX_I2S_TIMING_LANE_MASK | ZX_I2S_TIMING_CHN_MASK |
		ZX_I2S_TIMING_TSCFG_MASK);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		len = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		len = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		len = 32;
		break;
	default:
		dev_err(socdai->dev, "Unknown data format\n");
		return -EINVAL;
	}
	val |= ZX_I2S_TIMING_TS_WIDTH(ts_width) | ZX_I2S_TIMING_DATA_SIZE(len);

	ch_num = params_channels(params);
	switch (ch_num) {
	case 1:
		lane = 1;
		chn_cfg = 2;
		break;
	case 2:
	case 4:
	case 6:
	case 8:
		lane = ch_num / 2;
		chn_cfg = 3;
		break;
	default:
		dev_err(socdai->dev, "Not support channel num %d\n", ch_num);
		return -EINVAL;
	}
	val |= ZX_I2S_TIMING_LANE(lane);
	val |= ZX_I2S_TIMING_TSCFG(chn_cfg);
	val |= ZX_I2S_TIMING_CHN(ch_num);
	writel_relaxed(val, i2s->reg_base + ZX_I2S_TIMING_CTRL);

	if (i2s->master)
		ret = clk_set_rate(i2s->dai_wclk,
				params_rate(params) * ch_num * CLK_RAT);

	return ret;
}

static int zx_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			  struct snd_soc_dai *dai)
{
	struct zx_i2s_info *zx_i2s = dev_get_drvdata(dai->dev);
	int capture = (substream->stream == SNDRV_PCM_STREAM_CAPTURE);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (capture)
			zx_i2s_rx_dma_en(zx_i2s->reg_base, true);
		else
			zx_i2s_tx_dma_en(zx_i2s->reg_base, true);
		fallthrough;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (capture)
			zx_i2s_rx_en(zx_i2s->reg_base, true);
		else
			zx_i2s_tx_en(zx_i2s->reg_base, true);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		if (capture)
			zx_i2s_rx_dma_en(zx_i2s->reg_base, false);
		else
			zx_i2s_tx_dma_en(zx_i2s->reg_base, false);
		fallthrough;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (capture)
			zx_i2s_rx_en(zx_i2s->reg_base, false);
		else
			zx_i2s_tx_en(zx_i2s->reg_base, false);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int zx_i2s_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct zx_i2s_info *zx_i2s = dev_get_drvdata(dai->dev);
	int ret;

	ret = clk_prepare_enable(zx_i2s->dai_wclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(zx_i2s->dai_pclk);
	if (ret) {
		clk_disable_unprepare(zx_i2s->dai_wclk);
		return ret;
	}

	return ret;
}

static void zx_i2s_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct zx_i2s_info *zx_i2s = dev_get_drvdata(dai->dev);

	clk_disable_unprepare(zx_i2s->dai_wclk);
	clk_disable_unprepare(zx_i2s->dai_pclk);
}

static const struct snd_soc_dai_ops zx_i2s_dai_ops = {
	.trigger	= zx_i2s_trigger,
	.hw_params	= zx_i2s_hw_params,
	.set_fmt	= zx_i2s_set_fmt,
	.startup	= zx_i2s_startup,
	.shutdown	= zx_i2s_shutdown,
};

static const struct snd_soc_component_driver zx_i2s_component = {
	.name			= "zx-i2s",
};

static struct snd_soc_dai_driver zx_i2s_dai = {
	.name	= "zx-i2s-dai",
	.id	= 0,
	.probe	= zx_i2s_dai_probe,
	.playback   = {
		.channels_min	= 1,
		.channels_max	= 8,
		.rates		= ZX_I2S_RATES,
		.formats	= ZX_I2S_FMTBIT,
	},
	.capture = {
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= ZX_I2S_RATES,
		.formats	= ZX_I2S_FMTBIT,
	},
	.ops	= &zx_i2s_dai_ops,
};

static int zx_i2s_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct zx_i2s_info *zx_i2s;
	int ret;

	zx_i2s = devm_kzalloc(&pdev->dev, sizeof(*zx_i2s), GFP_KERNEL);
	if (!zx_i2s)
		return -ENOMEM;

	zx_i2s->dai_wclk = devm_clk_get(&pdev->dev, "wclk");
	if (IS_ERR(zx_i2s->dai_wclk)) {
		dev_err(&pdev->dev, "Fail to get wclk\n");
		return PTR_ERR(zx_i2s->dai_wclk);
	}

	zx_i2s->dai_pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(zx_i2s->dai_pclk)) {
		dev_err(&pdev->dev, "Fail to get pclk\n");
		return PTR_ERR(zx_i2s->dai_pclk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	zx_i2s->mapbase = res->start;
	zx_i2s->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(zx_i2s->reg_base)) {
		dev_err(&pdev->dev, "ioremap failed!\n");
		return PTR_ERR(zx_i2s->reg_base);
	}

	writel_relaxed(0, zx_i2s->reg_base + ZX_I2S_FIFO_CTRL);
	platform_set_drvdata(pdev, zx_i2s);

	ret = devm_snd_soc_register_component(&pdev->dev, &zx_i2s_component,
					      &zx_i2s_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register DAI failed: %d\n", ret);
		return ret;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret)
		dev_err(&pdev->dev, "Register platform PCM failed: %d\n", ret);

	return ret;
}

static const struct of_device_id zx_i2s_dt_ids[] = {
	{ .compatible = "zte,zx296702-i2s", },
	{}
};
MODULE_DEVICE_TABLE(of, zx_i2s_dt_ids);

static struct platform_driver i2s_driver = {
	.probe = zx_i2s_probe,
	.driver = {
		.name = "zx-i2s",
		.of_match_table = zx_i2s_dt_ids,
	},
};

module_platform_driver(i2s_driver);

MODULE_AUTHOR("Jun Nie <jun.nie@linaro.org>");
MODULE_DESCRIPTION("ZTE I2S SoC DAI");
MODULE_LICENSE("GPL");

/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/clk.h>
#include <linux/delay.h>

#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>

#include "jz4740-i2s.h"

#define JZ4740_DMA_TYPE_AIC_TRANSMIT 24
#define JZ4740_DMA_TYPE_AIC_RECEIVE 25

#define JZ_REG_AIC_CONF		0x00
#define JZ_REG_AIC_CTRL		0x04
#define JZ_REG_AIC_I2S_FMT	0x10
#define JZ_REG_AIC_FIFO_STATUS	0x14
#define JZ_REG_AIC_I2S_STATUS	0x1c
#define JZ_REG_AIC_CLK_DIV	0x30
#define JZ_REG_AIC_FIFO		0x34

#define JZ_AIC_CONF_FIFO_RX_THRESHOLD_MASK (0xf << 12)
#define JZ_AIC_CONF_FIFO_TX_THRESHOLD_MASK (0xf <<  8)
#define JZ_AIC_CONF_OVERFLOW_PLAY_LAST BIT(6)
#define JZ_AIC_CONF_INTERNAL_CODEC BIT(5)
#define JZ_AIC_CONF_I2S BIT(4)
#define JZ_AIC_CONF_RESET BIT(3)
#define JZ_AIC_CONF_BIT_CLK_MASTER BIT(2)
#define JZ_AIC_CONF_SYNC_CLK_MASTER BIT(1)
#define JZ_AIC_CONF_ENABLE BIT(0)

#define JZ_AIC_CONF_FIFO_RX_THRESHOLD_OFFSET 12
#define JZ_AIC_CONF_FIFO_TX_THRESHOLD_OFFSET 8
#define JZ4780_AIC_CONF_FIFO_RX_THRESHOLD_OFFSET 24
#define JZ4780_AIC_CONF_FIFO_TX_THRESHOLD_OFFSET 16
#define JZ4780_AIC_CONF_FIFO_RX_THRESHOLD_MASK \
			(0xf << JZ4780_AIC_CONF_FIFO_RX_THRESHOLD_OFFSET)
#define JZ4780_AIC_CONF_FIFO_TX_THRESHOLD_MASK \
			(0x1f <<  JZ4780_AIC_CONF_FIFO_TX_THRESHOLD_OFFSET)

#define JZ_AIC_CTRL_OUTPUT_SAMPLE_SIZE_MASK (0x7 << 19)
#define JZ_AIC_CTRL_INPUT_SAMPLE_SIZE_MASK (0x7 << 16)
#define JZ_AIC_CTRL_ENABLE_RX_DMA BIT(15)
#define JZ_AIC_CTRL_ENABLE_TX_DMA BIT(14)
#define JZ_AIC_CTRL_MONO_TO_STEREO BIT(11)
#define JZ_AIC_CTRL_SWITCH_ENDIANNESS BIT(10)
#define JZ_AIC_CTRL_SIGNED_TO_UNSIGNED BIT(9)
#define JZ_AIC_CTRL_FLUSH		BIT(8)
#define JZ_AIC_CTRL_ENABLE_ROR_INT BIT(6)
#define JZ_AIC_CTRL_ENABLE_TUR_INT BIT(5)
#define JZ_AIC_CTRL_ENABLE_RFS_INT BIT(4)
#define JZ_AIC_CTRL_ENABLE_TFS_INT BIT(3)
#define JZ_AIC_CTRL_ENABLE_LOOPBACK BIT(2)
#define JZ_AIC_CTRL_ENABLE_PLAYBACK BIT(1)
#define JZ_AIC_CTRL_ENABLE_CAPTURE BIT(0)

#define JZ_AIC_CTRL_OUTPUT_SAMPLE_SIZE_OFFSET 19
#define JZ_AIC_CTRL_INPUT_SAMPLE_SIZE_OFFSET  16

#define JZ_AIC_I2S_FMT_DISABLE_BIT_CLK BIT(12)
#define JZ_AIC_I2S_FMT_DISABLE_BIT_ICLK BIT(13)
#define JZ_AIC_I2S_FMT_ENABLE_SYS_CLK BIT(4)
#define JZ_AIC_I2S_FMT_MSB BIT(0)

#define JZ_AIC_I2S_STATUS_BUSY BIT(2)

#define JZ_AIC_CLK_DIV_MASK 0xf
#define I2SDIV_DV_SHIFT 8
#define I2SDIV_DV_MASK (0xf << I2SDIV_DV_SHIFT)
#define I2SDIV_IDV_SHIFT 8
#define I2SDIV_IDV_MASK (0xf << I2SDIV_IDV_SHIFT)

enum jz47xx_i2s_version {
	JZ_I2S_JZ4740,
	JZ_I2S_JZ4780,
};

struct jz4740_i2s {
	struct resource *mem;
	void __iomem *base;
	dma_addr_t phys_base;

	struct clk *clk_aic;
	struct clk *clk_i2s;

	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;

	enum jz47xx_i2s_version version;
};

static inline uint32_t jz4740_i2s_read(const struct jz4740_i2s *i2s,
	unsigned int reg)
{
	return readl(i2s->base + reg);
}

static inline void jz4740_i2s_write(const struct jz4740_i2s *i2s,
	unsigned int reg, uint32_t value)
{
	writel(value, i2s->base + reg);
}

static int jz4740_i2s_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t conf, ctrl;

	if (dai->active)
		return 0;

	ctrl = jz4740_i2s_read(i2s, JZ_REG_AIC_CTRL);
	ctrl |= JZ_AIC_CTRL_FLUSH;
	jz4740_i2s_write(i2s, JZ_REG_AIC_CTRL, ctrl);

	clk_prepare_enable(i2s->clk_i2s);

	conf = jz4740_i2s_read(i2s, JZ_REG_AIC_CONF);
	conf |= JZ_AIC_CONF_ENABLE;
	jz4740_i2s_write(i2s, JZ_REG_AIC_CONF, conf);

	return 0;
}

static void jz4740_i2s_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t conf;

	if (dai->active)
		return;

	conf = jz4740_i2s_read(i2s, JZ_REG_AIC_CONF);
	conf &= ~JZ_AIC_CONF_ENABLE;
	jz4740_i2s_write(i2s, JZ_REG_AIC_CONF, conf);

	clk_disable_unprepare(i2s->clk_i2s);
}

static int jz4740_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	uint32_t ctrl;
	uint32_t mask;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		mask = JZ_AIC_CTRL_ENABLE_PLAYBACK | JZ_AIC_CTRL_ENABLE_TX_DMA;
	else
		mask = JZ_AIC_CTRL_ENABLE_CAPTURE | JZ_AIC_CTRL_ENABLE_RX_DMA;

	ctrl = jz4740_i2s_read(i2s, JZ_REG_AIC_CTRL);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ctrl |= mask;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ctrl &= ~mask;
		break;
	default:
		return -EINVAL;
	}

	jz4740_i2s_write(i2s, JZ_REG_AIC_CTRL, ctrl);

	return 0;
}

static int jz4740_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	uint32_t format = 0;
	uint32_t conf;

	conf = jz4740_i2s_read(i2s, JZ_REG_AIC_CONF);

	conf &= ~(JZ_AIC_CONF_BIT_CLK_MASTER | JZ_AIC_CONF_SYNC_CLK_MASTER);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		conf |= JZ_AIC_CONF_BIT_CLK_MASTER | JZ_AIC_CONF_SYNC_CLK_MASTER;
		format |= JZ_AIC_I2S_FMT_ENABLE_SYS_CLK;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		conf |= JZ_AIC_CONF_SYNC_CLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		conf |= JZ_AIC_CONF_BIT_CLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_MSB:
		format |= JZ_AIC_I2S_FMT_MSB;
		break;
	case SND_SOC_DAIFMT_I2S:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	jz4740_i2s_write(i2s, JZ_REG_AIC_CONF, conf);
	jz4740_i2s_write(i2s, JZ_REG_AIC_I2S_FMT, format);

	return 0;
}

static int jz4740_i2s_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int sample_size;
	uint32_t ctrl, div_reg;
	int div;

	ctrl = jz4740_i2s_read(i2s, JZ_REG_AIC_CTRL);

	div_reg = jz4740_i2s_read(i2s, JZ_REG_AIC_CLK_DIV);
	div = clk_get_rate(i2s->clk_i2s) / (64 * params_rate(params));

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		sample_size = 0;
		break;
	case SNDRV_PCM_FORMAT_S16:
		sample_size = 1;
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ctrl &= ~JZ_AIC_CTRL_OUTPUT_SAMPLE_SIZE_MASK;
		ctrl |= sample_size << JZ_AIC_CTRL_OUTPUT_SAMPLE_SIZE_OFFSET;
		if (params_channels(params) == 1)
			ctrl |= JZ_AIC_CTRL_MONO_TO_STEREO;
		else
			ctrl &= ~JZ_AIC_CTRL_MONO_TO_STEREO;

		div_reg &= ~I2SDIV_DV_MASK;
		div_reg |= (div - 1) << I2SDIV_DV_SHIFT;
	} else {
		ctrl &= ~JZ_AIC_CTRL_INPUT_SAMPLE_SIZE_MASK;
		ctrl |= sample_size << JZ_AIC_CTRL_INPUT_SAMPLE_SIZE_OFFSET;

		if (i2s->version >= JZ_I2S_JZ4780) {
			div_reg &= ~I2SDIV_IDV_MASK;
			div_reg |= (div - 1) << I2SDIV_IDV_SHIFT;
		} else {
			div_reg &= ~I2SDIV_DV_MASK;
			div_reg |= (div - 1) << I2SDIV_DV_SHIFT;
		}
	}

	jz4740_i2s_write(i2s, JZ_REG_AIC_CTRL, ctrl);
	jz4740_i2s_write(i2s, JZ_REG_AIC_CLK_DIV, div_reg);

	return 0;
}

static int jz4740_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
	unsigned int freq, int dir)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	struct clk *parent;
	int ret = 0;

	switch (clk_id) {
	case JZ4740_I2S_CLKSRC_EXT:
		parent = clk_get(NULL, "ext");
		clk_set_parent(i2s->clk_i2s, parent);
		break;
	case JZ4740_I2S_CLKSRC_PLL:
		parent = clk_get(NULL, "pll half");
		clk_set_parent(i2s->clk_i2s, parent);
		ret = clk_set_rate(i2s->clk_i2s, freq);
		break;
	default:
		return -EINVAL;
	}
	clk_put(parent);

	return ret;
}

static int jz4740_i2s_suspend(struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t conf;

	if (dai->active) {
		conf = jz4740_i2s_read(i2s, JZ_REG_AIC_CONF);
		conf &= ~JZ_AIC_CONF_ENABLE;
		jz4740_i2s_write(i2s, JZ_REG_AIC_CONF, conf);

		clk_disable_unprepare(i2s->clk_i2s);
	}

	clk_disable_unprepare(i2s->clk_aic);

	return 0;
}

static int jz4740_i2s_resume(struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t conf;

	clk_prepare_enable(i2s->clk_aic);

	if (dai->active) {
		clk_prepare_enable(i2s->clk_i2s);

		conf = jz4740_i2s_read(i2s, JZ_REG_AIC_CONF);
		conf |= JZ_AIC_CONF_ENABLE;
		jz4740_i2s_write(i2s, JZ_REG_AIC_CONF, conf);
	}

	return 0;
}

static void jz4740_i2c_init_pcm_config(struct jz4740_i2s *i2s)
{
	struct snd_dmaengine_dai_dma_data *dma_data;

	/* Playback */
	dma_data = &i2s->playback_dma_data;
	dma_data->maxburst = 16;
	dma_data->slave_id = JZ4740_DMA_TYPE_AIC_TRANSMIT;
	dma_data->addr = i2s->phys_base + JZ_REG_AIC_FIFO;

	/* Capture */
	dma_data = &i2s->capture_dma_data;
	dma_data->maxburst = 16;
	dma_data->slave_id = JZ4740_DMA_TYPE_AIC_RECEIVE;
	dma_data->addr = i2s->phys_base + JZ_REG_AIC_FIFO;
}

static int jz4740_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t conf;

	clk_prepare_enable(i2s->clk_aic);

	jz4740_i2c_init_pcm_config(i2s);
	snd_soc_dai_init_dma_data(dai, &i2s->playback_dma_data,
		&i2s->capture_dma_data);

	if (i2s->version >= JZ_I2S_JZ4780) {
		conf = (7 << JZ4780_AIC_CONF_FIFO_RX_THRESHOLD_OFFSET) |
			(8 << JZ4780_AIC_CONF_FIFO_TX_THRESHOLD_OFFSET) |
			JZ_AIC_CONF_OVERFLOW_PLAY_LAST |
			JZ_AIC_CONF_I2S |
			JZ_AIC_CONF_INTERNAL_CODEC;
	} else {
		conf = (7 << JZ_AIC_CONF_FIFO_RX_THRESHOLD_OFFSET) |
			(8 << JZ_AIC_CONF_FIFO_TX_THRESHOLD_OFFSET) |
			JZ_AIC_CONF_OVERFLOW_PLAY_LAST |
			JZ_AIC_CONF_I2S |
			JZ_AIC_CONF_INTERNAL_CODEC;
	}

	jz4740_i2s_write(i2s, JZ_REG_AIC_CONF, JZ_AIC_CONF_RESET);
	jz4740_i2s_write(i2s, JZ_REG_AIC_CONF, conf);

	return 0;
}

static int jz4740_i2s_dai_remove(struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	clk_disable_unprepare(i2s->clk_aic);
	return 0;
}

static const struct snd_soc_dai_ops jz4740_i2s_dai_ops = {
	.startup = jz4740_i2s_startup,
	.shutdown = jz4740_i2s_shutdown,
	.trigger = jz4740_i2s_trigger,
	.hw_params = jz4740_i2s_hw_params,
	.set_fmt = jz4740_i2s_set_fmt,
	.set_sysclk = jz4740_i2s_set_sysclk,
};

#define JZ4740_I2S_FMTS (SNDRV_PCM_FMTBIT_S8 | \
		SNDRV_PCM_FMTBIT_S16_LE)

static struct snd_soc_dai_driver jz4740_i2s_dai = {
	.probe = jz4740_i2s_dai_probe,
	.remove = jz4740_i2s_dai_remove,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = JZ4740_I2S_FMTS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = JZ4740_I2S_FMTS,
	},
	.symmetric_rates = 1,
	.ops = &jz4740_i2s_dai_ops,
	.suspend = jz4740_i2s_suspend,
	.resume = jz4740_i2s_resume,
};

static struct snd_soc_dai_driver jz4780_i2s_dai = {
	.probe = jz4740_i2s_dai_probe,
	.remove = jz4740_i2s_dai_remove,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = JZ4740_I2S_FMTS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = JZ4740_I2S_FMTS,
	},
	.ops = &jz4740_i2s_dai_ops,
	.suspend = jz4740_i2s_suspend,
	.resume = jz4740_i2s_resume,
};

static const struct snd_soc_component_driver jz4740_i2s_component = {
	.name		= "jz4740-i2s",
};

#ifdef CONFIG_OF
static const struct of_device_id jz4740_of_matches[] = {
	{ .compatible = "ingenic,jz4740-i2s", .data = (void *)JZ_I2S_JZ4740 },
	{ .compatible = "ingenic,jz4780-i2s", .data = (void *)JZ_I2S_JZ4780 },
	{ /* sentinel */ }
};
#endif

static int jz4740_i2s_dev_probe(struct platform_device *pdev)
{
	struct jz4740_i2s *i2s;
	struct resource *mem;
	int ret;
	const struct of_device_id *match;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	match = of_match_device(jz4740_of_matches, &pdev->dev);
	if (match)
		i2s->version = (enum jz47xx_i2s_version)match->data;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2s->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(i2s->base))
		return PTR_ERR(i2s->base);

	i2s->phys_base = mem->start;

	i2s->clk_aic = devm_clk_get(&pdev->dev, "aic");
	if (IS_ERR(i2s->clk_aic))
		return PTR_ERR(i2s->clk_aic);

	i2s->clk_i2s = devm_clk_get(&pdev->dev, "i2s");
	if (IS_ERR(i2s->clk_i2s))
		return PTR_ERR(i2s->clk_i2s);

	platform_set_drvdata(pdev, i2s);

	if (i2s->version == JZ_I2S_JZ4780)
		ret = devm_snd_soc_register_component(&pdev->dev,
			&jz4740_i2s_component, &jz4780_i2s_dai, 1);
	else
		ret = devm_snd_soc_register_component(&pdev->dev,
			&jz4740_i2s_component, &jz4740_i2s_dai, 1);

	if (ret)
		return ret;

	return devm_snd_dmaengine_pcm_register(&pdev->dev, NULL,
		SND_DMAENGINE_PCM_FLAG_COMPAT);
}

static struct platform_driver jz4740_i2s_driver = {
	.probe = jz4740_i2s_dev_probe,
	.driver = {
		.name = "jz4740-i2s",
		.of_match_table = of_match_ptr(jz4740_of_matches)
	},
};

module_platform_driver(jz4740_i2s_driver);

MODULE_AUTHOR("Lars-Peter Clausen, <lars@metafoo.de>");
MODULE_DESCRIPTION("Ingenic JZ4740 SoC I2S driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jz4740-i2s");

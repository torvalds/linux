// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>

#define JZ_REG_AIC_CONF		0x00
#define JZ_REG_AIC_CTRL		0x04
#define JZ_REG_AIC_I2S_FMT	0x10
#define JZ_REG_AIC_FIFO_STATUS	0x14
#define JZ_REG_AIC_I2S_STATUS	0x1c
#define JZ_REG_AIC_CLK_DIV	0x30
#define JZ_REG_AIC_FIFO		0x34

#define JZ_AIC_CONF_OVERFLOW_PLAY_LAST	BIT(6)
#define JZ_AIC_CONF_INTERNAL_CODEC	BIT(5)
#define JZ_AIC_CONF_I2S			BIT(4)
#define JZ_AIC_CONF_RESET		BIT(3)
#define JZ_AIC_CONF_BIT_CLK_MASTER	BIT(2)
#define JZ_AIC_CONF_SYNC_CLK_MASTER	BIT(1)
#define JZ_AIC_CONF_ENABLE		BIT(0)

#define JZ_AIC_CTRL_OUTPUT_SAMPLE_SIZE	GENMASK(21, 19)
#define JZ_AIC_CTRL_INPUT_SAMPLE_SIZE	GENMASK(18, 16)
#define JZ_AIC_CTRL_ENABLE_RX_DMA	BIT(15)
#define JZ_AIC_CTRL_ENABLE_TX_DMA	BIT(14)
#define JZ_AIC_CTRL_MONO_TO_STEREO	BIT(11)
#define JZ_AIC_CTRL_SWITCH_ENDIANNESS	BIT(10)
#define JZ_AIC_CTRL_SIGNED_TO_UNSIGNED	BIT(9)
#define JZ_AIC_CTRL_TFLUSH		BIT(8)
#define JZ_AIC_CTRL_RFLUSH		BIT(7)
#define JZ_AIC_CTRL_ENABLE_ROR_INT	BIT(6)
#define JZ_AIC_CTRL_ENABLE_TUR_INT	BIT(5)
#define JZ_AIC_CTRL_ENABLE_RFS_INT	BIT(4)
#define JZ_AIC_CTRL_ENABLE_TFS_INT	BIT(3)
#define JZ_AIC_CTRL_ENABLE_LOOPBACK	BIT(2)
#define JZ_AIC_CTRL_ENABLE_PLAYBACK	BIT(1)
#define JZ_AIC_CTRL_ENABLE_CAPTURE	BIT(0)

#define JZ_AIC_I2S_FMT_DISABLE_BIT_CLK	BIT(12)
#define JZ_AIC_I2S_FMT_DISABLE_BIT_ICLK	BIT(13)
#define JZ_AIC_I2S_FMT_ENABLE_SYS_CLK	BIT(4)
#define JZ_AIC_I2S_FMT_MSB		BIT(0)

#define JZ_AIC_I2S_STATUS_BUSY		BIT(2)

struct i2s_soc_info {
	struct snd_soc_dai_driver *dai;

	struct reg_field field_rx_fifo_thresh;
	struct reg_field field_tx_fifo_thresh;
	struct reg_field field_i2sdiv_capture;
	struct reg_field field_i2sdiv_playback;

	bool shared_fifo_flush;
};

struct jz4740_i2s {
	struct regmap *regmap;

	struct regmap_field *field_rx_fifo_thresh;
	struct regmap_field *field_tx_fifo_thresh;
	struct regmap_field *field_i2sdiv_capture;
	struct regmap_field *field_i2sdiv_playback;

	struct clk *clk_aic;
	struct clk *clk_i2s;

	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;

	const struct i2s_soc_info *soc_info;
};

static int jz4740_i2s_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	int ret;

	/*
	 * When we can flush FIFOs independently, only flush the FIFO
	 * that is starting up. We can do this when the DAI is active
	 * because it does not disturb other active substreams.
	 */
	if (!i2s->soc_info->shared_fifo_flush) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_set_bits(i2s->regmap, JZ_REG_AIC_CTRL, JZ_AIC_CTRL_TFLUSH);
		else
			regmap_set_bits(i2s->regmap, JZ_REG_AIC_CTRL, JZ_AIC_CTRL_RFLUSH);
	}

	if (snd_soc_dai_active(dai))
		return 0;

	/*
	 * When there is a shared flush bit for both FIFOs, the TFLUSH
	 * bit flushes both FIFOs. Flushing while the DAI is active would
	 * cause FIFO underruns in other active substreams so we have to
	 * guard this behind the snd_soc_dai_active() check.
	 */
	if (i2s->soc_info->shared_fifo_flush)
		regmap_set_bits(i2s->regmap, JZ_REG_AIC_CTRL, JZ_AIC_CTRL_TFLUSH);

	ret = clk_prepare_enable(i2s->clk_i2s);
	if (ret)
		return ret;

	regmap_set_bits(i2s->regmap, JZ_REG_AIC_CONF, JZ_AIC_CONF_ENABLE);
	return 0;
}

static void jz4740_i2s_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	if (snd_soc_dai_active(dai))
		return;

	regmap_clear_bits(i2s->regmap, JZ_REG_AIC_CONF, JZ_AIC_CONF_ENABLE);

	clk_disable_unprepare(i2s->clk_i2s);
}

static int jz4740_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t mask;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		mask = JZ_AIC_CTRL_ENABLE_PLAYBACK | JZ_AIC_CTRL_ENABLE_TX_DMA;
	else
		mask = JZ_AIC_CTRL_ENABLE_CAPTURE | JZ_AIC_CTRL_ENABLE_RX_DMA;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		regmap_set_bits(i2s->regmap, JZ_REG_AIC_CTRL, mask);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		regmap_clear_bits(i2s->regmap, JZ_REG_AIC_CTRL, mask);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int jz4740_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	const unsigned int conf_mask = JZ_AIC_CONF_BIT_CLK_MASTER |
				       JZ_AIC_CONF_SYNC_CLK_MASTER;
	unsigned int conf = 0, format = 0;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		conf |= JZ_AIC_CONF_BIT_CLK_MASTER | JZ_AIC_CONF_SYNC_CLK_MASTER;
		format |= JZ_AIC_I2S_FMT_ENABLE_SYS_CLK;
		break;
	case SND_SOC_DAIFMT_BC_FP:
		conf |= JZ_AIC_CONF_SYNC_CLK_MASTER;
		break;
	case SND_SOC_DAIFMT_BP_FC:
		conf |= JZ_AIC_CONF_BIT_CLK_MASTER;
		break;
	case SND_SOC_DAIFMT_BC_FC:
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

	regmap_update_bits(i2s->regmap, JZ_REG_AIC_CONF, conf_mask, conf);
	regmap_write(i2s->regmap, JZ_REG_AIC_I2S_FMT, format);

	return 0;
}

static int jz4740_i2s_get_i2sdiv(unsigned long mclk, unsigned long rate,
				 unsigned long i2sdiv_max)
{
	unsigned long div, rate1, rate2, err1, err2;

	div = mclk / (64 * rate);
	if (div == 0)
		div = 1;

	rate1 = mclk / (64 * div);
	rate2 = mclk / (64 * (div + 1));

	err1 = abs(rate1 - rate);
	err2 = abs(rate2 - rate);

	/*
	 * Choose the divider that produces the smallest error in the
	 * output rate and reject dividers with a 5% or higher error.
	 * In the event that both dividers are outside the acceptable
	 * error margin, reject the rate to prevent distorted audio.
	 * (The number 5% is arbitrary.)
	 */
	if (div <= i2sdiv_max && err1 <= err2 && err1 < rate/20)
		return div;
	if (div < i2sdiv_max && err2 < rate/20)
		return div + 1;

	return -EINVAL;
}

static int jz4740_i2s_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	struct regmap_field *div_field;
	unsigned long i2sdiv_max;
	unsigned int sample_size;
	uint32_t ctrl, conf;
	int div = 1;

	regmap_read(i2s->regmap, JZ_REG_AIC_CTRL, &ctrl);
	regmap_read(i2s->regmap, JZ_REG_AIC_CONF, &conf);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		sample_size = 0;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		sample_size = 1;
		break;
	case SNDRV_PCM_FORMAT_S20_LE:
		sample_size = 3;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		sample_size = 4;
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ctrl &= ~JZ_AIC_CTRL_OUTPUT_SAMPLE_SIZE;
		ctrl |= FIELD_PREP(JZ_AIC_CTRL_OUTPUT_SAMPLE_SIZE, sample_size);

		if (params_channels(params) == 1)
			ctrl |= JZ_AIC_CTRL_MONO_TO_STEREO;
		else
			ctrl &= ~JZ_AIC_CTRL_MONO_TO_STEREO;

		div_field = i2s->field_i2sdiv_playback;
		i2sdiv_max = GENMASK(i2s->soc_info->field_i2sdiv_playback.msb,
				     i2s->soc_info->field_i2sdiv_playback.lsb);
	} else {
		ctrl &= ~JZ_AIC_CTRL_INPUT_SAMPLE_SIZE;
		ctrl |= FIELD_PREP(JZ_AIC_CTRL_INPUT_SAMPLE_SIZE, sample_size);

		div_field = i2s->field_i2sdiv_capture;
		i2sdiv_max = GENMASK(i2s->soc_info->field_i2sdiv_capture.msb,
				     i2s->soc_info->field_i2sdiv_capture.lsb);
	}

	/*
	 * Only calculate I2SDIV if we're supplying the bit or frame clock.
	 * If the codec is supplying both clocks then the divider output is
	 * unused, and we don't want it to limit the allowed sample rates.
	 */
	if (conf & (JZ_AIC_CONF_BIT_CLK_MASTER | JZ_AIC_CONF_SYNC_CLK_MASTER)) {
		div = jz4740_i2s_get_i2sdiv(clk_get_rate(i2s->clk_i2s),
					    params_rate(params), i2sdiv_max);
		if (div < 0)
			return div;
	}

	regmap_write(i2s->regmap, JZ_REG_AIC_CTRL, ctrl);
	regmap_field_write(div_field, div - 1);

	return 0;
}

static int jz4740_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct jz4740_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &i2s->playback_dma_data,
		&i2s->capture_dma_data);

	return 0;
}

static const struct snd_soc_dai_ops jz4740_i2s_dai_ops = {
	.startup = jz4740_i2s_startup,
	.shutdown = jz4740_i2s_shutdown,
	.trigger = jz4740_i2s_trigger,
	.hw_params = jz4740_i2s_hw_params,
	.set_fmt = jz4740_i2s_set_fmt,
};

#define JZ4740_I2S_FMTS (SNDRV_PCM_FMTBIT_S8 | \
			 SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S20_LE | \
			 SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver jz4740_i2s_dai = {
	.probe = jz4740_i2s_dai_probe,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = JZ4740_I2S_FMTS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = JZ4740_I2S_FMTS,
	},
	.symmetric_rate = 1,
	.ops = &jz4740_i2s_dai_ops,
};

static const struct i2s_soc_info jz4740_i2s_soc_info = {
	.dai			= &jz4740_i2s_dai,
	.field_rx_fifo_thresh	= REG_FIELD(JZ_REG_AIC_CONF, 12, 15),
	.field_tx_fifo_thresh	= REG_FIELD(JZ_REG_AIC_CONF, 8, 11),
	.field_i2sdiv_capture	= REG_FIELD(JZ_REG_AIC_CLK_DIV, 0, 3),
	.field_i2sdiv_playback	= REG_FIELD(JZ_REG_AIC_CLK_DIV, 0, 3),
	.shared_fifo_flush	= true,
};

static const struct i2s_soc_info jz4760_i2s_soc_info = {
	.dai			= &jz4740_i2s_dai,
	.field_rx_fifo_thresh	= REG_FIELD(JZ_REG_AIC_CONF, 24, 27),
	.field_tx_fifo_thresh	= REG_FIELD(JZ_REG_AIC_CONF, 16, 20),
	.field_i2sdiv_capture	= REG_FIELD(JZ_REG_AIC_CLK_DIV, 0, 3),
	.field_i2sdiv_playback	= REG_FIELD(JZ_REG_AIC_CLK_DIV, 0, 3),
};

static struct snd_soc_dai_driver jz4770_i2s_dai = {
	.probe = jz4740_i2s_dai_probe,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = JZ4740_I2S_FMTS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = JZ4740_I2S_FMTS,
	},
	.ops = &jz4740_i2s_dai_ops,
};

static const struct i2s_soc_info jz4770_i2s_soc_info = {
	.dai			= &jz4770_i2s_dai,
	.field_rx_fifo_thresh	= REG_FIELD(JZ_REG_AIC_CONF, 24, 27),
	.field_tx_fifo_thresh	= REG_FIELD(JZ_REG_AIC_CONF, 16, 20),
	.field_i2sdiv_capture	= REG_FIELD(JZ_REG_AIC_CLK_DIV, 8, 11),
	.field_i2sdiv_playback	= REG_FIELD(JZ_REG_AIC_CLK_DIV, 0, 3),
};

static const struct i2s_soc_info jz4780_i2s_soc_info = {
	.dai			= &jz4770_i2s_dai,
	.field_rx_fifo_thresh	= REG_FIELD(JZ_REG_AIC_CONF, 24, 27),
	.field_tx_fifo_thresh	= REG_FIELD(JZ_REG_AIC_CONF, 16, 20),
	.field_i2sdiv_capture	= REG_FIELD(JZ_REG_AIC_CLK_DIV, 8, 11),
	.field_i2sdiv_playback	= REG_FIELD(JZ_REG_AIC_CLK_DIV, 0, 3),
};

static int jz4740_i2s_suspend(struct snd_soc_component *component)
{
	struct jz4740_i2s *i2s = snd_soc_component_get_drvdata(component);

	if (snd_soc_component_active(component)) {
		regmap_clear_bits(i2s->regmap, JZ_REG_AIC_CONF, JZ_AIC_CONF_ENABLE);
		clk_disable_unprepare(i2s->clk_i2s);
	}

	clk_disable_unprepare(i2s->clk_aic);

	return 0;
}

static int jz4740_i2s_resume(struct snd_soc_component *component)
{
	struct jz4740_i2s *i2s = snd_soc_component_get_drvdata(component);
	int ret;

	ret = clk_prepare_enable(i2s->clk_aic);
	if (ret)
		return ret;

	if (snd_soc_component_active(component)) {
		ret = clk_prepare_enable(i2s->clk_i2s);
		if (ret) {
			clk_disable_unprepare(i2s->clk_aic);
			return ret;
		}

		regmap_set_bits(i2s->regmap, JZ_REG_AIC_CONF, JZ_AIC_CONF_ENABLE);
	}

	return 0;
}

static int jz4740_i2s_probe(struct snd_soc_component *component)
{
	struct jz4740_i2s *i2s = snd_soc_component_get_drvdata(component);
	int ret;

	ret = clk_prepare_enable(i2s->clk_aic);
	if (ret)
		return ret;

	regmap_write(i2s->regmap, JZ_REG_AIC_CONF, JZ_AIC_CONF_RESET);

	regmap_write(i2s->regmap, JZ_REG_AIC_CONF,
		     JZ_AIC_CONF_OVERFLOW_PLAY_LAST |
		     JZ_AIC_CONF_I2S | JZ_AIC_CONF_INTERNAL_CODEC);

	regmap_field_write(i2s->field_rx_fifo_thresh, 7);
	regmap_field_write(i2s->field_tx_fifo_thresh, 8);

	return 0;
}

static void jz4740_i2s_remove(struct snd_soc_component *component)
{
	struct jz4740_i2s *i2s = snd_soc_component_get_drvdata(component);

	clk_disable_unprepare(i2s->clk_aic);
}

static const struct snd_soc_component_driver jz4740_i2s_component = {
	.name			= "jz4740-i2s",
	.probe			= jz4740_i2s_probe,
	.remove			= jz4740_i2s_remove,
	.suspend		= jz4740_i2s_suspend,
	.resume			= jz4740_i2s_resume,
	.legacy_dai_naming	= 1,
};

static const struct of_device_id jz4740_of_matches[] = {
	{ .compatible = "ingenic,jz4740-i2s", .data = &jz4740_i2s_soc_info },
	{ .compatible = "ingenic,jz4760-i2s", .data = &jz4760_i2s_soc_info },
	{ .compatible = "ingenic,jz4770-i2s", .data = &jz4770_i2s_soc_info },
	{ .compatible = "ingenic,jz4780-i2s", .data = &jz4780_i2s_soc_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jz4740_of_matches);

static int jz4740_i2s_init_regmap_fields(struct device *dev,
					 struct jz4740_i2s *i2s)
{
	i2s->field_rx_fifo_thresh =
		devm_regmap_field_alloc(dev, i2s->regmap,
					i2s->soc_info->field_rx_fifo_thresh);
	if (IS_ERR(i2s->field_rx_fifo_thresh))
		return PTR_ERR(i2s->field_rx_fifo_thresh);

	i2s->field_tx_fifo_thresh =
		devm_regmap_field_alloc(dev, i2s->regmap,
					i2s->soc_info->field_tx_fifo_thresh);
	if (IS_ERR(i2s->field_tx_fifo_thresh))
		return PTR_ERR(i2s->field_tx_fifo_thresh);

	i2s->field_i2sdiv_capture =
		devm_regmap_field_alloc(dev, i2s->regmap,
					i2s->soc_info->field_i2sdiv_capture);
	if (IS_ERR(i2s->field_i2sdiv_capture))
		return PTR_ERR(i2s->field_i2sdiv_capture);

	i2s->field_i2sdiv_playback =
		devm_regmap_field_alloc(dev, i2s->regmap,
					i2s->soc_info->field_i2sdiv_playback);
	if (IS_ERR(i2s->field_i2sdiv_playback))
		return PTR_ERR(i2s->field_i2sdiv_playback);

	return 0;
}

static const struct regmap_config jz4740_i2s_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= JZ_REG_AIC_FIFO,
};

static int jz4740_i2s_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct jz4740_i2s *i2s;
	struct resource *mem;
	void __iomem *regs;
	int ret;

	i2s = devm_kzalloc(dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->soc_info = device_get_match_data(dev);

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	i2s->playback_dma_data.maxburst = 16;
	i2s->playback_dma_data.addr = mem->start + JZ_REG_AIC_FIFO;

	i2s->capture_dma_data.maxburst = 16;
	i2s->capture_dma_data.addr = mem->start + JZ_REG_AIC_FIFO;

	i2s->clk_aic = devm_clk_get(dev, "aic");
	if (IS_ERR(i2s->clk_aic))
		return PTR_ERR(i2s->clk_aic);

	i2s->clk_i2s = devm_clk_get(dev, "i2s");
	if (IS_ERR(i2s->clk_i2s))
		return PTR_ERR(i2s->clk_i2s);

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &jz4740_i2s_regmap_config);
	if (IS_ERR(i2s->regmap))
		return PTR_ERR(i2s->regmap);

	ret = jz4740_i2s_init_regmap_fields(dev, i2s);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, i2s);

	ret = devm_snd_soc_register_component(dev, &jz4740_i2s_component,
					      i2s->soc_info->dai, 1);
	if (ret)
		return ret;

	return devm_snd_dmaengine_pcm_register(dev, NULL,
		SND_DMAENGINE_PCM_FLAG_COMPAT);
}

static struct platform_driver jz4740_i2s_driver = {
	.probe = jz4740_i2s_dev_probe,
	.driver = {
		.name = "jz4740-i2s",
		.of_match_table = jz4740_of_matches,
	},
};

module_platform_driver(jz4740_i2s_driver);

MODULE_AUTHOR("Lars-Peter Clausen, <lars@metafoo.de>");
MODULE_DESCRIPTION("Ingenic JZ4740 SoC I2S driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jz4740-i2s");

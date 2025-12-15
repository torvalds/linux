// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Troy Mitchell <troy.mitchell@linux.spacemit.com> */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#define SSCR			0x00	/* SPI/I2S top control register */
#define SSFCR			0x04	/* SPI/I2S FIFO control register */
#define SSINTEN			0x08	/* SPI/I2S interrupt enable register */
#define SSDATR			0x10	/* SPI/I2S data register */
#define SSPSP			0x18	/* SPI/I2S programmable serial protocol control register */
#define SSRWT			0x24	/* SPI/I2S root control register */

/* SPI/I2S Work data size, register bits value 0~31 indicated data size 1~32 bits */
#define SSCR_FIELD_DSS		GENMASK(9, 5)
#define SSCR_DW_8BYTE		FIELD_PREP(SSCR_FIELD_DSS, 0x7)
#define SSCR_DW_16BYTE		FIELD_PREP(SSCR_FIELD_DSS, 0xf)
#define SSCR_DW_18BYTE		FIELD_PREP(SSCR_FIELD_DSS, 0x11)
#define SSCR_DW_32BYTE		FIELD_PREP(SSCR_FIELD_DSS, 0x1f)

#define SSCR_SSE		BIT(0)		/* SPI/I2S Enable */
#define SSCR_FRF_PSP		GENMASK(2, 1)	/* Frame Format*/
#define SSCR_TRAIL		BIT(13)		/* Trailing Byte */

#define SSFCR_FIELD_TFT		GENMASK(3, 0)   /* TXFIFO Trigger Threshold */
#define SSFCR_FIELD_RFT		GENMASK(8, 5)   /* RXFIFO Trigger Threshold */
#define SSFCR_TSRE		BIT(10)		/* Transmit Service Request Enable */
#define SSFCR_RSRE		BIT(11)		/* Receive Service Request Enable */

#define SSPSP_FSRT		BIT(3)		/* Frame Sync Relative Timing Bit */
#define SSPSP_SFRMP		BIT(4)		/* Serial Frame Polarity */
#define SSPSP_FIELD_SFRMWDTH	GENMASK(17, 12)	/* Serial Frame Width field  */

#define SSRWT_RWOT		BIT(0)		/* Receive Without Transmit */

#define SPACEMIT_PCM_RATES	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | \
				SNDRV_PCM_RATE_48000)
#define SPACEMIT_PCM_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE)

#define SPACEMIT_I2S_PERIOD_SIZE 1024

struct spacemit_i2s_dev {
	struct device *dev;

	void __iomem *base;

	struct reset_control *reset;

	struct clk *sysclk;
	struct clk *bclk;
	struct clk *sspa_clk;

	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;

	bool has_capture;
	bool has_playback;

	int dai_fmt;

	int started_count;
};

static const struct snd_pcm_hardware spacemit_pcm_hardware = {
	.info		  = SNDRV_PCM_INFO_INTERLEAVED |
			    SNDRV_PCM_INFO_BATCH,
	.formats          = SPACEMIT_PCM_FORMATS,
	.rates		  = SPACEMIT_PCM_RATES,
	.rate_min         = SNDRV_PCM_RATE_8000,
	.rate_max         = SNDRV_PCM_RATE_192000,
	.channels_min     = 1,
	.channels_max     = 2,
	.buffer_bytes_max = SPACEMIT_I2S_PERIOD_SIZE * 4 * 4,
	.period_bytes_min = SPACEMIT_I2S_PERIOD_SIZE * 2,
	.period_bytes_max = SPACEMIT_I2S_PERIOD_SIZE * 4,
	.periods_min	  = 2,
	.periods_max	  = 4,
};

static const struct snd_dmaengine_pcm_config spacemit_dmaengine_pcm_config = {
	.pcm_hardware = &spacemit_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.chan_names = {"tx", "rx"},
	.prealloc_buffer_size = 32 * 1024,
};

static void spacemit_i2s_init(struct spacemit_i2s_dev *i2s)
{
	u32 sscr_val, sspsp_val, ssfcr_val, ssrwt_val;

	sscr_val = SSCR_TRAIL | SSCR_FRF_PSP;
	ssfcr_val = FIELD_PREP(SSFCR_FIELD_TFT, 5) |
		    FIELD_PREP(SSFCR_FIELD_RFT, 5) |
		    SSFCR_RSRE | SSFCR_TSRE;
	ssrwt_val = SSRWT_RWOT;
	sspsp_val = SSPSP_SFRMP;

	writel(sscr_val, i2s->base + SSCR);
	writel(ssfcr_val, i2s->base + SSFCR);
	writel(sspsp_val, i2s->base + SSPSP);
	writel(ssrwt_val, i2s->base + SSRWT);
	writel(0, i2s->base + SSINTEN);
}

static int spacemit_i2s_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);
	struct snd_dmaengine_dai_dma_data *dma_data;
	u32 data_width, data_bits;
	unsigned long bclk_rate;
	u32 val;
	int ret;

	val = readl(i2s->base + SSCR);
	if (val & SSCR_SSE)
		return 0;

	dma_data = &i2s->playback_dma_data;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		dma_data = &i2s->capture_dma_data;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		data_bits = 8;
		data_width = SSCR_DW_8BYTE;
		dma_data->maxburst = 8;
		dma_data->addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		data_bits = 16;
		data_width = SSCR_DW_16BYTE;
		dma_data->maxburst = 16;
		dma_data->addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		data_bits = 32;
		data_width = SSCR_DW_32BYTE;
		dma_data->maxburst = 32;
		dma_data->addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	default:
		dev_dbg(i2s->dev, "unexpected data width type");
		return -EINVAL;
	}

	switch (i2s->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		if (data_bits == 16) {
			data_width = SSCR_DW_32BYTE;
			dma_data->maxburst = 32;
			dma_data->addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		}

		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_CHANNELS,
					     1, 2);
		snd_pcm_hw_constraint_mask64(substream->runtime,
					     SNDRV_PCM_HW_PARAM_FORMAT,
					     SNDRV_PCM_FMTBIT_S16_LE);
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_CHANNELS,
					     1, 1);
		snd_pcm_hw_constraint_mask64(substream->runtime,
					     SNDRV_PCM_HW_PARAM_FORMAT,
					     SNDRV_PCM_FMTBIT_S32_LE);
		break;
	default:
		dev_dbg(i2s->dev, "unexpected format type");
		return -EINVAL;

	}

	val = readl(i2s->base + SSCR);
	val &= ~SSCR_DW_32BYTE;
	val |= data_width;
	writel(val, i2s->base + SSCR);

	bclk_rate = params_channels(params) *
		    params_rate(params) *
		    data_bits;

	ret = clk_set_rate(i2s->bclk, bclk_rate);
	if (ret)
		return ret;

	return clk_set_rate(i2s->sspa_clk, bclk_rate);
}

static int spacemit_i2s_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
				   unsigned int freq, int dir)
{
	struct spacemit_i2s_dev *i2s = dev_get_drvdata(cpu_dai->dev);

	if (freq == 0)
		return 0;

	return clk_set_rate(i2s->sysclk, freq);
}

static int spacemit_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
				unsigned int fmt)
{
	struct spacemit_i2s_dev *i2s = dev_get_drvdata(cpu_dai->dev);
	u32 sspsp_val;

	sspsp_val = readl(i2s->base + SSPSP);
	sspsp_val &= ~SSPSP_FIELD_SFRMWDTH;
	sspsp_val |= SSPSP_FSRT;

	i2s->dai_fmt = fmt;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		sspsp_val |= FIELD_PREP(SSPSP_FIELD_SFRMWDTH, 0x10);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		/* DSP_B: next frame asserted after previous frame end, so clear FSRT */
		sspsp_val &= ~SSPSP_FSRT;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_A:
		sspsp_val |= FIELD_PREP(SSPSP_FIELD_SFRMWDTH, 0x1);
		break;
	default:
		dev_dbg(i2s->dev, "unexpected format type");
		return -EINVAL;
	}

	writel(sspsp_val, i2s->base + SSPSP);

	return 0;
}

static int spacemit_i2s_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);
	u32 val;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!i2s->started_count) {
			val = readl(i2s->base + SSCR);
			val |= SSCR_SSE;
			writel(val, i2s->base + SSCR);
		}
		i2s->started_count++;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (i2s->started_count)
			i2s->started_count--;

		if (!i2s->started_count) {
			val = readl(i2s->base + SSCR);
			val &= ~SSCR_SSE;
			writel(val, i2s->base + SSCR);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int spacemit_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai,
				  i2s->has_playback ? &i2s->playback_dma_data : NULL,
				  i2s->has_capture ? &i2s->capture_dma_data : NULL);

	reset_control_deassert(i2s->reset);

	spacemit_i2s_init(i2s);

	return 0;
}

static int spacemit_i2s_dai_remove(struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);

	reset_control_assert(i2s->reset);

	return 0;
}

static const struct snd_soc_dai_ops spacemit_i2s_dai_ops = {
	.probe = spacemit_i2s_dai_probe,
	.remove = spacemit_i2s_dai_remove,
	.hw_params = spacemit_i2s_hw_params,
	.set_sysclk = spacemit_i2s_set_sysclk,
	.set_fmt = spacemit_i2s_set_fmt,
	.trigger = spacemit_i2s_trigger,
};

static struct snd_soc_dai_driver spacemit_i2s_dai = {
	.ops = &spacemit_i2s_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SPACEMIT_PCM_RATES,
		.rate_min = SNDRV_PCM_RATE_8000,
		.rate_max = SNDRV_PCM_RATE_48000,
		.formats = SPACEMIT_PCM_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SPACEMIT_PCM_RATES,
		.rate_min = SNDRV_PCM_RATE_8000,
		.rate_max = SNDRV_PCM_RATE_48000,
		.formats = SPACEMIT_PCM_FORMATS,
	},
	.symmetric_rate = 1,
};

static int spacemit_i2s_init_dai(struct spacemit_i2s_dev *i2s,
				 struct snd_soc_dai_driver **dp,
				 dma_addr_t addr)
{
	struct device_node *node = i2s->dev->of_node;
	struct snd_soc_dai_driver *dai;
	struct property *dma_names;
	const char *dma_name;

	of_property_for_each_string(node, "dma-names", dma_names, dma_name) {
		if (!strcmp(dma_name, "tx"))
			i2s->has_playback = true;
		if (!strcmp(dma_name, "rx"))
			i2s->has_capture = true;
	}

	dai = devm_kmemdup(i2s->dev, &spacemit_i2s_dai,
			   sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	if (i2s->has_playback) {
		dai->playback.stream_name = "Playback";
		dai->playback.channels_min = 1;
		dai->playback.channels_max = 2;
		dai->playback.rates = SPACEMIT_PCM_RATES;
		dai->playback.formats = SPACEMIT_PCM_FORMATS;

		i2s->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		i2s->playback_dma_data.maxburst = 32;
		i2s->playback_dma_data.addr = addr;
	}

	if (i2s->has_capture) {
		dai->capture.stream_name = "Capture";
		dai->capture.channels_min = 1;
		dai->capture.channels_max = 2;
		dai->capture.rates = SPACEMIT_PCM_RATES;
		dai->capture.formats = SPACEMIT_PCM_FORMATS;

		i2s->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		i2s->capture_dma_data.maxburst = 32;
		i2s->capture_dma_data.addr = addr;
	}

	if (dp)
		*dp = dai;

	return 0;
}

static const struct snd_soc_component_driver spacemit_i2s_component = {
	.name = "i2s-k1",
	.legacy_dai_naming = 1,
};

static int spacemit_i2s_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_driver *dai;
	struct spacemit_i2s_dev *i2s;
	struct resource *res;
	struct clk *clk;
	int ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->dev = &pdev->dev;

	i2s->sysclk = devm_clk_get_enabled(i2s->dev, "sysclk");
	if (IS_ERR(i2s->sysclk))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->sysclk),
				     "failed to enable sysbase clock\n");

	i2s->bclk = devm_clk_get_enabled(i2s->dev, "bclk");
	if (IS_ERR(i2s->bclk))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->bclk), "failed to enable bit clock\n");

	clk = devm_clk_get_enabled(i2s->dev, "sspa_bus");
	if (IS_ERR(clk))
		return dev_err_probe(i2s->dev, PTR_ERR(clk), "failed to enable sspa_bus clock\n");

	i2s->sspa_clk = devm_clk_get_enabled(i2s->dev, "sspa");
	if (IS_ERR(i2s->sspa_clk))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->sspa_clk),
				     "failed to enable sspa clock\n");

	i2s->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(i2s->base))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->base), "failed to map registers\n");

	i2s->reset = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(i2s->reset))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->reset),
				     "failed to get reset control");

	dev_set_drvdata(i2s->dev, i2s);

	ret = spacemit_i2s_init_dai(i2s, &dai, res->start + SSDATR);
	if (ret)
		return ret;

	ret = devm_snd_soc_register_component(i2s->dev,
					      &spacemit_i2s_component,
					      dai, 1);
	if (ret)
		return dev_err_probe(i2s->dev, ret, "failed to register component");

	return devm_snd_dmaengine_pcm_register(&pdev->dev, &spacemit_dmaengine_pcm_config, 0);
}

static const struct of_device_id spacemit_i2s_of_match[] = {
	{ .compatible = "spacemit,k1-i2s", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_i2s_of_match);

static struct platform_driver spacemit_i2s_driver = {
	.probe = spacemit_i2s_probe,
	.driver = {
		.name = "i2s-k1",
		.of_match_table = spacemit_i2s_of_match,
	},
};
module_platform_driver(spacemit_i2s_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2S bus driver for SpacemiT K1 SoC");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AC97 Controller Driver for Loongson-1 SoC
 *
 * Copyright (C) 2025 Keguang Zhang <keguang.zhang@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

/* Loongson-1 AC97 Controller Registers */
#define AC97_CSR		0x0
#define AC97_OCC0		0x4
#define AC97_ICC		0x10
#define AC97_CRAC		0x18
#define AC97_INTRAW		0x54
#define AC97_INTM		0x58
#define AC97_INT_CW_CLR		0x68
#define AC97_INT_CR_CLR		0x6c

/* Control Status Register Bits (CSR) */
#define CSR_RESUME		BIT(1)
#define CSR_RST_FORCE		BIT(0)

/* MIC Channel Configuration Bits */
#define M_DMA_EN		BIT(22)
#define M_FIFO_THRES		GENMASK(21, 20)
#define M_FIFO_THRES_FULL	FIELD_PREP(M_FIFO_THRES, 3)
#define M_FIFO_THRES_HALF	FIELD_PREP(M_FIFO_THRES, 1)
#define M_FIFO_THRES_QUARTER	FIELD_PREP(M_FIFO_THRES, 0)
#define M_SW			GENMASK(19, 18)
#define M_SW_16_BITS		FIELD_PREP(M_SW, 2)
#define M_SW_8_BITS		FIELD_PREP(M_SW, 0)
#define M_VSR			BIT(17)
#define M_CH_EN			BIT(16)
/* Right Channel Configuration Bits */
#define R_DMA_EN		BIT(14)
#define R_FIFO_THRES		GENMASK(13, 12)
#define R_FIFO_THRES_EMPTY	FIELD_PREP(R_FIFO_THRES, 3)
#define R_FIFO_THRES_HALF	FIELD_PREP(R_FIFO_THRES, 1)
#define R_FIFO_THRES_QUARTER	FIELD_PREP(R_FIFO_THRES, 0)
#define R_SW			GENMASK(11, 10)
#define R_SW_16_BITS		FIELD_PREP(R_SW, 2)
#define R_SW_8_BITS		FIELD_PREP(R_SW, 0)
#define R_VSR			BIT(9)
#define R_CH_EN			BIT(8)
/* Left Channel Configuration Bits */
#define L_DMA_EN		BIT(6)
#define L_FIFO_THRES		GENMASK(5, 4)
#define L_FIFO_THRES_EMPTY	FIELD_PREP(L_FIFO_THRES, 3)
#define L_FIFO_THRES_HALF	FIELD_PREP(L_FIFO_THRES, 1)
#define L_FIFO_THRES_QUARTER	FIELD_PREP(L_FIFO_THRES, 0)
#define L_SW			GENMASK(3, 2)
#define L_SW_16_BITS		FIELD_PREP(L_SW, 2)
#define L_SW_8_BITS		FIELD_PREP(L_SW, 0)
#define L_VSR			BIT(1)
#define L_CH_EN			BIT(0)

/* Codec Register Access Command Bits (CRAC) */
#define CODEC_WR		BIT(31)
#define CODEC_ADR		GENMASK(22, 16)
#define CODEC_DAT		GENMASK(15, 0)

/* Interrupt Register (INTRAW) */
#define CW_DONE			BIT(1)
#define CR_DONE			BIT(0)

#define LS1X_AC97_DMA_TX_EN		BIT(31)
#define LS1X_AC97_DMA_STEREO		BIT(30)
#define LS1X_AC97_DMA_TX_BYTES		GENMASK(29, 28)
#define LS1X_AC97_DMA_TX_4_BYTES	FIELD_PREP(LS1X_AC97_DMA_TX_BYTES, 2)
#define LS1X_AC97_DMA_TX_2_BYTES	FIELD_PREP(LS1X_AC97_DMA_TX_BYTES, 1)
#define LS1X_AC97_DMA_TX_1_BYTE		FIELD_PREP(LS1X_AC97_DMA_TX_BYTES, 0)
#define LS1X_AC97_DMA_DADDR_MASK	GENMASK(27, 0)

#define LS1X_AC97_DMA_FIFO_SIZE		128

#define LS1X_AC97_TIMEOUT		3000

struct ls1x_ac97 {
	void __iomem *reg_base;
	struct regmap *regmap;
	dma_addr_t tx_dma_base;
	dma_addr_t rx_dma_base;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
};

static struct ls1x_ac97 *ls1x_ac97;

static const struct regmap_config ls1x_ac97_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static void ls1x_ac97_reset(struct snd_ac97 *ac97)
{
	int val;

	regmap_write(ls1x_ac97->regmap, AC97_CSR, CSR_RST_FORCE);
	regmap_read_poll_timeout(ls1x_ac97->regmap, AC97_CSR, val,
				 !(val & CSR_RESUME), 0, LS1X_AC97_TIMEOUT);
}

static void ls1x_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short val)
{
	int tmp, ret;

	tmp = FIELD_PREP(CODEC_ADR, reg) | FIELD_PREP(CODEC_DAT, val);
	regmap_write(ls1x_ac97->regmap, AC97_CRAC, tmp);
	ret = regmap_read_poll_timeout(ls1x_ac97->regmap, AC97_INTRAW, tmp,
				       (tmp & CW_DONE), 0, LS1X_AC97_TIMEOUT);
	if (ret)
		pr_err("timeout on AC97 write! %d\n", ret);

	regmap_read(ls1x_ac97->regmap, AC97_INT_CW_CLR, &ret);
}

static unsigned short ls1x_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	int val, ret;

	val = CODEC_WR | FIELD_PREP(CODEC_ADR, reg);
	regmap_write(ls1x_ac97->regmap, AC97_CRAC, val);
	ret = regmap_read_poll_timeout(ls1x_ac97->regmap, AC97_INTRAW, val,
				       (val & CR_DONE), 0, LS1X_AC97_TIMEOUT);
	if (ret) {
		pr_err("timeout on AC97 read! %d\n", ret);
		return ret;
	}

	regmap_read(ls1x_ac97->regmap, AC97_INT_CR_CLR, &ret);
	regmap_read(ls1x_ac97->regmap, AC97_CRAC, &ret);

	return (ret & CODEC_DAT);
}

static void ls1x_ac97_init(struct snd_ac97 *ac97)
{
	writel(0, ls1x_ac97->reg_base + AC97_INTRAW);
	writel(0, ls1x_ac97->reg_base + AC97_INTM);

	/* Config output channels */
	regmap_update_bits(ls1x_ac97->regmap, AC97_OCC0,
			   R_DMA_EN | R_FIFO_THRES | R_CH_EN |
			   L_DMA_EN | L_FIFO_THRES | L_CH_EN,
			   R_DMA_EN | R_FIFO_THRES_EMPTY | R_CH_EN |
			   L_DMA_EN | L_FIFO_THRES_EMPTY | L_CH_EN);

	/* Config inputs channel */
	regmap_update_bits(ls1x_ac97->regmap, AC97_ICC,
			   M_DMA_EN | M_FIFO_THRES | M_CH_EN |
			   R_DMA_EN | R_FIFO_THRES | R_CH_EN |
			   L_DMA_EN | L_FIFO_THRES | L_CH_EN,
			   M_DMA_EN | M_FIFO_THRES_FULL | M_CH_EN |
			   R_DMA_EN | R_FIFO_THRES_EMPTY | R_CH_EN |
			   L_DMA_EN | L_FIFO_THRES_EMPTY | L_CH_EN);

	if (ac97->ext_id & AC97_EI_VRA) {
		regmap_update_bits(ls1x_ac97->regmap, AC97_OCC0, R_VSR | L_VSR, R_VSR | L_VSR);
		regmap_update_bits(ls1x_ac97->regmap, AC97_ICC, M_VSR, M_VSR);
	}
}

static struct snd_ac97_bus_ops ls1x_ac97_ops = {
	.reset	= ls1x_ac97_reset,
	.write	= ls1x_ac97_write,
	.read	= ls1x_ac97_read,
	.init	= ls1x_ac97_init,
};

static int ls1x_ac97_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *cpu_dai)
{
	struct ls1x_ac97 *ac97 = dev_get_drvdata(cpu_dai->dev);
	struct snd_dmaengine_dai_dma_data *dma_data = snd_soc_dai_get_dma_data(cpu_dai, substream);

	switch (params_channels(params)) {
	case 1:
		dma_data->addr &= ~LS1X_AC97_DMA_STEREO;
		break;
	case 2:
		dma_data->addr |= LS1X_AC97_DMA_STEREO;
		break;
	default:
		dev_err(cpu_dai->dev, "unsupported channels! %d\n", params_channels(params));
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
	case SNDRV_PCM_FORMAT_U8:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(ac97->regmap, AC97_OCC0,
					   R_SW | L_SW,
					   R_SW_8_BITS | L_SW_8_BITS);
		else
			regmap_update_bits(ac97->regmap, AC97_ICC,
					   M_SW | R_SW | L_SW,
					   M_SW_8_BITS | R_SW_8_BITS | L_SW_8_BITS);
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_U16_BE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(ac97->regmap, AC97_OCC0,
					   R_SW | L_SW,
					   R_SW_16_BITS | L_SW_16_BITS);
		else
			regmap_update_bits(ac97->regmap, AC97_ICC,
					   M_SW | R_SW | L_SW,
					   M_SW_16_BITS | R_SW_16_BITS | L_SW_16_BITS);
		break;
	default:
		dev_err(cpu_dai->dev, "unsupported format! %d\n", params_format(params));
		return -EINVAL;
	}

	return 0;
}

static int ls1x_ac97_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct ls1x_ac97 *ac97 = dev_get_drvdata(cpu_dai->dev);

	ac97->capture_dma_data.addr = ac97->rx_dma_base & LS1X_AC97_DMA_DADDR_MASK;
	ac97->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	ac97->capture_dma_data.fifo_size = LS1X_AC97_DMA_FIFO_SIZE;

	ac97->playback_dma_data.addr = ac97->tx_dma_base & LS1X_AC97_DMA_DADDR_MASK;
	ac97->playback_dma_data.addr |= LS1X_AC97_DMA_TX_4_BYTES;
	ac97->playback_dma_data.addr |= LS1X_AC97_DMA_TX_EN;
	ac97->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	ac97->playback_dma_data.fifo_size = LS1X_AC97_DMA_FIFO_SIZE;

	snd_soc_dai_init_dma_data(cpu_dai, &ac97->playback_dma_data, &ac97->capture_dma_data);
	snd_soc_dai_set_drvdata(cpu_dai, ac97);

	return 0;
}

static const struct snd_soc_dai_ops ls1x_ac97_dai_ops = {
	.probe		= ls1x_ac97_dai_probe,
	.hw_params	= ls1x_ac97_hw_params,
};

#define LS1X_AC97_FMTS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |\
	SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |\
	SNDRV_PCM_FMTBIT_U16_LE	| SNDRV_PCM_FMTBIT_U16_BE)

static struct snd_soc_dai_driver ls1x_ac97_dai[] = {
	{
		.name = "ls1x-ac97",
		.playback = {
			.stream_name = "AC97 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = LS1X_AC97_FMTS,
		},
		.capture = {
			.stream_name = "AC97 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = LS1X_AC97_FMTS,
		},
		.ops = &ls1x_ac97_dai_ops,
	},
};

static const struct snd_soc_component_driver ls1x_ac97_component = {
	.name = KBUILD_MODNAME,
	.legacy_dai_naming = 1,
};

static int ls1x_ac97_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ls1x_ac97 *ac97;
	struct resource *res;
	int ret;

	ac97 = devm_kzalloc(dev, sizeof(struct ls1x_ac97), GFP_KERNEL);
	if (!ac97)
		return -ENOMEM;
	ls1x_ac97 = ac97;
	platform_set_drvdata(pdev, ac97);

	ac97->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ac97->reg_base))
		return PTR_ERR(ac97->reg_base);

	ac97->regmap = devm_regmap_init_mmio(dev, ac97->reg_base, &ls1x_ac97_regmap_config);
	if (IS_ERR(ac97->regmap))
		return dev_err_probe(dev, PTR_ERR(ac97->regmap), "devm_regmap_init_mmio failed\n");

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "audio-tx");
	if (!res)
		return dev_err_probe(dev, -EINVAL, "Missing 'audio-tx' in reg-names property\n");

	ac97->tx_dma_base = dma_map_resource(dev, res->start, resource_size(res),
					     DMA_TO_DEVICE, 0);
	if (dma_mapping_error(dev, ac97->tx_dma_base))
		return -ENXIO;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "audio-rx");
	if (!res)
		return dev_err_probe(dev, -EINVAL, "Missing 'audio-rx' in reg-names property\n");

	ac97->rx_dma_base = dma_map_resource(dev, res->start, resource_size(res),
					     DMA_FROM_DEVICE, 0);
	if (dma_mapping_error(dev, ac97->rx_dma_base))
		return -ENXIO;

	ret = devm_snd_dmaengine_pcm_register(dev, NULL, 0);
	if (ret)
		dev_err_probe(dev, ret, "failed to register PCM\n");

	ret = devm_snd_soc_register_component(dev, &ls1x_ac97_component,
					      ls1x_ac97_dai, ARRAY_SIZE(ls1x_ac97_dai));
	if (ret)
		dev_err_probe(dev, ret, "failed to register DAI\n");

	return snd_soc_set_ac97_ops(&ls1x_ac97_ops);
}

static void ls1x_ac97_remove(struct platform_device *pdev)
{
	ls1x_ac97 = NULL;
	snd_soc_set_ac97_ops(NULL);
}

#ifdef CONFIG_PM_SLEEP
static int ls1x_ac97_suspend(struct device *dev)
{
	int val;

	regmap_clear_bits(ls1x_ac97->regmap, AC97_OCC0, R_DMA_EN | R_CH_EN | L_DMA_EN | L_CH_EN);
	regmap_clear_bits(ls1x_ac97->regmap, AC97_ICC,
			  M_DMA_EN | M_CH_EN | R_DMA_EN | R_CH_EN | L_DMA_EN | L_CH_EN);
	regmap_set_bits(ls1x_ac97->regmap, AC97_CSR, CSR_RESUME);

	return regmap_read_poll_timeout(ls1x_ac97->regmap, AC97_CSR, val,
					(val & CSR_RESUME), 0, LS1X_AC97_TIMEOUT);
}

static int ls1x_ac97_resume(struct device *dev)
{
	int val;

	regmap_set_bits(ls1x_ac97->regmap, AC97_OCC0, R_DMA_EN | R_CH_EN | L_DMA_EN | L_CH_EN);
	regmap_set_bits(ls1x_ac97->regmap, AC97_ICC,
			M_DMA_EN | M_CH_EN | R_DMA_EN | R_CH_EN | L_DMA_EN | L_CH_EN);
	regmap_set_bits(ls1x_ac97->regmap, AC97_CSR, CSR_RESUME);

	return regmap_read_poll_timeout(ls1x_ac97->regmap, AC97_CSR, val,
					!(val & CSR_RESUME), 0, LS1X_AC97_TIMEOUT);
}
#endif

static const struct dev_pm_ops ls1x_ac97_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ls1x_ac97_suspend, ls1x_ac97_resume)
};

static const struct of_device_id ls1x_ac97_match[] = {
	{ .compatible = "loongson,ls1b-ac97" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ls1x_ac97_match);

static struct platform_driver ls1x_ac97_driver = {
	.probe		= ls1x_ac97_probe,
	.remove		= ls1x_ac97_remove,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table = ls1x_ac97_match,
		.pm = &ls1x_ac97_pm_ops,
	},
};

module_platform_driver(ls1x_ac97_driver);

MODULE_AUTHOR("Keguang Zhang <keguang.zhang@gmail.com>");
MODULE_DESCRIPTION("Loongson-1 AC97 Controller Driver");
MODULE_LICENSE("GPL");

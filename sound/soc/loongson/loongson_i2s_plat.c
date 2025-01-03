// SPDX-License-Identifier: GPL-2.0
//
// Loongson I2S controller master mode dirver(platform device)
//
// Copyright (C) 2023-2024 Loongson Technology Corporation Limited
//
// Author: Yingkun Meng <mengyingkun@loongson.cn>
//         Binbin Zhou <zhoubinbin@loongson.cn>

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "loongson_i2s.h"

#define LOONGSON_I2S_RX_DMA_OFFSET	21
#define LOONGSON_I2S_TX_DMA_OFFSET	18

#define LOONGSON_DMA0_CONF	0x0
#define LOONGSON_DMA1_CONF	0x1
#define LOONGSON_DMA2_CONF	0x2
#define LOONGSON_DMA3_CONF	0x3
#define LOONGSON_DMA4_CONF	0x4

/* periods_max = PAGE_SIZE / sizeof(struct ls_dma_chan_reg) */
static const struct snd_pcm_hardware loongson_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_RESUME |
		SNDRV_PCM_INFO_PAUSE,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S20_3LE |
		   SNDRV_PCM_FMTBIT_S24_LE,
	.period_bytes_min = 128,
	.period_bytes_max = 128 * 1024,
	.periods_min = 1,
	.periods_max = 64,
	.buffer_bytes_max = 1024 * 1024,
};

static const struct snd_dmaengine_pcm_config loongson_dmaengine_pcm_config = {
	.pcm_hardware = &loongson_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.prealloc_buffer_size = 128 * 1024,
};

static int loongson_pcm_open(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (substream->pcm->device & 1) {
		runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
		runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
	}

	if (substream->pcm->device & 2)
		runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP |
				      SNDRV_PCM_INFO_MMAP_VALID);
	/*
	 * For mysterious reasons (and despite what the manual says)
	 * playback samples are lost if the DMA count is not a multiple
	 * of the DMA burst size.  Let's add a rule to enforce that.
	 */
	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 128);
	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 128);
	snd_pcm_hw_constraint_integer(substream->runtime,
				      SNDRV_PCM_HW_PARAM_PERIODS);

	return 0;
}

static const struct snd_soc_component_driver loongson_i2s_component_driver = {
	.name   = LS_I2S_DRVNAME,
	.open	= loongson_pcm_open,
};

static const struct regmap_config loongson_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x14,
	.cache_type = REGCACHE_FLAT,
};

static int loongson_i2s_apbdma_config(struct platform_device *pdev)
{
	int val;
	void __iomem *regs;

	regs = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	val = readl(regs);
	val |= LOONGSON_DMA2_CONF << LOONGSON_I2S_TX_DMA_OFFSET;
	val |= LOONGSON_DMA3_CONF << LOONGSON_I2S_RX_DMA_OFFSET;
	writel(val, regs);

	return 0;
}

static int loongson_i2s_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct loongson_i2s *i2s;
	struct resource *res;
	struct clk *i2s_clk;
	int ret;

	i2s = devm_kzalloc(dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	ret = loongson_i2s_apbdma_config(pdev);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2s->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2s->reg_base))
		return dev_err_probe(dev, PTR_ERR(i2s->reg_base),
				     "devm_ioremap_resource failed\n");

	i2s->regmap = devm_regmap_init_mmio(dev, i2s->reg_base,
					    &loongson_i2s_regmap_config);
	if (IS_ERR(i2s->regmap))
		return dev_err_probe(dev, PTR_ERR(i2s->regmap),
				     "devm_regmap_init_mmio failed\n");

	i2s->playback_dma_data.addr = res->start + LS_I2S_TX_DATA;
	i2s->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->playback_dma_data.maxburst = 4;

	i2s->capture_dma_data.addr = res->start + LS_I2S_RX_DATA;
	i2s->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->capture_dma_data.maxburst = 4;

	i2s_clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(i2s_clk))
		return dev_err_probe(dev, PTR_ERR(i2s_clk), "clock property invalid\n");
	i2s->clk_rate = clk_get_rate(i2s_clk);

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	dev_set_name(dev, LS_I2S_DRVNAME);
	dev_set_drvdata(dev, i2s);

	ret = devm_snd_soc_register_component(dev, &loongson_i2s_component_driver,
					      &loongson_i2s_dai, 1);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register DAI\n");

	return devm_snd_dmaengine_pcm_register(dev, &loongson_dmaengine_pcm_config,
					       SND_DMAENGINE_PCM_FLAG_COMPAT);
}

static const struct of_device_id loongson_i2s_ids[] = {
	{ .compatible = "loongson,ls2k1000-i2s" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, loongson_i2s_ids);

static struct platform_driver loongson_i2s_driver = {
	.probe = loongson_i2s_plat_probe,
	.driver = {
		.name = "loongson-i2s-plat",
		.pm = pm_sleep_ptr(&loongson_i2s_pm),
		.of_match_table = loongson_i2s_ids,
	},
};
module_platform_driver(loongson_i2s_driver);

MODULE_DESCRIPTION("Loongson I2S Master Mode ASoC Driver");
MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_LICENSE("GPL");

/*
 * tegra_i2s.c - Tegra I2S driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010 - NVIDIA, Inc.
 *
 * Based on code copyright/by:
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 * Scott Peterson <speterson@nvidia.com>
 *
 * Copyright (C) 2010 Google, Inc.
 * Iliyan Malchev <malchev@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <mach/iomap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra_i2s.h"

#define DRV_NAME "tegra-i2s"

static inline void tegra_i2s_write(struct tegra_i2s *i2s, u32 reg, u32 val)
{
	__raw_writel(val, i2s->regs + reg);
}

static inline u32 tegra_i2s_read(struct tegra_i2s *i2s, u32 reg)
{
	return __raw_readl(i2s->regs + reg);
}

#ifdef CONFIG_DEBUG_FS
static int tegra_i2s_show(struct seq_file *s, void *unused)
{
#define REG(r) { r, #r }
	static const struct {
		int offset;
		const char *name;
	} regs[] = {
		REG(TEGRA_I2S_CTRL),
		REG(TEGRA_I2S_STATUS),
		REG(TEGRA_I2S_TIMING),
		REG(TEGRA_I2S_FIFO_SCR),
		REG(TEGRA_I2S_PCM_CTRL),
		REG(TEGRA_I2S_NW_CTRL),
		REG(TEGRA_I2S_TDM_CTRL),
		REG(TEGRA_I2S_TDM_TX_RX_CTRL),
	};
#undef REG

	struct tegra_i2s *i2s = s->private;
	int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		u32 val = tegra_i2s_read(i2s, regs[i].offset);
		seq_printf(s, "%s = %08x\n", regs[i].name, val);
	}

	return 0;
}

static int tegra_i2s_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, tegra_i2s_show, inode->i_private);
}

static const struct file_operations tegra_i2s_debug_fops = {
	.open    = tegra_i2s_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

static void tegra_i2s_debug_add(struct tegra_i2s *i2s)
{
	i2s->debug = debugfs_create_file(i2s->dai.name, S_IRUGO,
					 snd_soc_debugfs_root, i2s,
					 &tegra_i2s_debug_fops);
}

static void tegra_i2s_debug_remove(struct tegra_i2s *i2s)
{
	if (i2s->debug)
		debugfs_remove(i2s->debug);
}
#else
static inline void tegra_i2s_debug_add(struct tegra_i2s *i2s, int id)
{
}

static inline void tegra_i2s_debug_remove(struct tegra_i2s *i2s)
{
}
#endif

static int tegra_i2s_set_fmt(struct snd_soc_dai *dai,
				unsigned int fmt)
{
	struct tegra_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	i2s->reg_ctrl &= ~TEGRA_I2S_CTRL_MASTER_ENABLE;
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_MASTER_ENABLE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		return -EINVAL;
	}

	i2s->reg_ctrl &= ~(TEGRA_I2S_CTRL_BIT_FORMAT_MASK | 
				TEGRA_I2S_CTRL_LRCK_MASK);
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_BIT_FORMAT_DSP;
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_LRCK_L_LOW;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_BIT_FORMAT_DSP;
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_LRCK_R_LOW;
		break;
	case SND_SOC_DAIFMT_I2S:
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_BIT_FORMAT_I2S;
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_LRCK_L_LOW;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_BIT_FORMAT_RJM;
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_LRCK_L_LOW;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_BIT_FORMAT_LJM;
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_LRCK_L_LOW;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
        struct device *dev = substream->pcm->card->dev;
	struct tegra_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	u32 reg;
	int ret, sample_size, srate, i2sclock, bitcnt;

	i2s->reg_ctrl &= ~TEGRA_I2S_CTRL_BIT_SIZE_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_BIT_SIZE_16;
		sample_size = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_BIT_SIZE_24;
		sample_size = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		i2s->reg_ctrl |= TEGRA_I2S_CTRL_BIT_SIZE_32;
		sample_size = 32;
		break;
	default:
		return -EINVAL;
	}

	srate = params_rate(params);

	/* Final "* 2" required by Tegra hardware */
	i2sclock = srate * params_channels(params) * sample_size * 2;

	ret = clk_set_rate(i2s->clk_i2s, i2sclock);
	if (ret) {
		dev_err(dev, "Can't set I2S clock rate: %d\n", ret);
		return ret;
	}

	bitcnt = (i2sclock / (2 * srate)) - 1;
	if (bitcnt < 0 || bitcnt > TEGRA_I2S_TIMING_CHANNEL_BIT_COUNT_MASK_US)
		return -EINVAL;
	reg = bitcnt << TEGRA_I2S_TIMING_CHANNEL_BIT_COUNT_SHIFT;

	if (i2sclock % (2 * srate))
		reg |= TEGRA_I2S_TIMING_NON_SYM_ENABLE;

	if (!i2s->clk_refs)
		clk_enable(i2s->clk_i2s);

	tegra_i2s_write(i2s, TEGRA_I2S_TIMING, reg);

	tegra_i2s_write(i2s, TEGRA_I2S_FIFO_SCR,
		TEGRA_I2S_FIFO_SCR_FIFO2_ATN_LVL_FOUR_SLOTS |
		TEGRA_I2S_FIFO_SCR_FIFO1_ATN_LVL_FOUR_SLOTS);

	if (!i2s->clk_refs)
		clk_disable(i2s->clk_i2s);

	return 0;
}

static void tegra_i2s_start_playback(struct tegra_i2s *i2s)
{
	i2s->reg_ctrl |= TEGRA_I2S_CTRL_FIFO1_ENABLE;
	tegra_i2s_write(i2s, TEGRA_I2S_CTRL, i2s->reg_ctrl);
}

static void tegra_i2s_stop_playback(struct tegra_i2s *i2s)
{
	i2s->reg_ctrl &= ~TEGRA_I2S_CTRL_FIFO1_ENABLE;
	tegra_i2s_write(i2s, TEGRA_I2S_CTRL, i2s->reg_ctrl);
}

static void tegra_i2s_start_capture(struct tegra_i2s *i2s)
{
	i2s->reg_ctrl |= TEGRA_I2S_CTRL_FIFO2_ENABLE;
	tegra_i2s_write(i2s, TEGRA_I2S_CTRL, i2s->reg_ctrl);
}

static void tegra_i2s_stop_capture(struct tegra_i2s *i2s)
{
	i2s->reg_ctrl &= ~TEGRA_I2S_CTRL_FIFO2_ENABLE;
	tegra_i2s_write(i2s, TEGRA_I2S_CTRL, i2s->reg_ctrl);
}

static int tegra_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct tegra_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (!i2s->clk_refs)
			clk_enable(i2s->clk_i2s);
		i2s->clk_refs++;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra_i2s_start_playback(i2s);
		else
			tegra_i2s_start_capture(i2s);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra_i2s_stop_playback(i2s);
		else
			tegra_i2s_stop_capture(i2s);
		i2s->clk_refs--;
		if (!i2s->clk_refs)
			clk_disable(i2s->clk_i2s);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra_i2s_probe(struct snd_soc_dai *dai)
{
	struct tegra_i2s * i2s = snd_soc_dai_get_drvdata(dai);

	dai->capture_dma_data = &i2s->capture_dma_data;
	dai->playback_dma_data = &i2s->playback_dma_data;

	return 0;
}

static const struct snd_soc_dai_ops tegra_i2s_dai_ops = {
	.set_fmt	= tegra_i2s_set_fmt,
	.hw_params	= tegra_i2s_hw_params,
	.trigger	= tegra_i2s_trigger,
};

static const struct snd_soc_dai_driver tegra_i2s_dai_template = {
	.probe = tegra_i2s_probe,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tegra_i2s_dai_ops,
	.symmetric_rates = 1,
};

static __devinit int tegra_i2s_platform_probe(struct platform_device *pdev)
{
	struct tegra_i2s * i2s;
	struct resource *mem, *memregion, *dmareq;
	u32 of_dma[2];
	u32 dma_ch;
	int ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(struct tegra_i2s), GFP_KERNEL);
	if (!i2s) {
		dev_err(&pdev->dev, "Can't allocate tegra_i2s\n");
		ret = -ENOMEM;
		goto err;
	}
	dev_set_drvdata(&pdev->dev, i2s);

	i2s->dai = tegra_i2s_dai_template;
	i2s->dai.name = dev_name(&pdev->dev);

	i2s->clk_i2s = clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2s->clk_i2s)) {
		dev_err(&pdev->dev, "Can't retrieve i2s clock\n");
		ret = PTR_ERR(i2s->clk_i2s);
		goto err;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "No memory resource\n");
		ret = -ENODEV;
		goto err_clk_put;
	}

	dmareq = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!dmareq) {
		if (of_property_read_u32_array(pdev->dev.of_node,
					"nvidia,dma-request-selector",
					of_dma, 2) < 0) {
			dev_err(&pdev->dev, "No DMA resource\n");
			ret = -ENODEV;
			goto err_clk_put;
		}
		dma_ch = of_dma[1];
	} else {
		dma_ch = dmareq->start;
	}

	memregion = devm_request_mem_region(&pdev->dev, mem->start,
					    resource_size(mem), DRV_NAME);
	if (!memregion) {
		dev_err(&pdev->dev, "Memory region already claimed\n");
		ret = -EBUSY;
		goto err_clk_put;
	}

	i2s->regs = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!i2s->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_clk_put;
	}

	i2s->capture_dma_data.addr = mem->start + TEGRA_I2S_FIFO2;
	i2s->capture_dma_data.wrap = 4;
	i2s->capture_dma_data.width = 32;
	i2s->capture_dma_data.req_sel = dma_ch;

	i2s->playback_dma_data.addr = mem->start + TEGRA_I2S_FIFO1;
	i2s->playback_dma_data.wrap = 4;
	i2s->playback_dma_data.width = 32;
	i2s->playback_dma_data.req_sel = dma_ch;

	i2s->reg_ctrl = TEGRA_I2S_CTRL_FIFO_FORMAT_PACKED;

	ret = snd_soc_register_dai(&pdev->dev, &i2s->dai);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", ret);
		ret = -ENOMEM;
		goto err_clk_put;
	}

	tegra_i2s_debug_add(i2s);

	return 0;

err_clk_put:
	clk_put(i2s->clk_i2s);
err:
	return ret;
}

static int __devexit tegra_i2s_platform_remove(struct platform_device *pdev)
{
	struct tegra_i2s *i2s = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_dai(&pdev->dev);

	tegra_i2s_debug_remove(i2s);

	clk_put(i2s->clk_i2s);

	return 0;
}

static const struct of_device_id tegra_i2s_of_match[] __devinitconst = {
	{ .compatible = "nvidia,tegra20-i2s", },
	{},
};

static struct platform_driver tegra_i2s_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = tegra_i2s_of_match,
	},
	.probe = tegra_i2s_platform_probe,
	.remove = __devexit_p(tegra_i2s_platform_remove),
};
module_platform_driver(tegra_i2s_driver);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra I2S ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra_i2s_of_match);

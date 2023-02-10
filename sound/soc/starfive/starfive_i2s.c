// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA SoC Synopsys I2S Audio Layer
 *
 * sound/soc/dwc/designware_i2s.c
 *
 * Copyright (C) 2010 ST Microelectronics
 * Rajeev Kumar <rajeevkumar.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <sound/designware_i2s.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include "starfive_i2s.h"

static inline void i2s_write_reg(void __iomem *io_base, int reg, u32 val)
{
	writel(val, io_base + reg);
}

static inline u32 i2s_read_reg(void __iomem *io_base, int reg)
{
	return readl(io_base + reg);
}

static inline void i2s_disable_channels(struct dw_i2s_dev *dev, u32 stream)
{
	u32 i = 0;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < 4; i++)
			i2s_write_reg(dev->i2s_base, TER(i), 0);
	} else {
		for (i = 0; i < 4; i++)
			i2s_write_reg(dev->i2s_base, RER(i), 0);
	}
}

static inline void i2s_clear_irqs(struct dw_i2s_dev *dev, u32 stream)
{
	u32 i = 0;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < 4; i++)
			i2s_read_reg(dev->i2s_base, TOR(i));
	} else {
		for (i = 0; i < 4; i++)
			i2s_read_reg(dev->i2s_base, ROR(i));
	}
}

static inline void i2s_disable_irqs(struct dw_i2s_dev *dev, u32 stream,
				    int chan_nr)
{
	u32 i, irq;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < (chan_nr / 2); i++) {
			irq = i2s_read_reg(dev->i2s_base, IMR(i));
			i2s_write_reg(dev->i2s_base, IMR(i), irq | 0x30);
		}
	} else {
		for (i = 0; i < (chan_nr / 2); i++) {
			irq = i2s_read_reg(dev->i2s_base, IMR(i));
			i2s_write_reg(dev->i2s_base, IMR(i), irq | 0x03);
		}
	}
}

static inline void i2s_enable_irqs(struct dw_i2s_dev *dev, u32 stream,
				   int chan_nr)
{
	u32 i, irq;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < (chan_nr / 2); i++) {
			irq = i2s_read_reg(dev->i2s_base, IMR(i));
			i2s_write_reg(dev->i2s_base, IMR(i), irq & ~0x30);
		}
	} else {
		for (i = 0; i < (chan_nr / 2); i++) {
			irq = i2s_read_reg(dev->i2s_base, IMR(i));
			i2s_write_reg(dev->i2s_base, IMR(i), irq & ~0x03);
		}
	}
}

static irqreturn_t i2s_irq_handler(int irq, void *dev_id)
{
	struct dw_i2s_dev *dev = dev_id;
	bool irq_valid = false;
	u32 isr[4];
	int i;

	for (i = 0; i < 4; i++)
		isr[i] = i2s_read_reg(dev->i2s_base, ISR(i));

	i2s_clear_irqs(dev, SNDRV_PCM_STREAM_PLAYBACK);
	i2s_clear_irqs(dev, SNDRV_PCM_STREAM_CAPTURE);

	for (i = 0; i < 4; i++) {
		/*
		 * Check if TX fifo is empty. If empty fill FIFO with samples
		 * NOTE: Only two channels supported
		 */
		if ((isr[i] & ISR_TXFE) && (i == 0) && dev->use_pio) {
			dw_pcm_push_tx(dev);
			irq_valid = true;
		}

		/*
		 * Data available. Retrieve samples from FIFO
		 * NOTE: Only two channels supported
		 */
		if ((isr[i] & ISR_RXDA) && (i == 0) && dev->use_pio) {
			dw_pcm_pop_rx(dev);
			irq_valid = true;
		}

		/* Error Handling: TX */
		if (isr[i] & ISR_TXFO) {
			dev_err(dev->dev, "TX overrun (ch_id=%d)\n", i);
			irq_valid = true;
		}

		/* Error Handling: TX */
		if (isr[i] & ISR_RXFO) {
			dev_err(dev->dev, "RX overrun (ch_id=%d)\n", i);
			irq_valid = true;
		}
	}

	if (irq_valid)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static void i2s_start(struct dw_i2s_dev *dev,
		      struct snd_pcm_substream *substream)
{
	struct i2s_clk_config_data *config = &dev->config;

	i2s_write_reg(dev->i2s_base, IER, 1);
	i2s_enable_irqs(dev, substream->stream, config->chan_nr);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		i2s_write_reg(dev->i2s_base, ITER, 1);
	else
		i2s_write_reg(dev->i2s_base, IRER, 1);

	i2s_write_reg(dev->i2s_base, CER, 1);

}

static void i2s_stop(struct dw_i2s_dev *dev,
		struct snd_pcm_substream *substream)
{
	i2s_clear_irqs(dev, substream->stream);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		i2s_write_reg(dev->i2s_base, ITER, 0);
	else
		i2s_write_reg(dev->i2s_base, IRER, 0);

	i2s_disable_irqs(dev, substream->stream, 8);

	if (!dev->active) {
		i2s_write_reg(dev->i2s_base, CER, 0);
		i2s_write_reg(dev->i2s_base, IER, 0);
	}
}

static void dw_i2s_config(struct dw_i2s_dev *dev, int stream)
{
	u32 ch_reg;
	struct i2s_clk_config_data *config = &dev->config;

	i2s_disable_channels(dev, stream);

	for (ch_reg = 0; ch_reg < (config->chan_nr / 2); ch_reg++) {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
			i2s_write_reg(dev->i2s_base, TCR(ch_reg),
				      dev->xfer_resolution);
			i2s_write_reg(dev->i2s_base, TFCR(ch_reg),
				      dev->fifo_th - 1);
			i2s_write_reg(dev->i2s_base, TER(ch_reg), 1);
		} else {
			i2s_write_reg(dev->i2s_base, RCR(ch_reg),
				      dev->xfer_resolution);
			i2s_write_reg(dev->i2s_base, RFCR(ch_reg),
				      dev->fifo_th - 1);
			i2s_write_reg(dev->i2s_base, RER(ch_reg), 1);
		}
	}
}

static int dw_i2s_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct dw_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct i2s_clk_config_data *config = &dev->config;
	int ret;
	unsigned int bclk_rate;
	bool playback;
	union dw_i2s_snd_dma_data *dma_data = NULL;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai_link *dai_link = rtd->dai_link;

	dai_link->stop_dma_first = 1;
	config->chan_nr = params_channels(params);
	config->sample_rate = params_rate(params);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		playback = false;
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		playback = true;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		if ((config->sample_rate == 8000) && playback &&
			(dev->capability & DW_I2S_SLAVE)) {
			dev_warn(dev->dev, "I2S: unsupported 8000 rate with S16_LE, Stereo if use wm8960.\n");
		}

		config->data_width = 16;
		dev->ccr = 0x00;
		dev->xfer_resolution = 0x02;
		if (playback)
			dev->play_dma_data.dt.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		else
			dev->capture_dma_data.dt.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		config->data_width = 32;
		dev->ccr = 0x10;
		dev->xfer_resolution = 0x05;
		if (playback)
			dev->play_dma_data.dt.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		else
			dev->capture_dma_data.dt.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;

	default:
		dev_err(dev->dev, "designware-i2s: unsupported PCM fmt");
		return -EINVAL;
	}

	if (dev->capability & DW_I2S_MASTER) {
		switch (params_rate(params)) {
		case 8000:
			bclk_rate = 512000;
			break;
		case 11025:
			bclk_rate = 705600;
			break;
		case 16000:
			bclk_rate = 1024000;
			break;
		case 32000:
			bclk_rate = 2048000;
			break;
		case 48000:
			bclk_rate = 3072000;
			break;
		default:
			dev_err(dai->dev, "%d rate not supported\n",
					params_rate(params));
			ret = -EINVAL;
			goto err_return;
		}
	}

	switch (config->chan_nr) {
	case EIGHT_CHANNEL_SUPPORT:
	case SIX_CHANNEL_SUPPORT:
	case FOUR_CHANNEL_SUPPORT:
	case TWO_CHANNEL_SUPPORT:
		break;
	default:
		dev_err(dev->dev, "channel not supported\n");
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		dma_data = &dev->capture_dma_data;
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &dev->play_dma_data;
	snd_soc_dai_set_dma_data(dai, substream, (void *)dma_data);

	dw_i2s_config(dev, substream->stream);

	i2s_write_reg(dev->i2s_base, CCR, dev->ccr);

	if (dev->capability & DW_I2S_MASTER) {
		if (dev->i2s_clk_cfg) {
			ret = dev->i2s_clk_cfg(config);
			if (ret < 0) {
				dev_err(dev->dev, "runtime audio clk config fail\n");
				goto err_return;
			}
		} else {
			if (playback) {
				ret = clk_set_parent(dev->clk_mclk, dev->clk_mclk_ext);
				if (ret) {
					dev_err(dev->dev,
						"failed to set mclk parent to mclk_ext\n");
					goto err_return;
				}
			}

			ret = clk_set_rate(dev->clk_i2s_bclk_mst, bclk_rate);
			if (ret) {
				dev_err(dev->dev, "Can't set i2s bclk: %d\n", ret);
				goto err_return;
			}

			dev_dbg(dev->dev, "get rate mclk: %ld\n",
				clk_get_rate(dev->clk_mclk));
			dev_dbg(dev->dev, "get rate bclk:%ld\n",
				clk_get_rate(dev->clk_i2s_bclk));
			dev_dbg(dev->dev, "get rate lrck:%ld\n",
				clk_get_rate(dev->clk_i2s_lrck));
		}
	}
	return 0;

err_return:
	return ret;
}

static int dw_i2s_prepare(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct dw_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		i2s_write_reg(dev->i2s_base, TXFFR, 1);
	else
		i2s_write_reg(dev->i2s_base, RXFFR, 1);

	return 0;
}

static int dw_i2s_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct dw_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev->active++;
		i2s_start(dev, substream);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dev->active--;
		i2s_stop(dev, substream);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int dw_i2s_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct dw_i2s_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		if (dev->capability & DW_I2S_SLAVE)
			ret = 0;
		else
			ret = -EINVAL;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		if (dev->capability & DW_I2S_MASTER)
			ret = 0;
		else
			ret = -EINVAL;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		ret = -EINVAL;
		break;
	default:
		dev_dbg(dev->dev, "dwc : Invalid master/slave format\n");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const struct snd_soc_dai_ops dw_i2s_dai_ops = {
	.hw_params	= dw_i2s_hw_params,
	.prepare	= dw_i2s_prepare,
	.trigger	= dw_i2s_trigger,
	.set_fmt	= dw_i2s_set_fmt,
};

#ifdef CONFIG_PM
static int dw_i2s_runtime_suspend(struct device *dev)
{
	struct dw_i2s_dev *dw_dev = dev_get_drvdata(dev);

	clk_disable_unprepare(dw_dev->clk_i2s_bclk_mst);
	clk_disable_unprepare(dw_dev->clk_i2s_apb);

	return 0;
}

static int dw_i2s_runtime_resume(struct device *dev)
{
	struct dw_i2s_dev *dw_dev = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(dw_dev->clk_i2s_apb);
	if (ret) {
		dev_err(dw_dev->dev, "Failed to enable clk_i2s_apb\n");
		return ret;
	}

	ret = clk_prepare_enable(dw_dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(dw_dev->dev, "Failed to enable clk_i2s_lrck\n");
		return ret;
	}

	if (dw_dev->rst_i2s_apb) {
		ret = reset_control_deassert(dw_dev->rst_i2s_apb);
		if (ret)
			return ret;
	}
	if (dw_dev->rst_i2s_bclk) {
		ret = reset_control_deassert(dw_dev->rst_i2s_bclk);
		if (ret)
			return ret;
	}

	return 0;
}

static int dw_i2s_suspend(struct snd_soc_component *component)
{
	return pm_runtime_force_suspend(component->dev);
}

static int dw_i2s_resume(struct snd_soc_component *component)
{
	struct dw_i2s_dev *dev = snd_soc_component_get_drvdata(component);
	struct snd_soc_dai *dai;
	int stream;
	int ret;

	if (!dev->is_master) {
		ret = clk_set_parent(dev->clk_i2s_bclk, dev->clk_i2s_bclk_mst);
		if (ret) {
			dev_err(dev->dev, "%s: failed to set clk_i2s_bclk parent to bclk_mst.\n",
				__func__);
			goto exit;
		}

		ret = clk_set_parent(dev->clk_i2s_lrck, dev->clk_i2s_lrck_mst);
		if (ret) {
			dev_err(dev->dev, "%s: failed to set clk_i2s_lrck parent to lrck_mst.\n",
				__func__);
			goto exit;
		}
	}

	ret = pm_runtime_force_resume(component->dev);
	if (ret)
		return ret;

	if (dev->syscon_base) {
		if (dev->is_master) {
			/* config i2s data source: PDM  */
			regmap_update_bits(dev->syscon_base, dev->syscon_offset_34,
						AUDIO_SDIN_MUX_MASK, I2SRX_DATA_SRC_PDM);

			regmap_update_bits(dev->syscon_base, dev->syscon_offset_18,
				I2SRX_3CH_ADC_MASK, I2SRX_3CH_ADC_EN);
		} else {
			/*i2srx_3ch_adc_enable*/
			regmap_update_bits(dev->syscon_base, dev->syscon_offset_18,
						0x1 << 1, 0x1 << 1);

			/*set i2sdin_sel*/
			regmap_update_bits(dev->syscon_base, dev->syscon_offset_34,
				(0x1 << 10) | (0x1 << 14) | (0x1<<17), (0x0<<10) | (0x0<<14) | (0x0<<17));
		}
	}

	for_each_component_dais(component, dai) {
		for_each_pcm_streams(stream)
			if (snd_soc_dai_stream_active(dai, stream))
				dw_i2s_config(dev, stream);
	}

	i2s_write_reg(dev->i2s_base, CCR, dev->ccr);

	if (!dev->is_master) {
		ret = clk_set_parent(dev->clk_mclk, dev->clk_mclk_ext);
		if (ret) {
			dev_err(dev->dev, "failed to set mclk parent to mclk_ext\n");
			goto exit;
		}

		ret = clk_set_parent(dev->clk_i2s_bclk, dev->clk_bclk_ext);
		if (ret) {
			dev_err(dev->dev, "%s: failed to set clk_i2s_bclk parent to bclk_ext.\n",
				__func__);
			goto exit;
		}

		ret = clk_set_parent(dev->clk_i2s_lrck, dev->clk_lrck_ext);
		if (ret) {
			dev_err(dev->dev, "%s: failed to set clk_i2s_lrck parent to lrck_ext.\n",
				__func__);
			goto exit;
		}
	}

	return 0;

exit:
	return ret;
}

#else
#define dw_i2s_suspend	NULL
#define dw_i2s_resume	NULL
#endif

static const struct snd_soc_component_driver dw_i2s_component = {
	.name		= "dw-i2s",
	.suspend	= dw_i2s_suspend,
	.resume		= dw_i2s_resume,
};

static int dw_i2srx_mst_clk_init(struct platform_device *pdev, struct dw_i2s_dev *dev)
{
	int ret = 0;

	static struct clk_bulk_data clks[] = {
		{ .id = "i2srx_apb" },
		{ .id = "i2srx_bclk_mst" },
		{ .id = "i2srx_lrck_mst" },
		{ .id = "i2srx_bclk" },
		{ .id = "i2srx_lrck" },
		{ .id = "mclk" },
		{ .id = "mclk_ext" },
	};

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(clks), clks);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to get i2srx clocks\n", __func__);
		goto exit;
	}

	dev->clk_i2s_apb = clks[0].clk;
	dev->clk_i2s_bclk_mst = clks[1].clk;
	dev->clk_i2s_lrck_mst = clks[2].clk;
	dev->clk_i2s_bclk = clks[3].clk;
	dev->clk_i2s_lrck = clks[4].clk;
	dev->clk_mclk = clks[5].clk;
	dev->clk_mclk_ext = clks[6].clk;

	dev->rst_i2s_apb = devm_reset_control_get_exclusive(&pdev->dev, "rst_apb_rx");
	if (IS_ERR(dev->rst_i2s_apb)) {
		dev_err(&pdev->dev, "failed to get apb_i2srx reset control\n");
		ret = PTR_ERR(dev->rst_i2s_apb);
		goto exit;
	}

	dev->rst_i2s_bclk = devm_reset_control_get_exclusive(&pdev->dev, "rst_bclk_rx");
	if (IS_ERR(dev->rst_i2s_bclk)) {
		dev_err(&pdev->dev, "failed to get i2s bclk rx reset control\n");
		ret = PTR_ERR(dev->rst_i2s_bclk);
		goto exit;
	}

	ret = clk_prepare_enable(dev->clk_i2s_apb);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_i2srx_apb\n");
		goto exit;
	}

	ret = clk_set_parent(dev->clk_i2s_bclk, dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(&pdev->dev, "failed to set parent clk_i2s_bclk\n");
		goto err_dis_bclk_mst;
	}

	ret = clk_set_parent(dev->clk_i2s_lrck, dev->clk_i2s_lrck_mst);
	if (ret) {
		dev_err(&pdev->dev, "failed to set parent clk_i2s_lrck\n");
		goto err_dis_bclk_mst;
	}

	ret = clk_set_parent(dev->clk_mclk, dev->clk_mclk_ext);
	if (ret) {
		dev_err(&pdev->dev, "failed to set mclk parent to mclk_ext\n");
		goto err_dis_bclk_mst;
	}

	ret = clk_prepare_enable(dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_i2srx_3ch_bclk_mst\n");
		goto err_dis_bclk_mst;
	}

	reset_control_deassert(dev->rst_i2s_apb);
	reset_control_deassert(dev->rst_i2s_bclk);

	regmap_update_bits(dev->syscon_base, dev->syscon_offset_18,
				I2SRX_3CH_ADC_MASK, I2SRX_3CH_ADC_EN);

	return 0;

err_dis_bclk_mst:
	clk_disable_unprepare(dev->clk_i2s_apb);
exit:
	return ret;
}

static int dw_i2srx_clk_init(struct platform_device *pdev, struct dw_i2s_dev *dev)
{
	int ret = 0;

	static struct clk_bulk_data clks[] = {
		{ .id = "3ch-apb" },
		{ .id = "bclk_mst" },
		{ .id = "3ch-lrck" },
		{ .id = "rx-bclk" },
		{ .id = "rx-lrck" },
		{ .id = "bclk-ext" },
		{ .id = "lrck-ext" },
		{ .id = "mclk" },
		{ .id = "mclk_ext" },
	};

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(clks), clks);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to get audio_subsys clocks\n", __func__);
		return ret;
	}

	dev->clk_i2s_apb = clks[0].clk;
	dev->clk_i2s_bclk_mst = clks[1].clk;
	dev->clk_i2s_lrck_mst = clks[2].clk;
	dev->clk_i2s_bclk = clks[3].clk;
	dev->clk_i2s_lrck = clks[4].clk;
	dev->clk_bclk_ext = clks[5].clk;
	dev->clk_lrck_ext = clks[6].clk;
	dev->clk_mclk = clks[7].clk;
	dev->clk_mclk_ext = clks[8].clk;

	ret = clk_set_parent(dev->clk_i2s_bclk, dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to set clk_i2s_bclk parent to bclk_mst.\n",
			__func__);
		goto exit;
	}

	ret = clk_set_parent(dev->clk_i2s_lrck, dev->clk_i2s_lrck_mst);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to set clk_i2s_lrck parent to lrck_mst.\n",
			__func__);
		goto exit;
	}

	ret = clk_prepare_enable(dev->clk_i2s_apb);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to enable clk_i2s_apb\n", __func__);
		goto disable_apb_clk;
	}

	ret = clk_prepare_enable(dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to enable CLK_ADC_BCLK\n", __func__);
		goto disable_bclk;
	}

	dev->rst_i2s_apb = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(dev->rst_i2s_apb)) {
		dev_err(&pdev->dev, "%s: failed to get rsts reset control\n", __func__);
		goto disable_rst;
	}

	ret = reset_control_assert(dev->rst_i2s_apb);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to reset control assert rstc_rx\n", __func__);
		goto disable_rst;
	}
	udelay(5);
	ret = reset_control_deassert(dev->rst_i2s_apb);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to reset control deassert rstc_rx\n", __func__);
		goto disable_rst;
	}

	/*i2srx_3ch_adc_enable*/
	regmap_update_bits(dev->syscon_base, dev->syscon_offset_18,
					0x1 << 1, 0x1 << 1);

	/*set i2sdin_sel*/
	regmap_update_bits(dev->syscon_base, dev->syscon_offset_34,
		(0x1 << 10) | (0x1 << 14) | (0x1<<17), (0x0<<10) | (0x0<<14) | (0x0<<17));


	ret = clk_set_parent(dev->clk_mclk, dev->clk_mclk_ext);
	if (ret) {
		dev_err(&pdev->dev, "failed to set mclk parent to mclk_ext\n");
		goto disable_parent;
	}

	ret = clk_set_parent(dev->clk_i2s_bclk, dev->clk_bclk_ext);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to set clk_i2s_bclk parent to bclk_ext.\n",
			__func__);
		goto disable_parent;
	}

	ret = clk_set_parent(dev->clk_i2s_lrck, dev->clk_lrck_ext);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to set clk_i2s_lrck parent to lrck_ext.\n",
			__func__);
		goto disable_parent;
	}

	return 0;

disable_parent:
disable_rst:
	clk_disable_unprepare(dev->clk_i2s_bclk_mst);
disable_bclk:
	clk_disable_unprepare(dev->clk_i2s_apb);
disable_apb_clk:
exit:

	return ret;
}

static int dw_i2stx_4ch0_clk_init(struct platform_device *pdev, struct dw_i2s_dev *dev)
{
	int ret = 0;

	static struct clk_bulk_data clks[] = {
		{ .id = "bclk-mst" },
		{ .id = "lrck-mst" },
		{ .id = "mclk" },
		{ .id = "bclk0" },
		{ .id = "lrck0" },
		{ .id = "i2s_apb" },
		{ .id = "mclk_ext" },
	};

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(clks), clks);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to get i2s clocks\n", __func__);
		goto exit;
	}

	dev->clk_i2s_bclk_mst = clks[0].clk;
	dev->clk_i2s_lrck_mst = clks[1].clk;
	dev->clk_mclk = clks[2].clk;
	dev->clk_i2s_bclk = clks[3].clk;
	dev->clk_i2s_lrck = clks[4].clk;
	dev->clk_i2s_apb = clks[5].clk;
	dev->clk_mclk_ext = clks[6].clk;

	dev->rst_i2s_apb = devm_reset_control_get_exclusive(&pdev->dev, "rst_apb");
	if (IS_ERR(dev->rst_i2s_apb)) {
		dev_err(&pdev->dev, "failed to get i2s apb reset control\n");
		ret = PTR_ERR(dev->rst_i2s_apb);
		goto exit;
	}

	dev->rst_i2s_bclk = devm_reset_control_get_exclusive(&pdev->dev, "rst_bclk");
	if (IS_ERR(dev->rst_i2s_bclk)) {
		dev_err(&pdev->dev, "failed to get i2s bclk reset control\n");
		ret = PTR_ERR(dev->rst_i2s_bclk);
		goto exit;
	}

	ret = clk_prepare_enable(dev->clk_i2s_apb);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_i2s_apb\n");
		goto exit;
	}

	ret = clk_set_parent(dev->clk_mclk, dev->clk_mclk_ext);
	if (ret) {
		dev_err(&pdev->dev, "failed to set mclk parent to mclk_ext\n");
		goto err_set_parent;
	}

	ret = clk_set_parent(dev->clk_i2s_lrck_mst, dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(&pdev->dev, "failed to set parent clk_i2s_lrck_mst\n");
		goto err_dis_bclk_mst;
	}

	ret = clk_set_parent(dev->clk_i2s_bclk, dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(&pdev->dev, "failed to set parent clk_i2s_bclk\n");
		goto err_dis_bclk_mst;
	}

	ret = clk_set_parent(dev->clk_i2s_lrck, dev->clk_i2s_lrck_mst);
	if (ret) {
		dev_err(&pdev->dev, "failed to set parent clk_i2s_lrck\n");
		goto err_dis_bclk_mst;
	}

	ret = clk_prepare_enable(dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_i2srx_3ch_bclk_mst\n");
		goto err_dis_bclk_mst;
	}

	reset_control_deassert(dev->rst_i2s_apb);
	reset_control_deassert(dev->rst_i2s_bclk);

	return 0;

err_dis_bclk_mst:
err_set_parent:
	clk_disable_unprepare(dev->clk_i2s_apb);
exit:
	return ret;
}

static int dw_i2stx_4ch1_clk_init(struct platform_device *pdev, struct dw_i2s_dev *dev)
{
	int ret = 0;

	static struct clk_bulk_data clks[] = {
		{ .id = "bclk_mst" },
		{ .id = "lrck_mst" },
		{ .id = "mclk" },
		{ .id = "4chbclk" },
		{ .id = "4chlrck" },
		{ .id = "clk_apb" },
		{ .id = "mclk_ext" },
		{ .id = "bclk_ext" },
		{ .id = "lrck_ext" },
	};

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(clks), clks);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to get i2s clocks\n", __func__);
		goto exit;
	}

	dev->clk_i2s_bclk_mst = clks[0].clk;
	dev->clk_i2s_lrck_mst = clks[1].clk;
	dev->clk_mclk = clks[2].clk;
	dev->clk_i2s_bclk = clks[3].clk;
	dev->clk_i2s_lrck = clks[4].clk;
	dev->clk_i2s_apb = clks[5].clk;
	dev->clk_mclk_ext = clks[6].clk;
	dev->clk_bclk_ext = clks[7].clk;
	dev->clk_lrck_ext = clks[8].clk;

	ret = clk_set_parent(dev->clk_i2s_bclk, dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to set clk_i2s_bclk parent to bclk_mst.\n",
			__func__);
		goto exit;
	}

	ret = clk_set_parent(dev->clk_i2s_lrck, dev->clk_i2s_lrck_mst);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to set clk_i2s_lrck parent to lrck_mst.\n",
			__func__);
		goto exit;
	}

	ret = clk_prepare_enable(dev->clk_i2s_apb);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to enable clk_i2s_apb\n", __func__);
		goto exit;
	}

	ret = clk_prepare_enable(dev->clk_i2s_bclk_mst);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to enable clk_i2s_bclk_mst\n", __func__);
		goto disable_bclk;
	}

	dev->rst_i2s_apb = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(dev->rst_i2s_apb)) {
		dev_err(&pdev->dev, "%s: failed to get rsts reset control\n", __func__);
		goto disable_rst;
	}

	ret = reset_control_assert(dev->rst_i2s_apb);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to reset control assert rsts\n", __func__);
		goto disable_rst;
	}
	udelay(5);
	ret = reset_control_deassert(dev->rst_i2s_apb);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to reset control deassert rsts\n", __func__);
		goto disable_rst;
	}

	ret = clk_set_parent(dev->clk_mclk, dev->clk_mclk_ext);
	if (ret) {
		dev_err(&pdev->dev, "failed to set mclk parent to mclk_ext\n");
		goto exit;
	}

	ret = clk_set_parent(dev->clk_i2s_bclk, dev->clk_bclk_ext);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to set clk_i2s_bclk parent to bclk_ext.\n",
			__func__);
		goto disable_parent;
	}

	ret = clk_set_parent(dev->clk_i2s_lrck, dev->clk_lrck_ext);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to set clk_i2s_lrck parent to lrck_ext.\n",
			__func__);
		goto disable_parent;
	}

	return 0;

disable_parent:
disable_rst:
	clk_disable_unprepare(dev->clk_i2s_bclk_mst);
disable_bclk:
	clk_disable_unprepare(dev->clk_i2s_apb);
exit:
	return ret;
}

/*
 * The following tables allow a direct lookup of various parameters
 * defined in the I2S block's configuration in terms of sound system
 * parameters.  Each table is sized to the number of entries possible
 * according to the number of configuration bits describing an I2S
 * block parameter.
 */

/* Maximum bit resolution of a channel - not uniformly spaced */
static const u32 fifo_width[COMP_MAX_WORDSIZE] = {
	12, 16, 20, 24, 32, 0, 0, 0
};

/* Width of (DMA) bus */
static const u32 bus_widths[COMP_MAX_DATA_WIDTH] = {
	DMA_SLAVE_BUSWIDTH_1_BYTE,
	DMA_SLAVE_BUSWIDTH_2_BYTES,
	DMA_SLAVE_BUSWIDTH_4_BYTES,
	DMA_SLAVE_BUSWIDTH_UNDEFINED
};

/* PCM format to support channel resolution */
static const u32 formats[COMP_MAX_WORDSIZE] = {
	SNDRV_PCM_FMTBIT_S16_LE,
	SNDRV_PCM_FMTBIT_S16_LE,
	SNDRV_PCM_FMTBIT_S24_LE,
	SNDRV_PCM_FMTBIT_S24_LE,
	SNDRV_PCM_FMTBIT_S32_LE,
	0,
	0,
	0
};

#define SF_PCM_RATE_32000_192000  (SNDRV_PCM_RATE_32000 | \
				   SNDRV_PCM_RATE_44100 | \
				   SNDRV_PCM_RATE_48000 | \
				   SNDRV_PCM_RATE_88200 | \
				   SNDRV_PCM_RATE_96000 | \
				   SNDRV_PCM_RATE_176400 | \
				   SNDRV_PCM_RATE_192000)

static int dw_configure_dai(struct dw_i2s_dev *dev,
				   struct snd_soc_dai_driver *dw_i2s_dai,
				   unsigned int rates)
{
	/*
	 * Read component parameter registers to extract
	 * the I2S block's configuration.
	 */
	u32 comp1 = i2s_read_reg(dev->i2s_base, dev->i2s_reg_comp1);
	u32 comp2 = i2s_read_reg(dev->i2s_base, dev->i2s_reg_comp2);
	u32 fifo_depth = 1 << (1 + COMP1_FIFO_DEPTH_GLOBAL(comp1));
	u32 idx;

	if (dev->capability & DWC_I2S_RECORD &&
			dev->quirks & DW_I2S_QUIRK_COMP_PARAM1)
		comp1 = comp1 & ~BIT(5);

	if (dev->capability & DWC_I2S_PLAY &&
			dev->quirks & DW_I2S_QUIRK_COMP_PARAM1)
		comp1 = comp1 & ~BIT(6);

	if (COMP1_TX_ENABLED(comp1)) {
		dev_info(dev->dev, " designware: play supported\n");
		idx = COMP1_TX_WORDSIZE_0(comp1);
		if (WARN_ON(idx >= ARRAY_SIZE(formats)))
			return -EINVAL;
		if (dev->quirks & DW_I2S_QUIRK_16BIT_IDX_OVERRIDE)
			idx = 1;
		dw_i2s_dai->playback.channels_min = MIN_CHANNEL_NUM;
		dw_i2s_dai->playback.channels_max =
				(COMP1_TX_CHANNELS(comp1) + 1);
		dw_i2s_dai->playback.formats = formats[idx];
		for (; idx > 0; idx--)
			dw_i2s_dai->playback.formats |= formats[idx - 1];
		dw_i2s_dai->playback.rates = rates;
	}

	if (COMP1_RX_ENABLED(comp1)) {
		dev_info(dev->dev, "designware: record supported\n");
		idx = COMP2_RX_WORDSIZE_0(comp2);
		if (WARN_ON(idx >= ARRAY_SIZE(formats)))
			return -EINVAL;
		if (dev->quirks & DW_I2S_QUIRK_16BIT_IDX_OVERRIDE)
			idx = 1;
		dw_i2s_dai->capture.channels_min = MIN_CHANNEL_NUM;
		dw_i2s_dai->capture.channels_max =
				(COMP1_RX_CHANNELS(comp1) + 1);
		dw_i2s_dai->capture.formats = formats[idx];
		for (; idx > 0; idx--)
			dw_i2s_dai->capture.formats |= formats[idx - 1];
		dw_i2s_dai->capture.rates = rates;
	}

	if (COMP1_MODE_EN(comp1) || dev->is_master) {
		dev_err(dev->dev, "designware: i2s master mode supported\n");
		dev->capability |= DW_I2S_MASTER;
	} else {
		dev_err(dev->dev, "designware: i2s slave mode supported\n");
		dev->capability |= DW_I2S_SLAVE;
	}

	dev->fifo_th = fifo_depth / 2;
	dev_dbg(dev->dev, "fifo_th :%d\n", dev->fifo_th);
	return 0;
}

static int dw_configure_dai_by_pd(struct dw_i2s_dev *dev,
				   struct snd_soc_dai_driver *dw_i2s_dai,
				   struct resource *res,
				   const struct i2s_platform_data *pdata)
{
	u32 comp1 = i2s_read_reg(dev->i2s_base, dev->i2s_reg_comp1);
	u32 idx = COMP1_APB_DATA_WIDTH(comp1);
	int ret;

	if (WARN_ON(idx >= ARRAY_SIZE(bus_widths)))
		return -EINVAL;

	ret = dw_configure_dai(dev, dw_i2s_dai, pdata->snd_rates);
	if (ret < 0)
		return ret;

	if (dev->quirks & DW_I2S_QUIRK_16BIT_IDX_OVERRIDE)
		idx = 1;
	/* Set DMA slaves info */
	dev->play_dma_data.pd.data = pdata->play_dma_data;
	dev->capture_dma_data.pd.data = pdata->capture_dma_data;
	dev->play_dma_data.pd.addr = res->start + I2S_TXDMA;
	dev->capture_dma_data.pd.addr = res->start + I2S_RXDMA;
	dev->play_dma_data.pd.max_burst = 16;
	dev->capture_dma_data.pd.max_burst = 16;
	dev->play_dma_data.pd.addr_width = bus_widths[idx];
	dev->capture_dma_data.pd.addr_width = bus_widths[idx];
	dev->play_dma_data.pd.filter = pdata->filter;
	dev->capture_dma_data.pd.filter = pdata->filter;

	return 0;
}

static int dw_configure_dai_by_dt(struct dw_i2s_dev *dev,
				   struct snd_soc_dai_driver *dw_i2s_dai,
				   struct resource *res)
{
	u32 comp1 = i2s_read_reg(dev->i2s_base, I2S_COMP_PARAM_1);
	u32 comp2 = i2s_read_reg(dev->i2s_base, I2S_COMP_PARAM_2);
	u32 fifo_depth = 1 << (1 + COMP1_FIFO_DEPTH_GLOBAL(comp1));
	u32 idx = COMP1_APB_DATA_WIDTH(comp1);
	u32 idx2;
	int ret;

	if (WARN_ON(idx >= ARRAY_SIZE(bus_widths)))
		return -EINVAL;

	ret = dw_configure_dai(dev, dw_i2s_dai, dev->susport_rate);
	if (ret < 0)
		return ret;

	if (COMP1_TX_ENABLED(comp1)) {
		idx2 = COMP1_TX_WORDSIZE_0(comp1);

		dev->capability |= DWC_I2S_PLAY;
		dev->play_dma_data.dt.addr = res->start + I2S_TXDMA;
		dev->play_dma_data.dt.addr_width = bus_widths[idx];
		dev->play_dma_data.dt.fifo_size = fifo_depth *
			(fifo_width[idx2]) >> 8;
		dev->play_dma_data.dt.maxburst = 16;
	}
	if (COMP1_RX_ENABLED(comp1)) {
		idx2 = COMP2_RX_WORDSIZE_0(comp2);

		/* force change to 1 */
		if (dev->is_master) {
			idx = 1;
			idx2 = 1;
		}
		dev->capability |= DWC_I2S_RECORD;
		dev->capture_dma_data.dt.addr = res->start + I2S_RXDMA;
		dev->capture_dma_data.dt.addr_width = bus_widths[idx];
		dev->capture_dma_data.dt.fifo_size = fifo_depth *
			(fifo_width[idx2] >> 8);
		dev->capture_dma_data.dt.maxburst = 16;
	}

	return 0;

}

static int dw_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct dw_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &dev->play_dma_data, &dev->capture_dma_data);
	return 0;
}

static int dw_i2s_probe(struct platform_device *pdev)
{
	const struct i2s_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_args args;
	struct dw_i2s_dev *dev;
	struct resource *res;
	int ret, irq;
	struct snd_soc_dai_driver *dw_i2s_dai;
	const char *clk_id;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dw_i2s_dai = devm_kzalloc(&pdev->dev, sizeof(*dw_i2s_dai), GFP_KERNEL);
	if (!dw_i2s_dai)
		return -ENOMEM;

	dw_i2s_dai->ops = &dw_i2s_dai_ops;
	dw_i2s_dai->probe = dw_i2s_dai_probe;

	dev->i2s_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(dev->i2s_base))
		return PTR_ERR(dev->i2s_base);

	dev->dev = &pdev->dev;

	irq = platform_get_irq_optional(pdev, 0);
	if (irq >= 0) {
		ret = devm_request_irq(&pdev->dev, irq, i2s_irq_handler, 0,
				pdev->name, dev);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request irq\n");
			return ret;
		}
	}

	if (of_device_is_compatible(np, "starfive,jh7110-i2srx-master")) {
		ret = of_parse_phandle_with_fixed_args(dev->dev->of_node,
						"starfive,sys-syscon", 2, 0, &args);
		if (ret) {
			dev_err(dev->dev, "Failed to parse starfive,sys-syscon\n");
			return -EINVAL;
		}

		dev->syscon_base = syscon_node_to_regmap(args.np);
		of_node_put(args.np);
		if (IS_ERR(dev->syscon_base))
			return PTR_ERR(dev->syscon_base);

		dev->syscon_offset_18 = args.args[0];
		dev->syscon_offset_34 = args.args[1];

		/* config i2s data source: PDM  */
		regmap_update_bits(dev->syscon_base, dev->syscon_offset_34,
					AUDIO_SDIN_MUX_MASK, I2SRX_DATA_SRC_PDM);

		ret = dw_i2srx_mst_clk_init(pdev, dev);
		if (ret < 0)
			goto err_init;
		dev->is_master = true;
		dev->susport_rate = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000;
	} else if (of_device_is_compatible(np, "starfive,jh7110-i2srx")) {
		ret = of_parse_phandle_with_fixed_args(dev->dev->of_node,
							"starfive,sys-syscon", 2, 0, &args);
		if (ret) {
			dev_err(dev->dev, "Failed to parse starfive,sys-syscon\n");
			return -EINVAL;
		}
		dev->syscon_base = syscon_node_to_regmap(args.np);
		of_node_put(args.np);
		if (IS_ERR(dev->syscon_base))
			return PTR_ERR(dev->syscon_base);

		dev->syscon_offset_18 = args.args[0];
		dev->syscon_offset_34 = args.args[1];
		ret = dw_i2srx_clk_init(pdev, dev);
		if (ret < 0)
			goto err_init;
		dev->is_master = false;
		dev->susport_rate = SNDRV_PCM_RATE_8000_192000;
	} else if (of_device_is_compatible(np, "starfive,jh7110-i2stx-4ch0")) {
		/* hdmi audio  */
		ret = dw_i2stx_4ch0_clk_init(pdev, dev);
		if (ret < 0)
			goto err_init;
		dev->is_master = true;
		dev->susport_rate = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000;
	} else if (of_device_is_compatible(np, "starfive,jh7110-i2stx-4ch1")) {
		ret = dw_i2stx_4ch1_clk_init(pdev, dev);
		if (ret < 0)
			goto err_init;
		dev->is_master = false;
		dev->susport_rate = SNDRV_PCM_RATE_8000_192000;
	}

	dev->i2s_reg_comp1 = I2S_COMP_PARAM_1;
	dev->i2s_reg_comp2 = I2S_COMP_PARAM_2;

	if (pdata) {
		dev->capability = pdata->cap;
		clk_id = NULL;
		dev->quirks = pdata->quirks;
		if (dev->quirks & DW_I2S_QUIRK_COMP_REG_OFFSET) {
			dev->i2s_reg_comp1 = pdata->i2s_reg_comp1;
			dev->i2s_reg_comp2 = pdata->i2s_reg_comp2;
		}
		ret = dw_configure_dai_by_pd(dev, dw_i2s_dai, res, pdata);
	} else {
		clk_id = "i2stx_bclk";
		ret = dw_configure_dai_by_dt(dev, dw_i2s_dai, res);
	}
	if (ret < 0)
		goto err_clk_disable;

	if (dev->capability & DW_I2S_MASTER) {
		if (pdata) {
			dev->i2s_clk_cfg = pdata->i2s_clk_cfg;
			if (!dev->i2s_clk_cfg) {
				dev_err(&pdev->dev, "no clock configure method\n");
				ret = -ENODEV;
				goto err_clk_disable;
			}
		}
	}

	dev_set_drvdata(&pdev->dev, dev);
	ret = devm_snd_soc_register_component(&pdev->dev, &dw_i2s_component,
					 dw_i2s_dai, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "not able to register dai\n");
		goto err_clk_disable;
	}

	if (!pdata) {
		if (irq >= 0) {
			ret = dw_pcm_register(pdev);
			dev->use_pio = true;
		} else {
			ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL,
					0);
			dev->use_pio = false;
		}

		if (ret) {
			dev_err(&pdev->dev, "could not register pcm: %d\n",
					ret);
			goto err_clk_disable;
		}
	}

	pm_runtime_enable(&pdev->dev);

#ifdef CONFIG_PM
	clk_disable_unprepare(dev->clk_i2s_bclk_mst);
	clk_disable_unprepare(dev->clk_i2s_apb);
#endif

	return 0;

err_clk_disable:
	clk_disable_unprepare(dev->clk_i2s_bclk_mst);
	clk_disable_unprepare(dev->clk_i2s_apb);
err_init:
	return ret;
}

static int dw_i2s_remove(struct platform_device *pdev)
{
#ifndef CONFIG_PM
	struct dw_i2s_dev *dev = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(dev->clk_i2s_bclk_mst);
	clk_disable_unprepare(dev->clk_i2s_apb);
#endif

	pm_runtime_disable(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dw_i2s_of_match[] = {
	{ .compatible = "starfive,jh7110-i2srx-master", },
	{ .compatible = "starfive,jh7110-i2srx", },
	{ .compatible = "starfive,jh7110-i2stx-4ch0", },
	{ .compatible = "starfive,jh7110-i2stx-4ch1", },
	{},
};

MODULE_DEVICE_TABLE(of, dw_i2s_of_match);
#endif

static const struct dev_pm_ops dwc_pm_ops = {
	SET_RUNTIME_PM_OPS(dw_i2s_runtime_suspend, dw_i2s_runtime_resume, NULL)
};

static struct platform_driver dw_i2s_driver = {
	.probe		= dw_i2s_probe,
	.remove		= dw_i2s_remove,
	.driver		= {
		.name	= "starfive-i2s",
		.of_match_table = of_match_ptr(dw_i2s_of_match),
		.pm = &dwc_pm_ops,
	},
};

module_platform_driver(dw_i2s_driver);

MODULE_AUTHOR("Rajeev Kumar <rajeevkumar.linux@gmail.com>");
MODULE_AUTHOR("Walker Chen <walker.chen@starfivetech.com>");
MODULE_AUTHOR("Xingyu Wu <xingyu.wu@starfivetech.com>");
MODULE_DESCRIPTION("DESIGNWARE I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:designware_i2s");

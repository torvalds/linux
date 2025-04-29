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
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <sound/designware_i2s.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include "local.h"

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
			dev_err_ratelimited(dev->dev, "TX overrun (ch_id=%d)\n", i);
			irq_valid = true;
		}

		/* Error Handling: TX */
		if (isr[i] & ISR_RXFO) {
			dev_err_ratelimited(dev->dev, "RX overrun (ch_id=%d)\n", i);
			irq_valid = true;
		}
	}

	if (irq_valid)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static void i2s_enable_dma(struct dw_i2s_dev *dev, u32 stream)
{
	u32 dma_reg = i2s_read_reg(dev->i2s_base, I2S_DMACR);

	/* Enable DMA handshake for stream */
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_reg |= I2S_DMAEN_TXBLOCK;
	else
		dma_reg |= I2S_DMAEN_RXBLOCK;

	i2s_write_reg(dev->i2s_base, I2S_DMACR, dma_reg);
}

static void i2s_disable_dma(struct dw_i2s_dev *dev, u32 stream)
{
	u32 dma_reg = i2s_read_reg(dev->i2s_base, I2S_DMACR);

	/* Disable DMA handshake for stream */
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dma_reg &= ~I2S_DMAEN_TXBLOCK;
		i2s_write_reg(dev->i2s_base, I2S_RTXDMA, 1);
	} else {
		dma_reg &= ~I2S_DMAEN_RXBLOCK;
		i2s_write_reg(dev->i2s_base, I2S_RRXDMA, 1);
	}
	i2s_write_reg(dev->i2s_base, I2S_DMACR, dma_reg);
}

static void i2s_start(struct dw_i2s_dev *dev,
		      struct snd_pcm_substream *substream)
{
	struct i2s_clk_config_data *config = &dev->config;

	u32 reg = IER_IEN;

	if (dev->tdm_slots) {
		reg |= (dev->tdm_slots - 1) << IER_TDM_SLOTS_SHIFT;
		reg |= IER_INTF_TYPE;
		reg |= dev->frame_offset << IER_FRAME_OFF_SHIFT;
	}

	i2s_write_reg(dev->i2s_base, IER, reg);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		i2s_write_reg(dev->i2s_base, ITER, 1);
	else
		i2s_write_reg(dev->i2s_base, IRER, 1);

	/* I2S needs to enable IRQ to make a handshake with DMAC on the JH7110 SoC */
	if (dev->use_pio || dev->is_jh7110)
		i2s_enable_irqs(dev, substream->stream, config->chan_nr);
	else
		i2s_enable_dma(dev, substream->stream);

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

	if (dev->use_pio || dev->is_jh7110)
		i2s_disable_irqs(dev, substream->stream, 8);
	else
		i2s_disable_dma(dev, substream->stream);

	if (!dev->active) {
		i2s_write_reg(dev->i2s_base, CER, 0);
		i2s_write_reg(dev->i2s_base, IER, 0);
	}
}

static int dw_i2s_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *cpu_dai)
{
	struct dw_i2s_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);

	if (dev->is_jh7110) {
		struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
		struct snd_soc_dai_link *dai_link = rtd->dai_link;

		dai_link->trigger_stop = SND_SOC_TRIGGER_ORDER_LDC;
	}

	return 0;
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
			i2s_write_reg(dev->i2s_base, TER(ch_reg), TER_TXCHEN |
				      dev->tdm_mask << TER_TXSLOT_SHIFT);
		} else {
			i2s_write_reg(dev->i2s_base, RCR(ch_reg),
				      dev->xfer_resolution);
			i2s_write_reg(dev->i2s_base, RFCR(ch_reg),
				      dev->fifo_th - 1);
			i2s_write_reg(dev->i2s_base, RER(ch_reg), RER_RXCHEN |
				      dev->tdm_mask << RER_RXSLOT_SHIFT);
		}

	}
}

static int dw_i2s_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct dw_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct i2s_clk_config_data *config = &dev->config;
	int ret;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		config->data_width = 16;
		dev->ccr = 0x00;
		dev->xfer_resolution = 0x02;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		config->data_width = 24;
		dev->ccr = 0x08;
		dev->xfer_resolution = 0x04;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		config->data_width = 32;
		dev->ccr = 0x10;
		dev->xfer_resolution = 0x05;
		break;

	default:
		dev_err(dev->dev, "designware-i2s: unsupported PCM fmt");
		return -EINVAL;
	}

	if (dev->tdm_slots)
		config->data_width = 32;

	config->chan_nr = params_channels(params);

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

	dw_i2s_config(dev, substream->stream);

	i2s_write_reg(dev->i2s_base, CCR, dev->ccr);

	config->sample_rate = params_rate(params);

	if (dev->capability & DW_I2S_MASTER) {
		if (dev->i2s_clk_cfg) {
			ret = dev->i2s_clk_cfg(config);
			if (ret < 0) {
				dev_err(dev->dev, "runtime audio clk config fail\n");
				return ret;
			}
		} else {
			u32 bitclk = config->sample_rate *
					config->data_width * 2;

			ret = clk_set_rate(dev->clk, bitclk);
			if (ret) {
				dev_err(dev->dev, "Can't set I2S clock rate: %d\n",
					ret);
				return ret;
			}
		}
	}
	return 0;
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

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BC_FC:
		if (dev->capability & DW_I2S_SLAVE)
			ret = 0;
		else
			ret = -EINVAL;
		break;
	case SND_SOC_DAIFMT_BP_FP:
		if (dev->capability & DW_I2S_MASTER)
			ret = 0;
		else
			ret = -EINVAL;
		break;
	case SND_SOC_DAIFMT_BC_FP:
	case SND_SOC_DAIFMT_BP_FC:
		ret = -EINVAL;
		break;
	default:
		dev_dbg(dev->dev, "dwc : Invalid clock provider format\n");
		ret = -EINVAL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_DSP_A:
		dev->frame_offset = 1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		dev->frame_offset = 0;
		break;
	default:
		dev_err(dev->dev, "DAI format unsupported");
		return -EINVAL;
	}

	return ret;
}

static int dw_i2s_set_tdm_slot(struct snd_soc_dai *cpu_dai,	unsigned int tx_mask,
			   unsigned int rx_mask, int slots, int slot_width)
{
	struct dw_i2s_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);

	if (slot_width != 32)
		return -EINVAL;

	if (slots < 0 || slots > 16)
		return -EINVAL;

	if (rx_mask != tx_mask)
		return -EINVAL;

	if (!rx_mask)
		return -EINVAL;

	dev->tdm_slots = slots;
	dev->tdm_mask = rx_mask;

	dev->l_reg = RSLOT_TSLOT(ffs(rx_mask) - 1);
	dev->r_reg = RSLOT_TSLOT(fls(rx_mask) - 1);

	return 0;
}

static int dw_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct dw_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &dev->play_dma_data, &dev->capture_dma_data);
	return 0;
}

static const struct snd_soc_dai_ops dw_i2s_dai_ops = {
	.probe		= dw_i2s_dai_probe,
	.startup	= dw_i2s_startup,
	.hw_params	= dw_i2s_hw_params,
	.prepare	= dw_i2s_prepare,
	.trigger	= dw_i2s_trigger,
	.set_fmt	= dw_i2s_set_fmt,
	.set_tdm_slot	= dw_i2s_set_tdm_slot,
};

static int dw_i2s_runtime_suspend(struct device *dev)
{
	struct dw_i2s_dev *dw_dev = dev_get_drvdata(dev);

	if (dw_dev->capability & DW_I2S_MASTER)
		clk_disable(dw_dev->clk);
	return 0;
}

static int dw_i2s_runtime_resume(struct device *dev)
{
	struct dw_i2s_dev *dw_dev = dev_get_drvdata(dev);
	int ret;

	if (dw_dev->capability & DW_I2S_MASTER) {
		ret = clk_enable(dw_dev->clk);
		if (ret)
			return ret;
	}
	return 0;
}

#ifdef CONFIG_PM
static int dw_i2s_suspend(struct snd_soc_component *component)
{
	struct dw_i2s_dev *dev = snd_soc_component_get_drvdata(component);

	if (dev->capability & DW_I2S_MASTER)
		clk_disable(dev->clk);
	return 0;
}

static int dw_i2s_resume(struct snd_soc_component *component)
{
	struct dw_i2s_dev *dev = snd_soc_component_get_drvdata(component);
	struct snd_soc_dai *dai;
	int stream, ret;

	if (dev->capability & DW_I2S_MASTER) {
		ret = clk_enable(dev->clk);
		if (ret)
			return ret;
	}

	for_each_component_dais(component, dai) {
		for_each_pcm_streams(stream)
			if (snd_soc_dai_stream_active(dai, stream))
				dw_i2s_config(dev, stream);
	}

	return 0;
}

#else
#define dw_i2s_suspend	NULL
#define dw_i2s_resume	NULL
#endif

static const struct snd_soc_component_driver dw_i2s_component = {
	.name			= "dw-i2s",
	.suspend		= dw_i2s_suspend,
	.resume			= dw_i2s_resume,
	.legacy_dai_naming	= 1,
};

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
	SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	0,
	0,
	0
};

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
		dev_dbg(dev->dev, " designware: play supported\n");
		idx = COMP1_TX_WORDSIZE_0(comp1);
		if (WARN_ON(idx >= ARRAY_SIZE(formats)))
			return -EINVAL;
		if (dev->quirks & DW_I2S_QUIRK_16BIT_IDX_OVERRIDE)
			idx = 1;
		dw_i2s_dai->playback.channels_min = MIN_CHANNEL_NUM;
		dw_i2s_dai->playback.channels_max =
				1 << (COMP1_TX_CHANNELS(comp1) + 1);
		dw_i2s_dai->playback.formats = formats[idx];
		dw_i2s_dai->playback.rates = rates;
	}

	if (COMP1_RX_ENABLED(comp1)) {
		dev_dbg(dev->dev, "designware: record supported\n");
		idx = COMP2_RX_WORDSIZE_0(comp2);
		if (WARN_ON(idx >= ARRAY_SIZE(formats)))
			return -EINVAL;
		if (dev->quirks & DW_I2S_QUIRK_16BIT_IDX_OVERRIDE)
			idx = 1;
		dw_i2s_dai->capture.channels_min = MIN_CHANNEL_NUM;
		dw_i2s_dai->capture.channels_max =
				1 << (COMP1_RX_CHANNELS(comp1) + 1);
		dw_i2s_dai->capture.formats = formats[idx];
		dw_i2s_dai->capture.rates = rates;
	}

	if (COMP1_MODE_EN(comp1)) {
		dev_dbg(dev->dev, "designware: i2s master mode supported\n");
		dev->capability |= DW_I2S_MASTER;
	} else {
		dev_dbg(dev->dev, "designware: i2s slave mode supported\n");
		dev->capability |= DW_I2S_SLAVE;
	}

	dev->fifo_th = fifo_depth / 2;
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

	if (dev->is_jh7110) {
		/* Use platform data and snd_dmaengine_dai_dma_data struct at the same time */
		u32 comp2 = i2s_read_reg(dev->i2s_base, I2S_COMP_PARAM_2);
		u32 idx2;

		if (COMP1_TX_ENABLED(comp1)) {
			idx2 = COMP1_TX_WORDSIZE_0(comp1);
			dev->play_dma_data.dt.addr = res->start + I2S_TXDMA;
			dev->play_dma_data.dt.fifo_size = dev->fifo_th * 2 *
				(fifo_width[idx2]) >> 8;
			dev->play_dma_data.dt.maxburst = 16;
		}
		if (COMP1_RX_ENABLED(comp1)) {
			idx2 = COMP2_RX_WORDSIZE_0(comp2);
			dev->capture_dma_data.dt.addr = res->start + I2S_RXDMA;
			dev->capture_dma_data.dt.fifo_size = dev->fifo_th * 2 *
				(fifo_width[idx2] >> 8);
			dev->capture_dma_data.dt.maxburst = 16;
		}
	} else {
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
	}

	return 0;
}

static int dw_configure_dai_by_dt(struct dw_i2s_dev *dev,
				   struct snd_soc_dai_driver *dw_i2s_dai,
				   struct resource *res)
{
	u32 comp1 = i2s_read_reg(dev->i2s_base, I2S_COMP_PARAM_1);
	u32 comp2 = i2s_read_reg(dev->i2s_base, I2S_COMP_PARAM_2);
	u32 fifo_depth = 1 << (1 + COMP1_FIFO_DEPTH_GLOBAL(comp1));
	u32 idx2;
	int ret;

	ret = dw_configure_dai(dev, dw_i2s_dai, SNDRV_PCM_RATE_8000_192000);
	if (ret < 0)
		return ret;

	if (COMP1_TX_ENABLED(comp1)) {
		idx2 = COMP1_TX_WORDSIZE_0(comp1);

		dev->capability |= DWC_I2S_PLAY;
		dev->play_dma_data.dt.addr = res->start + I2S_TXDMA;
		dev->play_dma_data.dt.fifo_size = fifo_depth *
			(fifo_width[idx2]) >> 8;
		dev->play_dma_data.dt.maxburst = 16;
	}
	if (COMP1_RX_ENABLED(comp1)) {
		idx2 = COMP2_RX_WORDSIZE_0(comp2);

		dev->capability |= DWC_I2S_RECORD;
		dev->capture_dma_data.dt.addr = res->start + I2S_RXDMA;
		dev->capture_dma_data.dt.fifo_size = fifo_depth *
			(fifo_width[idx2] >> 8);
		dev->capture_dma_data.dt.maxburst = 16;
	}

	return 0;

}

#ifdef CONFIG_OF
/* clocks initialization with master mode on JH7110 SoC */
static int jh7110_i2s_crg_master_init(struct dw_i2s_dev *dev)
{
	static struct clk_bulk_data clks[] = {
		{ .id = "mclk" },
		{ .id = "mclk_ext" },
		{ .id = "mclk_inner" },
		{ .id = "apb" },
		{ .id = "i2sclk" },
	};
	struct reset_control *resets = devm_reset_control_array_get_exclusive(dev->dev);
	int ret;
	struct clk *pclk;
	struct clk *bclk_mst;
	struct clk *mclk;
	struct clk *mclk_ext;
	struct clk *mclk_inner;

	if (IS_ERR(resets))
		return dev_err_probe(dev->dev, PTR_ERR(resets), "failed to get i2s resets\n");

	ret = clk_bulk_get(dev->dev, ARRAY_SIZE(clks), clks);
	if (ret)
		return dev_err_probe(dev->dev, ret, "failed to get i2s clocks\n");

	mclk = clks[0].clk;
	mclk_ext = clks[1].clk;
	mclk_inner = clks[2].clk;
	pclk = clks[3].clk;
	bclk_mst = clks[4].clk;

	ret = clk_prepare_enable(pclk);
	if (ret)
		goto exit;

	/* Use inner mclk first and avoid uninitialized gpio for external mclk */
	ret = clk_set_parent(mclk, mclk_inner);
	if (ret)
		goto err_dis_pclk;

	ret = clk_prepare_enable(bclk_mst);
	if (ret)
		goto err_dis_pclk;

	/* deassert resets before set clock parent */
	ret = reset_control_deassert(resets);
	if (ret)
		goto err_dis_all;

	/* external clock (12.288MHz) for Audio */
	ret = clk_set_parent(mclk, mclk_ext);
	if (ret)
		goto err_dis_all;

	/* i2sclk will be got and enabled repeatedly later and should be disabled now. */
	clk_disable_unprepare(bclk_mst);
	clk_bulk_put(ARRAY_SIZE(clks), clks);
	dev->is_jh7110 = true;

	return 0;

err_dis_all:
	clk_disable_unprepare(bclk_mst);
err_dis_pclk:
	clk_disable_unprepare(pclk);
exit:
	clk_bulk_put(ARRAY_SIZE(clks), clks);
	return ret;
}

/* clocks initialization with slave mode on JH7110 SoC */
static int jh7110_i2s_crg_slave_init(struct dw_i2s_dev *dev)
{
	static struct clk_bulk_data clks[] = {
		{ .id = "mclk" },
		{ .id = "mclk_ext" },
		{ .id = "apb" },
		{ .id = "bclk_ext" },
		{ .id = "lrck_ext" },
		{ .id = "bclk" },
		{ .id = "lrck" },
		{ .id = "mclk_inner" },
		{ .id = "i2sclk" },
	};
	struct reset_control *resets = devm_reset_control_array_get_exclusive(dev->dev);
	int ret;
	struct clk *pclk;
	struct clk *bclk_mst;
	struct clk *bclk_ext;
	struct clk *lrck_ext;
	struct clk *bclk;
	struct clk *lrck;
	struct clk *mclk;
	struct clk *mclk_ext;
	struct clk *mclk_inner;

	if (IS_ERR(resets))
		return dev_err_probe(dev->dev, PTR_ERR(resets), "failed to get i2s resets\n");

	ret = clk_bulk_get(dev->dev, ARRAY_SIZE(clks), clks);
	if (ret)
		return dev_err_probe(dev->dev, ret, "failed to get i2s clocks\n");

	mclk = clks[0].clk;
	mclk_ext = clks[1].clk;
	pclk = clks[2].clk;
	bclk_ext = clks[3].clk;
	lrck_ext = clks[4].clk;
	bclk = clks[5].clk;
	lrck = clks[6].clk;
	mclk_inner = clks[7].clk;
	bclk_mst = clks[8].clk;

	ret = clk_prepare_enable(pclk);
	if (ret)
		goto exit;

	ret = clk_set_parent(mclk, mclk_inner);
	if (ret)
		goto err_dis_pclk;

	ret = clk_prepare_enable(bclk_mst);
	if (ret)
		goto err_dis_pclk;

	ret = reset_control_deassert(resets);
	if (ret)
		goto err_dis_all;

	/* The sources of BCLK and LRCK are the external codec. */
	ret = clk_set_parent(bclk, bclk_ext);
	if (ret)
		goto err_dis_all;

	ret = clk_set_parent(lrck, lrck_ext);
	if (ret)
		goto err_dis_all;

	ret = clk_set_parent(mclk, mclk_ext);
	if (ret)
		goto err_dis_all;

	/* The i2sclk will be got and enabled repeatedly later and should be disabled now. */
	clk_disable_unprepare(bclk_mst);
	clk_bulk_put(ARRAY_SIZE(clks), clks);
	dev->is_jh7110 = true;

	return 0;

err_dis_all:
	clk_disable_unprepare(bclk_mst);
err_dis_pclk:
	clk_disable_unprepare(pclk);
exit:
	clk_bulk_put(ARRAY_SIZE(clks), clks);
	return ret;
}

/* Special syscon initialization about RX channel with slave mode on JH7110 SoC */
static int jh7110_i2srx_crg_init(struct dw_i2s_dev *dev)
{
	struct regmap *regmap;
	unsigned int args[2];

	regmap = syscon_regmap_lookup_by_phandle_args(dev->dev->of_node,
						      "starfive,syscon",
						      2, args);
	if (IS_ERR(regmap))
		return dev_err_probe(dev->dev, PTR_ERR(regmap), "getting the regmap failed\n");

	/* Enable I2Srx with syscon register, args[0]: offset, args[1]: mask */
	regmap_update_bits(regmap, args[0], args[1], args[1]);

	return jh7110_i2s_crg_slave_init(dev);
}

static int jh7110_i2stx0_clk_cfg(struct i2s_clk_config_data *config)
{
	struct dw_i2s_dev *dev = container_of(config, struct dw_i2s_dev, config);
	u32 bclk_rate = config->sample_rate * 64;

	return clk_set_rate(dev->clk, bclk_rate);
}
#endif /* CONFIG_OF */

static int dw_i2s_probe(struct platform_device *pdev)
{
	const struct i2s_platform_data *pdata = pdev->dev.platform_data;
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

	dev->i2s_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(dev->i2s_base))
		return PTR_ERR(dev->i2s_base);

	dev->dev = &pdev->dev;
	dev->is_jh7110 = false;
	if (pdata) {
		if (pdata->i2s_pd_init) {
			ret = pdata->i2s_pd_init(dev);
			if (ret)
				return ret;
		}
	}

	if (!dev->is_jh7110) {
		dev->reset = devm_reset_control_array_get_optional_shared(&pdev->dev);
		if (IS_ERR(dev->reset))
			return PTR_ERR(dev->reset);

		ret = reset_control_deassert(dev->reset);
		if (ret)
			return ret;
	}

	irq = platform_get_irq_optional(pdev, 0);
	if (irq >= 0) {
		ret = devm_request_irq(&pdev->dev, irq, i2s_irq_handler, 0,
				pdev->name, dev);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request irq\n");
			goto err_assert_reset;
		}
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
		clk_id = "i2sclk";
		ret = dw_configure_dai_by_dt(dev, dw_i2s_dai, res);
	}
	if (ret < 0)
		goto err_assert_reset;

	if (dev->capability & DW_I2S_MASTER) {
		if (pdata) {
			dev->i2s_clk_cfg = pdata->i2s_clk_cfg;
			if (!dev->i2s_clk_cfg) {
				dev_err(&pdev->dev, "no clock configure method\n");
				ret = -ENODEV;
				goto err_assert_reset;
			}
		}
		dev->clk = devm_clk_get_enabled(&pdev->dev, clk_id);

		if (IS_ERR(dev->clk)) {
			ret = PTR_ERR(dev->clk);
			goto err_assert_reset;
		}
	}

	dev_set_drvdata(&pdev->dev, dev);
	ret = devm_snd_soc_register_component(&pdev->dev, &dw_i2s_component,
					 dw_i2s_dai, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "not able to register dai\n");
		goto err_assert_reset;
	}

	if (!pdata || dev->is_jh7110) {
		if (irq >= 0) {
			ret = dw_pcm_register(pdev);
			dev->use_pio = true;
			dev->l_reg = LRBR_LTHR(0);
			dev->r_reg = RRBR_RTHR(0);
		} else {
			ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL,
					0);
			dev->use_pio = false;
		}

		if (ret) {
			dev_err(&pdev->dev, "could not register pcm: %d\n",
					ret);
			goto err_assert_reset;
		}
	}

	pm_runtime_enable(&pdev->dev);
	return 0;

err_assert_reset:
	reset_control_assert(dev->reset);
	return ret;
}

static void dw_i2s_remove(struct platform_device *pdev)
{
	struct dw_i2s_dev *dev = dev_get_drvdata(&pdev->dev);

	reset_control_assert(dev->reset);
	pm_runtime_disable(&pdev->dev);
}

#ifdef CONFIG_OF
static const struct i2s_platform_data jh7110_i2stx0_data = {
	.cap = DWC_I2S_PLAY | DW_I2S_MASTER,
	.channel = TWO_CHANNEL_SUPPORT,
	.snd_fmts = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.snd_rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000,
	.i2s_clk_cfg = jh7110_i2stx0_clk_cfg,
	.i2s_pd_init = jh7110_i2s_crg_master_init,
};

static const struct i2s_platform_data jh7110_i2stx1_data = {
	.cap = DWC_I2S_PLAY | DW_I2S_SLAVE,
	.channel = TWO_CHANNEL_SUPPORT,
	.snd_fmts = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.snd_rates = SNDRV_PCM_RATE_8000_192000,
	.i2s_pd_init = jh7110_i2s_crg_slave_init,
};

static const struct i2s_platform_data jh7110_i2srx_data = {
	.cap = DWC_I2S_RECORD | DW_I2S_SLAVE,
	.channel = TWO_CHANNEL_SUPPORT,
	.snd_fmts = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.snd_rates = SNDRV_PCM_RATE_8000_192000,
	.i2s_pd_init = jh7110_i2srx_crg_init,
};

static const struct of_device_id dw_i2s_of_match[] = {
	{ .compatible = "snps,designware-i2s",	 },
	{ .compatible = "starfive,jh7110-i2stx0", .data = &jh7110_i2stx0_data, },
	{ .compatible = "starfive,jh7110-i2stx1", .data = &jh7110_i2stx1_data,},
	{ .compatible = "starfive,jh7110-i2srx", .data = &jh7110_i2srx_data,},
	{},
};

MODULE_DEVICE_TABLE(of, dw_i2s_of_match);
#endif

static const struct dev_pm_ops dwc_pm_ops = {
	RUNTIME_PM_OPS(dw_i2s_runtime_suspend, dw_i2s_runtime_resume, NULL)
};

static struct platform_driver dw_i2s_driver = {
	.probe		= dw_i2s_probe,
	.remove		= dw_i2s_remove,
	.driver		= {
		.name	= "designware-i2s",
		.of_match_table = of_match_ptr(dw_i2s_of_match),
		.pm = pm_ptr(&dwc_pm_ops),
	},
};

module_platform_driver(dw_i2s_driver);

MODULE_AUTHOR("Rajeev Kumar <rajeevkumar.linux@gmail.com>");
MODULE_DESCRIPTION("DESIGNWARE I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:designware_i2s");

// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (C) 2020 Intel Corporation.
//
// Intel KeemBay Platform driver.
//

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "kmb_platform.h"

#define PERIODS_MIN		2
#define PERIODS_MAX		48
#define PERIOD_BYTES_MIN	4096
#define BUFFER_BYTES_MAX	(PERIODS_MAX * PERIOD_BYTES_MIN)
#define TDM_OPERATION		5
#define I2S_OPERATION		0
#define DATA_WIDTH_CONFIG_BIT	6
#define TDM_CHANNEL_CONFIG_BIT	3

static const struct snd_pcm_hardware kmb_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.rates = SNDRV_PCM_RATE_8000 |
		 SNDRV_PCM_RATE_16000 |
		 SNDRV_PCM_RATE_48000,
	.rate_min = 8000,
	.rate_max = 48000,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = BUFFER_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = BUFFER_BYTES_MAX / PERIODS_MIN,
	.periods_min = PERIODS_MIN,
	.periods_max = PERIODS_MAX,
	.fifo_size = 16,
};

static unsigned int kmb_pcm_tx_fn(struct kmb_i2s_info *kmb_i2s,
				  struct snd_pcm_runtime *runtime,
				  unsigned int tx_ptr, bool *period_elapsed)
{
	unsigned int period_pos = tx_ptr % runtime->period_size;
	void __iomem *i2s_base = kmb_i2s->i2s_base;
	void *buf = runtime->dma_area;
	int i;

	/* KMB i2s uses two separate L/R FIFO */
	for (i = 0; i < kmb_i2s->fifo_th; i++) {
		if (kmb_i2s->config.data_width == 16) {
			writel(((u16(*)[2])buf)[tx_ptr][0], i2s_base + LRBR_LTHR(0));
			writel(((u16(*)[2])buf)[tx_ptr][1], i2s_base + RRBR_RTHR(0));
		} else {
			writel(((u32(*)[2])buf)[tx_ptr][0], i2s_base + LRBR_LTHR(0));
			writel(((u32(*)[2])buf)[tx_ptr][1], i2s_base + RRBR_RTHR(0));
		}

		period_pos++;

		if (++tx_ptr >= runtime->buffer_size)
			tx_ptr = 0;
	}

	*period_elapsed = period_pos >= runtime->period_size;

	return tx_ptr;
}

static unsigned int kmb_pcm_rx_fn(struct kmb_i2s_info *kmb_i2s,
				  struct snd_pcm_runtime *runtime,
				  unsigned int rx_ptr, bool *period_elapsed)
{
	unsigned int period_pos = rx_ptr % runtime->period_size;
	void __iomem *i2s_base = kmb_i2s->i2s_base;
	int chan = kmb_i2s->config.chan_nr;
	void *buf = runtime->dma_area;
	int i, j;

	/* KMB i2s uses two separate L/R FIFO */
	for (i = 0; i < kmb_i2s->fifo_th; i++) {
		for (j = 0; j < chan / 2; j++) {
			if (kmb_i2s->config.data_width == 16) {
				((u16 *)buf)[rx_ptr * chan + (j * 2)] =
						readl(i2s_base + LRBR_LTHR(j));
				((u16 *)buf)[rx_ptr * chan + ((j * 2) + 1)] =
						readl(i2s_base + RRBR_RTHR(j));
			} else {
				((u32 *)buf)[rx_ptr * chan + (j * 2)] =
						readl(i2s_base + LRBR_LTHR(j));
				((u32 *)buf)[rx_ptr * chan + ((j * 2) + 1)] =
						readl(i2s_base + RRBR_RTHR(j));
			}
		}
		period_pos++;

		if (++rx_ptr >= runtime->buffer_size)
			rx_ptr = 0;
	}

	*period_elapsed = period_pos >= runtime->period_size;

	return rx_ptr;
}

static inline void kmb_i2s_disable_channels(struct kmb_i2s_info *kmb_i2s,
					    u32 stream)
{
	u32 i;

	/* Disable all channels regardless of configuration*/
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < MAX_ISR; i++)
			writel(0, kmb_i2s->i2s_base + TER(i));
	} else {
		for (i = 0; i < MAX_ISR; i++)
			writel(0, kmb_i2s->i2s_base + RER(i));
	}
}

static inline void kmb_i2s_clear_irqs(struct kmb_i2s_info *kmb_i2s, u32 stream)
{
	struct i2s_clk_config_data *config = &kmb_i2s->config;
	u32 i;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < config->chan_nr / 2; i++)
			readl(kmb_i2s->i2s_base + TOR(i));
	} else {
		for (i = 0; i < config->chan_nr / 2; i++)
			readl(kmb_i2s->i2s_base + ROR(i));
	}
}

static inline void kmb_i2s_irq_trigger(struct kmb_i2s_info *kmb_i2s,
				       u32 stream, int chan_nr, bool trigger)
{
	u32 i, irq;
	u32 flag;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		flag = TX_INT_FLAG;
	else
		flag = RX_INT_FLAG;

	for (i = 0; i < chan_nr / 2; i++) {
		irq = readl(kmb_i2s->i2s_base + IMR(i));

		if (trigger)
			irq = irq & ~flag;
		else
			irq = irq | flag;

		writel(irq, kmb_i2s->i2s_base + IMR(i));
	}
}

static void kmb_pcm_operation(struct kmb_i2s_info *kmb_i2s, bool playback)
{
	struct snd_pcm_substream *substream;
	bool period_elapsed;
	unsigned int new_ptr;
	unsigned int ptr;

	if (playback)
		substream = kmb_i2s->tx_substream;
	else
		substream = kmb_i2s->rx_substream;

	if (!substream || !snd_pcm_running(substream))
		return;

	if (playback) {
		ptr = kmb_i2s->tx_ptr;
		new_ptr = kmb_pcm_tx_fn(kmb_i2s, substream->runtime,
					ptr, &period_elapsed);
		cmpxchg(&kmb_i2s->tx_ptr, ptr, new_ptr);
	} else {
		ptr = kmb_i2s->rx_ptr;
		new_ptr = kmb_pcm_rx_fn(kmb_i2s, substream->runtime,
					ptr, &period_elapsed);
		cmpxchg(&kmb_i2s->rx_ptr, ptr, new_ptr);
	}

	if (period_elapsed)
		snd_pcm_period_elapsed(substream);
}

static int kmb_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct kmb_i2s_info *kmb_i2s;

	kmb_i2s = snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));
	snd_soc_set_runtime_hwparams(substream, &kmb_pcm_hardware);
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	runtime->private_data = kmb_i2s;

	return 0;
}

static int kmb_pcm_trigger(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct kmb_i2s_info *kmb_i2s = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			kmb_i2s->tx_ptr = 0;
			kmb_i2s->tx_substream = substream;
		} else {
			kmb_i2s->rx_ptr = 0;
			kmb_i2s->rx_substream = substream;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			kmb_i2s->tx_substream = NULL;
		else
			kmb_i2s->rx_substream = NULL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t kmb_i2s_irq_handler(int irq, void *dev_id)
{
	struct kmb_i2s_info *kmb_i2s = dev_id;
	struct i2s_clk_config_data *config = &kmb_i2s->config;
	irqreturn_t ret = IRQ_NONE;
	u32 tx_enabled = 0;
	u32 isr[4];
	int i;

	for (i = 0; i < config->chan_nr / 2; i++)
		isr[i] = readl(kmb_i2s->i2s_base + ISR(i));

	kmb_i2s_clear_irqs(kmb_i2s, SNDRV_PCM_STREAM_PLAYBACK);
	kmb_i2s_clear_irqs(kmb_i2s, SNDRV_PCM_STREAM_CAPTURE);
	/* Only check TX interrupt if TX is active */
	tx_enabled = readl(kmb_i2s->i2s_base + ITER);

	/*
	 * Data available. Retrieve samples from FIFO
	 */

	/*
	 * 8 channel audio will have isr[0..2] triggered,
	 * reading the specific isr based on the audio configuration,
	 * to avoid reading the buffers too early.
	 */
	switch (config->chan_nr) {
	case 2:
		if (isr[0] & ISR_RXDA)
			kmb_pcm_operation(kmb_i2s, false);
		ret = IRQ_HANDLED;
		break;
	case 4:
		if (isr[1] & ISR_RXDA)
			kmb_pcm_operation(kmb_i2s, false);
		ret = IRQ_HANDLED;
		break;
	case 8:
		if (isr[3] & ISR_RXDA)
			kmb_pcm_operation(kmb_i2s, false);
		ret = IRQ_HANDLED;
		break;
	}

	for (i = 0; i < config->chan_nr / 2; i++) {
		/*
		 * Check if TX fifo is empty. If empty fill FIFO with samples
		 */
		if ((isr[i] & ISR_TXFE) && tx_enabled) {
			kmb_pcm_operation(kmb_i2s, true);
			ret = IRQ_HANDLED;
		}

		/* Error Handling: TX */
		if (isr[i] & ISR_TXFO) {
			dev_dbg(kmb_i2s->dev, "TX overrun (ch_id=%d)\n", i);
			ret = IRQ_HANDLED;
		}
		/* Error Handling: RX */
		if (isr[i] & ISR_RXFO) {
			dev_dbg(kmb_i2s->dev, "RX overrun (ch_id=%d)\n", i);
			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

static int kmb_platform_pcm_new(struct snd_soc_component *component,
				struct snd_soc_pcm_runtime *soc_runtime)
{
	size_t size = kmb_pcm_hardware.buffer_bytes_max;
	/* Use SNDRV_DMA_TYPE_CONTINUOUS as KMB doesn't use PCI sg buffer */
	snd_pcm_set_managed_buffer_all(soc_runtime->pcm,
				       SNDRV_DMA_TYPE_CONTINUOUS,
				       NULL, size, size);
	return 0;
}

static snd_pcm_uframes_t kmb_pcm_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct kmb_i2s_info *kmb_i2s = runtime->private_data;
	snd_pcm_uframes_t pos;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		pos = kmb_i2s->tx_ptr;
	else
		pos = kmb_i2s->rx_ptr;

	return pos < runtime->buffer_size ? pos : 0;
}

static const struct snd_soc_component_driver kmb_component = {
	.name		= "kmb",
	.pcm_construct	= kmb_platform_pcm_new,
	.open		= kmb_pcm_open,
	.trigger	= kmb_pcm_trigger,
	.pointer	= kmb_pcm_pointer,
};

static void kmb_i2s_start(struct kmb_i2s_info *kmb_i2s,
			  struct snd_pcm_substream *substream)
{
	struct i2s_clk_config_data *config = &kmb_i2s->config;

	/* I2S Programming sequence in Keem_Bay_VPU_DB_v1.1 */
	writel(1, kmb_i2s->i2s_base + IER);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		writel(1, kmb_i2s->i2s_base + ITER);
	else
		writel(1, kmb_i2s->i2s_base + IRER);

	kmb_i2s_irq_trigger(kmb_i2s, substream->stream, config->chan_nr, true);

	if (kmb_i2s->master)
		writel(1, kmb_i2s->i2s_base + CER);
	else
		writel(0, kmb_i2s->i2s_base + CER);
}

static void kmb_i2s_stop(struct kmb_i2s_info *kmb_i2s,
			 struct snd_pcm_substream *substream)
{
	/* I2S Programming sequence in Keem_Bay_VPU_DB_v1.1 */
	kmb_i2s_clear_irqs(kmb_i2s, substream->stream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		writel(0, kmb_i2s->i2s_base + ITER);
	else
		writel(0, kmb_i2s->i2s_base + IRER);

	kmb_i2s_irq_trigger(kmb_i2s, substream->stream, 8, false);

	if (!kmb_i2s->active) {
		writel(0, kmb_i2s->i2s_base + CER);
		writel(0, kmb_i2s->i2s_base + IER);
	}
}

static void kmb_disable_clk(void *clk)
{
	clk_disable_unprepare(clk);
}

static int kmb_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct kmb_i2s_info *kmb_i2s = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		kmb_i2s->master = false;
		ret = 0;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		writel(MASTER_MODE, kmb_i2s->pss_base + I2S_GEN_CFG_0);

		ret = clk_prepare_enable(kmb_i2s->clk_i2s);
		if (ret < 0)
			return ret;

		ret = devm_add_action_or_reset(kmb_i2s->dev, kmb_disable_clk,
					       kmb_i2s->clk_i2s);
		if (ret)
			return ret;

		kmb_i2s->master = true;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int kmb_dai_trigger(struct snd_pcm_substream *substream,
			   int cmd, struct snd_soc_dai *cpu_dai)
{
	struct kmb_i2s_info *kmb_i2s  = snd_soc_dai_get_drvdata(cpu_dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* Keep track of i2s activity before turn off
		 * the i2s interface
		 */
		kmb_i2s->active++;
		kmb_i2s_start(kmb_i2s, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		kmb_i2s->active--;
		kmb_i2s_stop(kmb_i2s, substream);
		break;
	default:
		return  -EINVAL;
	}

	return 0;
}

static void kmb_i2s_config(struct kmb_i2s_info *kmb_i2s, int stream)
{
	struct i2s_clk_config_data *config = &kmb_i2s->config;
	u32 ch_reg;

	kmb_i2s_disable_channels(kmb_i2s, stream);

	for (ch_reg = 0; ch_reg < config->chan_nr / 2; ch_reg++) {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
			writel(kmb_i2s->xfer_resolution,
			       kmb_i2s->i2s_base + TCR(ch_reg));

			writel(kmb_i2s->fifo_th - 1,
			       kmb_i2s->i2s_base + TFCR(ch_reg));

			writel(1, kmb_i2s->i2s_base + TER(ch_reg));
		} else {
			writel(kmb_i2s->xfer_resolution,
			       kmb_i2s->i2s_base + RCR(ch_reg));

			writel(kmb_i2s->fifo_th - 1,
			       kmb_i2s->i2s_base + RFCR(ch_reg));

			writel(1, kmb_i2s->i2s_base + RER(ch_reg));
		}
	}
}

static int kmb_dai_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hw_params,
			     struct snd_soc_dai *cpu_dai)
{
	struct kmb_i2s_info *kmb_i2s = snd_soc_dai_get_drvdata(cpu_dai);
	struct i2s_clk_config_data *config = &kmb_i2s->config;
	u32 write_val;
	int ret;

	switch (params_format(hw_params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		config->data_width = 16;
		kmb_i2s->ccr = 0x00;
		kmb_i2s->xfer_resolution = 0x02;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		config->data_width = 24;
		kmb_i2s->ccr = 0x08;
		kmb_i2s->xfer_resolution = 0x04;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		config->data_width = 32;
		kmb_i2s->ccr = 0x10;
		kmb_i2s->xfer_resolution = 0x05;
		break;
	default:
		dev_err(kmb_i2s->dev, "kmb: unsupported PCM fmt");
		return -EINVAL;
	}

	config->chan_nr = params_channels(hw_params);

	switch (config->chan_nr) {
	case 8:
	case 4:
		/*
		 * Platform is not capable of providing clocks for
		 * multi channel audio
		 */
		if (kmb_i2s->master)
			return -EINVAL;

		write_val = ((config->chan_nr / 2) << TDM_CHANNEL_CONFIG_BIT) |
				(config->data_width << DATA_WIDTH_CONFIG_BIT) |
				TDM_OPERATION;

		writel(write_val, kmb_i2s->pss_base + I2S_GEN_CFG_0);
		break;
	case 2:
		/*
		 * Platform is only capable of providing clocks need for
		 * 2 channel master mode
		 */
		if (!(kmb_i2s->master))
			return -EINVAL;

		write_val = ((config->chan_nr / 2) << TDM_CHANNEL_CONFIG_BIT) |
				(config->data_width << DATA_WIDTH_CONFIG_BIT) |
				MASTER_MODE | I2S_OPERATION;

		writel(write_val, kmb_i2s->pss_base + I2S_GEN_CFG_0);
		break;
	default:
		dev_dbg(kmb_i2s->dev, "channel not supported\n");
		return -EINVAL;
	}

	kmb_i2s_config(kmb_i2s, substream->stream);

	writel(kmb_i2s->ccr, kmb_i2s->i2s_base + CCR);

	config->sample_rate = params_rate(hw_params);

	if (kmb_i2s->master) {
		/* Only 2 ch supported in Master mode */
		u32 bitclk = config->sample_rate * config->data_width * 2;

		ret = clk_set_rate(kmb_i2s->clk_i2s, bitclk);
		if (ret) {
			dev_err(kmb_i2s->dev,
				"Can't set I2S clock rate: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int kmb_dai_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *cpu_dai)
{
	struct kmb_i2s_info *kmb_i2s = snd_soc_dai_get_drvdata(cpu_dai);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		writel(1, kmb_i2s->i2s_base + TXFFR);
	else
		writel(1, kmb_i2s->i2s_base + RXFFR);

	return 0;
}

static struct snd_soc_dai_ops kmb_dai_ops = {
	.trigger	= kmb_dai_trigger,
	.hw_params	= kmb_dai_hw_params,
	.prepare	= kmb_dai_prepare,
	.set_fmt	= kmb_set_dai_fmt,
};

static struct snd_soc_dai_driver intel_kmb_i2s_dai[] = {
	{
		.name = "intel_kmb_i2s",
		.playback = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000,
			.rate_min = 8000,
			.rate_max = 48000,
			.formats = (SNDRV_PCM_FMTBIT_S32_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S16_LE),
		},
		.capture = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000,
			.rate_min = 8000,
			.rate_max = 48000,
			.formats = (SNDRV_PCM_FMTBIT_S32_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S16_LE),
		},
		.ops = &kmb_dai_ops,
	},
};

static struct snd_soc_dai_driver intel_kmb_tdm_dai[] = {
	{
		.name = "intel_kmb_tdm",
		.capture = {
			.channels_min = 4,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000,
			.rate_min = 8000,
			.rate_max = 48000,
			.formats = (SNDRV_PCM_FMTBIT_S32_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S16_LE),
		},
		.ops = &kmb_dai_ops,
	},
};

static const struct of_device_id kmb_plat_of_match[] = {
	{ .compatible = "intel,keembay-i2s", .data = &intel_kmb_i2s_dai},
	{ .compatible = "intel,keembay-tdm", .data = &intel_kmb_tdm_dai},
	{}
};

static int kmb_plat_dai_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_driver *kmb_i2s_dai;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct kmb_i2s_info *kmb_i2s;
	int ret, irq;
	u32 comp1_reg;

	kmb_i2s = devm_kzalloc(dev, sizeof(*kmb_i2s), GFP_KERNEL);
	if (!kmb_i2s)
		return -ENOMEM;

	kmb_i2s_dai = devm_kzalloc(dev, sizeof(*kmb_i2s_dai), GFP_KERNEL);
	if (!kmb_i2s_dai)
		return -ENOMEM;

	match = of_match_device(kmb_plat_of_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Error: No device match found\n");
		return -ENODEV;
	}
	kmb_i2s_dai = (struct snd_soc_dai_driver *) match->data;

	/* Prepare the related clocks */
	kmb_i2s->clk_apb = devm_clk_get(dev, "apb_clk");
	if (IS_ERR(kmb_i2s->clk_apb)) {
		dev_err(dev, "Failed to get apb clock\n");
		return PTR_ERR(kmb_i2s->clk_apb);
	}

	ret = clk_prepare_enable(kmb_i2s->clk_apb);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, kmb_disable_clk, kmb_i2s->clk_apb);
	if (ret) {
		dev_err(dev, "Failed to add clk_apb reset action\n");
		return ret;
	}

	kmb_i2s->clk_i2s = devm_clk_get(dev, "osc");
	if (IS_ERR(kmb_i2s->clk_i2s)) {
		dev_err(dev, "Failed to get osc clock\n");
		return PTR_ERR(kmb_i2s->clk_i2s);
	}

	kmb_i2s->i2s_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(kmb_i2s->i2s_base))
		return PTR_ERR(kmb_i2s->i2s_base);

	kmb_i2s->pss_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(kmb_i2s->pss_base))
		return PTR_ERR(kmb_i2s->pss_base);

	kmb_i2s->dev = &pdev->dev;

	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		ret = devm_request_irq(dev, irq, kmb_i2s_irq_handler, 0,
				       pdev->name, kmb_i2s);
		if (ret < 0) {
			dev_err(dev, "failed to request irq\n");
			return ret;
		}
	}

	comp1_reg = readl(kmb_i2s->i2s_base + I2S_COMP_PARAM_1);

	kmb_i2s->fifo_th = (1 << COMP1_FIFO_DEPTH(comp1_reg)) / 2;

	ret = devm_snd_soc_register_component(dev, &kmb_component,
					      kmb_i2s_dai, 1);
	if (ret) {
		dev_err(dev, "not able to register dai\n");
		return ret;
	}

	/* To ensure none of the channels are enabled at boot up */
	kmb_i2s_disable_channels(kmb_i2s, SNDRV_PCM_STREAM_PLAYBACK);
	kmb_i2s_disable_channels(kmb_i2s, SNDRV_PCM_STREAM_CAPTURE);

	dev_set_drvdata(dev, kmb_i2s);

	return ret;
}

static struct platform_driver kmb_plat_dai_driver = {
	.driver		= {
		.name		= "kmb-plat-dai",
		.of_match_table = kmb_plat_of_match,
	},
	.probe		= kmb_plat_dai_probe,
};

module_platform_driver(kmb_plat_dai_driver);

MODULE_DESCRIPTION("ASoC Intel KeemBay Platform driver");
MODULE_AUTHOR("Sia Jee Heng <jee.heng.sia@intel.com>");
MODULE_AUTHOR("Sit, Michael Wei Hong <michael.wei.hong.sit@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:kmb_platform");

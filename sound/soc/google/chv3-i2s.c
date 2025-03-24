// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

/*
 * The I2S interface consists of two ring buffers - one for RX and one for
 * TX.  A ring buffer has a producer index and a consumer index. Depending
 * on which way the data is flowing, either the software or the hardware
 * writes data and updates the producer index, and the other end reads data
 * and updates the consumer index.
 *
 * The pointer managed by software is updated using the .ack callback
 * (see chv3_dma_ack). This seems to be the only way to reliably obtain
 * the appl_ptr from within the driver and pass it to hardware.
 *
 * Because of the two pointer design, the ring buffer can never be full. With
 * capture this isn't a problem, because the hardware being the producer
 * will wait for the consumer index to move out of the way.  With playback,
 * however, this is problematic, because ALSA wants to fill up the buffer
 * completely when waiting for hardware. In the .ack callback, the driver
 * would have to wait for the consumer index to move out of the way by
 * busy-waiting, which would keep stalling the kernel for quite a long time.
 *
 * The workaround to this problem is to "lie" to ALSA that the hw_pointer
 * is one frame behind what it actually is (see chv3_dma_pointer). This
 * way, ALSA will not try to fill up the entire buffer, and all callbacks
 * are wait-free.
 */

#define I2S_TX_ENABLE		0x00
#define I2S_TX_BASE_ADDR	0x04
#define I2S_TX_BUFFER_SIZE	0x08
#define I2S_TX_PRODUCER_IDX	0x0c
#define I2S_TX_CONSUMER_IDX	0x10
#define I2S_RX_ENABLE		0x14
#define I2S_RX_BASE_ADDR	0x18
#define I2S_RX_BUFFER_SIZE	0x1c
#define I2S_RX_PRODUCER_IDX	0x20
#define I2S_RX_CONSUMER_IDX	0x24

#define I2S_SOFT_RESET		0x2c
#define I2S_SOFT_RESET_RX_BIT	0x1
#define I2S_SOFT_RESET_TX_BIT	0x2

#define I2S_RX_IRQ		0x4c
#define I2S_RX_IRQ_CONST	0x50
#define I2S_TX_IRQ		0x54
#define I2S_TX_IRQ_CONST	0x58

#define I2S_IRQ_MASK	0x8
#define I2S_IRQ_CLR	0xc
#define I2S_IRQ_RX_BIT	0x1
#define I2S_IRQ_TX_BIT	0x2

#define I2S_MAX_BUFFER_SIZE	0x200000

struct chv3_i2s_dev {
	struct device *dev;
	void __iomem *iobase;
	void __iomem *iobase_irq;
	struct snd_pcm_substream *rx_substream;
	struct snd_pcm_substream *tx_substream;
	int tx_bytes_to_fetch;
};

static struct snd_soc_dai_driver chv3_i2s_dai = {
	.name = "chv3-i2s",
	.capture = {
		.channels_min = 1,
		.channels_max = 128,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 8000,
		.rate_max = 96000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
	},
	.playback = {
		.channels_min = 1,
		.channels_max = 128,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 8000,
		.rate_max = 96000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
	},
};

static const struct snd_pcm_hardware chv3_dma_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.buffer_bytes_max = I2S_MAX_BUFFER_SIZE,
	.period_bytes_min = 64,
	.period_bytes_max = 8192,
	.periods_min = 4,
	.periods_max = 256,
};

static inline void chv3_i2s_wr(struct chv3_i2s_dev *i2s, int offset, u32 val)
{
	writel(val, i2s->iobase + offset);
}

static inline u32 chv3_i2s_rd(struct chv3_i2s_dev *i2s, int offset)
{
	return readl(i2s->iobase + offset);
}

static irqreturn_t chv3_i2s_isr(int irq, void *data)
{
	struct chv3_i2s_dev *i2s = data;
	u32 reg;

	reg = readl(i2s->iobase_irq + I2S_IRQ_CLR);
	if (!reg)
		return IRQ_NONE;

	if (reg & I2S_IRQ_RX_BIT)
		snd_pcm_period_elapsed(i2s->rx_substream);

	if (reg & I2S_IRQ_TX_BIT)
		snd_pcm_period_elapsed(i2s->tx_substream);

	writel(reg, i2s->iobase_irq + I2S_IRQ_CLR);

	return IRQ_HANDLED;
}

static int chv3_dma_open(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct chv3_i2s_dev *i2s = snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	int res;

	snd_soc_set_runtime_hwparams(substream, &chv3_dma_hw);

	res = snd_pcm_hw_constraint_pow2(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
	if (res)
		return res;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		i2s->rx_substream = substream;
	else
		i2s->tx_substream = substream;

	return 0;
}
static int chv3_dma_close(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct chv3_i2s_dev *i2s = snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		chv3_i2s_wr(i2s, I2S_RX_ENABLE, 0);
	else
		chv3_i2s_wr(i2s, I2S_TX_ENABLE, 0);

	return 0;
}

static int chv3_dma_pcm_construct(struct snd_soc_component *component,
				  struct snd_soc_pcm_runtime *rtd)
{
	struct chv3_i2s_dev *i2s = snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	struct snd_pcm_substream *substream;
	int res;

	substream = rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	if (substream) {
		res = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, i2s->dev,
				I2S_MAX_BUFFER_SIZE, &substream->dma_buffer);
		if (res)
			return res;
	}

	substream = rtd->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	if (substream) {
		res = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, i2s->dev,
				I2S_MAX_BUFFER_SIZE, &substream->dma_buffer);
		if (res)
			return res;
	}

	return 0;
}

static int chv3_dma_hw_params(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	return 0;
}

static int chv3_dma_prepare(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct chv3_i2s_dev *i2s = snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	unsigned int buffer_bytes, period_bytes, period_size;

	buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
	period_bytes = snd_pcm_lib_period_bytes(substream);
	period_size = substream->runtime->period_size;

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE) {
		chv3_i2s_wr(i2s, I2S_SOFT_RESET, I2S_SOFT_RESET_RX_BIT);
		chv3_i2s_wr(i2s, I2S_RX_BASE_ADDR, substream->dma_buffer.addr);
		chv3_i2s_wr(i2s, I2S_RX_BUFFER_SIZE, buffer_bytes);
		chv3_i2s_wr(i2s, I2S_RX_IRQ, (period_size << 8) | 1);
		chv3_i2s_wr(i2s, I2S_RX_ENABLE, 1);
	} else {
		chv3_i2s_wr(i2s, I2S_SOFT_RESET, I2S_SOFT_RESET_TX_BIT);
		chv3_i2s_wr(i2s, I2S_TX_BASE_ADDR, substream->dma_buffer.addr);
		chv3_i2s_wr(i2s, I2S_TX_BUFFER_SIZE, buffer_bytes);
		chv3_i2s_wr(i2s, I2S_TX_IRQ, ((period_bytes / i2s->tx_bytes_to_fetch) << 8) | 1);
		chv3_i2s_wr(i2s, I2S_TX_ENABLE, 1);
	}
	writel(I2S_IRQ_RX_BIT | I2S_IRQ_TX_BIT, i2s->iobase_irq + I2S_IRQ_MASK);

	return 0;
}

static snd_pcm_uframes_t chv3_dma_pointer(struct snd_soc_component *component,
					  struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct chv3_i2s_dev *i2s = snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	u32 frame_bytes, buffer_bytes;
	u32 idx_bytes;

	frame_bytes = substream->runtime->frame_bits * 8;
	buffer_bytes = snd_pcm_lib_buffer_bytes(substream);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE) {
		idx_bytes = chv3_i2s_rd(i2s, I2S_RX_PRODUCER_IDX);
	} else {
		idx_bytes = chv3_i2s_rd(i2s, I2S_TX_CONSUMER_IDX);
		/* lag the pointer by one frame */
		idx_bytes = (idx_bytes - frame_bytes) & (buffer_bytes - 1);
	}

	return bytes_to_frames(substream->runtime, idx_bytes);
}

static int chv3_dma_ack(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct chv3_i2s_dev *i2s = snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	unsigned int bytes, idx;

	bytes = frames_to_bytes(runtime, runtime->control->appl_ptr);
	idx = bytes & (snd_pcm_lib_buffer_bytes(substream) - 1);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		chv3_i2s_wr(i2s, I2S_RX_CONSUMER_IDX, idx);
	else
		chv3_i2s_wr(i2s, I2S_TX_PRODUCER_IDX, idx);

	return 0;
}

static const struct snd_soc_component_driver chv3_i2s_comp = {
	.name = "chv3-i2s-comp",
	.open = chv3_dma_open,
	.close = chv3_dma_close,
	.pcm_construct = chv3_dma_pcm_construct,
	.hw_params = chv3_dma_hw_params,
	.prepare = chv3_dma_prepare,
	.pointer = chv3_dma_pointer,
	.ack = chv3_dma_ack,
};

static int chv3_i2s_probe(struct platform_device *pdev)
{
	struct chv3_i2s_dev *i2s;
	int res;
	int irq;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->iobase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(i2s->iobase))
		return PTR_ERR(i2s->iobase);

	i2s->iobase_irq = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(i2s->iobase_irq))
		return PTR_ERR(i2s->iobase_irq);

	i2s->tx_bytes_to_fetch = (chv3_i2s_rd(i2s, I2S_TX_IRQ_CONST) >> 8) & 0xffff;

	i2s->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, i2s);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENXIO;
	res = devm_request_irq(i2s->dev, irq, chv3_i2s_isr, 0, "chv3-i2s", i2s);
	if (res)
		return res;

	res = devm_snd_soc_register_component(&pdev->dev, &chv3_i2s_comp,
					      &chv3_i2s_dai, 1);
	if (res) {
		dev_err(&pdev->dev, "couldn't register component: %d\n", res);
		return res;
	}

	return 0;
}

static const struct of_device_id chv3_i2s_of_match[] = {
	{ .compatible = "google,chv3-i2s" },
	{},
};
MODULE_DEVICE_TABLE(of, chv3_i2s_of_match);

static struct platform_driver chv3_i2s_driver = {
	.probe = chv3_i2s_probe,
	.driver = {
		.name = "chv3-i2s",
		.of_match_table = chv3_i2s_of_match,
	},
};

module_platform_driver(chv3_i2s_driver);

MODULE_AUTHOR("Pawel Anikiel <pan@semihalf.com>");
MODULE_DESCRIPTION("Chameleon v3 I2S interface");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 *   Sound driver for Nintendo 64.
 *
 *   Copyright 2021 Lauri Kasanen
 */

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

MODULE_AUTHOR("Lauri Kasanen <cand@gmx.com>");
MODULE_DESCRIPTION("N64 Audio");
MODULE_LICENSE("GPL");

#define AI_NTSC_DACRATE 48681812
#define AI_STATUS_BUSY  (1 << 30)
#define AI_STATUS_FULL  (1 << 31)

#define AI_ADDR_REG 0
#define AI_LEN_REG 1
#define AI_CONTROL_REG 2
#define AI_STATUS_REG 3
#define AI_RATE_REG 4
#define AI_BITCLOCK_REG 5

#define MI_INTR_REG 2
#define MI_MASK_REG 3

#define MI_INTR_AI 0x04

#define MI_MASK_CLR_AI 0x0010
#define MI_MASK_SET_AI 0x0020


struct n64audio {
	u32 __iomem *ai_reg_base;
	u32 __iomem *mi_reg_base;

	void *ring_base;
	dma_addr_t ring_base_dma;

	struct snd_card *card;

	struct {
		struct snd_pcm_substream *substream;
		int pos, nextpos;
		u32 writesize;
		u32 bufsize;
		spinlock_t lock;
	} chan;
};

static void n64audio_write_reg(struct n64audio *priv, const u8 reg, const u32 value)
{
	writel(value, priv->ai_reg_base + reg);
}

static void n64mi_write_reg(struct n64audio *priv, const u8 reg, const u32 value)
{
	writel(value, priv->mi_reg_base + reg);
}

static u32 n64mi_read_reg(struct n64audio *priv, const u8 reg)
{
	return readl(priv->mi_reg_base + reg);
}

static void n64audio_push(struct n64audio *priv)
{
	struct snd_pcm_runtime *runtime = priv->chan.substream->runtime;
	unsigned long flags;
	u32 count;

	spin_lock_irqsave(&priv->chan.lock, flags);

	count = priv->chan.writesize;

	memcpy(priv->ring_base + priv->chan.nextpos,
	       runtime->dma_area + priv->chan.nextpos, count);

	/*
	 * The hw registers are double-buffered, and the IRQ fires essentially
	 * one period behind. The core only allows one period's distance, so we
	 * keep a private DMA buffer to afford two.
	 */
	n64audio_write_reg(priv, AI_ADDR_REG, priv->ring_base_dma + priv->chan.nextpos);
	barrier();
	n64audio_write_reg(priv, AI_LEN_REG, count);

	priv->chan.nextpos += count;
	priv->chan.nextpos %= priv->chan.bufsize;

	runtime->delay = runtime->period_size;

	spin_unlock_irqrestore(&priv->chan.lock, flags);
}

static irqreturn_t n64audio_isr(int irq, void *dev_id)
{
	struct n64audio *priv = dev_id;
	const u32 intrs = n64mi_read_reg(priv, MI_INTR_REG);
	unsigned long flags;

	// Check it's ours
	if (!(intrs & MI_INTR_AI))
		return IRQ_NONE;

	n64audio_write_reg(priv, AI_STATUS_REG, 1);

	if (priv->chan.substream && snd_pcm_running(priv->chan.substream)) {
		spin_lock_irqsave(&priv->chan.lock, flags);

		priv->chan.pos = priv->chan.nextpos;

		spin_unlock_irqrestore(&priv->chan.lock, flags);

		snd_pcm_period_elapsed(priv->chan.substream);
		if (priv->chan.substream && snd_pcm_running(priv->chan.substream))
			n64audio_push(priv);
	}

	return IRQ_HANDLED;
}

static const struct snd_pcm_hardware n64audio_pcm_hw = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER),
	.formats =          SNDRV_PCM_FMTBIT_S16_BE,
	.rates =            SNDRV_PCM_RATE_8000_48000,
	.rate_min =         8000,
	.rate_max =         48000,
	.channels_min =     2,
	.channels_max =     2,
	.buffer_bytes_max = 32768,
	.period_bytes_min = 1024,
	.period_bytes_max = 32768,
	.periods_min =      3,
	// 3 periods lets the double-buffering hw read one buffer behind safely
	.periods_max =      128,
};

static int hw_rule_period_size(struct snd_pcm_hw_params *params,
			       struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *c = hw_param_interval(params,
						   SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
	int changed = 0;

	/*
	 * The DMA unit has errata on (start + len) & 0x3fff == 0x2000.
	 * This constraint makes sure that the period size is not a power of two,
	 * which combined with dma_alloc_coherent aligning the buffer to the largest
	 * PoT <= size guarantees it won't be hit.
	 */

	if (is_power_of_2(c->min)) {
		c->min += 2;
		changed = 1;
	}
	if (is_power_of_2(c->max)) {
		c->max -= 2;
		changed = 1;
	}
	if (snd_interval_checkempty(c)) {
		c->empty = 1;
		return -EINVAL;
	}

	return changed;
}

static int n64audio_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	runtime->hw = n64audio_pcm_hw;
	err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 2);
	if (err < 0)
		return err;

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
			    hw_rule_period_size, NULL, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, -1);
	if (err < 0)
		return err;

	return 0;
}

static int n64audio_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct n64audio *priv = substream->pcm->private_data;
	u32 rate;

	rate = ((2 * AI_NTSC_DACRATE / runtime->rate) + 1) / 2 - 1;

	n64audio_write_reg(priv, AI_RATE_REG, rate);

	rate /= 66;
	if (rate > 16)
		rate = 16;
	n64audio_write_reg(priv, AI_BITCLOCK_REG, rate - 1);

	spin_lock_irq(&priv->chan.lock);

	/* Setup the pseudo-dma transfer pointers.  */
	priv->chan.pos = 0;
	priv->chan.nextpos = 0;
	priv->chan.substream = substream;
	priv->chan.writesize = snd_pcm_lib_period_bytes(substream);
	priv->chan.bufsize = snd_pcm_lib_buffer_bytes(substream);

	spin_unlock_irq(&priv->chan.lock);
	return 0;
}

static int n64audio_pcm_trigger(struct snd_pcm_substream *substream,
				int cmd)
{
	struct n64audio *priv = substream->pcm->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		n64audio_push(substream->pcm->private_data);
		n64audio_write_reg(priv, AI_CONTROL_REG, 1);
		n64mi_write_reg(priv, MI_MASK_REG, MI_MASK_SET_AI);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		n64audio_write_reg(priv, AI_CONTROL_REG, 0);
		n64mi_write_reg(priv, MI_MASK_REG, MI_MASK_CLR_AI);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t n64audio_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct n64audio *priv = substream->pcm->private_data;

	return bytes_to_frames(substream->runtime,
			       priv->chan.pos);
}

static int n64audio_pcm_close(struct snd_pcm_substream *substream)
{
	struct n64audio *priv = substream->pcm->private_data;

	priv->chan.substream = NULL;

	return 0;
}

static const struct snd_pcm_ops n64audio_pcm_ops = {
	.open =		n64audio_pcm_open,
	.prepare =	n64audio_pcm_prepare,
	.trigger =	n64audio_pcm_trigger,
	.pointer =	n64audio_pcm_pointer,
	.close =	n64audio_pcm_close,
};

/*
 * The target device is embedded and RAM-constrained. We save RAM
 * by initializing in __init code that gets dropped late in boot.
 * For the same reason there is no module or unloading support.
 */
static int __init n64audio_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct n64audio *priv;
	struct resource *res;
	int err;

	err = snd_card_new(&pdev->dev, SNDRV_DEFAULT_IDX1,
			   SNDRV_DEFAULT_STR1,
			   THIS_MODULE, sizeof(*priv), &card);
	if (err < 0)
		return err;

	priv = card->private_data;

	spin_lock_init(&priv->chan.lock);

	priv->card = card;

	priv->ring_base = dma_alloc_coherent(card->dev, 32 * 1024, &priv->ring_base_dma,
					     GFP_DMA|GFP_KERNEL);
	if (!priv->ring_base) {
		err = -ENOMEM;
		goto fail_card;
	}

	priv->mi_reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (!priv->mi_reg_base) {
		err = -EINVAL;
		goto fail_dma_alloc;
	}

	priv->ai_reg_base = devm_platform_ioremap_resource(pdev, 1);
	if (!priv->ai_reg_base) {
		err = -EINVAL;
		goto fail_dma_alloc;
	}

	err = snd_pcm_new(card, "N64 Audio", 0, 1, 0, &pcm);
	if (err < 0)
		goto fail_dma_alloc;

	pcm->private_data = priv;
	strcpy(pcm->name, "N64 Audio");

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &n64audio_pcm_ops);
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, card->dev, 0, 0);

	strcpy(card->driver, "N64 Audio");
	strcpy(card->shortname, "N64 Audio");
	strcpy(card->longname, "N64 Audio");

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (devm_request_irq(&pdev->dev, res->start, n64audio_isr,
				IRQF_SHARED, "N64 Audio", priv)) {
		err = -EBUSY;
		goto fail_dma_alloc;
	}

	err = snd_card_register(card);
	if (err < 0)
		goto fail_dma_alloc;

	return 0;

fail_dma_alloc:
	dma_free_coherent(card->dev, 32 * 1024, priv->ring_base, priv->ring_base_dma);

fail_card:
	snd_card_free(card);
	return err;
}

static struct platform_driver n64audio_driver = {
	.driver = {
		.name = "n64audio",
	},
};

static int __init n64audio_init(void)
{
	return platform_driver_probe(&n64audio_driver, n64audio_probe);
}

module_init(n64audio_init);

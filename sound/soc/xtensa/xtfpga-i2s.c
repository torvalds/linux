// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xtfpga I2S controller driver
 *
 * Copyright (c) 2014 Cadence Design Systems Inc.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define DRV_NAME	"xtfpga-i2s"

#define XTFPGA_I2S_VERSION	0x00
#define XTFPGA_I2S_CONFIG	0x04
#define XTFPGA_I2S_INT_MASK	0x08
#define XTFPGA_I2S_INT_STATUS	0x0c
#define XTFPGA_I2S_CHAN0_DATA	0x10
#define XTFPGA_I2S_CHAN1_DATA	0x14
#define XTFPGA_I2S_CHAN2_DATA	0x18
#define XTFPGA_I2S_CHAN3_DATA	0x1c

#define XTFPGA_I2S_CONFIG_TX_ENABLE	0x1
#define XTFPGA_I2S_CONFIG_INT_ENABLE	0x2
#define XTFPGA_I2S_CONFIG_LEFT		0x4
#define XTFPGA_I2S_CONFIG_RATIO_BASE	8
#define XTFPGA_I2S_CONFIG_RATIO_MASK	0x0000ff00
#define XTFPGA_I2S_CONFIG_RES_BASE	16
#define XTFPGA_I2S_CONFIG_RES_MASK	0x003f0000
#define XTFPGA_I2S_CONFIG_LEVEL_BASE	24
#define XTFPGA_I2S_CONFIG_LEVEL_MASK	0x0f000000
#define XTFPGA_I2S_CONFIG_CHANNEL_BASE	28

#define XTFPGA_I2S_INT_UNDERRUN		0x1
#define XTFPGA_I2S_INT_LEVEL		0x2
#define XTFPGA_I2S_INT_VALID		0x3

#define XTFPGA_I2S_FIFO_SIZE		8192

/*
 * I2S controller operation:
 *
 * Enabling TX: output 1 period of zeros (starting with left channel)
 * and then queued data.
 *
 * Level status and interrupt: whenever FIFO level is below FIFO trigger,
 * level status is 1 and an IRQ is asserted (if enabled).
 *
 * Underrun status and interrupt: whenever FIFO is empty, underrun status
 * is 1 and an IRQ is asserted (if enabled).
 */
struct xtfpga_i2s {
	struct device *dev;
	struct clk *clk;
	struct regmap *regmap;
	void __iomem *regs;

	/* current playback substream. NULL if not playing.
	 *
	 * Access to that field is synchronized between the interrupt handler
	 * and userspace through RCU.
	 *
	 * Interrupt handler (threaded part) does PIO on substream data in RCU
	 * read-side critical section. Trigger callback sets and clears the
	 * pointer when the playback is started and stopped with
	 * rcu_assign_pointer. When userspace is about to free the playback
	 * stream in the pcm_close callback it synchronizes with the interrupt
	 * handler by means of synchronize_rcu call.
	 */
	struct snd_pcm_substream __rcu *tx_substream;
	unsigned (*tx_fn)(struct xtfpga_i2s *i2s,
			  struct snd_pcm_runtime *runtime,
			  unsigned tx_ptr);
	unsigned tx_ptr; /* next frame index in the sample buffer */

	/* current fifo level estimate.
	 * Doesn't have to be perfectly accurate, but must be not less than
	 * the actual FIFO level in order to avoid stall on push attempt.
	 */
	unsigned tx_fifo_level;

	/* FIFO level at which level interrupt occurs */
	unsigned tx_fifo_low;

	/* maximal FIFO level */
	unsigned tx_fifo_high;
};

static bool xtfpga_i2s_wr_reg(struct device *dev, unsigned int reg)
{
	return reg >= XTFPGA_I2S_CONFIG;
}

static bool xtfpga_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	return reg < XTFPGA_I2S_CHAN0_DATA;
}

static bool xtfpga_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == XTFPGA_I2S_INT_STATUS;
}

static const struct regmap_config xtfpga_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = XTFPGA_I2S_CHAN3_DATA,
	.writeable_reg = xtfpga_i2s_wr_reg,
	.readable_reg = xtfpga_i2s_rd_reg,
	.volatile_reg = xtfpga_i2s_volatile_reg,
	.cache_type = REGCACHE_FLAT,
};

/* Generate functions that do PIO from TX DMA area to FIFO for all supported
 * stream formats.
 * Functions will be called xtfpga_pcm_tx_<channels>x<sample bits>, e.g.
 * xtfpga_pcm_tx_2x16 for 16-bit stereo.
 *
 * FIFO consists of 32-bit words, one word per channel, always 2 channels.
 * If I2S interface is configured with smaller sample resolution, only
 * the LSB of each word is used.
 */
#define xtfpga_pcm_tx_fn(channels, sample_bits) \
static unsigned xtfpga_pcm_tx_##channels##x##sample_bits( \
	struct xtfpga_i2s *i2s, struct snd_pcm_runtime *runtime, \
	unsigned tx_ptr) \
{ \
	const u##sample_bits (*p)[channels] = \
		(void *)runtime->dma_area; \
\
	for (; i2s->tx_fifo_level < i2s->tx_fifo_high; \
	     i2s->tx_fifo_level += 2) { \
		iowrite32(p[tx_ptr][0], \
			  i2s->regs + XTFPGA_I2S_CHAN0_DATA); \
		iowrite32(p[tx_ptr][channels - 1], \
			  i2s->regs + XTFPGA_I2S_CHAN0_DATA); \
		if (++tx_ptr >= runtime->buffer_size) \
			tx_ptr = 0; \
	} \
	return tx_ptr; \
}

xtfpga_pcm_tx_fn(1, 16)
xtfpga_pcm_tx_fn(2, 16)
xtfpga_pcm_tx_fn(1, 32)
xtfpga_pcm_tx_fn(2, 32)

#undef xtfpga_pcm_tx_fn

static bool xtfpga_pcm_push_tx(struct xtfpga_i2s *i2s)
{
	struct snd_pcm_substream *tx_substream;
	bool tx_active;

	rcu_read_lock();
	tx_substream = rcu_dereference(i2s->tx_substream);
	tx_active = tx_substream && snd_pcm_running(tx_substream);
	if (tx_active) {
		unsigned tx_ptr = READ_ONCE(i2s->tx_ptr);
		unsigned new_tx_ptr = i2s->tx_fn(i2s, tx_substream->runtime,
						 tx_ptr);

		cmpxchg(&i2s->tx_ptr, tx_ptr, new_tx_ptr);
	}
	rcu_read_unlock();

	return tx_active;
}

static void xtfpga_pcm_refill_fifo(struct xtfpga_i2s *i2s)
{
	unsigned int_status;
	unsigned i;

	regmap_read(i2s->regmap, XTFPGA_I2S_INT_STATUS,
		    &int_status);

	for (i = 0; i < 2; ++i) {
		bool tx_active = xtfpga_pcm_push_tx(i2s);

		regmap_write(i2s->regmap, XTFPGA_I2S_INT_STATUS,
			     XTFPGA_I2S_INT_VALID);
		if (tx_active)
			regmap_read(i2s->regmap, XTFPGA_I2S_INT_STATUS,
				    &int_status);

		if (!tx_active ||
		    !(int_status & XTFPGA_I2S_INT_LEVEL))
			break;

		/* After the push the level IRQ is still asserted,
		 * means FIFO level is below tx_fifo_low. Estimate
		 * it as tx_fifo_low.
		 */
		i2s->tx_fifo_level = i2s->tx_fifo_low;
	}

	if (!(int_status & XTFPGA_I2S_INT_LEVEL))
		regmap_write(i2s->regmap, XTFPGA_I2S_INT_MASK,
			     XTFPGA_I2S_INT_VALID);
	else if (!(int_status & XTFPGA_I2S_INT_UNDERRUN))
		regmap_write(i2s->regmap, XTFPGA_I2S_INT_MASK,
			     XTFPGA_I2S_INT_UNDERRUN);

	if (!(int_status & XTFPGA_I2S_INT_UNDERRUN))
		regmap_update_bits(i2s->regmap, XTFPGA_I2S_CONFIG,
				   XTFPGA_I2S_CONFIG_INT_ENABLE |
				   XTFPGA_I2S_CONFIG_TX_ENABLE,
				   XTFPGA_I2S_CONFIG_INT_ENABLE |
				   XTFPGA_I2S_CONFIG_TX_ENABLE);
	else
		regmap_update_bits(i2s->regmap, XTFPGA_I2S_CONFIG,
				   XTFPGA_I2S_CONFIG_INT_ENABLE |
				   XTFPGA_I2S_CONFIG_TX_ENABLE, 0);
}

static irqreturn_t xtfpga_i2s_threaded_irq_handler(int irq, void *dev_id)
{
	struct xtfpga_i2s *i2s = dev_id;
	struct snd_pcm_substream *tx_substream;
	unsigned config, int_status, int_mask;

	regmap_read(i2s->regmap, XTFPGA_I2S_CONFIG, &config);
	regmap_read(i2s->regmap, XTFPGA_I2S_INT_MASK, &int_mask);
	regmap_read(i2s->regmap, XTFPGA_I2S_INT_STATUS, &int_status);

	if (!(config & XTFPGA_I2S_CONFIG_INT_ENABLE) ||
	    !(int_status & int_mask & XTFPGA_I2S_INT_VALID))
		return IRQ_NONE;

	/* Update FIFO level estimate in accordance with interrupt status
	 * register.
	 */
	if (int_status & XTFPGA_I2S_INT_UNDERRUN) {
		i2s->tx_fifo_level = 0;
		regmap_update_bits(i2s->regmap, XTFPGA_I2S_CONFIG,
				   XTFPGA_I2S_CONFIG_TX_ENABLE, 0);
	} else {
		/* The FIFO isn't empty, but is below tx_fifo_low. Estimate
		 * it as tx_fifo_low.
		 */
		i2s->tx_fifo_level = i2s->tx_fifo_low;
	}

	rcu_read_lock();
	tx_substream = rcu_dereference(i2s->tx_substream);

	if (tx_substream && snd_pcm_running(tx_substream)) {
		snd_pcm_period_elapsed(tx_substream);
		if (int_status & XTFPGA_I2S_INT_UNDERRUN)
			dev_dbg_ratelimited(i2s->dev, "%s: underrun\n",
					    __func__);
	}
	rcu_read_unlock();

	/* Refill FIFO, update allowed IRQ reasons, enable IRQ if FIFO is
	 * not empty.
	 */
	xtfpga_pcm_refill_fifo(i2s);

	return IRQ_HANDLED;
}

static int xtfpga_i2s_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct xtfpga_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_set_dma_data(dai, substream, i2s);
	return 0;
}

static int xtfpga_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct xtfpga_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned srate = params_rate(params);
	unsigned channels = params_channels(params);
	unsigned period_size = params_period_size(params);
	unsigned sample_size = snd_pcm_format_width(params_format(params));
	unsigned freq, ratio, level;
	int err;

	regmap_update_bits(i2s->regmap, XTFPGA_I2S_CONFIG,
			   XTFPGA_I2S_CONFIG_RES_MASK,
			   sample_size << XTFPGA_I2S_CONFIG_RES_BASE);

	freq = 256 * srate;
	err = clk_set_rate(i2s->clk, freq);
	if (err < 0)
		return err;

	/* ratio field of the config register controls MCLK->I2S clock
	 * derivation: I2S clock = MCLK / (2 * (ratio + 2)).
	 *
	 * So with MCLK = 256 * sample rate ratio is 0 for 32 bit stereo
	 * and 2 for 16 bit stereo.
	 */
	ratio = (freq - (srate * sample_size * 8)) /
		(srate * sample_size * 4);

	regmap_update_bits(i2s->regmap, XTFPGA_I2S_CONFIG,
			   XTFPGA_I2S_CONFIG_RATIO_MASK,
			   ratio << XTFPGA_I2S_CONFIG_RATIO_BASE);

	i2s->tx_fifo_low = XTFPGA_I2S_FIFO_SIZE / 2;

	/* period_size * 2: FIFO always gets 2 samples per frame */
	for (level = 1;
	     i2s->tx_fifo_low / 2 >= period_size * 2 &&
	     level < (XTFPGA_I2S_CONFIG_LEVEL_MASK >>
		      XTFPGA_I2S_CONFIG_LEVEL_BASE); ++level)
		i2s->tx_fifo_low /= 2;

	i2s->tx_fifo_high = 2 * i2s->tx_fifo_low;

	regmap_update_bits(i2s->regmap, XTFPGA_I2S_CONFIG,
			   XTFPGA_I2S_CONFIG_LEVEL_MASK,
			   level << XTFPGA_I2S_CONFIG_LEVEL_BASE);

	dev_dbg(i2s->dev,
		"%s srate: %u, channels: %u, sample_size: %u, period_size: %u\n",
		__func__, srate, channels, sample_size, period_size);
	dev_dbg(i2s->dev, "%s freq: %u, ratio: %u, level: %u\n",
		__func__, freq, ratio, level);

	return 0;
}

static int xtfpga_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
			      unsigned int fmt)
{
	if ((fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF)
		return -EINVAL;
	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS)
		return -EINVAL;
	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_I2S)
		return -EINVAL;

	return 0;
}

/* PCM */

static const struct snd_pcm_hardware xtfpga_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min		= 1,
	.channels_max		= 2,
	.period_bytes_min	= 2,
	.period_bytes_max	= XTFPGA_I2S_FIFO_SIZE / 2 * 8,
	.periods_min		= 2,
	.periods_max		= XTFPGA_I2S_FIFO_SIZE * 8 / 2,
	.buffer_bytes_max	= XTFPGA_I2S_FIFO_SIZE * 8,
	.fifo_size		= 16,
};

static int xtfpga_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	void *p;

	snd_soc_set_runtime_hwparams(substream, &xtfpga_pcm_hardware);
	p = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	runtime->private_data = p;

	return 0;
}

static int xtfpga_pcm_close(struct snd_pcm_substream *substream)
{
	synchronize_rcu();
	return 0;
}

static int xtfpga_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params)
{
	int ret;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct xtfpga_i2s *i2s = runtime->private_data;
	unsigned channels = params_channels(hw_params);

	switch (channels) {
	case 1:
	case 2:
		break;

	default:
		return -EINVAL;

	}

	switch (params_format(hw_params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		i2s->tx_fn = (channels == 1) ?
			xtfpga_pcm_tx_1x16 :
			xtfpga_pcm_tx_2x16;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		i2s->tx_fn = (channels == 1) ?
			xtfpga_pcm_tx_1x32 :
			xtfpga_pcm_tx_2x32;
		break;

	default:
		return -EINVAL;
	}

	ret = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	return ret;
}

static int xtfpga_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct xtfpga_i2s *i2s = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		WRITE_ONCE(i2s->tx_ptr, 0);
		rcu_assign_pointer(i2s->tx_substream, substream);
		xtfpga_pcm_refill_fifo(i2s);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		rcu_assign_pointer(i2s->tx_substream, NULL);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static snd_pcm_uframes_t xtfpga_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct xtfpga_i2s *i2s = runtime->private_data;
	snd_pcm_uframes_t pos = READ_ONCE(i2s->tx_ptr);

	return pos < runtime->buffer_size ? pos : 0;
}

static int xtfpga_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	size_t size = xtfpga_pcm_hardware.buffer_bytes_max;

	snd_pcm_lib_preallocate_pages_for_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
					      card->dev, size, size);
	return 0;
}

static const struct snd_pcm_ops xtfpga_pcm_ops = {
	.open		= xtfpga_pcm_open,
	.close		= xtfpga_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= xtfpga_pcm_hw_params,
	.trigger	= xtfpga_pcm_trigger,
	.pointer	= xtfpga_pcm_pointer,
};

static const struct snd_soc_component_driver xtfpga_i2s_component = {
	.name		= DRV_NAME,
	.pcm_new	= xtfpga_pcm_new,
	.ops		= &xtfpga_pcm_ops,
};

static const struct snd_soc_dai_ops xtfpga_i2s_dai_ops = {
	.startup	= xtfpga_i2s_startup,
	.hw_params      = xtfpga_i2s_hw_params,
	.set_fmt        = xtfpga_i2s_set_fmt,
};

static struct snd_soc_dai_driver xtfpga_i2s_dai[] = {
	{
		.name = "xtfpga-i2s",
		.id = 0,
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &xtfpga_i2s_dai_ops,
	},
};

static int xtfpga_i2s_runtime_suspend(struct device *dev)
{
	struct xtfpga_i2s *i2s = dev_get_drvdata(dev);

	clk_disable_unprepare(i2s->clk);
	return 0;
}

static int xtfpga_i2s_runtime_resume(struct device *dev)
{
	struct xtfpga_i2s *i2s = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2s->clk);
	if (ret) {
		dev_err(dev, "clk_prepare_enable failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static int xtfpga_i2s_probe(struct platform_device *pdev)
{
	struct xtfpga_i2s *i2s;
	struct resource *mem;
	int err, irq;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s) {
		err = -ENOMEM;
		goto err;
	}
	platform_set_drvdata(pdev, i2s);
	i2s->dev = &pdev->dev;
	dev_dbg(&pdev->dev, "dev: %p, i2s: %p\n", &pdev->dev, i2s);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2s->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(i2s->regs)) {
		err = PTR_ERR(i2s->regs);
		goto err;
	}

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, i2s->regs,
					    &xtfpga_i2s_regmap_config);
	if (IS_ERR(i2s->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		err = PTR_ERR(i2s->regmap);
		goto err;
	}

	i2s->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2s->clk)) {
		dev_err(&pdev->dev, "couldn't get clock\n");
		err = PTR_ERR(i2s->clk);
		goto err;
	}

	regmap_write(i2s->regmap, XTFPGA_I2S_CONFIG,
		     (0x1 << XTFPGA_I2S_CONFIG_CHANNEL_BASE));
	regmap_write(i2s->regmap, XTFPGA_I2S_INT_STATUS, XTFPGA_I2S_INT_VALID);
	regmap_write(i2s->regmap, XTFPGA_I2S_INT_MASK, XTFPGA_I2S_INT_UNDERRUN);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource\n");
		err = irq;
		goto err;
	}
	err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					xtfpga_i2s_threaded_irq_handler,
					IRQF_SHARED | IRQF_ONESHOT,
					pdev->name, i2s);
	if (err < 0) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err;
	}

	err = devm_snd_soc_register_component(&pdev->dev,
					      &xtfpga_i2s_component,
					      xtfpga_i2s_dai,
					      ARRAY_SIZE(xtfpga_i2s_dai));
	if (err < 0) {
		dev_err(&pdev->dev, "couldn't register component\n");
		goto err;
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		err = xtfpga_i2s_runtime_resume(&pdev->dev);
		if (err)
			goto err_pm_disable;
	}
	return 0;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);
err:
	dev_err(&pdev->dev, "%s: err = %d\n", __func__, err);
	return err;
}

static int xtfpga_i2s_remove(struct platform_device *pdev)
{
	struct xtfpga_i2s *i2s = dev_get_drvdata(&pdev->dev);

	if (i2s->regmap && !IS_ERR(i2s->regmap)) {
		regmap_write(i2s->regmap, XTFPGA_I2S_CONFIG, 0);
		regmap_write(i2s->regmap, XTFPGA_I2S_INT_MASK, 0);
		regmap_write(i2s->regmap, XTFPGA_I2S_INT_STATUS,
			     XTFPGA_I2S_INT_VALID);
	}
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		xtfpga_i2s_runtime_suspend(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id xtfpga_i2s_of_match[] = {
	{ .compatible = "cdns,xtfpga-i2s", },
	{},
};
MODULE_DEVICE_TABLE(of, xtfpga_i2s_of_match);
#endif

static const struct dev_pm_ops xtfpga_i2s_pm_ops = {
	SET_RUNTIME_PM_OPS(xtfpga_i2s_runtime_suspend,
			   xtfpga_i2s_runtime_resume, NULL)
};

static struct platform_driver xtfpga_i2s_driver = {
	.probe   = xtfpga_i2s_probe,
	.remove  = xtfpga_i2s_remove,
	.driver  = {
		.name = "xtfpga-i2s",
		.of_match_table = of_match_ptr(xtfpga_i2s_of_match),
		.pm = &xtfpga_i2s_pm_ops,
	},
};

module_platform_driver(xtfpga_i2s_driver);

MODULE_AUTHOR("Max Filippov <jcmvbkbc@gmail.com>");
MODULE_DESCRIPTION("xtfpga I2S controller driver");
MODULE_LICENSE("GPL v2");

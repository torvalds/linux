/*
 * Driver for the Atmel on-chip Audio Bitstream DAC (ABDAC)
 *
 * Copyright (C) 2006-2009 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/bitmap.h>
#include <linux/dw_dmac.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/atmel-abdac.h>

/* DAC register offsets */
#define DAC_DATA                                0x0000
#define DAC_CTRL                                0x0008
#define DAC_INT_MASK                            0x000c
#define DAC_INT_EN                              0x0010
#define DAC_INT_DIS                             0x0014
#define DAC_INT_CLR                             0x0018
#define DAC_INT_STATUS                          0x001c

/* Bitfields in CTRL */
#define DAC_SWAP_OFFSET                         30
#define DAC_SWAP_SIZE                           1
#define DAC_EN_OFFSET                           31
#define DAC_EN_SIZE                             1

/* Bitfields in INT_MASK/INT_EN/INT_DIS/INT_STATUS/INT_CLR */
#define DAC_UNDERRUN_OFFSET                     28
#define DAC_UNDERRUN_SIZE                       1
#define DAC_TX_READY_OFFSET                     29
#define DAC_TX_READY_SIZE                       1

/* Bit manipulation macros */
#define DAC_BIT(name)					\
	(1 << DAC_##name##_OFFSET)
#define DAC_BF(name, value)				\
	(((value) & ((1 << DAC_##name##_SIZE) - 1))	\
	 << DAC_##name##_OFFSET)
#define DAC_BFEXT(name, value)				\
	(((value) >> DAC_##name##_OFFSET)		\
	 & ((1 << DAC_##name##_SIZE) - 1))
#define DAC_BFINS(name, value, old)			\
	(((old) & ~(((1 << DAC_##name##_SIZE) - 1)	\
		    << DAC_##name##_OFFSET))		\
	 | DAC_BF(name, value))

/* Register access macros */
#define dac_readl(port, reg)				\
	__raw_readl((port)->regs + DAC_##reg)
#define dac_writel(port, reg, value)			\
	__raw_writel((value), (port)->regs + DAC_##reg)

/*
 * ABDAC supports a maximum of 6 different rates from a generic clock. The
 * generic clock has a power of two divider, which gives 6 steps from 192 kHz
 * to 5112 Hz.
 */
#define MAX_NUM_RATES	6
/* ALSA seems to use rates between 192000 Hz and 5112 Hz. */
#define RATE_MAX	192000
#define RATE_MIN	5112

enum {
	DMA_READY = 0,
};

struct atmel_abdac_dma {
	struct dma_chan		*chan;
	struct dw_cyclic_desc	*cdesc;
};

struct atmel_abdac {
	struct clk				*pclk;
	struct clk				*sample_clk;
	struct platform_device			*pdev;
	struct atmel_abdac_dma			dma;

	struct snd_pcm_hw_constraint_list	constraints_rates;
	struct snd_pcm_substream		*substream;
	struct snd_card				*card;
	struct snd_pcm				*pcm;

	void __iomem				*regs;
	unsigned long				flags;
	unsigned int				rates[MAX_NUM_RATES];
	unsigned int				rates_num;
	int					irq;
};

#define get_dac(card) ((struct atmel_abdac *)(card)->private_data)

/* This function is called by the DMA driver. */
static void atmel_abdac_dma_period_done(void *arg)
{
	struct atmel_abdac *dac = arg;
	snd_pcm_period_elapsed(dac->substream);
}

static int atmel_abdac_prepare_dma(struct atmel_abdac *dac,
		struct snd_pcm_substream *substream,
		enum dma_data_direction direction)
{
	struct dma_chan			*chan = dac->dma.chan;
	struct dw_cyclic_desc		*cdesc;
	struct snd_pcm_runtime		*runtime = substream->runtime;
	unsigned long			buffer_len, period_len;

	/*
	 * We don't do DMA on "complex" transfers, i.e. with
	 * non-halfword-aligned buffers or lengths.
	 */
	if (runtime->dma_addr & 1 || runtime->buffer_size & 1) {
		dev_dbg(&dac->pdev->dev, "too complex transfer\n");
		return -EINVAL;
	}

	buffer_len = frames_to_bytes(runtime, runtime->buffer_size);
	period_len = frames_to_bytes(runtime, runtime->period_size);

	cdesc = dw_dma_cyclic_prep(chan, runtime->dma_addr, buffer_len,
			period_len, DMA_MEM_TO_DEV);
	if (IS_ERR(cdesc)) {
		dev_dbg(&dac->pdev->dev, "could not prepare cyclic DMA\n");
		return PTR_ERR(cdesc);
	}

	cdesc->period_callback = atmel_abdac_dma_period_done;
	cdesc->period_callback_param = dac;

	dac->dma.cdesc = cdesc;

	set_bit(DMA_READY, &dac->flags);

	return 0;
}

static struct snd_pcm_hardware atmel_abdac_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP
				  | SNDRV_PCM_INFO_MMAP_VALID
				  | SNDRV_PCM_INFO_INTERLEAVED
				  | SNDRV_PCM_INFO_BLOCK_TRANSFER
				  | SNDRV_PCM_INFO_RESUME
				  | SNDRV_PCM_INFO_PAUSE),
	.formats		= (SNDRV_PCM_FMTBIT_S16_BE),
	.rates			= (SNDRV_PCM_RATE_KNOT),
	.rate_min		= RATE_MIN,
	.rate_max		= RATE_MAX,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 64 * 4096,
	.period_bytes_min	= 4096,
	.period_bytes_max	= 4096,
	.periods_min		= 6,
	.periods_max		= 64,
};

static int atmel_abdac_open(struct snd_pcm_substream *substream)
{
	struct atmel_abdac *dac = snd_pcm_substream_chip(substream);

	dac->substream = substream;
	atmel_abdac_hw.rate_max = dac->rates[dac->rates_num - 1];
	atmel_abdac_hw.rate_min = dac->rates[0];
	substream->runtime->hw = atmel_abdac_hw;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &dac->constraints_rates);
}

static int atmel_abdac_close(struct snd_pcm_substream *substream)
{
	struct atmel_abdac *dac = snd_pcm_substream_chip(substream);
	dac->substream = NULL;
	return 0;
}

static int atmel_abdac_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params)
{
	struct atmel_abdac *dac = snd_pcm_substream_chip(substream);
	int retval;

	retval = snd_pcm_lib_malloc_pages(substream,
			params_buffer_bytes(hw_params));
	if (retval < 0)
		return retval;
	/* snd_pcm_lib_malloc_pages returns 1 if buffer is changed. */
	if (retval == 1)
		if (test_and_clear_bit(DMA_READY, &dac->flags))
			dw_dma_cyclic_free(dac->dma.chan);

	return retval;
}

static int atmel_abdac_hw_free(struct snd_pcm_substream *substream)
{
	struct atmel_abdac *dac = snd_pcm_substream_chip(substream);
	if (test_and_clear_bit(DMA_READY, &dac->flags))
		dw_dma_cyclic_free(dac->dma.chan);
	return snd_pcm_lib_free_pages(substream);
}

static int atmel_abdac_prepare(struct snd_pcm_substream *substream)
{
	struct atmel_abdac *dac = snd_pcm_substream_chip(substream);
	int retval;

	retval = clk_set_rate(dac->sample_clk, 256 * substream->runtime->rate);
	if (retval)
		return retval;

	if (!test_bit(DMA_READY, &dac->flags))
		retval = atmel_abdac_prepare_dma(dac, substream, DMA_TO_DEVICE);

	return retval;
}

static int atmel_abdac_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct atmel_abdac *dac = snd_pcm_substream_chip(substream);
	int retval = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE: /* fall through */
	case SNDRV_PCM_TRIGGER_RESUME: /* fall through */
	case SNDRV_PCM_TRIGGER_START:
		clk_enable(dac->sample_clk);
		retval = dw_dma_cyclic_start(dac->dma.chan);
		if (retval)
			goto out;
		dac_writel(dac, CTRL, DAC_BIT(EN));
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH: /* fall through */
	case SNDRV_PCM_TRIGGER_SUSPEND: /* fall through */
	case SNDRV_PCM_TRIGGER_STOP:
		dw_dma_cyclic_stop(dac->dma.chan);
		dac_writel(dac, DATA, 0);
		dac_writel(dac, CTRL, 0);
		clk_disable(dac->sample_clk);
		break;
	default:
		retval = -EINVAL;
		break;
	}
out:
	return retval;
}

static snd_pcm_uframes_t
atmel_abdac_pointer(struct snd_pcm_substream *substream)
{
	struct atmel_abdac	*dac = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime	*runtime = substream->runtime;
	snd_pcm_uframes_t	frames;
	unsigned long		bytes;

	bytes = dw_dma_get_src_addr(dac->dma.chan);
	bytes -= runtime->dma_addr;

	frames = bytes_to_frames(runtime, bytes);
	if (frames >= runtime->buffer_size)
		frames -= runtime->buffer_size;

	return frames;
}

static irqreturn_t abdac_interrupt(int irq, void *dev_id)
{
	struct atmel_abdac *dac = dev_id;
	u32 status;

	status = dac_readl(dac, INT_STATUS);
	if (status & DAC_BIT(UNDERRUN)) {
		dev_err(&dac->pdev->dev, "underrun detected\n");
		dac_writel(dac, INT_CLR, DAC_BIT(UNDERRUN));
	} else {
		dev_err(&dac->pdev->dev, "spurious interrupt (status=0x%x)\n",
			status);
		dac_writel(dac, INT_CLR, status);
	}

	return IRQ_HANDLED;
}

static struct snd_pcm_ops atmel_abdac_ops = {
	.open		= atmel_abdac_open,
	.close		= atmel_abdac_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= atmel_abdac_hw_params,
	.hw_free	= atmel_abdac_hw_free,
	.prepare	= atmel_abdac_prepare,
	.trigger	= atmel_abdac_trigger,
	.pointer	= atmel_abdac_pointer,
};

static int __devinit atmel_abdac_pcm_new(struct atmel_abdac *dac)
{
	struct snd_pcm_hardware hw = atmel_abdac_hw;
	struct snd_pcm *pcm;
	int retval;

	retval = snd_pcm_new(dac->card, dac->card->shortname,
			dac->pdev->id, 1, 0, &pcm);
	if (retval)
		return retval;

	strcpy(pcm->name, dac->card->shortname);
	pcm->private_data = dac;
	pcm->info_flags = 0;
	dac->pcm = pcm;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &atmel_abdac_ops);

	retval = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
			&dac->pdev->dev, hw.periods_min * hw.period_bytes_min,
			hw.buffer_bytes_max);

	return retval;
}

static bool filter(struct dma_chan *chan, void *slave)
{
	struct dw_dma_slave *dws = slave;

	if (dws->dma_dev == chan->device->dev) {
		chan->private = dws;
		return true;
	} else
		return false;
}

static int set_sample_rates(struct atmel_abdac *dac)
{
	long new_rate = RATE_MAX;
	int retval = -EINVAL;
	int index = 0;

	/* we start at 192 kHz and work our way down to 5112 Hz */
	while (new_rate >= RATE_MIN && index < (MAX_NUM_RATES + 1)) {
		new_rate = clk_round_rate(dac->sample_clk, 256 * new_rate);
		if (new_rate < 0)
			break;
		/* make sure we are below the ABDAC clock */
		if (new_rate <= clk_get_rate(dac->pclk)) {
			dac->rates[index] = new_rate / 256;
			index++;
		}
		/* divide by 256 and then by two to get next rate */
		new_rate /= 256 * 2;
	}

	if (index) {
		int i;

		/* reverse array, smallest go first */
		for (i = 0; i < (index / 2); i++) {
			unsigned int tmp = dac->rates[index - 1 - i];
			dac->rates[index - 1 - i] = dac->rates[i];
			dac->rates[i] = tmp;
		}

		dac->constraints_rates.count = index;
		dac->constraints_rates.list = dac->rates;
		dac->constraints_rates.mask = 0;
		dac->rates_num = index;

		retval = 0;
	}

	return retval;
}

static int __devinit atmel_abdac_probe(struct platform_device *pdev)
{
	struct snd_card		*card;
	struct atmel_abdac	*dac;
	struct resource		*regs;
	struct atmel_abdac_pdata	*pdata;
	struct clk		*pclk;
	struct clk		*sample_clk;
	int			retval;
	int			irq;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_dbg(&pdev->dev, "no memory resource\n");
		return -ENXIO;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_dbg(&pdev->dev, "could not get IRQ number\n");
		return irq;
	}

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_dbg(&pdev->dev, "no platform data\n");
		return -ENXIO;
	}

	pclk = clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pclk)) {
		dev_dbg(&pdev->dev, "no peripheral clock\n");
		return PTR_ERR(pclk);
	}
	sample_clk = clk_get(&pdev->dev, "sample_clk");
	if (IS_ERR(sample_clk)) {
		dev_dbg(&pdev->dev, "no sample clock\n");
		retval = PTR_ERR(sample_clk);
		goto out_put_pclk;
	}
	clk_enable(pclk);

	retval = snd_card_create(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			THIS_MODULE, sizeof(struct atmel_abdac), &card);
	if (retval) {
		dev_dbg(&pdev->dev, "could not create sound card device\n");
		goto out_put_sample_clk;
	}

	dac = get_dac(card);

	dac->irq = irq;
	dac->card = card;
	dac->pclk = pclk;
	dac->sample_clk = sample_clk;
	dac->pdev = pdev;

	retval = set_sample_rates(dac);
	if (retval < 0) {
		dev_dbg(&pdev->dev, "could not set supported rates\n");
		goto out_free_card;
	}

	dac->regs = ioremap(regs->start, resource_size(regs));
	if (!dac->regs) {
		dev_dbg(&pdev->dev, "could not remap register memory\n");
		goto out_free_card;
	}

	/* make sure the DAC is silent and disabled */
	dac_writel(dac, DATA, 0);
	dac_writel(dac, CTRL, 0);

	retval = request_irq(irq, abdac_interrupt, 0, "abdac", dac);
	if (retval) {
		dev_dbg(&pdev->dev, "could not request irq\n");
		goto out_unmap_regs;
	}

	snd_card_set_dev(card, &pdev->dev);

	if (pdata->dws.dma_dev) {
		struct dw_dma_slave *dws = &pdata->dws;
		dma_cap_mask_t mask;

		dws->tx_reg = regs->start + DAC_DATA;

		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		dac->dma.chan = dma_request_channel(mask, filter, dws);
	}
	if (!pdata->dws.dma_dev || !dac->dma.chan) {
		dev_dbg(&pdev->dev, "DMA not available\n");
		retval = -ENODEV;
		goto out_unset_card_dev;
	}

	strcpy(card->driver, "Atmel ABDAC");
	strcpy(card->shortname, "Atmel ABDAC");
	sprintf(card->longname, "Atmel Audio Bitstream DAC");

	retval = atmel_abdac_pcm_new(dac);
	if (retval) {
		dev_dbg(&pdev->dev, "could not register ABDAC pcm device\n");
		goto out_release_dma;
	}

	retval = snd_card_register(card);
	if (retval) {
		dev_dbg(&pdev->dev, "could not register sound card\n");
		goto out_release_dma;
	}

	platform_set_drvdata(pdev, card);

	dev_info(&pdev->dev, "Atmel ABDAC at 0x%p using %s\n",
			dac->regs, dev_name(&dac->dma.chan->dev->device));

	return retval;

out_release_dma:
	dma_release_channel(dac->dma.chan);
	dac->dma.chan = NULL;
out_unset_card_dev:
	snd_card_set_dev(card, NULL);
	free_irq(irq, dac);
out_unmap_regs:
	iounmap(dac->regs);
out_free_card:
	snd_card_free(card);
out_put_sample_clk:
	clk_put(sample_clk);
	clk_disable(pclk);
out_put_pclk:
	clk_put(pclk);
	return retval;
}

#ifdef CONFIG_PM
static int atmel_abdac_suspend(struct platform_device *pdev, pm_message_t msg)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct atmel_abdac *dac = card->private_data;

	dw_dma_cyclic_stop(dac->dma.chan);
	clk_disable(dac->sample_clk);
	clk_disable(dac->pclk);

	return 0;
}

static int atmel_abdac_resume(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct atmel_abdac *dac = card->private_data;

	clk_enable(dac->pclk);
	clk_enable(dac->sample_clk);
	if (test_bit(DMA_READY, &dac->flags))
		dw_dma_cyclic_start(dac->dma.chan);

	return 0;
}
#else
#define atmel_abdac_suspend NULL
#define atmel_abdac_resume NULL
#endif

static int __devexit atmel_abdac_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct atmel_abdac *dac = get_dac(card);

	clk_put(dac->sample_clk);
	clk_disable(dac->pclk);
	clk_put(dac->pclk);

	dma_release_channel(dac->dma.chan);
	dac->dma.chan = NULL;
	snd_card_set_dev(card, NULL);
	iounmap(dac->regs);
	free_irq(dac->irq, dac);
	snd_card_free(card);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver atmel_abdac_driver = {
	.remove		= __devexit_p(atmel_abdac_remove),
	.driver		= {
		.name	= "atmel_abdac",
	},
	.suspend	= atmel_abdac_suspend,
	.resume		= atmel_abdac_resume,
};

static int __init atmel_abdac_init(void)
{
	return platform_driver_probe(&atmel_abdac_driver,
			atmel_abdac_probe);
}
module_init(atmel_abdac_init);

static void __exit atmel_abdac_exit(void)
{
	platform_driver_unregister(&atmel_abdac_driver);
}
module_exit(atmel_abdac_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for Atmel Audio Bitstream DAC (ABDAC)");
MODULE_AUTHOR("Hans-Christian Egtvedt <egtvedt@samfundet.no>");

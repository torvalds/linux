/*
 * ASoC driver for Cirrus Logic EP93xx AC97 controller.
 *
 * Copyright (c) 2010 Mika Westerberg
 *
 * Based on s3c-ac97 ASoC driver by Jaswinder Singh.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/ac97_codec.h>
#include <sound/soc.h>

#include <linux/platform_data/dma-ep93xx.h>

/*
 * Per channel (1-4) registers.
 */
#define AC97CH(n)		(((n) - 1) * 0x20)

#define AC97DR(n)		(AC97CH(n) + 0x0000)

#define AC97RXCR(n)		(AC97CH(n) + 0x0004)
#define AC97RXCR_REN		BIT(0)
#define AC97RXCR_RX3		BIT(3)
#define AC97RXCR_RX4		BIT(4)
#define AC97RXCR_CM		BIT(15)

#define AC97TXCR(n)		(AC97CH(n) + 0x0008)
#define AC97TXCR_TEN		BIT(0)
#define AC97TXCR_TX3		BIT(3)
#define AC97TXCR_TX4		BIT(4)
#define AC97TXCR_CM		BIT(15)

#define AC97SR(n)		(AC97CH(n) + 0x000c)
#define AC97SR_TXFE		BIT(1)
#define AC97SR_TXUE		BIT(6)

#define AC97RISR(n)		(AC97CH(n) + 0x0010)
#define AC97ISR(n)		(AC97CH(n) + 0x0014)
#define AC97IE(n)		(AC97CH(n) + 0x0018)

/*
 * Global AC97 controller registers.
 */
#define AC97S1DATA		0x0080
#define AC97S2DATA		0x0084
#define AC97S12DATA		0x0088

#define AC97RGIS		0x008c
#define AC97GIS			0x0090
#define AC97IM			0x0094
/*
 * Common bits for RGIS, GIS and IM registers.
 */
#define AC97_SLOT2RXVALID	BIT(1)
#define AC97_CODECREADY		BIT(5)
#define AC97_SLOT2TXCOMPLETE	BIT(6)

#define AC97EOI			0x0098
#define AC97EOI_WINT		BIT(0)
#define AC97EOI_CODECREADY	BIT(1)

#define AC97GCR			0x009c
#define AC97GCR_AC97IFE		BIT(0)

#define AC97RESET		0x00a0
#define AC97RESET_TIMEDRESET	BIT(0)

#define AC97SYNC		0x00a4
#define AC97SYNC_TIMEDSYNC	BIT(0)

#define AC97_TIMEOUT		msecs_to_jiffies(5)

/**
 * struct ep93xx_ac97_info - EP93xx AC97 controller info structure
 * @lock: mutex serializing access to the bus (slot 1 & 2 ops)
 * @dev: pointer to the platform device dev structure
 * @regs: mapped AC97 controller registers
 * @done: bus ops wait here for an interrupt
 */
struct ep93xx_ac97_info {
	struct mutex		lock;
	struct device		*dev;
	void __iomem		*regs;
	struct completion	done;
};

/* currently ALSA only supports a single AC97 device */
static struct ep93xx_ac97_info *ep93xx_ac97_info;

static struct ep93xx_dma_data ep93xx_ac97_pcm_out = {
	.name		= "ac97-pcm-out",
	.dma_port	= EP93XX_DMA_AAC1,
	.direction	= DMA_MEM_TO_DEV,
};

static struct ep93xx_dma_data ep93xx_ac97_pcm_in = {
	.name		= "ac97-pcm-in",
	.dma_port	= EP93XX_DMA_AAC1,
	.direction	= DMA_DEV_TO_MEM,
};

static inline unsigned ep93xx_ac97_read_reg(struct ep93xx_ac97_info *info,
					    unsigned reg)
{
	return __raw_readl(info->regs + reg);
}

static inline void ep93xx_ac97_write_reg(struct ep93xx_ac97_info *info,
					 unsigned reg, unsigned val)
{
	__raw_writel(val, info->regs + reg);
}

static unsigned short ep93xx_ac97_read(struct snd_ac97 *ac97,
				       unsigned short reg)
{
	struct ep93xx_ac97_info *info = ep93xx_ac97_info;
	unsigned short val;

	mutex_lock(&info->lock);

	ep93xx_ac97_write_reg(info, AC97S1DATA, reg);
	ep93xx_ac97_write_reg(info, AC97IM, AC97_SLOT2RXVALID);
	if (!wait_for_completion_timeout(&info->done, AC97_TIMEOUT)) {
		dev_warn(info->dev, "timeout reading register %x\n", reg);
		mutex_unlock(&info->lock);
		return -ETIMEDOUT;
	}
	val = (unsigned short)ep93xx_ac97_read_reg(info, AC97S2DATA);

	mutex_unlock(&info->lock);
	return val;
}

static void ep93xx_ac97_write(struct snd_ac97 *ac97,
			      unsigned short reg,
			      unsigned short val)
{
	struct ep93xx_ac97_info *info = ep93xx_ac97_info;

	mutex_lock(&info->lock);

	/*
	 * Writes to the codec need to be done so that slot 2 is filled in
	 * before slot 1.
	 */
	ep93xx_ac97_write_reg(info, AC97S2DATA, val);
	ep93xx_ac97_write_reg(info, AC97S1DATA, reg);

	ep93xx_ac97_write_reg(info, AC97IM, AC97_SLOT2TXCOMPLETE);
	if (!wait_for_completion_timeout(&info->done, AC97_TIMEOUT))
		dev_warn(info->dev, "timeout writing register %x\n", reg);

	mutex_unlock(&info->lock);
}

static void ep93xx_ac97_warm_reset(struct snd_ac97 *ac97)
{
	struct ep93xx_ac97_info *info = ep93xx_ac97_info;

	mutex_lock(&info->lock);

	/*
	 * We are assuming that before this functions gets called, the codec
	 * BIT_CLK is stopped by forcing the codec into powerdown mode. We can
	 * control the SYNC signal directly via AC97SYNC register. Using
	 * TIMEDSYNC the controller will keep the SYNC high > 1us.
	 */
	ep93xx_ac97_write_reg(info, AC97SYNC, AC97SYNC_TIMEDSYNC);
	ep93xx_ac97_write_reg(info, AC97IM, AC97_CODECREADY);
	if (!wait_for_completion_timeout(&info->done, AC97_TIMEOUT))
		dev_warn(info->dev, "codec warm reset timeout\n");

	mutex_unlock(&info->lock);
}

static void ep93xx_ac97_cold_reset(struct snd_ac97 *ac97)
{
	struct ep93xx_ac97_info *info = ep93xx_ac97_info;

	mutex_lock(&info->lock);

	/*
	 * For doing cold reset, we disable the AC97 controller interface, clear
	 * WINT and CODECREADY bits, and finally enable the interface again.
	 */
	ep93xx_ac97_write_reg(info, AC97GCR, 0);
	ep93xx_ac97_write_reg(info, AC97EOI, AC97EOI_CODECREADY | AC97EOI_WINT);
	ep93xx_ac97_write_reg(info, AC97GCR, AC97GCR_AC97IFE);

	/*
	 * Now, assert the reset and wait for the codec to become ready.
	 */
	ep93xx_ac97_write_reg(info, AC97RESET, AC97RESET_TIMEDRESET);
	ep93xx_ac97_write_reg(info, AC97IM, AC97_CODECREADY);
	if (!wait_for_completion_timeout(&info->done, AC97_TIMEOUT))
		dev_warn(info->dev, "codec cold reset timeout\n");

	/*
	 * Give the codec some time to come fully out from the reset. This way
	 * we ensure that the subsequent reads/writes will work.
	 */
	usleep_range(15000, 20000);

	mutex_unlock(&info->lock);
}

static irqreturn_t ep93xx_ac97_interrupt(int irq, void *dev_id)
{
	struct ep93xx_ac97_info *info = dev_id;
	unsigned status, mask;

	/*
	 * Just mask out the interrupt and wake up the waiting thread.
	 * Interrupts are cleared via reading/writing to slot 1 & 2 registers by
	 * the waiting thread.
	 */
	status = ep93xx_ac97_read_reg(info, AC97GIS);
	mask = ep93xx_ac97_read_reg(info, AC97IM);
	mask &= ~status;
	ep93xx_ac97_write_reg(info, AC97IM, mask);

	complete(&info->done);
	return IRQ_HANDLED;
}

struct snd_ac97_bus_ops soc_ac97_ops = {
	.read		= ep93xx_ac97_read,
	.write		= ep93xx_ac97_write,
	.reset		= ep93xx_ac97_cold_reset,
	.warm_reset	= ep93xx_ac97_warm_reset,
};
EXPORT_SYMBOL_GPL(soc_ac97_ops);

static int ep93xx_ac97_trigger(struct snd_pcm_substream *substream,
			       int cmd, struct snd_soc_dai *dai)
{
	struct ep93xx_ac97_info *info = snd_soc_dai_get_drvdata(dai);
	unsigned v = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/*
			 * Enable compact mode, TX slots 3 & 4, and the TX FIFO
			 * itself.
			 */
			v |= AC97TXCR_CM;
			v |= AC97TXCR_TX3 | AC97TXCR_TX4;
			v |= AC97TXCR_TEN;
			ep93xx_ac97_write_reg(info, AC97TXCR(1), v);
		} else {
			/*
			 * Enable compact mode, RX slots 3 & 4, and the RX FIFO
			 * itself.
			 */
			v |= AC97RXCR_CM;
			v |= AC97RXCR_RX3 | AC97RXCR_RX4;
			v |= AC97RXCR_REN;
			ep93xx_ac97_write_reg(info, AC97RXCR(1), v);
		}
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/*
			 * As per Cirrus EP93xx errata described below:
			 *
			 * http://www.cirrus.com/en/pubs/errata/ER667E2B.pdf
			 *
			 * we will wait for the TX FIFO to be empty before
			 * clearing the TEN bit.
			 */
			unsigned long timeout = jiffies + AC97_TIMEOUT;

			do {
				v = ep93xx_ac97_read_reg(info, AC97SR(1));
				if (time_after(jiffies, timeout)) {
					dev_warn(info->dev, "TX timeout\n");
					break;
				}
			} while (!(v & (AC97SR_TXFE | AC97SR_TXUE)));

			/* disable the TX FIFO */
			ep93xx_ac97_write_reg(info, AC97TXCR(1), 0);
		} else {
			/* disable the RX FIFO */
			ep93xx_ac97_write_reg(info, AC97RXCR(1), 0);
		}
		break;

	default:
		dev_warn(info->dev, "unknown command %d\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static int ep93xx_ac97_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct ep93xx_dma_data *dma_data;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &ep93xx_ac97_pcm_out;
	else
		dma_data = &ep93xx_ac97_pcm_in;

	snd_soc_dai_set_dma_data(dai, substream, dma_data);
	return 0;
}

static const struct snd_soc_dai_ops ep93xx_ac97_dai_ops = {
	.startup	= ep93xx_ac97_startup,
	.trigger	= ep93xx_ac97_trigger,
};

static struct snd_soc_dai_driver ep93xx_ac97_dai = {
	.name		= "ep93xx-ac97",
	.id		= 0,
	.ac97_control	= 1,
	.playback	= {
		.stream_name	= "AC97 Playback",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture	= {
		.stream_name	= "AC97 Capture",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops			= &ep93xx_ac97_dai_ops,
};

static const struct snd_soc_component_driver ep93xx_ac97_component = {
	.name		= "ep93xx-ac97",
};

static int ep93xx_ac97_probe(struct platform_device *pdev)
{
	struct ep93xx_ac97_info *info;
	struct resource *res;
	unsigned int irq;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	info->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	irq = platform_get_irq(pdev, 0);
	if (!irq)
		return -ENODEV;

	ret = devm_request_irq(&pdev->dev, irq, ep93xx_ac97_interrupt,
			       IRQF_TRIGGER_HIGH, pdev->name, info);
	if (ret)
		goto fail;

	dev_set_drvdata(&pdev->dev, info);

	mutex_init(&info->lock);
	init_completion(&info->done);
	info->dev = &pdev->dev;

	ep93xx_ac97_info = info;
	platform_set_drvdata(pdev, info);

	ret = snd_soc_register_component(&pdev->dev, &ep93xx_ac97_component,
					 &ep93xx_ac97_dai, 1);
	if (ret)
		goto fail;

	return 0;

fail:
	ep93xx_ac97_info = NULL;
	dev_set_drvdata(&pdev->dev, NULL);
	return ret;
}

static int ep93xx_ac97_remove(struct platform_device *pdev)
{
	struct ep93xx_ac97_info	*info = platform_get_drvdata(pdev);

	snd_soc_unregister_component(&pdev->dev);

	/* disable the AC97 controller */
	ep93xx_ac97_write_reg(info, AC97GCR, 0);

	ep93xx_ac97_info = NULL;
	dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}

static struct platform_driver ep93xx_ac97_driver = {
	.probe	= ep93xx_ac97_probe,
	.remove	= ep93xx_ac97_remove,
	.driver = {
		.name = "ep93xx-ac97",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(ep93xx_ac97_driver);

MODULE_DESCRIPTION("EP93xx AC97 ASoC Driver");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@iki.fi>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ep93xx-ac97");

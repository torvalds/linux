/*
 * Freescale MPC5200 PSC in I2S mode
 * ALSA SoC Digital Audio Interface (DAI) driver
 *
 * Copyright (C) 2008 Secret Lab Technologies Ltd.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-of-simple.h>

#include <sysdev/bestcomm/bestcomm.h>
#include <sysdev/bestcomm/gen_bd.h>
#include <asm/mpc52xx_psc.h>

#include "mpc5200_dma.h"

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_DESCRIPTION("Freescale MPC5200 PSC in I2S mode ASoC Driver");
MODULE_LICENSE("GPL");

/**
 * PSC_I2S_RATES: sample rates supported by the I2S
 *
 * This driver currently only supports the PSC running in I2S slave mode,
 * which means the codec determines the sample rate.  Therefore, we tell
 * ALSA that we support all rates and let the codec driver decide what rates
 * are really supported.
 */
#define PSC_I2S_RATES (SNDRV_PCM_RATE_5512 | SNDRV_PCM_RATE_8000_192000 | \
			SNDRV_PCM_RATE_CONTINUOUS)

/**
 * PSC_I2S_FORMATS: audio formats supported by the PSC I2S mode
 */
#define PSC_I2S_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_BE | \
			 SNDRV_PCM_FMTBIT_S24_BE | SNDRV_PCM_FMTBIT_S24_BE | \
			 SNDRV_PCM_FMTBIT_S32_BE)

static int psc_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = rtd->dai->cpu_dai->private_data;
	u32 mode;

	dev_dbg(psc_dma->dev, "%s(substream=%p) p_size=%i p_bytes=%i"
		" periods=%i buffer_size=%i  buffer_bytes=%i\n",
		__func__, substream, params_period_size(params),
		params_period_bytes(params), params_periods(params),
		params_buffer_size(params), params_buffer_bytes(params));

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		mode = MPC52xx_PSC_SICR_SIM_CODEC_8;
		break;
	case SNDRV_PCM_FORMAT_S16_BE:
		mode = MPC52xx_PSC_SICR_SIM_CODEC_16;
		break;
	case SNDRV_PCM_FORMAT_S24_BE:
		mode = MPC52xx_PSC_SICR_SIM_CODEC_24;
		break;
	case SNDRV_PCM_FORMAT_S32_BE:
		mode = MPC52xx_PSC_SICR_SIM_CODEC_32;
		break;
	default:
		dev_dbg(psc_dma->dev, "invalid format\n");
		return -EINVAL;
	}
	out_be32(&psc_dma->psc_regs->sicr, psc_dma->sicr | mode);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

/**
 * psc_i2s_set_sysclk: set the clock frequency and direction
 *
 * This function is called by the machine driver to tell us what the clock
 * frequency and direction are.
 *
 * Currently, we only support operating as a clock slave (SND_SOC_CLOCK_IN),
 * and we don't care about the frequency.  Return an error if the direction
 * is not SND_SOC_CLOCK_IN.
 *
 * @clk_id: reserved, should be zero
 * @freq: the frequency of the given clock ID, currently ignored
 * @dir: SND_SOC_CLOCK_IN (clock slave) or SND_SOC_CLOCK_OUT (clock master)
 */
static int psc_i2s_set_sysclk(struct snd_soc_dai *cpu_dai,
			      int clk_id, unsigned int freq, int dir)
{
	struct psc_dma *psc_dma = cpu_dai->private_data;
	dev_dbg(psc_dma->dev, "psc_i2s_set_sysclk(cpu_dai=%p, dir=%i)\n",
				cpu_dai, dir);
	return (dir == SND_SOC_CLOCK_IN) ? 0 : -EINVAL;
}

/**
 * psc_i2s_set_fmt: set the serial format.
 *
 * This function is called by the machine driver to tell us what serial
 * format to use.
 *
 * This driver only supports I2S mode.  Return an error if the format is
 * not SND_SOC_DAIFMT_I2S.
 *
 * @format: one of SND_SOC_DAIFMT_xxx
 */
static int psc_i2s_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int format)
{
	struct psc_dma *psc_dma = cpu_dai->private_data;
	dev_dbg(psc_dma->dev, "psc_i2s_set_fmt(cpu_dai=%p, format=%i)\n",
				cpu_dai, format);
	return (format == SND_SOC_DAIFMT_I2S) ? 0 : -EINVAL;
}

/* ---------------------------------------------------------------------
 * ALSA SoC Bindings
 *
 * - Digital Audio Interface (DAI) template
 * - create/destroy dai hooks
 */

/**
 * psc_i2s_dai_template: template CPU Digital Audio Interface
 */
static struct snd_soc_dai_ops psc_i2s_dai_ops = {
	.startup	= psc_dma_startup,
	.hw_params	= psc_i2s_hw_params,
	.hw_free	= psc_dma_hw_free,
	.shutdown	= psc_dma_shutdown,
	.trigger	= psc_dma_trigger,
	.set_sysclk	= psc_i2s_set_sysclk,
	.set_fmt	= psc_i2s_set_fmt,
};

static struct snd_soc_dai psc_i2s_dai_template = {
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = PSC_I2S_RATES,
		.formats = PSC_I2S_FORMATS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = PSC_I2S_RATES,
		.formats = PSC_I2S_FORMATS,
	},
	.ops = &psc_i2s_dai_ops,
};

/* ---------------------------------------------------------------------
 * Sysfs attributes for debugging
 */

static ssize_t psc_i2s_status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct psc_dma *psc_dma = dev_get_drvdata(dev);

	return sprintf(buf, "status=%.4x sicr=%.8x rfnum=%i rfstat=0x%.4x "
			"tfnum=%i tfstat=0x%.4x\n",
			in_be16(&psc_dma->psc_regs->sr_csr.status),
			in_be32(&psc_dma->psc_regs->sicr),
			in_be16(&psc_dma->fifo_regs->rfnum) & 0x1ff,
			in_be16(&psc_dma->fifo_regs->rfstat),
			in_be16(&psc_dma->fifo_regs->tfnum) & 0x1ff,
			in_be16(&psc_dma->fifo_regs->tfstat));
}

static int *psc_i2s_get_stat_attr(struct psc_dma *psc_dma, const char *name)
{
	if (strcmp(name, "playback_underrun") == 0)
		return &psc_dma->stats.underrun_count;
	if (strcmp(name, "capture_overrun") == 0)
		return &psc_dma->stats.overrun_count;

	return NULL;
}

static ssize_t psc_i2s_stat_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct psc_dma *psc_dma = dev_get_drvdata(dev);
	int *attrib;

	attrib = psc_i2s_get_stat_attr(psc_dma, attr->attr.name);
	if (!attrib)
		return 0;

	return sprintf(buf, "%i\n", *attrib);
}

static ssize_t psc_i2s_stat_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	struct psc_dma *psc_dma = dev_get_drvdata(dev);
	int *attrib;

	attrib = psc_i2s_get_stat_attr(psc_dma, attr->attr.name);
	if (!attrib)
		return 0;

	*attrib = simple_strtoul(buf, NULL, 0);
	return count;
}

static DEVICE_ATTR(status, 0644, psc_i2s_status_show, NULL);
static DEVICE_ATTR(playback_underrun, 0644, psc_i2s_stat_show,
			psc_i2s_stat_store);
static DEVICE_ATTR(capture_overrun, 0644, psc_i2s_stat_show,
			psc_i2s_stat_store);

/* ---------------------------------------------------------------------
 * OF platform bus binding code:
 * - Probe/remove operations
 * - OF device match table
 */
static int __devinit psc_i2s_of_probe(struct of_device *op,
				      const struct of_device_id *match)
{
	phys_addr_t fifo;
	struct psc_dma *psc_dma;
	struct resource res;
	int size, psc_id, irq, rc;
	const __be32 *prop;
	void __iomem *regs;

	dev_dbg(&op->dev, "probing psc i2s device\n");

	/* Get the PSC ID */
	prop = of_get_property(op->node, "cell-index", &size);
	if (!prop || size < sizeof *prop)
		return -ENODEV;
	psc_id = be32_to_cpu(*prop);

	/* Fetch the registers and IRQ of the PSC */
	irq = irq_of_parse_and_map(op->node, 0);
	if (of_address_to_resource(op->node, 0, &res)) {
		dev_err(&op->dev, "Missing reg property\n");
		return -ENODEV;
	}
	regs = ioremap(res.start, 1 + res.end - res.start);
	if (!regs) {
		dev_err(&op->dev, "Could not map registers\n");
		return -ENODEV;
	}

	/* Allocate and initialize the driver private data */
	psc_dma = kzalloc(sizeof *psc_dma, GFP_KERNEL);
	if (!psc_dma) {
		iounmap(regs);
		return -ENOMEM;
	}
	spin_lock_init(&psc_dma->lock);
	psc_dma->irq = irq;
	psc_dma->psc_regs = regs;
	psc_dma->fifo_regs = regs + sizeof *psc_dma->psc_regs;
	psc_dma->dev = &op->dev;
	psc_dma->playback.psc_dma = psc_dma;
	psc_dma->capture.psc_dma = psc_dma;
	snprintf(psc_dma->name, sizeof psc_dma->name, "PSC%u", psc_id+1);

	/* Fill out the CPU DAI structure */
	memcpy(&psc_dma->dai, &psc_i2s_dai_template, sizeof psc_dma->dai);
	psc_dma->dai.private_data = psc_dma;
	psc_dma->dai.name = psc_dma->name;
	psc_dma->dai.id = psc_id;

	/* Find the address of the fifo data registers and setup the
	 * DMA tasks */
	fifo = res.start + offsetof(struct mpc52xx_psc, buffer.buffer_32);
	psc_dma->capture.bcom_task =
		bcom_psc_gen_bd_rx_init(psc_id, 10, fifo, 512);
	psc_dma->playback.bcom_task =
		bcom_psc_gen_bd_tx_init(psc_id, 10, fifo);
	if (!psc_dma->capture.bcom_task ||
	    !psc_dma->playback.bcom_task) {
		dev_err(&op->dev, "Could not allocate bestcomm tasks\n");
		iounmap(regs);
		kfree(psc_dma);
		return -ENODEV;
	}

	/* Disable all interrupts and reset the PSC */
	out_be16(&psc_dma->psc_regs->isr_imr.imr, 0);
	out_8(&psc_dma->psc_regs->command, 3 << 4); /* reset transmitter */
	out_8(&psc_dma->psc_regs->command, 2 << 4); /* reset receiver */
	out_8(&psc_dma->psc_regs->command, 1 << 4); /* reset mode */
	out_8(&psc_dma->psc_regs->command, 4 << 4); /* reset error */

	/* Configure the serial interface mode; defaulting to CODEC8 mode */
	psc_dma->sicr = MPC52xx_PSC_SICR_DTS1 | MPC52xx_PSC_SICR_I2S |
			MPC52xx_PSC_SICR_CLKPOL;
	if (of_get_property(op->node, "fsl,cellslave", NULL))
		psc_dma->sicr |= MPC52xx_PSC_SICR_CELLSLAVE |
				 MPC52xx_PSC_SICR_GENCLK;
	out_be32(&psc_dma->psc_regs->sicr,
		 psc_dma->sicr | MPC52xx_PSC_SICR_SIM_CODEC_8);

	/* Check for the codec handle.  If it is not present then we
	 * are done */
	if (!of_get_property(op->node, "codec-handle", NULL))
		return 0;

	/* Set up mode register;
	 * First write: RxRdy (FIFO Alarm) generates rx FIFO irq
	 * Second write: register Normal mode for non loopback
	 */
	out_8(&psc_dma->psc_regs->mode, 0);
	out_8(&psc_dma->psc_regs->mode, 0);

	/* Set the TX and RX fifo alarm thresholds */
	out_be16(&psc_dma->fifo_regs->rfalarm, 0x100);
	out_8(&psc_dma->fifo_regs->rfcntl, 0x4);
	out_be16(&psc_dma->fifo_regs->tfalarm, 0x100);
	out_8(&psc_dma->fifo_regs->tfcntl, 0x7);

	/* Lookup the IRQ numbers */
	psc_dma->playback.irq =
		bcom_get_task_irq(psc_dma->playback.bcom_task);
	psc_dma->capture.irq =
		bcom_get_task_irq(psc_dma->capture.bcom_task);

	/* Save what we've done so it can be found again later */
	dev_set_drvdata(&op->dev, psc_dma);

	/* Register the SYSFS files */
	rc = device_create_file(psc_dma->dev, &dev_attr_status);
	rc |= device_create_file(psc_dma->dev, &dev_attr_capture_overrun);
	rc |= device_create_file(psc_dma->dev, &dev_attr_playback_underrun);
	if (rc)
		dev_info(psc_dma->dev, "error creating sysfs files\n");

	snd_soc_register_platform(&psc_dma_pcm_soc_platform);

	/* Tell the ASoC OF helpers about it */
	of_snd_soc_register_platform(&psc_dma_pcm_soc_platform, op->node,
				     &psc_dma->dai);

	return 0;
}

static int __devexit psc_i2s_of_remove(struct of_device *op)
{
	struct psc_dma *psc_dma = dev_get_drvdata(&op->dev);

	dev_dbg(&op->dev, "psc_i2s_remove()\n");

	snd_soc_unregister_platform(&psc_dma_pcm_soc_platform);

	bcom_gen_bd_rx_release(psc_dma->capture.bcom_task);
	bcom_gen_bd_tx_release(psc_dma->playback.bcom_task);

	iounmap(psc_dma->psc_regs);
	iounmap(psc_dma->fifo_regs);
	kfree(psc_dma);
	dev_set_drvdata(&op->dev, NULL);

	return 0;
}

/* Match table for of_platform binding */
static struct of_device_id psc_i2s_match[] __devinitdata = {
	{ .compatible = "fsl,mpc5200-psc-i2s", },
	{}
};
MODULE_DEVICE_TABLE(of, psc_i2s_match);

static struct of_platform_driver psc_i2s_driver = {
	.match_table = psc_i2s_match,
	.probe = psc_i2s_of_probe,
	.remove = __devexit_p(psc_i2s_of_remove),
	.driver = {
		.name = "mpc5200-psc-i2s",
		.owner = THIS_MODULE,
	},
};

/* ---------------------------------------------------------------------
 * Module setup and teardown; simply register the of_platform driver
 * for the PSC in I2S mode.
 */
static int __init psc_i2s_init(void)
{
	return of_register_platform_driver(&psc_i2s_driver);
}
module_init(psc_i2s_init);

static void __exit psc_i2s_exit(void)
{
	of_unregister_platform_driver(&psc_i2s_driver);
}
module_exit(psc_i2s_exit);



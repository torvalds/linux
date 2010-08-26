/*
 * Freescale MPC5200 PSC in I2S mode
 * ALSA SoC Digital Audio Interface (DAI) driver
 *
 * Copyright (C) 2008 Secret Lab Technologies Ltd.
 * Copyright (C) 2009 Jon Smirl, Digispeaker
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/mpc52xx_psc.h>

#include "mpc5200_dma.h"

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
			 SNDRV_PCM_FMTBIT_S24_BE | SNDRV_PCM_FMTBIT_S32_BE)

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
	.hw_params	= psc_i2s_hw_params,
	.set_sysclk	= psc_i2s_set_sysclk,
	.set_fmt	= psc_i2s_set_fmt,
};

struct snd_soc_dai psc_i2s_dai[] = {{
	.name   = "I2S",
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
} };
EXPORT_SYMBOL_GPL(psc_i2s_dai);

/* ---------------------------------------------------------------------
 * OF platform bus binding code:
 * - Probe/remove operations
 * - OF device match table
 */
static int __devinit psc_i2s_of_probe(struct platform_device *op,
				      const struct of_device_id *match)
{
	int rc;
	struct psc_dma *psc_dma;
	struct mpc52xx_psc __iomem *regs;

	rc = mpc5200_audio_dma_create(op);
	if (rc != 0)
		return rc;

	rc = snd_soc_register_dais(psc_i2s_dai, ARRAY_SIZE(psc_i2s_dai));
	if (rc != 0) {
		pr_err("Failed to register DAI\n");
		return 0;
	}

	psc_dma = dev_get_drvdata(&op->dev);
	regs = psc_dma->psc_regs;

	/* Configure the serial interface mode; defaulting to CODEC8 mode */
	psc_dma->sicr = MPC52xx_PSC_SICR_DTS1 | MPC52xx_PSC_SICR_I2S |
			MPC52xx_PSC_SICR_CLKPOL;
	out_be32(&psc_dma->psc_regs->sicr,
		 psc_dma->sicr | MPC52xx_PSC_SICR_SIM_CODEC_8);

	/* Check for the codec handle.  If it is not present then we
	 * are done */
	if (!of_get_property(op->dev.of_node, "codec-handle", NULL))
		return 0;

	/* Due to errata in the dma mode; need to line up enabling
	 * the transmitter with a transition on the frame sync
	 * line */

	/* first make sure it is low */
	while ((in_8(&regs->ipcr_acr.ipcr) & 0x80) != 0)
		;
	/* then wait for the transition to high */
	while ((in_8(&regs->ipcr_acr.ipcr) & 0x80) == 0)
		;
	/* Finally, enable the PSC.
	 * Receiver must always be enabled; even when we only want
	 * transmit.  (see 15.3.2.3 of MPC5200B User's Guide) */

	/* Go */
	out_8(&psc_dma->psc_regs->command,
			MPC52xx_PSC_TX_ENABLE | MPC52xx_PSC_RX_ENABLE);

	return 0;

}

static int __devexit psc_i2s_of_remove(struct platform_device *op)
{
	return mpc5200_audio_dma_destroy(op);
}

/* Match table for of_platform binding */
static struct of_device_id psc_i2s_match[] __devinitdata = {
	{ .compatible = "fsl,mpc5200-psc-i2s", },
	{ .compatible = "fsl,mpc5200b-psc-i2s", },
	{}
};
MODULE_DEVICE_TABLE(of, psc_i2s_match);

static struct of_platform_driver psc_i2s_driver = {
	.probe = psc_i2s_of_probe,
	.remove = __devexit_p(psc_i2s_of_remove),
	.driver = {
		.name = "mpc5200-psc-i2s",
		.owner = THIS_MODULE,
		.of_match_table = psc_i2s_match,
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

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_DESCRIPTION("Freescale MPC5200 PSC in I2S mode ASoC Driver");
MODULE_LICENSE("GPL");


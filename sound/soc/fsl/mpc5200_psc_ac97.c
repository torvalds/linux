// SPDX-License-Identifier: GPL-2.0
//
// linux/sound/mpc5200-ac97.c -- AC97 support for the Freescale MPC52xx chip.
//
// Copyright (C) 2009 Jon Smirl, Digispeaker
// Author: Jon Smirl <jonsmirl@gmail.com>

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/time.h>
#include <asm/delay.h>
#include <asm/mpc52xx.h>
#include <asm/mpc52xx_psc.h>

#include "mpc5200_dma.h"

#define DRV_NAME "mpc5200-psc-ac97"

/* ALSA only supports a single AC97 device so static is recommend here */
static struct psc_dma *psc_dma;

static unsigned short psc_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	int status;
	unsigned int val;

	mutex_lock(&psc_dma->mutex);

	/* Wait for command send status zero = ready */
	status = spin_event_timeout(!(in_be16(&psc_dma->psc_regs->sr_csr.status) &
				MPC52xx_PSC_SR_CMDSEND), 100, 0);
	if (status == 0) {
		pr_err("timeout on ac97 bus (rdy)\n");
		mutex_unlock(&psc_dma->mutex);
		return -ENODEV;
	}

	/* Force clear the data valid bit */
	in_be32(&psc_dma->psc_regs->ac97_data);

	/* Send the read */
	out_be32(&psc_dma->psc_regs->ac97_cmd, (1<<31) | ((reg & 0x7f) << 24));

	/* Wait for the answer */
	status = spin_event_timeout((in_be16(&psc_dma->psc_regs->sr_csr.status) &
				MPC52xx_PSC_SR_DATA_VAL), 100, 0);
	if (status == 0) {
		pr_err("timeout on ac97 read (val) %x\n",
				in_be16(&psc_dma->psc_regs->sr_csr.status));
		mutex_unlock(&psc_dma->mutex);
		return -ENODEV;
	}
	/* Get the data */
	val = in_be32(&psc_dma->psc_regs->ac97_data);
	if (((val >> 24) & 0x7f) != reg) {
		pr_err("reg echo error on ac97 read\n");
		mutex_unlock(&psc_dma->mutex);
		return -ENODEV;
	}
	val = (val >> 8) & 0xffff;

	mutex_unlock(&psc_dma->mutex);
	return (unsigned short) val;
}

static void psc_ac97_write(struct snd_ac97 *ac97,
				unsigned short reg, unsigned short val)
{
	int status;

	mutex_lock(&psc_dma->mutex);

	/* Wait for command status zero = ready */
	status = spin_event_timeout(!(in_be16(&psc_dma->psc_regs->sr_csr.status) &
				MPC52xx_PSC_SR_CMDSEND), 100, 0);
	if (status == 0) {
		pr_err("timeout on ac97 bus (write)\n");
		goto out;
	}
	/* Write data */
	out_be32(&psc_dma->psc_regs->ac97_cmd,
			((reg & 0x7f) << 24) | (val << 8));

 out:
	mutex_unlock(&psc_dma->mutex);
}

static void psc_ac97_warm_reset(struct snd_ac97 *ac97)
{
	struct mpc52xx_psc __iomem *regs = psc_dma->psc_regs;

	mutex_lock(&psc_dma->mutex);

	out_be32(&regs->sicr, psc_dma->sicr | MPC52xx_PSC_SICR_AWR);
	udelay(3);
	out_be32(&regs->sicr, psc_dma->sicr);

	mutex_unlock(&psc_dma->mutex);
}

static void psc_ac97_cold_reset(struct snd_ac97 *ac97)
{
	struct mpc52xx_psc __iomem *regs = psc_dma->psc_regs;

	mutex_lock(&psc_dma->mutex);
	dev_dbg(psc_dma->dev, "cold reset\n");

	mpc5200_psc_ac97_gpio_reset(psc_dma->id);

	/* Notify the PSC that a reset has occurred */
	out_be32(&regs->sicr, psc_dma->sicr | MPC52xx_PSC_SICR_ACRB);

	/* Re-enable RX and TX */
	out_8(&regs->command, MPC52xx_PSC_TX_ENABLE | MPC52xx_PSC_RX_ENABLE);

	mutex_unlock(&psc_dma->mutex);

	usleep_range(1000, 2000);
	psc_ac97_warm_reset(ac97);
}

static struct snd_ac97_bus_ops psc_ac97_ops = {
	.read		= psc_ac97_read,
	.write		= psc_ac97_write,
	.reset		= psc_ac97_cold_reset,
	.warm_reset	= psc_ac97_warm_reset,
};

static int psc_ac97_hw_analog_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *cpu_dai)
{
	struct psc_dma *psc_dma = snd_soc_dai_get_drvdata(cpu_dai);
	struct psc_dma_stream *s = to_psc_dma_stream(substream, psc_dma);

	dev_dbg(psc_dma->dev, "%s(substream=%p) p_size=%i p_bytes=%i"
		" periods=%i buffer_size=%i  buffer_bytes=%i channels=%i"
		" rate=%i format=%i\n",
		__func__, substream, params_period_size(params),
		params_period_bytes(params), params_periods(params),
		params_buffer_size(params), params_buffer_bytes(params),
		params_channels(params), params_rate(params),
		params_format(params));

	/* Determine the set of enable bits to turn on */
	s->ac97_slot_bits = (params_channels(params) == 1) ? 0x100 : 0x300;
	if (substream->pstr->stream != SNDRV_PCM_STREAM_CAPTURE)
		s->ac97_slot_bits <<= 16;
	return 0;
}

static int psc_ac97_hw_digital_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *cpu_dai)
{
	struct psc_dma *psc_dma = snd_soc_dai_get_drvdata(cpu_dai);

	dev_dbg(psc_dma->dev, "%s(substream=%p)\n", __func__, substream);

	if (params_channels(params) == 1)
		out_be32(&psc_dma->psc_regs->ac97_slots, 0x01000000);
	else
		out_be32(&psc_dma->psc_regs->ac97_slots, 0x03000000);

	return 0;
}

static int psc_ac97_trigger(struct snd_pcm_substream *substream, int cmd,
							struct snd_soc_dai *dai)
{
	struct psc_dma *psc_dma = snd_soc_dai_get_drvdata(dai);
	struct psc_dma_stream *s = to_psc_dma_stream(substream, psc_dma);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dev_dbg(psc_dma->dev, "AC97 START: stream=%i\n",
			substream->pstr->stream);

		/* Set the slot enable bits */
		psc_dma->slots |= s->ac97_slot_bits;
		out_be32(&psc_dma->psc_regs->ac97_slots, psc_dma->slots);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		dev_dbg(psc_dma->dev, "AC97 STOP: stream=%i\n",
			substream->pstr->stream);

		/* Clear the slot enable bits */
		psc_dma->slots &= ~(s->ac97_slot_bits);
		out_be32(&psc_dma->psc_regs->ac97_slots, psc_dma->slots);
		break;
	}
	return 0;
}

static int psc_ac97_probe(struct snd_soc_dai *cpu_dai)
{
	struct psc_dma *psc_dma = snd_soc_dai_get_drvdata(cpu_dai);
	struct mpc52xx_psc __iomem *regs = psc_dma->psc_regs;

	/* Go */
	out_8(&regs->command, MPC52xx_PSC_TX_ENABLE | MPC52xx_PSC_RX_ENABLE);
	return 0;
}

/* ---------------------------------------------------------------------
 * ALSA SoC Bindings
 *
 * - Digital Audio Interface (DAI) template
 * - create/destroy dai hooks
 */

/**
 * psc_ac97_dai_template: template CPU Digital Audio Interface
 */
static const struct snd_soc_dai_ops psc_ac97_analog_ops = {
	.probe		= psc_ac97_probe,
	.hw_params	= psc_ac97_hw_analog_params,
	.trigger	= psc_ac97_trigger,
};

static const struct snd_soc_dai_ops psc_ac97_digital_ops = {
	.hw_params	= psc_ac97_hw_digital_params,
};

static struct snd_soc_dai_driver psc_ac97_dai[] = {
{
	.name = "mpc5200-psc-ac97.0",
	.playback = {
		.stream_name	= "AC97 Playback",
		.channels_min   = 1,
		.channels_max   = 6,
		.rates          = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_BE,
	},
	.capture = {
		.stream_name	= "AC97 Capture",
		.channels_min   = 1,
		.channels_max   = 2,
		.rates          = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_BE,
	},
	.ops = &psc_ac97_analog_ops,
},
{
	.name = "mpc5200-psc-ac97.1",
	.playback = {
		.stream_name	= "AC97 SPDIF",
		.channels_min   = 1,
		.channels_max   = 2,
		.rates          = SNDRV_PCM_RATE_32000 | \
			SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE,
	},
	.ops = &psc_ac97_digital_ops,
} };

static const struct snd_soc_component_driver psc_ac97_component = {
	.name		= DRV_NAME,
};


/* ---------------------------------------------------------------------
 * OF platform bus binding code:
 * - Probe/remove operations
 * - OF device match table
 */
static int psc_ac97_of_probe(struct platform_device *op)
{
	int rc;
	struct mpc52xx_psc __iomem *regs;

	rc = mpc5200_audio_dma_create(op);
	if (rc != 0)
		return rc;

	rc = snd_soc_set_ac97_ops(&psc_ac97_ops);
	if (rc != 0) {
		dev_err(&op->dev, "Failed to set AC'97 ops: %d\n", rc);
		return rc;
	}

	rc = snd_soc_register_component(&op->dev, &psc_ac97_component,
					psc_ac97_dai, ARRAY_SIZE(psc_ac97_dai));
	if (rc != 0) {
		dev_err(&op->dev, "Failed to register DAI\n");
		return rc;
	}

	psc_dma = dev_get_drvdata(&op->dev);
	regs = psc_dma->psc_regs;

	psc_dma->imr = 0;
	out_be16(&psc_dma->psc_regs->isr_imr.imr, psc_dma->imr);

	/* Configure the serial interface mode to AC97 */
	psc_dma->sicr = MPC52xx_PSC_SICR_SIM_AC97 | MPC52xx_PSC_SICR_ENAC97;
	out_be32(&regs->sicr, psc_dma->sicr);

	/* No slots active */
	out_be32(&regs->ac97_slots, 0x00000000);

	return 0;
}

static void psc_ac97_of_remove(struct platform_device *op)
{
	mpc5200_audio_dma_destroy(op);
	snd_soc_unregister_component(&op->dev);
	snd_soc_set_ac97_ops(NULL);
}

/* Match table for of_platform binding */
static const struct of_device_id psc_ac97_match[] = {
	{ .compatible = "fsl,mpc5200-psc-ac97", },
	{ .compatible = "fsl,mpc5200b-psc-ac97", },
	{}
};
MODULE_DEVICE_TABLE(of, psc_ac97_match);

static struct platform_driver psc_ac97_driver = {
	.probe = psc_ac97_of_probe,
	.remove_new = psc_ac97_of_remove,
	.driver = {
		.name = "mpc5200-psc-ac97",
		.of_match_table = psc_ac97_match,
	},
};

module_platform_driver(psc_ac97_driver);

MODULE_AUTHOR("Jon Smirl <jonsmirl@gmail.com>");
MODULE_DESCRIPTION("mpc5200 AC97 module");
MODULE_LICENSE("GPL");


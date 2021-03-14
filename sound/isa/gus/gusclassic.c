// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for Gravis UltraSound Classic soundcard
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/isa.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/module.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/gus.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

#define CRD_NAME "Gravis UltraSound Classic"
#define DEV_NAME "gusclassic"

MODULE_DESCRIPTION(CRD_NAME);
MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Gravis,UltraSound Classic}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x230,0x240,0x250,0x260 */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 3,5,9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int joystick_dac[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 29};
				/* 0 to 31, (0.59V-4.52V or 0.389V-2.98V) */
static int channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 24};
static int pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CRD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CRD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " CRD_NAME " soundcard.");
module_param_hw_array(port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for " CRD_NAME " driver.");
module_param_hw_array(irq, int, irq, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for " CRD_NAME " driver.");
module_param_hw_array(dma1, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 # for " CRD_NAME " driver.");
module_param_hw_array(dma2, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA2 # for " CRD_NAME " driver.");
module_param_array(joystick_dac, int, NULL, 0444);
MODULE_PARM_DESC(joystick_dac, "Joystick DAC level 0.59V-4.52V or 0.389V-2.98V for " CRD_NAME " driver.");
module_param_array(channels, int, NULL, 0444);
MODULE_PARM_DESC(channels, "GF1 channels for " CRD_NAME " driver.");
module_param_array(pcm_channels, int, NULL, 0444);
MODULE_PARM_DESC(pcm_channels, "Reserved PCM channels for " CRD_NAME " driver.");

static int snd_gusclassic_match(struct device *dev, unsigned int n)
{
	return enable[n];
}

static int snd_gusclassic_create(struct snd_card *card,
				 struct device *dev, unsigned int n,
				 struct snd_gus_card **rgus)
{
	static const long possible_ports[] = {0x220, 0x230, 0x240, 0x250, 0x260};
	static const int possible_irqs[] = {5, 11, 12, 9, 7, 15, 3, 4, -1};
	static const int possible_dmas[] = {5, 6, 7, 1, 3, -1};

	int i, error;

	if (irq[n] == SNDRV_AUTO_IRQ) {
		irq[n] = snd_legacy_find_free_irq(possible_irqs);
		if (irq[n] < 0) {
			dev_err(dev, "unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	if (dma1[n] == SNDRV_AUTO_DMA) {
		dma1[n] = snd_legacy_find_free_dma(possible_dmas);
		if (dma1[n] < 0) {
			dev_err(dev, "unable to find a free DMA1\n");
			return -EBUSY;
		}
	}
	if (dma2[n] == SNDRV_AUTO_DMA) {
		dma2[n] = snd_legacy_find_free_dma(possible_dmas);
		if (dma2[n] < 0) {
			dev_err(dev, "unable to find a free DMA2\n");
			return -EBUSY;
		}
	}

	if (port[n] != SNDRV_AUTO_PORT)
		return snd_gus_create(card, port[n], irq[n], dma1[n], dma2[n],
				0, channels[n], pcm_channels[n], 0, rgus);

	i = 0;
	do {
		port[n] = possible_ports[i];
		error = snd_gus_create(card, port[n], irq[n], dma1[n], dma2[n],
				0, channels[n], pcm_channels[n], 0, rgus);
	} while (error < 0 && ++i < ARRAY_SIZE(possible_ports));

	return error;
}

static int snd_gusclassic_detect(struct snd_gus_card *gus)
{
	unsigned char d;

	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 0);	/* reset GF1 */
	if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 0) {
		snd_printdd("[0x%lx] check 1 failed - 0x%x\n", gus->gf1.port, d);
		return -ENODEV;
	}
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 1);	/* release reset */
	udelay(160);
	if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 1) {
		snd_printdd("[0x%lx] check 2 failed - 0x%x\n", gus->gf1.port, d);
		return -ENODEV;
	}
	return 0;
}

static int snd_gusclassic_probe(struct device *dev, unsigned int n)
{
	struct snd_card *card;
	struct snd_gus_card *gus;
	int error;

	error = snd_card_new(dev, index[n], id[n], THIS_MODULE, 0, &card);
	if (error < 0)
		return error;

	if (pcm_channels[n] < 2)
		pcm_channels[n] = 2;

	error = snd_gusclassic_create(card, dev, n, &gus);
	if (error < 0)
		goto out;

	error = snd_gusclassic_detect(gus);
	if (error < 0)
		goto out;

	gus->joystick_dac = joystick_dac[n];

	error = snd_gus_initialize(gus);
	if (error < 0)
		goto out;

	error = -ENODEV;
	if (gus->max_flag || gus->ess_flag) {
		dev_err(dev, "GUS Classic or ACE soundcard was "
			"not detected at 0x%lx\n", gus->gf1.port);
		goto out;
	}

	error = snd_gf1_new_mixer(gus);
	if (error < 0)
		goto out;

	error = snd_gf1_pcm_new(gus, 0, 0);
	if (error < 0)
		goto out;

	if (!gus->ace_flag) {
		error = snd_gf1_rawmidi_new(gus, 0);
		if (error < 0)
			goto out;
	}

	sprintf(card->longname + strlen(card->longname),
		" at 0x%lx, irq %d, dma %d",
		gus->gf1.port, gus->gf1.irq, gus->gf1.dma1);

	if (gus->gf1.dma2 >= 0)
		sprintf(card->longname + strlen(card->longname),
			"&%d", gus->gf1.dma2);

	error = snd_card_register(card);
	if (error < 0)
		goto out;

	dev_set_drvdata(dev, card);
	return 0;

out:	snd_card_free(card);
	return error;
}

static void snd_gusclassic_remove(struct device *dev, unsigned int n)
{
	snd_card_free(dev_get_drvdata(dev));
}

static struct isa_driver snd_gusclassic_driver = {
	.match		= snd_gusclassic_match,
	.probe		= snd_gusclassic_probe,
	.remove		= snd_gusclassic_remove,
#if 0	/* FIXME */
	.suspend	= snd_gusclassic_suspend,
	.remove		= snd_gusclassic_remove,
#endif
	.driver		= {
		.name	= DEV_NAME
	}
};

module_isa_driver(snd_gusclassic_driver, SNDRV_CARDS);

/*
 *  Driver for Gravis UltraSound Classic soundcard
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/moduleparam.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/gus.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Gravis UltraSound Classic");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Gravis,UltraSound Classic}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x230,0x240,0x250,0x260 */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 3,5,9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int joystick_dac[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 29};
				/* 0 to 31, (0.59V-4.52V or 0.389V-2.98V) */
static int channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 24};
static int pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for GUS Classic soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for GUS Classic soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable GUS Classic soundcard.");
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for GUS Classic driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for GUS Classic driver.");
module_param_array(dma1, int, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 # for GUS Classic driver.");
module_param_array(dma2, int, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA2 # for GUS Classic driver.");
module_param_array(joystick_dac, int, NULL, 0444);
MODULE_PARM_DESC(joystick_dac, "Joystick DAC level 0.59V-4.52V or 0.389V-2.98V for GUS Classic driver.");
module_param_array(channels, int, NULL, 0444);
MODULE_PARM_DESC(channels, "GF1 channels for GUS Classic driver.");
module_param_array(pcm_channels, int, NULL, 0444);
MODULE_PARM_DESC(pcm_channels, "Reserved PCM channels for GUS Classic driver.");

static struct platform_device *devices[SNDRV_CARDS];


#define PFX	"gusclassic: "

static int __init snd_gusclassic_detect(struct snd_gus_card * gus)
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

static void __init snd_gusclassic_init(int dev, struct snd_gus_card * gus)
{
	gus->equal_irq = 0;
	gus->codec_flag = 0;
	gus->max_flag = 0;
	gus->joystick_dac = joystick_dac[dev];
}

static int __init snd_gusclassic_probe(struct platform_device *pdev)
{
	int dev = pdev->id;
	static int possible_irqs[] = {5, 11, 12, 9, 7, 15, 3, 4, -1};
	static int possible_dmas[] = {5, 6, 7, 1, 3, -1};
	int xirq, xdma1, xdma2;
	struct snd_card *card;
	struct snd_gus_card *gus = NULL;
	int err;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	if (pcm_channels[dev] < 2)
		pcm_channels[dev] = 2;

	xirq = irq[dev];
	if (xirq == SNDRV_AUTO_IRQ) {
		if ((xirq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free IRQ\n");
			err = -EBUSY;
			goto _err;
		}
	}
	xdma1 = dma1[dev];
	if (xdma1 == SNDRV_AUTO_DMA) {
		if ((xdma1 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA1\n");
			err = -EBUSY;
			goto _err;
		}
	}
	xdma2 = dma2[dev];
	if (xdma2 == SNDRV_AUTO_DMA) {
		if ((xdma2 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA2\n");
			err = -EBUSY;
			goto _err;
		}
	}

	if (port[dev] != SNDRV_AUTO_PORT) {
		err = snd_gus_create(card,
				     port[dev],
				     xirq, xdma1, xdma2,
				     0, channels[dev], pcm_channels[dev],
				     0, &gus);
	} else {
		/* auto-probe legacy ports */
		static unsigned long possible_ports[] = {
			0x220, 0x230, 0x240, 0x250, 0x260,
		};
		int i;
		for (i = 0; i < ARRAY_SIZE(possible_ports); i++) {
			err = snd_gus_create(card,
					     possible_ports[i],
					     xirq, xdma1, xdma2,
					     0, channels[dev], pcm_channels[dev],
					     0, &gus);
			if (err >= 0) {
				port[dev] = possible_ports[i];
				break;
			}
		}
	}
	if (err < 0)
		goto _err;

	if ((err = snd_gusclassic_detect(gus)) < 0)
		goto _err;

	snd_gusclassic_init(dev, gus);
	if ((err = snd_gus_initialize(gus)) < 0)
		goto _err;

	if (gus->max_flag || gus->ess_flag) {
		snd_printk(KERN_ERR PFX "GUS Classic or ACE soundcard was not detected at 0x%lx\n", gus->gf1.port);
		err = -ENODEV;
		goto _err;
	}

	if ((err = snd_gf1_new_mixer(gus)) < 0)
		goto _err;

	if ((err = snd_gf1_pcm_new(gus, 0, 0, NULL)) < 0)
		goto _err;

	if (!gus->ace_flag) {
		if ((err = snd_gf1_rawmidi_new(gus, 0, NULL)) < 0)
			goto _err;
	}
	sprintf(card->longname + strlen(card->longname), " at 0x%lx, irq %d, dma %d", gus->gf1.port, xirq, xdma1);
	if (dma2 >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", xdma2);

	snd_card_set_dev(card, &pdev->dev);

	if ((err = snd_card_register(card)) < 0)
		goto _err;

	platform_set_drvdata(pdev, card);
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

static int snd_gusclassic_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#define GUSCLASSIC_DRIVER	"snd_gusclassic"

static struct platform_driver snd_gusclassic_driver = {
	.probe		= snd_gusclassic_probe,
	.remove		= snd_gusclassic_remove,
	/* FIXME: suspend/resume */
	.driver		= {
		.name	= GUSCLASSIC_DRIVER
	},
};

static void __init_or_module snd_gusclassic_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_gusclassic_driver);
}

static int __init alsa_card_gusclassic_init(void)
{
	int i, cards, err;

	err = platform_driver_register(&snd_gusclassic_driver);
	if (err < 0)
		return err;

	cards = 0;
	for (i = 0; i < SNDRV_CARDS && enable[i]; i++) {
		struct platform_device *device;
		device = platform_device_register_simple(GUSCLASSIC_DRIVER,
							 i, NULL, 0);
		if (IS_ERR(device)) {
			err = PTR_ERR(device);
			goto errout;
		}
		devices[i] = device;
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "GUS Classic soundcard not found or device busy\n");
#endif
		err = -ENODEV;
		goto errout;
	}
	return 0;

 errout:
	snd_gusclassic_unregister_all();
	return err;
}

static void __exit alsa_card_gusclassic_exit(void)
{
	snd_gusclassic_unregister_all();
}

module_init(alsa_card_gusclassic_init)
module_exit(alsa_card_gusclassic_exit)

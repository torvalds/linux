/*
 *  Driver for generic ESS AudioDrive ESx688 soundcards
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
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/es1688.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("ESS ESx688 AudioDrive");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ESS,ES688 PnP AudioDrive,pnp:ESS0100},"
	        "{ESS,ES1688 PnP AudioDrive,pnp:ESS0102},"
	        "{ESS,ES688 AudioDrive,pnp:ESS6881},"
	        "{ESS,ES1688 AudioDrive,pnp:ESS1681}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260 */
static long mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for ESx688 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for ESx688 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable ESx688 soundcard.");
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for ESx688 driver.");
module_param_array(mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for ESx688 driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for ESx688 driver.");
module_param_array(mpu_irq, int, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for ESx688 driver.");
module_param_array(dma8, int, NULL, 0444);
MODULE_PARM_DESC(dma8, "8-bit DMA # for ESx688 driver.");

static struct platform_device *devices[SNDRV_CARDS];

#define PFX	"es1688: "

static int __init snd_es1688_probe(struct platform_device *pdev)
{
	int dev = pdev->id;
	static int possible_irqs[] = {5, 9, 10, 7, -1};
	static int possible_dmas[] = {1, 3, 0, -1};
	int xirq, xdma, xmpu_irq;
	struct snd_card *card;
	struct snd_es1688 *chip;
	struct snd_opl3 *opl3;
	struct snd_pcm *pcm;
	int err;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	xirq = irq[dev];
	if (xirq == SNDRV_AUTO_IRQ) {
		if ((xirq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free IRQ\n");
			err = -EBUSY;
			goto _err;
		}
	}
	xmpu_irq = mpu_irq[dev];
	xdma = dma8[dev];
	if (xdma == SNDRV_AUTO_DMA) {
		if ((xdma = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA\n");
			err = -EBUSY;
			goto _err;
		}
	}

	if (port[dev] != SNDRV_AUTO_PORT) {
		if ((err = snd_es1688_create(card, port[dev], mpu_port[dev],
					     xirq, xmpu_irq, xdma,
					     ES1688_HW_AUTO, &chip)) < 0)
			goto _err;
	} else {
		/* auto-probe legacy ports */
		static unsigned long possible_ports[] = {
			0x220, 0x240, 0x260,
		};
		int i;
		for (i = 0; i < ARRAY_SIZE(possible_ports); i++) {
			err = snd_es1688_create(card, possible_ports[i],
						mpu_port[dev],
						xirq, xmpu_irq, xdma,
						ES1688_HW_AUTO, &chip);
			if (err >= 0) {
				port[dev] = possible_ports[i];
				break;
			}
		}
		if (i >= ARRAY_SIZE(possible_ports))
			goto _err;
	}

	if ((err = snd_es1688_pcm(chip, 0, &pcm)) < 0)
		goto _err;

	if ((err = snd_es1688_mixer(chip)) < 0)
		goto _err;

	strcpy(card->driver, "ES1688");
	strcpy(card->shortname, pcm->name);
	sprintf(card->longname, "%s at 0x%lx, irq %i, dma %i", pcm->name, chip->port, xirq, xdma);

	if ((snd_opl3_create(card, chip->port, chip->port + 2, OPL3_HW_OPL3, 0, &opl3)) < 0) {
		printk(KERN_WARNING PFX "opl3 not detected at 0x%lx\n", chip->port);
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0)
			goto _err;
	}

	if (xmpu_irq >= 0 && xmpu_irq != SNDRV_AUTO_IRQ && chip->mpu_port > 0) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_ES1688,
					       chip->mpu_port, 0,
					       xmpu_irq,
					       SA_INTERRUPT,
					       NULL)) < 0)
			goto _err;
	}

	snd_card_set_dev(card, &pdev->dev);

	if ((err = snd_card_register(card)) < 0)
		goto _err;

	platform_set_drvdata(pdev, card);
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

static int snd_es1688_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#define ES1688_DRIVER	"snd_es1688"

static struct platform_driver snd_es1688_driver = {
	.probe		= snd_es1688_probe,
	.remove		= snd_es1688_remove,
	/* FIXME: suspend/resume */
	.driver		= {
		.name	= ES1688_DRIVER
	},
};

static void __init_or_module snd_es1688_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_es1688_driver);
}

static int __init alsa_card_es1688_init(void)
{
	int i, cards, err;

	err = platform_driver_register(&snd_es1688_driver);
	if (err < 0)
		return err;

	cards = 0;
	for (i = 0; i < SNDRV_CARDS && enable[i]; i++) {
		struct platform_device *device;
		device = platform_device_register_simple(ES1688_DRIVER,
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
		printk(KERN_ERR "ESS AudioDrive ES1688 soundcard not found or device busy\n");
#endif
		err = -ENODEV;
		goto errout;
	}
	return 0;

 errout:
	snd_es1688_unregister_all();
	return err;
}

static void __exit alsa_card_es1688_exit(void)
{
	snd_es1688_unregister_all();
}

module_init(alsa_card_es1688_init)
module_exit(alsa_card_es1688_exit)

/*
 *  Driver for Aztech Sound Galaxy cards
 *  Copyright (c) by Christopher Butler <chrisb@sandy.force9.co.uk.
 *
 *  I don't have documentation for this card, I based this driver on the
 *  driver for OSS/Free included in the kernel source (drivers/sound/sgalaxy.c)
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
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/ad1848.h>
#include <sound/control.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

MODULE_AUTHOR("Christopher Butler <chrisb@sandy.force9.co.uk>");
MODULE_DESCRIPTION("Aztech Sound Galaxy");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Aztech Systems,Sound Galaxy}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long sbport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240 */
static long wssport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x530,0xe80,0xf40,0x604 */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 7,9,10,11 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Sound Galaxy soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Sound Galaxy soundcard.");
module_param_array(sbport, long, NULL, 0444);
MODULE_PARM_DESC(sbport, "Port # for Sound Galaxy SB driver.");
module_param_array(wssport, long, NULL, 0444);
MODULE_PARM_DESC(wssport, "Port # for Sound Galaxy WSS driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for Sound Galaxy driver.");
module_param_array(dma1, int, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 # for Sound Galaxy driver.");

static struct platform_device *devices[SNDRV_CARDS];

#define SGALAXY_AUXC_LEFT 18
#define SGALAXY_AUXC_RIGHT 19

#define PFX	"sgalaxy: "

/*

 */

#define AD1848P1( port, x ) ( port + c_d_c_AD1848##x )

/* from lowlevel/sb/sb.c - to avoid having to allocate a struct snd_sb for the */
/* short time we actually need it.. */

static int snd_sgalaxy_sbdsp_reset(unsigned long port)
{
	int i;

	outb(1, SBP1(port, RESET));
	udelay(10);
	outb(0, SBP1(port, RESET));
	udelay(30);
	for (i = 0; i < 1000 && !(inb(SBP1(port, DATA_AVAIL)) & 0x80); i++);
	if (inb(SBP1(port, READ)) != 0xaa) {
		snd_printd("sb_reset: failed at 0x%lx!!!\n", port);
		return -ENODEV;
	}
	return 0;
}

static int __init snd_sgalaxy_sbdsp_command(unsigned long port, unsigned char val)
{
	int i;
       	
	for (i = 10000; i; i--)
		if ((inb(SBP1(port, STATUS)) & 0x80) == 0) {
			outb(val, SBP1(port, COMMAND));
			return 1;
		}

	return 0;
}

static irqreturn_t snd_sgalaxy_dummy_interrupt(int irq, void *dev_id)
{
	return IRQ_NONE;
}

static int __init snd_sgalaxy_setup_wss(unsigned long port, int irq, int dma)
{
	static int interrupt_bits[] = {-1, -1, -1, -1, -1, -1, -1, 0x08, -1, 
				       0x10, 0x18, 0x20, -1, -1, -1, -1};
	static int dma_bits[] = {1, 2, 0, 3};
	int tmp, tmp1;

	if ((tmp = inb(port + 3)) == 0xff)
	{
		snd_printdd("I/O address dead (0x%lx)\n", port);
		return 0;
	}
#if 0
	snd_printdd("WSS signature = 0x%x\n", tmp);
#endif

        if ((tmp & 0x3f) != 0x04 &&
            (tmp & 0x3f) != 0x0f &&
            (tmp & 0x3f) != 0x00) {
		snd_printdd("No WSS signature detected on port 0x%lx\n",
			    port + 3);
		return 0;
	}

#if 0
	snd_printdd(PFX "setting up IRQ/DMA for WSS\n");
#endif

        /* initialize IRQ for WSS codec */
        tmp = interrupt_bits[irq % 16];
        if (tmp < 0)
                return -EINVAL;

	if (request_irq(irq, snd_sgalaxy_dummy_interrupt, IRQF_DISABLED, "sgalaxy", NULL)) {
		snd_printk(KERN_ERR "sgalaxy: can't grab irq %d\n", irq);
		return -EIO;
	}

        outb(tmp | 0x40, port);
        tmp1 = dma_bits[dma % 4];
        outb(tmp | tmp1, port);

	free_irq(irq, NULL);

	return 0;
}

static int __init snd_sgalaxy_detect(int dev, int irq, int dma)
{
#if 0
	snd_printdd(PFX "switching to WSS mode\n");
#endif

	/* switch to WSS mode */
	snd_sgalaxy_sbdsp_reset(sbport[dev]);

	snd_sgalaxy_sbdsp_command(sbport[dev], 9);
	snd_sgalaxy_sbdsp_command(sbport[dev], 0);

	udelay(400);
	return snd_sgalaxy_setup_wss(wssport[dev], irq, dma);
}

static struct ad1848_mix_elem snd_sgalaxy_controls[] = {
AD1848_DOUBLE("Aux Playback Switch", 0, SGALAXY_AUXC_LEFT, SGALAXY_AUXC_RIGHT, 7, 7, 1, 1),
AD1848_DOUBLE("Aux Playback Volume", 0, SGALAXY_AUXC_LEFT, SGALAXY_AUXC_RIGHT, 0, 0, 31, 0)
};

static int __init snd_sgalaxy_mixer(struct snd_ad1848 *chip)
{
	struct snd_card *card = chip->card;
	struct snd_ctl_elem_id id1, id2;
	unsigned int idx;
	int err;

	memset(&id1, 0, sizeof(id1));
	memset(&id2, 0, sizeof(id2));
	id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	/* reassign AUX0 to LINE */
	strcpy(id1.name, "Aux Playback Switch");
	strcpy(id2.name, "Line Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "Line Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	/* reassign AUX1 to FM */
	strcpy(id1.name, "Aux Playback Switch"); id1.index = 1;
	strcpy(id2.name, "FM Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "FM Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	/* build AUX2 input */
	for (idx = 0; idx < ARRAY_SIZE(snd_sgalaxy_controls); idx++) {
		if ((err = snd_ad1848_add_ctl_elem(chip, &snd_sgalaxy_controls[idx])) < 0)
			return err;
	}
	return 0;
}

static int __init snd_sgalaxy_probe(struct platform_device *devptr)
{
	int dev = devptr->id;
	static int possible_irqs[] = {7, 9, 10, 11, -1};
	static int possible_dmas[] = {1, 3, 0, -1};
	int err, xirq, xdma1;
	struct snd_card *card;
	struct snd_ad1848 *chip;

	if (sbport[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR PFX "specify SB port\n");
		return -EINVAL;
	}
	if (wssport[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR PFX "specify WSS port\n");
		return -EINVAL;
	}
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
	xdma1 = dma1[dev];
        if (xdma1 == SNDRV_AUTO_DMA) {
		if ((xdma1 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA\n");
			err = -EBUSY;
			goto _err;
		}
	}

	if ((err = snd_sgalaxy_detect(dev, xirq, xdma1)) < 0)
		goto _err;

	if ((err = snd_ad1848_create(card, wssport[dev] + 4,
				     xirq, xdma1,
				     AD1848_HW_DETECT, &chip)) < 0)
		goto _err;
	card->private_data = chip;

	if ((err = snd_ad1848_pcm(chip, 0, NULL)) < 0) {
		snd_printdd(PFX "error creating new ad1848 PCM device\n");
		goto _err;
	}
	if ((err = snd_ad1848_mixer(chip)) < 0) {
		snd_printdd(PFX "error creating new ad1848 mixer\n");
		goto _err;
	}
	if ((err = snd_sgalaxy_mixer(chip)) < 0) {
		snd_printdd(PFX "the mixer rewrite failed\n");
		goto _err;
	}

	strcpy(card->driver, "Sound Galaxy");
	strcpy(card->shortname, "Sound Galaxy");
	sprintf(card->longname, "Sound Galaxy at 0x%lx, irq %d, dma %d",
		wssport[dev], xirq, xdma1);

	snd_card_set_dev(card, &devptr->dev);

	if ((err = snd_card_register(card)) < 0)
		goto _err;

	platform_set_drvdata(devptr, card);
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

static int __devexit snd_sgalaxy_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int snd_sgalaxy_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct snd_ad1848 *chip = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	chip->suspend(chip);
	return 0;
}

static int snd_sgalaxy_resume(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct snd_ad1848 *chip = card->private_data;

	chip->resume(chip);
	snd_ad1848_out(chip, SGALAXY_AUXC_LEFT, chip->image[SGALAXY_AUXC_LEFT]);
	snd_ad1848_out(chip, SGALAXY_AUXC_RIGHT, chip->image[SGALAXY_AUXC_RIGHT]);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

#define SND_SGALAXY_DRIVER	"snd_sgalaxy"

static struct platform_driver snd_sgalaxy_driver = {
	.probe		= snd_sgalaxy_probe,
	.remove		= __devexit_p(snd_sgalaxy_remove),
#ifdef CONFIG_PM
	.suspend	= snd_sgalaxy_suspend,
	.resume		= snd_sgalaxy_resume,
#endif
	.driver		= {
		.name	= SND_SGALAXY_DRIVER
	},
};

static void __init_or_module snd_sgalaxy_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_sgalaxy_driver);
}

static int __init alsa_card_sgalaxy_init(void)
{
	int i, cards, err;

	err = platform_driver_register(&snd_sgalaxy_driver);
	if (err < 0)
		return err;

	cards = 0;
	for (i = 0; i < SNDRV_CARDS; i++) {
		struct platform_device *device;
		if (! enable[i])
			continue;
		device = platform_device_register_simple(SND_SGALAXY_DRIVER,
							 i, NULL, 0);
		if (IS_ERR(device))
			continue;
		if (!platform_get_drvdata(device)) {
			platform_device_unregister(device);
			continue;
		}
		devices[i] = device;
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		snd_printk(KERN_ERR "Sound Galaxy soundcard not found or device busy\n");
#endif
		snd_sgalaxy_unregister_all();
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_sgalaxy_exit(void)
{
	snd_sgalaxy_unregister_all();
}

module_init(alsa_card_sgalaxy_init)
module_exit(alsa_card_sgalaxy_exit)

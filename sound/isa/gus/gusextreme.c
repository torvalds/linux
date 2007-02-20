/*
 *  Driver for Gravis UltraSound Extreme soundcards
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
#include <sound/es1688.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Gravis UltraSound Extreme");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Gravis,UltraSound Extreme}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260 */
static long gf1_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS) - 1] = -1}; /* 0x210,0x220,0x230,0x240,0x250,0x260,0x270 */
static long mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS) - 1] = -1}; /* 0x300,0x310,0x320 */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int gf1_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 2,3,5,9,11,12,15 */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;
static int joystick_dac[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 29};
				/* 0 to 31, (0.59V-4.52V or 0.389V-2.98V) */
static int channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 24};
static int pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for GUS Extreme soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for GUS Extreme soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable GUS Extreme soundcard.");
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for GUS Extreme driver.");
module_param_array(gf1_port, long, NULL, 0444);
MODULE_PARM_DESC(gf1_port, "GF1 port # for GUS Extreme driver (optional).");
module_param_array(mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for GUS Extreme driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for GUS Extreme driver.");
module_param_array(mpu_irq, int, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for GUS Extreme driver.");
module_param_array(gf1_irq, int, NULL, 0444);
MODULE_PARM_DESC(gf1_irq, "GF1 IRQ # for GUS Extreme driver.");
module_param_array(dma8, int, NULL, 0444);
MODULE_PARM_DESC(dma8, "8-bit DMA # for GUS Extreme driver.");
module_param_array(dma1, int, NULL, 0444);
MODULE_PARM_DESC(dma1, "GF1 DMA # for GUS Extreme driver.");
module_param_array(joystick_dac, int, NULL, 0444);
MODULE_PARM_DESC(joystick_dac, "Joystick DAC level 0.59V-4.52V or 0.389V-2.98V for GUS Extreme driver.");
module_param_array(channels, int, NULL, 0444);
MODULE_PARM_DESC(channels, "GF1 channels for GUS Extreme driver.");
module_param_array(pcm_channels, int, NULL, 0444);
MODULE_PARM_DESC(pcm_channels, "Reserved PCM channels for GUS Extreme driver.");

static struct platform_device *devices[SNDRV_CARDS];


#define PFX	"gusextreme: "

static int __devinit snd_gusextreme_detect(int dev,
					struct snd_card *card,
					struct snd_gus_card * gus,
					struct snd_es1688 *es1688)
{
	unsigned long flags;
	unsigned char d;

	/*
	 * This is main stuff - enable access to GF1 chip...
	 * I'm not sure, if this will work for card which have
	 * ES1688 chip in another place than 0x220.
         *
         * I used reverse-engineering in DOSEMU. [--jk]
	 *
	 * ULTRINIT.EXE:
	 * 0x230 = 0,2,3
	 * 0x240 = 2,0,1
	 * 0x250 = 2,0,3
	 * 0x260 = 2,2,1
	 */

	spin_lock_irqsave(&es1688->mixer_lock, flags);
	snd_es1688_mixer_write(es1688, 0x40, 0x0b);	/* don't change!!! */
	spin_unlock_irqrestore(&es1688->mixer_lock, flags);
	spin_lock_irqsave(&es1688->reg_lock, flags);
	outb(gf1_port[dev] & 0x040 ? 2 : 0, ES1688P(es1688, INIT1));
	outb(0, 0x201);
	outb(gf1_port[dev] & 0x020 ? 2 : 0, ES1688P(es1688, INIT1));
	outb(0, 0x201);
	outb(gf1_port[dev] & 0x010 ? 3 : 1, ES1688P(es1688, INIT1));
	spin_unlock_irqrestore(&es1688->reg_lock, flags);

	udelay(100);

	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 0);	/* reset GF1 */
	if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 0) {
		snd_printdd("[0x%lx] check 1 failed - 0x%x\n", gus->gf1.port, d);
		return -EIO;
	}
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 1);	/* release reset */
	udelay(160);
	if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 1) {
		snd_printdd("[0x%lx] check 2 failed - 0x%x\n", gus->gf1.port, d);
		return -EIO;
	}
	return 0;
}

static void __devinit snd_gusextreme_init(int dev, struct snd_gus_card * gus)
{
	gus->joystick_dac = joystick_dac[dev];
}

static int __devinit snd_gusextreme_mixer(struct snd_es1688 *chip)
{
	struct snd_card *card = chip->card;
	struct snd_ctl_elem_id id1, id2;
	int err;

	memset(&id1, 0, sizeof(id1));
	memset(&id2, 0, sizeof(id2));
	id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	/* reassign AUX to SYNTHESIZER */
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "Synth Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	/* reassign Master Playback Switch to Synth Playback Switch */
	strcpy(id1.name, "Master Playback Switch");
	strcpy(id2.name, "Synth Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	return 0;
}

static int __devinit snd_gusextreme_probe(struct platform_device *pdev)
{
	int dev = pdev->id;
	static int possible_ess_irqs[] = {5, 9, 10, 7, -1};
	static int possible_ess_dmas[] = {1, 3, 0, -1};
	static int possible_gf1_irqs[] = {5, 11, 12, 9, 7, 15, 3, -1};
	static int possible_gf1_dmas[] = {5, 6, 7, 1, 3, -1};
	int xgf1_irq, xgf1_dma, xess_irq, xmpu_irq, xess_dma;
	struct snd_card *card;
	struct snd_gus_card *gus;
	struct snd_es1688 *es1688;
	struct snd_opl3 *opl3;
	int err;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	xgf1_irq = gf1_irq[dev];
	if (xgf1_irq == SNDRV_AUTO_IRQ) {
		if ((xgf1_irq = snd_legacy_find_free_irq(possible_gf1_irqs)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free IRQ for GF1\n");
			err = -EBUSY;
			goto out;
		}
	}
	xess_irq = irq[dev];
	if (xess_irq == SNDRV_AUTO_IRQ) {
		if ((xess_irq = snd_legacy_find_free_irq(possible_ess_irqs)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free IRQ for ES1688\n");
			err = -EBUSY;
			goto out;
		}
	}
	if (mpu_port[dev] == SNDRV_AUTO_PORT)
		mpu_port[dev] = 0;
	xmpu_irq = mpu_irq[dev];
	if (xmpu_irq > 15)
		xmpu_irq = -1;
	xgf1_dma = dma1[dev];
	if (xgf1_dma == SNDRV_AUTO_DMA) {
		if ((xgf1_dma = snd_legacy_find_free_dma(possible_gf1_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA for GF1\n");
			err = -EBUSY;
			goto out;
		}
	}
	xess_dma = dma8[dev];
	if (xess_dma == SNDRV_AUTO_DMA) {
		if ((xess_dma = snd_legacy_find_free_dma(possible_ess_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA for ES1688\n");
			err = -EBUSY;
			goto out;
		}
	}

	if (port[dev] != SNDRV_AUTO_PORT) {
		err = snd_es1688_create(card, port[dev], mpu_port[dev],
					xess_irq, xmpu_irq, xess_dma,
					ES1688_HW_1688, &es1688);
	} else {
		/* auto-probe legacy ports */
		static unsigned long possible_ports[] = {0x220, 0x240, 0x260};
		int i;
		for (i = 0; i < ARRAY_SIZE(possible_ports); i++) {
			err = snd_es1688_create(card,
						possible_ports[i],
						mpu_port[dev],
						xess_irq, xmpu_irq, xess_dma,
						ES1688_HW_1688, &es1688);
			if (err >= 0) {
				port[dev] = possible_ports[i];
				break;
			}
		}
	}
	if (err < 0)
		goto out;

	if (gf1_port[dev] < 0)
		gf1_port[dev] = port[dev] + 0x20;
	if ((err = snd_gus_create(card,
				  gf1_port[dev],
				  xgf1_irq,
				  xgf1_dma,
				  -1,
				  0, channels[dev],
				  pcm_channels[dev], 0,
				  &gus)) < 0)
		goto out;

	if ((err = snd_gusextreme_detect(dev, card, gus, es1688)) < 0)
		goto out;

	snd_gusextreme_init(dev, gus);
	if ((err = snd_gus_initialize(gus)) < 0)
		goto out;

	if (!gus->ess_flag) {
		snd_printk(KERN_ERR PFX "GUS Extreme soundcard was not detected at 0x%lx\n", gus->gf1.port);
		err = -ENODEV;
		goto out;
	}
	if ((err = snd_es1688_pcm(es1688, 0, NULL)) < 0)
		goto out;

	if ((err = snd_es1688_mixer(es1688)) < 0)
		goto out;

	snd_component_add(card, "ES1688");
	if (pcm_channels[dev] > 0) {
		if ((err = snd_gf1_pcm_new(gus, 1, 1, NULL)) < 0)
			goto out;
	}
	if ((err = snd_gf1_new_mixer(gus)) < 0)
		goto out;

	if ((err = snd_gusextreme_mixer(es1688)) < 0)
		goto out;

	if (snd_opl3_create(card, es1688->port, es1688->port + 2,
			    OPL3_HW_OPL3, 0, &opl3) < 0) {
		printk(KERN_ERR PFX "gusextreme: opl3 not detected at 0x%lx\n", es1688->port);
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 2, NULL)) < 0)
			goto out;
	}

	if (es1688->mpu_port >= 0x300 &&
	    (err = snd_mpu401_uart_new(card, 0, MPU401_HW_ES1688,
					       es1688->mpu_port, 0,
					       xmpu_irq,
					       IRQF_DISABLED,
					       NULL)) < 0)
		goto out;

	sprintf(card->longname, "Gravis UltraSound Extreme at 0x%lx, irq %i&%i, dma %i&%i",
		es1688->port, xgf1_irq, xess_irq, xgf1_dma, xess_dma);

	snd_card_set_dev(card, &pdev->dev);

	if ((err = snd_card_register(card)) < 0)
		goto out;

	platform_set_drvdata(pdev, card);
	return 0;

      out:
	snd_card_free(card);
	return err;
}

static int __devexit snd_gusextreme_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#define GUSEXTREME_DRIVER	"snd_gusextreme"

static struct platform_driver snd_gusextreme_driver = {
	.probe		= snd_gusextreme_probe,
	.remove		= __devexit_p(snd_gusextreme_remove),
	/* FIXME: suspend/resume */
	.driver		= {
		.name	= GUSEXTREME_DRIVER
	},
};

static void __init_or_module snd_gusextreme_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_gusextreme_driver);
}

static int __init alsa_card_gusextreme_init(void)
{
	int i, cards, err;

	err = platform_driver_register(&snd_gusextreme_driver);
	if (err < 0)
		return err;

	cards = 0;
	for (i = 0; i < SNDRV_CARDS; i++) {
		struct platform_device *device;
		if (! enable[i])
			continue;
		device = platform_device_register_simple(GUSEXTREME_DRIVER,
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
		printk(KERN_ERR "GUS Extreme soundcard not found or device busy\n");
#endif
		snd_gusextreme_unregister_all();
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_gusextreme_exit(void)
{
	snd_gusextreme_unregister_all();
}

module_init(alsa_card_gusextreme_init)
module_exit(alsa_card_gusextreme_exit)

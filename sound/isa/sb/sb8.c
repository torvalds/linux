/*
 *  Driver for SoundBlaster 1.0/2.0/Pro soundcards and compatible
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
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/opl3.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Sound Blaster 1.0/2.0/Pro");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Creative Labs,SB 1.0/SB 2.0/SB Pro}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260 */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3 */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Sound Blaster soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Sound Blaster soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Sound Blaster soundcard.");
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for SB8 driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for SB8 driver.");
module_param_array(dma8, int, NULL, 0444);
MODULE_PARM_DESC(dma8, "8-bit DMA # for SB8 driver.");

static struct platform_device *devices[SNDRV_CARDS];

struct snd_sb8 {
	struct resource *fm_res;	/* used to block FM i/o region for legacy cards */
	struct snd_sb *chip;
};

static irqreturn_t snd_sb8_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct snd_sb *chip = dev_id;

	if (chip->open & SB_OPEN_PCM) {
		return snd_sb8dsp_interrupt(chip);
	} else {
		return snd_sb8dsp_midi_interrupt(chip);
	}
}

static void snd_sb8_free(struct snd_card *card)
{
	struct snd_sb8 *acard = (struct snd_sb8 *)card->private_data;

	if (acard == NULL)
		return;
	release_and_free_resource(acard->fm_res);
}

static int __init snd_sb8_probe(struct platform_device *pdev)
{
	int dev = pdev->id;
	struct snd_sb *chip;
	struct snd_card *card;
	struct snd_sb8 *acard;
	struct snd_opl3 *opl3;
	int err;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct snd_sb8));
	if (card == NULL)
		return -ENOMEM;
	acard = card->private_data;
	card->private_free = snd_sb8_free;

	/* block the 0x388 port to avoid PnP conflicts */
	acard->fm_res = request_region(0x388, 4, "SoundBlaster FM");

	if (port[dev] != SNDRV_AUTO_PORT) {
		if ((err = snd_sbdsp_create(card, port[dev], irq[dev],
					    snd_sb8_interrupt,
					    dma8[dev],
					    -1,
					    SB_HW_AUTO,
					    &chip)) < 0)
			goto _err;
	} else {
		/* auto-probe legacy ports */
		static unsigned long possible_ports[] = {
			0x220, 0x240, 0x260,
		};
		int i;
		for (i = 0; i < ARRAY_SIZE(possible_ports); i++) {
			err = snd_sbdsp_create(card, possible_ports[i],
					       irq[dev],
					       snd_sb8_interrupt,
					       dma8[dev],
					       -1,
					       SB_HW_AUTO,
					       &chip);
			if (err >= 0) {
				port[dev] = possible_ports[i];
				break;
			}
		}
		if (i >= ARRAY_SIZE(possible_ports))
			goto _err;
	}
	acard->chip = chip;
			
	if (chip->hardware >= SB_HW_16) {
		if (chip->hardware == SB_HW_ALS100)
			snd_printk(KERN_WARNING "ALS100 chip detected at 0x%lx, try snd-als100 module\n",
				    port[dev]);
		else
			snd_printk(KERN_WARNING "SB 16 chip detected at 0x%lx, try snd-sb16 module\n",
				   port[dev]);
		err = -ENODEV;
		goto _err;
	}

	if ((err = snd_sb8dsp_pcm(chip, 0, NULL)) < 0)
		goto _err;

	if ((err = snd_sbmixer_new(chip)) < 0)
		goto _err;

	if (chip->hardware == SB_HW_10 || chip->hardware == SB_HW_20) {
		if ((err = snd_opl3_create(card, chip->port + 8, 0,
					   OPL3_HW_AUTO, 1,
					   &opl3)) < 0) {
			snd_printk(KERN_WARNING "sb8: no OPL device at 0x%lx\n", chip->port + 8);
		}
	} else {
		if ((err = snd_opl3_create(card, chip->port, chip->port + 2,
					   OPL3_HW_AUTO, 1,
					   &opl3)) < 0) {
			snd_printk(KERN_WARNING "sb8: no OPL device at 0x%lx-0x%lx\n",
				   chip->port, chip->port + 2);
		}
	}
	if (err >= 0) {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0)
			goto _err;
	}

	if ((err = snd_sb8dsp_midi(chip, 0, NULL)) < 0)
		goto _err;

	strcpy(card->driver, chip->hardware == SB_HW_PRO ? "SB Pro" : "SB8");
	strcpy(card->shortname, chip->name);
	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
		chip->name,
		chip->port,
		irq[dev], dma8[dev]);

	snd_card_set_dev(card, &pdev->dev);

	if ((err = snd_card_register(card)) < 0)
		goto _err;

	platform_set_drvdata(pdev, card);
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

static int snd_sb8_remove(struct platform_device *pdev)
{
	snd_card_free(platform_get_drvdata(pdev));
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int snd_sb8_suspend(struct platform_device *dev, pm_message_t state)
{
	struct snd_card *card = platform_get_drvdata(dev);
	struct snd_sb8 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
	snd_sbmixer_suspend(chip);
	return 0;
}

static int snd_sb8_resume(struct platform_device *dev)
{
	struct snd_card *card = platform_get_drvdata(dev);
	struct snd_sb8 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_sbdsp_reset(chip);
	snd_sbmixer_resume(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

#define SND_SB8_DRIVER	"snd_sb8"

static struct platform_driver snd_sb8_driver = {
	.probe		= snd_sb8_probe,
	.remove		= snd_sb8_remove,
#ifdef CONFIG_PM
	.suspend	= snd_sb8_suspend,
	.resume		= snd_sb8_resume,
#endif
	.driver		= {
		.name	= SND_SB8_DRIVER
	},
};

static void __init_or_module snd_sb8_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_sb8_driver);
}

static int __init alsa_card_sb8_init(void)
{
	int i, cards, err;

	err = platform_driver_register(&snd_sb8_driver);
	if (err < 0)
		return err;

	cards = 0;
	for (i = 0; i < SNDRV_CARDS; i++) {
		struct platform_device *device;
		if (! enable[i])
			continue;
		device = platform_device_register_simple(SND_SB8_DRIVER,
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
		snd_printk(KERN_ERR "Sound Blaster soundcard not found or device busy\n");
#endif
		snd_sb8_unregister_all();
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_sb8_exit(void)
{
	snd_sb8_unregister_all();
}

module_init(alsa_card_sb8_init)
module_exit(alsa_card_sb8_exit)

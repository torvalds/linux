/*
 *  Generic driver for AD1848/AD1847/CS4248 chips (0.1 Alpha)
 *  Copyright (c) by Tugrul Galatali <galatalt@stuy.edu>,
 *                   Jaroslav Kysela <perex@suse.cz>
 *  Based on card-4232.c by Jaroslav Kysela <perex@suse.cz>
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
#include <linux/isa.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/ad1848.h>
#include <sound/initval.h>

#define CRD_NAME "Generic AD1848/AD1847/CS4248"
#define DEV_NAME "ad1848"

MODULE_DESCRIPTION(CRD_NAME);
MODULE_AUTHOR("Tugrul Galatali <galatalt@stuy.edu>, Jaroslav Kysela <perex@suse.cz>");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Analog Devices,AD1848},"
	        "{Analog Devices,AD1847},"
		"{Crystal Semiconductors,CS4248}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */
static int thinkpad[SNDRV_CARDS];			/* Thinkpad special case */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CRD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CRD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " CRD_NAME " soundcard.");
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for " CRD_NAME " driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for " CRD_NAME " driver.");
module_param_array(dma1, int, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 # for " CRD_NAME " driver.");
module_param_array(thinkpad, bool, NULL, 0444);
MODULE_PARM_DESC(thinkpad, "Enable only for the onboard CS4248 of IBM Thinkpad 360/750/755 series.");

static int __devinit snd_ad1848_match(struct device *dev, unsigned int n)
{
	if (!enable[n])
		return 0;

	if (port[n] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR "%s: please specify port\n", dev->bus_id);
		return 0;
	}
	if (irq[n] == SNDRV_AUTO_IRQ) {
		snd_printk(KERN_ERR "%s: please specify irq\n", dev->bus_id);
		return 0;	
	}
	if (dma1[n] == SNDRV_AUTO_DMA) {
		snd_printk(KERN_ERR "%s: please specify dma1\n", dev->bus_id);
		return 0;
	}
	return 1;
}

static int __devinit snd_ad1848_probe(struct device *dev, unsigned int n)
{
	struct snd_card *card;
	struct snd_ad1848 *chip;
	struct snd_pcm *pcm;
	int error;

	card = snd_card_new(index[n], id[n], THIS_MODULE, 0);
	if (!card)
		return -EINVAL;

	error = snd_ad1848_create(card, port[n], irq[n], dma1[n],
			thinkpad[n] ? AD1848_HW_THINKPAD : AD1848_HW_DETECT, &chip);
	if (error < 0)
		goto out;

	card->private_data = chip;

	error = snd_ad1848_pcm(chip, 0, &pcm);
	if (error < 0)
		goto out;

	error = snd_ad1848_mixer(chip);
	if (error < 0)
		goto out;

	strcpy(card->driver, "AD1848");
	strcpy(card->shortname, pcm->name);

	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
		pcm->name, chip->port, irq[n], dma1[n]);
	if (thinkpad[n])
		strcat(card->longname, " [Thinkpad]");

	snd_card_set_dev(card, dev);

	error = snd_card_register(card);
	if (error < 0)
		goto out;

	dev_set_drvdata(dev, card);
	return 0;

out:	snd_card_free(card);
	return error;
}

static int __devexit snd_ad1848_remove(struct device *dev, unsigned int n)
{
	snd_card_free(dev_get_drvdata(dev));
	dev_set_drvdata(dev, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int snd_ad1848_suspend(struct device *dev, unsigned int n, pm_message_t state)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_ad1848 *chip = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	chip->suspend(chip);
	return 0;
}

static int snd_ad1848_resume(struct device *dev, unsigned int n)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_ad1848 *chip = card->private_data;

	chip->resume(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

static struct isa_driver snd_ad1848_driver = {
	.match		= snd_ad1848_match,
	.probe		= snd_ad1848_probe,
	.remove		= __devexit_p(snd_ad1848_remove),
#ifdef CONFIG_PM
	.suspend	= snd_ad1848_suspend,
	.resume		= snd_ad1848_resume,
#endif
	.driver		= {
		.name	= DEV_NAME
	}
};

static int __init alsa_card_ad1848_init(void)
{
	return isa_register_driver(&snd_ad1848_driver, SNDRV_CARDS);
}

static void __exit alsa_card_ad1848_exit(void)
{
	isa_unregister_driver(&snd_ad1848_driver);
}

module_init(alsa_card_ad1848_init);
module_exit(alsa_card_ad1848_exit);

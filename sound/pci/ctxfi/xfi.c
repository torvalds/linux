/*
 * xfi linux driver.
 *
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include "ctatc.h"
#include "ctdrv.h"

MODULE_AUTHOR("Creative Technology Ltd");
MODULE_DESCRIPTION("X-Fi driver version 1.03");
MODULE_LICENSE("GPLv2");
MODULE_SUPPORTED_DEVICE("{{Creative Labs, Sound Blaster X-Fi}");

static unsigned int reference_rate = 48000;
static unsigned int multiple = 2;
module_param(reference_rate, uint, S_IRUGO);
module_param(multiple, uint, S_IRUGO);

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

static struct pci_device_id ct_pci_dev_ids[] = {
	/* only X-Fi is supported, so... */
	{ PCI_DEVICE(PCI_VENDOR_CREATIVE, PCI_DEVICE_CREATIVE_20K1) },
	{ PCI_DEVICE(PCI_VENDOR_CREATIVE, PCI_DEVICE_CREATIVE_20K2) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ct_pci_dev_ids);

static int __devinit
ct_card_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct ct_atc *atc;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}
	err = snd_card_create(index[dev], id[dev], THIS_MODULE, 0, &card);
	if (err)
		return err;
	if ((reference_rate != 48000) && (reference_rate != 44100)) {
		printk(KERN_ERR "Invalid reference_rate value %u!!!\n"
				"The valid values for reference_rate "
				"are 48000 and 44100.\nValue 48000 is "
				"assumed.\n", reference_rate);
		reference_rate = 48000;
	}
	if ((multiple != 1) && (multiple != 2)) {
		printk(KERN_ERR "Invalid multiple value %u!!!\nThe valid "
				"values for multiple are 1 and 2.\nValue 2 "
				"is assumed.\n", multiple);
		multiple = 2;
	}
	err = ct_atc_create(card, pci, reference_rate, multiple, &atc);
	if (err < 0)
		goto error;

	card->private_data = atc;

	/* Create alsa devices supported by this card */
	err = atc->create_alsa_devs(atc);
	if (err < 0)
		goto error;

	strcpy(card->driver, "SB-XFi");
	strcpy(card->shortname, "Creative X-Fi");
	strcpy(card->longname, "Creative ALSA Driver X-Fi");

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	pci_set_drvdata(pci, card);
	dev++;

	return 0;

error:
	snd_card_free(card);
	return err;
}

static void __devexit ct_card_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver ct_driver = {
	.name = "SB-XFi",
	.id_table = ct_pci_dev_ids,
	.probe = ct_card_probe,
	.remove = __devexit_p(ct_card_remove),
};

static int __init ct_card_init(void)
{
	return pci_register_driver(&ct_driver);
}

static void __exit ct_card_exit(void)
{
	pci_unregister_driver(&ct_driver);
}

module_init(ct_card_init)
module_exit(ct_card_exit)

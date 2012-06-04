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
#include <linux/pci_ids.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/initval.h>
#include "ctatc.h"
#include "cthardware.h"

MODULE_AUTHOR("Creative Technology Ltd");
MODULE_DESCRIPTION("X-Fi driver version 1.03");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{Creative Labs, Sound Blaster X-Fi}");

static unsigned int reference_rate = 48000;
static unsigned int multiple = 2;
MODULE_PARM_DESC(reference_rate, "Reference rate (default=48000)");
module_param(reference_rate, uint, S_IRUGO);
MODULE_PARM_DESC(multiple, "Rate multiplier (default=2)");
module_param(multiple, uint, S_IRUGO);

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static unsigned int subsystem[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Creative X-Fi driver");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Creative X-Fi driver");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Creative X-Fi driver");
module_param_array(subsystem, int, NULL, 0444);
MODULE_PARM_DESC(subsystem, "Override subsystem ID for Creative X-Fi driver");

static DEFINE_PCI_DEVICE_TABLE(ct_pci_dev_ids) = {
	/* only X-Fi is supported, so... */
	{ PCI_DEVICE(PCI_VENDOR_ID_CREATIVE, PCI_DEVICE_ID_CREATIVE_20K1),
	  .driver_data = ATC20K1,
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_CREATIVE, PCI_DEVICE_ID_CREATIVE_20K2),
	  .driver_data = ATC20K2,
	},
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
		printk(KERN_ERR "ctxfi: Invalid reference_rate value %u!!!\n",
		       reference_rate);
		printk(KERN_ERR "ctxfi: The valid values for reference_rate "
		       "are 48000 and 44100, Value 48000 is assumed.\n");
		reference_rate = 48000;
	}
	if ((multiple != 1) && (multiple != 2) && (multiple != 4)) {
		printk(KERN_ERR "ctxfi: Invalid multiple value %u!!!\n",
		       multiple);
		printk(KERN_ERR "ctxfi: The valid values for multiple are "
		       "1, 2 and 4, Value 2 is assumed.\n");
		multiple = 2;
	}
	err = ct_atc_create(card, pci, reference_rate, multiple,
			    pci_id->driver_data, subsystem[dev], &atc);
	if (err < 0)
		goto error;

	card->private_data = atc;

	/* Create alsa devices supported by this card */
	err = ct_atc_create_alsa_devs(atc);
	if (err < 0)
		goto error;

	strcpy(card->driver, "SB-XFi");
	strcpy(card->shortname, "Creative X-Fi");
	snprintf(card->longname, sizeof(card->longname), "%s %s %s",
		 card->shortname, atc->chip_name, atc->model_name);

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

#ifdef CONFIG_PM
static int ct_card_suspend(struct pci_dev *pci, pm_message_t state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct ct_atc *atc = card->private_data;

	return atc->suspend(atc, state);
}

static int ct_card_resume(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct ct_atc *atc = card->private_data;

	return atc->resume(atc);
}
#endif

static struct pci_driver ct_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ct_pci_dev_ids,
	.probe = ct_card_probe,
	.remove = __devexit_p(ct_card_remove),
#ifdef CONFIG_PM
	.suspend = ct_card_suspend,
	.resume = ct_card_resume,
#endif
};

module_pci_driver(ct_driver);

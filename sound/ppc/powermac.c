/*
 * Driver for PowerMac AWACS
 * Copyright (c) 2001 by Takashi Iwai <tiwai@suse.de>
 *   based on dmasound.c.
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
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include "pmac.h"
#include "awacs.h"
#include "burgundy.h"

#define CHIP_NAME "PMac"

MODULE_DESCRIPTION("PowerMac");
MODULE_SUPPORTED_DEVICE("{{Apple,PowerMac}}");
MODULE_LICENSE("GPL");

static int index = SNDRV_DEFAULT_IDX1;		/* Index 0-MAX */
static char *id = SNDRV_DEFAULT_STR1;		/* ID for this card */
static int enable_beep = 1;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for " CHIP_NAME " soundchip.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for " CHIP_NAME " soundchip.");
module_param(enable_beep, bool, 0444);
MODULE_PARM_DESC(enable_beep, "Enable beep using PCM.");

static struct platform_device *device;


/*
 */

static int __init snd_pmac_probe(struct platform_device *devptr)
{
	struct snd_card *card;
	struct snd_pmac *chip;
	char *name_ext;
	int err;

	card = snd_card_new(index, id, THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	if ((err = snd_pmac_new(card, &chip)) < 0)
		goto __error;
	card->private_data = chip;

	switch (chip->model) {
	case PMAC_BURGUNDY:
		strcpy(card->driver, "PMac Burgundy");
		strcpy(card->shortname, "PowerMac Burgundy");
		sprintf(card->longname, "%s (Dev %d) Sub-frame %d",
			card->shortname, chip->device_id, chip->subframe);
		if ((err = snd_pmac_burgundy_init(chip)) < 0)
			goto __error;
		break;
	case PMAC_DACA:
		strcpy(card->driver, "PMac DACA");
		strcpy(card->shortname, "PowerMac DACA");
		sprintf(card->longname, "%s (Dev %d) Sub-frame %d",
			card->shortname, chip->device_id, chip->subframe);
		if ((err = snd_pmac_daca_init(chip)) < 0)
			goto __error;
		break;
	case PMAC_TUMBLER:
	case PMAC_SNAPPER:
		name_ext = chip->model == PMAC_TUMBLER ? "Tumbler" : "Snapper";
		sprintf(card->driver, "PMac %s", name_ext);
		sprintf(card->shortname, "PowerMac %s", name_ext);
		sprintf(card->longname, "%s (Dev %d) Sub-frame %d",
			card->shortname, chip->device_id, chip->subframe);
		if ( snd_pmac_tumbler_init(chip) < 0 || snd_pmac_tumbler_post_init() < 0)
			goto __error;
		break;
	case PMAC_AWACS:
	case PMAC_SCREAMER:
		name_ext = chip->model == PMAC_SCREAMER ? "Screamer" : "AWACS";
		sprintf(card->driver, "PMac %s", name_ext);
		sprintf(card->shortname, "PowerMac %s", name_ext);
		if (chip->is_pbook_3400)
			name_ext = " [PB3400]";
		else if (chip->is_pbook_G3)
			name_ext = " [PBG3]";
		else
			name_ext = "";
		sprintf(card->longname, "%s%s Rev %d",
			card->shortname, name_ext, chip->revision);
		if ((err = snd_pmac_awacs_init(chip)) < 0)
			goto __error;
		break;
	default:
		snd_printk("unsupported hardware %d\n", chip->model);
		err = -EINVAL;
		goto __error;
	}

	if ((err = snd_pmac_pcm_new(chip)) < 0)
		goto __error;

	chip->initialized = 1;
	if (enable_beep)
		snd_pmac_attach_beep(chip);

	snd_card_set_dev(card, &devptr->dev);

	if ((err = snd_card_register(card)) < 0)
		goto __error;

	platform_set_drvdata(devptr, card);
	return 0;

__error:
	snd_card_free(card);
	return err;
}


static int __devexit snd_pmac_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int snd_pmac_driver_suspend(struct platform_device *devptr, pm_message_t state)
{
	struct snd_card *card = platform_get_drvdata(devptr);
	snd_pmac_suspend(card->private_data);
	return 0;
}

static int snd_pmac_driver_resume(struct platform_device *devptr)
{
	struct snd_card *card = platform_get_drvdata(devptr);
	snd_pmac_resume(card->private_data);
	return 0;
}
#endif

#define SND_PMAC_DRIVER		"snd_powermac"

static struct platform_driver snd_pmac_driver = {
	.probe		= snd_pmac_probe,
	.remove		= __devexit_p(snd_pmac_remove),
#ifdef CONFIG_PM
	.suspend	= snd_pmac_driver_suspend,
	.resume		= snd_pmac_driver_resume,
#endif
	.driver		= {
		.name	= SND_PMAC_DRIVER
	},
};

static int __init alsa_card_pmac_init(void)
{
	int err;

	if ((err = platform_driver_register(&snd_pmac_driver)) < 0)
		return err;
	device = platform_device_register_simple(SND_PMAC_DRIVER, -1, NULL, 0);
	if (!IS_ERR(device)) {
		if (platform_get_drvdata(device))
			return 0;
		platform_device_unregister(device);
		err = -ENODEV;
	} else
		err = PTR_ERR(device);
	platform_driver_unregister(&snd_pmac_driver);
	return err;

}

static void __exit alsa_card_pmac_exit(void)
{
	platform_device_unregister(device);
	platform_driver_unregister(&snd_pmac_driver);
}

module_init(alsa_card_pmac_init)
module_exit(alsa_card_pmac_exit)

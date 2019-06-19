// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux driver for M2Tech hiFace compatible devices
 *
 * Copyright 2012-2013 (C) M2TECH S.r.l and Amarula Solutions B.V.
 *
 * Authors:  Michael Trimarchi <michael@amarulasolutions.com>
 *           Antonio Ospite <ao2@amarulasolutions.com>
 *
 * The driver is based on the work done in TerraTec DMX 6Fire USB
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <sound/initval.h>

#include "chip.h"
#include "pcm.h"

MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_AUTHOR("Antonio Ospite <ao2@amarulasolutions.com>");
MODULE_DESCRIPTION("M2Tech hiFace USB-SPDIF audio driver");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{M2Tech,Young},"
			 "{M2Tech,hiFace},"
			 "{M2Tech,North Star},"
			 "{M2Tech,W4S Young},"
			 "{M2Tech,Corrson},"
			 "{M2Tech,AUDIA},"
			 "{M2Tech,SL Audio},"
			 "{M2Tech,Empirical},"
			 "{M2Tech,Rockna},"
			 "{M2Tech,Pathos},"
			 "{M2Tech,Metronome},"
			 "{M2Tech,CAD},"
			 "{M2Tech,Audio Esclusive},"
			 "{M2Tech,Rotel},"
			 "{M2Tech,Eeaudio},"
			 "{The Chord Company,CHORD},"
			 "{AVA Group A/S,Vitus}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; /* Index 0-max */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; /* Id for card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP; /* Enable this card */

#define DRIVER_NAME "snd-usb-hiface"
#define CARD_NAME "hiFace"

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " CARD_NAME " soundcard.");

static DEFINE_MUTEX(register_mutex);

struct hiface_vendor_quirk {
	const char *device_name;
	u8 extra_freq;
};

static int hiface_chip_create(struct usb_interface *intf,
			      struct usb_device *device, int idx,
			      const struct hiface_vendor_quirk *quirk,
			      struct hiface_chip **rchip)
{
	struct snd_card *card = NULL;
	struct hiface_chip *chip;
	int ret;
	int len;

	*rchip = NULL;

	/* if we are here, card can be registered in alsa. */
	ret = snd_card_new(&intf->dev, index[idx], id[idx], THIS_MODULE,
			   sizeof(*chip), &card);
	if (ret < 0) {
		dev_err(&device->dev, "cannot create alsa card.\n");
		return ret;
	}

	strlcpy(card->driver, DRIVER_NAME, sizeof(card->driver));

	if (quirk && quirk->device_name)
		strlcpy(card->shortname, quirk->device_name, sizeof(card->shortname));
	else
		strlcpy(card->shortname, "M2Tech generic audio", sizeof(card->shortname));

	strlcat(card->longname, card->shortname, sizeof(card->longname));
	len = strlcat(card->longname, " at ", sizeof(card->longname));
	if (len < sizeof(card->longname))
		usb_make_path(device, card->longname + len,
			      sizeof(card->longname) - len);

	chip = card->private_data;
	chip->dev = device;
	chip->card = card;

	*rchip = chip;
	return 0;
}

static int hiface_chip_probe(struct usb_interface *intf,
			     const struct usb_device_id *usb_id)
{
	const struct hiface_vendor_quirk *quirk = (struct hiface_vendor_quirk *)usb_id->driver_info;
	int ret;
	int i;
	struct hiface_chip *chip;
	struct usb_device *device = interface_to_usbdev(intf);

	ret = usb_set_interface(device, 0, 0);
	if (ret != 0) {
		dev_err(&device->dev, "can't set first interface for " CARD_NAME " device.\n");
		return -EIO;
	}

	/* check whether the card is already registered */
	chip = NULL;
	mutex_lock(&register_mutex);

	for (i = 0; i < SNDRV_CARDS; i++)
		if (enable[i])
			break;

	if (i >= SNDRV_CARDS) {
		dev_err(&device->dev, "no available " CARD_NAME " audio device\n");
		ret = -ENODEV;
		goto err;
	}

	ret = hiface_chip_create(intf, device, i, quirk, &chip);
	if (ret < 0)
		goto err;

	ret = hiface_pcm_init(chip, quirk ? quirk->extra_freq : 0);
	if (ret < 0)
		goto err_chip_destroy;

	ret = snd_card_register(chip->card);
	if (ret < 0) {
		dev_err(&device->dev, "cannot register " CARD_NAME " card\n");
		goto err_chip_destroy;
	}

	mutex_unlock(&register_mutex);

	usb_set_intfdata(intf, chip);
	return 0;

err_chip_destroy:
	snd_card_free(chip->card);
err:
	mutex_unlock(&register_mutex);
	return ret;
}

static void hiface_chip_disconnect(struct usb_interface *intf)
{
	struct hiface_chip *chip;
	struct snd_card *card;

	chip = usb_get_intfdata(intf);
	if (!chip)
		return;

	card = chip->card;

	/* Make sure that the userspace cannot create new request */
	snd_card_disconnect(card);

	hiface_pcm_abort(chip);
	snd_card_free_when_closed(card);
}

static const struct usb_device_id device_table[] = {
	{
		USB_DEVICE(0x04b4, 0x0384),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Young",
			.extra_freq = 1,
		}
	},
	{
		USB_DEVICE(0x04b4, 0x930b),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "hiFace",
		}
	},
	{
		USB_DEVICE(0x04b4, 0x931b),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "North Star",
		}
	},
	{
		USB_DEVICE(0x04b4, 0x931c),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "W4S Young",
		}
	},
	{
		USB_DEVICE(0x04b4, 0x931d),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Corrson",
		}
	},
	{
		USB_DEVICE(0x04b4, 0x931e),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "AUDIA",
		}
	},
	{
		USB_DEVICE(0x04b4, 0x931f),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "SL Audio",
		}
	},
	{
		USB_DEVICE(0x04b4, 0x9320),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Empirical",
		}
	},
	{
		USB_DEVICE(0x04b4, 0x9321),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Rockna",
		}
	},
	{
		USB_DEVICE(0x249c, 0x9001),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Pathos",
		}
	},
	{
		USB_DEVICE(0x249c, 0x9002),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Metronome",
		}
	},
	{
		USB_DEVICE(0x249c, 0x9006),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "CAD",
		}
	},
	{
		USB_DEVICE(0x249c, 0x9008),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Audio Esclusive",
		}
	},
	{
		USB_DEVICE(0x249c, 0x931c),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Rotel",
		}
	},
	{
		USB_DEVICE(0x249c, 0x932c),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Eeaudio",
		}
	},
	{
		USB_DEVICE(0x245f, 0x931c),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "CHORD",
		}
	},
	{
		USB_DEVICE(0x25c6, 0x9002),
		.driver_info = (unsigned long)&(const struct hiface_vendor_quirk) {
			.device_name = "Vitus",
		}
	},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

static struct usb_driver hiface_usb_driver = {
	.name = DRIVER_NAME,
	.probe = hiface_chip_probe,
	.disconnect = hiface_chip_disconnect,
	.id_table = device_table,
};

module_usb_driver(hiface_usb_driver);

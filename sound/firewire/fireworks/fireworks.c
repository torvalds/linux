/*
 * fireworks.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

/*
 * Fireworks is a board module which Echo Audio produced. This module consists
 * of three chipsets:
 *  - Communication chipset for IEEE1394 PHY/Link and IEC 61883-1/6
 *  - DSP or/and FPGA for signal processing
 *  - Flash Memory to store firmwares
 */

#include "fireworks.h"

MODULE_DESCRIPTION("Echo Fireworks driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

static int index[SNDRV_CARDS]	= SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS]	= SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS]	= SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable Fireworks sound card");

static DEFINE_MUTEX(devices_mutex);
static DECLARE_BITMAP(devices_used, SNDRV_CARDS);

#define VENDOR_LOUD			0x000ff2
#define  MODEL_MACKIE_400F		0x00400f
#define  MODEL_MACKIE_1200F		0x01200f

#define VENDOR_ECHO			0x001486
#define  MODEL_ECHO_AUDIOFIRE_12	0x00af12
#define  MODEL_ECHO_AUDIOFIRE_12HD	0x0af12d
#define  MODEL_ECHO_AUDIOFIRE_12_APPLE	0x0af12a
/* This is applied for AudioFire8 (until 2009 July) */
#define  MODEL_ECHO_AUDIOFIRE_8		0x000af8
#define  MODEL_ECHO_AUDIOFIRE_2		0x000af2
#define  MODEL_ECHO_AUDIOFIRE_4		0x000af4
/* AudioFire9 is applied for AudioFire8(since 2009 July) and AudioFirePre8 */
#define  MODEL_ECHO_AUDIOFIRE_9		0x000af9
/* unknown as product */
#define  MODEL_ECHO_FIREWORKS_8		0x0000f8
#define  MODEL_ECHO_FIREWORKS_HDMI	0x00afd1

#define VENDOR_GIBSON			0x00075b
/* for Robot Interface Pack of Dark Fire, Dusk Tiger, Les Paul Standard 2010 */
#define  MODEL_GIBSON_RIP		0x00afb2
/* unknown as product */
#define  MODEL_GIBSON_GOLDTOP		0x00afb9

static void
efw_card_free(struct snd_card *card)
{
	struct snd_efw *efw = card->private_data;

	if (efw->card_index >= 0) {
		mutex_lock(&devices_mutex);
		clear_bit(efw->card_index, devices_used);
		mutex_unlock(&devices_mutex);
	}

	mutex_destroy(&efw->mutex);
}

static int
efw_probe(struct fw_unit *unit,
	  const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_efw *efw;
	int card_index, err;

	mutex_lock(&devices_mutex);

	/* check registered cards */
	for (card_index = 0; card_index < SNDRV_CARDS; ++card_index) {
		if (!test_bit(card_index, devices_used) && enable[card_index])
			break;
	}
	if (card_index >= SNDRV_CARDS) {
		err = -ENOENT;
		goto end;
	}

	err = snd_card_new(&unit->device, index[card_index], id[card_index],
			   THIS_MODULE, sizeof(struct snd_efw), &card);
	if (err < 0)
		goto end;
	efw = card->private_data;
	efw->card_index = card_index;
	set_bit(card_index, devices_used);
	card->private_free = efw_card_free;

	efw->card = card;
	efw->unit = unit;
	mutex_init(&efw->mutex);
	spin_lock_init(&efw->lock);

	strcpy(efw->card->driver, "Fireworks");
	strcpy(efw->card->shortname, efw->card->driver);
	strcpy(efw->card->longname, efw->card->driver);
	strcpy(efw->card->mixername, efw->card->driver);

	err = snd_card_register(card);
	if (err < 0)
		goto error;
	dev_set_drvdata(&unit->device, efw);
end:
	mutex_unlock(&devices_mutex);
	return err;
error:
	mutex_unlock(&devices_mutex);
	snd_card_free(card);
	return err;
}

static void efw_update(struct fw_unit *unit)
{
	return;
}

static void efw_remove(struct fw_unit *unit)
{
	struct snd_efw *efw = dev_get_drvdata(&unit->device);
	snd_card_disconnect(efw->card);
	snd_card_free_when_closed(efw->card);
}

static const struct ieee1394_device_id efw_id_table[] = {
	SND_EFW_DEV_ENTRY(VENDOR_LOUD, MODEL_MACKIE_400F),
	SND_EFW_DEV_ENTRY(VENDOR_LOUD, MODEL_MACKIE_1200F),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_8),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_12),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_12HD),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_12_APPLE),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_2),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_4),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_9),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_FIREWORKS_8),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_FIREWORKS_HDMI),
	SND_EFW_DEV_ENTRY(VENDOR_GIBSON, MODEL_GIBSON_RIP),
	SND_EFW_DEV_ENTRY(VENDOR_GIBSON, MODEL_GIBSON_GOLDTOP),
	{}
};
MODULE_DEVICE_TABLE(ieee1394, efw_id_table);

static struct fw_driver efw_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "snd-fireworks",
		.bus = &fw_bus_type,
	},
	.probe    = efw_probe,
	.update   = efw_update,
	.remove   = efw_remove,
	.id_table = efw_id_table,
};

static int __init snd_efw_init(void)
{
	return driver_register(&efw_driver.driver);
}

static void __exit snd_efw_exit(void)
{
	driver_unregister(&efw_driver.driver);
	mutex_destroy(&devices_mutex);
}

module_init(snd_efw_init);
module_exit(snd_efw_exit);

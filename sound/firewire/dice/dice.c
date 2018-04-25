/*
 * TC Applied Technologies Digital Interface Communications Engine driver
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "dice.h"

MODULE_DESCRIPTION("DICE driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");

#define OUI_WEISS		0x001c6a
#define OUI_LOUD		0x000ff2
#define OUI_FOCUSRITE		0x00130e
#define OUI_TCELECTRONIC	0x000166

#define DICE_CATEGORY_ID	0x04
#define WEISS_CATEGORY_ID	0x00
#define LOUD_CATEGORY_ID	0x10

/*
 * Some models support several isochronous channels, while these streams are not
 * always available. In this case, add the model name to this list.
 */
static bool force_two_pcm_support(struct fw_unit *unit)
{
	static const char *const models[] = {
		/* TC Electronic models. */
		"StudioKonnekt48",
		/* Focusrite models. */
		"SAFFIRE_PRO_40",
		"LIQUID_SAFFIRE_56",
		"SAFFIRE_PRO_40_1",
	};
	char model[32];
	unsigned int i;
	int err;

	err = fw_csr_string(unit->directory, CSR_MODEL, model, sizeof(model));
	if (err < 0)
		return false;

	for (i = 0; i < ARRAY_SIZE(models); i++) {
		if (strcmp(models[i], model) == 0)
			break;
	}

	return i < ARRAY_SIZE(models);
}

static int check_dice_category(struct fw_unit *unit)
{
	struct fw_device *device = fw_parent_device(unit);
	struct fw_csr_iterator it;
	int key, val, vendor = -1, model = -1;
	unsigned int category;

	/*
	 * Check that GUID and unit directory are constructed according to DICE
	 * rules, i.e., that the specifier ID is the GUID's OUI, and that the
	 * GUID chip ID consists of the 8-bit category ID, the 10-bit product
	 * ID, and a 22-bit serial number.
	 */
	fw_csr_iterator_init(&it, unit->directory);
	while (fw_csr_iterator_next(&it, &key, &val)) {
		switch (key) {
		case CSR_SPECIFIER_ID:
			vendor = val;
			break;
		case CSR_MODEL:
			model = val;
			break;
		}
	}

	if (vendor == OUI_FOCUSRITE || vendor == OUI_TCELECTRONIC) {
		if (force_two_pcm_support(unit))
			return 0;
	}

	if (vendor == OUI_WEISS)
		category = WEISS_CATEGORY_ID;
	else if (vendor == OUI_LOUD)
		category = LOUD_CATEGORY_ID;
	else
		category = DICE_CATEGORY_ID;
	if (device->config_rom[3] != ((vendor << 8) | category) ||
	    device->config_rom[4] >> 22 != model)
		return -ENODEV;

	return 0;
}

static int check_clock_caps(struct snd_dice *dice)
{
	__be32 value;
	int err;

	/* some very old firmwares don't tell about their clock support */
	if (dice->clock_caps > 0) {
		err = snd_dice_transaction_read_global(dice,
						GLOBAL_CLOCK_CAPABILITIES,
						&value, 4);
		if (err < 0)
			return err;
		dice->clock_caps = be32_to_cpu(value);
	} else {
		/* this should be supported by any device */
		dice->clock_caps = CLOCK_CAP_RATE_44100 |
				   CLOCK_CAP_RATE_48000 |
				   CLOCK_CAP_SOURCE_ARX1 |
				   CLOCK_CAP_SOURCE_INTERNAL;
	}

	return 0;
}

static void dice_card_strings(struct snd_dice *dice)
{
	struct snd_card *card = dice->card;
	struct fw_device *dev = fw_parent_device(dice->unit);
	char vendor[32], model[32];
	unsigned int i;
	int err;

	strcpy(card->driver, "DICE");

	strcpy(card->shortname, "DICE");
	BUILD_BUG_ON(NICK_NAME_SIZE < sizeof(card->shortname));
	err = snd_dice_transaction_read_global(dice, GLOBAL_NICK_NAME,
					       card->shortname,
					       sizeof(card->shortname));
	if (err >= 0) {
		/* DICE strings are returned in "always-wrong" endianness */
		BUILD_BUG_ON(sizeof(card->shortname) % 4 != 0);
		for (i = 0; i < sizeof(card->shortname); i += 4)
			swab32s((u32 *)&card->shortname[i]);
		card->shortname[sizeof(card->shortname) - 1] = '\0';
	}

	strcpy(vendor, "?");
	fw_csr_string(dev->config_rom + 5, CSR_VENDOR, vendor, sizeof(vendor));
	strcpy(model, "?");
	fw_csr_string(dice->unit->directory, CSR_MODEL, model, sizeof(model));
	snprintf(card->longname, sizeof(card->longname),
		 "%s %s (serial %u) at %s, S%d",
		 vendor, model, dev->config_rom[4] & 0x3fffff,
		 dev_name(&dice->unit->device), 100 << dev->max_speed);

	strcpy(card->mixername, "DICE");
}

static void dice_free(struct snd_dice *dice)
{
	snd_dice_stream_destroy_duplex(dice);
	snd_dice_transaction_destroy(dice);
	fw_unit_put(dice->unit);

	mutex_destroy(&dice->mutex);
	kfree(dice);
}

/*
 * This module releases the FireWire unit data after all ALSA character devices
 * are released by applications. This is for releasing stream data or finishing
 * transactions safely. Thus at returning from .remove(), this module still keep
 * references for the unit.
 */
static void dice_card_free(struct snd_card *card)
{
	dice_free(card->private_data);
}

static void do_registration(struct work_struct *work)
{
	struct snd_dice *dice = container_of(work, struct snd_dice, dwork.work);
	int err;

	if (dice->registered)
		return;

	err = snd_card_new(&dice->unit->device, -1, NULL, THIS_MODULE, 0,
			   &dice->card);
	if (err < 0)
		return;

	if (force_two_pcm_support(dice->unit))
		dice->force_two_pcms = true;

	err = snd_dice_transaction_init(dice);
	if (err < 0)
		goto error;

	err = check_clock_caps(dice);
	if (err < 0)
		goto error;

	dice_card_strings(dice);

	err = snd_dice_stream_init_duplex(dice);
	if (err < 0)
		goto error;

	snd_dice_create_proc(dice);

	err = snd_dice_create_pcm(dice);
	if (err < 0)
		goto error;

	err = snd_dice_create_midi(dice);
	if (err < 0)
		goto error;

	err = snd_dice_create_hwdep(dice);
	if (err < 0)
		goto error;

	err = snd_card_register(dice->card);
	if (err < 0)
		goto error;

	/*
	 * After registered, dice instance can be released corresponding to
	 * releasing the sound card instance.
	 */
	dice->card->private_free = dice_card_free;
	dice->card->private_data = dice;
	dice->registered = true;

	return;
error:
	snd_dice_stream_destroy_duplex(dice);
	snd_dice_transaction_destroy(dice);
	snd_dice_stream_destroy_duplex(dice);
	snd_card_free(dice->card);
	dev_info(&dice->unit->device,
		 "Sound card registration failed: %d\n", err);
}

static int dice_probe(struct fw_unit *unit, const struct ieee1394_device_id *id)
{
	struct snd_dice *dice;
	int err;

	err = check_dice_category(unit);
	if (err < 0)
		return -ENODEV;

	/* Allocate this independent of sound card instance. */
	dice = kzalloc(sizeof(struct snd_dice), GFP_KERNEL);
	if (dice == NULL)
		return -ENOMEM;

	dice->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, dice);

	spin_lock_init(&dice->lock);
	mutex_init(&dice->mutex);
	init_completion(&dice->clock_accepted);
	init_waitqueue_head(&dice->hwdep_wait);

	/* Allocate and register this sound card later. */
	INIT_DEFERRABLE_WORK(&dice->dwork, do_registration);
	snd_fw_schedule_registration(unit, &dice->dwork);

	return 0;
}

static void dice_remove(struct fw_unit *unit)
{
	struct snd_dice *dice = dev_get_drvdata(&unit->device);

	/*
	 * Confirm to stop the work for registration before the sound card is
	 * going to be released. The work is not scheduled again because bus
	 * reset handler is not called anymore.
	 */
	cancel_delayed_work_sync(&dice->dwork);

	if (dice->registered) {
		/* No need to wait for releasing card object in this context. */
		snd_card_free_when_closed(dice->card);
	} else {
		/* Don't forget this case. */
		dice_free(dice);
	}
}

static void dice_bus_reset(struct fw_unit *unit)
{
	struct snd_dice *dice = dev_get_drvdata(&unit->device);

	/* Postpone a workqueue for deferred registration. */
	if (!dice->registered)
		snd_fw_schedule_registration(unit, &dice->dwork);

	/* The handler address register becomes initialized. */
	snd_dice_transaction_reinit(dice);

	/*
	 * After registration, userspace can start packet streaming, then this
	 * code block works fine.
	 */
	if (dice->registered) {
		mutex_lock(&dice->mutex);
		snd_dice_stream_update_duplex(dice);
		mutex_unlock(&dice->mutex);
	}
}

#define DICE_INTERFACE	0x000001

static const struct ieee1394_device_id dice_id_table[] = {
	{
		.match_flags = IEEE1394_MATCH_VERSION,
		.version     = DICE_INTERFACE,
	},
	/* M-Audio Profire 610/2626 has a different value in version field. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_SPECIFIER_ID,
		.vendor_id	= 0x000d6c,
		.specifier_id	= 0x000d6c,
	},
	{ }
};
MODULE_DEVICE_TABLE(ieee1394, dice_id_table);

static struct fw_driver dice_driver = {
	.driver   = {
		.owner	= THIS_MODULE,
		.name	= KBUILD_MODNAME,
		.bus	= &fw_bus_type,
	},
	.probe    = dice_probe,
	.update   = dice_bus_reset,
	.remove   = dice_remove,
	.id_table = dice_id_table,
};

static int __init alsa_dice_init(void)
{
	return driver_register(&dice_driver.driver);
}

static void __exit alsa_dice_exit(void)
{
	driver_unregister(&dice_driver.driver);
}

module_init(alsa_dice_init);
module_exit(alsa_dice_exit);

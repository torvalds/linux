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

#define DICE_CATEGORY_ID	0x04
#define WEISS_CATEGORY_ID	0x00

static int dice_interface_check(struct fw_unit *unit)
{
	static const int min_values[10] = {
		10, 0x64 / 4,
		10, 0x18 / 4,
		10, 0x18 / 4,
		0, 0,
		0, 0,
	};
	struct fw_device *device = fw_parent_device(unit);
	struct fw_csr_iterator it;
	int key, val, vendor = -1, model = -1, err;
	unsigned int category, i;
	__be32 *pointers;
	u32 value;
	__be32 version;

	pointers = kmalloc_array(ARRAY_SIZE(min_values), sizeof(__be32),
				 GFP_KERNEL);
	if (pointers == NULL)
		return -ENOMEM;

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
	if (vendor == OUI_WEISS)
		category = WEISS_CATEGORY_ID;
	else
		category = DICE_CATEGORY_ID;
	if (device->config_rom[3] != ((vendor << 8) | category) ||
	    device->config_rom[4] >> 22 != model) {
		err = -ENODEV;
		goto end;
	}

	/*
	 * Check that the sub address spaces exist and are located inside the
	 * private address space.  The minimum values are chosen so that all
	 * minimally required registers are included.
	 */
	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_PRIVATE_SPACE, pointers,
				 sizeof(__be32) * ARRAY_SIZE(min_values), 0);
	if (err < 0) {
		err = -ENODEV;
		goto end;
	}
	for (i = 0; i < ARRAY_SIZE(min_values); ++i) {
		value = be32_to_cpu(pointers[i]);
		if (value < min_values[i] || value >= 0x40000) {
			err = -ENODEV;
			goto end;
		}
	}

	/*
	 * Check that the implemented DICE driver specification major version
	 * number matches.
	 */
	err = snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
				 DICE_PRIVATE_SPACE +
				 be32_to_cpu(pointers[0]) * 4 + GLOBAL_VERSION,
				 &version, 4, 0);
	if (err < 0) {
		err = -ENODEV;
		goto end;
	}
	if ((version & cpu_to_be32(0xff000000)) != cpu_to_be32(0x01000000)) {
		dev_err(&unit->device,
			"unknown DICE version: 0x%08x\n", be32_to_cpu(version));
		err = -ENODEV;
		goto end;
	}
end:
	return err;
}

static int highest_supported_mode_rate(struct snd_dice *dice,
				       unsigned int mode, unsigned int *rate)
{
	unsigned int i, m;

	for (i = ARRAY_SIZE(snd_dice_rates); i > 0; i--) {
		*rate = snd_dice_rates[i - 1];
		if (snd_dice_stream_get_rate_mode(dice, *rate, &m) < 0)
			continue;
		if (mode == m)
			break;
	}
	if (i == 0)
		return -EINVAL;

	return 0;
}

static int dice_read_mode_params(struct snd_dice *dice, unsigned int mode)
{
	__be32 values[2];
	unsigned int rate;
	int err;

	if (highest_supported_mode_rate(dice, mode, &rate) < 0) {
		dice->tx_channels[mode] = 0;
		dice->tx_midi_ports[mode] = 0;
		dice->rx_channels[mode] = 0;
		dice->rx_midi_ports[mode] = 0;
		return 0;
	}

	err = snd_dice_transaction_set_rate(dice, rate);
	if (err < 0)
		return err;

	err = snd_dice_transaction_read_tx(dice, TX_NUMBER_AUDIO,
					   values, sizeof(values));
	if (err < 0)
		return err;

	dice->tx_channels[mode]   = be32_to_cpu(values[0]);
	dice->tx_midi_ports[mode] = be32_to_cpu(values[1]);

	err = snd_dice_transaction_read_rx(dice, RX_NUMBER_AUDIO,
					   values, sizeof(values));
	if (err < 0)
		return err;

	dice->rx_channels[mode]   = be32_to_cpu(values[0]);
	dice->rx_midi_ports[mode] = be32_to_cpu(values[1]);

	return 0;
}

static int dice_read_params(struct snd_dice *dice)
{
	__be32 value;
	int mode, err;

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

	for (mode = 2; mode >= 0; --mode) {
		err = dice_read_mode_params(dice, mode);
		if (err < 0)
			return err;
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

/*
 * This module releases the FireWire unit data after all ALSA character devices
 * are released by applications. This is for releasing stream data or finishing
 * transactions safely. Thus at returning from .remove(), this module still keep
 * references for the unit.
 */
static void dice_card_free(struct snd_card *card)
{
	struct snd_dice *dice = card->private_data;

	snd_dice_stream_destroy_duplex(dice);
	snd_dice_transaction_destroy(dice);
	fw_unit_put(dice->unit);

	mutex_destroy(&dice->mutex);
}

static int dice_probe(struct fw_unit *unit, const struct ieee1394_device_id *id)
{
	struct snd_card *card;
	struct snd_dice *dice;
	int err;

	err = dice_interface_check(unit);
	if (err < 0)
		goto end;

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(*dice), &card);
	if (err < 0)
		goto end;

	dice = card->private_data;
	dice->card = card;
	dice->unit = fw_unit_get(unit);
	card->private_free = dice_card_free;

	spin_lock_init(&dice->lock);
	mutex_init(&dice->mutex);
	init_completion(&dice->clock_accepted);
	init_waitqueue_head(&dice->hwdep_wait);

	err = snd_dice_transaction_init(dice);
	if (err < 0)
		goto error;

	err = dice_read_params(dice);
	if (err < 0)
		goto error;

	dice_card_strings(dice);

	err = snd_dice_create_pcm(dice);
	if (err < 0)
		goto error;

	err = snd_dice_create_hwdep(dice);
	if (err < 0)
		goto error;

	snd_dice_create_proc(dice);

	err = snd_dice_create_midi(dice);
	if (err < 0)
		goto error;

	err = snd_dice_stream_init_duplex(dice);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0) {
		snd_dice_stream_destroy_duplex(dice);
		goto error;
	}

	dev_set_drvdata(&unit->device, dice);
end:
	return err;
error:
	snd_card_free(card);
	return err;
}

static void dice_remove(struct fw_unit *unit)
{
	struct snd_dice *dice = dev_get_drvdata(&unit->device);

	/* No need to wait for releasing card object in this context. */
	snd_card_free_when_closed(dice->card);
}

static void dice_bus_reset(struct fw_unit *unit)
{
	struct snd_dice *dice = dev_get_drvdata(&unit->device);

	/* The handler address register becomes initialized. */
	snd_dice_transaction_reinit(dice);

	mutex_lock(&dice->mutex);
	snd_dice_stream_update_duplex(dice);
	mutex_unlock(&dice->mutex);
}

#define DICE_INTERFACE	0x000001

static const struct ieee1394_device_id dice_id_table[] = {
	{
		.match_flags = IEEE1394_MATCH_VERSION,
		.version     = DICE_INTERFACE,
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

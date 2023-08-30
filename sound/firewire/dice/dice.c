// SPDX-License-Identifier: GPL-2.0-only
/*
 * TC Applied Technologies Digital Interface Communications Engine driver
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

#include "dice.h"

MODULE_DESCRIPTION("DICE driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL");

#define OUI_WEISS		0x001c6a
#define OUI_LOUD		0x000ff2
#define OUI_FOCUSRITE		0x00130e
#define OUI_TCELECTRONIC	0x000166
#define OUI_ALESIS		0x000595
#define OUI_MAUDIO		0x000d6c
#define OUI_MYTEK		0x001ee8
#define OUI_SSL			0x0050c2	// Actually ID reserved by IEEE.
#define OUI_PRESONUS		0x000a92
#define OUI_HARMAN		0x000fd7
#define OUI_AVID		0x00a07e

#define DICE_CATEGORY_ID	0x04
#define WEISS_CATEGORY_ID	0x00
#define LOUD_CATEGORY_ID	0x10
#define HARMAN_CATEGORY_ID	0x20

#define MODEL_ALESIS_IO_BOTH	0x000001

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

	if (vendor == OUI_WEISS)
		category = WEISS_CATEGORY_ID;
	else if (vendor == OUI_LOUD)
		category = LOUD_CATEGORY_ID;
	else if (vendor == OUI_HARMAN)
		category = HARMAN_CATEGORY_ID;
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

static void dice_card_free(struct snd_card *card)
{
	struct snd_dice *dice = card->private_data;

	snd_dice_stream_destroy_duplex(dice);
	snd_dice_transaction_destroy(dice);

	mutex_destroy(&dice->mutex);
	fw_unit_put(dice->unit);
}

static int dice_probe(struct fw_unit *unit, const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_dice *dice;
	snd_dice_detect_formats_t detect_formats;
	int err;

	if (!entry->driver_data && entry->vendor_id != OUI_SSL) {
		err = check_dice_category(unit);
		if (err < 0)
			return -ENODEV;
	}

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE, sizeof(*dice), &card);
	if (err < 0)
		return err;
	card->private_free = dice_card_free;

	dice = card->private_data;
	dice->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, dice);
	dice->card = card;

	if (!entry->driver_data)
		detect_formats = snd_dice_stream_detect_current_formats;
	else
		detect_formats = (snd_dice_detect_formats_t)entry->driver_data;

	// Below models are compliant to IEC 61883-1/6 and have no quirk at high sampling transfer
	// frequency.
	// * Avid M-Box 3 Pro
	// * M-Audio Profire 610
	// * M-Audio Profire 2626
	if (entry->vendor_id == OUI_MAUDIO || entry->vendor_id == OUI_AVID)
		dice->disable_double_pcm_frames = true;

	spin_lock_init(&dice->lock);
	mutex_init(&dice->mutex);
	init_completion(&dice->clock_accepted);
	init_waitqueue_head(&dice->hwdep_wait);

	err = snd_dice_transaction_init(dice);
	if (err < 0)
		goto error;

	err = check_clock_caps(dice);
	if (err < 0)
		goto error;

	dice_card_strings(dice);

	err = detect_formats(dice);
	if (err < 0)
		goto error;

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

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	return 0;
error:
	snd_card_free(card);
	return err;
}

static void dice_remove(struct fw_unit *unit)
{
	struct snd_dice *dice = dev_get_drvdata(&unit->device);

	// Block till all of ALSA character devices are released.
	snd_card_free(dice->card);
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

#define DICE_DEV_ENTRY_TYPICAL(vendor, model, data) \
	{ \
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | \
				  IEEE1394_MATCH_MODEL_ID | \
				  IEEE1394_MATCH_SPECIFIER_ID | \
				  IEEE1394_MATCH_VERSION, \
		.vendor_id	= (vendor), \
		.model_id	= (model), \
		.specifier_id	= (vendor), \
		.version	= DICE_INTERFACE, \
		.driver_data = (kernel_ulong_t)(data), \
	}

static const struct ieee1394_device_id dice_id_table[] = {
	// Avid M-Box 3 Pro. To match in probe function.
	DICE_DEV_ENTRY_TYPICAL(OUI_AVID, 0x000004, snd_dice_detect_extension_formats),
	/* M-Audio Profire 2626 has a different value in version field. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_MAUDIO,
		.model_id	= 0x000010,
		.driver_data = (kernel_ulong_t)snd_dice_detect_extension_formats,
	},
	/* M-Audio Profire 610 has a different value in version field. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_MAUDIO,
		.model_id	= 0x000011,
		.driver_data = (kernel_ulong_t)snd_dice_detect_extension_formats,
	},
	/* TC Electronic Konnekt 24D. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_TCELECTRONIC,
		.model_id	= 0x000020,
		.driver_data = (kernel_ulong_t)snd_dice_detect_tcelectronic_formats,
	},
	/* TC Electronic Konnekt 8. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_TCELECTRONIC,
		.model_id	= 0x000021,
		.driver_data = (kernel_ulong_t)snd_dice_detect_tcelectronic_formats,
	},
	/* TC Electronic Studio Konnekt 48. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_TCELECTRONIC,
		.model_id	= 0x000022,
		.driver_data = (kernel_ulong_t)snd_dice_detect_tcelectronic_formats,
	},
	/* TC Electronic Konnekt Live. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_TCELECTRONIC,
		.model_id	= 0x000023,
		.driver_data = (kernel_ulong_t)snd_dice_detect_tcelectronic_formats,
	},
	/* TC Electronic Desktop Konnekt 6. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_TCELECTRONIC,
		.model_id	= 0x000024,
		.driver_data = (kernel_ulong_t)snd_dice_detect_tcelectronic_formats,
	},
	/* TC Electronic Impact Twin. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_TCELECTRONIC,
		.model_id	= 0x000027,
		.driver_data = (kernel_ulong_t)snd_dice_detect_tcelectronic_formats,
	},
	/* TC Electronic Digital Konnekt x32. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_TCELECTRONIC,
		.model_id	= 0x000030,
		.driver_data = (kernel_ulong_t)snd_dice_detect_tcelectronic_formats,
	},
	/* Alesis iO14/iO26. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_ALESIS,
		.model_id	= MODEL_ALESIS_IO_BOTH,
		.driver_data = (kernel_ulong_t)snd_dice_detect_alesis_formats,
	},
	// Alesis MasterControl.
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_ALESIS,
		.model_id	= 0x000002,
		.driver_data = (kernel_ulong_t)snd_dice_detect_alesis_mastercontrol_formats,
	},
	/* Mytek Stereo 192 DSD-DAC. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_MYTEK,
		.model_id	= 0x000002,
		.driver_data = (kernel_ulong_t)snd_dice_detect_mytek_formats,
	},
	// Solid State Logic, Duende Classic and Mini.
	// NOTE: each field of GUID in config ROM is not compliant to standard
	// DICE scheme.
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_SSL,
		.model_id	= 0x000070,
	},
	// Presonus FireStudio.
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_PRESONUS,
		.model_id	= 0x000008,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_presonus_formats,
	},
	// Lexicon I-ONYX FW810S.
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_HARMAN,
		.model_id	= 0x000001,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_harman_formats,
	},
	// Focusrite Saffire Pro 40 with TCD3070-CH.
	// The model has quirk in its GUID, in which model field is 0x000013 and different from
	// model ID (0x0000de) in its root/unit directory.
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_FOCUSRITE,
		.model_id	= 0x0000de,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_focusrite_pro40_tcd3070_formats,
	},
	// Weiss DAC202: 192kHz 2-channel DAC
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_WEISS,
		.model_id	= 0x000007,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_weiss_formats,
	},
	// Weiss DAC202: 192kHz 2-channel DAC (Maya edition)
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_WEISS,
		.model_id	= 0x000008,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_weiss_formats,
	},
	// Weiss MAN301: 192kHz 2-channel music archive network player
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_WEISS,
		.model_id	= 0x00000b,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_weiss_formats,
	},
	// Weiss INT202: 192kHz unidirectional 2-channel digital Firewire face
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_WEISS,
		.model_id	= 0x000006,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_weiss_formats,
	},
	// Weiss INT203: 192kHz bidirectional 2-channel digital Firewire face
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_WEISS,
		.model_id	= 0x00000a,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_weiss_formats,
	},
	// Weiss ADC2: 192kHz A/D converter with microphone preamps and inputs
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_WEISS,
		.model_id	= 0x000001,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_weiss_formats,
	},
	// Weiss DAC2/Minerva: 192kHz 2-channel DAC
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_WEISS,
		.model_id	= 0x000003,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_weiss_formats,
	},
	// Weiss Vesta: 192kHz 2-channel Firewire to AES/EBU interface
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_WEISS,
		.model_id	= 0x000002,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_weiss_formats,
	},
	// Weiss AFI1: 192kHz 24-channel Firewire to ADAT or AES/EBU face
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_WEISS,
		.model_id	= 0x000004,
		.driver_data	= (kernel_ulong_t)snd_dice_detect_weiss_formats,
	},
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

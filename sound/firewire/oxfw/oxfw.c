// SPDX-License-Identifier: GPL-2.0-only
/*
 * oxfw.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

#include "oxfw.h"

#define OXFORD_FIRMWARE_ID_ADDRESS	(CSR_REGISTER_BASE + 0x50000)
/* 0x970?vvvv or 0x971?vvvv, where vvvv = firmware version */

#define OXFORD_HARDWARE_ID_ADDRESS	(CSR_REGISTER_BASE + 0x90020)
#define OXFORD_HARDWARE_ID_OXFW970	0x39443841
#define OXFORD_HARDWARE_ID_OXFW971	0x39373100

#define VENDOR_LOUD		0x000ff2
#define VENDOR_GRIFFIN		0x001292
#define VENDOR_BEHRINGER	0x001564
#define VENDOR_LACIE		0x00d04b
#define VENDOR_TASCAM		0x00022e
#define OUI_STANTON		0x001260
#define OUI_APOGEE		0x0003db

#define MODEL_SATELLITE		0x00200f
#define MODEL_SCS1M		0x001000
#define MODEL_DUET_FW		0x01dddd
#define MODEL_ONYX_1640I	0x001640

#define SPECIFIER_1394TA	0x00a02d
#define VERSION_AVC		0x010001

MODULE_DESCRIPTION("Oxford Semiconductor FW970/971 driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("snd-firewire-speakers");
MODULE_ALIAS("snd-scs1x");

struct compat_info {
	const char *driver_name;
	const char *vendor_name;
	const char *model_name;
};

static bool detect_loud_models(struct fw_unit *unit)
{
	const char *const models[] = {
		"Onyxi",
		"Onyx-i",
		"Onyx 1640i",
		"d.Pro",
		"U.420"};
	char model[32];
	int err;

	err = fw_csr_string(unit->directory, CSR_MODEL,
			    model, sizeof(model));
	if (err < 0)
		return false;

	return match_string(models, ARRAY_SIZE(models), model) >= 0;
}

static int name_card(struct snd_oxfw *oxfw, const struct ieee1394_device_id *entry)
{
	struct fw_device *fw_dev = fw_parent_device(oxfw->unit);
	const struct compat_info *info;
	char vendor[24];
	char model[32];
	const char *d, *v, *m;
	u32 firmware;
	int err;

	/* get vendor name from root directory */
	err = fw_csr_string(fw_dev->config_rom + 5, CSR_VENDOR,
			    vendor, sizeof(vendor));
	if (err < 0)
		goto end;

	/* get model name from unit directory */
	err = fw_csr_string(oxfw->unit->directory, CSR_MODEL,
			    model, sizeof(model));
	if (err < 0)
		goto end;

	err = snd_fw_transaction(oxfw->unit, TCODE_READ_QUADLET_REQUEST,
				 OXFORD_FIRMWARE_ID_ADDRESS, &firmware, 4, 0);
	if (err < 0)
		goto end;
	be32_to_cpus(&firmware);

	if (firmware >> 20 == 0x970)
		oxfw->quirks |= SND_OXFW_QUIRK_JUMBO_PAYLOAD;

	/* to apply card definitions */
	if (entry->vendor_id == VENDOR_GRIFFIN || entry->vendor_id == VENDOR_LACIE) {
		info = (const struct compat_info *)entry->driver_data;
		d = info->driver_name;
		v = info->vendor_name;
		m = info->model_name;
	} else {
		d = "OXFW";
		v = vendor;
		m = model;
	}

	strcpy(oxfw->card->driver, d);
	strcpy(oxfw->card->mixername, m);
	strcpy(oxfw->card->shortname, m);

	snprintf(oxfw->card->longname, sizeof(oxfw->card->longname),
		 "%s %s (OXFW%x %04x), GUID %08x%08x at %s, S%d",
		 v, m, firmware >> 20, firmware & 0xffff,
		 fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&oxfw->unit->device), 100 << fw_dev->max_speed);
end:
	return err;
}

static void oxfw_card_free(struct snd_card *card)
{
	struct snd_oxfw *oxfw = card->private_data;

	if (oxfw->has_output || oxfw->has_input)
		snd_oxfw_stream_destroy_duplex(oxfw);

	mutex_destroy(&oxfw->mutex);
	fw_unit_put(oxfw->unit);
}

static int detect_quirks(struct snd_oxfw *oxfw, const struct ieee1394_device_id *entry)
{
	struct fw_device *fw_dev = fw_parent_device(oxfw->unit);
	struct fw_csr_iterator it;
	int key, val;
	int vendor, model;

	/*
	 * Add ALSA control elements for two models to keep compatibility to
	 * old firewire-speaker module.
	 */
	if (entry->vendor_id == VENDOR_GRIFFIN)
		return snd_oxfw_add_spkr(oxfw, false);
	if (entry->vendor_id == VENDOR_LACIE)
		return snd_oxfw_add_spkr(oxfw, true);

	/*
	 * Stanton models supports asynchronous transactions for unique MIDI
	 * messages.
	 */
	if (entry->vendor_id == OUI_STANTON) {
		oxfw->quirks |= SND_OXFW_QUIRK_SCS_TRANSACTION;
		if (entry->model_id == MODEL_SCS1M)
			oxfw->quirks |= SND_OXFW_QUIRK_BLOCKING_TRANSMISSION;

		// No physical MIDI ports.
		oxfw->midi_input_ports = 0;
		oxfw->midi_output_ports = 0;

		return snd_oxfw_scs1x_add(oxfw);
	}

	if (entry->vendor_id == OUI_APOGEE && entry->model_id == MODEL_DUET_FW) {
		oxfw->quirks |= SND_OXFW_QUIRK_BLOCKING_TRANSMISSION |
				SND_OXFW_QUIRK_IGNORE_NO_INFO_PACKET;
	}

	/*
	 * TASCAM FireOne has physical control and requires a pair of additional
	 * MIDI ports.
	 */
	if (entry->vendor_id == VENDOR_TASCAM) {
		oxfw->midi_input_ports++;
		oxfw->midi_output_ports++;
		return 0;
	}

	/* Seek from Root Directory of Config ROM. */
	vendor = model = 0;
	fw_csr_iterator_init(&it, fw_dev->config_rom + 5);
	while (fw_csr_iterator_next(&it, &key, &val)) {
		if (key == CSR_VENDOR)
			vendor = val;
		else if (key == CSR_MODEL)
			model = val;
	}

	if (vendor == VENDOR_LOUD) {
		// Mackie Onyx Satellite with base station has a quirk to report a wrong
		// value in 'dbs' field of CIP header against its format information.
		oxfw->quirks |= SND_OXFW_QUIRK_WRONG_DBS;

		// OXFW971-based models may transfer events by blocking method.
		if (!(oxfw->quirks & SND_OXFW_QUIRK_JUMBO_PAYLOAD))
			oxfw->quirks |= SND_OXFW_QUIRK_BLOCKING_TRANSMISSION;

		if (model == MODEL_ONYX_1640I) {
			//Unless receiving packets without NOINFO packet, the device transfers
			//mostly half of events in packets than expected.
			oxfw->quirks |= SND_OXFW_QUIRK_IGNORE_NO_INFO_PACKET |
					SND_OXFW_QUIRK_VOLUNTARY_RECOVERY;
		}
	}

	return 0;
}

static int oxfw_probe(struct fw_unit *unit, const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_oxfw *oxfw;
	int err;

	if (entry->vendor_id == VENDOR_LOUD && entry->model_id == 0 && !detect_loud_models(unit))
		return -ENODEV;

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE, sizeof(*oxfw), &card);
	if (err < 0)
		return err;
	card->private_free = oxfw_card_free;

	oxfw = card->private_data;
	oxfw->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, oxfw);
	oxfw->card = card;

	mutex_init(&oxfw->mutex);
	spin_lock_init(&oxfw->lock);
	init_waitqueue_head(&oxfw->hwdep_wait);

	err = name_card(oxfw, entry);
	if (err < 0)
		goto error;

	err = snd_oxfw_stream_discover(oxfw);
	if (err < 0)
		goto error;

	err = detect_quirks(oxfw, entry);
	if (err < 0)
		goto error;

	if (oxfw->has_output || oxfw->has_input) {
		err = snd_oxfw_stream_init_duplex(oxfw);
		if (err < 0)
			goto error;

		err = snd_oxfw_create_pcm(oxfw);
		if (err < 0)
			goto error;

		snd_oxfw_proc_init(oxfw);

		err = snd_oxfw_create_midi(oxfw);
		if (err < 0)
			goto error;

		err = snd_oxfw_create_hwdep(oxfw);
		if (err < 0)
			goto error;
	}

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	return 0;
error:
	snd_card_free(card);
	return err;
}

static void oxfw_bus_reset(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	fcp_bus_reset(oxfw->unit);

	if (oxfw->has_output || oxfw->has_input) {
		mutex_lock(&oxfw->mutex);
		snd_oxfw_stream_update_duplex(oxfw);
		mutex_unlock(&oxfw->mutex);
	}

	if (oxfw->quirks & SND_OXFW_QUIRK_SCS_TRANSACTION)
		snd_oxfw_scs1x_update(oxfw);
}

static void oxfw_remove(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	// Block till all of ALSA character devices are released.
	snd_card_free(oxfw->card);
}

static const struct compat_info griffin_firewave = {
	.driver_name = "FireWave",
	.vendor_name = "Griffin",
	.model_name = "FireWave",
};

static const struct compat_info lacie_speakers = {
	.driver_name = "FWSpeakers",
	.vendor_name = "LaCie",
	.model_name = "FireWire Speakers",
};

#define OXFW_DEV_ENTRY(vendor, model, data) \
{ \
	.match_flags  = IEEE1394_MATCH_VENDOR_ID | \
			IEEE1394_MATCH_MODEL_ID | \
			IEEE1394_MATCH_SPECIFIER_ID | \
			IEEE1394_MATCH_VERSION, \
	.vendor_id    = vendor, \
	.model_id     = model, \
	.specifier_id = SPECIFIER_1394TA, \
	.version      = VERSION_AVC, \
	.driver_data  = (kernel_ulong_t)data, \
}

static const struct ieee1394_device_id oxfw_id_table[] = {
	//
	// OXFW970 devices:
	// Initial firmware has a quirk to postpone isoc packet transmission during finishing async
	// transaction. As a result, several isochronous cycles are skipped to transfer the packets
	// and the audio data frames which should have been transferred during the cycles are put
	// into packet at the first isoc cycle after the postpone. Furthermore, the value of SYT
	// field in CIP header is not reliable as synchronization timing,
	//
	OXFW_DEV_ENTRY(VENDOR_GRIFFIN, 0x00f970, &griffin_firewave),
	OXFW_DEV_ENTRY(VENDOR_LACIE, 0x00f970, &lacie_speakers),
	// Behringer,F-Control Audio 202. The value of SYT field is not reliable at all.
	OXFW_DEV_ENTRY(VENDOR_BEHRINGER, 0x00fc22, NULL),
	// Loud Technologies, Tapco Link.FireWire 4x6. The value of SYT field is always 0xffff.
	OXFW_DEV_ENTRY(VENDOR_LOUD, 0x000460, NULL),
	// Loud Technologies, Mackie Onyx Satellite. Although revised version of firmware is
	// installed to avoid the postpone, the value of SYT field is always 0xffff.
	OXFW_DEV_ENTRY(VENDOR_LOUD, MODEL_SATELLITE, NULL),
	// Miglia HarmonyAudio. Not yet identified.

	//
	// OXFW971 devices:
	// The value of SYT field in CIP header is enough reliable. Both of blocking and non-blocking
	// transmission methods are available.
	//
	// Any Mackie(Loud) models (name string/model id):
	//  Onyx-i series (former models):	0x081216
	//  Onyx 1640i:				0x001640
	//  d.2 pro/d.4 pro (built-in card):	Unknown
	//  U.420:				Unknown
	//  U.420d:				Unknown
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_SPECIFIER_ID |
				  IEEE1394_MATCH_VERSION,
		.vendor_id	= VENDOR_LOUD,
		.model_id	= 0,
		.specifier_id	= SPECIFIER_1394TA,
		.version	= VERSION_AVC,
	},
	// TASCAM, FireOne.
	OXFW_DEV_ENTRY(VENDOR_TASCAM, 0x800007, NULL),
	// Stanton, Stanton Controllers & Systems 1 Mixer (SCS.1m).
	OXFW_DEV_ENTRY(OUI_STANTON, MODEL_SCS1M, NULL),
	// Stanton, Stanton Controllers & Systems 1 Deck (SCS.1d).
	OXFW_DEV_ENTRY(OUI_STANTON, 0x002000, NULL),
	// APOGEE, duet FireWire.
	OXFW_DEV_ENTRY(OUI_APOGEE, MODEL_DUET_FW, NULL),
	{ }
};
MODULE_DEVICE_TABLE(ieee1394, oxfw_id_table);

static struct fw_driver oxfw_driver = {
	.driver   = {
		.owner	= THIS_MODULE,
		.name	= KBUILD_MODNAME,
		.bus	= &fw_bus_type,
	},
	.probe    = oxfw_probe,
	.update   = oxfw_bus_reset,
	.remove   = oxfw_remove,
	.id_table = oxfw_id_table,
};

static int __init snd_oxfw_init(void)
{
	return driver_register(&oxfw_driver.driver);
}

static void __exit snd_oxfw_exit(void)
{
	driver_unregister(&oxfw_driver.driver);
}

module_init(snd_oxfw_init);
module_exit(snd_oxfw_exit);

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

#define SPECIFIER_1394TA	0x00a02d
#define VERSION_AVC		0x010001

MODULE_DESCRIPTION("Oxford Semiconductor FW970/971 driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");
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
		"Mackie Onyx Satellite",
		"Tapco LINK.firewire 4x6",
		"U.420"};
	char model[32];
	int err;

	err = fw_csr_string(unit->directory, CSR_MODEL,
			    model, sizeof(model));
	if (err < 0)
		return false;

	return match_string(models, ARRAY_SIZE(models), model) >= 0;
}

static int name_card(struct snd_oxfw *oxfw)
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

	/* to apply card definitions */
	if (oxfw->entry->vendor_id == VENDOR_GRIFFIN ||
	    oxfw->entry->vendor_id == VENDOR_LACIE) {
		info = (const struct compat_info *)oxfw->entry->driver_data;
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

	snd_oxfw_stream_destroy_duplex(oxfw);
}

static int detect_quirks(struct snd_oxfw *oxfw)
{
	struct fw_device *fw_dev = fw_parent_device(oxfw->unit);
	struct fw_csr_iterator it;
	int key, val;
	int vendor, model;

	/*
	 * Add ALSA control elements for two models to keep compatibility to
	 * old firewire-speaker module.
	 */
	if (oxfw->entry->vendor_id == VENDOR_GRIFFIN)
		return snd_oxfw_add_spkr(oxfw, false);
	if (oxfw->entry->vendor_id == VENDOR_LACIE)
		return snd_oxfw_add_spkr(oxfw, true);

	/*
	 * Stanton models supports asynchronous transactions for unique MIDI
	 * messages.
	 */
	if (oxfw->entry->vendor_id == OUI_STANTON) {
		/* No physical MIDI ports. */
		oxfw->midi_input_ports = 0;
		oxfw->midi_output_ports = 0;

		return snd_oxfw_scs1x_add(oxfw);
	}

	/*
	 * TASCAM FireOne has physical control and requires a pair of additional
	 * MIDI ports.
	 */
	if (oxfw->entry->vendor_id == VENDOR_TASCAM) {
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

	/*
	 * Mackie Onyx Satellite with base station has a quirk to report a wrong
	 * value in 'dbs' field of CIP header against its format information.
	 */
	if (vendor == VENDOR_LOUD && model == MODEL_SATELLITE)
		oxfw->wrong_dbs = true;

	return 0;
}

static void do_registration(struct work_struct *work)
{
	struct snd_oxfw *oxfw = container_of(work, struct snd_oxfw, dwork.work);
	int err;

	if (oxfw->registered)
		return;

	err = snd_card_new(&oxfw->unit->device, -1, NULL, THIS_MODULE, 0,
			   &oxfw->card);
	if (err < 0)
		return;
	oxfw->card->private_free = oxfw_card_free;
	oxfw->card->private_data = oxfw;

	err = name_card(oxfw);
	if (err < 0)
		goto error;

	err = snd_oxfw_stream_discover(oxfw);
	if (err < 0)
		goto error;

	err = detect_quirks(oxfw);
	if (err < 0)
		goto error;

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

	err = snd_card_register(oxfw->card);
	if (err < 0)
		goto error;

	oxfw->registered = true;

	return;
error:
	snd_card_free(oxfw->card);
	dev_info(&oxfw->unit->device,
		 "Sound card registration failed: %d\n", err);
}

static int oxfw_probe(struct fw_unit *unit,
		      const struct ieee1394_device_id *entry)
{
	struct snd_oxfw *oxfw;

	if (entry->vendor_id == VENDOR_LOUD && !detect_loud_models(unit))
		return -ENODEV;

	/* Allocate this independent of sound card instance. */
	oxfw = devm_kzalloc(&unit->device, sizeof(struct snd_oxfw), GFP_KERNEL);
	if (!oxfw)
		return -ENOMEM;
	oxfw->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, oxfw);

	oxfw->entry = entry;
	mutex_init(&oxfw->mutex);
	spin_lock_init(&oxfw->lock);
	init_waitqueue_head(&oxfw->hwdep_wait);

	/* Allocate and register this sound card later. */
	INIT_DEFERRABLE_WORK(&oxfw->dwork, do_registration);
	snd_fw_schedule_registration(unit, &oxfw->dwork);

	return 0;
}

static void oxfw_bus_reset(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	if (!oxfw->registered)
		snd_fw_schedule_registration(unit, &oxfw->dwork);

	fcp_bus_reset(oxfw->unit);

	if (oxfw->registered) {
		mutex_lock(&oxfw->mutex);
		snd_oxfw_stream_update_duplex(oxfw);
		mutex_unlock(&oxfw->mutex);

		if (oxfw->entry->vendor_id == OUI_STANTON)
			snd_oxfw_scs1x_update(oxfw);
	}
}

static void oxfw_remove(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	/*
	 * Confirm to stop the work for registration before the sound card is
	 * going to be released. The work is not scheduled again because bus
	 * reset handler is not called anymore.
	 */
	cancel_delayed_work_sync(&oxfw->dwork);

	if (oxfw->registered) {
		// Block till all of ALSA character devices are released.
		snd_card_free(oxfw->card);
	}

	mutex_destroy(&oxfw->mutex);
	fw_unit_put(oxfw->unit);
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

static const struct ieee1394_device_id oxfw_id_table[] = {
	{
		.match_flags  = IEEE1394_MATCH_VENDOR_ID |
				IEEE1394_MATCH_MODEL_ID |
				IEEE1394_MATCH_SPECIFIER_ID |
				IEEE1394_MATCH_VERSION,
		.vendor_id    = VENDOR_GRIFFIN,
		.model_id     = 0x00f970,
		.specifier_id = SPECIFIER_1394TA,
		.version      = VERSION_AVC,
		.driver_data  = (kernel_ulong_t)&griffin_firewave,
	},
	{
		.match_flags  = IEEE1394_MATCH_VENDOR_ID |
				IEEE1394_MATCH_MODEL_ID |
				IEEE1394_MATCH_SPECIFIER_ID |
				IEEE1394_MATCH_VERSION,
		.vendor_id    = VENDOR_LACIE,
		.model_id     = 0x00f970,
		.specifier_id = SPECIFIER_1394TA,
		.version      = VERSION_AVC,
		.driver_data  = (kernel_ulong_t)&lacie_speakers,
	},
	/* Behringer,F-Control Audio 202 */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_BEHRINGER,
		.model_id	= 0x00fc22,
	},
	/*
	 * Any Mackie(Loud) models (name string/model id):
	 *  Onyx-i series (former models):	0x081216
	 *  Mackie Onyx Satellite:		0x00200f
	 *  Tapco LINK.firewire 4x6:		0x000460
	 *  d.2 pro:				Unknown
	 *  d.4 pro:				Unknown
	 *  U.420:				Unknown
	 *  U.420d:				Unknown
	 */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_SPECIFIER_ID |
				  IEEE1394_MATCH_VERSION,
		.vendor_id	= VENDOR_LOUD,
		.specifier_id	= SPECIFIER_1394TA,
		.version	= VERSION_AVC,
	},
	/* TASCAM, FireOne */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_TASCAM,
		.model_id	= 0x800007,
	},
	/* Stanton, Stanton Controllers & Systems 1 Mixer (SCS.1m) */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_STANTON,
		.model_id	= 0x001000,
	},
	/* Stanton, Stanton Controllers & Systems 1 Deck (SCS.1d) */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_STANTON,
		.model_id	= 0x002000,
	},
	// APOGEE, duet FireWire
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_APOGEE,
		.model_id	= 0x01dddd,
	},
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

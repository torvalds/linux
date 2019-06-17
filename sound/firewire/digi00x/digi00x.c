// SPDX-License-Identifier: GPL-2.0-only
/*
 * digi00x.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 */

#include "digi00x.h"

MODULE_DESCRIPTION("Digidesign Digi 002/003 family Driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

#define VENDOR_DIGIDESIGN	0x00a07e
#define MODEL_CONSOLE		0x000001
#define MODEL_RACK		0x000002

static int name_card(struct snd_dg00x *dg00x)
{
	struct fw_device *fw_dev = fw_parent_device(dg00x->unit);
	char name[32] = {0};
	char *model;
	int err;

	err = fw_csr_string(dg00x->unit->directory, CSR_MODEL, name,
			    sizeof(name));
	if (err < 0)
		return err;

	model = skip_spaces(name);

	strcpy(dg00x->card->driver, "Digi00x");
	strcpy(dg00x->card->shortname, model);
	strcpy(dg00x->card->mixername, model);
	snprintf(dg00x->card->longname, sizeof(dg00x->card->longname),
		 "Digidesign %s, GUID %08x%08x at %s, S%d", model,
		 fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&dg00x->unit->device), 100 << fw_dev->max_speed);

	return 0;
}

static void dg00x_card_free(struct snd_card *card)
{
	struct snd_dg00x *dg00x = card->private_data;

	snd_dg00x_stream_destroy_duplex(dg00x);
	snd_dg00x_transaction_unregister(dg00x);
}

static void do_registration(struct work_struct *work)
{
	struct snd_dg00x *dg00x =
			container_of(work, struct snd_dg00x, dwork.work);
	int err;

	if (dg00x->registered)
		return;

	err = snd_card_new(&dg00x->unit->device, -1, NULL, THIS_MODULE, 0,
			   &dg00x->card);
	if (err < 0)
		return;
	dg00x->card->private_free = dg00x_card_free;
	dg00x->card->private_data = dg00x;

	err = name_card(dg00x);
	if (err < 0)
		goto error;

	err = snd_dg00x_stream_init_duplex(dg00x);
	if (err < 0)
		goto error;

	snd_dg00x_proc_init(dg00x);

	err = snd_dg00x_create_pcm_devices(dg00x);
	if (err < 0)
		goto error;

	err = snd_dg00x_create_midi_devices(dg00x);
	if (err < 0)
		goto error;

	err = snd_dg00x_create_hwdep_device(dg00x);
	if (err < 0)
		goto error;

	err = snd_dg00x_transaction_register(dg00x);
	if (err < 0)
		goto error;

	err = snd_card_register(dg00x->card);
	if (err < 0)
		goto error;

	dg00x->registered = true;

	return;
error:
	snd_card_free(dg00x->card);
	dev_info(&dg00x->unit->device,
		 "Sound card registration failed: %d\n", err);
}

static int snd_dg00x_probe(struct fw_unit *unit,
			   const struct ieee1394_device_id *entry)
{
	struct snd_dg00x *dg00x;

	/* Allocate this independent of sound card instance. */
	dg00x = devm_kzalloc(&unit->device, sizeof(struct snd_dg00x),
			     GFP_KERNEL);
	if (!dg00x)
		return -ENOMEM;

	dg00x->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, dg00x);

	mutex_init(&dg00x->mutex);
	spin_lock_init(&dg00x->lock);
	init_waitqueue_head(&dg00x->hwdep_wait);

	dg00x->is_console = entry->model_id == MODEL_CONSOLE;

	/* Allocate and register this sound card later. */
	INIT_DEFERRABLE_WORK(&dg00x->dwork, do_registration);
	snd_fw_schedule_registration(unit, &dg00x->dwork);

	return 0;
}

static void snd_dg00x_update(struct fw_unit *unit)
{
	struct snd_dg00x *dg00x = dev_get_drvdata(&unit->device);

	/* Postpone a workqueue for deferred registration. */
	if (!dg00x->registered)
		snd_fw_schedule_registration(unit, &dg00x->dwork);

	snd_dg00x_transaction_reregister(dg00x);

	/*
	 * After registration, userspace can start packet streaming, then this
	 * code block works fine.
	 */
	if (dg00x->registered) {
		mutex_lock(&dg00x->mutex);
		snd_dg00x_stream_update_duplex(dg00x);
		mutex_unlock(&dg00x->mutex);
	}
}

static void snd_dg00x_remove(struct fw_unit *unit)
{
	struct snd_dg00x *dg00x = dev_get_drvdata(&unit->device);

	/*
	 * Confirm to stop the work for registration before the sound card is
	 * going to be released. The work is not scheduled again because bus
	 * reset handler is not called anymore.
	 */
	cancel_delayed_work_sync(&dg00x->dwork);

	if (dg00x->registered) {
		// Block till all of ALSA character devices are released.
		snd_card_free(dg00x->card);
	}

	mutex_destroy(&dg00x->mutex);
	fw_unit_put(dg00x->unit);
}

static const struct ieee1394_device_id snd_dg00x_id_table[] = {
	/* Both of 002/003 use the same ID. */
	{
		.match_flags = IEEE1394_MATCH_VENDOR_ID |
			       IEEE1394_MATCH_MODEL_ID,
		.vendor_id = VENDOR_DIGIDESIGN,
		.model_id = MODEL_CONSOLE,
	},
	{
		.match_flags = IEEE1394_MATCH_VENDOR_ID |
			       IEEE1394_MATCH_MODEL_ID,
		.vendor_id = VENDOR_DIGIDESIGN,
		.model_id = MODEL_RACK,
	},
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_dg00x_id_table);

static struct fw_driver dg00x_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "snd-firewire-digi00x",
		.bus = &fw_bus_type,
	},
	.probe    = snd_dg00x_probe,
	.update   = snd_dg00x_update,
	.remove   = snd_dg00x_remove,
	.id_table = snd_dg00x_id_table,
};

static int __init snd_dg00x_init(void)
{
	return driver_register(&dg00x_driver.driver);
}

static void __exit snd_dg00x_exit(void)
{
	driver_unregister(&dg00x_driver.driver);
}

module_init(snd_dg00x_init);
module_exit(snd_dg00x_exit);

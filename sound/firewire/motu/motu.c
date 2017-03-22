/*
 * motu.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "motu.h"

#define OUI_MOTU	0x0001f2

MODULE_DESCRIPTION("MOTU FireWire driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

static void name_card(struct snd_motu *motu)
{
	struct fw_device *fw_dev = fw_parent_device(motu->unit);
	struct fw_csr_iterator it;
	int key, val;
	u32 version = 0;

	fw_csr_iterator_init(&it, motu->unit->directory);
	while (fw_csr_iterator_next(&it, &key, &val)) {
		switch (key) {
		case CSR_VERSION:
			version = val;
			break;
		}
	}

	strcpy(motu->card->driver, "FW-MOTU");
	strcpy(motu->card->shortname, motu->spec->name);
	strcpy(motu->card->mixername, motu->spec->name);
	snprintf(motu->card->longname, sizeof(motu->card->longname),
		 "MOTU %s (version:%d), GUID %08x%08x at %s, S%d",
		 motu->spec->name, version,
		 fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&motu->unit->device), 100 << fw_dev->max_speed);
}

static void motu_free(struct snd_motu *motu)
{
	fw_unit_put(motu->unit);

	mutex_destroy(&motu->mutex);
	kfree(motu);
}

/*
 * This module releases the FireWire unit data after all ALSA character devices
 * are released by applications. This is for releasing stream data or finishing
 * transactions safely. Thus at returning from .remove(), this module still keep
 * references for the unit.
 */
static void motu_card_free(struct snd_card *card)
{
	motu_free(card->private_data);
}

static void do_registration(struct work_struct *work)
{
	struct snd_motu *motu = container_of(work, struct snd_motu, dwork.work);
	int err;

	if (motu->registered)
		return;

	err = snd_card_new(&motu->unit->device, -1, NULL, THIS_MODULE, 0,
			   &motu->card);
	if (err < 0)
		return;

	name_card(motu);

	err = snd_card_register(motu->card);
	if (err < 0)
		goto error;

	/*
	 * After registered, motu instance can be released corresponding to
	 * releasing the sound card instance.
	 */
	motu->card->private_free = motu_card_free;
	motu->card->private_data = motu;
	motu->registered = true;

	return;
error:
	snd_card_free(motu->card);
	dev_info(&motu->unit->device,
		 "Sound card registration failed: %d\n", err);
}

static int motu_probe(struct fw_unit *unit,
		      const struct ieee1394_device_id *entry)
{
	struct snd_motu *motu;

	/* Allocate this independently of sound card instance. */
	motu = kzalloc(sizeof(struct snd_motu), GFP_KERNEL);
	if (motu == NULL)
		return -ENOMEM;

	motu->spec = (const struct snd_motu_spec *)entry->driver_data;
	motu->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, motu);

	mutex_init(&motu->mutex);

	/* Allocate and register this sound card later. */
	INIT_DEFERRABLE_WORK(&motu->dwork, do_registration);
	snd_fw_schedule_registration(unit, &motu->dwork);

	return 0;
}

static void motu_remove(struct fw_unit *unit)
{
	struct snd_motu *motu = dev_get_drvdata(&unit->device);

	/*
	 * Confirm to stop the work for registration before the sound card is
	 * going to be released. The work is not scheduled again because bus
	 * reset handler is not called anymore.
	 */
	cancel_delayed_work_sync(&motu->dwork);

	if (motu->registered) {
		/* No need to wait for releasing card object in this context. */
		snd_card_free_when_closed(motu->card);
	} else {
		/* Don't forget this case. */
		motu_free(motu);
	}
}

static void motu_bus_update(struct fw_unit *unit)
{
	struct snd_motu *motu = dev_get_drvdata(&unit->device);

	/* Postpone a workqueue for deferred registration. */
	if (!motu->registered)
		snd_fw_schedule_registration(unit, &motu->dwork);
}

#define SND_MOTU_DEV_ENTRY(model, data)			\
{							\
	.match_flags	= IEEE1394_MATCH_VENDOR_ID |	\
			  IEEE1394_MATCH_MODEL_ID |	\
			  IEEE1394_MATCH_SPECIFIER_ID,	\
	.vendor_id	= OUI_MOTU,			\
	.model_id	= model,			\
	.specifier_id	= OUI_MOTU,			\
	.driver_data	= (kernel_ulong_t)data,		\
}

static const struct ieee1394_device_id motu_id_table[] = {
	{ }
};
MODULE_DEVICE_TABLE(ieee1394, motu_id_table);

static struct fw_driver motu_driver = {
	.driver   = {
		.owner	= THIS_MODULE,
		.name	= KBUILD_MODNAME,
		.bus	= &fw_bus_type,
	},
	.probe    = motu_probe,
	.update   = motu_bus_update,
	.remove   = motu_remove,
	.id_table = motu_id_table,
};

static int __init alsa_motu_init(void)
{
	return driver_register(&motu_driver.driver);
}

static void __exit alsa_motu_exit(void)
{
	driver_unregister(&motu_driver.driver);
}

module_init(alsa_motu_init);
module_exit(alsa_motu_exit);

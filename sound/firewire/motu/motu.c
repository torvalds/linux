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
	snprintf(motu->card->longname, sizeof(motu->card->longname),
		 "MOTU (version:%d), GUID %08x%08x at %s, S%d",
		 version,
		 fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&motu->unit->device), 100 << fw_dev->max_speed);
}

static void motu_card_free(struct snd_card *card)
{
	struct snd_motu *motu = card->private_data;

	fw_unit_put(motu->unit);

	mutex_destroy(&motu->mutex);
}

static int motu_probe(struct fw_unit *unit,
		      const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_motu *motu;
	int err;

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(*motu), &card);
	if (err < 0)
		return err;

	motu = card->private_data;
	motu->card = card;
	motu->unit = fw_unit_get(unit);
	card->private_free = motu_card_free;

	mutex_init(&motu->mutex);

	name_card(motu);

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	dev_set_drvdata(&unit->device, motu);

	return 0;
error:
	snd_card_free(card);
	return err;
}

static void motu_remove(struct fw_unit *unit)
{
	struct snd_motu *motu = dev_get_drvdata(&unit->device);

	/* No need to wait for releasing card object in this context. */
	snd_card_free_when_closed(motu->card);
}

static void motu_bus_update(struct fw_unit *unit)
{
	return;
}

#define SND_MOTU_DEV_ENTRY(model)			\
{							\
	.match_flags	= IEEE1394_MATCH_VENDOR_ID |	\
			  IEEE1394_MATCH_MODEL_ID |	\
			  IEEE1394_MATCH_SPECIFIER_ID,	\
	.vendor_id	= OUI_MOTU,			\
	.model_id	= model,			\
	.specifier_id	= OUI_MOTU,			\
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

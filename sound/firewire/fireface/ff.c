/*
 * ff.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "ff.h"

#define OUI_RME	0x000a35

MODULE_DESCRIPTION("RME Fireface series Driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

static void name_card(struct snd_ff *ff)
{
	struct fw_device *fw_dev = fw_parent_device(ff->unit);
	const char *const model = "Fireface Skeleton";

	strcpy(ff->card->driver, "Fireface");
	strcpy(ff->card->shortname, model);
	strcpy(ff->card->mixername, model);
	snprintf(ff->card->longname, sizeof(ff->card->longname),
		 "RME %s, GUID %08x%08x at %s, S%d", model,
		 fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&ff->unit->device), 100 << fw_dev->max_speed);
}

static void ff_card_free(struct snd_card *card)
{
	struct snd_ff *ff = card->private_data;

	fw_unit_put(ff->unit);

	mutex_destroy(&ff->mutex);
}

static int snd_ff_probe(struct fw_unit *unit,
			   const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_ff *ff;
	int err;

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(struct snd_ff), &card);
	if (err < 0)
		return err;
	card->private_free = ff_card_free;

	/* initialize myself */
	ff = card->private_data;
	ff->card = card;
	ff->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, ff);

	mutex_init(&ff->mutex);

	name_card(ff);

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	return 0;
}

static void snd_ff_update(struct fw_unit *unit)
{
	return;
}

static void snd_ff_remove(struct fw_unit *unit)
{
	struct snd_ff *ff = dev_get_drvdata(&unit->device);

	/* No need to wait for releasing card object in this context. */
	snd_card_free_when_closed(ff->card);
}

static const struct ieee1394_device_id snd_ff_id_table[] = {
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_ff_id_table);

static struct fw_driver ff_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "snd-fireface",
		.bus	= &fw_bus_type,
	},
	.probe    = snd_ff_probe,
	.update   = snd_ff_update,
	.remove   = snd_ff_remove,
	.id_table = snd_ff_id_table,
};

static int __init snd_ff_init(void)
{
	return driver_register(&ff_driver.driver);
}

static void __exit snd_ff_exit(void)
{
	driver_unregister(&ff_driver.driver);
}

module_init(snd_ff_init);
module_exit(snd_ff_exit);

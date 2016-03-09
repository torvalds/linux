/*
 * tascam.c - a part of driver for TASCAM FireWire series
 *
 * Copyright (c) 2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "tascam.h"

MODULE_DESCRIPTION("TASCAM FireWire series Driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

static struct snd_tscm_spec model_specs[] = {
	{
		.name = "FW-1884",
		.has_adat = true,
		.has_spdif = true,
		.pcm_capture_analog_channels = 8,
		.pcm_playback_analog_channels = 8,
		.midi_capture_ports = 4,
		.midi_playback_ports = 4,
	},
	{
		.name = "FW-1082",
		.has_adat = false,
		.has_spdif = true,
		.pcm_capture_analog_channels = 8,
		.pcm_playback_analog_channels = 2,
		.midi_capture_ports = 2,
		.midi_playback_ports = 2,
	},
	{
		.name = "FW-1804",
		.has_adat = true,
		.has_spdif = true,
		.pcm_capture_analog_channels = 8,
		.pcm_playback_analog_channels = 2,
		.midi_capture_ports = 2,
		.midi_playback_ports = 4,
	},
};

static int identify_model(struct snd_tscm *tscm)
{
	struct fw_device *fw_dev = fw_parent_device(tscm->unit);
	const u32 *config_rom = fw_dev->config_rom;
	char model[9];
	unsigned int i;
	u8 c;

	if (fw_dev->config_rom_length < 30) {
		dev_err(&tscm->unit->device,
			"Configuration ROM is too short.\n");
		return -ENODEV;
	}

	/* Pick up model name from certain addresses. */
	for (i = 0; i < 8; i++) {
		c = config_rom[28 + i / 4] >> (24 - 8 * (i % 4));
		if (c == '\0')
			break;
		model[i] = c;
	}
	model[i] = '\0';

	for (i = 0; i < ARRAY_SIZE(model_specs); i++) {
		if (strcmp(model, model_specs[i].name) == 0) {
			tscm->spec = &model_specs[i];
			break;
		}
	}
	if (tscm->spec == NULL)
		return -ENODEV;

	strcpy(tscm->card->driver, "FW-TASCAM");
	strcpy(tscm->card->shortname, model);
	strcpy(tscm->card->mixername, model);
	snprintf(tscm->card->longname, sizeof(tscm->card->longname),
		 "TASCAM %s, GUID %08x%08x at %s, S%d", model,
		 fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&tscm->unit->device), 100 << fw_dev->max_speed);

	return 0;
}

static void tscm_card_free(struct snd_card *card)
{
	struct snd_tscm *tscm = card->private_data;

	snd_tscm_transaction_unregister(tscm);
	snd_tscm_stream_destroy_duplex(tscm);

	fw_unit_put(tscm->unit);

	mutex_destroy(&tscm->mutex);
}

static int snd_tscm_probe(struct fw_unit *unit,
			   const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_tscm *tscm;
	int err;

	/* create card */
	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(struct snd_tscm), &card);
	if (err < 0)
		return err;
	card->private_free = tscm_card_free;

	/* initialize myself */
	tscm = card->private_data;
	tscm->card = card;
	tscm->unit = fw_unit_get(unit);

	mutex_init(&tscm->mutex);
	spin_lock_init(&tscm->lock);
	init_waitqueue_head(&tscm->hwdep_wait);

	err = identify_model(tscm);
	if (err < 0)
		goto error;

	snd_tscm_proc_init(tscm);

	err = snd_tscm_stream_init_duplex(tscm);
	if (err < 0)
		goto error;

	err = snd_tscm_create_pcm_devices(tscm);
	if (err < 0)
		goto error;

	err = snd_tscm_transaction_register(tscm);
	if (err < 0)
		goto error;

	err = snd_tscm_create_midi_devices(tscm);
	if (err < 0)
		goto error;

	err = snd_tscm_create_hwdep_device(tscm);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	dev_set_drvdata(&unit->device, tscm);

	return err;
error:
	snd_card_free(card);
	return err;
}

static void snd_tscm_update(struct fw_unit *unit)
{
	struct snd_tscm *tscm = dev_get_drvdata(&unit->device);

	snd_tscm_transaction_reregister(tscm);

	mutex_lock(&tscm->mutex);
	snd_tscm_stream_update_duplex(tscm);
	mutex_unlock(&tscm->mutex);
}

static void snd_tscm_remove(struct fw_unit *unit)
{
	struct snd_tscm *tscm = dev_get_drvdata(&unit->device);

	/* No need to wait for releasing card object in this context. */
	snd_card_free_when_closed(tscm->card);
}

static const struct ieee1394_device_id snd_tscm_id_table[] = {
	{
		.match_flags = IEEE1394_MATCH_VENDOR_ID |
			       IEEE1394_MATCH_SPECIFIER_ID,
		.vendor_id = 0x00022e,
		.specifier_id = 0x00022e,
	},
	/* FE-08 requires reverse-engineering because it just has faders. */
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_tscm_id_table);

static struct fw_driver tscm_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "snd-firewire-tascam",
		.bus = &fw_bus_type,
	},
	.probe    = snd_tscm_probe,
	.update   = snd_tscm_update,
	.remove   = snd_tscm_remove,
	.id_table = snd_tscm_id_table,
};

static int __init snd_tscm_init(void)
{
	return driver_register(&tscm_driver.driver);
}

static void __exit snd_tscm_exit(void)
{
	driver_unregister(&tscm_driver.driver);
}

module_init(snd_tscm_init);
module_exit(snd_tscm_exit);

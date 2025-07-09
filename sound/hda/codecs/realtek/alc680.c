// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek ALC680 codec
//

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

static int alc680_parse_auto_config(struct hda_codec *codec)
{
	return alc_parse_auto_config(codec, NULL, NULL);
}

/*
 */
static int patch_alc680(struct hda_codec *codec)
{
	int err;

	/* ALC680 has no aa-loopback mixer */
	err = alc_alloc_spec(codec, 0);
	if (err < 0)
		return err;

	/* automatic parse from the BIOS config */
	err = alc680_parse_auto_config(codec);
	if (err < 0) {
		alc_free(codec);
		return err;
	}

	return 0;
}

/*
 * driver entries
 */
static const struct hda_device_id snd_hda_id_alc680[] = {
	HDA_CODEC_ENTRY(0x10ec0680, "ALC680", patch_alc680),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc680);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC680 HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");

static struct hda_codec_driver alc680_driver = {
	.id = snd_hda_id_alc680,
};

module_hda_codec_driver(alc680_driver);

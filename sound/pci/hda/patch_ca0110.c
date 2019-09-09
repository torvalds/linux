// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HD audio interface patch for Creative X-Fi CA0110-IBG chip
 *
 * Copyright (c) 2008 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"


static const struct hda_codec_ops ca0110_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.free = snd_hda_gen_free,
	.unsol_event = snd_hda_jack_unsol_event,
};

static int ca0110_parse_auto_config(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_defcfg(codec, &spec->autocfg, NULL, 0);
	if (err < 0)
		return err;
	err = snd_hda_gen_parse_auto_config(codec, &spec->autocfg);
	if (err < 0)
		return err;

	return 0;
}


static int patch_ca0110(struct hda_codec *codec)
{
	struct hda_gen_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	snd_hda_gen_spec_init(spec);
	codec->spec = spec;
	codec->patch_ops = ca0110_patch_ops;

	spec->multi_cap_vol = 1;
	codec->bus->needs_damn_long_delay = 1;

	err = ca0110_parse_auto_config(codec);
	if (err < 0)
		goto error;

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * patch entries
 */
static const struct hda_device_id snd_hda_id_ca0110[] = {
	HDA_CODEC_ENTRY(0x1102000a, "CA0110-IBG", patch_ca0110),
	HDA_CODEC_ENTRY(0x1102000b, "CA0110-IBG", patch_ca0110),
	HDA_CODEC_ENTRY(0x1102000d, "SB0880 X-Fi", patch_ca0110),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_ca0110);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Creative CA0110-IBG HD-audio codec");

static struct hda_codec_driver ca0110_driver = {
	.id = snd_hda_id_ca0110,
};

module_hda_codec_driver(ca0110_driver);

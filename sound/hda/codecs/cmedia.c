// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal codec driver for Intel High Definition Audio Codec
 *
 * HD audio codec driver for C-Media CMI9880
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "generic.h"

static int cmedia_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct hda_gen_spec *spec;
	struct auto_pin_cfg *cfg;
	bool is_cmi8888 = id->vendor_id == 0x13f68888;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	cfg = &spec->autocfg;
	snd_hda_gen_spec_init(spec);

	if (is_cmi8888) {
		/* mask NID 0x10 from the playback volume selection;
		 * it's a headphone boost volume handled manually below
		 */
		spec->out_vol_mask = (1ULL << 0x10);
	}

	err = snd_hda_parse_pin_defcfg(codec, cfg, NULL, 0);
	if (err < 0)
		goto error;
	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		goto error;

	err = snd_hda_parse_pin_defcfg(codec, cfg, NULL, 0);
	if (err < 0)
		goto error;
	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		goto error;

	if (is_cmi8888) {
		if (get_defcfg_device(snd_hda_codec_get_pincfg(codec, 0x10)) ==
		    AC_JACK_HP_OUT) {
			static const struct snd_kcontrol_new amp_kctl =
				HDA_CODEC_VOLUME("Headphone Amp Playback Volume",
						 0x10, 0, HDA_OUTPUT);
			if (!snd_hda_gen_add_kctl(spec, NULL, &amp_kctl)) {
				err = -ENOMEM;
				goto error;
			}
		}
	}

	return 0;

 error:
	snd_hda_gen_remove(codec);
	return err;
}

static const struct hda_codec_ops cmedia_codec_ops = {
	.probe = cmedia_probe,
	.remove = snd_hda_gen_remove,
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.unsol_event = snd_hda_jack_unsol_event,
	.check_power_status = snd_hda_gen_check_power_status,
	.stream_pm = snd_hda_gen_stream_pm,
};

/*
 * driver entries
 */
static const struct hda_device_id snd_hda_id_cmedia[] = {
	HDA_CODEC_ID(0x13f68888, "CMI8888"),
	HDA_CODEC_ID(0x13f69880, "CMI9880"),
	HDA_CODEC_ID(0x434d4980, "CMI9880"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_cmedia);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("C-Media HD-audio codec");

static struct hda_codec_driver cmedia_driver = {
	.id = snd_hda_id_cmedia,
	.ops = &cmedia_codec_ops,
};

module_hda_codec_driver(cmedia_driver);

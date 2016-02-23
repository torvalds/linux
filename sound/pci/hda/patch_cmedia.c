/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for C-Media CMI9880
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

struct cmi_spec {
	struct hda_gen_spec gen;
};

/*
 * stuff for auto-parser
 */
static const struct hda_codec_ops cmi_auto_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.free = snd_hda_gen_free,
	.unsol_event = snd_hda_jack_unsol_event,
};

static int patch_cmi9880(struct hda_codec *codec)
{
	struct cmi_spec *spec;
	struct auto_pin_cfg *cfg;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	codec->patch_ops = cmi_auto_patch_ops;
	cfg = &spec->gen.autocfg;
	snd_hda_gen_spec_init(&spec->gen);

	err = snd_hda_parse_pin_defcfg(codec, cfg, NULL, 0);
	if (err < 0)
		goto error;
	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		goto error;

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}

static int patch_cmi8888(struct hda_codec *codec)
{
	struct cmi_spec *spec;
	struct auto_pin_cfg *cfg;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	codec->spec = spec;
	codec->patch_ops = cmi_auto_patch_ops;
	cfg = &spec->gen.autocfg;
	snd_hda_gen_spec_init(&spec->gen);

	/* mask NID 0x10 from the playback volume selection;
	 * it's a headphone boost volume handled manually below
	 */
	spec->gen.out_vol_mask = (1ULL << 0x10);

	err = snd_hda_parse_pin_defcfg(codec, cfg, NULL, 0);
	if (err < 0)
		goto error;
	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		goto error;

	if (get_defcfg_device(snd_hda_codec_get_pincfg(codec, 0x10)) ==
	    AC_JACK_HP_OUT) {
		static const struct snd_kcontrol_new amp_kctl =
			HDA_CODEC_VOLUME("Headphone Amp Playback Volume",
					 0x10, 0, HDA_OUTPUT);
		if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &amp_kctl)) {
			err = -ENOMEM;
			goto error;
		}
	}

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}

/*
 * patch entries
 */
static const struct hda_device_id snd_hda_id_cmedia[] = {
	HDA_CODEC_ENTRY(0x13f68888, "CMI8888", patch_cmi8888),
	HDA_CODEC_ENTRY(0x13f69880, "CMI9880", patch_cmi9880),
	HDA_CODEC_ENTRY(0x434d4980, "CMI9880", patch_cmi9880),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_cmedia);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("C-Media HD-audio codec");

static struct hda_codec_driver cmedia_driver = {
	.id = snd_hda_id_cmedia,
};

module_hda_codec_driver(cmedia_driver);

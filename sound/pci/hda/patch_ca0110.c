/*
 * HD audio interface patch for Creative X-Fi CA0110-IBG chip
 *
 * Copyright (c) 2008 Takashi Iwai <tiwai@suse.de>
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

	spec->multi_cap_vol = 1;
	codec->bus->needs_damn_long_delay = 1;

	err = ca0110_parse_auto_config(codec);
	if (err < 0)
		goto error;

	codec->patch_ops = ca0110_patch_ops;

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_ca0110[] = {
	{ .id = 0x1102000a, .name = "CA0110-IBG", .patch = patch_ca0110 },
	{ .id = 0x1102000b, .name = "CA0110-IBG", .patch = patch_ca0110 },
	{ .id = 0x1102000d, .name = "SB0880 X-Fi", .patch = patch_ca0110 },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:1102000a");
MODULE_ALIAS("snd-hda-codec-id:1102000b");
MODULE_ALIAS("snd-hda-codec-id:1102000d");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Creative CA0110-IBG HD-audio codec");

static struct hda_codec_preset_list ca0110_list = {
	.preset = snd_hda_preset_ca0110,
	.owner = THIS_MODULE,
};

static int __init patch_ca0110_init(void)
{
	return snd_hda_add_codec_preset(&ca0110_list);
}

static void __exit patch_ca0110_exit(void)
{
	snd_hda_delete_codec_preset(&ca0110_list);
}

module_init(patch_ca0110_init)
module_exit(patch_ca0110_exit)

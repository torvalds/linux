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
static int alc680_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	int err;

	/* ALC680 has no aa-loopback mixer */
	err = alc_alloc_spec(codec, 0);
	if (err < 0)
		return err;

	/* automatic parse from the BIOS config */
	err = alc680_parse_auto_config(codec);
	if (err < 0) {
		snd_hda_gen_remove(codec);
		return err;
	}

	return 0;
}

static const struct hda_codec_ops alc680_codec_ops = {
	.probe = alc680_probe,
	.remove = snd_hda_gen_remove,
	.build_controls = alc_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = alc_init,
	.unsol_event = snd_hda_jack_unsol_event,
	.resume = alc_resume,
	.suspend = alc_suspend,
	.check_power_status = snd_hda_gen_check_power_status,
	.stream_pm = snd_hda_gen_stream_pm,
};

/*
 * driver entries
 */
static const struct hda_device_id snd_hda_id_alc680[] = {
	HDA_CODEC_ID(0x10ec0680, "ALC680"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc680);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC680 HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");

static struct hda_codec_driver alc680_driver = {
	.id = snd_hda_id_alc680,
	.ops = &alc680_codec_ops,
};

module_hda_codec_driver(alc680_driver);

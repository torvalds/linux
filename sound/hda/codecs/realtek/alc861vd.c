// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek ALC861-VD codec
// Based on ALC882
// In addition, an independent DAC
//

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

static int alc861vd_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc861vd_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc861vd_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc861vd_ignore, alc861vd_ssids);
}

enum {
	ALC660VD_FIX_ASUS_GPIO1,
	ALC861VD_FIX_DALLAS,
};

/* exclude VREF80 */
static void alc861vd_fixup_dallas(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		snd_hda_override_pin_caps(codec, 0x18, 0x00000734);
		snd_hda_override_pin_caps(codec, 0x19, 0x0000073c);
	}
}

/* reset GPIO1 */
static void alc660vd_fixup_asus_gpio1(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->gpio_mask |= 0x02;
	alc_fixup_gpio(codec, action, 0x01);
}

static const struct hda_fixup alc861vd_fixups[] = {
	[ALC660VD_FIX_ASUS_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc660vd_fixup_asus_gpio1,
	},
	[ALC861VD_FIX_DALLAS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc861vd_fixup_dallas,
	},
};

static const struct hda_quirk alc861vd_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30bf, "HP TX1000", ALC861VD_FIX_DALLAS),
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS A7-K", ALC660VD_FIX_ASUS_GPIO1),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba L30-149", ALC861VD_FIX_DALLAS),
	{}
};

/*
 */
static int alc861vd_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x23;

	spec->shutup = alc_eapd_shutup;

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, NULL, alc861vd_fixup_tbl, alc861vd_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc861vd_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog) {
		err = set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	snd_hda_gen_remove(codec);
	return err;
}

static const struct hda_codec_ops alc861vd_codec_ops = {
	.probe = alc861vd_probe,
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
static const struct hda_device_id snd_hda_id_alc861vd[] = {
	HDA_CODEC_ID(0x10ec0660, "ALC660-VD"),
	HDA_CODEC_ID(0x10ec0862, "ALC861-VD"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc861vd);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC861-VD HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");

static struct hda_codec_driver alc861vd_driver = {
	.id = snd_hda_id_alc861vd,
	.ops = &alc861vd_codec_ops,
};

module_hda_codec_driver(alc861vd_driver);

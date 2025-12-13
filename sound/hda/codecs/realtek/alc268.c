// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

/* bind Beep switches of both NID 0x0f and 0x10 */
static int alc268_beep_switch_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned long pval;
	int err;

	guard(mutex)(&codec->control_mutex);
	pval = kcontrol->private_value;
	kcontrol->private_value = (pval & ~0xff) | 0x0f;
	err = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
	if (err >= 0) {
		kcontrol->private_value = (pval & ~0xff) | 0x10;
		err = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
	}
	kcontrol->private_value = pval;
	return err;
}

static const struct snd_kcontrol_new alc268_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x1d, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Beep Playback Switch",
		.subdevice = HDA_SUBDEV_AMP_FLAG,
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc268_beep_switch_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x0f, 3, 1, HDA_INPUT)
	},
};

/* set PCBEEP vol = 0, mute connections */
static const struct hda_verb alc268_beep_init_verbs[] = {
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ }
};

enum {
	ALC268_FIXUP_INV_DMIC,
	ALC268_FIXUP_HP_EAPD,
	ALC268_FIXUP_SPDIF,
};

static const struct hda_fixup alc268_fixups[] = {
	[ALC268_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC268_FIXUP_HP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x15, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC268_FIXUP_SPDIF] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x014b1180 }, /* enable SPDIF out */
			{}
		}
	},
};

static const struct hda_model_fixup alc268_fixup_models[] = {
	{.id = ALC268_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC268_FIXUP_HP_EAPD, .name = "hp-eapd"},
	{.id = ALC268_FIXUP_SPDIF, .name = "spdif"},
	{}
};

static const struct hda_quirk alc268_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0139, "Acer TravelMate 6293", ALC268_FIXUP_SPDIF),
	SND_PCI_QUIRK(0x1025, 0x015b, "Acer AOA 150 (ZG5)", ALC268_FIXUP_INV_DMIC),
	/* below is codec SSID since multiple Toshiba laptops have the
	 * same PCI SSID 1179:ff00
	 */
	SND_PCI_QUIRK(0x1179, 0xff06, "Toshiba P200", ALC268_FIXUP_HP_EAPD),
	{}
};

/*
 * BIOS auto configuration
 */
static int alc268_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc268_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, NULL, alc268_ssids);
}

/*
 */
static int alc268_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct alc_spec *spec;
	int i, err;

	/* ALC268 has no aa-loopback mixer */
	err = alc_alloc_spec(codec, 0);
	if (err < 0)
		return err;

	spec = codec->spec;
	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	spec->shutup = alc_eapd_shutup;

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc268_fixup_models, alc268_fixup_tbl, alc268_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc268_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (err > 0 && !spec->gen.no_analog &&
	    spec->gen.autocfg.speaker_pins[0] != 0x1d) {
		for (i = 0; i < ARRAY_SIZE(alc268_beep_mixer); i++) {
			if (!snd_hda_gen_add_kctl(&spec->gen, NULL,
						  &alc268_beep_mixer[i])) {
				err = -ENOMEM;
				goto error;
			}
		}
		snd_hda_add_verbs(codec, alc268_beep_init_verbs);
		if (!query_amp_caps(codec, 0x1d, HDA_INPUT))
			/* override the amp caps for beep generator */
			snd_hda_override_amp_caps(codec, 0x1d, HDA_INPUT,
					  (0x0c << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x0c << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x07 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (0 << AC_AMPCAP_MUTE_SHIFT));
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	snd_hda_gen_remove(codec);
	return err;
}

static const struct hda_codec_ops alc268_codec_ops = {
	.probe = alc268_probe,
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
static const struct hda_device_id snd_hda_id_alc268[] = {
	HDA_CODEC_ID(0x10ec0267, "ALC267"),
	HDA_CODEC_ID(0x10ec0268, "ALC268"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc268);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC267/268 HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");

static struct hda_codec_driver alc268_driver = {
	.id = snd_hda_id_alc268,
	.ops = &alc268_codec_ops,
};

module_hda_codec_driver(alc268_driver);

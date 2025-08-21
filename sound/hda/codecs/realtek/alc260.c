// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek ALC260 codec
//

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

static int alc260_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc260_ignore[] = { 0x17, 0 };
	static const hda_nid_t alc260_ssids[] = { 0x10, 0x15, 0x0f, 0 };
	return alc_parse_auto_config(codec, alc260_ignore, alc260_ssids);
}

/*
 * Pin config fixes
 */
enum {
	ALC260_FIXUP_HP_DC5750,
	ALC260_FIXUP_HP_PIN_0F,
	ALC260_FIXUP_COEF,
	ALC260_FIXUP_GPIO1,
	ALC260_FIXUP_GPIO1_TOGGLE,
	ALC260_FIXUP_REPLACER,
	ALC260_FIXUP_HP_B1900,
	ALC260_FIXUP_KN1,
	ALC260_FIXUP_FSC_S7020,
	ALC260_FIXUP_FSC_S7020_JWSE,
	ALC260_FIXUP_VAIO_PINS,
};

static void alc260_gpio1_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	alc_update_gpio_data(codec, 0x01, spec->gen.hp_jack_present);
}

static void alc260_fixup_gpio1_toggle(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PROBE) {
		/* although the machine has only one output pin, we need to
		 * toggle GPIO1 according to the jack state
		 */
		spec->gen.automute_hook = alc260_gpio1_automute;
		spec->gen.detect_hp = 1;
		spec->gen.automute_speaker = 1;
		spec->gen.autocfg.hp_pins[0] = 0x0f; /* copy it for automute */
		snd_hda_jack_detect_enable_callback(codec, 0x0f,
						    snd_hda_gen_hp_automute);
		alc_setup_gpio(codec, 0x01);
	}
}

static void alc260_fixup_kn1(struct hda_codec *codec,
			     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_pintbl pincfgs[] = {
		{ 0x0f, 0x02214000 }, /* HP/speaker */
		{ 0x12, 0x90a60160 }, /* int mic */
		{ 0x13, 0x02a19000 }, /* ext mic */
		{ 0x18, 0x01446000 }, /* SPDIF out */
		/* disable bogus I/O pins */
		{ 0x10, 0x411111f0 },
		{ 0x11, 0x411111f0 },
		{ 0x14, 0x411111f0 },
		{ 0x15, 0x411111f0 },
		{ 0x16, 0x411111f0 },
		{ 0x17, 0x411111f0 },
		{ 0x19, 0x411111f0 },
		{ }
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_apply_pincfgs(codec, pincfgs);
		spec->init_amp = ALC_INIT_NONE;
		break;
	}
}

static void alc260_fixup_fsc_s7020(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->init_amp = ALC_INIT_NONE;
}

static void alc260_fixup_fsc_s7020_jwse(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.add_jack_modes = 1;
		spec->gen.hp_mic = 1;
	}
}

static const struct hda_fixup alc260_fixups[] = {
	[ALC260_FIXUP_HP_DC5750] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x11, 0x90130110 }, /* speaker */
			{ }
		}
	},
	[ALC260_FIXUP_HP_PIN_0F] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x0f, 0x01214000 }, /* HP */
			{ }
		}
	},
	[ALC260_FIXUP_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x1a, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x1a, AC_VERB_SET_PROC_COEF,  0x3040 },
			{ }
		},
	},
	[ALC260_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio1,
	},
	[ALC260_FIXUP_GPIO1_TOGGLE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_gpio1_toggle,
		.chained = true,
		.chain_id = ALC260_FIXUP_HP_PIN_0F,
	},
	[ALC260_FIXUP_REPLACER] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x1a, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x1a, AC_VERB_SET_PROC_COEF,  0x3050 },
			{ }
		},
		.chained = true,
		.chain_id = ALC260_FIXUP_GPIO1_TOGGLE,
	},
	[ALC260_FIXUP_HP_B1900] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_gpio1_toggle,
		.chained = true,
		.chain_id = ALC260_FIXUP_COEF,
	},
	[ALC260_FIXUP_KN1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_kn1,
	},
	[ALC260_FIXUP_FSC_S7020] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_fsc_s7020,
	},
	[ALC260_FIXUP_FSC_S7020_JWSE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_fsc_s7020_jwse,
		.chained = true,
		.chain_id = ALC260_FIXUP_FSC_S7020,
	},
	[ALC260_FIXUP_VAIO_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* Pin configs are missing completely on some VAIOs */
			{ 0x0f, 0x01211020 },
			{ 0x10, 0x0001003f },
			{ 0x11, 0x411111f0 },
			{ 0x12, 0x01a15930 },
			{ 0x13, 0x411111f0 },
			{ 0x14, 0x411111f0 },
			{ 0x15, 0x411111f0 },
			{ 0x16, 0x411111f0 },
			{ 0x17, 0x411111f0 },
			{ 0x18, 0x411111f0 },
			{ 0x19, 0x411111f0 },
			{ }
		}
	},
};

static const struct hda_quirk alc260_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x007b, "Acer C20x", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x1025, 0x007f, "Acer Aspire 9500", ALC260_FIXUP_COEF),
	SND_PCI_QUIRK(0x1025, 0x008f, "Acer", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x103c, 0x280a, "HP dc5750", ALC260_FIXUP_HP_DC5750),
	SND_PCI_QUIRK(0x103c, 0x30ba, "HP Presario B1900", ALC260_FIXUP_HP_B1900),
	SND_PCI_QUIRK(0x104d, 0x81bb, "Sony VAIO", ALC260_FIXUP_VAIO_PINS),
	SND_PCI_QUIRK(0x104d, 0x81e2, "Sony VAIO TX", ALC260_FIXUP_HP_PIN_0F),
	SND_PCI_QUIRK(0x10cf, 0x1326, "FSC LifeBook S7020", ALC260_FIXUP_FSC_S7020),
	SND_PCI_QUIRK(0x1509, 0x4540, "Favorit 100XS", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x152d, 0x0729, "Quanta KN1", ALC260_FIXUP_KN1),
	SND_PCI_QUIRK(0x161f, 0x2057, "Replacer 672V", ALC260_FIXUP_REPLACER),
	SND_PCI_QUIRK(0x1631, 0xc017, "PB V7900", ALC260_FIXUP_COEF),
	{}
};

static const struct hda_model_fixup alc260_fixup_models[] = {
	{.id = ALC260_FIXUP_GPIO1, .name = "gpio1"},
	{.id = ALC260_FIXUP_COEF, .name = "coef"},
	{.id = ALC260_FIXUP_FSC_S7020, .name = "fujitsu"},
	{.id = ALC260_FIXUP_FSC_S7020_JWSE, .name = "fujitsu-jwse"},
	{}
};

/*
 */
static int alc260_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x07);
	if (err < 0)
		return err;

	spec = codec->spec;
	/* as quite a few machines require HP amp for speaker outputs,
	 * it's easier to enable it unconditionally; even if it's unneeded,
	 * it's almost harmless.
	 */
	spec->gen.prefer_hp_amp = 1;
	spec->gen.beep_nid = 0x01;

	spec->shutup = alc_eapd_shutup;

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc260_fixup_models, alc260_fixup_tbl,
			   alc260_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc260_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog) {
		err = set_beep_amp(spec, 0x07, 0x05, HDA_INPUT);
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	snd_hda_gen_remove(codec);
	return err;
}

static const struct hda_codec_ops alc260_codec_ops = {
	.probe = alc260_probe,
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
static const struct hda_device_id snd_hda_id_alc260[] = {
	HDA_CODEC_ID(0x10ec0260, "ALC260"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc260);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC260 HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");

static struct hda_codec_driver alc260_driver = {
	.id = snd_hda_id_alc260,
	.ops = &alc260_codec_ops,
};

module_hda_codec_driver(alc260_driver);

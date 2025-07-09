// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek ALC262 codec
//

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

static int alc262_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc262_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc262_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc262_ignore, alc262_ssids);
}

/*
 * Pin config fixes
 */
enum {
	ALC262_FIXUP_FSC_H270,
	ALC262_FIXUP_FSC_S7110,
	ALC262_FIXUP_HP_Z200,
	ALC262_FIXUP_TYAN,
	ALC262_FIXUP_LENOVO_3000,
	ALC262_FIXUP_BENQ,
	ALC262_FIXUP_BENQ_T31,
	ALC262_FIXUP_INV_DMIC,
	ALC262_FIXUP_INTEL_BAYLEYBAY,
};

static const struct hda_fixup alc262_fixups[] = {
	[ALC262_FIXUP_FSC_H270] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0221142f }, /* front HP */
			{ 0x1b, 0x0121141f }, /* rear HP */
			{ }
		}
	},
	[ALC262_FIXUP_FSC_S7110] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x90170110 }, /* speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC262_FIXUP_BENQ,
	},
	[ALC262_FIXUP_HP_Z200] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130120 }, /* internal speaker */
			{ }
		}
	},
	[ALC262_FIXUP_TYAN] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x1993e1f0 }, /* int AUX */
			{ }
		}
	},
	[ALC262_FIXUP_LENOVO_3000] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, PIN_VREF50 },
			{}
		},
		.chained = true,
		.chain_id = ALC262_FIXUP_BENQ,
	},
	[ALC262_FIXUP_BENQ] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3070 },
			{}
		}
	},
	[ALC262_FIXUP_BENQ_T31] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{}
		}
	},
	[ALC262_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC262_FIXUP_INTEL_BAYLEYBAY] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_depop_delay,
	},
};

static const struct hda_quirk alc262_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x170b, "HP Z200", ALC262_FIXUP_HP_Z200),
	SND_PCI_QUIRK(0x10cf, 0x1397, "Fujitsu Lifebook S7110", ALC262_FIXUP_FSC_S7110),
	SND_PCI_QUIRK(0x10cf, 0x142d, "Fujitsu Lifebook E8410", ALC262_FIXUP_BENQ),
	SND_PCI_QUIRK(0x10f1, 0x2915, "Tyan Thunder n6650W", ALC262_FIXUP_TYAN),
	SND_PCI_QUIRK(0x1734, 0x1141, "FSC ESPRIMO U9210", ALC262_FIXUP_FSC_H270),
	SND_PCI_QUIRK(0x1734, 0x1147, "FSC Celsius H270", ALC262_FIXUP_FSC_H270),
	SND_PCI_QUIRK(0x17aa, 0x384e, "Lenovo 3000", ALC262_FIXUP_LENOVO_3000),
	SND_PCI_QUIRK(0x17ff, 0x0560, "Benq ED8", ALC262_FIXUP_BENQ),
	SND_PCI_QUIRK(0x17ff, 0x058d, "Benq T31-16", ALC262_FIXUP_BENQ_T31),
	SND_PCI_QUIRK(0x8086, 0x7270, "BayleyBay", ALC262_FIXUP_INTEL_BAYLEYBAY),
	{}
};

static const struct hda_model_fixup alc262_fixup_models[] = {
	{.id = ALC262_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC262_FIXUP_FSC_H270, .name = "fsc-h270"},
	{.id = ALC262_FIXUP_FSC_S7110, .name = "fsc-s7110"},
	{.id = ALC262_FIXUP_HP_Z200, .name = "hp-z200"},
	{.id = ALC262_FIXUP_TYAN, .name = "tyan"},
	{.id = ALC262_FIXUP_LENOVO_3000, .name = "lenovo-3000"},
	{.id = ALC262_FIXUP_BENQ, .name = "benq"},
	{.id = ALC262_FIXUP_BENQ_T31, .name = "benq-t31"},
	{.id = ALC262_FIXUP_INTEL_BAYLEYBAY, .name = "bayleybay"},
	{}
};

/*
 */
static int alc262_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.shared_mic_vref_pin = 0x18;

	spec->shutup = alc_eapd_shutup;

#if 0
	/* pshou 07/11/05  set a zero PCM sample to DAC when FIFO is
	 * under-run
	 */
	alc_update_coefex_idx(codec, 0x1a, 7, 0, 0x80);
#endif
	alc_fix_pll_init(codec, 0x20, 0x0a, 10);

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc262_fixup_models, alc262_fixup_tbl,
		       alc262_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	/* automatic parse from the BIOS config */
	err = alc262_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog && spec->gen.beep_nid) {
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

static const struct hda_codec_ops alc262_codec_ops = {
	.probe = alc262_probe,
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
static const struct hda_device_id snd_hda_id_alc262[] = {
	HDA_CODEC_ID(0x10ec0262, "ALC262"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc262);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC262 HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");

static struct hda_codec_driver alc262_driver = {
	.id = snd_hda_id_alc262,
	.ops = &alc262_codec_ops,
};

module_hda_codec_driver(alc262_driver);

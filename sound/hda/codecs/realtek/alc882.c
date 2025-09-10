// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek ALC882/883/885/888/889 codec support
//
// ALC882 is almost identical with ALC880 but has cleaner and more flexible
// configuration.  Each pin widget can choose any input DACs and a mixer.
// Each ADC is connected from a mixer of all inputs.  This makes possible
// 6-channel independent captures.
//
// In addition, an independent DAC for the multi-playback (not used in this
// driver yet).
//

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

/*
 * Pin config fixes
 */
enum {
	ALC882_FIXUP_ABIT_AW9D_MAX,
	ALC882_FIXUP_LENOVO_Y530,
	ALC882_FIXUP_PB_M5210,
	ALC882_FIXUP_ACER_ASPIRE_7736,
	ALC882_FIXUP_ASUS_W90V,
	ALC889_FIXUP_CD,
	ALC889_FIXUP_FRONT_HP_NO_PRESENCE,
	ALC889_FIXUP_VAIO_TT,
	ALC888_FIXUP_EEE1601,
	ALC886_FIXUP_EAPD,
	ALC882_FIXUP_EAPD,
	ALC883_FIXUP_EAPD,
	ALC883_FIXUP_ACER_EAPD,
	ALC882_FIXUP_GPIO1,
	ALC882_FIXUP_GPIO2,
	ALC882_FIXUP_GPIO3,
	ALC889_FIXUP_COEF,
	ALC882_FIXUP_ASUS_W2JC,
	ALC882_FIXUP_ACER_ASPIRE_4930G,
	ALC882_FIXUP_ACER_ASPIRE_8930G,
	ALC882_FIXUP_ASPIRE_8930G_VERBS,
	ALC885_FIXUP_MACPRO_GPIO,
	ALC889_FIXUP_DAC_ROUTE,
	ALC889_FIXUP_MBP_VREF,
	ALC889_FIXUP_IMAC91_VREF,
	ALC889_FIXUP_MBA11_VREF,
	ALC889_FIXUP_MBA21_VREF,
	ALC889_FIXUP_MP11_VREF,
	ALC889_FIXUP_MP41_VREF,
	ALC882_FIXUP_INV_DMIC,
	ALC882_FIXUP_NO_PRIMARY_HP,
	ALC887_FIXUP_ASUS_BASS,
	ALC887_FIXUP_BASS_CHMAP,
	ALC1220_FIXUP_GB_DUAL_CODECS,
	ALC1220_FIXUP_GB_X570,
	ALC1220_FIXUP_CLEVO_P950,
	ALC1220_FIXUP_CLEVO_PB51ED,
	ALC1220_FIXUP_CLEVO_PB51ED_PINS,
	ALC887_FIXUP_ASUS_AUDIO,
	ALC887_FIXUP_ASUS_HMIC,
	ALCS1200A_FIXUP_MIC_VREF,
	ALC888VD_FIXUP_MIC_100VREF,
};

static void alc889_fixup_coef(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_INIT)
		return;
	alc_update_coef_idx(codec, 7, 0, 0x2030);
}

/* set up GPIO at initialization */
static void alc885_fixup_macpro_gpio(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	spec->gpio_write_delay = true;
	alc_fixup_gpio3(codec, fix, action);
}

/* Fix the connection of some pins for ALC889:
 * At least, Acer Aspire 5935 shows the connections to DAC3/4 don't
 * work correctly (bko#42740)
 */
static void alc889_fixup_dac_route(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* fake the connections during parsing the tree */
		static const hda_nid_t conn1[] = { 0x0c, 0x0d };
		static const hda_nid_t conn2[] = { 0x0e, 0x0f };
		snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn1), conn1);
		snd_hda_override_conn_list(codec, 0x15, ARRAY_SIZE(conn1), conn1);
		snd_hda_override_conn_list(codec, 0x18, ARRAY_SIZE(conn2), conn2);
		snd_hda_override_conn_list(codec, 0x1a, ARRAY_SIZE(conn2), conn2);
	} else if (action == HDA_FIXUP_ACT_PROBE) {
		/* restore the connections */
		static const hda_nid_t conn[] = { 0x0c, 0x0d, 0x0e, 0x0f, 0x26 };
		snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn), conn);
		snd_hda_override_conn_list(codec, 0x15, ARRAY_SIZE(conn), conn);
		snd_hda_override_conn_list(codec, 0x18, ARRAY_SIZE(conn), conn);
		snd_hda_override_conn_list(codec, 0x1a, ARRAY_SIZE(conn), conn);
	}
}

/* Set VREF on HP pin */
static void alc889_fixup_mbp_vref(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	static const hda_nid_t nids[] = { 0x14, 0x15, 0x19 };
	struct alc_spec *spec = codec->spec;
	int i;

	if (action != HDA_FIXUP_ACT_INIT)
		return;
	for (i = 0; i < ARRAY_SIZE(nids); i++) {
		unsigned int val = snd_hda_codec_get_pincfg(codec, nids[i]);
		if (get_defcfg_device(val) != AC_JACK_HP_OUT)
			continue;
		val = snd_hda_codec_get_pin_target(codec, nids[i]);
		val |= AC_PINCTL_VREF_80;
		snd_hda_set_pin_ctl(codec, nids[i], val);
		spec->gen.keep_vref_in_automute = 1;
		break;
	}
}

static void alc889_fixup_mac_pins(struct hda_codec *codec,
				  const hda_nid_t *nids, int num_nids)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < num_nids; i++) {
		unsigned int val;
		val = snd_hda_codec_get_pin_target(codec, nids[i]);
		val |= AC_PINCTL_VREF_50;
		snd_hda_set_pin_ctl(codec, nids[i], val);
	}
	spec->gen.keep_vref_in_automute = 1;
}

/* Set VREF on speaker pins on imac91 */
static void alc889_fixup_imac91_vref(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	static const hda_nid_t nids[] = { 0x18, 0x1a };

	if (action == HDA_FIXUP_ACT_INIT)
		alc889_fixup_mac_pins(codec, nids, ARRAY_SIZE(nids));
}

/* Set VREF on speaker pins on mba11 */
static void alc889_fixup_mba11_vref(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	static const hda_nid_t nids[] = { 0x18 };

	if (action == HDA_FIXUP_ACT_INIT)
		alc889_fixup_mac_pins(codec, nids, ARRAY_SIZE(nids));
}

/* Set VREF on speaker pins on mba21 */
static void alc889_fixup_mba21_vref(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	static const hda_nid_t nids[] = { 0x18, 0x19 };

	if (action == HDA_FIXUP_ACT_INIT)
		alc889_fixup_mac_pins(codec, nids, ARRAY_SIZE(nids));
}

/* Don't take HP output as primary
 * Strangely, the speaker output doesn't work on Vaio Z and some Vaio
 * all-in-one desktop PCs (for example VGC-LN51JGB) through DAC 0x05
 */
static void alc882_fixup_no_primary_hp(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.no_primary_hp = 1;
		spec->gen.no_multi_io = 1;
	}
}

static void alc1220_fixup_gb_x570(struct hda_codec *codec,
				     const struct hda_fixup *fix,
				     int action)
{
	static const hda_nid_t conn1[] = { 0x0c };
	static const struct coef_fw gb_x570_coefs[] = {
		WRITE_COEF(0x07, 0x03c0),
		WRITE_COEF(0x1a, 0x01c1),
		WRITE_COEF(0x1b, 0x0202),
		WRITE_COEF(0x43, 0x3005),
		{}
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn1), conn1);
		snd_hda_override_conn_list(codec, 0x1b, ARRAY_SIZE(conn1), conn1);
		break;
	case HDA_FIXUP_ACT_INIT:
		alc_process_coef_fw(codec, gb_x570_coefs);
		break;
	}
}

static void alc1220_fixup_clevo_p950(struct hda_codec *codec,
				     const struct hda_fixup *fix,
				     int action)
{
	static const hda_nid_t conn1[] = { 0x0c };

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	alc_update_coef_idx(codec, 0x7, 0, 0x3c3);
	/* We therefore want to make sure 0x14 (front headphone) and
	 * 0x1b (speakers) use the stereo DAC 0x02
	 */
	snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn1), conn1);
	snd_hda_override_conn_list(codec, 0x1b, ARRAY_SIZE(conn1), conn1);
}

static void alc1220_fixup_clevo_pb51ed(struct hda_codec *codec,
				     const struct hda_fixup *fix,
				     int action)
{
	alc1220_fixup_clevo_p950(codec, fix, action);
	alc_fixup_headset_mode_no_hp_mic(codec, fix, action);
}

static void alc887_asus_hp_automute_hook(struct hda_codec *codec,
					 struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;
	unsigned int vref;

	snd_hda_gen_hp_automute(codec, jack);

	if (spec->gen.hp_jack_present)
		vref = AC_PINCTL_VREF_80;
	else
		vref = AC_PINCTL_VREF_HIZ;
	snd_hda_set_pin_ctl(codec, 0x19, PIN_HP | vref);
}

static void alc887_fixup_asus_jack(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action != HDA_FIXUP_ACT_PROBE)
		return;
	snd_hda_set_pin_ctl_cache(codec, 0x1b, PIN_HP);
	spec->gen.hp_automute_hook = alc887_asus_hp_automute_hook;
}

static const struct hda_fixup alc882_fixups[] = {
	[ALC882_FIXUP_ABIT_AW9D_MAX] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x01080104 }, /* side */
			{ 0x16, 0x01011012 }, /* rear */
			{ 0x17, 0x01016011 }, /* clfe */
			{ }
		}
	},
	[ALC882_FIXUP_LENOVO_Y530] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x99130112 }, /* rear int speakers */
			{ 0x16, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[ALC882_FIXUP_PB_M5210] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, PIN_VREF50 },
			{}
		}
	},
	[ALC882_FIXUP_ACER_ASPIRE_7736] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_sku_ignore,
	},
	[ALC882_FIXUP_ASUS_W90V] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130110 }, /* fix sequence for CLFE */
			{ }
		}
	},
	[ALC889_FIXUP_CD] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1c, 0x993301f0 }, /* CD */
			{ }
		}
	},
	[ALC889_FIXUP_FRONT_HP_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x02214120 }, /* Front HP jack is flaky, disable jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC889_FIXUP_CD,
	},
	[ALC889_FIXUP_VAIO_TT] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170111 }, /* hidden surround speaker */
			{ }
		}
	},
	[ALC888_FIXUP_EEE1601] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0b },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x0838 },
			{ }
		}
	},
	[ALC886_FIXUP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0068 },
			{ }
		}
	},
	[ALC882_FIXUP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3060 },
			{ }
		}
	},
	[ALC883_FIXUP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3070 },
			{ }
		}
	},
	[ALC883_FIXUP_ACER_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* eanable EAPD on Acer laptops */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{ }
		}
	},
	[ALC882_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio1,
	},
	[ALC882_FIXUP_GPIO2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio2,
	},
	[ALC882_FIXUP_GPIO3] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio3,
	},
	[ALC882_FIXUP_ASUS_W2JC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio1,
		.chained = true,
		.chain_id = ALC882_FIXUP_EAPD,
	},
	[ALC889_FIXUP_COEF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_coef,
	},
	[ALC882_FIXUP_ACER_ASPIRE_4930G] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130111 }, /* CLFE speaker */
			{ 0x17, 0x99130112 }, /* surround speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC882_FIXUP_ACER_ASPIRE_8930G] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130111 }, /* CLFE speaker */
			{ 0x1b, 0x99130112 }, /* surround speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_ASPIRE_8930G_VERBS,
	},
	[ALC882_FIXUP_ASPIRE_8930G_VERBS] = {
		/* additional init verbs for Acer Aspire 8930G */
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enable all DACs */
			/* DAC DISABLE/MUTE 1? */
			/*  setting bits 1-5 disables DAC nids 0x02-0x06
			 *  apparently. Init=0x38 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x03 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0000 },
			/* DAC DISABLE/MUTE 2? */
			/*  some bit here disables the other DACs.
			 *  Init=0x4900 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x08 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0000 },
			/* DMIC fix
			 * This laptop has a stereo digital microphone.
			 * The mics are only 1cm apart which makes the stereo
			 * useless. However, either the mic or the ALC889
			 * makes the signal become a difference/sum signal
			 * instead of standard stereo, which is annoying.
			 * So instead we flip this bit which makes the
			 * codec replicate the sum signal to both channels,
			 * turning it into a normal mono mic.
			 */
			/* DMIC_CONTROL? Init value = 0x0001 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0b },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0003 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC885_FIXUP_MACPRO_GPIO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc885_fixup_macpro_gpio,
	},
	[ALC889_FIXUP_DAC_ROUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_dac_route,
	},
	[ALC889_FIXUP_MBP_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mbp_vref,
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC889_FIXUP_IMAC91_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_imac91_vref,
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC889_FIXUP_MBA11_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mba11_vref,
		.chained = true,
		.chain_id = ALC889_FIXUP_MBP_VREF,
	},
	[ALC889_FIXUP_MBA21_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mba21_vref,
		.chained = true,
		.chain_id = ALC889_FIXUP_MBP_VREF,
	},
	[ALC889_FIXUP_MP11_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mba11_vref,
		.chained = true,
		.chain_id = ALC885_FIXUP_MACPRO_GPIO,
	},
	[ALC889_FIXUP_MP41_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mbp_vref,
		.chained = true,
		.chain_id = ALC885_FIXUP_MACPRO_GPIO,
	},
	[ALC882_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC882_FIXUP_NO_PRIMARY_HP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc882_fixup_no_primary_hp,
	},
	[ALC887_FIXUP_ASUS_BASS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x16, 0x99130130}, /* bass speaker */
			{}
		},
		.chained = true,
		.chain_id = ALC887_FIXUP_BASS_CHMAP,
	},
	[ALC887_FIXUP_BASS_CHMAP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_bass_chmap,
	},
	[ALC1220_FIXUP_GB_DUAL_CODECS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc1220_fixup_gb_dual_codecs,
	},
	[ALC1220_FIXUP_GB_X570] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc1220_fixup_gb_x570,
	},
	[ALC1220_FIXUP_CLEVO_P950] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc1220_fixup_clevo_p950,
	},
	[ALC1220_FIXUP_CLEVO_PB51ED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc1220_fixup_clevo_pb51ed,
	},
	[ALC1220_FIXUP_CLEVO_PB51ED_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{}
		},
		.chained = true,
		.chain_id = ALC1220_FIXUP_CLEVO_PB51ED,
	},
	[ALC887_FIXUP_ASUS_AUDIO] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x02a14150 }, /* use as headset mic, without its own jack detect */
			{ 0x19, 0x22219420 },
			{}
		},
	},
	[ALC887_FIXUP_ASUS_HMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc887_fixup_asus_jack,
		.chained = true,
		.chain_id = ALC887_FIXUP_ASUS_AUDIO,
	},
	[ALCS1200A_FIXUP_MIC_VREF] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, PIN_VREF50 }, /* rear mic */
			{ 0x19, PIN_VREF50 }, /* front mic */
			{}
		}
	},
	[ALC888VD_FIXUP_MIC_100VREF] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, PIN_VREF100 }, /* headset mic */
			{}
		}
	},
};

static const struct hda_quirk alc882_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x006c, "Acer Aspire 9810", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0090, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0107, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x010a, "Acer Ferrari 5000", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0110, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0112, "Acer Aspire 9303", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0121, "Acer Aspire 5920G", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x013e, "Acer Aspire 4930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x013f, "Acer Aspire 5930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0145, "Acer Aspire 8930G",
		      ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0146, "Acer Aspire 6935G",
		      ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0142, "Acer Aspire 7730G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0155, "Packard-Bell M5120", ALC882_FIXUP_PB_M5210),
	SND_PCI_QUIRK(0x1025, 0x015e, "Acer Aspire 6930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0166, "Acer Aspire 6530G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x021e, "Acer Aspire 5739G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0259, "Acer Aspire 5935", ALC889_FIXUP_DAC_ROUTE),
	SND_PCI_QUIRK(0x1025, 0x026b, "Acer Aspire 8940G", ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0296, "Acer Aspire 7736z", ALC882_FIXUP_ACER_ASPIRE_7736),
	SND_PCI_QUIRK(0x1043, 0x13c2, "Asus A7M", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1873, "ASUS W90V", ALC882_FIXUP_ASUS_W90V),
	SND_PCI_QUIRK(0x1043, 0x1971, "Asus W2JC", ALC882_FIXUP_ASUS_W2JC),
	SND_PCI_QUIRK(0x1043, 0x2390, "Asus D700SA", ALC887_FIXUP_ASUS_HMIC),
	SND_PCI_QUIRK(0x1043, 0x835f, "Asus Eee 1601", ALC888_FIXUP_EEE1601),
	SND_PCI_QUIRK(0x1043, 0x84bc, "ASUS ET2700", ALC887_FIXUP_ASUS_BASS),
	SND_PCI_QUIRK(0x1043, 0x8691, "ASUS ROG Ranger VIII", ALC882_FIXUP_GPIO3),
	SND_PCI_QUIRK(0x1043, 0x8797, "ASUS TUF B550M-PLUS", ALCS1200A_FIXUP_MIC_VREF),
	SND_PCI_QUIRK(0x104d, 0x9043, "Sony Vaio VGC-LN51JGB", ALC882_FIXUP_NO_PRIMARY_HP),
	SND_PCI_QUIRK(0x104d, 0x9044, "Sony VAIO AiO", ALC882_FIXUP_NO_PRIMARY_HP),
	SND_PCI_QUIRK(0x104d, 0x9047, "Sony Vaio TT", ALC889_FIXUP_VAIO_TT),
	SND_PCI_QUIRK(0x104d, 0x905a, "Sony Vaio Z", ALC882_FIXUP_NO_PRIMARY_HP),
	SND_PCI_QUIRK(0x104d, 0x9060, "Sony Vaio VPCL14M1R", ALC882_FIXUP_NO_PRIMARY_HP),

	/* All Apple entries are in codec SSIDs */
	SND_PCI_QUIRK(0x106b, 0x00a0, "MacBookPro 3,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x00a1, "Macbook", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x00a4, "MacbookPro 4,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x0c00, "Mac Pro", ALC889_FIXUP_MP11_VREF),
	SND_PCI_QUIRK(0x106b, 0x1000, "iMac 24", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x2800, "AppleTV", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x2c00, "MacbookPro rev3", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3000, "iMac", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3200, "iMac 7,1 Aluminum", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x106b, 0x3400, "MacBookAir 1,1", ALC889_FIXUP_MBA11_VREF),
	SND_PCI_QUIRK(0x106b, 0x3500, "MacBookAir 2,1", ALC889_FIXUP_MBA21_VREF),
	SND_PCI_QUIRK(0x106b, 0x3600, "Macbook 3,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3800, "MacbookPro 4,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3e00, "iMac 24 Aluminum", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x3f00, "Macbook 5,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4000, "MacbookPro 5,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4100, "Macmini 3,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4200, "Mac Pro 4,1/5,1", ALC889_FIXUP_MP41_VREF),
	SND_PCI_QUIRK(0x106b, 0x4300, "iMac 9,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4600, "MacbookPro 5,2", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4900, "iMac 9,1 Aluminum", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4a00, "Macbook 5,2", ALC889_FIXUP_MBA11_VREF),

	SND_PCI_QUIRK(0x1071, 0x8258, "Evesham Voyaeger", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x10ec, 0x12d8, "iBase Elo Touch", ALC888VD_FIXUP_MIC_100VREF),
	SND_PCI_QUIRK(0x13fe, 0x1009, "Advantech MIT-W101", ALC886_FIXUP_EAPD),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte EP45-DS3/Z87X-UD3H", ALC889_FIXUP_FRONT_HP_NO_PRESENCE),
	SND_PCI_QUIRK(0x1458, 0xa0b8, "Gigabyte AZ370-Gaming", ALC1220_FIXUP_GB_DUAL_CODECS),
	SND_PCI_QUIRK(0x1458, 0xa0cd, "Gigabyte X570 Aorus Master", ALC1220_FIXUP_GB_X570),
	SND_PCI_QUIRK(0x1458, 0xa0ce, "Gigabyte X570 Aorus Xtreme", ALC1220_FIXUP_GB_X570),
	SND_PCI_QUIRK(0x1458, 0xa0d5, "Gigabyte X570S Aorus Master", ALC1220_FIXUP_GB_X570),
	SND_PCI_QUIRK(0x1462, 0x11f7, "MSI-GE63", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x1228, "MSI-GP63", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x1229, "MSI-GP73", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x1275, "MSI-GL63", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x1276, "MSI-GL73", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x1293, "MSI-GP65", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x7350, "MSI-7350", ALC889_FIXUP_CD),
	SND_PCI_QUIRK(0x1462, 0xcc34, "MSI Godlike X570", ALC1220_FIXUP_GB_DUAL_CODECS),
	SND_PCI_QUIRK(0x1462, 0xda57, "MSI Z270-Gaming", ALC1220_FIXUP_GB_DUAL_CODECS),
	SND_PCI_QUIRK_VENDOR(0x1462, "MSI", ALC882_FIXUP_GPIO3),
	SND_PCI_QUIRK(0x147b, 0x107a, "Abit AW9D-MAX", ALC882_FIXUP_ABIT_AW9D_MAX),
	SND_PCI_QUIRK(0x1558, 0x3702, "Clevo X370SN[VW]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x50d3, "Clevo PC50[ER][CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x5802, "Clevo X58[05]WN[RST]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x65d1, "Clevo PB51[ER][CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x65d2, "Clevo PB51R[CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x65e1, "Clevo PB51[ED][DF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x65e5, "Clevo PC50D[PRS](?:-D|-G)?", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x65f1, "Clevo PC50HS", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x65f5, "Clevo PD50PN[NRT]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x66a2, "Clevo PE60RNE", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x66a6, "Clevo PE60SN[CDE]-[GS]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x67d1, "Clevo PB71[ER][CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x67e1, "Clevo PB71[DE][CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x67e5, "Clevo PC70D[PRS](?:-D|-G)?", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x67f1, "Clevo PC70H[PRS]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x67f5, "Clevo PD70PN[NRT]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x70d1, "Clevo PC70[ER][CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x7714, "Clevo X170SM", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x7715, "Clevo X170KM-G", ALC1220_FIXUP_CLEVO_PB51ED),
	SND_PCI_QUIRK(0x1558, 0x9501, "Clevo P950HR", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x9506, "Clevo P955HQ", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x950a, "Clevo P955H[PR]", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x95e1, "Clevo P95xER", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x95e2, "Clevo P950ER", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x95e3, "Clevo P955[ER]T", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x95e4, "Clevo P955ER", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x95e5, "Clevo P955EE6", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x95e6, "Clevo P950R[CDF]", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x96e1, "Clevo P960[ER][CDFN]-K", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x97e1, "Clevo P970[ER][CDFN]", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x97e2, "Clevo P970RC-M", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0xd502, "Clevo PD50SNE", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo laptop", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x161f, 0x2054, "Medion laptop", ALC883_FIXUP_EAPD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Y530", ALC882_FIXUP_LENOVO_Y530),
	SND_PCI_QUIRK(0x8086, 0x0022, "DX58SO", ALC889_FIXUP_COEF),
	{}
};

static const struct hda_model_fixup alc882_fixup_models[] = {
	{.id = ALC882_FIXUP_ABIT_AW9D_MAX, .name = "abit-aw9d"},
	{.id = ALC882_FIXUP_LENOVO_Y530, .name = "lenovo-y530"},
	{.id = ALC882_FIXUP_ACER_ASPIRE_7736, .name = "acer-aspire-7736"},
	{.id = ALC882_FIXUP_ASUS_W90V, .name = "asus-w90v"},
	{.id = ALC889_FIXUP_CD, .name = "cd"},
	{.id = ALC889_FIXUP_FRONT_HP_NO_PRESENCE, .name = "no-front-hp"},
	{.id = ALC889_FIXUP_VAIO_TT, .name = "vaio-tt"},
	{.id = ALC888_FIXUP_EEE1601, .name = "eee1601"},
	{.id = ALC882_FIXUP_EAPD, .name = "alc882-eapd"},
	{.id = ALC883_FIXUP_EAPD, .name = "alc883-eapd"},
	{.id = ALC882_FIXUP_GPIO1, .name = "gpio1"},
	{.id = ALC882_FIXUP_GPIO2, .name = "gpio2"},
	{.id = ALC882_FIXUP_GPIO3, .name = "gpio3"},
	{.id = ALC889_FIXUP_COEF, .name = "alc889-coef"},
	{.id = ALC882_FIXUP_ASUS_W2JC, .name = "asus-w2jc"},
	{.id = ALC882_FIXUP_ACER_ASPIRE_4930G, .name = "acer-aspire-4930g"},
	{.id = ALC882_FIXUP_ACER_ASPIRE_8930G, .name = "acer-aspire-8930g"},
	{.id = ALC883_FIXUP_ACER_EAPD, .name = "acer-aspire"},
	{.id = ALC885_FIXUP_MACPRO_GPIO, .name = "macpro-gpio"},
	{.id = ALC889_FIXUP_DAC_ROUTE, .name = "dac-route"},
	{.id = ALC889_FIXUP_MBP_VREF, .name = "mbp-vref"},
	{.id = ALC889_FIXUP_IMAC91_VREF, .name = "imac91-vref"},
	{.id = ALC889_FIXUP_MBA11_VREF, .name = "mba11-vref"},
	{.id = ALC889_FIXUP_MBA21_VREF, .name = "mba21-vref"},
	{.id = ALC889_FIXUP_MP11_VREF, .name = "mp11-vref"},
	{.id = ALC889_FIXUP_MP41_VREF, .name = "mp41-vref"},
	{.id = ALC882_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC882_FIXUP_NO_PRIMARY_HP, .name = "no-primary-hp"},
	{.id = ALC887_FIXUP_ASUS_BASS, .name = "asus-bass"},
	{.id = ALC1220_FIXUP_GB_DUAL_CODECS, .name = "dual-codecs"},
	{.id = ALC1220_FIXUP_GB_X570, .name = "gb-x570"},
	{.id = ALC1220_FIXUP_CLEVO_P950, .name = "clevo-p950"},
	{}
};

static const struct snd_hda_pin_quirk alc882_pin_fixup_tbl[] = {
	SND_HDA_PIN_QUIRK(0x10ec1220, 0x1043, "ASUS", ALC1220_FIXUP_CLEVO_P950,
		{0x14, 0x01014010},
		{0x15, 0x01011012},
		{0x16, 0x01016011},
		{0x18, 0x01a19040},
		{0x19, 0x02a19050},
		{0x1a, 0x0181304f},
		{0x1b, 0x0221401f},
		{0x1e, 0x01456130}),
	SND_HDA_PIN_QUIRK(0x10ec1220, 0x1462, "MS-7C35", ALC1220_FIXUP_CLEVO_P950,
		{0x14, 0x01015010},
		{0x15, 0x01011012},
		{0x16, 0x01011011},
		{0x18, 0x01a11040},
		{0x19, 0x02a19050},
		{0x1a, 0x0181104f},
		{0x1b, 0x0221401f},
		{0x1e, 0x01451130}),
	{}
};

/*
 * BIOS auto configuration
 */
/* almost identical with ALC880 parser... */
static int alc882_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc882_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc882_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc882_ignore, alc882_ssids);
}

/*
 */
static int alc882_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;

	switch (codec->core.vendor_id) {
	case 0x10ec0882:
	case 0x10ec0885:
	case 0x10ec0900:
	case 0x10ec0b00:
	case 0x10ec1220:
		break;
	default:
		/* ALC883 and variants */
		alc_fix_pll_init(codec, 0x20, 0x0a, 10);
		break;
	}

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc882_fixup_models, alc882_fixup_tbl,
		       alc882_fixups);
	snd_hda_pick_pin_fixup(codec, alc882_pin_fixup_tbl, alc882_fixups, true);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	/* automatic parse from the BIOS config */
	err = alc882_parse_auto_config(codec);
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

static const struct hda_codec_ops alc882_codec_ops = {
	.probe = alc882_probe,
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
static const struct hda_device_id snd_hda_id_alc882[] = {
	HDA_CODEC_ID_REV(0x10ec0662, 0x100002, "ALC662 rev2"),
	HDA_CODEC_ID(0x10ec0882, "ALC882"),
	HDA_CODEC_ID(0x10ec0883, "ALC883"),
	HDA_CODEC_ID_REV(0x10ec0885, 0x100101, "ALC889A"),
	HDA_CODEC_ID_REV(0x10ec0885, 0x100103, "ALC889A"),
	HDA_CODEC_ID(0x10ec0885, "ALC885"),
	HDA_CODEC_ID(0x10ec0887, "ALC887"),
	HDA_CODEC_ID_REV(0x10ec0888, 0x100101, "ALC1200"),
	HDA_CODEC_ID(0x10ec0888, "ALC888"),
	HDA_CODEC_ID(0x10ec0889, "ALC889"),
	HDA_CODEC_ID(0x10ec0899, "ALC898"),
	HDA_CODEC_ID(0x10ec0900, "ALC1150"),
	HDA_CODEC_ID(0x10ec0b00, "ALCS1200A"),
	HDA_CODEC_ID(0x10ec1168, "ALC1220"),
	HDA_CODEC_ID(0x10ec1220, "ALC1220"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc882);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC882 and compatible HD-audio codecs");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");

static struct hda_codec_driver alc882_driver = {
	.id = snd_hda_id_alc882,
	.ops = &alc882_codec_ops,
};

module_hda_codec_driver(alc882_driver);

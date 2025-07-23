// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek ALC880 codec
//

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

static void alc880_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* For some reason, the res given from ALC880 is broken.
	   Here we adjust it properly. */
	snd_hda_jack_unsol_event(codec, res >> 2);
}

static int alc880_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc880_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc880_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc880_ignore, alc880_ssids);
}

/*
 * ALC880 fix-ups
 */
enum {
	ALC880_FIXUP_GPIO1,
	ALC880_FIXUP_GPIO2,
	ALC880_FIXUP_MEDION_RIM,
	ALC880_FIXUP_LG,
	ALC880_FIXUP_LG_LW25,
	ALC880_FIXUP_W810,
	ALC880_FIXUP_EAPD_COEF,
	ALC880_FIXUP_TCL_S700,
	ALC880_FIXUP_VOL_KNOB,
	ALC880_FIXUP_FUJITSU,
	ALC880_FIXUP_F1734,
	ALC880_FIXUP_UNIWILL,
	ALC880_FIXUP_UNIWILL_DIG,
	ALC880_FIXUP_Z71V,
	ALC880_FIXUP_ASUS_W5A,
	ALC880_FIXUP_3ST_BASE,
	ALC880_FIXUP_3ST,
	ALC880_FIXUP_3ST_DIG,
	ALC880_FIXUP_5ST_BASE,
	ALC880_FIXUP_5ST,
	ALC880_FIXUP_5ST_DIG,
	ALC880_FIXUP_6ST_BASE,
	ALC880_FIXUP_6ST,
	ALC880_FIXUP_6ST_DIG,
	ALC880_FIXUP_6ST_AUTOMUTE,
};

/* enable the volume-knob widget support on NID 0x21 */
static void alc880_fixup_vol_knob(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PROBE)
		snd_hda_jack_detect_enable_callback(codec, 0x21,
						    alc_update_knob_master);
}

static const struct hda_fixup alc880_fixups[] = {
	[ALC880_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio1,
	},
	[ALC880_FIXUP_GPIO2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio2,
	},
	[ALC880_FIXUP_MEDION_RIM] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3060 },
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_LG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* disable bogus unused pins */
			{ 0x16, 0x411111f0 },
			{ 0x18, 0x411111f0 },
			{ 0x1a, 0x411111f0 },
			{ }
		}
	},
	[ALC880_FIXUP_LG_LW25] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x0181344f }, /* line-in */
			{ 0x1b, 0x0321403f }, /* headphone */
			{ }
		}
	},
	[ALC880_FIXUP_W810] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* disable bogus unused pins */
			{ 0x17, 0x411111f0 },
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_EAPD_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3060 },
			{}
		},
	},
	[ALC880_FIXUP_TCL_S700] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3070 },
			{}
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_VOL_KNOB] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc880_fixup_vol_knob,
	},
	[ALC880_FIXUP_FUJITSU] = {
		/* override all pins as BIOS on old Amilo is broken */
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0121401f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x99030130 }, /* bass speaker */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x411111f0 }, /* N/A */
			{ 0x19, 0x01a19950 }, /* mic-in */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x01454140 }, /* SPDIF out */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_VOL_KNOB,
	},
	[ALC880_FIXUP_F1734] = {
		/* almost compatible with FUJITSU, but no bass and SPDIF */
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0121401f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x411111f0 }, /* N/A */
			{ 0x19, 0x01a19950 }, /* mic-in */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_VOL_KNOB,
	},
	[ALC880_FIXUP_UNIWILL] = {
		/* need to fix HP and speaker pins to be parsed correctly */
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0121411f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x99030130 }, /* bass speaker */
			{ }
		},
	},
	[ALC880_FIXUP_UNIWILL_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* disable bogus unused pins */
			{ 0x17, 0x411111f0 },
			{ 0x19, 0x411111f0 },
			{ 0x1b, 0x411111f0 },
			{ 0x1f, 0x411111f0 },
			{ }
		}
	},
	[ALC880_FIXUP_Z71V] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* set up the whole pins as BIOS is utterly broken */
			{ 0x14, 0x99030120 }, /* speaker */
			{ 0x15, 0x0121411f }, /* HP */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x01a19950 }, /* mic-in */
			{ 0x19, 0x411111f0 }, /* N/A */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		}
	},
	[ALC880_FIXUP_ASUS_W5A] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* set up the whole pins as BIOS is utterly broken */
			{ 0x14, 0x0121411f }, /* HP */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x90a60160 }, /* mic */
			{ 0x19, 0x411111f0 }, /* N/A */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0xb743111e }, /* SPDIF out */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO1,
	},
	[ALC880_FIXUP_3ST_BASE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x01014010 }, /* line-out */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x0121411f }, /* HP */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x02a19c40 }, /* front-mic */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_3ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_3ST_BASE,
	},
	[ALC880_FIXUP_3ST_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_3ST_BASE,
	},
	[ALC880_FIXUP_5ST_BASE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x01014010 }, /* front */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x01011411 }, /* CLFE */
			{ 0x17, 0x01016412 }, /* surr */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x0121411f }, /* HP */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x02a19c40 }, /* front-mic */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_5ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_5ST_BASE,
	},
	[ALC880_FIXUP_5ST_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_5ST_BASE,
	},
	[ALC880_FIXUP_6ST_BASE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x01014010 }, /* front */
			{ 0x15, 0x01016412 }, /* surr */
			{ 0x16, 0x01011411 }, /* CLFE */
			{ 0x17, 0x01012414 }, /* side */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x02a19c40 }, /* front-mic */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x0121411f }, /* HP */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_6ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
	[ALC880_FIXUP_6ST_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
	[ALC880_FIXUP_6ST_AUTOMUTE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x0121401f }, /* HP with jack detect */
			{ }
		},
		.chained_before = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
};

static const struct hda_quirk alc880_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x0f69, "Coeus G610P", ALC880_FIXUP_W810),
	SND_PCI_QUIRK(0x1043, 0x10c3, "ASUS W5A", ALC880_FIXUP_ASUS_W5A),
	SND_PCI_QUIRK(0x1043, 0x1964, "ASUS Z71V", ALC880_FIXUP_Z71V),
	SND_PCI_QUIRK_VENDOR(0x1043, "ASUS", ALC880_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x147b, 0x1045, "ABit AA8XE", ALC880_FIXUP_6ST_AUTOMUTE),
	SND_PCI_QUIRK(0x1558, 0x5401, "Clevo GPIO2", ALC880_FIXUP_GPIO2),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo", ALC880_FIXUP_EAPD_COEF),
	SND_PCI_QUIRK(0x1584, 0x9050, "Uniwill", ALC880_FIXUP_UNIWILL_DIG),
	SND_PCI_QUIRK(0x1584, 0x9054, "Uniwill", ALC880_FIXUP_F1734),
	SND_PCI_QUIRK(0x1584, 0x9070, "Uniwill", ALC880_FIXUP_UNIWILL),
	SND_PCI_QUIRK(0x1584, 0x9077, "Uniwill P53", ALC880_FIXUP_VOL_KNOB),
	SND_PCI_QUIRK(0x161f, 0x203d, "W810", ALC880_FIXUP_W810),
	SND_PCI_QUIRK(0x161f, 0x205d, "Medion Rim 2150", ALC880_FIXUP_MEDION_RIM),
	SND_PCI_QUIRK(0x1631, 0xe011, "PB 13201056", ALC880_FIXUP_6ST_AUTOMUTE),
	SND_PCI_QUIRK(0x1734, 0x107c, "FSC Amilo M1437", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x1094, "FSC Amilo M1451G", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x10ac, "FSC AMILO Xi 1526", ALC880_FIXUP_F1734),
	SND_PCI_QUIRK(0x1734, 0x10b0, "FSC Amilo Pi1556", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1854, 0x003b, "LG", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x005f, "LG P1 Express", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x0068, "LG w1", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x0077, "LG LW25", ALC880_FIXUP_LG_LW25),
	SND_PCI_QUIRK(0x19db, 0x4188, "TCL S700", ALC880_FIXUP_TCL_S700),

	/* Below is the copied entries from alc880_quirks.c.
	 * It's not quite sure whether BIOS sets the correct pin-config table
	 * on these machines, thus they are kept to be compatible with
	 * the old static quirks.  Once when it's confirmed to work without
	 * these overrides, it'd be better to remove.
	 */
	SND_PCI_QUIRK(0x1019, 0xa880, "ECS", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1019, 0xa884, "Acer APFV", ALC880_FIXUP_6ST),
	SND_PCI_QUIRK(0x1025, 0x0070, "ULI", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0077, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0078, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0087, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe309, "ULI", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe310, "ULI", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x1039, 0x1234, NULL, ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x104d, 0x81a0, "Sony", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x104d, 0x81d6, "Sony", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x107b, 0x3032, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x107b, 0x3033, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x107b, 0x4039, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x1297, 0xc790, "Shuttle ST20G5", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1458, 0xa102, "Gigabyte K8", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x1150, "MSI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1509, 0x925d, "FIC P4M", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1565, 0x8202, "Biostar", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x400d, "EPoX", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x4012, "EPox EP-5LDA", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x2668, 0x8086, NULL, ALC880_FIXUP_6ST_DIG), /* broken BIOS */
	SND_PCI_QUIRK(0x8086, 0x2668, NULL, ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xa100, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd400, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd401, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd402, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe224, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe305, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe308, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe400, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe401, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe402, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	/* default Intel */
	SND_PCI_QUIRK_VENDOR(0x8086, "Intel mobo", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0xa0a0, 0x0560, "AOpen i915GMm-HFS", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0xe803, 0x1019, NULL, ALC880_FIXUP_6ST_DIG),
	{}
};

static const struct hda_model_fixup alc880_fixup_models[] = {
	{.id = ALC880_FIXUP_3ST, .name = "3stack"},
	{.id = ALC880_FIXUP_3ST_DIG, .name = "3stack-digout"},
	{.id = ALC880_FIXUP_5ST, .name = "5stack"},
	{.id = ALC880_FIXUP_5ST_DIG, .name = "5stack-digout"},
	{.id = ALC880_FIXUP_6ST, .name = "6stack"},
	{.id = ALC880_FIXUP_6ST_DIG, .name = "6stack-digout"},
	{.id = ALC880_FIXUP_6ST_AUTOMUTE, .name = "6stack-automute"},
	{}
};


/*
 * OK, here we have finally the probe for ALC880
 */
static int alc880_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.need_dac_fix = 1;
	spec->gen.beep_nid = 0x01;

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc880_fixup_models, alc880_fixup_tbl,
		       alc880_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc880_parse_auto_config(codec);
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

static const struct hda_codec_ops alc880_codec_ops = {
	.probe = alc880_probe,
	.remove = snd_hda_gen_remove,
	.build_controls = alc_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = alc_init,
	.unsol_event = alc880_unsol_event,
	.resume = alc_resume,
	.suspend = alc_suspend,
	.check_power_status = snd_hda_gen_check_power_status,
	.stream_pm = snd_hda_gen_stream_pm,
};

/*
 * driver entries
 */
static const struct hda_device_id snd_hda_id_alc880[] = {
	HDA_CODEC_ID(0x10ec0880, "ALC880"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc880);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC880 HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");

static struct hda_codec_driver alc880_driver = {
	.id = snd_hda_id_alc880,
	.ops = &alc880_codec_ops,
};

module_hda_codec_driver(alc880_driver);

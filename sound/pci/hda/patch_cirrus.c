// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HD audio interface patch for Cirrus Logic CS420x chip
 *
 * Copyright (c) 2009 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <sound/tlv.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

/*
 */

#define CS42L42_HP_CH     (2U)
#define CS42L42_HS_MIC_CH (1U)

struct cs_spec {
	struct hda_gen_spec gen;

	unsigned int gpio_mask;
	unsigned int gpio_dir;
	unsigned int gpio_data;
	unsigned int gpio_eapd_hp; /* EAPD GPIO bit for headphones */
	unsigned int gpio_eapd_speaker; /* EAPD GPIO bit for speakers */

	/* CS421x */
	unsigned int spdif_detect:1;
	unsigned int spdif_present:1;
	unsigned int sense_b:1;
	hda_nid_t vendor_nid;

	/* for MBP SPDIF control */
	int (*spdif_sw_put)(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol);

	unsigned int cs42l42_hp_jack_in:1;
	unsigned int cs42l42_mic_jack_in:1;
	unsigned int cs42l42_volume_init:1;
	char cs42l42_hp_volume[CS42L42_HP_CH];
	char cs42l42_hs_mic_volume[CS42L42_HS_MIC_CH];

	struct mutex cs8409_i2c_mux;

	/* verb exec op override */
	int (*exec_verb)(struct hdac_device *dev, unsigned int cmd,
				 unsigned int flags, unsigned int *res);
};

/* available models with CS420x */
enum {
	CS420X_MBP53,
	CS420X_MBP55,
	CS420X_IMAC27,
	CS420X_GPIO_13,
	CS420X_GPIO_23,
	CS420X_MBP101,
	CS420X_MBP81,
	CS420X_MBA42,
	CS420X_AUTO,
	/* aliases */
	CS420X_IMAC27_122 = CS420X_GPIO_23,
	CS420X_APPLE = CS420X_GPIO_13,
};

/* CS421x boards */
enum {
	CS421X_CDB4210,
	CS421X_SENSE_B,
	CS421X_STUMPY,
};

/* Vendor-specific processing widget */
#define CS420X_VENDOR_NID	0x11
#define CS_DIG_OUT1_PIN_NID	0x10
#define CS_DIG_OUT2_PIN_NID	0x15
#define CS_DMIC1_PIN_NID	0x0e
#define CS_DMIC2_PIN_NID	0x12

/* coef indices */
#define IDX_SPDIF_STAT		0x0000
#define IDX_SPDIF_CTL		0x0001
#define IDX_ADC_CFG		0x0002
/* SZC bitmask, 4 modes below:
 * 0 = immediate,
 * 1 = digital immediate, analog zero-cross
 * 2 = digtail & analog soft-ramp
 * 3 = digital soft-ramp, analog zero-cross
 */
#define   CS_COEF_ADC_SZC_MASK		(3 << 0)
#define   CS_COEF_ADC_MIC_SZC_MODE	(3 << 0) /* SZC setup for mic */
#define   CS_COEF_ADC_LI_SZC_MODE	(3 << 0) /* SZC setup for line-in */
/* PGA mode: 0 = differential, 1 = signle-ended */
#define   CS_COEF_ADC_MIC_PGA_MODE	(1 << 5) /* PGA setup for mic */
#define   CS_COEF_ADC_LI_PGA_MODE	(1 << 6) /* PGA setup for line-in */
#define IDX_DAC_CFG		0x0003
/* SZC bitmask, 4 modes below:
 * 0 = Immediate
 * 1 = zero-cross
 * 2 = soft-ramp
 * 3 = soft-ramp on zero-cross
 */
#define   CS_COEF_DAC_HP_SZC_MODE	(3 << 0) /* nid 0x02 */
#define   CS_COEF_DAC_LO_SZC_MODE	(3 << 2) /* nid 0x03 */
#define   CS_COEF_DAC_SPK_SZC_MODE	(3 << 4) /* nid 0x04 */

#define IDX_BEEP_CFG		0x0004
/* 0x0008 - test reg key */
/* 0x0009 - 0x0014 -> 12 test regs */
/* 0x0015 - visibility reg */

/* Cirrus Logic CS4208 */
#define CS4208_VENDOR_NID	0x24

/*
 * Cirrus Logic CS4210
 *
 * 1 DAC => HP(sense) / Speakers,
 * 1 ADC <= LineIn(sense) / MicIn / DMicIn,
 * 1 SPDIF OUT => SPDIF Trasmitter(sense)
 */
#define CS4210_DAC_NID		0x02
#define CS4210_ADC_NID		0x03
#define CS4210_VENDOR_NID	0x0B
#define CS421X_DMIC_PIN_NID	0x09 /* Port E */
#define CS421X_SPDIF_PIN_NID	0x0A /* Port H */

#define CS421X_IDX_DEV_CFG	0x01
#define CS421X_IDX_ADC_CFG	0x02
#define CS421X_IDX_DAC_CFG	0x03
#define CS421X_IDX_SPK_CTL	0x04

/* Cirrus Logic CS4213 is like CS4210 but does not have SPDIF input/output */
#define CS4213_VENDOR_NID	0x09


static inline int cs_vendor_coef_get(struct hda_codec *codec, unsigned int idx)
{
	struct cs_spec *spec = codec->spec;

	snd_hda_codec_write(codec, spec->vendor_nid, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	return snd_hda_codec_read(codec, spec->vendor_nid, 0,
				  AC_VERB_GET_PROC_COEF, 0);
}

static inline void cs_vendor_coef_set(struct hda_codec *codec, unsigned int idx,
				      unsigned int coef)
{
	struct cs_spec *spec = codec->spec;

	snd_hda_codec_write(codec, spec->vendor_nid, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	snd_hda_codec_write(codec, spec->vendor_nid, 0,
			    AC_VERB_SET_PROC_COEF, coef);
}

/*
 * auto-mute and auto-mic switching
 * CS421x auto-output redirecting
 * HP/SPK/SPDIF
 */

static void cs_automute(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;

	/* mute HPs if spdif jack (SENSE_B) is present */
	spec->gen.master_mute = !!(spec->spdif_present && spec->sense_b);

	snd_hda_gen_update_outputs(codec);

	if (spec->gpio_eapd_hp || spec->gpio_eapd_speaker) {
		if (spec->gen.automute_speaker)
			spec->gpio_data = spec->gen.hp_jack_present ?
				spec->gpio_eapd_hp : spec->gpio_eapd_speaker;
		else
			spec->gpio_data =
				spec->gpio_eapd_hp | spec->gpio_eapd_speaker;
		snd_hda_codec_write(codec, 0x01, 0,
				    AC_VERB_SET_GPIO_DATA, spec->gpio_data);
	}
}

static bool is_active_pin(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int val;

	val = snd_hda_codec_get_pincfg(codec, nid);
	return (get_defcfg_connect(val) != AC_JACK_PORT_NONE);
}

static void init_input_coef(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	unsigned int coef;

	/* CS420x has multiple ADC, CS421x has single ADC */
	if (spec->vendor_nid == CS420X_VENDOR_NID) {
		coef = cs_vendor_coef_get(codec, IDX_BEEP_CFG);
		if (is_active_pin(codec, CS_DMIC2_PIN_NID))
			coef |= 1 << 4; /* DMIC2 2 chan on, GPIO1 off */
		if (is_active_pin(codec, CS_DMIC1_PIN_NID))
			coef |= 1 << 3; /* DMIC1 2 chan on, GPIO0 off
					 * No effect if SPDIF_OUT2 is
					 * selected in IDX_SPDIF_CTL.
					 */

		cs_vendor_coef_set(codec, IDX_BEEP_CFG, coef);
	}
}

static const struct hda_verb cs_coef_init_verbs[] = {
	{0x11, AC_VERB_SET_PROC_STATE, 1},
	{0x11, AC_VERB_SET_COEF_INDEX, IDX_DAC_CFG},
	{0x11, AC_VERB_SET_PROC_COEF,
	 (0x002a /* DAC1/2/3 SZCMode Soft Ramp */
	  | 0x0040 /* Mute DACs on FIFO error */
	  | 0x1000 /* Enable DACs High Pass Filter */
	  | 0x0400 /* Disable Coefficient Auto increment */
	  )},
	/* ADC1/2 - Digital and Analog Soft Ramp */
	{0x11, AC_VERB_SET_COEF_INDEX, IDX_ADC_CFG},
	{0x11, AC_VERB_SET_PROC_COEF, 0x000a},
	/* Beep */
	{0x11, AC_VERB_SET_COEF_INDEX, IDX_BEEP_CFG},
	{0x11, AC_VERB_SET_PROC_COEF, 0x0007}, /* Enable Beep thru DAC1/2/3 */

	{} /* terminator */
};

static const struct hda_verb cs4208_coef_init_verbs[] = {
	{0x01, AC_VERB_SET_POWER_STATE, 0x00}, /* AFG: D0 */
	{0x24, AC_VERB_SET_PROC_STATE, 0x01},  /* VPW: processing on */
	{0x24, AC_VERB_SET_COEF_INDEX, 0x0033},
	{0x24, AC_VERB_SET_PROC_COEF, 0x0001}, /* A1 ICS */
	{0x24, AC_VERB_SET_COEF_INDEX, 0x0034},
	{0x24, AC_VERB_SET_PROC_COEF, 0x1C01}, /* A1 Enable, A Thresh = 300mV */
	{} /* terminator */
};

/* Errata: CS4207 rev C0/C1/C2 Silicon
 *
 * http://www.cirrus.com/en/pubs/errata/ER880C3.pdf
 *
 * 6. At high temperature (TA > +85°C), the digital supply current (IVD)
 * may be excessive (up to an additional 200 μA), which is most easily
 * observed while the part is being held in reset (RESET# active low).
 *
 * Root Cause: At initial powerup of the device, the logic that drives
 * the clock and write enable to the S/PDIF SRC RAMs is not properly
 * initialized.
 * Certain random patterns will cause a steady leakage current in those
 * RAM cells. The issue will resolve once the SRCs are used (turned on).
 *
 * Workaround: The following verb sequence briefly turns on the S/PDIF SRC
 * blocks, which will alleviate the issue.
 */

static const struct hda_verb cs_errata_init_verbs[] = {
	{0x01, AC_VERB_SET_POWER_STATE, 0x00}, /* AFG: D0 */
	{0x11, AC_VERB_SET_PROC_STATE, 0x01},  /* VPW: processing on */

	{0x11, AC_VERB_SET_COEF_INDEX, 0x0008},
	{0x11, AC_VERB_SET_PROC_COEF, 0x9999},
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0017},
	{0x11, AC_VERB_SET_PROC_COEF, 0xa412},
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0001},
	{0x11, AC_VERB_SET_PROC_COEF, 0x0009},

	{0x07, AC_VERB_SET_POWER_STATE, 0x00}, /* S/PDIF Rx: D0 */
	{0x08, AC_VERB_SET_POWER_STATE, 0x00}, /* S/PDIF Tx: D0 */

	{0x11, AC_VERB_SET_COEF_INDEX, 0x0017},
	{0x11, AC_VERB_SET_PROC_COEF, 0x2412},
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0008},
	{0x11, AC_VERB_SET_PROC_COEF, 0x0000},
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0001},
	{0x11, AC_VERB_SET_PROC_COEF, 0x0008},
	{0x11, AC_VERB_SET_PROC_STATE, 0x00},
	{} /* terminator */
};

/* SPDIF setup */
static void init_digital_coef(struct hda_codec *codec)
{
	unsigned int coef;

	coef = 0x0002; /* SRC_MUTE soft-mute on SPDIF (if no lock) */
	coef |= 0x0008; /* Replace with mute on error */
	if (is_active_pin(codec, CS_DIG_OUT2_PIN_NID))
		coef |= 0x4000; /* RX to TX1 or TX2 Loopthru / SPDIF2
				 * SPDIF_OUT2 is shared with GPIO1 and
				 * DMIC_SDA2.
				 */
	cs_vendor_coef_set(codec, IDX_SPDIF_CTL, coef);
}

static int cs_init(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;

	if (spec->vendor_nid == CS420X_VENDOR_NID) {
		/* init_verb sequence for C0/C1/C2 errata*/
		snd_hda_sequence_write(codec, cs_errata_init_verbs);
		snd_hda_sequence_write(codec, cs_coef_init_verbs);
	} else if (spec->vendor_nid == CS4208_VENDOR_NID) {
		snd_hda_sequence_write(codec, cs4208_coef_init_verbs);
	}

	snd_hda_gen_init(codec);

	if (spec->gpio_mask) {
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_MASK,
				    spec->gpio_mask);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DIRECTION,
				    spec->gpio_dir);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
				    spec->gpio_data);
	}

	if (spec->vendor_nid == CS420X_VENDOR_NID) {
		init_input_coef(codec);
		init_digital_coef(codec);
	}

	return 0;
}

static int cs_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_BUILD);
	return 0;
}

#define cs_free		snd_hda_gen_free

static const struct hda_codec_ops cs_patch_ops = {
	.build_controls = cs_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cs_init,
	.free = cs_free,
	.unsol_event = snd_hda_jack_unsol_event,
};

static int cs_parse_auto_config(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	int err;
	int i;

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL, 0);
	if (err < 0)
		return err;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		return err;

	/* keep the ADCs powered up when it's dynamically switchable */
	if (spec->gen.dyn_adc_switch) {
		unsigned int done = 0;

		for (i = 0; i < spec->gen.input_mux.num_items; i++) {
			int idx = spec->gen.dyn_adc_idx[i];

			if (done & (1 << idx))
				continue;
			snd_hda_gen_fix_pin_power(codec,
						  spec->gen.adc_nids[idx]);
			done |= 1 << idx;
		}
	}

	return 0;
}

static const struct hda_model_fixup cs420x_models[] = {
	{ .id = CS420X_MBP53, .name = "mbp53" },
	{ .id = CS420X_MBP55, .name = "mbp55" },
	{ .id = CS420X_IMAC27, .name = "imac27" },
	{ .id = CS420X_IMAC27_122, .name = "imac27_122" },
	{ .id = CS420X_APPLE, .name = "apple" },
	{ .id = CS420X_MBP101, .name = "mbp101" },
	{ .id = CS420X_MBP81, .name = "mbp81" },
	{ .id = CS420X_MBA42, .name = "mba42" },
	{}
};

static const struct snd_pci_quirk cs420x_fixup_tbl[] = {
	SND_PCI_QUIRK(0x10de, 0x0ac0, "MacBookPro 5,3", CS420X_MBP53),
	SND_PCI_QUIRK(0x10de, 0x0d94, "MacBookAir 3,1(2)", CS420X_MBP55),
	SND_PCI_QUIRK(0x10de, 0xcb79, "MacBookPro 5,5", CS420X_MBP55),
	SND_PCI_QUIRK(0x10de, 0xcb89, "MacBookPro 7,1", CS420X_MBP55),
	/* this conflicts with too many other models */
	/*SND_PCI_QUIRK(0x8086, 0x7270, "IMac 27 Inch", CS420X_IMAC27),*/

	/* codec SSID */
	SND_PCI_QUIRK(0x106b, 0x0600, "iMac 14,1", CS420X_IMAC27_122),
	SND_PCI_QUIRK(0x106b, 0x1c00, "MacBookPro 8,1", CS420X_MBP81),
	SND_PCI_QUIRK(0x106b, 0x2000, "iMac 12,2", CS420X_IMAC27_122),
	SND_PCI_QUIRK(0x106b, 0x2800, "MacBookPro 10,1", CS420X_MBP101),
	SND_PCI_QUIRK(0x106b, 0x5600, "MacBookAir 5,2", CS420X_MBP81),
	SND_PCI_QUIRK(0x106b, 0x5b00, "MacBookAir 4,2", CS420X_MBA42),
	SND_PCI_QUIRK_VENDOR(0x106b, "Apple", CS420X_APPLE),
	{} /* terminator */
};

static const struct hda_pintbl mbp53_pincfgs[] = {
	{ 0x09, 0x012b4050 },
	{ 0x0a, 0x90100141 },
	{ 0x0b, 0x90100140 },
	{ 0x0c, 0x018b3020 },
	{ 0x0d, 0x90a00110 },
	{ 0x0e, 0x400000f0 },
	{ 0x0f, 0x01cbe030 },
	{ 0x10, 0x014be060 },
	{ 0x12, 0x400000f0 },
	{ 0x15, 0x400000f0 },
	{} /* terminator */
};

static const struct hda_pintbl mbp55_pincfgs[] = {
	{ 0x09, 0x012b4030 },
	{ 0x0a, 0x90100121 },
	{ 0x0b, 0x90100120 },
	{ 0x0c, 0x400000f0 },
	{ 0x0d, 0x90a00110 },
	{ 0x0e, 0x400000f0 },
	{ 0x0f, 0x400000f0 },
	{ 0x10, 0x014be040 },
	{ 0x12, 0x400000f0 },
	{ 0x15, 0x400000f0 },
	{} /* terminator */
};

static const struct hda_pintbl imac27_pincfgs[] = {
	{ 0x09, 0x012b4050 },
	{ 0x0a, 0x90100140 },
	{ 0x0b, 0x90100142 },
	{ 0x0c, 0x018b3020 },
	{ 0x0d, 0x90a00110 },
	{ 0x0e, 0x400000f0 },
	{ 0x0f, 0x01cbe030 },
	{ 0x10, 0x014be060 },
	{ 0x12, 0x01ab9070 },
	{ 0x15, 0x400000f0 },
	{} /* terminator */
};

static const struct hda_pintbl mbp101_pincfgs[] = {
	{ 0x0d, 0x40ab90f0 },
	{ 0x0e, 0x90a600f0 },
	{ 0x12, 0x50a600f0 },
	{} /* terminator */
};

static const struct hda_pintbl mba42_pincfgs[] = {
	{ 0x09, 0x012b4030 }, /* HP */
	{ 0x0a, 0x400000f0 },
	{ 0x0b, 0x90100120 }, /* speaker */
	{ 0x0c, 0x400000f0 },
	{ 0x0d, 0x90a00110 }, /* mic */
	{ 0x0e, 0x400000f0 },
	{ 0x0f, 0x400000f0 },
	{ 0x10, 0x400000f0 },
	{ 0x12, 0x400000f0 },
	{ 0x15, 0x400000f0 },
	{} /* terminator */
};

static const struct hda_pintbl mba6_pincfgs[] = {
	{ 0x10, 0x032120f0 }, /* HP */
	{ 0x11, 0x500000f0 },
	{ 0x12, 0x90100010 }, /* Speaker */
	{ 0x13, 0x500000f0 },
	{ 0x14, 0x500000f0 },
	{ 0x15, 0x770000f0 },
	{ 0x16, 0x770000f0 },
	{ 0x17, 0x430000f0 },
	{ 0x18, 0x43ab9030 }, /* Mic */
	{ 0x19, 0x770000f0 },
	{ 0x1a, 0x770000f0 },
	{ 0x1b, 0x770000f0 },
	{ 0x1c, 0x90a00090 },
	{ 0x1d, 0x500000f0 },
	{ 0x1e, 0x500000f0 },
	{ 0x1f, 0x500000f0 },
	{ 0x20, 0x500000f0 },
	{ 0x21, 0x430000f0 },
	{ 0x22, 0x430000f0 },
	{} /* terminator */
};

static void cs420x_fixup_gpio_13(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct cs_spec *spec = codec->spec;

		spec->gpio_eapd_hp = 2; /* GPIO1 = headphones */
		spec->gpio_eapd_speaker = 8; /* GPIO3 = speakers */
		spec->gpio_mask = spec->gpio_dir =
			spec->gpio_eapd_hp | spec->gpio_eapd_speaker;
	}
}

static void cs420x_fixup_gpio_23(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct cs_spec *spec = codec->spec;

		spec->gpio_eapd_hp = 4; /* GPIO2 = headphones */
		spec->gpio_eapd_speaker = 8; /* GPIO3 = speakers */
		spec->gpio_mask = spec->gpio_dir =
			spec->gpio_eapd_hp | spec->gpio_eapd_speaker;
	}
}

static const struct hda_fixup cs420x_fixups[] = {
	[CS420X_MBP53] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = mbp53_pincfgs,
		.chained = true,
		.chain_id = CS420X_APPLE,
	},
	[CS420X_MBP55] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = mbp55_pincfgs,
		.chained = true,
		.chain_id = CS420X_GPIO_13,
	},
	[CS420X_IMAC27] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = imac27_pincfgs,
		.chained = true,
		.chain_id = CS420X_GPIO_13,
	},
	[CS420X_GPIO_13] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs420x_fixup_gpio_13,
	},
	[CS420X_GPIO_23] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs420x_fixup_gpio_23,
	},
	[CS420X_MBP101] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = mbp101_pincfgs,
		.chained = true,
		.chain_id = CS420X_GPIO_13,
	},
	[CS420X_MBP81] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* internal mic ADC2: right only, single ended */
			{0x11, AC_VERB_SET_COEF_INDEX, IDX_ADC_CFG},
			{0x11, AC_VERB_SET_PROC_COEF, 0x102a},
			{}
		},
		.chained = true,
		.chain_id = CS420X_GPIO_13,
	},
	[CS420X_MBA42] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = mba42_pincfgs,
		.chained = true,
		.chain_id = CS420X_GPIO_13,
	},
};

static struct cs_spec *cs_alloc_spec(struct hda_codec *codec, int vendor_nid)
{
	struct cs_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;
	codec->spec = spec;
	spec->vendor_nid = vendor_nid;
	codec->power_save_node = 1;
	snd_hda_gen_spec_init(&spec->gen);

	return spec;
}

static int patch_cs420x(struct hda_codec *codec)
{
	struct cs_spec *spec;
	int err;

	spec = cs_alloc_spec(codec, CS420X_VENDOR_NID);
	if (!spec)
		return -ENOMEM;

	codec->patch_ops = cs_patch_ops;
	spec->gen.automute_hook = cs_automute;
	codec->single_adc_amp = 1;

	snd_hda_pick_fixup(codec, cs420x_models, cs420x_fixup_tbl,
			   cs420x_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = cs_parse_auto_config(codec);
	if (err < 0)
		goto error;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	cs_free(codec);
	return err;
}

/*
 * CS4208 support:
 * Its layout is no longer compatible with CS4206/CS4207
 */
enum {
	CS4208_MAC_AUTO,
	CS4208_MBA6,
	CS4208_MBP11,
	CS4208_MACMINI,
	CS4208_GPIO0,
};

static const struct hda_model_fixup cs4208_models[] = {
	{ .id = CS4208_GPIO0, .name = "gpio0" },
	{ .id = CS4208_MBA6, .name = "mba6" },
	{ .id = CS4208_MBP11, .name = "mbp11" },
	{ .id = CS4208_MACMINI, .name = "macmini" },
	{}
};

static const struct snd_pci_quirk cs4208_fixup_tbl[] = {
	SND_PCI_QUIRK_VENDOR(0x106b, "Apple", CS4208_MAC_AUTO),
	{} /* terminator */
};

/* codec SSID matching */
static const struct snd_pci_quirk cs4208_mac_fixup_tbl[] = {
	SND_PCI_QUIRK(0x106b, 0x5e00, "MacBookPro 11,2", CS4208_MBP11),
	SND_PCI_QUIRK(0x106b, 0x6c00, "MacMini 7,1", CS4208_MACMINI),
	SND_PCI_QUIRK(0x106b, 0x7100, "MacBookAir 6,1", CS4208_MBA6),
	SND_PCI_QUIRK(0x106b, 0x7200, "MacBookAir 6,2", CS4208_MBA6),
	SND_PCI_QUIRK(0x106b, 0x7b00, "MacBookPro 12,1", CS4208_MBP11),
	{} /* terminator */
};

static void cs4208_fixup_gpio0(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct cs_spec *spec = codec->spec;

		spec->gpio_eapd_hp = 0;
		spec->gpio_eapd_speaker = 1;
		spec->gpio_mask = spec->gpio_dir =
			spec->gpio_eapd_hp | spec->gpio_eapd_speaker;
	}
}

static const struct hda_fixup cs4208_fixups[];

/* remap the fixup from codec SSID and apply it */
static void cs4208_fixup_mac(struct hda_codec *codec,
			     const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	codec->fixup_id = HDA_FIXUP_ID_NOT_SET;
	snd_hda_pick_fixup(codec, NULL, cs4208_mac_fixup_tbl, cs4208_fixups);
	if (codec->fixup_id == HDA_FIXUP_ID_NOT_SET)
		codec->fixup_id = CS4208_GPIO0; /* default fixup */
	snd_hda_apply_fixup(codec, action);
}

/* MacMini 7,1 has the inverted jack detection */
static void cs4208_fixup_macmini(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	static const struct hda_pintbl pincfgs[] = {
		{ 0x18, 0x00ab9150 }, /* mic (audio-in) jack: disable detect */
		{ 0x21, 0x004be140 }, /* SPDIF: disable detect */
		{ }
	};

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* HP pin (0x10) has an inverted detection */
		codec->inv_jack_detect = 1;
		/* disable the bogus Mic and SPDIF jack detections */
		snd_hda_apply_pincfgs(codec, pincfgs);
	}
}

static int cs4208_spdif_sw_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;
	hda_nid_t pin = spec->gen.autocfg.dig_out_pins[0];
	int pinctl = ucontrol->value.integer.value[0] ? PIN_OUT : 0;

	snd_hda_set_pin_ctl_cache(codec, pin, pinctl);
	return spec->spdif_sw_put(kcontrol, ucontrol);
}

/* hook the SPDIF switch */
static void cs4208_fixup_spdif_switch(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_BUILD) {
		struct cs_spec *spec = codec->spec;
		struct snd_kcontrol *kctl;

		if (!spec->gen.autocfg.dig_out_pins[0])
			return;
		kctl = snd_hda_find_mixer_ctl(codec, "IEC958 Playback Switch");
		if (!kctl)
			return;
		spec->spdif_sw_put = kctl->put;
		kctl->put = cs4208_spdif_sw_put;
	}
}

static const struct hda_fixup cs4208_fixups[] = {
	[CS4208_MBA6] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = mba6_pincfgs,
		.chained = true,
		.chain_id = CS4208_GPIO0,
	},
	[CS4208_MBP11] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs4208_fixup_spdif_switch,
		.chained = true,
		.chain_id = CS4208_GPIO0,
	},
	[CS4208_MACMINI] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs4208_fixup_macmini,
		.chained = true,
		.chain_id = CS4208_GPIO0,
	},
	[CS4208_GPIO0] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs4208_fixup_gpio0,
	},
	[CS4208_MAC_AUTO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs4208_fixup_mac,
	},
};

/* correct the 0dB offset of input pins */
static void cs4208_fix_amp_caps(struct hda_codec *codec, hda_nid_t adc)
{
	unsigned int caps;

	caps = query_amp_caps(codec, adc, HDA_INPUT);
	caps &= ~(AC_AMPCAP_OFFSET);
	caps |= 0x02;
	snd_hda_override_amp_caps(codec, adc, HDA_INPUT, caps);
}

static int patch_cs4208(struct hda_codec *codec)
{
	struct cs_spec *spec;
	int err;

	spec = cs_alloc_spec(codec, CS4208_VENDOR_NID);
	if (!spec)
		return -ENOMEM;

	codec->patch_ops = cs_patch_ops;
	spec->gen.automute_hook = cs_automute;
	/* exclude NID 0x10 (HP) from output volumes due to different steps */
	spec->gen.out_vol_mask = 1ULL << 0x10;

	snd_hda_pick_fixup(codec, cs4208_models, cs4208_fixup_tbl,
			   cs4208_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	snd_hda_override_wcaps(codec, 0x18,
			       get_wcaps(codec, 0x18) | AC_WCAP_STEREO);
	cs4208_fix_amp_caps(codec, 0x18);
	cs4208_fix_amp_caps(codec, 0x1b);
	cs4208_fix_amp_caps(codec, 0x1c);

	err = cs_parse_auto_config(codec);
	if (err < 0)
		goto error;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	cs_free(codec);
	return err;
}

/*
 * Cirrus Logic CS4210
 *
 * 1 DAC => HP(sense) / Speakers,
 * 1 ADC <= LineIn(sense) / MicIn / DMicIn,
 * 1 SPDIF OUT => SPDIF Trasmitter(sense)
 */

/* CS4210 board names */
static const struct hda_model_fixup cs421x_models[] = {
	{ .id = CS421X_CDB4210, .name = "cdb4210" },
	{ .id = CS421X_STUMPY, .name = "stumpy" },
	{}
};

static const struct snd_pci_quirk cs421x_fixup_tbl[] = {
	/* Test Intel board + CDB2410  */
	SND_PCI_QUIRK(0x8086, 0x5001, "DP45SG/CDB4210", CS421X_CDB4210),
	{} /* terminator */
};

/* CS4210 board pinconfigs */
/* Default CS4210 (CDB4210)*/
static const struct hda_pintbl cdb4210_pincfgs[] = {
	{ 0x05, 0x0321401f },
	{ 0x06, 0x90170010 },
	{ 0x07, 0x03813031 },
	{ 0x08, 0xb7a70037 },
	{ 0x09, 0xb7a6003e },
	{ 0x0a, 0x034510f0 },
	{} /* terminator */
};

/* Stumpy ChromeBox */
static const struct hda_pintbl stumpy_pincfgs[] = {
	{ 0x05, 0x022120f0 },
	{ 0x06, 0x901700f0 },
	{ 0x07, 0x02a120f0 },
	{ 0x08, 0x77a70037 },
	{ 0x09, 0x77a6003e },
	{ 0x0a, 0x434510f0 },
	{} /* terminator */
};

/* Setup GPIO/SENSE for each board (if used) */
static void cs421x_fixup_sense_b(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct cs_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->sense_b = 1;
}

static const struct hda_fixup cs421x_fixups[] = {
	[CS421X_CDB4210] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cdb4210_pincfgs,
		.chained = true,
		.chain_id = CS421X_SENSE_B,
	},
	[CS421X_SENSE_B] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs421x_fixup_sense_b,
	},
	[CS421X_STUMPY] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = stumpy_pincfgs,
	},
};

static const struct hda_verb cs421x_coef_init_verbs[] = {
	{0x0B, AC_VERB_SET_PROC_STATE, 1},
	{0x0B, AC_VERB_SET_COEF_INDEX, CS421X_IDX_DEV_CFG},
	/*
	 *  Disable Coefficient Index Auto-Increment(DAI)=1,
	 *  PDREF=0
	 */
	{0x0B, AC_VERB_SET_PROC_COEF, 0x0001 },

	{0x0B, AC_VERB_SET_COEF_INDEX, CS421X_IDX_ADC_CFG},
	/* ADC SZCMode = Digital Soft Ramp */
	{0x0B, AC_VERB_SET_PROC_COEF, 0x0002 },

	{0x0B, AC_VERB_SET_COEF_INDEX, CS421X_IDX_DAC_CFG},
	{0x0B, AC_VERB_SET_PROC_COEF,
	 (0x0002 /* DAC SZCMode = Digital Soft Ramp */
	  | 0x0004 /* Mute DAC on FIFO error */
	  | 0x0008 /* Enable DAC High Pass Filter */
	  )},
	{} /* terminator */
};

/* Errata: CS4210 rev A1 Silicon
 *
 * http://www.cirrus.com/en/pubs/errata/
 *
 * Description:
 * 1. Performance degredation is present in the ADC.
 * 2. Speaker output is not completely muted upon HP detect.
 * 3. Noise is present when clipping occurs on the amplified
 *    speaker outputs.
 *
 * Workaround:
 * The following verb sequence written to the registers during
 * initialization will correct the issues listed above.
 */

static const struct hda_verb cs421x_coef_init_verbs_A1_silicon_fixes[] = {
	{0x0B, AC_VERB_SET_PROC_STATE, 0x01},  /* VPW: processing on */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x0006},
	{0x0B, AC_VERB_SET_PROC_COEF, 0x9999}, /* Test mode: on */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x000A},
	{0x0B, AC_VERB_SET_PROC_COEF, 0x14CB}, /* Chop double */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x0011},
	{0x0B, AC_VERB_SET_PROC_COEF, 0xA2D0}, /* Increase ADC current */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x001A},
	{0x0B, AC_VERB_SET_PROC_COEF, 0x02A9}, /* Mute speaker */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x001B},
	{0x0B, AC_VERB_SET_PROC_COEF, 0X1006}, /* Remove noise */

	{} /* terminator */
};

/* Speaker Amp Gain is controlled by the vendor widget's coef 4 */
static const DECLARE_TLV_DB_SCALE(cs421x_speaker_boost_db_scale, 900, 300, 0);

static int cs421x_boost_vol_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 3;
	return 0;
}

static int cs421x_boost_vol_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] =
		cs_vendor_coef_get(codec, CS421X_IDX_SPK_CTL) & 0x0003;
	return 0;
}

static int cs421x_boost_vol_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	unsigned int vol = ucontrol->value.integer.value[0];
	unsigned int coef =
		cs_vendor_coef_get(codec, CS421X_IDX_SPK_CTL);
	unsigned int original_coef = coef;

	coef &= ~0x0003;
	coef |= (vol & 0x0003);
	if (original_coef != coef) {
		cs_vendor_coef_set(codec, CS421X_IDX_SPK_CTL, coef);
		return 1;
	}

	return 0;
}

static const struct snd_kcontrol_new cs421x_speaker_boost_ctl = {

	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name = "Speaker Boost Playback Volume",
	.info = cs421x_boost_vol_info,
	.get = cs421x_boost_vol_get,
	.put = cs421x_boost_vol_put,
	.tlv = { .p = cs421x_speaker_boost_db_scale },
};

static void cs4210_pinmux_init(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	unsigned int def_conf, coef;

	/* GPIO, DMIC_SCL, DMIC_SDA and SENSE_B are multiplexed */
	coef = cs_vendor_coef_get(codec, CS421X_IDX_DEV_CFG);

	if (spec->gpio_mask)
		coef |= 0x0008; /* B1,B2 are GPIOs */
	else
		coef &= ~0x0008;

	if (spec->sense_b)
		coef |= 0x0010; /* B2 is SENSE_B, not inverted  */
	else
		coef &= ~0x0010;

	cs_vendor_coef_set(codec, CS421X_IDX_DEV_CFG, coef);

	if ((spec->gpio_mask || spec->sense_b) &&
	    is_active_pin(codec, CS421X_DMIC_PIN_NID)) {

		/*
		 *  GPIO or SENSE_B forced - disconnect the DMIC pin.
		 */
		def_conf = snd_hda_codec_get_pincfg(codec, CS421X_DMIC_PIN_NID);
		def_conf &= ~AC_DEFCFG_PORT_CONN;
		def_conf |= (AC_JACK_PORT_NONE << AC_DEFCFG_PORT_CONN_SHIFT);
		snd_hda_codec_set_pincfg(codec, CS421X_DMIC_PIN_NID, def_conf);
	}
}

static void cs4210_spdif_automute(struct hda_codec *codec,
				  struct hda_jack_callback *tbl)
{
	struct cs_spec *spec = codec->spec;
	bool spdif_present = false;
	hda_nid_t spdif_pin = spec->gen.autocfg.dig_out_pins[0];

	/* detect on spdif is specific to CS4210 */
	if (!spec->spdif_detect ||
	    spec->vendor_nid != CS4210_VENDOR_NID)
		return;

	spdif_present = snd_hda_jack_detect(codec, spdif_pin);
	if (spdif_present == spec->spdif_present)
		return;

	spec->spdif_present = spdif_present;
	/* SPDIF TX on/off */
	snd_hda_set_pin_ctl(codec, spdif_pin, spdif_present ? PIN_OUT : 0);

	cs_automute(codec);
}

static void parse_cs421x_digital(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	int i;

	for (i = 0; i < cfg->dig_outs; i++) {
		hda_nid_t nid = cfg->dig_out_pins[i];

		if (get_wcaps(codec, nid) & AC_WCAP_UNSOL_CAP) {
			spec->spdif_detect = 1;
			snd_hda_jack_detect_enable_callback(codec, nid,
							    cs4210_spdif_automute);
		}
	}
}

static int cs421x_init(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;

	if (spec->vendor_nid == CS4210_VENDOR_NID) {
		snd_hda_sequence_write(codec, cs421x_coef_init_verbs);
		snd_hda_sequence_write(codec, cs421x_coef_init_verbs_A1_silicon_fixes);
		cs4210_pinmux_init(codec);
	}

	snd_hda_gen_init(codec);

	if (spec->gpio_mask) {
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_MASK,
				    spec->gpio_mask);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DIRECTION,
				    spec->gpio_dir);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
				    spec->gpio_data);
	}

	init_input_coef(codec);

	cs4210_spdif_automute(codec, NULL);

	return 0;
}

static void fix_volume_caps(struct hda_codec *codec, hda_nid_t dac)
{
	unsigned int caps;

	/* set the upper-limit for mixer amp to 0dB */
	caps = query_amp_caps(codec, dac, HDA_OUTPUT);
	caps &= ~(0x7f << AC_AMPCAP_NUM_STEPS_SHIFT);
	caps |= ((caps >> AC_AMPCAP_OFFSET_SHIFT) & 0x7f)
		<< AC_AMPCAP_NUM_STEPS_SHIFT;
	snd_hda_override_amp_caps(codec, dac, HDA_OUTPUT, caps);
}

static int cs421x_parse_auto_config(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	hda_nid_t dac = CS4210_DAC_NID;
	int err;

	fix_volume_caps(codec, dac);

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL, 0);
	if (err < 0)
		return err;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		return err;

	parse_cs421x_digital(codec);

	if (spec->gen.autocfg.speaker_outs &&
	    spec->vendor_nid == CS4210_VENDOR_NID) {
		if (!snd_hda_gen_add_kctl(&spec->gen, NULL,
					  &cs421x_speaker_boost_ctl))
			return -ENOMEM;
	}

	return 0;
}

#ifdef CONFIG_PM
/*
 *	Manage PDREF, when transitioning to D3hot
 *	(DAC,ADC) -> D3, PDREF=1, AFG->D3
 */
static int cs421x_suspend(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	unsigned int coef;

	snd_hda_shutup_pins(codec);

	snd_hda_codec_write(codec, CS4210_DAC_NID, 0,
			    AC_VERB_SET_POWER_STATE,  AC_PWRST_D3);
	snd_hda_codec_write(codec, CS4210_ADC_NID, 0,
			    AC_VERB_SET_POWER_STATE,  AC_PWRST_D3);

	if (spec->vendor_nid == CS4210_VENDOR_NID) {
		coef = cs_vendor_coef_get(codec, CS421X_IDX_DEV_CFG);
		coef |= 0x0004; /* PDREF */
		cs_vendor_coef_set(codec, CS421X_IDX_DEV_CFG, coef);
	}

	return 0;
}
#endif

static const struct hda_codec_ops cs421x_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cs421x_init,
	.free = cs_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = cs421x_suspend,
#endif
};

static int patch_cs4210(struct hda_codec *codec)
{
	struct cs_spec *spec;
	int err;

	spec = cs_alloc_spec(codec, CS4210_VENDOR_NID);
	if (!spec)
		return -ENOMEM;

	codec->patch_ops = cs421x_patch_ops;
	spec->gen.automute_hook = cs_automute;

	snd_hda_pick_fixup(codec, cs421x_models, cs421x_fixup_tbl,
			   cs421x_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/*
	 *  Update the GPIO/DMIC/SENSE_B pinmux before the configuration
	 *   is auto-parsed. If GPIO or SENSE_B is forced, DMIC input
	 *   is disabled.
	 */
	cs4210_pinmux_init(codec);

	err = cs421x_parse_auto_config(codec);
	if (err < 0)
		goto error;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	cs_free(codec);
	return err;
}

static int patch_cs4213(struct hda_codec *codec)
{
	struct cs_spec *spec;
	int err;

	spec = cs_alloc_spec(codec, CS4213_VENDOR_NID);
	if (!spec)
		return -ENOMEM;

	codec->patch_ops = cs421x_patch_ops;

	err = cs421x_parse_auto_config(codec);
	if (err < 0)
		goto error;

	return 0;

 error:
	cs_free(codec);
	return err;
}

/* Cirrus Logic CS8409 HDA bridge with
 * companion codec CS42L42
 */
#define CS8409_VENDOR_NID 0x47

#define CS8409_CS42L42_HP_PIN_NID	0x24
#define CS8409_CS42L42_SPK_PIN_NID	0x2c
#define CS8409_CS42L42_AMIC_PIN_NID	0x34
#define CS8409_CS42L42_DMIC_PIN_NID	0x44
#define CS8409_CS42L42_DMIC_ADC_PIN_NID	0x22

#define CS42L42_HSDET_AUTO_DONE	0x02
#define CS42L42_HSTYPE_MASK		0x03

#define CS42L42_JACK_INSERTED	0x0C
#define CS42L42_JACK_REMOVED	0x00

#define GPIO3_INT (1 << 3)
#define GPIO4_INT (1 << 4)
#define GPIO5_INT (1 << 5)

#define CS42L42_I2C_ADDR	(0x48 << 1)

#define CIR_I2C_ADDR	0x0059
#define CIR_I2C_DATA	0x005A
#define CIR_I2C_CTRL	0x005B
#define CIR_I2C_STATUS	0x005C
#define CIR_I2C_QWRITE	0x005D
#define CIR_I2C_QREAD	0x005E

#define CS8409_CS42L42_HP_VOL_REAL_MIN   (-63)
#define CS8409_CS42L42_HP_VOL_REAL_MAX   (0)
#define CS8409_CS42L42_AMIC_VOL_REAL_MIN (-97)
#define CS8409_CS42L42_AMIC_VOL_REAL_MAX (12)
#define CS8409_CS42L42_REG_HS_VOLUME_CHA (0x2301)
#define CS8409_CS42L42_REG_HS_VOLUME_CHB (0x2303)
#define CS8409_CS42L42_REG_AMIC_VOLUME   (0x1D03)

struct cs8409_i2c_param {
	unsigned int addr;
	unsigned int reg;
};

struct cs8409_cir_param {
	unsigned int nid;
	unsigned int cir;
	unsigned int coeff;
};

enum {
	CS8409_BULLSEYE,
	CS8409_WARLOCK,
	CS8409_CYBORG,
	CS8409_FIXUPS,
};

static void cs8409_cs42l42_fixups(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action);
static int cs8409_cs42l42_exec_verb(struct hdac_device *dev,
		unsigned int cmd, unsigned int flags, unsigned int *res);

/* Dell Inspiron models with cs8409/cs42l42 */
static const struct hda_model_fixup cs8409_models[] = {
	{ .id = CS8409_BULLSEYE, .name = "bullseye" },
	{ .id = CS8409_WARLOCK, .name = "warlock" },
	{ .id = CS8409_CYBORG, .name = "cyborg" },
	{}
};

/* Dell Inspiron platforms
 * with cs8409 bridge and cs42l42 codec
 */
static const struct snd_pci_quirk cs8409_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1028, 0x0A11, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A12, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A23, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A24, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A25, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A29, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A2A, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A2B, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0AB0, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB2, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB1, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB3, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB4, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB5, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AD9, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0ADA, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0ADB, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0ADC, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AF4, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AF5, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0A77, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A78, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A79, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A7A, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A7D, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A7E, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A7F, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A80, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0ADF, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AE0, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AE1, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AE2, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AE9, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEA, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEB, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEC, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AED, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEE, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEF, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AF0, "Cyborg", CS8409_CYBORG),
	{} /* terminator */
};

static const struct hda_verb cs8409_cs42l42_init_verbs[] = {
	{ 0x01, AC_VERB_SET_GPIO_WAKE_MASK, 0x0018 }, /* WAKE from GPIO 3,4 */
	{ 0x47, AC_VERB_SET_PROC_STATE, 0x0001 },     /* Enable VPW processing  */
	{ 0x47, AC_VERB_SET_COEF_INDEX, 0x0002 },     /* Configure GPIO 6,7 */
	{ 0x47, AC_VERB_SET_PROC_COEF,  0x0080 },     /* I2C mode */
	{ 0x47, AC_VERB_SET_COEF_INDEX, 0x005b },     /* Set I2C bus speed */
	{ 0x47, AC_VERB_SET_PROC_COEF,  0x0200 },     /* 100kHz I2C_STO = 2 */
	{} /* terminator */
};

static const struct hda_pintbl cs8409_cs42l42_pincfgs[] = {
	{ 0x24, 0x042120f0 }, /* ASP-1-TX */
	{ 0x34, 0x04a12050 }, /* ASP-1-RX */
	{ 0x2c, 0x901000f0 }, /* ASP-2-TX */
	{ 0x44, 0x90a00090 }, /* DMIC-1 */
	{} /* terminator */
};

static const struct hda_fixup cs8409_fixups[] = {
	[CS8409_BULLSEYE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cs8409_cs42l42_pincfgs,
		.chained = true,
		.chain_id = CS8409_FIXUPS,
	},
	[CS8409_WARLOCK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cs8409_cs42l42_pincfgs,
		.chained = true,
		.chain_id = CS8409_FIXUPS,
	},
	[CS8409_CYBORG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cs8409_cs42l42_pincfgs,
		.chained = true,
		.chain_id = CS8409_FIXUPS,
	},
	[CS8409_FIXUPS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs8409_cs42l42_fixups,
	},
};

/* Vendor specific HW configuration for CS42L42 */
static const struct cs8409_i2c_param cs42l42_init_reg_seq[] = {
	{ 0x1010, 0xB0 },
	{ 0x1D01, 0x00 },
	{ 0x1D02, 0x06 },
	{ 0x1D03, 0x00 },
	{ 0x1107, 0x01 },
	{ 0x1009, 0x02 },
	{ 0x1007, 0x03 },
	{ 0x1201, 0x00 },
	{ 0x1208, 0x13 },
	{ 0x1205, 0xFF },
	{ 0x1206, 0x00 },
	{ 0x1207, 0x20 },
	{ 0x1202, 0x0D },
	{ 0x2A02, 0x02 },
	{ 0x2A03, 0x00 },
	{ 0x2A04, 0x00 },
	{ 0x2A05, 0x02 },
	{ 0x2A06, 0x00 },
	{ 0x2A07, 0x20 },
	{ 0x2A08, 0x02 },
	{ 0x2A09, 0x00 },
	{ 0x2A0A, 0x80 },
	{ 0x2A0B, 0x02 },
	{ 0x2A0C, 0x00 },
	{ 0x2A0D, 0xA0 },
	{ 0x2A01, 0x0C },
	{ 0x2902, 0x01 },
	{ 0x2903, 0x02 },
	{ 0x2904, 0x00 },
	{ 0x2905, 0x00 },
	{ 0x2901, 0x01 },
	{ 0x1101, 0x0A },
	{ 0x1102, 0x84 },
	{ 0x2301, 0x00 },
	{ 0x2303, 0x00 },
	{ 0x2302, 0x3f },
	{ 0x2001, 0x03 },
	{ 0x1B75, 0xB6 },
	{ 0x1B73, 0xC2 },
	{ 0x1129, 0x01 },
	{ 0x1121, 0xF3 },
	{ 0x1103, 0x20 },
	{ 0x1105, 0x00 },
	{ 0x1112, 0xC0 },
	{ 0x1113, 0x80 },
	{ 0x1C03, 0xC0 },
	{ 0x1105, 0x00 },
	{ 0x1112, 0xC0 },
	{ 0x1101, 0x02 },
	{} /* Terminator */
};

/* Vendor specific hw configuration for CS8409 */
static const struct cs8409_cir_param cs8409_cs42l42_hw_cfg[] = {
	{ 0x47, 0x00, 0xb008 }, /* +PLL1/2_EN, +I2C_EN */
	{ 0x47, 0x01, 0x0002 }, /* ASP1/2_EN=0, ASP1_STP=1 */
	{ 0x47, 0x02, 0x0a80 }, /* ASP1/2_BUS_IDLE=10, +GPIO_I2C */
	{ 0x47, 0x19, 0x0800 }, /* ASP1.A: TX.LAP=0, TX.LSZ=24 bits, TX.LCS=0 */
	{ 0x47, 0x1a, 0x0820 }, /* ASP1.A: TX.RAP=0, TX.RSZ=24 bits, TX.RCS=32 */
	{ 0x47, 0x29, 0x0800 }, /* ASP2.A: TX.LAP=0, TX.LSZ=24 bits, TX.LCS=0 */
	{ 0x47, 0x2a, 0x2800 }, /* ASP2.A: TX.RAP=1, TX.RSZ=24 bits, TX.RCS=0 */
	{ 0x47, 0x39, 0x0800 }, /* ASP1.A: RX.LAP=0, RX.LSZ=24 bits, RX.LCS=0 */
	{ 0x47, 0x3a, 0x0800 }, /* ASP1.A: RX.RAP=0, RX.RSZ=24 bits, RX.RCS=0 */
	{ 0x47, 0x03, 0x8000 }, /* ASP1: LCHI = 00h */
	{ 0x47, 0x04, 0x28ff }, /* ASP1: MC/SC_SRCSEL=PLL1, LCPR=FFh */
	{ 0x47, 0x05, 0x0062 }, /* ASP1: MCEN=0, FSD=011, SCPOL_IN/OUT=0, SCDIV=1:4 */
	{ 0x47, 0x06, 0x801f }, /* ASP2: LCHI=1Fh */
	{ 0x47, 0x07, 0x283f }, /* ASP2: MC/SC_SRCSEL=PLL1, LCPR=3Fh */
	{ 0x47, 0x08, 0x805c }, /* ASP2: 5050=1, MCEN=0, FSD=010, SCPOL_IN/OUT=1, SCDIV=1:16 */
	{ 0x47, 0x09, 0x0023 }, /* DMIC1_MO=10b, DMIC1/2_SR=1 */
	{ 0x47, 0x0a, 0x0000 }, /* ASP1/2_BEEP=0 */
	{ 0x47, 0x01, 0x0062 }, /* ASP1/2_EN=1, ASP1_STP=1 */
	{ 0x47, 0x00, 0x9008 }, /* -PLL2_EN */
	{ 0x47, 0x68, 0x0000 }, /* TX2.A: pre-scale att.=0 dB */
	{ 0x47, 0x82, 0xfc03 }, /* ASP1/2_xxx_EN=1, ASP1/2_MCLK_EN=0, DMIC1_SCL_EN=1 */
	{ 0x47, 0xc0, 0x9999 }, /* test mode on */
	{ 0x47, 0xc5, 0x0000 }, /* GPIO hysteresis = 30 us */
	{ 0x47, 0xc0, 0x0000 }, /* test mode off */
	{} /* Terminator */
};

static const struct cs8409_cir_param cs8409_cs42l42_bullseye_atn[] = {
	{ 0x47, 0x65, 0x4000 }, /* EQ_SEL=1, EQ1/2_EN=0 */
	{ 0x47, 0x64, 0x4000 }, /* +EQ_ACC */
	{ 0x47, 0x65, 0x4010 }, /* +EQ2_EN */
	{ 0x47, 0x63, 0x0647 }, /* EQ_DATA_HI=0x0647 */
	{ 0x47, 0x64, 0xc0c7 }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=0, EQ_DATA_LO=0x67 */
	{ 0x47, 0x63, 0x0647 }, /* EQ_DATA_HI=0x0647 */
	{ 0x47, 0x64, 0xc1c7 }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=1, EQ_DATA_LO=0x67 */
	{ 0x47, 0x63, 0xf370 }, /* EQ_DATA_HI=0xf370 */
	{ 0x47, 0x64, 0xc271 }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=2, EQ_DATA_LO=0x71 */
	{ 0x47, 0x63, 0x1ef8 }, /* EQ_DATA_HI=0x1ef8 */
	{ 0x47, 0x64, 0xc348 }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=3, EQ_DATA_LO=0x48 */
	{ 0x47, 0x63, 0xc110 }, /* EQ_DATA_HI=0xc110 */
	{ 0x47, 0x64, 0xc45a }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=4, EQ_DATA_LO=0x5a */
	{ 0x47, 0x63, 0x1f29 }, /* EQ_DATA_HI=0x1f29 */
	{ 0x47, 0x64, 0xc574 }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=5, EQ_DATA_LO=0x74 */
	{ 0x47, 0x63, 0x1d7a }, /* EQ_DATA_HI=0x1d7a */
	{ 0x47, 0x64, 0xc653 }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=6, EQ_DATA_LO=0x53 */
	{ 0x47, 0x63, 0xc38c }, /* EQ_DATA_HI=0xc38c */
	{ 0x47, 0x64, 0xc714 }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=7, EQ_DATA_LO=0x14 */
	{ 0x47, 0x63, 0x1ca3 }, /* EQ_DATA_HI=0x1ca3 */
	{ 0x47, 0x64, 0xc8c7 }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=8, EQ_DATA_LO=0xc7 */
	{ 0x47, 0x63, 0xc38c }, /* EQ_DATA_HI=0xc38c */
	{ 0x47, 0x64, 0xc914 }, /* +EQ_WRT, +EQ_ACC, EQ_ADR=9, EQ_DATA_LO=0x14 */
	{ 0x47, 0x64, 0x0000 }, /* -EQ_ACC, -EQ_WRT */
	{} /* Terminator */
};

/**
 * cs8409_enable_i2c_clock - Enable I2C clocks
 * @codec: the codec instance
 * @enable: Enable or disable I2C clocks
 *
 * Enable or Disable I2C clocks.
 */
static void cs8409_enable_i2c_clock(struct hda_codec *codec, unsigned int enable)
{
	unsigned int retval;
	unsigned int newval;

	retval = cs_vendor_coef_get(codec, 0x0);
	newval = (enable) ? (retval | 0x8) : (retval & 0xfffffff7);
	cs_vendor_coef_set(codec, 0x0, newval);
}

/**
 * cs8409_i2c_wait_complete - Wait for I2C transaction
 * @codec: the codec instance
 *
 * Wait for I2C transaction to complete.
 * Return -1 if transaction wait times out.
 */
static int cs8409_i2c_wait_complete(struct hda_codec *codec)
{
	int repeat = 5;
	unsigned int retval;

	do {
		retval = cs_vendor_coef_get(codec, CIR_I2C_STATUS);
		if ((retval & 0x18) != 0x18) {
			usleep_range(2000, 4000);
			--repeat;
		} else
			return 0;

	} while (repeat);

	return -1;
}

/**
 * cs8409_i2c_read - CS8409 I2C Read.
 * @codec: the codec instance
 * @i2c_address: I2C Address
 * @i2c_reg: Register to read
 * @paged: Is a paged transaction
 *
 * CS8409 I2C Read.
 * Returns negative on error, otherwise returns read value in bits 0-7.
 */
static int cs8409_i2c_read(struct hda_codec *codec,
		unsigned int i2c_address,
		unsigned int i2c_reg,
		unsigned int paged)
{
	unsigned int i2c_reg_data;
	unsigned int read_data;

	cs8409_enable_i2c_clock(codec, 1);
	cs_vendor_coef_set(codec, CIR_I2C_ADDR, i2c_address);

	if (paged) {
		cs_vendor_coef_set(codec, CIR_I2C_QWRITE, i2c_reg >> 8);
		if (cs8409_i2c_wait_complete(codec) < 0) {
			codec_err(codec,
				"%s() Paged Transaction Failed 0x%02x : 0x%04x\n",
				__func__, i2c_address, i2c_reg);
			return -EIO;
		}
	}

	i2c_reg_data = (i2c_reg << 8) & 0x0ffff;
	cs_vendor_coef_set(codec, CIR_I2C_QREAD, i2c_reg_data);
	if (cs8409_i2c_wait_complete(codec) < 0) {
		codec_err(codec, "%s() Transaction Failed 0x%02x : 0x%04x\n",
			__func__, i2c_address, i2c_reg);
		return -EIO;
	}

	/* Register in bits 15-8 and the data in 7-0 */
	read_data = cs_vendor_coef_get(codec, CIR_I2C_QREAD);

	cs8409_enable_i2c_clock(codec, 0);

	return read_data & 0x0ff;
}

/**
 * cs8409_i2c_write - CS8409 I2C Write.
 * @codec: the codec instance
 * @i2c_address: I2C Address
 * @i2c_reg: Register to write to
 * @i2c_data: Data to write
 * @paged: Is a paged transaction
 *
 * CS8409 I2C Write.
 * Returns negative on error, otherwise returns 0.
 */
static int cs8409_i2c_write(struct hda_codec *codec,
		unsigned int i2c_address, unsigned int i2c_reg,
		unsigned int i2c_data,
		unsigned int paged)
{
	unsigned int i2c_reg_data;

	cs8409_enable_i2c_clock(codec, 1);
	cs_vendor_coef_set(codec, CIR_I2C_ADDR, i2c_address);

	if (paged) {
		cs_vendor_coef_set(codec, CIR_I2C_QWRITE, i2c_reg >> 8);
		if (cs8409_i2c_wait_complete(codec) < 0) {
			codec_err(codec,
				"%s() Paged Transaction Failed 0x%02x : 0x%04x\n",
				__func__, i2c_address, i2c_reg);
			return -EIO;
		}
	}

	i2c_reg_data = ((i2c_reg << 8) & 0x0ff00) | (i2c_data & 0x0ff);
	cs_vendor_coef_set(codec, CIR_I2C_QWRITE, i2c_reg_data);

	if (cs8409_i2c_wait_complete(codec) < 0) {
		codec_err(codec, "%s() Transaction Failed 0x%02x : 0x%04x\n",
			__func__, i2c_address, i2c_reg);
		return -EIO;
	}

	cs8409_enable_i2c_clock(codec, 0);

	return 0;
}

static int cs8409_cs42l42_volume_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 nid = get_amp_nid(kcontrol);
	u8 chs = get_amp_channels(kcontrol);

	codec_dbg(codec, "%s() nid: %d\n", __func__, nid);
	switch (nid) {
	case CS8409_CS42L42_HP_PIN_NID:
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->count = chs == 3 ? 2 : 1;
		uinfo->value.integer.min = CS8409_CS42L42_HP_VOL_REAL_MIN;
		uinfo->value.integer.max = CS8409_CS42L42_HP_VOL_REAL_MAX;
		break;
	case CS8409_CS42L42_AMIC_PIN_NID:
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->count = chs == 3 ? 2 : 1;
		uinfo->value.integer.min = CS8409_CS42L42_AMIC_VOL_REAL_MIN;
		uinfo->value.integer.max = CS8409_CS42L42_AMIC_VOL_REAL_MAX;
		break;
	default:
		break;
	}
	return 0;
}

static void cs8409_cs42l42_update_volume(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	int data;

	mutex_lock(&spec->cs8409_i2c_mux);
	data = cs8409_i2c_read(codec, CS42L42_I2C_ADDR,
				CS8409_CS42L42_REG_HS_VOLUME_CHA, 1);
	if (data >= 0)
		spec->cs42l42_hp_volume[0] = -data;
	else
		spec->cs42l42_hp_volume[0] = CS8409_CS42L42_HP_VOL_REAL_MIN;
	data = cs8409_i2c_read(codec, CS42L42_I2C_ADDR,
				CS8409_CS42L42_REG_HS_VOLUME_CHB, 1);
	if (data >= 0)
		spec->cs42l42_hp_volume[1] = -data;
	else
		spec->cs42l42_hp_volume[1] = CS8409_CS42L42_HP_VOL_REAL_MIN;
	data = cs8409_i2c_read(codec, CS42L42_I2C_ADDR,
				CS8409_CS42L42_REG_AMIC_VOLUME, 1);
	if (data >= 0)
		spec->cs42l42_hs_mic_volume[0] = -data;
	else
		spec->cs42l42_hs_mic_volume[0] = CS8409_CS42L42_AMIC_VOL_REAL_MIN;
	mutex_unlock(&spec->cs8409_i2c_mux);
	spec->cs42l42_volume_init = 1;
}

static int cs8409_cs42l42_volume_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	long *valp = ucontrol->value.integer.value;

	if (!spec->cs42l42_volume_init) {
		snd_hda_power_up(codec);
		cs8409_cs42l42_update_volume(codec);
		snd_hda_power_down(codec);
	}
	switch (nid) {
	case CS8409_CS42L42_HP_PIN_NID:
		if (chs & BIT(0))
			*valp++ = spec->cs42l42_hp_volume[0];
		if (chs & BIT(1))
			*valp++ = spec->cs42l42_hp_volume[1];
		break;
	case CS8409_CS42L42_AMIC_PIN_NID:
		if (chs & BIT(0))
			*valp++ = spec->cs42l42_hs_mic_volume[0];
		break;
	default:
		break;
	}
	return 0;
}

static int cs8409_cs42l42_volume_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change = 0;
	char vol;

	snd_hda_power_up(codec);
	switch (nid) {
	case CS8409_CS42L42_HP_PIN_NID:
		mutex_lock(&spec->cs8409_i2c_mux);
		if (chs & BIT(0)) {
			vol = -(*valp);
			change = cs8409_i2c_write(codec, CS42L42_I2C_ADDR,
				CS8409_CS42L42_REG_HS_VOLUME_CHA, vol, 1);
			valp++;
		}
		if (chs & BIT(1)) {
			vol = -(*valp);
			change |= cs8409_i2c_write(codec, CS42L42_I2C_ADDR,
				CS8409_CS42L42_REG_HS_VOLUME_CHB, vol, 1);
		}
		mutex_unlock(&spec->cs8409_i2c_mux);
		break;
	case CS8409_CS42L42_AMIC_PIN_NID:
		mutex_lock(&spec->cs8409_i2c_mux);
		if (chs & BIT(0)) {
			change = cs8409_i2c_write(
				codec, CS42L42_I2C_ADDR,
				CS8409_CS42L42_REG_AMIC_VOLUME, (char)*valp, 1);
			valp++;
		}
		mutex_unlock(&spec->cs8409_i2c_mux);
		break;
	default:
		break;
	}
	cs8409_cs42l42_update_volume(codec);
	snd_hda_power_down(codec);
	return change;
}

static const DECLARE_TLV_DB_SCALE(
	cs8409_cs42l42_hp_db_scale,
	CS8409_CS42L42_HP_VOL_REAL_MIN * 100, 100, 1);

static const DECLARE_TLV_DB_SCALE(
	cs8409_cs42l42_amic_db_scale,
	CS8409_CS42L42_AMIC_VOL_REAL_MIN * 100, 100, 1);

static const struct snd_kcontrol_new cs8409_cs42l42_hp_volume_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.index = 0,
	.name = "Headphone Playback Volume",
	.subdevice = (HDA_SUBDEV_AMP_FLAG | HDA_SUBDEV_NID_FLAG),
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE
			 | SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = cs8409_cs42l42_volume_info,
	.get = cs8409_cs42l42_volume_get,
	.put = cs8409_cs42l42_volume_put,
	.tlv = { .p = cs8409_cs42l42_hp_db_scale },
	.private_value = HDA_COMPOSE_AMP_VAL(
		CS8409_CS42L42_HP_PIN_NID, 3, 0, HDA_OUTPUT)
		| HDA_AMP_VAL_MIN_MUTE
};

static const struct snd_kcontrol_new cs8409_cs42l42_amic_volume_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.index = 0,
	.name = "Mic Capture Volume",
	.subdevice = (HDA_SUBDEV_AMP_FLAG | HDA_SUBDEV_NID_FLAG),
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE
			 | SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = cs8409_cs42l42_volume_info,
	.get = cs8409_cs42l42_volume_get,
	.put = cs8409_cs42l42_volume_put,
	.tlv = { .p = cs8409_cs42l42_amic_db_scale },
	.private_value = HDA_COMPOSE_AMP_VAL(
		CS8409_CS42L42_AMIC_PIN_NID, 1, 0, HDA_INPUT)
		| HDA_AMP_VAL_MIN_MUTE
};

/* Assert/release RTS# line to CS42L42 */
static void cs8409_cs42l42_reset(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;

	/* Assert RTS# line */
	snd_hda_codec_write(codec,
			codec->core.afg, 0, AC_VERB_SET_GPIO_DATA, 0);
	/* wait ~10ms */
	usleep_range(10000, 15000);
	/* Release RTS# line */
	snd_hda_codec_write(codec,
			codec->core.afg, 0, AC_VERB_SET_GPIO_DATA, GPIO5_INT);
	/* wait ~10ms */
	usleep_range(10000, 15000);

	mutex_lock(&spec->cs8409_i2c_mux);

	/* Clear interrupts, by reading interrupt status registers */
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1308, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1309, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x130A, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x130F, 1);

	mutex_unlock(&spec->cs8409_i2c_mux);

}

/* Configure CS42L42 slave codec for jack autodetect */
static void cs8409_cs42l42_enable_jack_detect(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;

	mutex_lock(&spec->cs8409_i2c_mux);

	/* Set TIP_SENSE_EN for analog front-end of tip sense. */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b70, 0x0020, 1);
	/* Clear WAKE# */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b71, 0x0001, 1);
	/* Wait ~2.5ms */
	usleep_range(2500, 3000);
	/* Set mode WAKE# output follows the combination logic directly */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b71, 0x0020, 1);
	/* Clear interrupts status */
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x130f, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1b7b, 1);
	/* Enable interrupt */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1320, 0x03, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b79, 0x00, 1);

	mutex_unlock(&spec->cs8409_i2c_mux);
}

/* Enable and run CS42L42 slave codec jack auto detect */
static void cs8409_cs42l42_run_jack_detect(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;

	mutex_lock(&spec->cs8409_i2c_mux);

	/* Clear interrupts */
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1308, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1b77, 1);

	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1102, 0x87, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1f06, 0x86, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b74, 0x07, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x131b, 0x01, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1120, 0x80, 1);
	/* Wait ~110ms*/
	usleep_range(110000, 200000);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x111f, 0x77, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1120, 0xc0, 1);
	/* Wait ~10ms */
	usleep_range(10000, 25000);

	mutex_unlock(&spec->cs8409_i2c_mux);

}

static void cs8409_cs42l42_reg_setup(struct hda_codec *codec)
{
	const struct cs8409_i2c_param *seq = cs42l42_init_reg_seq;
	struct cs_spec *spec = codec->spec;

	mutex_lock(&spec->cs8409_i2c_mux);

	for (; seq->addr; seq++)
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, seq->addr, seq->reg, 1);

	mutex_unlock(&spec->cs8409_i2c_mux);

}

/*
 * In the case of CS8409 we do not have unsolicited events from NID's 0x24
 * and 0x34 where hs mic and hp are connected. Companion codec CS42L42 will
 * generate interrupt via gpio 4 to notify jack events. We have to overwrite
 * generic snd_hda_jack_unsol_event(), read CS42L42 jack detect status registers
 * and then notify status via generic snd_hda_jack_unsol_event() call.
 */
static void cs8409_jack_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct cs_spec *spec = codec->spec;
	int status_changed = 0;
	int reg_cdc_status;
	int reg_hs_status;
	int reg_ts_status;
	int type;
	struct hda_jack_tbl *jk;

	/* jack_unsol_event() will be called every time gpio line changing state.
	 * In this case gpio4 line goes up as a result of reading interrupt status
	 * registers in previous cs8409_jack_unsol_event() call.
	 * We don't need to handle this event, ignoring...
	 */
	if ((res & (1 << 4)))
		return;

	mutex_lock(&spec->cs8409_i2c_mux);

	/* Read jack detect status registers */
	reg_cdc_status = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1308, 1);
	reg_hs_status = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1124, 1);
	reg_ts_status = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x130f, 1);

	/* Clear interrupts, by reading interrupt status registers */
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1b7b, 1);

	mutex_unlock(&spec->cs8409_i2c_mux);

	/* If status values are < 0, read error has occurred. */
	if (reg_cdc_status < 0 || reg_hs_status < 0 || reg_ts_status < 0)
		return;

	/* HSDET_AUTO_DONE */
	if (reg_cdc_status & CS42L42_HSDET_AUTO_DONE) {

		type = ((reg_hs_status & CS42L42_HSTYPE_MASK) + 1);
		/* CS42L42 reports optical jack as type 4
		 * We don't handle optical jack
		 */
		if (type != 4) {
			if (!spec->cs42l42_hp_jack_in) {
				status_changed = 1;
				spec->cs42l42_hp_jack_in = 1;
			}
			/* type = 3 has no mic */
			if ((!spec->cs42l42_mic_jack_in) && (type != 3)) {
				status_changed = 1;
				spec->cs42l42_mic_jack_in = 1;
			}
		} else {
			if (spec->cs42l42_hp_jack_in || spec->cs42l42_mic_jack_in) {
				status_changed = 1;
				spec->cs42l42_hp_jack_in = 0;
				spec->cs42l42_mic_jack_in = 0;
			}
		}

	} else {
		/* TIP_SENSE INSERT/REMOVE */
		switch (reg_ts_status) {
		case CS42L42_JACK_INSERTED:
			cs8409_cs42l42_run_jack_detect(codec);
			break;

		case CS42L42_JACK_REMOVED:
			if (spec->cs42l42_hp_jack_in || spec->cs42l42_mic_jack_in) {
				status_changed = 1;
				spec->cs42l42_hp_jack_in = 0;
				spec->cs42l42_mic_jack_in = 0;
			}
			break;

		default:
			/* jack in transition */
			status_changed = 0;
			break;
		}
	}

	if (status_changed) {

		snd_hda_set_pin_ctl(codec, CS8409_CS42L42_SPK_PIN_NID,
				spec->cs42l42_hp_jack_in ? 0 : PIN_OUT);

		/* Report jack*/
		jk = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L42_HP_PIN_NID, 0);
		if (jk) {
			snd_hda_jack_unsol_event(codec,
				(jk->tag << AC_UNSOL_RES_TAG_SHIFT) & AC_UNSOL_RES_TAG);
		}
		/* Report jack*/
		jk = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L42_AMIC_PIN_NID, 0);
		if (jk) {
			snd_hda_jack_unsol_event(codec,
				(jk->tag << AC_UNSOL_RES_TAG_SHIFT) & AC_UNSOL_RES_TAG);
		}
	}
}

#ifdef CONFIG_PM
/* Manage PDREF, when transition to D3hot */
static int cs8409_suspend(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;

	mutex_lock(&spec->cs8409_i2c_mux);
	/* Power down CS42L42 ASP/EQ/MIX/HP */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1101, 0xfe, 1);
	mutex_unlock(&spec->cs8409_i2c_mux);
	/* Assert CS42L42 RTS# line */
	snd_hda_codec_write(codec,
			codec->core.afg, 0, AC_VERB_SET_GPIO_DATA, 0);

	snd_hda_shutup_pins(codec);

	return 0;
}
#endif

/* Enable/Disable Unsolicited Response for gpio(s) 3,4 */
static void cs8409_enable_ur(struct hda_codec *codec, int flag)
{
	/* GPIO4 INT# and GPIO3 WAKE# */
	snd_hda_codec_write(codec, codec->core.afg,
			0, AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK,
			flag ? (GPIO3_INT | GPIO4_INT) : 0);

	snd_hda_codec_write(codec, codec->core.afg,
			0, AC_VERB_SET_UNSOLICITED_ENABLE,
			flag ? AC_UNSOL_ENABLED : 0);

}

/* Vendor specific HW configuration
 * PLL, ASP, I2C, SPI, GPIOs, DMIC etc...
 */
static void cs8409_cs42l42_hw_init(struct hda_codec *codec)
{
	const struct cs8409_cir_param *seq = cs8409_cs42l42_hw_cfg;
	const struct cs8409_cir_param *seq_bullseye = cs8409_cs42l42_bullseye_atn;
	struct cs_spec *spec = codec->spec;

	if (spec->gpio_mask) {
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_MASK,
				    spec->gpio_mask);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DIRECTION,
				    spec->gpio_dir);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
				    spec->gpio_data);
	}

	for (; seq->nid; seq++)
		cs_vendor_coef_set(codec, seq->cir, seq->coeff);

	if (codec->fixup_id == CS8409_BULLSEYE)
		for (; seq_bullseye->nid; seq_bullseye++)
			cs_vendor_coef_set(codec, seq_bullseye->cir, seq_bullseye->coeff);

	/* Disable Unsolicited Response during boot */
	cs8409_enable_ur(codec, 0);

	/* Reset CS42L42 */
	cs8409_cs42l42_reset(codec);

	/* Initialise CS42L42 companion codec */
	cs8409_cs42l42_reg_setup(codec);

	if (codec->fixup_id == CS8409_WARLOCK ||
			codec->fixup_id == CS8409_CYBORG) {
		/* FULL_SCALE_VOL = 0 for Warlock / Cyborg */
		mutex_lock(&spec->cs8409_i2c_mux);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x2001, 0x01, 1);
		mutex_unlock(&spec->cs8409_i2c_mux);
		/* DMIC1_MO=00b, DMIC1/2_SR=1 */
		cs_vendor_coef_set(codec, 0x09, 0x0003);
	}

	/* Restore Volumes after Resume */
	if (spec->cs42l42_volume_init) {
		mutex_lock(&spec->cs8409_i2c_mux);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR,
					CS8409_CS42L42_REG_HS_VOLUME_CHA,
					-spec->cs42l42_hp_volume[0],
					1);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR,
					CS8409_CS42L42_REG_HS_VOLUME_CHB,
					-spec->cs42l42_hp_volume[1],
					1);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR,
					CS8409_CS42L42_REG_AMIC_VOLUME,
					spec->cs42l42_hs_mic_volume[0],
					1);
		mutex_unlock(&spec->cs8409_i2c_mux);
	}

	cs8409_cs42l42_update_volume(codec);

	cs8409_cs42l42_enable_jack_detect(codec);

	/* Enable Unsolicited Response */
	cs8409_enable_ur(codec, 1);
}

static int cs8409_cs42l42_init(struct hda_codec *codec)
{
	int ret = snd_hda_gen_init(codec);

	if (!ret)
		snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT);

	return ret;
}

static const struct hda_codec_ops cs8409_cs42l42_patch_ops = {
	.build_controls = cs_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cs8409_cs42l42_init,
	.free = cs_free,
	.unsol_event = cs8409_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = cs8409_suspend,
#endif
};

static void cs8409_cs42l42_fixups(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct cs_spec *spec = codec->spec;
	int caps;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_add_verbs(codec, cs8409_cs42l42_init_verbs);
		/* verb exec op override */
		spec->exec_verb = codec->core.exec_verb;
		codec->core.exec_verb = cs8409_cs42l42_exec_verb;

		mutex_init(&spec->cs8409_i2c_mux);

		codec->patch_ops = cs8409_cs42l42_patch_ops;

		spec->gen.suppress_auto_mute = 1;
		spec->gen.no_primary_hp = 1;
		spec->gen.suppress_vmaster = 1;

		/* GPIO 5 out, 3,4 in */
		spec->gpio_dir = GPIO5_INT;
		spec->gpio_data = 0;
		spec->gpio_mask = 0x03f;

		spec->cs42l42_hp_jack_in = 0;
		spec->cs42l42_mic_jack_in = 0;

		/* Basic initial sequence for specific hw configuration */
		snd_hda_sequence_write(codec, cs8409_cs42l42_init_verbs);

		/* CS8409 is simple HDA bridge and intended to be used with a remote
		 * companion codec. Most of input/output PIN(s) have only basic
		 * capabilities. NID(s) 0x24 and 0x34 have only OUTC and INC
		 * capabilities and no presence detect capable (PDC) and call to
		 * snd_hda_gen_build_controls() will mark them as non detectable
		 * phantom jacks. However, in this configuration companion codec
		 * CS42L42 is connected to these pins and it has jack detect
		 * capabilities. We have to override pin capabilities,
		 * otherwise they will not be created as input devices.
		 */
		caps = snd_hdac_read_parm(&codec->core, CS8409_CS42L42_HP_PIN_NID,
				AC_PAR_PIN_CAP);
		if (caps >= 0)
			snd_hdac_override_parm(&codec->core,
				CS8409_CS42L42_HP_PIN_NID, AC_PAR_PIN_CAP,
				(caps | (AC_PINCAP_IMP_SENSE | AC_PINCAP_PRES_DETECT)));

		caps = snd_hdac_read_parm(&codec->core, CS8409_CS42L42_AMIC_PIN_NID,
				AC_PAR_PIN_CAP);
		if (caps >= 0)
			snd_hdac_override_parm(&codec->core,
				CS8409_CS42L42_AMIC_PIN_NID, AC_PAR_PIN_CAP,
				(caps | (AC_PINCAP_IMP_SENSE | AC_PINCAP_PRES_DETECT)));

		snd_hda_override_wcaps(codec, CS8409_CS42L42_HP_PIN_NID,
			(get_wcaps(codec, CS8409_CS42L42_HP_PIN_NID) | AC_WCAP_UNSOL_CAP));

		snd_hda_override_wcaps(codec, CS8409_CS42L42_AMIC_PIN_NID,
			(get_wcaps(codec, CS8409_CS42L42_AMIC_PIN_NID) | AC_WCAP_UNSOL_CAP));
		break;
	case HDA_FIXUP_ACT_PROBE:

		/* Set initial DMIC volume to -26 dB */
		snd_hda_codec_amp_init_stereo(codec, CS8409_CS42L42_DMIC_ADC_PIN_NID,
				HDA_INPUT, 0, 0xff, 0x19);
		snd_hda_gen_add_kctl(&spec->gen,
			NULL, &cs8409_cs42l42_hp_volume_mixer);
		snd_hda_gen_add_kctl(&spec->gen,
			NULL, &cs8409_cs42l42_amic_volume_mixer);
		cs8409_cs42l42_hw_init(codec);
		snd_hda_codec_set_name(codec, "CS8409/CS42L42");
		break;
	case HDA_FIXUP_ACT_INIT:
		cs8409_cs42l42_hw_init(codec);
		fallthrough;
	case HDA_FIXUP_ACT_BUILD:
		/* Run jack auto detect first time on boot
		 * after controls have been added, to check if jack has
		 * been already plugged in.
		 * Run immediately after init.
		 */
		cs8409_cs42l42_run_jack_detect(codec);
		usleep_range(100000, 150000);
		break;
	default:
		break;
	}
}

static int cs8409_cs42l42_exec_verb(struct hdac_device *dev,
		unsigned int cmd, unsigned int flags, unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct cs_spec *spec = codec->spec;

	unsigned int nid = ((cmd >> 20) & 0x07f);
	unsigned int verb = ((cmd >> 8) & 0x0fff);

	/* CS8409 pins have no AC_PINSENSE_PRESENCE
	 * capabilities. We have to intercept 2 calls for pins 0x24 and 0x34
	 * and return correct pin sense values for read_pin_sense() call from
	 * hda_jack based on CS42L42 jack detect status.
	 */
	switch (nid) {
	case CS8409_CS42L42_HP_PIN_NID:
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (spec->cs42l42_hp_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;

	case CS8409_CS42L42_AMIC_PIN_NID:
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (spec->cs42l42_mic_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;

	default:
		break;
	}

	return spec->exec_verb(dev, cmd, flags, res);
}

static int patch_cs8409(struct hda_codec *codec)
{
	int err;

	if (!cs_alloc_spec(codec, CS8409_VENDOR_NID))
		return -ENOMEM;

	snd_hda_pick_fixup(codec,
			cs8409_models, cs8409_fixup_tbl, cs8409_fixups);

	codec_dbg(codec, "Picked ID=%d, VID=%08x, DEV=%08x\n",
			codec->fixup_id,
			codec->bus->pci->subsystem_vendor,
			codec->bus->pci->subsystem_device);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = cs_parse_auto_config(codec);
	if (err < 0) {
		cs_free(codec);
		return err;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);
	return 0;
}

/*
 * patch entries
 */
static const struct hda_device_id snd_hda_id_cirrus[] = {
	HDA_CODEC_ENTRY(0x10134206, "CS4206", patch_cs420x),
	HDA_CODEC_ENTRY(0x10134207, "CS4207", patch_cs420x),
	HDA_CODEC_ENTRY(0x10134208, "CS4208", patch_cs4208),
	HDA_CODEC_ENTRY(0x10134210, "CS4210", patch_cs4210),
	HDA_CODEC_ENTRY(0x10134213, "CS4213", patch_cs4213),
	HDA_CODEC_ENTRY(0x10138409, "CS8409", patch_cs8409),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_cirrus);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cirrus Logic HD-audio codec");

static struct hda_codec_driver cirrus_driver = {
	.id = snd_hda_id_cirrus,
};

module_hda_codec_driver(cirrus_driver);

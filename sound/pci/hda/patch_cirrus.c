/*
 * HD audio interface patch for Cirrus Logic CS420x chip
 *
 * Copyright (c) 2009 Takashi Iwai <tiwai@suse.de>
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
#include <linux/pci.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/tlv.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

/*
 */

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

#define SPDIF_EVENT		0x04

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

	if (spec->gpio_eapd_hp) {
		spec->gpio_data = spec->gen.hp_jack_present ?
			spec->gpio_eapd_hp : spec->gpio_eapd_speaker;
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

#if 0 /* Don't to set to D3 as we are in power-up sequence */
	{0x07, AC_VERB_SET_POWER_STATE, 0x03}, /* S/PDIF Rx: D3 */
	{0x08, AC_VERB_SET_POWER_STATE, 0x03}, /* S/PDIF Tx: D3 */
	/*{0x01, AC_VERB_SET_POWER_STATE, 0x03},*/ /* AFG: D3 This is already handled */
#endif

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

	/* init_verb sequence for C0/C1/C2 errata*/
	snd_hda_sequence_write(codec, cs_errata_init_verbs);

	snd_hda_sequence_write(codec, cs_coef_init_verbs);

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
	init_digital_coef(codec);

	return 0;
}

#define cs_free		snd_hda_gen_free

static const struct hda_codec_ops cs_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cs_init,
	.free = cs_free,
	.unsol_event = snd_hda_jack_unsol_event,
};

static int cs_parse_auto_config(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL, 0);
	if (err < 0)
		return err;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		return err;

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
	SND_PCI_QUIRK(0x106b, 0x1c00, "MacBookPro 8,1", CS420X_MBP81),
	SND_PCI_QUIRK(0x106b, 0x2000, "iMac 12,2", CS420X_IMAC27_122),
	SND_PCI_QUIRK(0x106b, 0x2800, "MacBookPro 10,1", CS420X_MBP101),
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
};

static struct cs_spec *cs_alloc_spec(struct hda_codec *codec, int vendor_nid)
{
	struct cs_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;
	codec->spec = spec;
	spec->vendor_nid = vendor_nid;
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

	spec->gen.automute_hook = cs_automute;

	snd_hda_pick_fixup(codec, cs420x_models, cs420x_fixup_tbl,
			   cs420x_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = cs_parse_auto_config(codec);
	if (err < 0)
		goto error;

	codec->patch_ops = cs_patch_ops;

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
	    Disable Coefficient Index Auto-Increment(DAI)=1,
	    PDREF=0
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
	if (original_coef == coef)
		return 0;
	else {
		cs_vendor_coef_set(codec, CS421X_IDX_SPK_CTL, coef);
		return 1;
	}
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
		    GPIO or SENSE_B forced - disconnect the DMIC pin.
		*/
		def_conf = snd_hda_codec_get_pincfg(codec, CS421X_DMIC_PIN_NID);
		def_conf &= ~AC_DEFCFG_PORT_CONN;
		def_conf |= (AC_JACK_PORT_NONE << AC_DEFCFG_PORT_CONN_SHIFT);
		snd_hda_codec_set_pincfg(codec, CS421X_DMIC_PIN_NID, def_conf);
	}
}

static void cs4210_spdif_automute(struct hda_codec *codec,
				  struct hda_jack_tbl *tbl)
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
	if (spdif_present)
		snd_hda_set_pin_ctl(codec, spdif_pin,
				    spdif_present ? PIN_OUT : 0);

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
							    SPDIF_EVENT,
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

static int cs421x_build_controls(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;

	if (spec->gen.autocfg.speaker_outs &&
	    spec->vendor_nid == CS4210_VENDOR_NID) {
		err = snd_hda_ctl_add(codec, 0,
			snd_ctl_new1(&cs421x_speaker_boost_ctl, codec));
		if (err < 0)
			return err;
	}
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
	return 0;
}

#ifdef CONFIG_PM
/*
	Manage PDREF, when transitioning to D3hot
	(DAC,ADC) -> D3, PDREF=1, AFG->D3
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
	.build_controls = cs421x_build_controls,
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

	spec->gen.automute_hook = cs_automute;

	snd_hda_pick_fixup(codec, cs421x_models, cs421x_fixup_tbl,
			   cs421x_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/*
	    Update the GPIO/DMIC/SENSE_B pinmux before the configuration
	    is auto-parsed. If GPIO or SENSE_B is forced, DMIC input
	    is disabled.
	*/
	cs4210_pinmux_init(codec);

	err = cs421x_parse_auto_config(codec);
	if (err < 0)
		goto error;

	codec->patch_ops = cs421x_patch_ops;

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

	err = cs421x_parse_auto_config(codec);
	if (err < 0)
		goto error;

	codec->patch_ops = cs421x_patch_ops;
	return 0;

 error:
	cs_free(codec);
	return err;
}


/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_cirrus[] = {
	{ .id = 0x10134206, .name = "CS4206", .patch = patch_cs420x },
	{ .id = 0x10134207, .name = "CS4207", .patch = patch_cs420x },
	{ .id = 0x10134210, .name = "CS4210", .patch = patch_cs4210 },
	{ .id = 0x10134213, .name = "CS4213", .patch = patch_cs4213 },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:10134206");
MODULE_ALIAS("snd-hda-codec-id:10134207");
MODULE_ALIAS("snd-hda-codec-id:10134210");
MODULE_ALIAS("snd-hda-codec-id:10134213");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cirrus Logic HD-audio codec");

static struct hda_codec_preset_list cirrus_list = {
	.preset = snd_hda_preset_cirrus,
	.owner = THIS_MODULE,
};

static int __init patch_cirrus_init(void)
{
	return snd_hda_add_codec_preset(&cirrus_list);
}

static void __exit patch_cirrus_exit(void)
{
	snd_hda_delete_codec_preset(&cirrus_list);
}

module_init(patch_cirrus_init)
module_exit(patch_cirrus_exit)

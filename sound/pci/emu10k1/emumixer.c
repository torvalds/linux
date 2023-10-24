// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>,
 *                   Takashi Iwai <tiwai@suse.de>
 *                   Lee Revell <rlrevell@joe-job.com>
 *                   James Courtier-Dutton <James@superbug.co.uk>
 *                   Oswald Buddenhagen <oswald.buddenhagen@gmx.de>
 *                   Creative Labs, Inc.
 *
 *  Routines for control of EMU10K1 chips / mixer routines
 */

#include <linux/time.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/emu10k1.h>
#include <linux/delay.h>
#include <sound/tlv.h>

#include "p17v.h"

#define AC97_ID_STAC9758	0x83847658

static const DECLARE_TLV_DB_SCALE(snd_audigy_db_scale2, -10350, 50, 1); /* WM8775 gain scale */


static int add_ctls(struct snd_emu10k1 *emu, const struct snd_kcontrol_new *tpl,
		    const char * const *ctls, unsigned nctls)
{
	struct snd_kcontrol_new kctl = *tpl;
	int err;

	for (unsigned i = 0; i < nctls; i++) {
		kctl.name = ctls[i];
		kctl.private_value = i;
		err = snd_ctl_add(emu->card, snd_ctl_new1(&kctl, emu));
		if (err < 0)
			return err;
	}
	return 0;
}


static int snd_emu10k1_spdif_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_emu10k1_spdif_get(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	/* Limit: emu->spdif_bits */
	if (idx >= 3)
		return -EINVAL;
	ucontrol->value.iec958.status[0] = (emu->spdif_bits[idx] >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (emu->spdif_bits[idx] >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (emu->spdif_bits[idx] >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (emu->spdif_bits[idx] >> 24) & 0xff;
	return 0;
}

static int snd_emu10k1_spdif_get_mask(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
	return 0;
}

#define PAIR_PS(base, one, two, sfx) base " " one sfx, base " " two sfx
#define LR_PS(base, sfx) PAIR_PS(base, "Left", "Right", sfx)

#define ADAT_PS(pfx, sfx) \
	pfx "ADAT 0" sfx, pfx "ADAT 1" sfx, pfx "ADAT 2" sfx, pfx "ADAT 3" sfx, \
	pfx "ADAT 4" sfx, pfx "ADAT 5" sfx, pfx "ADAT 6" sfx, pfx "ADAT 7" sfx

#define PAIR_REGS(base, one, two) \
	base ## one ## 1, \
	base ## two ## 1

#define LR_REGS(base) PAIR_REGS(base, _LEFT, _RIGHT)

#define ADAT_REGS(base) \
	base+0, base+1, base+2, base+3, base+4, base+5, base+6, base+7

/*
 * List of data sources available for each destination
 */

#define DSP_TEXTS \
	"DSP 0", "DSP 1", "DSP 2", "DSP 3", "DSP 4", "DSP 5", "DSP 6", "DSP 7", \
	"DSP 8", "DSP 9", "DSP 10", "DSP 11", "DSP 12", "DSP 13", "DSP 14", "DSP 15", \
	"DSP 16", "DSP 17", "DSP 18", "DSP 19", "DSP 20", "DSP 21", "DSP 22", "DSP 23", \
	"DSP 24", "DSP 25", "DSP 26", "DSP 27", "DSP 28", "DSP 29", "DSP 30", "DSP 31"

#define PAIR_TEXTS(base, one, two) PAIR_PS(base, one, two, "")
#define LR_TEXTS(base) LR_PS(base, "")
#define ADAT_TEXTS(pfx) ADAT_PS(pfx, "")

#define EMU32_SRC_REGS \
	EMU_SRC_ALICE_EMU32A, \
	EMU_SRC_ALICE_EMU32A+1, \
	EMU_SRC_ALICE_EMU32A+2, \
	EMU_SRC_ALICE_EMU32A+3, \
	EMU_SRC_ALICE_EMU32A+4, \
	EMU_SRC_ALICE_EMU32A+5, \
	EMU_SRC_ALICE_EMU32A+6, \
	EMU_SRC_ALICE_EMU32A+7, \
	EMU_SRC_ALICE_EMU32A+8, \
	EMU_SRC_ALICE_EMU32A+9, \
	EMU_SRC_ALICE_EMU32A+0xa, \
	EMU_SRC_ALICE_EMU32A+0xb, \
	EMU_SRC_ALICE_EMU32A+0xc, \
	EMU_SRC_ALICE_EMU32A+0xd, \
	EMU_SRC_ALICE_EMU32A+0xe, \
	EMU_SRC_ALICE_EMU32A+0xf, \
	EMU_SRC_ALICE_EMU32B, \
	EMU_SRC_ALICE_EMU32B+1, \
	EMU_SRC_ALICE_EMU32B+2, \
	EMU_SRC_ALICE_EMU32B+3, \
	EMU_SRC_ALICE_EMU32B+4, \
	EMU_SRC_ALICE_EMU32B+5, \
	EMU_SRC_ALICE_EMU32B+6, \
	EMU_SRC_ALICE_EMU32B+7, \
	EMU_SRC_ALICE_EMU32B+8, \
	EMU_SRC_ALICE_EMU32B+9, \
	EMU_SRC_ALICE_EMU32B+0xa, \
	EMU_SRC_ALICE_EMU32B+0xb, \
	EMU_SRC_ALICE_EMU32B+0xc, \
	EMU_SRC_ALICE_EMU32B+0xd, \
	EMU_SRC_ALICE_EMU32B+0xe, \
	EMU_SRC_ALICE_EMU32B+0xf

/* 1010 rev1 */

#define EMU1010_COMMON_TEXTS \
	"Silence", \
	PAIR_TEXTS("Dock Mic", "A", "B"), \
	LR_TEXTS("Dock ADC1"), \
	LR_TEXTS("Dock ADC2"), \
	LR_TEXTS("Dock ADC3"), \
	LR_TEXTS("0202 ADC"), \
	LR_TEXTS("1010 SPDIF"), \
	ADAT_TEXTS("1010 ")

static const char * const emu1010_src_texts[] = {
	EMU1010_COMMON_TEXTS,
	DSP_TEXTS,
};

static const unsigned short emu1010_src_regs[] = {
	EMU_SRC_SILENCE,
	PAIR_REGS(EMU_SRC_DOCK_MIC, _A, _B),
	LR_REGS(EMU_SRC_DOCK_ADC1),
	LR_REGS(EMU_SRC_DOCK_ADC2),
	LR_REGS(EMU_SRC_DOCK_ADC3),
	LR_REGS(EMU_SRC_HAMOA_ADC),
	LR_REGS(EMU_SRC_HANA_SPDIF),
	ADAT_REGS(EMU_SRC_HANA_ADAT),
	EMU32_SRC_REGS,
};
static_assert(ARRAY_SIZE(emu1010_src_regs) == ARRAY_SIZE(emu1010_src_texts));

/* 1010 rev2 */

#define EMU1010b_COMMON_TEXTS \
	"Silence", \
	PAIR_TEXTS("Dock Mic", "A", "B"), \
	LR_TEXTS("Dock ADC1"), \
	LR_TEXTS("Dock ADC2"), \
	LR_TEXTS("0202 ADC"), \
	LR_TEXTS("Dock SPDIF"), \
	LR_TEXTS("1010 SPDIF"), \
	ADAT_TEXTS("Dock "), \
	ADAT_TEXTS("1010 ")

static const char * const emu1010b_src_texts[] = {
	EMU1010b_COMMON_TEXTS,
	DSP_TEXTS,
};

static const unsigned short emu1010b_src_regs[] = {
	EMU_SRC_SILENCE,
	PAIR_REGS(EMU_SRC_DOCK_MIC, _A, _B),
	LR_REGS(EMU_SRC_DOCK_ADC1),
	LR_REGS(EMU_SRC_DOCK_ADC2),
	LR_REGS(EMU_SRC_HAMOA_ADC),
	LR_REGS(EMU_SRC_MDOCK_SPDIF),
	LR_REGS(EMU_SRC_HANA_SPDIF),
	ADAT_REGS(EMU_SRC_MDOCK_ADAT),
	ADAT_REGS(EMU_SRC_HANA_ADAT),
	EMU32_SRC_REGS,
};
static_assert(ARRAY_SIZE(emu1010b_src_regs) == ARRAY_SIZE(emu1010b_src_texts));

/* 1616(m) cardbus */

#define EMU1616_COMMON_TEXTS \
	"Silence", \
	PAIR_TEXTS("Mic", "A", "B"), \
	LR_TEXTS("ADC1"), \
	LR_TEXTS("ADC2"), \
	LR_TEXTS("SPDIF"), \
	ADAT_TEXTS("")

static const char * const emu1616_src_texts[] = {
	EMU1616_COMMON_TEXTS,
	DSP_TEXTS,
};

static const unsigned short emu1616_src_regs[] = {
	EMU_SRC_SILENCE,
	PAIR_REGS(EMU_SRC_DOCK_MIC, _A, _B),
	LR_REGS(EMU_SRC_DOCK_ADC1),
	LR_REGS(EMU_SRC_DOCK_ADC2),
	LR_REGS(EMU_SRC_MDOCK_SPDIF),
	ADAT_REGS(EMU_SRC_MDOCK_ADAT),
	EMU32_SRC_REGS,
};
static_assert(ARRAY_SIZE(emu1616_src_regs) == ARRAY_SIZE(emu1616_src_texts));

/* 0404 rev1 & rev2 */

#define EMU0404_COMMON_TEXTS \
	"Silence", \
	LR_TEXTS("ADC"), \
	LR_TEXTS("SPDIF")

static const char * const emu0404_src_texts[] = {
	EMU0404_COMMON_TEXTS,
	DSP_TEXTS,
};

static const unsigned short emu0404_src_regs[] = {
	EMU_SRC_SILENCE,
	LR_REGS(EMU_SRC_HAMOA_ADC),
	LR_REGS(EMU_SRC_HANA_SPDIF),
	EMU32_SRC_REGS,
};
static_assert(ARRAY_SIZE(emu0404_src_regs) == ARRAY_SIZE(emu0404_src_texts));

/*
 * Data destinations - physical EMU outputs.
 * Each destination has an enum mixer control to choose a data source
 */

#define LR_CTLS(base) LR_PS(base, " Playback Enum")
#define ADAT_CTLS(pfx) ADAT_PS(pfx, " Playback Enum")

/* 1010 rev1 */

static const char * const emu1010_output_texts[] = {
	LR_CTLS("Dock DAC1"),
	LR_CTLS("Dock DAC2"),
	LR_CTLS("Dock DAC3"),
	LR_CTLS("Dock DAC4"),
	LR_CTLS("Dock Phones"),
	LR_CTLS("Dock SPDIF"),
	LR_CTLS("0202 DAC"),
	LR_CTLS("1010 SPDIF"),
	ADAT_CTLS("1010 "),
};
static_assert(ARRAY_SIZE(emu1010_output_texts) <= NUM_OUTPUT_DESTS);

static const unsigned short emu1010_output_dst[] = {
	LR_REGS(EMU_DST_DOCK_DAC1),
	LR_REGS(EMU_DST_DOCK_DAC2),
	LR_REGS(EMU_DST_DOCK_DAC3),
	LR_REGS(EMU_DST_DOCK_DAC4),
	LR_REGS(EMU_DST_DOCK_PHONES),
	LR_REGS(EMU_DST_DOCK_SPDIF),
	LR_REGS(EMU_DST_HAMOA_DAC),
	LR_REGS(EMU_DST_HANA_SPDIF),
	ADAT_REGS(EMU_DST_HANA_ADAT),
};
static_assert(ARRAY_SIZE(emu1010_output_dst) == ARRAY_SIZE(emu1010_output_texts));

static const unsigned short emu1010_output_dflt[] = {
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+2, EMU_SRC_ALICE_EMU32A+3,
	EMU_SRC_ALICE_EMU32A+4, EMU_SRC_ALICE_EMU32A+5,
	EMU_SRC_ALICE_EMU32A+6, EMU_SRC_ALICE_EMU32A+7,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1, EMU_SRC_ALICE_EMU32A+2, EMU_SRC_ALICE_EMU32A+3,
	EMU_SRC_ALICE_EMU32A+4, EMU_SRC_ALICE_EMU32A+5, EMU_SRC_ALICE_EMU32A+6, EMU_SRC_ALICE_EMU32A+7,
};
static_assert(ARRAY_SIZE(emu1010_output_dflt) == ARRAY_SIZE(emu1010_output_dst));

/* 1010 rev2 */

static const char * const snd_emu1010b_output_texts[] = {
	LR_CTLS("Dock DAC1"),
	LR_CTLS("Dock DAC2"),
	LR_CTLS("Dock DAC3"),
	LR_CTLS("Dock SPDIF"),
	ADAT_CTLS("Dock "),
	LR_CTLS("0202 DAC"),
	LR_CTLS("1010 SPDIF"),
	ADAT_CTLS("1010 "),
};
static_assert(ARRAY_SIZE(snd_emu1010b_output_texts) <= NUM_OUTPUT_DESTS);

static const unsigned short emu1010b_output_dst[] = {
	LR_REGS(EMU_DST_DOCK_DAC1),
	LR_REGS(EMU_DST_DOCK_DAC2),
	LR_REGS(EMU_DST_DOCK_DAC3),
	LR_REGS(EMU_DST_MDOCK_SPDIF),
	ADAT_REGS(EMU_DST_MDOCK_ADAT),
	LR_REGS(EMU_DST_HAMOA_DAC),
	LR_REGS(EMU_DST_HANA_SPDIF),
	ADAT_REGS(EMU_DST_HANA_ADAT),
};
static_assert(ARRAY_SIZE(emu1010b_output_dst) == ARRAY_SIZE(snd_emu1010b_output_texts));

static const unsigned short emu1010b_output_dflt[] = {
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+2, EMU_SRC_ALICE_EMU32A+3,
	EMU_SRC_ALICE_EMU32A+4, EMU_SRC_ALICE_EMU32A+5,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1, EMU_SRC_ALICE_EMU32A+2, EMU_SRC_ALICE_EMU32A+3,
	EMU_SRC_ALICE_EMU32A+4, EMU_SRC_ALICE_EMU32A+5, EMU_SRC_ALICE_EMU32A+6, EMU_SRC_ALICE_EMU32A+7,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1, EMU_SRC_ALICE_EMU32A+2, EMU_SRC_ALICE_EMU32A+3,
	EMU_SRC_ALICE_EMU32A+4, EMU_SRC_ALICE_EMU32A+5, EMU_SRC_ALICE_EMU32A+6, EMU_SRC_ALICE_EMU32A+7,
};

/* 1616(m) cardbus */

static const char * const snd_emu1616_output_texts[] = {
	LR_CTLS("Dock DAC1"),
	LR_CTLS("Dock DAC2"),
	LR_CTLS("Dock DAC3"),
	LR_CTLS("Dock SPDIF"),
	ADAT_CTLS("Dock "),
	LR_CTLS("Mana DAC"),
};
static_assert(ARRAY_SIZE(snd_emu1616_output_texts) <= NUM_OUTPUT_DESTS);

static const unsigned short emu1616_output_dst[] = {
	LR_REGS(EMU_DST_DOCK_DAC1),
	LR_REGS(EMU_DST_DOCK_DAC2),
	LR_REGS(EMU_DST_DOCK_DAC3),
	LR_REGS(EMU_DST_MDOCK_SPDIF),
	ADAT_REGS(EMU_DST_MDOCK_ADAT),
	EMU_DST_MANA_DAC_LEFT, EMU_DST_MANA_DAC_RIGHT,
};
static_assert(ARRAY_SIZE(emu1616_output_dst) == ARRAY_SIZE(snd_emu1616_output_texts));

static const unsigned short emu1616_output_dflt[] = {
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+2, EMU_SRC_ALICE_EMU32A+3,
	EMU_SRC_ALICE_EMU32A+4, EMU_SRC_ALICE_EMU32A+5,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1, EMU_SRC_ALICE_EMU32A+2, EMU_SRC_ALICE_EMU32A+3,
	EMU_SRC_ALICE_EMU32A+4, EMU_SRC_ALICE_EMU32A+5, EMU_SRC_ALICE_EMU32A+6, EMU_SRC_ALICE_EMU32A+7,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
};
static_assert(ARRAY_SIZE(emu1616_output_dflt) == ARRAY_SIZE(emu1616_output_dst));

/* 0404 rev1 & rev2 */

static const char * const snd_emu0404_output_texts[] = {
	LR_CTLS("DAC"),
	LR_CTLS("SPDIF"),
};
static_assert(ARRAY_SIZE(snd_emu0404_output_texts) <= NUM_OUTPUT_DESTS);

static const unsigned short emu0404_output_dst[] = {
	LR_REGS(EMU_DST_HAMOA_DAC),
	LR_REGS(EMU_DST_HANA_SPDIF),
};
static_assert(ARRAY_SIZE(emu0404_output_dst) == ARRAY_SIZE(snd_emu0404_output_texts));

static const unsigned short emu0404_output_dflt[] = {
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+0, EMU_SRC_ALICE_EMU32A+1,
};
static_assert(ARRAY_SIZE(emu0404_output_dflt) == ARRAY_SIZE(emu0404_output_dst));

/*
 * Data destinations - FPGA outputs going to Alice2 (Audigy) for
 *   capture (EMU32 + I2S links)
 * Each destination has an enum mixer control to choose a data source
 */

static const char * const emu1010_input_texts[] = {
	"DSP 0 Capture Enum",
	"DSP 1 Capture Enum",
	"DSP 2 Capture Enum",
	"DSP 3 Capture Enum",
	"DSP 4 Capture Enum",
	"DSP 5 Capture Enum",
	"DSP 6 Capture Enum",
	"DSP 7 Capture Enum",
	"DSP 8 Capture Enum",
	"DSP 9 Capture Enum",
	"DSP A Capture Enum",
	"DSP B Capture Enum",
	"DSP C Capture Enum",
	"DSP D Capture Enum",
	"DSP E Capture Enum",
	"DSP F Capture Enum",
	/* These exist only on rev1 EMU1010 cards. */
	"DSP 10 Capture Enum",
	"DSP 11 Capture Enum",
	"DSP 12 Capture Enum",
	"DSP 13 Capture Enum",
	"DSP 14 Capture Enum",
	"DSP 15 Capture Enum",
};
static_assert(ARRAY_SIZE(emu1010_input_texts) <= NUM_INPUT_DESTS);

static const unsigned short emu1010_input_dst[] = {
	EMU_DST_ALICE2_EMU32_0,
	EMU_DST_ALICE2_EMU32_1,
	EMU_DST_ALICE2_EMU32_2,
	EMU_DST_ALICE2_EMU32_3,
	EMU_DST_ALICE2_EMU32_4,
	EMU_DST_ALICE2_EMU32_5,
	EMU_DST_ALICE2_EMU32_6,
	EMU_DST_ALICE2_EMU32_7,
	EMU_DST_ALICE2_EMU32_8,
	EMU_DST_ALICE2_EMU32_9,
	EMU_DST_ALICE2_EMU32_A,
	EMU_DST_ALICE2_EMU32_B,
	EMU_DST_ALICE2_EMU32_C,
	EMU_DST_ALICE2_EMU32_D,
	EMU_DST_ALICE2_EMU32_E,
	EMU_DST_ALICE2_EMU32_F,
	/* These exist only on rev1 EMU1010 cards. */
	EMU_DST_ALICE_I2S0_LEFT,
	EMU_DST_ALICE_I2S0_RIGHT,
	EMU_DST_ALICE_I2S1_LEFT,
	EMU_DST_ALICE_I2S1_RIGHT,
	EMU_DST_ALICE_I2S2_LEFT,
	EMU_DST_ALICE_I2S2_RIGHT,
};
static_assert(ARRAY_SIZE(emu1010_input_dst) == ARRAY_SIZE(emu1010_input_texts));

static const unsigned short emu1010_input_dflt[] = {
	EMU_SRC_DOCK_MIC_A1,
	EMU_SRC_DOCK_MIC_B1,
	EMU_SRC_HAMOA_ADC_LEFT1,
	EMU_SRC_HAMOA_ADC_RIGHT1,
	EMU_SRC_DOCK_ADC1_LEFT1,
	EMU_SRC_DOCK_ADC1_RIGHT1,
	EMU_SRC_DOCK_ADC2_LEFT1,
	EMU_SRC_DOCK_ADC2_RIGHT1,
	/* Pavel Hofman - setting defaults for all capture channels.
	 * Defaults only, users will set their own values anyways, let's
	 * just copy/paste. */
	EMU_SRC_DOCK_MIC_A1,
	EMU_SRC_DOCK_MIC_B1,
	EMU_SRC_HAMOA_ADC_LEFT1,
	EMU_SRC_HAMOA_ADC_RIGHT1,
	EMU_SRC_DOCK_ADC1_LEFT1,
	EMU_SRC_DOCK_ADC1_RIGHT1,
	EMU_SRC_DOCK_ADC2_LEFT1,
	EMU_SRC_DOCK_ADC2_RIGHT1,

	EMU_SRC_DOCK_ADC1_LEFT1,
	EMU_SRC_DOCK_ADC1_RIGHT1,
	EMU_SRC_DOCK_ADC2_LEFT1,
	EMU_SRC_DOCK_ADC2_RIGHT1,
	EMU_SRC_DOCK_ADC3_LEFT1,
	EMU_SRC_DOCK_ADC3_RIGHT1,
};
static_assert(ARRAY_SIZE(emu1010_input_dflt) == ARRAY_SIZE(emu1010_input_dst));

static const unsigned short emu0404_input_dflt[] = {
	EMU_SRC_HAMOA_ADC_LEFT1,
	EMU_SRC_HAMOA_ADC_RIGHT1,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
	EMU_SRC_HANA_SPDIF_LEFT1,
	EMU_SRC_HANA_SPDIF_RIGHT1,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
	EMU_SRC_SILENCE,
};

struct snd_emu1010_routing_info {
	const char * const *src_texts;
	const char * const *out_texts;
	const unsigned short *src_regs;
	const unsigned short *out_regs;
	const unsigned short *in_regs;
	const unsigned short *out_dflts;
	const unsigned short *in_dflts;
	unsigned n_srcs;
	unsigned n_outs;
	unsigned n_ins;
};

static const struct snd_emu1010_routing_info emu1010_routing_info[] = {
	{
		/* rev1 1010 */
		.src_regs = emu1010_src_regs,
		.src_texts = emu1010_src_texts,
		.n_srcs = ARRAY_SIZE(emu1010_src_texts),

		.out_dflts = emu1010_output_dflt,
		.out_regs = emu1010_output_dst,
		.out_texts = emu1010_output_texts,
		.n_outs = ARRAY_SIZE(emu1010_output_dst),

		.in_dflts = emu1010_input_dflt,
		.in_regs = emu1010_input_dst,
		.n_ins = ARRAY_SIZE(emu1010_input_dst),
	},
	{
		/* rev2 1010 */
		.src_regs = emu1010b_src_regs,
		.src_texts = emu1010b_src_texts,
		.n_srcs = ARRAY_SIZE(emu1010b_src_texts),

		.out_dflts = emu1010b_output_dflt,
		.out_regs = emu1010b_output_dst,
		.out_texts = snd_emu1010b_output_texts,
		.n_outs = ARRAY_SIZE(emu1010b_output_dst),

		.in_dflts = emu1010_input_dflt,
		.in_regs = emu1010_input_dst,
		.n_ins = ARRAY_SIZE(emu1010_input_dst) - 6,
	},
	{
		/* 1616(m) cardbus */
		.src_regs = emu1616_src_regs,
		.src_texts = emu1616_src_texts,
		.n_srcs = ARRAY_SIZE(emu1616_src_texts),

		.out_dflts = emu1616_output_dflt,
		.out_regs = emu1616_output_dst,
		.out_texts = snd_emu1616_output_texts,
		.n_outs = ARRAY_SIZE(emu1616_output_dst),

		.in_dflts = emu1010_input_dflt,
		.in_regs = emu1010_input_dst,
		.n_ins = ARRAY_SIZE(emu1010_input_dst) - 6,
	},
	{
		/* 0404 */
		.src_regs = emu0404_src_regs,
		.src_texts = emu0404_src_texts,
		.n_srcs = ARRAY_SIZE(emu0404_src_texts),

		.out_dflts = emu0404_output_dflt,
		.out_regs = emu0404_output_dst,
		.out_texts = snd_emu0404_output_texts,
		.n_outs = ARRAY_SIZE(emu0404_output_dflt),

		.in_dflts = emu0404_input_dflt,
		.in_regs = emu1010_input_dst,
		.n_ins = ARRAY_SIZE(emu1010_input_dst) - 6,
	},
};

static unsigned emu1010_idx(struct snd_emu10k1 *emu)
{
	return emu->card_capabilities->emu_model - 1;
}

static void snd_emu1010_output_source_apply(struct snd_emu10k1 *emu,
					    int channel, int src)
{
	const struct snd_emu1010_routing_info *emu_ri =
		&emu1010_routing_info[emu1010_idx(emu)];

	snd_emu1010_fpga_link_dst_src_write(emu,
		emu_ri->out_regs[channel], emu_ri->src_regs[src]);
}

static void snd_emu1010_input_source_apply(struct snd_emu10k1 *emu,
					   int channel, int src)
{
	const struct snd_emu1010_routing_info *emu_ri =
		&emu1010_routing_info[emu1010_idx(emu)];

	snd_emu1010_fpga_link_dst_src_write(emu,
		emu_ri->in_regs[channel], emu_ri->src_regs[src]);
}

static void snd_emu1010_apply_sources(struct snd_emu10k1 *emu)
{
	const struct snd_emu1010_routing_info *emu_ri =
		&emu1010_routing_info[emu1010_idx(emu)];

	for (unsigned i = 0; i < emu_ri->n_outs; i++)
		snd_emu1010_output_source_apply(
			emu, i, emu->emu1010.output_source[i]);
	for (unsigned i = 0; i < emu_ri->n_ins; i++)
		snd_emu1010_input_source_apply(
			emu, i, emu->emu1010.input_source[i]);
}

static u8 emu1010_map_source(const struct snd_emu1010_routing_info *emu_ri,
			     unsigned val)
{
	for (unsigned i = 0; i < emu_ri->n_srcs; i++)
		if (val == emu_ri->src_regs[i])
			return i;
	return 0;
}

static int snd_emu1010_input_output_source_info(struct snd_kcontrol *kcontrol,
						struct snd_ctl_elem_info *uinfo)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	const struct snd_emu1010_routing_info *emu_ri =
		&emu1010_routing_info[emu1010_idx(emu)];

	return snd_ctl_enum_info(uinfo, 1, emu_ri->n_srcs, emu_ri->src_texts);
}

static int snd_emu1010_output_source_get(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	const struct snd_emu1010_routing_info *emu_ri =
		&emu1010_routing_info[emu1010_idx(emu)];
	unsigned channel = kcontrol->private_value;

	if (channel >= emu_ri->n_outs)
		return -EINVAL;
	ucontrol->value.enumerated.item[0] = emu->emu1010.output_source[channel];
	return 0;
}

static int snd_emu1010_output_source_put(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	const struct snd_emu1010_routing_info *emu_ri =
		&emu1010_routing_info[emu1010_idx(emu)];
	unsigned val = ucontrol->value.enumerated.item[0];
	unsigned channel = kcontrol->private_value;
	int change;

	if (val >= emu_ri->n_srcs)
		return -EINVAL;
	if (channel >= emu_ri->n_outs)
		return -EINVAL;
	change = (emu->emu1010.output_source[channel] != val);
	if (change) {
		emu->emu1010.output_source[channel] = val;
		snd_emu1010_output_source_apply(emu, channel, val);
	}
	return change;
}

static const struct snd_kcontrol_new emu1010_output_source_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_emu1010_input_output_source_info,
	.get = snd_emu1010_output_source_get,
	.put = snd_emu1010_output_source_put
};

static int snd_emu1010_input_source_get(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	const struct snd_emu1010_routing_info *emu_ri =
		&emu1010_routing_info[emu1010_idx(emu)];
	unsigned channel = kcontrol->private_value;

	if (channel >= emu_ri->n_ins)
		return -EINVAL;
	ucontrol->value.enumerated.item[0] = emu->emu1010.input_source[channel];
	return 0;
}

static int snd_emu1010_input_source_put(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	const struct snd_emu1010_routing_info *emu_ri =
		&emu1010_routing_info[emu1010_idx(emu)];
	unsigned val = ucontrol->value.enumerated.item[0];
	unsigned channel = kcontrol->private_value;
	int change;

	if (val >= emu_ri->n_srcs)
		return -EINVAL;
	if (channel >= emu_ri->n_ins)
		return -EINVAL;
	change = (emu->emu1010.input_source[channel] != val);
	if (change) {
		emu->emu1010.input_source[channel] = val;
		snd_emu1010_input_source_apply(emu, channel, val);
	}
	return change;
}

static const struct snd_kcontrol_new emu1010_input_source_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_emu1010_input_output_source_info,
	.get = snd_emu1010_input_source_get,
	.put = snd_emu1010_input_source_put
};

static int add_emu1010_source_mixers(struct snd_emu10k1 *emu)
{
	const struct snd_emu1010_routing_info *emu_ri =
		&emu1010_routing_info[emu1010_idx(emu)];
	int err;

	err = add_ctls(emu, &emu1010_output_source_ctl,
		       emu_ri->out_texts, emu_ri->n_outs);
	if (err < 0)
		return err;
	err = add_ctls(emu, &emu1010_input_source_ctl,
		       emu1010_input_texts, emu_ri->n_ins);
	return err;
}


static const char * const snd_emu1010_adc_pads[] = {
	"ADC1 14dB PAD 0202 Capture Switch",
	"ADC1 14dB PAD Audio Dock Capture Switch",
	"ADC2 14dB PAD Audio Dock Capture Switch",
	"ADC3 14dB PAD Audio Dock Capture Switch",
};

static const unsigned short snd_emu1010_adc_pad_regs[] = {
	EMU_HANA_0202_ADC_PAD1,
	EMU_HANA_DOCK_ADC_PAD1,
	EMU_HANA_DOCK_ADC_PAD2,
	EMU_HANA_DOCK_ADC_PAD3,
};

#define snd_emu1010_adc_pads_info	snd_ctl_boolean_mono_info

static int snd_emu1010_adc_pads_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int mask = snd_emu1010_adc_pad_regs[kcontrol->private_value];

	ucontrol->value.integer.value[0] = (emu->emu1010.adc_pads & mask) ? 1 : 0;
	return 0;
}

static int snd_emu1010_adc_pads_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int mask = snd_emu1010_adc_pad_regs[kcontrol->private_value];
	unsigned int val, cache;
	int change;

	val = ucontrol->value.integer.value[0];
	cache = emu->emu1010.adc_pads;
	if (val == 1) 
		cache = cache | mask;
	else
		cache = cache & ~mask;
	change = (cache != emu->emu1010.adc_pads);
	if (change) {
		snd_emu1010_fpga_write(emu, EMU_HANA_ADC_PADS, cache );
	        emu->emu1010.adc_pads = cache;
	}

	return change;
}

static const struct snd_kcontrol_new emu1010_adc_pads_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_emu1010_adc_pads_info,
	.get = snd_emu1010_adc_pads_get,
	.put = snd_emu1010_adc_pads_put
};


static const char * const snd_emu1010_dac_pads[] = {
	"DAC1 0202 14dB PAD Playback Switch",
	"DAC1 Audio Dock 14dB PAD Playback Switch",
	"DAC2 Audio Dock 14dB PAD Playback Switch",
	"DAC3 Audio Dock 14dB PAD Playback Switch",
	"DAC4 Audio Dock 14dB PAD Playback Switch",
};

static const unsigned short snd_emu1010_dac_regs[] = {
	EMU_HANA_0202_DAC_PAD1,
	EMU_HANA_DOCK_DAC_PAD1,
	EMU_HANA_DOCK_DAC_PAD2,
	EMU_HANA_DOCK_DAC_PAD3,
	EMU_HANA_DOCK_DAC_PAD4,
};

#define snd_emu1010_dac_pads_info	snd_ctl_boolean_mono_info

static int snd_emu1010_dac_pads_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int mask = snd_emu1010_dac_regs[kcontrol->private_value];

	ucontrol->value.integer.value[0] = (emu->emu1010.dac_pads & mask) ? 1 : 0;
	return 0;
}

static int snd_emu1010_dac_pads_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int mask = snd_emu1010_dac_regs[kcontrol->private_value];
	unsigned int val, cache;
	int change;

	val = ucontrol->value.integer.value[0];
	cache = emu->emu1010.dac_pads;
	if (val == 1) 
		cache = cache | mask;
	else
		cache = cache & ~mask;
	change = (cache != emu->emu1010.dac_pads);
	if (change) {
		snd_emu1010_fpga_write(emu, EMU_HANA_DAC_PADS, cache );
	        emu->emu1010.dac_pads = cache;
	}

	return change;
}

static const struct snd_kcontrol_new emu1010_dac_pads_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_emu1010_dac_pads_info,
	.get = snd_emu1010_dac_pads_get,
	.put = snd_emu1010_dac_pads_put
};


struct snd_emu1010_pads_info {
	const char * const *adc_ctls, * const *dac_ctls;
	unsigned n_adc_ctls, n_dac_ctls;
};

static const struct snd_emu1010_pads_info emu1010_pads_info[] = {
	{
		/* rev1 1010 */
		.adc_ctls = snd_emu1010_adc_pads,
		.n_adc_ctls = ARRAY_SIZE(snd_emu1010_adc_pads),
		.dac_ctls = snd_emu1010_dac_pads,
		.n_dac_ctls = ARRAY_SIZE(snd_emu1010_dac_pads),
	},
	{
		/* rev2 1010 */
		.adc_ctls = snd_emu1010_adc_pads,
		.n_adc_ctls = ARRAY_SIZE(snd_emu1010_adc_pads) - 1,
		.dac_ctls = snd_emu1010_dac_pads,
		.n_dac_ctls = ARRAY_SIZE(snd_emu1010_dac_pads) - 1,
	},
	{
		/* 1616(m) cardbus */
		.adc_ctls = snd_emu1010_adc_pads + 1,
		.n_adc_ctls = ARRAY_SIZE(snd_emu1010_adc_pads) - 2,
		.dac_ctls = snd_emu1010_dac_pads + 1,
		.n_dac_ctls = ARRAY_SIZE(snd_emu1010_dac_pads) - 2,
	},
	{
		/* 0404 */
		.adc_ctls = NULL,
		.n_adc_ctls = 0,
		.dac_ctls = NULL,
		.n_dac_ctls = 0,
	},
};

static const char * const emu1010_clock_texts[] = {
	"44100", "48000", "SPDIF", "ADAT", "Dock", "BNC"
};

static const u8 emu1010_clock_vals[] = {
	EMU_HANA_WCLOCK_INT_44_1K,
	EMU_HANA_WCLOCK_INT_48K,
	EMU_HANA_WCLOCK_HANA_SPDIF_IN,
	EMU_HANA_WCLOCK_HANA_ADAT_IN,
	EMU_HANA_WCLOCK_2ND_HANA,
	EMU_HANA_WCLOCK_SYNC_BNC,
};

static const char * const emu0404_clock_texts[] = {
	"44100", "48000", "SPDIF", "BNC"
};

static const u8 emu0404_clock_vals[] = {
	EMU_HANA_WCLOCK_INT_44_1K,
	EMU_HANA_WCLOCK_INT_48K,
	EMU_HANA_WCLOCK_HANA_SPDIF_IN,
	EMU_HANA_WCLOCK_SYNC_BNC,
};

struct snd_emu1010_clock_info {
	const char * const *texts;
	const u8 *vals;
	unsigned num;
};

static const struct snd_emu1010_clock_info emu1010_clock_info[] = {
	{
		// rev1 1010
		.texts = emu1010_clock_texts,
		.vals = emu1010_clock_vals,
		.num = ARRAY_SIZE(emu1010_clock_vals),
	},
	{
		// rev2 1010
		.texts = emu1010_clock_texts,
		.vals = emu1010_clock_vals,
		.num = ARRAY_SIZE(emu1010_clock_vals) - 1,
	},
	{
		// 1616(m) CardBus
		.texts = emu1010_clock_texts,
		// TODO: determine what is actually available.
		// Pedantically, *every* source comes from the 2nd FPGA, as the
		// card itself has no own (digital) audio ports. The user manual
		// claims that ADAT and S/PDIF clock sources are separate, which
		// can mean two things: either E-MU mapped the dock's sources to
		// the primary ones, or they determine the meaning of the "Dock"
		// source depending on how the ports are actually configured
		// (which the 2nd FPGA must be doing anyway).
		.vals = emu1010_clock_vals,
		.num = ARRAY_SIZE(emu1010_clock_vals),
	},
	{
		// 0404
		.texts = emu0404_clock_texts,
		.vals = emu0404_clock_vals,
		.num = ARRAY_SIZE(emu0404_clock_vals),
	},
};

static int snd_emu1010_clock_source_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	const struct snd_emu1010_clock_info *emu_ci =
		&emu1010_clock_info[emu1010_idx(emu)];
		
	return snd_ctl_enum_info(uinfo, 1, emu_ci->num, emu_ci->texts);
}

static int snd_emu1010_clock_source_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->emu1010.clock_source;
	return 0;
}

static int snd_emu1010_clock_source_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	const struct snd_emu1010_clock_info *emu_ci =
		&emu1010_clock_info[emu1010_idx(emu)];
	unsigned int val;
	int change = 0;

	val = ucontrol->value.enumerated.item[0] ;
	if (val >= emu_ci->num)
		return -EINVAL;
	spin_lock_irq(&emu->reg_lock);
	change = (emu->emu1010.clock_source != val);
	if (change) {
		emu->emu1010.clock_source = val;
		emu->emu1010.wclock = emu_ci->vals[val];
		snd_emu1010_update_clock(emu);

		snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_MUTE);
		snd_emu1010_fpga_write(emu, EMU_HANA_WCLOCK, emu->emu1010.wclock);
		spin_unlock_irq(&emu->reg_lock);

		msleep(10);  // Allow DLL to settle
		snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_UNMUTE);
	} else {
		spin_unlock_irq(&emu->reg_lock);
	}
	return change;
}

static const struct snd_kcontrol_new snd_emu1010_clock_source =
{
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Clock Source",
	.count = 1,
	.info = snd_emu1010_clock_source_info,
	.get = snd_emu1010_clock_source_get,
	.put = snd_emu1010_clock_source_put
};

static int snd_emu1010_clock_fallback_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = {
		"44100", "48000"
	};

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}

static int snd_emu1010_clock_fallback_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->emu1010.clock_fallback;
	return 0;
}

static int snd_emu1010_clock_fallback_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val = ucontrol->value.enumerated.item[0];
	int change;

	if (val >= 2)
		return -EINVAL;
	change = (emu->emu1010.clock_fallback != val);
	if (change) {
		emu->emu1010.clock_fallback = val;
		snd_emu1010_fpga_write(emu, EMU_HANA_DEFCLOCK, 1 - val);
	}
	return change;
}

static const struct snd_kcontrol_new snd_emu1010_clock_fallback =
{
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Clock Fallback",
	.count = 1,
	.info = snd_emu1010_clock_fallback_info,
	.get = snd_emu1010_clock_fallback_get,
	.put = snd_emu1010_clock_fallback_put
};

static int snd_emu1010_optical_out_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = {
		"SPDIF", "ADAT"
	};

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}

static int snd_emu1010_optical_out_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->emu1010.optical_out;
	return 0;
}

static int snd_emu1010_optical_out_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	u32 tmp;
	int change = 0;

	val = ucontrol->value.enumerated.item[0];
	/* Limit: uinfo->value.enumerated.items = 2; */
	if (val >= 2)
		return -EINVAL;
	change = (emu->emu1010.optical_out != val);
	if (change) {
		emu->emu1010.optical_out = val;
		tmp = (emu->emu1010.optical_in ? EMU_HANA_OPTICAL_IN_ADAT : EMU_HANA_OPTICAL_IN_SPDIF) |
			(emu->emu1010.optical_out ? EMU_HANA_OPTICAL_OUT_ADAT : EMU_HANA_OPTICAL_OUT_SPDIF);
		snd_emu1010_fpga_write(emu, EMU_HANA_OPTICAL_TYPE, tmp);
	}
	return change;
}

static const struct snd_kcontrol_new snd_emu1010_optical_out = {
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Optical Output Mode",
	.count =	1,
	.info =         snd_emu1010_optical_out_info,
	.get =          snd_emu1010_optical_out_get,
	.put =          snd_emu1010_optical_out_put
};

static int snd_emu1010_optical_in_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = {
		"SPDIF", "ADAT"
	};

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}

static int snd_emu1010_optical_in_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->emu1010.optical_in;
	return 0;
}

static int snd_emu1010_optical_in_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	u32 tmp;
	int change = 0;

	val = ucontrol->value.enumerated.item[0];
	/* Limit: uinfo->value.enumerated.items = 2; */
	if (val >= 2)
		return -EINVAL;
	change = (emu->emu1010.optical_in != val);
	if (change) {
		emu->emu1010.optical_in = val;
		tmp = (emu->emu1010.optical_in ? EMU_HANA_OPTICAL_IN_ADAT : EMU_HANA_OPTICAL_IN_SPDIF) |
			(emu->emu1010.optical_out ? EMU_HANA_OPTICAL_OUT_ADAT : EMU_HANA_OPTICAL_OUT_SPDIF);
		snd_emu1010_fpga_write(emu, EMU_HANA_OPTICAL_TYPE, tmp);
	}
	return change;
}

static const struct snd_kcontrol_new snd_emu1010_optical_in = {
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Optical Input Mode",
	.count =	1,
	.info =         snd_emu1010_optical_in_info,
	.get =          snd_emu1010_optical_in_get,
	.put =          snd_emu1010_optical_in_put
};

static int snd_audigy_i2c_capture_source_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
#if 0
	static const char * const texts[4] = {
		"Unknown1", "Unknown2", "Mic", "Line"
	};
#endif
	static const char * const texts[2] = {
		"Mic", "Line"
	};

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}

static int snd_audigy_i2c_capture_source_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->i2c_capture_source;
	return 0;
}

static int snd_audigy_i2c_capture_source_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int source_id;
	unsigned int ngain, ogain;
	u16 gpio;
	int change = 0;
	u32 source;
	/* If the capture source has changed,
	 * update the capture volume from the cached value
	 * for the particular source.
	 */
	source_id = ucontrol->value.enumerated.item[0];
	/* Limit: uinfo->value.enumerated.items = 2; */
	/*        emu->i2c_capture_volume */
	if (source_id >= 2)
		return -EINVAL;
	change = (emu->i2c_capture_source != source_id);
	if (change) {
		snd_emu10k1_i2c_write(emu, ADC_MUX, 0); /* Mute input */
		spin_lock_irq(&emu->emu_lock);
		gpio = inw(emu->port + A_IOCFG);
		if (source_id==0)
			outw(gpio | 0x4, emu->port + A_IOCFG);
		else
			outw(gpio & ~0x4, emu->port + A_IOCFG);
		spin_unlock_irq(&emu->emu_lock);

		ngain = emu->i2c_capture_volume[source_id][0]; /* Left */
		ogain = emu->i2c_capture_volume[emu->i2c_capture_source][0]; /* Left */
		if (ngain != ogain)
			snd_emu10k1_i2c_write(emu, ADC_ATTEN_ADCL, ((ngain) & 0xff));
		ngain = emu->i2c_capture_volume[source_id][1]; /* Right */
		ogain = emu->i2c_capture_volume[emu->i2c_capture_source][1]; /* Right */
		if (ngain != ogain)
			snd_emu10k1_i2c_write(emu, ADC_ATTEN_ADCR, ((ngain) & 0xff));

		source = 1 << (source_id + 2);
		snd_emu10k1_i2c_write(emu, ADC_MUX, source); /* Set source */
		emu->i2c_capture_source = source_id;
	}
        return change;
}

static const struct snd_kcontrol_new snd_audigy_i2c_capture_source =
{
		.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
		.name =		"Capture Source",
		.info =		snd_audigy_i2c_capture_source_info,
		.get =		snd_audigy_i2c_capture_source_get,
		.put =		snd_audigy_i2c_capture_source_put
};

static int snd_audigy_i2c_volume_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_audigy_i2c_volume_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int source_id;

	source_id = kcontrol->private_value;
	/* Limit: emu->i2c_capture_volume */
        /*        capture_source: uinfo->value.enumerated.items = 2 */
	if (source_id >= 2)
		return -EINVAL;

	ucontrol->value.integer.value[0] = emu->i2c_capture_volume[source_id][0];
	ucontrol->value.integer.value[1] = emu->i2c_capture_volume[source_id][1];
	return 0;
}

static int snd_audigy_i2c_volume_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int ogain;
	unsigned int ngain0, ngain1;
	unsigned int source_id;
	int change = 0;

	source_id = kcontrol->private_value;
	/* Limit: emu->i2c_capture_volume */
        /*        capture_source: uinfo->value.enumerated.items = 2 */
	if (source_id >= 2)
		return -EINVAL;
	ngain0 = ucontrol->value.integer.value[0];
	ngain1 = ucontrol->value.integer.value[1];
	if (ngain0 > 0xff)
		return -EINVAL;
	if (ngain1 > 0xff)
		return -EINVAL;
	ogain = emu->i2c_capture_volume[source_id][0]; /* Left */
	if (ogain != ngain0) {
		if (emu->i2c_capture_source == source_id)
			snd_emu10k1_i2c_write(emu, ADC_ATTEN_ADCL, ngain0);
		emu->i2c_capture_volume[source_id][0] = ngain0;
		change = 1;
	}
	ogain = emu->i2c_capture_volume[source_id][1]; /* Right */
	if (ogain != ngain1) {
		if (emu->i2c_capture_source == source_id)
			snd_emu10k1_i2c_write(emu, ADC_ATTEN_ADCR, ngain1);
		emu->i2c_capture_volume[source_id][1] = ngain1;
		change = 1;
	}

	return change;
}

static const struct snd_kcontrol_new i2c_volume_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
	          SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.info = snd_audigy_i2c_volume_info,
	.get = snd_audigy_i2c_volume_get,
	.put = snd_audigy_i2c_volume_put,
	.tlv = { .p = snd_audigy_db_scale2 }
};

static const char * const snd_audigy_i2c_volume_ctls[] = {
	"Mic Capture Volume",
	"Line Capture Volume",
};

#if 0
static int snd_audigy_spdif_output_rate_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {"44100", "48000", "96000"};

	return snd_ctl_enum_info(uinfo, 1, 3, texts);
}

static int snd_audigy_spdif_output_rate_get(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int tmp;

	tmp = snd_emu10k1_ptr_read(emu, A_SPDIF_SAMPLERATE, 0);
	switch (tmp & A_SPDIF_RATE_MASK) {
	case A_SPDIF_44100:
		ucontrol->value.enumerated.item[0] = 0;
		break;
	case A_SPDIF_48000:
		ucontrol->value.enumerated.item[0] = 1;
		break;
	case A_SPDIF_96000:
		ucontrol->value.enumerated.item[0] = 2;
		break;
	default:
		ucontrol->value.enumerated.item[0] = 1;
	}
	return 0;
}

static int snd_audigy_spdif_output_rate_put(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int reg, val, tmp;

	switch(ucontrol->value.enumerated.item[0]) {
	case 0:
		val = A_SPDIF_44100;
		break;
	case 1:
		val = A_SPDIF_48000;
		break;
	case 2:
		val = A_SPDIF_96000;
		break;
	default:
		val = A_SPDIF_48000;
		break;
	}

	
	spin_lock_irq(&emu->reg_lock);
	reg = snd_emu10k1_ptr_read(emu, A_SPDIF_SAMPLERATE, 0);
	tmp = reg & ~A_SPDIF_RATE_MASK;
	tmp |= val;
	change = (tmp != reg);
	if (change)
		snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, 0, tmp);
	spin_unlock_irq(&emu->reg_lock);
	return change;
}

static const struct snd_kcontrol_new snd_audigy_spdif_output_rate =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Audigy SPDIF Output Sample Rate",
	.count =	1,
	.info =         snd_audigy_spdif_output_rate_info,
	.get =          snd_audigy_spdif_output_rate_get,
	.put =          snd_audigy_spdif_output_rate_put
};
#endif

static int snd_emu10k1_spdif_put(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	int change;
	unsigned int val;

	/* Limit: emu->spdif_bits */
	if (idx >= 3)
		return -EINVAL;
	val = (ucontrol->value.iec958.status[0] << 0) |
	      (ucontrol->value.iec958.status[1] << 8) |
	      (ucontrol->value.iec958.status[2] << 16) |
	      (ucontrol->value.iec958.status[3] << 24);
	change = val != emu->spdif_bits[idx];
	if (change) {
		snd_emu10k1_ptr_write(emu, SPCS0 + idx, 0, val);
		emu->spdif_bits[idx] = val;
	}
	return change;
}

static const struct snd_kcontrol_new snd_emu10k1_spdif_mask_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	.count =	3,
	.info =         snd_emu10k1_spdif_info,
	.get =          snd_emu10k1_spdif_get_mask
};

static const struct snd_kcontrol_new snd_emu10k1_spdif_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.count =	3,
	.info =         snd_emu10k1_spdif_info,
	.get =          snd_emu10k1_spdif_get,
	.put =          snd_emu10k1_spdif_put
};


static void update_emu10k1_fxrt(struct snd_emu10k1 *emu, int voice, unsigned char *route)
{
	if (emu->audigy) {
		snd_emu10k1_ptr_write_multiple(emu, voice,
			A_FXRT1, snd_emu10k1_compose_audigy_fxrt1(route),
			A_FXRT2, snd_emu10k1_compose_audigy_fxrt2(route),
			REGLIST_END);
	} else {
		snd_emu10k1_ptr_write(emu, FXRT, voice,
				      snd_emu10k1_compose_send_routing(route));
	}
}

static void update_emu10k1_send_volume(struct snd_emu10k1 *emu, int voice, unsigned char *volume)
{
	snd_emu10k1_ptr_write(emu, PTRX_FXSENDAMOUNT_A, voice, volume[0]);
	snd_emu10k1_ptr_write(emu, PTRX_FXSENDAMOUNT_B, voice, volume[1]);
	snd_emu10k1_ptr_write(emu, PSST_FXSENDAMOUNT_C, voice, volume[2]);
	snd_emu10k1_ptr_write(emu, DSL_FXSENDAMOUNT_D, voice, volume[3]);
	if (emu->audigy) {
		snd_emu10k1_ptr_write(emu, A_SENDAMOUNTS, voice,
				      snd_emu10k1_compose_audigy_sendamounts(volume));
	}
}

/* PCM stream controls */

static int snd_emu10k1_send_routing_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = emu->audigy ? 3*8 : 3*4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = emu->audigy ? 0x3f : 0x0f;
	return 0;
}

static int snd_emu10k1_send_routing_get(struct snd_kcontrol *kcontrol,
                                        struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int voice, idx;
	int num_efx = emu->audigy ? 8 : 4;
	int mask = emu->audigy ? 0x3f : 0x0f;

	for (voice = 0; voice < 3; voice++)
		for (idx = 0; idx < num_efx; idx++)
			ucontrol->value.integer.value[(voice * num_efx) + idx] = 
				mix->send_routing[voice][idx] & mask;
	return 0;
}

static int snd_emu10k1_send_routing_put(struct snd_kcontrol *kcontrol,
                                        struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int change = 0, voice, idx, val;
	int num_efx = emu->audigy ? 8 : 4;
	int mask = emu->audigy ? 0x3f : 0x0f;

	spin_lock_irq(&emu->reg_lock);
	for (voice = 0; voice < 3; voice++)
		for (idx = 0; idx < num_efx; idx++) {
			val = ucontrol->value.integer.value[(voice * num_efx) + idx] & mask;
			if (mix->send_routing[voice][idx] != val) {
				mix->send_routing[voice][idx] = val;
				change = 1;
			}
		}	
	if (change && mix->epcm && mix->epcm->voices[0]) {
		if (!mix->epcm->voices[0]->last) {
			update_emu10k1_fxrt(emu, mix->epcm->voices[0]->number,
					    &mix->send_routing[1][0]);
			update_emu10k1_fxrt(emu, mix->epcm->voices[0]->number + 1,
					    &mix->send_routing[2][0]);
		} else {
			update_emu10k1_fxrt(emu, mix->epcm->voices[0]->number,
					    &mix->send_routing[0][0]);
		}
	}
	spin_unlock_irq(&emu->reg_lock);
	return change;
}

static const struct snd_kcontrol_new snd_emu10k1_send_routing_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "EMU10K1 PCM Send Routing",
	.count =	32,
	.info =         snd_emu10k1_send_routing_info,
	.get =          snd_emu10k1_send_routing_get,
	.put =          snd_emu10k1_send_routing_put
};

static int snd_emu10k1_send_volume_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = emu->audigy ? 3*8 : 3*4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_emu10k1_send_volume_get(struct snd_kcontrol *kcontrol,
                                       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int idx;
	int num_efx = emu->audigy ? 8 : 4;

	for (idx = 0; idx < 3*num_efx; idx++)
		ucontrol->value.integer.value[idx] = mix->send_volume[idx/num_efx][idx%num_efx];
	return 0;
}

static int snd_emu10k1_send_volume_put(struct snd_kcontrol *kcontrol,
                                       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int change = 0, idx, val;
	int num_efx = emu->audigy ? 8 : 4;

	spin_lock_irq(&emu->reg_lock);
	for (idx = 0; idx < 3*num_efx; idx++) {
		val = ucontrol->value.integer.value[idx] & 255;
		if (mix->send_volume[idx/num_efx][idx%num_efx] != val) {
			mix->send_volume[idx/num_efx][idx%num_efx] = val;
			change = 1;
		}
	}
	if (change && mix->epcm && mix->epcm->voices[0]) {
		if (!mix->epcm->voices[0]->last) {
			update_emu10k1_send_volume(emu, mix->epcm->voices[0]->number,
						   &mix->send_volume[1][0]);
			update_emu10k1_send_volume(emu, mix->epcm->voices[0]->number + 1,
						   &mix->send_volume[2][0]);
		} else {
			update_emu10k1_send_volume(emu, mix->epcm->voices[0]->number,
						   &mix->send_volume[0][0]);
		}
	}
	spin_unlock_irq(&emu->reg_lock);
	return change;
}

static const struct snd_kcontrol_new snd_emu10k1_send_volume_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "EMU10K1 PCM Send Volume",
	.count =	32,
	.info =         snd_emu10k1_send_volume_info,
	.get =          snd_emu10k1_send_volume_get,
	.put =          snd_emu10k1_send_volume_put
};

static int snd_emu10k1_attn_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 3;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0x1fffd;
	return 0;
}

static int snd_emu10k1_attn_get(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int idx;

	for (idx = 0; idx < 3; idx++)
		ucontrol->value.integer.value[idx] = mix->attn[idx] * 0xffffU / 0x8000U;
	return 0;
}

static int snd_emu10k1_attn_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int change = 0, idx, val;

	spin_lock_irq(&emu->reg_lock);
	for (idx = 0; idx < 3; idx++) {
		unsigned uval = ucontrol->value.integer.value[idx] & 0x1ffff;
		val = uval * 0x8000U / 0xffffU;
		if (mix->attn[idx] != val) {
			mix->attn[idx] = val;
			change = 1;
		}
	}
	if (change && mix->epcm && mix->epcm->voices[0]) {
		if (!mix->epcm->voices[0]->last) {
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[0]->number, mix->attn[1]);
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[0]->number + 1, mix->attn[2]);
		} else {
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[0]->number, mix->attn[0]);
		}
	}
	spin_unlock_irq(&emu->reg_lock);
	return change;
}

static const struct snd_kcontrol_new snd_emu10k1_attn_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "EMU10K1 PCM Volume",
	.count =	32,
	.info =         snd_emu10k1_attn_info,
	.get =          snd_emu10k1_attn_get,
	.put =          snd_emu10k1_attn_put
};

/* Mutichannel PCM stream controls */

static int snd_emu10k1_efx_send_routing_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = emu->audigy ? 8 : 4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = emu->audigy ? 0x3f : 0x0f;
	return 0;
}

static int snd_emu10k1_efx_send_routing_get(struct snd_kcontrol *kcontrol,
                                        struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->efx_pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int idx;
	int num_efx = emu->audigy ? 8 : 4;
	int mask = emu->audigy ? 0x3f : 0x0f;

	for (idx = 0; idx < num_efx; idx++)
		ucontrol->value.integer.value[idx] = 
			mix->send_routing[0][idx] & mask;
	return 0;
}

static int snd_emu10k1_efx_send_routing_put(struct snd_kcontrol *kcontrol,
                                        struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int ch = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct snd_emu10k1_pcm_mixer *mix = &emu->efx_pcm_mixer[ch];
	int change = 0, idx, val;
	int num_efx = emu->audigy ? 8 : 4;
	int mask = emu->audigy ? 0x3f : 0x0f;

	spin_lock_irq(&emu->reg_lock);
	for (idx = 0; idx < num_efx; idx++) {
		val = ucontrol->value.integer.value[idx] & mask;
		if (mix->send_routing[0][idx] != val) {
			mix->send_routing[0][idx] = val;
			change = 1;
		}
	}	

	if (change && mix->epcm) {
		if (mix->epcm->voices[ch]) {
			update_emu10k1_fxrt(emu, mix->epcm->voices[ch]->number,
					&mix->send_routing[0][0]);
		}
	}
	spin_unlock_irq(&emu->reg_lock);
	return change;
}

static const struct snd_kcontrol_new snd_emu10k1_efx_send_routing_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "Multichannel PCM Send Routing",
	.count =	16,
	.info =         snd_emu10k1_efx_send_routing_info,
	.get =          snd_emu10k1_efx_send_routing_get,
	.put =          snd_emu10k1_efx_send_routing_put
};

static int snd_emu10k1_efx_send_volume_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = emu->audigy ? 8 : 4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_emu10k1_efx_send_volume_get(struct snd_kcontrol *kcontrol,
                                       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->efx_pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int idx;
	int num_efx = emu->audigy ? 8 : 4;

	for (idx = 0; idx < num_efx; idx++)
		ucontrol->value.integer.value[idx] = mix->send_volume[0][idx];
	return 0;
}

static int snd_emu10k1_efx_send_volume_put(struct snd_kcontrol *kcontrol,
                                       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int ch = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct snd_emu10k1_pcm_mixer *mix = &emu->efx_pcm_mixer[ch];
	int change = 0, idx, val;
	int num_efx = emu->audigy ? 8 : 4;

	spin_lock_irq(&emu->reg_lock);
	for (idx = 0; idx < num_efx; idx++) {
		val = ucontrol->value.integer.value[idx] & 255;
		if (mix->send_volume[0][idx] != val) {
			mix->send_volume[0][idx] = val;
			change = 1;
		}
	}
	if (change && mix->epcm) {
		if (mix->epcm->voices[ch]) {
			update_emu10k1_send_volume(emu, mix->epcm->voices[ch]->number,
						   &mix->send_volume[0][0]);
		}
	}
	spin_unlock_irq(&emu->reg_lock);
	return change;
}


static const struct snd_kcontrol_new snd_emu10k1_efx_send_volume_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "Multichannel PCM Send Volume",
	.count =	16,
	.info =         snd_emu10k1_efx_send_volume_info,
	.get =          snd_emu10k1_efx_send_volume_get,
	.put =          snd_emu10k1_efx_send_volume_put
};

static int snd_emu10k1_efx_attn_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0x1fffd;
	return 0;
}

static int snd_emu10k1_efx_attn_get(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->efx_pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];

	ucontrol->value.integer.value[0] = mix->attn[0] * 0xffffU / 0x8000U;
	return 0;
}

static int snd_emu10k1_efx_attn_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int ch = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct snd_emu10k1_pcm_mixer *mix = &emu->efx_pcm_mixer[ch];
	int change = 0, val;
	unsigned uval;

	spin_lock_irq(&emu->reg_lock);
	uval = ucontrol->value.integer.value[0] & 0x1ffff;
	val = uval * 0x8000U / 0xffffU;
	if (mix->attn[0] != val) {
		mix->attn[0] = val;
		change = 1;
	}
	if (change && mix->epcm) {
		if (mix->epcm->voices[ch]) {
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[ch]->number, mix->attn[0]);
		}
	}
	spin_unlock_irq(&emu->reg_lock);
	return change;
}

static const struct snd_kcontrol_new snd_emu10k1_efx_attn_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "Multichannel PCM Volume",
	.count =	16,
	.info =         snd_emu10k1_efx_attn_info,
	.get =          snd_emu10k1_efx_attn_get,
	.put =          snd_emu10k1_efx_attn_put
};

#define snd_emu10k1_shared_spdif_info	snd_ctl_boolean_mono_info

static int snd_emu10k1_shared_spdif_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);

	if (emu->audigy)
		ucontrol->value.integer.value[0] = inw(emu->port + A_IOCFG) & A_IOCFG_GPOUT0 ? 1 : 0;
	else
		ucontrol->value.integer.value[0] = inl(emu->port + HCFG) & HCFG_GPOUT0 ? 1 : 0;
	if (emu->card_capabilities->invert_shared_spdif)
		ucontrol->value.integer.value[0] =
			!ucontrol->value.integer.value[0];
		
	return 0;
}

static int snd_emu10k1_shared_spdif_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int reg, val, sw;
	int change = 0;

	sw = ucontrol->value.integer.value[0];
	if (emu->card_capabilities->invert_shared_spdif)
		sw = !sw;
	spin_lock_irq(&emu->emu_lock);
	if ( emu->card_capabilities->i2c_adc) {
		/* Do nothing for Audigy 2 ZS Notebook */
	} else if (emu->audigy) {
		reg = inw(emu->port + A_IOCFG);
		val = sw ? A_IOCFG_GPOUT0 : 0;
		change = (reg & A_IOCFG_GPOUT0) != val;
		if (change) {
			reg &= ~A_IOCFG_GPOUT0;
			reg |= val;
			outw(reg | val, emu->port + A_IOCFG);
		}
	}
	reg = inl(emu->port + HCFG);
	val = sw ? HCFG_GPOUT0 : 0;
	change |= (reg & HCFG_GPOUT0) != val;
	if (change) {
		reg &= ~HCFG_GPOUT0;
		reg |= val;
		outl(reg | val, emu->port + HCFG);
	}
	spin_unlock_irq(&emu->emu_lock);
	return change;
}

static const struct snd_kcontrol_new snd_emu10k1_shared_spdif =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"SB Live Analog/Digital Output Jack",
	.info =		snd_emu10k1_shared_spdif_info,
	.get =		snd_emu10k1_shared_spdif_get,
	.put =		snd_emu10k1_shared_spdif_put
};

static const struct snd_kcontrol_new snd_audigy_shared_spdif =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Audigy Analog/Digital Output Jack",
	.info =		snd_emu10k1_shared_spdif_info,
	.get =		snd_emu10k1_shared_spdif_get,
	.put =		snd_emu10k1_shared_spdif_put
};

/* workaround for too low volume on Audigy due to 16bit/24bit conversion */

#define snd_audigy_capture_boost_info	snd_ctl_boolean_mono_info

static int snd_audigy_capture_boost_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;

	/* FIXME: better to use a cached version */
	val = snd_ac97_read(emu->ac97, AC97_REC_GAIN);
	ucontrol->value.integer.value[0] = !!val;
	return 0;
}

static int snd_audigy_capture_boost_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;

	if (ucontrol->value.integer.value[0])
		val = 0x0f0f;
	else
		val = 0;
	return snd_ac97_update(emu->ac97, AC97_REC_GAIN, val);
}

static const struct snd_kcontrol_new snd_audigy_capture_boost =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Mic Extra Boost",
	.info =		snd_audigy_capture_boost_info,
	.get =		snd_audigy_capture_boost_get,
	.put =		snd_audigy_capture_boost_put
};


/*
 */
static void snd_emu10k1_mixer_free_ac97(struct snd_ac97 *ac97)
{
	struct snd_emu10k1 *emu = ac97->private_data;
	emu->ac97 = NULL;
}

/*
 */
static int remove_ctl(struct snd_card *card, const char *name)
{
	struct snd_ctl_elem_id id;
	memset(&id, 0, sizeof(id));
	strcpy(id.name, name);
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_remove_id(card, &id);
}

static int rename_ctl(struct snd_card *card, const char *src, const char *dst)
{
	struct snd_kcontrol *kctl = snd_ctl_find_id_mixer(card, src);
	if (kctl) {
		snd_ctl_rename(card, kctl, dst);
		return 0;
	}
	return -ENOENT;
}

int snd_emu10k1_mixer(struct snd_emu10k1 *emu,
		      int pcm_device, int multi_device)
{
	int err;
	struct snd_kcontrol *kctl;
	struct snd_card *card = emu->card;
	const char * const *c;
	static const char * const emu10k1_remove_ctls[] = {
		/* no AC97 mono, surround, center/lfe */
		"Master Mono Playback Switch",
		"Master Mono Playback Volume",
		"PCM Out Path & Mute",
		"Mono Output Select",
		"Surround Playback Switch",
		"Surround Playback Volume",
		"Center Playback Switch",
		"Center Playback Volume",
		"LFE Playback Switch",
		"LFE Playback Volume",
		NULL
	};
	static const char * const emu10k1_rename_ctls[] = {
		"Surround Digital Playback Volume", "Surround Playback Volume",
		"Center Digital Playback Volume", "Center Playback Volume",
		"LFE Digital Playback Volume", "LFE Playback Volume",
		NULL
	};
	static const char * const audigy_remove_ctls[] = {
		/* Master/PCM controls on ac97 of Audigy has no effect */
		/* On the Audigy2 the AC97 playback is piped into
		 * the Philips ADC for 24bit capture */
		"PCM Playback Switch",
		"PCM Playback Volume",
		"Master Playback Switch",
		"Master Playback Volume",
		"PCM Out Path & Mute",
		"Mono Output Select",
		/* remove unused AC97 capture controls */
		"Capture Source",
		"Capture Switch",
		"Capture Volume",
		"Mic Select",
		"Headphone Playback Switch",
		"Headphone Playback Volume",
		"3D Control - Center",
		"3D Control - Depth",
		"3D Control - Switch",
		"Video Playback Switch",
		"Video Playback Volume",
		"Mic Playback Switch",
		"Mic Playback Volume",
		"External Amplifier",
		NULL
	};
	static const char * const audigy_rename_ctls[] = {
		/* use conventional names */
		"Wave Playback Volume", "PCM Playback Volume",
		/* "Wave Capture Volume", "PCM Capture Volume", */
		"Wave Master Playback Volume", "Master Playback Volume",
		"AMic Playback Volume", "Mic Playback Volume",
		"Master Mono Playback Switch", "Phone Output Playback Switch",
		"Master Mono Playback Volume", "Phone Output Playback Volume",
		NULL
	};
	static const char * const audigy_rename_ctls_i2c_adc[] = {
		//"Analog Mix Capture Volume","OLD Analog Mix Capture Volume",
		"Line Capture Volume", "Analog Mix Capture Volume",
		"Wave Playback Volume", "OLD PCM Playback Volume",
		"Wave Master Playback Volume", "Master Playback Volume",
		"AMic Playback Volume", "Old Mic Playback Volume",
		"CD Capture Volume", "IEC958 Optical Capture Volume",
		NULL
	};
	static const char * const audigy_remove_ctls_i2c_adc[] = {
		/* On the Audigy2 ZS Notebook
		 * Capture via WM8775  */
		"Mic Capture Volume",
		"Analog Mix Capture Volume",
		"Aux Capture Volume",
		"IEC958 Optical Capture Volume",
		NULL
	};
	static const char * const audigy_remove_ctls_1361t_adc[] = {
		/* On the Audigy2 the AC97 playback is piped into
		 * the Philips ADC for 24bit capture */
		"PCM Playback Switch",
		"PCM Playback Volume",
		"Capture Source",
		"Capture Switch",
		"Capture Volume",
		"Mic Capture Volume",
		"Headphone Playback Switch",
		"Headphone Playback Volume",
		"3D Control - Center",
		"3D Control - Depth",
		"3D Control - Switch",
		"Line2 Playback Volume",
		"Line2 Capture Volume",
		NULL
	};
	static const char * const audigy_rename_ctls_1361t_adc[] = {
		"Master Playback Switch", "Master Capture Switch",
		"Master Playback Volume", "Master Capture Volume",
		"Wave Master Playback Volume", "Master Playback Volume",
		"Beep Playback Switch", "Beep Capture Switch",
		"Beep Playback Volume", "Beep Capture Volume",
		"Phone Playback Switch", "Phone Capture Switch",
		"Phone Playback Volume", "Phone Capture Volume",
		"Mic Playback Switch", "Mic Capture Switch",
		"Mic Playback Volume", "Mic Capture Volume",
		"Line Playback Switch", "Line Capture Switch",
		"Line Playback Volume", "Line Capture Volume",
		"CD Playback Switch", "CD Capture Switch",
		"CD Playback Volume", "CD Capture Volume",
		"Aux Playback Switch", "Aux Capture Switch",
		"Aux Playback Volume", "Aux Capture Volume",
		"Video Playback Switch", "Video Capture Switch",
		"Video Playback Volume", "Video Capture Volume",
		"Master Mono Playback Switch", "Phone Output Playback Switch",
		"Master Mono Playback Volume", "Phone Output Playback Volume",
		NULL
	};

	if (emu->card_capabilities->ac97_chip) {
		struct snd_ac97_bus *pbus;
		struct snd_ac97_template ac97;
		static const struct snd_ac97_bus_ops ops = {
			.write = snd_emu10k1_ac97_write,
			.read = snd_emu10k1_ac97_read,
		};

		err = snd_ac97_bus(emu->card, 0, &ops, NULL, &pbus);
		if (err < 0)
			return err;
		pbus->no_vra = 1; /* we don't need VRA */
		
		memset(&ac97, 0, sizeof(ac97));
		ac97.private_data = emu;
		ac97.private_free = snd_emu10k1_mixer_free_ac97;
		ac97.scaps = AC97_SCAP_NO_SPDIF;
		err = snd_ac97_mixer(pbus, &ac97, &emu->ac97);
		if (err < 0) {
			if (emu->card_capabilities->ac97_chip == 1)
				return err;
			dev_info(emu->card->dev,
				 "AC97 is optional on this board\n");
			dev_info(emu->card->dev,
				 "Proceeding without ac97 mixers...\n");
			snd_device_free(emu->card, pbus);
			goto no_ac97; /* FIXME: get rid of ugly gotos.. */
		}
		if (emu->audigy) {
			/* set master volume to 0 dB */
			snd_ac97_write_cache(emu->ac97, AC97_MASTER, 0x0000);
			/* set capture source to mic */
			snd_ac97_write_cache(emu->ac97, AC97_REC_SEL, 0x0000);
			/* set mono output (TAD) to mic */
			snd_ac97_update_bits(emu->ac97, AC97_GENERAL_PURPOSE,
				0x0200, 0x0200);
			if (emu->card_capabilities->adc_1361t)
				c = audigy_remove_ctls_1361t_adc;
			else 
				c = audigy_remove_ctls;
		} else {
			/*
			 * Credits for cards based on STAC9758:
			 *   James Courtier-Dutton <James@superbug.demon.co.uk>
			 *   Voluspa <voluspa@comhem.se>
			 */
			if (emu->ac97->id == AC97_ID_STAC9758) {
				emu->rear_ac97 = 1;
				snd_emu10k1_ptr_write(emu, AC97SLOT, 0, AC97SLOT_CNTR|AC97SLOT_LFE|AC97SLOT_REAR_LEFT|AC97SLOT_REAR_RIGHT);
				snd_ac97_write_cache(emu->ac97, AC97_HEADPHONE, 0x0202);
				remove_ctl(card,"Front Playback Volume");
				remove_ctl(card,"Front Playback Switch");
			}
			/* remove unused AC97 controls */
			snd_ac97_write_cache(emu->ac97, AC97_SURROUND_MASTER, 0x0202);
			snd_ac97_write_cache(emu->ac97, AC97_CENTER_LFE_MASTER, 0x0202);
			c = emu10k1_remove_ctls;
		}
		for (; *c; c++)
			remove_ctl(card, *c);
	} else if (emu->card_capabilities->i2c_adc) {
		c = audigy_remove_ctls_i2c_adc;
		for (; *c; c++)
			remove_ctl(card, *c);
	} else {
	no_ac97:
		if (emu->card_capabilities->ecard)
			strcpy(emu->card->mixername, "EMU APS");
		else if (emu->audigy)
			strcpy(emu->card->mixername, "SB Audigy");
		else
			strcpy(emu->card->mixername, "Emu10k1");
	}

	if (emu->audigy)
		if (emu->card_capabilities->adc_1361t)
			c = audigy_rename_ctls_1361t_adc;
		else if (emu->card_capabilities->i2c_adc)
			c = audigy_rename_ctls_i2c_adc;
		else
			c = audigy_rename_ctls;
	else
		c = emu10k1_rename_ctls;
	for (; *c; c += 2)
		rename_ctl(card, c[0], c[1]);

	if (emu->card_capabilities->subsystem == 0x80401102) { /* SB Live! Platinum CT4760P */
		remove_ctl(card, "Center Playback Volume");
		remove_ctl(card, "LFE Playback Volume");
		remove_ctl(card, "Wave Center Playback Volume");
		remove_ctl(card, "Wave LFE Playback Volume");
	}
	if (emu->card_capabilities->subsystem == 0x20071102) {  /* Audigy 4 Pro */
		rename_ctl(card, "Line2 Capture Volume", "Line1/Mic Capture Volume");
		rename_ctl(card, "Analog Mix Capture Volume", "Line2 Capture Volume");
		rename_ctl(card, "Aux2 Capture Volume", "Line3 Capture Volume");
		rename_ctl(card, "Mic Capture Volume", "Unknown1 Capture Volume");
	}
	kctl = emu->ctl_send_routing = snd_ctl_new1(&snd_emu10k1_send_routing_control, emu);
	if (!kctl)
		return -ENOMEM;
	kctl->id.device = pcm_device;
	err = snd_ctl_add(card, kctl);
	if (err)
		return err;
	kctl = emu->ctl_send_volume = snd_ctl_new1(&snd_emu10k1_send_volume_control, emu);
	if (!kctl)
		return -ENOMEM;
	kctl->id.device = pcm_device;
	err = snd_ctl_add(card, kctl);
	if (err)
		return err;
	kctl = emu->ctl_attn = snd_ctl_new1(&snd_emu10k1_attn_control, emu);
	if (!kctl)
		return -ENOMEM;
	kctl->id.device = pcm_device;
	err = snd_ctl_add(card, kctl);
	if (err)
		return err;

	kctl = emu->ctl_efx_send_routing = snd_ctl_new1(&snd_emu10k1_efx_send_routing_control, emu);
	if (!kctl)
		return -ENOMEM;
	kctl->id.device = multi_device;
	err = snd_ctl_add(card, kctl);
	if (err)
		return err;
	
	kctl = emu->ctl_efx_send_volume = snd_ctl_new1(&snd_emu10k1_efx_send_volume_control, emu);
	if (!kctl)
		return -ENOMEM;
	kctl->id.device = multi_device;
	err = snd_ctl_add(card, kctl);
	if (err)
		return err;
	
	kctl = emu->ctl_efx_attn = snd_ctl_new1(&snd_emu10k1_efx_attn_control, emu);
	if (!kctl)
		return -ENOMEM;
	kctl->id.device = multi_device;
	err = snd_ctl_add(card, kctl);
	if (err)
		return err;

	if (!emu->card_capabilities->ecard && !emu->card_capabilities->emu_model) {
		/* sb live! and audigy */
		kctl = snd_ctl_new1(&snd_emu10k1_spdif_mask_control, emu);
		if (!kctl)
			return -ENOMEM;
		if (!emu->audigy)
			kctl->id.device = emu->pcm_efx->device;
		err = snd_ctl_add(card, kctl);
		if (err)
			return err;
		kctl = snd_ctl_new1(&snd_emu10k1_spdif_control, emu);
		if (!kctl)
			return -ENOMEM;
		if (!emu->audigy)
			kctl->id.device = emu->pcm_efx->device;
		err = snd_ctl_add(card, kctl);
		if (err)
			return err;
	}

	if (emu->card_capabilities->emu_model) {
		;  /* Disable the snd_audigy_spdif_shared_spdif */
	} else if (emu->audigy) {
		kctl = snd_ctl_new1(&snd_audigy_shared_spdif, emu);
		if (!kctl)
			return -ENOMEM;
		err = snd_ctl_add(card, kctl);
		if (err)
			return err;
#if 0
		kctl = snd_ctl_new1(&snd_audigy_spdif_output_rate, emu);
		if (!kctl)
			return -ENOMEM;
		err = snd_ctl_add(card, kctl);
		if (err)
			return err;
#endif
	} else if (! emu->card_capabilities->ecard) {
		/* sb live! */
		kctl = snd_ctl_new1(&snd_emu10k1_shared_spdif, emu);
		if (!kctl)
			return -ENOMEM;
		err = snd_ctl_add(card, kctl);
		if (err)
			return err;
	}
	if (emu->card_capabilities->ca0151_chip) { /* P16V */
		err = snd_p16v_mixer(emu);
		if (err)
			return err;
	}

	if (emu->card_capabilities->emu_model) {
		unsigned i, emu_idx = emu1010_idx(emu);
		const struct snd_emu1010_routing_info *emu_ri =
			&emu1010_routing_info[emu_idx];
		const struct snd_emu1010_pads_info *emu_pi = &emu1010_pads_info[emu_idx];

		for (i = 0; i < emu_ri->n_ins; i++)
			emu->emu1010.input_source[i] =
				emu1010_map_source(emu_ri, emu_ri->in_dflts[i]);
		for (i = 0; i < emu_ri->n_outs; i++)
			emu->emu1010.output_source[i] =
				emu1010_map_source(emu_ri, emu_ri->out_dflts[i]);
		snd_emu1010_apply_sources(emu);

		kctl = emu->ctl_clock_source = snd_ctl_new1(&snd_emu1010_clock_source, emu);
		err = snd_ctl_add(card, kctl);
		if (err < 0)
			return err;
		err = snd_ctl_add(card,
			snd_ctl_new1(&snd_emu1010_clock_fallback, emu));
		if (err < 0)
			return err;

		err = add_ctls(emu, &emu1010_adc_pads_ctl,
			       emu_pi->adc_ctls, emu_pi->n_adc_ctls);
		if (err < 0)
			return err;
		err = add_ctls(emu, &emu1010_dac_pads_ctl,
			       emu_pi->dac_ctls, emu_pi->n_dac_ctls);
		if (err < 0)
			return err;

		if (!emu->card_capabilities->no_adat) {
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1010_optical_out, emu));
			if (err < 0)
				return err;
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1010_optical_in, emu));
			if (err < 0)
				return err;
		}

		err = add_emu1010_source_mixers(emu);
		if (err < 0)
			return err;
	}

	if ( emu->card_capabilities->i2c_adc) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_audigy_i2c_capture_source, emu));
		if (err < 0)
			return err;

		err = add_ctls(emu, &i2c_volume_ctl,
			       snd_audigy_i2c_volume_ctls,
			       ARRAY_SIZE(snd_audigy_i2c_volume_ctls));
		if (err < 0)
			return err;
	}
		
	if (emu->card_capabilities->ac97_chip && emu->audigy) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_audigy_capture_boost,
						     emu));
		if (err < 0)
			return err;
	}

	return 0;
}

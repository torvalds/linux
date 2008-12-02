/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>,
 *                   Takashi Iwai <tiwai@suse.de>
 *                   Creative Labs, Inc.
 *  Routines for control of EMU10K1 chips / mixer routines
 *  Multichannel PCM support Copyright (c) Lee Revell <rlrevell@joe-job.com>
 *
 *  Copyright (c) by James Courtier-Dutton <James@superbug.co.uk>
 *  	Added EMU 1010 support.
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
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
	unsigned long flags;

	/* Limit: emu->spdif_bits */
	if (idx >= 3)
		return -EINVAL;
	spin_lock_irqsave(&emu->reg_lock, flags);
	ucontrol->value.iec958.status[0] = (emu->spdif_bits[idx] >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (emu->spdif_bits[idx] >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (emu->spdif_bits[idx] >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (emu->spdif_bits[idx] >> 24) & 0xff;
	spin_unlock_irqrestore(&emu->reg_lock, flags);
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

/*
 * Items labels in enum mixer controls assigning source data to
 * each destination
 */
static char *emu1010_src_texts[] = { 
	"Silence",
	"Dock Mic A",
	"Dock Mic B",
	"Dock ADC1 Left",
	"Dock ADC1 Right",
	"Dock ADC2 Left",
	"Dock ADC2 Right",
	"Dock ADC3 Left",
	"Dock ADC3 Right",
	"0202 ADC Left",
	"0202 ADC Right",
	"0202 SPDIF Left",
	"0202 SPDIF Right",
	"ADAT 0",
	"ADAT 1",
	"ADAT 2",
	"ADAT 3",
	"ADAT 4",
	"ADAT 5",
	"ADAT 6",
	"ADAT 7",
	"DSP 0",
	"DSP 1",
	"DSP 2",
	"DSP 3",
	"DSP 4",
	"DSP 5",
	"DSP 6",
	"DSP 7",
	"DSP 8",
	"DSP 9",
	"DSP 10",
	"DSP 11",
	"DSP 12",
	"DSP 13",
	"DSP 14",
	"DSP 15",
	"DSP 16",
	"DSP 17",
	"DSP 18",
	"DSP 19",
	"DSP 20",
	"DSP 21",
	"DSP 22",
	"DSP 23",
	"DSP 24",
	"DSP 25",
	"DSP 26",
	"DSP 27",
	"DSP 28",
	"DSP 29",
	"DSP 30",
	"DSP 31",
};

/* 1616(m) cardbus */

static char *emu1616_src_texts[] = {
	"Silence",
	"Dock Mic A",
	"Dock Mic B",
	"Dock ADC1 Left",
	"Dock ADC1 Right",
	"Dock ADC2 Left",
	"Dock ADC2 Right",
	"Dock SPDIF Left",
	"Dock SPDIF Right",
	"ADAT 0",
	"ADAT 1",
	"ADAT 2",
	"ADAT 3",
	"ADAT 4",
	"ADAT 5",
	"ADAT 6",
	"ADAT 7",
	"DSP 0",
	"DSP 1",
	"DSP 2",
	"DSP 3",
	"DSP 4",
	"DSP 5",
	"DSP 6",
	"DSP 7",
	"DSP 8",
	"DSP 9",
	"DSP 10",
	"DSP 11",
	"DSP 12",
	"DSP 13",
	"DSP 14",
	"DSP 15",
	"DSP 16",
	"DSP 17",
	"DSP 18",
	"DSP 19",
	"DSP 20",
	"DSP 21",
	"DSP 22",
	"DSP 23",
	"DSP 24",
	"DSP 25",
	"DSP 26",
	"DSP 27",
	"DSP 28",
	"DSP 29",
	"DSP 30",
	"DSP 31",
};


/*
 * List of data sources available for each destination
 */
static unsigned int emu1010_src_regs[] = {
	EMU_SRC_SILENCE,/* 0 */
	EMU_SRC_DOCK_MIC_A1, /* 1 */
	EMU_SRC_DOCK_MIC_B1, /* 2 */
	EMU_SRC_DOCK_ADC1_LEFT1, /* 3 */
	EMU_SRC_DOCK_ADC1_RIGHT1, /* 4 */
	EMU_SRC_DOCK_ADC2_LEFT1, /* 5 */
	EMU_SRC_DOCK_ADC2_RIGHT1, /* 6 */
	EMU_SRC_DOCK_ADC3_LEFT1, /* 7 */
	EMU_SRC_DOCK_ADC3_RIGHT1, /* 8 */
	EMU_SRC_HAMOA_ADC_LEFT1, /* 9 */
	EMU_SRC_HAMOA_ADC_RIGHT1, /* 10 */
	EMU_SRC_HANA_SPDIF_LEFT1, /* 11 */
	EMU_SRC_HANA_SPDIF_RIGHT1, /* 12 */
	EMU_SRC_HANA_ADAT, /* 13 */
	EMU_SRC_HANA_ADAT+1, /* 14 */
	EMU_SRC_HANA_ADAT+2, /* 15 */
	EMU_SRC_HANA_ADAT+3, /* 16 */
	EMU_SRC_HANA_ADAT+4, /* 17 */
	EMU_SRC_HANA_ADAT+5, /* 18 */
	EMU_SRC_HANA_ADAT+6, /* 19 */
	EMU_SRC_HANA_ADAT+7, /* 20 */
	EMU_SRC_ALICE_EMU32A, /* 21 */
	EMU_SRC_ALICE_EMU32A+1, /* 22 */
	EMU_SRC_ALICE_EMU32A+2, /* 23 */
	EMU_SRC_ALICE_EMU32A+3, /* 24 */
	EMU_SRC_ALICE_EMU32A+4, /* 25 */
	EMU_SRC_ALICE_EMU32A+5, /* 26 */
	EMU_SRC_ALICE_EMU32A+6, /* 27 */
	EMU_SRC_ALICE_EMU32A+7, /* 28 */
	EMU_SRC_ALICE_EMU32A+8, /* 29 */
	EMU_SRC_ALICE_EMU32A+9, /* 30 */
	EMU_SRC_ALICE_EMU32A+0xa, /* 31 */
	EMU_SRC_ALICE_EMU32A+0xb, /* 32 */
	EMU_SRC_ALICE_EMU32A+0xc, /* 33 */
	EMU_SRC_ALICE_EMU32A+0xd, /* 34 */
	EMU_SRC_ALICE_EMU32A+0xe, /* 35 */
	EMU_SRC_ALICE_EMU32A+0xf, /* 36 */
	EMU_SRC_ALICE_EMU32B, /* 37 */
	EMU_SRC_ALICE_EMU32B+1, /* 38 */
	EMU_SRC_ALICE_EMU32B+2, /* 39 */
	EMU_SRC_ALICE_EMU32B+3, /* 40 */
	EMU_SRC_ALICE_EMU32B+4, /* 41 */
	EMU_SRC_ALICE_EMU32B+5, /* 42 */
	EMU_SRC_ALICE_EMU32B+6, /* 43 */
	EMU_SRC_ALICE_EMU32B+7, /* 44 */
	EMU_SRC_ALICE_EMU32B+8, /* 45 */
	EMU_SRC_ALICE_EMU32B+9, /* 46 */
	EMU_SRC_ALICE_EMU32B+0xa, /* 47 */
	EMU_SRC_ALICE_EMU32B+0xb, /* 48 */
	EMU_SRC_ALICE_EMU32B+0xc, /* 49 */
	EMU_SRC_ALICE_EMU32B+0xd, /* 50 */
	EMU_SRC_ALICE_EMU32B+0xe, /* 51 */
	EMU_SRC_ALICE_EMU32B+0xf, /* 52 */
};

/* 1616(m) cardbus */
static unsigned int emu1616_src_regs[] = {
	EMU_SRC_SILENCE,
	EMU_SRC_DOCK_MIC_A1,
	EMU_SRC_DOCK_MIC_B1,
	EMU_SRC_DOCK_ADC1_LEFT1,
	EMU_SRC_DOCK_ADC1_RIGHT1,
	EMU_SRC_DOCK_ADC2_LEFT1,
	EMU_SRC_DOCK_ADC2_RIGHT1,
	EMU_SRC_MDOCK_SPDIF_LEFT1,
	EMU_SRC_MDOCK_SPDIF_RIGHT1,
	EMU_SRC_MDOCK_ADAT,
	EMU_SRC_MDOCK_ADAT+1,
	EMU_SRC_MDOCK_ADAT+2,
	EMU_SRC_MDOCK_ADAT+3,
	EMU_SRC_MDOCK_ADAT+4,
	EMU_SRC_MDOCK_ADAT+5,
	EMU_SRC_MDOCK_ADAT+6,
	EMU_SRC_MDOCK_ADAT+7,
	EMU_SRC_ALICE_EMU32A,
	EMU_SRC_ALICE_EMU32A+1,
	EMU_SRC_ALICE_EMU32A+2,
	EMU_SRC_ALICE_EMU32A+3,
	EMU_SRC_ALICE_EMU32A+4,
	EMU_SRC_ALICE_EMU32A+5,
	EMU_SRC_ALICE_EMU32A+6,
	EMU_SRC_ALICE_EMU32A+7,
	EMU_SRC_ALICE_EMU32A+8,
	EMU_SRC_ALICE_EMU32A+9,
	EMU_SRC_ALICE_EMU32A+0xa,
	EMU_SRC_ALICE_EMU32A+0xb,
	EMU_SRC_ALICE_EMU32A+0xc,
	EMU_SRC_ALICE_EMU32A+0xd,
	EMU_SRC_ALICE_EMU32A+0xe,
	EMU_SRC_ALICE_EMU32A+0xf,
	EMU_SRC_ALICE_EMU32B,
	EMU_SRC_ALICE_EMU32B+1,
	EMU_SRC_ALICE_EMU32B+2,
	EMU_SRC_ALICE_EMU32B+3,
	EMU_SRC_ALICE_EMU32B+4,
	EMU_SRC_ALICE_EMU32B+5,
	EMU_SRC_ALICE_EMU32B+6,
	EMU_SRC_ALICE_EMU32B+7,
	EMU_SRC_ALICE_EMU32B+8,
	EMU_SRC_ALICE_EMU32B+9,
	EMU_SRC_ALICE_EMU32B+0xa,
	EMU_SRC_ALICE_EMU32B+0xb,
	EMU_SRC_ALICE_EMU32B+0xc,
	EMU_SRC_ALICE_EMU32B+0xd,
	EMU_SRC_ALICE_EMU32B+0xe,
	EMU_SRC_ALICE_EMU32B+0xf,
};

/*
 * Data destinations - physical EMU outputs.
 * Each destination has an enum mixer control to choose a data source
 */
static unsigned int emu1010_output_dst[] = {
	EMU_DST_DOCK_DAC1_LEFT1, /* 0 */
	EMU_DST_DOCK_DAC1_RIGHT1, /* 1 */
	EMU_DST_DOCK_DAC2_LEFT1, /* 2 */
	EMU_DST_DOCK_DAC2_RIGHT1, /* 3 */
	EMU_DST_DOCK_DAC3_LEFT1, /* 4 */
	EMU_DST_DOCK_DAC3_RIGHT1, /* 5 */
	EMU_DST_DOCK_DAC4_LEFT1, /* 6 */
	EMU_DST_DOCK_DAC4_RIGHT1, /* 7 */
	EMU_DST_DOCK_PHONES_LEFT1, /* 8 */
	EMU_DST_DOCK_PHONES_RIGHT1, /* 9 */
	EMU_DST_DOCK_SPDIF_LEFT1, /* 10 */
	EMU_DST_DOCK_SPDIF_RIGHT1, /* 11 */
	EMU_DST_HANA_SPDIF_LEFT1, /* 12 */
	EMU_DST_HANA_SPDIF_RIGHT1, /* 13 */
	EMU_DST_HAMOA_DAC_LEFT1, /* 14 */
	EMU_DST_HAMOA_DAC_RIGHT1, /* 15 */
	EMU_DST_HANA_ADAT, /* 16 */
	EMU_DST_HANA_ADAT+1, /* 17 */
	EMU_DST_HANA_ADAT+2, /* 18 */
	EMU_DST_HANA_ADAT+3, /* 19 */
	EMU_DST_HANA_ADAT+4, /* 20 */
	EMU_DST_HANA_ADAT+5, /* 21 */
	EMU_DST_HANA_ADAT+6, /* 22 */
	EMU_DST_HANA_ADAT+7, /* 23 */
};

/* 1616(m) cardbus */
static unsigned int emu1616_output_dst[] = {
	EMU_DST_DOCK_DAC1_LEFT1,
	EMU_DST_DOCK_DAC1_RIGHT1,
	EMU_DST_DOCK_DAC2_LEFT1,
	EMU_DST_DOCK_DAC2_RIGHT1,
	EMU_DST_DOCK_DAC3_LEFT1,
	EMU_DST_DOCK_DAC3_RIGHT1,
	EMU_DST_MDOCK_SPDIF_LEFT1,
	EMU_DST_MDOCK_SPDIF_RIGHT1,
	EMU_DST_MDOCK_ADAT,
	EMU_DST_MDOCK_ADAT+1,
	EMU_DST_MDOCK_ADAT+2,
	EMU_DST_MDOCK_ADAT+3,
	EMU_DST_MDOCK_ADAT+4,
	EMU_DST_MDOCK_ADAT+5,
	EMU_DST_MDOCK_ADAT+6,
	EMU_DST_MDOCK_ADAT+7,
	EMU_DST_MANA_DAC_LEFT,
	EMU_DST_MANA_DAC_RIGHT,
};

/*
 * Data destinations - HANA outputs going to Alice2 (audigy) for
 *   capture (EMU32 + I2S links)
 * Each destination has an enum mixer control to choose a data source
 */
static unsigned int emu1010_input_dst[] = {
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
	EMU_DST_ALICE_I2S0_LEFT,
	EMU_DST_ALICE_I2S0_RIGHT,
	EMU_DST_ALICE_I2S1_LEFT,
	EMU_DST_ALICE_I2S1_RIGHT,
	EMU_DST_ALICE_I2S2_LEFT,
	EMU_DST_ALICE_I2S2_RIGHT,
};

static int snd_emu1010_input_output_source_info(struct snd_kcontrol *kcontrol,
						struct snd_ctl_elem_info *uinfo)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	char **items;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	if (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616) {
		uinfo->value.enumerated.items = 49;
		items = emu1616_src_texts;
	} else {
		uinfo->value.enumerated.items = 53;
		items = emu1010_src_texts;
	}
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
			uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       items[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_emu1010_output_source_get(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int channel;

	channel = (kcontrol->private_value) & 0xff;
	/* Limit: emu1010_output_dst, emu->emu1010.output_source */
	if (channel >= 24 ||
	    (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616 &&
	     channel >= 18))
		return -EINVAL;
	ucontrol->value.enumerated.item[0] = emu->emu1010.output_source[channel];
	return 0;
}

static int snd_emu1010_output_source_put(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	unsigned int channel;

	val = ucontrol->value.enumerated.item[0];
	if (val >= 53 ||
	    (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616 &&
	     val >= 49))
		return -EINVAL;
	channel = (kcontrol->private_value) & 0xff;
	/* Limit: emu1010_output_dst, emu->emu1010.output_source */
	if (channel >= 24 ||
	    (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616 &&
	     channel >= 18))
		return -EINVAL;
	if (emu->emu1010.output_source[channel] == val)
		return 0;
	emu->emu1010.output_source[channel] = val;
	if (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616)
		snd_emu1010_fpga_link_dst_src_write(emu,
			emu1616_output_dst[channel], emu1616_src_regs[val]);
	else
		snd_emu1010_fpga_link_dst_src_write(emu,
			emu1010_output_dst[channel], emu1010_src_regs[val]);
	return 1;
}

static int snd_emu1010_input_source_get(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int channel;

	channel = (kcontrol->private_value) & 0xff;
	/* Limit: emu1010_input_dst, emu->emu1010.input_source */
	if (channel >= 22)
		return -EINVAL;
	ucontrol->value.enumerated.item[0] = emu->emu1010.input_source[channel];
	return 0;
}

static int snd_emu1010_input_source_put(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	unsigned int channel;

	val = ucontrol->value.enumerated.item[0];
	if (val >= 53 ||
	    (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616 &&
	     val >= 49))
		return -EINVAL;
	channel = (kcontrol->private_value) & 0xff;
	/* Limit: emu1010_input_dst, emu->emu1010.input_source */
	if (channel >= 22)
		return -EINVAL;
	if (emu->emu1010.input_source[channel] == val)
		return 0;
	emu->emu1010.input_source[channel] = val;
	if (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616)
		snd_emu1010_fpga_link_dst_src_write(emu,
			emu1010_input_dst[channel], emu1616_src_regs[val]);
	else
		snd_emu1010_fpga_link_dst_src_write(emu,
			emu1010_input_dst[channel], emu1010_src_regs[val]);
	return 1;
}

#define EMU1010_SOURCE_OUTPUT(xname,chid) \
{								\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,	\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,		\
	.info =  snd_emu1010_input_output_source_info,		\
	.get =   snd_emu1010_output_source_get,			\
	.put =   snd_emu1010_output_source_put,			\
	.private_value = chid					\
}

static struct snd_kcontrol_new snd_emu1010_output_enum_ctls[] __devinitdata = {
	EMU1010_SOURCE_OUTPUT("Dock DAC1 Left Playback Enum", 0),
	EMU1010_SOURCE_OUTPUT("Dock DAC1 Right Playback Enum", 1),
	EMU1010_SOURCE_OUTPUT("Dock DAC2 Left Playback Enum", 2),
	EMU1010_SOURCE_OUTPUT("Dock DAC2 Right Playback Enum", 3),
	EMU1010_SOURCE_OUTPUT("Dock DAC3 Left Playback Enum", 4),
	EMU1010_SOURCE_OUTPUT("Dock DAC3 Right Playback Enum", 5),
	EMU1010_SOURCE_OUTPUT("Dock DAC4 Left Playback Enum", 6),
	EMU1010_SOURCE_OUTPUT("Dock DAC4 Right Playback Enum", 7),
	EMU1010_SOURCE_OUTPUT("Dock Phones Left Playback Enum", 8),
	EMU1010_SOURCE_OUTPUT("Dock Phones Right Playback Enum", 9),
	EMU1010_SOURCE_OUTPUT("Dock SPDIF Left Playback Enum", 0xa),
	EMU1010_SOURCE_OUTPUT("Dock SPDIF Right Playback Enum", 0xb),
	EMU1010_SOURCE_OUTPUT("1010 SPDIF Left Playback Enum", 0xc),
	EMU1010_SOURCE_OUTPUT("1010 SPDIF Right Playback Enum", 0xd),
	EMU1010_SOURCE_OUTPUT("0202 DAC Left Playback Enum", 0xe),
	EMU1010_SOURCE_OUTPUT("0202 DAC Right Playback Enum", 0xf),
	EMU1010_SOURCE_OUTPUT("1010 ADAT 0 Playback Enum", 0x10),
	EMU1010_SOURCE_OUTPUT("1010 ADAT 1 Playback Enum", 0x11),
	EMU1010_SOURCE_OUTPUT("1010 ADAT 2 Playback Enum", 0x12),
	EMU1010_SOURCE_OUTPUT("1010 ADAT 3 Playback Enum", 0x13),
	EMU1010_SOURCE_OUTPUT("1010 ADAT 4 Playback Enum", 0x14),
	EMU1010_SOURCE_OUTPUT("1010 ADAT 5 Playback Enum", 0x15),
	EMU1010_SOURCE_OUTPUT("1010 ADAT 6 Playback Enum", 0x16),
	EMU1010_SOURCE_OUTPUT("1010 ADAT 7 Playback Enum", 0x17),
};


/* 1616(m) cardbus */
static struct snd_kcontrol_new snd_emu1616_output_enum_ctls[] __devinitdata = {
	EMU1010_SOURCE_OUTPUT("Dock DAC1 Left Playback Enum", 0),
	EMU1010_SOURCE_OUTPUT("Dock DAC1 Right Playback Enum", 1),
	EMU1010_SOURCE_OUTPUT("Dock DAC2 Left Playback Enum", 2),
	EMU1010_SOURCE_OUTPUT("Dock DAC2 Right Playback Enum", 3),
	EMU1010_SOURCE_OUTPUT("Dock DAC3 Left Playback Enum", 4),
	EMU1010_SOURCE_OUTPUT("Dock DAC3 Right Playback Enum", 5),
	EMU1010_SOURCE_OUTPUT("Dock SPDIF Left Playback Enum", 6),
	EMU1010_SOURCE_OUTPUT("Dock SPDIF Right Playback Enum", 7),
	EMU1010_SOURCE_OUTPUT("Dock ADAT 0 Playback Enum", 8),
	EMU1010_SOURCE_OUTPUT("Dock ADAT 1 Playback Enum", 9),
	EMU1010_SOURCE_OUTPUT("Dock ADAT 2 Playback Enum", 0xa),
	EMU1010_SOURCE_OUTPUT("Dock ADAT 3 Playback Enum", 0xb),
	EMU1010_SOURCE_OUTPUT("Dock ADAT 4 Playback Enum", 0xc),
	EMU1010_SOURCE_OUTPUT("Dock ADAT 5 Playback Enum", 0xd),
	EMU1010_SOURCE_OUTPUT("Dock ADAT 6 Playback Enum", 0xe),
	EMU1010_SOURCE_OUTPUT("Dock ADAT 7 Playback Enum", 0xf),
	EMU1010_SOURCE_OUTPUT("Mana DAC Left Playback Enum", 0x10),
	EMU1010_SOURCE_OUTPUT("Mana DAC Right Playback Enum", 0x11),
};


#define EMU1010_SOURCE_INPUT(xname,chid) \
{								\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,	\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,		\
	.info =  snd_emu1010_input_output_source_info,		\
	.get =   snd_emu1010_input_source_get,			\
	.put =   snd_emu1010_input_source_put,			\
	.private_value = chid					\
}

static struct snd_kcontrol_new snd_emu1010_input_enum_ctls[] __devinitdata = {
	EMU1010_SOURCE_INPUT("DSP 0 Capture Enum", 0),
	EMU1010_SOURCE_INPUT("DSP 1 Capture Enum", 1),
	EMU1010_SOURCE_INPUT("DSP 2 Capture Enum", 2),
	EMU1010_SOURCE_INPUT("DSP 3 Capture Enum", 3),
	EMU1010_SOURCE_INPUT("DSP 4 Capture Enum", 4),
	EMU1010_SOURCE_INPUT("DSP 5 Capture Enum", 5),
	EMU1010_SOURCE_INPUT("DSP 6 Capture Enum", 6),
	EMU1010_SOURCE_INPUT("DSP 7 Capture Enum", 7),
	EMU1010_SOURCE_INPUT("DSP 8 Capture Enum", 8),
	EMU1010_SOURCE_INPUT("DSP 9 Capture Enum", 9),
	EMU1010_SOURCE_INPUT("DSP A Capture Enum", 0xa),
	EMU1010_SOURCE_INPUT("DSP B Capture Enum", 0xb),
	EMU1010_SOURCE_INPUT("DSP C Capture Enum", 0xc),
	EMU1010_SOURCE_INPUT("DSP D Capture Enum", 0xd),
	EMU1010_SOURCE_INPUT("DSP E Capture Enum", 0xe),
	EMU1010_SOURCE_INPUT("DSP F Capture Enum", 0xf),
	EMU1010_SOURCE_INPUT("DSP 10 Capture Enum", 0x10),
	EMU1010_SOURCE_INPUT("DSP 11 Capture Enum", 0x11),
	EMU1010_SOURCE_INPUT("DSP 12 Capture Enum", 0x12),
	EMU1010_SOURCE_INPUT("DSP 13 Capture Enum", 0x13),
	EMU1010_SOURCE_INPUT("DSP 14 Capture Enum", 0x14),
	EMU1010_SOURCE_INPUT("DSP 15 Capture Enum", 0x15),
};



#define snd_emu1010_adc_pads_info	snd_ctl_boolean_mono_info

static int snd_emu1010_adc_pads_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int mask = kcontrol->private_value & 0xff;
	ucontrol->value.integer.value[0] = (emu->emu1010.adc_pads & mask) ? 1 : 0;
	return 0;
}

static int snd_emu1010_adc_pads_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int mask = kcontrol->private_value & 0xff;
	unsigned int val, cache;
	val = ucontrol->value.integer.value[0];
	cache = emu->emu1010.adc_pads;
	if (val == 1) 
		cache = cache | mask;
	else
		cache = cache & ~mask;
	if (cache != emu->emu1010.adc_pads) {
		snd_emu1010_fpga_write(emu, EMU_HANA_ADC_PADS, cache );
	        emu->emu1010.adc_pads = cache;
	}

	return 0;
}



#define EMU1010_ADC_PADS(xname,chid) \
{								\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,	\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,		\
	.info =  snd_emu1010_adc_pads_info,			\
	.get =   snd_emu1010_adc_pads_get,			\
	.put =   snd_emu1010_adc_pads_put,			\
	.private_value = chid					\
}

static struct snd_kcontrol_new snd_emu1010_adc_pads[] __devinitdata = {
	EMU1010_ADC_PADS("ADC1 14dB PAD Audio Dock Capture Switch", EMU_HANA_DOCK_ADC_PAD1),
	EMU1010_ADC_PADS("ADC2 14dB PAD Audio Dock Capture Switch", EMU_HANA_DOCK_ADC_PAD2),
	EMU1010_ADC_PADS("ADC3 14dB PAD Audio Dock Capture Switch", EMU_HANA_DOCK_ADC_PAD3),
	EMU1010_ADC_PADS("ADC1 14dB PAD 0202 Capture Switch", EMU_HANA_0202_ADC_PAD1),
};

#define snd_emu1010_dac_pads_info	snd_ctl_boolean_mono_info

static int snd_emu1010_dac_pads_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int mask = kcontrol->private_value & 0xff;
	ucontrol->value.integer.value[0] = (emu->emu1010.dac_pads & mask) ? 1 : 0;
	return 0;
}

static int snd_emu1010_dac_pads_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int mask = kcontrol->private_value & 0xff;
	unsigned int val, cache;
	val = ucontrol->value.integer.value[0];
	cache = emu->emu1010.dac_pads;
	if (val == 1) 
		cache = cache | mask;
	else
		cache = cache & ~mask;
	if (cache != emu->emu1010.dac_pads) {
		snd_emu1010_fpga_write(emu, EMU_HANA_DAC_PADS, cache );
	        emu->emu1010.dac_pads = cache;
	}

	return 0;
}



#define EMU1010_DAC_PADS(xname,chid) \
{								\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,	\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,		\
	.info =  snd_emu1010_dac_pads_info,			\
	.get =   snd_emu1010_dac_pads_get,			\
	.put =   snd_emu1010_dac_pads_put,			\
	.private_value = chid					\
}

static struct snd_kcontrol_new snd_emu1010_dac_pads[] __devinitdata = {
	EMU1010_DAC_PADS("DAC1 Audio Dock 14dB PAD Playback Switch", EMU_HANA_DOCK_DAC_PAD1),
	EMU1010_DAC_PADS("DAC2 Audio Dock 14dB PAD Playback Switch", EMU_HANA_DOCK_DAC_PAD2),
	EMU1010_DAC_PADS("DAC3 Audio Dock 14dB PAD Playback Switch", EMU_HANA_DOCK_DAC_PAD3),
	EMU1010_DAC_PADS("DAC4 Audio Dock 14dB PAD Playback Switch", EMU_HANA_DOCK_DAC_PAD4),
	EMU1010_DAC_PADS("DAC1 0202 14dB PAD Playback Switch", EMU_HANA_0202_DAC_PAD1),
};


static int snd_emu1010_internal_clock_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = {
		"44100", "48000", "SPDIF", "ADAT"
	};
		
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
	
	
}

static int snd_emu1010_internal_clock_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->emu1010.internal_clock;
	return 0;
}

static int snd_emu1010_internal_clock_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	val = ucontrol->value.enumerated.item[0] ;
	/* Limit: uinfo->value.enumerated.items = 4; */
	if (val >= 4)
		return -EINVAL;
	change = (emu->emu1010.internal_clock != val);
	if (change) {
		emu->emu1010.internal_clock = val;
		switch (val) {
		case 0:
			/* 44100 */
			/* Mute all */
			snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_MUTE );
			/* Default fallback clock 48kHz */
			snd_emu1010_fpga_write(emu, EMU_HANA_DEFCLOCK, EMU_HANA_DEFCLOCK_44_1K );
			/* Word Clock source, Internal 44.1kHz x1 */
			snd_emu1010_fpga_write(emu, EMU_HANA_WCLOCK,
			EMU_HANA_WCLOCK_INT_44_1K | EMU_HANA_WCLOCK_1X );
			/* Set LEDs on Audio Dock */
			snd_emu1010_fpga_write(emu, EMU_HANA_DOCK_LEDS_2,
				EMU_HANA_DOCK_LEDS_2_44K | EMU_HANA_DOCK_LEDS_2_LOCK );
			/* Allow DLL to settle */
			msleep(10);
			/* Unmute all */
			snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_UNMUTE );
			break;
		case 1:
			/* 48000 */
			/* Mute all */
			snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_MUTE );
			/* Default fallback clock 48kHz */
			snd_emu1010_fpga_write(emu, EMU_HANA_DEFCLOCK, EMU_HANA_DEFCLOCK_48K );
			/* Word Clock source, Internal 48kHz x1 */
			snd_emu1010_fpga_write(emu, EMU_HANA_WCLOCK,
				EMU_HANA_WCLOCK_INT_48K | EMU_HANA_WCLOCK_1X );
			/* Set LEDs on Audio Dock */
			snd_emu1010_fpga_write(emu, EMU_HANA_DOCK_LEDS_2,
				EMU_HANA_DOCK_LEDS_2_48K | EMU_HANA_DOCK_LEDS_2_LOCK );
			/* Allow DLL to settle */
			msleep(10);
			/* Unmute all */
			snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_UNMUTE );
			break;
			
		case 2: /* Take clock from S/PDIF IN */
			/* Mute all */
			snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_MUTE );
			/* Default fallback clock 48kHz */
			snd_emu1010_fpga_write(emu, EMU_HANA_DEFCLOCK, EMU_HANA_DEFCLOCK_48K );
			/* Word Clock source, sync to S/PDIF input */
			snd_emu1010_fpga_write(emu, EMU_HANA_WCLOCK,
				EMU_HANA_WCLOCK_HANA_SPDIF_IN | EMU_HANA_WCLOCK_1X );
			/* Set LEDs on Audio Dock */
			snd_emu1010_fpga_write(emu, EMU_HANA_DOCK_LEDS_2,
				EMU_HANA_DOCK_LEDS_2_EXT | EMU_HANA_DOCK_LEDS_2_LOCK );
			/* FIXME: We should set EMU_HANA_DOCK_LEDS_2_LOCK only when clock signal is present and valid */	
			/* Allow DLL to settle */
			msleep(10);
			/* Unmute all */
			snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_UNMUTE );
			break;
		
		case 3: 			
			/* Take clock from ADAT IN */
			/* Mute all */
			snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_MUTE );
			/* Default fallback clock 48kHz */
			snd_emu1010_fpga_write(emu, EMU_HANA_DEFCLOCK, EMU_HANA_DEFCLOCK_48K );
			/* Word Clock source, sync to ADAT input */
			snd_emu1010_fpga_write(emu, EMU_HANA_WCLOCK,
				EMU_HANA_WCLOCK_HANA_ADAT_IN | EMU_HANA_WCLOCK_1X );
			/* Set LEDs on Audio Dock */
			snd_emu1010_fpga_write(emu, EMU_HANA_DOCK_LEDS_2, EMU_HANA_DOCK_LEDS_2_EXT | EMU_HANA_DOCK_LEDS_2_LOCK );
			/* FIXME: We should set EMU_HANA_DOCK_LEDS_2_LOCK only when clock signal is present and valid */	
			/* Allow DLL to settle */
			msleep(10);
			/*   Unmute all */
			snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_UNMUTE );
			 
			
			break;		
		}
	}
        return change;
}

static struct snd_kcontrol_new snd_emu1010_internal_clock =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Clock Internal Rate",
	.count =	1,
	.info =         snd_emu1010_internal_clock_info,
	.get =          snd_emu1010_internal_clock_get,
	.put =          snd_emu1010_internal_clock_put
};

static int snd_audigy_i2c_capture_source_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
#if 0
	static char *texts[4] = {
		"Unknown1", "Unknown2", "Mic", "Line"
	};
#endif
	static char *texts[2] = {
		"Mic", "Line"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
                uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
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
	u32 gpio;
	int change = 0;
	unsigned long flags;
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
		spin_lock_irqsave(&emu->emu_lock, flags);
		gpio = inl(emu->port + A_IOCFG);
		if (source_id==0)
			outl(gpio | 0x4, emu->port + A_IOCFG);
		else
			outl(gpio & ~0x4, emu->port + A_IOCFG);
		spin_unlock_irqrestore(&emu->emu_lock, flags);

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

static struct snd_kcontrol_new snd_audigy_i2c_capture_source =
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
	unsigned int ngain;
	unsigned int source_id;
	int change = 0;

	source_id = kcontrol->private_value;
	/* Limit: emu->i2c_capture_volume */
        /*        capture_source: uinfo->value.enumerated.items = 2 */
	if (source_id >= 2)
		return -EINVAL;
	ogain = emu->i2c_capture_volume[source_id][0]; /* Left */
	ngain = ucontrol->value.integer.value[0];
	if (ngain > 0xff)
		return 0;
	if (ogain != ngain) {
		if (emu->i2c_capture_source == source_id)
			snd_emu10k1_i2c_write(emu, ADC_ATTEN_ADCL, ((ngain) & 0xff) );
		emu->i2c_capture_volume[source_id][0] = ngain;
		change = 1;
	}
	ogain = emu->i2c_capture_volume[source_id][1]; /* Right */
	ngain = ucontrol->value.integer.value[1];
	if (ngain > 0xff)
		return 0;
	if (ogain != ngain) {
		if (emu->i2c_capture_source == source_id)
			snd_emu10k1_i2c_write(emu, ADC_ATTEN_ADCR, ((ngain) & 0xff));
		emu->i2c_capture_volume[source_id][1] = ngain;
		change = 1;
	}

	return change;
}

#define I2C_VOLUME(xname,chid) \
{								\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,	\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |		\
	          SNDRV_CTL_ELEM_ACCESS_TLV_READ,		\
	.info =  snd_audigy_i2c_volume_info,			\
	.get =   snd_audigy_i2c_volume_get,			\
	.put =   snd_audigy_i2c_volume_put,			\
	.tlv = { .p = snd_audigy_db_scale2 },			\
	.private_value = chid					\
}


static struct snd_kcontrol_new snd_audigy_i2c_volume_ctls[] __devinitdata = {
	I2C_VOLUME("Mic Capture Volume", 0),
	I2C_VOLUME("Line Capture Volume", 0)
};

#if 0
static int snd_audigy_spdif_output_rate_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"44100", "48000", "96000"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_audigy_spdif_output_rate_get(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int tmp;
	unsigned long flags;
	

	spin_lock_irqsave(&emu->reg_lock, flags);
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
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return 0;
}

static int snd_audigy_spdif_output_rate_put(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int reg, val, tmp;
	unsigned long flags;

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

	
	spin_lock_irqsave(&emu->reg_lock, flags);
	reg = snd_emu10k1_ptr_read(emu, A_SPDIF_SAMPLERATE, 0);
	tmp = reg & ~A_SPDIF_RATE_MASK;
	tmp |= val;
	if ((change = (tmp != reg)))
		snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, 0, tmp);
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return change;
}

static struct snd_kcontrol_new snd_audigy_spdif_output_rate =
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
	unsigned long flags;

	/* Limit: emu->spdif_bits */
	if (idx >= 3)
		return -EINVAL;
	val = (ucontrol->value.iec958.status[0] << 0) |
	      (ucontrol->value.iec958.status[1] << 8) |
	      (ucontrol->value.iec958.status[2] << 16) |
	      (ucontrol->value.iec958.status[3] << 24);
	spin_lock_irqsave(&emu->reg_lock, flags);
	change = val != emu->spdif_bits[idx];
	if (change) {
		snd_emu10k1_ptr_write(emu, SPCS0 + idx, 0, val);
		emu->spdif_bits[idx] = val;
	}
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return change;
}

static struct snd_kcontrol_new snd_emu10k1_spdif_mask_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	.count =	3,
	.info =         snd_emu10k1_spdif_info,
	.get =          snd_emu10k1_spdif_get_mask
};

static struct snd_kcontrol_new snd_emu10k1_spdif_control =
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
		snd_emu10k1_ptr_write(emu, A_FXRT1, voice,
				      snd_emu10k1_compose_audigy_fxrt1(route));
		snd_emu10k1_ptr_write(emu, A_FXRT2, voice,
				      snd_emu10k1_compose_audigy_fxrt2(route));
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
		unsigned int val = ((unsigned int)volume[4] << 24) |
			((unsigned int)volume[5] << 16) |
			((unsigned int)volume[6] << 8) |
			(unsigned int)volume[7];
		snd_emu10k1_ptr_write(emu, A_SENDAMOUNTS, voice, val);
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
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int voice, idx;
	int num_efx = emu->audigy ? 8 : 4;
	int mask = emu->audigy ? 0x3f : 0x0f;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (voice = 0; voice < 3; voice++)
		for (idx = 0; idx < num_efx; idx++)
			ucontrol->value.integer.value[(voice * num_efx) + idx] = 
				mix->send_routing[voice][idx] & mask;
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return 0;
}

static int snd_emu10k1_send_routing_put(struct snd_kcontrol *kcontrol,
                                        struct snd_ctl_elem_value *ucontrol)
{
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int change = 0, voice, idx, val;
	int num_efx = emu->audigy ? 8 : 4;
	int mask = emu->audigy ? 0x3f : 0x0f;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (voice = 0; voice < 3; voice++)
		for (idx = 0; idx < num_efx; idx++) {
			val = ucontrol->value.integer.value[(voice * num_efx) + idx] & mask;
			if (mix->send_routing[voice][idx] != val) {
				mix->send_routing[voice][idx] = val;
				change = 1;
			}
		}	
	if (change && mix->epcm) {
		if (mix->epcm->voices[0] && mix->epcm->voices[1]) {
			update_emu10k1_fxrt(emu, mix->epcm->voices[0]->number,
					    &mix->send_routing[1][0]);
			update_emu10k1_fxrt(emu, mix->epcm->voices[1]->number,
					    &mix->send_routing[2][0]);
		} else if (mix->epcm->voices[0]) {
			update_emu10k1_fxrt(emu, mix->epcm->voices[0]->number,
					    &mix->send_routing[0][0]);
		}
	}
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return change;
}

static struct snd_kcontrol_new snd_emu10k1_send_routing_control =
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
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int idx;
	int num_efx = emu->audigy ? 8 : 4;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < 3*num_efx; idx++)
		ucontrol->value.integer.value[idx] = mix->send_volume[idx/num_efx][idx%num_efx];
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return 0;
}

static int snd_emu10k1_send_volume_put(struct snd_kcontrol *kcontrol,
                                       struct snd_ctl_elem_value *ucontrol)
{
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int change = 0, idx, val;
	int num_efx = emu->audigy ? 8 : 4;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < 3*num_efx; idx++) {
		val = ucontrol->value.integer.value[idx] & 255;
		if (mix->send_volume[idx/num_efx][idx%num_efx] != val) {
			mix->send_volume[idx/num_efx][idx%num_efx] = val;
			change = 1;
		}
	}
	if (change && mix->epcm) {
		if (mix->epcm->voices[0] && mix->epcm->voices[1]) {
			update_emu10k1_send_volume(emu, mix->epcm->voices[0]->number,
						   &mix->send_volume[1][0]);
			update_emu10k1_send_volume(emu, mix->epcm->voices[1]->number,
						   &mix->send_volume[2][0]);
		} else if (mix->epcm->voices[0]) {
			update_emu10k1_send_volume(emu, mix->epcm->voices[0]->number,
						   &mix->send_volume[0][0]);
		}
	}
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return change;
}

static struct snd_kcontrol_new snd_emu10k1_send_volume_control =
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
	uinfo->value.integer.max = 0xffff;
	return 0;
}

static int snd_emu10k1_attn_get(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	unsigned long flags;
	int idx;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < 3; idx++)
		ucontrol->value.integer.value[idx] = mix->attn[idx];
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return 0;
}

static int snd_emu10k1_attn_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int change = 0, idx, val;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < 3; idx++) {
		val = ucontrol->value.integer.value[idx] & 0xffff;
		if (mix->attn[idx] != val) {
			mix->attn[idx] = val;
			change = 1;
		}
	}
	if (change && mix->epcm) {
		if (mix->epcm->voices[0] && mix->epcm->voices[1]) {
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[0]->number, mix->attn[1]);
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[1]->number, mix->attn[2]);
		} else if (mix->epcm->voices[0]) {
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[0]->number, mix->attn[0]);
		}
	}
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return change;
}

static struct snd_kcontrol_new snd_emu10k1_attn_control =
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
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->efx_pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int idx;
	int num_efx = emu->audigy ? 8 : 4;
	int mask = emu->audigy ? 0x3f : 0x0f;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < num_efx; idx++)
		ucontrol->value.integer.value[idx] = 
			mix->send_routing[0][idx] & mask;
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return 0;
}

static int snd_emu10k1_efx_send_routing_put(struct snd_kcontrol *kcontrol,
                                        struct snd_ctl_elem_value *ucontrol)
{
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int ch = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct snd_emu10k1_pcm_mixer *mix = &emu->efx_pcm_mixer[ch];
	int change = 0, idx, val;
	int num_efx = emu->audigy ? 8 : 4;
	int mask = emu->audigy ? 0x3f : 0x0f;

	spin_lock_irqsave(&emu->reg_lock, flags);
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
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return change;
}

static struct snd_kcontrol_new snd_emu10k1_efx_send_routing_control =
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
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->efx_pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	int idx;
	int num_efx = emu->audigy ? 8 : 4;

	spin_lock_irqsave(&emu->reg_lock, flags);
	for (idx = 0; idx < num_efx; idx++)
		ucontrol->value.integer.value[idx] = mix->send_volume[0][idx];
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return 0;
}

static int snd_emu10k1_efx_send_volume_put(struct snd_kcontrol *kcontrol,
                                       struct snd_ctl_elem_value *ucontrol)
{
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int ch = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct snd_emu10k1_pcm_mixer *mix = &emu->efx_pcm_mixer[ch];
	int change = 0, idx, val;
	int num_efx = emu->audigy ? 8 : 4;

	spin_lock_irqsave(&emu->reg_lock, flags);
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
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return change;
}


static struct snd_kcontrol_new snd_emu10k1_efx_send_volume_control =
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
	uinfo->value.integer.max = 0xffff;
	return 0;
}

static int snd_emu10k1_efx_attn_get(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	struct snd_emu10k1_pcm_mixer *mix =
		&emu->efx_pcm_mixer[snd_ctl_get_ioffidx(kcontrol, &ucontrol->id)];
	unsigned long flags;

	spin_lock_irqsave(&emu->reg_lock, flags);
	ucontrol->value.integer.value[0] = mix->attn[0];
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return 0;
}

static int snd_emu10k1_efx_attn_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int ch = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct snd_emu10k1_pcm_mixer *mix = &emu->efx_pcm_mixer[ch];
	int change = 0, val;

	spin_lock_irqsave(&emu->reg_lock, flags);
	val = ucontrol->value.integer.value[0] & 0xffff;
	if (mix->attn[0] != val) {
		mix->attn[0] = val;
		change = 1;
	}
	if (change && mix->epcm) {
		if (mix->epcm->voices[ch]) {
			snd_emu10k1_ptr_write(emu, VTFT_VOLUMETARGET, mix->epcm->voices[ch]->number, mix->attn[0]);
		}
	}
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return change;
}

static struct snd_kcontrol_new snd_emu10k1_efx_attn_control =
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
		ucontrol->value.integer.value[0] = inl(emu->port + A_IOCFG) & A_IOCFG_GPOUT0 ? 1 : 0;
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
	unsigned long flags;
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int reg, val, sw;
	int change = 0;

	sw = ucontrol->value.integer.value[0];
	if (emu->card_capabilities->invert_shared_spdif)
		sw = !sw;
	spin_lock_irqsave(&emu->reg_lock, flags);
	if ( emu->card_capabilities->i2c_adc) {
		/* Do nothing for Audigy 2 ZS Notebook */
	} else if (emu->audigy) {
		reg = inl(emu->port + A_IOCFG);
		val = sw ? A_IOCFG_GPOUT0 : 0;
		change = (reg & A_IOCFG_GPOUT0) != val;
		if (change) {
			reg &= ~A_IOCFG_GPOUT0;
			reg |= val;
			outl(reg | val, emu->port + A_IOCFG);
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
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return change;
}

static struct snd_kcontrol_new snd_emu10k1_shared_spdif __devinitdata =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"SB Live Analog/Digital Output Jack",
	.info =		snd_emu10k1_shared_spdif_info,
	.get =		snd_emu10k1_shared_spdif_get,
	.put =		snd_emu10k1_shared_spdif_put
};

static struct snd_kcontrol_new snd_audigy_shared_spdif __devinitdata =
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

static struct snd_kcontrol_new snd_audigy_capture_boost __devinitdata =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Analog Capture Boost",
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

static struct snd_kcontrol *ctl_find(struct snd_card *card, const char *name)
{
	struct snd_ctl_elem_id sid;
	memset(&sid, 0, sizeof(sid));
	strcpy(sid.name, name);
	sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_find_id(card, &sid);
}

static int rename_ctl(struct snd_card *card, const char *src, const char *dst)
{
	struct snd_kcontrol *kctl = ctl_find(card, src);
	if (kctl) {
		strcpy(kctl->id.name, dst);
		return 0;
	}
	return -ENOENT;
}

int __devinit snd_emu10k1_mixer(struct snd_emu10k1 *emu,
				int pcm_device, int multi_device)
{
	int err, pcm;
	struct snd_kcontrol *kctl;
	struct snd_card *card = emu->card;
	char **c;
	static char *emu10k1_remove_ctls[] = {
		/* no AC97 mono, surround, center/lfe */
		"Master Mono Playback Switch",
		"Master Mono Playback Volume",
		"PCM Out Path & Mute",
		"Mono Output Select",
		"Front Playback Switch",
		"Front Playback Volume",
		"Surround Playback Switch",
		"Surround Playback Volume",
		"Center Playback Switch",
		"Center Playback Volume",
		"LFE Playback Switch",
		"LFE Playback Volume",
		NULL
	};
	static char *emu10k1_rename_ctls[] = {
		"Surround Digital Playback Volume", "Surround Playback Volume",
		"Center Digital Playback Volume", "Center Playback Volume",
		"LFE Digital Playback Volume", "LFE Playback Volume",
		NULL
	};
	static char *audigy_remove_ctls[] = {
		/* Master/PCM controls on ac97 of Audigy has no effect */
		/* On the Audigy2 the AC97 playback is piped into
		 * the Philips ADC for 24bit capture */
		"PCM Playback Switch",
		"PCM Playback Volume",
		"Master Mono Playback Switch",
		"Master Mono Playback Volume",
		"Master Playback Switch",
		"Master Playback Volume",
		"PCM Out Path & Mute",
		"Mono Output Select",
		/* remove unused AC97 capture controls */
		"Capture Source",
		"Capture Switch",
		"Capture Volume",
		"Mic Select",
		"Video Playback Switch",
		"Video Playback Volume",
		"Mic Playback Switch",
		"Mic Playback Volume",
		NULL
	};
	static char *audigy_rename_ctls[] = {
		/* use conventional names */
		"Wave Playback Volume", "PCM Playback Volume",
		/* "Wave Capture Volume", "PCM Capture Volume", */
		"Wave Master Playback Volume", "Master Playback Volume",
		"AMic Playback Volume", "Mic Playback Volume",
		NULL
	};
	static char *audigy_rename_ctls_i2c_adc[] = {
		//"Analog Mix Capture Volume","OLD Analog Mix Capture Volume",
		"Line Capture Volume", "Analog Mix Capture Volume",
		"Wave Playback Volume", "OLD PCM Playback Volume",
		"Wave Master Playback Volume", "Master Playback Volume",
		"AMic Playback Volume", "Old Mic Playback Volume",
		"CD Capture Volume", "IEC958 Optical Capture Volume",
		NULL
	};
	static char *audigy_remove_ctls_i2c_adc[] = {
		/* On the Audigy2 ZS Notebook
		 * Capture via WM8775  */
		"Mic Capture Volume",
		"Analog Mix Capture Volume",
		"Aux Capture Volume",
		"IEC958 Optical Capture Volume",
		NULL
	};
	static char *audigy_remove_ctls_1361t_adc[] = {
		/* On the Audigy2 the AC97 playback is piped into
		 * the Philips ADC for 24bit capture */
		"PCM Playback Switch",
		"PCM Playback Volume",
		"Master Mono Playback Switch",
		"Master Mono Playback Volume",
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
	static char *audigy_rename_ctls_1361t_adc[] = {
		"Master Playback Switch", "Master Capture Switch",
		"Master Playback Volume", "Master Capture Volume",
		"Wave Master Playback Volume", "Master Playback Volume",
		"PC Speaker Playback Switch", "PC Speaker Capture Switch",
		"PC Speaker Playback Volume", "PC Speaker Capture Volume",
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

		NULL
	};

	if (emu->card_capabilities->ac97_chip) {
		struct snd_ac97_bus *pbus;
		struct snd_ac97_template ac97;
		static struct snd_ac97_bus_ops ops = {
			.write = snd_emu10k1_ac97_write,
			.read = snd_emu10k1_ac97_read,
		};

		if ((err = snd_ac97_bus(emu->card, 0, &ops, NULL, &pbus)) < 0)
			return err;
		pbus->no_vra = 1; /* we don't need VRA */
		
		memset(&ac97, 0, sizeof(ac97));
		ac97.private_data = emu;
		ac97.private_free = snd_emu10k1_mixer_free_ac97;
		ac97.scaps = AC97_SCAP_NO_SPDIF;
		if ((err = snd_ac97_mixer(pbus, &ac97, &emu->ac97)) < 0) {
			if (emu->card_capabilities->ac97_chip == 1)
				return err;
			snd_printd(KERN_INFO "emu10k1: AC97 is optional on this board\n");
			snd_printd(KERN_INFO"          Proceeding without ac97 mixers...\n");
			snd_device_free(emu->card, pbus);
			goto no_ac97; /* FIXME: get rid of ugly gotos.. */
		}
		if (emu->audigy) {
			/* set master volume to 0 dB */
			snd_ac97_write_cache(emu->ac97, AC97_MASTER, 0x0000);
			/* set capture source to mic */
			snd_ac97_write_cache(emu->ac97, AC97_REC_SEL, 0x0000);
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

	if (emu->card_capabilities->subsystem == 0x20071102) {  /* Audigy 4 Pro */
		rename_ctl(card, "Line2 Capture Volume", "Line1/Mic Capture Volume");
		rename_ctl(card, "Analog Mix Capture Volume", "Line2 Capture Volume");
		rename_ctl(card, "Aux2 Capture Volume", "Line3 Capture Volume");
		rename_ctl(card, "Mic Capture Volume", "Unknown1 Capture Volume");
		remove_ctl(card, "Headphone Playback Switch");
		remove_ctl(card, "Headphone Playback Volume");
		remove_ctl(card, "3D Control - Center");
		remove_ctl(card, "3D Control - Depth");
		remove_ctl(card, "3D Control - Switch");
	}
	if ((kctl = emu->ctl_send_routing = snd_ctl_new1(&snd_emu10k1_send_routing_control, emu)) == NULL)
		return -ENOMEM;
	kctl->id.device = pcm_device;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
	if ((kctl = emu->ctl_send_volume = snd_ctl_new1(&snd_emu10k1_send_volume_control, emu)) == NULL)
		return -ENOMEM;
	kctl->id.device = pcm_device;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
	if ((kctl = emu->ctl_attn = snd_ctl_new1(&snd_emu10k1_attn_control, emu)) == NULL)
		return -ENOMEM;
	kctl->id.device = pcm_device;
	if ((err = snd_ctl_add(card, kctl)))
		return err;

	if ((kctl = emu->ctl_efx_send_routing = snd_ctl_new1(&snd_emu10k1_efx_send_routing_control, emu)) == NULL)
		return -ENOMEM;
	kctl->id.device = multi_device;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
	
	if ((kctl = emu->ctl_efx_send_volume = snd_ctl_new1(&snd_emu10k1_efx_send_volume_control, emu)) == NULL)
		return -ENOMEM;
	kctl->id.device = multi_device;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
	
	if ((kctl = emu->ctl_efx_attn = snd_ctl_new1(&snd_emu10k1_efx_attn_control, emu)) == NULL)
		return -ENOMEM;
	kctl->id.device = multi_device;
	if ((err = snd_ctl_add(card, kctl)))
		return err;

	/* initialize the routing and volume table for each pcm playback stream */
	for (pcm = 0; pcm < 32; pcm++) {
		struct snd_emu10k1_pcm_mixer *mix;
		int v;
		
		mix = &emu->pcm_mixer[pcm];
		mix->epcm = NULL;

		for (v = 0; v < 4; v++)
			mix->send_routing[0][v] = 
				mix->send_routing[1][v] = 
				mix->send_routing[2][v] = v;
		
		memset(&mix->send_volume, 0, sizeof(mix->send_volume));
		mix->send_volume[0][0] = mix->send_volume[0][1] =
		mix->send_volume[1][0] = mix->send_volume[2][1] = 255;
		
		mix->attn[0] = mix->attn[1] = mix->attn[2] = 0xffff;
	}
	
	/* initialize the routing and volume table for the multichannel playback stream */
	for (pcm = 0; pcm < NUM_EFX_PLAYBACK; pcm++) {
		struct snd_emu10k1_pcm_mixer *mix;
		int v;
		
		mix = &emu->efx_pcm_mixer[pcm];
		mix->epcm = NULL;

		mix->send_routing[0][0] = pcm;
		mix->send_routing[0][1] = (pcm == 0) ? 1 : 0;
		for (v = 0; v < 2; v++)
			mix->send_routing[0][2+v] = 13+v;
		if (emu->audigy)
			for (v = 0; v < 4; v++)
				mix->send_routing[0][4+v] = 60+v;
		
		memset(&mix->send_volume, 0, sizeof(mix->send_volume));
		mix->send_volume[0][0]  = 255;
		
		mix->attn[0] = 0xffff;
	}
	
	if (! emu->card_capabilities->ecard) { /* FIXME: APS has these controls? */
		/* sb live! and audigy */
		if ((kctl = snd_ctl_new1(&snd_emu10k1_spdif_mask_control, emu)) == NULL)
			return -ENOMEM;
		if (!emu->audigy)
			kctl->id.device = emu->pcm_efx->device;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
		if ((kctl = snd_ctl_new1(&snd_emu10k1_spdif_control, emu)) == NULL)
			return -ENOMEM;
		if (!emu->audigy)
			kctl->id.device = emu->pcm_efx->device;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
	}

	if (emu->card_capabilities->emu_model) {
		;  /* Disable the snd_audigy_spdif_shared_spdif */
	} else if (emu->audigy) {
		if ((kctl = snd_ctl_new1(&snd_audigy_shared_spdif, emu)) == NULL)
			return -ENOMEM;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
#if 0
		if ((kctl = snd_ctl_new1(&snd_audigy_spdif_output_rate, emu)) == NULL)
			return -ENOMEM;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
#endif
	} else if (! emu->card_capabilities->ecard) {
		/* sb live! */
		if ((kctl = snd_ctl_new1(&snd_emu10k1_shared_spdif, emu)) == NULL)
			return -ENOMEM;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
	}
	if (emu->card_capabilities->ca0151_chip) { /* P16V */
		if ((err = snd_p16v_mixer(emu)))
			return err;
	}

	if (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616) {
		/* 1616(m) cardbus */
		int i;

		for (i = 0; i < ARRAY_SIZE(snd_emu1616_output_enum_ctls); i++) {
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1616_output_enum_ctls[i],
					     emu));
			if (err < 0)
				return err;
		}
		for (i = 0; i < ARRAY_SIZE(snd_emu1010_input_enum_ctls); i++) {
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1010_input_enum_ctls[i],
					     emu));
			if (err < 0)
				return err;
		}
		for (i = 0; i < ARRAY_SIZE(snd_emu1010_adc_pads) - 2; i++) {
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1010_adc_pads[i], emu));
			if (err < 0)
				return err;
		}
		for (i = 0; i < ARRAY_SIZE(snd_emu1010_dac_pads) - 2; i++) {
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1010_dac_pads[i], emu));
			if (err < 0)
				return err;
		}
		err = snd_ctl_add(card,
			snd_ctl_new1(&snd_emu1010_internal_clock, emu));
		if (err < 0)
			return err;

	} else if (emu->card_capabilities->emu_model) {
		/* all other e-mu cards for now */
		int i;

		for (i = 0; i < ARRAY_SIZE(snd_emu1010_output_enum_ctls); i++) {
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1010_output_enum_ctls[i],
					     emu));
			if (err < 0)
				return err;
		}
		for (i = 0; i < ARRAY_SIZE(snd_emu1010_input_enum_ctls); i++) {
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1010_input_enum_ctls[i],
					     emu));
			if (err < 0)
				return err;
		}
		for (i = 0; i < ARRAY_SIZE(snd_emu1010_adc_pads); i++) {
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1010_adc_pads[i], emu));
			if (err < 0)
				return err;
		}
		for (i = 0; i < ARRAY_SIZE(snd_emu1010_dac_pads); i++) {
			err = snd_ctl_add(card,
				snd_ctl_new1(&snd_emu1010_dac_pads[i], emu));
			if (err < 0)
				return err;
		}
		err = snd_ctl_add(card,
			snd_ctl_new1(&snd_emu1010_internal_clock, emu));
		if (err < 0)
			return err;
	}

	if ( emu->card_capabilities->i2c_adc) {
		int i;

		err = snd_ctl_add(card, snd_ctl_new1(&snd_audigy_i2c_capture_source, emu));
		if (err < 0)
			return err;

		for (i = 0; i < ARRAY_SIZE(snd_audigy_i2c_volume_ctls); i++) {
			err = snd_ctl_add(card, snd_ctl_new1(&snd_audigy_i2c_volume_ctls[i], emu));
			if (err < 0)
				return err;
		}
	}
		
	if (emu->card_capabilities->ac97_chip && emu->audigy) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_audigy_capture_boost,
						     emu));
		if (err < 0)
			return err;
	}

	return 0;
}

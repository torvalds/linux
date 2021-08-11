// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HD audio interface patch for Cirrus Logic CS8409 HDA bridge chip
 *
 * Copyright (C) 2021 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
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

#include "patch_cs8409.h"

static int cs8409_parse_auto_config(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;
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
			snd_hda_gen_fix_pin_power(codec, spec->gen.adc_nids[idx]);
			done |= 1 << idx;
		}
	}

	return 0;
}

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

static struct cs8409_spec *cs8409_alloc_spec(struct hda_codec *codec)
{
	struct cs8409_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;
	codec->spec = spec;
	codec->power_save_node = 1;
	snd_hda_gen_spec_init(&spec->gen);

	return spec;
}

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

static inline int cs8409_vendor_coef_get(struct hda_codec *codec, unsigned int idx)
{
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_COEF_INDEX, idx);
	return snd_hda_codec_read(codec, CS8409_VENDOR_NID, 0, AC_VERB_GET_PROC_COEF, 0);
}

static inline void cs8409_vendor_coef_set(struct hda_codec *codec, unsigned int idx,
					  unsigned int coef)
{
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_COEF_INDEX, idx);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_COEF, coef);
}

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

	retval = cs8409_vendor_coef_get(codec, 0x0);
	newval = (enable) ? (retval | 0x8) : (retval & 0xfffffff7);
	cs8409_vendor_coef_set(codec, 0x0, newval);
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
		retval = cs8409_vendor_coef_get(codec, CIR_I2C_STATUS);
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
static int cs8409_i2c_read(struct hda_codec *codec, unsigned int i2c_address, unsigned int i2c_reg,
			   unsigned int paged)
{
	unsigned int i2c_reg_data;
	unsigned int read_data;

	cs8409_enable_i2c_clock(codec, 1);
	cs8409_vendor_coef_set(codec, CIR_I2C_ADDR, i2c_address);

	if (paged) {
		cs8409_vendor_coef_set(codec, CIR_I2C_QWRITE, i2c_reg >> 8);
		if (cs8409_i2c_wait_complete(codec) < 0) {
			codec_err(codec, "%s() Paged Transaction Failed 0x%02x : 0x%04x\n",
				__func__, i2c_address, i2c_reg);
			return -EIO;
		}
	}

	i2c_reg_data = (i2c_reg << 8) & 0x0ffff;
	cs8409_vendor_coef_set(codec, CIR_I2C_QREAD, i2c_reg_data);
	if (cs8409_i2c_wait_complete(codec) < 0) {
		codec_err(codec, "%s() Transaction Failed 0x%02x : 0x%04x\n",
			  __func__, i2c_address, i2c_reg);
		return -EIO;
	}

	/* Register in bits 15-8 and the data in 7-0 */
	read_data = cs8409_vendor_coef_get(codec, CIR_I2C_QREAD);

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
static int cs8409_i2c_write(struct hda_codec *codec, unsigned int i2c_address, unsigned int i2c_reg,
			    unsigned int i2c_data, unsigned int paged)
{
	unsigned int i2c_reg_data;

	cs8409_enable_i2c_clock(codec, 1);
	cs8409_vendor_coef_set(codec, CIR_I2C_ADDR, i2c_address);

	if (paged) {
		cs8409_vendor_coef_set(codec, CIR_I2C_QWRITE, i2c_reg >> 8);
		if (cs8409_i2c_wait_complete(codec) < 0) {
			codec_err(codec, "%s() Paged Transaction Failed 0x%02x : 0x%04x\n",
				__func__, i2c_address, i2c_reg);
			return -EIO;
		}
	}

	i2c_reg_data = ((i2c_reg << 8) & 0x0ff00) | (i2c_data & 0x0ff);
	cs8409_vendor_coef_set(codec, CIR_I2C_QWRITE, i2c_reg_data);

	if (cs8409_i2c_wait_complete(codec) < 0) {
		codec_err(codec, "%s() Transaction Failed 0x%02x : 0x%04x\n",
			__func__, i2c_address, i2c_reg);
		return -EIO;
	}

	cs8409_enable_i2c_clock(codec, 0);

	return 0;
}

static int cs8409_cs42l42_volume_info(struct snd_kcontrol *kctrl, struct snd_ctl_elem_info *uinfo)
{
	u16 nid = get_amp_nid(kctrl);
	u8 chs = get_amp_channels(kctrl);

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
	struct cs8409_spec *spec = codec->spec;
	int data;

	mutex_lock(&spec->cs8409_i2c_mux);
	data = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_HS_VOLUME_CHA, 1);
	if (data >= 0)
		spec->cs42l42_hp_volume[0] = -data;
	else
		spec->cs42l42_hp_volume[0] = CS8409_CS42L42_HP_VOL_REAL_MIN;
	data = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_HS_VOLUME_CHB, 1);
	if (data >= 0)
		spec->cs42l42_hp_volume[1] = -data;
	else
		spec->cs42l42_hp_volume[1] = CS8409_CS42L42_HP_VOL_REAL_MIN;
	data = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_AMIC_VOLUME, 1);
	if (data >= 0)
		spec->cs42l42_hs_mic_volume[0] = -data;
	else
		spec->cs42l42_hs_mic_volume[0] = CS8409_CS42L42_AMIC_VOL_REAL_MIN;
	mutex_unlock(&spec->cs8409_i2c_mux);
	spec->cs42l42_volume_init = 1;
}

static int cs8409_cs42l42_volume_get(struct snd_kcontrol *kctrl, struct snd_ctl_elem_value *uctrl)
{
	struct hda_codec *codec = snd_kcontrol_chip(kctrl);
	struct cs8409_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kctrl);
	int chs = get_amp_channels(kctrl);
	long *valp = uctrl->value.integer.value;

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

static int cs8409_cs42l42_volume_put(struct snd_kcontrol *kctrl, struct snd_ctl_elem_value *uctrl)
{
	struct hda_codec *codec = snd_kcontrol_chip(kctrl);
	struct cs8409_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kctrl);
	int chs = get_amp_channels(kctrl);
	long *valp = uctrl->value.integer.value;
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
			change = cs8409_i2c_write(codec, CS42L42_I2C_ADDR,
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

static const DECLARE_TLV_DB_SCALE(cs8409_cs42l42_hp_db_scale,
				  CS8409_CS42L42_HP_VOL_REAL_MIN * 100, 100, 1);

static const DECLARE_TLV_DB_SCALE(cs8409_cs42l42_amic_db_scale,
				  CS8409_CS42L42_AMIC_VOL_REAL_MIN * 100, 100, 1);

static const struct snd_kcontrol_new cs8409_cs42l42_hp_volume_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.index = 0,
	.name = "Headphone Playback Volume",
	.subdevice = (HDA_SUBDEV_AMP_FLAG | HDA_SUBDEV_NID_FLAG),
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = cs8409_cs42l42_volume_info,
	.get = cs8409_cs42l42_volume_get,
	.put = cs8409_cs42l42_volume_put,
	.tlv = { .p = cs8409_cs42l42_hp_db_scale },
	.private_value = HDA_COMPOSE_AMP_VAL(CS8409_CS42L42_HP_PIN_NID, 3, 0, HDA_OUTPUT) |
			 HDA_AMP_VAL_MIN_MUTE
};

static const struct snd_kcontrol_new cs8409_cs42l42_amic_volume_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.index = 0,
	.name = "Mic Capture Volume",
	.subdevice = (HDA_SUBDEV_AMP_FLAG | HDA_SUBDEV_NID_FLAG),
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = cs8409_cs42l42_volume_info,
	.get = cs8409_cs42l42_volume_get,
	.put = cs8409_cs42l42_volume_put,
	.tlv = { .p = cs8409_cs42l42_amic_db_scale },
	.private_value = HDA_COMPOSE_AMP_VAL(CS8409_CS42L42_AMIC_PIN_NID, 1, 0, HDA_INPUT) |
			 HDA_AMP_VAL_MIN_MUTE
};

/* Assert/release RTS# line to CS42L42 */
static void cs8409_cs42l42_reset(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	/* Assert RTS# line */
	snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_GPIO_DATA, 0);
	/* wait ~10ms */
	usleep_range(10000, 15000);
	/* Release RTS# line */
	snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_GPIO_DATA, GPIO5_INT);
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
	struct cs8409_spec *spec = codec->spec;

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
	struct cs8409_spec *spec = codec->spec;

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
	struct cs8409_spec *spec = codec->spec;

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
	struct cs8409_spec *spec = codec->spec;
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
			snd_hda_jack_unsol_event(codec, (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
							AC_UNSOL_RES_TAG);
		}
		/* Report jack*/
		jk = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L42_AMIC_PIN_NID, 0);
		if (jk) {
			snd_hda_jack_unsol_event(codec, (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
							 AC_UNSOL_RES_TAG);
		}
	}
}

#ifdef CONFIG_PM
/* Manage PDREF, when transition to D3hot */
static int cs8409_suspend(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	mutex_lock(&spec->cs8409_i2c_mux);
	/* Power down CS42L42 ASP/EQ/MIX/HP */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1101, 0xfe, 1);
	mutex_unlock(&spec->cs8409_i2c_mux);
	/* Assert CS42L42 RTS# line */
	snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_GPIO_DATA, 0);

	snd_hda_shutup_pins(codec);

	return 0;
}
#endif

/* Enable/Disable Unsolicited Response for gpio(s) 3,4 */
static void cs8409_enable_ur(struct hda_codec *codec, int flag)
{
	/* GPIO4 INT# and GPIO3 WAKE# */
	snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK,
			    flag ? (GPIO3_INT | GPIO4_INT) : 0);

	snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_UNSOLICITED_ENABLE,
			    flag ? AC_UNSOL_ENABLED : 0);

}

/* Vendor specific HW configuration
 * PLL, ASP, I2C, SPI, GPIOs, DMIC etc...
 */
static void cs8409_cs42l42_hw_init(struct hda_codec *codec)
{
	const struct cs8409_cir_param *seq = cs8409_cs42l42_hw_cfg;
	const struct cs8409_cir_param *seq_bullseye = cs8409_cs42l42_bullseye_atn;
	struct cs8409_spec *spec = codec->spec;

	if (spec->gpio_mask) {
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_MASK, spec->gpio_mask);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DIRECTION, spec->gpio_dir);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA, spec->gpio_data);
	}

	for (; seq->nid; seq++)
		cs8409_vendor_coef_set(codec, seq->cir, seq->coeff);

	if (codec->fixup_id == CS8409_BULLSEYE)
		for (; seq_bullseye->nid; seq_bullseye++)
			cs8409_vendor_coef_set(codec, seq_bullseye->cir, seq_bullseye->coeff);

	/* Disable Unsolicited Response during boot */
	cs8409_enable_ur(codec, 0);

	/* Reset CS42L42 */
	cs8409_cs42l42_reset(codec);

	/* Initialise CS42L42 companion codec */
	cs8409_cs42l42_reg_setup(codec);

	if (codec->fixup_id == CS8409_WARLOCK || codec->fixup_id == CS8409_CYBORG) {
		/* FULL_SCALE_VOL = 0 for Warlock / Cyborg */
		mutex_lock(&spec->cs8409_i2c_mux);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x2001, 0x01, 1);
		mutex_unlock(&spec->cs8409_i2c_mux);
		/* DMIC1_MO=00b, DMIC1/2_SR=1 */
		cs8409_vendor_coef_set(codec, 0x09, 0x0003);
	}

	/* Restore Volumes after Resume */
	if (spec->cs42l42_volume_init) {
		mutex_lock(&spec->cs8409_i2c_mux);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_HS_VOLUME_CHA,
				 -spec->cs42l42_hp_volume[0], 1);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_HS_VOLUME_CHB,
				 -spec->cs42l42_hp_volume[1], 1);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_AMIC_VOLUME,
				 spec->cs42l42_hs_mic_volume[0], 1);
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

static int cs8409_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_BUILD);

	return 0;
}

static const struct hda_codec_ops cs8409_cs42l42_patch_ops = {
	.build_controls = cs8409_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cs8409_cs42l42_init,
	.free = snd_hda_gen_free,
	.unsol_event = cs8409_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = cs8409_suspend,
#endif
};

static int cs8409_cs42l42_exec_verb(struct hdac_device *dev, unsigned int cmd, unsigned int flags,
				    unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct cs8409_spec *spec = codec->spec;

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

static void cs8409_cs42l42_fixups(struct hda_codec *codec, const struct hda_fixup *fix, int action)
{
	struct cs8409_spec *spec = codec->spec;
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
		snd_hda_gen_add_kctl(&spec->gen, NULL, &cs8409_cs42l42_hp_volume_mixer);
		snd_hda_gen_add_kctl(&spec->gen, NULL, &cs8409_cs42l42_amic_volume_mixer);
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

static int patch_cs8409(struct hda_codec *codec)
{
	int err;

	if (!cs8409_alloc_spec(codec))
		return -ENOMEM;

	snd_hda_pick_fixup(codec, cs8409_models, cs8409_fixup_tbl, cs8409_fixups);

	codec_dbg(codec, "Picked ID=%d, VID=%08x, DEV=%08x\n", codec->fixup_id,
			 codec->bus->pci->subsystem_vendor,
			 codec->bus->pci->subsystem_device);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = cs8409_parse_auto_config(codec);
	if (err < 0) {
		snd_hda_gen_free(codec);
		return err;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);
	return 0;
}

static const struct hda_device_id snd_hda_id_cs8409[] = {
	HDA_CODEC_ENTRY(0x10138409, "CS8409", patch_cs8409),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_cs8409);

static struct hda_codec_driver cs8409_driver = {
	.id = snd_hda_id_cs8409,
};
module_hda_codec_driver(cs8409_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cirrus Logic HDA bridge");

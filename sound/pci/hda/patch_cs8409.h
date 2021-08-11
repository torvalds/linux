/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HD audio interface patch for Cirrus Logic CS8409 HDA bridge chip
 *
 * Copyright (C) 2021 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __CS8409_PATCH_H
#define __CS8409_PATCH_H

#include <linux/pci.h>
#include <sound/tlv.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

/* Cirrus Logic CS8409 HDA bridge with
 * companion codec CS42L42
 */
#define CS42L42_HP_CH				(2U)
#define CS42L42_HS_MIC_CH			(1U)

#define CS8409_VENDOR_NID			0x47

#define CS8409_CS42L42_HP_PIN_NID		0x24
#define CS8409_CS42L42_SPK_PIN_NID		0x2c
#define CS8409_CS42L42_AMIC_PIN_NID		0x34
#define CS8409_CS42L42_DMIC_PIN_NID		0x44
#define CS8409_CS42L42_DMIC_ADC_PIN_NID		0x22

#define CS42L42_HSDET_AUTO_DONE			0x02
#define CS42L42_HSTYPE_MASK			0x03

#define CS42L42_JACK_INSERTED			0x0C
#define CS42L42_JACK_REMOVED			0x00

#define GPIO3_INT				(1 << 3)
#define GPIO4_INT				(1 << 4)
#define GPIO5_INT				(1 << 5)

#define CS42L42_I2C_ADDR			(0x48 << 1)

#define CIR_I2C_ADDR				0x0059
#define CIR_I2C_DATA				0x005A
#define CIR_I2C_CTRL				0x005B
#define CIR_I2C_STATUS				0x005C
#define CIR_I2C_QWRITE				0x005D
#define CIR_I2C_QREAD				0x005E

#define CS8409_CS42L42_HP_VOL_REAL_MIN		(-63)
#define CS8409_CS42L42_HP_VOL_REAL_MAX		(0)
#define CS8409_CS42L42_AMIC_VOL_REAL_MIN	(-97)
#define CS8409_CS42L42_AMIC_VOL_REAL_MAX	(12)
#define CS8409_CS42L42_REG_HS_VOLUME_CHA	(0x2301)
#define CS8409_CS42L42_REG_HS_VOLUME_CHB	(0x2303)
#define CS8409_CS42L42_REG_AMIC_VOLUME		(0x1D03)

enum {
	CS8409_BULLSEYE,
	CS8409_WARLOCK,
	CS8409_CYBORG,
	CS8409_FIXUPS,
};

struct cs8409_i2c_param {
	unsigned int addr;
	unsigned int reg;
};

struct cs8409_cir_param {
	unsigned int nid;
	unsigned int cir;
	unsigned int coeff;
};

struct cs8409_spec {
	struct hda_gen_spec gen;

	unsigned int gpio_mask;
	unsigned int gpio_dir;
	unsigned int gpio_data;

	unsigned int cs42l42_hp_jack_in:1;
	unsigned int cs42l42_mic_jack_in:1;
	unsigned int cs42l42_volume_init:1;
	char cs42l42_hp_volume[CS42L42_HP_CH];
	char cs42l42_hs_mic_volume[CS42L42_HS_MIC_CH];

	struct mutex cs8409_i2c_mux;

	/* verb exec op override */
	int (*exec_verb)(struct hdac_device *dev, unsigned int cmd, unsigned int flags,
			 unsigned int *res);
};

extern const struct snd_pci_quirk cs8409_fixup_tbl[];
extern const struct hda_model_fixup cs8409_models[];
extern const struct hda_fixup cs8409_fixups[];
extern const struct hda_verb cs8409_cs42l42_init_verbs[];
extern const struct hda_pintbl cs8409_cs42l42_pincfgs[];
extern const struct cs8409_i2c_param cs42l42_init_reg_seq[];
extern const struct cs8409_cir_param cs8409_cs42l42_hw_cfg[];
extern const struct cs8409_cir_param cs8409_cs42l42_bullseye_atn[];

void cs8409_cs42l42_fixups(struct hda_codec *codec, const struct hda_fixup *fix, int action);

#endif

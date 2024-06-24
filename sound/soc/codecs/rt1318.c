// SPDX-License-Identifier: GPL-2.0-only
//
// rt1318.c  --  RT1318 ALSA SoC audio amplifier driver
// Author: Jack Yu <jack.yu@realtek.com>
//
// Copyright(c) 2024 Realtek Semiconductor Corp.
//
//

#include <linux/acpi.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/rt1318.h>

#include "rt1318.h"

static struct reg_sequence init_list[] = {
	{ 0x0000C000, 0x01},
	{ 0x0000F20D, 0x00},
	{ 0x0000F212, 0x3E},
	{ 0x0000C001, 0x02},
	{ 0x0000C003, 0x22},
	{ 0x0000C004, 0x44},
	{ 0x0000C005, 0x44},
	{ 0x0000C007, 0x64},
	{ 0x0000C00E, 0xE7},
	{ 0x0000F223, 0x7F},
	{ 0x0000F224, 0xDB},
	{ 0x0000F225, 0xEE},
	{ 0x0000F226, 0x3F},
	{ 0x0000F227, 0x0F},
	{ 0x0000F21A, 0x78},
	{ 0x0000F242, 0x3C},
	{ 0x0000C120, 0x40},
	{ 0x0000C125, 0x03},
	{ 0x0000C321, 0x0A},
	{ 0x0000C200, 0xD8},
	{ 0x0000C201, 0x27},
	{ 0x0000C202, 0x0F},
	{ 0x0000C400, 0x0E},
	{ 0x0000C401, 0x43},
	{ 0x0000C402, 0xE0},
	{ 0x0000C403, 0x00},
	{ 0x0000C404, 0x4C},
	{ 0x0000C406, 0x40},
	{ 0x0000C407, 0x02},
	{ 0x0000C408, 0x3F},
	{ 0x0000C300, 0x01},
	{ 0x0000C125, 0x03},
	{ 0x0000DF00, 0x10},
	{ 0x0000F20B, 0x2A},
	{ 0x0000DF5F, 0x01},
	{ 0x0000DF60, 0xA7},
	{ 0x0000C203, 0x84},
	{ 0x0000C206, 0x78},
	{ 0x0000F10A, 0x09},
	{ 0x0000F10B, 0x4C},
	{ 0x0000F104, 0xF4},
	{ 0x0000F105, 0x03},
	{ 0x0000F109, 0xE0},
	{ 0x0000F10B, 0x5C},
	{ 0x0000F104, 0xF4},
	{ 0x0000F105, 0x04},
	{ 0x0000F109, 0x65},
	{ 0x0000F10B, 0x5C},
	{ 0x0000F104, 0xF4},
	{ 0x0000F105, 0x02},
	{ 0x0000F109, 0x30},
	{ 0x0000F10B, 0x5C},
	{ 0x0000E706, 0x0F},
	{ 0x0000E707, 0x30},
	{ 0x0000E806, 0x0F},
	{ 0x0000E807, 0x30},
	{ 0x0000CE04, 0x03},
	{ 0x0000CE05, 0x5F},
	{ 0x0000CE06, 0xA2},
	{ 0x0000CE07, 0x6B},
	{ 0x0000CF04, 0x03},
	{ 0x0000CF05, 0x5F},
	{ 0x0000CF06, 0xA2},
	{ 0x0000CF07, 0x6B},
	{ 0x0000CE60, 0xE3},
	{ 0x0000C130, 0x51},
	{ 0x0000E000, 0xA8},
	{ 0x0000F102, 0x00},
	{ 0x0000F103, 0x00},
	{ 0x0000F104, 0xF5},
	{ 0x0000F105, 0x23},
	{ 0x0000F109, 0x04},
	{ 0x0000F10A, 0x0B},
	{ 0x0000F10B, 0x4C},
	{ 0x0000F10B, 0x5C},
	{ 0x41001888, 0x00},
	{ 0x0000C121, 0x0B},
	{ 0x0000F102, 0x00},
	{ 0x0000F103, 0x00},
	{ 0x0000F104, 0xF5},
	{ 0x0000F105, 0x23},
	{ 0x0000F109, 0x00},
	{ 0x0000F10A, 0x0B},
	{ 0x0000F10B, 0x4C},
	{ 0x0000F10B, 0x5C},
	{ 0x0000F800, 0x20},
	{ 0x0000CA00, 0x80},
	{ 0x0000CA10, 0x00},
	{ 0x0000CA02, 0x78},
	{ 0x0000CA12, 0x78},
	{ 0x0000ED00, 0x90},
	{ 0x0000E604, 0x00},
	{ 0x0000DB00, 0x0C},
	{ 0x0000DD00, 0x0C},
	{ 0x0000DC19, 0x00},
	{ 0x0000DC1A, 0x6A},
	{ 0x0000DC1B, 0xAA},
	{ 0x0000DC1C, 0xAB},
	{ 0x0000DC1D, 0x00},
	{ 0x0000DC1E, 0x16},
	{ 0x0000DC1F, 0xDB},
	{ 0x0000DC20, 0x6D},
	{ 0x0000DE19, 0x00},
	{ 0x0000DE1A, 0x6A},
	{ 0x0000DE1B, 0xAA},
	{ 0x0000DE1C, 0xAB},
	{ 0x0000DE1D, 0x00},
	{ 0x0000DE1E, 0x16},
	{ 0x0000DE1F, 0xDB},
	{ 0x0000DE20, 0x6D},
	{ 0x0000DB32, 0x00},
	{ 0x0000DD32, 0x00},
	{ 0x0000DB33, 0x0A},
	{ 0x0000DD33, 0x0A},
	{ 0x0000DB34, 0x1A},
	{ 0x0000DD34, 0x1A},
	{ 0x0000DB15, 0xEF},
	{ 0x0000DD15, 0xEF},
	{ 0x0000DB17, 0xEF},
	{ 0x0000DD17, 0xEF},
	{ 0x0000DB94, 0x70},
	{ 0x0000DD94, 0x70},
	{ 0x0000DB19, 0x40},
	{ 0x0000DD19, 0x40},
	{ 0x0000DB12, 0xC0},
	{ 0x0000DD12, 0xC0},
	{ 0x0000DB00, 0x4C},
	{ 0x0000DB04, 0x05},
	{ 0x0000DB05, 0x03},
	{ 0x0000DD04, 0x05},
	{ 0x0000DD05, 0x03},
	{ 0x0000DBBB, 0x09},
	{ 0x0000DBBC, 0x30},
	{ 0x0000DBBD, 0xF0},
	{ 0x0000DBBE, 0xF1},
	{ 0x0000DDBB, 0x09},
	{ 0x0000DDBC, 0x30},
	{ 0x0000DDBD, 0xF0},
	{ 0x0000DDBE, 0xF1},
	{ 0x0000DB01, 0x79},
	{ 0x0000DD01, 0x79},
	{ 0x0000DB08, 0x40},
	{ 0x0000DD08, 0x40},
	{ 0x0000DC52, 0xEF},
	{ 0x0000DE52, 0xEF},
	{ 0x0000DB00, 0xCC},
	{ 0x0000CC2C, 0x00},
	{ 0x0000CC2D, 0x2A},
	{ 0x0000CC2E, 0x83},
	{ 0x0000CC2F, 0xA8},
	{ 0x0000CD2C, 0x00},
	{ 0x0000CD2D, 0x2A},
	{ 0x0000CD2E, 0x83},
	{ 0x0000CD2F, 0xA8},
	{ 0x0000CC24, 0x00},
	{ 0x0000CC25, 0x51},
	{ 0x0000CC26, 0xEB},
	{ 0x0000CC27, 0x85},
	{ 0x0000CD24, 0x00},
	{ 0x0000CD25, 0x51},
	{ 0x0000CD26, 0xEB},
	{ 0x0000CD27, 0x85},
	{ 0x0000CC20, 0x00},
	{ 0x0000CC21, 0x00},
	{ 0x0000CC22, 0x43},
	{ 0x0000CD20, 0x00},
	{ 0x0000CD21, 0x00},
	{ 0x0000CD22, 0x43},
	{ 0x0000CC16, 0x0F},
	{ 0x0000CC17, 0x00},
	{ 0x0000CD16, 0x0F},
	{ 0x0000CD17, 0x00},
	{ 0x0000CC29, 0x5D},
	{ 0x0000CC2A, 0xC0},
	{ 0x0000CD29, 0x5D},
	{ 0x0000CD2A, 0xC0},
	{ 0x0000CC31, 0x20},
	{ 0x0000CC32, 0x00},
	{ 0x0000CC33, 0x00},
	{ 0x0000CC34, 0x00},
	{ 0x0000CD31, 0x20},
	{ 0x0000CD32, 0x00},
	{ 0x0000CD33, 0x00},
	{ 0x0000CD34, 0x00},
	{ 0x0000CC36, 0x79},
	{ 0x0000CC37, 0x99},
	{ 0x0000CC38, 0x99},
	{ 0x0000CC39, 0x99},
	{ 0x0000CD36, 0x79},
	{ 0x0000CD37, 0x99},
	{ 0x0000CD38, 0x99},
	{ 0x0000CD39, 0x99},
	{ 0x0000CC09, 0x00},
	{ 0x0000CC0A, 0x07},
	{ 0x0000CC0B, 0x5F},
	{ 0x0000CC0C, 0x6F},
	{ 0x0000CD09, 0x00},
	{ 0x0000CD0A, 0x07},
	{ 0x0000CD0B, 0x5F},
	{ 0x0000CD0C, 0x6F},
	{ 0x0000CC0E, 0x00},
	{ 0x0000CC0F, 0x03},
	{ 0x0000CC10, 0xAF},
	{ 0x0000CC11, 0xB7},
	{ 0x0000CD0E, 0x00},
	{ 0x0000CD0F, 0x03},
	{ 0x0000CD10, 0xAF},
	{ 0x0000CD11, 0xB7},
	{ 0x0000CCD6, 0x00},
	{ 0x0000CCD7, 0x03},
	{ 0x0000CDD6, 0x00},
	{ 0x0000CDD7, 0x03},
	{ 0x0000CCD8, 0x00},
	{ 0x0000CCD9, 0x03},
	{ 0x0000CDD8, 0x00},
	{ 0x0000CDD9, 0x03},
	{ 0x0000CCDA, 0x00},
	{ 0x0000CCDB, 0x03},
	{ 0x0000CDDA, 0x00},
	{ 0x0000CDDB, 0x03},
	{ 0x0000C320, 0x20},
	{ 0x0000C203, 0x9C},
};
#define rt1318_INIT_REG_LEN ARRAY_SIZE(init_list)

static const struct reg_default rt1318_reg[] = {
	{ 0xc000, 0x00 },
	{ 0xc001, 0x43 },
	{ 0xc003, 0x22 },
	{ 0xc004, 0x44 },
	{ 0xc005, 0x44 },
	{ 0xc006, 0x33 },
	{ 0xc007, 0x64 },
	{ 0xc008, 0x05 },
	{ 0xc00a, 0xfc },
	{ 0xc00b, 0x0f },
	{ 0xc00c, 0x0e },
	{ 0xc00d, 0xef },
	{ 0xc00e, 0xe5 },
	{ 0xc00f, 0xff },
	{ 0xc120, 0xc0 },
	{ 0xc121, 0x00 },
	{ 0xc122, 0x00 },
	{ 0xc123, 0x14 },
	{ 0xc125, 0x00 },
	{ 0xc130, 0x59 },
	{ 0xc200, 0x00 },
	{ 0xc201, 0x00 },
	{ 0xc202, 0x00 },
	{ 0xc203, 0x04 },
	{ 0xc204, 0x00 },
	{ 0xc205, 0x00 },
	{ 0xc206, 0x68 },
	{ 0xc207, 0x70 },
	{ 0xc208, 0x00 },
	{ 0xc20a, 0x00 },
	{ 0xc20b, 0x01 },
	{ 0xc20c, 0x7f },
	{ 0xc20d, 0x01 },
	{ 0xc20e, 0x7f },
	{ 0xc300, 0x00 },
	{ 0xc301, 0x00 },
	{ 0xc303, 0x80 },
	{ 0xc320, 0x00 },
	{ 0xc321, 0x09 },
	{ 0xc322, 0x02 },
	{ 0xc400, 0x00 },
	{ 0xc401, 0x00 },
	{ 0xc402, 0x00 },
	{ 0xc403, 0x00 },
	{ 0xc404, 0x00 },
	{ 0xc405, 0x00 },
	{ 0xc406, 0x00 },
	{ 0xc407, 0x00 },
	{ 0xc408, 0x00 },
	{ 0xc410, 0x04 },
	{ 0xc430, 0x00 },
	{ 0xc431, 0x00 },
	{ 0xca00, 0x10 },
	{ 0xca01, 0x00 },
	{ 0xca02, 0x0b },
	{ 0xca10, 0x10 },
	{ 0xca11, 0x00 },
	{ 0xca12, 0x0b },
	{ 0xce04, 0x08 },
	{ 0xce05, 0x00 },
	{ 0xce06, 0x00 },
	{ 0xce07, 0x00 },
	{ 0xce60, 0x63 },
	{ 0xcf04, 0x08 },
	{ 0xcf05, 0x00 },
	{ 0xcf06, 0x00 },
	{ 0xcf07, 0x00 },
	{ 0xdb00, 0x00 },
	{ 0xdb08, 0x40 },
	{ 0xdb12, 0x00 },
	{ 0xdb35, 0x00 },
	{ 0xdbb5, 0x00 },
	{ 0xdbb6, 0x40 },
	{ 0xdbb7, 0x00 },
	{ 0xdbb8, 0x00 },
	{ 0xdbc5, 0x00 },
	{ 0xdbc6, 0x00 },
	{ 0xdbc7, 0x00 },
	{ 0xdbc8, 0x00 },
	{ 0xdd08, 0x40 },
	{ 0xdd12, 0x00 },
	{ 0xdd35, 0x00 },
	{ 0xddb5, 0x00 },
	{ 0xddb6, 0x40 },
	{ 0xddb7, 0x00 },
	{ 0xddb8, 0x00 },
	{ 0xddc5, 0x00 },
	{ 0xddc6, 0x00 },
	{ 0xddc7, 0x00 },
	{ 0xddc8, 0x00 },
	{ 0xdd93, 0x00 },
	{ 0xdd94, 0x64 },
	{ 0xdf00, 0x00 },
	{ 0xdf5f, 0x00 },
	{ 0xdf60, 0x00 },
	{ 0xe000, 0x08 },
	{ 0xe300, 0xa0 },
	{ 0xe400, 0x22 },
	{ 0xe706, 0x2f },
	{ 0xe707, 0x2f },
	{ 0xe806, 0x2f },
	{ 0xe807, 0x2f },
	{ 0xea00, 0x43 },
	{ 0xed00, 0x80 },
	{ 0xed01, 0x0f },
	{ 0xed02, 0xff },
	{ 0xed03, 0x00 },
	{ 0xed04, 0x00 },
	{ 0xed05, 0x0f },
	{ 0xed06, 0xff },
	{ 0xf010, 0x10 },
	{ 0xf011, 0xec },
	{ 0xf012, 0x68 },
	{ 0xf013, 0x21 },
	{ 0xf102, 0x00 },
	{ 0xf103, 0x00 },
	{ 0xf104, 0x00 },
	{ 0xf105, 0x00 },
	{ 0xf106, 0x00 },
	{ 0xf107, 0x00 },
	{ 0xf108, 0x00 },
	{ 0xf109, 0x00 },
	{ 0xf10a, 0x03 },
	{ 0xf10b, 0x40 },
	{ 0xf20b, 0x28 },
	{ 0xf20d, 0x00 },
	{ 0xf212, 0x00 },
	{ 0xf21a, 0x00 },
	{ 0xf223, 0x40 },
	{ 0xf224, 0x00 },
	{ 0xf225, 0x00 },
	{ 0xf226, 0x00 },
	{ 0xf227, 0x00 },
	{ 0xf242, 0x0c },
	{ 0xf800, 0x00 },
	{ 0xf801, 0x12 },
	{ 0xf802, 0xe0 },
	{ 0xf803, 0x2f },
	{ 0xf804, 0x00 },
	{ 0xf805, 0x00 },
	{ 0xf806, 0x07 },
	{ 0xf807, 0xff },
};

static bool rt1318_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0xc000:
	case 0xc301:
	case 0xc410:
	case 0xc430 ... 0xc431:
	case 0xdb06:
	case 0xdb12:
	case 0xdb1d ... 0xdb1f:
	case 0xdb35:
	case 0xdb37:
	case 0xdb8a ... 0xdb92:
	case 0xdbc5 ... 0xdbc8:
	case 0xdc2b ... 0xdc49:
	case 0xdd0b:
	case 0xdd12:
	case 0xdd1d ... 0xdd1f:
	case 0xdd35:
	case 0xdd8a ... 0xdd92:
	case 0xddc5 ... 0xddc8:
	case 0xde2b ... 0xde44:
	case 0xdf4a ... 0xdf55:
	case 0xe224 ... 0xe23b:
	case 0xea01:
	case 0xebc5:
	case 0xebc8:
	case 0xebcb ... 0xebcc:
	case 0xed03 ... 0xed06:
	case 0xf010 ... 0xf014:
		return true;

	default:
		return false;
	}
}

static bool rt1318_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0xc000 ... 0xc00f:
	case 0xc120 ... 0xc130:
	case 0xc200 ... 0xc20e:
	case 0xc300 ... 0xc303:
	case 0xc320 ... 0xc322:
	case 0xc400 ... 0xc408:
	case 0xc430 ... 0xc431:
	case 0xca00 ... 0xca02:
	case 0xca10 ... 0xca12:
	case 0xcb00 ... 0xcb0b:
	case 0xcc00 ... 0xcce5:
	case 0xcd00 ... 0xcde5:
	case 0xce00 ... 0xce6a:
	case 0xcf00 ... 0xcf53:
	case 0xd000 ... 0xd0cc:
	case 0xd100 ... 0xd1b9:
	case 0xdb00 ... 0xdc53:
	case 0xdd00 ... 0xde53:
	case 0xdf00 ... 0xdf6b:
	case 0xe000:
	case 0xe300:
	case 0xe400:
	case 0xe706 ... 0xe707:
	case 0xe806 ... 0xe807:
	case 0xea00:
	case 0xeb00 ... 0xebcc:
	case 0xec00 ... 0xecb9:
	case 0xed00 ... 0xed06:
	case 0xf010 ... 0xf014:
	case 0xf102 ... 0xf10b:
	case 0xf20b:
	case 0xf20d ... 0xf242:
	case 0xf800 ... 0xf807:
		return true;
	default:
		return false;
	}
}

static int rt1318_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(rt1318->regmap, RT1318_PWR_STA1,
				RT1318_PDB_CTRL_MASK, RT1318_PDB_CTRL_HIGH);
		break;

	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(rt1318->regmap, RT1318_PWR_STA1,
				RT1318_PDB_CTRL_MASK, RT1318_PDB_CTRL_LOW);
		break;

	default:
		break;
	}
	return 0;
}

static int rt1318_dvol_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);

	rt1318->rt1318_dvol = ucontrol->value.integer.value[0];

	if (rt1318->rt1318_dvol <= RT1318_DVOL_STEP && rt1318->rt1318_dvol >= 0) {
		regmap_write(rt1318->regmap, RT1318_DA_VOL_L_8,
			rt1318->rt1318_dvol >> 8);
		regmap_write(rt1318->regmap, RT1318_DA_VOL_L_1_7,
			rt1318->rt1318_dvol & 0xff);
		regmap_write(rt1318->regmap, RT1318_DA_VOL_R_8,
			rt1318->rt1318_dvol >> 8);
		regmap_write(rt1318->regmap, RT1318_DA_VOL_R_1_7,
			rt1318->rt1318_dvol & 0xff);
		return 1;
	}

	return 0;
}

static int rt1318_dvol_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rt1318->rt1318_dvol;

	return 0;
}

static const struct snd_kcontrol_new rt1318_snd_controls[] = {
	SOC_SINGLE_EXT("Amp Playback Volume", SND_SOC_NOPM, 0, 383, 0,
		rt1318_dvol_get, rt1318_dvol_put),
};

static const struct snd_soc_dapm_widget rt1318_dapm_widgets[] = {
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	/* DACs */
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0,
		rt1318_dac_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),
	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("Amp"),
};

static const struct snd_soc_dapm_route rt1318_dapm_routes[] = {
	{"DAC", NULL, "AIF1RX"},
	{"Amp", NULL, "DAC"},
};

static int rt1318_get_clk_info(int sclk, int rate)
{
	int i, pd[] = {1, 2, 4, 8, 16, 24};

	if (sclk <= 0 || rate <= 0)
		return -EINVAL;

	rate = rate << 8;
	for (i = 0; i < ARRAY_SIZE(pd); i++)
		if (sclk == rate * pd[i])
			return i;

	return -EINVAL;
}

static int rt1318_clk_ip_info(struct snd_soc_component *component, int lrclk)
{
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);

	switch (lrclk) {
	case RT1318_LRCLK_48000:
	case RT1318_LRCLK_44100:
	case RT1318_LRCLK_16000:
		regmap_update_bits(rt1318->regmap, RT1318_SRC_TCON,
				RT1318_SRCIN_F12288_MASK | RT1318_SRCIN_DACLK_MASK,
				RT1318_SRCIN_TCON4 | RT1318_DACLK_TCON4);
		break;
	case RT1318_LRCLK_96000:
		regmap_update_bits(rt1318->regmap, RT1318_SRC_TCON,
				RT1318_SRCIN_F12288_MASK | RT1318_SRCIN_DACLK_MASK,
				RT1318_SRCIN_TCON4 | RT1318_DACLK_TCON2);
		break;
	case RT1318_LRCLK_192000:
		regmap_update_bits(rt1318->regmap, RT1318_SRC_TCON,
				RT1318_SRCIN_F12288_MASK | RT1318_SRCIN_DACLK_MASK,
				RT1318_SRCIN_TCON4 | RT1318_DACLK_TCON1);
		break;
	default:
		dev_err(component->dev, "Unsupported clock rate.\n");
		return -EINVAL;
	}

	return 0;
}

static int rt1318_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);
	int data_len = 0, ch_len = 0;
	int pre_div, ret;

	rt1318->lrck = params_rate(params);
	pre_div = rt1318_get_clk_info(rt1318->sysclk, rt1318->lrck);
	if (pre_div < 0) {
		dev_err(component->dev, "Unsupported clock setting\n");
		return -EINVAL;
	}
	ret = rt1318_clk_ip_info(component, rt1318->lrck);
	if (ret < 0) {
		dev_err(component->dev, "Unsupported clock setting\n");
		return -EINVAL;
	}

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		data_len = RT1318_I2S_DL_20;
		ch_len = RT1318_I2S_DL_20;
		break;
	case 24:
		data_len = RT1318_I2S_DL_24;
		ch_len = RT1318_I2S_DL_24;
		break;
	case 32:
		data_len = RT1318_I2S_DL_32;
		ch_len = RT1318_I2S_DL_32;
		break;
	case 8:
		data_len = RT1318_I2S_DL_8;
		ch_len = RT1318_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(rt1318->regmap, RT1318_CLK2,
				RT1318_DIV_AP_MASK | RT1318_DIV_DAMOD_MASK,
				pre_div << RT1318_DIV_AP_SFT |
				pre_div << RT1318_DIV_DAMOD_SFT);
	regmap_update_bits(rt1318->regmap, RT1318_CLK3,
				RT1318_AD_STO1_MASK | RT1318_AD_STO2_MASK,
				pre_div << RT1318_AD_STO1_SFT |
				pre_div << RT1318_AD_STO2_SFT);
	regmap_update_bits(rt1318->regmap, RT1318_CLK4,
				RT1318_AD_ANA_STO1_MASK | RT1318_AD_ANA_STO2_MASK,
				pre_div << RT1318_AD_ANA_STO1_SFT |
				pre_div << RT1318_AD_ANA_STO2_SFT);
	regmap_update_bits(rt1318->regmap, RT1318_CLK5,
				RT1318_DIV_FIFO_IN_MASK | RT1318_DIV_FIFO_OUT_MASK,
				pre_div << RT1318_DIV_FIFO_IN_SFT |
				pre_div << RT1318_DIV_FIFO_OUT_SFT);
	regmap_update_bits(rt1318->regmap, RT1318_CLK6,
				RT1318_DIV_NLMS_MASK | RT1318_DIV_AD_MONO_MASK |
				RT1318_DIV_POST_G_MASK,  pre_div << RT1318_DIV_NLMS_SFT |
				pre_div << RT1318_DIV_AD_MONO_SFT |
				pre_div << RT1318_DIV_POST_G_SFT);

	regmap_update_bits(rt1318->regmap, RT1318_TDM_CTRL2,
				RT1318_I2S_DL_MASK, data_len << RT1318_I2S_DL_SFT);
	regmap_update_bits(rt1318->regmap, RT1318_TDM_CTRL3,
				RT1318_I2S_TX_CHL_MASK | RT1318_I2S_RX_CHL_MASK,
				ch_len << RT1318_I2S_TX_CHL_SFT |
				ch_len << RT1318_I2S_RX_CHL_SFT);

	return 0;
}

static int rt1318_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0, reg_val2 = 0;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val2 |= RT1318_TDM_BCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT1318_FMT_LEFT_J;
		break;

	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT1318_FMT_PCM_A_R;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT1318_FMT_PCM_B_R;
		break;

	default:
		return -EINVAL;
	}

	regmap_update_bits(rt1318->regmap, RT1318_TDM_CTRL1,
			RT1318_I2S_FMT_MASK, reg_val);
	regmap_update_bits(rt1318->regmap, RT1318_TDM_CTRL1,
			RT1318_TDM_BCLK_MASK, reg_val2);

	return 0;
}

static int rt1318_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);
	int reg_val = 0;

	if (freq == rt1318->sysclk && clk_id == rt1318->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT1318_SCLK_S_BCLK:
		reg_val |= RT1318_SYSCLK_BCLK;
		break;
	case RT1318_SCLK_S_SDW:
		reg_val |= RT1318_SYSCLK_SDW;
		break;
	case RT1318_SCLK_S_PLL2F:
		reg_val |= RT1318_SYSCLK_PLL2F;
		break;
	case RT1318_SCLK_S_PLL2B:
		reg_val |= RT1318_SYSCLK_PLL2B;
		break;
	case RT1318_SCLK_S_MCLK:
		reg_val |= RT1318_SYSCLK_MCLK;
		break;
	case RT1318_SCLK_S_RC0:
		reg_val |= RT1318_SYSCLK_RC1;
		break;
	case RT1318_SCLK_S_RC1:
		reg_val |= RT1318_SYSCLK_RC2;
		break;
	case RT1318_SCLK_S_RC2:
		reg_val |= RT1318_SYSCLK_RC3;
		break;
	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	rt1318->sysclk = freq;
	rt1318->sysclk_src = clk_id;
	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);
	regmap_update_bits(rt1318->regmap, RT1318_CLK1,
			RT1318_SYSCLK_SEL_MASK, reg_val);

	return 0;
}

static const struct pll_calc_map pll_preset_table[] = {
	{512000, 4096000, 22, 190, 0, true, false},
	{1024000, 4096000, 22, 94, 0, true, false},
	{1024000, 16384000, 4, 190, 0, true, false},
	{1411200, 11289600, 6, 62, 0, true, false},
	{1536000, 12288000, 6, 62, 0, true, false},
	{2822400, 11289600, 6, 62, 0, true, false},
	{2822400, 45158400, 0, 62, 0, true, false},
	{2822400, 49152000, 0, 62, 0, true, false},
	{3072000, 12288000, 6, 62, 0, true, false},
	{3072000, 24576000, 2, 62, 0, true, false},
	{3072000, 49152000, 0, 62, 0, true, false},
	{6144000, 24576000, 2, 94, 4, false, false},
	{6144000, 49152000, 0, 30, 0, true, false},
	{6144000, 98304000, 0, 94, 4, false, true},
	{12288000, 49152000, 0, 62, 6, false, false},
};

static int rt1318_pll_calc(const unsigned int freq_in,
	const unsigned int freq_out, struct rt1318_pll_code *pll_code)
{
	int max_n = RT1318_PLL_N_MAX, max_m = RT1318_PLL_M_MAX;
	int i, k, red, n_t, pll_out, in_t, out_t;
	int n = 0, m = 0, m_t = 0;
	int red_t = abs(freq_out - freq_in);
	bool m_bypass = false, k_bypass = false;

	if (RT1318_PLL_INP_MAX < freq_in || RT1318_PLL_INP_MIN > freq_in)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(pll_preset_table); i++) {
		if (freq_in == pll_preset_table[i].pll_in &&
			freq_out == pll_preset_table[i].pll_out) {
			k = pll_preset_table[i].k;
			m = pll_preset_table[i].m;
			n = pll_preset_table[i].n;
			m_bypass = pll_preset_table[i].m_bp;
			k_bypass = pll_preset_table[i].k_bp;
			goto code_find;
		}
	}

	k = 100000000 / freq_out - 2;
	if (k > RT1318_PLL_K_MAX)
		k = RT1318_PLL_K_MAX;
	if (k < 0) {
		k = 0;
		k_bypass = true;
	}
	for (n_t = 0; n_t <= max_n; n_t++) {
		in_t = freq_in / (k_bypass ? 1 : (k + 2));
		pll_out = freq_out / (n_t + 2);
		if (in_t < 0)
			continue;
		if (in_t == pll_out) {
			m_bypass = true;
			n = n_t;
			goto code_find;
		}
		red = abs(in_t - pll_out);
		if (red < red_t) {
			m_bypass = true;
			n = n_t;
			m = m_t;
			if (red == 0)
				goto code_find;
			red_t = red;
		}
		for (m_t = 0; m_t <= max_m; m_t++) {
			out_t = in_t / (m_t + 2);
			red = abs(out_t - pll_out);
			if (red < red_t) {
				m_bypass = false;
				n = n_t;
				m = m_t;
				if (red == 0)
					goto code_find;
				red_t = red;
			}
		}
	}
	pr_debug("Only get approximation about PLL\n");

code_find:

	pll_code->m_bp = m_bypass;
	pll_code->k_bp = k_bypass;
	pll_code->m_code = m;
	pll_code->n_code = n;
	pll_code->k_code = k;
	return 0;
}

static int rt1318_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_component *component = dai->component;
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);
	struct rt1318_pll_code pll_code;
	int ret;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");
		rt1318->pll_in = 0;
		rt1318->pll_out = 0;
		return 0;
	}

	if (source == rt1318->pll_src && freq_in == rt1318->pll_in &&
		freq_out == rt1318->pll_out)
		return 0;

	switch (source) {
	case RT1318_PLL_S_BCLK0:
		regmap_update_bits(rt1318->regmap, RT1318_CLK1,
			RT1318_PLLIN_MASK, RT1318_PLLIN_BCLK0);
		break;
	case RT1318_PLL_S_BCLK1:
		regmap_update_bits(rt1318->regmap, RT1318_CLK1,
			RT1318_PLLIN_MASK, RT1318_PLLIN_BCLK1);
		break;
	case RT1318_PLL_S_RC:
		regmap_update_bits(rt1318->regmap, RT1318_CLK1,
			RT1318_PLLIN_MASK, RT1318_PLLIN_RC);
		break;
	case RT1318_PLL_S_MCLK:
		regmap_update_bits(rt1318->regmap, RT1318_CLK1,
			RT1318_PLLIN_MASK, RT1318_PLLIN_MCLK);
		break;
	case RT1318_PLL_S_SDW_IN_PLL:
		regmap_update_bits(rt1318->regmap, RT1318_CLK1,
			RT1318_PLLIN_MASK, RT1318_PLLIN_SDW1);
		break;
	case RT1318_PLL_S_SDW_0:
		regmap_update_bits(rt1318->regmap, RT1318_CLK1,
			RT1318_PLLIN_MASK, RT1318_PLLIN_SDW2);
		break;
	case RT1318_PLL_S_SDW_1:
		regmap_update_bits(rt1318->regmap, RT1318_CLK1,
			RT1318_PLLIN_MASK, RT1318_PLLIN_SDW3);
		break;
	case RT1318_PLL_S_SDW_2:
		regmap_update_bits(rt1318->regmap, RT1318_CLK1,
			RT1318_PLLIN_MASK, RT1318_PLLIN_SDW4);
		break;
	default:
		dev_err(component->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}

	ret = rt1318_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(component->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(component->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	regmap_update_bits(rt1318->regmap, RT1318_PLL1_K,
			RT1318_K_PLL1_MASK, pll_code.k_code);
	regmap_update_bits(rt1318->regmap, RT1318_PLL1_M,
			RT1318_M_PLL1_MASK, (pll_code.m_bp ? 0 : pll_code.m_code));
	regmap_update_bits(rt1318->regmap, RT1318_PLL1_N_8,
			RT1318_N_8_PLL1_MASK, pll_code.n_code >> 8);
	regmap_update_bits(rt1318->regmap, RT1318_PLL1_N_7_0,
			RT1318_N_7_0_PLL1_MASK, pll_code.n_code);

	rt1318->pll_in = freq_in;
	rt1318->pll_out = freq_out;
	rt1318->pll_src = source;

	return 0;
}

static int rt1318_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);
	unsigned int cn = 0, cl = 0, rx_slotnum;
	int ret = 0, first_bit;

	switch (slots) {
	case 4:
		cn |= RT1318_I2S_CH_TX_4CH;
		cn |= RT1318_I2S_CH_RX_4CH;
		break;
	case 6:
		cn |= RT1318_I2S_CH_TX_6CH;
		cn |= RT1318_I2S_CH_RX_6CH;
		break;
	case 8:
		cn |= RT1318_I2S_CH_TX_8CH;
		cn |= RT1318_I2S_CH_RX_8CH;
		break;
	case 2:
		break;
	default:
		return -EINVAL;
	}

	switch (slot_width) {
	case 20:
		cl |= RT1318_I2S_TX_CHL_20;
		cl |= RT1318_I2S_RX_CHL_20;
		break;
	case 24:
		cl |= RT1318_I2S_TX_CHL_24;
		cl |= RT1318_I2S_RX_CHL_24;
		break;
	case 32:
		cl |= RT1318_I2S_TX_CHL_32;
		cl |= RT1318_I2S_RX_CHL_32;
		break;
	case 8:
		cl |= RT1318_I2S_TX_CHL_8;
		cl |= RT1318_I2S_RX_CHL_8;
		break;
	case 16:
		break;
	default:
		return -EINVAL;
	}

	/* Rx slot configuration */
	rx_slotnum = hweight_long(rx_mask);
	if (rx_slotnum != 1) {
		ret = -EINVAL;
		dev_err(component->dev, "too many rx slots or zero slot\n");
		goto _set_tdm_err_;
	}

	first_bit = __ffs(rx_mask);
	switch (first_bit) {
	case 0:
	case 2:
	case 4:
	case 6:
		regmap_update_bits(rt1318->regmap,
			RT1318_TDM_CTRL9,
			RT1318_TDM_I2S_TX_L_DAC1_1_MASK |
			RT1318_TDM_I2S_TX_R_DAC1_1_MASK,
			(first_bit << RT1318_TDM_I2S_TX_L_DAC1_1_SFT) |
			((first_bit + 1) << RT1318_TDM_I2S_TX_R_DAC1_1_SFT));
		break;
	case 1:
	case 3:
	case 5:
	case 7:
		regmap_update_bits(rt1318->regmap,
			RT1318_TDM_CTRL9,
			RT1318_TDM_I2S_TX_L_DAC1_1_MASK |
			RT1318_TDM_I2S_TX_R_DAC1_1_MASK,
			((first_bit - 1) << RT1318_TDM_I2S_TX_L_DAC1_1_SFT) |
			(first_bit << RT1318_TDM_I2S_TX_R_DAC1_1_SFT));
		break;
	default:
		ret = -EINVAL;
		goto _set_tdm_err_;
	}

	regmap_update_bits(rt1318->regmap, RT1318_TDM_CTRL2,
			RT1318_I2S_CH_TX_MASK | RT1318_I2S_CH_RX_MASK, cn);
	regmap_update_bits(rt1318->regmap, RT1318_TDM_CTRL3,
			RT1318_I2S_TX_CHL_MASK | RT1318_I2S_RX_CHL_MASK, cl);

_set_tdm_err_:
	return ret;
}

static int rt1318_probe(struct snd_soc_component *component)
{
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);

	rt1318->component = component;

	schedule_work(&rt1318->cali_work);
	rt1318->rt1318_dvol = RT1318_DVOL_STEP;

	return 0;
}

static void rt1318_remove(struct snd_soc_component *component)
{
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);

	cancel_work_sync(&rt1318->cali_work);
}

#ifdef CONFIG_PM
static int rt1318_suspend(struct snd_soc_component *component)
{
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1318->regmap, true);
	regcache_mark_dirty(rt1318->regmap);
	return 0;
}

static int rt1318_resume(struct snd_soc_component *component)
{
	struct rt1318_priv *rt1318 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1318->regmap, false);
	regcache_sync(rt1318->regmap);
	return 0;
}
#else
#define rt1318_suspend NULL
#define rt1318_resume NULL
#endif

#define RT1318_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT1318_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt1318_aif_dai_ops = {
	.hw_params = rt1318_hw_params,
	.set_fmt = rt1318_set_dai_fmt,
	.set_sysclk = rt1318_set_dai_sysclk,
	.set_pll = rt1318_set_dai_pll,
	.set_tdm_slot = rt1318_set_tdm_slot,
};

static struct snd_soc_dai_driver rt1318_dai[] = {
	{
		.name = "rt1318-aif",
		.id = 0,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1318_STEREO_RATES,
			.formats = RT1318_FORMATS,
		},
		.ops = &rt1318_aif_dai_ops,
	}
};

static const struct snd_soc_component_driver soc_component_dev_rt1318 = {
	.probe = rt1318_probe,
	.remove = rt1318_remove,
	.suspend = rt1318_suspend,
	.resume = rt1318_resume,
	.controls = rt1318_snd_controls,
	.num_controls = ARRAY_SIZE(rt1318_snd_controls),
	.dapm_widgets = rt1318_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1318_dapm_widgets),
	.dapm_routes = rt1318_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1318_dapm_routes),
	.use_pmdown_time = 1,
	.endianness = 1,
};

static const struct regmap_config rt1318_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt1318_readable_register,
	.volatile_reg = rt1318_volatile_register,
	.max_register = 0x41001888,
	.reg_defaults = rt1318_reg,
	.num_reg_defaults = ARRAY_SIZE(rt1318_reg),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static const struct i2c_device_id rt1318_i2c_id[] = {
	{ "rt1318" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt1318_i2c_id);

static const struct of_device_id rt1318_of_match[] = {
	{ .compatible = "realtek,rt1318", },
	{},
};
MODULE_DEVICE_TABLE(of, rt1318_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt1318_acpi_match[] = {
	{ "10EC1318", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, rt1318_acpi_match);
#endif

static int rt1318_parse_dt(struct rt1318_priv *rt1318, struct device *dev)
{
	device_property_read_u32(dev, "realtek,r0_l",
		&rt1318->pdata.init_r0_l);
	device_property_read_u32(dev, "realtek,r0_r",
		&rt1318->pdata.init_r0_r);

	return 0;
}

static void rt1318_calibration_sequence(struct rt1318_priv *rt1318)
{
	regmap_write(rt1318->regmap, RT1318_CLK1, 0x22);
	regmap_write(rt1318->regmap, RT1318_PLL1_N_7_0, 0x06);
	regmap_write(rt1318->regmap, RT1318_STP_TEMP_L, 0xCC);
	regmap_write(rt1318->regmap, RT1318_STP_SEL_L, 0x40);
	regmap_write(rt1318->regmap, RT1318_STP_SEL_R, 0x40);
	regmap_write(rt1318->regmap, RT1318_SINE_GEN0, 0x20);
	regmap_write(rt1318->regmap, RT1318_SPK_VOL_TH, 0x00);
	regmap_write(rt1318->regmap, RT1318_FEEDBACK_PATH, 0x0B);
	regmap_write(rt1318->regmap, RT1318_TCON, 0x1C);
	regmap_write(rt1318->regmap, RT1318_TCON_RELATE, 0x58);
	regmap_write(rt1318->regmap, RT1318_TCON_RELATE, 0x78);
	regmap_write(rt1318->regmap, RT1318_STP_R0_EN_L, 0xC2);
}

static void rt1318_r0_calculate(struct rt1318_priv *rt1318)
{
	unsigned int r0_l, r0_l_byte0, r0_l_byte1, r0_l_byte2, r0_l_byte3;
	unsigned int r0_r, r0_r_byte0, r0_r_byte1, r0_r_byte2, r0_r_byte3;
	unsigned int r0_l_integer, r0_l_factor, r0_r_integer, r0_r_factor;
	unsigned int format = 16777216; /* 2^24 */

	regmap_read(rt1318->regmap, RT1318_R0_L_24, &r0_l_byte0);
	regmap_read(rt1318->regmap, RT1318_R0_L_23_16, &r0_l_byte1);
	regmap_read(rt1318->regmap, RT1318_R0_L_15_8, &r0_l_byte2);
	regmap_read(rt1318->regmap, RT1318_R0_L_7_0, &r0_l_byte3);
	r0_l = r0_l_byte0 << 24 | r0_l_byte1 << 16 | r0_l_byte2 << 8 | r0_l_byte3;
	r0_l_integer = format / r0_l;
	r0_l_factor = (format * 10) / r0_l - r0_l_integer * 10;

	regmap_read(rt1318->regmap, RT1318_R0_R_24, &r0_r_byte0);
	regmap_read(rt1318->regmap, RT1318_R0_R_23_16, &r0_r_byte1);
	regmap_read(rt1318->regmap, RT1318_R0_R_15_8, &r0_r_byte2);
	regmap_read(rt1318->regmap, RT1318_R0_R_7_0, &r0_r_byte3);
	r0_r = r0_r_byte0 << 24 | r0_r_byte1 << 16 | r0_r_byte2 << 8 | r0_r_byte3;
	r0_r_integer = format / r0_r;
	r0_r_factor = (format * 10) / r0_r - r0_r_integer * 10;

	dev_dbg(rt1318->component->dev, "r0_l_ch:%d.%d ohm\n", r0_l_integer, r0_l_factor);
	dev_dbg(rt1318->component->dev, "r0_r_ch:%d.%d ohm\n", r0_r_integer, r0_r_factor);
}

static void rt1318_r0_restore(struct rt1318_priv *rt1318)
{
	regmap_write(rt1318->regmap, RT1318_PRE_R0_L_24,
		(rt1318->pdata.init_r0_l >> 24) & 0xff);
	regmap_write(rt1318->regmap, RT1318_PRE_R0_L_23_16,
		(rt1318->pdata.init_r0_l >> 16) & 0xff);
	regmap_write(rt1318->regmap, RT1318_PRE_R0_L_15_8,
		(rt1318->pdata.init_r0_l >> 8) & 0xff);
	regmap_write(rt1318->regmap, RT1318_PRE_R0_L_7_0,
		(rt1318->pdata.init_r0_l >> 0) & 0xff);
	regmap_write(rt1318->regmap, RT1318_PRE_R0_R_24,
		(rt1318->pdata.init_r0_r >> 24) & 0xff);
	regmap_write(rt1318->regmap, RT1318_PRE_R0_R_23_16,
		(rt1318->pdata.init_r0_r >> 16) & 0xff);
	regmap_write(rt1318->regmap, RT1318_PRE_R0_R_15_8,
		(rt1318->pdata.init_r0_r >> 8) & 0xff);
	regmap_write(rt1318->regmap, RT1318_PRE_R0_R_7_0,
		(rt1318->pdata.init_r0_r >> 0) & 0xff);
	regmap_write(rt1318->regmap, RT1318_STP_SEL_L, 0x80);
	regmap_write(rt1318->regmap, RT1318_STP_SEL_R, 0x80);
	regmap_write(rt1318->regmap, RT1318_R0_CMP_L_FLAG, 0xc0);
	regmap_write(rt1318->regmap, RT1318_R0_CMP_R_FLAG, 0xc0);
	regmap_write(rt1318->regmap, RT1318_STP_R0_EN_L, 0xc0);
	regmap_write(rt1318->regmap, RT1318_STP_R0_EN_R, 0xc0);
	regmap_write(rt1318->regmap, RT1318_STP_TEMP_L, 0xcc);
	regmap_write(rt1318->regmap, RT1318_TCON, 0x9c);
}

static int rt1318_calibrate(struct rt1318_priv *rt1318)
{
	int chk_cnt = 30, count = 0;
	int val, val2;

	regmap_write(rt1318->regmap, RT1318_PWR_STA1, 0x1);
	usleep_range(0, 10000);
	rt1318_calibration_sequence(rt1318);

	while (count < chk_cnt) {
		msleep(100);
		regmap_read(rt1318->regmap, RT1318_R0_CMP_L_FLAG, &val);
		regmap_read(rt1318->regmap, RT1318_R0_CMP_R_FLAG, &val2);
		val = (val >> 1) & 0x1;
		val2 = (val2 >> 1) & 0x1;
		if (val & val2) {
			dev_dbg(rt1318->component->dev, "Calibration done.\n");
			break;
		}
		count++;
		if (count == chk_cnt) {
			regmap_write(rt1318->regmap, RT1318_PWR_STA1, 0x0);
			return RT1318_R0_CALIB_NOT_DONE;
		}
	}
	regmap_write(rt1318->regmap, RT1318_PWR_STA1, 0x0);
	regmap_read(rt1318->regmap, RT1318_R0_CMP_L_FLAG, &val);
	regmap_read(rt1318->regmap, RT1318_R0_CMP_R_FLAG, &val2);
	if ((val & 0x1) & (val2 & 0x1))
		return RT1318_R0_IN_RANGE;
	else
		return RT1318_R0_OUT_OF_RANGE;
}

static void rt1318_calibration_work(struct work_struct *work)
{
	struct rt1318_priv *rt1318 =
		container_of(work, struct rt1318_priv, cali_work);
	int ret;

	if (rt1318->pdata.init_r0_l && rt1318->pdata.init_r0_r)
		rt1318_r0_restore(rt1318);
	else {
		ret = rt1318_calibrate(rt1318);
		if (ret == RT1318_R0_IN_RANGE)
			rt1318_r0_calculate(rt1318);
		dev_dbg(rt1318->component->dev, "Calibrate R0 result:%d\n", ret);
	}
}

static int rt1318_i2c_probe(struct i2c_client *i2c)
{
	struct rt1318_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt1318_priv *rt1318;
	int ret, val, val2, dev_id;

	rt1318 = devm_kzalloc(&i2c->dev, sizeof(struct rt1318_priv),
				GFP_KERNEL);
	if (!rt1318)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt1318);

	if (pdata)
		rt1318->pdata = *pdata;
	else
		rt1318_parse_dt(rt1318, &i2c->dev);

	rt1318->regmap = devm_regmap_init_i2c(i2c, &rt1318_regmap);
	if (IS_ERR(rt1318->regmap)) {
		ret = PTR_ERR(rt1318->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt1318->regmap, RT1318_DEV_ID1, &val);
	regmap_read(rt1318->regmap, RT1318_DEV_ID2, &val2);
	dev_id = (val << 8) | val2;
	if (dev_id != 0x6821) {
		dev_err(&i2c->dev,
			"Device with ID register %#x is not rt1318\n",
			dev_id);
		return -ENODEV;
	}

	ret = regmap_register_patch(rt1318->regmap, init_list,
				    ARRAY_SIZE(init_list));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply regmap patch: %d\n", ret);

	INIT_WORK(&rt1318->cali_work, rt1318_calibration_work);

	return devm_snd_soc_register_component(&i2c->dev,
		&soc_component_dev_rt1318, rt1318_dai, ARRAY_SIZE(rt1318_dai));
}

static struct i2c_driver rt1318_i2c_driver = {
	.driver = {
		.name = "rt1318",
		.of_match_table = of_match_ptr(rt1318_of_match),
		.acpi_match_table = ACPI_PTR(rt1318_acpi_match),
	},
	.probe = rt1318_i2c_probe,
	.id_table = rt1318_i2c_id,
};
module_i2c_driver(rt1318_i2c_driver);

MODULE_DESCRIPTION("ASoC RT1318 driver");
MODULE_AUTHOR("Jack Yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL");

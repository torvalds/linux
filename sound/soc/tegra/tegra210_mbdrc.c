// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_mbdrc.c - Tegra210 MBDRC driver
//
// Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "tegra210_mbdrc.h"
#include "tegra210_ope.h"

#define MBDRC_FILTER_REG(reg, id)					    \
	((reg) + ((id) * TEGRA210_MBDRC_FILTER_PARAM_STRIDE))

#define MBDRC_FILTER_REG_DEFAULTS(id)					    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_IIR_CFG, id), 0x00000005},	    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_IN_ATTACK, id), 0x3e48590c},	    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_IN_RELEASE, id), 0x08414e9f},	    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_FAST_ATTACK, id), 0x7fffffff},    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_IN_THRESHOLD, id), 0x06145082},   \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_OUT_THRESHOLD, id), 0x060d379b},  \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_RATIO_1ST, id), 0x0000a000},	    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_RATIO_2ND, id), 0x00002000},	    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_RATIO_3RD, id), 0x00000b33},	    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_RATIO_4TH, id), 0x00000800},	    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_RATIO_5TH, id), 0x0000019a},	    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_MAKEUP_GAIN, id), 0x00000002},    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_INIT_GAIN, id), 0x00066666},	    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_GAIN_ATTACK, id), 0x00d9ba0e},    \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_GAIN_RELEASE, id), 0x3e48590c},   \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_FAST_RELEASE, id), 0x7ffff26a},   \
	{ MBDRC_FILTER_REG(TEGRA210_MBDRC_CFG_RAM_CTRL, id), 0x4000}

static const struct reg_default tegra210_mbdrc_reg_defaults[] = {
	{ TEGRA210_MBDRC_CFG, 0x0030de51},
	{ TEGRA210_MBDRC_CHANNEL_MASK, 0x00000003},
	{ TEGRA210_MBDRC_FAST_FACTOR, 0x30000800},

	MBDRC_FILTER_REG_DEFAULTS(0),
	MBDRC_FILTER_REG_DEFAULTS(1),
	MBDRC_FILTER_REG_DEFAULTS(2),
};

/* Default MBDRC parameters */
static const struct tegra210_mbdrc_config mbdrc_init_config = {
	.mode			= 0, /* Bypass */
	.rms_off		= 48,
	.peak_rms_mode		= 1, /* PEAK */
	.filter_structure	= 0, /* All-pass tree */
	.shift_ctrl		= 30,
	.frame_size		= 32,
	.channel_mask		= 0x3,
	.fa_factor		= 2048,
	.fr_factor		= 14747,

	.band_params[MBDRC_LOW_BAND] = {
		.band			= MBDRC_LOW_BAND,
		.iir_stages		= 5,
		.in_attack_tc		= 1044928780,
		.in_release_tc		= 138497695,
		.fast_attack_tc		= 2147483647,
		.in_threshold		= {130, 80, 20, 6},
		.out_threshold		= {155, 55, 13, 6},
		.ratio			= {40960, 8192, 2867, 2048, 410},
		.makeup_gain		= 4,
		.gain_init		= 419430,
		.gain_attack_tc		= 14268942,
		.gain_release_tc	= 1440547090,
		.fast_release_tc	= 2147480170,

		.biquad_params	= {
			/*
			 * Gains:
			 *
			 * b0, b1, a0,
			 * a1, a2,
			 */

			/* Band-0 */
			961046798, -2030431983, 1073741824,
			2030431983, -961046798,
			/* Band-1 */
			1030244425, -2099481453, 1073741824,
			2099481453, -1030244425,
			/* Band-2 */
			1067169294, -2136327263, 1073741824,
			2136327263, -1067169294,
			/* Band-3 */
			434951949, -1306567134, 1073741824,
			1306567134, -434951949,
			/* Band-4 */
			780656019, -1605955641, 1073741824,
			1605955641, -780656019,
			/* Band-5 */
			1024497031, -1817128152, 1073741824,
			1817128152, -1024497031,
			/* Band-6 */
			1073741824, 0, 0,
			0, 0,
			/* Band-7 */
			1073741824, 0, 0,
			0, 0,
		}
	},

	.band_params[MBDRC_MID_BAND] = {
		.band			= MBDRC_MID_BAND,
		.iir_stages		= 5,
		.in_attack_tc		= 1581413104,
		.in_release_tc		= 35494783,
		.fast_attack_tc		= 2147483647,
		.in_threshold		= {130, 50, 30, 6},
		.out_threshold		= {106, 50, 30, 13},
		.ratio			= {40960, 2867, 4096, 2867, 410},
		.makeup_gain		= 6,
		.gain_init		= 419430,
		.gain_attack_tc		= 4766887,
		.gain_release_tc	= 1044928780,
		.fast_release_tc	= 2147480170,

		.biquad_params = {
			/*
			 * Gains:
			 *
			 * b0, b1, a0,
			 * a1, a2,
			 */

			/* Band-0 */
			-1005668963, 1073741824, 0,
			1005668963, 0,
			/* Band-1 */
			998437058, -2067742187, 1073741824,
			2067742187, -998437058,
			/* Band-2 */
			1051963422, -2121153948, 1073741824,
			2121153948, -1051963422,
			/* Band-3 */
			434951949, -1306567134, 1073741824,
			1306567134, -434951949,
			/* Band-4 */
			780656019, -1605955641, 1073741824,
			1605955641, -780656019,
			/* Band-5 */
			1024497031, -1817128152, 1073741824,
			1817128152, -1024497031,
			/* Band-6 */
			1073741824, 0, 0,
			0, 0,
			/* Band-7 */
			1073741824, 0, 0,
			0, 0,
		}
	},

	.band_params[MBDRC_HIGH_BAND] = {
		.band			= MBDRC_HIGH_BAND,
		.iir_stages		= 5,
		.in_attack_tc		= 2144750688,
		.in_release_tc		= 70402888,
		.fast_attack_tc		= 2147483647,
		.in_threshold		= {130, 50, 30, 6},
		.out_threshold		= {106, 50, 30, 13},
		.ratio			= {40960, 2867, 4096, 2867, 410},
		.makeup_gain		= 6,
		.gain_init		= 419430,
		.gain_attack_tc		= 4766887,
		.gain_release_tc	= 1044928780,
		.fast_release_tc	= 2147480170,

		.biquad_params = {
			/*
			 * Gains:
			 *
			 * b0, b1, a0,
			 * a1, a2,
			 */

			/* Band-0 */
			1073741824, 0, 0,
			0, 0,
			/* Band-1 */
			1073741824, 0, 0,
			0, 0,
			/* Band-2 */
			1073741824, 0, 0,
			0, 0,
			/* Band-3 */
			-619925131, 1073741824, 0,
			619925131, 0,
			/* Band-4 */
			606839335, -1455425976, 1073741824,
			1455425976, -606839335,
			/* Band-5 */
			917759617, -1724690840, 1073741824,
			1724690840, -917759617,
			/* Band-6 */
			1073741824, 0, 0,
			0, 0,
			/* Band-7 */
			1073741824, 0, 0,
			0, 0,
		}
	}
};

static void tegra210_mbdrc_write_ram(struct regmap *regmap, unsigned int reg_ctrl,
				     unsigned int reg_data, unsigned int ram_offset,
				     unsigned int *data, size_t size)
{
	unsigned int val;
	unsigned int i;

	val = ram_offset & TEGRA210_MBDRC_RAM_CTRL_RAM_ADDR_MASK;
	val |= TEGRA210_MBDRC_RAM_CTRL_ADDR_INIT_EN;
	val |= TEGRA210_MBDRC_RAM_CTRL_SEQ_ACCESS_EN;
	val |= TEGRA210_MBDRC_RAM_CTRL_RW_WRITE;

	regmap_write(regmap, reg_ctrl, val);

	for (i = 0; i < size; i++)
		regmap_write(regmap, reg_data, data[i]);
}

static int tegra210_mbdrc_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	unsigned int val;

	regmap_read(ope->mbdrc_regmap, mc->reg, &val);

	ucontrol->value.integer.value[0] = (val >> mc->shift) & mc->max;

	return 0;
}

static int tegra210_mbdrc_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	unsigned int val = ucontrol->value.integer.value[0];
	bool change = false;

	val = val << mc->shift;

	regmap_update_bits_check(ope->mbdrc_regmap, mc->reg,
				 (mc->max << mc->shift), val, &change);

	return change ? 1 : 0;
}

static int tegra210_mbdrc_get_enum(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val;

	regmap_read(ope->mbdrc_regmap, e->reg, &val);

	ucontrol->value.enumerated.item[0] = (val >> e->shift_l) & e->mask;

	return 0;
}

static int tegra210_mbdrc_put_enum(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	bool change = false;
	unsigned int val;
	unsigned int mask;

	if (ucontrol->value.enumerated.item[0] > e->items - 1)
		return -EINVAL;

	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = e->mask << e->shift_l;

	regmap_update_bits_check(ope->mbdrc_regmap, e->reg, mask, val,
				 &change);

	return change ? 1 : 0;
}

static int tegra210_mbdrc_band_params_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	u32 *data = (u32 *)ucontrol->value.bytes.data;
	u32 regs = params->soc.base;
	u32 mask = params->soc.mask;
	u32 shift = params->shift;
	unsigned int i;

	for (i = 0; i < params->soc.num_regs; i++, regs += cmpnt->val_bytes) {
		regmap_read(ope->mbdrc_regmap, regs, &data[i]);

		data[i] = ((data[i] & mask) >> shift);
	}

	return 0;
}

static int tegra210_mbdrc_band_params_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	u32 *data = (u32 *)ucontrol->value.bytes.data;
	u32 regs = params->soc.base;
	u32 mask = params->soc.mask;
	u32 shift = params->shift;
	bool change = false;
	unsigned int i;

	for (i = 0; i < params->soc.num_regs; i++, regs += cmpnt->val_bytes) {
		bool update = false;

		regmap_update_bits_check(ope->mbdrc_regmap, regs, mask,
					 data[i] << shift, &update);

		change |= update;
	}

	return change ? 1 : 0;
}

static int tegra210_mbdrc_threshold_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	u32 *data = (u32 *)ucontrol->value.bytes.data;
	u32 regs = params->soc.base;
	u32 num_regs = params->soc.num_regs;
	u32 val;
	unsigned int i;

	for (i = 0; i < num_regs; i += 4, regs += cmpnt->val_bytes) {
		regmap_read(ope->mbdrc_regmap, regs, &val);

		data[i] = (val & TEGRA210_MBDRC_THRESH_1ST_MASK) >>
			  TEGRA210_MBDRC_THRESH_1ST_SHIFT;
		data[i + 1] = (val & TEGRA210_MBDRC_THRESH_2ND_MASK) >>
			      TEGRA210_MBDRC_THRESH_2ND_SHIFT;
		data[i + 2] = (val & TEGRA210_MBDRC_THRESH_3RD_MASK) >>
			      TEGRA210_MBDRC_THRESH_3RD_SHIFT;
		data[i + 3] = (val & TEGRA210_MBDRC_THRESH_4TH_MASK) >>
			      TEGRA210_MBDRC_THRESH_4TH_SHIFT;
	}

	return 0;
}

static int tegra210_mbdrc_threshold_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	u32 *data = (u32 *)ucontrol->value.bytes.data;
	u32 regs = params->soc.base;
	u32 num_regs = params->soc.num_regs;
	bool change = false;
	unsigned int i;

	for (i = 0; i < num_regs; i += 4, regs += cmpnt->val_bytes) {
		bool update = false;

		data[i] = (((data[i] >> TEGRA210_MBDRC_THRESH_1ST_SHIFT) &
			    TEGRA210_MBDRC_THRESH_1ST_MASK) |
			   ((data[i + 1] >> TEGRA210_MBDRC_THRESH_2ND_SHIFT) &
			    TEGRA210_MBDRC_THRESH_2ND_MASK) |
			   ((data[i + 2] >> TEGRA210_MBDRC_THRESH_3RD_SHIFT) &
			    TEGRA210_MBDRC_THRESH_3RD_MASK) |
			   ((data[i + 3] >> TEGRA210_MBDRC_THRESH_4TH_SHIFT) &
			    TEGRA210_MBDRC_THRESH_4TH_MASK));

		regmap_update_bits_check(ope->mbdrc_regmap, regs, 0xffffffff,
					 data[i], &update);

		change |= update;
	}

	return change ? 1 : 0;
}

static int tegra210_mbdrc_biquad_coeffs_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	u32 *data = (u32 *)ucontrol->value.bytes.data;

	memset(data, 0, params->soc.num_regs * cmpnt->val_bytes);

	return 0;
}

static int tegra210_mbdrc_biquad_coeffs_put(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	u32 reg_ctrl = params->soc.base;
	u32 reg_data = reg_ctrl + cmpnt->val_bytes;
	u32 *data = (u32 *)ucontrol->value.bytes.data;

	tegra210_mbdrc_write_ram(ope->mbdrc_regmap, reg_ctrl, reg_data,
				 params->shift, data, params->soc.num_regs);

	return 1;
}

static int tegra210_mbdrc_param_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	struct soc_bytes *params = (void *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = params->num_regs * sizeof(u32);

	return 0;
}

static int tegra210_mbdrc_vol_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	int val;

	regmap_read(ope->mbdrc_regmap, mc->reg, &val);

	ucontrol->value.integer.value[0] =
		((val >> mc->shift) - TEGRA210_MBDRC_MASTER_VOL_MIN);

	return 0;
}

static int tegra210_mbdrc_vol_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	int val = ucontrol->value.integer.value[0];
	bool change = false;

	val += TEGRA210_MBDRC_MASTER_VOL_MIN;

	regmap_update_bits_check(ope->mbdrc_regmap, mc->reg,
				 mc->max << mc->shift, val << mc->shift,
				 &change);

	regmap_read(ope->mbdrc_regmap, mc->reg, &val);

	return change ? 1 : 0;
}

static const char * const tegra210_mbdrc_mode_text[] = {
	"Bypass", "Fullband", "Dualband", "Multiband"
};

static const struct soc_enum tegra210_mbdrc_mode_enum =
	SOC_ENUM_SINGLE(TEGRA210_MBDRC_CFG, TEGRA210_MBDRC_CFG_MBDRC_MODE_SHIFT,
			4, tegra210_mbdrc_mode_text);

static const char * const tegra210_mbdrc_peak_rms_text[] = {
	"Peak", "RMS"
};

static const struct soc_enum tegra210_mbdrc_peak_rms_enum =
	SOC_ENUM_SINGLE(TEGRA210_MBDRC_CFG, TEGRA210_MBDRC_CFG_PEAK_RMS_SHIFT,
			2, tegra210_mbdrc_peak_rms_text);

static const char * const tegra210_mbdrc_filter_structure_text[] = {
	"All-pass-tree", "Flexible"
};

static const struct soc_enum tegra210_mbdrc_filter_structure_enum =
	SOC_ENUM_SINGLE(TEGRA210_MBDRC_CFG,
			TEGRA210_MBDRC_CFG_FILTER_STRUCTURE_SHIFT, 2,
			tegra210_mbdrc_filter_structure_text);

static const char * const tegra210_mbdrc_frame_size_text[] = {
	"N1", "N2", "N4", "N8", "N16", "N32", "N64"
};

static const struct soc_enum tegra210_mbdrc_frame_size_enum =
	SOC_ENUM_SINGLE(TEGRA210_MBDRC_CFG, TEGRA210_MBDRC_CFG_FRAME_SIZE_SHIFT,
			7, tegra210_mbdrc_frame_size_text);

#define TEGRA_MBDRC_BYTES_EXT(xname, xbase, xregs, xshift, xmask, xinfo)    \
	TEGRA_SOC_BYTES_EXT(xname, xbase, xregs, xshift, xmask,		    \
			    tegra210_mbdrc_band_params_get,		    \
			    tegra210_mbdrc_band_params_put,		    \
			    tegra210_mbdrc_param_info)

#define TEGRA_MBDRC_BAND_BYTES_EXT(xname, xbase, xshift, xmask, xinfo)	    \
	TEGRA_MBDRC_BYTES_EXT(xname, xbase, TEGRA210_MBDRC_FILTER_COUNT,    \
			      xshift, xmask, xinfo)

static const DECLARE_TLV_DB_MINMAX(mdbrc_vol_tlv, -25600, 25500);

static const struct snd_kcontrol_new tegra210_mbdrc_controls[] = {
	SOC_ENUM_EXT("MBDRC Peak RMS Mode", tegra210_mbdrc_peak_rms_enum,
		     tegra210_mbdrc_get_enum, tegra210_mbdrc_put_enum),

	SOC_ENUM_EXT("MBDRC Filter Structure",
		     tegra210_mbdrc_filter_structure_enum,
		     tegra210_mbdrc_get_enum, tegra210_mbdrc_put_enum),

	SOC_ENUM_EXT("MBDRC Frame Size", tegra210_mbdrc_frame_size_enum,
		     tegra210_mbdrc_get_enum, tegra210_mbdrc_put_enum),

	SOC_ENUM_EXT("MBDRC Mode", tegra210_mbdrc_mode_enum,
		     tegra210_mbdrc_get_enum, tegra210_mbdrc_put_enum),

	SOC_SINGLE_EXT("MBDRC RMS Offset", TEGRA210_MBDRC_CFG,
		       TEGRA210_MBDRC_CFG_RMS_OFFSET_SHIFT, 0x1ff, 0,
		       tegra210_mbdrc_get, tegra210_mbdrc_put),

	SOC_SINGLE_EXT("MBDRC Shift Control", TEGRA210_MBDRC_CFG,
		       TEGRA210_MBDRC_CFG_SHIFT_CTRL_SHIFT, 0x1f, 0,
		       tegra210_mbdrc_get, tegra210_mbdrc_put),

	SOC_SINGLE_EXT("MBDRC Fast Attack Factor", TEGRA210_MBDRC_FAST_FACTOR,
		       TEGRA210_MBDRC_FAST_FACTOR_ATTACK_SHIFT, 0xffff, 0,
		       tegra210_mbdrc_get, tegra210_mbdrc_put),

	SOC_SINGLE_EXT("MBDRC Fast Release Factor", TEGRA210_MBDRC_FAST_FACTOR,
		       TEGRA210_MBDRC_FAST_FACTOR_RELEASE_SHIFT, 0xffff, 0,
		       tegra210_mbdrc_get, tegra210_mbdrc_put),

	SOC_SINGLE_RANGE_EXT_TLV("MBDRC Master Volume",
				 TEGRA210_MBDRC_MASTER_VOL,
				 TEGRA210_MBDRC_MASTER_VOL_SHIFT,
				 0, 0x1ff, 0,
				 tegra210_mbdrc_vol_get, tegra210_mbdrc_vol_put,
				 mdbrc_vol_tlv),

	TEGRA_SOC_BYTES_EXT("MBDRC IIR Stages", TEGRA210_MBDRC_IIR_CFG,
			    TEGRA210_MBDRC_FILTER_COUNT,
			    TEGRA210_MBDRC_IIR_CFG_NUM_STAGES_SHIFT,
			    TEGRA210_MBDRC_IIR_CFG_NUM_STAGES_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC In Attack Time Const", TEGRA210_MBDRC_IN_ATTACK,
			    TEGRA210_MBDRC_FILTER_COUNT,
			    TEGRA210_MBDRC_IN_ATTACK_TC_SHIFT,
			    TEGRA210_MBDRC_IN_ATTACK_TC_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC In Release Time Const", TEGRA210_MBDRC_IN_RELEASE,
			    TEGRA210_MBDRC_FILTER_COUNT,
			    TEGRA210_MBDRC_IN_RELEASE_TC_SHIFT,
			    TEGRA210_MBDRC_IN_RELEASE_TC_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Fast Attack Time Const", TEGRA210_MBDRC_FAST_ATTACK,
			    TEGRA210_MBDRC_FILTER_COUNT,
			    TEGRA210_MBDRC_FAST_ATTACK_TC_SHIFT,
			    TEGRA210_MBDRC_FAST_ATTACK_TC_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC In Threshold", TEGRA210_MBDRC_IN_THRESHOLD,
			    TEGRA210_MBDRC_FILTER_COUNT * 4, 0, 0xffffffff,
			    tegra210_mbdrc_threshold_get,
			    tegra210_mbdrc_threshold_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Out Threshold", TEGRA210_MBDRC_OUT_THRESHOLD,
			    TEGRA210_MBDRC_FILTER_COUNT * 4, 0, 0xffffffff,
			    tegra210_mbdrc_threshold_get,
			    tegra210_mbdrc_threshold_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Ratio", TEGRA210_MBDRC_RATIO_1ST,
			    TEGRA210_MBDRC_FILTER_COUNT * 5,
			    TEGRA210_MBDRC_RATIO_1ST_SHIFT, TEGRA210_MBDRC_RATIO_1ST_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Makeup Gain", TEGRA210_MBDRC_MAKEUP_GAIN,
			    TEGRA210_MBDRC_FILTER_COUNT,
			    TEGRA210_MBDRC_MAKEUP_GAIN_SHIFT,
			    TEGRA210_MBDRC_MAKEUP_GAIN_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Init Gain", TEGRA210_MBDRC_INIT_GAIN,
			    TEGRA210_MBDRC_FILTER_COUNT,
			    TEGRA210_MBDRC_INIT_GAIN_SHIFT,
			    TEGRA210_MBDRC_INIT_GAIN_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Attack Gain", TEGRA210_MBDRC_GAIN_ATTACK,
			    TEGRA210_MBDRC_FILTER_COUNT,
			    TEGRA210_MBDRC_GAIN_ATTACK_SHIFT,
			    TEGRA210_MBDRC_GAIN_ATTACK_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Release Gain", TEGRA210_MBDRC_GAIN_RELEASE,
			    TEGRA210_MBDRC_FILTER_COUNT,
			    TEGRA210_MBDRC_GAIN_RELEASE_SHIFT,
			    TEGRA210_MBDRC_GAIN_RELEASE_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Fast Release Gain",
			    TEGRA210_MBDRC_FAST_RELEASE,
			    TEGRA210_MBDRC_FILTER_COUNT,
			    TEGRA210_MBDRC_FAST_RELEASE_SHIFT,
			    TEGRA210_MBDRC_FAST_RELEASE_MASK,
			    tegra210_mbdrc_band_params_get,
			    tegra210_mbdrc_band_params_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Low Band Biquad Coeffs",
			    TEGRA210_MBDRC_CFG_RAM_CTRL,
			    TEGRA210_MBDRC_MAX_BIQUAD_STAGES * 5, 0, 0xffffffff,
			    tegra210_mbdrc_biquad_coeffs_get,
			    tegra210_mbdrc_biquad_coeffs_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC Mid Band Biquad Coeffs",
			    TEGRA210_MBDRC_CFG_RAM_CTRL +
				TEGRA210_MBDRC_FILTER_PARAM_STRIDE,
			    TEGRA210_MBDRC_MAX_BIQUAD_STAGES * 5, 0, 0xffffffff,
			    tegra210_mbdrc_biquad_coeffs_get,
			    tegra210_mbdrc_biquad_coeffs_put,
			    tegra210_mbdrc_param_info),

	TEGRA_SOC_BYTES_EXT("MBDRC High Band Biquad Coeffs",
			    TEGRA210_MBDRC_CFG_RAM_CTRL +
				(TEGRA210_MBDRC_FILTER_PARAM_STRIDE * 2),
			    TEGRA210_MBDRC_MAX_BIQUAD_STAGES * 5, 0, 0xffffffff,
			    tegra210_mbdrc_biquad_coeffs_get,
			    tegra210_mbdrc_biquad_coeffs_put,
			    tegra210_mbdrc_param_info),
};

static bool tegra210_mbdrc_wr_reg(struct device *dev, unsigned int reg)
{
	if (reg >= TEGRA210_MBDRC_IIR_CFG)
		reg -= ((reg - TEGRA210_MBDRC_IIR_CFG) %
			(TEGRA210_MBDRC_FILTER_PARAM_STRIDE *
			 TEGRA210_MBDRC_FILTER_COUNT));

	switch (reg) {
	case TEGRA210_MBDRC_SOFT_RESET:
	case TEGRA210_MBDRC_CG:
	case TEGRA210_MBDRC_CFG ... TEGRA210_MBDRC_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_mbdrc_rd_reg(struct device *dev, unsigned int reg)
{
	if (tegra210_mbdrc_wr_reg(dev, reg))
		return true;

	if (reg >= TEGRA210_MBDRC_IIR_CFG)
		reg -= ((reg - TEGRA210_MBDRC_IIR_CFG) %
			(TEGRA210_MBDRC_FILTER_PARAM_STRIDE *
			 TEGRA210_MBDRC_FILTER_COUNT));

	switch (reg) {
	case TEGRA210_MBDRC_STATUS:
		return true;
	default:
		return false;
	}
}

static bool tegra210_mbdrc_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg >= TEGRA210_MBDRC_IIR_CFG)
		reg -= ((reg - TEGRA210_MBDRC_IIR_CFG) %
			(TEGRA210_MBDRC_FILTER_PARAM_STRIDE *
			 TEGRA210_MBDRC_FILTER_COUNT));

	switch (reg) {
	case TEGRA210_MBDRC_SOFT_RESET:
	case TEGRA210_MBDRC_STATUS:
	case TEGRA210_MBDRC_CFG_RAM_CTRL:
	case TEGRA210_MBDRC_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_mbdrc_precious_reg(struct device *dev, unsigned int reg)
{
	if (reg >= TEGRA210_MBDRC_IIR_CFG)
		reg -= ((reg - TEGRA210_MBDRC_IIR_CFG) %
			(TEGRA210_MBDRC_FILTER_PARAM_STRIDE *
			 TEGRA210_MBDRC_FILTER_COUNT));

	switch (reg) {
	case TEGRA210_MBDRC_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra210_mbdrc_regmap_cfg = {
	.name			= "mbdrc",
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_MBDRC_MAX_REG,
	.writeable_reg		= tegra210_mbdrc_wr_reg,
	.readable_reg		= tegra210_mbdrc_rd_reg,
	.volatile_reg		= tegra210_mbdrc_volatile_reg,
	.precious_reg		= tegra210_mbdrc_precious_reg,
	.reg_defaults		= tegra210_mbdrc_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_mbdrc_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

int tegra210_mbdrc_hw_params(struct snd_soc_component *cmpnt)
{
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	const struct tegra210_mbdrc_config *conf = &mbdrc_init_config;
	u32 val = 0;
	unsigned int i;

	regmap_read(ope->mbdrc_regmap, TEGRA210_MBDRC_CFG, &val);

	val &= TEGRA210_MBDRC_CFG_MBDRC_MODE_MASK;

	if (val == TEGRA210_MBDRC_CFG_MBDRC_MODE_BYPASS)
		return 0;

	for (i = 0; i < MBDRC_NUM_BAND; i++) {
		const struct tegra210_mbdrc_band_params *params =
			&conf->band_params[i];

		u32 reg_off = i * TEGRA210_MBDRC_FILTER_PARAM_STRIDE;

		tegra210_mbdrc_write_ram(ope->mbdrc_regmap,
					 reg_off + TEGRA210_MBDRC_CFG_RAM_CTRL,
					 reg_off + TEGRA210_MBDRC_CFG_RAM_DATA,
					 0, (u32 *)&params->biquad_params[0],
					 TEGRA210_MBDRC_MAX_BIQUAD_STAGES * 5);
	}
	return 0;
}

int tegra210_mbdrc_component_init(struct snd_soc_component *cmpnt)
{
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	const struct tegra210_mbdrc_config *conf = &mbdrc_init_config;
	unsigned int i;
	u32 val;

	pm_runtime_get_sync(cmpnt->dev);

	/* Initialize MBDRC registers and AHUB RAM with default params */
	regmap_update_bits(ope->mbdrc_regmap, TEGRA210_MBDRC_CFG,
		TEGRA210_MBDRC_CFG_MBDRC_MODE_MASK,
		conf->mode << TEGRA210_MBDRC_CFG_MBDRC_MODE_SHIFT);

	regmap_update_bits(ope->mbdrc_regmap, TEGRA210_MBDRC_CFG,
		TEGRA210_MBDRC_CFG_RMS_OFFSET_MASK,
		conf->rms_off << TEGRA210_MBDRC_CFG_RMS_OFFSET_SHIFT);

	regmap_update_bits(ope->mbdrc_regmap, TEGRA210_MBDRC_CFG,
		TEGRA210_MBDRC_CFG_PEAK_RMS_MASK,
		conf->peak_rms_mode << TEGRA210_MBDRC_CFG_PEAK_RMS_SHIFT);

	regmap_update_bits(ope->mbdrc_regmap, TEGRA210_MBDRC_CFG,
		TEGRA210_MBDRC_CFG_FILTER_STRUCTURE_MASK,
		conf->filter_structure <<
		TEGRA210_MBDRC_CFG_FILTER_STRUCTURE_SHIFT);

	regmap_update_bits(ope->mbdrc_regmap, TEGRA210_MBDRC_CFG,
		TEGRA210_MBDRC_CFG_SHIFT_CTRL_MASK,
		conf->shift_ctrl << TEGRA210_MBDRC_CFG_SHIFT_CTRL_SHIFT);

	regmap_update_bits(ope->mbdrc_regmap, TEGRA210_MBDRC_CFG,
		TEGRA210_MBDRC_CFG_FRAME_SIZE_MASK,
		__ffs(conf->frame_size) <<
		TEGRA210_MBDRC_CFG_FRAME_SIZE_SHIFT);

	regmap_update_bits(ope->mbdrc_regmap, TEGRA210_MBDRC_CHANNEL_MASK,
		TEGRA210_MBDRC_CHANNEL_MASK_MASK,
		conf->channel_mask << TEGRA210_MBDRC_CHANNEL_MASK_SHIFT);

	regmap_update_bits(ope->mbdrc_regmap, TEGRA210_MBDRC_FAST_FACTOR,
		TEGRA210_MBDRC_FAST_FACTOR_ATTACK_MASK,
		conf->fa_factor << TEGRA210_MBDRC_FAST_FACTOR_ATTACK_SHIFT);

	regmap_update_bits(ope->mbdrc_regmap, TEGRA210_MBDRC_FAST_FACTOR,
		TEGRA210_MBDRC_FAST_FACTOR_ATTACK_MASK,
		conf->fr_factor << TEGRA210_MBDRC_FAST_FACTOR_ATTACK_SHIFT);

	for (i = 0; i < MBDRC_NUM_BAND; i++) {
		const struct tegra210_mbdrc_band_params *params =
						&conf->band_params[i];
		u32 reg_off = i * TEGRA210_MBDRC_FILTER_PARAM_STRIDE;

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_IIR_CFG,
			TEGRA210_MBDRC_IIR_CFG_NUM_STAGES_MASK,
			params->iir_stages <<
				TEGRA210_MBDRC_IIR_CFG_NUM_STAGES_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_IN_ATTACK,
			TEGRA210_MBDRC_IN_ATTACK_TC_MASK,
			params->in_attack_tc <<
				TEGRA210_MBDRC_IN_ATTACK_TC_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_IN_RELEASE,
			TEGRA210_MBDRC_IN_RELEASE_TC_MASK,
			params->in_release_tc <<
				TEGRA210_MBDRC_IN_RELEASE_TC_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_FAST_ATTACK,
			TEGRA210_MBDRC_FAST_ATTACK_TC_MASK,
			params->fast_attack_tc <<
				TEGRA210_MBDRC_FAST_ATTACK_TC_SHIFT);

		val = (((params->in_threshold[0] >>
			 TEGRA210_MBDRC_THRESH_1ST_SHIFT) &
			TEGRA210_MBDRC_THRESH_1ST_MASK) |
			((params->in_threshold[1] >>
			  TEGRA210_MBDRC_THRESH_2ND_SHIFT) &
			 TEGRA210_MBDRC_THRESH_2ND_MASK) |
			((params->in_threshold[2] >>
			  TEGRA210_MBDRC_THRESH_3RD_SHIFT) &
			 TEGRA210_MBDRC_THRESH_3RD_MASK) |
			((params->in_threshold[3] >>
			  TEGRA210_MBDRC_THRESH_4TH_SHIFT) &
			 TEGRA210_MBDRC_THRESH_4TH_MASK));

		regmap_update_bits(ope->mbdrc_regmap,
				   reg_off + TEGRA210_MBDRC_IN_THRESHOLD,
				   0xffffffff, val);

		val = (((params->out_threshold[0] >>
			 TEGRA210_MBDRC_THRESH_1ST_SHIFT) &
			TEGRA210_MBDRC_THRESH_1ST_MASK) |
			((params->out_threshold[1] >>
			  TEGRA210_MBDRC_THRESH_2ND_SHIFT) &
			 TEGRA210_MBDRC_THRESH_2ND_MASK) |
			((params->out_threshold[2] >>
			  TEGRA210_MBDRC_THRESH_3RD_SHIFT) &
			 TEGRA210_MBDRC_THRESH_3RD_MASK) |
			((params->out_threshold[3] >>
			  TEGRA210_MBDRC_THRESH_4TH_SHIFT) &
			 TEGRA210_MBDRC_THRESH_4TH_MASK));

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_OUT_THRESHOLD,
			0xffffffff, val);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_RATIO_1ST,
			TEGRA210_MBDRC_RATIO_1ST_MASK,
			params->ratio[0] << TEGRA210_MBDRC_RATIO_1ST_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_RATIO_2ND,
			TEGRA210_MBDRC_RATIO_2ND_MASK,
			params->ratio[1] << TEGRA210_MBDRC_RATIO_2ND_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_RATIO_3RD,
			TEGRA210_MBDRC_RATIO_3RD_MASK,
			params->ratio[2] << TEGRA210_MBDRC_RATIO_3RD_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_RATIO_4TH,
			TEGRA210_MBDRC_RATIO_4TH_MASK,
			params->ratio[3] << TEGRA210_MBDRC_RATIO_4TH_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_RATIO_5TH,
			TEGRA210_MBDRC_RATIO_5TH_MASK,
			params->ratio[4] << TEGRA210_MBDRC_RATIO_5TH_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_MAKEUP_GAIN,
			TEGRA210_MBDRC_MAKEUP_GAIN_MASK,
			params->makeup_gain <<
				TEGRA210_MBDRC_MAKEUP_GAIN_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_INIT_GAIN,
			TEGRA210_MBDRC_INIT_GAIN_MASK,
			params->gain_init <<
				TEGRA210_MBDRC_INIT_GAIN_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_GAIN_ATTACK,
			TEGRA210_MBDRC_GAIN_ATTACK_MASK,
			params->gain_attack_tc <<
				TEGRA210_MBDRC_GAIN_ATTACK_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_GAIN_RELEASE,
			TEGRA210_MBDRC_GAIN_RELEASE_MASK,
			params->gain_release_tc <<
				TEGRA210_MBDRC_GAIN_RELEASE_SHIFT);

		regmap_update_bits(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_FAST_RELEASE,
			TEGRA210_MBDRC_FAST_RELEASE_MASK,
			params->fast_release_tc <<
				TEGRA210_MBDRC_FAST_RELEASE_SHIFT);

		tegra210_mbdrc_write_ram(ope->mbdrc_regmap,
			reg_off + TEGRA210_MBDRC_CFG_RAM_CTRL,
			reg_off + TEGRA210_MBDRC_CFG_RAM_DATA, 0,
			(u32 *)&params->biquad_params[0],
			TEGRA210_MBDRC_MAX_BIQUAD_STAGES * 5);
	}

	pm_runtime_put_sync(cmpnt->dev);

	snd_soc_add_component_controls(cmpnt, tegra210_mbdrc_controls,
				       ARRAY_SIZE(tegra210_mbdrc_controls));

	return 0;
}

int tegra210_mbdrc_regmap_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_ope *ope = dev_get_drvdata(dev);
	struct device_node *child;
	struct resource mem;
	void __iomem *regs;
	int err;

	child = of_get_child_by_name(dev->of_node, "dynamic-range-compressor");
	if (!child)
		return -ENODEV;

	err = of_address_to_resource(child, 0, &mem);
	of_node_put(child);
	if (err < 0) {
		dev_err(dev, "fail to get MBDRC resource\n");
		return err;
	}

	mem.flags = IORESOURCE_MEM;
	regs = devm_ioremap_resource(dev, &mem);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	ope->mbdrc_regmap = devm_regmap_init_mmio(dev, regs,
						  &tegra210_mbdrc_regmap_cfg);
	if (IS_ERR(ope->mbdrc_regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(ope->mbdrc_regmap);
	}

	regcache_cache_only(ope->mbdrc_regmap, true);

	return 0;
}

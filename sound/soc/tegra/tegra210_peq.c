// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_peq.c - Tegra210 PEQ driver
//
// Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra210_ope.h"
#include "tegra210_peq.h"

static const struct reg_default tegra210_peq_reg_defaults[] = {
	{ TEGRA210_PEQ_CFG, 0x00000013},
	{ TEGRA210_PEQ_CFG_RAM_CTRL, 0x00004000},
	{ TEGRA210_PEQ_CFG_RAM_SHIFT_CTRL, 0x00004000},
};

static const u32 biquad_init_gains[TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH] = {
	1495012349, /* Pre-gain */

	/* Gains : b0, b1, a0, a1, a2 */
	536870912, -1073741824, 536870912, 2143508246, -1069773768, /* Band-0 */
	134217728, -265414508, 131766272, 2140402222, -1071252997,  /* Band-1 */
	268435456, -233515765, -33935948, 1839817267, -773826124,   /* Band-2 */
	536870912, -672537913, 139851540, 1886437554, -824433167,   /* Band-3 */
	268435456, -114439279, 173723964, 205743566, 278809729,     /* Band-4 */
	1, 0, 0, 0, 0, /* Band-5 */
	1, 0, 0, 0, 0, /* Band-6 */
	1, 0, 0, 0, 0, /* Band-7 */
	1, 0, 0, 0, 0, /* Band-8 */
	1, 0, 0, 0, 0, /* Band-9 */
	1, 0, 0, 0, 0, /* Band-10 */
	1, 0, 0, 0, 0, /* Band-11 */

	963423114, /* Post-gain */
};

static const u32 biquad_init_shifts[TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH] = {
	23, /* Pre-shift */
	30, 30, 30, 30, 30, 0, 0, 0, 0, 0, 0, 0, /* Shift for bands */
	28, /* Post-shift */
};

static s32 biquad_coeff_buffer[TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH];

static void tegra210_peq_read_ram(struct regmap *regmap, unsigned int reg_ctrl,
				  unsigned int reg_data, unsigned int ram_offset,
				  unsigned int *data, size_t size)
{
	unsigned int val;
	unsigned int i;

	val = ram_offset & TEGRA210_PEQ_RAM_CTRL_RAM_ADDR_MASK;
	val |= TEGRA210_PEQ_RAM_CTRL_ADDR_INIT_EN;
	val |= TEGRA210_PEQ_RAM_CTRL_SEQ_ACCESS_EN;
	val |= TEGRA210_PEQ_RAM_CTRL_RW_READ;

	regmap_write(regmap, reg_ctrl, val);

	/*
	 * Since all ahub non-io modules work under same ahub clock it is not
	 * necessary to check ahub read busy bit after every read.
	 */
	for (i = 0; i < size; i++)
		regmap_read(regmap, reg_data, &data[i]);
}

static void tegra210_peq_write_ram(struct regmap *regmap, unsigned int reg_ctrl,
				   unsigned int reg_data, unsigned int ram_offset,
				   unsigned int *data, size_t size)
{
	unsigned int val;
	unsigned int i;

	val = ram_offset & TEGRA210_PEQ_RAM_CTRL_RAM_ADDR_MASK;
	val |= TEGRA210_PEQ_RAM_CTRL_ADDR_INIT_EN;
	val |= TEGRA210_PEQ_RAM_CTRL_SEQ_ACCESS_EN;
	val |= TEGRA210_PEQ_RAM_CTRL_RW_WRITE;

	regmap_write(regmap, reg_ctrl, val);

	for (i = 0; i < size; i++)
		regmap_write(regmap, reg_data, data[i]);
}

static int tegra210_peq_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int val;

	regmap_read(ope->peq_regmap, mc->reg, &val);

	ucontrol->value.integer.value[0] = (val >> mc->shift) & mask;

	if (!mc->invert)
		return 0;

	ucontrol->value.integer.value[0] =
		mc->max - ucontrol->value.integer.value[0];

	return 0;
}

static int tegra210_peq_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mask = (1 << fls(mc->max)) - 1;
	bool change = false;
	unsigned int val;

	val = (ucontrol->value.integer.value[0] & mask);

	if (mc->invert)
		val = mc->max - val;

	val = val << mc->shift;

	regmap_update_bits_check(ope->peq_regmap, mc->reg, (mask << mc->shift),
				 val, &change);

	return change ? 1 : 0;
}

static int tegra210_peq_ram_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	u32 i, reg_ctrl = params->soc.base;
	u32 reg_data = reg_ctrl + cmpnt->val_bytes;
	s32 *data = (s32 *)biquad_coeff_buffer;

	pm_runtime_get_sync(cmpnt->dev);

	tegra210_peq_read_ram(ope->peq_regmap, reg_ctrl, reg_data,
			      params->shift, data, params->soc.num_regs);

	pm_runtime_put_sync(cmpnt->dev);

	for (i = 0; i < params->soc.num_regs; i++)
		ucontrol->value.integer.value[i] = (long)data[i];

	return 0;
}

static int tegra210_peq_ram_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	u32 i, reg_ctrl = params->soc.base;
	u32 reg_data = reg_ctrl + cmpnt->val_bytes;
	s32 *data = (s32 *)biquad_coeff_buffer;

	for (i = 0; i < params->soc.num_regs; i++)
		data[i] = (s32)ucontrol->value.integer.value[i];

	pm_runtime_get_sync(cmpnt->dev);

	tegra210_peq_write_ram(ope->peq_regmap, reg_ctrl, reg_data,
			       params->shift, data, params->soc.num_regs);

	pm_runtime_put_sync(cmpnt->dev);

	return 1;
}

static int tegra210_peq_param_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	struct soc_bytes *params = (void *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->value.integer.min = INT_MIN;
	uinfo->value.integer.max = INT_MAX;
	uinfo->count = params->num_regs;

	return 0;
}

#define TEGRA210_PEQ_GAIN_PARAMS_CTRL(chan)				  \
	TEGRA_SOC_BYTES_EXT("PEQ Channel-" #chan " Biquad Gain Params",	  \
		TEGRA210_PEQ_CFG_RAM_CTRL,				  \
		TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH,			  \
		(TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH * chan), 0xffffffff, \
		tegra210_peq_ram_get, tegra210_peq_ram_put,		  \
		tegra210_peq_param_info)

#define TEGRA210_PEQ_SHIFT_PARAMS_CTRL(chan)				  \
	TEGRA_SOC_BYTES_EXT("PEQ Channel-" #chan " Biquad Shift Params",  \
		TEGRA210_PEQ_CFG_RAM_SHIFT_CTRL,			  \
		TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH,			  \
		(TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH * chan), 0x1f,	  \
		tegra210_peq_ram_get, tegra210_peq_ram_put,		  \
		tegra210_peq_param_info)

static const struct snd_kcontrol_new tegra210_peq_controls[] = {
	SOC_SINGLE_EXT("PEQ Active", TEGRA210_PEQ_CFG,
		       TEGRA210_PEQ_CFG_MODE_SHIFT, 1, 0,
		       tegra210_peq_get, tegra210_peq_put),

	SOC_SINGLE_EXT("PEQ Biquad Stages", TEGRA210_PEQ_CFG,
		       TEGRA210_PEQ_CFG_BIQUAD_STAGES_SHIFT,
		       TEGRA210_PEQ_MAX_BIQUAD_STAGES - 1, 0,
		       tegra210_peq_get, tegra210_peq_put),

	TEGRA210_PEQ_GAIN_PARAMS_CTRL(0),
	TEGRA210_PEQ_GAIN_PARAMS_CTRL(1),
	TEGRA210_PEQ_GAIN_PARAMS_CTRL(2),
	TEGRA210_PEQ_GAIN_PARAMS_CTRL(3),
	TEGRA210_PEQ_GAIN_PARAMS_CTRL(4),
	TEGRA210_PEQ_GAIN_PARAMS_CTRL(5),
	TEGRA210_PEQ_GAIN_PARAMS_CTRL(6),
	TEGRA210_PEQ_GAIN_PARAMS_CTRL(7),

	TEGRA210_PEQ_SHIFT_PARAMS_CTRL(0),
	TEGRA210_PEQ_SHIFT_PARAMS_CTRL(1),
	TEGRA210_PEQ_SHIFT_PARAMS_CTRL(2),
	TEGRA210_PEQ_SHIFT_PARAMS_CTRL(3),
	TEGRA210_PEQ_SHIFT_PARAMS_CTRL(4),
	TEGRA210_PEQ_SHIFT_PARAMS_CTRL(5),
	TEGRA210_PEQ_SHIFT_PARAMS_CTRL(6),
	TEGRA210_PEQ_SHIFT_PARAMS_CTRL(7),
};

static bool tegra210_peq_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_PEQ_SOFT_RESET:
	case TEGRA210_PEQ_CG:
	case TEGRA210_PEQ_CFG ... TEGRA210_PEQ_CFG_RAM_SHIFT_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_peq_rd_reg(struct device *dev, unsigned int reg)
{
	if (tegra210_peq_wr_reg(dev, reg))
		return true;

	switch (reg) {
	case TEGRA210_PEQ_STATUS:
		return true;
	default:
		return false;
	}
}

static bool tegra210_peq_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_PEQ_SOFT_RESET:
	case TEGRA210_PEQ_STATUS:
	case TEGRA210_PEQ_CFG_RAM_CTRL ... TEGRA210_PEQ_CFG_RAM_SHIFT_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_peq_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_PEQ_CFG_RAM_DATA:
	case TEGRA210_PEQ_CFG_RAM_SHIFT_DATA:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra210_peq_regmap_config = {
	.name			= "peq",
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_PEQ_CFG_RAM_SHIFT_DATA,
	.writeable_reg		= tegra210_peq_wr_reg,
	.readable_reg		= tegra210_peq_rd_reg,
	.volatile_reg		= tegra210_peq_volatile_reg,
	.precious_reg		= tegra210_peq_precious_reg,
	.reg_defaults		= tegra210_peq_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_peq_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

void tegra210_peq_restore(struct regmap *regmap, u32 *biquad_gains,
			  u32 *biquad_shifts)
{
	unsigned int i;

	for (i = 0; i < TEGRA210_PEQ_MAX_CHANNELS; i++) {
		tegra210_peq_write_ram(regmap, TEGRA210_PEQ_CFG_RAM_CTRL,
			TEGRA210_PEQ_CFG_RAM_DATA,
			(i * TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH),
			biquad_gains,
			TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH);

		tegra210_peq_write_ram(regmap,
			TEGRA210_PEQ_CFG_RAM_SHIFT_CTRL,
			TEGRA210_PEQ_CFG_RAM_SHIFT_DATA,
			(i * TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH),
			biquad_shifts,
			TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH);

	}
}

void tegra210_peq_save(struct regmap *regmap, u32 *biquad_gains,
		       u32 *biquad_shifts)
{
	unsigned int i;

	for (i = 0; i < TEGRA210_PEQ_MAX_CHANNELS; i++) {
		tegra210_peq_read_ram(regmap,
			TEGRA210_PEQ_CFG_RAM_CTRL,
			TEGRA210_PEQ_CFG_RAM_DATA,
			(i * TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH),
			biquad_gains,
			TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH);

		tegra210_peq_read_ram(regmap,
			TEGRA210_PEQ_CFG_RAM_SHIFT_CTRL,
			TEGRA210_PEQ_CFG_RAM_SHIFT_DATA,
			(i * TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH),
			biquad_shifts,
			TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH);
	}
}

int tegra210_peq_component_init(struct snd_soc_component *cmpnt)
{
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	unsigned int i;

	pm_runtime_get_sync(cmpnt->dev);
	regmap_update_bits(ope->peq_regmap, TEGRA210_PEQ_CFG,
		TEGRA210_PEQ_CFG_MODE_MASK,
		0 << TEGRA210_PEQ_CFG_MODE_SHIFT);
	regmap_update_bits(ope->peq_regmap, TEGRA210_PEQ_CFG,
		TEGRA210_PEQ_CFG_BIQUAD_STAGES_MASK,
		(TEGRA210_PEQ_BIQUAD_INIT_STAGE - 1) <<
		TEGRA210_PEQ_CFG_BIQUAD_STAGES_SHIFT);

	/* Initialize PEQ AHUB RAM with default params */
	for (i = 0; i < TEGRA210_PEQ_MAX_CHANNELS; i++) {

		/* Set default gain params */
		tegra210_peq_write_ram(ope->peq_regmap,
			TEGRA210_PEQ_CFG_RAM_CTRL,
			TEGRA210_PEQ_CFG_RAM_DATA,
			(i * TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH),
			(u32 *)&biquad_init_gains,
			TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH);

		/* Set default shift params */
		tegra210_peq_write_ram(ope->peq_regmap,
			TEGRA210_PEQ_CFG_RAM_SHIFT_CTRL,
			TEGRA210_PEQ_CFG_RAM_SHIFT_DATA,
			(i * TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH),
			(u32 *)&biquad_init_shifts,
			TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH);

	}

	pm_runtime_put_sync(cmpnt->dev);

	snd_soc_add_component_controls(cmpnt, tegra210_peq_controls,
				       ARRAY_SIZE(tegra210_peq_controls));

	return 0;
}

int tegra210_peq_regmap_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_ope *ope = dev_get_drvdata(dev);
	struct device_node *child;
	struct resource mem;
	void __iomem *regs;
	int err;

	child = of_get_child_by_name(dev->of_node, "equalizer");
	if (!child)
		return -ENODEV;

	err = of_address_to_resource(child, 0, &mem);
	of_node_put(child);
	if (err < 0) {
		dev_err(dev, "fail to get PEQ resource\n");
		return err;
	}

	mem.flags = IORESOURCE_MEM;
	regs = devm_ioremap_resource(dev, &mem);
	if (IS_ERR(regs))
		return PTR_ERR(regs);
	ope->peq_regmap = devm_regmap_init_mmio(dev, regs,
						&tegra210_peq_regmap_config);
	if (IS_ERR(ope->peq_regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(ope->peq_regmap);
	}

	regcache_cache_only(ope->peq_regmap, true);

	return 0;
}

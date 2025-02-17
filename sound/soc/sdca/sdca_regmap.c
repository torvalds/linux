// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/bitops.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/types.h>
#include <sound/sdca_function.h>
#include <sound/sdca_regmap.h>

static struct sdca_entity *
function_find_entity(struct sdca_function_data *function, unsigned int reg)
{
	int i;

	for (i = 0; i < function->num_entities; i++)
		if (SDW_SDCA_CTL_ENT(reg) == function->entities[i].id)
			return &function->entities[i];

	return NULL;
}

static struct sdca_control *
entity_find_control(struct sdca_entity *entity, unsigned int reg)
{
	int i;

	for (i = 0; i < entity->num_controls; i++) {
		if (SDW_SDCA_CTL_CSEL(reg) == entity->controls[i].sel)
			return &entity->controls[i];
	}

	return NULL;
}

static struct sdca_control *
function_find_control(struct sdca_function_data *function, unsigned int reg)
{
	struct sdca_entity *entity;

	entity = function_find_entity(function, reg);
	if (!entity)
		return NULL;

	return entity_find_control(entity, reg);
}

/**
 * sdca_regmap_readable - return if a given SDCA Control is readable
 * @function: Pointer to the Function information.
 * @reg: Register address/Control to be processed.
 *
 * Return: Returns true if the register is readable.
 */
bool sdca_regmap_readable(struct sdca_function_data *function, unsigned int reg)
{
	struct sdca_control *control;

	if (!SDW_SDCA_VALID_CTL(reg))
		return false;

	control = function_find_control(function, reg);
	if (!control)
		return false;

	switch (control->mode) {
	case SDCA_ACCESS_MODE_RW:
	case SDCA_ACCESS_MODE_RO:
	case SDCA_ACCESS_MODE_DUAL:
	case SDCA_ACCESS_MODE_RW1S:
	case SDCA_ACCESS_MODE_RW1C:
		/* No access to registers marked solely for device use */
		return control->layers & ~SDCA_ACCESS_LAYER_DEVICE;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS(sdca_regmap_readable, "SND_SOC_SDCA");

/**
 * sdca_regmap_writeable - return if a given SDCA Control is writeable
 * @function: Pointer to the Function information.
 * @reg: Register address/Control to be processed.
 *
 * Return: Returns true if the register is writeable.
 */
bool sdca_regmap_writeable(struct sdca_function_data *function, unsigned int reg)
{
	struct sdca_control *control;

	if (!SDW_SDCA_VALID_CTL(reg))
		return false;

	control = function_find_control(function, reg);
	if (!control)
		return false;

	switch (control->mode) {
	case SDCA_ACCESS_MODE_RW:
	case SDCA_ACCESS_MODE_DUAL:
	case SDCA_ACCESS_MODE_RW1S:
	case SDCA_ACCESS_MODE_RW1C:
		/* No access to registers marked solely for device use */
		return control->layers & ~SDCA_ACCESS_LAYER_DEVICE;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS(sdca_regmap_writeable, "SND_SOC_SDCA");

/**
 * sdca_regmap_volatile - return if a given SDCA Control is volatile
 * @function: Pointer to the Function information.
 * @reg: Register address/Control to be processed.
 *
 * Return: Returns true if the register is volatile.
 */
bool sdca_regmap_volatile(struct sdca_function_data *function, unsigned int reg)
{
	struct sdca_control *control;

	if (!SDW_SDCA_VALID_CTL(reg))
		return false;

	control = function_find_control(function, reg);
	if (!control)
		return false;

	switch (control->mode) {
	case SDCA_ACCESS_MODE_RO:
	case SDCA_ACCESS_MODE_RW1S:
	case SDCA_ACCESS_MODE_RW1C:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS(sdca_regmap_volatile, "SND_SOC_SDCA");

/**
 * sdca_regmap_deferrable - return if a given SDCA Control is deferrable
 * @function: Pointer to the Function information.
 * @reg: Register address/Control to be processed.
 *
 * Return: Returns true if the register is deferrable.
 */
bool sdca_regmap_deferrable(struct sdca_function_data *function, unsigned int reg)
{
	struct sdca_control *control;

	if (!SDW_SDCA_VALID_CTL(reg))
		return false;

	control = function_find_control(function, reg);
	if (!control)
		return false;

	return control->deferrable;
}
EXPORT_SYMBOL_NS(sdca_regmap_deferrable, "SND_SOC_SDCA");

/**
 * sdca_regmap_mbq_size - return size in bytes of a given SDCA Control
 * @function: Pointer to the Function information.
 * @reg: Register address/Control to be processed.
 *
 * Return: Returns the size in bytes of the Control.
 */
int sdca_regmap_mbq_size(struct sdca_function_data *function, unsigned int reg)
{
	struct sdca_control *control;

	if (!SDW_SDCA_VALID_CTL(reg))
		return -EINVAL;

	control = function_find_control(function, reg);
	if (!control)
		return false;

	return clamp_val(control->nbits / BITS_PER_BYTE, sizeof(u8), sizeof(u32));
}
EXPORT_SYMBOL_NS(sdca_regmap_mbq_size, "SND_SOC_SDCA");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SDCA library");

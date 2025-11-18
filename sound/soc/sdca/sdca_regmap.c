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
#include <linux/regmap.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/types.h>
#include <sound/sdca.h>
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

	if (!(BIT(SDW_SDCA_CTL_CNUM(reg)) & control->cn_list))
		return false;

	switch (control->mode) {
	case SDCA_ACCESS_MODE_RW:
	case SDCA_ACCESS_MODE_RO:
	case SDCA_ACCESS_MODE_RW1S:
	case SDCA_ACCESS_MODE_RW1C:
		if (SDW_SDCA_NEXT_CTL(0) & reg)
			return false;
		fallthrough;
	case SDCA_ACCESS_MODE_DUAL:
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

	if (!(BIT(SDW_SDCA_CTL_CNUM(reg)) & control->cn_list))
		return false;

	switch (control->mode) {
	case SDCA_ACCESS_MODE_RW:
	case SDCA_ACCESS_MODE_RW1S:
	case SDCA_ACCESS_MODE_RW1C:
		if (SDW_SDCA_NEXT_CTL(0) & reg)
			return false;
		fallthrough;
	case SDCA_ACCESS_MODE_DUAL:
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
		return -EINVAL;

	return clamp_val(control->nbits / BITS_PER_BYTE, sizeof(u8), sizeof(u32));
}
EXPORT_SYMBOL_NS(sdca_regmap_mbq_size, "SND_SOC_SDCA");

/**
 * sdca_regmap_count_constants - count the number of DisCo constant Controls
 * @dev: Pointer to the device.
 * @function: Pointer to the Function information, to be parsed.
 *
 * This function returns the number of DisCo constant Controls present
 * in a function. Typically this information will be used to populate
 * the regmap defaults array, allowing drivers to access the values of
 * DisCo constants as any other physical register.
 *
 * Return: Returns number of DisCo constant controls, or a negative error
 * code on failure.
 */
int sdca_regmap_count_constants(struct device *dev,
				struct sdca_function_data *function)
{
	int nconsts = 0;
	int i, j;

	for (i = 0; i < function->num_entities; i++) {
		struct sdca_entity *entity = &function->entities[i];

		for (j = 0; j < entity->num_controls; j++) {
			if (entity->controls[j].mode == SDCA_ACCESS_MODE_DC)
				nconsts += hweight64(entity->controls[j].cn_list);
		}
	}

	return nconsts;
}
EXPORT_SYMBOL_NS(sdca_regmap_count_constants, "SND_SOC_SDCA");

/**
 * sdca_regmap_populate_constants - fill an array with DisCo constant values
 * @dev: Pointer to the device.
 * @function: Pointer to the Function information, to be parsed.
 * @consts: Pointer to the array which should be filled with the DisCo
 * constant values.
 *
 * This function will populate a regmap struct reg_default array with
 * the values of the DisCo constants for a given Function. This
 * allows to access the values of DisCo constants the same as any
 * other physical register.
 *
 * Return: Returns the number of constants populated on success, a negative
 * error code on failure.
 */
int sdca_regmap_populate_constants(struct device *dev,
				   struct sdca_function_data *function,
				   struct reg_default *consts)
{
	int i, j, k, l;

	for (i = 0, k = 0; i < function->num_entities; i++) {
		struct sdca_entity *entity = &function->entities[i];

		for (j = 0; j < entity->num_controls; j++) {
			struct sdca_control *control = &entity->controls[j];
			int cn;

			if (control->mode != SDCA_ACCESS_MODE_DC)
				continue;

			l = 0;
			for_each_set_bit(cn, (unsigned long *)&control->cn_list,
					 BITS_PER_TYPE(control->cn_list)) {
				consts[k].reg = SDW_SDCA_CTL(function->desc->adr,
							     entity->id,
							     control->sel, cn);
				consts[k].def = control->values[l];
				k++;
				l++;
			}
		}
	}

	return k;
}
EXPORT_SYMBOL_NS(sdca_regmap_populate_constants, "SND_SOC_SDCA");

/**
 * sdca_regmap_write_defaults - write out DisCo defaults to device
 * @dev: Pointer to the device.
 * @regmap: Pointer to the Function register map.
 * @function: Pointer to the Function information, to be parsed.
 *
 * This function will write out to the hardware all the DisCo default and
 * fixed value controls. This will cause them to be populated into the cache,
 * and subsequent handling can be done through a cache sync.
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_regmap_write_defaults(struct device *dev, struct regmap *regmap,
			       struct sdca_function_data *function)
{
	int i, j, k;
	int ret;

	for (i = 0; i < function->num_entities; i++) {
		struct sdca_entity *entity = &function->entities[i];

		for (j = 0; j < entity->num_controls; j++) {
			struct sdca_control *control = &entity->controls[j];
			int cn;

			if (control->mode == SDCA_ACCESS_MODE_DC)
				continue;

			if (!control->has_default && !control->has_fixed)
				continue;

			k = 0;
			for_each_set_bit(cn, (unsigned long *)&control->cn_list,
					 BITS_PER_TYPE(control->cn_list)) {
				unsigned int reg;

				reg = SDW_SDCA_CTL(function->desc->adr, entity->id,
						   control->sel, cn);

				ret = regmap_write(regmap, reg, control->values[k]);
				if (ret)
					return ret;

				k++;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS(sdca_regmap_write_defaults, "SND_SOC_SDCA");

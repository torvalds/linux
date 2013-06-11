/*
    access.c - Part of libsensors, a Linux library for reading sensor data.
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>
    Copyright (C) 2007-2009   Jean Delvare <khali@linux-fr.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
    MA 02110-1301 USA.
*/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "access.h"
#include "sensors.h"
#include "data.h"
#include "error.h"
#include "sysfs.h"

/* We watch the recursion depth for variables only, as an easy way to
   detect cycles. */
#define DEPTH_MAX	8

static int sensors_eval_expr(const sensors_chip_features *chip_features,
			     const sensors_expr *expr,
			     double val, int depth, double *result);

/* Compare two chips name descriptions, to see whether they could match.
   Return 0 if it does not match, return 1 if it does match. */
static int sensors_match_chip(const sensors_chip_name *chip1,
		       const sensors_chip_name *chip2)
{
	if ((chip1->prefix != SENSORS_CHIP_NAME_PREFIX_ANY) &&
	    (chip2->prefix != SENSORS_CHIP_NAME_PREFIX_ANY) &&
	    strcmp(chip1->prefix, chip2->prefix))
		return 0;

	if ((chip1->bus.type != SENSORS_BUS_TYPE_ANY) &&
	    (chip2->bus.type != SENSORS_BUS_TYPE_ANY) &&
	    (chip1->bus.type != chip2->bus.type))
		return 0;

	if ((chip1->bus.nr != SENSORS_BUS_NR_ANY) &&
	    (chip2->bus.nr != SENSORS_BUS_NR_ANY) &&
	    (chip1->bus.nr != chip2->bus.nr))
		return 0;

	if ((chip1->addr != chip2->addr) &&
	    (chip1->addr != SENSORS_CHIP_NAME_ADDR_ANY) &&
	    (chip2->addr != SENSORS_CHIP_NAME_ADDR_ANY))
		return 0;

	return 1;
}

/* Returns, one by one, a pointer to all sensor_chip structs of the
   config file which match with the given chip name. Last should be
   the value returned by the last call, or NULL if this is the first
   call. Returns NULL if no more matches are found. Do not modify
   the struct the return value points to! 
   Note that this visits the list of chips from last to first. Usually,
   you want the match that was latest in the config file. */
static sensors_chip *
sensors_for_all_config_chips(const sensors_chip_name *name,
			     const sensors_chip *last)
{
	int nr, i;
	sensors_chip_name_list chips;

	for (nr = last ? last - sensors_config_chips - 1 :
			 sensors_config_chips_count - 1; nr >= 0; nr--) {

		chips = sensors_config_chips[nr].chips;
		for (i = 0; i < chips.fits_count; i++) {
			if (sensors_match_chip(&chips.fits[i], name))
				return sensors_config_chips + nr;
		}
	}
	return NULL;
}

/* Look up a chip in the intern chip list, and return a pointer to it.
   Do not modify the struct the return value points to! Returns NULL if
   not found.*/
static const sensors_chip_features *
sensors_lookup_chip(const sensors_chip_name *name)
{
	int i;

	for (i = 0; i < sensors_proc_chips_count; i++)
		if (sensors_match_chip(&sensors_proc_chips[i].chip, name))
			return &sensors_proc_chips[i];

	return NULL;
}

/* Look up a subfeature of the given chip, and return a pointer to it.
   Do not modify the struct the return value points to! Returns NULL if
   not found.*/
static const sensors_subfeature *
sensors_lookup_subfeature_nr(const sensors_chip_features *chip,
			     int subfeat_nr)
{
	if (subfeat_nr < 0 ||
	    subfeat_nr >= chip->subfeature_count)
		return NULL;
	return chip->subfeature + subfeat_nr;
}

/* Look up a feature of the given chip, and return a pointer to it.
   Do not modify the struct the return value points to! Returns NULL if
   not found.*/
static const sensors_feature *
sensors_lookup_feature_nr(const sensors_chip_features *chip, int feat_nr)
{
	if (feat_nr < 0 ||
	    feat_nr >= chip->feature_count)
		return NULL;
	return chip->feature + feat_nr;
}

/* Look up a subfeature by name, and return a pointer to it.
   Do not modify the struct the return value points to! Returns NULL if 
   not found.*/
static const sensors_subfeature *
sensors_lookup_subfeature_name(const sensors_chip_features *chip,
			       const char *name)
{
	int j;

	for (j = 0; j < chip->subfeature_count; j++)
		if (!strcmp(chip->subfeature[j].name, name))
			return chip->subfeature + j;
	return NULL;
}

/* Check whether the chip name is an 'absolute' name, which can only match
   one chip, or whether it has wildcards. Returns 0 if it is absolute, 1
   if there are wildcards. */
int sensors_chip_name_has_wildcards(const sensors_chip_name *chip)
{
	if ((chip->prefix == SENSORS_CHIP_NAME_PREFIX_ANY) ||
	    (chip->bus.type == SENSORS_BUS_TYPE_ANY) ||
	    (chip->bus.nr == SENSORS_BUS_NR_ANY) ||
	    (chip->addr == SENSORS_CHIP_NAME_ADDR_ANY))
		return 1;
	else
		return 0;
}

/* Look up the label for a given feature. Note that chip should not
   contain wildcard values! The returned string is newly allocated (free it
   yourself). On failure, NULL is returned.
   If no label exists for this feature, its name is returned itself. */
char *sensors_get_label(const sensors_chip_name *name,
			const sensors_feature *feature)
{
	char *label;
	const sensors_chip *chip;
	char buf[PATH_MAX];
	FILE *f;
	int i;

	if (sensors_chip_name_has_wildcards(name))
		return NULL;

	for (chip = NULL; (chip = sensors_for_all_config_chips(name, chip));)
		for (i = 0; i < chip->labels_count; i++)
			if (!strcmp(feature->name, chip->labels[i].name)) {
				label = chip->labels[i].value;
				goto sensors_get_label_exit;
			}

	/* No user specified label, check for a _label sysfs file */
	snprintf(buf, PATH_MAX, "%s/%s_label", name->path, feature->name);
	
	if ((f = fopen(buf, "r"))) {
		i = fread(buf, 1, sizeof(buf), f);
		fclose(f);
		if (i > 0) {
			/* i - 1 to strip the '\n' at the end */
			buf[i - 1] = 0;
			label = buf;
			goto sensors_get_label_exit;
		}
	}

	/* No label, return the feature name instead */
	label = feature->name;
	
sensors_get_label_exit:
	label = strdup(label);
	if (!label)
		sensors_fatal_error(__func__, "Allocating label text");
	return label;
}

/* Looks up whether a feature should be ignored. Returns
   1 if it should be ignored, 0 if not. */
static int sensors_get_ignored(const sensors_chip_name *name,
			       const sensors_feature *feature)
{
	const sensors_chip *chip;
	int i;

	for (chip = NULL; (chip = sensors_for_all_config_chips(name, chip));)
		for (i = 0; i < chip->ignores_count; i++)
			if (!strcmp(feature->name, chip->ignores[i].name))
				return 1;
	return 0;
}

/* Read the value of a subfeature of a certain chip. Note that chip should not
   contain wildcard values! This function will return 0 on success, and <0
   on failure. */
static int __sensors_get_value(const sensors_chip_name *name, int subfeat_nr,
			       int depth, double *result)
{
	const sensors_chip_features *chip_features;
	const sensors_subfeature *subfeature;
	const sensors_expr *expr = NULL;
	double val;
	int res, i;

	if (depth >= DEPTH_MAX)
		return -SENSORS_ERR_RECURSION;
	if (sensors_chip_name_has_wildcards(name))
		return -SENSORS_ERR_WILDCARDS;
	if (!(chip_features = sensors_lookup_chip(name)))
		return -SENSORS_ERR_NO_ENTRY;
	if (!(subfeature = sensors_lookup_subfeature_nr(chip_features,
							subfeat_nr)))
		return -SENSORS_ERR_NO_ENTRY;
	if (!(subfeature->flags & SENSORS_MODE_R))
		return -SENSORS_ERR_ACCESS_R;

	/* Apply compute statement if it exists */
	if (subfeature->flags & SENSORS_COMPUTE_MAPPING) {
		const sensors_feature *feature;
		const sensors_chip *chip;

		feature = sensors_lookup_feature_nr(chip_features,
					subfeature->mapping);

		chip = NULL;
		while (!expr &&
		       (chip = sensors_for_all_config_chips(name, chip)))
			for (i = 0; i < chip->computes_count; i++) {
				if (!strcmp(feature->name,
					    chip->computes[i].name)) {
					expr = chip->computes[i].from_proc;
					break;
				}
			}
	}

	res = sensors_read_sysfs_attr(name, subfeature, &val);
	if (res)
		return res;
	if (!expr)
		*result = val;
	else if ((res = sensors_eval_expr(chip_features, expr, val, depth,
					  result)))
		return res;
	return 0;
}

int sensors_get_value(const sensors_chip_name *name, int subfeat_nr,
		      double *result)
{
	return __sensors_get_value(name, subfeat_nr, 0, result);
}

/* Set the value of a subfeature of a certain chip. Note that chip should not
   contain wildcard values! This function will return 0 on success, and <0
   on failure. */
int sensors_set_value(const sensors_chip_name *name, int subfeat_nr,
		      double value)
{
	const sensors_chip_features *chip_features;
	const sensors_subfeature *subfeature;
	const sensors_expr *expr = NULL;
	int i, res;
	double to_write;

	if (sensors_chip_name_has_wildcards(name))
		return -SENSORS_ERR_WILDCARDS;
	if (!(chip_features = sensors_lookup_chip(name)))
		return -SENSORS_ERR_NO_ENTRY;
	if (!(subfeature = sensors_lookup_subfeature_nr(chip_features,
							subfeat_nr)))
		return -SENSORS_ERR_NO_ENTRY;
	if (!(subfeature->flags & SENSORS_MODE_W))
		return -SENSORS_ERR_ACCESS_W;

	/* Apply compute statement if it exists */
	if (subfeature->flags & SENSORS_COMPUTE_MAPPING) {
		const sensors_feature *feature;
		const sensors_chip *chip;

		feature = sensors_lookup_feature_nr(chip_features,
					subfeature->mapping);

		chip = NULL;
		while (!expr &&
		       (chip = sensors_for_all_config_chips(name, chip)))
			for (i = 0; i < chip->computes_count; i++) {
				if (!strcmp(feature->name,
					    chip->computes[i].name)) {
					expr = chip->computes[i].to_proc;
					break;
				}
			}
	}

	to_write = value;
	if (expr)
		if ((res = sensors_eval_expr(chip_features, expr,
					     value, 0, &to_write)))
			return res;
	return sensors_write_sysfs_attr(name, subfeature, to_write);
}

const sensors_chip_name *sensors_get_detected_chips(const sensors_chip_name
						    *match, int *nr)
{
	const sensors_chip_name *res;

	while (*nr < sensors_proc_chips_count) {
		res = &sensors_proc_chips[(*nr)++].chip;
		if (!match || sensors_match_chip(res, match))
			return res;
	}
	return NULL;
}

const char *sensors_get_adapter_name(const sensors_bus_id *bus)
{
	int i;

	/* bus types with a single instance */
	switch (bus->type) {
	case SENSORS_BUS_TYPE_ISA:
		return "ISA adapter";
	case SENSORS_BUS_TYPE_PCI:
		return "PCI adapter";
	/* SPI should not be here, but for now SPI adapters have no name
	   so we don't have any custom string to return. */
	case SENSORS_BUS_TYPE_SPI:
		return "SPI adapter";
	case SENSORS_BUS_TYPE_VIRTUAL:
		return "Virtual device";
	case SENSORS_BUS_TYPE_ACPI:
		return "ACPI interface";
	/* HID should probably not be there either, but I don't know if
	   HID buses have a name nor where to find it. */
	case SENSORS_BUS_TYPE_HID:
		return "HID adapter";
	}

	/* bus types with several instances */
	for (i = 0; i < sensors_proc_bus_count; i++)
		if (sensors_proc_bus[i].bus.type == bus->type &&
		    sensors_proc_bus[i].bus.nr == bus->nr)
			return sensors_proc_bus[i].adapter;
	return NULL;
}

const sensors_feature *
sensors_get_features(const sensors_chip_name *name, int *nr)
{
	const sensors_chip_features *chip;

	if (!(chip = sensors_lookup_chip(name)))
		return NULL;	/* No such chip */

	while (*nr < chip->feature_count
	    && sensors_get_ignored(name, &chip->feature[*nr]))
		(*nr)++;
	if (*nr >= chip->feature_count)
		return NULL;
	return &chip->feature[(*nr)++];
}

const sensors_subfeature *
sensors_get_all_subfeatures(const sensors_chip_name *name,
			const sensors_feature *feature, int *nr)
{
	const sensors_chip_features *chip;
	const sensors_subfeature *subfeature;

	if (!(chip = sensors_lookup_chip(name)))
		return NULL;	/* No such chip */

	/* Seek directly to the first subfeature */
	if (*nr < feature->first_subfeature)
		*nr = feature->first_subfeature;

	if (*nr >= chip->subfeature_count)
		return NULL;	/* end of list */
	subfeature = &chip->subfeature[(*nr)++];
	if (subfeature->mapping == feature->number)
		return subfeature;
	return NULL;	/* end of subfeature list */
}

const sensors_subfeature *
sensors_get_subfeature(const sensors_chip_name *name,
		       const sensors_feature *feature,
		       sensors_subfeature_type type)
{
	const sensors_chip_features *chip;
	int i;

	if (!(chip = sensors_lookup_chip(name)))
		return NULL;	/* No such chip */

	for (i = feature->first_subfeature; i < chip->subfeature_count &&
	     chip->subfeature[i].mapping == feature->number; i++) {
		if (chip->subfeature[i].type == type)
			return &chip->subfeature[i];
	}
	return NULL;	/* No such subfeature */
}

/* Evaluate an expression */
int sensors_eval_expr(const sensors_chip_features *chip_features,
		      const sensors_expr *expr,
		      double val, int depth, double *result)
{
	double res1, res2;
	int res;
	const sensors_subfeature *subfeature;

	if (expr->kind == sensors_kind_val) {
		*result = expr->data.val;
		return 0;
	}
	if (expr->kind == sensors_kind_source) {
		*result = val;
		return 0;
	}
	if (expr->kind == sensors_kind_var) {
		if (!(subfeature = sensors_lookup_subfeature_name(chip_features,
							    expr->data.var)))
			return -SENSORS_ERR_NO_ENTRY;
		return __sensors_get_value(&chip_features->chip,
					   subfeature->number, depth + 1,
					   result);
	}
	if ((res = sensors_eval_expr(chip_features, expr->data.subexpr.sub1,
				     val, depth, &res1)))
		return res;
	if (expr->data.subexpr.sub2 &&
	    (res = sensors_eval_expr(chip_features, expr->data.subexpr.sub2,
				     val, depth, &res2)))
		return res;
	switch (expr->data.subexpr.op) {
	case sensors_add:
		*result = res1 + res2;
		return 0;
	case sensors_sub:
		*result = res1 - res2;
		return 0;
	case sensors_multiply:
		*result = res1 * res2;
		return 0;
	case sensors_divide:
		if (res2 == 0.0)
			return -SENSORS_ERR_DIV_ZERO;
		*result = res1 / res2;
		return 0;
	case sensors_negate:
		*result = -res1;
		return 0;
	case sensors_exp:
		*result = exp(res1);
		return 0;
	case sensors_log:
		if (res1 < 0.0)
			return -SENSORS_ERR_DIV_ZERO;
		*result = log(res1);
		return 0;
	}
	return 0;
}

/* Execute all set statements for this particular chip. The chip may not 
   contain wildcards!  This function will return 0 on success, and <0 on 
   failure. */
static int sensors_do_this_chip_sets(const sensors_chip_name *name)
{
	const sensors_chip_features *chip_features;
	sensors_chip *chip;
	double value;
	int i;
	int err = 0, res;
	const sensors_subfeature *subfeature;

	chip_features = sensors_lookup_chip(name);	/* Can't fail */

	for (chip = NULL; (chip = sensors_for_all_config_chips(name, chip));)
		for (i = 0; i < chip->sets_count; i++) {
			subfeature = sensors_lookup_subfeature_name(chip_features,
							chip->sets[i].name);
			if (!subfeature) {
				sensors_parse_error_wfn("Unknown feature name",
						    chip->sets[i].line.filename,
						    chip->sets[i].line.lineno);
				err = -SENSORS_ERR_NO_ENTRY;
				continue;
			}

			res = sensors_eval_expr(chip_features,
						chip->sets[i].value, 0,
						0, &value);
			if (res) {
				sensors_parse_error_wfn("Error parsing expression",
						    chip->sets[i].line.filename,
						    chip->sets[i].line.lineno);
				err = res;
				continue;
			}
			if ((res = sensors_set_value(name, subfeature->number,
						     value))) {
				sensors_parse_error_wfn("Failed to set value",
						chip->sets[i].line.filename,
						chip->sets[i].line.lineno);
				err = res;
				continue;
			}
		}
	return err;
}

/* Execute all set statements for this particular chip. The chip may contain
   wildcards!  This function will return 0 on success, and <0 on failure. */
int sensors_do_chip_sets(const sensors_chip_name *name)
{
	int nr, this_res;
	const sensors_chip_name *found_name;
	int res = 0;

	for (nr = 0; (found_name = sensors_get_detected_chips(name, &nr));) {
		this_res = sensors_do_this_chip_sets(found_name);
		if (this_res)
			res = this_res;
	}
	return res;
}

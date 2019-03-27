/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/*
 * Common routines used by zfs and zpool property management.
 */

#include <sys/zio.h>
#include <sys/spa.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_znode.h>
#include <sys/fs/zfs.h>

#include "zfs_prop.h"
#include "zfs_deleg.h"

#if defined(_KERNEL)
#include <sys/systm.h>
#include <sys/libkern.h>
#else
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#endif

static zprop_desc_t *
zprop_get_proptable(zfs_type_t type)
{
	if (type == ZFS_TYPE_POOL)
		return (zpool_prop_get_table());
	else
		return (zfs_prop_get_table());
}

static int
zprop_get_numprops(zfs_type_t type)
{
	if (type == ZFS_TYPE_POOL)
		return (ZPOOL_NUM_PROPS);
	else
		return (ZFS_NUM_PROPS);
}

void
zprop_register_impl(int prop, const char *name, zprop_type_t type,
    uint64_t numdefault, const char *strdefault, zprop_attr_t attr,
    int objset_types, const char *values, const char *colname,
    boolean_t rightalign, boolean_t visible, const zprop_index_t *idx_tbl)
{
	zprop_desc_t *prop_tbl = zprop_get_proptable(objset_types);
	zprop_desc_t *pd;

	pd = &prop_tbl[prop];

	ASSERT(pd->pd_name == NULL || pd->pd_name == name);
	ASSERT(name != NULL);
	ASSERT(colname != NULL);

	pd->pd_name = name;
	pd->pd_propnum = prop;
	pd->pd_proptype = type;
	pd->pd_numdefault = numdefault;
	pd->pd_strdefault = strdefault;
	pd->pd_attr = attr;
	pd->pd_types = objset_types;
	pd->pd_values = values;
	pd->pd_colname = colname;
	pd->pd_rightalign = rightalign;
	pd->pd_visible = visible;
	pd->pd_table = idx_tbl;
	pd->pd_table_size = 0;
	while (idx_tbl && (idx_tbl++)->pi_name != NULL)
		pd->pd_table_size++;
}

void
zprop_register_string(int prop, const char *name, const char *def,
    zprop_attr_t attr, int objset_types, const char *values,
    const char *colname)
{
	zprop_register_impl(prop, name, PROP_TYPE_STRING, 0, def, attr,
	    objset_types, values, colname, B_FALSE, B_TRUE, NULL);

}

void
zprop_register_number(int prop, const char *name, uint64_t def,
    zprop_attr_t attr, int objset_types, const char *values,
    const char *colname)
{
	zprop_register_impl(prop, name, PROP_TYPE_NUMBER, def, NULL, attr,
	    objset_types, values, colname, B_TRUE, B_TRUE, NULL);
}

void
zprop_register_index(int prop, const char *name, uint64_t def,
    zprop_attr_t attr, int objset_types, const char *values,
    const char *colname, const zprop_index_t *idx_tbl)
{
	zprop_register_impl(prop, name, PROP_TYPE_INDEX, def, NULL, attr,
	    objset_types, values, colname, B_TRUE, B_TRUE, idx_tbl);
}

void
zprop_register_hidden(int prop, const char *name, zprop_type_t type,
    zprop_attr_t attr, int objset_types, const char *colname)
{
	zprop_register_impl(prop, name, type, 0, NULL, attr,
	    objset_types, NULL, colname,
	    type == PROP_TYPE_NUMBER, B_FALSE, NULL);
}


/*
 * A comparison function we can use to order indexes into property tables.
 */
static int
zprop_compare(const void *arg1, const void *arg2)
{
	const zprop_desc_t *p1 = *((zprop_desc_t **)arg1);
	const zprop_desc_t *p2 = *((zprop_desc_t **)arg2);
	boolean_t p1ro, p2ro;

	p1ro = (p1->pd_attr == PROP_READONLY);
	p2ro = (p2->pd_attr == PROP_READONLY);

	if (p1ro == p2ro)
		return (strcmp(p1->pd_name, p2->pd_name));

	return (p1ro ? -1 : 1);
}

/*
 * Iterate over all properties in the given property table, calling back
 * into the specified function for each property. We will continue to
 * iterate until we either reach the end or the callback function returns
 * something other than ZPROP_CONT.
 */
int
zprop_iter_common(zprop_func func, void *cb, boolean_t show_all,
    boolean_t ordered, zfs_type_t type)
{
	int i, j, num_props, size, prop;
	zprop_desc_t *prop_tbl;
	zprop_desc_t **order;

	prop_tbl = zprop_get_proptable(type);
	num_props = zprop_get_numprops(type);
	size = num_props * sizeof (zprop_desc_t *);

#if defined(_KERNEL)
	order = kmem_alloc(size, KM_SLEEP);
#else
	if ((order = malloc(size)) == NULL)
		return (ZPROP_CONT);
#endif

	for (j = 0; j < num_props; j++)
		order[j] = &prop_tbl[j];

	if (ordered) {
		qsort((void *)order, num_props, sizeof (zprop_desc_t *),
		    zprop_compare);
	}

	prop = ZPROP_CONT;
	for (i = 0; i < num_props; i++) {
		if ((order[i]->pd_visible || show_all) &&
		    (func(order[i]->pd_propnum, cb) != ZPROP_CONT)) {
			prop = order[i]->pd_propnum;
			break;
		}
	}

#if defined(_KERNEL)
	kmem_free(order, size);
#else
	free(order);
#endif
	return (prop);
}

static boolean_t
propname_match(const char *p, size_t len, zprop_desc_t *prop_entry)
{
	const char *propname = prop_entry->pd_name;
#ifndef _KERNEL
	const char *colname = prop_entry->pd_colname;
	int c;
#endif

	if (len == strlen(propname) &&
	    strncmp(p, propname, len) == 0)
		return (B_TRUE);

#ifndef _KERNEL
	if (colname == NULL || len != strlen(colname))
		return (B_FALSE);

	for (c = 0; c < len; c++)
		if (p[c] != tolower(colname[c]))
			break;

	return (colname[c] == '\0');
#else
	return (B_FALSE);
#endif
}

typedef struct name_to_prop_cb {
	const char *propname;
	zprop_desc_t *prop_tbl;
} name_to_prop_cb_t;

static int
zprop_name_to_prop_cb(int prop, void *cb_data)
{
	name_to_prop_cb_t *data = cb_data;

	if (propname_match(data->propname, strlen(data->propname),
	    &data->prop_tbl[prop]))
		return (prop);

	return (ZPROP_CONT);
}

int
zprop_name_to_prop(const char *propname, zfs_type_t type)
{
	int prop;
	name_to_prop_cb_t cb_data;

	cb_data.propname = propname;
	cb_data.prop_tbl = zprop_get_proptable(type);

	prop = zprop_iter_common(zprop_name_to_prop_cb, &cb_data,
	    B_TRUE, B_FALSE, type);

	return (prop == ZPROP_CONT ? ZPROP_INVAL : prop);
}

int
zprop_string_to_index(int prop, const char *string, uint64_t *index,
    zfs_type_t type)
{
	zprop_desc_t *prop_tbl;
	const zprop_index_t *idx_tbl;
	int i;

	if (prop == ZPROP_INVAL || prop == ZPROP_CONT)
		return (-1);

	ASSERT(prop < zprop_get_numprops(type));
	prop_tbl = zprop_get_proptable(type);
	if ((idx_tbl = prop_tbl[prop].pd_table) == NULL)
		return (-1);

	for (i = 0; idx_tbl[i].pi_name != NULL; i++) {
		if (strcmp(string, idx_tbl[i].pi_name) == 0) {
			*index = idx_tbl[i].pi_value;
			return (0);
		}
	}

	return (-1);
}

int
zprop_index_to_string(int prop, uint64_t index, const char **string,
    zfs_type_t type)
{
	zprop_desc_t *prop_tbl;
	const zprop_index_t *idx_tbl;
	int i;

	if (prop == ZPROP_INVAL || prop == ZPROP_CONT)
		return (-1);

	ASSERT(prop < zprop_get_numprops(type));
	prop_tbl = zprop_get_proptable(type);
	if ((idx_tbl = prop_tbl[prop].pd_table) == NULL)
		return (-1);

	for (i = 0; idx_tbl[i].pi_name != NULL; i++) {
		if (idx_tbl[i].pi_value == index) {
			*string = idx_tbl[i].pi_name;
			return (0);
		}
	}

	return (-1);
}

/*
 * Return a random valid property value.  Used by ztest.
 */
uint64_t
zprop_random_value(int prop, uint64_t seed, zfs_type_t type)
{
	zprop_desc_t *prop_tbl;
	const zprop_index_t *idx_tbl;

	ASSERT((uint_t)prop < zprop_get_numprops(type));
	prop_tbl = zprop_get_proptable(type);
	idx_tbl = prop_tbl[prop].pd_table;

	if (idx_tbl == NULL)
		return (seed);

	return (idx_tbl[seed % prop_tbl[prop].pd_table_size].pi_value);
}

const char *
zprop_values(int prop, zfs_type_t type)
{
	zprop_desc_t *prop_tbl;

	ASSERT(prop != ZPROP_INVAL && prop != ZPROP_CONT);
	ASSERT(prop < zprop_get_numprops(type));

	prop_tbl = zprop_get_proptable(type);

	return (prop_tbl[prop].pd_values);
}

/*
 * Returns TRUE if the property applies to any of the given dataset types.
 */
boolean_t
zprop_valid_for_type(int prop, zfs_type_t type)
{
	zprop_desc_t *prop_tbl;

	if (prop == ZPROP_INVAL || prop == ZPROP_CONT)
		return (B_FALSE);

	ASSERT(prop < zprop_get_numprops(type));
	prop_tbl = zprop_get_proptable(type);
	return ((prop_tbl[prop].pd_types & type) != 0);
}

#ifndef _KERNEL

/*
 * Determines the minimum width for the column, and indicates whether it's fixed
 * or not.  Only string columns are non-fixed.
 */
size_t
zprop_width(int prop, boolean_t *fixed, zfs_type_t type)
{
	zprop_desc_t *prop_tbl, *pd;
	const zprop_index_t *idx;
	size_t ret;
	int i;

	ASSERT(prop != ZPROP_INVAL && prop != ZPROP_CONT);
	ASSERT(prop < zprop_get_numprops(type));

	prop_tbl = zprop_get_proptable(type);
	pd = &prop_tbl[prop];

	*fixed = B_TRUE;

	/*
	 * Start with the width of the column name.
	 */
	ret = strlen(pd->pd_colname);

	/*
	 * For fixed-width values, make sure the width is large enough to hold
	 * any possible value.
	 */
	switch (pd->pd_proptype) {
	case PROP_TYPE_NUMBER:
		/*
		 * The maximum length of a human-readable number is 5 characters
		 * ("20.4M", for example).
		 */
		if (ret < 5)
			ret = 5;
		/*
		 * 'creation' is handled specially because it's a number
		 * internally, but displayed as a date string.
		 */
		if (prop == ZFS_PROP_CREATION)
			*fixed = B_FALSE;
		break;
	case PROP_TYPE_INDEX:
		idx = prop_tbl[prop].pd_table;
		for (i = 0; idx[i].pi_name != NULL; i++) {
			if (strlen(idx[i].pi_name) > ret)
				ret = strlen(idx[i].pi_name);
		}
		break;

	case PROP_TYPE_STRING:
		*fixed = B_FALSE;
		break;
	}

	return (ret);
}

#endif

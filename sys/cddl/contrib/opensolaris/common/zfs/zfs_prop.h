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

#ifndef	_ZFS_PROP_H
#define	_ZFS_PROP_H

#include <sys/fs/zfs.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * For index types (e.g. compression and checksum), we want the numeric value
 * in the kernel, but the string value in userland.
 */
typedef enum {
	PROP_TYPE_NUMBER,	/* numeric value */
	PROP_TYPE_STRING,	/* string value */
	PROP_TYPE_INDEX		/* numeric value indexed by string */
} zprop_type_t;

typedef enum {
	PROP_DEFAULT,
	PROP_READONLY,
	PROP_INHERIT,
	/*
	 * ONETIME properties are a sort of conglomeration of READONLY
	 * and INHERIT.  They can be set only during object creation,
	 * after that they are READONLY.  If not explicitly set during
	 * creation, they can be inherited.
	 */
	PROP_ONETIME
} zprop_attr_t;

typedef struct zfs_index {
	const char *pi_name;
	uint64_t pi_value;
} zprop_index_t;

typedef struct {
	const char *pd_name;		/* human-readable property name */
	int pd_propnum;			/* property number */
	zprop_type_t pd_proptype;	/* string, boolean, index, number */
	const char *pd_strdefault;	/* default for strings */
	uint64_t pd_numdefault;		/* for boolean / index / number */
	zprop_attr_t pd_attr;		/* default, readonly, inherit */
	int pd_types;			/* bitfield of valid dataset types */
					/* fs | vol | snap; or pool */
	const char *pd_values;		/* string telling acceptable values */
	const char *pd_colname;		/* column header for "zfs list" */
	boolean_t pd_rightalign;	/* column alignment for "zfs list" */
	boolean_t pd_visible;		/* do we list this property with the */
					/* "zfs get" help message */
	const zprop_index_t *pd_table;	/* for index properties, a table */
					/* defining the possible values */
	size_t pd_table_size;		/* number of entries in pd_table[] */
} zprop_desc_t;

/*
 * zfs dataset property functions
 */
void zfs_prop_init(void);
zprop_type_t zfs_prop_get_type(zfs_prop_t);
boolean_t zfs_prop_delegatable(zfs_prop_t prop);
zprop_desc_t *zfs_prop_get_table(void);

/*
 * zpool property functions
 */
void zpool_prop_init(void);
zprop_type_t zpool_prop_get_type(zpool_prop_t);
zprop_desc_t *zpool_prop_get_table(void);

/*
 * Common routines to initialize property tables
 */
void zprop_register_impl(int, const char *, zprop_type_t, uint64_t,
    const char *, zprop_attr_t, int, const char *, const char *,
    boolean_t, boolean_t, const zprop_index_t *);
void zprop_register_string(int, const char *, const char *,
    zprop_attr_t attr, int, const char *, const char *);
void zprop_register_number(int, const char *, uint64_t, zprop_attr_t, int,
    const char *, const char *);
void zprop_register_index(int, const char *, uint64_t, zprop_attr_t, int,
    const char *, const char *, const zprop_index_t *);
void zprop_register_hidden(int, const char *, zprop_type_t, zprop_attr_t,
    int, const char *);

/*
 * Common routines for zfs and zpool property management
 */
int zprop_iter_common(zprop_func, void *, boolean_t, boolean_t, zfs_type_t);
int zprop_name_to_prop(const char *, zfs_type_t);
int zprop_string_to_index(int, const char *, uint64_t *, zfs_type_t);
int zprop_index_to_string(int, uint64_t, const char **, zfs_type_t);
uint64_t zprop_random_value(int, uint64_t, zfs_type_t);
const char *zprop_values(int, zfs_type_t);
size_t zprop_width(int, boolean_t *, zfs_type_t);
boolean_t zprop_valid_for_type(int, zfs_type_t);
boolean_t zfs_prop_written(const char *name);


#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_PROP_H */

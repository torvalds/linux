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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2010 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 */

#include <sys/zfs_context.h>

#if defined(_KERNEL)
#include <sys/systm.h>
#include <sys/sunddi.h>
#include <sys/ctype.h>
#else
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <libnvpair.h>
#include <ctype.h>
#endif
#include <sys/dsl_deleg.h>
#include "zfs_prop.h"
#include "zfs_deleg.h"
#include "zfs_namecheck.h"

zfs_deleg_perm_tab_t zfs_deleg_perm_tab[] = {
	{ZFS_DELEG_PERM_ALLOW},
	{ZFS_DELEG_PERM_BOOKMARK},
	{ZFS_DELEG_PERM_CLONE},
	{ZFS_DELEG_PERM_CREATE},
	{ZFS_DELEG_PERM_DESTROY},
	{ZFS_DELEG_PERM_DIFF},
	{ZFS_DELEG_PERM_MOUNT},
	{ZFS_DELEG_PERM_PROMOTE},
	{ZFS_DELEG_PERM_RECEIVE},
	{ZFS_DELEG_PERM_RENAME},
	{ZFS_DELEG_PERM_ROLLBACK},
	{ZFS_DELEG_PERM_SNAPSHOT},
	{ZFS_DELEG_PERM_SHARE},
	{ZFS_DELEG_PERM_SEND},
	{ZFS_DELEG_PERM_USERPROP},
	{ZFS_DELEG_PERM_USERQUOTA},
	{ZFS_DELEG_PERM_GROUPQUOTA},
	{ZFS_DELEG_PERM_USERUSED},
	{ZFS_DELEG_PERM_GROUPUSED},
	{ZFS_DELEG_PERM_USEROBJQUOTA},
	{ZFS_DELEG_PERM_GROUPOBJQUOTA},
	{ZFS_DELEG_PERM_USEROBJUSED},
	{ZFS_DELEG_PERM_GROUPOBJUSED},
	{ZFS_DELEG_PERM_HOLD},
	{ZFS_DELEG_PERM_RELEASE},
	{NULL}
};

static int
zfs_valid_permission_name(const char *perm)
{
	if (zfs_deleg_canonicalize_perm(perm))
		return (0);

	return (permset_namecheck(perm, NULL, NULL));
}

const char *
zfs_deleg_canonicalize_perm(const char *perm)
{
	int i;
	zfs_prop_t prop;

	for (i = 0; zfs_deleg_perm_tab[i].z_perm != NULL; i++) {
		if (strcmp(perm, zfs_deleg_perm_tab[i].z_perm) == 0)
			return (perm);
	}

	prop = zfs_name_to_prop(perm);
	if (prop != ZPROP_INVAL && zfs_prop_delegatable(prop))
		return (zfs_prop_to_name(prop));
	return (NULL);

}

static int
zfs_validate_who(char *who)
{
	char *p;

	if (who[2] != ZFS_DELEG_FIELD_SEP_CHR)
		return (-1);

	switch (who[0]) {
	case ZFS_DELEG_USER:
	case ZFS_DELEG_GROUP:
	case ZFS_DELEG_USER_SETS:
	case ZFS_DELEG_GROUP_SETS:
		if (who[1] != ZFS_DELEG_LOCAL && who[1] != ZFS_DELEG_DESCENDENT)
			return (-1);
		for (p = &who[3]; *p; p++)
			if (!isdigit(*p))
				return (-1);
		break;

	case ZFS_DELEG_NAMED_SET:
	case ZFS_DELEG_NAMED_SET_SETS:
		if (who[1] != ZFS_DELEG_NA)
			return (-1);
		return (permset_namecheck(&who[3], NULL, NULL));

	case ZFS_DELEG_CREATE:
	case ZFS_DELEG_CREATE_SETS:
		if (who[1] != ZFS_DELEG_NA)
			return (-1);
		if (who[3] != '\0')
			return (-1);
		break;

	case ZFS_DELEG_EVERYONE:
	case ZFS_DELEG_EVERYONE_SETS:
		if (who[1] != ZFS_DELEG_LOCAL && who[1] != ZFS_DELEG_DESCENDENT)
			return (-1);
		if (who[3] != '\0')
			return (-1);
		break;

	default:
		return (-1);
	}

	return (0);
}

int
zfs_deleg_verify_nvlist(nvlist_t *nvp)
{
	nvpair_t *who, *perm_name;
	nvlist_t *perms;
	int error;

	if (nvp == NULL)
		return (-1);

	who = nvlist_next_nvpair(nvp, NULL);
	if (who == NULL)
		return (-1);

	do {
		if (zfs_validate_who(nvpair_name(who)))
			return (-1);

		error = nvlist_lookup_nvlist(nvp, nvpair_name(who), &perms);

		if (error && error != ENOENT)
			return (-1);
		if (error == ENOENT)
			continue;

		perm_name = nvlist_next_nvpair(perms, NULL);
		if (perm_name == NULL) {
			return (-1);
		}
		do {
			error = zfs_valid_permission_name(
			    nvpair_name(perm_name));
			if (error)
				return (-1);
		} while ((perm_name = nvlist_next_nvpair(perms, perm_name))
		    != NULL);
	} while ((who = nvlist_next_nvpair(nvp, who)) != NULL);
	return (0);
}

/*
 * Construct the base attribute name.  The base attribute names
 * are the "key" to locate the jump objects which contain the actual
 * permissions.  The base attribute names are encoded based on
 * type of entry and whether it is a local or descendent permission.
 *
 * Arguments:
 * attr - attribute name return string, attribute is assumed to be
 *        ZFS_MAX_DELEG_NAME long.
 * type - type of entry to construct
 * inheritchr - inheritance type (local,descendent, or NA for create and
 *                               permission set definitions
 * data - is either a permission set name or a 64 bit uid/gid.
 */
void
zfs_deleg_whokey(char *attr, zfs_deleg_who_type_t type,
    char inheritchr, void *data)
{
	int len = ZFS_MAX_DELEG_NAME;
	uint64_t *id = data;

	switch (type) {
	case ZFS_DELEG_USER:
	case ZFS_DELEG_GROUP:
	case ZFS_DELEG_USER_SETS:
	case ZFS_DELEG_GROUP_SETS:
		(void) snprintf(attr, len, "%c%c%c%lld", type, inheritchr,
		    ZFS_DELEG_FIELD_SEP_CHR, (longlong_t)*id);
		break;
	case ZFS_DELEG_NAMED_SET_SETS:
	case ZFS_DELEG_NAMED_SET:
		(void) snprintf(attr, len, "%c-%c%s", type,
		    ZFS_DELEG_FIELD_SEP_CHR, (char *)data);
		break;
	case ZFS_DELEG_CREATE:
	case ZFS_DELEG_CREATE_SETS:
		(void) snprintf(attr, len, "%c-%c", type,
		    ZFS_DELEG_FIELD_SEP_CHR);
		break;
	case ZFS_DELEG_EVERYONE:
	case ZFS_DELEG_EVERYONE_SETS:
		(void) snprintf(attr, len, "%c%c%c", type, inheritchr,
		    ZFS_DELEG_FIELD_SEP_CHR);
		break;
	default:
		cmn_err(CE_PANIC, "bad zfs_deleg_who_type_t %d", type);
	}
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(zfs_deleg_verify_nvlist);
EXPORT_SYMBOL(zfs_deleg_whokey);
EXPORT_SYMBOL(zfs_deleg_canonicalize_perm);
#endif

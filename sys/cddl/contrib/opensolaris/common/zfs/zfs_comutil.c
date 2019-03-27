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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 */

/*
 * This file is intended for functions that ought to be common between user
 * land (libzfs) and the kernel. When many common routines need to be shared
 * then a separate file should to be created.
 */

#if defined(_KERNEL)
#include <sys/systm.h>
#else
#include <string.h>
#endif

#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/nvpair.h>
#include "zfs_comutil.h"

/*
 * Are there allocatable vdevs?
 */
boolean_t
zfs_allocatable_devs(nvlist_t *nv)
{
	uint64_t is_log;
	uint_t c;
	nvlist_t **child;
	uint_t children;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0) {
		return (B_FALSE);
	}
	for (c = 0; c < children; c++) {
		is_log = 0;
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (!is_log)
			return (B_TRUE);
	}
	return (B_FALSE);
}

void
zpool_get_load_policy(nvlist_t *nvl, zpool_load_policy_t *zlpp)
{
	nvlist_t *policy;
	nvpair_t *elem;
	char *nm;

	/* Defaults */
	zlpp->zlp_rewind = ZPOOL_NO_REWIND;
	zlpp->zlp_maxmeta = 0;
	zlpp->zlp_maxdata = UINT64_MAX;
	zlpp->zlp_txg = UINT64_MAX;

	if (nvl == NULL)
		return;

	elem = NULL;
	while ((elem = nvlist_next_nvpair(nvl, elem)) != NULL) {
		nm = nvpair_name(elem);
		if (strcmp(nm, ZPOOL_LOAD_POLICY) == 0) {
			if (nvpair_value_nvlist(elem, &policy) == 0)
				zpool_get_load_policy(policy, zlpp);
			return;
		} else if (strcmp(nm, ZPOOL_LOAD_REWIND_POLICY) == 0) {
			if (nvpair_value_uint32(elem, &zlpp->zlp_rewind) == 0)
				if (zlpp->zlp_rewind & ~ZPOOL_REWIND_POLICIES)
					zlpp->zlp_rewind = ZPOOL_NO_REWIND;
		} else if (strcmp(nm, ZPOOL_LOAD_REQUEST_TXG) == 0) {
			(void) nvpair_value_uint64(elem, &zlpp->zlp_txg);
		} else if (strcmp(nm, ZPOOL_LOAD_META_THRESH) == 0) {
			(void) nvpair_value_uint64(elem, &zlpp->zlp_maxmeta);
		} else if (strcmp(nm, ZPOOL_LOAD_DATA_THRESH) == 0) {
			(void) nvpair_value_uint64(elem, &zlpp->zlp_maxdata);
		}
	}
	if (zlpp->zlp_rewind == 0)
		zlpp->zlp_rewind = ZPOOL_NO_REWIND;
}

typedef struct zfs_version_spa_map {
	int	version_zpl;
	int	version_spa;
} zfs_version_spa_map_t;

/*
 * Keep this table in monotonically increasing version number order.
 */
static zfs_version_spa_map_t zfs_version_table[] = {
	{ZPL_VERSION_INITIAL, SPA_VERSION_INITIAL},
	{ZPL_VERSION_DIRENT_TYPE, SPA_VERSION_INITIAL},
	{ZPL_VERSION_FUID, SPA_VERSION_FUID},
	{ZPL_VERSION_USERSPACE, SPA_VERSION_USERSPACE},
	{ZPL_VERSION_SA, SPA_VERSION_SA},
	{0, 0}
};

/*
 * Return the max zpl version for a corresponding spa version
 * -1 is returned if no mapping exists.
 */
int
zfs_zpl_version_map(int spa_version)
{
	int i;
	int version = -1;

	for (i = 0; zfs_version_table[i].version_spa; i++) {
		if (spa_version >= zfs_version_table[i].version_spa)
			version = zfs_version_table[i].version_zpl;
	}

	return (version);
}

/*
 * Return the min spa version for a corresponding spa version
 * -1 is returned if no mapping exists.
 */
int
zfs_spa_version_map(int zpl_version)
{
	int i;
	int version = -1;

	for (i = 0; zfs_version_table[i].version_zpl; i++) {
		if (zfs_version_table[i].version_zpl >= zpl_version)
			return (zfs_version_table[i].version_spa);
	}

	return (version);
}

/*
 * This is the table of legacy internal event names; it should not be modified.
 * The internal events are now stored in the history log as strings.
 */
const char *zfs_history_event_names[ZFS_NUM_LEGACY_HISTORY_EVENTS] = {
	"invalid event",
	"pool create",
	"vdev add",
	"pool remove",
	"pool destroy",
	"pool export",
	"pool import",
	"vdev attach",
	"vdev replace",
	"vdev detach",
	"vdev online",
	"vdev offline",
	"vdev upgrade",
	"pool clear",
	"pool scrub",
	"pool property set",
	"create",
	"clone",
	"destroy",
	"destroy_begin_sync",
	"inherit",
	"property set",
	"quota set",
	"permission update",
	"permission remove",
	"permission who remove",
	"promote",
	"receive",
	"rename",
	"reservation set",
	"replay_inc_sync",
	"replay_full_sync",
	"rollback",
	"snapshot",
	"filesystem version upgrade",
	"refquota set",
	"refreservation set",
	"pool scrub done",
	"user hold",
	"user release",
	"pool split",
};

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
 * Copyright (c) 2012 by Delphix. All rights reserved.
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
#include <sys/int_limits.h>
#include <sys/nvpair.h>
#include "zfs_comutil.h"
#include <sys/zfs_ratelimit.h>

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
zpool_get_rewind_policy(nvlist_t *nvl, zpool_rewind_policy_t *zrpp)
{
	nvlist_t *policy;
	nvpair_t *elem;
	char *nm;

	/* Defaults */
	zrpp->zrp_request = ZPOOL_NO_REWIND;
	zrpp->zrp_maxmeta = 0;
	zrpp->zrp_maxdata = UINT64_MAX;
	zrpp->zrp_txg = UINT64_MAX;

	if (nvl == NULL)
		return;

	elem = NULL;
	while ((elem = nvlist_next_nvpair(nvl, elem)) != NULL) {
		nm = nvpair_name(elem);
		if (strcmp(nm, ZPOOL_REWIND_POLICY) == 0) {
			if (nvpair_value_nvlist(elem, &policy) == 0)
				zpool_get_rewind_policy(policy, zrpp);
			return;
		} else if (strcmp(nm, ZPOOL_REWIND_REQUEST) == 0) {
			if (nvpair_value_uint32(elem, &zrpp->zrp_request) == 0)
				if (zrpp->zrp_request & ~ZPOOL_REWIND_POLICIES)
					zrpp->zrp_request = ZPOOL_NO_REWIND;
		} else if (strcmp(nm, ZPOOL_REWIND_REQUEST_TXG) == 0) {
			(void) nvpair_value_uint64(elem, &zrpp->zrp_txg);
		} else if (strcmp(nm, ZPOOL_REWIND_META_THRESH) == 0) {
			(void) nvpair_value_uint64(elem, &zrpp->zrp_maxmeta);
		} else if (strcmp(nm, ZPOOL_REWIND_DATA_THRESH) == 0) {
			(void) nvpair_value_uint64(elem, &zrpp->zrp_maxdata);
		}
	}
	if (zrpp->zrp_request == 0)
		zrpp->zrp_request = ZPOOL_NO_REWIND;
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

/*
 * Initialize rate limit struct
 *
 * rl:		zfs_ratelimit_t struct
 * burst:	Number to allow in an interval before rate limiting
 * interval:	Interval time in seconds
 */
void
zfs_ratelimit_init(zfs_ratelimit_t *rl, unsigned int burst,
    unsigned int interval)
{
	rl->count = 0;
	rl->start = 0;
	rl->interval = interval;
	rl->burst = burst;
	mutex_init(&rl->lock, NULL, MUTEX_DEFAULT, NULL);
}

/*
 * Finalize rate limit struct
 *
 * rl:		zfs_ratelimit_t struct
 */
void
zfs_ratelimit_fini(zfs_ratelimit_t *rl)
{
	mutex_destroy(&rl->lock);
}

/*
 * Re-implementation of the kernel's __ratelimit() function
 *
 * We had to write our own rate limiter because the kernel's __ratelimit()
 * function annoyingly prints out how many times it rate limited to the kernel
 * logs (and there's no way to turn it off):
 *
 * 	__ratelimit: 59 callbacks suppressed
 *
 * If the kernel ever allows us to disable these prints, we should go back to
 * using __ratelimit() instead.
 *
 * Return values are the same as __ratelimit():
 *
 * 0: If we're rate limiting
 * 1: If we're not rate limiting.
 */
int
zfs_ratelimit(zfs_ratelimit_t *rl)
{
	hrtime_t now;
	hrtime_t elapsed;
	int rc = 1;

	mutex_enter(&rl->lock);

	now = gethrtime();
	elapsed = now - rl->start;

	rl->count++;
	if (NSEC2SEC(elapsed) >= rl->interval) {
		rl->start = now;
		rl->count = 0;
	} else {
		if (rl->count >= rl->burst) {
			rc = 0;	/* We're ratelimiting */
		}
	}
	mutex_exit(&rl->lock);

	return (rc);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(zfs_allocatable_devs);
EXPORT_SYMBOL(zpool_get_rewind_policy);
EXPORT_SYMBOL(zfs_zpl_version_map);
EXPORT_SYMBOL(zfs_spa_version_map);
EXPORT_SYMBOL(zfs_history_event_names);
EXPORT_SYMBOL(zfs_ratelimit_init);
EXPORT_SYMBOL(zfs_ratelimit_fini);
EXPORT_SYMBOL(zfs_ratelimit);
#endif

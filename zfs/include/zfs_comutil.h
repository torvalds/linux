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

#ifndef	_ZFS_COMUTIL_H
#define	_ZFS_COMUTIL_H

#include <sys/fs/zfs.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern boolean_t zfs_allocatable_devs(nvlist_t *);
extern void zpool_get_rewind_policy(nvlist_t *, zpool_rewind_policy_t *);

extern int zfs_zpl_version_map(int spa_version);
extern int zfs_spa_version_map(int zpl_version);
#define	ZFS_NUM_LEGACY_HISTORY_EVENTS 41
extern const char *zfs_history_event_names[ZFS_NUM_LEGACY_HISTORY_EVENTS];

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_COMUTIL_H */

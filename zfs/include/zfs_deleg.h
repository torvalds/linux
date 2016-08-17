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
 */

#ifndef	_ZFS_DELEG_H
#define	_ZFS_DELEG_H

#include <sys/fs/zfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZFS_DELEG_SET_NAME_CHR		'@'		/* set name lead char */
#define	ZFS_DELEG_FIELD_SEP_CHR		'$'		/* field separator */

/*
 * Max name length for a delegation attribute
 */
#define	ZFS_MAX_DELEG_NAME	128

#define	ZFS_DELEG_LOCAL		'l'
#define	ZFS_DELEG_DESCENDENT	'd'
#define	ZFS_DELEG_NA		'-'

typedef enum {
	ZFS_DELEG_NOTE_CREATE,
	ZFS_DELEG_NOTE_DESTROY,
	ZFS_DELEG_NOTE_SNAPSHOT,
	ZFS_DELEG_NOTE_ROLLBACK,
	ZFS_DELEG_NOTE_CLONE,
	ZFS_DELEG_NOTE_PROMOTE,
	ZFS_DELEG_NOTE_RENAME,
	ZFS_DELEG_NOTE_SEND,
	ZFS_DELEG_NOTE_RECEIVE,
	ZFS_DELEG_NOTE_ALLOW,
	ZFS_DELEG_NOTE_USERPROP,
	ZFS_DELEG_NOTE_MOUNT,
	ZFS_DELEG_NOTE_SHARE,
	ZFS_DELEG_NOTE_USERQUOTA,
	ZFS_DELEG_NOTE_GROUPQUOTA,
	ZFS_DELEG_NOTE_USERUSED,
	ZFS_DELEG_NOTE_GROUPUSED,
	ZFS_DELEG_NOTE_HOLD,
	ZFS_DELEG_NOTE_RELEASE,
	ZFS_DELEG_NOTE_DIFF,
	ZFS_DELEG_NOTE_BOOKMARK,
	ZFS_DELEG_NOTE_NONE
} zfs_deleg_note_t;

typedef struct zfs_deleg_perm_tab {
	char *z_perm;
	zfs_deleg_note_t z_note;
} zfs_deleg_perm_tab_t;

extern zfs_deleg_perm_tab_t zfs_deleg_perm_tab[];

int zfs_deleg_verify_nvlist(nvlist_t *nvlist);
void zfs_deleg_whokey(char *attr, zfs_deleg_who_type_t type,
    char checkflag, void *data);
const char *zfs_deleg_canonicalize_perm(const char *perm);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_DELEG_H */

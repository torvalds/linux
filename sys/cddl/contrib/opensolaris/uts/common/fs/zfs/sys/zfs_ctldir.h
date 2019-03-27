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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_ZFS_CTLDIR_H
#define	_ZFS_CTLDIR_H

#include <sys/vnode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZFS_CTLDIR_NAME		".zfs"

#define	zfs_has_ctldir(zdp)	\
	((zdp)->z_id == (zdp)->z_zfsvfs->z_root && \
	((zdp)->z_zfsvfs->z_ctldir != NULL))
#define	zfs_show_ctldir(zdp)	\
	(zfs_has_ctldir(zdp) && \
	((zdp)->z_zfsvfs->z_show_ctldir))

void zfsctl_create(zfsvfs_t *);
void zfsctl_destroy(zfsvfs_t *);
int zfsctl_root(zfsvfs_t *, int, vnode_t **);
void zfsctl_init(void);
void zfsctl_fini(void);
boolean_t zfsctl_is_node(vnode_t *);

int zfsctl_rename_snapshot(const char *from, const char *to);
int zfsctl_destroy_snapshot(const char *snapname, int force);
int zfsctl_umount_snapshots(vfs_t *, int, cred_t *);

int zfsctl_lookup_objset(vfs_t *vfsp, uint64_t objsetid, zfsvfs_t **zfsvfsp);

#define	ZFSCTL_INO_ROOT		0x1
#define	ZFSCTL_INO_SNAPDIR	0x2

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_CTLDIR_H */

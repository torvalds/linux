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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_ZFS_ONEXIT_H
#define	_SYS_ZFS_ONEXIT_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

typedef struct zfs_onexit {
	kmutex_t	zo_lock;
	list_t		zo_actions;
} zfs_onexit_t;

typedef struct zfs_onexit_action_node {
	list_node_t	za_link;
	void		(*za_func)(void *);
	void		*za_data;
} zfs_onexit_action_node_t;

extern void zfs_onexit_init(zfs_onexit_t **zo);
extern void zfs_onexit_destroy(zfs_onexit_t *zo);

#endif

extern int zfs_onexit_fd_hold(int fd, minor_t *minorp);
extern void zfs_onexit_fd_rele(int fd);
extern int zfs_onexit_add_cb(minor_t minor, void (*func)(void *), void *data,
    uint64_t *action_handle);
extern int zfs_onexit_del_cb(minor_t minor, uint64_t action_handle,
    boolean_t fire);
extern int zfs_onexit_cb_data(minor_t minor, uint64_t action_handle,
    void **data);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_ONEXIT_H */

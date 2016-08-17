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
 * Copyright (c) 2015 by Delphix. All rights reserved.
 */

#ifndef	_SYS_ZRLOCK_H
#define	_SYS_ZRLOCK_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zrlock {
	kmutex_t zr_mtx;
	volatile int32_t zr_refcount;
	kcondvar_t zr_cv;
	uint16_t zr_pad;
#ifdef	ZFS_DEBUG
	kthread_t *zr_owner;
	const char *zr_caller;
#endif
} zrlock_t;

extern void zrl_init(zrlock_t *);
extern void zrl_destroy(zrlock_t *);
#define	zrl_add(_z)	zrl_add_impl((_z), __func__)
extern void zrl_add_impl(zrlock_t *, const char *);
extern void zrl_remove(zrlock_t *);
extern int zrl_tryenter(zrlock_t *);
extern void zrl_exit(zrlock_t *);
extern int zrl_is_zero(zrlock_t *);
extern int zrl_is_locked(zrlock_t *);
#ifdef	ZFS_DEBUG
extern kthread_t *zrl_owner(zrlock_t *);
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZRLOCK_H */

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
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */

#ifndef _ZFS_KMAP_H
#define	_ZFS_KMAP_H

#include <linux/highmem.h>

#ifdef HAVE_1ARG_KMAP_ATOMIC
/* 2.6.37 API change */
#define	zfs_kmap_atomic(page, km_type)		kmap_atomic(page)
#define	zfs_kunmap_atomic(addr, km_type)	kunmap_atomic(addr)
#else
#define	zfs_kmap_atomic(page, km_type)		kmap_atomic(page, km_type)
#define	zfs_kunmap_atomic(addr, km_type)	kunmap_atomic(addr, km_type)
#endif

#endif	/* _ZFS_KMAP_H */

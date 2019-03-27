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
 * Copyright (c) 2012 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
 */

#ifndef _SYS_TRIM_MAP_H
#define	_SYS_TRIM_MAP_H

#include <sys/avl.h>
#include <sys/list.h>
#include <sys/spa.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern void trim_map_create(vdev_t *vd);
extern void trim_map_destroy(vdev_t *vd);
extern void trim_map_free(vdev_t *vd, uint64_t offset, uint64_t size, uint64_t txg);
extern boolean_t trim_map_write_start(zio_t *zio);
extern void trim_map_write_done(zio_t *zio);

extern void trim_thread_create(spa_t *spa);
extern void trim_thread_destroy(spa_t *spa);
extern void trim_thread_wakeup(spa_t *spa);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TRIM_MAP_H */

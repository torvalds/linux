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
 * Copyright (C) 2016 Gvozden Neskovic <neskovic@compeng.uni-frankfurt.de>.
 */

#ifndef _SYS_VDEV_RAIDZ_H
#define	_SYS_VDEV_RAIDZ_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct zio;
struct raidz_map;
#if !defined(_KERNEL)
struct kernel_param {};
#endif

/*
 * vdev_raidz interface
 */
struct raidz_map *vdev_raidz_map_alloc(struct zio *, uint64_t, uint64_t,
    uint64_t);
void vdev_raidz_map_free(struct raidz_map *);
void vdev_raidz_generate_parity(struct raidz_map *);
int vdev_raidz_reconstruct(struct raidz_map *, const int *, int);

/*
 * vdev_raidz_math interface
 */
void vdev_raidz_math_init(void);
void vdev_raidz_math_fini(void);
struct raidz_impl_ops *vdev_raidz_math_get_ops(void);
int vdev_raidz_math_generate(struct raidz_map *);
int vdev_raidz_math_reconstruct(struct raidz_map *, const int *, const int *,
    const int);
int vdev_raidz_impl_set(const char *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_RAIDZ_H */

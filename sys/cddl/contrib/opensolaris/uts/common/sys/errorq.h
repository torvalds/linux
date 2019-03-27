/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_ERRORQ_H
#define	_ERRORQ_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/nvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct errorq errorq_t;
typedef struct errorq_elem errorq_elem_t;
typedef void (*errorq_func_t)(void *, const void *, const errorq_elem_t *);

/*
 * Public flags for errorq_create(): bit range 0-15
 */
#define	ERRORQ_VITAL	0x0001	/* drain queue automatically on system reset */

/*
 * Public flags for errorq_dispatch():
 */
#define	ERRORQ_ASYNC	0	/* schedule async queue drain for caller */
#define	ERRORQ_SYNC	1	/* do not schedule drain; caller will drain */

#ifdef	_KERNEL

extern errorq_t *errorq_create(const char *, errorq_func_t, void *,
    ulong_t, size_t, uint_t, uint_t);

extern errorq_t *errorq_nvcreate(const char *, errorq_func_t, void *,
    ulong_t, size_t, uint_t, uint_t);

extern void errorq_destroy(errorq_t *);
extern void errorq_dispatch(errorq_t *, const void *, size_t, uint_t);
extern void errorq_drain(errorq_t *);
extern void errorq_init(void);
extern void errorq_panic(void);
extern errorq_elem_t *errorq_reserve(errorq_t *);
extern void errorq_commit(errorq_t *, errorq_elem_t *, uint_t);
extern void errorq_cancel(errorq_t *, errorq_elem_t *);
extern nvlist_t *errorq_elem_nvl(errorq_t *, const errorq_elem_t *);
extern nv_alloc_t *errorq_elem_nva(errorq_t *, const errorq_elem_t *);
extern void *errorq_elem_dup(errorq_t *, const errorq_elem_t *,
    errorq_elem_t **);
extern void errorq_dump();

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _ERRORQ_H */

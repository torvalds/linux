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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2014 by Delphix. All rights reserved.
 */

#ifndef _SYS_UBERBLOCK_H
#define	_SYS_UBERBLOCK_H

#include <sys/spa.h>
#include <sys/vdev.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct uberblock uberblock_t;

extern int uberblock_verify(uberblock_t *);
extern boolean_t uberblock_update(uberblock_t *, vdev_t *, uint64_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UBERBLOCK_H */

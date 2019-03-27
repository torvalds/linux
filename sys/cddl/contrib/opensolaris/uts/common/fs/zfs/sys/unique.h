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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_UNIQUE_H
#define	_SYS_UNIQUE_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* The number of significant bits in each unique value. */
#define	UNIQUE_BITS	56

void unique_init(void);
void unique_fini(void);

/*
 * Return a new unique value (which will not be uniquified against until
 * it is unique_insert()-ed).
 */
uint64_t unique_create(void);

/* Return a unique value, which equals the one passed in if possible. */
uint64_t unique_insert(uint64_t value);

/* Indicate that this value no longer needs to be uniquified against. */
void unique_remove(uint64_t value);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_UNIQUE_H */

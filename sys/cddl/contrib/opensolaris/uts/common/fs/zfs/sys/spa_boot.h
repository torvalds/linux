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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#ifndef _SYS_SPA_BOOT_H
#define	_SYS_SPA_BOOT_H

#include <sys/nvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern char *spa_get_bootprop(char *prop);
extern void spa_free_bootprop(char *prop);

extern void spa_arch_init(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPA_BOOT_H */

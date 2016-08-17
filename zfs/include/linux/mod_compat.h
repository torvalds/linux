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
 * Copyright (C) 2016 Gvozden Neskovic <neskovic@gmail.com>.
 */

#ifndef _MOD_COMPAT_H
#define	_MOD_COMPAT_H

#include <linux/module.h>
#include <linux/moduleparam.h>

/* Grsecurity kernel API change */
#ifdef MODULE_PARAM_CALL_CONST
typedef const struct kernel_param zfs_kernel_param_t;
#else
typedef struct kernel_param zfs_kernel_param_t;
#endif

#endif	/* _MOD_COMPAT_H */

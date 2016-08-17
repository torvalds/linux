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
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 */

#ifndef	_SYS_ZVOL_H
#define	_SYS_ZVOL_H

#include <sys/zfs_context.h>

#define	ZVOL_OBJ		1ULL
#define	ZVOL_ZAP_OBJ		2ULL

extern void zvol_create_minors(spa_t *spa, const char *name, boolean_t async);
extern void zvol_remove_minors(spa_t *spa, const char *name, boolean_t async);
extern void zvol_rename_minors(spa_t *spa, const char *oldname,
    const char *newname, boolean_t async);

#ifdef _KERNEL
extern int zvol_check_volsize(uint64_t volsize, uint64_t blocksize);
extern int zvol_check_volblocksize(const char *name, uint64_t volblocksize);
extern int zvol_get_stats(objset_t *os, nvlist_t *nv);
extern boolean_t zvol_is_zvol(const char *);
extern void zvol_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
extern int zvol_set_volsize(const char *, uint64_t);
extern int zvol_set_volblocksize(const char *, uint64_t);
extern int zvol_set_snapdev(const char *, zprop_source_t, uint64_t);

extern int zvol_init(void);
extern void zvol_fini(void);
#endif /* _KERNEL */
#endif /* _SYS_ZVOL_H */

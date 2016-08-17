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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef	_SYS_DSL_DESTROY_H
#define	_SYS_DSL_DESTROY_H

#ifdef	__cplusplus
extern "C" {
#endif

struct nvlist;
struct dsl_dataset;
struct dmu_tx;

int dsl_destroy_snapshots_nvl(struct nvlist *, boolean_t,
    struct nvlist *);
int dsl_destroy_snapshot(const char *, boolean_t);
int dsl_destroy_head(const char *);
int dsl_destroy_head_check_impl(struct dsl_dataset *, int);
void dsl_destroy_head_sync_impl(struct dsl_dataset *, struct dmu_tx *);
int dsl_destroy_inconsistent(const char *, void *);
int dsl_destroy_snapshot_check_impl(struct dsl_dataset *, boolean_t);
void dsl_destroy_snapshot_sync_impl(struct dsl_dataset *,
    boolean_t, struct dmu_tx *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_DESTROY_H */

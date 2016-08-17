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
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 */

#ifndef _DMU_SEND_H
#define	_DMU_SEND_H

#include <sys/inttypes.h>
#include <sys/spa.h>

struct vnode;
struct dsl_dataset;
struct drr_begin;
struct avl_tree;

int dmu_send(const char *tosnap, const char *fromsnap,
    boolean_t embedok, boolean_t large_block_ok,
    int outfd, struct vnode *vp, offset_t *off);
int dmu_send_estimate(struct dsl_dataset *ds, struct dsl_dataset *fromds,
    uint64_t *sizep);
int dmu_send_estimate_from_txg(struct dsl_dataset *ds, uint64_t fromtxg,
    uint64_t *sizep);
int dmu_send_obj(const char *pool, uint64_t tosnap, uint64_t fromsnap,
    boolean_t embedok, boolean_t large_block_ok,
    int outfd, struct vnode *vp, offset_t *off);

typedef struct dmu_recv_cookie {
	struct dsl_dataset *drc_ds;
	struct drr_begin *drc_drrb;
	const char *drc_tofs;
	const char *drc_tosnap;
	boolean_t drc_newfs;
	boolean_t drc_byteswap;
	boolean_t drc_force;
	struct avl_tree *drc_guid_to_ds_map;
	zio_cksum_t drc_cksum;
	uint64_t drc_newsnapobj;
	void *drc_owner;
	cred_t *drc_cred;
} dmu_recv_cookie_t;

int dmu_recv_begin(char *tofs, char *tosnap, struct drr_begin *drrb,
    boolean_t force, char *origin, dmu_recv_cookie_t *drc);
int dmu_recv_stream(dmu_recv_cookie_t *drc, struct vnode *vp, offset_t *voffp,
    int cleanup_fd, uint64_t *action_handlep);
int dmu_recv_end(dmu_recv_cookie_t *drc, void *owner);
boolean_t dmu_objset_is_receiving(objset_t *os);

#endif /* _DMU_SEND_H */

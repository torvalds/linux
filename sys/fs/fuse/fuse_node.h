/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007-2009 Google Inc. and Amit Singh
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Google Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Copyright (C) 2005 Csaba Henk.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _FUSE_NODE_H_
#define _FUSE_NODE_H_

#include <sys/types.h>
#include <sys/mutex.h>

#include "fuse_file.h"

#define FN_REVOKED           0x00000020
#define FN_FLUSHINPROG       0x00000040
#define FN_FLUSHWANT         0x00000080
#define FN_SIZECHANGE        0x00000100
#define FN_DIRECTIO          0x00000200

struct fuse_vnode_data {
	/** self **/
	uint64_t	nid;

	/** parent **/
	/* XXXIP very likely to be stale, it's not updated in rename() */
	uint64_t	parent_nid;

	/** I/O **/
	struct		fuse_filehandle fufh[FUFH_MAXTYPE];

	/** flags **/
	uint32_t	flag;

	/** meta **/
	bool		valid_attr_cache;
	struct vattr	cached_attrs;
	off_t		filesize;
	uint64_t	nlookup;
	enum vtype	vtype;
};

#define VTOFUD(vp) \
	((struct fuse_vnode_data *)((vp)->v_data))
#define VTOI(vp)    (VTOFUD(vp)->nid)
#define VTOVA(vp) \
	(VTOFUD(vp)->valid_attr_cache ? \
	&(VTOFUD(vp)->cached_attrs) : NULL)
#define VTOILLU(vp) ((uint64_t)(VTOFUD(vp) ? VTOI(vp) : 0))

#define FUSE_NULL_ID 0

extern struct vop_vector fuse_vnops;

static inline void
fuse_vnode_setparent(struct vnode *vp, struct vnode *dvp)
{
	if (dvp != NULL && vp->v_type == VDIR) {
		MPASS(dvp->v_type == VDIR);
		VTOFUD(vp)->parent_nid = VTOI(dvp);
	}
}

void fuse_vnode_destroy(struct vnode *vp);

int fuse_vnode_get(struct mount *mp, struct fuse_entry_out *feo,
    uint64_t nodeid, struct vnode *dvp, struct vnode **vpp,
    struct componentname *cnp, enum vtype vtyp);

void fuse_vnode_open(struct vnode *vp, int32_t fuse_open_flags,
    struct thread *td);

void fuse_vnode_refreshsize(struct vnode *vp, struct ucred *cred);

int fuse_vnode_savesize(struct vnode *vp, struct ucred *cred);

int fuse_vnode_setsize(struct vnode *vp, struct ucred *cred, off_t newsize);

#endif /* _FUSE_NODE_H_ */

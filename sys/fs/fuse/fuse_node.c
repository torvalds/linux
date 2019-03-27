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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/fcntl.h>
#include <sys/fnv_hash.h>
#include <sys/priv.h>
#include <security/mac/mac_framework.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>

#include "fuse.h"
#include "fuse_node.h"
#include "fuse_internal.h"
#include "fuse_io.h"
#include "fuse_ipc.h"

#define FUSE_DEBUG_MODULE VNOPS
#include "fuse_debug.h"

MALLOC_DEFINE(M_FUSEVN, "fuse_vnode", "fuse vnode private data");

static int sysctl_fuse_cache_mode(SYSCTL_HANDLER_ARGS);

static int fuse_node_count = 0;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, node_count, CTLFLAG_RD,
    &fuse_node_count, 0, "Count of FUSE vnodes");

int	fuse_data_cache_mode = FUSE_CACHE_WT;

SYSCTL_PROC(_vfs_fusefs, OID_AUTO, data_cache_mode, CTLTYPE_INT|CTLFLAG_RW,
    &fuse_data_cache_mode, 0, sysctl_fuse_cache_mode, "I",
    "Zero: disable caching of FUSE file data; One: write-through caching "
    "(default); Two: write-back caching (generally unsafe)");

int	fuse_data_cache_invalidate = 0;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, data_cache_invalidate, CTLFLAG_RW,
    &fuse_data_cache_invalidate, 0,
    "If non-zero, discard cached clean file data when there are no active file"
    " users");

int	fuse_mmap_enable = 1;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, mmap_enable, CTLFLAG_RW,
    &fuse_mmap_enable, 0,
    "If non-zero, and data_cache_mode is also non-zero, enable mmap(2) of "
    "FUSE files");

int	fuse_refresh_size = 0;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, refresh_size, CTLFLAG_RW,
    &fuse_refresh_size, 0,
    "If non-zero, and no dirty file extension data is buffered, fetch file "
    "size before write operations");

int	fuse_sync_resize = 1;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, sync_resize, CTLFLAG_RW,
    &fuse_sync_resize, 0,
    "If a cached write extended a file, inform FUSE filesystem of the changed"
    "size immediately subsequent to the issued writes");

int	fuse_fix_broken_io = 0;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, fix_broken_io, CTLFLAG_RW,
    &fuse_fix_broken_io, 0,
    "If non-zero, print a diagnostic warning if a userspace filesystem returns"
    " EIO on reads of recently extended portions of files");

static int
sysctl_fuse_cache_mode(SYSCTL_HANDLER_ARGS)
{
	int val, error;

	val = *(int *)arg1;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
		return (error);

	switch (val) {
	case FUSE_CACHE_UC:
	case FUSE_CACHE_WT:
	case FUSE_CACHE_WB:
		*(int *)arg1 = val;
		break;
	default:
		return (EDOM);
	}
	return (0);
}

static void
fuse_vnode_init(struct vnode *vp, struct fuse_vnode_data *fvdat,
    uint64_t nodeid, enum vtype vtyp)
{
	int i;

	fvdat->nid = nodeid;
	vattr_null(&fvdat->cached_attrs);
	if (nodeid == FUSE_ROOT_ID) {
		vp->v_vflag |= VV_ROOT;
	}
	vp->v_type = vtyp;
	vp->v_data = fvdat;

	for (i = 0; i < FUFH_MAXTYPE; i++)
		fvdat->fufh[i].fh_type = FUFH_INVALID;

	atomic_add_acq_int(&fuse_node_count, 1);
}

void
fuse_vnode_destroy(struct vnode *vp)
{
	struct fuse_vnode_data *fvdat = vp->v_data;

	vp->v_data = NULL;
	free(fvdat, M_FUSEVN);

	atomic_subtract_acq_int(&fuse_node_count, 1);
}

static int
fuse_vnode_cmp(struct vnode *vp, void *nidp)
{
	return (VTOI(vp) != *((uint64_t *)nidp));
}

static uint32_t inline
fuse_vnode_hash(uint64_t id)
{
	return (fnv_32_buf(&id, sizeof(id), FNV1_32_INIT));
}

static int
fuse_vnode_alloc(struct mount *mp,
    struct thread *td,
    uint64_t nodeid,
    enum vtype vtyp,
    struct vnode **vpp)
{
	struct fuse_vnode_data *fvdat;
	struct vnode *vp2;
	int err = 0;

	FS_DEBUG("been asked for vno #%ju\n", (uintmax_t)nodeid);

	if (vtyp == VNON) {
		return EINVAL;
	}
	*vpp = NULL;
	err = vfs_hash_get(mp, fuse_vnode_hash(nodeid), LK_EXCLUSIVE, td, vpp,
	    fuse_vnode_cmp, &nodeid);
	if (err)
		return (err);

	if (*vpp) {
		MPASS((*vpp)->v_type == vtyp && (*vpp)->v_data != NULL);
		FS_DEBUG("vnode taken from hash\n");
		return (0);
	}
	fvdat = malloc(sizeof(*fvdat), M_FUSEVN, M_WAITOK | M_ZERO);
	err = getnewvnode("fuse", mp, &fuse_vnops, vpp);
	if (err) {
		free(fvdat, M_FUSEVN);
		return (err);
	}
	lockmgr((*vpp)->v_vnlock, LK_EXCLUSIVE, NULL);
	fuse_vnode_init(*vpp, fvdat, nodeid, vtyp);
	err = insmntque(*vpp, mp);
	ASSERT_VOP_ELOCKED(*vpp, "fuse_vnode_alloc");
	if (err) {
		free(fvdat, M_FUSEVN);
		*vpp = NULL;
		return (err);
	}
	err = vfs_hash_insert(*vpp, fuse_vnode_hash(nodeid), LK_EXCLUSIVE,
	    td, &vp2, fuse_vnode_cmp, &nodeid);
	if (err)
		return (err);
	if (vp2 != NULL) {
		*vpp = vp2;
		return (0);
	}

	ASSERT_VOP_ELOCKED(*vpp, "fuse_vnode_alloc");

	return (0);
}

int
fuse_vnode_get(struct mount *mp,
    struct fuse_entry_out *feo,
    uint64_t nodeid,
    struct vnode *dvp,
    struct vnode **vpp,
    struct componentname *cnp,
    enum vtype vtyp)
{
	struct thread *td = (cnp != NULL ? cnp->cn_thread : curthread);
	int err = 0;

	debug_printf("dvp=%p\n", dvp);

	err = fuse_vnode_alloc(mp, td, nodeid, vtyp, vpp);
	if (err) {
		return err;
	}
	if (dvp != NULL) {
		MPASS((cnp->cn_flags & ISDOTDOT) == 0);
		MPASS(!(cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.'));
		fuse_vnode_setparent(*vpp, dvp);
	}
	if (dvp != NULL && cnp != NULL && (cnp->cn_flags & MAKEENTRY) != 0 &&
	    feo != NULL &&
	    (feo->entry_valid != 0 || feo->entry_valid_nsec != 0)) {
		ASSERT_VOP_LOCKED(*vpp, "fuse_vnode_get");
		ASSERT_VOP_LOCKED(dvp, "fuse_vnode_get");
		cache_enter(dvp, *vpp, cnp);
	}

	/*
	 * In userland, libfuse uses cached lookups for dot and dotdot entries,
	 * thus it does not really bump the nlookup counter for forget.
	 * Follow the same semantic and avoid tu bump it in order to keep
	 * nlookup counters consistent.
	 */
	if (cnp == NULL || ((cnp->cn_flags & ISDOTDOT) == 0 &&
	    (cnp->cn_namelen != 1 || cnp->cn_nameptr[0] != '.')))
		VTOFUD(*vpp)->nlookup++;

	return 0;
}

void
fuse_vnode_open(struct vnode *vp, int32_t fuse_open_flags, struct thread *td)
{
	/*
	 * Funcation is called for every vnode open.
	 * Merge fuse_open_flags it may be 0
	 */
	/*
	 * Ideally speaking, direct io should be enabled on
	 * fd's but do not see of any way of providing that
	 * this implementation.
	 *
	 * Also cannot think of a reason why would two
	 * different fd's on same vnode would like
	 * have DIRECT_IO turned on and off. But linux
	 * based implementation works on an fd not an
	 * inode and provides such a feature.
	 *
	 * XXXIP: Handle fd based DIRECT_IO
	 */
	if (fuse_open_flags & FOPEN_DIRECT_IO) {
		ASSERT_VOP_ELOCKED(vp, __func__);
		VTOFUD(vp)->flag |= FN_DIRECTIO;
		fuse_io_invalbuf(vp, td);
	} else {
		if ((fuse_open_flags & FOPEN_KEEP_CACHE) == 0)
			fuse_io_invalbuf(vp, td);
	        VTOFUD(vp)->flag &= ~FN_DIRECTIO;
	}

	if (vnode_vtype(vp) == VREG) {
		/* XXXIP prevent getattr, by using cached node size */
		vnode_create_vobject(vp, 0, td);
	}
}

int
fuse_vnode_savesize(struct vnode *vp, struct ucred *cred)
{
	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct thread *td = curthread;
	struct fuse_filehandle *fufh = NULL;
	struct fuse_dispatcher fdi;
	struct fuse_setattr_in *fsai;
	int err = 0;

	FS_DEBUG("inode=%ju size=%ju\n", (uintmax_t)VTOI(vp),
	    (uintmax_t)fvdat->filesize);
	ASSERT_VOP_ELOCKED(vp, "fuse_io_extend");

	if (fuse_isdeadfs(vp)) {
		return EBADF;
	}
	if (vnode_vtype(vp) == VDIR) {
		return EISDIR;
	}
	if (vfs_isrdonly(vnode_mount(vp))) {
		return EROFS;
	}
	if (cred == NULL) {
		cred = td->td_ucred;
	}
	fdisp_init(&fdi, sizeof(*fsai));
	fdisp_make_vp(&fdi, FUSE_SETATTR, vp, td, cred);
	fsai = fdi.indata;
	fsai->valid = 0;

	/* Truncate to a new value. */
	fsai->size = fvdat->filesize;
	fsai->valid |= FATTR_SIZE;

	fuse_filehandle_getrw(vp, FUFH_WRONLY, &fufh);
	if (fufh) {
		fsai->fh = fufh->fh_id;
		fsai->valid |= FATTR_FH;
	}
	err = fdisp_wait_answ(&fdi);
	fdisp_destroy(&fdi);
	if (err == 0)
		fvdat->flag &= ~FN_SIZECHANGE;

	return err;
}

void
fuse_vnode_refreshsize(struct vnode *vp, struct ucred *cred)
{

	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct vattr va;

	if ((fvdat->flag & FN_SIZECHANGE) != 0 ||
	    fuse_data_cache_mode == FUSE_CACHE_UC ||
	    (fuse_refresh_size == 0 && fvdat->filesize != 0))
		return;

	VOP_GETATTR(vp, &va, cred);
	FS_DEBUG("refreshed file size: %jd\n", (intmax_t)VTOFUD(vp)->filesize);
}

int
fuse_vnode_setsize(struct vnode *vp, struct ucred *cred, off_t newsize)
{
	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	off_t oldsize;
	int err = 0;

	FS_DEBUG("inode=%ju oldsize=%ju newsize=%ju\n",
	    (uintmax_t)VTOI(vp), (uintmax_t)fvdat->filesize,
	    (uintmax_t)newsize);
	ASSERT_VOP_ELOCKED(vp, "fuse_vnode_setsize");

	oldsize = fvdat->filesize;
	fvdat->filesize = newsize;
	fvdat->flag |= FN_SIZECHANGE;

	if (newsize < oldsize) {
		err = vtruncbuf(vp, cred, newsize, fuse_iosize(vp));
	}
	vnode_pager_setsize(vp, newsize);
	return err;
}

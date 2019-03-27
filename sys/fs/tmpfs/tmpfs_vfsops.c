/*	$NetBSD: tmpfs_vfsops.c,v 1.10 2005/12/11 12:24:29 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Efficient memory file system.
 *
 * tmpfs is a file system that uses FreeBSD's virtual memory
 * sub-system to store file data and metadata in an efficient way.
 * This means that it does not follow the structure of an on-disk file
 * system because it simply does not need to.  Instead, it uses
 * memory-specific data structures and algorithms to automatically
 * allocate and release resources.
 */

#include "opt_tmpfs.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/dirent.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/rwlock.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_param.h>

#include <fs/tmpfs/tmpfs.h>

/*
 * Default permission for root node
 */
#define TMPFS_DEFAULT_ROOT_MODE	(S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)

MALLOC_DEFINE(M_TMPFSMNT, "tmpfs mount", "tmpfs mount structures");
MALLOC_DEFINE(M_TMPFSNAME, "tmpfs name", "tmpfs file names");

static int	tmpfs_mount(struct mount *);
static int	tmpfs_unmount(struct mount *, int);
static int	tmpfs_root(struct mount *, int flags, struct vnode **);
static int	tmpfs_fhtovp(struct mount *, struct fid *, int,
		    struct vnode **);
static int	tmpfs_statfs(struct mount *, struct statfs *);
static void	tmpfs_susp_clean(struct mount *);

static const char *tmpfs_opts[] = {
	"from", "size", "maxfilesize", "inodes", "uid", "gid", "mode", "export",
	"union", "nonc", NULL
};

static const char *tmpfs_updateopts[] = {
	"from", "export", "size", NULL
};

static int
tmpfs_node_ctor(void *mem, int size, void *arg, int flags)
{
	struct tmpfs_node *node = (struct tmpfs_node *)mem;

	node->tn_gen++;
	node->tn_size = 0;
	node->tn_status = 0;
	node->tn_flags = 0;
	node->tn_links = 0;
	node->tn_vnode = NULL;
	node->tn_vpstate = 0;

	return (0);
}

static void
tmpfs_node_dtor(void *mem, int size, void *arg)
{
	struct tmpfs_node *node = (struct tmpfs_node *)mem;
	node->tn_type = VNON;
}

static int
tmpfs_node_init(void *mem, int size, int flags)
{
	struct tmpfs_node *node = (struct tmpfs_node *)mem;
	node->tn_id = 0;

	mtx_init(&node->tn_interlock, "tmpfs node interlock", NULL, MTX_DEF);
	node->tn_gen = arc4random();

	return (0);
}

static void
tmpfs_node_fini(void *mem, int size)
{
	struct tmpfs_node *node = (struct tmpfs_node *)mem;

	mtx_destroy(&node->tn_interlock);
}

static int
tmpfs_mount(struct mount *mp)
{
	const size_t nodes_per_page = howmany(PAGE_SIZE,
	    sizeof(struct tmpfs_dirent) + sizeof(struct tmpfs_node));
	struct tmpfs_mount *tmp;
	struct tmpfs_node *root;
	int error, flags;
	bool nonc;
	/* Size counters. */
	u_quad_t pages;
	off_t nodes_max, size_max, maxfilesize;

	/* Root node attributes. */
	uid_t root_uid;
	gid_t root_gid;
	mode_t root_mode;

	struct vattr va;

	if (vfs_filteropt(mp->mnt_optnew, tmpfs_opts))
		return (EINVAL);

	if (mp->mnt_flag & MNT_UPDATE) {
		/* Only support update mounts for certain options. */
		if (vfs_filteropt(mp->mnt_optnew, tmpfs_updateopts) != 0)
			return (EOPNOTSUPP);
		if (vfs_getopt_size(mp->mnt_optnew, "size", &size_max) == 0) {
			/*
			 * On-the-fly resizing is not supported (yet). We still
			 * need to have "size" listed as "supported", otherwise
			 * trying to update fs that is listed in fstab with size
			 * parameter, say trying to change rw to ro or vice
			 * versa, would cause vfs_filteropt() to bail.
			 */
			if (size_max != VFS_TO_TMPFS(mp)->tm_size_max)
				return (EOPNOTSUPP);
		}
		if (vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0) &&
		    !(VFS_TO_TMPFS(mp)->tm_ronly)) {
			/* RW -> RO */
			error = VFS_SYNC(mp, MNT_WAIT);
			if (error)
				return (error);
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			error = vflush(mp, 0, flags, curthread);
			if (error)
				return (error);
			VFS_TO_TMPFS(mp)->tm_ronly = 1;
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_RDONLY;
			MNT_IUNLOCK(mp);
		} else if (!vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0) &&
		    VFS_TO_TMPFS(mp)->tm_ronly) {
			/* RO -> RW */
			VFS_TO_TMPFS(mp)->tm_ronly = 0;
			MNT_ILOCK(mp);
			mp->mnt_flag &= ~MNT_RDONLY;
			MNT_IUNLOCK(mp);
		}
		return (0);
	}

	vn_lock(mp->mnt_vnodecovered, LK_SHARED | LK_RETRY);
	error = VOP_GETATTR(mp->mnt_vnodecovered, &va, mp->mnt_cred);
	VOP_UNLOCK(mp->mnt_vnodecovered, 0);
	if (error)
		return (error);

	if (mp->mnt_cred->cr_ruid != 0 ||
	    vfs_scanopt(mp->mnt_optnew, "gid", "%d", &root_gid) != 1)
		root_gid = va.va_gid;
	if (mp->mnt_cred->cr_ruid != 0 ||
	    vfs_scanopt(mp->mnt_optnew, "uid", "%d", &root_uid) != 1)
		root_uid = va.va_uid;
	if (mp->mnt_cred->cr_ruid != 0 ||
	    vfs_scanopt(mp->mnt_optnew, "mode", "%ho", &root_mode) != 1)
		root_mode = va.va_mode;
	if (vfs_getopt_size(mp->mnt_optnew, "inodes", &nodes_max) != 0)
		nodes_max = 0;
	if (vfs_getopt_size(mp->mnt_optnew, "size", &size_max) != 0)
		size_max = 0;
	if (vfs_getopt_size(mp->mnt_optnew, "maxfilesize", &maxfilesize) != 0)
		maxfilesize = 0;
	nonc = vfs_getopt(mp->mnt_optnew, "nonc", NULL, NULL) == 0;

	/* Do not allow mounts if we do not have enough memory to preserve
	 * the minimum reserved pages. */
	if (tmpfs_mem_avail() < TMPFS_PAGES_MINRESERVED)
		return (ENOSPC);

	/* Get the maximum number of memory pages this file system is
	 * allowed to use, based on the maximum size the user passed in
	 * the mount structure.  A value of zero is treated as if the
	 * maximum available space was requested. */
	if (size_max == 0 || size_max > OFF_MAX - PAGE_SIZE ||
	    (SIZE_MAX < OFF_MAX && size_max / PAGE_SIZE >= SIZE_MAX))
		pages = SIZE_MAX;
	else {
		size_max = roundup(size_max, PAGE_SIZE);
		pages = howmany(size_max, PAGE_SIZE);
	}
	MPASS(pages > 0);

	if (nodes_max <= 3) {
		if (pages < INT_MAX / nodes_per_page)
			nodes_max = pages * nodes_per_page;
		else
			nodes_max = INT_MAX;
	}
	if (nodes_max > INT_MAX)
		nodes_max = INT_MAX;
	MPASS(nodes_max >= 3);

	/* Allocate the tmpfs mount structure and fill it. */
	tmp = (struct tmpfs_mount *)malloc(sizeof(struct tmpfs_mount),
	    M_TMPFSMNT, M_WAITOK | M_ZERO);

	mtx_init(&tmp->tm_allnode_lock, "tmpfs allnode lock", NULL, MTX_DEF);
	tmp->tm_nodes_max = nodes_max;
	tmp->tm_nodes_inuse = 0;
	tmp->tm_refcount = 1;
	tmp->tm_maxfilesize = maxfilesize > 0 ? maxfilesize : OFF_MAX;
	LIST_INIT(&tmp->tm_nodes_used);

	tmp->tm_size_max = size_max;
	tmp->tm_pages_max = pages;
	tmp->tm_pages_used = 0;
	new_unrhdr64(&tmp->tm_ino_unr, 2);
	tmp->tm_dirent_pool = uma_zcreate("TMPFS dirent",
	    sizeof(struct tmpfs_dirent), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	tmp->tm_node_pool = uma_zcreate("TMPFS node",
	    sizeof(struct tmpfs_node), tmpfs_node_ctor, tmpfs_node_dtor,
	    tmpfs_node_init, tmpfs_node_fini, UMA_ALIGN_PTR, 0);
	tmp->tm_ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	tmp->tm_nonc = nonc;

	/* Allocate the root node. */
	error = tmpfs_alloc_node(mp, tmp, VDIR, root_uid, root_gid,
	    root_mode & ALLPERMS, NULL, NULL, VNOVAL, &root);

	if (error != 0 || root == NULL) {
		uma_zdestroy(tmp->tm_node_pool);
		uma_zdestroy(tmp->tm_dirent_pool);
		free(tmp, M_TMPFSMNT);
		return (error);
	}
	KASSERT(root->tn_id == 2,
	    ("tmpfs root with invalid ino: %ju", (uintmax_t)root->tn_id));
	tmp->tm_root = root;

	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_LOOKUP_SHARED | MNTK_EXTENDED_SHARED;
	MNT_IUNLOCK(mp);

	mp->mnt_data = tmp;
	mp->mnt_stat.f_namemax = MAXNAMLEN;
	vfs_getnewfsid(mp);
	vfs_mountedfrom(mp, "tmpfs");

	return 0;
}

/* ARGSUSED2 */
static int
tmpfs_unmount(struct mount *mp, int mntflags)
{
	struct tmpfs_mount *tmp;
	struct tmpfs_node *node;
	int error, flags;

	flags = (mntflags & MNT_FORCE) != 0 ? FORCECLOSE : 0;
	tmp = VFS_TO_TMPFS(mp);

	/* Stop writers */
	error = vfs_write_suspend_umnt(mp);
	if (error != 0)
		return (error);
	/*
	 * At this point, nodes cannot be destroyed by any other
	 * thread because write suspension is started.
	 */

	for (;;) {
		error = vflush(mp, 0, flags, curthread);
		if (error != 0) {
			vfs_write_resume(mp, VR_START_WRITE);
			return (error);
		}
		MNT_ILOCK(mp);
		if (mp->mnt_nvnodelistsize == 0) {
			MNT_IUNLOCK(mp);
			break;
		}
		MNT_IUNLOCK(mp);
		if ((mntflags & MNT_FORCE) == 0) {
			vfs_write_resume(mp, VR_START_WRITE);
			return (EBUSY);
		}
	}

	TMPFS_LOCK(tmp);
	while ((node = LIST_FIRST(&tmp->tm_nodes_used)) != NULL) {
		TMPFS_NODE_LOCK(node);
		if (node->tn_type == VDIR)
			tmpfs_dir_destroy(tmp, node);
		if (tmpfs_free_node_locked(tmp, node, true))
			TMPFS_LOCK(tmp);
		else
			TMPFS_NODE_UNLOCK(node);
	}

	mp->mnt_data = NULL;
	tmpfs_free_tmp(tmp);
	vfs_write_resume(mp, VR_START_WRITE);

	MNT_ILOCK(mp);
	mp->mnt_flag &= ~MNT_LOCAL;
	MNT_IUNLOCK(mp);

	return (0);
}

void
tmpfs_free_tmp(struct tmpfs_mount *tmp)
{

	MPASS(tmp->tm_refcount > 0);
	tmp->tm_refcount--;
	if (tmp->tm_refcount > 0) {
		TMPFS_UNLOCK(tmp);
		return;
	}
	TMPFS_UNLOCK(tmp);

	uma_zdestroy(tmp->tm_dirent_pool);
	uma_zdestroy(tmp->tm_node_pool);

	mtx_destroy(&tmp->tm_allnode_lock);
	MPASS(tmp->tm_pages_used == 0);
	MPASS(tmp->tm_nodes_inuse == 0);

	free(tmp, M_TMPFSMNT);
}

static int
tmpfs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	int error;

	error = tmpfs_alloc_vp(mp, VFS_TO_TMPFS(mp)->tm_root, flags, vpp);
	if (error == 0)
		(*vpp)->v_vflag |= VV_ROOT;
	return (error);
}

static int
tmpfs_fhtovp(struct mount *mp, struct fid *fhp, int flags,
    struct vnode **vpp)
{
	struct tmpfs_fid *tfhp;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *node;
	int error;

	tmp = VFS_TO_TMPFS(mp);

	tfhp = (struct tmpfs_fid *)fhp;
	if (tfhp->tf_len != sizeof(struct tmpfs_fid))
		return (EINVAL);

	if (tfhp->tf_id >= tmp->tm_nodes_max)
		return (EINVAL);

	TMPFS_LOCK(tmp);
	LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
		if (node->tn_id == tfhp->tf_id &&
		    node->tn_gen == tfhp->tf_gen) {
			tmpfs_ref_node(node);
			break;
		}
	}
	TMPFS_UNLOCK(tmp);

	if (node != NULL) {
		error = tmpfs_alloc_vp(mp, node, LK_EXCLUSIVE, vpp);
		tmpfs_free_node(tmp, node);
	} else
		error = EINVAL;
	return (error);
}

/* ARGSUSED2 */
static int
tmpfs_statfs(struct mount *mp, struct statfs *sbp)
{
	struct tmpfs_mount *tmp;
	size_t used;

	tmp = VFS_TO_TMPFS(mp);

	sbp->f_iosize = PAGE_SIZE;
	sbp->f_bsize = PAGE_SIZE;

	used = tmpfs_pages_used(tmp);
	if (tmp->tm_pages_max != ULONG_MAX)
		 sbp->f_blocks = tmp->tm_pages_max;
	else
		 sbp->f_blocks = used + tmpfs_mem_avail();
	if (sbp->f_blocks <= used)
		sbp->f_bavail = 0;
	else
		sbp->f_bavail = sbp->f_blocks - used;
	sbp->f_bfree = sbp->f_bavail;
	used = tmp->tm_nodes_inuse;
	sbp->f_files = tmp->tm_nodes_max;
	if (sbp->f_files <= used)
		sbp->f_ffree = 0;
	else
		sbp->f_ffree = sbp->f_files - used;
	/* sbp->f_owner = tmp->tn_uid; */

	return 0;
}

static int
tmpfs_sync(struct mount *mp, int waitfor)
{
	struct vnode *vp, *mvp;
	struct vm_object *obj;

	if (waitfor == MNT_SUSPEND) {
		MNT_ILOCK(mp);
		mp->mnt_kern_flag |= MNTK_SUSPEND2 | MNTK_SUSPENDED;
		MNT_IUNLOCK(mp);
	} else if (waitfor == MNT_LAZY) {
		/*
		 * Handle lazy updates of mtime from writes to mmaped
		 * regions.  Use MNT_VNODE_FOREACH_ALL instead of
		 * MNT_VNODE_FOREACH_ACTIVE, since unmap of the
		 * tmpfs-backed vnode does not call vinactive(), due
		 * to vm object type is OBJT_SWAP.
		 */
		MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
			if (vp->v_type != VREG) {
				VI_UNLOCK(vp);
				continue;
			}
			obj = vp->v_object;
			KASSERT((obj->flags & (OBJ_TMPFS_NODE | OBJ_TMPFS)) ==
			    (OBJ_TMPFS_NODE | OBJ_TMPFS), ("non-tmpfs obj"));

			/*
			 * Unlocked read, avoid taking vnode lock if
			 * not needed.  Lost update will be handled on
			 * the next call.
			 */
			if ((obj->flags & OBJ_TMPFS_DIRTY) == 0) {
				VI_UNLOCK(vp);
				continue;
			}
			if (vget(vp, LK_EXCLUSIVE | LK_RETRY | LK_INTERLOCK,
			    curthread) != 0)
				continue;
			tmpfs_check_mtime(vp);
			vput(vp);
		}
	}
	return (0);
}

/*
 * The presence of a susp_clean method tells the VFS to track writes.
 */
static void
tmpfs_susp_clean(struct mount *mp __unused)
{
}

/*
 * tmpfs vfs operations.
 */

struct vfsops tmpfs_vfsops = {
	.vfs_mount =			tmpfs_mount,
	.vfs_unmount =			tmpfs_unmount,
	.vfs_root =			tmpfs_root,
	.vfs_statfs =			tmpfs_statfs,
	.vfs_fhtovp =			tmpfs_fhtovp,
	.vfs_sync =			tmpfs_sync,
	.vfs_susp_clean =		tmpfs_susp_clean,
};
VFS_SET(tmpfs_vfsops, tmpfs, VFCF_JAIL);

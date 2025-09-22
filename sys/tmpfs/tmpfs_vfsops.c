/*	$OpenBSD: tmpfs_vfsops.c,v 1.19 2021/10/24 15:41:47 patrick Exp $	*/
/*	$NetBSD: tmpfs_vfsops.c,v 1.52 2011/09/27 01:10:43 christos Exp $	*/

/*
 * Copyright (c) 2005, 2006, 2007 The NetBSD Foundation, Inc.
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
 * tmpfs is a file system that uses NetBSD's virtual memory sub-system
 * (the well-known UVM) to store file data and metadata in an efficient
 * way.  This means that it does not follow the structure of an on-disk
 * file system because it simply does not need to.  Instead, it uses
 * memory-specific data structures and algorithms to automatically
 * allocate and release resources.
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/malloc.h>

#include <tmpfs/tmpfs.h>

/* MODULE(MODULE_CLASS_VFS, tmpfs, NULL); */

struct pool	tmpfs_dirent_pool;
struct pool	tmpfs_node_pool;

int	tmpfs_mount(struct mount *, const char *, void *, struct nameidata *,
	    struct proc *);
int	tmpfs_start(struct mount *, int, struct proc *);
int	tmpfs_unmount(struct mount *, int, struct proc *);
int	tmpfs_root(struct mount *, struct vnode **);
int	tmpfs_vget(struct mount *, ino_t, struct vnode **);
int	tmpfs_fhtovp(struct mount *, struct fid *, struct vnode **);
int	tmpfs_vptofh(struct vnode *, struct fid *);
int	tmpfs_statfs(struct mount *, struct statfs *, struct proc *);
int	tmpfs_sync(struct mount *, int, int, struct ucred *, struct proc *);
int	tmpfs_init(struct vfsconf *);
int	tmpfs_mount_update(struct mount *);

int
tmpfs_init(struct vfsconf *vfsp)
{

	pool_init(&tmpfs_dirent_pool, sizeof(tmpfs_dirent_t), 0, IPL_NONE,
	    PR_WAITOK, "tmpfs_dirent", NULL);
	pool_init(&tmpfs_node_pool, sizeof(tmpfs_node_t), 0, IPL_NONE,
	    PR_WAITOK, "tmpfs_node", NULL);

	return 0;
}

int
tmpfs_mount_update(struct mount *mp)
{
	tmpfs_mount_t *tmp;
	struct vnode *rootvp;
	int error;

	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		return EOPNOTSUPP;

	/* ro->rw transition: nothing to do? */
	if (mp->mnt_flag & MNT_WANTRDWR)
		return 0;

	tmp = mp->mnt_data;
	rootvp = tmp->tm_root->tn_vnode;

	/* Lock root to prevent lookups. */
	error = vn_lock(rootvp, LK_EXCLUSIVE | LK_RETRY);
	if (error)
		return error;

	/* Lock mount point to prevent nodes from being added/removed. */
	rw_enter_write(&tmp->tm_lock);

	/* Flush files opened for writing; skip rootvp. */
	error = vflush(mp, rootvp, WRITECLOSE);

	rw_exit_write(&tmp->tm_lock);
	VOP_UNLOCK(rootvp);

	return error;
}

int
tmpfs_mount(struct mount *mp, const char *path, void *data,
    struct nameidata *ndp, struct proc *p)
{
	struct tmpfs_args *args = data;
	tmpfs_mount_t *tmp;
	tmpfs_node_t *root;
	uint64_t memlimit;
	uint64_t nodes;
	int error;

	if (mp->mnt_flag & MNT_UPDATE)
		return (tmpfs_mount_update(mp));

	/* Prohibit mounts if there is not enough memory. */
	if (tmpfs_mem_info(1) < TMPFS_PAGES_RESERVED)
		return EINVAL;

	if (args->ta_root_uid == VNOVAL || args->ta_root_gid == VNOVAL ||
	    args->ta_root_mode == VNOVAL)
		return EINVAL;

	/* Get the memory usage limit for this file-system. */
	if (args->ta_size_max < PAGE_SIZE) {
		memlimit = UINT64_MAX;
	} else {
		memlimit = args->ta_size_max;
	}
	KASSERT(memlimit > 0);

	if (args->ta_nodes_max <= 3) {
		nodes = 3 + (memlimit / 1024);
	} else {
		nodes = args->ta_nodes_max;
	}
	nodes = MIN(nodes, INT_MAX);
	KASSERT(nodes >= 3);

	/* Allocate the tmpfs mount structure and fill it. */
	tmp = malloc(sizeof(tmpfs_mount_t), M_MISCFSMNT, M_WAITOK);

	tmp->tm_nodes_max = (ino_t)nodes;
	tmp->tm_nodes_cnt = 0;
	tmp->tm_highest_inode = 1;
	LIST_INIT(&tmp->tm_nodes);

	rw_init(&tmp->tm_lock, "tmplk");
	tmpfs_mntmem_init(tmp, memlimit);

	/* Allocate the root node. */
	error = tmpfs_alloc_node(tmp, VDIR, args->ta_root_uid,
	    args->ta_root_gid, args->ta_root_mode & ALLPERMS, NULL,
	    VNOVAL, &root);
	KASSERT(error == 0 && root != NULL);

	/*
	 * Parent of the root inode is itself.  Also, root inode has no
	 * directory entry (i.e. is never attached), thus hold an extra
	 * reference (link) for it.
	 */
	root->tn_links++;
	root->tn_spec.tn_dir.tn_parent = root;
	tmp->tm_root = root;

	mp->mnt_data = tmp;
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_stat.f_namemax = TMPFS_MAXNAMLEN;
	vfs_getnewfsid(mp);

	mp->mnt_stat.mount_info.tmpfs_args = *args;

	bzero(&mp->mnt_stat.f_mntonname, sizeof(mp->mnt_stat.f_mntonname));
	bzero(&mp->mnt_stat.f_mntfromname, sizeof(mp->mnt_stat.f_mntfromname));
	bzero(&mp->mnt_stat.f_mntfromspec, sizeof(mp->mnt_stat.f_mntfromspec));

	strlcpy(mp->mnt_stat.f_mntonname, path,
	    sizeof(mp->mnt_stat.f_mntonname) - 1);
	strlcpy(mp->mnt_stat.f_mntfromname, "tmpfs",
	    sizeof(mp->mnt_stat.f_mntfromname) - 1);
	strlcpy(mp->mnt_stat.f_mntfromspec, "tmpfs",
	    sizeof(mp->mnt_stat.f_mntfromspec) - 1);

	return error;
}

int
tmpfs_start(struct mount *mp, int flags, struct proc *p)
{
	return 0;
}

int
tmpfs_unmount(struct mount *mp, int mntflags, struct proc *p)
{
	tmpfs_mount_t *tmp = VFS_TO_TMPFS(mp);
	tmpfs_node_t *node, *cnode;
	int error, flags = 0;

	/* Handle forced unmounts. */
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	/* Finalize all pending I/O. */
	error = vflush(mp, NULL, flags);
	if (error != 0)
		return error;

	/*
	 * First round, detach and destroy all directory entries.
	 * Also, clear the pointers to the vnodes - they are gone.
	 */
	LIST_FOREACH(node, &tmp->tm_nodes, tn_entries) {
		tmpfs_dirent_t *de;

		node->tn_vnode = NULL;
		if (node->tn_type != VDIR) {
			continue;
		}
		while ((de = TAILQ_FIRST(&node->tn_spec.tn_dir.tn_dir)) != NULL) {
			cnode = de->td_node;
			if (cnode)
				cnode->tn_vnode = NULL;
			tmpfs_dir_detach(node, de);
			tmpfs_free_dirent(tmp, de);
		}
	}

	/* Second round, destroy all inodes. */
	while ((node = LIST_FIRST(&tmp->tm_nodes)) != NULL) {
		tmpfs_free_node(tmp, node);
	}

	/* Throw away the tmpfs_mount structure. */
	tmpfs_mntmem_destroy(tmp);
	/* mutex_destroy(&tmp->tm_lock); */
	/* kmem_free(tmp, sizeof(*tmp)); */
	free(tmp, M_MISCFSMNT, sizeof(tmpfs_mount_t));
	mp->mnt_data = NULL;

	return 0;
}

int
tmpfs_root(struct mount *mp, struct vnode **vpp)
{
	tmpfs_node_t *node = VFS_TO_TMPFS(mp)->tm_root;

	rw_enter_write(&node->tn_nlock);
	return tmpfs_vnode_get(mp, node, vpp);
}

int
tmpfs_vget(struct mount *mp, ino_t ino, struct vnode **vpp)
{

	printf("tmpfs_vget called; need for it unknown yet\n");
	return EOPNOTSUPP;
}

int
tmpfs_fhtovp(struct mount *mp, struct fid *fhp, struct vnode **vpp)
{
	tmpfs_mount_t *tmp = VFS_TO_TMPFS(mp);
	tmpfs_node_t *node;
	tmpfs_fid_t tfh;

	if (fhp->fid_len != sizeof(tmpfs_fid_t)) {
		return EINVAL;
	}
	memcpy(&tfh, fhp, sizeof(tmpfs_fid_t));

	rw_enter_write(&tmp->tm_lock);
	LIST_FOREACH(node, &tmp->tm_nodes, tn_entries) {
		if (node->tn_id != tfh.tf_id) {
			continue;
		}
		if (TMPFS_NODE_GEN(node) != tfh.tf_gen) {
			continue;
		}
		rw_enter_write(&node->tn_nlock);
		break;
	}
	rw_exit_write(&tmp->tm_lock);

	/* Will release the tn_nlock. */
	return node ? tmpfs_vnode_get(mp, node, vpp) : ESTALE;
}

int
tmpfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	tmpfs_fid_t tfh;
	tmpfs_node_t *node;

	node = VP_TO_TMPFS_NODE(vp);

	memset(&tfh, 0, sizeof(tfh));
	tfh.tf_len = sizeof(tmpfs_fid_t);
	tfh.tf_gen = TMPFS_NODE_GEN(node);
	tfh.tf_id = node->tn_id;
	memcpy(fhp, &tfh, sizeof(tfh));

	return 0;
}

int
tmpfs_statfs(struct mount *mp, struct statfs *sbp, struct proc *p)
{
	tmpfs_mount_t *tmp;
	fsfilcnt_t freenodes;
	uint64_t avail;

	tmp = VFS_TO_TMPFS(mp);

	sbp->f_iosize = sbp->f_bsize = PAGE_SIZE;

	rw_enter_write(&tmp->tm_acc_lock);
	avail =  tmpfs_pages_avail(tmp);
	sbp->f_blocks = (tmpfs_bytes_max(tmp) >> PAGE_SHIFT);
	sbp->f_bfree = avail;
	sbp->f_bavail = avail & INT64_MAX; /* f_bavail is int64_t */

	freenodes = MIN(tmp->tm_nodes_max - tmp->tm_nodes_cnt,
	    avail * PAGE_SIZE / sizeof(tmpfs_node_t));

	sbp->f_files = tmp->tm_nodes_cnt + freenodes;
	sbp->f_ffree = freenodes;
	sbp->f_favail = freenodes & INT64_MAX; /* f_favail is int64_t */
	rw_exit_write(&tmp->tm_acc_lock);

	copy_statfs_info(sbp, mp);

	return 0;
}

int
tmpfs_sync(struct mount *mp, int waitfor, int stall, struct ucred *cred,
    struct proc *p)
{

	return 0;
}

/*
 * tmpfs vfs operations.
 */

const struct vfsops tmpfs_vfsops = {
	.vfs_mount	= tmpfs_mount,
	.vfs_start	= tmpfs_start,
	.vfs_unmount	= tmpfs_unmount,
	.vfs_root	= tmpfs_root,
	.vfs_quotactl	= (void *)eopnotsupp,
	.vfs_statfs	= tmpfs_statfs,
	.vfs_sync	= tmpfs_sync,
	.vfs_vget	= tmpfs_vget,
	.vfs_fhtovp	= tmpfs_fhtovp,
	.vfs_vptofh	= tmpfs_vptofh,
	.vfs_init	= tmpfs_init,
	.vfs_sysctl	= (void *)eopnotsupp,
	.vfs_checkexp	= (void *)eopnotsupp,
};

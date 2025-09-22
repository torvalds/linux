/*	$OpenBSD: ufs_inode.c,v 1.47 2024/07/13 14:37:56 beck Exp $	*/
/*	$NetBSD: ufs_inode.c,v 1.7 1996/05/11 18:27:52 mycroft Exp $	*/

/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ufs_inode.c	8.7 (Berkeley) 7/22/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/namei.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>
#ifdef UFS_DIRHASH
#include <ufs/ufs/dir.h>
#include <ufs/ufs/dirhash.h>
#endif

/*
 * Last reference to an inode.  If necessary, write or delete it.
 */
int
ufs_inactive(void *v)
{
	struct vop_inactive_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	mode_t mode;
	int error = 0;
#ifdef DIAGNOSTIC
	extern int prtactive;

	if (prtactive && vp->v_usecount != 0)
		vprint("ufs_inactive: pushing active", vp);
#endif

	/*
	 * Ignore inodes related to stale file handles.
	 */
	if (ip->i_din1 == NULL || DIP(ip, mode) == 0)
		goto out;

	if (DIP(ip, nlink) <= 0 && (vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
		if (getinoquota(ip) == 0)
			(void)ufs_quota_free_inode(ip, NOCRED);

		error = UFS_TRUNCATE(ip, (off_t)0, 0, NOCRED);

		DIP_ASSIGN(ip, rdev, 0);
		mode = DIP(ip, mode);
		DIP_ASSIGN(ip, mode, 0);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;

		UFS_INODE_FREE(ip, ip->i_number, mode);
	}

	if (ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) {
		UFS_UPDATE(ip, 0);
	}
out:
	VOP_UNLOCK(vp);

	/*
	 * If we are done with the inode, reclaim it
	 * so that it can be reused immediately.
	 */
	if (ip->i_din1 == NULL || DIP(ip, mode) == 0)
		vrecycle(vp, ap->a_p);

	return (error);
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 */
int
ufs_reclaim(struct vnode *vp)
{
	struct inode *ip;
#ifdef DIAGNOSTIC
	extern int prtactive;

	if (prtactive && vp->v_usecount != 0)
		vprint("ufs_reclaim: pushing active", vp);
#endif

	ip = VTOI(vp);

	/*
	 * Stop deferring timestamp writes
	 */
	if (ip->i_flag & IN_LAZYMOD) {
		ip->i_flag |= IN_MODIFIED;
		UFS_UPDATE(ip, 0);
	}

	/*
	 * Remove the inode from its hash chain.
	 */
	ufs_ihashrem(ip);
	/*
	 * Purge old data structures associated with the inode.
	 */
	cache_purge(vp);

	if (ip->i_devvp) {
		vrele(ip->i_devvp);
	}
#ifdef UFS_DIRHASH
	if (ip->i_dirhash != NULL)
		ufsdirhash_free(ip);
#endif
	ufs_quota_delete(ip);
	return (0);
}

/*	$OpenBSD: ufs_vfsops.c,v 1.21 2025/09/20 13:53:36 mpi Exp $	*/
/*	$NetBSD: ufs_vfsops.c,v 1.4 1996/02/09 22:36:12 christos Exp $	*/

/*
 * Copyright (c) 1991, 1993, 1994
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
 *	@(#)ufs_vfsops.c	8.4 (Berkeley) 4/16/94
 */

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/specdev.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>
#ifdef UFS_DIRHASH
#include <ufs/ufs/dir.h>
#include <ufs/ufs/dirhash.h>
#endif

/*
 * Make a filesystem operational.
 * Nothing to do at the moment.
 */
int
ufs_start(struct mount *mp, int flags, struct proc *p)
{
	return (0);
}

/*
 * Return the root of a filesystem.
 */
int
ufs_root(struct mount *mp, struct vnode **vpp)
{
	struct vnode *nvp;
	int error;

	if ((error = VFS_VGET(mp, ROOTINO, &nvp)) != 0)
		return (error);
	*vpp = nvp;
	return (0);
}

/*
 * Verify a remote client has export rights and return these rights via.
 * exflagsp and credanonp.
 */
int
ufs_check_export(struct mount *mp, struct mbuf *nam, int *exflagsp,
    struct ucred **credanonp) 
{
	struct netcred *np;
	struct ufsmount *ump = VFSTOUFS(mp);

	/*
	 * Get the export permission structure for this <mp, client> tuple.
	 */
	np = vfs_export_lookup(mp, &ump->um_export, nam);
	if (np == NULL)
		return (EACCES);

	*exflagsp = np->netc_exflags;
	*credanonp = &np->netc_anon;
	return (0);
}

/*
 * Initialize UFS file systems, done only once.
 */
int
ufs_init(struct vfsconf *vfsp)
{
	static int done;

	if (done)
		return (0);
	done = 1;
	ufs_ihashinit();
	ufs_quota_init();
#ifdef UFS_DIRHASH
	ufsdirhash_init();
#endif

	return (0);
}

/*
 * This is the generic part of fhtovp called after the underlying
 * filesystem has validated the file handle.
 */
int
ufs_fhtovp(struct mount *mp, struct ufid *ufhp, struct vnode **vpp)
{
	struct inode *ip;
	struct vnode *nvp;
	int error;

	if ((error = VFS_VGET(mp, ufhp->ufid_ino, &nvp)) != 0) {
		*vpp = NULL;
		return (error);
	}
	ip = VTOI(nvp);
	if (DIP(ip, mode) == 0 || DIP(ip, gen) != ufhp->ufid_gen) {
		vput(nvp);
		*vpp = NULL;
		return (ESTALE);
	}
	*vpp = nvp;
	return (0);
}

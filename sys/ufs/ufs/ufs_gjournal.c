/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include "opt_ufs.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/mutex.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/gjournal.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

/*
 * Change the number of unreferenced inodes.
 */
static int
ufs_gjournal_modref(struct vnode *vp, int count)
{
	struct cg *cgp;
	struct buf *bp;
	int error, cg;
	struct cdev *dev;
	struct inode *ip;
	struct ufsmount *ump;
	struct fs *fs;
	struct vnode *devvp;
	ino_t ino;

	ip = VTOI(vp);
	ump = VFSTOUFS(vp->v_mount);
	fs = ump->um_fs;
	devvp = ump->um_devvp;
	ino = ip->i_number;

	cg = ino_to_cg(fs, ino);
	if (devvp->v_type == VREG) {
		/* devvp is a snapshot */
		dev = VFSTOUFS(devvp->v_mount)->um_devvp->v_rdev;
	} else if (devvp->v_type == VCHR) {
		/* devvp is a normal disk device */
		dev = devvp->v_rdev;
	} else {
		bp = NULL;
		return (EIO);
	}
	if ((u_int)ino >= fs->fs_ipg * fs->fs_ncg)
		panic("ufs_gjournal_modref: range: dev = %s, ino = %lu, fs = %s",
		    devtoname(dev), (u_long)ino, fs->fs_fsmnt);
	if ((error = ffs_getcg(fs, devvp, cg, &bp, &cgp)) != 0)
		return (error);
	cgp->cg_unrefs += count;
	UFS_LOCK(ump);
	fs->fs_unrefs += count;
	fs->fs_fmod = 1;
	ACTIVECLEAR(fs, cg);
	UFS_UNLOCK(ump);
	bdwrite(bp);
	return (0);
}

void
ufs_gjournal_orphan(struct vnode *vp)
{
	struct inode *ip;

	if (vp->v_mount->mnt_gjprovider == NULL)
		return;
	if (vp->v_usecount < 2 || (vp->v_vflag & VV_DELETED))
		return;
	ip = VTOI(vp);
	if ((vp->v_type == VDIR && ip->i_nlink > 2) ||
	    (vp->v_type != VDIR && ip->i_nlink > 1)) {
		return;
	}
	vp->v_vflag |= VV_DELETED;

	ufs_gjournal_modref(vp, 1);
}

void
ufs_gjournal_close(struct vnode *vp)
{
	struct inode *ip;

	if (vp->v_mount->mnt_gjprovider == NULL)
		return;
	if (!(vp->v_vflag & VV_DELETED))
		return;
	ip = VTOI(vp);
	if (ip->i_nlink > 0)
		return;
	ufs_gjournal_modref(vp, -1);
}

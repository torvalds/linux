/* $OpenBSD: fuse_lookup.c,v 1.22 2024/10/31 13:55:21 claudio Exp $ */
/*
 * Copyright (c) 2012-2013 Sylvestre Gallon <ccna.syl@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/fusebuf.h>

#include "fusefs_node.h"
#include "fusefs.h"

int fusefs_lookup(void *);

int
fusefs_lookup(void *v)
{
	struct vop_lookup_args *ap = v;
	struct vnode *vdp;	/* vnode for directory being searched */
	struct fusefs_node *dp;	/* inode for directory being searched */
	struct fusefs_mnt *fmp;	/* file system that directory is in */
	int lockparent;		/* 1 => lockparent flag is set */
	struct vnode *tdp;	/* returned by VOP_VGET */
	struct fusebuf *fbuf;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct proc *p = cnp->cn_proc;
	struct ucred *cred = cnp->cn_cred;
	uint64_t nid;
	enum vtype nvtype;
	int flags;
	int nameiop = cnp->cn_nameiop;
	int wantparent;
	int error = 0;

	flags = cnp->cn_flags;
	*vpp = NULL;
	vdp = ap->a_dvp;
	dp = VTOI(vdp);
	fmp = (struct fusefs_mnt *)dp->i_ump;
	lockparent = flags & LOCKPARENT;
	wantparent = flags & (LOCKPARENT | WANTPARENT);

	if ((error = VOP_ACCESS(vdp, VEXEC, cred, cnp->cn_proc)) != 0)
		return (error);

	if ((flags & ISLASTCN) && (vdp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME))
		return (EROFS);

	if (cnp->cn_namelen == 1 && *(cnp->cn_nameptr) == '.') {
		nid = dp->i_number;
	} else {
		if (!fmp->sess_init)
			return (ENOENT);

		/* got a real entry */
		fbuf = fb_setup(cnp->cn_namelen + 1, dp->i_number,
		    FBT_LOOKUP, p);

		memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
		fbuf->fb_dat[cnp->cn_namelen] = '\0';

		error = fb_queue(fmp->dev, fbuf);

		if (error) {
			fb_delete(fbuf);

			/* file system is dead */
			if (error == ENXIO)
				return (error);

			if ((nameiop == CREATE || nameiop == RENAME) &&
			    (flags & ISLASTCN)) {
				/*
				 * Access for write is interpreted as allowing
				 * creation of files in the directory.
				 */
				if ((error = VOP_ACCESS(vdp, VWRITE, cred,
				    cnp->cn_proc)) != 0)
					return (error); 

				cnp->cn_flags |= SAVENAME;

				if (!lockparent) {
					VOP_UNLOCK(vdp);
					cnp->cn_flags |= PDIRUNLOCK;
				}

				return (EJUSTRETURN);
			}

			return (ENOENT);
		}

		nid = fbuf->fb_ino;
		nvtype = IFTOVT(fbuf->fb_attr.st_mode);
		fb_delete(fbuf);
	}

	if (nameiop == DELETE && (flags & ISLASTCN)) {
		/*
		 * Write access to directory required to delete files.
		 */
		error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_proc);
		if (error)
			goto reclaim;

		cnp->cn_flags |= SAVENAME;
	}

	if (nameiop == RENAME && wantparent && (flags & ISLASTCN)) {
		/*
		 * Write access to directory required to delete files.
		 */
		if ((error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_proc)) != 0)
			goto reclaim;

		if (nid == dp->i_number)
			return (EISDIR);

		error = VFS_VGET(fmp->mp, nid, &tdp);
		if (error)
			goto reclaim;

		tdp->v_type = nvtype;
		*vpp = tdp;
		cnp->cn_flags |= SAVENAME;

		return (0);
	}

	if (flags & ISDOTDOT) {
		VOP_UNLOCK(vdp);	/* race to get the inode */
		cnp->cn_flags |= PDIRUNLOCK;

		error = VFS_VGET(fmp->mp, nid, &tdp);

		if (error) {
			if (vn_lock(vdp, LK_EXCLUSIVE | LK_RETRY) == 0)
				cnp->cn_flags &= ~PDIRUNLOCK;

			goto reclaim;
		}

		tdp->v_type = nvtype;

		if (lockparent && (flags & ISLASTCN)) {
			if ((error = vn_lock(vdp, LK_EXCLUSIVE))) {
				vput(tdp);
				return (error);
			}
			cnp->cn_flags &= ~PDIRUNLOCK;
		}
		*vpp = tdp;

	} else if (nid == dp->i_number) {
		vref(vdp);
		*vpp = vdp;
		error = 0;
	} else {
		error = VFS_VGET(fmp->mp, nid, &tdp);
		if (error)
			goto reclaim;

		tdp->v_type = nvtype;

		if (!lockparent || !(flags & ISLASTCN)) {
			VOP_UNLOCK(vdp);
			cnp->cn_flags |= PDIRUNLOCK;
		}

		*vpp = tdp;
	}

	return (error);

reclaim:
	if (nid != dp->i_number && nid != FUSE_ROOTINO) {
		fbuf = fb_setup(0, nid, FBT_RECLAIM, p);
		if (fb_queue(fmp->dev, fbuf))
			printf("fusefs: libfuse vnode reclaim failed\n");
		fb_delete(fbuf);
	}
	return (error);
}

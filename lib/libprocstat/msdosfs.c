/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000 Peter Edwards
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by Peter Edwards
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/vnode.h>

#include <netinet/in.h>

#define	_KERNEL
#include <sys/mount.h>
#include <fs/msdosfs/bpb.h>
#include <fs/msdosfs/msdosfsmount.h>
#undef _KERNEL

#include <fs/msdosfs/denode.h>
#include <fs/msdosfs/direntry.h>
#include <fs/msdosfs/fat.h>

#include <err.h>
#include <kvm.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * XXX -
 * VTODE is defined in denode.h only if _KERNEL is defined, but that leads to
 * header explosion
 */
#define VTODE(vp) ((struct denode *)getvnodedata(vp))

#include "libprocstat.h"
#include "common_kvm.h"

struct dosmount {
	struct dosmount *next;
	struct msdosfsmount *kptr;	/* Pointer in kernel space */
	struct msdosfsmount data;	/* User space copy of structure */
};

int
msdosfs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn)
{
	struct denode denode;
	static struct dosmount *mounts;
	struct dosmount *mnt;
	u_long dirsperblk;
	int fileid;

	if (!kvm_read_all(kd, (unsigned long)VTODE(vp), &denode,
	    sizeof(denode))) {
		warnx("can't read denode at %p", (void *)VTODE(vp));
		return (1);
	}

	/*
	 * Find msdosfsmount structure for the vnode's filesystem. Needed
	 * for some filesystem parameters
	 */
	for (mnt = mounts; mnt; mnt = mnt->next)
		if (mnt->kptr == denode.de_pmp)
			break;

	if (!mnt) {
		if ((mnt = malloc(sizeof(struct dosmount))) == NULL) {
			warn("malloc()");
			return (1);
		}
		if (!kvm_read_all(kd, (unsigned long)denode.de_pmp,
		    &mnt->data, sizeof(mnt->data))) {
			free(mnt);
			    warnx("can't read mount info at %p",
			    (void *)denode.de_pmp);
			return (1);
		}
		mnt->next = mounts;
		mounts = mnt;
		mnt->kptr = denode.de_pmp;
	}

	vn->vn_fsid = dev2udev(kd, mnt->data.pm_dev);
	vn->vn_mode = 0555;
	vn->vn_mode |= denode.de_Attributes & ATTR_READONLY ? 0 : 0222;
	vn->vn_mode &= mnt->data.pm_mask;

	/* Distinguish directories and files. No "special" files in FAT. */
	vn->vn_mode |= denode.de_Attributes & ATTR_DIRECTORY ? S_IFDIR : S_IFREG;
	vn->vn_size = denode.de_FileSize;

	/*
	 * XXX -
	 * Culled from msdosfs_vnops.c. There appears to be a problem
	 * here, in that a directory has the same inode number as the first
	 * file in the directory. stat(2) suffers from this problem also, so
	 * I won't try to fix it here.
	 * 
	 * The following computation of the fileid must be the same as that
	 * used in msdosfs_readdir() to compute d_fileno. If not, pwd
	 * doesn't work.
	 */
	dirsperblk = mnt->data.pm_BytesPerSec / sizeof(struct direntry);
	if (denode.de_Attributes & ATTR_DIRECTORY) {
		fileid = cntobn(&mnt->data, denode.de_StartCluster)
		    * dirsperblk;
		if (denode.de_StartCluster == MSDOSFSROOT)
			fileid = 1;
	} else {
		fileid = cntobn(&mnt->data, denode.de_dirclust) * dirsperblk;
		if (denode.de_dirclust == MSDOSFSROOT)
			fileid = roottobn(&mnt->data, 0) * dirsperblk;
		fileid += denode.de_diroffset / sizeof(struct direntry);
	}

	vn->vn_fileid = fileid;
	return (0);
}

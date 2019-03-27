/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Juli Mallett.  All rights reserved.
 *
 * This software was written by Juli Mallett <jmallett@FreeBSD.org> for the
 * FreeBSD project.  Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistribution of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistribution in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/disklabel.h>
#include <sys/stat.h>

#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libufs.h>

int
getinode(struct uufsd *disk, union dinodep *dp, ino_t inum)
{
	ino_t min, max;
	caddr_t inoblock;
	struct fs *fs;

	ERROR(disk, NULL);

	fs = &disk->d_fs;
	if (inum >= (ino_t)fs->fs_ipg * fs->fs_ncg) {
		ERROR(disk, "inode number out of range");
		return (-1);
	}
	inoblock = disk->d_inoblock;
	min = disk->d_inomin;
	max = disk->d_inomax;

	if (inoblock == NULL) {
		inoblock = malloc(fs->fs_bsize);
		if (inoblock == NULL) {
			ERROR(disk, "unable to allocate inode block");
			return (-1);
		}
		disk->d_inoblock = inoblock;
	}
	if (inum >= min && inum < max)
		goto gotit;
	bread(disk, fsbtodb(fs, ino_to_fsba(fs, inum)), inoblock,
	    fs->fs_bsize);
	disk->d_inomin = min = inum - (inum % INOPB(fs));
	disk->d_inomax = max = min + INOPB(fs);
gotit:	switch (disk->d_ufs) {
	case 1:
		disk->d_dp.dp1 = &((struct ufs1_dinode *)inoblock)[inum - min];
		if (dp != NULL)
			*dp = disk->d_dp;
		return (0);
	case 2:
		disk->d_dp.dp2 = &((struct ufs2_dinode *)inoblock)[inum - min];
		if (dp != NULL)
			*dp = disk->d_dp;
		if (ffs_verify_dinode_ckhash(fs, disk->d_dp.dp2) == 0)
			return (0);
		ERROR(disk, "check-hash failed for inode read from disk");
		return (-1);
	default:
		break;
	}
	ERROR(disk, "unknown UFS filesystem type");
	return (-1);
}

int
putinode(struct uufsd *disk)
{
	struct fs *fs;

	fs = &disk->d_fs;
	if (disk->d_inoblock == NULL) {
		ERROR(disk, "No inode block allocated");
		return (-1);
	}
	if (disk->d_ufs == 2)
		ffs_update_dinode_ckhash(fs, disk->d_dp.dp2);
	if (bwrite(disk, fsbtodb(fs, ino_to_fsba(&disk->d_fs, disk->d_inomin)),
	    disk->d_inoblock, disk->d_fs.fs_bsize) <= 0)
		return (-1);
	return (0);
}

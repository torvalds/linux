/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Juli Mallett.  All rights reserved.
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

ufs2_daddr_t
cgballoc(struct uufsd *disk)
{
	u_int8_t *blksfree;
	struct cg *cgp;
	struct fs *fs;
	long bno;

	fs = &disk->d_fs;
	cgp = &disk->d_cg;
	blksfree = cg_blksfree(cgp);
	for (bno = 0; bno < fs->fs_fpg / fs->fs_frag; bno++)
		if (ffs_isblock(fs, blksfree, bno))
			goto gotit;
	return (0);
gotit:
	fs->fs_cs(fs, cgp->cg_cgx).cs_nbfree--;
	ffs_clrblock(fs, blksfree, (long)bno);
	ffs_clusteracct(fs, cgp, bno, -1);
	cgp->cg_cs.cs_nbfree--;
	fs->fs_cstotal.cs_nbfree--;
	fs->fs_fmod = 1;
	return (cgbase(fs, cgp->cg_cgx) + blkstofrags(fs, bno));
}

int
cgbfree(struct uufsd *disk, ufs2_daddr_t bno, long size)
{
	u_int8_t *blksfree;
	struct fs *fs;
	struct cg *cgp;
	ufs1_daddr_t fragno, cgbno;
	int i, cg, blk, frags, bbase;

	fs = &disk->d_fs;
	cg = dtog(fs, bno);
	if (cgread1(disk, cg) != 1)
		return (-1);
	cgp = &disk->d_cg;
	cgbno = dtogd(fs, bno);
	blksfree = cg_blksfree(cgp);
	if (size == fs->fs_bsize) {
		fragno = fragstoblks(fs, cgbno);
		ffs_setblock(fs, blksfree, fragno);
		ffs_clusteracct(fs, cgp, fragno, 1);
		cgp->cg_cs.cs_nbfree++;
		fs->fs_cstotal.cs_nbfree++;
		fs->fs_cs(fs, cg).cs_nbfree++;
	} else {
		bbase = cgbno - fragnum(fs, cgbno);
		/*
		 * decrement the counts associated with the old frags
		 */
		blk = blkmap(fs, blksfree, bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, -1);
		/*
		 * deallocate the fragment
		 */
		frags = numfrags(fs, size);
		for (i = 0; i < frags; i++)
			setbit(blksfree, cgbno + i);
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		/*
		 * add back in counts associated with the new frags
		 */
		blk = blkmap(fs, blksfree, bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, 1);
		/*
		 * if a complete block has been reassembled, account for it
		 */
		fragno = fragstoblks(fs, bbase);
		if (ffs_isblock(fs, blksfree, fragno)) {
			cgp->cg_cs.cs_nffree -= fs->fs_frag;
			fs->fs_cstotal.cs_nffree -= fs->fs_frag;
			fs->fs_cs(fs, cg).cs_nffree -= fs->fs_frag;
			ffs_clusteracct(fs, cgp, fragno, 1);
			cgp->cg_cs.cs_nbfree++;
			fs->fs_cstotal.cs_nbfree++;
			fs->fs_cs(fs, cg).cs_nbfree++;
		}
	}
	return cgwrite(disk);
}

ino_t
cgialloc(struct uufsd *disk)
{
	struct ufs2_dinode *dp2;
	u_int8_t *inosused;
	struct cg *cgp;
	struct fs *fs;
	ino_t ino;
	int i;

	fs = &disk->d_fs;
	cgp = &disk->d_cg;
	inosused = cg_inosused(cgp);
	for (ino = 0; ino < fs->fs_ipg; ino++)
		if (isclr(inosused, ino))
			goto gotit;
	return (0);
gotit:
	if (fs->fs_magic == FS_UFS2_MAGIC &&
	    ino + INOPB(fs) > cgp->cg_initediblk &&
	    cgp->cg_initediblk < cgp->cg_niblk) {
		char block[MAXBSIZE];
		bzero(block, (int)fs->fs_bsize);
		dp2 = (struct ufs2_dinode *)&block;
		for (i = 0; i < INOPB(fs); i++) {
			dp2->di_gen = arc4random();
			dp2++;
		}
		if (bwrite(disk, ino_to_fsba(fs,
		    cgp->cg_cgx * fs->fs_ipg + cgp->cg_initediblk),
		    block, fs->fs_bsize))
			return (0);
		cgp->cg_initediblk += INOPB(fs);
	}

	setbit(inosused, ino);
	cgp->cg_irotor = ino;
	cgp->cg_cs.cs_nifree--;
	fs->fs_cstotal.cs_nifree--;
	fs->fs_cs(fs, cgp->cg_cgx).cs_nifree--;
	fs->fs_fmod = 1;

	return (ino + (cgp->cg_cgx * fs->fs_ipg));
}

int
cgread(struct uufsd *disk)
{

	if (disk->d_ccg >= disk->d_fs.fs_ncg)
		return (0);
	return (cgread1(disk, disk->d_ccg++));
}

int
cgread1(struct uufsd *disk, int c)
{

	if ((cgget(disk, c, &disk->d_cg)) == 0)
		return (1);
	return (-1);
}

int
cgget(struct uufsd *disk, int cg, struct cg *cgp)
{
	struct fs *fs;
	uint32_t cghash, calchash;

	fs = &disk->d_fs;
	if (bread(disk, fsbtodb(fs, cgtod(fs, cg)), (void *)cgp,
	    fs->fs_cgsize) == -1) {
		ERROR(disk, "unable to read cylinder group");
		return (-1);
	}
	calchash = cgp->cg_ckhash;
	if ((fs->fs_metackhash & CK_CYLGRP) != 0) {
		cghash = cgp->cg_ckhash;
		cgp->cg_ckhash = 0;
		calchash = calculate_crc32c(~0L, (void *)cgp, fs->fs_cgsize);
		cgp->cg_ckhash = cghash;
	}
	if (cgp->cg_ckhash != calchash || !cg_chkmagic(cgp) ||
	    cgp->cg_cgx != cg) {
		ERROR(disk, "cylinder group checks failed");
		errno = EIO;
		return (-1);
	}
	disk->d_lcg = cg;
	return (0);
}

int
cgwrite(struct uufsd *disk)
{

	return (cgput(disk, &disk->d_cg));
}

int
cgwrite1(struct uufsd *disk, int cg)
{
	static char errmsg[BUFSIZ];

	if (cg == disk->d_cg.cg_cgx)
		return (cgput(disk, &disk->d_cg));
	snprintf(errmsg, BUFSIZ, "Cylinder group %d in buffer does not match "
	    "the cylinder group %d that cgwrite1 requested",
	    disk->d_cg.cg_cgx, cg);
	ERROR(disk, errmsg);
	errno = EDOOFUS;
	return (-1);
}

int
cgput(struct uufsd *disk, struct cg *cgp)
{
	struct fs *fs;

	fs = &disk->d_fs;
	if ((fs->fs_metackhash & CK_CYLGRP) != 0) {
		cgp->cg_ckhash = 0;
		cgp->cg_ckhash =
		    calculate_crc32c(~0L, (void *)cgp, fs->fs_cgsize);
	}
	if (bwrite(disk, fsbtodb(fs, cgtod(fs, cgp->cg_cgx)), cgp,
	    fs->fs_cgsize) == -1) {
		ERROR(disk, "unable to write cylinder group");
		return (-1);
	}
	return (0);
}

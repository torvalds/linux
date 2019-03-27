/*
 * Copyright (c) 1998, 2003, 2013, 2018 Marshall Kirk McKusick.
 * All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY MARSHALL KIRK MCKUSICK ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL MARSHALL KIRK MCKUSICK BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <stdio.h>
#include <libufs.h>

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};

void prtblknos(struct uufsd *disk, union dinode *dp);

static const char *distance(struct fs *, ufs2_daddr_t, ufs2_daddr_t);
static void  printblk(struct fs *, ufs_lbn_t, ufs2_daddr_t, int, ufs_lbn_t);
static void  indirprt(struct uufsd *, int, ufs_lbn_t, ufs_lbn_t, ufs2_daddr_t,
		ufs_lbn_t);

void
prtblknos(disk, dp)
	struct uufsd *disk;
	union dinode *dp;
{
	int i, mode, frags;
	ufs_lbn_t lbn, lastlbn, len, blksperindir;
	ufs2_daddr_t blkno;
	struct fs *fs;
	off_t size;

	fs = (struct fs *)&disk->d_sb;
	if (fs->fs_magic == FS_UFS1_MAGIC) {
		size = dp->dp1.di_size;
		mode = dp->dp1.di_mode;
	} else {
		size = dp->dp2.di_size;
		mode = dp->dp2.di_mode;
	}
	switch (mode & IFMT) {
	default:
		printf("unknown inode type 0%d\n", (mode & IFMT));
		return;
	case 0:
		printf("unallocated inode\n");
		return;
	case IFIFO:
		printf("fifo\n");
		return;
	case IFCHR:
		printf("character device\n");
		return;
	case IFBLK:
		printf("block device\n");
		return;
	case IFSOCK:
		printf("socket\n");
		return;
	case IFWHT:
		printf("whiteout\n");
		return;
	case IFLNK:
		if (size == 0) {
			printf("empty symbolic link\n");
			return;
		}
		if (size < fs->fs_maxsymlinklen) {
			printf("symbolic link referencing %s\n",
			    (fs->fs_magic == FS_UFS1_MAGIC) ?
			    (char *)dp->dp1.di_db :
			    (char *)dp->dp2.di_db);
			return;
		}
		printf("symbolic link\n");
		break;
	case IFREG:
		if (size == 0) {
			printf("empty file\n");
			return;
		}
		printf("regular file, size %jd\n", (intmax_t)size);
		break;
	case IFDIR:
		if (size == 0) {
			printf("empty directory\n");
			return;
		}
		printf("directory, size %jd\n", (intmax_t)size);
		break;
	}
	lastlbn = howmany(size, fs->fs_bsize);
	len = lastlbn < UFS_NDADDR ? lastlbn : UFS_NDADDR;
	for (i = 0; i < len; i++) {
		if (i < lastlbn - 1)
			frags = fs->fs_frag;
		else
			frags = howmany(size - (lastlbn - 1) * fs->fs_bsize,
					  fs->fs_fsize);
		if (fs->fs_magic == FS_UFS1_MAGIC)
			blkno = dp->dp1.di_db[i];
		else
			blkno = dp->dp2.di_db[i];
		printblk(fs, i, blkno, frags, lastlbn);
	}

	blksperindir = 1;
	len = lastlbn - UFS_NDADDR;
	lbn = UFS_NDADDR;
	for (i = 0; len > 0 && i < UFS_NIADDR; i++) {
		if (fs->fs_magic == FS_UFS1_MAGIC)
			blkno = dp->dp1.di_ib[i];
		else
			blkno = dp->dp2.di_ib[i];
		indirprt(disk, i, blksperindir, lbn, blkno, lastlbn);
		blksperindir *= NINDIR(fs);
		lbn += blksperindir;
		len -= blksperindir;
	}

	/* dummy print to flush out last extent */
	printblk(fs, lastlbn, 0, frags, 0);
}

static void
indirprt(disk, level, blksperindir, lbn, blkno, lastlbn)
	struct uufsd *disk;
	int level;
	ufs_lbn_t blksperindir;
	ufs_lbn_t lbn;
	ufs2_daddr_t blkno;
	ufs_lbn_t lastlbn;
{
	char indir[MAXBSIZE];
	struct fs *fs;
	ufs_lbn_t i, last;

	fs = (struct fs *)&disk->d_sb;
	if (blkno == 0) {
		printblk(fs, lbn, blkno,
		    blksperindir * NINDIR(fs) * fs->fs_frag, lastlbn);
		return;
	}
	printblk(fs, lbn, blkno, fs->fs_frag, -level);
	/* read in the indirect block. */
	if (bread(disk, fsbtodb(fs, blkno), indir, fs->fs_bsize) == -1) {
		warn("Read of indirect block %jd failed", (intmax_t)blkno);
		/* List the unreadable part as a hole */
		printblk(fs, lbn, 0,
		    blksperindir * NINDIR(fs) * fs->fs_frag, lastlbn);
		return;
	}
	last = howmany(lastlbn - lbn, blksperindir) < NINDIR(fs) ?
	    howmany(lastlbn - lbn, blksperindir) : NINDIR(fs);
	if (blksperindir == 1) {
		for (i = 0; i < last; i++) {
			if (fs->fs_magic == FS_UFS1_MAGIC)
				blkno = ((ufs1_daddr_t *)indir)[i];
			else
				blkno = ((ufs2_daddr_t *)indir)[i];
			printblk(fs, lbn + i, blkno, fs->fs_frag, lastlbn);
		}
		return;
	}
	for (i = 0; i < last; i++) {
		if (fs->fs_magic == FS_UFS1_MAGIC)
			blkno = ((ufs1_daddr_t *)indir)[i];
		else
			blkno = ((ufs2_daddr_t *)indir)[i];
		indirprt(disk, level - 1, blksperindir / NINDIR(fs),
		    lbn + blksperindir * i, blkno, lastlbn);
	}
}

static const char *
distance(fs, lastblk, firstblk)
	struct fs *fs;
	ufs2_daddr_t lastblk;
	ufs2_daddr_t firstblk;
{
	ufs2_daddr_t delta;
	int firstcg, lastcg;
	static char buf[100];

	if (lastblk == 0)
		return ("");
	delta = firstblk - lastblk - 1;
	firstcg = dtog(fs, firstblk);
	lastcg = dtog(fs, lastblk);
	if (firstcg == lastcg) {
		snprintf(buf, 100, " distance %jd", (intmax_t)delta);
		return (&buf[0]);
	}
	snprintf(buf, 100, " cg %d blk %jd to cg %d blk %jd",
	    lastcg, (intmax_t)dtogd(fs, lastblk), firstcg,
	    (intmax_t)dtogd(fs, firstblk));
	return (&buf[0]);
}
	

static const char *indirname[UFS_NIADDR] = { "First", "Second", "Third" };

static void
printblk(fs, lbn, blkno, numfrags, lastlbn)
	struct fs *fs;
	ufs_lbn_t lbn;
	ufs2_daddr_t blkno;
	int numfrags;
	ufs_lbn_t lastlbn;
{
	static int seq;
	static ufs2_daddr_t totfrags, lastindirblk, lastblk, firstblk;

	if (lastlbn <= 0)
		goto flush;
	if (seq == 0) {
		seq = howmany(numfrags, fs->fs_frag);
		totfrags = numfrags;
		firstblk = blkno;
		return;
	}
	if (lbn == 0) {
		seq = howmany(numfrags, fs->fs_frag);
		totfrags = numfrags;
		lastblk = 0;
		firstblk = blkno;
		lastindirblk = 0;
		return;
	}
	if (lbn < lastlbn && ((firstblk == 0 && blkno == 0) ||
	    (firstblk == BLK_NOCOPY && blkno == BLK_NOCOPY) ||
	    (firstblk == BLK_SNAP && blkno == BLK_SNAP) ||
	    blkno == firstblk + seq * fs->fs_frag)) {
		seq += howmany(numfrags, fs->fs_frag);
		totfrags += numfrags;
		return;
	}
flush:
	if (seq == 0)
		goto prtindir;
	if (firstblk <= BLK_SNAP) {
		if (seq == 1)
			printf("\tlbn %jd %s\n", (intmax_t)(lbn - seq),
			    firstblk == 0 ? "hole" :
			    firstblk == BLK_NOCOPY ? "nocopy" :
			    "snapblk");
		else
			printf("\tlbn %jd-%jd %s\n",
			    (intmax_t)lbn - seq, (intmax_t)lbn - 1,
			    firstblk == 0 ? "hole" :
			    firstblk == BLK_NOCOPY ? "nocopy" :
			    "snapblk");
	} else if (seq == 1) {
		if (totfrags == 1)
			printf("\tlbn %jd blkno %jd%s\n", (intmax_t)(lbn - seq),
			   (intmax_t)firstblk, distance(fs, lastblk, firstblk));
		else
			printf("\tlbn %jd blkno %jd-%jd%s\n",
			    (intmax_t)(lbn - seq), (intmax_t)firstblk,
			    (intmax_t)(firstblk + totfrags - 1),
			    distance(fs, lastblk, firstblk));
		lastblk = firstblk + totfrags - 1;
	} else {
		printf("\tlbn %jd-%jd blkno %jd-%jd%s\n", (intmax_t)(lbn - seq),
		    (intmax_t)(lbn - 1), (intmax_t)firstblk,
		    (intmax_t)(firstblk + totfrags - 1),
		    distance(fs, lastblk, firstblk));
		lastblk = firstblk + totfrags - 1;
	}
	if (lastlbn > 0 || blkno == 0) {
		seq = 1;
		totfrags = numfrags;
		firstblk = blkno;
		return;
	}
prtindir:
	if (seq != 0 && (fs->fs_metaspace == 0 || lastindirblk == 0))
		lastindirblk = lastblk;
	printf("%s-level indirect, blkno %jd-%jd%s\n", indirname[-lastlbn],
	    (intmax_t)blkno, (intmax_t)(blkno + numfrags - 1),
	    distance(fs, lastindirblk, blkno));
	lastindirblk = blkno + numfrags - 1;
	if (fs->fs_metaspace == 0)
		lastblk = lastindirblk;
	seq = 0;
}

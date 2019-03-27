/*-
 * SPDX-License-Identifier: BSD-3-Clause AND BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libufs.h>
#include <strings.h>
#include <err.h>
#include <assert.h>

#include "fsck.h"

struct cgchain {
	union {
		struct cg cgcu_cg;
		char cgcu_buf[MAXBSIZE];
	} cgc_union;
	int	cgc_busy;
	int	cgc_dirty;
	LIST_ENTRY(cgchain) cgc_next;
};
#define cgc_cg	cgc_union.cgcu_cg

#define	MAX_CACHED_CGS	1024
static unsigned ncgs = 0;
static LIST_HEAD(, cgchain) cglist = LIST_HEAD_INITIALIZER(cglist);

static const char *devnam;
static struct uufsd *diskp = NULL;
static struct fs *fs = NULL;
struct ufs2_dinode ufs2_zino;

static void putcgs(void);

/*
 * Return cylinder group from the cache or load it if it is not in the
 * cache yet.
 * Don't cache more than MAX_CACHED_CGS cylinder groups.
 */
static struct cgchain *
getcg(int cg)
{
	struct cgchain *cgc;

	assert(diskp != NULL && fs != NULL);
	LIST_FOREACH(cgc, &cglist, cgc_next) {
		if (cgc->cgc_cg.cg_cgx == cg) {
			//printf("%s: Found cg=%d\n", __func__, cg);
			return (cgc);
		}
	}
	/*
	 * Our cache is full? Let's clean it up.
	 */
	if (ncgs >= MAX_CACHED_CGS) {
		//printf("%s: Flushing CGs.\n", __func__);
		putcgs();
	}
	cgc = malloc(sizeof(*cgc));
	if (cgc == NULL) {
		/*
		 * Cannot allocate memory?
		 * Let's put all currently loaded and not busy cylinder groups
		 * on disk and try again.
		 */
		//printf("%s: No memory, flushing CGs.\n", __func__);
		putcgs();
		cgc = malloc(sizeof(*cgc));
		if (cgc == NULL)
			err(1, "malloc(%zu)", sizeof(*cgc));
	}
	if (cgget(diskp, cg, &cgc->cgc_cg) == -1)
		err(1, "cgget(%d)", cg);
	cgc->cgc_busy = 0;
	cgc->cgc_dirty = 0;
	LIST_INSERT_HEAD(&cglist, cgc, cgc_next);
	ncgs++;
	//printf("%s: Read cg=%d\n", __func__, cg);
	return (cgc);
}

/*
 * Mark cylinder group as dirty - it will be written back on putcgs().
 */
static void
dirtycg(struct cgchain *cgc)
{

	cgc->cgc_dirty = 1;
}

/*
 * Mark cylinder group as busy - it will not be freed on putcgs().
 */
static void
busycg(struct cgchain *cgc)
{

	cgc->cgc_busy = 1;
}

/*
 * Unmark the given cylinder group as busy.
 */
static void
unbusycg(struct cgchain *cgc)
{

	cgc->cgc_busy = 0;
}

/*
 * Write back all dirty cylinder groups.
 * Free all non-busy cylinder groups.
 */
static void
putcgs(void)
{
	struct cgchain *cgc, *cgc2;

	assert(diskp != NULL && fs != NULL);
	LIST_FOREACH_SAFE(cgc, &cglist, cgc_next, cgc2) {
		if (cgc->cgc_busy)
			continue;
		LIST_REMOVE(cgc, cgc_next);
		ncgs--;
		if (cgc->cgc_dirty) {
			if (cgput(diskp, &cgc->cgc_cg) == -1)
				err(1, "cgput(%d)", cgc->cgc_cg.cg_cgx);
			//printf("%s: Wrote cg=%d\n", __func__,
			//    cgc->cgc_cg.cg_cgx);
		}
		free(cgc);
	}
}

#if 0
/*
 * Free all non-busy cylinder groups without storing the dirty ones.
 */
static void
cancelcgs(void)
{
	struct cgchain *cgc;

	assert(diskp != NULL && fs != NULL);
	while ((cgc = LIST_FIRST(&cglist)) != NULL) {
		if (cgc->cgc_busy)
			continue;
		LIST_REMOVE(cgc, cgc_next);
		//printf("%s: Canceled cg=%d\n", __func__, cgc->cgc_cg.cg_cgx);
		free(cgc);
	}
}
#endif

/*
 * Open the given provider, load superblock.
 */
static void
opendisk(void)
{
	if (diskp != NULL)
		return;
	diskp = &disk;
	if (ufs_disk_fillout(diskp, devnam) == -1) {
		err(1, "ufs_disk_fillout(%s) failed: %s", devnam,
		    diskp->d_error);
	}
	fs = &diskp->d_fs;
}

/*
 * Mark file system as clean, write the super-block back, close the disk.
 */
static void
closedisk(void)
{

	fs->fs_clean = 1;
	if (sbwrite(diskp, 0) == -1)
		err(1, "sbwrite(%s)", devnam);
	if (ufs_disk_close(diskp) == -1)
		err(1, "ufs_disk_close(%s)", devnam);
	free(diskp);
	diskp = NULL;
	fs = NULL;
}

static void
blkfree(ufs2_daddr_t bno, long size)
{
	struct cgchain *cgc;
	struct cg *cgp;
	ufs1_daddr_t fragno, cgbno;
	int i, cg, blk, frags, bbase;
	u_int8_t *blksfree;

	cg = dtog(fs, bno);
	cgc = getcg(cg);
	dirtycg(cgc);
	cgp = &cgc->cgc_cg;
	cgbno = dtogd(fs, bno);
	blksfree = cg_blksfree(cgp);
	if (size == fs->fs_bsize) {
		fragno = fragstoblks(fs, cgbno);
		if (!ffs_isfreeblock(fs, blksfree, fragno))
			assert(!"blkfree: freeing free block");
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
		for (i = 0; i < frags; i++) {
			if (isset(blksfree, cgbno + i))
				assert(!"blkfree: freeing free frag");
			setbit(blksfree, cgbno + i);
		}
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
}

/*
 * Recursively free all indirect blocks.
 */
static void
freeindir(ufs2_daddr_t blk, int level)
{
	char sblks[MAXBSIZE];
	ufs2_daddr_t *blks;
	int i;

	if (bread(diskp, fsbtodb(fs, blk), (void *)&sblks, (size_t)fs->fs_bsize) == -1)
		err(1, "bread: %s", diskp->d_error);
	blks = (ufs2_daddr_t *)&sblks;
	for (i = 0; i < NINDIR(fs); i++) {
		if (blks[i] == 0)
			break;
		if (level == 0)
			blkfree(blks[i], fs->fs_bsize);
		else
			freeindir(blks[i], level - 1);
	}
	blkfree(blk, fs->fs_bsize);
}

#define	dblksize(fs, dino, lbn) \
	((dino)->di_size >= smalllblktosize(fs, (lbn) + 1) \
	    ? (fs)->fs_bsize \
	    : fragroundup(fs, blkoff(fs, (dino)->di_size)))

/*
 * Free all blocks associated with the given inode.
 */
static void
clear_inode(struct ufs2_dinode *dino)
{
	ufs2_daddr_t bn;
	int extblocks, i, level;
	off_t osize;
	long bsize;

	extblocks = 0;
	if (fs->fs_magic == FS_UFS2_MAGIC && dino->di_extsize > 0)
		extblocks = btodb(fragroundup(fs, dino->di_extsize));
	/* deallocate external attributes blocks */
	if (extblocks > 0) {
		osize = dino->di_extsize;
		dino->di_blocks -= extblocks;
		dino->di_extsize = 0;
		for (i = 0; i < UFS_NXADDR; i++) {
			if (dino->di_extb[i] == 0)
				continue;
			blkfree(dino->di_extb[i], sblksize(fs, osize, i));
		}
	}
#define	SINGLE	0	/* index of single indirect block */
#define	DOUBLE	1	/* index of double indirect block */
#define	TRIPLE	2	/* index of triple indirect block */
	/* deallocate indirect blocks */
	for (level = SINGLE; level <= TRIPLE; level++) {
		if (dino->di_ib[level] == 0)
			break;
		freeindir(dino->di_ib[level], level);
	}
	/* deallocate direct blocks and fragments */
	for (i = 0; i < UFS_NDADDR; i++) {
		bn = dino->di_db[i];
		if (bn == 0)
			continue;
		bsize = dblksize(fs, dino, i);
		blkfree(bn, bsize);
	}
}

void
gjournal_check(const char *filesys)
{
	union dinodep dp;
	struct cgchain *cgc;
	struct cg *cgp;
	uint8_t *inosused;
	ino_t cino, ino;
	int cg;

	devnam = filesys;
	opendisk();
	/* Are there any unreferenced inodes in this file system? */
	if (fs->fs_unrefs == 0) {
		//printf("No unreferenced inodes.\n");
		closedisk();
		return;
	}

	for (cg = 0; cg < fs->fs_ncg; cg++) {
		/* Show progress if requested. */
		if (got_siginfo) {
			printf("%s: phase j: cyl group %d of %d (%d%%)\n",
			    cdevname, cg, fs->fs_ncg, cg * 100 / fs->fs_ncg);
			got_siginfo = 0;
		}
		if (got_sigalarm) {
			setproctitle("%s pj %d%%", cdevname,
			     cg * 100 / fs->fs_ncg);
			got_sigalarm = 0;
		}
		cgc = getcg(cg);
		cgp = &cgc->cgc_cg;
		/* Are there any unreferenced inodes in this cylinder group? */
		if (cgp->cg_unrefs == 0)
			continue;
		//printf("Analizing cylinder group %d (count=%d)\n", cg, cgp->cg_unrefs);
		/*
		 * We are going to modify this cylinder group, so we want it to
		 * be written back.
		 */
		dirtycg(cgc);
		/* We don't want it to be freed in the meantime. */
		busycg(cgc);
		inosused = cg_inosused(cgp);
		/*
		 * Now go through the list of all inodes in this cylinder group
		 * to find unreferenced ones.
		 */
		for (cino = 0; cino < fs->fs_ipg; cino++) {
			ino = fs->fs_ipg * cg + cino;
			/* Unallocated? Skip it. */
			if (isclr(inosused, cino))
				continue;
			if (getinode(diskp, &dp, ino) == -1)
				err(1, "getinode (cg=%d ino=%ju) %s",
				    cg, (uintmax_t)ino, diskp->d_error);
			/* Not a regular file nor directory? Skip it. */
			if (!S_ISREG(dp.dp2->di_mode) &&
			    !S_ISDIR(dp.dp2->di_mode))
				continue;
			/* Has reference(s)? Skip it. */
			if (dp.dp2->di_nlink > 0)
				continue;
			/* printf("Clearing inode=%d (size=%jd)\n", ino,
			    (intmax_t)dp.dp2->di_size); */
			/* Free inode's blocks. */
			clear_inode(dp.dp2);
			/* Deallocate it. */
			clrbit(inosused, cino);
			/* Update position of last used inode. */
			if (ino < cgp->cg_irotor)
				cgp->cg_irotor = ino;
			/* Update statistics. */
			cgp->cg_cs.cs_nifree++;
			fs->fs_cs(fs, cg).cs_nifree++;
			fs->fs_cstotal.cs_nifree++;
			cgp->cg_unrefs--;
			fs->fs_unrefs--;
			/* If this is directory, update related statistics. */
			if (S_ISDIR(dp.dp2->di_mode)) {
				cgp->cg_cs.cs_ndir--;
				fs->fs_cs(fs, cg).cs_ndir--;
				fs->fs_cstotal.cs_ndir--;
			}
			/* Zero-fill the inode. */
			*dp.dp2 = ufs2_zino;
			/* Write the inode back. */
			if (putinode(diskp) == -1)
				err(1, "putinode (cg=%d ino=%ju) %s",
				    cg, (uintmax_t)ino, diskp->d_error);
			if (cgp->cg_unrefs == 0) {
				//printf("No more unreferenced inodes in cg=%d.\n", cg);
				break;
			}
		}
		/*
		 * We don't need this cylinder group anymore, so feel free to
		 * free it if needed.
		 */
		unbusycg(cgc);
		/*
		 * If there are no more unreferenced inodes, there is no need to
		 * check other cylinder groups.
		 */
		if (fs->fs_unrefs == 0) {
			//printf("No more unreferenced inodes (cg=%d/%d).\n", cg,
			//    fs->fs_ncg);
			break;
		}
	}
	/* Write back modified cylinder groups. */
	putcgs();
	/* Write back updated statistics and super-block. */
	closedisk();
}

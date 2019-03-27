/*	$NetBSD: mkfs.c,v 1.22 2011/10/09 22:30:13 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program
 *
 * Copyright (c) 1980, 1989, 1993
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
#include <sys/time.h>
#include <sys/resource.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <util.h>

#include "makefs.h"
#include "ffs.h"

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include "ffs/ufs_bswap.h"
#include "ffs/ufs_inode.h"
#include "ffs/ffs_extern.h"
#include "ffs/newfs_extern.h"

#ifndef BBSIZE
#define	BBSIZE	8192			/* size of boot area, with label */
#endif

static void initcg(uint32_t, time_t, const fsinfo_t *);
static int ilog2(int);

static int count_digits(int);

/*
 * make file system for cylinder-group style file systems
 */
#define	UMASK		0755
#define	POWEROF2(num)	(((num) & ((num) - 1)) == 0)

static union {
	struct fs fs;
	char pad[SBLOCKSIZE];
} fsun;
#define	sblock	fsun.fs

static union {
	struct cg cg;
	char pad[FFS_MAXBSIZE];
} cgun;
#define	acg	cgun.cg

static char *iobuf;
static int iobufsize;

static char writebuf[FFS_MAXBSIZE];

static int     Oflag;	   /* format as an 4.3BSD file system */
static int64_t fssize;	   /* file system size */
static int     sectorsize;	   /* bytes/sector */
static int     fsize;	   /* fragment size */
static int     bsize;	   /* block size */
static int     maxbsize;   /* maximum clustering */
static int     maxblkspercg;
static int     minfree;	   /* free space threshold */
static int     opt;		   /* optimization preference (space or time) */
static int     density;	   /* number of bytes per inode */
static int     maxcontig;	   /* max contiguous blocks to allocate */
static int     maxbpg;	   /* maximum blocks per file in a cyl group */
static int     bbsize;	   /* boot block size */
static int     sbsize;	   /* superblock size */
static int     avgfilesize;	   /* expected average file size */
static int     avgfpdir;	   /* expected number of files per directory */

struct fs *
ffs_mkfs(const char *fsys, const fsinfo_t *fsopts, time_t tstamp)
{
	int fragsperinode, optimalfpg, origdensity, minfpg, lastminfpg;
	int32_t csfrags;
	uint32_t i, cylno;
	long long sizepb;
	void *space;
	int size;
	int nprintcols, printcolwidth;
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;

	Oflag =		ffs_opts->version;
	fssize =        fsopts->size / fsopts->sectorsize;
	sectorsize =    fsopts->sectorsize;
	fsize =         ffs_opts->fsize;
	bsize =         ffs_opts->bsize;
	maxbsize =      ffs_opts->maxbsize;
	maxblkspercg =  ffs_opts->maxblkspercg;
	minfree =       ffs_opts->minfree;
	opt =           ffs_opts->optimization;
	density =       ffs_opts->density;
	maxcontig =     ffs_opts->maxcontig;
	maxbpg =        ffs_opts->maxbpg;
	avgfilesize =   ffs_opts->avgfilesize;
	avgfpdir =      ffs_opts->avgfpdir;
	bbsize =        BBSIZE;
	sbsize =        SBLOCKSIZE;

	strlcpy(sblock.fs_volname, ffs_opts->label, sizeof(sblock.fs_volname));

	if (Oflag == 0) {
		sblock.fs_old_inodefmt = FS_42INODEFMT;
		sblock.fs_maxsymlinklen = 0;
		sblock.fs_old_flags = 0;
	} else {
		sblock.fs_old_inodefmt = FS_44INODEFMT;
		sblock.fs_maxsymlinklen = (Oflag == 1 ? UFS1_MAXSYMLINKLEN :
		    UFS2_MAXSYMLINKLEN);
		sblock.fs_old_flags = FS_FLAGS_UPDATED;
		sblock.fs_flags = 0;
	}
	/*
	 * Validate the given file system size.
	 * Verify that its last block can actually be accessed.
	 * Convert to file system fragment sized units.
	 */
	if (fssize <= 0) {
		printf("preposterous size %lld\n", (long long)fssize);
		exit(13);
	}
	ffs_wtfs(fssize - 1, sectorsize, (char *)&sblock, fsopts);

	/*
	 * collect and verify the filesystem density info
	 */
	sblock.fs_avgfilesize = avgfilesize;
	sblock.fs_avgfpdir = avgfpdir;
	if (sblock.fs_avgfilesize <= 0)
		printf("illegal expected average file size %d\n",
		    sblock.fs_avgfilesize), exit(14);
	if (sblock.fs_avgfpdir <= 0)
		printf("illegal expected number of files per directory %d\n",
		    sblock.fs_avgfpdir), exit(15);
	/*
	 * collect and verify the block and fragment sizes
	 */
	sblock.fs_bsize = bsize;
	sblock.fs_fsize = fsize;
	if (!POWEROF2(sblock.fs_bsize)) {
		printf("block size must be a power of 2, not %d\n",
		    sblock.fs_bsize);
		exit(16);
	}
	if (!POWEROF2(sblock.fs_fsize)) {
		printf("fragment size must be a power of 2, not %d\n",
		    sblock.fs_fsize);
		exit(17);
	}
	if (sblock.fs_fsize < sectorsize) {
		printf("fragment size %d is too small, minimum is %d\n",
		    sblock.fs_fsize, sectorsize);
		exit(18);
	}
	if (sblock.fs_bsize < MINBSIZE) {
		printf("block size %d is too small, minimum is %d\n",
		    sblock.fs_bsize, MINBSIZE);
		exit(19);
	}
	if (sblock.fs_bsize > FFS_MAXBSIZE) {
		printf("block size %d is too large, maximum is %d\n",
		    sblock.fs_bsize, FFS_MAXBSIZE);
		exit(19);
	}
	if (sblock.fs_bsize < sblock.fs_fsize) {
		printf("block size (%d) cannot be smaller than fragment size (%d)\n",
		    sblock.fs_bsize, sblock.fs_fsize);
		exit(20);
	}

	if (maxbsize < bsize || !POWEROF2(maxbsize)) {
		sblock.fs_maxbsize = sblock.fs_bsize;
		printf("Extent size set to %d\n", sblock.fs_maxbsize);
	} else if (sblock.fs_maxbsize > FS_MAXCONTIG * sblock.fs_bsize) {
		sblock.fs_maxbsize = FS_MAXCONTIG * sblock.fs_bsize;
		printf("Extent size reduced to %d\n", sblock.fs_maxbsize);
	} else {
		sblock.fs_maxbsize = maxbsize;
	}
	sblock.fs_maxcontig = maxcontig;
	if (sblock.fs_maxcontig < sblock.fs_maxbsize / sblock.fs_bsize) {
		sblock.fs_maxcontig = sblock.fs_maxbsize / sblock.fs_bsize;
		printf("Maxcontig raised to %d\n", sblock.fs_maxbsize);
	}

	if (sblock.fs_maxcontig > 1)
		sblock.fs_contigsumsize = MIN(sblock.fs_maxcontig,FS_MAXCONTIG);

	sblock.fs_bmask = ~(sblock.fs_bsize - 1);
	sblock.fs_fmask = ~(sblock.fs_fsize - 1);
	sblock.fs_qbmask = ~sblock.fs_bmask;
	sblock.fs_qfmask = ~sblock.fs_fmask;
	for (sblock.fs_bshift = 0, i = sblock.fs_bsize; i > 1; i >>= 1)
		sblock.fs_bshift++;
	for (sblock.fs_fshift = 0, i = sblock.fs_fsize; i > 1; i >>= 1)
		sblock.fs_fshift++;
	sblock.fs_frag = numfrags(&sblock, sblock.fs_bsize);
	for (sblock.fs_fragshift = 0, i = sblock.fs_frag; i > 1; i >>= 1)
		sblock.fs_fragshift++;
	if (sblock.fs_frag > MAXFRAG) {
		printf("fragment size %d is too small, "
			"minimum with block size %d is %d\n",
		    sblock.fs_fsize, sblock.fs_bsize,
		    sblock.fs_bsize / MAXFRAG);
		exit(21);
	}
	sblock.fs_fsbtodb = ilog2(sblock.fs_fsize / sectorsize);
	sblock.fs_size = sblock.fs_providersize = fssize =
	    dbtofsb(&sblock, fssize);

	if (Oflag <= 1) {
		sblock.fs_magic = FS_UFS1_MAGIC;
		sblock.fs_sblockloc = SBLOCK_UFS1;
		sblock.fs_nindir = sblock.fs_bsize / sizeof(ufs1_daddr_t);
		sblock.fs_inopb = sblock.fs_bsize / sizeof(struct ufs1_dinode);
		sblock.fs_maxsymlinklen = ((UFS_NDADDR + UFS_NIADDR) *
		    sizeof (ufs1_daddr_t));
		sblock.fs_old_inodefmt = FS_44INODEFMT;
		sblock.fs_old_cgoffset = 0;
		sblock.fs_old_cgmask = 0xffffffff;
		sblock.fs_old_size = sblock.fs_size;
		sblock.fs_old_rotdelay = 0;
		sblock.fs_old_rps = 60;
		sblock.fs_old_nspf = sblock.fs_fsize / sectorsize;
		sblock.fs_old_cpg = 1;
		sblock.fs_old_interleave = 1;
		sblock.fs_old_trackskew = 0;
		sblock.fs_old_cpc = 0;
		sblock.fs_old_postblformat = 1;
		sblock.fs_old_nrpos = 1;
	} else {
		sblock.fs_magic = FS_UFS2_MAGIC;
		sblock.fs_sblockloc = SBLOCK_UFS2;
		sblock.fs_nindir = sblock.fs_bsize / sizeof(ufs2_daddr_t);
		sblock.fs_inopb = sblock.fs_bsize / sizeof(struct ufs2_dinode);
		sblock.fs_maxsymlinklen = ((UFS_NDADDR + UFS_NIADDR) *
		    sizeof (ufs2_daddr_t));
		if (ffs_opts->softupdates == 1)
			sblock.fs_flags |= FS_DOSOFTDEP;
	}

	sblock.fs_sblkno =
	    roundup(howmany(sblock.fs_sblockloc + SBLOCKSIZE, sblock.fs_fsize),
		sblock.fs_frag);
	sblock.fs_cblkno = (daddr_t)(sblock.fs_sblkno +
	    roundup(howmany(SBLOCKSIZE, sblock.fs_fsize), sblock.fs_frag));
	sblock.fs_iblkno = sblock.fs_cblkno + sblock.fs_frag;
	sblock.fs_maxfilesize = sblock.fs_bsize * UFS_NDADDR - 1;
	for (sizepb = sblock.fs_bsize, i = 0; i < UFS_NIADDR; i++) {
		sizepb *= NINDIR(&sblock);
		sblock.fs_maxfilesize += sizepb;
	}

	/*
	 * Calculate the number of blocks to put into each cylinder group.
	 *
	 * This algorithm selects the number of blocks per cylinder
	 * group. The first goal is to have at least enough data blocks
	 * in each cylinder group to meet the density requirement. Once
	 * this goal is achieved we try to expand to have at least
	 * 1 cylinder group. Once this goal is achieved, we pack as
	 * many blocks into each cylinder group map as will fit.
	 *
	 * We start by calculating the smallest number of blocks that we
	 * can put into each cylinder group. If this is too big, we reduce
	 * the density until it fits.
	 */
	origdensity = density;
	for (;;) {
		fragsperinode = MAX(numfrags(&sblock, density), 1);
		minfpg = fragsperinode * INOPB(&sblock);
		if (minfpg > sblock.fs_size)
			minfpg = sblock.fs_size;
		sblock.fs_ipg = INOPB(&sblock);
		sblock.fs_fpg = roundup(sblock.fs_iblkno +
		    sblock.fs_ipg / INOPF(&sblock), sblock.fs_frag);
		if (sblock.fs_fpg < minfpg)
			sblock.fs_fpg = minfpg;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		sblock.fs_fpg = roundup(sblock.fs_iblkno +
		    sblock.fs_ipg / INOPF(&sblock), sblock.fs_frag);
		if (sblock.fs_fpg < minfpg)
			sblock.fs_fpg = minfpg;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		if (CGSIZE(&sblock) < (unsigned long)sblock.fs_bsize)
			break;
		density -= sblock.fs_fsize;
	}
	if (density != origdensity)
		printf("density reduced from %d to %d\n", origdensity, density);

	if (maxblkspercg <= 0 || maxblkspercg >= fssize)
		maxblkspercg = fssize - 1;
	/*
	 * Start packing more blocks into the cylinder group until
	 * it cannot grow any larger, the number of cylinder groups
	 * drops below 1, or we reach the size requested.
	 */
	for ( ; sblock.fs_fpg < maxblkspercg; sblock.fs_fpg += sblock.fs_frag) {
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		if (sblock.fs_size / sblock.fs_fpg < 1)
			break;
		if (CGSIZE(&sblock) < (unsigned long)sblock.fs_bsize)
			continue;
		if (CGSIZE(&sblock) == (unsigned long)sblock.fs_bsize)
			break;
		sblock.fs_fpg -= sblock.fs_frag;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		break;
	}
	/*
	 * Check to be sure that the last cylinder group has enough blocks
	 * to be viable. If it is too small, reduce the number of blocks
	 * per cylinder group which will have the effect of moving more
	 * blocks into the last cylinder group.
	 */
	optimalfpg = sblock.fs_fpg;
	for (;;) {
		sblock.fs_ncg = howmany(sblock.fs_size, sblock.fs_fpg);
		lastminfpg = roundup(sblock.fs_iblkno +
		    sblock.fs_ipg / INOPF(&sblock), sblock.fs_frag);
		if (sblock.fs_size < lastminfpg) {
			printf("Filesystem size %lld < minimum size of %d\n",
			    (long long)sblock.fs_size, lastminfpg);
			exit(28);
		}
		if (sblock.fs_size % sblock.fs_fpg >= lastminfpg ||
		    sblock.fs_size % sblock.fs_fpg == 0)
			break;
		sblock.fs_fpg -= sblock.fs_frag;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
	}
	if (optimalfpg != sblock.fs_fpg)
		printf("Reduced frags per cylinder group from %d to %d %s\n",
		   optimalfpg, sblock.fs_fpg, "to enlarge last cyl group");
	sblock.fs_cgsize = fragroundup(&sblock, CGSIZE(&sblock));
	sblock.fs_dblkno = sblock.fs_iblkno + sblock.fs_ipg / INOPF(&sblock);
	if (Oflag <= 1) {
		sblock.fs_old_spc = sblock.fs_fpg * sblock.fs_old_nspf;
		sblock.fs_old_nsect = sblock.fs_old_spc;
		sblock.fs_old_npsect = sblock.fs_old_spc;
		sblock.fs_old_ncyl = sblock.fs_ncg;
	}

	/*
	 * fill in remaining fields of the super block
	 */
	sblock.fs_csaddr = cgdmin(&sblock, 0);
	sblock.fs_cssize =
	    fragroundup(&sblock, sblock.fs_ncg * sizeof(struct csum));

	/*
	 * Setup memory for temporary in-core cylgroup summaries.
	 * Cribbed from ffs_mountfs().
	 */
	size = sblock.fs_cssize;
	if (sblock.fs_contigsumsize > 0)
		size += sblock.fs_ncg * sizeof(int32_t);
	space = ecalloc(1, size);
	sblock.fs_csp = space;
	space = (char *)space + sblock.fs_cssize;
	if (sblock.fs_contigsumsize > 0) {
		int32_t *lp;

		sblock.fs_maxcluster = lp = space;
		for (i = 0; i < sblock.fs_ncg; i++)
		*lp++ = sblock.fs_contigsumsize;
	}

	sblock.fs_sbsize = fragroundup(&sblock, sizeof(struct fs));
	if (sblock.fs_sbsize > SBLOCKSIZE)
		sblock.fs_sbsize = SBLOCKSIZE;
	sblock.fs_minfree = minfree;
	sblock.fs_maxcontig = maxcontig;
	sblock.fs_maxbpg = maxbpg;
	sblock.fs_optim = opt;
	sblock.fs_cgrotor = 0;
	sblock.fs_pendingblocks = 0;
	sblock.fs_pendinginodes = 0;
	sblock.fs_cstotal.cs_ndir = 0;
	sblock.fs_cstotal.cs_nbfree = 0;
	sblock.fs_cstotal.cs_nifree = 0;
	sblock.fs_cstotal.cs_nffree = 0;
	sblock.fs_fmod = 0;
	sblock.fs_ronly = 0;
	sblock.fs_state = 0;
	sblock.fs_clean = FS_ISCLEAN;
	sblock.fs_ronly = 0;
	sblock.fs_id[0] = tstamp;
	sblock.fs_id[1] = random();
	sblock.fs_fsmnt[0] = '\0';
	csfrags = howmany(sblock.fs_cssize, sblock.fs_fsize);
	sblock.fs_dsize = sblock.fs_size - sblock.fs_sblkno -
	    sblock.fs_ncg * (sblock.fs_dblkno - sblock.fs_sblkno);
	sblock.fs_cstotal.cs_nbfree =
	    fragstoblks(&sblock, sblock.fs_dsize) -
	    howmany(csfrags, sblock.fs_frag);
	sblock.fs_cstotal.cs_nffree =
	    fragnum(&sblock, sblock.fs_size) +
	    (fragnum(&sblock, csfrags) > 0 ?
	    sblock.fs_frag - fragnum(&sblock, csfrags) : 0);
	sblock.fs_cstotal.cs_nifree =
	    sblock.fs_ncg * sblock.fs_ipg - UFS_ROOTINO;
	sblock.fs_cstotal.cs_ndir = 0;
	sblock.fs_dsize -= csfrags;
	sblock.fs_time = tstamp;
	if (Oflag <= 1) {
		sblock.fs_old_time = tstamp;
		sblock.fs_old_dsize = sblock.fs_dsize;
		sblock.fs_old_csaddr = sblock.fs_csaddr;
		sblock.fs_old_cstotal.cs_ndir = sblock.fs_cstotal.cs_ndir;
		sblock.fs_old_cstotal.cs_nbfree = sblock.fs_cstotal.cs_nbfree;
		sblock.fs_old_cstotal.cs_nifree = sblock.fs_cstotal.cs_nifree;
		sblock.fs_old_cstotal.cs_nffree = sblock.fs_cstotal.cs_nffree;
	}
	/*
	 * Dump out summary information about file system.
	 */
#define	B2MBFACTOR (1 / (1024.0 * 1024.0))
	printf("%s: %.1fMB (%lld sectors) block size %d, "
	       "fragment size %d\n",
	    fsys, (float)sblock.fs_size * sblock.fs_fsize * B2MBFACTOR,
	    (long long)fsbtodb(&sblock, sblock.fs_size),
	    sblock.fs_bsize, sblock.fs_fsize);
	printf("\tusing %d cylinder groups of %.2fMB, %d blks, "
	       "%d inodes.\n",
	    sblock.fs_ncg,
	    (float)sblock.fs_fpg * sblock.fs_fsize * B2MBFACTOR,
	    sblock.fs_fpg / sblock.fs_frag, sblock.fs_ipg);
#undef B2MBFACTOR
	/*
	 * Now determine how wide each column will be, and calculate how
	 * many columns will fit in a 76 char line. 76 is the width of the
	 * subwindows in sysinst.
	 */
	printcolwidth = count_digits(
			fsbtodb(&sblock, cgsblock(&sblock, sblock.fs_ncg -1)));
	nprintcols = 76 / (printcolwidth + 2);

	/*
	 * allocate space for superblock, cylinder group map, and
	 * two sets of inode blocks.
	 */
	if (sblock.fs_bsize < SBLOCKSIZE)
		iobufsize = SBLOCKSIZE + 3 * sblock.fs_bsize;
	else
		iobufsize = 4 * sblock.fs_bsize;
	iobuf = ecalloc(1, iobufsize);
	/*
	 * Make a copy of the superblock into the buffer that we will be
	 * writing out in each cylinder group.
	 */
	memcpy(writebuf, &sblock, sbsize);
	if (fsopts->needswap)
		ffs_sb_swap(&sblock, (struct fs*)writebuf);
	memcpy(iobuf, writebuf, SBLOCKSIZE);

	printf("super-block backups (for fsck -b #) at:");
	for (cylno = 0; cylno < sblock.fs_ncg; cylno++) {
		initcg(cylno, tstamp, fsopts);
		if (cylno % nprintcols == 0)
			printf("\n");
		printf(" %*lld,", printcolwidth,
			(long long)fsbtodb(&sblock, cgsblock(&sblock, cylno)));
		fflush(stdout);
	}
	printf("\n");

	/*
	 * Now construct the initial file system,
	 * then write out the super-block.
	 */
	sblock.fs_time = tstamp;
	if (Oflag <= 1) {
		sblock.fs_old_cstotal.cs_ndir = sblock.fs_cstotal.cs_ndir;
		sblock.fs_old_cstotal.cs_nbfree = sblock.fs_cstotal.cs_nbfree;
		sblock.fs_old_cstotal.cs_nifree = sblock.fs_cstotal.cs_nifree;
		sblock.fs_old_cstotal.cs_nffree = sblock.fs_cstotal.cs_nffree;
	}
	if (fsopts->needswap)
		sblock.fs_flags |= FS_SWAPPED;
	ffs_write_superblock(&sblock, fsopts);
	return (&sblock);
}

/*
 * Write out the superblock and its duplicates,
 * and the cylinder group summaries
 */
void
ffs_write_superblock(struct fs *fs, const fsinfo_t *fsopts)
{
	int size, blks, i, saveflag;
	uint32_t cylno;
	void *space;
	char *wrbuf;

	saveflag = fs->fs_flags & FS_INTERNAL;
	fs->fs_flags &= ~FS_INTERNAL;

        memcpy(writebuf, &sblock, sbsize);
	if (fsopts->needswap)
		ffs_sb_swap(fs, (struct fs*)writebuf);
	ffs_wtfs(fs->fs_sblockloc / sectorsize, sbsize, writebuf, fsopts);

	/* Write out the duplicate super blocks */
	for (cylno = 0; cylno < fs->fs_ncg; cylno++)
		ffs_wtfs(fsbtodb(fs, cgsblock(fs, cylno)),
		    sbsize, writebuf, fsopts);

	/* Write out the cylinder group summaries */
	size = fs->fs_cssize;
	blks = howmany(size, fs->fs_fsize);
	space = (void *)fs->fs_csp;
	wrbuf = emalloc(size);
	for (i = 0; i < blks; i+= fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > blks)
			size = (blks - i) * fs->fs_fsize;
		if (fsopts->needswap)
			ffs_csum_swap((struct csum *)space,
			    (struct csum *)wrbuf, size);
		else
			memcpy(wrbuf, space, (u_int)size);
		ffs_wtfs(fsbtodb(fs, fs->fs_csaddr + i), size, wrbuf, fsopts);
		space = (char *)space + size;
	}
	free(wrbuf);
	fs->fs_flags |= saveflag;
}

/*
 * Initialize a cylinder group.
 */
static void
initcg(uint32_t cylno, time_t utime, const fsinfo_t *fsopts)
{
	daddr_t cbase, dmax;
	int32_t blkno;
	uint32_t i, j, d, dlower, dupper;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;
	int start;

	/*
	 * Determine block bounds for cylinder group.
	 * Allow space for super block summary information in first
	 * cylinder group.
	 */
	cbase = cgbase(&sblock, cylno);
	dmax = cbase + sblock.fs_fpg;
	if (dmax > sblock.fs_size)
		dmax = sblock.fs_size;
	dlower = cgsblock(&sblock, cylno) - cbase;
	dupper = cgdmin(&sblock, cylno) - cbase;
	if (cylno == 0)
		dupper += howmany(sblock.fs_cssize, sblock.fs_fsize);
	memset(&acg, 0, sblock.fs_cgsize);
	acg.cg_time = utime;
	acg.cg_magic = CG_MAGIC;
	acg.cg_cgx = cylno;
	acg.cg_niblk = sblock.fs_ipg;
	acg.cg_initediblk = MIN(sblock.fs_ipg, 2 * INOPB(&sblock));
	acg.cg_ndblk = dmax - cbase;
	if (sblock.fs_contigsumsize > 0)
		acg.cg_nclusterblks = acg.cg_ndblk >> sblock.fs_fragshift;
	start = &acg.cg_space[0] - (u_char *)(&acg.cg_firstfield);
	if (Oflag == 2) {
		acg.cg_iusedoff = start;
	} else {
		if (cylno == sblock.fs_ncg - 1)
			acg.cg_old_ncyl = howmany(acg.cg_ndblk,
			    sblock.fs_fpg / sblock.fs_old_cpg);
		else
			acg.cg_old_ncyl = sblock.fs_old_cpg;
		acg.cg_old_time = acg.cg_time;
		acg.cg_time = 0;
		acg.cg_old_niblk = acg.cg_niblk;
		acg.cg_niblk = 0;
		acg.cg_initediblk = 0;
		acg.cg_old_btotoff = start;
		acg.cg_old_boff = acg.cg_old_btotoff +
		    sblock.fs_old_cpg * sizeof(int32_t);
		acg.cg_iusedoff = acg.cg_old_boff +
		    sblock.fs_old_cpg * sizeof(u_int16_t);
	}
	acg.cg_freeoff = acg.cg_iusedoff + howmany(sblock.fs_ipg, CHAR_BIT);
	if (sblock.fs_contigsumsize <= 0) {
		acg.cg_nextfreeoff = acg.cg_freeoff +
		   howmany(sblock.fs_fpg, CHAR_BIT);
	} else {
		acg.cg_clustersumoff = acg.cg_freeoff +
		    howmany(sblock.fs_fpg, CHAR_BIT) - sizeof(int32_t);
		acg.cg_clustersumoff =
		    roundup(acg.cg_clustersumoff, sizeof(int32_t));
		acg.cg_clusteroff = acg.cg_clustersumoff +
		    (sblock.fs_contigsumsize + 1) * sizeof(int32_t);
		acg.cg_nextfreeoff = acg.cg_clusteroff +
		    howmany(fragstoblks(&sblock, sblock.fs_fpg), CHAR_BIT);
	}
	if (acg.cg_nextfreeoff > (uint32_t)sblock.fs_cgsize) {
		printf("Panic: cylinder group too big\n");
		exit(37);
	}
	acg.cg_cs.cs_nifree += sblock.fs_ipg;
	if (cylno == 0)
		for (i = 0; i < UFS_ROOTINO; i++) {
			setbit(cg_inosused_swap(&acg, 0), i);
			acg.cg_cs.cs_nifree--;
		}
	if (cylno > 0) {
		/*
		 * In cylno 0, beginning space is reserved
		 * for boot and super blocks.
		 */
		for (d = 0, blkno = 0; d < dlower;) {
			ffs_setblock(&sblock, cg_blksfree_swap(&acg, 0), blkno);
			if (sblock.fs_contigsumsize > 0)
				setbit(cg_clustersfree_swap(&acg, 0), blkno);
			acg.cg_cs.cs_nbfree++;
			d += sblock.fs_frag;
			blkno++;
		}
	}
	if ((i = (dupper & (sblock.fs_frag - 1))) != 0) {
		acg.cg_frsum[sblock.fs_frag - i]++;
		for (d = dupper + sblock.fs_frag - i; dupper < d; dupper++) {
			setbit(cg_blksfree_swap(&acg, 0), dupper);
			acg.cg_cs.cs_nffree++;
		}
	}
	for (d = dupper, blkno = dupper >> sblock.fs_fragshift;
	     d + sblock.fs_frag <= acg.cg_ndblk; ) {
		ffs_setblock(&sblock, cg_blksfree_swap(&acg, 0), blkno);
		if (sblock.fs_contigsumsize > 0)
			setbit(cg_clustersfree_swap(&acg, 0), blkno);
		acg.cg_cs.cs_nbfree++;
		d += sblock.fs_frag;
		blkno++;
	}
	if (d < acg.cg_ndblk) {
		acg.cg_frsum[acg.cg_ndblk - d]++;
		for (; d < acg.cg_ndblk; d++) {
			setbit(cg_blksfree_swap(&acg, 0), d);
			acg.cg_cs.cs_nffree++;
		}
	}
	if (sblock.fs_contigsumsize > 0) {
		int32_t *sump = cg_clustersum_swap(&acg, 0);
		u_char *mapp = cg_clustersfree_swap(&acg, 0);
		int map = *mapp++;
		int bit = 1;
		int run = 0;

		for (i = 0; i < acg.cg_nclusterblks; i++) {
			if ((map & bit) != 0) {
				run++;
			} else if (run != 0) {
				if (run > sblock.fs_contigsumsize)
					run = sblock.fs_contigsumsize;
				sump[run]++;
				run = 0;
			}
			if ((i & (CHAR_BIT - 1)) != (CHAR_BIT - 1)) {
				bit <<= 1;
			} else {
				map = *mapp++;
				bit = 1;
			}
		}
		if (run != 0) {
			if (run > sblock.fs_contigsumsize)
				run = sblock.fs_contigsumsize;
			sump[run]++;
		}
	}
	sblock.fs_cs(&sblock, cylno) = acg.cg_cs;
	/*
	 * Write out the duplicate super block, the cylinder group map
	 * and two blocks worth of inodes in a single write.
	 */
	start = MAX(sblock.fs_bsize, SBLOCKSIZE);
	memcpy(&iobuf[start], &acg, sblock.fs_cgsize);
	if (fsopts->needswap)
		ffs_cg_swap(&acg, (struct cg*)&iobuf[start], &sblock);
	start += sblock.fs_bsize;
	dp1 = (struct ufs1_dinode *)(&iobuf[start]);
	dp2 = (struct ufs2_dinode *)(&iobuf[start]);
	for (i = 0; i < acg.cg_initediblk; i++) {
		if (sblock.fs_magic == FS_UFS1_MAGIC) {
			/* No need to swap, it'll stay random */
			dp1->di_gen = random();
			dp1++;
		} else {
			dp2->di_gen = random();
			dp2++;
		}
	}
	ffs_wtfs(fsbtodb(&sblock, cgsblock(&sblock, cylno)), iobufsize, iobuf,
	    fsopts);
	/*
	 * For the old file system, we have to initialize all the inodes.
	 */
	if (Oflag <= 1) {
		for (i = 2 * sblock.fs_frag;
		     i < sblock.fs_ipg / INOPF(&sblock);
		     i += sblock.fs_frag) {
			dp1 = (struct ufs1_dinode *)(&iobuf[start]);
			for (j = 0; j < INOPB(&sblock); j++) {
				dp1->di_gen = random();
				dp1++;
			}
			ffs_wtfs(fsbtodb(&sblock, cgimin(&sblock, cylno) + i),
			    sblock.fs_bsize, &iobuf[start], fsopts);
		}
	}
}

/*
 * read a block from the file system
 */
void
ffs_rdfs(daddr_t bno, int size, void *bf, const fsinfo_t *fsopts)
{
	int n;
	off_t offset;

	offset = bno * fsopts->sectorsize + fsopts->offset;
	if (lseek(fsopts->fd, offset, SEEK_SET) < 0)
		err(1, "%s: seek error for sector %lld", __func__,
		    (long long)bno);
	n = read(fsopts->fd, bf, size);
	if (n == -1) {
		abort();
		err(1, "%s: read error bno %lld size %d", __func__,
		    (long long)bno, size);
	}
	else if (n != size)
		errx(1, "%s: read error for sector %lld", __func__,
		    (long long)bno);
}

/*
 * write a block to the file system
 */
void
ffs_wtfs(daddr_t bno, int size, void *bf, const fsinfo_t *fsopts)
{
	int n;
	off_t offset;

	offset = bno * fsopts->sectorsize + fsopts->offset;
	if (lseek(fsopts->fd, offset, SEEK_SET) < 0)
		err(1, "%s: seek error for sector %lld", __func__,
		    (long long)bno);
	n = write(fsopts->fd, bf, size);
	if (n == -1)
		err(1, "%s: write error for sector %lld", __func__,
		    (long long)bno);
	else if (n != size)
		errx(1, "%s: write error for sector %lld", __func__,
		    (long long)bno);
}


/* Determine how many digits are needed to print a given integer */
static int
count_digits(int num)
{
	int ndig;

	for(ndig = 1; num > 9; num /=10, ndig++);

	return (ndig);
}

static int
ilog2(int val)
{
	u_int n;

	for (n = 0; n < sizeof(n) * CHAR_BIT; n++)
		if (1 << n == val)
			return (n);
	errx(1, "%s: %d is not a power of 2", __func__, val);
}

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
 * research program.
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

#if 0
#ifndef lint
static char sccsid[] = "@(#)mkfs.c	8.11 (Berkeley) 5/3/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define	IN_RTLD			/* So we pickup the P_OSREL defines */
#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <err.h>
#include <grp.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>
#include "newfs.h"

/*
 * make file system for cylinder-group style file systems
 */
#define UMASK		0755
#define POWEROF2(num)	(((num) & ((num) - 1)) == 0)

static struct	csum *fscs;
#define	sblock	disk.d_fs
#define	acg	disk.d_cg

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};
#define DIP(dp, field) \
	((sblock.fs_magic == FS_UFS1_MAGIC) ? \
	(dp)->dp1.field : (dp)->dp2.field)

static caddr_t iobuf;
static long iobufsize;
static ufs2_daddr_t alloc(int size, int mode);
static int charsperline(void);
static void clrblock(struct fs *, unsigned char *, int);
static void fsinit(time_t);
static int ilog2(int);
static void initcg(int, time_t);
static int isblock(struct fs *, unsigned char *, int);
static void iput(union dinode *, ino_t);
static int makedir(struct direct *, int);
static void setblock(struct fs *, unsigned char *, int);
static void wtfs(ufs2_daddr_t, int, char *);
static u_int32_t newfs_random(void);

void
mkfs(struct partition *pp, char *fsys)
{
	int fragsperinode, optimalfpg, origdensity, minfpg, lastminfpg;
	long i, j, csfrags;
	uint cg;
	time_t utime;
	quad_t sizepb;
	int width;
	ino_t maxinum;
	int minfragsperinode;	/* minimum ratio of frags to inodes */
	char tmpbuf[100];	/* XXX this will break in about 2,500 years */
	struct fsrecovery *fsr;
	char *fsrbuf;
	union {
		struct fs fdummy;
		char cdummy[SBLOCKSIZE];
	} dummy;
#define fsdummy dummy.fdummy
#define chdummy dummy.cdummy

	/*
	 * Our blocks == sector size, and the version of UFS we are using is
	 * specified by Oflag.
	 */
	disk.d_bsize = sectorsize;
	disk.d_ufs = Oflag;
	if (Rflag)
		utime = 1000000000;
	else
		time(&utime);
	sblock.fs_old_flags = FS_FLAGS_UPDATED;
	sblock.fs_flags = 0;
	if (Uflag)
		sblock.fs_flags |= FS_DOSOFTDEP;
	if (Lflag)
		strlcpy(sblock.fs_volname, volumelabel, MAXVOLLEN);
	if (Jflag)
		sblock.fs_flags |= FS_GJOURNAL;
	if (lflag)
		sblock.fs_flags |= FS_MULTILABEL;
	if (tflag)
		sblock.fs_flags |= FS_TRIM;
	/*
	 * Validate the given file system size.
	 * Verify that its last block can actually be accessed.
	 * Convert to file system fragment sized units.
	 */
	if (fssize <= 0) {
		printf("preposterous size %jd\n", (intmax_t)fssize);
		exit(13);
	}
	wtfs(fssize - (realsectorsize / DEV_BSIZE), realsectorsize,
	    (char *)&sblock);
	/*
	 * collect and verify the file system density info
	 */
	sblock.fs_avgfilesize = avgfilesize;
	sblock.fs_avgfpdir = avgfilesperdir;
	if (sblock.fs_avgfilesize <= 0)
		printf("illegal expected average file size %d\n",
		    sblock.fs_avgfilesize), exit(14);
	if (sblock.fs_avgfpdir <= 0)
		printf("illegal expected number of files per directory %d\n",
		    sblock.fs_avgfpdir), exit(15);

restart:
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
		printf("increasing fragment size from %d to sector size (%d)\n",
		    sblock.fs_fsize, sectorsize);
		sblock.fs_fsize = sectorsize;
	}
	if (sblock.fs_bsize > MAXBSIZE) {
		printf("decreasing block size from %d to maximum (%d)\n",
		    sblock.fs_bsize, MAXBSIZE);
		sblock.fs_bsize = MAXBSIZE;
	}
	if (sblock.fs_bsize < MINBSIZE) {
		printf("increasing block size from %d to minimum (%d)\n",
		    sblock.fs_bsize, MINBSIZE);
		sblock.fs_bsize = MINBSIZE;
	}
	if (sblock.fs_fsize > MAXBSIZE) {
		printf("decreasing fragment size from %d to maximum (%d)\n",
		    sblock.fs_fsize, MAXBSIZE);
		sblock.fs_fsize = MAXBSIZE;
	}
	if (sblock.fs_bsize < sblock.fs_fsize) {
		printf("increasing block size from %d to fragment size (%d)\n",
		    sblock.fs_bsize, sblock.fs_fsize);
		sblock.fs_bsize = sblock.fs_fsize;
	}
	if (sblock.fs_fsize * MAXFRAG < sblock.fs_bsize) {
		printf(
		"increasing fragment size from %d to block size / %d (%d)\n",
		    sblock.fs_fsize, MAXFRAG, sblock.fs_bsize / MAXFRAG);
		sblock.fs_fsize = sblock.fs_bsize / MAXFRAG;
	}
	if (maxbsize == 0)
		maxbsize = bsize;
	if (maxbsize < bsize || !POWEROF2(maxbsize)) {
		sblock.fs_maxbsize = sblock.fs_bsize;
		printf("Extent size set to %d\n", sblock.fs_maxbsize);
	} else if (sblock.fs_maxbsize > FS_MAXCONTIG * sblock.fs_bsize) {
		sblock.fs_maxbsize = FS_MAXCONTIG * sblock.fs_bsize;
		printf("Extent size reduced to %d\n", sblock.fs_maxbsize);
	} else {
		sblock.fs_maxbsize = maxbsize;
	}
	/*
	 * Maxcontig sets the default for the maximum number of blocks
	 * that may be allocated sequentially. With file system clustering
	 * it is possible to allocate contiguous blocks up to the maximum
	 * transfer size permitted by the controller or buffering.
	 */
	if (maxcontig == 0)
		maxcontig = MAX(1, MAXPHYS / bsize);
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
	sblock.fs_bshift = ilog2(sblock.fs_bsize);
	sblock.fs_fshift = ilog2(sblock.fs_fsize);
	sblock.fs_frag = numfrags(&sblock, sblock.fs_bsize);
	sblock.fs_fragshift = ilog2(sblock.fs_frag);
	if (sblock.fs_frag > MAXFRAG) {
		printf("fragment size %d is still too small (can't happen)\n",
		    sblock.fs_bsize / MAXFRAG);
		exit(21);
	}
	sblock.fs_fsbtodb = ilog2(sblock.fs_fsize / sectorsize);
	sblock.fs_size = fssize = dbtofsb(&sblock, fssize);
	sblock.fs_providersize = dbtofsb(&sblock, mediasize / sectorsize);

	/*
	 * Before the filesystem is finally initialized, mark it
	 * as incompletely initialized.
	 */
	sblock.fs_magic = FS_BAD_MAGIC;

	if (Oflag == 1) {
		sblock.fs_sblockloc = SBLOCK_UFS1;
		sblock.fs_sblockactualloc = SBLOCK_UFS1;
		sblock.fs_nindir = sblock.fs_bsize / sizeof(ufs1_daddr_t);
		sblock.fs_inopb = sblock.fs_bsize / sizeof(struct ufs1_dinode);
		sblock.fs_maxsymlinklen = ((UFS_NDADDR + UFS_NIADDR) *
		    sizeof(ufs1_daddr_t));
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
		sblock.fs_sblockloc = SBLOCK_UFS2;
		sblock.fs_sblockactualloc = SBLOCK_UFS2;
		sblock.fs_nindir = sblock.fs_bsize / sizeof(ufs2_daddr_t);
		sblock.fs_inopb = sblock.fs_bsize / sizeof(struct ufs2_dinode);
		sblock.fs_maxsymlinklen = ((UFS_NDADDR + UFS_NIADDR) *
		    sizeof(ufs2_daddr_t));
	}
	sblock.fs_sblkno =
	    roundup(howmany(sblock.fs_sblockloc + SBLOCKSIZE, sblock.fs_fsize),
		sblock.fs_frag);
	sblock.fs_cblkno = sblock.fs_sblkno +
	    roundup(howmany(SBLOCKSIZE, sblock.fs_fsize), sblock.fs_frag);
	sblock.fs_iblkno = sblock.fs_cblkno + sblock.fs_frag;
	sblock.fs_maxfilesize = sblock.fs_bsize * UFS_NDADDR - 1;
	for (sizepb = sblock.fs_bsize, i = 0; i < UFS_NIADDR; i++) {
		sizepb *= NINDIR(&sblock);
		sblock.fs_maxfilesize += sizepb;
	}

	/*
	 * It's impossible to create a snapshot in case that fs_maxfilesize
	 * is smaller than the fssize.
	 */
	if (sblock.fs_maxfilesize < (u_quad_t)fssize) {
		warnx("WARNING: You will be unable to create snapshots on this "
		      "file system.  Correct by using a larger blocksize.");
	}

	/*
	 * Calculate the number of blocks to put into each cylinder group.
	 *
	 * This algorithm selects the number of blocks per cylinder
	 * group. The first goal is to have at least enough data blocks
	 * in each cylinder group to meet the density requirement. Once
	 * this goal is achieved we try to expand to have at least
	 * MINCYLGRPS cylinder groups. Once this goal is achieved, we
	 * pack as many blocks into each cylinder group map as will fit.
	 *
	 * We start by calculating the smallest number of blocks that we
	 * can put into each cylinder group. If this is too big, we reduce
	 * the density until it fits.
	 */
	maxinum = (((int64_t)(1)) << 32) - INOPB(&sblock);
	minfragsperinode = 1 + fssize / maxinum;
	if (density == 0) {
		density = MAX(NFPI, minfragsperinode) * fsize;
	} else if (density < minfragsperinode * fsize) {
		origdensity = density;
		density = minfragsperinode * fsize;
		fprintf(stderr, "density increased from %d to %d\n",
		    origdensity, density);
	}
	origdensity = density;
	for (;;) {
		fragsperinode = MAX(numfrags(&sblock, density), 1);
		if (fragsperinode < minfragsperinode) {
			bsize <<= 1;
			fsize <<= 1;
			printf("Block size too small for a file system %s %d\n",
			     "of this size. Increasing blocksize to", bsize);
			goto restart;
		}
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
	/*
	 * Start packing more blocks into the cylinder group until
	 * it cannot grow any larger, the number of cylinder groups
	 * drops below MINCYLGRPS, or we reach the size requested.
	 * For UFS1 inodes per cylinder group are stored in an int16_t
	 * so fs_ipg is limited to 2^15 - 1.
	 */
	for ( ; sblock.fs_fpg < maxblkspercg; sblock.fs_fpg += sblock.fs_frag) {
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		if (Oflag > 1 || (Oflag == 1 && sblock.fs_ipg <= 0x7fff)) {
			if (sblock.fs_size / sblock.fs_fpg < MINCYLGRPS)
				break;
			if (CGSIZE(&sblock) < (unsigned long)sblock.fs_bsize)
				continue;
			if (CGSIZE(&sblock) == (unsigned long)sblock.fs_bsize)
				break;
		}
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
			printf("Filesystem size %jd < minimum size of %d\n",
			    (intmax_t)sblock.fs_size, lastminfpg);
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
	if (Oflag == 1) {
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
	fscs = (struct csum *)calloc(1, sblock.fs_cssize);
	if (fscs == NULL)
		errx(31, "calloc failed");
	sblock.fs_sbsize = fragroundup(&sblock, sizeof(struct fs));
	if (sblock.fs_sbsize > SBLOCKSIZE)
		sblock.fs_sbsize = SBLOCKSIZE;
	if (sblock.fs_sbsize < realsectorsize)
		sblock.fs_sbsize = realsectorsize;
	sblock.fs_minfree = minfree;
	if (metaspace > 0 && metaspace < sblock.fs_fpg / 2)
		sblock.fs_metaspace = blknum(&sblock, metaspace);
	else if (metaspace != -1)
		/* reserve half of minfree for metadata blocks */
		sblock.fs_metaspace = blknum(&sblock,
		    (sblock.fs_fpg * minfree) / 200);
	if (maxbpg == 0)
		sblock.fs_maxbpg = MAXBLKPG(sblock.fs_bsize);
	else
		sblock.fs_maxbpg = maxbpg;
	sblock.fs_optim = opt;
	sblock.fs_cgrotor = 0;
	sblock.fs_pendingblocks = 0;
	sblock.fs_pendinginodes = 0;
	sblock.fs_fmod = 0;
	sblock.fs_ronly = 0;
	sblock.fs_state = 0;
	sblock.fs_clean = 1;
	sblock.fs_id[0] = (long)utime;
	sblock.fs_id[1] = newfs_random();
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
	sblock.fs_time = utime;
	if (Oflag == 1) {
		sblock.fs_old_time = utime;
		sblock.fs_old_dsize = sblock.fs_dsize;
		sblock.fs_old_csaddr = sblock.fs_csaddr;
		sblock.fs_old_cstotal.cs_ndir = sblock.fs_cstotal.cs_ndir;
		sblock.fs_old_cstotal.cs_nbfree = sblock.fs_cstotal.cs_nbfree;
		sblock.fs_old_cstotal.cs_nifree = sblock.fs_cstotal.cs_nifree;
		sblock.fs_old_cstotal.cs_nffree = sblock.fs_cstotal.cs_nffree;
	}
	/*
	 * Set flags for metadata that is being check-hashed.
	 *
	 * Metadata check hashes are not supported in the UFS version 1
	 * filesystem to keep it as small and simple as possible.
	 */
	if (Oflag > 1) {
		sblock.fs_flags |= FS_METACKHASH;
		if (getosreldate() >= P_OSREL_CK_CYLGRP)
			sblock.fs_metackhash |= CK_CYLGRP;
		if (getosreldate() >= P_OSREL_CK_SUPERBLOCK)
			sblock.fs_metackhash |= CK_SUPERBLOCK;
		if (getosreldate() >= P_OSREL_CK_INODE)
			sblock.fs_metackhash |= CK_INODE;
	}

	/*
	 * Dump out summary information about file system.
	 */
#	define B2MBFACTOR (1 / (1024.0 * 1024.0))
	printf("%s: %.1fMB (%jd sectors) block size %d, fragment size %d\n",
	    fsys, (float)sblock.fs_size * sblock.fs_fsize * B2MBFACTOR,
	    (intmax_t)fsbtodb(&sblock, sblock.fs_size), sblock.fs_bsize,
	    sblock.fs_fsize);
	printf("\tusing %d cylinder groups of %.2fMB, %d blks, %d inodes.\n",
	    sblock.fs_ncg, (float)sblock.fs_fpg * sblock.fs_fsize * B2MBFACTOR,
	    sblock.fs_fpg / sblock.fs_frag, sblock.fs_ipg);
	if (sblock.fs_flags & FS_DOSOFTDEP)
		printf("\twith soft updates\n");
#	undef B2MBFACTOR

	if (Eflag && !Nflag) {
		printf("Erasing sectors [%jd...%jd]\n", 
		    sblock.fs_sblockloc / disk.d_bsize,
		    fsbtodb(&sblock, sblock.fs_size) - 1);
		berase(&disk, sblock.fs_sblockloc / disk.d_bsize,
		    sblock.fs_size * sblock.fs_fsize - sblock.fs_sblockloc);
	}
	/*
	 * Wipe out old UFS1 superblock(s) if necessary.
	 */
	if (!Nflag && Oflag != 1 && realsectorsize <= SBLOCK_UFS1) {
		i = bread(&disk, part_ofs + SBLOCK_UFS1 / disk.d_bsize, chdummy,
		    SBLOCKSIZE);
		if (i == -1)
			err(1, "can't read old UFS1 superblock: %s",
			    disk.d_error);

		if (fsdummy.fs_magic == FS_UFS1_MAGIC) {
			fsdummy.fs_magic = 0;
			bwrite(&disk, part_ofs + SBLOCK_UFS1 / disk.d_bsize,
			    chdummy, SBLOCKSIZE);
			for (cg = 0; cg < fsdummy.fs_ncg; cg++) {
				if (fsbtodb(&fsdummy, cgsblock(&fsdummy, cg)) >
				    fssize)
					break;
				bwrite(&disk, part_ofs + fsbtodb(&fsdummy,
				  cgsblock(&fsdummy, cg)), chdummy, SBLOCKSIZE);
			}
		}
	}
	if (!Nflag && sbput(disk.d_fd, &disk.d_fs, 0) != 0)
		err(1, "sbput: %s", disk.d_error);
	if (Xflag == 1) {
		printf("** Exiting on Xflag 1\n");
		exit(0);
	}
	if (Xflag == 2)
		printf("** Leaving BAD MAGIC on Xflag 2\n");
	else
		sblock.fs_magic = (Oflag != 1) ? FS_UFS2_MAGIC : FS_UFS1_MAGIC;

	/*
	 * Now build the cylinders group blocks and
	 * then print out indices of cylinder groups.
	 */
	printf("super-block backups (for fsck_ffs -b #) at:\n");
	i = 0;
	width = charsperline();
	/*
	 * Allocate space for two sets of inode blocks.
	 */
	iobufsize = 2 * sblock.fs_bsize;
	if ((iobuf = calloc(1, iobufsize)) == 0) {
		printf("Cannot allocate I/O buffer\n");
		exit(38);
	}
	/*
	 * Write out all the cylinder groups and backup superblocks.
	 */
	for (cg = 0; cg < sblock.fs_ncg; cg++) {
		if (!Nflag)
			initcg(cg, utime);
		j = snprintf(tmpbuf, sizeof(tmpbuf), " %jd%s",
		    (intmax_t)fsbtodb(&sblock, cgsblock(&sblock, cg)),
		    cg < (sblock.fs_ncg-1) ? "," : "");
		if (j < 0)
			tmpbuf[j = 0] = '\0';
		if (i + j >= width) {
			printf("\n");
			i = 0;
		}
		i += j;
		printf("%s", tmpbuf);
		fflush(stdout);
	}
	printf("\n");
	if (Nflag)
		exit(0);
	/*
	 * Now construct the initial file system,
	 * then write out the super-block.
	 */
	fsinit(utime);
	if (Oflag == 1) {
		sblock.fs_old_cstotal.cs_ndir = sblock.fs_cstotal.cs_ndir;
		sblock.fs_old_cstotal.cs_nbfree = sblock.fs_cstotal.cs_nbfree;
		sblock.fs_old_cstotal.cs_nifree = sblock.fs_cstotal.cs_nifree;
		sblock.fs_old_cstotal.cs_nffree = sblock.fs_cstotal.cs_nffree;
	}
	if (Xflag == 3) {
		printf("** Exiting on Xflag 3\n");
		exit(0);
	}
	/*
	 * Reference the summary information so it will also be written.
	 */
	sblock.fs_csp = fscs;
	if (sbput(disk.d_fd, &disk.d_fs, 0) != 0)
		err(1, "sbput: %s", disk.d_error);
	/*
	 * For UFS1 filesystems with a blocksize of 64K, the first
	 * alternate superblock resides at the location used for
	 * the default UFS2 superblock. As there is a valid
	 * superblock at this location, the boot code will use
	 * it as its first choice. Thus we have to ensure that
	 * all of its statistcs on usage are correct.
	 */
	if (Oflag == 1 && sblock.fs_bsize == 65536)
		wtfs(fsbtodb(&sblock, cgsblock(&sblock, 0)),
		    sblock.fs_bsize, (char *)&sblock);
	/*
	 * Read the last sector of the boot block, replace the last
	 * 20 bytes with the recovery information, then write it back.
	 * The recovery information only works for UFS2 filesystems.
	 */
	if (sblock.fs_magic == FS_UFS2_MAGIC) {
		if ((fsrbuf = malloc(realsectorsize)) == NULL || bread(&disk,
		    part_ofs + (SBLOCK_UFS2 - realsectorsize) / disk.d_bsize,
		    fsrbuf, realsectorsize) == -1)
			err(1, "can't read recovery area: %s", disk.d_error);
		fsr =
		    (struct fsrecovery *)&fsrbuf[realsectorsize - sizeof *fsr];
		fsr->fsr_magic = sblock.fs_magic;
		fsr->fsr_fpg = sblock.fs_fpg;
		fsr->fsr_fsbtodb = sblock.fs_fsbtodb;
		fsr->fsr_sblkno = sblock.fs_sblkno;
		fsr->fsr_ncg = sblock.fs_ncg;
		wtfs((SBLOCK_UFS2 - realsectorsize) / disk.d_bsize,
		    realsectorsize, fsrbuf);
		free(fsrbuf);
	}
	/*
	 * Update information about this partition in pack
	 * label, to that it may be updated on disk.
	 */
	if (pp != NULL) {
		pp->p_fstype = FS_BSDFFS;
		pp->p_fsize = sblock.fs_fsize;
		pp->p_frag = sblock.fs_frag;
		pp->p_cpg = sblock.fs_fpg;
	}
}

/*
 * Initialize a cylinder group.
 */
void
initcg(int cylno, time_t utime)
{
	long blkno, start;
	off_t savedactualloc;
	uint i, j, d, dlower, dupper;
	ufs2_daddr_t cbase, dmax;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;
	struct csum *cs;

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
	cs = &fscs[cylno];
	memset(&acg, 0, sblock.fs_cgsize);
	acg.cg_time = utime;
	acg.cg_magic = CG_MAGIC;
	acg.cg_cgx = cylno;
	acg.cg_niblk = sblock.fs_ipg;
	acg.cg_initediblk = MIN(sblock.fs_ipg, 2 * INOPB(&sblock));
	acg.cg_ndblk = dmax - cbase;
	if (sblock.fs_contigsumsize > 0)
		acg.cg_nclusterblks = acg.cg_ndblk / sblock.fs_frag;
	start = &acg.cg_space[0] - (u_char *)(&acg.cg_firstfield);
	if (Oflag == 2) {
		acg.cg_iusedoff = start;
	} else {
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
	acg.cg_nextfreeoff = acg.cg_freeoff + howmany(sblock.fs_fpg, CHAR_BIT);
	if (sblock.fs_contigsumsize > 0) {
		acg.cg_clustersumoff =
		    roundup(acg.cg_nextfreeoff, sizeof(u_int32_t));
		acg.cg_clustersumoff -= sizeof(u_int32_t);
		acg.cg_clusteroff = acg.cg_clustersumoff +
		    (sblock.fs_contigsumsize + 1) * sizeof(u_int32_t);
		acg.cg_nextfreeoff = acg.cg_clusteroff +
		    howmany(fragstoblks(&sblock, sblock.fs_fpg), CHAR_BIT);
	}
	if (acg.cg_nextfreeoff > (unsigned)sblock.fs_cgsize) {
		printf("Panic: cylinder group too big\n");
		exit(37);
	}
	acg.cg_cs.cs_nifree += sblock.fs_ipg;
	if (cylno == 0)
		for (i = 0; i < (long)UFS_ROOTINO; i++) {
			setbit(cg_inosused(&acg), i);
			acg.cg_cs.cs_nifree--;
		}
	if (cylno > 0) {
		/*
		 * In cylno 0, beginning space is reserved
		 * for boot and super blocks.
		 */
		for (d = 0; d < dlower; d += sblock.fs_frag) {
			blkno = d / sblock.fs_frag;
			setblock(&sblock, cg_blksfree(&acg), blkno);
			if (sblock.fs_contigsumsize > 0)
				setbit(cg_clustersfree(&acg), blkno);
			acg.cg_cs.cs_nbfree++;
		}
	}
	if ((i = dupper % sblock.fs_frag)) {
		acg.cg_frsum[sblock.fs_frag - i]++;
		for (d = dupper + sblock.fs_frag - i; dupper < d; dupper++) {
			setbit(cg_blksfree(&acg), dupper);
			acg.cg_cs.cs_nffree++;
		}
	}
	for (d = dupper; d + sblock.fs_frag <= acg.cg_ndblk;
	     d += sblock.fs_frag) {
		blkno = d / sblock.fs_frag;
		setblock(&sblock, cg_blksfree(&acg), blkno);
		if (sblock.fs_contigsumsize > 0)
			setbit(cg_clustersfree(&acg), blkno);
		acg.cg_cs.cs_nbfree++;
	}
	if (d < acg.cg_ndblk) {
		acg.cg_frsum[acg.cg_ndblk - d]++;
		for (; d < acg.cg_ndblk; d++) {
			setbit(cg_blksfree(&acg), d);
			acg.cg_cs.cs_nffree++;
		}
	}
	if (sblock.fs_contigsumsize > 0) {
		int32_t *sump = cg_clustersum(&acg);
		u_char *mapp = cg_clustersfree(&acg);
		int map = *mapp++;
		int bit = 1;
		int run = 0;

		for (i = 0; i < acg.cg_nclusterblks; i++) {
			if ((map & bit) != 0)
				run++;
			else if (run != 0) {
				if (run > sblock.fs_contigsumsize)
					run = sblock.fs_contigsumsize;
				sump[run]++;
				run = 0;
			}
			if ((i & (CHAR_BIT - 1)) != CHAR_BIT - 1)
				bit <<= 1;
			else {
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
	*cs = acg.cg_cs;
	/*
	 * Write out the duplicate super block. Then write the cylinder
	 * group map and two blocks worth of inodes in a single write.
	 */
	savedactualloc = sblock.fs_sblockactualloc;
	sblock.fs_sblockactualloc =
	    dbtob(fsbtodb(&sblock, cgsblock(&sblock, cylno)));
	if (sbput(disk.d_fd, &disk.d_fs, 0) != 0)
		err(1, "sbput: %s", disk.d_error);
	sblock.fs_sblockactualloc = savedactualloc;
	if (cgput(&disk, &acg) != 0)
		err(1, "initcg: cgput: %s", disk.d_error);
	start = 0;
	dp1 = (struct ufs1_dinode *)(&iobuf[start]);
	dp2 = (struct ufs2_dinode *)(&iobuf[start]);
	for (i = 0; i < acg.cg_initediblk; i++) {
		if (sblock.fs_magic == FS_UFS1_MAGIC) {
			dp1->di_gen = newfs_random();
			dp1++;
		} else {
			dp2->di_gen = newfs_random();
			dp2++;
		}
	}
	wtfs(fsbtodb(&sblock, cgimin(&sblock, cylno)), iobufsize, iobuf);
	/*
	 * For the old file system, we have to initialize all the inodes.
	 */
	if (Oflag == 1) {
		for (i = 2 * sblock.fs_frag;
		     i < sblock.fs_ipg / INOPF(&sblock);
		     i += sblock.fs_frag) {
			dp1 = (struct ufs1_dinode *)(&iobuf[start]);
			for (j = 0; j < INOPB(&sblock); j++) {
				dp1->di_gen = newfs_random();
				dp1++;
			}
			wtfs(fsbtodb(&sblock, cgimin(&sblock, cylno) + i),
			    sblock.fs_bsize, &iobuf[start]);
		}
	}
}

/*
 * initialize the file system
 */
#define ROOTLINKCNT 3

static struct direct root_dir[] = {
	{ UFS_ROOTINO, sizeof(struct direct), DT_DIR, 1, "." },
	{ UFS_ROOTINO, sizeof(struct direct), DT_DIR, 2, ".." },
	{ UFS_ROOTINO + 1, sizeof(struct direct), DT_DIR, 5, ".snap" },
};

#define SNAPLINKCNT 2

static struct direct snap_dir[] = {
	{ UFS_ROOTINO + 1, sizeof(struct direct), DT_DIR, 1, "." },
	{ UFS_ROOTINO, sizeof(struct direct), DT_DIR, 2, ".." },
};

void
fsinit(time_t utime)
{
	union dinode node;
	struct group *grp;
	gid_t gid;
	int entries;

	memset(&node, 0, sizeof node);
	if ((grp = getgrnam("operator")) != NULL) {
		gid = grp->gr_gid;
	} else {
		warnx("Cannot retrieve operator gid, using gid 0.");
		gid = 0;
	}
	entries = (nflag) ? ROOTLINKCNT - 1: ROOTLINKCNT;
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		/*
		 * initialize the node
		 */
		node.dp1.di_atime = utime;
		node.dp1.di_mtime = utime;
		node.dp1.di_ctime = utime;
		/*
		 * create the root directory
		 */
		node.dp1.di_mode = IFDIR | UMASK;
		node.dp1.di_nlink = entries;
		node.dp1.di_size = makedir(root_dir, entries);
		node.dp1.di_db[0] = alloc(sblock.fs_fsize, node.dp1.di_mode);
		node.dp1.di_blocks =
		    btodb(fragroundup(&sblock, node.dp1.di_size));
		wtfs(fsbtodb(&sblock, node.dp1.di_db[0]), sblock.fs_fsize,
		    iobuf);
		iput(&node, UFS_ROOTINO);
		if (!nflag) {
			/*
			 * create the .snap directory
			 */
			node.dp1.di_mode |= 020;
			node.dp1.di_gid = gid;
			node.dp1.di_nlink = SNAPLINKCNT;
			node.dp1.di_size = makedir(snap_dir, SNAPLINKCNT);
				node.dp1.di_db[0] =
				    alloc(sblock.fs_fsize, node.dp1.di_mode);
			node.dp1.di_blocks =
			    btodb(fragroundup(&sblock, node.dp1.di_size));
				wtfs(fsbtodb(&sblock, node.dp1.di_db[0]),
				    sblock.fs_fsize, iobuf);
			iput(&node, UFS_ROOTINO + 1);
		}
	} else {
		/*
		 * initialize the node
		 */
		node.dp2.di_atime = utime;
		node.dp2.di_mtime = utime;
		node.dp2.di_ctime = utime;
		node.dp2.di_birthtime = utime;
		/*
		 * create the root directory
		 */
		node.dp2.di_mode = IFDIR | UMASK;
		node.dp2.di_nlink = entries;
		node.dp2.di_size = makedir(root_dir, entries);
		node.dp2.di_db[0] = alloc(sblock.fs_fsize, node.dp2.di_mode);
		node.dp2.di_blocks =
		    btodb(fragroundup(&sblock, node.dp2.di_size));
		wtfs(fsbtodb(&sblock, node.dp2.di_db[0]), sblock.fs_fsize,
		    iobuf);
		iput(&node, UFS_ROOTINO);
		if (!nflag) {
			/*
			 * create the .snap directory
			 */
			node.dp2.di_mode |= 020;
			node.dp2.di_gid = gid;
			node.dp2.di_nlink = SNAPLINKCNT;
			node.dp2.di_size = makedir(snap_dir, SNAPLINKCNT);
				node.dp2.di_db[0] =
				    alloc(sblock.fs_fsize, node.dp2.di_mode);
			node.dp2.di_blocks =
			    btodb(fragroundup(&sblock, node.dp2.di_size));
				wtfs(fsbtodb(&sblock, node.dp2.di_db[0]), 
				    sblock.fs_fsize, iobuf);
			iput(&node, UFS_ROOTINO + 1);
		}
	}
}

/*
 * construct a set of directory entries in "iobuf".
 * return size of directory.
 */
int
makedir(struct direct *protodir, int entries)
{
	char *cp;
	int i, spcleft;

	spcleft = DIRBLKSIZ;
	memset(iobuf, 0, DIRBLKSIZ);
	for (cp = iobuf, i = 0; i < entries - 1; i++) {
		protodir[i].d_reclen = DIRSIZ(0, &protodir[i]);
		memmove(cp, &protodir[i], protodir[i].d_reclen);
		cp += protodir[i].d_reclen;
		spcleft -= protodir[i].d_reclen;
	}
	protodir[i].d_reclen = spcleft;
	memmove(cp, &protodir[i], DIRSIZ(0, &protodir[i]));
	return (DIRBLKSIZ);
}

/*
 * allocate a block or frag
 */
ufs2_daddr_t
alloc(int size, int mode)
{
	int i, blkno, frag;
	uint d;

	bread(&disk, part_ofs + fsbtodb(&sblock, cgtod(&sblock, 0)), (char *)&acg,
	    sblock.fs_cgsize);
	if (acg.cg_magic != CG_MAGIC) {
		printf("cg 0: bad magic number\n");
		exit(38);
	}
	if (acg.cg_cs.cs_nbfree == 0) {
		printf("first cylinder group ran out of space\n");
		exit(39);
	}
	for (d = 0; d < acg.cg_ndblk; d += sblock.fs_frag)
		if (isblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag))
			goto goth;
	printf("internal error: can't find block in cyl 0\n");
	exit(40);
goth:
	blkno = fragstoblks(&sblock, d);
	clrblock(&sblock, cg_blksfree(&acg), blkno);
	if (sblock.fs_contigsumsize > 0)
		clrbit(cg_clustersfree(&acg), blkno);
	acg.cg_cs.cs_nbfree--;
	sblock.fs_cstotal.cs_nbfree--;
	fscs[0].cs_nbfree--;
	if (mode & IFDIR) {
		acg.cg_cs.cs_ndir++;
		sblock.fs_cstotal.cs_ndir++;
		fscs[0].cs_ndir++;
	}
	if (size != sblock.fs_bsize) {
		frag = howmany(size, sblock.fs_fsize);
		fscs[0].cs_nffree += sblock.fs_frag - frag;
		sblock.fs_cstotal.cs_nffree += sblock.fs_frag - frag;
		acg.cg_cs.cs_nffree += sblock.fs_frag - frag;
		acg.cg_frsum[sblock.fs_frag - frag]++;
		for (i = frag; i < sblock.fs_frag; i++)
			setbit(cg_blksfree(&acg), d + i);
	}
	if (cgput(&disk, &acg) != 0)
		err(1, "alloc: cgput: %s", disk.d_error);
	return ((ufs2_daddr_t)d);
}

/*
 * Allocate an inode on the disk
 */
void
iput(union dinode *ip, ino_t ino)
{
	union dinodep dp;

	bread(&disk, part_ofs + fsbtodb(&sblock, cgtod(&sblock, 0)), (char *)&acg,
	    sblock.fs_cgsize);
	if (acg.cg_magic != CG_MAGIC) {
		printf("cg 0: bad magic number\n");
		exit(31);
	}
	acg.cg_cs.cs_nifree--;
	setbit(cg_inosused(&acg), ino);
	if (cgput(&disk, &acg) != 0)
		err(1, "iput: cgput: %s", disk.d_error);
	sblock.fs_cstotal.cs_nifree--;
	fscs[0].cs_nifree--;
	if (getinode(&disk, &dp, ino) == -1) {
		printf("iput: %s\n", disk.d_error);
		exit(32);
	}
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		*dp.dp1 = ip->dp1;
	else
		*dp.dp2 = ip->dp2;
	putinode(&disk);
}

/*
 * possibly write to disk
 */
static void
wtfs(ufs2_daddr_t bno, int size, char *bf)
{
	if (Nflag)
		return;
	if (bwrite(&disk, part_ofs + bno, bf, size) < 0)
		err(36, "wtfs: %d bytes at sector %jd", size, (intmax_t)bno);
}

/*
 * check if a block is available
 */
static int
isblock(struct fs *fs, unsigned char *cp, int h)
{
	unsigned char mask;

	switch (fs->fs_frag) {
	case 8:
		return (cp[h] == 0xff);
	case 4:
		mask = 0x0f << ((h & 0x1) << 2);
		return ((cp[h >> 1] & mask) == mask);
	case 2:
		mask = 0x03 << ((h & 0x3) << 1);
		return ((cp[h >> 2] & mask) == mask);
	case 1:
		mask = 0x01 << (h & 0x7);
		return ((cp[h >> 3] & mask) == mask);
	default:
		fprintf(stderr, "isblock bad fs_frag %d\n", fs->fs_frag);
		return (0);
	}
}

/*
 * take a block out of the map
 */
static void
clrblock(struct fs *fs, unsigned char *cp, int h)
{
	switch ((fs)->fs_frag) {
	case 8:
		cp[h] = 0;
		return;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		return;
	default:
		fprintf(stderr, "clrblock bad fs_frag %d\n", fs->fs_frag);
		return;
	}
}

/*
 * put a block into the map
 */
static void
setblock(struct fs *fs, unsigned char *cp, int h)
{
	switch (fs->fs_frag) {
	case 8:
		cp[h] = 0xff;
		return;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		return;
	default:
		fprintf(stderr, "setblock bad fs_frag %d\n", fs->fs_frag);
		return;
	}
}

/*
 * Determine the number of characters in a
 * single line.
 */

static int
charsperline(void)
{
	int columns;
	char *cp;
	struct winsize ws;

	columns = 0;
	if (ioctl(0, TIOCGWINSZ, &ws) != -1)
		columns = ws.ws_col;
	if (columns == 0 && (cp = getenv("COLUMNS")))
		columns = atoi(cp);
	if (columns == 0)
		columns = 80;	/* last resort */
	return (columns);
}

static int
ilog2(int val)
{
	u_int n;

	for (n = 0; n < sizeof(n) * CHAR_BIT; n++)
		if (1 << n == val)
			return (n);
	errx(1, "ilog2: %d is not a power of 2\n", val);
}

/*
 * For the regression test, return predictable random values.
 * Otherwise use a true random number generator.
 */
static u_int32_t
newfs_random(void)
{
	static int nextnum = 1;

	if (Rflag)
		return (nextnum++);
	return (arc4random());
}

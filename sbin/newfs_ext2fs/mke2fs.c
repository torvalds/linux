/* $OpenBSD: mke2fs.c,v 1.18 2019/07/01 07:17:26 kevlo Exp $ */
/*	$NetBSD: mke2fs.c,v 1.13 2009/10/19 18:41:08 bouyer Exp $	*/

/*-
 * Copyright (c) 2007 Izumi Tsutsui.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
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

/*
 * Copyright (c) 1997 Manuel Bouyer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * mke2fs.c: "re-invent (dumb but non-GPLed) wheel as a fun project"
 *
 *	In spite of this name, there is no piece of code
 *	derived from GPLed e2fsprogs written for Linux.
 *	I referred them only to see how each structure
 *	member should be initialized.
 *
 * Reference:
 *	- All NetBSD sources under src/sys/ufs/ext2fs and src/sbin/fsck_ext2fs
 *	- Ext2fs Home Page
 *		http://e2fsprogs.sourceforge.net/ext2.html
 *	- Design and Implementation of the Second Extended Filesystem
 *		http://e2fsprogs.sourceforge.net/ext2intro.html
 *	- Linux Documentation "The Second Extended Filesystem"
 *		src/linux/Documentation/filesystems/ext2.txt
 *		    in the Linux kernel distribution
 */

#include <sys/param.h>	/* MAXBSIZE powerof2 roundup setbit isset MIN */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <ufs/ext2fs/ext2fs_dinode.h>
#include <ufs/ext2fs/ext2fs_dir.h>
#include <ufs/ext2fs/ext2fs.h>
#include <sys/ioctl.h>

#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "extern.h"

static void initcg(uint);
static void zap_old_sblock(daddr32_t);
static uint cgoverhead(uint);
static int fsinit(const struct timeval *);
static int makedir(struct ext2fs_direct *, int);
static void copy_dir(struct ext2fs_direct *, struct ext2fs_direct *);
static void init_resizeino(const struct timeval *);
static uint32_t alloc(uint32_t, uint16_t);
static void iput(struct ext2fs_dinode *, ino_t);
static void rdfs(daddr32_t, int, void *);
static void wtfs(daddr32_t, int, void *);
static int ilog2(uint);
static int skpc(int, size_t, uint8_t *);
static void uuid_get(struct m_ext2fs *);

/* XXX: some of these macro should be into <ufs/ext2fs/ext2fs.h>? */
#define EXT2_DEF_MAX_MNT_COUNT	20
#define EXT2_DEF_FSCKINTV	(180 * 24 * 60 * 60)	/* 180 days */
#define EXT2_RESERVED_INODES	(EXT2_FIRSTINO - 1)
#define EXT2_UMASK		0755

#define EXT2_INO_INDEX(ino)	((ino) - 1)		/* no inode zero */

#define EXT2_LOSTFOUNDSIZE	16384
#define EXT2_LOSTFOUNDINO	EXT2_FIRSTINO		/* XXX: not quite */
#define EXT2_LOSTFOUNDUMASK	0700

#define EXT2_RESIZEINOUMASK	0600

#define NBLOCK_SUPERBLOCK	1
#define NBLOCK_BLOCK_BITMAP	1
#define NBLOCK_INODE_BITMAP	1

#define cgbase(fs, c)	\
	((fs)->e2fs.e2fs_first_dblock + (fs)->e2fs.e2fs_bpg * (c))

#define       rounddown(x,y)  (((x)/(y))*(y))

/*
 * ext2fs super block and group descriptor structures
 *
 *   We don't have to use or setup whole in-memory m_ext2fs structure,
 *   but prepare it to use several macro defined in kernel headers.
 */
union {
	struct m_ext2fs m_ext2fs;
	char pad[SBSIZE];
} ext2fsun;
#define sblock	ext2fsun.m_ext2fs
#define gd	ext2fsun.m_ext2fs.e2fs_gd

static uint8_t *iobuf;		/* for superblock and group descriptors */
static int iobufsize;

static uint8_t buf[MAXBSIZE];	/* for initcg() and makedir() ops */

static int fd;

extern int max_cols;

void
mke2fs(const char *fsys, int f)
{
	struct timeval tv;
	int64_t minfssize;
	uint bcount, fbcount, ficount;
	uint blocks_gd, blocks_per_cg, inodes_per_cg, iblocks_per_cg;
	uint minblocks_per_cg, blocks_lastcg;
	uint ncg, cylno, sboff;
	int i, len, col, delta, fld_width;

	gettimeofday(&tv, NULL);
	fd = f;

	/*
	 * collect and verify the block and fragment sizes
	 */
	if (!powerof2(bsize)) {
		errx(EXIT_FAILURE,
		    "block size must be a power of 2, not %u\n",
		    bsize);
	}
	if (!powerof2(fsize)) {
		errx(EXIT_FAILURE,
		    "fragment size must be a power of 2, not %u\n",
		    fsize);
	}
	if (fsize < sectorsize) {
		errx(EXIT_FAILURE,
		    "fragment size %u is too small, minimum is %u\n",
		    fsize, sectorsize);
	}
	if (bsize < MINBSIZE) {
		errx(EXIT_FAILURE,
		    "block size %u is too small, minimum is %u\n",
		    bsize, MINBSIZE);
	}
	if (bsize > EXT2_MAXBSIZE) {
		errx(EXIT_FAILURE,
		    "block size %u is too large, maximum is %u\n",
		    bsize, MAXBSIZE);
	}
	if (bsize != fsize) {
		/*
		 * There is no fragment support on current ext2fs (yet?),
		 * but some kernel code refers fsize or fpg as bsize or bpg
		 * and Linux seems to set the same values to them.
		 */
		errx(EXIT_FAILURE,
		    "block size (%u) can't be different from "
		    "fragment size (%u)\n",
		    bsize, fsize);
	}

	/* variable inodesize is REV1 feature */
	if (Oflag == 0 && inodesize != EXT2_REV0_DINODE_SIZE) {
		errx(EXIT_FAILURE, "GOOD_OLD_REV file system format"
		    " doesn't support %d byte inode\n", inodesize);
	}

	sblock.e2fs.e2fs_log_bsize = ilog2(bsize) - LOG_MINBSIZE;
	sblock.e2fs.e2fs_log_fsize = ilog2(fsize) - LOG_MINFSIZE;

	sblock.e2fs_bsize = bsize;
	sblock.e2fs_fsize = fsize;
	sblock.e2fs_bshift = sblock.e2fs.e2fs_log_bsize + LOG_MINBSIZE;
	sblock.e2fs_qbmask = sblock.e2fs_bsize - 1;
	sblock.e2fs_bmask = ~sblock.e2fs_qbmask;
	sblock.e2fs_fsbtodb = ilog2(sblock.e2fs_bsize) - ilog2(sectorsize);
	sblock.e2fs_ipb = sblock.e2fs_bsize / inodesize;

	/*
	 * Ext2fs preserves BBSIZE (1024 bytes) space at the top for
	 * bootloader (though it is not enough at all for our bootloader).
	 * If bsize == BBSIZE we have to preserve one block.
	 * If bsize > BBSIZE, the first block already contains BBSIZE space
	 * before superblock because superblock is allocated at SBOFF and
	 * bsize is a power of two (i.e. 2048 bytes or more).
	 */
	sblock.e2fs.e2fs_first_dblock = (sblock.e2fs_bsize > BBSIZE) ? 0 : 1;
	minfssize = fsbtodb(&sblock,
	    sblock.e2fs.e2fs_first_dblock +
	    NBLOCK_SUPERBLOCK +
	    1 /* at least one group descriptor */ +
	    NBLOCK_BLOCK_BITMAP	+
	    NBLOCK_INODE_BITMAP +
	    1 /* at least one inode table block */ +
	    1 /* at least one data block for rootdir */ +
	    1 /* at least one data block for data */
	    );			/* XXX and more? */

	if (fssize < minfssize)
		errx(EXIT_FAILURE, "Filesystem size %" PRId64
		    " < minimum size of %" PRId64 "\n", fssize, minfssize);

	bcount = dbtofsb(&sblock, fssize);

	/*
	 * While many people claim that ext2fs is a (bad) clone of ufs/ffs,
	 * it isn't actual ffs so maybe we should call it "block group"
	 * as their native name rather than ffs derived "cylinder group."
	 * But we'll use the latter here since other kernel sources use it.
	 * (I also agree "cylinder" based allocation is obsolete though)
	 */

	/* maybe "simple is the best" */
	blocks_per_cg = sblock.e2fs_bsize * NBBY;

	ncg = howmany(bcount - sblock.e2fs.e2fs_first_dblock, blocks_per_cg);
	blocks_gd = howmany(sizeof(struct ext2_gd) * ncg, bsize);

	/* check range of inode number */
	if (num_inodes < EXT2_FIRSTINO)
		num_inodes = EXT2_FIRSTINO;	/* needs reserved inodes + 1 */
	if (num_inodes > UINT16_MAX * ncg)
		num_inodes = UINT16_MAX * ncg;	/* ext2bgd_nifree is uint16_t */

	inodes_per_cg = num_inodes / ncg;
	iblocks_per_cg = howmany(inodesize * inodes_per_cg, bsize);

	/* Check that the last cylinder group has enough space for inodes */
	minblocks_per_cg =
	    NBLOCK_BLOCK_BITMAP +
	    NBLOCK_INODE_BITMAP +
	    iblocks_per_cg +
	    1;	/* at least one data block */
	if (Oflag == 0 || cg_has_sb(ncg - 1) != 0)
		minblocks_per_cg += NBLOCK_SUPERBLOCK + blocks_gd;

	blocks_lastcg = bcount - sblock.e2fs.e2fs_first_dblock -
	    blocks_per_cg * (ncg - 1);
	if (blocks_lastcg < minblocks_per_cg) {
		/*
		 * Since we make all the cylinder groups the same size, the
		 * last will only be small if there are more than one
		 * cylinder groups. If the last one is too small to store
		 * filesystem data, just kill it.
		 *
		 * XXX: Does fsck_ext2fs(8) properly handle this case?
		 */
		bcount -= blocks_lastcg;
		ncg--;
		blocks_lastcg = blocks_per_cg;
		blocks_gd = howmany(sizeof(struct ext2_gd) * ncg, bsize);
		inodes_per_cg = num_inodes / ncg;
	}
	/* roundup inodes_per_cg to make it use whole inode table blocks */
	inodes_per_cg = roundup(inodes_per_cg, sblock.e2fs_ipb);
	num_inodes = inodes_per_cg * ncg;
	iblocks_per_cg = inodes_per_cg / sblock.e2fs_ipb;

	/* XXX: probably we should check these adjusted values again */

	sblock.e2fs.e2fs_bcount = bcount;
	sblock.e2fs.e2fs_icount = num_inodes;

	sblock.e2fs_ncg = ncg;
	sblock.e2fs_ngdb = blocks_gd;
	sblock.e2fs_itpg = iblocks_per_cg;

	sblock.e2fs.e2fs_rbcount = sblock.e2fs.e2fs_bcount * minfree / 100;
	/* e2fs_fbcount will be accounted later */
	/* e2fs_ficount will be accounted later */

	sblock.e2fs.e2fs_bpg = blocks_per_cg;
	sblock.e2fs.e2fs_fpg = blocks_per_cg;

	sblock.e2fs.e2fs_ipg = inodes_per_cg;

	sblock.e2fs.e2fs_mtime = 0;
	sblock.e2fs.e2fs_wtime = (u_int32_t)tv.tv_sec;
	sblock.e2fs.e2fs_mnt_count = 0;
	/* XXX: should add some entropy to avoid checking all fs at once? */
	sblock.e2fs.e2fs_max_mnt_count = EXT2_DEF_MAX_MNT_COUNT;

	sblock.e2fs.e2fs_magic = E2FS_MAGIC;
	sblock.e2fs.e2fs_state = E2FS_ISCLEAN;
	sblock.e2fs.e2fs_beh = E2FS_BEH_DEFAULT;
	sblock.e2fs.e2fs_minrev = 0;
	sblock.e2fs.e2fs_lastfsck = (u_int32_t)tv.tv_sec;
	sblock.e2fs.e2fs_fsckintv = EXT2_DEF_FSCKINTV;

	/*
	 * Maybe we can use E2FS_OS_FREEBSD here and it would be more proper,
	 * but the purpose of this newfs_ext2fs(8) command is to provide
	 * a filesystem which can be recognized by firmware on some
	 * Linux based appliances that can load bootstrap files only from
	 * (their native) ext2fs, and anyway we will (and should) try to
	 * act like them as much as possible.
	 *
	 * Anyway, I hope that all newer such boxes will keep their support
	 * for the "GOOD_OLD_REV" ext2fs.
	 */
	sblock.e2fs.e2fs_creator = E2FS_OS_LINUX;

	if (Oflag == 0) {
		sblock.e2fs.e2fs_rev = E2FS_REV0;
		sblock.e2fs.e2fs_features_compat   = 0;
		sblock.e2fs.e2fs_features_incompat = 0;
		sblock.e2fs.e2fs_features_rocompat = 0;
	} else {
		sblock.e2fs.e2fs_rev = E2FS_REV1;
		/*
		 * e2fsprogs say "REV1" is "dynamic" so
		 * it isn't quite a version and maybe it means
		 * "extended from REV0 so check compat features."
		 *
		 * XXX: We don't have any native tool to activate
		 *      the EXT2F_COMPAT_RESIZE feature and
		 *      fsck_ext2fs(8) might not fix structures for it.
		 */
		sblock.e2fs.e2fs_features_compat   = EXT2F_COMPAT_RESIZE;
		sblock.e2fs.e2fs_features_incompat = EXT2F_INCOMPAT_FTYPE;
		sblock.e2fs.e2fs_features_rocompat =
		    EXT2F_ROCOMPAT_SPARSE_SUPER | EXT2F_ROCOMPAT_LARGE_FILE;
	}

	sblock.e2fs.e2fs_ruid = geteuid();
	sblock.e2fs.e2fs_rgid = getegid();

	sblock.e2fs.e2fs_first_ino = EXT2_FIRSTINO;
	sblock.e2fs.e2fs_inode_size = inodesize;

	/* e2fs_block_group_nr is set on writing superblock to each group */

	uuid_get(&sblock);
	if (volname != NULL) {
		if (strlen(volname) > sizeof(sblock.e2fs.e2fs_vname))
			errx(EXIT_FAILURE, "Volume name is too long");
		strlcpy(sblock.e2fs.e2fs_vname, volname,
		    sizeof(sblock.e2fs.e2fs_vname));
	}

	sblock.e2fs.e2fs_fsmnt[0] = '\0';
	sblock.e2fs_fsmnt[0] = '\0';

	sblock.e2fs.e2fs_algo = 0;		/* XXX unsupported? */
	sblock.e2fs.e2fs_prealloc = 0;		/* XXX unsupported? */
	sblock.e2fs.e2fs_dir_prealloc = 0;	/* XXX unsupported? */

	/* calculate blocks for reserved group descriptors for resize */
	sblock.e2fs.e2fs_reserved_ngdb = 0;
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    (sblock.e2fs.e2fs_features_compat & EXT2F_COMPAT_RESIZE) != 0) {
		uint64_t target_blocks;
		uint target_ncg, target_ngdb, reserved_ngdb;

		/* reserve descriptors for size as 1024 times as current */
		target_blocks =
		    (sblock.e2fs.e2fs_bcount - sblock.e2fs.e2fs_first_dblock)
		    * 1024ULL;
		/* number of blocks must be in uint32_t */
		if (target_blocks > UINT32_MAX)
			target_blocks = UINT32_MAX;
		target_ncg = howmany(target_blocks, sblock.e2fs.e2fs_bpg);
		target_ngdb = howmany(sizeof(struct ext2_gd) * target_ncg,
		    sblock.e2fs_bsize);
		/*
		 * Reserved group descriptor blocks are preserved as
		 * the second level double indirect reference blocks in
		 * the EXT2_RESIZEINO inode, so the maximum number of
		 * the blocks is NINDIR(fs).
		 * (see also descriptions in init_resizeino() function)
		 *
		 * We check a number including current e2fs_ngdb here
		 * because they will be moved into reserved gdb on
		 * possible future size shrink, though e2fsprogs don't
		 * seem to care about it.
		 */
		if (target_ngdb > NINDIR(&sblock))
			target_ngdb = NINDIR(&sblock);

		reserved_ngdb = target_ngdb - sblock.e2fs_ngdb;

		/* make sure reserved_ngdb fits in the last cg */
		if (reserved_ngdb >= blocks_lastcg - cgoverhead(ncg - 1))
			reserved_ngdb = blocks_lastcg - cgoverhead(ncg - 1);
		if (reserved_ngdb == 0) {
			/* if no space for reserved gdb, disable the feature */
			sblock.e2fs.e2fs_features_compat &=
			    ~EXT2F_COMPAT_RESIZE;
		}
		sblock.e2fs.e2fs_reserved_ngdb = reserved_ngdb;
	}

	/*
	 * Initialize group descriptors
	 */
	gd = calloc(sblock.e2fs_ngdb, bsize);
	if (gd == NULL)
		errx(EXIT_FAILURE, "Can't allocate descriptors buffer");

	fbcount = 0;
	ficount = 0;
	for (cylno = 0; cylno < ncg; cylno++) {
		uint boffset;

		boffset = cgbase(&sblock, cylno);
		if (sblock.e2fs.e2fs_rev == E2FS_REV0 ||
		    (sblock.e2fs.e2fs_features_rocompat &
		     EXT2F_ROCOMPAT_SPARSE_SUPER) == 0 ||
		    cg_has_sb(cylno)) {
			boffset += NBLOCK_SUPERBLOCK + sblock.e2fs_ngdb;
			if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
			    (sblock.e2fs.e2fs_features_compat &
			     EXT2F_COMPAT_RESIZE) != 0)
				boffset += sblock.e2fs.e2fs_reserved_ngdb;
		}
		gd[cylno].ext2bgd_b_bitmap = boffset;
		boffset += NBLOCK_BLOCK_BITMAP;
		gd[cylno].ext2bgd_i_bitmap = boffset;
		boffset += NBLOCK_INODE_BITMAP;
		gd[cylno].ext2bgd_i_tables = boffset;
		if (cylno == (ncg - 1))
			gd[cylno].ext2bgd_nbfree =
			    blocks_lastcg - cgoverhead(cylno);
		else
			gd[cylno].ext2bgd_nbfree =
			    sblock.e2fs.e2fs_bpg - cgoverhead(cylno);
		fbcount += gd[cylno].ext2bgd_nbfree;
		gd[cylno].ext2bgd_nifree = sblock.e2fs.e2fs_ipg;
		if (cylno == 0) {
			/* take reserved inodes off nifree */
			gd[cylno].ext2bgd_nifree -= EXT2_RESERVED_INODES;
		}
		ficount += gd[cylno].ext2bgd_nifree;
		gd[cylno].ext2bgd_ndirs = 0;
	}
	sblock.e2fs.e2fs_fbcount = fbcount;
	sblock.e2fs.e2fs_ficount = ficount;

	/*
	 * Dump out summary information about file system.
	 */
	if (verbosity > 0) {
		printf("%s: %u.%1uMB (%" PRId64 " sectors) "
		    "block size %u, fragment size %u\n",
		    fsys,
		    (uint)(((uint64_t)bcount * bsize) / (1024 * 1024)),
		    (uint)((uint64_t)bcount * bsize -
		    rounddown((uint64_t)bcount * bsize, 1024 * 1024))
		    / 1024 / 100,
		    fssize, bsize, fsize);
		printf("\tusing %u block groups of %u.0MB, %u blks, "
		    "%u inodes.\n",
		    ncg, bsize * sblock.e2fs.e2fs_bpg / (1024 * 1024),
		    sblock.e2fs.e2fs_bpg, sblock.e2fs.e2fs_ipg);
	}

	/*
	 * allocate space for superblock and group descriptors
	 */
	iobufsize = (NBLOCK_SUPERBLOCK + sblock.e2fs_ngdb) * sblock.e2fs_bsize;
	iobuf = mmap(0, iobufsize, PROT_READ|PROT_WRITE,
	    MAP_ANON|MAP_PRIVATE, -1, 0);
	if (iobuf == MAP_FAILED)
		errx(EXIT_FAILURE, "Cannot allocate I/O buffer\n");

	/*
	 * We now start writing to the filesystem
	 */

	if (!Nflag) {
		static const uint pbsize[] = { 1024, 2048, 4096, 0 };
		uint pblock, epblock;
		/*
		 * Validate the given file system size.
		 * Verify that its last block can actually be accessed.
		 * Convert to file system fragment sized units.
		 */
		if (fssize <= 0)
			errx(EXIT_FAILURE, "Preposterous size %" PRId64 "\n",
			    fssize);
		wtfs(fssize - 1, sectorsize, iobuf);

		/*
		 * Ensure there is nothing that looks like a filesystem
		 * superblock anywhere other than where ours will be.
		 * If fsck_ext2fs finds the wrong one all hell breaks loose!
		 *
		 * XXX: needs to check how fsck_ext2fs programs even
		 *      on other OSes determine alternate superblocks
		 */
		for (i = 0; pbsize[i] != 0; i++) {
			epblock = (uint64_t)bcount * bsize / pbsize[i];
			for (pblock = ((pbsize[i] == SBSIZE) ? 1 : 0);
			    pblock < epblock;
			    pblock += pbsize[i] * NBBY /* bpg */)
				zap_old_sblock((daddr32_t)pblock *
				    pbsize[i] / sectorsize);
		}
	}

	if (verbosity >= 3)
		printf("super-block backups (for fsck_ext2fs -b #) at:\n");
	/* If we are printing more than one line of numbers, line up columns */
	fld_width = verbosity < 4 ? 1 : snprintf(NULL, 0, "%" PRIu64,
	    (uint64_t)cgbase(&sblock, ncg - 1));
	if (Nflag && verbosity == 3)
		/* Leave space to add " ..." after one row of numbers */
		max_cols -= 4;
#define BASE 0x10000	/* For some fixed-point maths */
	col = 0;
	delta = verbosity > 2 ? 0 : max_cols * BASE / ncg;
	for (cylno = 0; cylno < ncg; cylno++) {
		fflush(stdout);
		initcg(cylno);
		if (verbosity < 2)
			continue;
		/* the first one is a master, not backup */
		if (cylno == 0)
			continue;
		/* skip if this cylinder doesn't have a backup */
		if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
		    (sblock.e2fs.e2fs_features_rocompat &
		     EXT2F_ROCOMPAT_SPARSE_SUPER) != 0 &&
		    cg_has_sb(cylno) == 0)
			continue;

		if (delta > 0) {
			if (Nflag)
				/* No point doing dots for -N */
				break;
			/* Print dots scaled to end near RH margin */
			for (col += delta; col > BASE; col -= BASE)
				printf(".");
			continue;
		}
		/* Print superblock numbers */
		len = printf("%s%*" PRIu64 ",", (col ? " " : ""), fld_width,
		    (uint64_t)cgbase(&sblock, cylno));
		col += len;
		if (col + len < max_cols)
			/* Next number fits */
			continue;
		/* Next number won't fit, need a newline */
		if (verbosity <= 3) {
			/* Print dots for subsequent cylinder groups */
			delta = sblock.e2fs_ncg - cylno - 1;
			if (delta != 0) {
				if (Nflag) {
					printf(" ...");
					break;
				}
				delta = max_cols * BASE / delta;
			}
		}
		col = 0;
		printf("\n");
	}
#undef BASE
	if (col > 0)
		printf("\n");
	if (Nflag)
		return;

	/*
	 * Now construct the initial file system,
	 */
	if (fsinit(&tv) == 0)
		errx(EXIT_FAILURE, "Error making filesystem");
	/*
	 * Write out the superblock and group descriptors
	 */
	sblock.e2fs.e2fs_block_group_nr = 0;
	sboff = 0;
	if (cgbase(&sblock, 0) == 0) {
		/*
		 * If the first block contains the boot block sectors,
		 * (i.e. in case of sblock.e2fs.e2fs_bsize > BBSIZE)
		 * we have to preserve data in it.
		 */
		sboff = SBOFF;
	}
	e2fs_sbsave(&sblock.e2fs, (struct ext2fs *)(iobuf + sboff));
	e2fs_cgsave(gd, (struct ext2_gd *)(iobuf + sblock.e2fs_bsize),
	   sizeof(struct ext2_gd) * sblock.e2fs_ncg);
	wtfs(fsbtodb(&sblock, cgbase(&sblock, 0)) + sboff / sectorsize,
	    iobufsize - sboff, iobuf + sboff);

	munmap(iobuf, iobufsize);
}

/*
 * Initialize a cylinder (block) group.
 */
void
initcg(uint cylno)
{
	uint nblcg, i, j, sboff;
	struct ext2fs_dinode *dp;

	/*
	 * Make a copy of the superblock and group descriptors.
	 */
	if (sblock.e2fs.e2fs_rev == E2FS_REV0 ||
	    (sblock.e2fs.e2fs_features_rocompat &
	     EXT2F_ROCOMPAT_SPARSE_SUPER) == 0 ||
	    cg_has_sb(cylno)) {
		sblock.e2fs.e2fs_block_group_nr = cylno;
		sboff = 0;
		if (cgbase(&sblock, cylno) == 0) {
			/* preserve data in bootblock in cg0 */
			sboff = SBOFF;
		}
		e2fs_sbsave(&sblock.e2fs, (struct ext2fs *)(iobuf + sboff));
		e2fs_cgsave(gd, (struct ext2_gd *)(iobuf +
		    sblock.e2fs_bsize * NBLOCK_SUPERBLOCK),
		    sizeof(struct ext2_gd) * sblock.e2fs_ncg);
		/* write superblock and group descriptor backups */
		wtfs(fsbtodb(&sblock, cgbase(&sblock, cylno)) +
		    sboff / sectorsize, iobufsize - sboff, iobuf + sboff);
	}

	/*
	 * Initialize block bitmap.
	 */
	memset(buf, 0, sblock.e2fs_bsize);
	if (cylno == (sblock.e2fs_ncg - 1)) {
		/* The last group could have less blocks than e2fs_bpg. */
		nblcg = sblock.e2fs.e2fs_bcount -
		    cgbase(&sblock, sblock.e2fs_ncg - 1);
		for (i = nblcg; i < roundup(nblcg, NBBY); i++)
			setbit(buf, i);
		memset(&buf[i / NBBY], ~0U, sblock.e2fs.e2fs_bpg - i);
	}
	/* set overhead (superblock, group descriptor etc.) blocks used */
	for (i = 0; i < cgoverhead(cylno) / NBBY; i++)
		buf[i] = ~0;
	i = i * NBBY;
	for (; i < cgoverhead(cylno); i++)
		setbit(buf, i);
	wtfs(fsbtodb(&sblock, gd[cylno].ext2bgd_b_bitmap), sblock.e2fs_bsize,
	    buf);

	/*
	 * Initialize inode bitmap.
	 *
	 *  Assume e2fs_ipg is a multiple of NBBY since
	 *  it's a multiple of e2fs_ipb (as we did above).
	 *  Note even (possibly smaller) the last group has the same e2fs_ipg.
	 */
	i = sblock.e2fs.e2fs_ipg / NBBY;
	memset(buf, 0, i);
	memset(buf + i, ~0U, sblock.e2fs_bsize - i);
	if (cylno == 0) {
		/* mark reserved inodes */
		for (i = 1; i < EXT2_FIRSTINO; i++)
			setbit(buf, EXT2_INO_INDEX(i));
	}
	wtfs(fsbtodb(&sblock, gd[cylno].ext2bgd_i_bitmap), sblock.e2fs_bsize,
	    buf);

	/*
	 * Initialize inode tables.
	 *
	 *  Just initialize generation numbers for NFS security.
	 *  XXX: sys/ufs/ext2fs/ext2fs_alloc.c:ext2fs_valloc() seems
	 *       to override these generated numbers.
	 */
	memset(buf, 0, sblock.e2fs_bsize);
	for (i = 0; i < sblock.e2fs_itpg; i++) {
		for (j = 0; j < sblock.e2fs_ipb; j++) {
			dp = (struct ext2fs_dinode *)(buf + inodesize * j);
			/* If there is some bias in arc4random(), keep it. */
			dp->e2di_gen = htole32(arc4random());
		}
		wtfs(fsbtodb(&sblock, gd[cylno].ext2bgd_i_tables + i),
		    sblock.e2fs_bsize, buf);
	}
}

/*
 * Zap possible lingering old superblock data
 */
static void
zap_old_sblock(daddr32_t sec)
{
	static daddr32_t cg0_data;
	uint32_t oldfs[SBSIZE / sizeof(uint32_t)];
	static const struct fsm {
		uint32_t offset;
		uint32_t magic;
		uint32_t mask;
	} fs_magics[] = {
		{offsetof(struct ext2fs, e2fs_magic) / 4, E2FS_MAGIC, 0xffff},
		{offsetof(struct ext2fs, e2fs_magic) / 4,
		    E2FS_MAGIC << 16, 0xffff0000},
		{14, 0xef530000, 0xffff0000},	/* EXT2FS (big) */
		{0x55c / 4, 0x00011954, ~0U},	/* FS_UFS1_MAGIC */
		{0x55c / 4, 0x19540119, ~0U},	/* FS_UFS2_MAGIC */
		{0, 0x70162, ~0U},		/* LFS_MAGIC */
		{.offset = ~0U},
	};
	const struct fsm *fsm;

	if (Nflag)
		return;

	/* don't override data before superblock */
	if (sec < SBOFF / sectorsize)
		return;

	if (cg0_data == 0) {
		cg0_data =
		    ((daddr32_t)sblock.e2fs.e2fs_first_dblock + cgoverhead(0)) *
		    sblock.e2fs_bsize / sectorsize;
	}

	/* Ignore anything that is beyond our filesystem */
	if (sec >= fssize)
		return;
	/* Zero anything inside our filesystem... */
	if (sec >= sblock.e2fs.e2fs_first_dblock * bsize / sectorsize) {
		/* ...unless we will write that area anyway */
		if (sec >= cg0_data)
			/* assume iobuf is zero'ed here */
			wtfs(sec, roundup(SBSIZE, sectorsize), iobuf);
		return;
	}

	/*
	 * The sector might contain boot code, so we must validate it
	 *
	 * XXX: ext2fs won't preserve data after SBOFF,
	 *      but first_dblock could have a different value.
	 */
	rdfs(sec, sizeof(oldfs), &oldfs);
	for (fsm = fs_magics;; fsm++) {
		uint32_t v;
		if (fsm->mask == 0)
			return;
		v = oldfs[fsm->offset];
		if ((v & fsm->mask) == fsm->magic ||
		    (swap32(v) & fsm->mask) == fsm->magic)
			break;
	}

	/* Just zap the magic number */
	oldfs[fsm->offset] = 0;
	wtfs(sec, sizeof(oldfs), &oldfs);
}

/*
 * uint cgoverhead(uint c)
 *
 * 	Return a number of reserved blocks on the specified group.
 * 	XXX: should be shared with src/sbin/fsck_ext2fs/setup.c
 */
uint
cgoverhead(uint c)
{
	uint overh;

	overh = NBLOCK_BLOCK_BITMAP + NBLOCK_INODE_BITMAP + sblock.e2fs_itpg;

	if (sblock.e2fs.e2fs_rev == E2FS_REV0 ||
	    (sblock.e2fs.e2fs_features_rocompat &
	     EXT2F_ROCOMPAT_SPARSE_SUPER) == 0 ||
	    cg_has_sb(c) != 0) {
		overh += NBLOCK_SUPERBLOCK + sblock.e2fs_ngdb;

		if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
		    (sblock.e2fs.e2fs_features_compat &
		     EXT2F_COMPAT_RESIZE) != 0)
			overh += sblock.e2fs.e2fs_reserved_ngdb;
	}

	return overh;
}

/*
 * Initialize the file system
 */

#define LOSTDIR		/* e2fsck complains if there is no lost+found */

#define	PREDEFDIR	2

#ifdef LOSTDIR
#define	PREDEFROOTDIR	(PREDEFDIR + 1)
#else
#define	PREDEFROOTDIR	PREDEFDIR
#endif

struct ext2fs_direct root_dir[] = {
	{ EXT2_ROOTINO, 0, 1, 0, "." },
	{ EXT2_ROOTINO, 0, 2, 0, ".." },
#ifdef LOSTDIR
	{ EXT2_LOSTFOUNDINO, 0, 10, 0, "lost+found" },
#endif
};

#ifdef LOSTDIR
struct ext2fs_direct lost_found_dir[] = {
	{ EXT2_LOSTFOUNDINO, 0, 1, 0, "." },
	{ EXT2_ROOTINO, 0, 2, 0, ".." },
};
struct ext2fs_direct pad_dir = { 0, sizeof(struct ext2fs_direct), 0, 0, "" };
#endif

int
fsinit(const struct timeval *tv)
{
	struct ext2fs_dinode node;
#ifdef LOSTDIR
	uint i, nblks_lostfound, blk;
#endif

	/*
	 * Initialize the inode for the resizefs feature
	 */
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    (sblock.e2fs.e2fs_features_compat & EXT2F_COMPAT_RESIZE) != 0)
		init_resizeino(tv);

	/*
	 * Initialize the node
	 */

#ifdef LOSTDIR
	/*
	 * Create the lost+found directory
	 */
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE) {
		lost_found_dir[0].e2d_type = EXT2_FT_DIR;
		lost_found_dir[1].e2d_type = EXT2_FT_DIR;
	}
	(void)makedir(lost_found_dir, nitems(lost_found_dir));

	/* prepare a bit large directory for preserved files */
	nblks_lostfound = EXT2_LOSTFOUNDSIZE / sblock.e2fs_bsize;
	/* ...but only with direct blocks */
	if (nblks_lostfound > NDADDR)
		nblks_lostfound = NDADDR;

	memset(&node, 0, sizeof(node));
	node.e2di_mode = EXT2_IFDIR | EXT2_LOSTFOUNDUMASK;
	node.e2di_uid_low = geteuid();
	node.e2di_size = sblock.e2fs_bsize * nblks_lostfound;
	node.e2di_atime = (u_int32_t)tv->tv_sec;
	node.e2di_ctime = (u_int32_t)tv->tv_sec;
	node.e2di_mtime = (u_int32_t)tv->tv_sec;
	node.e2di_gid_low = getegid();
	node.e2di_nlink = PREDEFDIR;
	/* e2di_nblock is a number of disk blocks, not ext2fs blocks */
	node.e2di_nblock = fsbtodb(&sblock, nblks_lostfound);
	node.e2di_blocks[0] = alloc(sblock.e2fs_bsize, node.e2di_mode);
	if (node.e2di_blocks[0] == 0) {
		printf("%s: can't allocate block for lost+found\n", __func__);
		return 0;
	}
	for (i = 1; i < nblks_lostfound; i++) {
		blk = alloc(sblock.e2fs_bsize, 0);
		if (blk == 0) {
			printf("%s: can't allocate blocks for lost+found\n",
			    __func__);
			return 0;
		}
		node.e2di_blocks[i] = blk;
	}
	wtfs(fsbtodb(&sblock, node.e2di_blocks[0]), sblock.e2fs_bsize, buf);
	pad_dir.e2d_reclen = sblock.e2fs_bsize;
	for (i = 1; i < nblks_lostfound; i++) {
		memset(buf, 0, sblock.e2fs_bsize);
		copy_dir(&pad_dir, (struct ext2fs_direct *)buf);
		wtfs(fsbtodb(&sblock, node.e2di_blocks[i]), sblock.e2fs_bsize,
		    buf);
	}
	iput(&node, EXT2_LOSTFOUNDINO);
#endif
	/*
	 * create the root directory
	 */
	memset(&node, 0, sizeof(node));
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE) {
		root_dir[0].e2d_type = EXT2_FT_DIR;
		root_dir[1].e2d_type = EXT2_FT_DIR;
#ifdef LOSTDIR
		root_dir[2].e2d_type = EXT2_FT_DIR;
#endif
	}
	node.e2di_mode = EXT2_IFDIR | EXT2_UMASK;
	node.e2di_uid_low = geteuid();
	node.e2di_size = makedir(root_dir, nitems(root_dir));
	node.e2di_atime = (u_int32_t)tv->tv_sec;
	node.e2di_ctime = (u_int32_t)tv->tv_sec;
	node.e2di_mtime = (u_int32_t)tv->tv_sec;
	node.e2di_gid_low = getegid();
	node.e2di_nlink = PREDEFROOTDIR;
	/* e2di_nblock is a number of disk block, not ext2fs block */
	node.e2di_nblock = fsbtodb(&sblock, 1);
	node.e2di_blocks[0] = alloc(node.e2di_size, node.e2di_mode);
	if (node.e2di_blocks[0] == 0) {
		printf("%s: can't allocate block for root dir\n", __func__);
		return 0;
	}
	wtfs(fsbtodb(&sblock, node.e2di_blocks[0]), sblock.e2fs_bsize, buf);
	iput(&node, EXT2_ROOTINO);
	return 1;
}

/*
 * Construct a set of directory entries in "buf".
 * return size of directory.
 */
int
makedir(struct ext2fs_direct *protodir, int entries)
{
	uint8_t *cp;
	uint i, spcleft;
	uint dirblksiz;

	dirblksiz = sblock.e2fs_bsize;
	memset(buf, 0, dirblksiz);
	spcleft = dirblksiz;
	for (cp = buf, i = 0; i < entries - 1; i++) {
		protodir[i].e2d_reclen = EXT2FS_DIRSIZ(protodir[i].e2d_namlen);
		copy_dir(&protodir[i], (struct ext2fs_direct *)cp);
		cp += protodir[i].e2d_reclen;
		spcleft -= protodir[i].e2d_reclen;
	}
	protodir[i].e2d_reclen = spcleft;
	copy_dir(&protodir[i], (struct ext2fs_direct *)cp);
	return dirblksiz;
}

/*
 * Copy a direntry to a buffer, in fs byte order
 */
static void
copy_dir(struct ext2fs_direct *dir, struct ext2fs_direct *dbuf)
{

	memcpy(dbuf, dir, EXT2FS_DIRSIZ(dir->e2d_namlen));
	dbuf->e2d_ino = htole32(dir->e2d_ino);
	dbuf->e2d_reclen = htole16(dir->e2d_reclen);
}

/*
 * void init_resizeino(const struct timeval *tv);
 *
 *	Initialize the EXT2_RESEIZE_INO inode to preserve
 *	reserved group descriptor blocks for future growth of this ext2fs.
 */
void
init_resizeino(const struct timeval *tv)
{
	struct ext2fs_dinode node;
	uint64_t isize;
	uint32_t *dindir_block, *reserved_gdb;
	uint nblock, i, cylno, n;

	memset(&node, 0, sizeof(node));

	/*
	 * Note this function only prepares required structures for
	 * future resize. It's a quite different work to implement
	 * a utility like resize_ext2fs(8) which handles actual
	 * resize ops even on offline.
	 *
	 * Anyway, I'm not sure if there is any documentation about
	 * this resize ext2fs feature and related data structures,
	 * and I've written this function based on things what I see
	 * on some existing implementation and real file system data
	 * created by existing tools. To be honest, they are not
	 * so easy to read, so I will try to implement it here without
	 * any dumb optimization for people who would eventually
	 * work on "yet another wheel" like resize_ext2fs(8).
	 */

	/*
	 * I'm not sure what type is appropriate for this inode.
	 * The release notes of e2fsprogs says they changed e2fsck to allow
	 * IFREG for RESIZEINO since a certain resize tool used it. Hmm.
	 */
	node.e2di_mode = EXT2_IFREG | EXT2_RESIZEINOUMASK;
	node.e2di_uid_low = geteuid();
	node.e2di_atime = (u_int32_t)tv->tv_sec;
	node.e2di_ctime = (u_int32_t)tv->tv_sec;
	node.e2di_mtime = (u_int32_t)tv->tv_sec;
	node.e2di_gid_low = getegid();
	node.e2di_nlink = 1;

	/*
	 * To preserve the reserved group descriptor blocks,
	 * EXT2_RESIZEINO uses only double indirect reference
	 * blocks in its inode entries.
	 *
	 * All entries for direct, single indirect and triple
	 * indirect references are left zero'ed. Maybe it's safe
	 * because no write operation will happen with this inode.
	 *
	 * We have to allocate a block for the first level double
	 * indirect reference block. Indexes of inode entries in
	 * this first level dindirect block are corresponding to
	 * indexes of group descriptors including both used (e2fs_ngdb)
	 * and reserved (e2fs_reserved_ngdb) group descriptor blocks.
	 *
	 * Inode entries of indexes for used (e2fs_ngdb) descriptors are
	 * left zero'ed. Entries for reserved (e2fs_reserved_ngdb) ones
	 * have block numbers of actual reserved group descriptors
	 * allocated at block group zero. This means e2fs_reserved_ngdb
	 * blocks are reserved as the second level dindirect reference
	 * blocks, and they actually contain block numbers of indirect
	 * references. It may be safe since they don't have to keep any
	 * data yet.
	 *
	 * Each these second dindirect blocks (i.e. reserved group
	 * descriptor blocks in the first block group) should have
	 * block numbers of its backups in all other block groups.
	 * I.e. reserved_ngdb[0] block in block group 0 contains block
	 * numbers of resreved_ngdb[0] from group 1 through (e2fs_ncg - 1).
	 * The number of backups can be determined by the
	 * EXT2_ROCOMPAT_SPARSESUPER feature and cg_has_sb() macro
	 * as done in the above initcg() function.
	 */

	/* set e2di_size which occupies whole blocks through DINDIR blocks */
	isize = (uint64_t)sblock.e2fs_bsize * NDADDR +
	    (uint64_t)sblock.e2fs_bsize * NINDIR(&sblock) +
	    (uint64_t)sblock.e2fs_bsize * NINDIR(&sblock) * NINDIR(&sblock);
	if (isize > UINT32_MAX &&
	    (sblock.e2fs.e2fs_features_rocompat &
	     EXT2F_ROCOMPAT_LARGE_FILE) == 0) {
		/* XXX should enable it here and update all backups? */
		errx(EXIT_FAILURE, "%s: large_file rocompat feature is "
		    "required to enable resize feature for this filesystem\n",
		    __func__);
	}
	/* upper 32bit is stored into e2di_size_hi on REV1 feature */
	node.e2di_size = isize & UINT32_MAX;
	node.e2di_size_hi = isize >> 32;

#define SINGLE	0	/* index of single indirect block */
#define DOUBLE	1	/* index of double indirect block */
#define TRIPLE	2	/* index of triple indirect block */

	/* zero out entries for direct references */
	for (i = 0; i < NDADDR; i++)
		node.e2di_blocks[i] = 0;
	/* also zero out entries for single and triple indirect references */
	node.e2di_blocks[NDADDR + SINGLE] = 0;
	node.e2di_blocks[NDADDR + TRIPLE] = 0;

	/* allocate a block for the first level double indirect reference */
	node.e2di_blocks[NDADDR + DOUBLE] =
	    alloc(sblock.e2fs_bsize, node.e2di_mode);
	if (node.e2di_blocks[NDADDR + DOUBLE] == 0)
		errx(EXIT_FAILURE, "%s: Can't allocate a dindirect block",
		    __func__);

	/* account this first block */
	nblock = fsbtodb(&sblock, 1);

	/* allocate buffer to set data in the dindirect block */
	dindir_block = malloc(sblock.e2fs_bsize);
	if (dindir_block == NULL)
		errx(EXIT_FAILURE,
		    "%s: Can't allocate buffer for a dindirect block",
		    __func__);

	/* allocate buffer to set data in the group descriptor blocks */
	reserved_gdb = malloc(sblock.e2fs_bsize);
	if (reserved_gdb == NULL)
		errx(EXIT_FAILURE,
		    "%s: Can't allocate buffer for group descriptor blocks",
		    __func__);

	/*
	 * Setup block entries in the first level dindirect blocks
	 */
	for (i = 0; i < sblock.e2fs_ngdb; i++) {
		/* no need to handle used group descriptor blocks */
		dindir_block[i] = 0;
	}
	for (; i < sblock.e2fs_ngdb + sblock.e2fs.e2fs_reserved_ngdb; i++) {
		/*
		 * point reserved group descriptor block in the first
		 * (i.e. master) block group
		 * 
		 * XXX: e2fsprogs seem to use "(i % NINDIR(&sblock))" here
		 *      to store maximum NINDIR(&sblock) reserved gdbs.
		 *      I'm not sure what will be done on future filesystem
		 *      shrink in that case on their way.
		 */
		if (i >= NINDIR(&sblock))
			errx(EXIT_FAILURE, "%s: too many reserved "
			    "group descriptors (%u) for resize inode",
			    __func__, sblock.e2fs.e2fs_reserved_ngdb);
		dindir_block[i] =
		    htole32(cgbase(&sblock, 0) + NBLOCK_SUPERBLOCK + i);

		/*
		 * Setup block entries in the second dindirect blocks
		 * (which are primary reserved group descriptor blocks)
		 * to point their backups.
		 */
		for (n = 0, cylno = 1; cylno < sblock.e2fs_ncg; cylno++) {
			/* skip block groups without backup */
			if ((sblock.e2fs.e2fs_features_rocompat &
			     EXT2F_ROCOMPAT_SPARSE_SUPER) != 0 &&
			    cg_has_sb(cylno) == 0)
				continue;

			if (n >= NINDIR(&sblock))
				errx(EXIT_FAILURE, "%s: too many block groups "
				    "for the resize feature", __func__);
			/*
			 * These blocks are already reserved in
			 * initcg() so no need to use alloc() here.
			 */
			reserved_gdb[n++] = htole32(cgbase(&sblock, cylno) +
			    NBLOCK_SUPERBLOCK + i);
			nblock += fsbtodb(&sblock, 1);
		}
		for (; n < NINDIR(&sblock); n++)
			reserved_gdb[n] = 0;

		/* write group descriptor block as the second dindirect refs */
		wtfs(fsbtodb(&sblock, letoh32(dindir_block[i])),
		    sblock.e2fs_bsize, reserved_gdb);
		nblock += fsbtodb(&sblock, 1);
	}
	for (; i < NINDIR(&sblock); i++) {
		/* leave trailing entries unallocated */
		dindir_block[i] = 0;
	}
	free(reserved_gdb);

	/* finally write the first level dindirect block */
	wtfs(fsbtodb(&sblock, node.e2di_blocks[NDADDR + DOUBLE]),
	    sblock.e2fs_bsize, dindir_block);
	free(dindir_block);

	node.e2di_nblock = nblock;
	iput(&node, EXT2_RESIZEINO);
}

/*
 * uint32_t alloc(uint32_t size, uint16_t mode)
 *
 *	Allocate a block (from cylinder group 0)
 *	Reference: src/sys/ufs/ext2fs/ext2fs_alloc.c:ext2fs_alloccg()
 */
uint32_t
alloc(uint32_t size, uint16_t mode)
{
	uint32_t loc, bno;
	uint8_t *bbp;
	uint len, map, i;

	if (gd[0].ext2bgd_nbfree == 0)
		return 0;

	if (size > sblock.e2fs_bsize)
		return 0;

	bbp = malloc(sblock.e2fs_bsize);
	if (bbp == NULL)
		return 0;
	rdfs(fsbtodb(&sblock, gd[0].ext2bgd_b_bitmap), sblock.e2fs_bsize, bbp);

	/* XXX: kernel uses e2fs_fpg here */
	len = sblock.e2fs.e2fs_bpg / NBBY;

#if 0	/* no need block allocation for root or lost+found dir */
	for (loc = 0; loc < len; loc++) {
		if (bbp[loc] == 0) {
			bno = loc * NBBY;
			goto gotit;
		}
	}
#endif

	loc = skpc(~0U, len, bbp);
	if (loc == 0) {
		free(bbp);
		return 0;
	}
	loc = len - loc;
	map = bbp[loc];
	bno = loc * NBBY;
	for (i = 0; i < NBBY; i++, bno++) {
		if ((map & (1 << i)) == 0)
			goto gotit;
	}
	free(bbp);
	return 0;
	
 gotit:
	if (isset(bbp, bno))
		errx(EXIT_FAILURE, "%s: inconsistent bitmap\n", __func__);

	setbit(bbp, bno);
	wtfs(fsbtodb(&sblock, gd[0].ext2bgd_b_bitmap), sblock.e2fs_bsize, bbp);
	free(bbp);
	/* XXX: modified group descriptors won't be written into backups */
	gd[0].ext2bgd_nbfree--;
	if ((mode & EXT2_IFDIR) != 0)
		gd[0].ext2bgd_ndirs++;
	sblock.e2fs.e2fs_fbcount--;

	return sblock.e2fs.e2fs_first_dblock + bno;
}

/*
 * void iput(struct ext2fs_dinode *ip, ino_t ino)
 *
 *	Put an inode entry into the corresponding table.
 */
static void
iput(struct ext2fs_dinode *ip, ino_t ino)
{
	daddr32_t d;
	uint c, i;
	struct ext2fs_dinode *dp;
	uint8_t *bp;

	bp = malloc(sblock.e2fs_bsize);
	if (bp == NULL)
		errx(EXIT_FAILURE, "%s: can't allocate buffer for inode\n",
		    __func__);

	/*
	 * Reserved inodes are allocated and accounted in initcg()
	 * so skip checks of the bitmap and allocation for them.
	 */
	if (ino >= EXT2_FIRSTINO) {
		c = ino_to_cg(&sblock, ino);

		/* sanity check */
		if (gd[c].ext2bgd_nifree == 0)
			errx(EXIT_FAILURE,
			    "%s: no free inode %" PRIu64 " in block group %u\n",
			    __func__, (uint64_t)ino, c);

		/* update inode bitmap */
		rdfs(fsbtodb(&sblock, gd[0].ext2bgd_i_bitmap),
		    sblock.e2fs_bsize, bp);

		/* more sanity */
		if (isset(bp, EXT2_INO_INDEX(ino)))
			errx(EXIT_FAILURE, "%s: inode %" PRIu64
			    " already in use\n", __func__, (uint64_t)ino);
		setbit(bp, EXT2_INO_INDEX(ino));
		wtfs(fsbtodb(&sblock, gd[0].ext2bgd_i_bitmap),
		    sblock.e2fs_bsize, bp);
		gd[c].ext2bgd_nifree--;
		sblock.e2fs.e2fs_ficount--;
	}

	if (ino >= sblock.e2fs.e2fs_ipg * sblock.e2fs_ncg)
		errx(EXIT_FAILURE, "%s: inode value out of range (%" PRIu64
		    ").\n", __func__, (uint64_t)ino);

	/* update an inode entry in the table */
	d = fsbtodb(&sblock, ino_to_fsba(&sblock, ino));
	rdfs(d, sblock.e2fs_bsize, bp);

	dp = (struct ext2fs_dinode *)(bp +
	    inodesize * ino_to_fsbo(&sblock, ino));
	e2fs_isave(&sblock, ip, dp);
	/* e2fs_i_bswap() doesn't swap e2di_blocks addrs */
	if ((ip->e2di_mode & EXT2_IFMT) != EXT2_IFLNK) {
		for (i = 0; i < NDADDR + NIADDR; i++)
			dp->e2di_blocks[i] = htole32(ip->e2di_blocks[i]);
	}
	/* If there is some bias in arc4random(), keep it. */
	dp->e2di_gen = htole32(arc4random());

	wtfs(d, sblock.e2fs_bsize, bp);
	free(bp);
}

/*
 * Read a block from the file system
 */
void
rdfs(daddr32_t bno, int size, void *bf)
{
	int n;
	off_t offset;

	offset = bno;
	n = pread(fd, bf, size, offset * sectorsize);
	if (n != size)
		err(EXIT_FAILURE, "%s: read error for sector %" PRId64,
		    __func__, (int64_t)bno);
}

/*
 * Write a block to the file system
 */
void
wtfs(daddr32_t bno, int size, void *bf)
{
	int n;
	off_t offset;

	if (Nflag)
		return;
	offset = bno;
	n = pwrite(fd, bf, size, offset * sectorsize);
	if (n != size)
		err(EXIT_FAILURE, "%s: write error for sector %" PRId64,
		    __func__, (int64_t)bno);
}

int
ilog2(uint val)
{

	if (val == 0 || !powerof2(val))
		errx(EXIT_FAILURE, "%s: %u is not a power of 2\n",
		    __func__, val);

	return ffs(val) - 1;
}

/*
 * int skpc(int mask, size_t size, uint8_t *cp)
 *
 *	Locate an unsigned character of value mask inside cp[].
 * 	(from src/sys/lib/libkern/skpc.c)
 */
int
skpc(int mask, size_t size, uint8_t *cp)
{
	uint8_t *end;

	end = &cp[size];
	while (cp < end && *cp == (uint8_t)mask)
		cp++;

	return end - cp;
}

static void
uuid_get(struct m_ext2fs *sb)
{
	unsigned char buf[sizeof(sb->e2fs.e2fs_uuid)];

	arc4random_buf(buf, sizeof(buf));
	/* UUID version 4: random */
	buf[6] &= 0x0f;
	buf[6] |= 0x40;
	/* RFC4122 variant */
	buf[8] &= 0x3f;
	buf[8] |= 0x80;
	memcpy(sb->e2fs.e2fs_uuid, buf, sizeof(sb->e2fs.e2fs_uuid));
}

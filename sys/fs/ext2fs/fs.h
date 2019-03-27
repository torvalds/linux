/*-
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
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
 *
 *	@(#)fs.h	8.7 (Berkeley) 4/19/94
 * $FreeBSD$
 */

#ifndef _FS_EXT2FS_FS_H_
#define	_FS_EXT2FS_FS_H_

/*
 * Each disk drive contains some number of file systems.
 * A file system consists of a number of cylinder groups.
 * Each cylinder group has inodes and data.
 *
 * A file system is described by its super-block, which in turn
 * describes the cylinder groups.  The super-block is critical
 * data and is replicated in each cylinder group to protect against
 * catastrophic loss.  This is done at `newfs' time and the critical
 * super-block data does not change, so the copies need not be
 * referenced further unless disaster strikes.
 *
 * The first boot and super blocks are given in absolute disk addresses.
 * The byte-offset forms are preferred, as they don't imply a sector size.
 */
#define	SBSIZE		1024
#define	SBLOCK		2

/*
 * The path name on which the file system is mounted is maintained
 * in fs_fsmnt. MAXMNTLEN defines the amount of space allocated in
 * the super block for this name.
 */
#define	MAXMNTLEN	512

/*
 * A summary of contiguous blocks of various sizes is maintained
 * in each cylinder group. Normally this is set by the initial
 * value of fs_maxcontig.
 *
 * XXX:FS_MAXCONTIG is set to 16 to conserve space. Here we set
 * EXT2_MAXCONTIG to 32 for better performance.
 */
#define	EXT2_MAXCONTIG	32

/*
 * Grigoriy Orlov <gluk@ptci.ru> has done some extensive work to fine
 * tune the layout preferences for directories within a filesystem.
 * His algorithm can be tuned by adjusting the following parameters
 * which tell the system the average file size and the average number
 * of files per directory. These defaults are well selected for typical
 * filesystems, but may need to be tuned for odd cases like filesystems
 * being used for squid caches or news spools.
 * AVFPDIR is the expected number of files per directory. AVGDIRSIZE is
 * obtained by multiplying AVFPDIR and AVFILESIZ which is assumed to be
 * 16384.
 */

#define	AFPDIR		64
#define	AVGDIRSIZE	1048576

/*
 * Macros for access to superblock array structures
 */

/*
 * Turn file system block numbers into disk block addresses.
 * This maps file system blocks to device size blocks.
 */
#define	fsbtodb(fs, b)	((daddr_t)(b) << (fs)->e2fs_fsbtodb)
#define	dbtofsb(fs, b)	((b) >> (fs)->e2fs_fsbtodb)

/* get group containing inode */
#define	ino_to_cg(fs, x)	(((x) - 1) / (fs->e2fs_ipg))

/* get block containing inode from its number x */
#define	ino_to_fsba(fs, x)                                              \
        (e2fs_gd_get_i_tables(&(fs)->e2fs_gd[ino_to_cg((fs), (x))]) +   \
        (((x) - 1) % (fs)->e2fs->e2fs_ipg) / (fs)->e2fs_ipb)

/* get offset for inode in block */
#define	ino_to_fsbo(fs, x)	((x-1) % (fs->e2fs_ipb))

/*
 * Give cylinder group number for a file system block.
 * Give cylinder group block number for a file system block.
 */
#define	dtog(fs, d)	(((d) - fs->e2fs->e2fs_first_dblock) / \
			EXT2_BLOCKS_PER_GROUP(fs))
#define	dtogd(fs, d)	(((d) - fs->e2fs->e2fs_first_dblock) % \
			EXT2_BLOCKS_PER_GROUP(fs))

/*
 * The following macros optimize certain frequently calculated
 * quantities by using shifts and masks in place of divisions
 * modulos and multiplications.
 */
#define	blkoff(fs, loc)		/* calculates (loc % fs->fs_bsize) */ \
	((loc) & (fs)->e2fs_qbmask)

#define	lblktosize(fs, blk)	/* calculates (blk * fs->fs_bsize) */ \
	((blk) << (fs->e2fs_bshift))

#define	lblkno(fs, loc)		/* calculates (loc / fs->fs_bsize) */ \
	((loc) >> (fs->e2fs_bshift))

/* no fragments -> logical block number equal # of frags */
#define	numfrags(fs, loc)	/* calculates (loc / fs->fs_fsize) */ \
	((loc) >> (fs->e2fs_bshift))

#define	fragroundup(fs, size)	/* calculates roundup(size, fs->fs_fsize) */ \
	roundup(size, fs->e2fs_fsize)
	/* was (((size) + (fs)->fs_qfmask) & (fs)->fs_fmask) */

/*
 * Determining the size of a file block in the file system.
 * easy w/o fragments
 */
#define	blksize(fs, ip, lbn) ((fs)->e2fs_fsize)

/*
 * INOPB is the number of inodes in a secondary storage block.
 */
#define	INOPB(fs)	(fs->e2fs_ipb)

/*
 * NINDIR is the number of indirects in a file system block.
 */
#define	NINDIR(fs)	(EXT2_ADDR_PER_BLOCK(fs))

/*
 * Use if additional debug logging is required.
 */
/* #define EXT2FS_DEBUG */

#endif	/* !_FS_EXT2FS_FS_H_ */

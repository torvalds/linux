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
 *	@(#)fs.h	8.13 (Berkeley) 3/21/95
 * $FreeBSD$
 */

#ifndef _UFS_FFS_FS_H_
#define	_UFS_FFS_FS_H_

#include <sys/mount.h>
#include <ufs/ufs/dinode.h>

/*
 * Each disk drive contains some number of filesystems.
 * A filesystem consists of a number of cylinder groups.
 * Each cylinder group has inodes and data.
 *
 * A filesystem is described by its super-block, which in turn
 * describes the cylinder groups.  The super-block is critical
 * data and is replicated in each cylinder group to protect against
 * catastrophic loss.  This is done at `newfs' time and the critical
 * super-block data does not change, so the copies need not be
 * referenced further unless disaster strikes.
 *
 * For filesystem fs, the offsets of the various blocks of interest
 * are given in the super block as:
 *	[fs->fs_sblkno]		Super-block
 *	[fs->fs_cblkno]		Cylinder group block
 *	[fs->fs_iblkno]		Inode blocks
 *	[fs->fs_dblkno]		Data blocks
 * The beginning of cylinder group cg in fs, is given by
 * the ``cgbase(fs, cg)'' macro.
 *
 * Depending on the architecture and the media, the superblock may
 * reside in any one of four places. For tiny media where every block 
 * counts, it is placed at the very front of the partition. Historically,
 * UFS1 placed it 8K from the front to leave room for the disk label and
 * a small bootstrap. For UFS2 it got moved to 64K from the front to leave
 * room for the disk label and a bigger bootstrap, and for really piggy
 * systems we check at 256K from the front if the first three fail. In
 * all cases the size of the superblock will be SBLOCKSIZE. All values are
 * given in byte-offset form, so they do not imply a sector size. The
 * SBLOCKSEARCH specifies the order in which the locations should be searched.
 */
#define	SBLOCK_FLOPPY	     0
#define	SBLOCK_UFS1	  8192
#define	SBLOCK_UFS2	 65536
#define	SBLOCK_PIGGY	262144
#define	SBLOCKSIZE	  8192
#define	SBLOCKSEARCH \
	{ SBLOCK_UFS2, SBLOCK_UFS1, SBLOCK_FLOPPY, SBLOCK_PIGGY, -1 }

/*
 * Max number of fragments per block. This value is NOT tweakable.
 */
#define	MAXFRAG 	8

/*
 * Addresses stored in inodes are capable of addressing fragments
 * of `blocks'. File system blocks of at most size MAXBSIZE can
 * be optionally broken into 2, 4, or 8 pieces, each of which is
 * addressable; these pieces may be DEV_BSIZE, or some multiple of
 * a DEV_BSIZE unit.
 *
 * Large files consist of exclusively large data blocks.  To avoid
 * undue wasted disk space, the last data block of a small file may be
 * allocated as only as many fragments of a large block as are
 * necessary.  The filesystem format retains only a single pointer
 * to such a fragment, which is a piece of a single large block that
 * has been divided.  The size of such a fragment is determinable from
 * information in the inode, using the ``blksize(fs, ip, lbn)'' macro.
 *
 * The filesystem records space availability at the fragment level;
 * to determine block availability, aligned fragments are examined.
 */

/*
 * MINBSIZE is the smallest allowable block size.
 * In order to insure that it is possible to create files of size
 * 2^32 with only two levels of indirection, MINBSIZE is set to 4096.
 * MINBSIZE must be big enough to hold a cylinder group block,
 * thus changes to (struct cg) must keep its size within MINBSIZE.
 * Note that super blocks are always of size SBLOCKSIZE,
 * and that both SBLOCKSIZE and MAXBSIZE must be >= MINBSIZE.
 */
#define	MINBSIZE	4096

/*
 * The path name on which the filesystem is mounted is maintained
 * in fs_fsmnt. MAXMNTLEN defines the amount of space allocated in
 * the super block for this name.
 */
#define	MAXMNTLEN	468

/*
 * The volume name for this filesystem is maintained in fs_volname.
 * MAXVOLLEN defines the length of the buffer allocated.
 */
#define	MAXVOLLEN	32

/*
 * There is a 128-byte region in the superblock reserved for in-core
 * pointers to summary information. Originally this included an array
 * of pointers to blocks of struct csum; now there are just a few
 * pointers and the remaining space is padded with fs_ocsp[].
 *
 * NOCSPTRS determines the size of this padding. One pointer (fs_csp)
 * is taken away to point to a contiguous array of struct csum for
 * all cylinder groups; a second (fs_maxcluster) points to an array
 * of cluster sizes that is computed as cylinder groups are inspected,
 * and the third points to an array that tracks the creation of new
 * directories. A fourth pointer, fs_active, is used when creating
 * snapshots; it points to a bitmap of cylinder groups for which the
 * free-block bitmap has changed since the snapshot operation began.
 */
#define	NOCSPTRS	((128 / sizeof(void *)) - 4)

/*
 * A summary of contiguous blocks of various sizes is maintained
 * in each cylinder group. Normally this is set by the initial
 * value of fs_maxcontig. To conserve space, a maximum summary size
 * is set by FS_MAXCONTIG.
 */
#define	FS_MAXCONTIG	16

/*
 * MINFREE gives the minimum acceptable percentage of filesystem
 * blocks which may be free. If the freelist drops below this level
 * only the superuser may continue to allocate blocks. This may
 * be set to 0 if no reserve of free blocks is deemed necessary,
 * however throughput drops by fifty percent if the filesystem
 * is run at between 95% and 100% full; thus the minimum default
 * value of fs_minfree is 5%. However, to get good clustering
 * performance, 10% is a better choice. hence we use 10% as our
 * default value. With 10% free space, fragmentation is not a
 * problem, so we choose to optimize for time.
 */
#define	MINFREE		8
#define	DEFAULTOPT	FS_OPTTIME

/*
 * Grigoriy Orlov <gluk@ptci.ru> has done some extensive work to fine
 * tune the layout preferences for directories within a filesystem.
 * His algorithm can be tuned by adjusting the following parameters
 * which tell the system the average file size and the average number
 * of files per directory. These defaults are well selected for typical
 * filesystems, but may need to be tuned for odd cases like filesystems
 * being used for squid caches or news spools.
 */
#define	AVFILESIZ	16384	/* expected average file size */
#define	AFPDIR		64	/* expected number of files per directory */

/*
 * The maximum number of snapshot nodes that can be associated
 * with each filesystem. This limit affects only the number of
 * snapshot files that can be recorded within the superblock so
 * that they can be found when the filesystem is mounted. However,
 * maintaining too many will slow the filesystem performance, so
 * having this limit is a good idea.
 */
#define	FSMAXSNAP 20

/*
 * Used to identify special blocks in snapshots:
 *
 * BLK_NOCOPY - A block that was unallocated at the time the snapshot
 *	was taken, hence does not need to be copied when written.
 * BLK_SNAP - A block held by another snapshot that is not needed by this
 *	snapshot. When the other snapshot is freed, the BLK_SNAP entries
 *	are converted to BLK_NOCOPY. These are needed to allow fsck to
 *	identify blocks that are in use by other snapshots (which are
 *	expunged from this snapshot).
 */
#define	BLK_NOCOPY ((ufs2_daddr_t)(1))
#define	BLK_SNAP ((ufs2_daddr_t)(2))

/*
 * Sysctl values for the fast filesystem.
 */
#define	FFS_ADJ_REFCNT		 1	/* adjust inode reference count */
#define	FFS_ADJ_BLKCNT		 2	/* adjust inode used block count */
#define	FFS_BLK_FREE		 3	/* free range of blocks in map */
#define	FFS_DIR_FREE		 4	/* free specified dir inodes in map */
#define	FFS_FILE_FREE		 5	/* free specified file inodes in map */
#define	FFS_SET_FLAGS		 6	/* set filesystem flags */
#define	FFS_ADJ_NDIR		 7	/* adjust number of directories */
#define	FFS_ADJ_NBFREE		 8	/* adjust number of free blocks */
#define	FFS_ADJ_NIFREE		 9	/* adjust number of free inodes */
#define	FFS_ADJ_NFFREE		10 	/* adjust number of free frags */
#define	FFS_ADJ_NUMCLUSTERS	11	/* adjust number of free clusters */
#define	FFS_SET_CWD		12	/* set current directory */
#define	FFS_SET_DOTDOT		13	/* set inode number for ".." */
#define	FFS_UNLINK		14	/* remove a name in the filesystem */
#define	FFS_SET_INODE		15	/* update an on-disk inode */
#define	FFS_SET_BUFOUTPUT	16	/* set buffered writing on descriptor */
#define	FFS_SET_SIZE		17	/* set inode size */
#define	FFS_MAXID		17	/* number of valid ffs ids */

/*
 * Command structure passed in to the filesystem to adjust filesystem values.
 */
#define	FFS_CMD_VERSION		0x19790518	/* version ID */
struct fsck_cmd {
	int32_t	version;	/* version of command structure */
	int32_t	handle;		/* reference to filesystem to be changed */
	int64_t	value;		/* inode or block number to be affected */
	int64_t	size;		/* amount or range to be adjusted */
	int64_t	spare;		/* reserved for future use */
};

/*
 * A recovery structure placed at the end of the boot block area by newfs
 * that can be used by fsck to search for alternate superblocks.
 */
struct fsrecovery {
	int32_t	fsr_magic;	/* magic number */
	int32_t	fsr_fsbtodb;	/* fsbtodb and dbtofsb shift constant */
	int32_t	fsr_sblkno;	/* offset of super-block in filesys */
	int32_t	fsr_fpg;	/* blocks per group * fs_frag */
	u_int32_t fsr_ncg;	/* number of cylinder groups */
};

/*
 * Per cylinder group information; summarized in blocks allocated
 * from first cylinder group data blocks.  These blocks have to be
 * read in from fs_csaddr (size fs_cssize) in addition to the
 * super block.
 */
struct csum {
	int32_t	cs_ndir;		/* number of directories */
	int32_t	cs_nbfree;		/* number of free blocks */
	int32_t	cs_nifree;		/* number of free inodes */
	int32_t	cs_nffree;		/* number of free frags */
};
struct csum_total {
	int64_t	cs_ndir;		/* number of directories */
	int64_t	cs_nbfree;		/* number of free blocks */
	int64_t	cs_nifree;		/* number of free inodes */
	int64_t	cs_nffree;		/* number of free frags */
	int64_t	cs_numclusters;		/* number of free clusters */
	int64_t	cs_spare[3];		/* future expansion */
};

/*
 * Super block for an FFS filesystem.
 */
struct fs {
	int32_t	 fs_firstfield;		/* historic filesystem linked list, */
	int32_t	 fs_unused_1;		/*     used for incore super blocks */
	int32_t	 fs_sblkno;		/* offset of super-block in filesys */
	int32_t	 fs_cblkno;		/* offset of cyl-block in filesys */
	int32_t	 fs_iblkno;		/* offset of inode-blocks in filesys */
	int32_t	 fs_dblkno;		/* offset of first data after cg */
	int32_t	 fs_old_cgoffset;	/* cylinder group offset in cylinder */
	int32_t	 fs_old_cgmask;		/* used to calc mod fs_ntrak */
	int32_t  fs_old_time;		/* last time written */
	int32_t	 fs_old_size;		/* number of blocks in fs */
	int32_t	 fs_old_dsize;		/* number of data blocks in fs */
	u_int32_t fs_ncg;		/* number of cylinder groups */
	int32_t	 fs_bsize;		/* size of basic blocks in fs */
	int32_t	 fs_fsize;		/* size of frag blocks in fs */
	int32_t	 fs_frag;		/* number of frags in a block in fs */
/* these are configuration parameters */
	int32_t	 fs_minfree;		/* minimum percentage of free blocks */
	int32_t	 fs_old_rotdelay;	/* num of ms for optimal next block */
	int32_t	 fs_old_rps;		/* disk revolutions per second */
/* these fields can be computed from the others */
	int32_t	 fs_bmask;		/* ``blkoff'' calc of blk offsets */
	int32_t	 fs_fmask;		/* ``fragoff'' calc of frag offsets */
	int32_t	 fs_bshift;		/* ``lblkno'' calc of logical blkno */
	int32_t	 fs_fshift;		/* ``numfrags'' calc number of frags */
/* these are configuration parameters */
	int32_t	 fs_maxcontig;		/* max number of contiguous blks */
	int32_t	 fs_maxbpg;		/* max number of blks per cyl group */
/* these fields can be computed from the others */
	int32_t	 fs_fragshift;		/* block to frag shift */
	int32_t	 fs_fsbtodb;		/* fsbtodb and dbtofsb shift constant */
	int32_t	 fs_sbsize;		/* actual size of super block */
	int32_t	 fs_spare1[2];		/* old fs_csmask */
					/* old fs_csshift */
	int32_t	 fs_nindir;		/* value of NINDIR */
	u_int32_t fs_inopb;		/* value of INOPB */
	int32_t	 fs_old_nspf;		/* value of NSPF */
/* yet another configuration parameter */
	int32_t	 fs_optim;		/* optimization preference, see below */
	int32_t	 fs_old_npsect;		/* # sectors/track including spares */
	int32_t	 fs_old_interleave;	/* hardware sector interleave */
	int32_t	 fs_old_trackskew;	/* sector 0 skew, per track */
	int32_t	 fs_id[2];		/* unique filesystem id */
/* sizes determined by number of cylinder groups and their sizes */
	int32_t	 fs_old_csaddr;		/* blk addr of cyl grp summary area */
	int32_t	 fs_cssize;		/* size of cyl grp summary area */
	int32_t	 fs_cgsize;		/* cylinder group size */
	int32_t	 fs_spare2;		/* old fs_ntrak */
	int32_t	 fs_old_nsect;		/* sectors per track */
	int32_t  fs_old_spc;		/* sectors per cylinder */
	int32_t	 fs_old_ncyl;		/* cylinders in filesystem */
	int32_t	 fs_old_cpg;		/* cylinders per group */
	u_int32_t fs_ipg;		/* inodes per group */
	int32_t	 fs_fpg;		/* blocks per group * fs_frag */
/* this data must be re-computed after crashes */
	struct	csum fs_old_cstotal;	/* cylinder summary information */
/* these fields are cleared at mount time */
	int8_t   fs_fmod;		/* super block modified flag */
	int8_t   fs_clean;		/* filesystem is clean flag */
	int8_t 	 fs_ronly;		/* mounted read-only flag */
	int8_t   fs_old_flags;		/* old FS_ flags */
	u_char	 fs_fsmnt[MAXMNTLEN];	/* name mounted on */
	u_char	 fs_volname[MAXVOLLEN];	/* volume name */
	u_int64_t fs_swuid;		/* system-wide uid */
	int32_t  fs_pad;		/* due to alignment of fs_swuid */
/* these fields retain the current block allocation info */
	int32_t	 fs_cgrotor;		/* last cg searched */
	void 	*fs_ocsp[NOCSPTRS];	/* padding; was list of fs_cs buffers */
	u_int8_t *fs_contigdirs;	/* (u) # of contig. allocated dirs */
	struct	csum *fs_csp;		/* (u) cg summary info buffer */
	int32_t	*fs_maxcluster;		/* (u) max cluster in each cyl group */
	u_int	*fs_active;		/* (u) used by snapshots to track fs */
	int32_t	 fs_old_cpc;		/* cyl per cycle in postbl */
	int32_t	 fs_maxbsize;		/* maximum blocking factor permitted */
	int64_t	 fs_unrefs;		/* number of unreferenced inodes */
	int64_t  fs_providersize;	/* size of underlying GEOM provider */
	int64_t	 fs_metaspace;		/* size of area reserved for metadata */
	int64_t	 fs_sparecon64[13];	/* old rotation block list head */
	int64_t	 fs_sblockactualloc;	/* byte offset of this superblock */
	int64_t	 fs_sblockloc;		/* byte offset of standard superblock */
	struct	csum_total fs_cstotal;	/* (u) cylinder summary information */
	ufs_time_t fs_time;		/* last time written */
	int64_t	 fs_size;		/* number of blocks in fs */
	int64_t	 fs_dsize;		/* number of data blocks in fs */
	ufs2_daddr_t fs_csaddr;		/* blk addr of cyl grp summary area */
	int64_t	 fs_pendingblocks;	/* (u) blocks being freed */
	u_int32_t fs_pendinginodes;	/* (u) inodes being freed */
	uint32_t fs_snapinum[FSMAXSNAP];/* list of snapshot inode numbers */
	u_int32_t fs_avgfilesize;	/* expected average file size */
	u_int32_t fs_avgfpdir;		/* expected # of files per directory */
	int32_t	 fs_save_cgsize;	/* save real cg size to use fs_bsize */
	ufs_time_t fs_mtime;		/* Last mount or fsck time. */
	int32_t  fs_sujfree;		/* SUJ free list */
	int32_t	 fs_sparecon32[21];	/* reserved for future constants */
	u_int32_t fs_ckhash;		/* if CK_SUPERBLOCK, its check-hash */
	u_int32_t fs_metackhash;	/* metadata check-hash, see CK_ below */
	int32_t  fs_flags;		/* see FS_ flags below */
	int32_t	 fs_contigsumsize;	/* size of cluster summary array */ 
	int32_t	 fs_maxsymlinklen;	/* max length of an internal symlink */
	int32_t	 fs_old_inodefmt;	/* format of on-disk inodes */
	u_int64_t fs_maxfilesize;	/* maximum representable file size */
	int64_t	 fs_qbmask;		/* ~fs_bmask for use with 64-bit size */
	int64_t	 fs_qfmask;		/* ~fs_fmask for use with 64-bit size */
	int32_t	 fs_state;		/* validate fs_clean field */
	int32_t	 fs_old_postblformat;	/* format of positional layout tables */
	int32_t	 fs_old_nrpos;		/* number of rotational positions */
	int32_t	 fs_spare5[2];		/* old fs_postbloff */
					/* old fs_rotbloff */
	int32_t	 fs_magic;		/* magic number */
};

/* Sanity checking. */
#ifdef CTASSERT
CTASSERT(sizeof(struct fs) == 1376);
#endif

/*
 * Filesystem identification
 */
#define	FS_UFS1_MAGIC	0x011954	/* UFS1 fast filesystem magic number */
#define	FS_UFS2_MAGIC	0x19540119	/* UFS2 fast filesystem magic number */
#define	FS_BAD_MAGIC	0x19960408	/* UFS incomplete newfs magic number */
#define	FS_42INODEFMT	-1		/* 4.2BSD inode format */
#define	FS_44INODEFMT	2		/* 4.4BSD inode format */

/*
 * Preference for optimization.
 */
#define	FS_OPTTIME	0	/* minimize allocation time */
#define	FS_OPTSPACE	1	/* minimize disk fragmentation */

/*
 * Filesystem flags.
 *
 * The FS_UNCLEAN flag is set by the kernel when the filesystem was
 * mounted with fs_clean set to zero. The FS_DOSOFTDEP flag indicates
 * that the filesystem should be managed by the soft updates code.
 * Note that the FS_NEEDSFSCK flag is set and cleared only by the
 * fsck utility. It is set when background fsck finds an unexpected
 * inconsistency which requires a traditional foreground fsck to be
 * run. Such inconsistencies should only be found after an uncorrectable
 * disk error. A foreground fsck will clear the FS_NEEDSFSCK flag when
 * it has successfully cleaned up the filesystem. The kernel uses this
 * flag to enforce that inconsistent filesystems be mounted read-only.
 * The FS_INDEXDIRS flag when set indicates that the kernel maintains
 * on-disk auxiliary indexes (such as B-trees) for speeding directory
 * accesses. Kernels that do not support auxiliary indices clear the
 * flag to indicate that the indices need to be rebuilt (by fsck) before
 * they can be used. When a filesystem is mounted, any flags not
 * included in FS_SUPPORTED are cleared. This lets newer features
 * know that the filesystem has been run on an older version of the
 * filesystem and thus that data structures associated with those
 * features are out-of-date and need to be rebuilt.
 *
 * FS_ACLS indicates that POSIX.1e ACLs are administratively enabled
 * for the file system, so they should be loaded from extended attributes,
 * observed for access control purposes, and be administered by object
 * owners.  FS_NFS4ACLS indicates that NFSv4 ACLs are administratively
 * enabled.  This flag is mutually exclusive with FS_ACLS.  FS_MULTILABEL
 * indicates that the TrustedBSD MAC Framework should attempt to back MAC
 * labels into extended attributes on the file system rather than maintain
 * a single mount label for all objects.
 */
#define	FS_UNCLEAN	0x00000001 /* filesystem not clean at mount */
#define	FS_DOSOFTDEP	0x00000002 /* filesystem using soft dependencies */
#define	FS_NEEDSFSCK	0x00000004 /* filesystem needs sync fsck before mount */
#define	FS_SUJ       	0x00000008 /* Filesystem using softupdate journal */
#define	FS_ACLS		0x00000010 /* file system has POSIX.1e ACLs enabled */
#define	FS_MULTILABEL	0x00000020 /* file system is MAC multi-label */
#define	FS_GJOURNAL	0x00000040 /* gjournaled file system */
#define	FS_FLAGS_UPDATED 0x0000080 /* flags have been moved to new location */
#define	FS_NFS4ACLS	0x00000100 /* file system has NFSv4 ACLs enabled */
#define	FS_METACKHASH	0x00000200 /* kernel supports metadata check hashes */
#define	FS_TRIM		0x00000400 /* issue BIO_DELETE for deleted blocks */
#define	FS_SUPPORTED	0x00FFFFFF /* supported flags, others cleared at mount*/
/*
 * Things that we may someday support, but currently do not.
 * These flags are all cleared so we know if we ran on a kernel
 * that does not support them.
 */
#define	FS_INDEXDIRS	0x01000000 /* kernel supports indexed directories */
#define	FS_VARBLKSIZE	0x02000000 /* kernel supports variable block sizes */
#define	FS_COOLOPT1	0x04000000 /* kernel supports cool option 1 */
#define	FS_COOLOPT2	0x08000000 /* kernel supports cool option 2 */
#define	FS_COOLOPT3	0x10000000 /* kernel supports cool option 3 */
#define	FS_COOLOPT4	0x20000000 /* kernel supports cool option 4 */
#define	FS_COOLOPT5	0x40000000 /* kernel supports cool option 5 */
#define	FS_COOLOPT6	0x80000000 /* kernel supports cool option 6 */

/*
 * The fs_metackhash field indicates the types of metadata check-hash
 * that are maintained for a filesystem. Not all filesystems check-hash
 * all metadata.
 */
#define	CK_SUPERBLOCK	0x0001	/* the superblock */
#define	CK_CYLGRP	0x0002	/* the cylinder groups */
#define	CK_INODE	0x0004	/* inodes */
#define	CK_INDIR	0x0008	/* indirect blocks */
#define	CK_DIR		0x0010	/* directory contents */
/*
 * The BX_FSPRIV buffer b_xflags are used to track types of data in buffers.
 */
#define	BX_SUPERBLOCK	0x00010000	/* superblock */
#define	BX_CYLGRP	0x00020000	/* cylinder groups */
#define	BX_INODE	0x00040000	/* inodes */
#define	BX_INDIR	0x00080000	/* indirect blocks */
#define	BX_DIR		0x00100000	/* directory contents */

#define	PRINT_UFS_BUF_XFLAGS "\20\25dir\24indir\23inode\22cylgrp\21superblock"

/*
 * Macros to access bits in the fs_active array.
 */
#define	ACTIVECGNUM(fs, cg)	((fs)->fs_active[(cg) / (NBBY * sizeof(int))])
#define	ACTIVECGOFF(cg)		(1 << ((cg) % (NBBY * sizeof(int))))
#define	ACTIVESET(fs, cg)	do {					\
	if ((fs)->fs_active)						\
		ACTIVECGNUM((fs), (cg)) |= ACTIVECGOFF((cg));		\
} while (0)
#define	ACTIVECLEAR(fs, cg)	do {					\
	if ((fs)->fs_active)						\
		ACTIVECGNUM((fs), (cg)) &= ~ACTIVECGOFF((cg));		\
} while (0)

/*
 * The size of a cylinder group is calculated by CGSIZE. The maximum size
 * is limited by the fact that cylinder groups are at most one block.
 * Its size is derived from the size of the maps maintained in the
 * cylinder group and the (struct cg) size.
 */
#define	CGSIZE(fs) \
    /* base cg */	(sizeof(struct cg) + sizeof(int32_t) + \
    /* old btotoff */	(fs)->fs_old_cpg * sizeof(int32_t) + \
    /* old boff */	(fs)->fs_old_cpg * sizeof(u_int16_t) + \
    /* inode map */	howmany((fs)->fs_ipg, NBBY) + \
    /* block map */	howmany((fs)->fs_fpg, NBBY) +\
    /* if present */	((fs)->fs_contigsumsize <= 0 ? 0 : \
    /* cluster sum */	(fs)->fs_contigsumsize * sizeof(int32_t) + \
    /* cluster map */	howmany(fragstoblks(fs, (fs)->fs_fpg), NBBY)))

/*
 * The minimal number of cylinder groups that should be created.
 */
#define	MINCYLGRPS	4

/*
 * Convert cylinder group to base address of its global summary info.
 */
#define	fs_cs(fs, indx) fs_csp[indx]

/*
 * Cylinder group block for a filesystem.
 */
#define	CG_MAGIC	0x090255
struct cg {
	int32_t	 cg_firstfield;		/* historic cyl groups linked list */
	int32_t	 cg_magic;		/* magic number */
	int32_t  cg_old_time;		/* time last written */
	u_int32_t cg_cgx;		/* we are the cgx'th cylinder group */
	int16_t	 cg_old_ncyl;		/* number of cyl's this cg */
	int16_t  cg_old_niblk;		/* number of inode blocks this cg */
	u_int32_t cg_ndblk;		/* number of data blocks this cg */
	struct	 csum cg_cs;		/* cylinder summary information */
	u_int32_t cg_rotor;		/* position of last used block */
	u_int32_t cg_frotor;		/* position of last used frag */
	u_int32_t cg_irotor;		/* position of last used inode */
	u_int32_t cg_frsum[MAXFRAG];	/* counts of available frags */
	int32_t	 cg_old_btotoff;	/* (int32) block totals per cylinder */
	int32_t	 cg_old_boff;		/* (u_int16) free block positions */
	u_int32_t cg_iusedoff;		/* (u_int8) used inode map */
	u_int32_t cg_freeoff;		/* (u_int8) free block map */
	u_int32_t cg_nextfreeoff;	/* (u_int8) next available space */
	u_int32_t cg_clustersumoff;	/* (u_int32) counts of avail clusters */
	u_int32_t cg_clusteroff;		/* (u_int8) free cluster map */
	u_int32_t cg_nclusterblks;	/* number of clusters this cg */
	u_int32_t cg_niblk;		/* number of inode blocks this cg */
	u_int32_t cg_initediblk;		/* last initialized inode */
	u_int32_t cg_unrefs;		/* number of unreferenced inodes */
	int32_t	 cg_sparecon32[1];	/* reserved for future use */
	u_int32_t cg_ckhash;		/* check-hash of this cg */
	ufs_time_t cg_time;		/* time last written */
	int64_t	 cg_sparecon64[3];	/* reserved for future use */
	u_int8_t cg_space[1];		/* space for cylinder group maps */
/* actually longer */
};

/*
 * Macros for access to cylinder group array structures
 */
#define	cg_chkmagic(cgp) ((cgp)->cg_magic == CG_MAGIC)
#define	cg_inosused(cgp) \
    ((u_int8_t *)((u_int8_t *)(cgp) + (cgp)->cg_iusedoff))
#define	cg_blksfree(cgp) \
    ((u_int8_t *)((u_int8_t *)(cgp) + (cgp)->cg_freeoff))
#define	cg_clustersfree(cgp) \
    ((u_int8_t *)((u_int8_t *)(cgp) + (cgp)->cg_clusteroff))
#define	cg_clustersum(cgp) \
    ((int32_t *)((uintptr_t)(cgp) + (cgp)->cg_clustersumoff))

/*
 * Turn filesystem block numbers into disk block addresses.
 * This maps filesystem blocks to device size blocks.
 */
#define	fsbtodb(fs, b)	((daddr_t)(b) << (fs)->fs_fsbtodb)
#define	dbtofsb(fs, b)	((b) >> (fs)->fs_fsbtodb)

/*
 * Cylinder group macros to locate things in cylinder groups.
 * They calc filesystem addresses of cylinder group data structures.
 */
#define	cgbase(fs, c)	(((ufs2_daddr_t)(fs)->fs_fpg) * (c))
#define	cgdata(fs, c)	(cgdmin(fs, c) + (fs)->fs_metaspace)	/* data zone */
#define	cgmeta(fs, c)	(cgdmin(fs, c))				/* meta data */
#define	cgdmin(fs, c)	(cgstart(fs, c) + (fs)->fs_dblkno)	/* 1st data */
#define	cgimin(fs, c)	(cgstart(fs, c) + (fs)->fs_iblkno)	/* inode blk */
#define	cgsblock(fs, c)	(cgstart(fs, c) + (fs)->fs_sblkno)	/* super blk */
#define	cgtod(fs, c)	(cgstart(fs, c) + (fs)->fs_cblkno)	/* cg block */
#define	cgstart(fs, c)							\
       ((fs)->fs_magic == FS_UFS2_MAGIC ? cgbase(fs, c) :		\
       (cgbase(fs, c) + (fs)->fs_old_cgoffset * ((c) & ~((fs)->fs_old_cgmask))))

/*
 * Macros for handling inode numbers:
 *     inode number to filesystem block offset.
 *     inode number to cylinder group number.
 *     inode number to filesystem block address.
 */
#define	ino_to_cg(fs, x)	(((ino_t)(x)) / (fs)->fs_ipg)
#define	ino_to_fsba(fs, x)						\
	((ufs2_daddr_t)(cgimin(fs, ino_to_cg(fs, (ino_t)(x))) +		\
	    (blkstofrags((fs), ((((ino_t)(x)) % (fs)->fs_ipg) / INOPB(fs))))))
#define	ino_to_fsbo(fs, x)	(((ino_t)(x)) % INOPB(fs))

/*
 * Give cylinder group number for a filesystem block.
 * Give cylinder group block number for a filesystem block.
 */
#define	dtog(fs, d)	((d) / (fs)->fs_fpg)
#define	dtogd(fs, d)	((d) % (fs)->fs_fpg)

/*
 * Extract the bits for a block from a map.
 * Compute the cylinder and rotational position of a cyl block addr.
 */
#define	blkmap(fs, map, loc) \
    (((map)[(loc) / NBBY] >> ((loc) % NBBY)) & (0xff >> (NBBY - (fs)->fs_frag)))

/*
 * The following macros optimize certain frequently calculated
 * quantities by using shifts and masks in place of divisions
 * modulos and multiplications.
 */
#define	blkoff(fs, loc)		/* calculates (loc % fs->fs_bsize) */ \
	((loc) & (fs)->fs_qbmask)
#define	fragoff(fs, loc)	/* calculates (loc % fs->fs_fsize) */ \
	((loc) & (fs)->fs_qfmask)
#define	lfragtosize(fs, frag)	/* calculates ((off_t)frag * fs->fs_fsize) */ \
	(((off_t)(frag)) << (fs)->fs_fshift)
#define	lblktosize(fs, blk)	/* calculates ((off_t)blk * fs->fs_bsize) */ \
	(((off_t)(blk)) << (fs)->fs_bshift)
/* Use this only when `blk' is known to be small, e.g., < UFS_NDADDR. */
#define	smalllblktosize(fs, blk)    /* calculates (blk * fs->fs_bsize) */ \
	((blk) << (fs)->fs_bshift)
#define	lblkno(fs, loc)		/* calculates (loc / fs->fs_bsize) */ \
	((loc) >> (fs)->fs_bshift)
#define	numfrags(fs, loc)	/* calculates (loc / fs->fs_fsize) */ \
	((loc) >> (fs)->fs_fshift)
#define	blkroundup(fs, size)	/* calculates roundup(size, fs->fs_bsize) */ \
	(((size) + (fs)->fs_qbmask) & (fs)->fs_bmask)
#define	fragroundup(fs, size)	/* calculates roundup(size, fs->fs_fsize) */ \
	(((size) + (fs)->fs_qfmask) & (fs)->fs_fmask)
#define	fragstoblks(fs, frags)	/* calculates (frags / fs->fs_frag) */ \
	((frags) >> (fs)->fs_fragshift)
#define	blkstofrags(fs, blks)	/* calculates (blks * fs->fs_frag) */ \
	((blks) << (fs)->fs_fragshift)
#define	fragnum(fs, fsb)	/* calculates (fsb % fs->fs_frag) */ \
	((fsb) & ((fs)->fs_frag - 1))
#define	blknum(fs, fsb)		/* calculates rounddown(fsb, fs->fs_frag) */ \
	((fsb) &~ ((fs)->fs_frag - 1))

/*
 * Determine the number of available frags given a
 * percentage to hold in reserve.
 */
#define	freespace(fs, percentreserved) \
	(blkstofrags((fs), (fs)->fs_cstotal.cs_nbfree) + \
	(fs)->fs_cstotal.cs_nffree - \
	(((off_t)((fs)->fs_dsize)) * (percentreserved) / 100))

/*
 * Determining the size of a file block in the filesystem.
 */
#define	blksize(fs, ip, lbn) \
	(((lbn) >= UFS_NDADDR || (ip)->i_size >= \
	    (uint64_t)smalllblktosize(fs, (lbn) + 1)) \
	    ? (fs)->fs_bsize \
	    : (fragroundup(fs, blkoff(fs, (ip)->i_size))))
#define	sblksize(fs, size, lbn) \
	(((lbn) >= UFS_NDADDR || (size) >= ((lbn) + 1) << (fs)->fs_bshift) \
	  ? (fs)->fs_bsize \
	  : (fragroundup(fs, blkoff(fs, (size)))))

/*
 * Number of indirects in a filesystem block.
 */
#define	NINDIR(fs)	((fs)->fs_nindir)

/*
 * Indirect lbns are aligned on UFS_NDADDR addresses where single indirects
 * are the negated address of the lowest lbn reachable, double indirects
 * are this lbn - 1 and triple indirects are this lbn - 2.  This yields
 * an unusual bit order to determine level.
 */
static inline int
lbn_level(ufs_lbn_t lbn)
{
	if (lbn >= 0)
		return 0;
	switch (lbn & 0x3) {
	case 0:
		return (0);
	case 1:
		break;
	case 2:
		return (2);
	case 3:
		return (1);
	default:
		break;
	}
	return (-1);
}

static inline ufs_lbn_t
lbn_offset(struct fs *fs, int level)
{
	ufs_lbn_t res;

	for (res = 1; level > 0; level--)
		res *= NINDIR(fs);
	return (res);
}

/*
 * Number of inodes in a secondary storage block/fragment.
 */
#define	INOPB(fs)	((fs)->fs_inopb)
#define	INOPF(fs)	((fs)->fs_inopb >> (fs)->fs_fragshift)

/*
 * Softdep journal record format.
 */

#define	JOP_ADDREF	1	/* Add a reference to an inode. */
#define	JOP_REMREF	2	/* Remove a reference from an inode. */
#define	JOP_NEWBLK	3	/* Allocate a block. */
#define	JOP_FREEBLK	4	/* Free a block or a tree of blocks. */
#define	JOP_MVREF	5	/* Move a reference from one off to another. */
#define	JOP_TRUNC	6	/* Partial truncation record. */
#define	JOP_SYNC	7	/* fsync() complete record. */

#define	JREC_SIZE	32	/* Record and segment header size. */

#define	SUJ_MIN		(4 * 1024 * 1024)	/* Minimum journal size */
#define	SUJ_MAX		(32 * 1024 * 1024)	/* Maximum journal size */
#define	SUJ_FILE	".sujournal"		/* Journal file name */

/*
 * Size of the segment record header.  There is at most one for each disk
 * block in the journal.  The segment header is followed by an array of
 * records.  fsck depends on the first element in each record being 'op'
 * and the second being 'ino'.  Segments may span multiple disk blocks but
 * the header is present on each.
 */
struct jsegrec {
	uint64_t	jsr_seq;	/* Our sequence number */
	uint64_t	jsr_oldest;	/* Oldest valid sequence number */
	uint16_t	jsr_cnt;	/* Count of valid records */
	uint16_t	jsr_blocks;	/* Count of device bsize blocks. */
	uint32_t	jsr_crc;	/* 32bit crc of the valid space */
	ufs_time_t	jsr_time;	/* timestamp for mount instance */
};

/*
 * Reference record.  Records a single link count modification.
 */
struct jrefrec {
	uint32_t	jr_op;
	uint32_t	jr_ino;
	uint32_t	jr_parent;
	uint16_t	jr_nlink;
	uint16_t	jr_mode;
	int64_t		jr_diroff;
	uint64_t	jr_unused;
};

/*
 * Move record.  Records a reference moving within a directory block.  The
 * nlink is unchanged but we must search both locations.
 */
struct jmvrec {
	uint32_t	jm_op;
	uint32_t	jm_ino;
	uint32_t	jm_parent;
	uint16_t	jm_unused;
	int64_t		jm_oldoff;
	int64_t		jm_newoff;
};

/*
 * Block record.  A set of frags or tree of blocks starting at an indirect are
 * freed or a set of frags are allocated.
 */
struct jblkrec {
	uint32_t	jb_op;
	uint32_t	jb_ino;
	ufs2_daddr_t	jb_blkno;
	ufs_lbn_t	jb_lbn;
	uint16_t	jb_frags;
	uint16_t	jb_oldfrags;
	uint32_t	jb_unused;
};

/*
 * Truncation record.  Records a partial truncation so that it may be
 * completed at check time.  Also used for sync records.
 */
struct jtrncrec {
	uint32_t	jt_op;
	uint32_t	jt_ino;
	int64_t		jt_size;
	uint32_t	jt_extsize;
	uint32_t	jt_pad[3];
};

union jrec {
	struct jsegrec	rec_jsegrec;
	struct jrefrec	rec_jrefrec;
	struct jmvrec	rec_jmvrec;
	struct jblkrec	rec_jblkrec;
	struct jtrncrec	rec_jtrncrec;
};

#ifdef CTASSERT
CTASSERT(sizeof(struct jsegrec) == JREC_SIZE);
CTASSERT(sizeof(struct jrefrec) == JREC_SIZE);
CTASSERT(sizeof(struct jmvrec) == JREC_SIZE);
CTASSERT(sizeof(struct jblkrec) == JREC_SIZE);
CTASSERT(sizeof(struct jtrncrec) == JREC_SIZE);
CTASSERT(sizeof(union jrec) == JREC_SIZE);
#endif

extern int inside[], around[];
extern u_char *fragtbl[];

/*
 * IOCTLs used for filesystem write suspension.
 */
#define	UFSSUSPEND	_IOW('U', 1, fsid_t)
#define	UFSRESUME	_IO('U', 2)

#endif

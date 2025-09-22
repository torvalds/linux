/*	$OpenBSD: ext2fs.h,v 1.27 2024/07/15 13:27:36 martijn Exp $	*/
/*	$NetBSD: ext2fs.h,v 1.10 2000/01/28 16:00:23 bouyer Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
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
 *	@(#)fs.h	8.10 (Berkeley) 10/27/94
 *  Modified for ext2fs by Manuel Bouyer.
 */

#include <sys/endian.h>

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
#define BBSIZE		1024
#define SBSIZE		1024
#define	BBOFF		((off_t)(0))
#define	SBOFF		((off_t)(BBOFF + BBSIZE))
#define	BBLOCK		((daddr_t)(0))
#define	SBLOCK		((daddr_t)(BBLOCK + BBSIZE / DEV_BSIZE))

/*
 * Inodes are, like in UFS, 32-bit unsigned integers and therefore ufsino_t.
 * Disk blocks are 32-bit, if the filesystem isn't operating in 64-bit mode
 * (the incompatible ext4 64BIT flag).  More work is needed to properly use
 * daddr_t as the disk block data type on both BE and LE architectures.
 * XXX disk blocks are simply u_int32_t for now.
 */

/*
 * MINBSIZE is the smallest allowable block size.
 * MINBSIZE must be big enough to hold a cylinder group block,
 * thus changes to (struct cg) must keep its size within MINBSIZE.
 * Note that super blocks are always of size SBSIZE,
 * and that both SBSIZE and MAXBSIZE must be >= MINBSIZE.
 * FSIZE means fragment size.
 */
#define LOG_MINBSIZE	10
#define MINBSIZE	(1 << LOG_MINBSIZE)
#define LOG_MINFSIZE	10
#define MINFSIZE	(1 << LOG_MINFSIZE)

/*
 * The path name on which the file system is mounted is maintained
 * in fs_fsmnt. MAXMNTLEN defines the amount of space allocated in
 * the super block for this name.
 */
#define MAXMNTLEN	512

/*
 * MINFREE gives the minimum acceptable percentage of file system
 * blocks which may be free. If the freelist drops below this level
 * only the superuser may continue to allocate blocks. This may
 * be set to 0 if no reserve of free blocks is deemed necessary,
 * however throughput drops by fifty percent if the file system
 * is run at between 95% and 100% full; thus the minimum default
 * value of fs_minfree is 5%. However, to get good clustering
 * performance, 10% is a better choice. hence we use 10% as our
 * default value. With 10% free space, fragmentation is not a
 * problem, so we choose to optimize for time.
 */
#define MINFREE		5

/*
 * Super block for an ext2fs file system.
 */
struct ext2fs {
	u_int32_t  e2fs_icount;		/* Inode count */
	u_int32_t  e2fs_bcount;		/* blocks count */
	u_int32_t  e2fs_rbcount;	/* reserved blocks count */
	u_int32_t  e2fs_fbcount;	/* free blocks count */
	u_int32_t  e2fs_ficount;	/* free inodes count */
	u_int32_t  e2fs_first_dblock;	/* first data block */
	u_int32_t  e2fs_log_bsize;	/* block size = 1024*(2^e2fs_log_bsize) */
	u_int32_t  e2fs_log_fsize;	/* fragment size log2 */
	u_int32_t  e2fs_bpg;		/* blocks per group */
	u_int32_t  e2fs_fpg;		/* frags per group */
	u_int32_t  e2fs_ipg;		/* inodes per group */
	u_int32_t  e2fs_mtime;		/* mount time */
	u_int32_t  e2fs_wtime;		/* write time */
	u_int16_t  e2fs_mnt_count;	/* mount count */
	u_int16_t  e2fs_max_mnt_count;	/* max mount count */
	u_int16_t  e2fs_magic;		/* magic number */
	u_int16_t  e2fs_state;		/* file system state */
	u_int16_t  e2fs_beh;		/* behavior on errors */
	u_int16_t  e2fs_minrev;		/* minor revision level */
	u_int32_t  e2fs_lastfsck;	/* time of last fsck */
	u_int32_t  e2fs_fsckintv;	/* max time between fscks */
	u_int32_t  e2fs_creator;	/* creator OS */
	u_int32_t  e2fs_rev;		/* revision level */
	u_int16_t  e2fs_ruid;		/* default uid for reserved blocks */
	u_int16_t  e2fs_rgid;		/* default gid for reserved blocks */
	/* EXT2_DYNAMIC_REV superblocks */
	u_int32_t  e2fs_first_ino;	/* first non-reserved inode */
	u_int16_t  e2fs_inode_size;	/* size of inode structure */
	u_int16_t  e2fs_block_group_nr;	/* block grp number of this sblk*/
	u_int32_t  e2fs_features_compat; /*  compatible feature set */
	u_int32_t  e2fs_features_incompat; /* incompatible feature set */
	u_int32_t  e2fs_features_rocompat; /* RO-compatible feature set */
	u_int8_t   e2fs_uuid[16];	/* 128-bit uuid for volume */
	char       e2fs_vname[16];	/* volume name */
	char       e2fs_fsmnt[64];	/* name mounted on */
	u_int32_t  e2fs_algo;		/* For compression */
	u_int8_t   e2fs_prealloc;	/* # of blocks to preallocate */
	u_int8_t   e2fs_dir_prealloc;	/* # of blocks to preallocate for dir */
	u_int16_t  e2fs_reserved_ngdb;	/* # of reserved gd blocks for resize */
	/* Ext3 JBD2 journaling. */
	u_int8_t   e2fs_journal_uuid[16];
	u_int32_t  e2fs_journal_ino;
	u_int32_t  e2fs_journal_dev;
	u_int32_t  e2fs_last_orphan;	/* start of list of inodes to delete */
	u_int32_t  e2fs_hash_seed[4];	/* htree hash seed */
	u_int8_t   e2fs_def_hash_version;
	u_int8_t   e2fs_journal_backup_type;
	u_int16_t  e2fs_gdesc_size;
	u_int32_t  e2fs_default_mount_opts;
	u_int32_t  e2fs_first_meta_bg;
	u_int32_t  e2fs_mkfs_time;
	u_int32_t  e2fs_journal_backup[17];
	u_int32_t  e2fs_bcount_hi;	/* high bits of blocks count */
	u_int32_t  e2fs_rbcount_hi;	/* high bits of reserved blocks count */
	u_int32_t  e2fs_fbcount_hi;	/* high bits of free blocks count */
	u_int16_t  e2fs_min_extra_isize; /* all inodes have some bytes */
	u_int16_t  e2fs_want_extra_isize;/* inodes must reserve some bytes */
	u_int32_t  e2fs_flags;		/* miscellaneous flags */
	u_int16_t  e2fs_raid_stride;	/* RAID stride */
	u_int16_t  e2fs_mmpintv;		/* seconds to wait in MMP checking */
	u_int64_t  e2fs_mmpblk;		/* block for multi-mount protection */
	u_int32_t  e2fs_raid_stripe_wid; /* blocks on data disks (N * stride) */
	u_int8_t   e2fs_log_gpf;		/* FLEX_BG group size */
	u_int8_t   e2fs_chksum_type;	/* metadata checksum algorithm used */
	u_int8_t   e2fs_encrypt;		/* versioning level for encryption */
	u_int8_t   e2fs_reserved_pad;
	u_int64_t  e2fs_kbytes_written;	/* number of lifetime kilobytes */
	u_int32_t  e2fs_snapinum;	/* inode number of active snapshot */
	u_int32_t  e2fs_snapid;		/* sequential ID of active snapshot */
	u_int64_t  e2fs_snaprbcount;	/* rsvd blocks for active snapshot */
	u_int32_t  e2fs_snaplist;	/* inode number for on-disk snapshot */
	u_int32_t  e2fs_errcount;	/* number of file system errors */
	u_int32_t  e2fs_first_errtime;	/* first time an error happened */
	u_int32_t  e2fs_first_errino;	/* inode involved in first error */
	u_int64_t  e2fs_first_errblk;	/* block involved of first error */
	u_int8_t   e2fs_first_errfunc[32];/* function where error happened */
	u_int32_t  e2fs_first_errline;	/* line number where error happened */
	u_int32_t  e2fs_last_errtime;	/* most recent time of an error */
	u_int32_t  e2fs_last_errino;	/* inode involved in last error */
	u_int32_t  e2fs_last_errline;	/* line number where error happened */
	u_int64_t  e2fs_last_errblk;	/* block involved of last error */
	u_int8_t   e2fs_last_errfunc[32];/* function where error happened */
	u_int8_t   e2fs_mount_opts[64];
	u_int32_t  e2fs_usrquota_inum;	/* inode for tracking user quota */
	u_int32_t  e2fs_grpquota_inum;	/* inode for tracking group quota */
	u_int32_t  e2fs_overhead_clusters;/* overhead blocks/clusters */
	u_int32_t  e2fs_backup_bgs[2];	/* groups with sparse_super2 SBs */
	u_int8_t   e2fs_encrypt_algos[4];/* encryption algorithms in use */
	u_int8_t   e2fs_encrypt_pw_salt[16];/* salt used for string2key */
	u_int32_t  e2fs_lpf_ino;		/* location of the lost+found inode */
	u_int32_t  e2fs_proj_quota_inum;	/* inode for tracking project quota */
	u_int32_t  e2fs_chksum_seed;	/* checksum seed */
	u_int32_t  e2fs_reserved[98];	/* padding to the end of the block */
	u_int32_t  e2fs_sbchksum;	/* superblock checksum */
};


/* in-memory data for ext2fs */
struct m_ext2fs {
	struct ext2fs e2fs;
	u_char	e2fs_fsmnt[MAXMNTLEN];	/* name mounted on */
	int8_t	e2fs_ronly;	/* mounted read-only flag */
	int8_t	e2fs_fmod;	/* super block modified flag */
	int32_t e2fs_fsize;	/* fragment size */
	int32_t	e2fs_bsize;	/* block size */
	int32_t e2fs_bshift;	/* ``lblkno'' calc of logical blkno */
	int32_t e2fs_bmask;	/* ``blkoff'' calc of blk offsets */
	int64_t e2fs_qbmask;	/* ~fs_bmask - for use with quad size */
	int32_t	e2fs_fsbtodb;	/* fsbtodb and dbtofsb shift constant */
	int32_t	e2fs_ncg;	/* number of cylinder groups */
	int32_t	e2fs_ngdb;	/* number of group descriptor block */
	int32_t	e2fs_ipb;	/* number of inodes per block */
	int32_t	e2fs_itpg;	/* number of inode table per group */
	off_t	e2fs_maxfilesize;	/* depends on LARGE/HUGE flags */
	struct	ext2_gd *e2fs_gd; /* group descriptors */
};

static inline int
e2fs_overflow(struct m_ext2fs *fs, off_t lower, off_t value)
{
	return (value < lower || value > fs->e2fs_maxfilesize);
}

/*
 * Filesystem identification
 */
#define	E2FS_MAGIC	0xef53	/* the ext2fs magic number */
#define E2FS_REV0	0	/* revision levels */
#define E2FS_REV1	1	/* revision levels */

/* compatible/incompatible features */
#define EXT2F_COMPAT_PREALLOC		0x0001
#define EXT2F_COMPAT_IMAGIC_INODES	0x0002
#define EXT2F_COMPAT_HAS_JOURNAL	0x0004
#define EXT2F_COMPAT_EXT_ATTR		0x0008
#define EXT2F_COMPAT_RESIZE		0x0010
#define EXT2F_COMPAT_DIR_INDEX		0x0020
#define EXT2F_COMPAT_SPARSE_SUPER2	0x0200

#define EXT2F_ROCOMPAT_SPARSE_SUPER	0x0001
#define EXT2F_ROCOMPAT_LARGE_FILE	0x0002
#define EXT2F_ROCOMPAT_BTREE_DIR	0x0004
#define EXT2F_ROCOMPAT_HUGE_FILE	0x0008
#define EXT2F_ROCOMPAT_GDT_CSUM		0x0010
#define EXT2F_ROCOMPAT_DIR_NLINK	0x0020
#define EXT2F_ROCOMPAT_EXTRA_ISIZE	0x0040
#define EXT2F_ROCOMPAT_QUOTA		0x0100
#define EXT2F_ROCOMPAT_BIGALLOC		0x0200
#define EXT2F_ROCOMPAT_METADATA_CKSUM	0x0400
#define EXT2F_ROCOMPAT_READONLY		0x1000
#define EXT2F_ROCOMPAT_PROJECT		0x2000

#define EXT2F_INCOMPAT_COMP		0x0001
#define EXT2F_INCOMPAT_FTYPE		0x0002
#define EXT2F_INCOMPAT_RECOVER		0x0004
#define EXT2F_INCOMPAT_JOURNAL_DEV	0x0008
#define EXT2F_INCOMPAT_META_BG		0x0010
#define EXT2F_INCOMPAT_EXTENTS		0x0040
#define EXT2F_INCOMPAT_64BIT		0x0080
#define EXT2F_INCOMPAT_MMP		0x0100
#define EXT2F_INCOMPAT_FLEX_BG		0x0200
#define EXT2F_INCOMPAT_EA_INODE		0x0400
#define EXT2F_INCOMPAT_DIRDATA		0x1000
#define EXT2F_INCOMPAT_CSUM_SEED	0x2000
#define EXT2F_INCOMPAT_LARGEDIR		0x4000
#define EXT2F_INCOMPAT_INLINE_DATA	0x8000
#define EXT2F_INCOMPAT_ENCRYPT		0x10000

struct ext2_feature {
	uint32_t mask;
	const char *name;
};

static const struct ext2_feature ro_compat[] = {
	{ EXT2F_ROCOMPAT_SPARSE_SUPER,		"sparse_super" },
	{ EXT2F_ROCOMPAT_LARGE_FILE,		"large_file" },
	{ EXT2F_ROCOMPAT_BTREE_DIR,		"btree_dir" },
	{ EXT2F_ROCOMPAT_HUGE_FILE,		"huge_file" },
	{ EXT2F_ROCOMPAT_GDT_CSUM,		"uninit_bg" },
	{ EXT2F_ROCOMPAT_DIR_NLINK,		"dir_nlink" },
	{ EXT2F_ROCOMPAT_EXTRA_ISIZE,		"extra_isize" },
	{ EXT2F_ROCOMPAT_QUOTA,			"quota" },
	{ EXT2F_ROCOMPAT_BIGALLOC,		"bigalloc" },
	{ EXT2F_ROCOMPAT_METADATA_CKSUM,	"metadata_csum" },
	{ EXT2F_ROCOMPAT_READONLY,		"read-only" },
	{ EXT2F_ROCOMPAT_PROJECT,		"project" }
};

static const struct ext2_feature incompat[] = {
	{ EXT2F_INCOMPAT_COMP,		"compression" },
	{ EXT2F_INCOMPAT_FTYPE,		"filetype" },
	{ EXT2F_INCOMPAT_RECOVER,	"needs_recovery" },
	{ EXT2F_INCOMPAT_JOURNAL_DEV,	"journal_dev" },
	{ EXT2F_INCOMPAT_META_BG,	"meta_bg" },
	{ EXT2F_INCOMPAT_EXTENTS,	"extents" },
	{ EXT2F_INCOMPAT_64BIT,		"64bit" },
	{ EXT2F_INCOMPAT_MMP,		"mmp" },
	{ EXT2F_INCOMPAT_FLEX_BG,	"flex_bg" },
	{ EXT2F_INCOMPAT_EA_INODE,	"ea_inode" },
	{ EXT2F_INCOMPAT_DIRDATA,	"dirdata" },
	{ EXT2F_INCOMPAT_CSUM_SEED,	"metadata_csum_seed" },
	{ EXT2F_INCOMPAT_LARGEDIR,	"large_dir" },
	{ EXT2F_INCOMPAT_INLINE_DATA,	"inline_data" },
	{ EXT2F_INCOMPAT_ENCRYPT,	"encrypt" }
};

/* features supported in this implementation */
#define EXT2F_COMPAT_SUPP		0x0000
#define EXT2F_ROCOMPAT_SUPP		(EXT2F_ROCOMPAT_SPARSE_SUPER | \
					 EXT2F_ROCOMPAT_LARGE_FILE)
#define EXT2F_INCOMPAT_SUPP		(EXT2F_INCOMPAT_FTYPE)
#define EXT4F_RO_INCOMPAT_SUPP		(EXT2F_INCOMPAT_EXTENTS | \
					 EXT2F_INCOMPAT_FLEX_BG | \
					 EXT2F_INCOMPAT_META_BG | \
					 EXT2F_INCOMPAT_RECOVER)

/*
 * Definitions of behavior on errors
 */
#define E2FS_BEH_CONTINUE	1	/* continue operation */
#define E2FS_BEH_READONLY	2	/* remount fs read only */
#define E2FS_BEH_PANIC		3	/* cause panic */
#define E2FS_BEH_DEFAULT	E2FS_BEH_CONTINUE

/*
 * OS identification
 */
#define E2FS_OS_LINUX 0
#define E2FS_OS_HURD  1
#define E2FS_OS_MASIX 2

/*
 * Filesystem clean flags
 */
#define	E2FS_ISCLEAN	0x01
#define	E2FS_ERRORS	0x02

/* ext2 file system block group descriptor */

struct ext2_gd {
	u_int32_t ext2bgd_b_bitmap;	/* blocks bitmap block */
	u_int32_t ext2bgd_i_bitmap;	/* inodes bitmap block */
	u_int32_t ext2bgd_i_tables;	/* inodes table block  */
	u_int16_t ext2bgd_nbfree;	/* number of free blocks */
	u_int16_t ext2bgd_nifree;	/* number of free inodes */
	u_int16_t ext2bgd_ndirs;	/* number of directories */
	u_int16_t reserved;
	u_int32_t reserved2[3];
};

/*
 * If the EXT2F_ROCOMPAT_SPARSE_SUPER flag is set, the cylinder group has a
 * copy of the super and cylinder group descriptors blocks only if it's
 * a power of 3, 5 or 7
 */

static __inline__ int cg_has_sb(int) __attribute__((__unused__));
static __inline int
cg_has_sb(int i)
{
	int a3 ,a5 , a7;

	if (i == 0 || i == 1)
		return 1;
	for (a3 = 3, a5 = 5, a7 = 7;
	    a3 <= i || a5 <= i || a7 <= i;
	    a3 *= 3, a5 *= 5, a7 *= 7)
		if (i == a3 || i == a5 || i == a7)
			return 1;
	return 0;
}

/*
 * Ext2 metadata is stored in little-endian byte order.
 * JBD2 journal used in ext3 and ext4 is big-endian!
 */
#if BYTE_ORDER == LITTLE_ENDIAN
#define e2fs_sbload(old, new) memcpy((new), (old), SBSIZE);
#define e2fs_cgload(old, new, size) memcpy((new), (old), (size));
#define e2fs_sbsave(old, new) memcpy((new), (old), SBSIZE);
#define e2fs_cgsave(old, new, size) memcpy((new), (old), (size));
#else
void e2fs_sb_bswap(struct ext2fs *, struct ext2fs *);
void e2fs_cg_bswap(struct ext2_gd *, struct ext2_gd *, int);
#define e2fs_sbload(old, new) e2fs_sb_bswap((old), (new))
#define e2fs_cgload(old, new, size) e2fs_cg_bswap((old), (new), (size));
#define e2fs_sbsave(old, new) e2fs_sb_bswap((old), (new))
#define e2fs_cgsave(old, new, size) e2fs_cg_bswap((old), (new), (size));
#endif

/*
 * Turn file system block numbers into disk block addresses.
 * This maps file system blocks to device size blocks.
 */
#define fsbtodb(fs, b)	((b) << (fs)->e2fs_fsbtodb)
#define dbtofsb(fs, b)	((b) >> (fs)->e2fs_fsbtodb)

/*
 * Macros for handling inode numbers:
 *	 inode number to file system block offset.
 *	 inode number to cylinder group number.
 *	 inode number to file system block address.
 */
#define	ino_to_cg(fs, x)	(((x) - 1) / (fs)->e2fs.e2fs_ipg)
#define	ino_to_fsba(fs, x)						\
	((fs)->e2fs_gd[ino_to_cg(fs, x)].ext2bgd_i_tables + \
	(((x)-1) % (fs)->e2fs.e2fs_ipg)/(fs)->e2fs_ipb)
#define	ino_to_fsbo(fs, x)	(((x)-1) % (fs)->e2fs_ipb)

/*
 * Give cylinder group number for a file system block.
 * Give cylinder group block number for a file system block.
 */
#define	dtog(fs, d) (((d) - (fs)->e2fs.e2fs_first_dblock) / (fs)->e2fs.e2fs_fpg)
#define	dtogd(fs, d) \
	(((d) - (fs)->e2fs.e2fs_first_dblock) % (fs)->e2fs.e2fs_fpg)

/*
 * The following macros optimize certain frequently calculated
 * quantities by using shifts and masks in place of divisions
 * modulos and multiplications.
 */
#define blkoff(fs, loc)		/* calculates (loc % fs->e2fs_bsize) */ \
	((loc) & (fs)->e2fs_qbmask)
#define lblktosize(fs, blk)	/* calculates (blk * fs->e2fs_bsize) */ \
	((blk) << (fs)->e2fs_bshift)
#define lblkno(fs, loc)		/* calculates (loc / fs->e2fs_bsize) */ \
	((loc) >> (fs)->e2fs_bshift)
#define blkroundup(fs, size)	/* calculates roundup(size, fs->e2fs_bsize) */ \
	(((size) + (fs)->e2fs_qbmask) & (fs)->e2fs_bmask)
#define fragroundup(fs, size)	/* calculates roundup(size, fs->e2fs_bsize) */ \
	(((size) + (fs)->e2fs_qbmask) & (fs)->e2fs_bmask)
/*
 * Determine the number of available frags given a
 * percentage to hold in reserve.
 */
#define freespace(fs) \
   ((fs)->e2fs.e2fs_fbcount - (fs)->e2fs.e2fs_rbcount)

/*
 * Number of indirects in a file system block.
 */
#define	NINDIR(fs)	((fs)->e2fs_bsize / sizeof(u_int32_t))

/*-
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 *
 * $FreeBSD$
 */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Aditya Sarawgi
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 */

#ifndef _FS_EXT2FS_EXT2FS_H_
#define	_FS_EXT2FS_EXT2FS_H_

#include <sys/types.h>

/*
 * Super block for an ext2fs file system.
 */
struct ext2fs {
	uint32_t  e2fs_icount;		/* Inode count */
	uint32_t  e2fs_bcount;		/* blocks count */
	uint32_t  e2fs_rbcount;		/* reserved blocks count */
	uint32_t  e2fs_fbcount;		/* free blocks count */
	uint32_t  e2fs_ficount;		/* free inodes count */
	uint32_t  e2fs_first_dblock;	/* first data block */
	uint32_t  e2fs_log_bsize;	/* block size = 1024*(2^e2fs_log_bsize) */
	uint32_t  e2fs_log_fsize;	/* fragment size */
	uint32_t  e2fs_bpg;		/* blocks per group */
	uint32_t  e2fs_fpg;		/* frags per group */
	uint32_t  e2fs_ipg;		/* inodes per group */
	uint32_t  e2fs_mtime;		/* mount time */
	uint32_t  e2fs_wtime;		/* write time */
	uint16_t  e2fs_mnt_count;	/* mount count */
	uint16_t  e2fs_max_mnt_count;	/* max mount count */
	uint16_t  e2fs_magic;		/* magic number */
	uint16_t  e2fs_state;		/* file system state */
	uint16_t  e2fs_beh;		/* behavior on errors */
	uint16_t  e2fs_minrev;		/* minor revision level */
	uint32_t  e2fs_lastfsck;	/* time of last fsck */
	uint32_t  e2fs_fsckintv;	/* max time between fscks */
	uint32_t  e2fs_creator;		/* creator OS */
	uint32_t  e2fs_rev;		/* revision level */
	uint16_t  e2fs_ruid;		/* default uid for reserved blocks */
	uint16_t  e2fs_rgid;		/* default gid for reserved blocks */
	/* EXT2_DYNAMIC_REV superblocks */
	uint32_t  e2fs_first_ino;	/* first non-reserved inode */
	uint16_t  e2fs_inode_size;	/* size of inode structure */
	uint16_t  e2fs_block_group_nr;	/* block grp number of this sblk*/
	uint32_t  e2fs_features_compat;	/* compatible feature set */
	uint32_t  e2fs_features_incompat; /* incompatible feature set */
	uint32_t  e2fs_features_rocompat; /* RO-compatible feature set */
	uint8_t	  e2fs_uuid[16];	/* 128-bit uuid for volume */
	char      e2fs_vname[16];	/* volume name */
	char      e2fs_fsmnt[64];	/* name mounted on */
	uint32_t  e2fs_algo;		/* For compression */
	uint8_t   e2fs_prealloc;	/* # of blocks for old prealloc */
	uint8_t   e2fs_dir_prealloc;	/* # of blocks for old prealloc dirs */
	uint16_t  e2fs_reserved_ngdb;	/* # of reserved gd blocks for resize */
	char      e3fs_journal_uuid[16]; /* uuid of journal superblock */
	uint32_t  e3fs_journal_inum;	/* inode number of journal file */
	uint32_t  e3fs_journal_dev;	/* device number of journal file */
	uint32_t  e3fs_last_orphan;	/* start of list of inodes to delete */
	uint32_t  e3fs_hash_seed[4];	/* HTREE hash seed */
	char      e3fs_def_hash_version;/* Default hash version to use */
	char      e3fs_jnl_backup_type;
	uint16_t  e3fs_desc_size;	/* size of group descriptor */
	uint32_t  e3fs_default_mount_opts;
	uint32_t  e3fs_first_meta_bg;	/* First metablock block group */
	uint32_t  e3fs_mkfs_time;	/* when the fs was created */
	uint32_t  e3fs_jnl_blks[17];	/* backup of the journal inode */
	uint32_t  e4fs_bcount_hi;	/* high bits of blocks count */
	uint32_t  e4fs_rbcount_hi;	/* high bits of reserved blocks count */
	uint32_t  e4fs_fbcount_hi;	/* high bits of free blocks count */
	uint16_t  e4fs_min_extra_isize; /* all inodes have some bytes */
	uint16_t  e4fs_want_extra_isize;/* inodes must reserve some bytes */
	uint32_t  e4fs_flags;		/* miscellaneous flags */
	uint16_t  e4fs_raid_stride;	/* RAID stride */
	uint16_t  e4fs_mmpintv;		/* seconds to wait in MMP checking */
	uint64_t  e4fs_mmpblk;		/* block for multi-mount protection */
	uint32_t  e4fs_raid_stripe_wid; /* blocks on data disks (N * stride) */
	uint8_t   e4fs_log_gpf;		/* FLEX_BG group size */
	uint8_t   e4fs_chksum_type;	/* metadata checksum algorithm used */
	uint8_t   e4fs_encrypt;		/* versioning level for encryption */
	uint8_t   e4fs_reserved_pad;
	uint64_t  e4fs_kbytes_written;	/* number of lifetime kilobytes */
	uint32_t  e4fs_snapinum;	/* inode number of active snapshot */
	uint32_t  e4fs_snapid;		/* sequential ID of active snapshot */
	uint64_t  e4fs_snaprbcount;	/* reserved blocks for active snapshot */
	uint32_t  e4fs_snaplist;	/* inode number for on-disk snapshot */
	uint32_t  e4fs_errcount;	/* number of file system errors */
	uint32_t  e4fs_first_errtime;	/* first time an error happened */
	uint32_t  e4fs_first_errino;	/* inode involved in first error */
	uint64_t  e4fs_first_errblk;	/* block involved of first error */
	uint8_t   e4fs_first_errfunc[32];/* function where error happened */
	uint32_t  e4fs_first_errline;	/* line number where error happened */
	uint32_t  e4fs_last_errtime;	/* most recent time of an error */
	uint32_t  e4fs_last_errino;	/* inode involved in last error */
	uint32_t  e4fs_last_errline;	/* line number where error happened */
	uint64_t  e4fs_last_errblk;	/* block involved of last error */
	uint8_t   e4fs_last_errfunc[32]; /* function where error happened */
	uint8_t   e4fs_mount_opts[64];
	uint32_t  e4fs_usrquota_inum;	/* inode for tracking user quota */
	uint32_t  e4fs_grpquota_inum;	/* inode for tracking group quota */
	uint32_t  e4fs_overhead_clusters;/* overhead blocks/clusters */
	uint32_t  e4fs_backup_bgs[2];	/* groups with sparse_super2 SBs */
	uint8_t   e4fs_encrypt_algos[4];/* encryption algorithms in use */
	uint8_t   e4fs_encrypt_pw_salt[16];/* salt used for string2key */
	uint32_t  e4fs_lpf_ino;		/* location of the lost+found inode */
	uint32_t  e4fs_proj_quota_inum;	/* inode for tracking project quota */
	uint32_t  e4fs_chksum_seed;	/* checksum seed */
	uint32_t  e4fs_reserved[98];	/* padding to the end of the block */
	uint32_t  e4fs_sbchksum;	/* superblock checksum */
};

/*
 * The path name on which the file system is mounted is maintained
 * in fs_fsmnt. MAXMNTLEN defines the amount of space allocated in
 * the super block for this name.
 */
#define	MAXMNTLEN 512

/*
 * In-Memory Superblock
 */

struct m_ext2fs {
	struct ext2fs * e2fs;
	char     e2fs_fsmnt[MAXMNTLEN];/* name mounted on */
	char     e2fs_ronly;	  /* mounted read-only flag */
	char     e2fs_fmod;	  /* super block modified flag */
	uint64_t e2fs_bcount;	  /* blocks count */
	uint64_t e2fs_rbcount;	  /* reserved blocks count */
	uint64_t e2fs_fbcount;	  /* free blocks count */
	uint32_t e2fs_bsize;	  /* Block size */
	uint32_t e2fs_bshift;	  /* calc of logical block no */
	uint32_t e2fs_bpg;	  /* Number of blocks per group */
	int64_t  e2fs_qbmask;	  /* = s_blocksize -1 */
	uint32_t e2fs_fsbtodb;	  /* Shift to get disk block */
	uint32_t e2fs_ipg;	  /* Number of inodes per group */
	uint32_t e2fs_ipb;	  /* Number of inodes per block */
	uint32_t e2fs_itpg;	  /* Number of inode table per group */
	uint32_t e2fs_fsize;	  /* Size of fragments per block */
	uint32_t e2fs_fpb;	  /* Number of fragments per block */
	uint32_t e2fs_fpg;	  /* Number of fragments per group */
	uint32_t e2fs_gdbcount;	  /* Number of group descriptors */
	uint32_t e2fs_gcount;	  /* Number of groups */
	uint32_t e2fs_isize;	  /* Size of inode */
	uint32_t e2fs_total_dir;  /* Total number of directories */
	uint8_t	*e2fs_contigdirs; /* (u) # of contig. allocated dirs */
	char     e2fs_wasvalid;	  /* valid at mount time */
	off_t    e2fs_maxfilesize;
	struct   ext2_gd *e2fs_gd; /* Group Descriptors */
	int32_t  e2fs_contigsumsize;    /* size of cluster summary array */
	int32_t *e2fs_maxcluster;       /* max cluster in each cyl group */
	struct   csum *e2fs_clustersum; /* cluster summary in each cyl group */
	int32_t  e2fs_uhash;	  /* 3 if hash should be signed, 0 if not */
	uint32_t e2fs_csum_seed;  /* sb checksum seed */
};

/* cluster summary information */

struct csum {
	int8_t   cs_init; /* cluster summary has been initialized */
	int32_t *cs_sum;  /* cluster summary array */
};

/*
 * The second extended file system magic number
 */
#define	E2FS_MAGIC		0xEF53

/*
 * Revision levels
 */
#define	E2FS_REV0		0	/* The good old (original) format */
#define	E2FS_REV1		1	/* V2 format w/ dynamic inode sizes */

#define	E2FS_REV0_INODE_SIZE 128

/*
 * Metadata checksum algorithm codes
 */
#define EXT4_CRC32C_CHKSUM		1

/*
 * compatible/incompatible features
 */
#define	EXT2F_COMPAT_PREALLOC		0x0001
#define	EXT2F_COMPAT_IMAGIC_INODES	0x0002
#define	EXT2F_COMPAT_HASJOURNAL		0x0004
#define	EXT2F_COMPAT_EXT_ATTR		0x0008
#define	EXT2F_COMPAT_RESIZE		0x0010
#define	EXT2F_COMPAT_DIRHASHINDEX	0x0020
#define	EXT2F_COMPAT_LAZY_BG		0x0040
#define	EXT2F_COMPAT_EXCLUDE_BITMAP	0x0100
#define	EXT2F_COMPAT_SPARSESUPER2	0x0200

#define	EXT2F_ROCOMPAT_SPARSESUPER	0x0001
#define	EXT2F_ROCOMPAT_LARGEFILE	0x0002
#define	EXT2F_ROCOMPAT_BTREE_DIR	0x0004
#define	EXT2F_ROCOMPAT_HUGE_FILE	0x0008
#define	EXT2F_ROCOMPAT_GDT_CSUM		0x0010
#define	EXT2F_ROCOMPAT_DIR_NLINK	0x0020
#define	EXT2F_ROCOMPAT_EXTRA_ISIZE	0x0040
#define	EXT2F_ROCOMPAT_HAS_SNAPSHOT	0x0080
#define	EXT2F_ROCOMPAT_QUOTA		0x0100
#define	EXT2F_ROCOMPAT_BIGALLOC		0x0200
#define	EXT2F_ROCOMPAT_METADATA_CKSUM	0x0400
#define	EXT2F_ROCOMPAT_REPLICA		0x0800
#define	EXT2F_ROCOMPAT_READONLY		0x1000
#define	EXT2F_ROCOMPAT_PROJECT		0x2000

#define	EXT2F_INCOMPAT_COMP		0x0001
#define	EXT2F_INCOMPAT_FTYPE		0x0002
#define	EXT2F_INCOMPAT_RECOVER		0x0004
#define	EXT2F_INCOMPAT_JOURNAL_DEV	0x0008
#define	EXT2F_INCOMPAT_META_BG		0x0010
#define	EXT2F_INCOMPAT_EXTENTS		0x0040
#define	EXT2F_INCOMPAT_64BIT		0x0080
#define	EXT2F_INCOMPAT_MMP		0x0100
#define	EXT2F_INCOMPAT_FLEX_BG		0x0200
#define	EXT2F_INCOMPAT_EA_INODE		0x0400
#define	EXT2F_INCOMPAT_DIRDATA		0x1000
#define	EXT2F_INCOMPAT_CSUM_SEED	0x2000
#define	EXT2F_INCOMPAT_LARGEDIR		0x4000
#define	EXT2F_INCOMPAT_INLINE_DATA	0x8000
#define	EXT2F_INCOMPAT_ENCRYPT		0x10000

struct ext2_feature
{
	int mask;
	const char *name;
};

static const struct ext2_feature compat[] = {
	{ EXT2F_COMPAT_PREALLOC,       "dir_prealloc"    },
	{ EXT2F_COMPAT_IMAGIC_INODES,  "imagic_inodes"   },
	{ EXT2F_COMPAT_HASJOURNAL,     "has_journal"     },
	{ EXT2F_COMPAT_EXT_ATTR,       "ext_attr"        },
	{ EXT2F_COMPAT_RESIZE,         "resize_inode"    },
	{ EXT2F_COMPAT_DIRHASHINDEX,   "dir_index"       },
	{ EXT2F_COMPAT_EXCLUDE_BITMAP, "snapshot_bitmap" },
	{ EXT2F_COMPAT_SPARSESUPER2,   "sparse_super2"   }
};

static const struct ext2_feature ro_compat[] = {
	{ EXT2F_ROCOMPAT_SPARSESUPER,    "sparse_super"  },
	{ EXT2F_ROCOMPAT_LARGEFILE,      "large_file"    },
	{ EXT2F_ROCOMPAT_BTREE_DIR,      "btree_dir"     },
	{ EXT2F_ROCOMPAT_HUGE_FILE,      "huge_file"     },
	{ EXT2F_ROCOMPAT_GDT_CSUM,       "uninit_groups" },
	{ EXT2F_ROCOMPAT_DIR_NLINK,      "dir_nlink"     },
	{ EXT2F_ROCOMPAT_EXTRA_ISIZE,    "extra_isize"   },
	{ EXT2F_ROCOMPAT_HAS_SNAPSHOT,   "snapshot"      },
	{ EXT2F_ROCOMPAT_QUOTA,          "quota"         },
	{ EXT2F_ROCOMPAT_BIGALLOC,       "bigalloc"      },
	{ EXT2F_ROCOMPAT_METADATA_CKSUM, "metadata_csum" },
	{ EXT2F_ROCOMPAT_REPLICA,        "replica"       },
	{ EXT2F_ROCOMPAT_READONLY,       "ro"            },
	{ EXT2F_ROCOMPAT_PROJECT,        "project"       }
};

static const struct ext2_feature incompat[] = {
	{ EXT2F_INCOMPAT_COMP,        "compression"        },
	{ EXT2F_INCOMPAT_FTYPE,       "filetype"           },
	{ EXT2F_INCOMPAT_RECOVER,     "needs_recovery"     },
	{ EXT2F_INCOMPAT_JOURNAL_DEV, "journal_dev"        },
	{ EXT2F_INCOMPAT_META_BG,     "meta_bg"            },
	{ EXT2F_INCOMPAT_EXTENTS,     "extents"            },
	{ EXT2F_INCOMPAT_64BIT,       "64bit"              },
	{ EXT2F_INCOMPAT_MMP,         "mmp"                },
	{ EXT2F_INCOMPAT_FLEX_BG,     "flex_bg"            },
	{ EXT2F_INCOMPAT_EA_INODE,    "ea_inode"           },
	{ EXT2F_INCOMPAT_DIRDATA,     "dirdata"            },
	{ EXT2F_INCOMPAT_CSUM_SEED,   "metadata_csum_seed" },
	{ EXT2F_INCOMPAT_LARGEDIR,    "large_dir"          },
	{ EXT2F_INCOMPAT_INLINE_DATA, "inline_data"        },
	{ EXT2F_INCOMPAT_ENCRYPT,     "encrypt"            }
};

/*
 * Features supported in this implementation
 *
 * We support the following REV1 features:
 * - EXT2F_ROCOMPAT_SPARSESUPER
 * - EXT2F_ROCOMPAT_LARGEFILE
 * - EXT2F_ROCOMPAT_EXTRA_ISIZE
 * - EXT2F_INCOMPAT_FTYPE
 *
 * We partially (read-only) support the following EXT4 features:
 * - EXT2F_ROCOMPAT_HUGE_FILE
 * - EXT2F_INCOMPAT_EXTENTS
 *
 */
#define	EXT2F_COMPAT_SUPP		EXT2F_COMPAT_DIRHASHINDEX
#define	EXT2F_ROCOMPAT_SUPP		(EXT2F_ROCOMPAT_SPARSESUPER | \
					 EXT2F_ROCOMPAT_LARGEFILE | \
					 EXT2F_ROCOMPAT_GDT_CSUM | \
					 EXT2F_ROCOMPAT_METADATA_CKSUM | \
					 EXT2F_ROCOMPAT_DIR_NLINK | \
					 EXT2F_ROCOMPAT_HUGE_FILE | \
					 EXT2F_ROCOMPAT_EXTRA_ISIZE)
#define	EXT2F_INCOMPAT_SUPP		(EXT2F_INCOMPAT_FTYPE | \
					 EXT2F_INCOMPAT_META_BG | \
					 EXT2F_INCOMPAT_EXTENTS | \
					 EXT2F_INCOMPAT_64BIT | \
					 EXT2F_INCOMPAT_FLEX_BG | \
					 EXT2F_INCOMPAT_CSUM_SEED)

/* Assume that user mode programs are passing in an ext2fs superblock, not
 * a kernel struct super_block.  This will allow us to call the feature-test
 * macros from user land. */
#define	EXT2_SB(sb)	(sb)

/*
 * Feature set definitions
 */
#define	EXT2_HAS_COMPAT_FEATURE(sb,mask)			\
	( EXT2_SB(sb)->e2fs->e2fs_features_compat & htole32(mask) )
#define	EXT2_HAS_RO_COMPAT_FEATURE(sb,mask)			\
	( EXT2_SB(sb)->e2fs->e2fs_features_rocompat & htole32(mask) )
#define	EXT2_HAS_INCOMPAT_FEATURE(sb,mask)			\
	( EXT2_SB(sb)->e2fs->e2fs_features_incompat & htole32(mask) )

/*
 * File clean flags
 */
#define	E2FS_ISCLEAN			0x0001	/* Unmounted cleanly */
#define	E2FS_ERRORS			0x0002	/* Errors detected */

/*
 * Filesystem miscellaneous flags
 */
#define	E2FS_SIGNED_HASH	0x0001
#define	E2FS_UNSIGNED_HASH	0x0002

#define	EXT2_BG_INODE_UNINIT	0x0001	/* Inode table/bitmap not in use */
#define	EXT2_BG_BLOCK_UNINIT	0x0002	/* Block bitmap not in use */
#define	EXT2_BG_INODE_ZEROED	0x0004	/* On-disk itable initialized to zero */

/* ext2 file system block group descriptor */

struct ext2_gd {
	uint32_t ext2bgd_b_bitmap;	/* blocks bitmap block */
	uint32_t ext2bgd_i_bitmap;	/* inodes bitmap block */
	uint32_t ext2bgd_i_tables;	/* inodes table block  */
	uint16_t ext2bgd_nbfree;	/* number of free blocks */
	uint16_t ext2bgd_nifree;	/* number of free inodes */
	uint16_t ext2bgd_ndirs;		/* number of directories */
	uint16_t ext4bgd_flags;		/* block group flags */
	uint32_t ext4bgd_x_bitmap;	/* snapshot exclusion bitmap loc. */
	uint16_t ext4bgd_b_bmap_csum;	/* block bitmap checksum */
	uint16_t ext4bgd_i_bmap_csum;	/* inode bitmap checksum */
	uint16_t ext4bgd_i_unused;	/* unused inode count */
	uint16_t ext4bgd_csum;		/* group descriptor checksum */
	uint32_t ext4bgd_b_bitmap_hi;	/* high bits of blocks bitmap block */
	uint32_t ext4bgd_i_bitmap_hi;	/* high bits of inodes bitmap block */
	uint32_t ext4bgd_i_tables_hi;	/* high bits of inodes table block */
	uint16_t ext4bgd_nbfree_hi;	/* high bits of number of free blocks */
	uint16_t ext4bgd_nifree_hi;	/* high bits of number of free inodes */
	uint16_t ext4bgd_ndirs_hi;	/* high bits of number of directories */
	uint16_t ext4bgd_i_unused_hi;	/* high bits of unused inode count */
	uint32_t ext4bgd_x_bitmap_hi;   /* high bits of snapshot exclusion */
	uint16_t ext4bgd_b_bmap_csum_hi;/* high bits of block bitmap checksum */
	uint16_t ext4bgd_i_bmap_csum_hi;/* high bits of inode bitmap checksum */
	uint32_t ext4bgd_reserved;
};

#define	E2FS_REV0_GD_SIZE (sizeof(struct ext2_gd) / 2)
#define	E2FS_64BIT_GD_SIZE (sizeof(struct ext2_gd))

/*
 * Macro-instructions used to manage several block sizes
 */
#define	EXT2_MIN_BLOCK_LOG_SIZE		  10
#define	EXT2_BLOCK_SIZE(s)		((s)->e2fs_bsize)
#define	EXT2_ADDR_PER_BLOCK(s)		(EXT2_BLOCK_SIZE(s) / sizeof(uint32_t))
#define	EXT2_INODE_SIZE(s)		(EXT2_SB(s)->e2fs_isize)

/*
 * Macro-instructions used to manage fragments
 */
#define	EXT2_MIN_FRAG_SIZE		1024
#define	EXT2_MIN_FRAG_LOG_SIZE		10
#define	EXT2_MAX_FRAG_LOG_SIZE		30
#define	EXT2_FRAG_SIZE(s)		(EXT2_SB(s)->e2fs_fsize)
#define	EXT2_FRAGS_PER_BLOCK(s)		(EXT2_SB(s)->e2fs_fpb)

/*
 * Macro-instructions used to manage group descriptors
 */
#define	EXT2_BLOCKS_PER_GROUP(s)	(EXT2_SB(s)->e2fs_bpg)
#define	EXT2_DESCS_PER_BLOCK(s)		(EXT2_HAS_INCOMPAT_FEATURE((s), \
	EXT2F_INCOMPAT_64BIT) ? ((s)->e2fs_bsize / sizeof(struct ext2_gd)) : \
	((s)->e2fs_bsize / E2FS_REV0_GD_SIZE))

/*
 * Macro-instructions used to manage inodes
 */
#define	EXT2_FIRST_INO(s)	((EXT2_SB(s)->e2fs->e2fs_rev == E2FS_REV0) ? \
				 EXT2_FIRSTINO : \
				 EXT2_SB(s)->e2fs->e2fs_first_ino)

#endif	/* !_FS_EXT2FS_EXT2FS_H_ */

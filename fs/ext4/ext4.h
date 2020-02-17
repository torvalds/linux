// SPDX-License-Identifier: GPL-2.0
/*
 *  ext4.h
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/include/linux/minix_fs.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef _EXT4_H
#define _EXT4_H

#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/magic.h>
#include <linux/jbd2.h>
#include <linux/quota.h>
#include <linux/rwsem.h>
#include <linux/rbtree.h>
#include <linux/seqlock.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/sched/signal.h>
#include <linux/blockgroup_lock.h>
#include <linux/percpu_counter.h>
#include <linux/ratelimit.h>
#include <crypto/hash.h>
#include <linux/falloc.h>
#include <linux/percpu-rwsem.h>
#ifdef __KERNEL__
#include <linux/compat.h>
#endif

#include <linux/fscrypt.h>
#include <linux/fsverity.h>

#include <linux/compiler.h>

/*
 * The fourth extended filesystem constants/structures
 */

/*
 * with AGGRESSIVE_CHECK allocator runs consistency checks over
 * structures. these checks slow things down a lot
 */
#define AGGRESSIVE_CHECK__

/*
 * with DOUBLE_CHECK defined mballoc creates persistent in-core
 * bitmaps, maintains and uses them to check for double allocations
 */
#define DOUBLE_CHECK__

/*
 * Define EXT4FS_DEBUG to produce debug messages
 */
#undef EXT4FS_DEBUG

/*
 * Debug code
 */
#ifdef EXT4FS_DEBUG
#define ext4_debug(f, a...)						\
	do {								\
		printk(KERN_DEBUG "EXT4-fs DEBUG (%s, %d): %s:",	\
			__FILE__, __LINE__, __func__);			\
		printk(KERN_DEBUG f, ## a);				\
	} while (0)
#else
#define ext4_debug(fmt, ...)	no_printk(fmt, ##__VA_ARGS__)
#endif

/*
 * Turn on EXT_DEBUG to get lots of info about extents operations.
 */
#define EXT_DEBUG__
#ifdef EXT_DEBUG
#define ext_debug(fmt, ...)	printk(fmt, ##__VA_ARGS__)
#else
#define ext_debug(fmt, ...)	no_printk(fmt, ##__VA_ARGS__)
#endif

/* data type for block offset of block group */
typedef int ext4_grpblk_t;

/* data type for filesystem-wide blocks number */
typedef unsigned long long ext4_fsblk_t;

/* data type for file logical block number */
typedef __u32 ext4_lblk_t;

/* data type for block group number */
typedef unsigned int ext4_group_t;

enum SHIFT_DIRECTION {
	SHIFT_LEFT = 0,
	SHIFT_RIGHT,
};

/*
 * Flags used in mballoc's allocation_context flags field.
 *
 * Also used to show what's going on for debugging purposes when the
 * flag field is exported via the traceport interface
 */

/* prefer goal again. length */
#define EXT4_MB_HINT_MERGE		0x0001
/* blocks already reserved */
#define EXT4_MB_HINT_RESERVED		0x0002
/* metadata is being allocated */
#define EXT4_MB_HINT_METADATA		0x0004
/* first blocks in the file */
#define EXT4_MB_HINT_FIRST		0x0008
/* search for the best chunk */
#define EXT4_MB_HINT_BEST		0x0010
/* data is being allocated */
#define EXT4_MB_HINT_DATA		0x0020
/* don't preallocate (for tails) */
#define EXT4_MB_HINT_NOPREALLOC		0x0040
/* allocate for locality group */
#define EXT4_MB_HINT_GROUP_ALLOC	0x0080
/* allocate goal blocks or none */
#define EXT4_MB_HINT_GOAL_ONLY		0x0100
/* goal is meaningful */
#define EXT4_MB_HINT_TRY_GOAL		0x0200
/* blocks already pre-reserved by delayed allocation */
#define EXT4_MB_DELALLOC_RESERVED	0x0400
/* We are doing stream allocation */
#define EXT4_MB_STREAM_ALLOC		0x0800
/* Use reserved root blocks if needed */
#define EXT4_MB_USE_ROOT_BLOCKS		0x1000
/* Use blocks from reserved pool */
#define EXT4_MB_USE_RESERVED		0x2000

struct ext4_allocation_request {
	/* target inode for block we're allocating */
	struct inode *inode;
	/* how many blocks we want to allocate */
	unsigned int len;
	/* logical block in target inode */
	ext4_lblk_t logical;
	/* the closest logical allocated block to the left */
	ext4_lblk_t lleft;
	/* the closest logical allocated block to the right */
	ext4_lblk_t lright;
	/* phys. target (a hint) */
	ext4_fsblk_t goal;
	/* phys. block for the closest logical allocated block to the left */
	ext4_fsblk_t pleft;
	/* phys. block for the closest logical allocated block to the right */
	ext4_fsblk_t pright;
	/* flags. see above EXT4_MB_HINT_* */
	unsigned int flags;
};

/*
 * Logical to physical block mapping, used by ext4_map_blocks()
 *
 * This structure is used to pass requests into ext4_map_blocks() as
 * well as to store the information returned by ext4_map_blocks().  It
 * takes less room on the stack than a struct buffer_head.
 */
#define EXT4_MAP_NEW		(1 << BH_New)
#define EXT4_MAP_MAPPED		(1 << BH_Mapped)
#define EXT4_MAP_UNWRITTEN	(1 << BH_Unwritten)
#define EXT4_MAP_BOUNDARY	(1 << BH_Boundary)
#define EXT4_MAP_FLAGS		(EXT4_MAP_NEW | EXT4_MAP_MAPPED |\
				 EXT4_MAP_UNWRITTEN | EXT4_MAP_BOUNDARY)

struct ext4_map_blocks {
	ext4_fsblk_t m_pblk;
	ext4_lblk_t m_lblk;
	unsigned int m_len;
	unsigned int m_flags;
};

/*
 * Block validity checking, system zone rbtree.
 */
struct ext4_system_blocks {
	struct rb_root root;
	struct rcu_head rcu;
};

/*
 * Flags for ext4_io_end->flags
 */
#define	EXT4_IO_END_UNWRITTEN	0x0001

struct ext4_io_end_vec {
	struct list_head list;		/* list of io_end_vec */
	loff_t offset;			/* offset in the file */
	ssize_t size;			/* size of the extent */
};

/*
 * For converting unwritten extents on a work queue. 'handle' is used for
 * buffered writeback.
 */
typedef struct ext4_io_end {
	struct list_head	list;		/* per-file finished IO list */
	handle_t		*handle;	/* handle reserved for extent
						 * conversion */
	struct inode		*inode;		/* file being written to */
	struct bio		*bio;		/* Linked list of completed
						 * bios covering the extent */
	unsigned int		flag;		/* unwritten or not */
	atomic_t		count;		/* reference counter */
	struct list_head	list_vec;	/* list of ext4_io_end_vec */
} ext4_io_end_t;

struct ext4_io_submit {
	struct writeback_control *io_wbc;
	struct bio		*io_bio;
	ext4_io_end_t		*io_end;
	sector_t		io_next_block;
};

/*
 * Special inodes numbers
 */
#define	EXT4_BAD_INO		 1	/* Bad blocks inode */
#define EXT4_ROOT_INO		 2	/* Root inode */
#define EXT4_USR_QUOTA_INO	 3	/* User quota inode */
#define EXT4_GRP_QUOTA_INO	 4	/* Group quota inode */
#define EXT4_BOOT_LOADER_INO	 5	/* Boot loader inode */
#define EXT4_UNDEL_DIR_INO	 6	/* Undelete directory inode */
#define EXT4_RESIZE_INO		 7	/* Reserved group descriptors inode */
#define EXT4_JOURNAL_INO	 8	/* Journal inode */

/* First non-reserved inode for old ext4 filesystems */
#define EXT4_GOOD_OLD_FIRST_INO	11

/*
 * Maximal count of links to a file
 */
#define EXT4_LINK_MAX		65000

/*
 * Macro-instructions used to manage several block sizes
 */
#define EXT4_MIN_BLOCK_SIZE		1024
#define	EXT4_MAX_BLOCK_SIZE		65536
#define EXT4_MIN_BLOCK_LOG_SIZE		10
#define EXT4_MAX_BLOCK_LOG_SIZE		16
#define EXT4_MAX_CLUSTER_LOG_SIZE	30
#ifdef __KERNEL__
# define EXT4_BLOCK_SIZE(s)		((s)->s_blocksize)
#else
# define EXT4_BLOCK_SIZE(s)		(EXT4_MIN_BLOCK_SIZE << (s)->s_log_block_size)
#endif
#define	EXT4_ADDR_PER_BLOCK(s)		(EXT4_BLOCK_SIZE(s) / sizeof(__u32))
#define EXT4_CLUSTER_SIZE(s)		(EXT4_BLOCK_SIZE(s) << \
					 EXT4_SB(s)->s_cluster_bits)
#ifdef __KERNEL__
# define EXT4_BLOCK_SIZE_BITS(s)	((s)->s_blocksize_bits)
# define EXT4_CLUSTER_BITS(s)		(EXT4_SB(s)->s_cluster_bits)
#else
# define EXT4_BLOCK_SIZE_BITS(s)	((s)->s_log_block_size + 10)
#endif
#ifdef __KERNEL__
#define	EXT4_ADDR_PER_BLOCK_BITS(s)	(EXT4_SB(s)->s_addr_per_block_bits)
#define EXT4_INODE_SIZE(s)		(EXT4_SB(s)->s_inode_size)
#define EXT4_FIRST_INO(s)		(EXT4_SB(s)->s_first_ino)
#else
#define EXT4_INODE_SIZE(s)	(((s)->s_rev_level == EXT4_GOOD_OLD_REV) ? \
				 EXT4_GOOD_OLD_INODE_SIZE : \
				 (s)->s_inode_size)
#define EXT4_FIRST_INO(s)	(((s)->s_rev_level == EXT4_GOOD_OLD_REV) ? \
				 EXT4_GOOD_OLD_FIRST_INO : \
				 (s)->s_first_ino)
#endif
#define EXT4_BLOCK_ALIGN(size, blkbits)		ALIGN((size), (1 << (blkbits)))
#define EXT4_MAX_BLOCKS(size, offset, blkbits) \
	((EXT4_BLOCK_ALIGN(size + offset, blkbits) >> blkbits) - (offset >> \
								  blkbits))

/* Translate a block number to a cluster number */
#define EXT4_B2C(sbi, blk)	((blk) >> (sbi)->s_cluster_bits)
/* Translate a cluster number to a block number */
#define EXT4_C2B(sbi, cluster)	((cluster) << (sbi)->s_cluster_bits)
/* Translate # of blks to # of clusters */
#define EXT4_NUM_B2C(sbi, blks)	(((blks) + (sbi)->s_cluster_ratio - 1) >> \
				 (sbi)->s_cluster_bits)
/* Mask out the low bits to get the starting block of the cluster */
#define EXT4_PBLK_CMASK(s, pblk) ((pblk) &				\
				  ~((ext4_fsblk_t) (s)->s_cluster_ratio - 1))
#define EXT4_LBLK_CMASK(s, lblk) ((lblk) &				\
				  ~((ext4_lblk_t) (s)->s_cluster_ratio - 1))
/* Fill in the low bits to get the last block of the cluster */
#define EXT4_LBLK_CFILL(sbi, lblk) ((lblk) |				\
				    ((ext4_lblk_t) (sbi)->s_cluster_ratio - 1))
/* Get the cluster offset */
#define EXT4_PBLK_COFF(s, pblk) ((pblk) &				\
				 ((ext4_fsblk_t) (s)->s_cluster_ratio - 1))
#define EXT4_LBLK_COFF(s, lblk) ((lblk) &				\
				 ((ext4_lblk_t) (s)->s_cluster_ratio - 1))

/*
 * Structure of a blocks group descriptor
 */
struct ext4_group_desc
{
	__le32	bg_block_bitmap_lo;	/* Blocks bitmap block */
	__le32	bg_inode_bitmap_lo;	/* Inodes bitmap block */
	__le32	bg_inode_table_lo;	/* Inodes table block */
	__le16	bg_free_blocks_count_lo;/* Free blocks count */
	__le16	bg_free_inodes_count_lo;/* Free inodes count */
	__le16	bg_used_dirs_count_lo;	/* Directories count */
	__le16	bg_flags;		/* EXT4_BG_flags (INODE_UNINIT, etc) */
	__le32  bg_exclude_bitmap_lo;   /* Exclude bitmap for snapshots */
	__le16  bg_block_bitmap_csum_lo;/* crc32c(s_uuid+grp_num+bbitmap) LE */
	__le16  bg_inode_bitmap_csum_lo;/* crc32c(s_uuid+grp_num+ibitmap) LE */
	__le16  bg_itable_unused_lo;	/* Unused inodes count */
	__le16  bg_checksum;		/* crc16(sb_uuid+group+desc) */
	__le32	bg_block_bitmap_hi;	/* Blocks bitmap block MSB */
	__le32	bg_inode_bitmap_hi;	/* Inodes bitmap block MSB */
	__le32	bg_inode_table_hi;	/* Inodes table block MSB */
	__le16	bg_free_blocks_count_hi;/* Free blocks count MSB */
	__le16	bg_free_inodes_count_hi;/* Free inodes count MSB */
	__le16	bg_used_dirs_count_hi;	/* Directories count MSB */
	__le16  bg_itable_unused_hi;    /* Unused inodes count MSB */
	__le32  bg_exclude_bitmap_hi;   /* Exclude bitmap block MSB */
	__le16  bg_block_bitmap_csum_hi;/* crc32c(s_uuid+grp_num+bbitmap) BE */
	__le16  bg_inode_bitmap_csum_hi;/* crc32c(s_uuid+grp_num+ibitmap) BE */
	__u32   bg_reserved;
};

#define EXT4_BG_INODE_BITMAP_CSUM_HI_END	\
	(offsetof(struct ext4_group_desc, bg_inode_bitmap_csum_hi) + \
	 sizeof(__le16))
#define EXT4_BG_BLOCK_BITMAP_CSUM_HI_END	\
	(offsetof(struct ext4_group_desc, bg_block_bitmap_csum_hi) + \
	 sizeof(__le16))

/*
 * Structure of a flex block group info
 */

struct flex_groups {
	atomic64_t	free_clusters;
	atomic_t	free_inodes;
	atomic_t	used_dirs;
};

#define EXT4_BG_INODE_UNINIT	0x0001 /* Inode table/bitmap not in use */
#define EXT4_BG_BLOCK_UNINIT	0x0002 /* Block bitmap not in use */
#define EXT4_BG_INODE_ZEROED	0x0004 /* On-disk itable initialized to zero */

/*
 * Macro-instructions used to manage group descriptors
 */
#define EXT4_MIN_DESC_SIZE		32
#define EXT4_MIN_DESC_SIZE_64BIT	64
#define	EXT4_MAX_DESC_SIZE		EXT4_MIN_BLOCK_SIZE
#define EXT4_DESC_SIZE(s)		(EXT4_SB(s)->s_desc_size)
#ifdef __KERNEL__
# define EXT4_BLOCKS_PER_GROUP(s)	(EXT4_SB(s)->s_blocks_per_group)
# define EXT4_CLUSTERS_PER_GROUP(s)	(EXT4_SB(s)->s_clusters_per_group)
# define EXT4_DESC_PER_BLOCK(s)		(EXT4_SB(s)->s_desc_per_block)
# define EXT4_INODES_PER_GROUP(s)	(EXT4_SB(s)->s_inodes_per_group)
# define EXT4_DESC_PER_BLOCK_BITS(s)	(EXT4_SB(s)->s_desc_per_block_bits)
#else
# define EXT4_BLOCKS_PER_GROUP(s)	((s)->s_blocks_per_group)
# define EXT4_DESC_PER_BLOCK(s)		(EXT4_BLOCK_SIZE(s) / EXT4_DESC_SIZE(s))
# define EXT4_INODES_PER_GROUP(s)	((s)->s_inodes_per_group)
#endif

/*
 * Constants relative to the data blocks
 */
#define	EXT4_NDIR_BLOCKS		12
#define	EXT4_IND_BLOCK			EXT4_NDIR_BLOCKS
#define	EXT4_DIND_BLOCK			(EXT4_IND_BLOCK + 1)
#define	EXT4_TIND_BLOCK			(EXT4_DIND_BLOCK + 1)
#define	EXT4_N_BLOCKS			(EXT4_TIND_BLOCK + 1)

/*
 * Inode flags
 */
#define	EXT4_SECRM_FL			0x00000001 /* Secure deletion */
#define	EXT4_UNRM_FL			0x00000002 /* Undelete */
#define	EXT4_COMPR_FL			0x00000004 /* Compress file */
#define EXT4_SYNC_FL			0x00000008 /* Synchronous updates */
#define EXT4_IMMUTABLE_FL		0x00000010 /* Immutable file */
#define EXT4_APPEND_FL			0x00000020 /* writes to file may only append */
#define EXT4_NODUMP_FL			0x00000040 /* do not dump file */
#define EXT4_NOATIME_FL			0x00000080 /* do not update atime */
/* Reserved for compression usage... */
#define EXT4_DIRTY_FL			0x00000100
#define EXT4_COMPRBLK_FL		0x00000200 /* One or more compressed clusters */
#define EXT4_NOCOMPR_FL			0x00000400 /* Don't compress */
	/* nb: was previously EXT2_ECOMPR_FL */
#define EXT4_ENCRYPT_FL			0x00000800 /* encrypted file */
/* End compression flags --- maybe not all used */
#define EXT4_INDEX_FL			0x00001000 /* hash-indexed directory */
#define EXT4_IMAGIC_FL			0x00002000 /* AFS directory */
#define EXT4_JOURNAL_DATA_FL		0x00004000 /* file data should be journaled */
#define EXT4_NOTAIL_FL			0x00008000 /* file tail should not be merged */
#define EXT4_DIRSYNC_FL			0x00010000 /* dirsync behaviour (directories only) */
#define EXT4_TOPDIR_FL			0x00020000 /* Top of directory hierarchies*/
#define EXT4_HUGE_FILE_FL               0x00040000 /* Set to each huge file */
#define EXT4_EXTENTS_FL			0x00080000 /* Inode uses extents */
#define EXT4_VERITY_FL			0x00100000 /* Verity protected inode */
#define EXT4_EA_INODE_FL	        0x00200000 /* Inode used for large EA */
#define EXT4_EOFBLOCKS_FL		0x00400000 /* Blocks allocated beyond EOF */
#define EXT4_INLINE_DATA_FL		0x10000000 /* Inode has inline data. */
#define EXT4_PROJINHERIT_FL		0x20000000 /* Create with parents projid */
#define EXT4_CASEFOLD_FL		0x40000000 /* Casefolded file */
#define EXT4_RESERVED_FL		0x80000000 /* reserved for ext4 lib */

#define EXT4_FL_USER_VISIBLE		0x705BDFFF /* User visible flags */
#define EXT4_FL_USER_MODIFIABLE		0x604BC0FF /* User modifiable flags */

/* Flags we can manipulate with through EXT4_IOC_FSSETXATTR */
#define EXT4_FL_XFLAG_VISIBLE		(EXT4_SYNC_FL | \
					 EXT4_IMMUTABLE_FL | \
					 EXT4_APPEND_FL | \
					 EXT4_NODUMP_FL | \
					 EXT4_NOATIME_FL | \
					 EXT4_PROJINHERIT_FL)

/* Flags that should be inherited by new inodes from their parent. */
#define EXT4_FL_INHERITED (EXT4_SECRM_FL | EXT4_UNRM_FL | EXT4_COMPR_FL |\
			   EXT4_SYNC_FL | EXT4_NODUMP_FL | EXT4_NOATIME_FL |\
			   EXT4_NOCOMPR_FL | EXT4_JOURNAL_DATA_FL |\
			   EXT4_NOTAIL_FL | EXT4_DIRSYNC_FL |\
			   EXT4_PROJINHERIT_FL | EXT4_CASEFOLD_FL)

/* Flags that are appropriate for regular files (all but dir-specific ones). */
#define EXT4_REG_FLMASK (~(EXT4_DIRSYNC_FL | EXT4_TOPDIR_FL | EXT4_CASEFOLD_FL |\
			   EXT4_PROJINHERIT_FL))

/* Flags that are appropriate for non-directories/regular files. */
#define EXT4_OTHER_FLMASK (EXT4_NODUMP_FL | EXT4_NOATIME_FL)

/* The only flags that should be swapped */
#define EXT4_FL_SHOULD_SWAP (EXT4_HUGE_FILE_FL | EXT4_EXTENTS_FL)

/* Mask out flags that are inappropriate for the given type of inode. */
static inline __u32 ext4_mask_flags(umode_t mode, __u32 flags)
{
	if (S_ISDIR(mode))
		return flags;
	else if (S_ISREG(mode))
		return flags & EXT4_REG_FLMASK;
	else
		return flags & EXT4_OTHER_FLMASK;
}

/*
 * Inode flags used for atomic set/get
 */
enum {
	EXT4_INODE_SECRM	= 0,	/* Secure deletion */
	EXT4_INODE_UNRM		= 1,	/* Undelete */
	EXT4_INODE_COMPR	= 2,	/* Compress file */
	EXT4_INODE_SYNC		= 3,	/* Synchronous updates */
	EXT4_INODE_IMMUTABLE	= 4,	/* Immutable file */
	EXT4_INODE_APPEND	= 5,	/* writes to file may only append */
	EXT4_INODE_NODUMP	= 6,	/* do not dump file */
	EXT4_INODE_NOATIME	= 7,	/* do not update atime */
/* Reserved for compression usage... */
	EXT4_INODE_DIRTY	= 8,
	EXT4_INODE_COMPRBLK	= 9,	/* One or more compressed clusters */
	EXT4_INODE_NOCOMPR	= 10,	/* Don't compress */
	EXT4_INODE_ENCRYPT	= 11,	/* Encrypted file */
/* End compression flags --- maybe not all used */
	EXT4_INODE_INDEX	= 12,	/* hash-indexed directory */
	EXT4_INODE_IMAGIC	= 13,	/* AFS directory */
	EXT4_INODE_JOURNAL_DATA	= 14,	/* file data should be journaled */
	EXT4_INODE_NOTAIL	= 15,	/* file tail should not be merged */
	EXT4_INODE_DIRSYNC	= 16,	/* dirsync behaviour (directories only) */
	EXT4_INODE_TOPDIR	= 17,	/* Top of directory hierarchies*/
	EXT4_INODE_HUGE_FILE	= 18,	/* Set to each huge file */
	EXT4_INODE_EXTENTS	= 19,	/* Inode uses extents */
	EXT4_INODE_VERITY	= 20,	/* Verity protected inode */
	EXT4_INODE_EA_INODE	= 21,	/* Inode used for large EA */
	EXT4_INODE_EOFBLOCKS	= 22,	/* Blocks allocated beyond EOF */
	EXT4_INODE_INLINE_DATA	= 28,	/* Data in inode. */
	EXT4_INODE_PROJINHERIT	= 29,	/* Create with parents projid */
	EXT4_INODE_RESERVED	= 31,	/* reserved for ext4 lib */
};

/*
 * Since it's pretty easy to mix up bit numbers and hex values, we use a
 * build-time check to make sure that EXT4_XXX_FL is consistent with respect to
 * EXT4_INODE_XXX. If all is well, the macros will be dropped, so, it won't cost
 * any extra space in the compiled kernel image, otherwise, the build will fail.
 * It's important that these values are the same, since we are using
 * EXT4_INODE_XXX to test for flag values, but EXT4_XXX_FL must be consistent
 * with the values of FS_XXX_FL defined in include/linux/fs.h and the on-disk
 * values found in ext2, ext3 and ext4 filesystems, and of course the values
 * defined in e2fsprogs.
 *
 * It's not paranoia if the Murphy's Law really *is* out to get you.  :-)
 */
#define TEST_FLAG_VALUE(FLAG) (EXT4_##FLAG##_FL == (1 << EXT4_INODE_##FLAG))
#define CHECK_FLAG_VALUE(FLAG) BUILD_BUG_ON(!TEST_FLAG_VALUE(FLAG))

static inline void ext4_check_flag_values(void)
{
	CHECK_FLAG_VALUE(SECRM);
	CHECK_FLAG_VALUE(UNRM);
	CHECK_FLAG_VALUE(COMPR);
	CHECK_FLAG_VALUE(SYNC);
	CHECK_FLAG_VALUE(IMMUTABLE);
	CHECK_FLAG_VALUE(APPEND);
	CHECK_FLAG_VALUE(NODUMP);
	CHECK_FLAG_VALUE(NOATIME);
	CHECK_FLAG_VALUE(DIRTY);
	CHECK_FLAG_VALUE(COMPRBLK);
	CHECK_FLAG_VALUE(NOCOMPR);
	CHECK_FLAG_VALUE(ENCRYPT);
	CHECK_FLAG_VALUE(INDEX);
	CHECK_FLAG_VALUE(IMAGIC);
	CHECK_FLAG_VALUE(JOURNAL_DATA);
	CHECK_FLAG_VALUE(NOTAIL);
	CHECK_FLAG_VALUE(DIRSYNC);
	CHECK_FLAG_VALUE(TOPDIR);
	CHECK_FLAG_VALUE(HUGE_FILE);
	CHECK_FLAG_VALUE(EXTENTS);
	CHECK_FLAG_VALUE(VERITY);
	CHECK_FLAG_VALUE(EA_INODE);
	CHECK_FLAG_VALUE(EOFBLOCKS);
	CHECK_FLAG_VALUE(INLINE_DATA);
	CHECK_FLAG_VALUE(PROJINHERIT);
	CHECK_FLAG_VALUE(RESERVED);
}

/* Used to pass group descriptor data when online resize is done */
struct ext4_new_group_input {
	__u32 group;		/* Group number for this data */
	__u64 block_bitmap;	/* Absolute block number of block bitmap */
	__u64 inode_bitmap;	/* Absolute block number of inode bitmap */
	__u64 inode_table;	/* Absolute block number of inode table start */
	__u32 blocks_count;	/* Total number of blocks in this group */
	__u16 reserved_blocks;	/* Number of reserved blocks in this group */
	__u16 unused;
};

#if defined(__KERNEL__) && defined(CONFIG_COMPAT)
struct compat_ext4_new_group_input {
	u32 group;
	compat_u64 block_bitmap;
	compat_u64 inode_bitmap;
	compat_u64 inode_table;
	u32 blocks_count;
	u16 reserved_blocks;
	u16 unused;
};
#endif

/* The struct ext4_new_group_input in kernel space, with free_blocks_count */
struct ext4_new_group_data {
	__u32 group;
	__u64 block_bitmap;
	__u64 inode_bitmap;
	__u64 inode_table;
	__u32 blocks_count;
	__u16 reserved_blocks;
	__u16 mdata_blocks;
	__u32 free_clusters_count;
};

/* Indexes used to index group tables in ext4_new_group_data */
enum {
	BLOCK_BITMAP = 0,	/* block bitmap */
	INODE_BITMAP,		/* inode bitmap */
	INODE_TABLE,		/* inode tables */
	GROUP_TABLE_COUNT,
};

/*
 * Flags used by ext4_map_blocks()
 */
	/* Allocate any needed blocks and/or convert an unwritten
	   extent to be an initialized ext4 */
#define EXT4_GET_BLOCKS_CREATE			0x0001
	/* Request the creation of an unwritten extent */
#define EXT4_GET_BLOCKS_UNWRIT_EXT		0x0002
#define EXT4_GET_BLOCKS_CREATE_UNWRIT_EXT	(EXT4_GET_BLOCKS_UNWRIT_EXT|\
						 EXT4_GET_BLOCKS_CREATE)
	/* Caller is from the delayed allocation writeout path
	 * finally doing the actual allocation of delayed blocks */
#define EXT4_GET_BLOCKS_DELALLOC_RESERVE	0x0004
	/* caller is from the direct IO path, request to creation of an
	unwritten extents if not allocated, split the unwritten
	extent if blocks has been preallocated already*/
#define EXT4_GET_BLOCKS_PRE_IO			0x0008
#define EXT4_GET_BLOCKS_CONVERT			0x0010
#define EXT4_GET_BLOCKS_IO_CREATE_EXT		(EXT4_GET_BLOCKS_PRE_IO|\
					 EXT4_GET_BLOCKS_CREATE_UNWRIT_EXT)
	/* Convert extent to initialized after IO complete */
#define EXT4_GET_BLOCKS_IO_CONVERT_EXT		(EXT4_GET_BLOCKS_CONVERT|\
					 EXT4_GET_BLOCKS_CREATE_UNWRIT_EXT)
	/* Eventual metadata allocation (due to growing extent tree)
	 * should not fail, so try to use reserved blocks for that.*/
#define EXT4_GET_BLOCKS_METADATA_NOFAIL		0x0020
	/* Don't normalize allocation size (used for fallocate) */
#define EXT4_GET_BLOCKS_NO_NORMALIZE		0x0040
	/* Request will not result in inode size update (user for fallocate) */
#define EXT4_GET_BLOCKS_KEEP_SIZE		0x0080
	/* Convert written extents to unwritten */
#define EXT4_GET_BLOCKS_CONVERT_UNWRITTEN	0x0100
	/* Write zeros to newly created written extents */
#define EXT4_GET_BLOCKS_ZERO			0x0200
#define EXT4_GET_BLOCKS_CREATE_ZERO		(EXT4_GET_BLOCKS_CREATE |\
					EXT4_GET_BLOCKS_ZERO)
	/* Caller will submit data before dropping transaction handle. This
	 * allows jbd2 to avoid submitting data before commit. */
#define EXT4_GET_BLOCKS_IO_SUBMIT		0x0400

/*
 * The bit position of these flags must not overlap with any of the
 * EXT4_GET_BLOCKS_*.  They are used by ext4_find_extent(),
 * read_extent_tree_block(), ext4_split_extent_at(),
 * ext4_ext_insert_extent(), and ext4_ext_create_new_leaf().
 * EXT4_EX_NOCACHE is used to indicate that the we shouldn't be
 * caching the extents when reading from the extent tree while a
 * truncate or punch hole operation is in progress.
 */
#define EXT4_EX_NOCACHE				0x40000000
#define EXT4_EX_FORCE_CACHE			0x20000000

/*
 * Flags used by ext4_free_blocks
 */
#define EXT4_FREE_BLOCKS_METADATA		0x0001
#define EXT4_FREE_BLOCKS_FORGET			0x0002
#define EXT4_FREE_BLOCKS_VALIDATED		0x0004
#define EXT4_FREE_BLOCKS_NO_QUOT_UPDATE		0x0008
#define EXT4_FREE_BLOCKS_NOFREE_FIRST_CLUSTER	0x0010
#define EXT4_FREE_BLOCKS_NOFREE_LAST_CLUSTER	0x0020
#define EXT4_FREE_BLOCKS_RERESERVE_CLUSTER      0x0040

/*
 * ioctl commands
 */
#define	EXT4_IOC_GETFLAGS		FS_IOC_GETFLAGS
#define	EXT4_IOC_SETFLAGS		FS_IOC_SETFLAGS
#define	EXT4_IOC_GETVERSION		_IOR('f', 3, long)
#define	EXT4_IOC_SETVERSION		_IOW('f', 4, long)
#define	EXT4_IOC_GETVERSION_OLD		FS_IOC_GETVERSION
#define	EXT4_IOC_SETVERSION_OLD		FS_IOC_SETVERSION
#define EXT4_IOC_GETRSVSZ		_IOR('f', 5, long)
#define EXT4_IOC_SETRSVSZ		_IOW('f', 6, long)
#define EXT4_IOC_GROUP_EXTEND		_IOW('f', 7, unsigned long)
#define EXT4_IOC_GROUP_ADD		_IOW('f', 8, struct ext4_new_group_input)
#define EXT4_IOC_MIGRATE		_IO('f', 9)
 /* note ioctl 10 reserved for an early version of the FIEMAP ioctl */
 /* note ioctl 11 reserved for filesystem-independent FIEMAP ioctl */
#define EXT4_IOC_ALLOC_DA_BLKS		_IO('f', 12)
#define EXT4_IOC_MOVE_EXT		_IOWR('f', 15, struct move_extent)
#define EXT4_IOC_RESIZE_FS		_IOW('f', 16, __u64)
#define EXT4_IOC_SWAP_BOOT		_IO('f', 17)
#define EXT4_IOC_PRECACHE_EXTENTS	_IO('f', 18)
#define EXT4_IOC_SET_ENCRYPTION_POLICY	FS_IOC_SET_ENCRYPTION_POLICY
#define EXT4_IOC_GET_ENCRYPTION_PWSALT	FS_IOC_GET_ENCRYPTION_PWSALT
#define EXT4_IOC_GET_ENCRYPTION_POLICY	FS_IOC_GET_ENCRYPTION_POLICY
/* ioctl codes 19--39 are reserved for fscrypt */
#define EXT4_IOC_CLEAR_ES_CACHE		_IO('f', 40)
#define EXT4_IOC_GETSTATE		_IOW('f', 41, __u32)
#define EXT4_IOC_GET_ES_CACHE		_IOWR('f', 42, struct fiemap)

#define EXT4_IOC_FSGETXATTR		FS_IOC_FSGETXATTR
#define EXT4_IOC_FSSETXATTR		FS_IOC_FSSETXATTR

#define EXT4_IOC_SHUTDOWN _IOR ('X', 125, __u32)

/*
 * Flags for going down operation
 */
#define EXT4_GOING_FLAGS_DEFAULT		0x0	/* going down */
#define EXT4_GOING_FLAGS_LOGFLUSH		0x1	/* flush log but not data */
#define EXT4_GOING_FLAGS_NOLOGFLUSH		0x2	/* don't flush log nor data */

/*
 * Flags returned by EXT4_IOC_GETSTATE
 *
 * We only expose to userspace a subset of the state flags in
 * i_state_flags
 */
#define EXT4_STATE_FLAG_EXT_PRECACHED	0x00000001
#define EXT4_STATE_FLAG_NEW		0x00000002
#define EXT4_STATE_FLAG_NEWENTRY	0x00000004
#define EXT4_STATE_FLAG_DA_ALLOC_CLOSE	0x00000008

#if defined(__KERNEL__) && defined(CONFIG_COMPAT)
/*
 * ioctl commands in 32 bit emulation
 */
#define EXT4_IOC32_GETFLAGS		FS_IOC32_GETFLAGS
#define EXT4_IOC32_SETFLAGS		FS_IOC32_SETFLAGS
#define EXT4_IOC32_GETVERSION		_IOR('f', 3, int)
#define EXT4_IOC32_SETVERSION		_IOW('f', 4, int)
#define EXT4_IOC32_GETRSVSZ		_IOR('f', 5, int)
#define EXT4_IOC32_SETRSVSZ		_IOW('f', 6, int)
#define EXT4_IOC32_GROUP_EXTEND		_IOW('f', 7, unsigned int)
#define EXT4_IOC32_GROUP_ADD		_IOW('f', 8, struct compat_ext4_new_group_input)
#define EXT4_IOC32_GETVERSION_OLD	FS_IOC32_GETVERSION
#define EXT4_IOC32_SETVERSION_OLD	FS_IOC32_SETVERSION
#endif

/*
 * Returned by EXT4_IOC_GET_ES_CACHE as an additional possible flag.
 * It indicates that the entry in extent status cache is for a hole.
 */
#define EXT4_FIEMAP_EXTENT_HOLE		0x08000000

/* Max physical block we can address w/o extents */
#define EXT4_MAX_BLOCK_FILE_PHYS	0xFFFFFFFF

/* Max logical block we can support */
#define EXT4_MAX_LOGICAL_BLOCK		0xFFFFFFFF

/*
 * Structure of an inode on the disk
 */
struct ext4_inode {
	__le16	i_mode;		/* File mode */
	__le16	i_uid;		/* Low 16 bits of Owner Uid */
	__le32	i_size_lo;	/* Size in bytes */
	__le32	i_atime;	/* Access time */
	__le32	i_ctime;	/* Inode Change time */
	__le32	i_mtime;	/* Modification time */
	__le32	i_dtime;	/* Deletion Time */
	__le16	i_gid;		/* Low 16 bits of Group Id */
	__le16	i_links_count;	/* Links count */
	__le32	i_blocks_lo;	/* Blocks count */
	__le32	i_flags;	/* File flags */
	union {
		struct {
			__le32  l_i_version;
		} linux1;
		struct {
			__u32  h_i_translator;
		} hurd1;
		struct {
			__u32  m_i_reserved1;
		} masix1;
	} osd1;				/* OS dependent 1 */
	__le32	i_block[EXT4_N_BLOCKS];/* Pointers to blocks */
	__le32	i_generation;	/* File version (for NFS) */
	__le32	i_file_acl_lo;	/* File ACL */
	__le32	i_size_high;
	__le32	i_obso_faddr;	/* Obsoleted fragment address */
	union {
		struct {
			__le16	l_i_blocks_high; /* were l_i_reserved1 */
			__le16	l_i_file_acl_high;
			__le16	l_i_uid_high;	/* these 2 fields */
			__le16	l_i_gid_high;	/* were reserved2[0] */
			__le16	l_i_checksum_lo;/* crc32c(uuid+inum+inode) LE */
			__le16	l_i_reserved;
		} linux2;
		struct {
			__le16	h_i_reserved1;	/* Obsoleted fragment number/size which are removed in ext4 */
			__u16	h_i_mode_high;
			__u16	h_i_uid_high;
			__u16	h_i_gid_high;
			__u32	h_i_author;
		} hurd2;
		struct {
			__le16	h_i_reserved1;	/* Obsoleted fragment number/size which are removed in ext4 */
			__le16	m_i_file_acl_high;
			__u32	m_i_reserved2[2];
		} masix2;
	} osd2;				/* OS dependent 2 */
	__le16	i_extra_isize;
	__le16	i_checksum_hi;	/* crc32c(uuid+inum+inode) BE */
	__le32  i_ctime_extra;  /* extra Change time      (nsec << 2 | epoch) */
	__le32  i_mtime_extra;  /* extra Modification time(nsec << 2 | epoch) */
	__le32  i_atime_extra;  /* extra Access time      (nsec << 2 | epoch) */
	__le32  i_crtime;       /* File Creation time */
	__le32  i_crtime_extra; /* extra FileCreationtime (nsec << 2 | epoch) */
	__le32  i_version_hi;	/* high 32 bits for 64-bit version */
	__le32	i_projid;	/* Project ID */
};

struct move_extent {
	__u32 reserved;		/* should be zero */
	__u32 donor_fd;		/* donor file descriptor */
	__u64 orig_start;	/* logical start offset in block for orig */
	__u64 donor_start;	/* logical start offset in block for donor */
	__u64 len;		/* block length to be moved */
	__u64 moved_len;	/* moved block length */
};

#define EXT4_EPOCH_BITS 2
#define EXT4_EPOCH_MASK ((1 << EXT4_EPOCH_BITS) - 1)
#define EXT4_NSEC_MASK  (~0UL << EXT4_EPOCH_BITS)

/*
 * Extended fields will fit into an inode if the filesystem was formatted
 * with large inodes (-I 256 or larger) and there are not currently any EAs
 * consuming all of the available space. For new inodes we always reserve
 * enough space for the kernel's known extended fields, but for inodes
 * created with an old kernel this might not have been the case. None of
 * the extended inode fields is critical for correct filesystem operation.
 * This macro checks if a certain field fits in the inode. Note that
 * inode-size = GOOD_OLD_INODE_SIZE + i_extra_isize
 */
#define EXT4_FITS_IN_INODE(ext4_inode, einode, field)	\
	((offsetof(typeof(*ext4_inode), field) +	\
	  sizeof((ext4_inode)->field))			\
	<= (EXT4_GOOD_OLD_INODE_SIZE +			\
	    (einode)->i_extra_isize))			\

/*
 * We use an encoding that preserves the times for extra epoch "00":
 *
 * extra  msb of                         adjust for signed
 * epoch  32-bit                         32-bit tv_sec to
 * bits   time    decoded 64-bit tv_sec  64-bit tv_sec      valid time range
 * 0 0    1    -0x80000000..-0x00000001  0x000000000 1901-12-13..1969-12-31
 * 0 0    0    0x000000000..0x07fffffff  0x000000000 1970-01-01..2038-01-19
 * 0 1    1    0x080000000..0x0ffffffff  0x100000000 2038-01-19..2106-02-07
 * 0 1    0    0x100000000..0x17fffffff  0x100000000 2106-02-07..2174-02-25
 * 1 0    1    0x180000000..0x1ffffffff  0x200000000 2174-02-25..2242-03-16
 * 1 0    0    0x200000000..0x27fffffff  0x200000000 2242-03-16..2310-04-04
 * 1 1    1    0x280000000..0x2ffffffff  0x300000000 2310-04-04..2378-04-22
 * 1 1    0    0x300000000..0x37fffffff  0x300000000 2378-04-22..2446-05-10
 *
 * Note that previous versions of the kernel on 64-bit systems would
 * incorrectly use extra epoch bits 1,1 for dates between 1901 and
 * 1970.  e2fsck will correct this, assuming that it is run on the
 * affected filesystem before 2242.
 */

static inline __le32 ext4_encode_extra_time(struct timespec64 *time)
{
	u32 extra =((time->tv_sec - (s32)time->tv_sec) >> 32) & EXT4_EPOCH_MASK;
	return cpu_to_le32(extra | (time->tv_nsec << EXT4_EPOCH_BITS));
}

static inline void ext4_decode_extra_time(struct timespec64 *time,
					  __le32 extra)
{
	if (unlikely(extra & cpu_to_le32(EXT4_EPOCH_MASK)))
		time->tv_sec += (u64)(le32_to_cpu(extra) & EXT4_EPOCH_MASK) << 32;
	time->tv_nsec = (le32_to_cpu(extra) & EXT4_NSEC_MASK) >> EXT4_EPOCH_BITS;
}

#define EXT4_INODE_SET_XTIME(xtime, inode, raw_inode)				\
do {										\
	if (EXT4_FITS_IN_INODE(raw_inode, EXT4_I(inode), xtime ## _extra))     {\
		(raw_inode)->xtime = cpu_to_le32((inode)->xtime.tv_sec);	\
		(raw_inode)->xtime ## _extra =					\
				ext4_encode_extra_time(&(inode)->xtime);	\
		}								\
	else	\
		(raw_inode)->xtime = cpu_to_le32(clamp_t(int32_t, (inode)->xtime.tv_sec, S32_MIN, S32_MAX));	\
} while (0)

#define EXT4_EINODE_SET_XTIME(xtime, einode, raw_inode)			       \
do {									       \
	if (EXT4_FITS_IN_INODE(raw_inode, einode, xtime))		       \
		(raw_inode)->xtime = cpu_to_le32((einode)->xtime.tv_sec);      \
	if (EXT4_FITS_IN_INODE(raw_inode, einode, xtime ## _extra))	       \
		(raw_inode)->xtime ## _extra =				       \
				ext4_encode_extra_time(&(einode)->xtime);      \
} while (0)

#define EXT4_INODE_GET_XTIME(xtime, inode, raw_inode)				\
do {										\
	(inode)->xtime.tv_sec = (signed)le32_to_cpu((raw_inode)->xtime);	\
	if (EXT4_FITS_IN_INODE(raw_inode, EXT4_I(inode), xtime ## _extra)) {	\
		ext4_decode_extra_time(&(inode)->xtime,				\
				       raw_inode->xtime ## _extra);		\
		}								\
	else									\
		(inode)->xtime.tv_nsec = 0;					\
} while (0)


#define EXT4_EINODE_GET_XTIME(xtime, einode, raw_inode)			       \
do {									       \
	if (EXT4_FITS_IN_INODE(raw_inode, einode, xtime))		       \
		(einode)->xtime.tv_sec = 				       \
			(signed)le32_to_cpu((raw_inode)->xtime);	       \
	else								       \
		(einode)->xtime.tv_sec = 0;				       \
	if (EXT4_FITS_IN_INODE(raw_inode, einode, xtime ## _extra))	       \
		ext4_decode_extra_time(&(einode)->xtime,		       \
				       raw_inode->xtime ## _extra);	       \
	else								       \
		(einode)->xtime.tv_nsec = 0;				       \
} while (0)

#define i_disk_version osd1.linux1.l_i_version

#if defined(__KERNEL__) || defined(__linux__)
#define i_reserved1	osd1.linux1.l_i_reserved1
#define i_file_acl_high	osd2.linux2.l_i_file_acl_high
#define i_blocks_high	osd2.linux2.l_i_blocks_high
#define i_uid_low	i_uid
#define i_gid_low	i_gid
#define i_uid_high	osd2.linux2.l_i_uid_high
#define i_gid_high	osd2.linux2.l_i_gid_high
#define i_checksum_lo	osd2.linux2.l_i_checksum_lo

#elif defined(__GNU__)

#define i_translator	osd1.hurd1.h_i_translator
#define i_uid_high	osd2.hurd2.h_i_uid_high
#define i_gid_high	osd2.hurd2.h_i_gid_high
#define i_author	osd2.hurd2.h_i_author

#elif defined(__masix__)

#define i_reserved1	osd1.masix1.m_i_reserved1
#define i_file_acl_high	osd2.masix2.m_i_file_acl_high
#define i_reserved2	osd2.masix2.m_i_reserved2

#endif /* defined(__KERNEL__) || defined(__linux__) */

#include "extents_status.h"

/*
 * Lock subclasses for i_data_sem in the ext4_inode_info structure.
 *
 * These are needed to avoid lockdep false positives when we need to
 * allocate blocks to the quota inode during ext4_map_blocks(), while
 * holding i_data_sem for a normal (non-quota) inode.  Since we don't
 * do quota tracking for the quota inode, this avoids deadlock (as
 * well as infinite recursion, since it isn't turtles all the way
 * down...)
 *
 *  I_DATA_SEM_NORMAL - Used for most inodes
 *  I_DATA_SEM_OTHER  - Used by move_inode.c for the second normal inode
 *			  where the second inode has larger inode number
 *			  than the first
 *  I_DATA_SEM_QUOTA  - Used for quota inodes only
 */
enum {
	I_DATA_SEM_NORMAL = 0,
	I_DATA_SEM_OTHER,
	I_DATA_SEM_QUOTA,
};


/*
 * fourth extended file system inode data in memory
 */
struct ext4_inode_info {
	__le32	i_data[15];	/* unconverted */
	__u32	i_dtime;
	ext4_fsblk_t	i_file_acl;

	/*
	 * i_block_group is the number of the block group which contains
	 * this file's inode.  Constant across the lifetime of the inode,
	 * it is used for making block allocation decisions - we try to
	 * place a file's data blocks near its inode block, and new inodes
	 * near to their parent directory's inode.
	 */
	ext4_group_t	i_block_group;
	ext4_lblk_t	i_dir_start_lookup;
#if (BITS_PER_LONG < 64)
	unsigned long	i_state_flags;		/* Dynamic state flags */
#endif
	unsigned long	i_flags;

	/*
	 * Extended attributes can be read independently of the main file
	 * data. Taking i_mutex even when reading would cause contention
	 * between readers of EAs and writers of regular file data, so
	 * instead we synchronize on xattr_sem when reading or changing
	 * EAs.
	 */
	struct rw_semaphore xattr_sem;

	struct list_head i_orphan;	/* unlinked but open inodes */

	/*
	 * i_disksize keeps track of what the inode size is ON DISK, not
	 * in memory.  During truncate, i_size is set to the new size by
	 * the VFS prior to calling ext4_truncate(), but the filesystem won't
	 * set i_disksize to 0 until the truncate is actually under way.
	 *
	 * The intent is that i_disksize always represents the blocks which
	 * are used by this file.  This allows recovery to restart truncate
	 * on orphans if we crash during truncate.  We actually write i_disksize
	 * into the on-disk inode when writing inodes out, instead of i_size.
	 *
	 * The only time when i_disksize and i_size may be different is when
	 * a truncate is in progress.  The only things which change i_disksize
	 * are ext4_get_block (growth) and ext4_truncate (shrinkth).
	 */
	loff_t	i_disksize;

	/*
	 * i_data_sem is for serialising ext4_truncate() against
	 * ext4_getblock().  In the 2.4 ext2 design, great chunks of inode's
	 * data tree are chopped off during truncate. We can't do that in
	 * ext4 because whenever we perform intermediate commits during
	 * truncate, the inode and all the metadata blocks *must* be in a
	 * consistent state which allows truncation of the orphans to restart
	 * during recovery.  Hence we must fix the get_block-vs-truncate race
	 * by other means, so we have i_data_sem.
	 */
	struct rw_semaphore i_data_sem;
	/*
	 * i_mmap_sem is for serializing page faults with truncate / punch hole
	 * operations. We have to make sure that new page cannot be faulted in
	 * a section of the inode that is being punched. We cannot easily use
	 * i_data_sem for this since we need protection for the whole punch
	 * operation and i_data_sem ranks below transaction start so we have
	 * to occasionally drop it.
	 */
	struct rw_semaphore i_mmap_sem;
	struct inode vfs_inode;
	struct jbd2_inode *jinode;

	spinlock_t i_raw_lock;	/* protects updates to the raw inode */

	/*
	 * File creation time. Its function is same as that of
	 * struct timespec64 i_{a,c,m}time in the generic inode.
	 */
	struct timespec64 i_crtime;

	/* mballoc */
	struct list_head i_prealloc_list;
	spinlock_t i_prealloc_lock;

	/* extents status tree */
	struct ext4_es_tree i_es_tree;
	rwlock_t i_es_lock;
	struct list_head i_es_list;
	unsigned int i_es_all_nr;	/* protected by i_es_lock */
	unsigned int i_es_shk_nr;	/* protected by i_es_lock */
	ext4_lblk_t i_es_shrink_lblk;	/* Offset where we start searching for
					   extents to shrink. Protected by
					   i_es_lock  */

	/* ialloc */
	ext4_group_t	i_last_alloc_group;

	/* allocation reservation info for delalloc */
	/* In case of bigalloc, this refer to clusters rather than blocks */
	unsigned int i_reserved_data_blocks;

	/* pending cluster reservations for bigalloc file systems */
	struct ext4_pending_tree i_pending_tree;

	/* on-disk additional length */
	__u16 i_extra_isize;

	/* Indicate the inline data space. */
	u16 i_inline_off;
	u16 i_inline_size;

#ifdef CONFIG_QUOTA
	/* quota space reservation, managed internally by quota code */
	qsize_t i_reserved_quota;
#endif

	/* Lock protecting lists below */
	spinlock_t i_completed_io_lock;
	/*
	 * Completed IOs that need unwritten extents handling and have
	 * transaction reserved
	 */
	struct list_head i_rsv_conversion_list;
	struct work_struct i_rsv_conversion_work;
	atomic_t i_unwritten; /* Nr. of inflight conversions pending */

	spinlock_t i_block_reservation_lock;

	/*
	 * Transactions that contain inode's metadata needed to complete
	 * fsync and fdatasync, respectively.
	 */
	tid_t i_sync_tid;
	tid_t i_datasync_tid;

#ifdef CONFIG_QUOTA
	struct dquot *i_dquot[MAXQUOTAS];
#endif

	/* Precomputed uuid+inum+igen checksum for seeding inode checksums */
	__u32 i_csum_seed;

	kprojid_t i_projid;
};

/*
 * File system states
 */
#define	EXT4_VALID_FS			0x0001	/* Unmounted cleanly */
#define	EXT4_ERROR_FS			0x0002	/* Errors detected */
#define	EXT4_ORPHAN_FS			0x0004	/* Orphans being recovered */

/*
 * Misc. filesystem flags
 */
#define EXT2_FLAGS_SIGNED_HASH		0x0001  /* Signed dirhash in use */
#define EXT2_FLAGS_UNSIGNED_HASH	0x0002  /* Unsigned dirhash in use */
#define EXT2_FLAGS_TEST_FILESYS		0x0004	/* to test development code */

/*
 * Mount flags set via mount options or defaults
 */
#define EXT4_MOUNT_NO_MBCACHE		0x00001 /* Do not use mbcache */
#define EXT4_MOUNT_GRPID		0x00004	/* Create files with directory's group */
#define EXT4_MOUNT_DEBUG		0x00008	/* Some debugging messages */
#define EXT4_MOUNT_ERRORS_CONT		0x00010	/* Continue on errors */
#define EXT4_MOUNT_ERRORS_RO		0x00020	/* Remount fs ro on errors */
#define EXT4_MOUNT_ERRORS_PANIC		0x00040	/* Panic on errors */
#define EXT4_MOUNT_ERRORS_MASK		0x00070
#define EXT4_MOUNT_MINIX_DF		0x00080	/* Mimics the Minix statfs */
#define EXT4_MOUNT_NOLOAD		0x00100	/* Don't use existing journal*/
#ifdef CONFIG_FS_DAX
#define EXT4_MOUNT_DAX			0x00200	/* Direct Access */
#else
#define EXT4_MOUNT_DAX			0
#endif
#define EXT4_MOUNT_DATA_FLAGS		0x00C00	/* Mode for data writes: */
#define EXT4_MOUNT_JOURNAL_DATA		0x00400	/* Write data to journal */
#define EXT4_MOUNT_ORDERED_DATA		0x00800	/* Flush data before commit */
#define EXT4_MOUNT_WRITEBACK_DATA	0x00C00	/* No data ordering */
#define EXT4_MOUNT_UPDATE_JOURNAL	0x01000	/* Update the journal format */
#define EXT4_MOUNT_NO_UID32		0x02000  /* Disable 32-bit UIDs */
#define EXT4_MOUNT_XATTR_USER		0x04000	/* Extended user attributes */
#define EXT4_MOUNT_POSIX_ACL		0x08000	/* POSIX Access Control Lists */
#define EXT4_MOUNT_NO_AUTO_DA_ALLOC	0x10000	/* No auto delalloc mapping */
#define EXT4_MOUNT_BARRIER		0x20000 /* Use block barriers */
#define EXT4_MOUNT_QUOTA		0x40000 /* Some quota option set */
#define EXT4_MOUNT_USRQUOTA		0x80000 /* "old" user quota,
						 * enable enforcement for hidden
						 * quota files */
#define EXT4_MOUNT_GRPQUOTA		0x100000 /* "old" group quota, enable
						  * enforcement for hidden quota
						  * files */
#define EXT4_MOUNT_PRJQUOTA		0x200000 /* Enable project quota
						  * enforcement */
#define EXT4_MOUNT_DIOREAD_NOLOCK	0x400000 /* Enable support for dio read nolocking */
#define EXT4_MOUNT_JOURNAL_CHECKSUM	0x800000 /* Journal checksums */
#define EXT4_MOUNT_JOURNAL_ASYNC_COMMIT	0x1000000 /* Journal Async Commit */
#define EXT4_MOUNT_WARN_ON_ERROR	0x2000000 /* Trigger WARN_ON on error */
#define EXT4_MOUNT_DELALLOC		0x8000000 /* Delalloc support */
#define EXT4_MOUNT_DATA_ERR_ABORT	0x10000000 /* Abort on file data write */
#define EXT4_MOUNT_BLOCK_VALIDITY	0x20000000 /* Block validity checking */
#define EXT4_MOUNT_DISCARD		0x40000000 /* Issue DISCARD requests */
#define EXT4_MOUNT_INIT_INODE_TABLE	0x80000000 /* Initialize uninitialized itables */

/*
 * Mount flags set either automatically (could not be set by mount option)
 * based on per file system feature or property or in special cases such as
 * distinguishing between explicit mount option definition and default.
 */
#define EXT4_MOUNT2_EXPLICIT_DELALLOC	0x00000001 /* User explicitly
						      specified delalloc */
#define EXT4_MOUNT2_STD_GROUP_SIZE	0x00000002 /* We have standard group
						      size of blocksize * 8
						      blocks */
#define EXT4_MOUNT2_HURD_COMPAT		0x00000004 /* Support HURD-castrated
						      file systems */

#define EXT4_MOUNT2_EXPLICIT_JOURNAL_CHECKSUM	0x00000008 /* User explicitly
						specified journal checksum */

#define clear_opt(sb, opt)		EXT4_SB(sb)->s_mount_opt &= \
						~EXT4_MOUNT_##opt
#define set_opt(sb, opt)		EXT4_SB(sb)->s_mount_opt |= \
						EXT4_MOUNT_##opt
#define test_opt(sb, opt)		(EXT4_SB(sb)->s_mount_opt & \
					 EXT4_MOUNT_##opt)

#define clear_opt2(sb, opt)		EXT4_SB(sb)->s_mount_opt2 &= \
						~EXT4_MOUNT2_##opt
#define set_opt2(sb, opt)		EXT4_SB(sb)->s_mount_opt2 |= \
						EXT4_MOUNT2_##opt
#define test_opt2(sb, opt)		(EXT4_SB(sb)->s_mount_opt2 & \
					 EXT4_MOUNT2_##opt)

#define ext4_test_and_set_bit		__test_and_set_bit_le
#define ext4_set_bit			__set_bit_le
#define ext4_set_bit_atomic		ext2_set_bit_atomic
#define ext4_test_and_clear_bit		__test_and_clear_bit_le
#define ext4_clear_bit			__clear_bit_le
#define ext4_clear_bit_atomic		ext2_clear_bit_atomic
#define ext4_test_bit			test_bit_le
#define ext4_find_next_zero_bit		find_next_zero_bit_le
#define ext4_find_next_bit		find_next_bit_le

extern void ext4_set_bits(void *bm, int cur, int len);

/*
 * Maximal mount counts between two filesystem checks
 */
#define EXT4_DFL_MAX_MNT_COUNT		20	/* Allow 20 mounts */
#define EXT4_DFL_CHECKINTERVAL		0	/* Don't use interval check */

/*
 * Behaviour when detecting errors
 */
#define EXT4_ERRORS_CONTINUE		1	/* Continue execution */
#define EXT4_ERRORS_RO			2	/* Remount fs read-only */
#define EXT4_ERRORS_PANIC		3	/* Panic */
#define EXT4_ERRORS_DEFAULT		EXT4_ERRORS_CONTINUE

/* Metadata checksum algorithm codes */
#define EXT4_CRC32C_CHKSUM		1

/*
 * Structure of the super block
 */
struct ext4_super_block {
/*00*/	__le32	s_inodes_count;		/* Inodes count */
	__le32	s_blocks_count_lo;	/* Blocks count */
	__le32	s_r_blocks_count_lo;	/* Reserved blocks count */
	__le32	s_free_blocks_count_lo;	/* Free blocks count */
/*10*/	__le32	s_free_inodes_count;	/* Free inodes count */
	__le32	s_first_data_block;	/* First Data Block */
	__le32	s_log_block_size;	/* Block size */
	__le32	s_log_cluster_size;	/* Allocation cluster size */
/*20*/	__le32	s_blocks_per_group;	/* # Blocks per group */
	__le32	s_clusters_per_group;	/* # Clusters per group */
	__le32	s_inodes_per_group;	/* # Inodes per group */
	__le32	s_mtime;		/* Mount time */
/*30*/	__le32	s_wtime;		/* Write time */
	__le16	s_mnt_count;		/* Mount count */
	__le16	s_max_mnt_count;	/* Maximal mount count */
	__le16	s_magic;		/* Magic signature */
	__le16	s_state;		/* File system state */
	__le16	s_errors;		/* Behaviour when detecting errors */
	__le16	s_minor_rev_level;	/* minor revision level */
/*40*/	__le32	s_lastcheck;		/* time of last check */
	__le32	s_checkinterval;	/* max. time between checks */
	__le32	s_creator_os;		/* OS */
	__le32	s_rev_level;		/* Revision level */
/*50*/	__le16	s_def_resuid;		/* Default uid for reserved blocks */
	__le16	s_def_resgid;		/* Default gid for reserved blocks */
	/*
	 * These fields are for EXT4_DYNAMIC_REV superblocks only.
	 *
	 * Note: the difference between the compatible feature set and
	 * the incompatible feature set is that if there is a bit set
	 * in the incompatible feature set that the kernel doesn't
	 * know about, it should refuse to mount the filesystem.
	 *
	 * e2fsck's requirements are more strict; if it doesn't know
	 * about a feature in either the compatible or incompatible
	 * feature set, it must abort and not try to meddle with
	 * things it doesn't understand...
	 */
	__le32	s_first_ino;		/* First non-reserved inode */
	__le16  s_inode_size;		/* size of inode structure */
	__le16	s_block_group_nr;	/* block group # of this superblock */
	__le32	s_feature_compat;	/* compatible feature set */
/*60*/	__le32	s_feature_incompat;	/* incompatible feature set */
	__le32	s_feature_ro_compat;	/* readonly-compatible feature set */
/*68*/	__u8	s_uuid[16];		/* 128-bit uuid for volume */
/*78*/	char	s_volume_name[16];	/* volume name */
/*88*/	char	s_last_mounted[64] __nonstring;	/* directory where last mounted */
/*C8*/	__le32	s_algorithm_usage_bitmap; /* For compression */
	/*
	 * Performance hints.  Directory preallocation should only
	 * happen if the EXT4_FEATURE_COMPAT_DIR_PREALLOC flag is on.
	 */
	__u8	s_prealloc_blocks;	/* Nr of blocks to try to preallocate*/
	__u8	s_prealloc_dir_blocks;	/* Nr to preallocate for dirs */
	__le16	s_reserved_gdt_blocks;	/* Per group desc for online growth */
	/*
	 * Journaling support valid if EXT4_FEATURE_COMPAT_HAS_JOURNAL set.
	 */
/*D0*/	__u8	s_journal_uuid[16];	/* uuid of journal superblock */
/*E0*/	__le32	s_journal_inum;		/* inode number of journal file */
	__le32	s_journal_dev;		/* device number of journal file */
	__le32	s_last_orphan;		/* start of list of inodes to delete */
	__le32	s_hash_seed[4];		/* HTREE hash seed */
	__u8	s_def_hash_version;	/* Default hash version to use */
	__u8	s_jnl_backup_type;
	__le16  s_desc_size;		/* size of group descriptor */
/*100*/	__le32	s_default_mount_opts;
	__le32	s_first_meta_bg;	/* First metablock block group */
	__le32	s_mkfs_time;		/* When the filesystem was created */
	__le32	s_jnl_blocks[17];	/* Backup of the journal inode */
	/* 64bit support valid if EXT4_FEATURE_COMPAT_64BIT */
/*150*/	__le32	s_blocks_count_hi;	/* Blocks count */
	__le32	s_r_blocks_count_hi;	/* Reserved blocks count */
	__le32	s_free_blocks_count_hi;	/* Free blocks count */
	__le16	s_min_extra_isize;	/* All inodes have at least # bytes */
	__le16	s_want_extra_isize; 	/* New inodes should reserve # bytes */
	__le32	s_flags;		/* Miscellaneous flags */
	__le16  s_raid_stride;		/* RAID stride */
	__le16  s_mmp_update_interval;  /* # seconds to wait in MMP checking */
	__le64  s_mmp_block;            /* Block for multi-mount protection */
	__le32  s_raid_stripe_width;    /* blocks on all data disks (N*stride)*/
	__u8	s_log_groups_per_flex;  /* FLEX_BG group size */
	__u8	s_checksum_type;	/* metadata checksum algorithm used */
	__u8	s_encryption_level;	/* versioning level for encryption */
	__u8	s_reserved_pad;		/* Padding to next 32bits */
	__le64	s_kbytes_written;	/* nr of lifetime kilobytes written */
	__le32	s_snapshot_inum;	/* Inode number of active snapshot */
	__le32	s_snapshot_id;		/* sequential ID of active snapshot */
	__le64	s_snapshot_r_blocks_count; /* reserved blocks for active
					      snapshot's future use */
	__le32	s_snapshot_list;	/* inode number of the head of the
					   on-disk snapshot list */
#define EXT4_S_ERR_START offsetof(struct ext4_super_block, s_error_count)
	__le32	s_error_count;		/* number of fs errors */
	__le32	s_first_error_time;	/* first time an error happened */
	__le32	s_first_error_ino;	/* inode involved in first error */
	__le64	s_first_error_block;	/* block involved of first error */
	__u8	s_first_error_func[32] __nonstring;	/* function where the error happened */
	__le32	s_first_error_line;	/* line number where error happened */
	__le32	s_last_error_time;	/* most recent time of an error */
	__le32	s_last_error_ino;	/* inode involved in last error */
	__le32	s_last_error_line;	/* line number where error happened */
	__le64	s_last_error_block;	/* block involved of last error */
	__u8	s_last_error_func[32] __nonstring;	/* function where the error happened */
#define EXT4_S_ERR_END offsetof(struct ext4_super_block, s_mount_opts)
	__u8	s_mount_opts[64];
	__le32	s_usr_quota_inum;	/* inode for tracking user quota */
	__le32	s_grp_quota_inum;	/* inode for tracking group quota */
	__le32	s_overhead_clusters;	/* overhead blocks/clusters in fs */
	__le32	s_backup_bgs[2];	/* groups with sparse_super2 SBs */
	__u8	s_encrypt_algos[4];	/* Encryption algorithms in use  */
	__u8	s_encrypt_pw_salt[16];	/* Salt used for string2key algorithm */
	__le32	s_lpf_ino;		/* Location of the lost+found inode */
	__le32	s_prj_quota_inum;	/* inode for tracking project quota */
	__le32	s_checksum_seed;	/* crc32c(uuid) if csum_seed set */
	__u8	s_wtime_hi;
	__u8	s_mtime_hi;
	__u8	s_mkfs_time_hi;
	__u8	s_lastcheck_hi;
	__u8	s_first_error_time_hi;
	__u8	s_last_error_time_hi;
	__u8	s_first_error_errcode;
	__u8    s_last_error_errcode;
	__le16  s_encoding;		/* Filename charset encoding */
	__le16  s_encoding_flags;	/* Filename charset encoding flags */
	__le32	s_reserved[95];		/* Padding to the end of the block */
	__le32	s_checksum;		/* crc32c(superblock) */
};

#define EXT4_S_ERR_LEN (EXT4_S_ERR_END - EXT4_S_ERR_START)

#ifdef __KERNEL__

/*
 * run-time mount flags
 */
#define EXT4_MF_MNTDIR_SAMPLED		0x0001
#define EXT4_MF_FS_ABORTED		0x0002	/* Fatal error detected */
#define EXT4_MF_TEST_DUMMY_ENCRYPTION	0x0004

#ifdef CONFIG_FS_ENCRYPTION
#define DUMMY_ENCRYPTION_ENABLED(sbi) (unlikely((sbi)->s_mount_flags & \
						EXT4_MF_TEST_DUMMY_ENCRYPTION))
#else
#define DUMMY_ENCRYPTION_ENABLED(sbi) (0)
#endif

/* Number of quota types we support */
#define EXT4_MAXQUOTAS 3

#define EXT4_ENC_UTF8_12_1	1

/*
 * Flags for ext4_sb_info.s_encoding_flags.
 */
#define EXT4_ENC_STRICT_MODE_FL	(1 << 0)

#define ext4_has_strict_mode(sbi) \
	(sbi->s_encoding_flags & EXT4_ENC_STRICT_MODE_FL)

/*
 * fourth extended-fs super-block data in memory
 */
struct ext4_sb_info {
	unsigned long s_desc_size;	/* Size of a group descriptor in bytes */
	unsigned long s_inodes_per_block;/* Number of inodes per block */
	unsigned long s_blocks_per_group;/* Number of blocks in a group */
	unsigned long s_clusters_per_group; /* Number of clusters in a group */
	unsigned long s_inodes_per_group;/* Number of inodes in a group */
	unsigned long s_itb_per_group;	/* Number of inode table blocks per group */
	unsigned long s_gdb_count;	/* Number of group descriptor blocks */
	unsigned long s_desc_per_block;	/* Number of group descriptors per block */
	ext4_group_t s_groups_count;	/* Number of groups in the fs */
	ext4_group_t s_blockfile_groups;/* Groups acceptable for non-extent files */
	unsigned long s_overhead;  /* # of fs overhead clusters */
	unsigned int s_cluster_ratio;	/* Number of blocks per cluster */
	unsigned int s_cluster_bits;	/* log2 of s_cluster_ratio */
	loff_t s_bitmap_maxbytes;	/* max bytes for bitmap files */
	struct buffer_head * s_sbh;	/* Buffer containing the super block */
	struct ext4_super_block *s_es;	/* Pointer to the super block in the buffer */
	struct buffer_head **s_group_desc;
	unsigned int s_mount_opt;
	unsigned int s_mount_opt2;
	unsigned int s_mount_flags;
	unsigned int s_def_mount_opt;
	ext4_fsblk_t s_sb_block;
	atomic64_t s_resv_clusters;
	kuid_t s_resuid;
	kgid_t s_resgid;
	unsigned short s_mount_state;
	unsigned short s_pad;
	int s_addr_per_block_bits;
	int s_desc_per_block_bits;
	int s_inode_size;
	int s_first_ino;
	unsigned int s_inode_readahead_blks;
	unsigned int s_inode_goal;
	u32 s_hash_seed[4];
	int s_def_hash_version;
	int s_hash_unsigned;	/* 3 if hash should be signed, 0 if not */
	struct percpu_counter s_freeclusters_counter;
	struct percpu_counter s_freeinodes_counter;
	struct percpu_counter s_dirs_counter;
	struct percpu_counter s_dirtyclusters_counter;
	struct blockgroup_lock *s_blockgroup_lock;
	struct proc_dir_entry *s_proc;
	struct kobject s_kobj;
	struct completion s_kobj_unregister;
	struct super_block *s_sb;
#ifdef CONFIG_UNICODE
	struct unicode_map *s_encoding;
	__u16 s_encoding_flags;
#endif

	/* Journaling */
	struct journal_s *s_journal;
	struct list_head s_orphan;
	struct mutex s_orphan_lock;
	unsigned long s_ext4_flags;		/* Ext4 superblock flags */
	unsigned long s_commit_interval;
	u32 s_max_batch_time;
	u32 s_min_batch_time;
	struct block_device *journal_bdev;
#ifdef CONFIG_QUOTA
	/* Names of quota files with journalled quota */
	char __rcu *s_qf_names[EXT4_MAXQUOTAS];
	int s_jquota_fmt;			/* Format of quota to use */
#endif
	unsigned int s_want_extra_isize; /* New inodes should reserve # bytes */
	struct ext4_system_blocks __rcu *system_blks;

#ifdef EXTENTS_STATS
	/* ext4 extents stats */
	unsigned long s_ext_min;
	unsigned long s_ext_max;
	unsigned long s_depth_max;
	spinlock_t s_ext_stats_lock;
	unsigned long s_ext_blocks;
	unsigned long s_ext_extents;
#endif

	/* for buddy allocator */
	struct ext4_group_info ***s_group_info;
	struct inode *s_buddy_cache;
	spinlock_t s_md_lock;
	unsigned short *s_mb_offsets;
	unsigned int *s_mb_maxs;
	unsigned int s_group_info_size;
	unsigned int s_mb_free_pending;
	struct list_head s_freed_data_list;	/* List of blocks to be freed
						   after commit completed */

	/* tunables */
	unsigned long s_stripe;
	unsigned int s_mb_stream_request;
	unsigned int s_mb_max_to_scan;
	unsigned int s_mb_min_to_scan;
	unsigned int s_mb_stats;
	unsigned int s_mb_order2_reqs;
	unsigned int s_mb_group_prealloc;
	unsigned int s_max_dir_size_kb;
	/* where last allocation was done - for stream allocation */
	unsigned long s_mb_last_group;
	unsigned long s_mb_last_start;

	/* stats for buddy allocator */
	atomic_t s_bal_reqs;	/* number of reqs with len > 1 */
	atomic_t s_bal_success;	/* we found long enough chunks */
	atomic_t s_bal_allocated;	/* in blocks */
	atomic_t s_bal_ex_scanned;	/* total extents scanned */
	atomic_t s_bal_goals;	/* goal hits */
	atomic_t s_bal_breaks;	/* too long searches */
	atomic_t s_bal_2orders;	/* 2^order hits */
	spinlock_t s_bal_lock;
	unsigned long s_mb_buddies_generated;
	unsigned long long s_mb_generation_time;
	atomic_t s_mb_lost_chunks;
	atomic_t s_mb_preallocated;
	atomic_t s_mb_discarded;
	atomic_t s_lock_busy;

	/* locality groups */
	struct ext4_locality_group __percpu *s_locality_groups;

	/* for write statistics */
	unsigned long s_sectors_written_start;
	u64 s_kbytes_written;

	/* the size of zero-out chunk */
	unsigned int s_extent_max_zeroout_kb;

	unsigned int s_log_groups_per_flex;
	struct flex_groups *s_flex_groups;
	ext4_group_t s_flex_groups_allocated;

	/* workqueue for reserved extent conversions (buffered io) */
	struct workqueue_struct *rsv_conversion_wq;

	/* timer for periodic error stats printing */
	struct timer_list s_err_report;

	/* Lazy inode table initialization info */
	struct ext4_li_request *s_li_request;
	/* Wait multiplier for lazy initialization thread */
	unsigned int s_li_wait_mult;

	/* Kernel thread for multiple mount protection */
	struct task_struct *s_mmp_tsk;

	/* record the last minlen when FITRIM is called. */
	atomic_t s_last_trim_minblks;

	/* Reference to checksum algorithm driver via cryptoapi */
	struct crypto_shash *s_chksum_driver;

	/* Precomputed FS UUID checksum for seeding other checksums */
	__u32 s_csum_seed;

	/* Reclaim extents from extent status tree */
	struct shrinker s_es_shrinker;
	struct list_head s_es_list;	/* List of inodes with reclaimable extents */
	long s_es_nr_inode;
	struct ext4_es_stats s_es_stats;
	struct mb_cache *s_ea_block_cache;
	struct mb_cache *s_ea_inode_cache;
	spinlock_t s_es_lock ____cacheline_aligned_in_smp;

	/* Ratelimit ext4 messages. */
	struct ratelimit_state s_err_ratelimit_state;
	struct ratelimit_state s_warning_ratelimit_state;
	struct ratelimit_state s_msg_ratelimit_state;

	/* Barrier between changing inodes' journal flags and writepages ops. */
	struct percpu_rw_semaphore s_journal_flag_rwsem;
	struct dax_device *s_daxdev;
#ifdef CONFIG_EXT4_DEBUG
	unsigned long s_simulate_fail;
#endif
};

static inline struct ext4_sb_info *EXT4_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}
static inline struct ext4_inode_info *EXT4_I(struct inode *inode)
{
	return container_of(inode, struct ext4_inode_info, vfs_inode);
}

static inline int ext4_valid_inum(struct super_block *sb, unsigned long ino)
{
	return ino == EXT4_ROOT_INO ||
		(ino >= EXT4_FIRST_INO(sb) &&
		 ino <= le32_to_cpu(EXT4_SB(sb)->s_es->s_inodes_count));
}

/*
 * Simulate_fail codes
 */
#define EXT4_SIM_BBITMAP_EIO	1
#define EXT4_SIM_BBITMAP_CRC	2
#define EXT4_SIM_IBITMAP_EIO	3
#define EXT4_SIM_IBITMAP_CRC	4
#define EXT4_SIM_INODE_EIO	5
#define EXT4_SIM_INODE_CRC	6
#define EXT4_SIM_DIRBLOCK_EIO	7
#define EXT4_SIM_DIRBLOCK_CRC	8

static inline bool ext4_simulate_fail(struct super_block *sb,
				     unsigned long code)
{
#ifdef CONFIG_EXT4_DEBUG
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (unlikely(sbi->s_simulate_fail == code)) {
		sbi->s_simulate_fail = 0;
		return true;
	}
#endif
	return false;
}

static inline void ext4_simulate_fail_bh(struct super_block *sb,
					 struct buffer_head *bh,
					 unsigned long code)
{
	if (!IS_ERR(bh) && ext4_simulate_fail(sb, code))
		clear_buffer_uptodate(bh);
}

/*
 * Error number codes for s_{first,last}_error_errno
 *
 * Linux errno numbers are architecture specific, so we need to translate
 * them into something which is architecture independent.   We don't define
 * codes for all errno's; just the ones which are most likely to be the cause
 * of an ext4_error() call.
 */
#define EXT4_ERR_UNKNOWN	 1
#define EXT4_ERR_EIO		 2
#define EXT4_ERR_ENOMEM		 3
#define EXT4_ERR_EFSBADCRC	 4
#define EXT4_ERR_EFSCORRUPTED	 5
#define EXT4_ERR_ENOSPC		 6
#define EXT4_ERR_ENOKEY		 7
#define EXT4_ERR_EROFS		 8
#define EXT4_ERR_EFBIG		 9
#define EXT4_ERR_EEXIST		10
#define EXT4_ERR_ERANGE		11
#define EXT4_ERR_EOVERFLOW	12
#define EXT4_ERR_EBUSY		13
#define EXT4_ERR_ENOTDIR	14
#define EXT4_ERR_ENOTEMPTY	15
#define EXT4_ERR_ESHUTDOWN	16
#define EXT4_ERR_EFAULT		17

/*
 * Inode dynamic state flags
 */
enum {
	EXT4_STATE_JDATA,		/* journaled data exists */
	EXT4_STATE_NEW,			/* inode is newly created */
	EXT4_STATE_XATTR,		/* has in-inode xattrs */
	EXT4_STATE_NO_EXPAND,		/* No space for expansion */
	EXT4_STATE_DA_ALLOC_CLOSE,	/* Alloc DA blks on close */
	EXT4_STATE_EXT_MIGRATE,		/* Inode is migrating */
	EXT4_STATE_NEWENTRY,		/* File just added to dir */
	EXT4_STATE_MAY_INLINE_DATA,	/* may have in-inode data */
	EXT4_STATE_EXT_PRECACHED,	/* extents have been precached */
	EXT4_STATE_LUSTRE_EA_INODE,	/* Lustre-style ea_inode */
	EXT4_STATE_VERITY_IN_PROGRESS,	/* building fs-verity Merkle tree */
};

#define EXT4_INODE_BIT_FNS(name, field, offset)				\
static inline int ext4_test_inode_##name(struct inode *inode, int bit)	\
{									\
	return test_bit(bit + (offset), &EXT4_I(inode)->i_##field);	\
}									\
static inline void ext4_set_inode_##name(struct inode *inode, int bit)	\
{									\
	set_bit(bit + (offset), &EXT4_I(inode)->i_##field);		\
}									\
static inline void ext4_clear_inode_##name(struct inode *inode, int bit) \
{									\
	clear_bit(bit + (offset), &EXT4_I(inode)->i_##field);		\
}

/* Add these declarations here only so that these functions can be
 * found by name.  Otherwise, they are very hard to locate. */
static inline int ext4_test_inode_flag(struct inode *inode, int bit);
static inline void ext4_set_inode_flag(struct inode *inode, int bit);
static inline void ext4_clear_inode_flag(struct inode *inode, int bit);
EXT4_INODE_BIT_FNS(flag, flags, 0)

/* Add these declarations here only so that these functions can be
 * found by name.  Otherwise, they are very hard to locate. */
static inline int ext4_test_inode_state(struct inode *inode, int bit);
static inline void ext4_set_inode_state(struct inode *inode, int bit);
static inline void ext4_clear_inode_state(struct inode *inode, int bit);
#if (BITS_PER_LONG < 64)
EXT4_INODE_BIT_FNS(state, state_flags, 0)

static inline void ext4_clear_state_flags(struct ext4_inode_info *ei)
{
	(ei)->i_state_flags = 0;
}
#else
EXT4_INODE_BIT_FNS(state, flags, 32)

static inline void ext4_clear_state_flags(struct ext4_inode_info *ei)
{
	/* We depend on the fact that callers will set i_flags */
}
#endif
#else
/* Assume that user mode programs are passing in an ext4fs superblock, not
 * a kernel struct super_block.  This will allow us to call the feature-test
 * macros from user land. */
#define EXT4_SB(sb)	(sb)
#endif

static inline bool ext4_verity_in_progress(struct inode *inode)
{
	return IS_ENABLED(CONFIG_FS_VERITY) &&
	       ext4_test_inode_state(inode, EXT4_STATE_VERITY_IN_PROGRESS);
}

#define NEXT_ORPHAN(inode) EXT4_I(inode)->i_dtime

/*
 * Codes for operating systems
 */
#define EXT4_OS_LINUX		0
#define EXT4_OS_HURD		1
#define EXT4_OS_MASIX		2
#define EXT4_OS_FREEBSD		3
#define EXT4_OS_LITES		4

/*
 * Revision levels
 */
#define EXT4_GOOD_OLD_REV	0	/* The good old (original) format */
#define EXT4_DYNAMIC_REV	1	/* V2 format w/ dynamic inode sizes */

#define EXT4_CURRENT_REV	EXT4_GOOD_OLD_REV
#define EXT4_MAX_SUPP_REV	EXT4_DYNAMIC_REV

#define EXT4_GOOD_OLD_INODE_SIZE 128

#define EXT4_EXTRA_TIMESTAMP_MAX	(((s64)1 << 34) - 1  + S32_MIN)
#define EXT4_NON_EXTRA_TIMESTAMP_MAX	S32_MAX
#define EXT4_TIMESTAMP_MIN		S32_MIN

/*
 * Feature set definitions
 */

#define EXT4_FEATURE_COMPAT_DIR_PREALLOC	0x0001
#define EXT4_FEATURE_COMPAT_IMAGIC_INODES	0x0002
#define EXT4_FEATURE_COMPAT_HAS_JOURNAL		0x0004
#define EXT4_FEATURE_COMPAT_EXT_ATTR		0x0008
#define EXT4_FEATURE_COMPAT_RESIZE_INODE	0x0010
#define EXT4_FEATURE_COMPAT_DIR_INDEX		0x0020
#define EXT4_FEATURE_COMPAT_SPARSE_SUPER2	0x0200
#define EXT4_FEATURE_COMPAT_STABLE_INODES	0x0800

#define EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER	0x0001
#define EXT4_FEATURE_RO_COMPAT_LARGE_FILE	0x0002
#define EXT4_FEATURE_RO_COMPAT_BTREE_DIR	0x0004
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE        0x0008
#define EXT4_FEATURE_RO_COMPAT_GDT_CSUM		0x0010
#define EXT4_FEATURE_RO_COMPAT_DIR_NLINK	0x0020
#define EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE	0x0040
#define EXT4_FEATURE_RO_COMPAT_QUOTA		0x0100
#define EXT4_FEATURE_RO_COMPAT_BIGALLOC		0x0200
/*
 * METADATA_CSUM also enables group descriptor checksums (GDT_CSUM).  When
 * METADATA_CSUM is set, group descriptor checksums use the same algorithm as
 * all other data structures' checksums.  However, the METADATA_CSUM and
 * GDT_CSUM bits are mutually exclusive.
 */
#define EXT4_FEATURE_RO_COMPAT_METADATA_CSUM	0x0400
#define EXT4_FEATURE_RO_COMPAT_READONLY		0x1000
#define EXT4_FEATURE_RO_COMPAT_PROJECT		0x2000
#define EXT4_FEATURE_RO_COMPAT_VERITY		0x8000

#define EXT4_FEATURE_INCOMPAT_COMPRESSION	0x0001
#define EXT4_FEATURE_INCOMPAT_FILETYPE		0x0002
#define EXT4_FEATURE_INCOMPAT_RECOVER		0x0004 /* Needs recovery */
#define EXT4_FEATURE_INCOMPAT_JOURNAL_DEV	0x0008 /* Journal device */
#define EXT4_FEATURE_INCOMPAT_META_BG		0x0010
#define EXT4_FEATURE_INCOMPAT_EXTENTS		0x0040 /* extents support */
#define EXT4_FEATURE_INCOMPAT_64BIT		0x0080
#define EXT4_FEATURE_INCOMPAT_MMP               0x0100
#define EXT4_FEATURE_INCOMPAT_FLEX_BG		0x0200
#define EXT4_FEATURE_INCOMPAT_EA_INODE		0x0400 /* EA in inode */
#define EXT4_FEATURE_INCOMPAT_DIRDATA		0x1000 /* data in dirent */
#define EXT4_FEATURE_INCOMPAT_CSUM_SEED		0x2000
#define EXT4_FEATURE_INCOMPAT_LARGEDIR		0x4000 /* >2GB or 3-lvl htree */
#define EXT4_FEATURE_INCOMPAT_INLINE_DATA	0x8000 /* data in inode */
#define EXT4_FEATURE_INCOMPAT_ENCRYPT		0x10000
#define EXT4_FEATURE_INCOMPAT_CASEFOLD		0x20000

extern void ext4_update_dynamic_rev(struct super_block *sb);

#define EXT4_FEATURE_COMPAT_FUNCS(name, flagname) \
static inline bool ext4_has_feature_##name(struct super_block *sb) \
{ \
	return ((EXT4_SB(sb)->s_es->s_feature_compat & \
		cpu_to_le32(EXT4_FEATURE_COMPAT_##flagname)) != 0); \
} \
static inline void ext4_set_feature_##name(struct super_block *sb) \
{ \
	ext4_update_dynamic_rev(sb); \
	EXT4_SB(sb)->s_es->s_feature_compat |= \
		cpu_to_le32(EXT4_FEATURE_COMPAT_##flagname); \
} \
static inline void ext4_clear_feature_##name(struct super_block *sb) \
{ \
	EXT4_SB(sb)->s_es->s_feature_compat &= \
		~cpu_to_le32(EXT4_FEATURE_COMPAT_##flagname); \
}

#define EXT4_FEATURE_RO_COMPAT_FUNCS(name, flagname) \
static inline bool ext4_has_feature_##name(struct super_block *sb) \
{ \
	return ((EXT4_SB(sb)->s_es->s_feature_ro_compat & \
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_##flagname)) != 0); \
} \
static inline void ext4_set_feature_##name(struct super_block *sb) \
{ \
	ext4_update_dynamic_rev(sb); \
	EXT4_SB(sb)->s_es->s_feature_ro_compat |= \
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_##flagname); \
} \
static inline void ext4_clear_feature_##name(struct super_block *sb) \
{ \
	EXT4_SB(sb)->s_es->s_feature_ro_compat &= \
		~cpu_to_le32(EXT4_FEATURE_RO_COMPAT_##flagname); \
}

#define EXT4_FEATURE_INCOMPAT_FUNCS(name, flagname) \
static inline bool ext4_has_feature_##name(struct super_block *sb) \
{ \
	return ((EXT4_SB(sb)->s_es->s_feature_incompat & \
		cpu_to_le32(EXT4_FEATURE_INCOMPAT_##flagname)) != 0); \
} \
static inline void ext4_set_feature_##name(struct super_block *sb) \
{ \
	ext4_update_dynamic_rev(sb); \
	EXT4_SB(sb)->s_es->s_feature_incompat |= \
		cpu_to_le32(EXT4_FEATURE_INCOMPAT_##flagname); \
} \
static inline void ext4_clear_feature_##name(struct super_block *sb) \
{ \
	EXT4_SB(sb)->s_es->s_feature_incompat &= \
		~cpu_to_le32(EXT4_FEATURE_INCOMPAT_##flagname); \
}

EXT4_FEATURE_COMPAT_FUNCS(dir_prealloc,		DIR_PREALLOC)
EXT4_FEATURE_COMPAT_FUNCS(imagic_inodes,	IMAGIC_INODES)
EXT4_FEATURE_COMPAT_FUNCS(journal,		HAS_JOURNAL)
EXT4_FEATURE_COMPAT_FUNCS(xattr,		EXT_ATTR)
EXT4_FEATURE_COMPAT_FUNCS(resize_inode,		RESIZE_INODE)
EXT4_FEATURE_COMPAT_FUNCS(dir_index,		DIR_INDEX)
EXT4_FEATURE_COMPAT_FUNCS(sparse_super2,	SPARSE_SUPER2)
EXT4_FEATURE_COMPAT_FUNCS(stable_inodes,	STABLE_INODES)

EXT4_FEATURE_RO_COMPAT_FUNCS(sparse_super,	SPARSE_SUPER)
EXT4_FEATURE_RO_COMPAT_FUNCS(large_file,	LARGE_FILE)
EXT4_FEATURE_RO_COMPAT_FUNCS(btree_dir,		BTREE_DIR)
EXT4_FEATURE_RO_COMPAT_FUNCS(huge_file,		HUGE_FILE)
EXT4_FEATURE_RO_COMPAT_FUNCS(gdt_csum,		GDT_CSUM)
EXT4_FEATURE_RO_COMPAT_FUNCS(dir_nlink,		DIR_NLINK)
EXT4_FEATURE_RO_COMPAT_FUNCS(extra_isize,	EXTRA_ISIZE)
EXT4_FEATURE_RO_COMPAT_FUNCS(quota,		QUOTA)
EXT4_FEATURE_RO_COMPAT_FUNCS(bigalloc,		BIGALLOC)
EXT4_FEATURE_RO_COMPAT_FUNCS(metadata_csum,	METADATA_CSUM)
EXT4_FEATURE_RO_COMPAT_FUNCS(readonly,		READONLY)
EXT4_FEATURE_RO_COMPAT_FUNCS(project,		PROJECT)
EXT4_FEATURE_RO_COMPAT_FUNCS(verity,		VERITY)

EXT4_FEATURE_INCOMPAT_FUNCS(compression,	COMPRESSION)
EXT4_FEATURE_INCOMPAT_FUNCS(filetype,		FILETYPE)
EXT4_FEATURE_INCOMPAT_FUNCS(journal_needs_recovery,	RECOVER)
EXT4_FEATURE_INCOMPAT_FUNCS(journal_dev,	JOURNAL_DEV)
EXT4_FEATURE_INCOMPAT_FUNCS(meta_bg,		META_BG)
EXT4_FEATURE_INCOMPAT_FUNCS(extents,		EXTENTS)
EXT4_FEATURE_INCOMPAT_FUNCS(64bit,		64BIT)
EXT4_FEATURE_INCOMPAT_FUNCS(mmp,		MMP)
EXT4_FEATURE_INCOMPAT_FUNCS(flex_bg,		FLEX_BG)
EXT4_FEATURE_INCOMPAT_FUNCS(ea_inode,		EA_INODE)
EXT4_FEATURE_INCOMPAT_FUNCS(dirdata,		DIRDATA)
EXT4_FEATURE_INCOMPAT_FUNCS(csum_seed,		CSUM_SEED)
EXT4_FEATURE_INCOMPAT_FUNCS(largedir,		LARGEDIR)
EXT4_FEATURE_INCOMPAT_FUNCS(inline_data,	INLINE_DATA)
EXT4_FEATURE_INCOMPAT_FUNCS(encrypt,		ENCRYPT)
EXT4_FEATURE_INCOMPAT_FUNCS(casefold,		CASEFOLD)

#define EXT2_FEATURE_COMPAT_SUPP	EXT4_FEATURE_COMPAT_EXT_ATTR
#define EXT2_FEATURE_INCOMPAT_SUPP	(EXT4_FEATURE_INCOMPAT_FILETYPE| \
					 EXT4_FEATURE_INCOMPAT_META_BG)
#define EXT2_FEATURE_RO_COMPAT_SUPP	(EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER| \
					 EXT4_FEATURE_RO_COMPAT_LARGE_FILE| \
					 EXT4_FEATURE_RO_COMPAT_BTREE_DIR)

#define EXT3_FEATURE_COMPAT_SUPP	EXT4_FEATURE_COMPAT_EXT_ATTR
#define EXT3_FEATURE_INCOMPAT_SUPP	(EXT4_FEATURE_INCOMPAT_FILETYPE| \
					 EXT4_FEATURE_INCOMPAT_RECOVER| \
					 EXT4_FEATURE_INCOMPAT_META_BG)
#define EXT3_FEATURE_RO_COMPAT_SUPP	(EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER| \
					 EXT4_FEATURE_RO_COMPAT_LARGE_FILE| \
					 EXT4_FEATURE_RO_COMPAT_BTREE_DIR)

#define EXT4_FEATURE_COMPAT_SUPP	EXT4_FEATURE_COMPAT_EXT_ATTR
#define EXT4_FEATURE_INCOMPAT_SUPP	(EXT4_FEATURE_INCOMPAT_FILETYPE| \
					 EXT4_FEATURE_INCOMPAT_RECOVER| \
					 EXT4_FEATURE_INCOMPAT_META_BG| \
					 EXT4_FEATURE_INCOMPAT_EXTENTS| \
					 EXT4_FEATURE_INCOMPAT_64BIT| \
					 EXT4_FEATURE_INCOMPAT_FLEX_BG| \
					 EXT4_FEATURE_INCOMPAT_EA_INODE| \
					 EXT4_FEATURE_INCOMPAT_MMP | \
					 EXT4_FEATURE_INCOMPAT_INLINE_DATA | \
					 EXT4_FEATURE_INCOMPAT_ENCRYPT | \
					 EXT4_FEATURE_INCOMPAT_CASEFOLD | \
					 EXT4_FEATURE_INCOMPAT_CSUM_SEED | \
					 EXT4_FEATURE_INCOMPAT_LARGEDIR)
#define EXT4_FEATURE_RO_COMPAT_SUPP	(EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER| \
					 EXT4_FEATURE_RO_COMPAT_LARGE_FILE| \
					 EXT4_FEATURE_RO_COMPAT_GDT_CSUM| \
					 EXT4_FEATURE_RO_COMPAT_DIR_NLINK | \
					 EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE | \
					 EXT4_FEATURE_RO_COMPAT_BTREE_DIR |\
					 EXT4_FEATURE_RO_COMPAT_HUGE_FILE |\
					 EXT4_FEATURE_RO_COMPAT_BIGALLOC |\
					 EXT4_FEATURE_RO_COMPAT_METADATA_CSUM|\
					 EXT4_FEATURE_RO_COMPAT_QUOTA |\
					 EXT4_FEATURE_RO_COMPAT_PROJECT |\
					 EXT4_FEATURE_RO_COMPAT_VERITY)

#define EXTN_FEATURE_FUNCS(ver) \
static inline bool ext4_has_unknown_ext##ver##_compat_features(struct super_block *sb) \
{ \
	return ((EXT4_SB(sb)->s_es->s_feature_compat & \
		cpu_to_le32(~EXT##ver##_FEATURE_COMPAT_SUPP)) != 0); \
} \
static inline bool ext4_has_unknown_ext##ver##_ro_compat_features(struct super_block *sb) \
{ \
	return ((EXT4_SB(sb)->s_es->s_feature_ro_compat & \
		cpu_to_le32(~EXT##ver##_FEATURE_RO_COMPAT_SUPP)) != 0); \
} \
static inline bool ext4_has_unknown_ext##ver##_incompat_features(struct super_block *sb) \
{ \
	return ((EXT4_SB(sb)->s_es->s_feature_incompat & \
		cpu_to_le32(~EXT##ver##_FEATURE_INCOMPAT_SUPP)) != 0); \
}

EXTN_FEATURE_FUNCS(2)
EXTN_FEATURE_FUNCS(3)
EXTN_FEATURE_FUNCS(4)

static inline bool ext4_has_compat_features(struct super_block *sb)
{
	return (EXT4_SB(sb)->s_es->s_feature_compat != 0);
}
static inline bool ext4_has_ro_compat_features(struct super_block *sb)
{
	return (EXT4_SB(sb)->s_es->s_feature_ro_compat != 0);
}
static inline bool ext4_has_incompat_features(struct super_block *sb)
{
	return (EXT4_SB(sb)->s_es->s_feature_incompat != 0);
}

/*
 * Superblock flags
 */
#define EXT4_FLAGS_RESIZING	0
#define EXT4_FLAGS_SHUTDOWN	1

static inline int ext4_forced_shutdown(struct ext4_sb_info *sbi)
{
	return test_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
}


/*
 * Default values for user and/or group using reserved blocks
 */
#define	EXT4_DEF_RESUID		0
#define	EXT4_DEF_RESGID		0

/*
 * Default project ID
 */
#define	EXT4_DEF_PROJID		0

#define EXT4_DEF_INODE_READAHEAD_BLKS	32

/*
 * Default mount options
 */
#define EXT4_DEFM_DEBUG		0x0001
#define EXT4_DEFM_BSDGROUPS	0x0002
#define EXT4_DEFM_XATTR_USER	0x0004
#define EXT4_DEFM_ACL		0x0008
#define EXT4_DEFM_UID16		0x0010
#define EXT4_DEFM_JMODE		0x0060
#define EXT4_DEFM_JMODE_DATA	0x0020
#define EXT4_DEFM_JMODE_ORDERED	0x0040
#define EXT4_DEFM_JMODE_WBACK	0x0060
#define EXT4_DEFM_NOBARRIER	0x0100
#define EXT4_DEFM_BLOCK_VALIDITY 0x0200
#define EXT4_DEFM_DISCARD	0x0400
#define EXT4_DEFM_NODELALLOC	0x0800

/*
 * Default journal batch times
 */
#define EXT4_DEF_MIN_BATCH_TIME	0
#define EXT4_DEF_MAX_BATCH_TIME	15000 /* 15ms */

/*
 * Minimum number of groups in a flexgroup before we separate out
 * directories into the first block group of a flexgroup
 */
#define EXT4_FLEX_SIZE_DIR_ALLOC_SCHEME	4

/*
 * Structure of a directory entry
 */
#define EXT4_NAME_LEN 255

struct ext4_dir_entry {
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__le16	name_len;		/* Name length */
	char	name[EXT4_NAME_LEN];	/* File name */
};

/*
 * The new version of the directory entry.  Since EXT4 structures are
 * stored in intel byte order, and the name_len field could never be
 * bigger than 255 chars, it's safe to reclaim the extra byte for the
 * file_type field.
 */
struct ext4_dir_entry_2 {
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__u8	name_len;		/* Name length */
	__u8	file_type;
	char	name[EXT4_NAME_LEN];	/* File name */
};

/*
 * This is a bogus directory entry at the end of each leaf block that
 * records checksums.
 */
struct ext4_dir_entry_tail {
	__le32	det_reserved_zero1;	/* Pretend to be unused */
	__le16	det_rec_len;		/* 12 */
	__u8	det_reserved_zero2;	/* Zero name length */
	__u8	det_reserved_ft;	/* 0xDE, fake file type */
	__le32	det_checksum;		/* crc32c(uuid+inum+dirblock) */
};

#define EXT4_DIRENT_TAIL(block, blocksize) \
	((struct ext4_dir_entry_tail *)(((void *)(block)) + \
					((blocksize) - \
					 sizeof(struct ext4_dir_entry_tail))))

/*
 * Ext4 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
#define EXT4_FT_UNKNOWN		0
#define EXT4_FT_REG_FILE	1
#define EXT4_FT_DIR		2
#define EXT4_FT_CHRDEV		3
#define EXT4_FT_BLKDEV		4
#define EXT4_FT_FIFO		5
#define EXT4_FT_SOCK		6
#define EXT4_FT_SYMLINK		7

#define EXT4_FT_MAX		8

#define EXT4_FT_DIR_CSUM	0xDE

/*
 * EXT4_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define EXT4_DIR_PAD			4
#define EXT4_DIR_ROUND			(EXT4_DIR_PAD - 1)
#define EXT4_DIR_REC_LEN(name_len)	(((name_len) + 8 + EXT4_DIR_ROUND) & \
					 ~EXT4_DIR_ROUND)
#define EXT4_MAX_REC_LEN		((1<<16)-1)

/*
 * If we ever get support for fs block sizes > page_size, we'll need
 * to remove the #if statements in the next two functions...
 */
static inline unsigned int
ext4_rec_len_from_disk(__le16 dlen, unsigned blocksize)
{
	unsigned len = le16_to_cpu(dlen);

#if (PAGE_SIZE >= 65536)
	if (len == EXT4_MAX_REC_LEN || len == 0)
		return blocksize;
	return (len & 65532) | ((len & 3) << 16);
#else
	return len;
#endif
}

static inline __le16 ext4_rec_len_to_disk(unsigned len, unsigned blocksize)
{
	if ((len > blocksize) || (blocksize > (1 << 18)) || (len & 3))
		BUG();
#if (PAGE_SIZE >= 65536)
	if (len < 65536)
		return cpu_to_le16(len);
	if (len == blocksize) {
		if (blocksize == 65536)
			return cpu_to_le16(EXT4_MAX_REC_LEN);
		else
			return cpu_to_le16(0);
	}
	return cpu_to_le16((len & 65532) | ((len >> 16) & 3));
#else
	return cpu_to_le16(len);
#endif
}

/*
 * Hash Tree Directory indexing
 * (c) Daniel Phillips, 2001
 */

#define is_dx(dir) (ext4_has_feature_dir_index((dir)->i_sb) && \
		    ext4_test_inode_flag((dir), EXT4_INODE_INDEX))
#define EXT4_DIR_LINK_MAX(dir) unlikely((dir)->i_nlink >= EXT4_LINK_MAX && \
		    !(ext4_has_feature_dir_nlink((dir)->i_sb) && is_dx(dir)))
#define EXT4_DIR_LINK_EMPTY(dir) ((dir)->i_nlink == 2 || (dir)->i_nlink == 1)

/* Legal values for the dx_root hash_version field: */

#define DX_HASH_LEGACY			0
#define DX_HASH_HALF_MD4		1
#define DX_HASH_TEA			2
#define DX_HASH_LEGACY_UNSIGNED		3
#define DX_HASH_HALF_MD4_UNSIGNED	4
#define DX_HASH_TEA_UNSIGNED		5

static inline u32 ext4_chksum(struct ext4_sb_info *sbi, u32 crc,
			      const void *address, unsigned int length)
{
	struct {
		struct shash_desc shash;
		char ctx[4];
	} desc;

	BUG_ON(crypto_shash_descsize(sbi->s_chksum_driver)!=sizeof(desc.ctx));

	desc.shash.tfm = sbi->s_chksum_driver;
	*(u32 *)desc.ctx = crc;

	BUG_ON(crypto_shash_update(&desc.shash, address, length));

	return *(u32 *)desc.ctx;
}

#ifdef __KERNEL__

/* hash info structure used by the directory hash */
struct dx_hash_info
{
	u32		hash;
	u32		minor_hash;
	int		hash_version;
	u32		*seed;
};


/* 32 and 64 bit signed EOF for dx directories */
#define EXT4_HTREE_EOF_32BIT   ((1UL  << (32 - 1)) - 1)
#define EXT4_HTREE_EOF_64BIT   ((1ULL << (64 - 1)) - 1)


/*
 * Control parameters used by ext4_htree_next_block
 */
#define HASH_NB_ALWAYS		1

struct ext4_filename {
	const struct qstr *usr_fname;
	struct fscrypt_str disk_name;
	struct dx_hash_info hinfo;
#ifdef CONFIG_FS_ENCRYPTION
	struct fscrypt_str crypto_buf;
#endif
#ifdef CONFIG_UNICODE
	struct fscrypt_str cf_name;
#endif
};

#define fname_name(p) ((p)->disk_name.name)
#define fname_len(p)  ((p)->disk_name.len)

/*
 * Describe an inode's exact location on disk and in memory
 */
struct ext4_iloc
{
	struct buffer_head *bh;
	unsigned long offset;
	ext4_group_t block_group;
};

static inline struct ext4_inode *ext4_raw_inode(struct ext4_iloc *iloc)
{
	return (struct ext4_inode *) (iloc->bh->b_data + iloc->offset);
}

static inline bool ext4_is_quota_file(struct inode *inode)
{
	return IS_NOQUOTA(inode) &&
	       !(EXT4_I(inode)->i_flags & EXT4_EA_INODE_FL);
}

/*
 * This structure is stuffed into the struct file's private_data field
 * for directories.  It is where we put information so that we can do
 * readdir operations in hash tree order.
 */
struct dir_private_info {
	struct rb_root	root;
	struct rb_node	*curr_node;
	struct fname	*extra_fname;
	loff_t		last_pos;
	__u32		curr_hash;
	__u32		curr_minor_hash;
	__u32		next_hash;
};

/* calculate the first block number of the group */
static inline ext4_fsblk_t
ext4_group_first_block_no(struct super_block *sb, ext4_group_t group_no)
{
	return group_no * (ext4_fsblk_t)EXT4_BLOCKS_PER_GROUP(sb) +
		le32_to_cpu(EXT4_SB(sb)->s_es->s_first_data_block);
}

/*
 * Special error return code only used by dx_probe() and its callers.
 */
#define ERR_BAD_DX_DIR	(-(MAX_ERRNO - 1))

/* htree levels for ext4 */
#define	EXT4_HTREE_LEVEL_COMPAT	2
#define	EXT4_HTREE_LEVEL	3

static inline int ext4_dir_htree_level(struct super_block *sb)
{
	return ext4_has_feature_largedir(sb) ?
		EXT4_HTREE_LEVEL : EXT4_HTREE_LEVEL_COMPAT;
}

/*
 * Timeout and state flag for lazy initialization inode thread.
 */
#define EXT4_DEF_LI_WAIT_MULT			10
#define EXT4_DEF_LI_MAX_START_DELAY		5
#define EXT4_LAZYINIT_QUIT			0x0001
#define EXT4_LAZYINIT_RUNNING			0x0002

/*
 * Lazy inode table initialization info
 */
struct ext4_lazy_init {
	unsigned long		li_state;
	struct list_head	li_request_list;
	struct mutex		li_list_mtx;
};

struct ext4_li_request {
	struct super_block	*lr_super;
	struct ext4_sb_info	*lr_sbi;
	ext4_group_t		lr_next_group;
	struct list_head	lr_request;
	unsigned long		lr_next_sched;
	unsigned long		lr_timeout;
};

struct ext4_features {
	struct kobject f_kobj;
	struct completion f_kobj_unregister;
};

/*
 * This structure will be used for multiple mount protection. It will be
 * written into the block number saved in the s_mmp_block field in the
 * superblock. Programs that check MMP should assume that if
 * SEQ_FSCK (or any unknown code above SEQ_MAX) is present then it is NOT safe
 * to use the filesystem, regardless of how old the timestamp is.
 */
#define EXT4_MMP_MAGIC     0x004D4D50U /* ASCII for MMP */
#define EXT4_MMP_SEQ_CLEAN 0xFF4D4D50U /* mmp_seq value for clean unmount */
#define EXT4_MMP_SEQ_FSCK  0xE24D4D50U /* mmp_seq value when being fscked */
#define EXT4_MMP_SEQ_MAX   0xE24D4D4FU /* maximum valid mmp_seq value */

struct mmp_struct {
	__le32	mmp_magic;		/* Magic number for MMP */
	__le32	mmp_seq;		/* Sequence no. updated periodically */

	/*
	 * mmp_time, mmp_nodename & mmp_bdevname are only used for information
	 * purposes and do not affect the correctness of the algorithm
	 */
	__le64	mmp_time;		/* Time last updated */
	char	mmp_nodename[64];	/* Node which last updated MMP block */
	char	mmp_bdevname[32];	/* Bdev which last updated MMP block */

	/*
	 * mmp_check_interval is used to verify if the MMP block has been
	 * updated on the block device. The value is updated based on the
	 * maximum time to write the MMP block during an update cycle.
	 */
	__le16	mmp_check_interval;

	__le16	mmp_pad1;
	__le32	mmp_pad2[226];
	__le32	mmp_checksum;		/* crc32c(uuid+mmp_block) */
};

/* arguments passed to the mmp thread */
struct mmpd_data {
	struct buffer_head *bh; /* bh from initial read_mmp_block() */
	struct super_block *sb;  /* super block of the fs */
};

/*
 * Check interval multiplier
 * The MMP block is written every update interval and initially checked every
 * update interval x the multiplier (the value is then adapted based on the
 * write latency). The reason is that writes can be delayed under load and we
 * don't want readers to incorrectly assume that the filesystem is no longer
 * in use.
 */
#define EXT4_MMP_CHECK_MULT		2UL

/*
 * Minimum interval for MMP checking in seconds.
 */
#define EXT4_MMP_MIN_CHECK_INTERVAL	5UL

/*
 * Maximum interval for MMP checking in seconds.
 */
#define EXT4_MMP_MAX_CHECK_INTERVAL	300UL

/*
 * Function prototypes
 */

/*
 * Ok, these declarations are also in <linux/kernel.h> but none of the
 * ext4 source programs needs to include it so they are duplicated here.
 */
# define NORET_TYPE	/**/
# define ATTRIB_NORET	__attribute__((noreturn))
# define NORET_AND	noreturn,

/* bitmap.c */
extern unsigned int ext4_count_free(char *bitmap, unsigned numchars);
void ext4_inode_bitmap_csum_set(struct super_block *sb, ext4_group_t group,
				struct ext4_group_desc *gdp,
				struct buffer_head *bh, int sz);
int ext4_inode_bitmap_csum_verify(struct super_block *sb, ext4_group_t group,
				  struct ext4_group_desc *gdp,
				  struct buffer_head *bh, int sz);
void ext4_block_bitmap_csum_set(struct super_block *sb, ext4_group_t group,
				struct ext4_group_desc *gdp,
				struct buffer_head *bh);
int ext4_block_bitmap_csum_verify(struct super_block *sb, ext4_group_t group,
				  struct ext4_group_desc *gdp,
				  struct buffer_head *bh);

/* balloc.c */
extern void ext4_get_group_no_and_offset(struct super_block *sb,
					 ext4_fsblk_t blocknr,
					 ext4_group_t *blockgrpp,
					 ext4_grpblk_t *offsetp);
extern ext4_group_t ext4_get_group_number(struct super_block *sb,
					  ext4_fsblk_t block);

extern unsigned int ext4_block_group(struct super_block *sb,
			ext4_fsblk_t blocknr);
extern ext4_grpblk_t ext4_block_group_offset(struct super_block *sb,
			ext4_fsblk_t blocknr);
extern int ext4_bg_has_super(struct super_block *sb, ext4_group_t group);
extern unsigned long ext4_bg_num_gdb(struct super_block *sb,
			ext4_group_t group);
extern ext4_fsblk_t ext4_new_meta_blocks(handle_t *handle, struct inode *inode,
					 ext4_fsblk_t goal,
					 unsigned int flags,
					 unsigned long *count,
					 int *errp);
extern int ext4_claim_free_clusters(struct ext4_sb_info *sbi,
				    s64 nclusters, unsigned int flags);
extern ext4_fsblk_t ext4_count_free_clusters(struct super_block *);
extern void ext4_check_blocks_bitmap(struct super_block *);
extern struct ext4_group_desc * ext4_get_group_desc(struct super_block * sb,
						    ext4_group_t block_group,
						    struct buffer_head ** bh);
extern int ext4_should_retry_alloc(struct super_block *sb, int *retries);

extern struct buffer_head *ext4_read_block_bitmap_nowait(struct super_block *sb,
						ext4_group_t block_group);
extern int ext4_wait_block_bitmap(struct super_block *sb,
				  ext4_group_t block_group,
				  struct buffer_head *bh);
extern struct buffer_head *ext4_read_block_bitmap(struct super_block *sb,
						  ext4_group_t block_group);
extern unsigned ext4_free_clusters_after_init(struct super_block *sb,
					      ext4_group_t block_group,
					      struct ext4_group_desc *gdp);
ext4_fsblk_t ext4_inode_to_goal_block(struct inode *);

#ifdef CONFIG_UNICODE
extern void ext4_fname_setup_ci_filename(struct inode *dir,
					 const struct qstr *iname,
					 struct fscrypt_str *fname);
#endif

#ifdef CONFIG_FS_ENCRYPTION
static inline void ext4_fname_from_fscrypt_name(struct ext4_filename *dst,
						const struct fscrypt_name *src)
{
	memset(dst, 0, sizeof(*dst));

	dst->usr_fname = src->usr_fname;
	dst->disk_name = src->disk_name;
	dst->hinfo.hash = src->hash;
	dst->hinfo.minor_hash = src->minor_hash;
	dst->crypto_buf = src->crypto_buf;
}

static inline int ext4_fname_setup_filename(struct inode *dir,
					    const struct qstr *iname,
					    int lookup,
					    struct ext4_filename *fname)
{
	struct fscrypt_name name;
	int err;

	err = fscrypt_setup_filename(dir, iname, lookup, &name);
	if (err)
		return err;

	ext4_fname_from_fscrypt_name(fname, &name);

#ifdef CONFIG_UNICODE
	ext4_fname_setup_ci_filename(dir, iname, &fname->cf_name);
#endif
	return 0;
}

static inline int ext4_fname_prepare_lookup(struct inode *dir,
					    struct dentry *dentry,
					    struct ext4_filename *fname)
{
	struct fscrypt_name name;
	int err;

	err = fscrypt_prepare_lookup(dir, dentry, &name);
	if (err)
		return err;

	ext4_fname_from_fscrypt_name(fname, &name);

#ifdef CONFIG_UNICODE
	ext4_fname_setup_ci_filename(dir, &dentry->d_name, &fname->cf_name);
#endif
	return 0;
}

static inline void ext4_fname_free_filename(struct ext4_filename *fname)
{
	struct fscrypt_name name;

	name.crypto_buf = fname->crypto_buf;
	fscrypt_free_filename(&name);

	fname->crypto_buf.name = NULL;
	fname->usr_fname = NULL;
	fname->disk_name.name = NULL;

#ifdef CONFIG_UNICODE
	kfree(fname->cf_name.name);
	fname->cf_name.name = NULL;
#endif
}
#else /* !CONFIG_FS_ENCRYPTION */
static inline int ext4_fname_setup_filename(struct inode *dir,
					    const struct qstr *iname,
					    int lookup,
					    struct ext4_filename *fname)
{
	fname->usr_fname = iname;
	fname->disk_name.name = (unsigned char *) iname->name;
	fname->disk_name.len = iname->len;

#ifdef CONFIG_UNICODE
	ext4_fname_setup_ci_filename(dir, iname, &fname->cf_name);
#endif

	return 0;
}

static inline int ext4_fname_prepare_lookup(struct inode *dir,
					    struct dentry *dentry,
					    struct ext4_filename *fname)
{
	return ext4_fname_setup_filename(dir, &dentry->d_name, 1, fname);
}

static inline void ext4_fname_free_filename(struct ext4_filename *fname)
{
#ifdef CONFIG_UNICODE
	kfree(fname->cf_name.name);
	fname->cf_name.name = NULL;
#endif
}
#endif /* !CONFIG_FS_ENCRYPTION */

/* dir.c */
extern int __ext4_check_dir_entry(const char *, unsigned int, struct inode *,
				  struct file *,
				  struct ext4_dir_entry_2 *,
				  struct buffer_head *, char *, int,
				  unsigned int);
#define ext4_check_dir_entry(dir, filp, de, bh, buf, size, offset)	\
	unlikely(__ext4_check_dir_entry(__func__, __LINE__, (dir), (filp), \
					(de), (bh), (buf), (size), (offset)))
extern int ext4_htree_store_dirent(struct file *dir_file, __u32 hash,
				__u32 minor_hash,
				struct ext4_dir_entry_2 *dirent,
				struct fscrypt_str *ent_name);
extern void ext4_htree_free_dir_info(struct dir_private_info *p);
extern int ext4_find_dest_de(struct inode *dir, struct inode *inode,
			     struct buffer_head *bh,
			     void *buf, int buf_size,
			     struct ext4_filename *fname,
			     struct ext4_dir_entry_2 **dest_de);
void ext4_insert_dentry(struct inode *inode,
			struct ext4_dir_entry_2 *de,
			int buf_size,
			struct ext4_filename *fname);
static inline void ext4_update_dx_flag(struct inode *inode)
{
	if (!ext4_has_feature_dir_index(inode->i_sb)) {
		/* ext4_iget() should have caught this... */
		WARN_ON_ONCE(ext4_has_feature_metadata_csum(inode->i_sb));
		ext4_clear_inode_flag(inode, EXT4_INODE_INDEX);
	}
}
static const unsigned char ext4_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_FIFO, DT_SOCK, DT_LNK
};

static inline  unsigned char get_dtype(struct super_block *sb, int filetype)
{
	if (!ext4_has_feature_filetype(sb) || filetype >= EXT4_FT_MAX)
		return DT_UNKNOWN;

	return ext4_filetype_table[filetype];
}
extern int ext4_check_all_de(struct inode *dir, struct buffer_head *bh,
			     void *buf, int buf_size);

/* fsync.c */
extern int ext4_sync_file(struct file *, loff_t, loff_t, int);

/* hash.c */
extern int ext4fs_dirhash(const struct inode *dir, const char *name, int len,
			  struct dx_hash_info *hinfo);

/* ialloc.c */
extern struct inode *__ext4_new_inode(handle_t *, struct inode *, umode_t,
				      const struct qstr *qstr, __u32 goal,
				      uid_t *owner, __u32 i_flags,
				      int handle_type, unsigned int line_no,
				      int nblocks);

#define ext4_new_inode(handle, dir, mode, qstr, goal, owner, i_flags) \
	__ext4_new_inode((handle), (dir), (mode), (qstr), (goal), (owner), \
			 i_flags, 0, 0, 0)
#define ext4_new_inode_start_handle(dir, mode, qstr, goal, owner, \
				    type, nblocks)		    \
	__ext4_new_inode(NULL, (dir), (mode), (qstr), (goal), (owner), \
			 0, (type), __LINE__, (nblocks))


extern void ext4_free_inode(handle_t *, struct inode *);
extern struct inode * ext4_orphan_get(struct super_block *, unsigned long);
extern unsigned long ext4_count_free_inodes(struct super_block *);
extern unsigned long ext4_count_dirs(struct super_block *);
extern void ext4_check_inodes_bitmap(struct super_block *);
extern void ext4_mark_bitmap_end(int start_bit, int end_bit, char *bitmap);
extern int ext4_init_inode_table(struct super_block *sb,
				 ext4_group_t group, int barrier);
extern void ext4_end_bitmap_read(struct buffer_head *bh, int uptodate);

/* mballoc.c */
extern const struct seq_operations ext4_mb_seq_groups_ops;
extern long ext4_mb_stats;
extern long ext4_mb_max_to_scan;
extern int ext4_mb_init(struct super_block *);
extern int ext4_mb_release(struct super_block *);
extern ext4_fsblk_t ext4_mb_new_blocks(handle_t *,
				struct ext4_allocation_request *, int *);
extern int ext4_mb_reserve_blocks(struct super_block *, int);
extern void ext4_discard_preallocations(struct inode *);
extern int __init ext4_init_mballoc(void);
extern void ext4_exit_mballoc(void);
extern void ext4_free_blocks(handle_t *handle, struct inode *inode,
			     struct buffer_head *bh, ext4_fsblk_t block,
			     unsigned long count, int flags);
extern int ext4_mb_alloc_groupinfo(struct super_block *sb,
				   ext4_group_t ngroups);
extern int ext4_mb_add_groupinfo(struct super_block *sb,
		ext4_group_t i, struct ext4_group_desc *desc);
extern int ext4_group_add_blocks(handle_t *handle, struct super_block *sb,
				ext4_fsblk_t block, unsigned long count);
extern int ext4_trim_fs(struct super_block *, struct fstrim_range *);
extern void ext4_process_freed_data(struct super_block *sb, tid_t commit_tid);

/* inode.c */
int ext4_inode_is_fast_symlink(struct inode *inode);
struct buffer_head *ext4_getblk(handle_t *, struct inode *, ext4_lblk_t, int);
struct buffer_head *ext4_bread(handle_t *, struct inode *, ext4_lblk_t, int);
int ext4_bread_batch(struct inode *inode, ext4_lblk_t block, int bh_count,
		     bool wait, struct buffer_head **bhs);
int ext4_get_block_unwritten(struct inode *inode, sector_t iblock,
			     struct buffer_head *bh_result, int create);
int ext4_get_block(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create);
int ext4_da_get_block_prep(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int create);
int ext4_walk_page_buffers(handle_t *handle,
			   struct buffer_head *head,
			   unsigned from,
			   unsigned to,
			   int *partial,
			   int (*fn)(handle_t *handle,
				     struct buffer_head *bh));
int do_journal_get_write_access(handle_t *handle,
				struct buffer_head *bh);
#define FALL_BACK_TO_NONDELALLOC 1
#define CONVERT_INLINE_DATA	 2

typedef enum {
	EXT4_IGET_NORMAL =	0,
	EXT4_IGET_SPECIAL =	0x0001, /* OK to iget a system inode */
	EXT4_IGET_HANDLE = 	0x0002	/* Inode # is from a handle */
} ext4_iget_flags;

extern struct inode *__ext4_iget(struct super_block *sb, unsigned long ino,
				 ext4_iget_flags flags, const char *function,
				 unsigned int line);

#define ext4_iget(sb, ino, flags) \
	__ext4_iget((sb), (ino), (flags), __func__, __LINE__)

extern int  ext4_write_inode(struct inode *, struct writeback_control *);
extern int  ext4_setattr(struct dentry *, struct iattr *);
extern int  ext4_getattr(const struct path *, struct kstat *, u32, unsigned int);
extern void ext4_evict_inode(struct inode *);
extern void ext4_clear_inode(struct inode *);
extern int  ext4_file_getattr(const struct path *, struct kstat *, u32, unsigned int);
extern int  ext4_sync_inode(handle_t *, struct inode *);
extern void ext4_dirty_inode(struct inode *, int);
extern int ext4_change_inode_journal_flag(struct inode *, int);
extern int ext4_get_inode_loc(struct inode *, struct ext4_iloc *);
extern int ext4_inode_attach_jinode(struct inode *inode);
extern int ext4_can_truncate(struct inode *inode);
extern int ext4_truncate(struct inode *);
extern int ext4_break_layouts(struct inode *);
extern int ext4_punch_hole(struct inode *inode, loff_t offset, loff_t length);
extern void ext4_set_inode_flags(struct inode *);
extern int ext4_alloc_da_blocks(struct inode *inode);
extern void ext4_set_aops(struct inode *inode);
extern int ext4_writepage_trans_blocks(struct inode *);
extern int ext4_chunk_trans_blocks(struct inode *, int nrblocks);
extern int ext4_zero_partial_blocks(handle_t *handle, struct inode *inode,
			     loff_t lstart, loff_t lend);
extern vm_fault_t ext4_page_mkwrite(struct vm_fault *vmf);
extern vm_fault_t ext4_filemap_fault(struct vm_fault *vmf);
extern qsize_t *ext4_get_reserved_space(struct inode *inode);
extern int ext4_get_projid(struct inode *inode, kprojid_t *projid);
extern void ext4_da_release_space(struct inode *inode, int to_free);
extern void ext4_da_update_reserve_space(struct inode *inode,
					int used, int quota_claim);
extern int ext4_issue_zeroout(struct inode *inode, ext4_lblk_t lblk,
			      ext4_fsblk_t pblk, ext4_lblk_t len);

/* indirect.c */
extern int ext4_ind_map_blocks(handle_t *handle, struct inode *inode,
				struct ext4_map_blocks *map, int flags);
extern int ext4_ind_trans_blocks(struct inode *inode, int nrblocks);
extern void ext4_ind_truncate(handle_t *, struct inode *inode);
extern int ext4_ind_remove_space(handle_t *handle, struct inode *inode,
				 ext4_lblk_t start, ext4_lblk_t end);

/* ioctl.c */
extern long ext4_ioctl(struct file *, unsigned int, unsigned long);
extern long ext4_compat_ioctl(struct file *, unsigned int, unsigned long);

/* migrate.c */
extern int ext4_ext_migrate(struct inode *);
extern int ext4_ind_migrate(struct inode *inode);

/* namei.c */
extern int ext4_dirblock_csum_verify(struct inode *inode,
				     struct buffer_head *bh);
extern int ext4_orphan_add(handle_t *, struct inode *);
extern int ext4_orphan_del(handle_t *, struct inode *);
extern int ext4_htree_fill_tree(struct file *dir_file, __u32 start_hash,
				__u32 start_minor_hash, __u32 *next_hash);
extern int ext4_search_dir(struct buffer_head *bh,
			   char *search_buf,
			   int buf_size,
			   struct inode *dir,
			   struct ext4_filename *fname,
			   unsigned int offset,
			   struct ext4_dir_entry_2 **res_dir);
extern int ext4_generic_delete_entry(handle_t *handle,
				     struct inode *dir,
				     struct ext4_dir_entry_2 *de_del,
				     struct buffer_head *bh,
				     void *entry_buf,
				     int buf_size,
				     int csum_size);
extern bool ext4_empty_dir(struct inode *inode);

/* resize.c */
extern int ext4_group_add(struct super_block *sb,
				struct ext4_new_group_data *input);
extern int ext4_group_extend(struct super_block *sb,
				struct ext4_super_block *es,
				ext4_fsblk_t n_blocks_count);
extern int ext4_resize_fs(struct super_block *sb, ext4_fsblk_t n_blocks_count);

/* super.c */
extern struct buffer_head *ext4_sb_bread(struct super_block *sb,
					 sector_t block, int op_flags);
extern int ext4_seq_options_show(struct seq_file *seq, void *offset);
extern int ext4_calculate_overhead(struct super_block *sb);
extern void ext4_superblock_csum_set(struct super_block *sb);
extern int ext4_alloc_flex_bg_array(struct super_block *sb,
				    ext4_group_t ngroup);
extern const char *ext4_decode_error(struct super_block *sb, int errno,
				     char nbuf[16]);
extern void ext4_mark_group_bitmap_corrupted(struct super_block *sb,
					     ext4_group_t block_group,
					     unsigned int flags);
extern void ext4_set_errno(struct super_block *sb, int err);

extern __printf(4, 5)
void __ext4_error(struct super_block *, const char *, unsigned int,
		  const char *, ...);
extern __printf(5, 6)
void __ext4_error_inode(struct inode *, const char *, unsigned int, ext4_fsblk_t,
		      const char *, ...);
extern __printf(5, 6)
void __ext4_error_file(struct file *, const char *, unsigned int, ext4_fsblk_t,
		     const char *, ...);
extern void __ext4_std_error(struct super_block *, const char *,
			     unsigned int, int);
extern __printf(4, 5)
void __ext4_abort(struct super_block *, const char *, unsigned int,
		  const char *, ...);
extern __printf(4, 5)
void __ext4_warning(struct super_block *, const char *, unsigned int,
		    const char *, ...);
extern __printf(4, 5)
void __ext4_warning_inode(const struct inode *inode, const char *function,
			  unsigned int line, const char *fmt, ...);
extern __printf(3, 4)
void __ext4_msg(struct super_block *, const char *, const char *, ...);
extern void __dump_mmp_msg(struct super_block *, struct mmp_struct *mmp,
			   const char *, unsigned int, const char *);
extern __printf(7, 8)
void __ext4_grp_locked_error(const char *, unsigned int,
			     struct super_block *, ext4_group_t,
			     unsigned long, ext4_fsblk_t,
			     const char *, ...);

#define EXT4_ERROR_INODE(inode, fmt, a...) \
	ext4_error_inode((inode), __func__, __LINE__, 0, (fmt), ## a)

#define EXT4_ERROR_INODE_BLOCK(inode, block, fmt, a...)			\
	ext4_error_inode((inode), __func__, __LINE__, (block), (fmt), ## a)

#define EXT4_ERROR_FILE(file, block, fmt, a...)				\
	ext4_error_file((file), __func__, __LINE__, (block), (fmt), ## a)

#ifdef CONFIG_PRINTK

#define ext4_error_inode(inode, func, line, block, fmt, ...)		\
	__ext4_error_inode(inode, func, line, block, fmt, ##__VA_ARGS__)
#define ext4_error_file(file, func, line, block, fmt, ...)		\
	__ext4_error_file(file, func, line, block, fmt, ##__VA_ARGS__)
#define ext4_error(sb, fmt, ...)					\
	__ext4_error(sb, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define ext4_abort(sb, fmt, ...)					\
	__ext4_abort(sb, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define ext4_warning(sb, fmt, ...)					\
	__ext4_warning(sb, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define ext4_warning_inode(inode, fmt, ...)				\
	__ext4_warning_inode(inode, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define ext4_msg(sb, level, fmt, ...)				\
	__ext4_msg(sb, level, fmt, ##__VA_ARGS__)
#define dump_mmp_msg(sb, mmp, msg)					\
	__dump_mmp_msg(sb, mmp, __func__, __LINE__, msg)
#define ext4_grp_locked_error(sb, grp, ino, block, fmt, ...)		\
	__ext4_grp_locked_error(__func__, __LINE__, sb, grp, ino, block, \
				fmt, ##__VA_ARGS__)

#else

#define ext4_error_inode(inode, func, line, block, fmt, ...)		\
do {									\
	no_printk(fmt, ##__VA_ARGS__);					\
	__ext4_error_inode(inode, "", 0, block, " ");			\
} while (0)
#define ext4_error_file(file, func, line, block, fmt, ...)		\
do {									\
	no_printk(fmt, ##__VA_ARGS__);					\
	__ext4_error_file(file, "", 0, block, " ");			\
} while (0)
#define ext4_error(sb, fmt, ...)					\
do {									\
	no_printk(fmt, ##__VA_ARGS__);					\
	__ext4_error(sb, "", 0, " ");					\
} while (0)
#define ext4_abort(sb, fmt, ...)					\
do {									\
	no_printk(fmt, ##__VA_ARGS__);					\
	__ext4_abort(sb, "", 0, " ");					\
} while (0)
#define ext4_warning(sb, fmt, ...)					\
do {									\
	no_printk(fmt, ##__VA_ARGS__);					\
	__ext4_warning(sb, "", 0, " ");					\
} while (0)
#define ext4_warning_inode(inode, fmt, ...)				\
do {									\
	no_printk(fmt, ##__VA_ARGS__);					\
	__ext4_warning_inode(inode, "", 0, " ");			\
} while (0)
#define ext4_msg(sb, level, fmt, ...)					\
do {									\
	no_printk(fmt, ##__VA_ARGS__);					\
	__ext4_msg(sb, "", " ");					\
} while (0)
#define dump_mmp_msg(sb, mmp, msg)					\
	__dump_mmp_msg(sb, mmp, "", 0, "")
#define ext4_grp_locked_error(sb, grp, ino, block, fmt, ...)		\
do {									\
	no_printk(fmt, ##__VA_ARGS__);				\
	__ext4_grp_locked_error("", 0, sb, grp, ino, block, " ");	\
} while (0)

#endif

extern int ext4_update_compat_feature(handle_t *handle, struct super_block *sb,
					__u32 compat);
extern int ext4_update_rocompat_feature(handle_t *handle,
					struct super_block *sb,	__u32 rocompat);
extern int ext4_update_incompat_feature(handle_t *handle,
					struct super_block *sb,	__u32 incompat);
extern ext4_fsblk_t ext4_block_bitmap(struct super_block *sb,
				      struct ext4_group_desc *bg);
extern ext4_fsblk_t ext4_inode_bitmap(struct super_block *sb,
				      struct ext4_group_desc *bg);
extern ext4_fsblk_t ext4_inode_table(struct super_block *sb,
				     struct ext4_group_desc *bg);
extern __u32 ext4_free_group_clusters(struct super_block *sb,
				      struct ext4_group_desc *bg);
extern __u32 ext4_free_inodes_count(struct super_block *sb,
				 struct ext4_group_desc *bg);
extern __u32 ext4_used_dirs_count(struct super_block *sb,
				struct ext4_group_desc *bg);
extern __u32 ext4_itable_unused_count(struct super_block *sb,
				   struct ext4_group_desc *bg);
extern void ext4_block_bitmap_set(struct super_block *sb,
				  struct ext4_group_desc *bg, ext4_fsblk_t blk);
extern void ext4_inode_bitmap_set(struct super_block *sb,
				  struct ext4_group_desc *bg, ext4_fsblk_t blk);
extern void ext4_inode_table_set(struct super_block *sb,
				 struct ext4_group_desc *bg, ext4_fsblk_t blk);
extern void ext4_free_group_clusters_set(struct super_block *sb,
					 struct ext4_group_desc *bg,
					 __u32 count);
extern void ext4_free_inodes_set(struct super_block *sb,
				struct ext4_group_desc *bg, __u32 count);
extern void ext4_used_dirs_set(struct super_block *sb,
				struct ext4_group_desc *bg, __u32 count);
extern void ext4_itable_unused_set(struct super_block *sb,
				   struct ext4_group_desc *bg, __u32 count);
extern int ext4_group_desc_csum_verify(struct super_block *sb, __u32 group,
				       struct ext4_group_desc *gdp);
extern void ext4_group_desc_csum_set(struct super_block *sb, __u32 group,
				     struct ext4_group_desc *gdp);
extern int ext4_register_li_request(struct super_block *sb,
				    ext4_group_t first_not_zeroed);

static inline int ext4_has_metadata_csum(struct super_block *sb)
{
	WARN_ON_ONCE(ext4_has_feature_metadata_csum(sb) &&
		     !EXT4_SB(sb)->s_chksum_driver);

	return ext4_has_feature_metadata_csum(sb) &&
	       (EXT4_SB(sb)->s_chksum_driver != NULL);
}

static inline int ext4_has_group_desc_csum(struct super_block *sb)
{
	return ext4_has_feature_gdt_csum(sb) || ext4_has_metadata_csum(sb);
}

static inline ext4_fsblk_t ext4_blocks_count(struct ext4_super_block *es)
{
	return ((ext4_fsblk_t)le32_to_cpu(es->s_blocks_count_hi) << 32) |
		le32_to_cpu(es->s_blocks_count_lo);
}

static inline ext4_fsblk_t ext4_r_blocks_count(struct ext4_super_block *es)
{
	return ((ext4_fsblk_t)le32_to_cpu(es->s_r_blocks_count_hi) << 32) |
		le32_to_cpu(es->s_r_blocks_count_lo);
}

static inline ext4_fsblk_t ext4_free_blocks_count(struct ext4_super_block *es)
{
	return ((ext4_fsblk_t)le32_to_cpu(es->s_free_blocks_count_hi) << 32) |
		le32_to_cpu(es->s_free_blocks_count_lo);
}

static inline void ext4_blocks_count_set(struct ext4_super_block *es,
					 ext4_fsblk_t blk)
{
	es->s_blocks_count_lo = cpu_to_le32((u32)blk);
	es->s_blocks_count_hi = cpu_to_le32(blk >> 32);
}

static inline void ext4_free_blocks_count_set(struct ext4_super_block *es,
					      ext4_fsblk_t blk)
{
	es->s_free_blocks_count_lo = cpu_to_le32((u32)blk);
	es->s_free_blocks_count_hi = cpu_to_le32(blk >> 32);
}

static inline void ext4_r_blocks_count_set(struct ext4_super_block *es,
					   ext4_fsblk_t blk)
{
	es->s_r_blocks_count_lo = cpu_to_le32((u32)blk);
	es->s_r_blocks_count_hi = cpu_to_le32(blk >> 32);
}

static inline loff_t ext4_isize(struct super_block *sb,
				struct ext4_inode *raw_inode)
{
	if (ext4_has_feature_largedir(sb) ||
	    S_ISREG(le16_to_cpu(raw_inode->i_mode)))
		return ((loff_t)le32_to_cpu(raw_inode->i_size_high) << 32) |
			le32_to_cpu(raw_inode->i_size_lo);

	return (loff_t) le32_to_cpu(raw_inode->i_size_lo);
}

static inline void ext4_isize_set(struct ext4_inode *raw_inode, loff_t i_size)
{
	raw_inode->i_size_lo = cpu_to_le32(i_size);
	raw_inode->i_size_high = cpu_to_le32(i_size >> 32);
}

static inline
struct ext4_group_info *ext4_get_group_info(struct super_block *sb,
					    ext4_group_t group)
{
	 struct ext4_group_info ***grp_info;
	 long indexv, indexh;
	 BUG_ON(group >= EXT4_SB(sb)->s_groups_count);
	 grp_info = EXT4_SB(sb)->s_group_info;
	 indexv = group >> (EXT4_DESC_PER_BLOCK_BITS(sb));
	 indexh = group & ((EXT4_DESC_PER_BLOCK(sb)) - 1);
	 return grp_info[indexv][indexh];
}

/*
 * Reading s_groups_count requires using smp_rmb() afterwards.  See
 * the locking protocol documented in the comments of ext4_group_add()
 * in resize.c
 */
static inline ext4_group_t ext4_get_groups_count(struct super_block *sb)
{
	ext4_group_t	ngroups = EXT4_SB(sb)->s_groups_count;

	smp_rmb();
	return ngroups;
}

static inline ext4_group_t ext4_flex_group(struct ext4_sb_info *sbi,
					     ext4_group_t block_group)
{
	return block_group >> sbi->s_log_groups_per_flex;
}

static inline unsigned int ext4_flex_bg_size(struct ext4_sb_info *sbi)
{
	return 1 << sbi->s_log_groups_per_flex;
}

#define ext4_std_error(sb, errno)				\
do {								\
	if ((errno))						\
		__ext4_std_error((sb), __func__, __LINE__, (errno));	\
} while (0)

#ifdef CONFIG_SMP
/* Each CPU can accumulate percpu_counter_batch clusters in their local
 * counters. So we need to make sure we have free clusters more
 * than percpu_counter_batch  * nr_cpu_ids. Also add a window of 4 times.
 */
#define EXT4_FREECLUSTERS_WATERMARK (4 * (percpu_counter_batch * nr_cpu_ids))
#else
#define EXT4_FREECLUSTERS_WATERMARK 0
#endif

/* Update i_disksize. Requires i_mutex to avoid races with truncate */
static inline void ext4_update_i_disksize(struct inode *inode, loff_t newsize)
{
	WARN_ON_ONCE(S_ISREG(inode->i_mode) &&
		     !inode_is_locked(inode));
	down_write(&EXT4_I(inode)->i_data_sem);
	if (newsize > EXT4_I(inode)->i_disksize)
		EXT4_I(inode)->i_disksize = newsize;
	up_write(&EXT4_I(inode)->i_data_sem);
}

/* Update i_size, i_disksize. Requires i_mutex to avoid races with truncate */
static inline int ext4_update_inode_size(struct inode *inode, loff_t newsize)
{
	int changed = 0;

	if (newsize > inode->i_size) {
		i_size_write(inode, newsize);
		changed = 1;
	}
	if (newsize > EXT4_I(inode)->i_disksize) {
		ext4_update_i_disksize(inode, newsize);
		changed |= 2;
	}
	return changed;
}

int ext4_update_disksize_before_punch(struct inode *inode, loff_t offset,
				      loff_t len);

struct ext4_group_info {
	unsigned long   bb_state;
	struct rb_root  bb_free_root;
	ext4_grpblk_t	bb_first_free;	/* first free block */
	ext4_grpblk_t	bb_free;	/* total free blocks */
	ext4_grpblk_t	bb_fragments;	/* nr of freespace fragments */
	ext4_grpblk_t	bb_largest_free_order;/* order of largest frag in BG */
	struct          list_head bb_prealloc_list;
#ifdef DOUBLE_CHECK
	void            *bb_bitmap;
#endif
	struct rw_semaphore alloc_sem;
	ext4_grpblk_t	bb_counters[];	/* Nr of free power-of-two-block
					 * regions, index is order.
					 * bb_counters[3] = 5 means
					 * 5 free 8-block regions. */
};

#define EXT4_GROUP_INFO_NEED_INIT_BIT		0
#define EXT4_GROUP_INFO_WAS_TRIMMED_BIT		1
#define EXT4_GROUP_INFO_BBITMAP_CORRUPT_BIT	2
#define EXT4_GROUP_INFO_IBITMAP_CORRUPT_BIT	3
#define EXT4_GROUP_INFO_BBITMAP_CORRUPT		\
	(1 << EXT4_GROUP_INFO_BBITMAP_CORRUPT_BIT)
#define EXT4_GROUP_INFO_IBITMAP_CORRUPT		\
	(1 << EXT4_GROUP_INFO_IBITMAP_CORRUPT_BIT)

#define EXT4_MB_GRP_NEED_INIT(grp)	\
	(test_bit(EXT4_GROUP_INFO_NEED_INIT_BIT, &((grp)->bb_state)))
#define EXT4_MB_GRP_BBITMAP_CORRUPT(grp)	\
	(test_bit(EXT4_GROUP_INFO_BBITMAP_CORRUPT_BIT, &((grp)->bb_state)))
#define EXT4_MB_GRP_IBITMAP_CORRUPT(grp)	\
	(test_bit(EXT4_GROUP_INFO_IBITMAP_CORRUPT_BIT, &((grp)->bb_state)))

#define EXT4_MB_GRP_WAS_TRIMMED(grp)	\
	(test_bit(EXT4_GROUP_INFO_WAS_TRIMMED_BIT, &((grp)->bb_state)))
#define EXT4_MB_GRP_SET_TRIMMED(grp)	\
	(set_bit(EXT4_GROUP_INFO_WAS_TRIMMED_BIT, &((grp)->bb_state)))
#define EXT4_MB_GRP_CLEAR_TRIMMED(grp)	\
	(clear_bit(EXT4_GROUP_INFO_WAS_TRIMMED_BIT, &((grp)->bb_state)))

#define EXT4_MAX_CONTENTION		8
#define EXT4_CONTENTION_THRESHOLD	2

static inline spinlock_t *ext4_group_lock_ptr(struct super_block *sb,
					      ext4_group_t group)
{
	return bgl_lock_ptr(EXT4_SB(sb)->s_blockgroup_lock, group);
}

/*
 * Returns true if the filesystem is busy enough that attempts to
 * access the block group locks has run into contention.
 */
static inline int ext4_fs_is_busy(struct ext4_sb_info *sbi)
{
	return (atomic_read(&sbi->s_lock_busy) > EXT4_CONTENTION_THRESHOLD);
}

static inline void ext4_lock_group(struct super_block *sb, ext4_group_t group)
{
	spinlock_t *lock = ext4_group_lock_ptr(sb, group);
	if (spin_trylock(lock))
		/*
		 * We're able to grab the lock right away, so drop the
		 * lock contention counter.
		 */
		atomic_add_unless(&EXT4_SB(sb)->s_lock_busy, -1, 0);
	else {
		/*
		 * The lock is busy, so bump the contention counter,
		 * and then wait on the spin lock.
		 */
		atomic_add_unless(&EXT4_SB(sb)->s_lock_busy, 1,
				  EXT4_MAX_CONTENTION);
		spin_lock(lock);
	}
}

static inline void ext4_unlock_group(struct super_block *sb,
					ext4_group_t group)
{
	spin_unlock(ext4_group_lock_ptr(sb, group));
}

/*
 * Block validity checking
 */
#define ext4_check_indirect_blockref(inode, bh)				\
	ext4_check_blockref(__func__, __LINE__, inode,			\
			    (__le32 *)(bh)->b_data,			\
			    EXT4_ADDR_PER_BLOCK((inode)->i_sb))

#define ext4_ind_check_inode(inode)					\
	ext4_check_blockref(__func__, __LINE__, inode,			\
			    EXT4_I(inode)->i_data,			\
			    EXT4_NDIR_BLOCKS)

/*
 * Inodes and files operations
 */

/* dir.c */
extern const struct file_operations ext4_dir_operations;

#ifdef CONFIG_UNICODE
extern const struct dentry_operations ext4_dentry_ops;
#endif

/* file.c */
extern const struct inode_operations ext4_file_inode_operations;
extern const struct file_operations ext4_file_operations;
extern loff_t ext4_llseek(struct file *file, loff_t offset, int origin);

/* inline.c */
extern int ext4_get_max_inline_size(struct inode *inode);
extern int ext4_find_inline_data_nolock(struct inode *inode);
extern int ext4_init_inline_data(handle_t *handle, struct inode *inode,
				 unsigned int len);
extern int ext4_destroy_inline_data(handle_t *handle, struct inode *inode);

extern int ext4_readpage_inline(struct inode *inode, struct page *page);
extern int ext4_try_to_write_inline_data(struct address_space *mapping,
					 struct inode *inode,
					 loff_t pos, unsigned len,
					 unsigned flags,
					 struct page **pagep);
extern int ext4_write_inline_data_end(struct inode *inode,
				      loff_t pos, unsigned len,
				      unsigned copied,
				      struct page *page);
extern struct buffer_head *
ext4_journalled_write_inline_data(struct inode *inode,
				  unsigned len,
				  struct page *page);
extern int ext4_da_write_inline_data_begin(struct address_space *mapping,
					   struct inode *inode,
					   loff_t pos, unsigned len,
					   unsigned flags,
					   struct page **pagep,
					   void **fsdata);
extern int ext4_da_write_inline_data_end(struct inode *inode, loff_t pos,
					 unsigned len, unsigned copied,
					 struct page *page);
extern int ext4_try_add_inline_entry(handle_t *handle,
				     struct ext4_filename *fname,
				     struct inode *dir, struct inode *inode);
extern int ext4_try_create_inline_dir(handle_t *handle,
				      struct inode *parent,
				      struct inode *inode);
extern int ext4_read_inline_dir(struct file *filp,
				struct dir_context *ctx,
				int *has_inline_data);
extern int ext4_inlinedir_to_tree(struct file *dir_file,
				  struct inode *dir, ext4_lblk_t block,
				  struct dx_hash_info *hinfo,
				  __u32 start_hash, __u32 start_minor_hash,
				  int *has_inline_data);
extern struct buffer_head *ext4_find_inline_entry(struct inode *dir,
					struct ext4_filename *fname,
					struct ext4_dir_entry_2 **res_dir,
					int *has_inline_data);
extern int ext4_delete_inline_entry(handle_t *handle,
				    struct inode *dir,
				    struct ext4_dir_entry_2 *de_del,
				    struct buffer_head *bh,
				    int *has_inline_data);
extern bool empty_inline_dir(struct inode *dir, int *has_inline_data);
extern struct buffer_head *ext4_get_first_inline_block(struct inode *inode,
					struct ext4_dir_entry_2 **parent_de,
					int *retval);
extern int ext4_inline_data_fiemap(struct inode *inode,
				   struct fiemap_extent_info *fieinfo,
				   int *has_inline, __u64 start, __u64 len);

struct iomap;
extern int ext4_inline_data_iomap(struct inode *inode, struct iomap *iomap);

extern int ext4_inline_data_truncate(struct inode *inode, int *has_inline);

extern int ext4_convert_inline_data(struct inode *inode);

static inline int ext4_has_inline_data(struct inode *inode)
{
	return ext4_test_inode_flag(inode, EXT4_INODE_INLINE_DATA) &&
	       EXT4_I(inode)->i_inline_off;
}

/* namei.c */
extern const struct inode_operations ext4_dir_inode_operations;
extern const struct inode_operations ext4_special_inode_operations;
extern struct dentry *ext4_get_parent(struct dentry *child);
extern struct ext4_dir_entry_2 *ext4_init_dot_dotdot(struct inode *inode,
				 struct ext4_dir_entry_2 *de,
				 int blocksize, int csum_size,
				 unsigned int parent_ino, int dotdot_real_len);
extern void ext4_initialize_dirent_tail(struct buffer_head *bh,
					unsigned int blocksize);
extern int ext4_handle_dirty_dirblock(handle_t *handle, struct inode *inode,
				      struct buffer_head *bh);
extern int ext4_ci_compare(const struct inode *parent,
			   const struct qstr *fname,
			   const struct qstr *entry, bool quick);

#define S_SHIFT 12
static const unsigned char ext4_type_by_mode[(S_IFMT >> S_SHIFT) + 1] = {
	[S_IFREG >> S_SHIFT]	= EXT4_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= EXT4_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= EXT4_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= EXT4_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= EXT4_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= EXT4_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= EXT4_FT_SYMLINK,
};

static inline void ext4_set_de_type(struct super_block *sb,
				struct ext4_dir_entry_2 *de,
				umode_t mode) {
	if (ext4_has_feature_filetype(sb))
		de->file_type = ext4_type_by_mode[(mode & S_IFMT)>>S_SHIFT];
}

/* readpages.c */
extern int ext4_mpage_readpages(struct address_space *mapping,
				struct list_head *pages, struct page *page,
				unsigned nr_pages, bool is_readahead);
extern int __init ext4_init_post_read_processing(void);
extern void ext4_exit_post_read_processing(void);

/* symlink.c */
extern const struct inode_operations ext4_encrypted_symlink_inode_operations;
extern const struct inode_operations ext4_symlink_inode_operations;
extern const struct inode_operations ext4_fast_symlink_inode_operations;

/* sysfs.c */
extern int ext4_register_sysfs(struct super_block *sb);
extern void ext4_unregister_sysfs(struct super_block *sb);
extern int __init ext4_init_sysfs(void);
extern void ext4_exit_sysfs(void);

/* block_validity */
extern void ext4_release_system_zone(struct super_block *sb);
extern int ext4_setup_system_zone(struct super_block *sb);
extern int __init ext4_init_system_zone(void);
extern void ext4_exit_system_zone(void);
extern int ext4_data_block_valid(struct ext4_sb_info *sbi,
				 ext4_fsblk_t start_blk,
				 unsigned int count);
extern int ext4_check_blockref(const char *, unsigned int,
			       struct inode *, __le32 *, unsigned int);

/* extents.c */
struct ext4_ext_path;
struct ext4_extent;

/*
 * Maximum number of logical blocks in a file; ext4_extent's ee_block is
 * __le32.
 */
#define EXT_MAX_BLOCKS	0xffffffff

extern int ext4_ext_tree_init(handle_t *handle, struct inode *);
extern int ext4_ext_index_trans_blocks(struct inode *inode, int extents);
extern int ext4_ext_map_blocks(handle_t *handle, struct inode *inode,
			       struct ext4_map_blocks *map, int flags);
extern int ext4_ext_truncate(handle_t *, struct inode *);
extern int ext4_ext_remove_space(struct inode *inode, ext4_lblk_t start,
				 ext4_lblk_t end);
extern void ext4_ext_init(struct super_block *);
extern void ext4_ext_release(struct super_block *);
extern long ext4_fallocate(struct file *file, int mode, loff_t offset,
			  loff_t len);
extern int ext4_convert_unwritten_extents(handle_t *handle, struct inode *inode,
					  loff_t offset, ssize_t len);
extern int ext4_convert_unwritten_io_end_vec(handle_t *handle,
					     ext4_io_end_t *io_end);
extern int ext4_map_blocks(handle_t *handle, struct inode *inode,
			   struct ext4_map_blocks *map, int flags);
extern int ext4_ext_calc_credits_for_single_extent(struct inode *inode,
						   int num,
						   struct ext4_ext_path *path);
extern int ext4_ext_insert_extent(handle_t *, struct inode *,
				  struct ext4_ext_path **,
				  struct ext4_extent *, int);
extern struct ext4_ext_path *ext4_find_extent(struct inode *, ext4_lblk_t,
					      struct ext4_ext_path **,
					      int flags);
extern void ext4_ext_drop_refs(struct ext4_ext_path *);
extern int ext4_ext_check_inode(struct inode *inode);
extern ext4_lblk_t ext4_ext_next_allocated_block(struct ext4_ext_path *path);
extern int ext4_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
			__u64 start, __u64 len);
extern int ext4_get_es_cache(struct inode *inode,
			     struct fiemap_extent_info *fieinfo,
			     __u64 start, __u64 len);
extern int ext4_ext_precache(struct inode *inode);
extern int ext4_swap_extents(handle_t *handle, struct inode *inode1,
				struct inode *inode2, ext4_lblk_t lblk1,
			     ext4_lblk_t lblk2,  ext4_lblk_t count,
			     int mark_unwritten,int *err);
extern int ext4_clu_mapped(struct inode *inode, ext4_lblk_t lclu);
extern int ext4_datasem_ensure_credits(handle_t *handle, struct inode *inode,
				       int check_cred, int restart_cred,
				       int revoke_cred);


/* move_extent.c */
extern void ext4_double_down_write_data_sem(struct inode *first,
					    struct inode *second);
extern void ext4_double_up_write_data_sem(struct inode *orig_inode,
					  struct inode *donor_inode);
extern int ext4_move_extents(struct file *o_filp, struct file *d_filp,
			     __u64 start_orig, __u64 start_donor,
			     __u64 len, __u64 *moved_len);

/* page-io.c */
extern int __init ext4_init_pageio(void);
extern void ext4_exit_pageio(void);
extern ext4_io_end_t *ext4_init_io_end(struct inode *inode, gfp_t flags);
extern ext4_io_end_t *ext4_get_io_end(ext4_io_end_t *io_end);
extern int ext4_put_io_end(ext4_io_end_t *io_end);
extern void ext4_put_io_end_defer(ext4_io_end_t *io_end);
extern void ext4_io_submit_init(struct ext4_io_submit *io,
				struct writeback_control *wbc);
extern void ext4_end_io_rsv_work(struct work_struct *work);
extern void ext4_io_submit(struct ext4_io_submit *io);
extern int ext4_bio_write_page(struct ext4_io_submit *io,
			       struct page *page,
			       int len,
			       struct writeback_control *wbc,
			       bool keep_towrite);
extern struct ext4_io_end_vec *ext4_alloc_io_end_vec(ext4_io_end_t *io_end);
extern struct ext4_io_end_vec *ext4_last_io_end_vec(ext4_io_end_t *io_end);

/* mmp.c */
extern int ext4_multi_mount_protect(struct super_block *, ext4_fsblk_t);

/* verity.c */
extern const struct fsverity_operations ext4_verityops;

/*
 * Add new method to test whether block and inode bitmaps are properly
 * initialized. With uninit_bg reading the block from disk is not enough
 * to mark the bitmap uptodate. We need to also zero-out the bitmap
 */
#define BH_BITMAP_UPTODATE BH_JBDPrivateStart

static inline int bitmap_uptodate(struct buffer_head *bh)
{
	return (buffer_uptodate(bh) &&
			test_bit(BH_BITMAP_UPTODATE, &(bh)->b_state));
}
static inline void set_bitmap_uptodate(struct buffer_head *bh)
{
	set_bit(BH_BITMAP_UPTODATE, &(bh)->b_state);
}

#define in_range(b, first, len)	((b) >= (first) && (b) <= (first) + (len) - 1)

/* For ioend & aio unwritten conversion wait queues */
#define EXT4_WQ_HASH_SZ		37
#define ext4_ioend_wq(v)   (&ext4__ioend_wq[((unsigned long)(v)) %\
					    EXT4_WQ_HASH_SZ])
extern wait_queue_head_t ext4__ioend_wq[EXT4_WQ_HASH_SZ];

extern int ext4_resize_begin(struct super_block *sb);
extern void ext4_resize_end(struct super_block *sb);

static inline void ext4_set_io_unwritten_flag(struct inode *inode,
					      struct ext4_io_end *io_end)
{
	if (!(io_end->flag & EXT4_IO_END_UNWRITTEN)) {
		io_end->flag |= EXT4_IO_END_UNWRITTEN;
		atomic_inc(&EXT4_I(inode)->i_unwritten);
	}
}

static inline void ext4_clear_io_unwritten_flag(ext4_io_end_t *io_end)
{
	struct inode *inode = io_end->inode;

	if (io_end->flag & EXT4_IO_END_UNWRITTEN) {
		io_end->flag &= ~EXT4_IO_END_UNWRITTEN;
		/* Wake up anyone waiting on unwritten extent conversion */
		if (atomic_dec_and_test(&EXT4_I(inode)->i_unwritten))
			wake_up_all(ext4_ioend_wq(inode));
	}
}

extern const struct iomap_ops ext4_iomap_ops;
extern const struct iomap_ops ext4_iomap_overwrite_ops;
extern const struct iomap_ops ext4_iomap_report_ops;

static inline int ext4_buffer_uptodate(struct buffer_head *bh)
{
	/*
	 * If the buffer has the write error flag, we have failed
	 * to write out data in the block.  In this  case, we don't
	 * have to read the block because we may read the old data
	 * successfully.
	 */
	if (!buffer_uptodate(bh) && buffer_write_io_error(bh))
		set_buffer_uptodate(bh);
	return buffer_uptodate(bh);
}

#endif	/* __KERNEL__ */

#define EFSBADCRC	EBADMSG		/* Bad CRC detected */
#define EFSCORRUPTED	EUCLEAN		/* Filesystem is corrupted */

#endif	/* _EXT4_H */

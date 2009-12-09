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
#include <linux/wait.h>
#include <linux/blockgroup_lock.h>
#include <linux/percpu_counter.h>

/*
 * The fourth extended filesystem constants/structures
 */

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
#define ext4_debug(f, a...)	do {} while (0)
#endif

/* data type for block offset of block group */
typedef int ext4_grpblk_t;

/* data type for filesystem-wide blocks number */
typedef unsigned long long ext4_fsblk_t;

/* data type for file logical block number */
typedef __u32 ext4_lblk_t;

/* data type for block group number */
typedef unsigned int ext4_group_t;

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
 * For delayed allocation tracking
 */
struct mpage_da_data {
	struct inode *inode;
	sector_t b_blocknr;		/* start block number of extent */
	size_t b_size;			/* size of extent */
	unsigned long b_state;		/* state of the extent */
	unsigned long first_page, next_page;	/* extent of pages */
	struct writeback_control *wbc;
	int io_done;
	int pages_written;
	int retval;
};
#define	DIO_AIO_UNWRITTEN	0x1
typedef struct ext4_io_end {
	struct list_head	list;		/* per-file finished AIO list */
	struct inode		*inode;		/* file being written to */
	unsigned int		flag;		/* unwritten or not */
	int			error;		/* I/O error code */
	ext4_lblk_t		offset;		/* offset in the file */
	size_t			size;		/* size of the extent */
	struct work_struct	work;		/* data work queue */
} ext4_io_end_t;

/*
 * Special inodes numbers
 */
#define	EXT4_BAD_INO		 1	/* Bad blocks inode */
#define EXT4_ROOT_INO		 2	/* Root inode */
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
#ifdef __KERNEL__
# define EXT4_BLOCK_SIZE(s)		((s)->s_blocksize)
#else
# define EXT4_BLOCK_SIZE(s)		(EXT4_MIN_BLOCK_SIZE << (s)->s_log_block_size)
#endif
#define	EXT4_ADDR_PER_BLOCK(s)		(EXT4_BLOCK_SIZE(s) / sizeof(__u32))
#ifdef __KERNEL__
# define EXT4_BLOCK_SIZE_BITS(s)	((s)->s_blocksize_bits)
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
	__u32	bg_reserved[2];		/* Likely block/inode bitmap checksum */
	__le16  bg_itable_unused_lo;	/* Unused inodes count */
	__le16  bg_checksum;		/* crc16(sb_uuid+group+desc) */
	__le32	bg_block_bitmap_hi;	/* Blocks bitmap block MSB */
	__le32	bg_inode_bitmap_hi;	/* Inodes bitmap block MSB */
	__le32	bg_inode_table_hi;	/* Inodes table block MSB */
	__le16	bg_free_blocks_count_hi;/* Free blocks count MSB */
	__le16	bg_free_inodes_count_hi;/* Free inodes count MSB */
	__le16	bg_used_dirs_count_hi;	/* Directories count MSB */
	__le16  bg_itable_unused_hi;    /* Unused inodes count MSB */
	__u32	bg_reserved2[3];
};

/*
 * Structure of a flex block group info
 */

struct flex_groups {
	atomic_t free_inodes;
	atomic_t free_blocks;
	atomic_t used_dirs;
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
#define EXT4_ECOMPR_FL			0x00000800 /* Compression error */
/* End compression flags --- maybe not all used */
#define EXT4_INDEX_FL			0x00001000 /* hash-indexed directory */
#define EXT4_IMAGIC_FL			0x00002000 /* AFS directory */
#define EXT4_JOURNAL_DATA_FL		0x00004000 /* file data should be journaled */
#define EXT4_NOTAIL_FL			0x00008000 /* file tail should not be merged */
#define EXT4_DIRSYNC_FL			0x00010000 /* dirsync behaviour (directories only) */
#define EXT4_TOPDIR_FL			0x00020000 /* Top of directory hierarchies*/
#define EXT4_HUGE_FILE_FL               0x00040000 /* Set to each huge file */
#define EXT4_EXTENTS_FL			0x00080000 /* Inode uses extents */
#define EXT4_RESERVED_FL		0x80000000 /* reserved for ext4 lib */

#define EXT4_FL_USER_VISIBLE		0x000BDFFF /* User visible flags */
#define EXT4_FL_USER_MODIFIABLE		0x000B80FF /* User modifiable flags */

/* Flags that should be inherited by new inodes from their parent. */
#define EXT4_FL_INHERITED (EXT4_SECRM_FL | EXT4_UNRM_FL | EXT4_COMPR_FL |\
			   EXT4_SYNC_FL | EXT4_IMMUTABLE_FL | EXT4_APPEND_FL |\
			   EXT4_NODUMP_FL | EXT4_NOATIME_FL |\
			   EXT4_NOCOMPR_FL | EXT4_JOURNAL_DATA_FL |\
			   EXT4_NOTAIL_FL | EXT4_DIRSYNC_FL)

/* Flags that are appropriate for regular files (all but dir-specific ones). */
#define EXT4_REG_FLMASK (~(EXT4_DIRSYNC_FL | EXT4_TOPDIR_FL))

/* Flags that are appropriate for non-directories/regular files. */
#define EXT4_OTHER_FLMASK (EXT4_NODUMP_FL | EXT4_NOATIME_FL)

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
 * Inode dynamic state flags
 */
#define EXT4_STATE_JDATA		0x00000001 /* journaled data exists */
#define EXT4_STATE_NEW			0x00000002 /* inode is newly created */
#define EXT4_STATE_XATTR		0x00000004 /* has in-inode xattrs */
#define EXT4_STATE_NO_EXPAND		0x00000008 /* No space for expansion */
#define EXT4_STATE_DA_ALLOC_CLOSE	0x00000010 /* Alloc DA blks on close */
#define EXT4_STATE_EXT_MIGRATE		0x00000020 /* Inode is migrating */
#define EXT4_STATE_DIO_UNWRITTEN	0x00000040 /* need convert on dio done*/

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

/* The struct ext4_new_group_input in kernel space, with free_blocks_count */
struct ext4_new_group_data {
	__u32 group;
	__u64 block_bitmap;
	__u64 inode_bitmap;
	__u64 inode_table;
	__u32 blocks_count;
	__u16 reserved_blocks;
	__u16 unused;
	__u32 free_blocks_count;
};

/*
 * Flags used by ext4_get_blocks()
 */
	/* Allocate any needed blocks and/or convert an unitialized
	   extent to be an initialized ext4 */
#define EXT4_GET_BLOCKS_CREATE			0x0001
	/* Request the creation of an unitialized extent */
#define EXT4_GET_BLOCKS_UNINIT_EXT		0x0002
#define EXT4_GET_BLOCKS_CREATE_UNINIT_EXT	(EXT4_GET_BLOCKS_UNINIT_EXT|\
						 EXT4_GET_BLOCKS_CREATE)
	/* Caller is from the delayed allocation writeout path,
	   so set the magic i_delalloc_reserve_flag after taking the 
	   inode allocation semaphore for */
#define EXT4_GET_BLOCKS_DELALLOC_RESERVE	0x0004
	/* Call ext4_da_update_reserve_space() after successfully 
	   allocating the blocks */
#define EXT4_GET_BLOCKS_UPDATE_RESERVE_SPACE	0x0008
	/* caller is from the direct IO path, request to creation of an
	unitialized extents if not allocated, split the uninitialized
	extent if blocks has been preallocated already*/
#define EXT4_GET_BLOCKS_DIO			0x0010
#define EXT4_GET_BLOCKS_CONVERT			0x0020
#define EXT4_GET_BLOCKS_DIO_CREATE_EXT		(EXT4_GET_BLOCKS_DIO|\
					 EXT4_GET_BLOCKS_CREATE_UNINIT_EXT)
	/* Convert extent to initialized after direct IO complete */
#define EXT4_GET_BLOCKS_DIO_CONVERT_EXT		(EXT4_GET_BLOCKS_CONVERT|\
					 EXT4_GET_BLOCKS_DIO_CREATE_EXT)

/*
 * ioctl commands
 */
#define	EXT4_IOC_GETFLAGS		FS_IOC_GETFLAGS
#define	EXT4_IOC_SETFLAGS		FS_IOC_SETFLAGS
#define	EXT4_IOC_GETVERSION		_IOR('f', 3, long)
#define	EXT4_IOC_SETVERSION		_IOW('f', 4, long)
#define	EXT4_IOC_GETVERSION_OLD		FS_IOC_GETVERSION
#define	EXT4_IOC_SETVERSION_OLD		FS_IOC_SETVERSION
#ifdef CONFIG_JBD2_DEBUG
#define EXT4_IOC_WAIT_FOR_READONLY	_IOR('f', 99, long)
#endif
#define EXT4_IOC_GETRSVSZ		_IOR('f', 5, long)
#define EXT4_IOC_SETRSVSZ		_IOW('f', 6, long)
#define EXT4_IOC_GROUP_EXTEND		_IOW('f', 7, unsigned long)
#define EXT4_IOC_GROUP_ADD		_IOW('f', 8, struct ext4_new_group_input)
#define EXT4_IOC_MIGRATE		_IO('f', 9)
 /* note ioctl 10 reserved for an early version of the FIEMAP ioctl */
 /* note ioctl 11 reserved for filesystem-independent FIEMAP ioctl */
#define EXT4_IOC_ALLOC_DA_BLKS		_IO('f', 12)
#define EXT4_IOC_MOVE_EXT		_IOWR('f', 15, struct move_extent)

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
#ifdef CONFIG_JBD2_DEBUG
#define EXT4_IOC32_WAIT_FOR_READONLY	_IOR('f', 99, int)
#endif
#define EXT4_IOC32_GETVERSION_OLD	FS_IOC32_GETVERSION
#define EXT4_IOC32_SETVERSION_OLD	FS_IOC32_SETVERSION


/*
 *  Mount options
 */
struct ext4_mount_options {
	unsigned long s_mount_opt;
	uid_t s_resuid;
	gid_t s_resgid;
	unsigned long s_commit_interval;
	u32 s_min_batch_time, s_max_batch_time;
#ifdef CONFIG_QUOTA
	int s_jquota_fmt;
	char *s_qf_names[MAXQUOTAS];
#endif
};

/* Max physical block we can addres w/o extents */
#define EXT4_MAX_BLOCK_FILE_PHYS	0xFFFFFFFF

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
			__u32	l_i_reserved2;
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
	__le16	i_pad1;
	__le32  i_ctime_extra;  /* extra Change time      (nsec << 2 | epoch) */
	__le32  i_mtime_extra;  /* extra Modification time(nsec << 2 | epoch) */
	__le32  i_atime_extra;  /* extra Access time      (nsec << 2 | epoch) */
	__le32  i_crtime;       /* File Creation time */
	__le32  i_crtime_extra; /* extra FileCreationtime (nsec << 2 | epoch) */
	__le32  i_version_hi;	/* high 32 bits for 64-bit version */
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

static inline __le32 ext4_encode_extra_time(struct timespec *time)
{
       return cpu_to_le32((sizeof(time->tv_sec) > 4 ?
			   (time->tv_sec >> 32) & EXT4_EPOCH_MASK : 0) |
                          ((time->tv_nsec << EXT4_EPOCH_BITS) & EXT4_NSEC_MASK));
}

static inline void ext4_decode_extra_time(struct timespec *time, __le32 extra)
{
       if (sizeof(time->tv_sec) > 4)
	       time->tv_sec |= (__u64)(le32_to_cpu(extra) & EXT4_EPOCH_MASK)
			       << 32;
       time->tv_nsec = (le32_to_cpu(extra) & EXT4_NSEC_MASK) >> EXT4_EPOCH_BITS;
}

#define EXT4_INODE_SET_XTIME(xtime, inode, raw_inode)			       \
do {									       \
	(raw_inode)->xtime = cpu_to_le32((inode)->xtime.tv_sec);	       \
	if (EXT4_FITS_IN_INODE(raw_inode, EXT4_I(inode), xtime ## _extra))     \
		(raw_inode)->xtime ## _extra =				       \
				ext4_encode_extra_time(&(inode)->xtime);       \
} while (0)

#define EXT4_EINODE_SET_XTIME(xtime, einode, raw_inode)			       \
do {									       \
	if (EXT4_FITS_IN_INODE(raw_inode, einode, xtime))		       \
		(raw_inode)->xtime = cpu_to_le32((einode)->xtime.tv_sec);      \
	if (EXT4_FITS_IN_INODE(raw_inode, einode, xtime ## _extra))	       \
		(raw_inode)->xtime ## _extra =				       \
				ext4_encode_extra_time(&(einode)->xtime);      \
} while (0)

#define EXT4_INODE_GET_XTIME(xtime, inode, raw_inode)			       \
do {									       \
	(inode)->xtime.tv_sec = (signed)le32_to_cpu((raw_inode)->xtime);       \
	if (EXT4_FITS_IN_INODE(raw_inode, EXT4_I(inode), xtime ## _extra))     \
		ext4_decode_extra_time(&(inode)->xtime,			       \
				       raw_inode->xtime ## _extra);	       \
} while (0)

#define EXT4_EINODE_GET_XTIME(xtime, einode, raw_inode)			       \
do {									       \
	if (EXT4_FITS_IN_INODE(raw_inode, einode, xtime))		       \
		(einode)->xtime.tv_sec = 				       \
			(signed)le32_to_cpu((raw_inode)->xtime);	       \
	if (EXT4_FITS_IN_INODE(raw_inode, einode, xtime ## _extra))	       \
		ext4_decode_extra_time(&(einode)->xtime,		       \
				       raw_inode->xtime ## _extra);	       \
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
#define i_reserved2	osd2.linux2.l_i_reserved2

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

/*
 * storage for cached extent
 */
struct ext4_ext_cache {
	ext4_fsblk_t	ec_start;
	ext4_lblk_t	ec_block;
	__u32		ec_len; /* must be 32bit to return holes */
	__u32		ec_type;
};

/*
 * fourth extended file system inode data in memory
 */
struct ext4_inode_info {
	__le32	i_data[15];	/* unconverted */
	__u32	i_flags;
	ext4_fsblk_t	i_file_acl;
	__u32	i_dtime;

	/*
	 * i_block_group is the number of the block group which contains
	 * this file's inode.  Constant across the lifetime of the inode,
	 * it is ued for making block allocation decisions - we try to
	 * place a file's data blocks near its inode block, and new inodes
	 * near to their parent directory's inode.
	 */
	ext4_group_t	i_block_group;
	__u32	i_state;		/* Dynamic state flags for ext4 */

	ext4_lblk_t		i_dir_start_lookup;
#ifdef CONFIG_EXT4_FS_XATTR
	/*
	 * Extended attributes can be read independently of the main file
	 * data. Taking i_mutex even when reading would cause contention
	 * between readers of EAs and writers of regular file data, so
	 * instead we synchronize on xattr_sem when reading or changing
	 * EAs.
	 */
	struct rw_semaphore xattr_sem;
#endif

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
	struct inode vfs_inode;
	struct jbd2_inode jinode;

	struct ext4_ext_cache i_cached_extent;
	/*
	 * File creation time. Its function is same as that of
	 * struct timespec i_{a,c,m}time in the generic inode.
	 */
	struct timespec i_crtime;

	/* mballoc */
	struct list_head i_prealloc_list;
	spinlock_t i_prealloc_lock;

	/* ialloc */
	ext4_group_t	i_last_alloc_group;

	/* allocation reservation info for delalloc */
	unsigned int i_reserved_data_blocks;
	unsigned int i_reserved_meta_blocks;
	unsigned int i_allocated_meta_blocks;
	unsigned short i_delalloc_reserved_flag;

	/* on-disk additional length */
	__u16 i_extra_isize;

	spinlock_t i_block_reservation_lock;

	/* completed async DIOs that might need unwritten extents handling */
	struct list_head i_aio_dio_complete_list;
	/* current io_end structure for async DIO write*/
	ext4_io_end_t *cur_aio_dio;

	/*
	 * Transactions that contain inode's metadata needed to complete
	 * fsync and fdatasync, respectively.
	 */
	tid_t i_sync_tid;
	tid_t i_datasync_tid;
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
 * Mount flags
 */
#define EXT4_MOUNT_OLDALLOC		0x00002  /* Don't use the new Orlov allocator */
#define EXT4_MOUNT_GRPID		0x00004	/* Create files with directory's group */
#define EXT4_MOUNT_DEBUG		0x00008	/* Some debugging messages */
#define EXT4_MOUNT_ERRORS_CONT		0x00010	/* Continue on errors */
#define EXT4_MOUNT_ERRORS_RO		0x00020	/* Remount fs ro on errors */
#define EXT4_MOUNT_ERRORS_PANIC		0x00040	/* Panic on errors */
#define EXT4_MOUNT_MINIX_DF		0x00080	/* Mimics the Minix statfs */
#define EXT4_MOUNT_NOLOAD		0x00100	/* Don't use existing journal*/
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
#define EXT4_MOUNT_NOBH			0x40000 /* No bufferheads */
#define EXT4_MOUNT_QUOTA		0x80000 /* Some quota option set */
#define EXT4_MOUNT_USRQUOTA		0x100000 /* "old" user quota */
#define EXT4_MOUNT_GRPQUOTA		0x200000 /* "old" group quota */
#define EXT4_MOUNT_JOURNAL_CHECKSUM	0x800000 /* Journal checksums */
#define EXT4_MOUNT_JOURNAL_ASYNC_COMMIT	0x1000000 /* Journal Async Commit */
#define EXT4_MOUNT_I_VERSION            0x2000000 /* i_version support */
#define EXT4_MOUNT_DELALLOC		0x8000000 /* Delalloc support */
#define EXT4_MOUNT_DATA_ERR_ABORT	0x10000000 /* Abort on file data write */
#define EXT4_MOUNT_BLOCK_VALIDITY	0x20000000 /* Block validity checking */
#define EXT4_MOUNT_DISCARD		0x40000000 /* Issue DISCARD requests */

#define clear_opt(o, opt)		o &= ~EXT4_MOUNT_##opt
#define set_opt(o, opt)			o |= EXT4_MOUNT_##opt
#define test_opt(sb, opt)		(EXT4_SB(sb)->s_mount_opt & \
					 EXT4_MOUNT_##opt)

#define ext4_set_bit			ext2_set_bit
#define ext4_set_bit_atomic		ext2_set_bit_atomic
#define ext4_clear_bit			ext2_clear_bit
#define ext4_clear_bit_atomic		ext2_clear_bit_atomic
#define ext4_test_bit			ext2_test_bit
#define ext4_find_first_zero_bit	ext2_find_first_zero_bit
#define ext4_find_next_zero_bit		ext2_find_next_zero_bit
#define ext4_find_next_bit		ext2_find_next_bit

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
	__le32	s_obso_log_frag_size;	/* Obsoleted fragment size */
/*20*/	__le32	s_blocks_per_group;	/* # Blocks per group */
	__le32	s_obso_frags_per_group;	/* Obsoleted fragments per group */
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
/*88*/	char	s_last_mounted[64];	/* directory where last mounted */
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
	__u8	s_reserved_char_pad;
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
	__le16  s_mmp_interval;         /* # seconds to wait in MMP checking */
	__le64  s_mmp_block;            /* Block for multi-mount protection */
	__le32  s_raid_stripe_width;    /* blocks on all data disks (N*stride)*/
	__u8	s_log_groups_per_flex;  /* FLEX_BG group size */
	__u8	s_reserved_char_pad2;
	__le16  s_reserved_pad;
	__le64	s_kbytes_written;	/* nr of lifetime kilobytes written */
	__u32   s_reserved[160];        /* Padding to the end of the block */
};

#ifdef __KERNEL__

/*
 * run-time mount flags
 */
#define EXT4_MF_MNTDIR_SAMPLED	0x0001
#define EXT4_MF_FS_ABORTED	0x0002	/* Fatal error detected */

/*
 * fourth extended-fs super-block data in memory
 */
struct ext4_sb_info {
	unsigned long s_desc_size;	/* Size of a group descriptor in bytes */
	unsigned long s_inodes_per_block;/* Number of inodes per block */
	unsigned long s_blocks_per_group;/* Number of blocks in a group */
	unsigned long s_inodes_per_group;/* Number of inodes in a group */
	unsigned long s_itb_per_group;	/* Number of inode table blocks per group */
	unsigned long s_gdb_count;	/* Number of group descriptor blocks */
	unsigned long s_desc_per_block;	/* Number of group descriptors per block */
	ext4_group_t s_groups_count;	/* Number of groups in the fs */
	ext4_group_t s_blockfile_groups;/* Groups acceptable for non-extent files */
	unsigned long s_overhead_last;  /* Last calculated overhead */
	unsigned long s_blocks_last;    /* Last seen block count */
	loff_t s_bitmap_maxbytes;	/* max bytes for bitmap files */
	struct buffer_head * s_sbh;	/* Buffer containing the super block */
	struct ext4_super_block *s_es;	/* Pointer to the super block in the buffer */
	struct buffer_head **s_group_desc;
	unsigned int s_mount_opt;
	unsigned int s_mount_flags;
	ext4_fsblk_t s_sb_block;
	uid_t s_resuid;
	gid_t s_resgid;
	unsigned short s_mount_state;
	unsigned short s_pad;
	int s_addr_per_block_bits;
	int s_desc_per_block_bits;
	int s_inode_size;
	int s_first_ino;
	unsigned int s_inode_readahead_blks;
	unsigned int s_inode_goal;
	spinlock_t s_next_gen_lock;
	u32 s_next_generation;
	u32 s_hash_seed[4];
	int s_def_hash_version;
	int s_hash_unsigned;	/* 3 if hash should be signed, 0 if not */
	struct percpu_counter s_freeblocks_counter;
	struct percpu_counter s_freeinodes_counter;
	struct percpu_counter s_dirs_counter;
	struct percpu_counter s_dirtyblocks_counter;
	struct blockgroup_lock *s_blockgroup_lock;
	struct proc_dir_entry *s_proc;
	struct kobject s_kobj;
	struct completion s_kobj_unregister;

	/* Journaling */
	struct inode *s_journal_inode;
	struct journal_s *s_journal;
	struct list_head s_orphan;
	struct mutex s_orphan_lock;
	struct mutex s_resize_lock;
	unsigned long s_commit_interval;
	u32 s_max_batch_time;
	u32 s_min_batch_time;
	struct block_device *journal_bdev;
#ifdef CONFIG_JBD2_DEBUG
	struct timer_list turn_ro_timer;	/* For turning read-only (crash simulation) */
	wait_queue_head_t ro_wait_queue;	/* For people waiting for the fs to go read-only */
#endif
#ifdef CONFIG_QUOTA
	char *s_qf_names[MAXQUOTAS];		/* Names of quota files with journalled quota */
	int s_jquota_fmt;			/* Format of quota to use */
#endif
	unsigned int s_want_extra_isize; /* New inodes should reserve # bytes */
	struct rb_root system_blks;

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
	long s_blocks_reserved;
	spinlock_t s_reserve_lock;
	spinlock_t s_md_lock;
	tid_t s_last_transaction;
	unsigned short *s_mb_offsets;
	unsigned int *s_mb_maxs;

	/* tunables */
	unsigned long s_stripe;
	unsigned int s_mb_stream_request;
	unsigned int s_mb_max_to_scan;
	unsigned int s_mb_min_to_scan;
	unsigned int s_mb_stats;
	unsigned int s_mb_order2_reqs;
	unsigned int s_mb_group_prealloc;
	unsigned int s_max_writeback_mb_bump;
	/* where last allocation was done - for stream allocation */
	unsigned long s_mb_last_group;
	unsigned long s_mb_last_start;

	/* stats for buddy allocator */
	spinlock_t s_mb_pa_lock;
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
	struct ext4_locality_group *s_locality_groups;

	/* for write statistics */
	unsigned long s_sectors_written_start;
	u64 s_kbytes_written;

	unsigned int s_log_groups_per_flex;
	struct flex_groups *s_flex_groups;

	/* workqueue for dio unwritten */
	struct workqueue_struct *dio_unwritten_wq;
};

static inline struct ext4_sb_info *EXT4_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}
static inline struct ext4_inode_info *EXT4_I(struct inode *inode)
{
	return container_of(inode, struct ext4_inode_info, vfs_inode);
}

static inline struct timespec ext4_current_time(struct inode *inode)
{
	return (inode->i_sb->s_time_gran < NSEC_PER_SEC) ?
		current_fs_time(inode->i_sb) : CURRENT_TIME_SEC;
}

static inline int ext4_valid_inum(struct super_block *sb, unsigned long ino)
{
	return ino == EXT4_ROOT_INO ||
		ino == EXT4_JOURNAL_INO ||
		ino == EXT4_RESIZE_INO ||
		(ino >= EXT4_FIRST_INO(sb) &&
		 ino <= le32_to_cpu(EXT4_SB(sb)->s_es->s_inodes_count));
}
#else
/* Assume that user mode programs are passing in an ext4fs superblock, not
 * a kernel struct super_block.  This will allow us to call the feature-test
 * macros from user land. */
#define EXT4_SB(sb)	(sb)
#endif

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

/*
 * Feature set definitions
 */

#define EXT4_HAS_COMPAT_FEATURE(sb,mask)			\
	((EXT4_SB(sb)->s_es->s_feature_compat & cpu_to_le32(mask)) != 0)
#define EXT4_HAS_RO_COMPAT_FEATURE(sb,mask)			\
	((EXT4_SB(sb)->s_es->s_feature_ro_compat & cpu_to_le32(mask)) != 0)
#define EXT4_HAS_INCOMPAT_FEATURE(sb,mask)			\
	((EXT4_SB(sb)->s_es->s_feature_incompat & cpu_to_le32(mask)) != 0)
#define EXT4_SET_COMPAT_FEATURE(sb,mask)			\
	EXT4_SB(sb)->s_es->s_feature_compat |= cpu_to_le32(mask)
#define EXT4_SET_RO_COMPAT_FEATURE(sb,mask)			\
	EXT4_SB(sb)->s_es->s_feature_ro_compat |= cpu_to_le32(mask)
#define EXT4_SET_INCOMPAT_FEATURE(sb,mask)			\
	EXT4_SB(sb)->s_es->s_feature_incompat |= cpu_to_le32(mask)
#define EXT4_CLEAR_COMPAT_FEATURE(sb,mask)			\
	EXT4_SB(sb)->s_es->s_feature_compat &= ~cpu_to_le32(mask)
#define EXT4_CLEAR_RO_COMPAT_FEATURE(sb,mask)			\
	EXT4_SB(sb)->s_es->s_feature_ro_compat &= ~cpu_to_le32(mask)
#define EXT4_CLEAR_INCOMPAT_FEATURE(sb,mask)			\
	EXT4_SB(sb)->s_es->s_feature_incompat &= ~cpu_to_le32(mask)

#define EXT4_FEATURE_COMPAT_DIR_PREALLOC	0x0001
#define EXT4_FEATURE_COMPAT_IMAGIC_INODES	0x0002
#define EXT4_FEATURE_COMPAT_HAS_JOURNAL		0x0004
#define EXT4_FEATURE_COMPAT_EXT_ATTR		0x0008
#define EXT4_FEATURE_COMPAT_RESIZE_INODE	0x0010
#define EXT4_FEATURE_COMPAT_DIR_INDEX		0x0020

#define EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER	0x0001
#define EXT4_FEATURE_RO_COMPAT_LARGE_FILE	0x0002
#define EXT4_FEATURE_RO_COMPAT_BTREE_DIR	0x0004
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE        0x0008
#define EXT4_FEATURE_RO_COMPAT_GDT_CSUM		0x0010
#define EXT4_FEATURE_RO_COMPAT_DIR_NLINK	0x0020
#define EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE	0x0040

#define EXT4_FEATURE_INCOMPAT_COMPRESSION	0x0001
#define EXT4_FEATURE_INCOMPAT_FILETYPE		0x0002
#define EXT4_FEATURE_INCOMPAT_RECOVER		0x0004 /* Needs recovery */
#define EXT4_FEATURE_INCOMPAT_JOURNAL_DEV	0x0008 /* Journal device */
#define EXT4_FEATURE_INCOMPAT_META_BG		0x0010
#define EXT4_FEATURE_INCOMPAT_EXTENTS		0x0040 /* extents support */
#define EXT4_FEATURE_INCOMPAT_64BIT		0x0080
#define EXT4_FEATURE_INCOMPAT_MMP               0x0100
#define EXT4_FEATURE_INCOMPAT_FLEX_BG		0x0200

#define EXT4_FEATURE_COMPAT_SUPP	EXT2_FEATURE_COMPAT_EXT_ATTR
#define EXT4_FEATURE_INCOMPAT_SUPP	(EXT4_FEATURE_INCOMPAT_FILETYPE| \
					 EXT4_FEATURE_INCOMPAT_RECOVER| \
					 EXT4_FEATURE_INCOMPAT_META_BG| \
					 EXT4_FEATURE_INCOMPAT_EXTENTS| \
					 EXT4_FEATURE_INCOMPAT_64BIT| \
					 EXT4_FEATURE_INCOMPAT_FLEX_BG)
#define EXT4_FEATURE_RO_COMPAT_SUPP	(EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER| \
					 EXT4_FEATURE_RO_COMPAT_LARGE_FILE| \
					 EXT4_FEATURE_RO_COMPAT_GDT_CSUM| \
					 EXT4_FEATURE_RO_COMPAT_DIR_NLINK | \
					 EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE | \
					 EXT4_FEATURE_RO_COMPAT_BTREE_DIR |\
					 EXT4_FEATURE_RO_COMPAT_HUGE_FILE)

/*
 * Default values for user and/or group using reserved blocks
 */
#define	EXT4_DEF_RESUID		0
#define	EXT4_DEF_RESGID		0

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
 * Hash Tree Directory indexing
 * (c) Daniel Phillips, 2001
 */

#define is_dx(dir) (EXT4_HAS_COMPAT_FEATURE(dir->i_sb, \
				      EXT4_FEATURE_COMPAT_DIR_INDEX) && \
		      (EXT4_I(dir)->i_flags & EXT4_INDEX_FL))
#define EXT4_DIR_LINK_MAX(dir) (!is_dx(dir) && (dir)->i_nlink >= EXT4_LINK_MAX)
#define EXT4_DIR_LINK_EMPTY(dir) ((dir)->i_nlink == 2 || (dir)->i_nlink == 1)

/* Legal values for the dx_root hash_version field: */

#define DX_HASH_LEGACY		0
#define DX_HASH_HALF_MD4	1
#define DX_HASH_TEA		2
#define DX_HASH_LEGACY_UNSIGNED	3
#define DX_HASH_HALF_MD4_UNSIGNED	4
#define DX_HASH_TEA_UNSIGNED		5

#ifdef __KERNEL__

/* hash info structure used by the directory hash */
struct dx_hash_info
{
	u32		hash;
	u32		minor_hash;
	int		hash_version;
	u32		*seed;
};

#define EXT4_HTREE_EOF	0x7fffffff

/*
 * Control parameters used by ext4_htree_next_block
 */
#define HASH_NB_ALWAYS		1


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
#define ERR_BAD_DX_DIR	-75000

void ext4_get_group_no_and_offset(struct super_block *sb, ext4_fsblk_t blocknr,
			ext4_group_t *blockgrpp, ext4_grpblk_t *offsetp);

extern struct proc_dir_entry *ext4_proc_root;

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
extern unsigned int ext4_count_free(struct buffer_head *, unsigned);

/* balloc.c */
extern unsigned int ext4_block_group(struct super_block *sb,
			ext4_fsblk_t blocknr);
extern ext4_grpblk_t ext4_block_group_offset(struct super_block *sb,
			ext4_fsblk_t blocknr);
extern int ext4_bg_has_super(struct super_block *sb, ext4_group_t group);
extern unsigned long ext4_bg_num_gdb(struct super_block *sb,
			ext4_group_t group);
extern ext4_fsblk_t ext4_new_meta_blocks(handle_t *handle, struct inode *inode,
			ext4_fsblk_t goal, unsigned long *count, int *errp);
extern int ext4_claim_free_blocks(struct ext4_sb_info *sbi, s64 nblocks);
extern int ext4_has_free_blocks(struct ext4_sb_info *sbi, s64 nblocks);
extern void ext4_free_blocks(handle_t *handle, struct inode *inode,
			ext4_fsblk_t block, unsigned long count, int metadata);
extern void ext4_add_groupblocks(handle_t *handle, struct super_block *sb,
				ext4_fsblk_t block, unsigned long count);
extern ext4_fsblk_t ext4_count_free_blocks(struct super_block *);
extern void ext4_check_blocks_bitmap(struct super_block *);
extern struct ext4_group_desc * ext4_get_group_desc(struct super_block * sb,
						    ext4_group_t block_group,
						    struct buffer_head ** bh);
extern int ext4_should_retry_alloc(struct super_block *sb, int *retries);
struct buffer_head *ext4_read_block_bitmap(struct super_block *sb,
				      ext4_group_t block_group);
extern unsigned ext4_init_block_bitmap(struct super_block *sb,
				       struct buffer_head *bh,
				       ext4_group_t group,
				       struct ext4_group_desc *desc);
#define ext4_free_blocks_after_init(sb, group, desc)			\
		ext4_init_block_bitmap(sb, NULL, group, desc)

/* dir.c */
extern int ext4_check_dir_entry(const char *, struct inode *,
				struct ext4_dir_entry_2 *,
				struct buffer_head *, unsigned int);
extern int ext4_htree_store_dirent(struct file *dir_file, __u32 hash,
				    __u32 minor_hash,
				    struct ext4_dir_entry_2 *dirent);
extern void ext4_htree_free_dir_info(struct dir_private_info *p);

/* fsync.c */
extern int ext4_sync_file(struct file *, struct dentry *, int);

/* hash.c */
extern int ext4fs_dirhash(const char *name, int len, struct
			  dx_hash_info *hinfo);

/* ialloc.c */
extern struct inode *ext4_new_inode(handle_t *, struct inode *, int,
				    const struct qstr *qstr, __u32 goal);
extern void ext4_free_inode(handle_t *, struct inode *);
extern struct inode * ext4_orphan_get(struct super_block *, unsigned long);
extern unsigned long ext4_count_free_inodes(struct super_block *);
extern unsigned long ext4_count_dirs(struct super_block *);
extern void ext4_check_inodes_bitmap(struct super_block *);
extern unsigned ext4_init_inode_bitmap(struct super_block *sb,
				       struct buffer_head *bh,
				       ext4_group_t group,
				       struct ext4_group_desc *desc);
extern void mark_bitmap_end(int start_bit, int end_bit, char *bitmap);

/* mballoc.c */
extern long ext4_mb_stats;
extern long ext4_mb_max_to_scan;
extern int ext4_mb_init(struct super_block *, int);
extern int ext4_mb_release(struct super_block *);
extern ext4_fsblk_t ext4_mb_new_blocks(handle_t *,
				struct ext4_allocation_request *, int *);
extern int ext4_mb_reserve_blocks(struct super_block *, int);
extern void ext4_discard_preallocations(struct inode *);
extern int __init init_ext4_mballoc(void);
extern void exit_ext4_mballoc(void);
extern void ext4_mb_free_blocks(handle_t *, struct inode *,
		ext4_fsblk_t, unsigned long, int, unsigned long *);
extern int ext4_mb_add_groupinfo(struct super_block *sb,
		ext4_group_t i, struct ext4_group_desc *desc);
extern int ext4_mb_get_buddy_cache_lock(struct super_block *, ext4_group_t);
extern void ext4_mb_put_buddy_cache_lock(struct super_block *,
						ext4_group_t, int);
/* inode.c */
int ext4_forget(handle_t *handle, int is_metadata, struct inode *inode,
		struct buffer_head *bh, ext4_fsblk_t blocknr);
struct buffer_head *ext4_getblk(handle_t *, struct inode *,
						ext4_lblk_t, int, int *);
struct buffer_head *ext4_bread(handle_t *, struct inode *,
						ext4_lblk_t, int, int *);
int ext4_get_block(struct inode *inode, sector_t iblock,
				struct buffer_head *bh_result, int create);

extern struct inode *ext4_iget(struct super_block *, unsigned long);
extern int  ext4_write_inode(struct inode *, int);
extern int  ext4_setattr(struct dentry *, struct iattr *);
extern int  ext4_getattr(struct vfsmount *mnt, struct dentry *dentry,
				struct kstat *stat);
extern void ext4_delete_inode(struct inode *);
extern int  ext4_sync_inode(handle_t *, struct inode *);
extern void ext4_dirty_inode(struct inode *);
extern int ext4_change_inode_journal_flag(struct inode *, int);
extern int ext4_get_inode_loc(struct inode *, struct ext4_iloc *);
extern int ext4_can_truncate(struct inode *inode);
extern void ext4_truncate(struct inode *);
extern int ext4_truncate_restart_trans(handle_t *, struct inode *, int nblocks);
extern void ext4_set_inode_flags(struct inode *);
extern void ext4_get_inode_flags(struct ext4_inode_info *);
extern int ext4_alloc_da_blocks(struct inode *inode);
extern void ext4_set_aops(struct inode *inode);
extern int ext4_writepage_trans_blocks(struct inode *);
extern int ext4_meta_trans_blocks(struct inode *, int nrblocks, int idxblocks);
extern int ext4_chunk_trans_blocks(struct inode *, int nrblocks);
extern int ext4_block_truncate_page(handle_t *handle,
		struct address_space *mapping, loff_t from);
extern int ext4_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf);
extern qsize_t ext4_get_reserved_space(struct inode *inode);
extern int flush_aio_dio_completed_IO(struct inode *inode);
/* ioctl.c */
extern long ext4_ioctl(struct file *, unsigned int, unsigned long);
extern long ext4_compat_ioctl(struct file *, unsigned int, unsigned long);

/* migrate.c */
extern int ext4_ext_migrate(struct inode *);

/* namei.c */
extern unsigned int ext4_rec_len_from_disk(__le16 dlen, unsigned blocksize);
extern __le16 ext4_rec_len_to_disk(unsigned len, unsigned blocksize);
extern int ext4_orphan_add(handle_t *, struct inode *);
extern int ext4_orphan_del(handle_t *, struct inode *);
extern int ext4_htree_fill_tree(struct file *dir_file, __u32 start_hash,
				__u32 start_minor_hash, __u32 *next_hash);

/* resize.c */
extern int ext4_group_add(struct super_block *sb,
				struct ext4_new_group_data *input);
extern int ext4_group_extend(struct super_block *sb,
				struct ext4_super_block *es,
				ext4_fsblk_t n_blocks_count);

/* super.c */
extern void ext4_error(struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern void __ext4_std_error(struct super_block *, const char *, int);
extern void ext4_abort(struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern void ext4_warning(struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern void ext4_msg(struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern void ext4_grp_locked_error(struct super_block *, ext4_group_t,
				const char *, const char *, ...)
	__attribute__ ((format (printf, 4, 5)));
extern void ext4_update_dynamic_rev(struct super_block *sb);
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
extern __u32 ext4_free_blks_count(struct super_block *sb,
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
extern void ext4_free_blks_set(struct super_block *sb,
			       struct ext4_group_desc *bg, __u32 count);
extern void ext4_free_inodes_set(struct super_block *sb,
				struct ext4_group_desc *bg, __u32 count);
extern void ext4_used_dirs_set(struct super_block *sb,
				struct ext4_group_desc *bg, __u32 count);
extern void ext4_itable_unused_set(struct super_block *sb,
				   struct ext4_group_desc *bg, __u32 count);
extern __le16 ext4_group_desc_csum(struct ext4_sb_info *sbi, __u32 group,
				   struct ext4_group_desc *gdp);
extern int ext4_group_desc_csum_verify(struct ext4_sb_info *sbi, __u32 group,
				       struct ext4_group_desc *gdp);

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

static inline loff_t ext4_isize(struct ext4_inode *raw_inode)
{
	if (S_ISREG(le16_to_cpu(raw_inode->i_mode)))
		return ((loff_t)le32_to_cpu(raw_inode->i_size_high) << 32) |
			le32_to_cpu(raw_inode->i_size_lo);
	else
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
		__ext4_std_error((sb), __func__, (errno));	\
} while (0)

#ifdef CONFIG_SMP
/* Each CPU can accumulate percpu_counter_batch blocks in their local
 * counters. So we need to make sure we have free blocks more
 * than percpu_counter_batch  * nr_cpu_ids. Also add a window of 4 times.
 */
#define EXT4_FREEBLOCKS_WATERMARK (4 * (percpu_counter_batch * nr_cpu_ids))
#else
#define EXT4_FREEBLOCKS_WATERMARK 0
#endif

static inline void ext4_update_i_disksize(struct inode *inode, loff_t newsize)
{
	/*
	 * XXX: replace with spinlock if seen contended -bzzz
	 */
	down_write(&EXT4_I(inode)->i_data_sem);
	if (newsize > EXT4_I(inode)->i_disksize)
		EXT4_I(inode)->i_disksize = newsize;
	up_write(&EXT4_I(inode)->i_data_sem);
	return ;
}

struct ext4_group_info {
	unsigned long   bb_state;
	struct rb_root  bb_free_root;
	ext4_grpblk_t	bb_first_free;	/* first free block */
	ext4_grpblk_t	bb_free;	/* total free blocks */
	ext4_grpblk_t	bb_fragments;	/* nr of freespace fragments */
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

#define EXT4_GROUP_INFO_NEED_INIT_BIT	0

#define EXT4_MB_GRP_NEED_INIT(grp)	\
	(test_bit(EXT4_GROUP_INFO_NEED_INIT_BIT, &((grp)->bb_state)))

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
 * Inodes and files operations
 */

/* dir.c */
extern const struct file_operations ext4_dir_operations;

/* file.c */
extern const struct inode_operations ext4_file_inode_operations;
extern const struct file_operations ext4_file_operations;

/* namei.c */
extern const struct inode_operations ext4_dir_inode_operations;
extern const struct inode_operations ext4_special_inode_operations;
extern struct dentry *ext4_get_parent(struct dentry *child);

/* symlink.c */
extern const struct inode_operations ext4_symlink_inode_operations;
extern const struct inode_operations ext4_fast_symlink_inode_operations;

/* block_validity */
extern void ext4_release_system_zone(struct super_block *sb);
extern int ext4_setup_system_zone(struct super_block *sb);
extern int __init init_ext4_system_zone(void);
extern void exit_ext4_system_zone(void);
extern int ext4_data_block_valid(struct ext4_sb_info *sbi,
				 ext4_fsblk_t start_blk,
				 unsigned int count);

/* extents.c */
extern int ext4_ext_tree_init(handle_t *handle, struct inode *);
extern int ext4_ext_writepage_trans_blocks(struct inode *, int);
extern int ext4_ext_index_trans_blocks(struct inode *inode, int nrblocks,
				       int chunk);
extern int ext4_ext_get_blocks(handle_t *handle, struct inode *inode,
			       ext4_lblk_t iblock, unsigned int max_blocks,
			       struct buffer_head *bh_result, int flags);
extern void ext4_ext_truncate(struct inode *);
extern void ext4_ext_init(struct super_block *);
extern void ext4_ext_release(struct super_block *);
extern long ext4_fallocate(struct inode *inode, int mode, loff_t offset,
			  loff_t len);
extern int ext4_convert_unwritten_extents(struct inode *inode, loff_t offset,
			  loff_t len);
extern int ext4_get_blocks(handle_t *handle, struct inode *inode,
			   sector_t block, unsigned int max_blocks,
			   struct buffer_head *bh, int flags);
extern int ext4_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
			__u64 start, __u64 len);
/* move_extent.c */
extern int ext4_move_extents(struct file *o_filp, struct file *d_filp,
			     __u64 start_orig, __u64 start_donor,
			     __u64 len, __u64 *moved_len);


/*
 * Add new method to test wether block and inode bitmaps are properly
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

#endif	/* __KERNEL__ */

#endif	/* _EXT4_H */

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
#include "ext4_i.h"

/*
 * The fourth extended filesystem constants/structures
 */

/*
 * Define EXT4FS_DEBUG to produce debug messages
 */
#undef EXT4FS_DEBUG

/*
 * Define EXT4_RESERVATION to reserve data blocks for expanding files
 */
#define EXT4_DEFAULT_RESERVE_BLOCKS	8
/*max window size: 1024(direct blocks) + 3([t,d]indirect blocks) */
#define EXT4_MAX_RESERVE_BLOCKS		1027
#define EXT4_RESERVE_WINDOW_NOT_ALLOCATED 0

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

#define EXT4_MULTIBLOCK_ALLOCATOR	1

/* prefer goal again. length */
#define EXT4_MB_HINT_MERGE		1
/* blocks already reserved */
#define EXT4_MB_HINT_RESERVED		2
/* metadata is being allocated */
#define EXT4_MB_HINT_METADATA		4
/* first blocks in the file */
#define EXT4_MB_HINT_FIRST		8
/* search for the best chunk */
#define EXT4_MB_HINT_BEST		16
/* data is being allocated */
#define EXT4_MB_HINT_DATA		32
/* don't preallocate (for tails) */
#define EXT4_MB_HINT_NOPREALLOC		64
/* allocate for locality group */
#define EXT4_MB_HINT_GROUP_ALLOC	128
/* allocate goal blocks or none */
#define EXT4_MB_HINT_GOAL_ONLY		256
/* goal is meaningful */
#define EXT4_MB_HINT_TRY_GOAL		512
/* blocks already pre-reserved by delayed allocation */
#define EXT4_MB_DELALLOC_RESERVED      1024


struct ext4_allocation_request {
	/* target inode for block we're allocating */
	struct inode *inode;
	/* logical block in target inode */
	ext4_lblk_t logical;
	/* phys. target (a hint) */
	ext4_fsblk_t goal;
	/* the closest logical allocated block to the left */
	ext4_lblk_t lleft;
	/* phys. block for ^^^ */
	ext4_fsblk_t pleft;
	/* the closest logical allocated block to the right */
	ext4_lblk_t lright;
	/* phys. block for ^^^ */
	ext4_fsblk_t pright;
	/* how many blocks we want to allocate */
	unsigned int len;
	/* flags. see above EXT4_MB_HINT_* */
	unsigned int flags;
};

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
	__u32 free_inodes;
	__u32 free_blocks;
};

#define EXT4_BG_INODE_UNINIT	0x0001 /* Inode table/bitmap not in use */
#define EXT4_BG_BLOCK_UNINIT	0x0002 /* Block bitmap not in use */
#define EXT4_BG_INODE_ZEROED	0x0004 /* On-disk itable initialized to zero */

#ifdef __KERNEL__
#include "ext4_sb.h"
#endif
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
#define EXT4_EXT_MIGRATE		0x00100000 /* Inode is migrating */
#define EXT4_RESERVED_FL		0x80000000 /* reserved for ext4 lib */

#define EXT4_FL_USER_VISIBLE		0x000BDFFF /* User visible flags */
#define EXT4_FL_USER_MODIFIABLE		0x000B80FF /* User modifiable flags */

/*
 * Inode dynamic state flags
 */
#define EXT4_STATE_JDATA		0x00000001 /* journaled data exists */
#define EXT4_STATE_NEW			0x00000002 /* inode is newly created */
#define EXT4_STATE_XATTR		0x00000004 /* has in-inode xattrs */
#define EXT4_STATE_NO_EXPAND		0x00000008 /* No space for expansion */

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
 * Following is used by preallocation code to tell get_blocks() that we
 * want uninitialzed extents.
 */
#define EXT4_CREATE_UNINITIALIZED_EXT		2

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
 /* note ioctl 11 reserved for filesystem-independent FIEMAP ioctl */

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
			   time->tv_sec >> 32 : 0) |
			   ((time->tv_nsec << 2) & EXT4_NSEC_MASK));
}

static inline void ext4_decode_extra_time(struct timespec *time, __le32 extra)
{
       if (sizeof(time->tv_sec) > 4)
	       time->tv_sec |= (__u64)(le32_to_cpu(extra) & EXT4_EPOCH_MASK)
			       << 32;
       time->tv_nsec = (le32_to_cpu(extra) & EXT4_NSEC_MASK) >> 2;
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
#define EXT4_MOUNT_ABORT		0x00200	/* Fatal error detected */
#define EXT4_MOUNT_DATA_FLAGS		0x00C00	/* Mode for data writes: */
#define EXT4_MOUNT_JOURNAL_DATA		0x00400	/* Write data to journal */
#define EXT4_MOUNT_ORDERED_DATA		0x00800	/* Flush data before commit */
#define EXT4_MOUNT_WRITEBACK_DATA	0x00C00	/* No data ordering */
#define EXT4_MOUNT_UPDATE_JOURNAL	0x01000	/* Update the journal format */
#define EXT4_MOUNT_NO_UID32		0x02000  /* Disable 32-bit UIDs */
#define EXT4_MOUNT_XATTR_USER		0x04000	/* Extended user attributes */
#define EXT4_MOUNT_POSIX_ACL		0x08000	/* POSIX Access Control Lists */
#define EXT4_MOUNT_RESERVATION		0x10000	/* Preallocation */
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

/* Compatibility, for having both ext2_fs.h and ext4_fs.h included at once */
#ifndef _LINUX_EXT2_FS_H
#define clear_opt(o, opt)		o &= ~EXT4_MOUNT_##opt
#define set_opt(o, opt)			o |= EXT4_MOUNT_##opt
#define test_opt(sb, opt)		(EXT4_SB(sb)->s_mount_opt & \
					 EXT4_MOUNT_##opt)
#else
#define EXT2_MOUNT_NOLOAD		EXT4_MOUNT_NOLOAD
#define EXT2_MOUNT_ABORT		EXT4_MOUNT_ABORT
#define EXT2_MOUNT_DATA_FLAGS		EXT4_MOUNT_DATA_FLAGS
#endif

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
	__u32   s_reserved[162];        /* Padding to the end of the block */
};

#ifdef __KERNEL__
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

static inline unsigned ext4_rec_len_from_disk(__le16 dlen)
{
	unsigned len = le16_to_cpu(dlen);

	if (len == EXT4_MAX_REC_LEN || len == 0)
		return 1 << 16;
	return len;
}

static inline __le16 ext4_rec_len_to_disk(unsigned len)
{
	if (len == (1 << 16))
		return cpu_to_le16(EXT4_MAX_REC_LEN);
	else if (len > (1 << 16))
		BUG();
	return cpu_to_le16(len);
}

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

#ifdef CONFIG_PROC_FS
extern const struct file_operations ext4_ui_proc_fops;

#define	EXT4_PROC_HANDLER(name, var)					\
do {									\
	proc = proc_create_data(name, mode, sbi->s_proc,		\
				&ext4_ui_proc_fops, &sbi->s_##var);	\
	if (proc == NULL) {						\
		printk(KERN_ERR "EXT4-fs: can't create %s\n", name);	\
		goto err_out;						\
	}								\
} while (0)
#else
#define EXT4_PROC_HANDLER(name, var)
#endif

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
extern struct inode * ext4_new_inode(handle_t *, struct inode *, int);
extern void ext4_free_inode(handle_t *, struct inode *);
extern struct inode * ext4_orphan_get(struct super_block *, unsigned long);
extern unsigned long ext4_count_free_inodes(struct super_block *);
extern unsigned long ext4_count_dirs(struct super_block *);
extern void ext4_check_inodes_bitmap(struct super_block *);

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
		unsigned long, unsigned long, int, unsigned long *);
extern int ext4_mb_add_groupinfo(struct super_block *sb,
		ext4_group_t i, struct ext4_group_desc *desc);
extern void ext4_mb_update_group_info(struct ext4_group_info *grp,
		ext4_grpblk_t add);
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
extern void ext4_set_inode_flags(struct inode *);
extern void ext4_get_inode_flags(struct ext4_inode_info *);
extern void ext4_set_aops(struct inode *inode);
extern int ext4_writepage_trans_blocks(struct inode *);
extern int ext4_meta_trans_blocks(struct inode *, int nrblocks, int idxblocks);
extern int ext4_chunk_trans_blocks(struct inode *, int nrblocks);
extern int ext4_block_truncate_page(handle_t *handle,
		struct address_space *mapping, loff_t from);
extern int ext4_page_mkwrite(struct vm_area_struct *vma, struct page *page);

/* ioctl.c */
extern long ext4_ioctl(struct file *, unsigned int, unsigned long);
extern long ext4_compat_ioctl(struct file *, unsigned int, unsigned long);

/* migrate.c */
extern int ext4_ext_migrate(struct inode *);
/* namei.c */
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
	unsigned short  bb_first_free;
	unsigned short  bb_free;
	unsigned short  bb_fragments;
	struct          list_head bb_prealloc_list;
#ifdef DOUBLE_CHECK
	void            *bb_bitmap;
#endif
	struct rw_semaphore alloc_sem;
	unsigned short  bb_counters[];
};

#define EXT4_GROUP_INFO_NEED_INIT_BIT	0
#define EXT4_GROUP_INFO_LOCKED_BIT	1

#define EXT4_MB_GRP_NEED_INIT(grp)	\
	(test_bit(EXT4_GROUP_INFO_NEED_INIT_BIT, &((grp)->bb_state)))

static inline void ext4_lock_group(struct super_block *sb, ext4_group_t group)
{
	struct ext4_group_info *grinfo = ext4_get_group_info(sb, group);

	bit_spin_lock(EXT4_GROUP_INFO_LOCKED_BIT, &(grinfo->bb_state));
}

static inline void ext4_unlock_group(struct super_block *sb,
					ext4_group_t group)
{
	struct ext4_group_info *grinfo = ext4_get_group_info(sb, group);

	bit_spin_unlock(EXT4_GROUP_INFO_LOCKED_BIT, &(grinfo->bb_state));
}

static inline int ext4_is_group_locked(struct super_block *sb,
					ext4_group_t group)
{
	struct ext4_group_info *grinfo = ext4_get_group_info(sb, group);

	return bit_spin_is_locked(EXT4_GROUP_INFO_LOCKED_BIT,
						&(grinfo->bb_state));
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

/* symlink.c */
extern const struct inode_operations ext4_symlink_inode_operations;
extern const struct inode_operations ext4_fast_symlink_inode_operations;

/* extents.c */
extern int ext4_ext_tree_init(handle_t *handle, struct inode *);
extern int ext4_ext_writepage_trans_blocks(struct inode *, int);
extern int ext4_ext_index_trans_blocks(struct inode *inode, int nrblocks,
				       int chunk);
extern int ext4_ext_get_blocks(handle_t *handle, struct inode *inode,
			       ext4_lblk_t iblock, unsigned int max_blocks,
			       struct buffer_head *bh_result,
			       int create, int extend_disksize);
extern void ext4_ext_truncate(struct inode *);
extern void ext4_ext_init(struct super_block *);
extern void ext4_ext_release(struct super_block *);
extern long ext4_fallocate(struct inode *inode, int mode, loff_t offset,
			  loff_t len);
extern int ext4_get_blocks_wrap(handle_t *handle, struct inode *inode,
			sector_t block, unsigned int max_blocks,
			struct buffer_head *bh, int create,
			int extend_disksize, int flag);
extern int ext4_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
			__u64 start, __u64 len);

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

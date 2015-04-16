/*
 * Written by Stephen C. Tweedie <sct@redhat.com>, 1999
 *
 * Copyright 1998--1999 Red Hat corp --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
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

#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/magic.h>
#include <linux/bug.h>
#include <linux/blockgroup_lock.h>

/*
 * The second extended filesystem constants/structures
 */

/*
 * Define EXT3FS_DEBUG to produce debug messages
 */
#undef EXT3FS_DEBUG

/*
 * Define EXT3_RESERVATION to reserve data blocks for expanding files
 */
#define EXT3_DEFAULT_RESERVE_BLOCKS     8
/*max window size: 1024(direct blocks) + 3([t,d]indirect blocks) */
#define EXT3_MAX_RESERVE_BLOCKS         1027
#define EXT3_RESERVE_WINDOW_NOT_ALLOCATED 0

/*
 * Debug code
 */
#ifdef EXT3FS_DEBUG
#define ext3_debug(f, a...)						\
	do {								\
		printk (KERN_DEBUG "EXT3-fs DEBUG (%s, %d): %s:",	\
			__FILE__, __LINE__, __func__);		\
		printk (KERN_DEBUG f, ## a);				\
	} while (0)
#else
#define ext3_debug(f, a...)	do {} while (0)
#endif

/*
 * Special inodes numbers
 */
#define	EXT3_BAD_INO		 1	/* Bad blocks inode */
#define EXT3_ROOT_INO		 2	/* Root inode */
#define EXT3_BOOT_LOADER_INO	 5	/* Boot loader inode */
#define EXT3_UNDEL_DIR_INO	 6	/* Undelete directory inode */
#define EXT3_RESIZE_INO		 7	/* Reserved group descriptors inode */
#define EXT3_JOURNAL_INO	 8	/* Journal inode */

/* First non-reserved inode for old ext3 filesystems */
#define EXT3_GOOD_OLD_FIRST_INO	11

/*
 * Maximal count of links to a file
 */
#define EXT3_LINK_MAX		32000

/*
 * Macro-instructions used to manage several block sizes
 */
#define EXT3_MIN_BLOCK_SIZE		1024
#define	EXT3_MAX_BLOCK_SIZE		65536
#define EXT3_MIN_BLOCK_LOG_SIZE		10
#define EXT3_BLOCK_SIZE(s)		((s)->s_blocksize)
#define	EXT3_ADDR_PER_BLOCK(s)		(EXT3_BLOCK_SIZE(s) / sizeof (__u32))
#define EXT3_BLOCK_SIZE_BITS(s)	((s)->s_blocksize_bits)
#define	EXT3_ADDR_PER_BLOCK_BITS(s)	(EXT3_SB(s)->s_addr_per_block_bits)
#define EXT3_INODE_SIZE(s)		(EXT3_SB(s)->s_inode_size)
#define EXT3_FIRST_INO(s)		(EXT3_SB(s)->s_first_ino)

/*
 * Macro-instructions used to manage fragments
 */
#define EXT3_MIN_FRAG_SIZE		1024
#define	EXT3_MAX_FRAG_SIZE		4096
#define EXT3_MIN_FRAG_LOG_SIZE		  10
#define EXT3_FRAG_SIZE(s)		(EXT3_SB(s)->s_frag_size)
#define EXT3_FRAGS_PER_BLOCK(s)		(EXT3_SB(s)->s_frags_per_block)

/*
 * Structure of a blocks group descriptor
 */
struct ext3_group_desc
{
	__le32	bg_block_bitmap;		/* Blocks bitmap block */
	__le32	bg_inode_bitmap;		/* Inodes bitmap block */
	__le32	bg_inode_table;		/* Inodes table block */
	__le16	bg_free_blocks_count;	/* Free blocks count */
	__le16	bg_free_inodes_count;	/* Free inodes count */
	__le16	bg_used_dirs_count;	/* Directories count */
	__u16	bg_pad;
	__le32	bg_reserved[3];
};

/*
 * Macro-instructions used to manage group descriptors
 */
#define EXT3_BLOCKS_PER_GROUP(s)	(EXT3_SB(s)->s_blocks_per_group)
#define EXT3_DESC_PER_BLOCK(s)		(EXT3_SB(s)->s_desc_per_block)
#define EXT3_INODES_PER_GROUP(s)	(EXT3_SB(s)->s_inodes_per_group)
#define EXT3_DESC_PER_BLOCK_BITS(s)	(EXT3_SB(s)->s_desc_per_block_bits)

/*
 * Constants relative to the data blocks
 */
#define	EXT3_NDIR_BLOCKS		12
#define	EXT3_IND_BLOCK			EXT3_NDIR_BLOCKS
#define	EXT3_DIND_BLOCK			(EXT3_IND_BLOCK + 1)
#define	EXT3_TIND_BLOCK			(EXT3_DIND_BLOCK + 1)
#define	EXT3_N_BLOCKS			(EXT3_TIND_BLOCK + 1)

/*
 * Inode flags
 */
#define	EXT3_SECRM_FL			0x00000001 /* Secure deletion */
#define	EXT3_UNRM_FL			0x00000002 /* Undelete */
#define	EXT3_COMPR_FL			0x00000004 /* Compress file */
#define EXT3_SYNC_FL			0x00000008 /* Synchronous updates */
#define EXT3_IMMUTABLE_FL		0x00000010 /* Immutable file */
#define EXT3_APPEND_FL			0x00000020 /* writes to file may only append */
#define EXT3_NODUMP_FL			0x00000040 /* do not dump file */
#define EXT3_NOATIME_FL			0x00000080 /* do not update atime */
/* Reserved for compression usage... */
#define EXT3_DIRTY_FL			0x00000100
#define EXT3_COMPRBLK_FL		0x00000200 /* One or more compressed clusters */
#define EXT3_NOCOMPR_FL			0x00000400 /* Don't compress */
#define EXT3_ECOMPR_FL			0x00000800 /* Compression error */
/* End compression flags --- maybe not all used */
#define EXT3_INDEX_FL			0x00001000 /* hash-indexed directory */
#define EXT3_IMAGIC_FL			0x00002000 /* AFS directory */
#define EXT3_JOURNAL_DATA_FL		0x00004000 /* file data should be journaled */
#define EXT3_NOTAIL_FL			0x00008000 /* file tail should not be merged */
#define EXT3_DIRSYNC_FL			0x00010000 /* dirsync behaviour (directories only) */
#define EXT3_TOPDIR_FL			0x00020000 /* Top of directory hierarchies*/
#define EXT3_RESERVED_FL		0x80000000 /* reserved for ext3 lib */

#define EXT3_FL_USER_VISIBLE		0x0003DFFF /* User visible flags */
#define EXT3_FL_USER_MODIFIABLE		0x000380FF /* User modifiable flags */

/* Flags that should be inherited by new inodes from their parent. */
#define EXT3_FL_INHERITED (EXT3_SECRM_FL | EXT3_UNRM_FL | EXT3_COMPR_FL |\
			   EXT3_SYNC_FL | EXT3_NODUMP_FL |\
			   EXT3_NOATIME_FL | EXT3_COMPRBLK_FL |\
			   EXT3_NOCOMPR_FL | EXT3_JOURNAL_DATA_FL |\
			   EXT3_NOTAIL_FL | EXT3_DIRSYNC_FL)

/* Flags that are appropriate for regular files (all but dir-specific ones). */
#define EXT3_REG_FLMASK (~(EXT3_DIRSYNC_FL | EXT3_TOPDIR_FL))

/* Flags that are appropriate for non-directories/regular files. */
#define EXT3_OTHER_FLMASK (EXT3_NODUMP_FL | EXT3_NOATIME_FL)

/* Mask out flags that are inappropriate for the given type of inode. */
static inline __u32 ext3_mask_flags(umode_t mode, __u32 flags)
{
	if (S_ISDIR(mode))
		return flags;
	else if (S_ISREG(mode))
		return flags & EXT3_REG_FLMASK;
	else
		return flags & EXT3_OTHER_FLMASK;
}

/* Used to pass group descriptor data when online resize is done */
struct ext3_new_group_input {
	__u32 group;            /* Group number for this data */
	__u32 block_bitmap;     /* Absolute block number of block bitmap */
	__u32 inode_bitmap;     /* Absolute block number of inode bitmap */
	__u32 inode_table;      /* Absolute block number of inode table start */
	__u32 blocks_count;     /* Total number of blocks in this group */
	__u16 reserved_blocks;  /* Number of reserved blocks in this group */
	__u16 unused;
};

/* The struct ext3_new_group_input in kernel space, with free_blocks_count */
struct ext3_new_group_data {
	__u32 group;
	__u32 block_bitmap;
	__u32 inode_bitmap;
	__u32 inode_table;
	__u32 blocks_count;
	__u16 reserved_blocks;
	__u16 unused;
	__u32 free_blocks_count;
};


/*
 * ioctl commands
 */
#define	EXT3_IOC_GETFLAGS		FS_IOC_GETFLAGS
#define	EXT3_IOC_SETFLAGS		FS_IOC_SETFLAGS
#define	EXT3_IOC_GETVERSION		_IOR('f', 3, long)
#define	EXT3_IOC_SETVERSION		_IOW('f', 4, long)
#define EXT3_IOC_GROUP_EXTEND		_IOW('f', 7, unsigned long)
#define EXT3_IOC_GROUP_ADD		_IOW('f', 8,struct ext3_new_group_input)
#define	EXT3_IOC_GETVERSION_OLD		FS_IOC_GETVERSION
#define	EXT3_IOC_SETVERSION_OLD		FS_IOC_SETVERSION
#ifdef CONFIG_JBD_DEBUG
#define EXT3_IOC_WAIT_FOR_READONLY	_IOR('f', 99, long)
#endif
#define EXT3_IOC_GETRSVSZ		_IOR('f', 5, long)
#define EXT3_IOC_SETRSVSZ		_IOW('f', 6, long)

/*
 * ioctl commands in 32 bit emulation
 */
#define EXT3_IOC32_GETFLAGS		FS_IOC32_GETFLAGS
#define EXT3_IOC32_SETFLAGS		FS_IOC32_SETFLAGS
#define EXT3_IOC32_GETVERSION		_IOR('f', 3, int)
#define EXT3_IOC32_SETVERSION		_IOW('f', 4, int)
#define EXT3_IOC32_GETRSVSZ		_IOR('f', 5, int)
#define EXT3_IOC32_SETRSVSZ		_IOW('f', 6, int)
#define EXT3_IOC32_GROUP_EXTEND		_IOW('f', 7, unsigned int)
#ifdef CONFIG_JBD_DEBUG
#define EXT3_IOC32_WAIT_FOR_READONLY	_IOR('f', 99, int)
#endif
#define EXT3_IOC32_GETVERSION_OLD	FS_IOC32_GETVERSION
#define EXT3_IOC32_SETVERSION_OLD	FS_IOC32_SETVERSION

/* Number of supported quota types */
#define EXT3_MAXQUOTAS 2

/*
 *  Mount options
 */
struct ext3_mount_options {
	unsigned long s_mount_opt;
	kuid_t s_resuid;
	kgid_t s_resgid;
	unsigned long s_commit_interval;
#ifdef CONFIG_QUOTA
	int s_jquota_fmt;
	char *s_qf_names[EXT3_MAXQUOTAS];
#endif
};

/*
 * Structure of an inode on the disk
 */
struct ext3_inode {
	__le16	i_mode;		/* File mode */
	__le16	i_uid;		/* Low 16 bits of Owner Uid */
	__le32	i_size;		/* Size in bytes */
	__le32	i_atime;	/* Access time */
	__le32	i_ctime;	/* Creation time */
	__le32	i_mtime;	/* Modification time */
	__le32	i_dtime;	/* Deletion Time */
	__le16	i_gid;		/* Low 16 bits of Group Id */
	__le16	i_links_count;	/* Links count */
	__le32	i_blocks;	/* Blocks count */
	__le32	i_flags;	/* File flags */
	union {
		struct {
			__u32  l_i_reserved1;
		} linux1;
		struct {
			__u32  h_i_translator;
		} hurd1;
		struct {
			__u32  m_i_reserved1;
		} masix1;
	} osd1;				/* OS dependent 1 */
	__le32	i_block[EXT3_N_BLOCKS];/* Pointers to blocks */
	__le32	i_generation;	/* File version (for NFS) */
	__le32	i_file_acl;	/* File ACL */
	__le32	i_dir_acl;	/* Directory ACL */
	__le32	i_faddr;	/* Fragment address */
	union {
		struct {
			__u8	l_i_frag;	/* Fragment number */
			__u8	l_i_fsize;	/* Fragment size */
			__u16	i_pad1;
			__le16	l_i_uid_high;	/* these 2 fields    */
			__le16	l_i_gid_high;	/* were reserved2[0] */
			__u32	l_i_reserved2;
		} linux2;
		struct {
			__u8	h_i_frag;	/* Fragment number */
			__u8	h_i_fsize;	/* Fragment size */
			__u16	h_i_mode_high;
			__u16	h_i_uid_high;
			__u16	h_i_gid_high;
			__u32	h_i_author;
		} hurd2;
		struct {
			__u8	m_i_frag;	/* Fragment number */
			__u8	m_i_fsize;	/* Fragment size */
			__u16	m_pad1;
			__u32	m_i_reserved2[2];
		} masix2;
	} osd2;				/* OS dependent 2 */
	__le16	i_extra_isize;
	__le16	i_pad1;
};

#define i_size_high	i_dir_acl

#define i_reserved1	osd1.linux1.l_i_reserved1
#define i_frag		osd2.linux2.l_i_frag
#define i_fsize		osd2.linux2.l_i_fsize
#define i_uid_low	i_uid
#define i_gid_low	i_gid
#define i_uid_high	osd2.linux2.l_i_uid_high
#define i_gid_high	osd2.linux2.l_i_gid_high
#define i_reserved2	osd2.linux2.l_i_reserved2

/*
 * File system states
 */
#define	EXT3_VALID_FS			0x0001	/* Unmounted cleanly */
#define	EXT3_ERROR_FS			0x0002	/* Errors detected */
#define	EXT3_ORPHAN_FS			0x0004	/* Orphans being recovered */

/*
 * Misc. filesystem flags
 */
#define EXT2_FLAGS_SIGNED_HASH		0x0001  /* Signed dirhash in use */
#define EXT2_FLAGS_UNSIGNED_HASH	0x0002  /* Unsigned dirhash in use */
#define EXT2_FLAGS_TEST_FILESYS		0x0004	/* to test development code */

/*
 * Mount flags
 */
#define EXT3_MOUNT_CHECK		0x00001	/* Do mount-time checks */
/* EXT3_MOUNT_OLDALLOC was there */
#define EXT3_MOUNT_GRPID		0x00004	/* Create files with directory's group */
#define EXT3_MOUNT_DEBUG		0x00008	/* Some debugging messages */
#define EXT3_MOUNT_ERRORS_CONT		0x00010	/* Continue on errors */
#define EXT3_MOUNT_ERRORS_RO		0x00020	/* Remount fs ro on errors */
#define EXT3_MOUNT_ERRORS_PANIC		0x00040	/* Panic on errors */
#define EXT3_MOUNT_MINIX_DF		0x00080	/* Mimics the Minix statfs */
#define EXT3_MOUNT_NOLOAD		0x00100	/* Don't use existing journal*/
#define EXT3_MOUNT_ABORT		0x00200	/* Fatal error detected */
#define EXT3_MOUNT_DATA_FLAGS		0x00C00	/* Mode for data writes: */
#define EXT3_MOUNT_JOURNAL_DATA		0x00400	/* Write data to journal */
#define EXT3_MOUNT_ORDERED_DATA		0x00800	/* Flush data before commit */
#define EXT3_MOUNT_WRITEBACK_DATA	0x00C00	/* No data ordering */
#define EXT3_MOUNT_UPDATE_JOURNAL	0x01000	/* Update the journal format */
#define EXT3_MOUNT_NO_UID32		0x02000  /* Disable 32-bit UIDs */
#define EXT3_MOUNT_XATTR_USER		0x04000	/* Extended user attributes */
#define EXT3_MOUNT_POSIX_ACL		0x08000	/* POSIX Access Control Lists */
#define EXT3_MOUNT_RESERVATION		0x10000	/* Preallocation */
#define EXT3_MOUNT_BARRIER		0x20000 /* Use block barriers */
#define EXT3_MOUNT_QUOTA		0x80000 /* Some quota option set */
#define EXT3_MOUNT_USRQUOTA		0x100000 /* "old" user quota */
#define EXT3_MOUNT_GRPQUOTA		0x200000 /* "old" group quota */
#define EXT3_MOUNT_DATA_ERR_ABORT	0x400000 /* Abort on file data write
						  * error in ordered mode */

/* Compatibility, for having both ext2_fs.h and ext3_fs.h included at once */
#ifndef _LINUX_EXT2_FS_H
#define clear_opt(o, opt)		o &= ~EXT3_MOUNT_##opt
#define set_opt(o, opt)			o |= EXT3_MOUNT_##opt
#define test_opt(sb, opt)		(EXT3_SB(sb)->s_mount_opt & \
					 EXT3_MOUNT_##opt)
#else
#define EXT2_MOUNT_NOLOAD		EXT3_MOUNT_NOLOAD
#define EXT2_MOUNT_ABORT		EXT3_MOUNT_ABORT
#define EXT2_MOUNT_DATA_FLAGS		EXT3_MOUNT_DATA_FLAGS
#endif

#define ext3_set_bit			__set_bit_le
#define ext3_set_bit_atomic		ext2_set_bit_atomic
#define ext3_clear_bit			__clear_bit_le
#define ext3_clear_bit_atomic		ext2_clear_bit_atomic
#define ext3_test_bit			test_bit_le
#define ext3_find_next_zero_bit		find_next_zero_bit_le

/*
 * Maximal mount counts between two filesystem checks
 */
#define EXT3_DFL_MAX_MNT_COUNT		20	/* Allow 20 mounts */
#define EXT3_DFL_CHECKINTERVAL		0	/* Don't use interval check */

/*
 * Behaviour when detecting errors
 */
#define EXT3_ERRORS_CONTINUE		1	/* Continue execution */
#define EXT3_ERRORS_RO			2	/* Remount fs read-only */
#define EXT3_ERRORS_PANIC		3	/* Panic */
#define EXT3_ERRORS_DEFAULT		EXT3_ERRORS_CONTINUE

/*
 * Structure of the super block
 */
struct ext3_super_block {
/*00*/	__le32	s_inodes_count;		/* Inodes count */
	__le32	s_blocks_count;		/* Blocks count */
	__le32	s_r_blocks_count;	/* Reserved blocks count */
	__le32	s_free_blocks_count;	/* Free blocks count */
/*10*/	__le32	s_free_inodes_count;	/* Free inodes count */
	__le32	s_first_data_block;	/* First Data Block */
	__le32	s_log_block_size;	/* Block size */
	__le32	s_log_frag_size;	/* Fragment size */
/*20*/	__le32	s_blocks_per_group;	/* # Blocks per group */
	__le32	s_frags_per_group;	/* # Fragments per group */
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
	 * These fields are for EXT3_DYNAMIC_REV superblocks only.
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
	__le16   s_inode_size;		/* size of inode structure */
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
	 * happen if the EXT3_FEATURE_COMPAT_DIR_PREALLOC flag is on.
	 */
	__u8	s_prealloc_blocks;	/* Nr of blocks to try to preallocate*/
	__u8	s_prealloc_dir_blocks;	/* Nr to preallocate for dirs */
	__le16	s_reserved_gdt_blocks;	/* Per group desc for online growth */
	/*
	 * Journaling support valid if EXT3_FEATURE_COMPAT_HAS_JOURNAL set.
	 */
/*D0*/	__u8	s_journal_uuid[16];	/* uuid of journal superblock */
/*E0*/	__le32	s_journal_inum;		/* inode number of journal file */
	__le32	s_journal_dev;		/* device number of journal file */
	__le32	s_last_orphan;		/* start of list of inodes to delete */
	__le32	s_hash_seed[4];		/* HTREE hash seed */
	__u8	s_def_hash_version;	/* Default hash version to use */
	__u8	s_reserved_char_pad;
	__u16	s_reserved_word_pad;
	__le32	s_default_mount_opts;
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

/* data type for block offset of block group */
typedef int ext3_grpblk_t;

/* data type for filesystem-wide blocks number */
typedef unsigned long ext3_fsblk_t;

#define E3FSBLK "%lu"

struct ext3_reserve_window {
	ext3_fsblk_t	_rsv_start;	/* First byte reserved */
	ext3_fsblk_t	_rsv_end;	/* Last byte reserved or 0 */
};

struct ext3_reserve_window_node {
	struct rb_node		rsv_node;
	__u32			rsv_goal_size;
	__u32			rsv_alloc_hit;
	struct ext3_reserve_window	rsv_window;
};

struct ext3_block_alloc_info {
	/* information about reservation window */
	struct ext3_reserve_window_node	rsv_window_node;
	/*
	 * was i_next_alloc_block in ext3_inode_info
	 * is the logical (file-relative) number of the
	 * most-recently-allocated block in this file.
	 * We use this for detecting linearly ascending allocation requests.
	 */
	__u32                   last_alloc_logical_block;
	/*
	 * Was i_next_alloc_goal in ext3_inode_info
	 * is the *physical* companion to i_next_alloc_block.
	 * it the physical block number of the block which was most-recentl
	 * allocated to this file.  This give us the goal (target) for the next
	 * allocation when we detect linearly ascending requests.
	 */
	ext3_fsblk_t		last_alloc_physical_block;
};

#define rsv_start rsv_window._rsv_start
#define rsv_end rsv_window._rsv_end

/*
 * third extended file system inode data in memory
 */
struct ext3_inode_info {
	__le32	i_data[15];	/* unconverted */
	__u32	i_flags;
#ifdef EXT3_FRAGMENTS
	__u32	i_faddr;
	__u8	i_frag_no;
	__u8	i_frag_size;
#endif
	ext3_fsblk_t	i_file_acl;
	__u32	i_dir_acl;
	__u32	i_dtime;

	/*
	 * i_block_group is the number of the block group which contains
	 * this file's inode.  Constant across the lifetime of the inode,
	 * it is ued for making block allocation decisions - we try to
	 * place a file's data blocks near its inode block, and new inodes
	 * near to their parent directory's inode.
	 */
	__u32	i_block_group;
	unsigned long	i_state_flags;	/* Dynamic state flags for ext3 */

	/* block reservation info */
	struct ext3_block_alloc_info *i_block_alloc_info;

	__u32	i_dir_start_lookup;
#ifdef CONFIG_EXT3_FS_XATTR
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
	 * the VFS prior to calling ext3_truncate(), but the filesystem won't
	 * set i_disksize to 0 until the truncate is actually under way.
	 *
	 * The intent is that i_disksize always represents the blocks which
	 * are used by this file.  This allows recovery to restart truncate
	 * on orphans if we crash during truncate.  We actually write i_disksize
	 * into the on-disk inode when writing inodes out, instead of i_size.
	 *
	 * The only time when i_disksize and i_size may be different is when
	 * a truncate is in progress.  The only things which change i_disksize
	 * are ext3_get_block (growth) and ext3_truncate (shrinkth).
	 */
	loff_t	i_disksize;

	/* on-disk additional length */
	__u16 i_extra_isize;

	/*
	 * truncate_mutex is for serialising ext3_truncate() against
	 * ext3_getblock().  In the 2.4 ext2 design, great chunks of inode's
	 * data tree are chopped off during truncate. We can't do that in
	 * ext3 because whenever we perform intermediate commits during
	 * truncate, the inode and all the metadata blocks *must* be in a
	 * consistent state which allows truncation of the orphans to restart
	 * during recovery.  Hence we must fix the get_block-vs-truncate race
	 * by other means, so we have truncate_mutex.
	 */
	struct mutex truncate_mutex;

	/*
	 * Transactions that contain inode's metadata needed to complete
	 * fsync and fdatasync, respectively.
	 */
	atomic_t i_sync_tid;
	atomic_t i_datasync_tid;

#ifdef CONFIG_QUOTA
	struct dquot *i_dquot[MAXQUOTAS];
#endif

	struct inode vfs_inode;
};

/*
 * third extended-fs super-block data in memory
 */
struct ext3_sb_info {
	unsigned long s_frag_size;	/* Size of a fragment in bytes */
	unsigned long s_frags_per_block;/* Number of fragments per block */
	unsigned long s_inodes_per_block;/* Number of inodes per block */
	unsigned long s_frags_per_group;/* Number of fragments in a group */
	unsigned long s_blocks_per_group;/* Number of blocks in a group */
	unsigned long s_inodes_per_group;/* Number of inodes in a group */
	unsigned long s_itb_per_group;	/* Number of inode table blocks per group */
	unsigned long s_gdb_count;	/* Number of group descriptor blocks */
	unsigned long s_desc_per_block;	/* Number of group descriptors per block */
	unsigned long s_groups_count;	/* Number of groups in the fs */
	unsigned long s_overhead_last;  /* Last calculated overhead */
	unsigned long s_blocks_last;    /* Last seen block count */
	struct buffer_head * s_sbh;	/* Buffer containing the super block */
	struct ext3_super_block * s_es;	/* Pointer to the super block in the buffer */
	struct buffer_head ** s_group_desc;
	unsigned long  s_mount_opt;
	ext3_fsblk_t s_sb_block;
	kuid_t s_resuid;
	kgid_t s_resgid;
	unsigned short s_mount_state;
	unsigned short s_pad;
	int s_addr_per_block_bits;
	int s_desc_per_block_bits;
	int s_inode_size;
	int s_first_ino;
	spinlock_t s_next_gen_lock;
	u32 s_next_generation;
	u32 s_hash_seed[4];
	int s_def_hash_version;
	int s_hash_unsigned;	/* 3 if hash should be signed, 0 if not */
	struct percpu_counter s_freeblocks_counter;
	struct percpu_counter s_freeinodes_counter;
	struct percpu_counter s_dirs_counter;
	struct blockgroup_lock *s_blockgroup_lock;

	/* root of the per fs reservation window tree */
	spinlock_t s_rsv_window_lock;
	struct rb_root s_rsv_window_root;
	struct ext3_reserve_window_node s_rsv_window_head;

	/* Journaling */
	struct inode * s_journal_inode;
	struct journal_s * s_journal;
	struct list_head s_orphan;
	struct mutex s_orphan_lock;
	struct mutex s_resize_lock;
	unsigned long s_commit_interval;
	struct block_device *journal_bdev;
#ifdef CONFIG_QUOTA
	char *s_qf_names[EXT3_MAXQUOTAS];	/* Names of quota files with journalled quota */
	int s_jquota_fmt;			/* Format of quota to use */
#endif
};

static inline spinlock_t *
sb_bgl_lock(struct ext3_sb_info *sbi, unsigned int block_group)
{
	return bgl_lock_ptr(sbi->s_blockgroup_lock, block_group);
}

static inline struct ext3_sb_info * EXT3_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}
static inline struct ext3_inode_info *EXT3_I(struct inode *inode)
{
	return container_of(inode, struct ext3_inode_info, vfs_inode);
}

static inline int ext3_valid_inum(struct super_block *sb, unsigned long ino)
{
	return ino == EXT3_ROOT_INO ||
		ino == EXT3_JOURNAL_INO ||
		ino == EXT3_RESIZE_INO ||
		(ino >= EXT3_FIRST_INO(sb) &&
		 ino <= le32_to_cpu(EXT3_SB(sb)->s_es->s_inodes_count));
}

/*
 * Inode dynamic state flags
 */
enum {
	EXT3_STATE_JDATA,		/* journaled data exists */
	EXT3_STATE_NEW,			/* inode is newly created */
	EXT3_STATE_XATTR,		/* has in-inode xattrs */
	EXT3_STATE_FLUSH_ON_CLOSE,	/* flush dirty pages on close */
};

static inline int ext3_test_inode_state(struct inode *inode, int bit)
{
	return test_bit(bit, &EXT3_I(inode)->i_state_flags);
}

static inline void ext3_set_inode_state(struct inode *inode, int bit)
{
	set_bit(bit, &EXT3_I(inode)->i_state_flags);
}

static inline void ext3_clear_inode_state(struct inode *inode, int bit)
{
	clear_bit(bit, &EXT3_I(inode)->i_state_flags);
}

#define NEXT_ORPHAN(inode) EXT3_I(inode)->i_dtime

/*
 * Codes for operating systems
 */
#define EXT3_OS_LINUX		0
#define EXT3_OS_HURD		1
#define EXT3_OS_MASIX		2
#define EXT3_OS_FREEBSD		3
#define EXT3_OS_LITES		4

/*
 * Revision levels
 */
#define EXT3_GOOD_OLD_REV	0	/* The good old (original) format */
#define EXT3_DYNAMIC_REV	1	/* V2 format w/ dynamic inode sizes */

#define EXT3_CURRENT_REV	EXT3_GOOD_OLD_REV
#define EXT3_MAX_SUPP_REV	EXT3_DYNAMIC_REV

#define EXT3_GOOD_OLD_INODE_SIZE 128

/*
 * Feature set definitions
 */

#define EXT3_HAS_COMPAT_FEATURE(sb,mask)			\
	( EXT3_SB(sb)->s_es->s_feature_compat & cpu_to_le32(mask) )
#define EXT3_HAS_RO_COMPAT_FEATURE(sb,mask)			\
	( EXT3_SB(sb)->s_es->s_feature_ro_compat & cpu_to_le32(mask) )
#define EXT3_HAS_INCOMPAT_FEATURE(sb,mask)			\
	( EXT3_SB(sb)->s_es->s_feature_incompat & cpu_to_le32(mask) )
#define EXT3_SET_COMPAT_FEATURE(sb,mask)			\
	EXT3_SB(sb)->s_es->s_feature_compat |= cpu_to_le32(mask)
#define EXT3_SET_RO_COMPAT_FEATURE(sb,mask)			\
	EXT3_SB(sb)->s_es->s_feature_ro_compat |= cpu_to_le32(mask)
#define EXT3_SET_INCOMPAT_FEATURE(sb,mask)			\
	EXT3_SB(sb)->s_es->s_feature_incompat |= cpu_to_le32(mask)
#define EXT3_CLEAR_COMPAT_FEATURE(sb,mask)			\
	EXT3_SB(sb)->s_es->s_feature_compat &= ~cpu_to_le32(mask)
#define EXT3_CLEAR_RO_COMPAT_FEATURE(sb,mask)			\
	EXT3_SB(sb)->s_es->s_feature_ro_compat &= ~cpu_to_le32(mask)
#define EXT3_CLEAR_INCOMPAT_FEATURE(sb,mask)			\
	EXT3_SB(sb)->s_es->s_feature_incompat &= ~cpu_to_le32(mask)

#define EXT3_FEATURE_COMPAT_DIR_PREALLOC	0x0001
#define EXT3_FEATURE_COMPAT_IMAGIC_INODES	0x0002
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL		0x0004
#define EXT3_FEATURE_COMPAT_EXT_ATTR		0x0008
#define EXT3_FEATURE_COMPAT_RESIZE_INODE	0x0010
#define EXT3_FEATURE_COMPAT_DIR_INDEX		0x0020

#define EXT3_FEATURE_RO_COMPAT_SPARSE_SUPER	0x0001
#define EXT3_FEATURE_RO_COMPAT_LARGE_FILE	0x0002
#define EXT3_FEATURE_RO_COMPAT_BTREE_DIR	0x0004

#define EXT3_FEATURE_INCOMPAT_COMPRESSION	0x0001
#define EXT3_FEATURE_INCOMPAT_FILETYPE		0x0002
#define EXT3_FEATURE_INCOMPAT_RECOVER		0x0004 /* Needs recovery */
#define EXT3_FEATURE_INCOMPAT_JOURNAL_DEV	0x0008 /* Journal device */
#define EXT3_FEATURE_INCOMPAT_META_BG		0x0010

#define EXT3_FEATURE_COMPAT_SUPP	EXT2_FEATURE_COMPAT_EXT_ATTR
#define EXT3_FEATURE_INCOMPAT_SUPP	(EXT3_FEATURE_INCOMPAT_FILETYPE| \
					 EXT3_FEATURE_INCOMPAT_RECOVER| \
					 EXT3_FEATURE_INCOMPAT_META_BG)
#define EXT3_FEATURE_RO_COMPAT_SUPP	(EXT3_FEATURE_RO_COMPAT_SPARSE_SUPER| \
					 EXT3_FEATURE_RO_COMPAT_LARGE_FILE| \
					 EXT3_FEATURE_RO_COMPAT_BTREE_DIR)

/*
 * Default values for user and/or group using reserved blocks
 */
#define	EXT3_DEF_RESUID		0
#define	EXT3_DEF_RESGID		0

/*
 * Default mount options
 */
#define EXT3_DEFM_DEBUG		0x0001
#define EXT3_DEFM_BSDGROUPS	0x0002
#define EXT3_DEFM_XATTR_USER	0x0004
#define EXT3_DEFM_ACL		0x0008
#define EXT3_DEFM_UID16		0x0010
#define EXT3_DEFM_JMODE		0x0060
#define EXT3_DEFM_JMODE_DATA	0x0020
#define EXT3_DEFM_JMODE_ORDERED	0x0040
#define EXT3_DEFM_JMODE_WBACK	0x0060

/*
 * Structure of a directory entry
 */
#define EXT3_NAME_LEN 255

struct ext3_dir_entry {
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__le16	name_len;		/* Name length */
	char	name[EXT3_NAME_LEN];	/* File name */
};

/*
 * The new version of the directory entry.  Since EXT3 structures are
 * stored in intel byte order, and the name_len field could never be
 * bigger than 255 chars, it's safe to reclaim the extra byte for the
 * file_type field.
 */
struct ext3_dir_entry_2 {
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__u8	name_len;		/* Name length */
	__u8	file_type;
	char	name[EXT3_NAME_LEN];	/* File name */
};

/*
 * Ext3 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
#define EXT3_FT_UNKNOWN		0
#define EXT3_FT_REG_FILE	1
#define EXT3_FT_DIR		2
#define EXT3_FT_CHRDEV		3
#define EXT3_FT_BLKDEV		4
#define EXT3_FT_FIFO		5
#define EXT3_FT_SOCK		6
#define EXT3_FT_SYMLINK		7

#define EXT3_FT_MAX		8

/*
 * EXT3_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define EXT3_DIR_PAD			4
#define EXT3_DIR_ROUND			(EXT3_DIR_PAD - 1)
#define EXT3_DIR_REC_LEN(name_len)	(((name_len) + 8 + EXT3_DIR_ROUND) & \
					 ~EXT3_DIR_ROUND)
#define EXT3_MAX_REC_LEN		((1<<16)-1)

/*
 * Tests against MAX_REC_LEN etc were put in place for 64k block
 * sizes; if that is not possible on this arch, we can skip
 * those tests and speed things up.
 */
static inline unsigned ext3_rec_len_from_disk(__le16 dlen)
{
	unsigned len = le16_to_cpu(dlen);

#if (PAGE_CACHE_SIZE >= 65536)
	if (len == EXT3_MAX_REC_LEN)
		return 1 << 16;
#endif
	return len;
}

static inline __le16 ext3_rec_len_to_disk(unsigned len)
{
#if (PAGE_CACHE_SIZE >= 65536)
	if (len == (1 << 16))
		return cpu_to_le16(EXT3_MAX_REC_LEN);
	else if (len > (1 << 16))
		BUG();
#endif
	return cpu_to_le16(len);
}

/*
 * Hash Tree Directory indexing
 * (c) Daniel Phillips, 2001
 */

#define is_dx(dir) (EXT3_HAS_COMPAT_FEATURE(dir->i_sb, \
				      EXT3_FEATURE_COMPAT_DIR_INDEX) && \
		      (EXT3_I(dir)->i_flags & EXT3_INDEX_FL))
#define EXT3_DIR_LINK_MAX(dir) (!is_dx(dir) && (dir)->i_nlink >= EXT3_LINK_MAX)
#define EXT3_DIR_LINK_EMPTY(dir) ((dir)->i_nlink == 2 || (dir)->i_nlink == 1)

/* Legal values for the dx_root hash_version field: */

#define DX_HASH_LEGACY		0
#define DX_HASH_HALF_MD4	1
#define DX_HASH_TEA		2
#define DX_HASH_LEGACY_UNSIGNED	3
#define DX_HASH_HALF_MD4_UNSIGNED	4
#define DX_HASH_TEA_UNSIGNED		5

/* hash info structure used by the directory hash */
struct dx_hash_info
{
	u32		hash;
	u32		minor_hash;
	int		hash_version;
	u32		*seed;
};


/* 32 and 64 bit signed EOF for dx directories */
#define EXT3_HTREE_EOF_32BIT   ((1UL  << (32 - 1)) - 1)
#define EXT3_HTREE_EOF_64BIT   ((1ULL << (64 - 1)) - 1)


/*
 * Control parameters used by ext3_htree_next_block
 */
#define HASH_NB_ALWAYS		1


/*
 * Describe an inode's exact location on disk and in memory
 */
struct ext3_iloc
{
	struct buffer_head *bh;
	unsigned long offset;
	unsigned long block_group;
};

static inline struct ext3_inode *ext3_raw_inode(struct ext3_iloc *iloc)
{
	return (struct ext3_inode *) (iloc->bh->b_data + iloc->offset);
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
static inline ext3_fsblk_t
ext3_group_first_block_no(struct super_block *sb, unsigned long group_no)
{
	return group_no * (ext3_fsblk_t)EXT3_BLOCKS_PER_GROUP(sb) +
		le32_to_cpu(EXT3_SB(sb)->s_es->s_first_data_block);
}

/*
 * Special error return code only used by dx_probe() and its callers.
 */
#define ERR_BAD_DX_DIR	-75000

/*
 * Function prototypes
 */

/*
 * Ok, these declarations are also in <linux/kernel.h> but none of the
 * ext3 source programs needs to include it so they are duplicated here.
 */
# define NORET_TYPE    /**/
# define ATTRIB_NORET  __attribute__((noreturn))
# define NORET_AND     noreturn,

/* balloc.c */
extern int ext3_bg_has_super(struct super_block *sb, int group);
extern unsigned long ext3_bg_num_gdb(struct super_block *sb, int group);
extern ext3_fsblk_t ext3_new_block (handle_t *handle, struct inode *inode,
			ext3_fsblk_t goal, int *errp);
extern ext3_fsblk_t ext3_new_blocks (handle_t *handle, struct inode *inode,
			ext3_fsblk_t goal, unsigned long *count, int *errp);
extern void ext3_free_blocks (handle_t *handle, struct inode *inode,
			ext3_fsblk_t block, unsigned long count);
extern void ext3_free_blocks_sb (handle_t *handle, struct super_block *sb,
				 ext3_fsblk_t block, unsigned long count,
				unsigned long *pdquot_freed_blocks);
extern ext3_fsblk_t ext3_count_free_blocks (struct super_block *);
extern void ext3_check_blocks_bitmap (struct super_block *);
extern struct ext3_group_desc * ext3_get_group_desc(struct super_block * sb,
						    unsigned int block_group,
						    struct buffer_head ** bh);
extern int ext3_should_retry_alloc(struct super_block *sb, int *retries);
extern void ext3_init_block_alloc_info(struct inode *);
extern void ext3_rsv_window_add(struct super_block *sb, struct ext3_reserve_window_node *rsv);
extern int ext3_trim_fs(struct super_block *sb, struct fstrim_range *range);

/* dir.c */
extern int ext3_check_dir_entry(const char *, struct inode *,
				struct ext3_dir_entry_2 *,
				struct buffer_head *, unsigned long);
extern int ext3_htree_store_dirent(struct file *dir_file, __u32 hash,
				    __u32 minor_hash,
				    struct ext3_dir_entry_2 *dirent);
extern void ext3_htree_free_dir_info(struct dir_private_info *p);

/* fsync.c */
extern int ext3_sync_file(struct file *, loff_t, loff_t, int);

/* hash.c */
extern int ext3fs_dirhash(const char *name, int len, struct
			  dx_hash_info *hinfo);

/* ialloc.c */
extern struct inode * ext3_new_inode (handle_t *, struct inode *,
				      const struct qstr *, umode_t);
extern void ext3_free_inode (handle_t *, struct inode *);
extern struct inode * ext3_orphan_get (struct super_block *, unsigned long);
extern unsigned long ext3_count_free_inodes (struct super_block *);
extern unsigned long ext3_count_dirs (struct super_block *);
extern void ext3_check_inodes_bitmap (struct super_block *);
extern unsigned long ext3_count_free (struct buffer_head *, unsigned);


/* inode.c */
int ext3_forget(handle_t *handle, int is_metadata, struct inode *inode,
		struct buffer_head *bh, ext3_fsblk_t blocknr);
struct buffer_head * ext3_getblk (handle_t *, struct inode *, long, int, int *);
struct buffer_head * ext3_bread (handle_t *, struct inode *, int, int, int *);
int ext3_get_blocks_handle(handle_t *handle, struct inode *inode,
	sector_t iblock, unsigned long maxblocks, struct buffer_head *bh_result,
	int create);

extern struct inode *ext3_iget(struct super_block *, unsigned long);
extern int  ext3_write_inode (struct inode *, struct writeback_control *);
extern int  ext3_setattr (struct dentry *, struct iattr *);
extern void ext3_evict_inode (struct inode *);
extern int  ext3_sync_inode (handle_t *, struct inode *);
extern void ext3_discard_reservation (struct inode *);
extern void ext3_dirty_inode(struct inode *, int);
extern int ext3_change_inode_journal_flag(struct inode *, int);
extern int ext3_get_inode_loc(struct inode *, struct ext3_iloc *);
extern int ext3_can_truncate(struct inode *inode);
extern void ext3_truncate(struct inode *inode);
extern void ext3_set_inode_flags(struct inode *);
extern void ext3_get_inode_flags(struct ext3_inode_info *);
extern void ext3_set_aops(struct inode *inode);
extern int ext3_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		       u64 start, u64 len);

/* ioctl.c */
extern long ext3_ioctl(struct file *, unsigned int, unsigned long);
extern long ext3_compat_ioctl(struct file *, unsigned int, unsigned long);

/* namei.c */
extern int ext3_orphan_add(handle_t *, struct inode *);
extern int ext3_orphan_del(handle_t *, struct inode *);
extern int ext3_htree_fill_tree(struct file *dir_file, __u32 start_hash,
				__u32 start_minor_hash, __u32 *next_hash);

/* resize.c */
extern int ext3_group_add(struct super_block *sb,
				struct ext3_new_group_data *input);
extern int ext3_group_extend(struct super_block *sb,
				struct ext3_super_block *es,
				ext3_fsblk_t n_blocks_count);

/* super.c */
extern __printf(3, 4)
void ext3_error(struct super_block *, const char *, const char *, ...);
extern void __ext3_std_error (struct super_block *, const char *, int);
extern __printf(3, 4)
void ext3_abort(struct super_block *, const char *, const char *, ...);
extern __printf(3, 4)
void ext3_warning(struct super_block *, const char *, const char *, ...);
extern __printf(3, 4)
void ext3_msg(struct super_block *, const char *, const char *, ...);
extern void ext3_update_dynamic_rev (struct super_block *sb);

#define ext3_std_error(sb, errno)				\
do {								\
	if ((errno))						\
		__ext3_std_error((sb), __func__, (errno));	\
} while (0)

/*
 * Inodes and files operations
 */

/* dir.c */
extern const struct file_operations ext3_dir_operations;

/* file.c */
extern const struct inode_operations ext3_file_inode_operations;
extern const struct file_operations ext3_file_operations;

/* namei.c */
extern const struct inode_operations ext3_dir_inode_operations;
extern const struct inode_operations ext3_special_inode_operations;

/* symlink.c */
extern const struct inode_operations ext3_symlink_inode_operations;
extern const struct inode_operations ext3_fast_symlink_inode_operations;

#define EXT3_JOURNAL(inode)	(EXT3_SB((inode)->i_sb)->s_journal)

/* Define the number of blocks we need to account to a transaction to
 * modify one block of data.
 *
 * We may have to touch one inode, one bitmap buffer, up to three
 * indirection blocks, the group and superblock summaries, and the data
 * block to complete the transaction.  */

#define EXT3_SINGLEDATA_TRANS_BLOCKS	8U

/* Extended attribute operations touch at most two data buffers,
 * two bitmap buffers, and two group summaries, in addition to the inode
 * and the superblock, which are already accounted for. */

#define EXT3_XATTR_TRANS_BLOCKS		6U

/* Define the minimum size for a transaction which modifies data.  This
 * needs to take into account the fact that we may end up modifying two
 * quota files too (one for the group, one for the user quota).  The
 * superblock only gets updated once, of course, so don't bother
 * counting that again for the quota updates. */

#define EXT3_DATA_TRANS_BLOCKS(sb)	(EXT3_SINGLEDATA_TRANS_BLOCKS + \
					 EXT3_XATTR_TRANS_BLOCKS - 2 + \
					 EXT3_MAXQUOTAS_TRANS_BLOCKS(sb))

/* Delete operations potentially hit one directory's namespace plus an
 * entire inode, plus arbitrary amounts of bitmap/indirection data.  Be
 * generous.  We can grow the delete transaction later if necessary. */

#define EXT3_DELETE_TRANS_BLOCKS(sb)   (EXT3_MAXQUOTAS_TRANS_BLOCKS(sb) + 64)

/* Define an arbitrary limit for the amount of data we will anticipate
 * writing to any given transaction.  For unbounded transactions such as
 * write(2) and truncate(2) we can write more than this, but we always
 * start off at the maximum transaction size and grow the transaction
 * optimistically as we go. */

#define EXT3_MAX_TRANS_DATA		64U

/* We break up a large truncate or write transaction once the handle's
 * buffer credits gets this low, we need either to extend the
 * transaction or to start a new one.  Reserve enough space here for
 * inode, bitmap, superblock, group and indirection updates for at least
 * one block, plus two quota updates.  Quota allocations are not
 * needed. */

#define EXT3_RESERVE_TRANS_BLOCKS	12U

#define EXT3_INDEX_EXTRA_TRANS_BLOCKS	8

#ifdef CONFIG_QUOTA
/* Amount of blocks needed for quota update - we know that the structure was
 * allocated so we need to update only inode+data */
#define EXT3_QUOTA_TRANS_BLOCKS(sb) (test_opt(sb, QUOTA) ? 2 : 0)
/* Amount of blocks needed for quota insert/delete - we do some block writes
 * but inode, sb and group updates are done only once */
#define EXT3_QUOTA_INIT_BLOCKS(sb) (test_opt(sb, QUOTA) ? (DQUOT_INIT_ALLOC*\
		(EXT3_SINGLEDATA_TRANS_BLOCKS-3)+3+DQUOT_INIT_REWRITE) : 0)
#define EXT3_QUOTA_DEL_BLOCKS(sb) (test_opt(sb, QUOTA) ? (DQUOT_DEL_ALLOC*\
		(EXT3_SINGLEDATA_TRANS_BLOCKS-3)+3+DQUOT_DEL_REWRITE) : 0)
#else
#define EXT3_QUOTA_TRANS_BLOCKS(sb) 0
#define EXT3_QUOTA_INIT_BLOCKS(sb) 0
#define EXT3_QUOTA_DEL_BLOCKS(sb) 0
#endif
#define EXT3_MAXQUOTAS_TRANS_BLOCKS(sb) (EXT3_MAXQUOTAS*EXT3_QUOTA_TRANS_BLOCKS(sb))
#define EXT3_MAXQUOTAS_INIT_BLOCKS(sb) (EXT3_MAXQUOTAS*EXT3_QUOTA_INIT_BLOCKS(sb))
#define EXT3_MAXQUOTAS_DEL_BLOCKS(sb) (EXT3_MAXQUOTAS*EXT3_QUOTA_DEL_BLOCKS(sb))

int
ext3_mark_iloc_dirty(handle_t *handle,
		     struct inode *inode,
		     struct ext3_iloc *iloc);

/*
 * On success, We end up with an outstanding reference count against
 * iloc->bh.  This _must_ be cleaned up later.
 */

int ext3_reserve_inode_write(handle_t *handle, struct inode *inode,
			struct ext3_iloc *iloc);

int ext3_mark_inode_dirty(handle_t *handle, struct inode *inode);

/*
 * Wrapper functions with which ext3 calls into JBD.  The intent here is
 * to allow these to be turned into appropriate stubs so ext3 can control
 * ext2 filesystems, so ext2+ext3 systems only nee one fs.  This work hasn't
 * been done yet.
 */

static inline void ext3_journal_release_buffer(handle_t *handle,
						struct buffer_head *bh)
{
	journal_release_buffer(handle, bh);
}

void ext3_journal_abort_handle(const char *caller, const char *err_fn,
		struct buffer_head *bh, handle_t *handle, int err);

int __ext3_journal_get_undo_access(const char *where, handle_t *handle,
				struct buffer_head *bh);

int __ext3_journal_get_write_access(const char *where, handle_t *handle,
				struct buffer_head *bh);

int __ext3_journal_forget(const char *where, handle_t *handle,
				struct buffer_head *bh);

int __ext3_journal_revoke(const char *where, handle_t *handle,
				unsigned long blocknr, struct buffer_head *bh);

int __ext3_journal_get_create_access(const char *where,
				handle_t *handle, struct buffer_head *bh);

int __ext3_journal_dirty_metadata(const char *where,
				handle_t *handle, struct buffer_head *bh);

#define ext3_journal_get_undo_access(handle, bh) \
	__ext3_journal_get_undo_access(__func__, (handle), (bh))
#define ext3_journal_get_write_access(handle, bh) \
	__ext3_journal_get_write_access(__func__, (handle), (bh))
#define ext3_journal_revoke(handle, blocknr, bh) \
	__ext3_journal_revoke(__func__, (handle), (blocknr), (bh))
#define ext3_journal_get_create_access(handle, bh) \
	__ext3_journal_get_create_access(__func__, (handle), (bh))
#define ext3_journal_dirty_metadata(handle, bh) \
	__ext3_journal_dirty_metadata(__func__, (handle), (bh))
#define ext3_journal_forget(handle, bh) \
	__ext3_journal_forget(__func__, (handle), (bh))

int ext3_journal_dirty_data(handle_t *handle, struct buffer_head *bh);

handle_t *ext3_journal_start_sb(struct super_block *sb, int nblocks);
int __ext3_journal_stop(const char *where, handle_t *handle);

static inline handle_t *ext3_journal_start(struct inode *inode, int nblocks)
{
	return ext3_journal_start_sb(inode->i_sb, nblocks);
}

#define ext3_journal_stop(handle) \
	__ext3_journal_stop(__func__, (handle))

static inline handle_t *ext3_journal_current_handle(void)
{
	return journal_current_handle();
}

static inline int ext3_journal_extend(handle_t *handle, int nblocks)
{
	return journal_extend(handle, nblocks);
}

static inline int ext3_journal_restart(handle_t *handle, int nblocks)
{
	return journal_restart(handle, nblocks);
}

static inline int ext3_journal_blocks_per_page(struct inode *inode)
{
	return journal_blocks_per_page(inode);
}

static inline int ext3_journal_force_commit(journal_t *journal)
{
	return journal_force_commit(journal);
}

/* super.c */
int ext3_force_commit(struct super_block *sb);

static inline int ext3_should_journal_data(struct inode *inode)
{
	if (!S_ISREG(inode->i_mode))
		return 1;
	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT3_MOUNT_JOURNAL_DATA)
		return 1;
	if (EXT3_I(inode)->i_flags & EXT3_JOURNAL_DATA_FL)
		return 1;
	return 0;
}

static inline int ext3_should_order_data(struct inode *inode)
{
	if (!S_ISREG(inode->i_mode))
		return 0;
	if (EXT3_I(inode)->i_flags & EXT3_JOURNAL_DATA_FL)
		return 0;
	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT3_MOUNT_ORDERED_DATA)
		return 1;
	return 0;
}

static inline int ext3_should_writeback_data(struct inode *inode)
{
	if (!S_ISREG(inode->i_mode))
		return 0;
	if (EXT3_I(inode)->i_flags & EXT3_JOURNAL_DATA_FL)
		return 0;
	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT3_MOUNT_WRITEBACK_DATA)
		return 1;
	return 0;
}

#include <trace/events/ext3.h>

/*
 *  ext4_i.h
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/include/linux/minix_fs_i.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef _EXT4_I
#define _EXT4_I

#include <linux/rwsem.h>
#include <linux/rbtree.h>
#include <linux/seqlock.h>
#include <linux/mutex.h>

/* data type for block offset of block group */
typedef int ext4_grpblk_t;

/* data type for filesystem-wide blocks number */
typedef unsigned long long ext4_fsblk_t;

/* data type for file logical block number */
typedef __u32 ext4_lblk_t;

/* data type for block group number */
typedef unsigned long ext4_group_t;

struct ext4_reserve_window {
	ext4_fsblk_t	_rsv_start;	/* First byte reserved */
	ext4_fsblk_t	_rsv_end;	/* Last byte reserved or 0 */
};

struct ext4_reserve_window_node {
	struct rb_node		rsv_node;
	__u32			rsv_goal_size;
	__u32			rsv_alloc_hit;
	struct ext4_reserve_window	rsv_window;
};

struct ext4_block_alloc_info {
	/* information about reservation window */
	struct ext4_reserve_window_node rsv_window_node;
	/*
	 * was i_next_alloc_block in ext4_inode_info
	 * is the logical (file-relative) number of the
	 * most-recently-allocated block in this file.
	 * We use this for detecting linearly ascending allocation requests.
	 */
	ext4_lblk_t last_alloc_logical_block;
	/*
	 * Was i_next_alloc_goal in ext4_inode_info
	 * is the *physical* companion to i_next_alloc_block.
	 * it the physical block number of the block which was most-recentl
	 * allocated to this file.  This give us the goal (target) for the next
	 * allocation when we detect linearly ascending requests.
	 */
	ext4_fsblk_t last_alloc_physical_block;
};

#define rsv_start rsv_window._rsv_start
#define rsv_end rsv_window._rsv_end

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
 * third extended file system inode data in memory
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

	/* block reservation info */
	struct ext4_block_alloc_info *i_block_alloc_info;

	ext4_lblk_t		i_dir_start_lookup;
#ifdef CONFIG_EXT4DEV_FS_XATTR
	/*
	 * Extended attributes can be read independently of the main file
	 * data. Taking i_mutex even when reading would cause contention
	 * between readers of EAs and writers of regular file data, so
	 * instead we synchronize on xattr_sem when reading or changing
	 * EAs.
	 */
	struct rw_semaphore xattr_sem;
#endif
#ifdef CONFIG_EXT4DEV_FS_POSIX_ACL
	struct posix_acl	*i_acl;
	struct posix_acl	*i_default_acl;
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

	/* on-disk additional length */
	__u16 i_extra_isize;

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

	unsigned long i_ext_generation;
	struct ext4_ext_cache i_cached_extent;
	/*
	 * File creation time. Its function is same as that of
	 * struct timespec i_{a,c,m}time in the generic inode.
	 */
	struct timespec i_crtime;

	/* mballoc */
	struct list_head i_prealloc_list;
	spinlock_t i_prealloc_lock;
};

#endif	/* _EXT4_I */

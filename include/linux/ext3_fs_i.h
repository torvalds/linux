/*
 *  linux/include/linux/ext3_fs_i.h
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

#ifndef _LINUX_EXT3_FS_I
#define _LINUX_EXT3_FS_I

#include <linux/rwsem.h>
#include <linux/rbtree.h>
#include <linux/seqlock.h>

struct ext3_reserve_window {
	__u32			_rsv_start;	/* First byte reserved */
	__u32			_rsv_end;	/* Last byte reserved or 0 */
};

struct ext3_reserve_window_node {
	struct rb_node	 	rsv_node;
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
	 * it the the physical block number of the block which was most-recentl
	 * allocated to this file.  This give us the goal (target) for the next
	 * allocation when we detect linearly ascending requests.
	 */
	__u32                   last_alloc_physical_block;
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
	__u32	i_file_acl;
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
	__u32	i_state;		/* Dynamic state flags for ext3 */

	/* block reservation info */
	struct ext3_block_alloc_info *i_block_alloc_info;

	__u32	i_dir_start_lookup;
#ifdef CONFIG_EXT3_FS_XATTR
	/*
	 * Extended attributes can be read independently of the main file
	 * data. Taking i_sem even when reading would cause contention
	 * between readers of EAs and writers of regular file data, so
	 * instead we synchronize on xattr_sem when reading or changing
	 * EAs.
	 */
	struct rw_semaphore xattr_sem;
#endif
#ifdef CONFIG_EXT3_FS_POSIX_ACL
	struct posix_acl	*i_acl;
	struct posix_acl	*i_default_acl;
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
	 * truncate_sem is for serialising ext3_truncate() against
	 * ext3_getblock().  In the 2.4 ext2 design, great chunks of inode's
	 * data tree are chopped off during truncate. We can't do that in
	 * ext3 because whenever we perform intermediate commits during
	 * truncate, the inode and all the metadata blocks *must* be in a
	 * consistent state which allows truncation of the orphans to restart
	 * during recovery.  Hence we must fix the get_block-vs-truncate race
	 * by other means, so we have truncate_sem.
	 */
	struct semaphore truncate_sem;
	struct inode vfs_inode;
};

#endif	/* _LINUX_EXT3_FS_I */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_INODE_H
#define BTRFS_INODE_H

#include <linux/hash.h>
#include <linux/refcount.h>
#include "extent_map.h"
#include "extent_io.h"
#include "ordered-data.h"
#include "delayed-inode.h"

/*
 * Since we search a directory based on f_pos (struct dir_context::pos) we have
 * to start at 2 since '.' and '..' have f_pos of 0 and 1 respectively, so
 * everybody else has to start at 2 (see btrfs_real_readdir() and dir_emit_dots()).
 */
#define BTRFS_DIR_START_INDEX 2

/*
 * ordered_data_close is set by truncate when a file that used
 * to have good data has been truncated to zero.  When it is set
 * the btrfs file release call will add this inode to the
 * ordered operations list so that we make sure to flush out any
 * new data the application may have written before commit.
 */
enum {
	BTRFS_INODE_FLUSH_ON_CLOSE,
	BTRFS_INODE_DUMMY,
	BTRFS_INODE_IN_DEFRAG,
	BTRFS_INODE_HAS_ASYNC_EXTENT,
	 /*
	  * Always set under the VFS' inode lock, otherwise it can cause races
	  * during fsync (we start as a fast fsync and then end up in a full
	  * fsync racing with ordered extent completion).
	  */
	BTRFS_INODE_NEEDS_FULL_SYNC,
	BTRFS_INODE_COPY_EVERYTHING,
	BTRFS_INODE_IN_DELALLOC_LIST,
	BTRFS_INODE_HAS_PROPS,
	BTRFS_INODE_SNAPSHOT_FLUSH,
	/*
	 * Set and used when logging an inode and it serves to signal that an
	 * inode does not have xattrs, so subsequent fsyncs can avoid searching
	 * for xattrs to log. This bit must be cleared whenever a xattr is added
	 * to an inode.
	 */
	BTRFS_INODE_NO_XATTRS,
	/*
	 * Set when we are in a context where we need to start a transaction and
	 * have dirty pages with the respective file range locked. This is to
	 * ensure that when reserving space for the transaction, if we are low
	 * on available space and need to flush delalloc, we will not flush
	 * delalloc for this inode, because that could result in a deadlock (on
	 * the file range, inode's io_tree).
	 */
	BTRFS_INODE_NO_DELALLOC_FLUSH,
	/*
	 * Set when we are working on enabling verity for a file. Computing and
	 * writing the whole Merkle tree can take a while so we want to prevent
	 * races where two separate tasks attempt to simultaneously start verity
	 * on the same file.
	 */
	BTRFS_INODE_VERITY_IN_PROGRESS,
};

/* in memory btrfs inode */
struct btrfs_inode {
	/* which subvolume this inode belongs to */
	struct btrfs_root *root;

	/* key used to find this inode on disk.  This is used by the code
	 * to read in roots of subvolumes
	 */
	struct btrfs_key location;

	/*
	 * Lock for counters and all fields used to determine if the inode is in
	 * the log or not (last_trans, last_sub_trans, last_log_commit,
	 * logged_trans), to access/update new_delalloc_bytes and to update the
	 * VFS' inode number of bytes used.
	 */
	spinlock_t lock;

	/* the extent_tree has caches of all the extent mappings to disk */
	struct extent_map_tree extent_tree;

	/* the io_tree does range state (DIRTY, LOCKED etc) */
	struct extent_io_tree io_tree;

	/* special utility tree used to record which mirrors have already been
	 * tried when checksums fail for a given block
	 */
	struct extent_io_tree io_failure_tree;

	/*
	 * Keep track of where the inode has extent items mapped in order to
	 * make sure the i_size adjustments are accurate
	 */
	struct extent_io_tree file_extent_tree;

	/* held while logging the inode in tree-log.c */
	struct mutex log_mutex;

	/* used to order data wrt metadata */
	struct btrfs_ordered_inode_tree ordered_tree;

	/* list of all the delalloc inodes in the FS.  There are times we need
	 * to write all the delalloc pages to disk, and this list is used
	 * to walk them all.
	 */
	struct list_head delalloc_inodes;

	/* node for the red-black tree that links inodes in subvolume root */
	struct rb_node rb_node;

	unsigned long runtime_flags;

	/* Keep track of who's O_SYNC/fsyncing currently */
	atomic_t sync_writers;

	/* full 64 bit generation number, struct vfs_inode doesn't have a big
	 * enough field for this.
	 */
	u64 generation;

	/*
	 * transid of the trans_handle that last modified this inode
	 */
	u64 last_trans;

	/*
	 * transid that last logged this inode
	 */
	u64 logged_trans;

	/*
	 * log transid when this inode was last modified
	 */
	int last_sub_trans;

	/* a local copy of root's last_log_commit */
	int last_log_commit;

	/*
	 * Total number of bytes pending delalloc, used by stat to calculate the
	 * real block usage of the file. This is used only for files.
	 */
	u64 delalloc_bytes;

	union {
		/*
		 * Total number of bytes pending delalloc that fall within a file
		 * range that is either a hole or beyond EOF (and no prealloc extent
		 * exists in the range). This is always <= delalloc_bytes and this
		 * is used only for files.
		 */
		u64 new_delalloc_bytes;
		/*
		 * The offset of the last dir index key that was logged.
		 * This is used only for directories.
		 */
		u64 last_dir_index_offset;
	};

	/*
	 * total number of bytes pending defrag, used by stat to check whether
	 * it needs COW.
	 */
	u64 defrag_bytes;

	/*
	 * the size of the file stored in the metadata on disk.  data=ordered
	 * means the in-memory i_size might be larger than the size on disk
	 * because not all the blocks are written yet.
	 */
	u64 disk_i_size;

	/*
	 * If this is a directory then index_cnt is the counter for the index
	 * number for new files that are created. For an empty directory, this
	 * must be initialized to BTRFS_DIR_START_INDEX.
	 */
	u64 index_cnt;

	/* Cache the directory index number to speed the dir/file remove */
	u64 dir_index;

	/* the fsync log has some corner cases that mean we have to check
	 * directories to see if any unlinks have been done before
	 * the directory was logged.  See tree-log.c for all the
	 * details
	 */
	u64 last_unlink_trans;

	/*
	 * The id/generation of the last transaction where this inode was
	 * either the source or the destination of a clone/dedupe operation.
	 * Used when logging an inode to know if there are shared extents that
	 * need special care when logging checksum items, to avoid duplicate
	 * checksum items in a log (which can lead to a corruption where we end
	 * up with missing checksum ranges after log replay).
	 * Protected by the vfs inode lock.
	 */
	u64 last_reflink_trans;

	/*
	 * Number of bytes outstanding that are going to need csums.  This is
	 * used in ENOSPC accounting.
	 */
	u64 csum_bytes;

	/* Backwards incompatible flags, lower half of inode_item::flags  */
	u32 flags;
	/* Read-only compatibility flags, upper half of inode_item::flags */
	u32 ro_flags;

	/*
	 * Counters to keep track of the number of extent item's we may use due
	 * to delalloc and such.  outstanding_extents is the number of extent
	 * items we think we'll end up using, and reserved_extents is the number
	 * of extent items we've reserved metadata for.
	 */
	unsigned outstanding_extents;

	struct btrfs_block_rsv block_rsv;

	/*
	 * Cached values of inode properties
	 */
	unsigned prop_compress;		/* per-file compression algorithm */
	/*
	 * Force compression on the file using the defrag ioctl, could be
	 * different from prop_compress and takes precedence if set
	 */
	unsigned defrag_compress;

	struct btrfs_delayed_node *delayed_node;

	/* File creation time. */
	struct timespec64 i_otime;

	/* Hook into fs_info->delayed_iputs */
	struct list_head delayed_iput;

	struct rw_semaphore i_mmap_lock;
	struct inode vfs_inode;
};

static inline u32 btrfs_inode_sectorsize(const struct btrfs_inode *inode)
{
	return inode->root->fs_info->sectorsize;
}

static inline struct btrfs_inode *BTRFS_I(const struct inode *inode)
{
	return container_of(inode, struct btrfs_inode, vfs_inode);
}

static inline unsigned long btrfs_inode_hash(u64 objectid,
					     const struct btrfs_root *root)
{
	u64 h = objectid ^ (root->root_key.objectid * GOLDEN_RATIO_PRIME);

#if BITS_PER_LONG == 32
	h = (h >> 32) ^ (h & 0xffffffff);
#endif

	return (unsigned long)h;
}

static inline void btrfs_insert_inode_hash(struct inode *inode)
{
	unsigned long h = btrfs_inode_hash(inode->i_ino, BTRFS_I(inode)->root);

	__insert_inode_hash(inode, h);
}

#if BITS_PER_LONG == 32

/*
 * On 32 bit systems the i_ino of struct inode is 32 bits (unsigned long), so
 * we use the inode's location objectid which is a u64 to avoid truncation.
 */
static inline u64 btrfs_ino(const struct btrfs_inode *inode)
{
	u64 ino = inode->location.objectid;

	/* type == BTRFS_ROOT_ITEM_KEY: subvol dir */
	if (inode->location.type == BTRFS_ROOT_ITEM_KEY)
		ino = inode->vfs_inode.i_ino;
	return ino;
}

#else

static inline u64 btrfs_ino(const struct btrfs_inode *inode)
{
	return inode->vfs_inode.i_ino;
}

#endif

static inline void btrfs_i_size_write(struct btrfs_inode *inode, u64 size)
{
	i_size_write(&inode->vfs_inode, size);
	inode->disk_i_size = size;
}

static inline bool btrfs_is_free_space_inode(struct btrfs_inode *inode)
{
	struct btrfs_root *root = inode->root;

	if (root == root->fs_info->tree_root &&
	    btrfs_ino(inode) != BTRFS_BTREE_INODE_OBJECTID)
		return true;

	return false;
}

static inline bool is_data_inode(struct inode *inode)
{
	return btrfs_ino(BTRFS_I(inode)) != BTRFS_BTREE_INODE_OBJECTID;
}

static inline void btrfs_mod_outstanding_extents(struct btrfs_inode *inode,
						 int mod)
{
	lockdep_assert_held(&inode->lock);
	inode->outstanding_extents += mod;
	if (btrfs_is_free_space_inode(inode))
		return;
	trace_btrfs_inode_mod_outstanding_extents(inode->root, btrfs_ino(inode),
						  mod);
}

/*
 * Called every time after doing a buffered, direct IO or memory mapped write.
 *
 * This is to ensure that if we write to a file that was previously fsynced in
 * the current transaction, then try to fsync it again in the same transaction,
 * we will know that there were changes in the file and that it needs to be
 * logged.
 */
static inline void btrfs_set_inode_last_sub_trans(struct btrfs_inode *inode)
{
	spin_lock(&inode->lock);
	inode->last_sub_trans = inode->root->log_transid;
	spin_unlock(&inode->lock);
}

/*
 * Should be called while holding the inode's VFS lock in exclusive mode or in a
 * context where no one else can access the inode concurrently (during inode
 * creation or when loading an inode from disk).
 */
static inline void btrfs_set_inode_full_sync(struct btrfs_inode *inode)
{
	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &inode->runtime_flags);
	/*
	 * The inode may have been part of a reflink operation in the last
	 * transaction that modified it, and then a fsync has reset the
	 * last_reflink_trans to avoid subsequent fsyncs in the same
	 * transaction to do unnecessary work. So update last_reflink_trans
	 * to the last_trans value (we have to be pessimistic and assume a
	 * reflink happened).
	 *
	 * The ->last_trans is protected by the inode's spinlock and we can
	 * have a concurrent ordered extent completion update it. Also set
	 * last_reflink_trans to ->last_trans only if the former is less than
	 * the later, because we can be called in a context where
	 * last_reflink_trans was set to the current transaction generation
	 * while ->last_trans was not yet updated in the current transaction,
	 * and therefore has a lower value.
	 */
	spin_lock(&inode->lock);
	if (inode->last_reflink_trans < inode->last_trans)
		inode->last_reflink_trans = inode->last_trans;
	spin_unlock(&inode->lock);
}

static inline bool btrfs_inode_in_log(struct btrfs_inode *inode, u64 generation)
{
	bool ret = false;

	spin_lock(&inode->lock);
	if (inode->logged_trans == generation &&
	    inode->last_sub_trans <= inode->last_log_commit &&
	    inode->last_sub_trans <= inode->root->last_log_commit)
		ret = true;
	spin_unlock(&inode->lock);
	return ret;
}

/*
 * Check if the inode has flags compatible with compression
 */
static inline bool btrfs_inode_can_compress(const struct btrfs_inode *inode)
{
	if (inode->flags & BTRFS_INODE_NODATACOW ||
	    inode->flags & BTRFS_INODE_NODATASUM)
		return false;
	return true;
}

/*
 * btrfs_inode_item stores flags in a u64, btrfs_inode stores them in two
 * separate u32s. These two functions convert between the two representations.
 */
static inline u64 btrfs_inode_combine_flags(u32 flags, u32 ro_flags)
{
	return (flags | ((u64)ro_flags << 32));
}

static inline void btrfs_inode_split_flags(u64 inode_item_flags,
					   u32 *flags, u32 *ro_flags)
{
	*flags = (u32)inode_item_flags;
	*ro_flags = (u32)(inode_item_flags >> 32);
}

/* Array of bytes with variable length, hexadecimal format 0x1234 */
#define CSUM_FMT				"0x%*phN"
#define CSUM_FMT_VALUE(size, bytes)		size, bytes

static inline void btrfs_print_data_csum_error(struct btrfs_inode *inode,
		u64 logical_start, u8 *csum, u8 *csum_expected, int mirror_num)
{
	struct btrfs_root *root = inode->root;
	const u32 csum_size = root->fs_info->csum_size;

	/* Output minus objectid, which is more meaningful */
	if (root->root_key.objectid >= BTRFS_LAST_FREE_OBJECTID)
		btrfs_warn_rl(root->fs_info,
"csum failed root %lld ino %lld off %llu csum " CSUM_FMT " expected csum " CSUM_FMT " mirror %d",
			root->root_key.objectid, btrfs_ino(inode),
			logical_start,
			CSUM_FMT_VALUE(csum_size, csum),
			CSUM_FMT_VALUE(csum_size, csum_expected),
			mirror_num);
	else
		btrfs_warn_rl(root->fs_info,
"csum failed root %llu ino %llu off %llu csum " CSUM_FMT " expected csum " CSUM_FMT " mirror %d",
			root->root_key.objectid, btrfs_ino(inode),
			logical_start,
			CSUM_FMT_VALUE(csum_size, csum),
			CSUM_FMT_VALUE(csum_size, csum_expected),
			mirror_num);
}

#endif

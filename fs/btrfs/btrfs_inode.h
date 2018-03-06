/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __BTRFS_I__
#define __BTRFS_I__

#include <linux/hash.h>
#include "extent_map.h"
#include "extent_io.h"
#include "ordered-data.h"
#include "delayed-inode.h"

/*
 * ordered_data_close is set by truncate when a file that used
 * to have good data has been truncated to zero.  When it is set
 * the btrfs file release call will add this inode to the
 * ordered operations list so that we make sure to flush out any
 * new data the application may have written before commit.
 */
#define BTRFS_INODE_ORDERED_DATA_CLOSE		0
#define BTRFS_INODE_ORPHAN_META_RESERVED	1
#define BTRFS_INODE_DUMMY			2
#define BTRFS_INODE_IN_DEFRAG			3
#define BTRFS_INODE_HAS_ORPHAN_ITEM		4
#define BTRFS_INODE_HAS_ASYNC_EXTENT		5
#define BTRFS_INODE_NEEDS_FULL_SYNC		6
#define BTRFS_INODE_COPY_EVERYTHING		7
#define BTRFS_INODE_IN_DELALLOC_LIST		8
#define BTRFS_INODE_READDIO_NEED_LOCK		9
#define BTRFS_INODE_HAS_PROPS		        10

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
	 * logged_trans).
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

	/* held while logging the inode in tree-log.c */
	struct mutex log_mutex;

	/* held while doing delalloc reservations */
	struct mutex delalloc_mutex;

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

	/* total number of bytes pending delalloc, used by stat to calc the
	 * real block usage of the file
	 */
	u64 delalloc_bytes;

	/*
	 * Total number of bytes pending delalloc that fall within a file
	 * range that is either a hole or beyond EOF (and no prealloc extent
	 * exists in the range). This is always <= delalloc_bytes.
	 */
	u64 new_delalloc_bytes;

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
	 * if this is a directory then index_cnt is the counter for the index
	 * number for new files that are created
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
	 * Number of bytes outstanding that are going to need csums.  This is
	 * used in ENOSPC accounting.
	 */
	u64 csum_bytes;

	/* flags field from the on disk inode */
	u32 flags;

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
	struct timespec i_otime;

	/* Hook into fs_info->delayed_iputs */
	struct list_head delayed_iput;

	/*
	 * To avoid races between lockless (i_mutex not held) direct IO writes
	 * and concurrent fsync requests. Direct IO writes must acquire read
	 * access on this semaphore for creating an extent map and its
	 * corresponding ordered extent. The fast fsync path must acquire write
	 * access on this semaphore before it collects ordered extents and
	 * extent maps.
	 */
	struct rw_semaphore dio_sem;

	struct inode vfs_inode;
};

extern unsigned char btrfs_filetype_table[];

static inline struct btrfs_inode *BTRFS_I(const struct inode *inode)
{
	return container_of(inode, struct btrfs_inode, vfs_inode);
}

static inline unsigned long btrfs_inode_hash(u64 objectid,
					     const struct btrfs_root *root)
{
	u64 h = objectid ^ (root->objectid * GOLDEN_RATIO_PRIME);

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

static inline u64 btrfs_ino(const struct btrfs_inode *inode)
{
	u64 ino = inode->location.objectid;

	/*
	 * !ino: btree_inode
	 * type == BTRFS_ROOT_ITEM_KEY: subvol dir
	 */
	if (!ino || inode->location.type == BTRFS_ROOT_ITEM_KEY)
		ino = inode->vfs_inode.i_ino;
	return ino;
}

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
	if (inode->location.objectid == BTRFS_FREE_INO_OBJECTID)
		return true;
	return false;
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

static inline int btrfs_inode_in_log(struct btrfs_inode *inode, u64 generation)
{
	int ret = 0;

	spin_lock(&inode->lock);
	if (inode->logged_trans == generation &&
	    inode->last_sub_trans <= inode->last_log_commit &&
	    inode->last_sub_trans <= inode->root->last_log_commit) {
		/*
		 * After a ranged fsync we might have left some extent maps
		 * (that fall outside the fsync's range). So return false
		 * here if the list isn't empty, to make sure btrfs_log_inode()
		 * will be called and process those extent maps.
		 */
		smp_mb();
		if (list_empty(&inode->extent_tree.modified_extents))
			ret = 1;
	}
	spin_unlock(&inode->lock);
	return ret;
}

#define BTRFS_DIO_ORIG_BIO_SUBMITTED	0x1

struct btrfs_dio_private {
	struct inode *inode;
	unsigned long flags;
	u64 logical_offset;
	u64 disk_bytenr;
	u64 bytes;
	void *private;

	/* number of bios pending for this dio */
	atomic_t pending_bios;

	/* IO errors */
	int errors;

	/* orig_bio is our btrfs_io_bio */
	struct bio *orig_bio;

	/* dio_bio came from fs/direct-io.c */
	struct bio *dio_bio;

	/*
	 * The original bio may be split to several sub-bios, this is
	 * done during endio of sub-bios
	 */
	blk_status_t (*subio_endio)(struct inode *, struct btrfs_io_bio *,
			blk_status_t);
};

/*
 * Disable DIO read nolock optimization, so new dio readers will be forced
 * to grab i_mutex. It is used to avoid the endless truncate due to
 * nonlocked dio read.
 */
static inline void btrfs_inode_block_unlocked_dio(struct btrfs_inode *inode)
{
	set_bit(BTRFS_INODE_READDIO_NEED_LOCK, &inode->runtime_flags);
	smp_mb();
}

static inline void btrfs_inode_resume_unlocked_dio(struct btrfs_inode *inode)
{
	smp_mb__before_atomic();
	clear_bit(BTRFS_INODE_READDIO_NEED_LOCK, &inode->runtime_flags);
}

static inline void btrfs_print_data_csum_error(struct btrfs_inode *inode,
		u64 logical_start, u32 csum, u32 csum_expected, int mirror_num)
{
	struct btrfs_root *root = inode->root;

	/* Output minus objectid, which is more meaningful */
	if (root->objectid >= BTRFS_LAST_FREE_OBJECTID)
		btrfs_warn_rl(root->fs_info,
	"csum failed root %lld ino %lld off %llu csum 0x%08x expected csum 0x%08x mirror %d",
			root->objectid, btrfs_ino(inode),
			logical_start, csum, csum_expected, mirror_num);
	else
		btrfs_warn_rl(root->fs_info,
	"csum failed root %llu ino %llu off %llu csum 0x%08x expected csum 0x%08x mirror %d",
			root->objectid, btrfs_ino(inode),
			logical_start, csum, csum_expected, mirror_num);
}

static inline bool btrfs_page_exists_in_range(struct inode *inode,
						loff_t start, loff_t end)
{
	return filemap_range_has_page(inode->i_mapping, start, end);
}

#endif

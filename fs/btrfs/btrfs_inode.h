/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_INODE_H
#define BTRFS_INODE_H

#include <linux/hash.h>
#include <linux/refcount.h>
#include <linux/fscrypt.h>
#include <trace/events/btrfs.h>
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
	/* Set when this inode is a free space inode. */
	BTRFS_INODE_FREE_SPACE_INODE,
};

/* in memory btrfs inode */
struct btrfs_inode {
	/* which subvolume this inode belongs to */
	struct btrfs_root *root;

	/* key used to find this inode on disk.  This is used by the code
	 * to read in roots of subvolumes
	 */
	struct btrfs_key location;

	/* Cached value of inode property 'compression'. */
	u8 prop_compress;

	/*
	 * Force compression on the file using the defrag ioctl, could be
	 * different from prop_compress and takes precedence if set.
	 */
	u8 defrag_compress;

	/*
	 * Lock for counters and all fields used to determine if the inode is in
	 * the log or not (last_trans, last_sub_trans, last_log_commit,
	 * logged_trans), to access/update delalloc_bytes, new_delalloc_bytes,
	 * defrag_bytes, disk_i_size, outstanding_extents, csum_bytes and to
	 * update the VFS' inode number of bytes used.
	 */
	spinlock_t lock;

	/* the extent_tree has caches of all the extent mappings to disk */
	struct extent_map_tree extent_tree;

	/* the io_tree does range state (DIRTY, LOCKED etc) */
	struct extent_io_tree io_tree;

	/*
	 * Keep track of where the inode has extent items mapped in order to
	 * make sure the i_size adjustments are accurate
	 */
	struct extent_io_tree file_extent_tree;

	/* held while logging the inode in tree-log.c */
	struct mutex log_mutex;

	/*
	 * Counters to keep track of the number of extent item's we may use due
	 * to delalloc and such.  outstanding_extents is the number of extent
	 * items we think we'll end up using, and reserved_extents is the number
	 * of extent items we've reserved metadata for. Protected by 'lock'.
	 */
	unsigned outstanding_extents;

	/* used to order data wrt metadata */
	spinlock_t ordered_tree_lock;
	struct rb_root ordered_tree;
	struct rb_node *ordered_tree_last;

	/* list of all the delalloc inodes in the FS.  There are times we need
	 * to write all the delalloc pages to disk, and this list is used
	 * to walk them all.
	 */
	struct list_head delalloc_inodes;

	/* node for the red-black tree that links inodes in subvolume root */
	struct rb_node rb_node;

	unsigned long runtime_flags;

	/* full 64 bit generation number, struct vfs_inode doesn't have a big
	 * enough field for this.
	 */
	u64 generation;

	/*
	 * ID of the transaction handle that last modified this inode.
	 * Protected by 'lock'.
	 */
	u64 last_trans;

	/*
	 * ID of the transaction that last logged this inode.
	 * Protected by 'lock'.
	 */
	u64 logged_trans;

	/*
	 * Log transaction ID when this inode was last modified.
	 * Protected by 'lock'.
	 */
	int last_sub_trans;

	/* A local copy of root's last_log_commit. Protected by 'lock'. */
	int last_log_commit;

	union {
		/*
		 * Total number of bytes pending delalloc, used by stat to
		 * calculate the real block usage of the file. This is used
		 * only for files. Protected by 'lock'.
		 */
		u64 delalloc_bytes;
		/*
		 * The lowest possible index of the next dir index key which
		 * points to an inode that needs to be logged.
		 * This is used only for directories.
		 * Use the helpers btrfs_get_first_dir_index_to_log() and
		 * btrfs_set_first_dir_index_to_log() to access this field.
		 */
		u64 first_dir_index_to_log;
	};

	union {
		/*
		 * Total number of bytes pending delalloc that fall within a file
		 * range that is either a hole or beyond EOF (and no prealloc extent
		 * exists in the range). This is always <= delalloc_bytes and this
		 * is used only for files. Protected by 'lock'.
		 */
		u64 new_delalloc_bytes;
		/*
		 * The offset of the last dir index key that was logged.
		 * This is used only for directories.
		 */
		u64 last_dir_index_offset;
	};

	/*
	 * Total number of bytes pending defrag, used by stat to check whether
	 * it needs COW. Protected by 'lock'.
	 */
	u64 defrag_bytes;

	/*
	 * The size of the file stored in the metadata on disk.  data=ordered
	 * means the in-memory i_size might be larger than the size on disk
	 * because not all the blocks are written yet. Protected by 'lock'.
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
	 * used in ENOSPC accounting. Protected by 'lock'.
	 */
	u64 csum_bytes;

	/* Backwards incompatible flags, lower half of inode_item::flags  */
	u32 flags;
	/* Read-only compatibility flags, upper half of inode_item::flags */
	u32 ro_flags;

	struct btrfs_block_rsv block_rsv;

	struct btrfs_delayed_node *delayed_node;

	/* File creation time. */
	u64 i_otime_sec;
	u32 i_otime_nsec;

	/* Hook into fs_info->delayed_iputs */
	struct list_head delayed_iput;

	struct rw_semaphore i_mmap_lock;
	struct inode vfs_inode;
};

static inline u64 btrfs_get_first_dir_index_to_log(const struct btrfs_inode *inode)
{
	return READ_ONCE(inode->first_dir_index_to_log);
}

static inline void btrfs_set_first_dir_index_to_log(struct btrfs_inode *inode,
						    u64 index)
{
	WRITE_ONCE(inode->first_dir_index_to_log, index);
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
	return test_bit(BTRFS_INODE_FREE_SPACE_INODE, &inode->runtime_flags);
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
						  mod, inode->outstanding_extents);
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
	    inode->last_sub_trans <= btrfs_get_root_last_log_commit(inode->root))
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

/* Array of bytes with variable length, hexadecimal format 0x1234 */
#define CSUM_FMT				"0x%*phN"
#define CSUM_FMT_VALUE(size, bytes)		size, bytes

int btrfs_check_sector_csum(struct btrfs_fs_info *fs_info, struct page *page,
			    u32 pgoff, u8 *csum, const u8 * const csum_expected);
bool btrfs_data_csum_ok(struct btrfs_bio *bbio, struct btrfs_device *dev,
			u32 bio_offset, struct bio_vec *bv);
noinline int can_nocow_extent(struct inode *inode, u64 offset, u64 *len,
			      u64 *orig_start, u64 *orig_block_len,
			      u64 *ram_bytes, bool nowait, bool strict);

void __btrfs_del_delalloc_inode(struct btrfs_root *root, struct btrfs_inode *inode);
struct inode *btrfs_lookup_dentry(struct inode *dir, struct dentry *dentry);
int btrfs_set_inode_index(struct btrfs_inode *dir, u64 *index);
int btrfs_unlink_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_inode *dir, struct btrfs_inode *inode,
		       const struct fscrypt_str *name);
int btrfs_add_link(struct btrfs_trans_handle *trans,
		   struct btrfs_inode *parent_inode, struct btrfs_inode *inode,
		   const struct fscrypt_str *name, int add_backref, u64 index);
int btrfs_delete_subvolume(struct btrfs_inode *dir, struct dentry *dentry);
int btrfs_truncate_block(struct btrfs_inode *inode, loff_t from, loff_t len,
			 int front);

int btrfs_start_delalloc_snapshot(struct btrfs_root *root, bool in_reclaim_context);
int btrfs_start_delalloc_roots(struct btrfs_fs_info *fs_info, long nr,
			       bool in_reclaim_context);
int btrfs_set_extent_delalloc(struct btrfs_inode *inode, u64 start, u64 end,
			      unsigned int extra_bits,
			      struct extent_state **cached_state);

struct btrfs_new_inode_args {
	/* Input */
	struct inode *dir;
	struct dentry *dentry;
	struct inode *inode;
	bool orphan;
	bool subvol;

	/* Output from btrfs_new_inode_prepare(), input to btrfs_create_new_inode(). */
	struct posix_acl *default_acl;
	struct posix_acl *acl;
	struct fscrypt_name fname;
};

int btrfs_new_inode_prepare(struct btrfs_new_inode_args *args,
			    unsigned int *trans_num_items);
int btrfs_create_new_inode(struct btrfs_trans_handle *trans,
			   struct btrfs_new_inode_args *args);
void btrfs_new_inode_args_destroy(struct btrfs_new_inode_args *args);
struct inode *btrfs_new_subvol_inode(struct mnt_idmap *idmap,
				     struct inode *dir);
 void btrfs_set_delalloc_extent(struct btrfs_inode *inode, struct extent_state *state,
			        u32 bits);
void btrfs_clear_delalloc_extent(struct btrfs_inode *inode,
				 struct extent_state *state, u32 bits);
void btrfs_merge_delalloc_extent(struct btrfs_inode *inode, struct extent_state *new,
				 struct extent_state *other);
void btrfs_split_delalloc_extent(struct btrfs_inode *inode,
				 struct extent_state *orig, u64 split);
void btrfs_set_range_writeback(struct btrfs_inode *inode, u64 start, u64 end);
vm_fault_t btrfs_page_mkwrite(struct vm_fault *vmf);
void btrfs_evict_inode(struct inode *inode);
struct inode *btrfs_alloc_inode(struct super_block *sb);
void btrfs_destroy_inode(struct inode *inode);
void btrfs_free_inode(struct inode *inode);
int btrfs_drop_inode(struct inode *inode);
int __init btrfs_init_cachep(void);
void __cold btrfs_destroy_cachep(void);
struct inode *btrfs_iget_path(struct super_block *s, u64 ino,
			      struct btrfs_root *root, struct btrfs_path *path);
struct inode *btrfs_iget(struct super_block *s, u64 ino, struct btrfs_root *root);
struct extent_map *btrfs_get_extent(struct btrfs_inode *inode,
				    struct page *page, size_t pg_offset,
				    u64 start, u64 end);
int btrfs_update_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_inode *inode);
int btrfs_update_inode_fallback(struct btrfs_trans_handle *trans,
				struct btrfs_inode *inode);
int btrfs_orphan_add(struct btrfs_trans_handle *trans, struct btrfs_inode *inode);
int btrfs_orphan_cleanup(struct btrfs_root *root);
int btrfs_cont_expand(struct btrfs_inode *inode, loff_t oldsize, loff_t size);
void btrfs_add_delayed_iput(struct btrfs_inode *inode);
void btrfs_run_delayed_iputs(struct btrfs_fs_info *fs_info);
int btrfs_wait_on_delayed_iputs(struct btrfs_fs_info *fs_info);
int btrfs_prealloc_file_range(struct inode *inode, int mode,
			      u64 start, u64 num_bytes, u64 min_size,
			      loff_t actual_len, u64 *alloc_hint);
int btrfs_prealloc_file_range_trans(struct inode *inode,
				    struct btrfs_trans_handle *trans, int mode,
				    u64 start, u64 num_bytes, u64 min_size,
				    loff_t actual_len, u64 *alloc_hint);
int btrfs_run_delalloc_range(struct btrfs_inode *inode, struct page *locked_page,
			     u64 start, u64 end, struct writeback_control *wbc);
int btrfs_writepage_cow_fixup(struct page *page);
int btrfs_encoded_io_compression_from_extent(struct btrfs_fs_info *fs_info,
					     int compress_type);
int btrfs_encoded_read_regular_fill_pages(struct btrfs_inode *inode,
					  u64 file_offset, u64 disk_bytenr,
					  u64 disk_io_size,
					  struct page **pages);
ssize_t btrfs_encoded_read(struct kiocb *iocb, struct iov_iter *iter,
			   struct btrfs_ioctl_encoded_io_args *encoded);
ssize_t btrfs_do_encoded_write(struct kiocb *iocb, struct iov_iter *from,
			       const struct btrfs_ioctl_encoded_io_args *encoded);

ssize_t btrfs_dio_read(struct kiocb *iocb, struct iov_iter *iter,
		       size_t done_before);
struct iomap_dio *btrfs_dio_write(struct kiocb *iocb, struct iov_iter *iter,
				  size_t done_before);

extern const struct dentry_operations btrfs_dentry_operations;

/* Inode locking type flags, by default the exclusive lock is taken. */
enum btrfs_ilock_type {
	ENUM_BIT(BTRFS_ILOCK_SHARED),
	ENUM_BIT(BTRFS_ILOCK_TRY),
	ENUM_BIT(BTRFS_ILOCK_MMAP),
};

int btrfs_inode_lock(struct btrfs_inode *inode, unsigned int ilock_flags);
void btrfs_inode_unlock(struct btrfs_inode *inode, unsigned int ilock_flags);
void btrfs_update_inode_bytes(struct btrfs_inode *inode, const u64 add_bytes,
			      const u64 del_bytes);
void btrfs_assert_inode_range_clean(struct btrfs_inode *inode, u64 start, u64 end);

#endif

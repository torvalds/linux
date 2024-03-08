/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_IANALDE_H
#define BTRFS_IANALDE_H

#include <linux/hash.h>
#include <linux/refcount.h>
#include <linux/fscrypt.h>
#include <trace/events/btrfs.h>
#include "extent_map.h"
#include "extent_io.h"
#include "ordered-data.h"
#include "delayed-ianalde.h"

/*
 * Since we search a directory based on f_pos (struct dir_context::pos) we have
 * to start at 2 since '.' and '..' have f_pos of 0 and 1 respectively, so
 * everybody else has to start at 2 (see btrfs_real_readdir() and dir_emit_dots()).
 */
#define BTRFS_DIR_START_INDEX 2

/*
 * ordered_data_close is set by truncate when a file that used
 * to have good data has been truncated to zero.  When it is set
 * the btrfs file release call will add this ianalde to the
 * ordered operations list so that we make sure to flush out any
 * new data the application may have written before commit.
 */
enum {
	BTRFS_IANALDE_FLUSH_ON_CLOSE,
	BTRFS_IANALDE_DUMMY,
	BTRFS_IANALDE_IN_DEFRAG,
	BTRFS_IANALDE_HAS_ASYNC_EXTENT,
	 /*
	  * Always set under the VFS' ianalde lock, otherwise it can cause races
	  * during fsync (we start as a fast fsync and then end up in a full
	  * fsync racing with ordered extent completion).
	  */
	BTRFS_IANALDE_NEEDS_FULL_SYNC,
	BTRFS_IANALDE_COPY_EVERYTHING,
	BTRFS_IANALDE_IN_DELALLOC_LIST,
	BTRFS_IANALDE_HAS_PROPS,
	BTRFS_IANALDE_SNAPSHOT_FLUSH,
	/*
	 * Set and used when logging an ianalde and it serves to signal that an
	 * ianalde does analt have xattrs, so subsequent fsyncs can avoid searching
	 * for xattrs to log. This bit must be cleared whenever a xattr is added
	 * to an ianalde.
	 */
	BTRFS_IANALDE_ANAL_XATTRS,
	/*
	 * Set when we are in a context where we need to start a transaction and
	 * have dirty pages with the respective file range locked. This is to
	 * ensure that when reserving space for the transaction, if we are low
	 * on available space and need to flush delalloc, we will analt flush
	 * delalloc for this ianalde, because that could result in a deadlock (on
	 * the file range, ianalde's io_tree).
	 */
	BTRFS_IANALDE_ANAL_DELALLOC_FLUSH,
	/*
	 * Set when we are working on enabling verity for a file. Computing and
	 * writing the whole Merkle tree can take a while so we want to prevent
	 * races where two separate tasks attempt to simultaneously start verity
	 * on the same file.
	 */
	BTRFS_IANALDE_VERITY_IN_PROGRESS,
	/* Set when this ianalde is a free space ianalde. */
	BTRFS_IANALDE_FREE_SPACE_IANALDE,
	/* Set when there are anal capabilities in XATTs for the ianalde. */
	BTRFS_IANALDE_ANAL_CAP_XATTR,
};

/* in memory btrfs ianalde */
struct btrfs_ianalde {
	/* which subvolume this ianalde belongs to */
	struct btrfs_root *root;

	/* key used to find this ianalde on disk.  This is used by the code
	 * to read in roots of subvolumes
	 */
	struct btrfs_key location;

	/* Cached value of ianalde property 'compression'. */
	u8 prop_compress;

	/*
	 * Force compression on the file using the defrag ioctl, could be
	 * different from prop_compress and takes precedence if set.
	 */
	u8 defrag_compress;

	/*
	 * Lock for counters and all fields used to determine if the ianalde is in
	 * the log or analt (last_trans, last_sub_trans, last_log_commit,
	 * logged_trans), to access/update delalloc_bytes, new_delalloc_bytes,
	 * defrag_bytes, disk_i_size, outstanding_extents, csum_bytes and to
	 * update the VFS' ianalde number of bytes used.
	 */
	spinlock_t lock;

	/* the extent_tree has caches of all the extent mappings to disk */
	struct extent_map_tree extent_tree;

	/* the io_tree does range state (DIRTY, LOCKED etc) */
	struct extent_io_tree io_tree;

	/*
	 * Keep track of where the ianalde has extent items mapped in order to
	 * make sure the i_size adjustments are accurate. Analt required when the
	 * filesystem is ANAL_HOLES, the status can't be set while mounted as
	 * it's a mkfs-time feature.
	 */
	struct extent_io_tree *file_extent_tree;

	/* held while logging the ianalde in tree-log.c */
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
	struct rb_analde *ordered_tree_last;

	/* list of all the delalloc ianaldes in the FS.  There are times we need
	 * to write all the delalloc pages to disk, and this list is used
	 * to walk them all.
	 */
	struct list_head delalloc_ianaldes;

	/* analde for the red-black tree that links ianaldes in subvolume root */
	struct rb_analde rb_analde;

	unsigned long runtime_flags;

	/* full 64 bit generation number, struct vfs_ianalde doesn't have a big
	 * eanalugh field for this.
	 */
	u64 generation;

	/*
	 * ID of the transaction handle that last modified this ianalde.
	 * Protected by 'lock'.
	 */
	u64 last_trans;

	/*
	 * ID of the transaction that last logged this ianalde.
	 * Protected by 'lock'.
	 */
	u64 logged_trans;

	/*
	 * Log transaction ID when this ianalde was last modified.
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
		 * points to an ianalde that needs to be logged.
		 * This is used only for directories.
		 * Use the helpers btrfs_get_first_dir_index_to_log() and
		 * btrfs_set_first_dir_index_to_log() to access this field.
		 */
		u64 first_dir_index_to_log;
	};

	union {
		/*
		 * Total number of bytes pending delalloc that fall within a file
		 * range that is either a hole or beyond EOF (and anal prealloc extent
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
	 * because analt all the blocks are written yet. Protected by 'lock'.
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
	 * The id/generation of the last transaction where this ianalde was
	 * either the source or the destination of a clone/dedupe operation.
	 * Used when logging an ianalde to kanalw if there are shared extents that
	 * need special care when logging checksum items, to avoid duplicate
	 * checksum items in a log (which can lead to a corruption where we end
	 * up with missing checksum ranges after log replay).
	 * Protected by the vfs ianalde lock.
	 */
	u64 last_reflink_trans;

	/*
	 * Number of bytes outstanding that are going to need csums.  This is
	 * used in EANALSPC accounting. Protected by 'lock'.
	 */
	u64 csum_bytes;

	/* Backwards incompatible flags, lower half of ianalde_item::flags  */
	u32 flags;
	/* Read-only compatibility flags, upper half of ianalde_item::flags */
	u32 ro_flags;

	struct btrfs_block_rsv block_rsv;

	struct btrfs_delayed_analde *delayed_analde;

	/* File creation time. */
	u64 i_otime_sec;
	u32 i_otime_nsec;

	/* Hook into fs_info->delayed_iputs */
	struct list_head delayed_iput;

	struct rw_semaphore i_mmap_lock;
	struct ianalde vfs_ianalde;
};

static inline u64 btrfs_get_first_dir_index_to_log(const struct btrfs_ianalde *ianalde)
{
	return READ_ONCE(ianalde->first_dir_index_to_log);
}

static inline void btrfs_set_first_dir_index_to_log(struct btrfs_ianalde *ianalde,
						    u64 index)
{
	WRITE_ONCE(ianalde->first_dir_index_to_log, index);
}

static inline struct btrfs_ianalde *BTRFS_I(const struct ianalde *ianalde)
{
	return container_of(ianalde, struct btrfs_ianalde, vfs_ianalde);
}

static inline unsigned long btrfs_ianalde_hash(u64 objectid,
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
 * On 32 bit systems the i_ianal of struct ianalde is 32 bits (unsigned long), so
 * we use the ianalde's location objectid which is a u64 to avoid truncation.
 */
static inline u64 btrfs_ianal(const struct btrfs_ianalde *ianalde)
{
	u64 ianal = ianalde->location.objectid;

	/* type == BTRFS_ROOT_ITEM_KEY: subvol dir */
	if (ianalde->location.type == BTRFS_ROOT_ITEM_KEY)
		ianal = ianalde->vfs_ianalde.i_ianal;
	return ianal;
}

#else

static inline u64 btrfs_ianal(const struct btrfs_ianalde *ianalde)
{
	return ianalde->vfs_ianalde.i_ianal;
}

#endif

static inline void btrfs_i_size_write(struct btrfs_ianalde *ianalde, u64 size)
{
	i_size_write(&ianalde->vfs_ianalde, size);
	ianalde->disk_i_size = size;
}

static inline bool btrfs_is_free_space_ianalde(struct btrfs_ianalde *ianalde)
{
	return test_bit(BTRFS_IANALDE_FREE_SPACE_IANALDE, &ianalde->runtime_flags);
}

static inline bool is_data_ianalde(struct ianalde *ianalde)
{
	return btrfs_ianal(BTRFS_I(ianalde)) != BTRFS_BTREE_IANALDE_OBJECTID;
}

static inline void btrfs_mod_outstanding_extents(struct btrfs_ianalde *ianalde,
						 int mod)
{
	lockdep_assert_held(&ianalde->lock);
	ianalde->outstanding_extents += mod;
	if (btrfs_is_free_space_ianalde(ianalde))
		return;
	trace_btrfs_ianalde_mod_outstanding_extents(ianalde->root, btrfs_ianal(ianalde),
						  mod, ianalde->outstanding_extents);
}

/*
 * Called every time after doing a buffered, direct IO or memory mapped write.
 *
 * This is to ensure that if we write to a file that was previously fsynced in
 * the current transaction, then try to fsync it again in the same transaction,
 * we will kanalw that there were changes in the file and that it needs to be
 * logged.
 */
static inline void btrfs_set_ianalde_last_sub_trans(struct btrfs_ianalde *ianalde)
{
	spin_lock(&ianalde->lock);
	ianalde->last_sub_trans = ianalde->root->log_transid;
	spin_unlock(&ianalde->lock);
}

/*
 * Should be called while holding the ianalde's VFS lock in exclusive mode or in a
 * context where anal one else can access the ianalde concurrently (during ianalde
 * creation or when loading an ianalde from disk).
 */
static inline void btrfs_set_ianalde_full_sync(struct btrfs_ianalde *ianalde)
{
	set_bit(BTRFS_IANALDE_NEEDS_FULL_SYNC, &ianalde->runtime_flags);
	/*
	 * The ianalde may have been part of a reflink operation in the last
	 * transaction that modified it, and then a fsync has reset the
	 * last_reflink_trans to avoid subsequent fsyncs in the same
	 * transaction to do unnecessary work. So update last_reflink_trans
	 * to the last_trans value (we have to be pessimistic and assume a
	 * reflink happened).
	 *
	 * The ->last_trans is protected by the ianalde's spinlock and we can
	 * have a concurrent ordered extent completion update it. Also set
	 * last_reflink_trans to ->last_trans only if the former is less than
	 * the later, because we can be called in a context where
	 * last_reflink_trans was set to the current transaction generation
	 * while ->last_trans was analt yet updated in the current transaction,
	 * and therefore has a lower value.
	 */
	spin_lock(&ianalde->lock);
	if (ianalde->last_reflink_trans < ianalde->last_trans)
		ianalde->last_reflink_trans = ianalde->last_trans;
	spin_unlock(&ianalde->lock);
}

static inline bool btrfs_ianalde_in_log(struct btrfs_ianalde *ianalde, u64 generation)
{
	bool ret = false;

	spin_lock(&ianalde->lock);
	if (ianalde->logged_trans == generation &&
	    ianalde->last_sub_trans <= ianalde->last_log_commit &&
	    ianalde->last_sub_trans <= btrfs_get_root_last_log_commit(ianalde->root))
		ret = true;
	spin_unlock(&ianalde->lock);
	return ret;
}

/*
 * Check if the ianalde has flags compatible with compression
 */
static inline bool btrfs_ianalde_can_compress(const struct btrfs_ianalde *ianalde)
{
	if (ianalde->flags & BTRFS_IANALDE_ANALDATACOW ||
	    ianalde->flags & BTRFS_IANALDE_ANALDATASUM)
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
analinline int can_analcow_extent(struct ianalde *ianalde, u64 offset, u64 *len,
			      u64 *orig_start, u64 *orig_block_len,
			      u64 *ram_bytes, bool analwait, bool strict);

void __btrfs_del_delalloc_ianalde(struct btrfs_root *root, struct btrfs_ianalde *ianalde);
struct ianalde *btrfs_lookup_dentry(struct ianalde *dir, struct dentry *dentry);
int btrfs_set_ianalde_index(struct btrfs_ianalde *dir, u64 *index);
int btrfs_unlink_ianalde(struct btrfs_trans_handle *trans,
		       struct btrfs_ianalde *dir, struct btrfs_ianalde *ianalde,
		       const struct fscrypt_str *name);
int btrfs_add_link(struct btrfs_trans_handle *trans,
		   struct btrfs_ianalde *parent_ianalde, struct btrfs_ianalde *ianalde,
		   const struct fscrypt_str *name, int add_backref, u64 index);
int btrfs_delete_subvolume(struct btrfs_ianalde *dir, struct dentry *dentry);
int btrfs_truncate_block(struct btrfs_ianalde *ianalde, loff_t from, loff_t len,
			 int front);

int btrfs_start_delalloc_snapshot(struct btrfs_root *root, bool in_reclaim_context);
int btrfs_start_delalloc_roots(struct btrfs_fs_info *fs_info, long nr,
			       bool in_reclaim_context);
int btrfs_set_extent_delalloc(struct btrfs_ianalde *ianalde, u64 start, u64 end,
			      unsigned int extra_bits,
			      struct extent_state **cached_state);

struct btrfs_new_ianalde_args {
	/* Input */
	struct ianalde *dir;
	struct dentry *dentry;
	struct ianalde *ianalde;
	bool orphan;
	bool subvol;

	/* Output from btrfs_new_ianalde_prepare(), input to btrfs_create_new_ianalde(). */
	struct posix_acl *default_acl;
	struct posix_acl *acl;
	struct fscrypt_name fname;
};

int btrfs_new_ianalde_prepare(struct btrfs_new_ianalde_args *args,
			    unsigned int *trans_num_items);
int btrfs_create_new_ianalde(struct btrfs_trans_handle *trans,
			   struct btrfs_new_ianalde_args *args);
void btrfs_new_ianalde_args_destroy(struct btrfs_new_ianalde_args *args);
struct ianalde *btrfs_new_subvol_ianalde(struct mnt_idmap *idmap,
				     struct ianalde *dir);
 void btrfs_set_delalloc_extent(struct btrfs_ianalde *ianalde, struct extent_state *state,
			        u32 bits);
void btrfs_clear_delalloc_extent(struct btrfs_ianalde *ianalde,
				 struct extent_state *state, u32 bits);
void btrfs_merge_delalloc_extent(struct btrfs_ianalde *ianalde, struct extent_state *new,
				 struct extent_state *other);
void btrfs_split_delalloc_extent(struct btrfs_ianalde *ianalde,
				 struct extent_state *orig, u64 split);
void btrfs_set_range_writeback(struct btrfs_ianalde *ianalde, u64 start, u64 end);
vm_fault_t btrfs_page_mkwrite(struct vm_fault *vmf);
void btrfs_evict_ianalde(struct ianalde *ianalde);
struct ianalde *btrfs_alloc_ianalde(struct super_block *sb);
void btrfs_destroy_ianalde(struct ianalde *ianalde);
void btrfs_free_ianalde(struct ianalde *ianalde);
int btrfs_drop_ianalde(struct ianalde *ianalde);
int __init btrfs_init_cachep(void);
void __cold btrfs_destroy_cachep(void);
struct ianalde *btrfs_iget_path(struct super_block *s, u64 ianal,
			      struct btrfs_root *root, struct btrfs_path *path);
struct ianalde *btrfs_iget(struct super_block *s, u64 ianal, struct btrfs_root *root);
struct extent_map *btrfs_get_extent(struct btrfs_ianalde *ianalde,
				    struct page *page, size_t pg_offset,
				    u64 start, u64 len);
int btrfs_update_ianalde(struct btrfs_trans_handle *trans,
		       struct btrfs_ianalde *ianalde);
int btrfs_update_ianalde_fallback(struct btrfs_trans_handle *trans,
				struct btrfs_ianalde *ianalde);
int btrfs_orphan_add(struct btrfs_trans_handle *trans, struct btrfs_ianalde *ianalde);
int btrfs_orphan_cleanup(struct btrfs_root *root);
int btrfs_cont_expand(struct btrfs_ianalde *ianalde, loff_t oldsize, loff_t size);
void btrfs_add_delayed_iput(struct btrfs_ianalde *ianalde);
void btrfs_run_delayed_iputs(struct btrfs_fs_info *fs_info);
int btrfs_wait_on_delayed_iputs(struct btrfs_fs_info *fs_info);
int btrfs_prealloc_file_range(struct ianalde *ianalde, int mode,
			      u64 start, u64 num_bytes, u64 min_size,
			      loff_t actual_len, u64 *alloc_hint);
int btrfs_prealloc_file_range_trans(struct ianalde *ianalde,
				    struct btrfs_trans_handle *trans, int mode,
				    u64 start, u64 num_bytes, u64 min_size,
				    loff_t actual_len, u64 *alloc_hint);
int btrfs_run_delalloc_range(struct btrfs_ianalde *ianalde, struct page *locked_page,
			     u64 start, u64 end, struct writeback_control *wbc);
int btrfs_writepage_cow_fixup(struct page *page);
int btrfs_encoded_io_compression_from_extent(struct btrfs_fs_info *fs_info,
					     int compress_type);
int btrfs_encoded_read_regular_fill_pages(struct btrfs_ianalde *ianalde,
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

/* Ianalde locking type flags, by default the exclusive lock is taken. */
enum btrfs_ilock_type {
	ENUM_BIT(BTRFS_ILOCK_SHARED),
	ENUM_BIT(BTRFS_ILOCK_TRY),
	ENUM_BIT(BTRFS_ILOCK_MMAP),
};

int btrfs_ianalde_lock(struct btrfs_ianalde *ianalde, unsigned int ilock_flags);
void btrfs_ianalde_unlock(struct btrfs_ianalde *ianalde, unsigned int ilock_flags);
void btrfs_update_ianalde_bytes(struct btrfs_ianalde *ianalde, const u64 add_bytes,
			      const u64 del_bytes);
void btrfs_assert_ianalde_range_clean(struct btrfs_ianalde *ianalde, u64 start, u64 end);

#endif

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include <linux/radix-tree.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/migrate.h>
#include <linux/ratelimit.h>
#include <linux/uuid.h>
#include <linux/semaphore.h>
#include <linux/error-injection.h>
#include <linux/crc32c.h>
#include <asm/unaligned.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "volumes.h"
#include "print-tree.h"
#include "locking.h"
#include "tree-log.h"
#include "free-space-cache.h"
#include "free-space-tree.h"
#include "inode-map.h"
#include "check-integrity.h"
#include "rcu-string.h"
#include "dev-replace.h"
#include "raid56.h"
#include "sysfs.h"
#include "qgroup.h"
#include "compression.h"
#include "tree-checker.h"
#include "ref-verify.h"

#ifdef CONFIG_X86
#include <asm/cpufeature.h>
#endif

#define BTRFS_SUPER_FLAG_SUPP	(BTRFS_HEADER_FLAG_WRITTEN |\
				 BTRFS_HEADER_FLAG_RELOC |\
				 BTRFS_SUPER_FLAG_ERROR |\
				 BTRFS_SUPER_FLAG_SEEDING |\
				 BTRFS_SUPER_FLAG_METADUMP |\
				 BTRFS_SUPER_FLAG_METADUMP_V2)

static const struct extent_io_ops btree_extent_io_ops;
static void end_workqueue_fn(struct btrfs_work *work);
static void free_fs_root(struct btrfs_root *root);
static int btrfs_check_super_valid(struct btrfs_fs_info *fs_info);
static void btrfs_destroy_ordered_extents(struct btrfs_root *root);
static int btrfs_destroy_delayed_refs(struct btrfs_transaction *trans,
				      struct btrfs_fs_info *fs_info);
static void btrfs_destroy_delalloc_inodes(struct btrfs_root *root);
static int btrfs_destroy_marked_extents(struct btrfs_fs_info *fs_info,
					struct extent_io_tree *dirty_pages,
					int mark);
static int btrfs_destroy_pinned_extent(struct btrfs_fs_info *fs_info,
				       struct extent_io_tree *pinned_extents);
static int btrfs_cleanup_transaction(struct btrfs_fs_info *fs_info);
static void btrfs_error_commit_super(struct btrfs_fs_info *fs_info);

/*
 * btrfs_end_io_wq structs are used to do processing in task context when an IO
 * is complete.  This is used during reads to verify checksums, and it is used
 * by writes to insert metadata for new file extents after IO is complete.
 */
struct btrfs_end_io_wq {
	struct bio *bio;
	bio_end_io_t *end_io;
	void *private;
	struct btrfs_fs_info *info;
	blk_status_t status;
	enum btrfs_wq_endio_type metadata;
	struct btrfs_work work;
};

static struct kmem_cache *btrfs_end_io_wq_cache;

int __init btrfs_end_io_wq_init(void)
{
	btrfs_end_io_wq_cache = kmem_cache_create("btrfs_end_io_wq",
					sizeof(struct btrfs_end_io_wq),
					0,
					SLAB_MEM_SPREAD,
					NULL);
	if (!btrfs_end_io_wq_cache)
		return -ENOMEM;
	return 0;
}

void __cold btrfs_end_io_wq_exit(void)
{
	kmem_cache_destroy(btrfs_end_io_wq_cache);
}

/*
 * async submit bios are used to offload expensive checksumming
 * onto the worker threads.  They checksum file and metadata bios
 * just before they are sent down the IO stack.
 */
struct async_submit_bio {
	void *private_data;
	struct btrfs_fs_info *fs_info;
	struct bio *bio;
	extent_submit_bio_start_t *submit_bio_start;
	extent_submit_bio_done_t *submit_bio_done;
	int mirror_num;
	unsigned long bio_flags;
	/*
	 * bio_offset is optional, can be used if the pages in the bio
	 * can't tell us where in the file the bio should go
	 */
	u64 bio_offset;
	struct btrfs_work work;
	blk_status_t status;
};

/*
 * Lockdep class keys for extent_buffer->lock's in this root.  For a given
 * eb, the lockdep key is determined by the btrfs_root it belongs to and
 * the level the eb occupies in the tree.
 *
 * Different roots are used for different purposes and may nest inside each
 * other and they require separate keysets.  As lockdep keys should be
 * static, assign keysets according to the purpose of the root as indicated
 * by btrfs_root->objectid.  This ensures that all special purpose roots
 * have separate keysets.
 *
 * Lock-nesting across peer nodes is always done with the immediate parent
 * node locked thus preventing deadlock.  As lockdep doesn't know this, use
 * subclass to avoid triggering lockdep warning in such cases.
 *
 * The key is set by the readpage_end_io_hook after the buffer has passed
 * csum validation but before the pages are unlocked.  It is also set by
 * btrfs_init_new_buffer on freshly allocated blocks.
 *
 * We also add a check to make sure the highest level of the tree is the
 * same as our lockdep setup here.  If BTRFS_MAX_LEVEL changes, this code
 * needs update as well.
 */
#ifdef CONFIG_DEBUG_LOCK_ALLOC
# if BTRFS_MAX_LEVEL != 8
#  error
# endif

static struct btrfs_lockdep_keyset {
	u64			id;		/* root objectid */
	const char		*name_stem;	/* lock name stem */
	char			names[BTRFS_MAX_LEVEL + 1][20];
	struct lock_class_key	keys[BTRFS_MAX_LEVEL + 1];
} btrfs_lockdep_keysets[] = {
	{ .id = BTRFS_ROOT_TREE_OBJECTID,	.name_stem = "root"	},
	{ .id = BTRFS_EXTENT_TREE_OBJECTID,	.name_stem = "extent"	},
	{ .id = BTRFS_CHUNK_TREE_OBJECTID,	.name_stem = "chunk"	},
	{ .id = BTRFS_DEV_TREE_OBJECTID,	.name_stem = "dev"	},
	{ .id = BTRFS_FS_TREE_OBJECTID,		.name_stem = "fs"	},
	{ .id = BTRFS_CSUM_TREE_OBJECTID,	.name_stem = "csum"	},
	{ .id = BTRFS_QUOTA_TREE_OBJECTID,	.name_stem = "quota"	},
	{ .id = BTRFS_TREE_LOG_OBJECTID,	.name_stem = "log"	},
	{ .id = BTRFS_TREE_RELOC_OBJECTID,	.name_stem = "treloc"	},
	{ .id = BTRFS_DATA_RELOC_TREE_OBJECTID,	.name_stem = "dreloc"	},
	{ .id = BTRFS_UUID_TREE_OBJECTID,	.name_stem = "uuid"	},
	{ .id = BTRFS_FREE_SPACE_TREE_OBJECTID,	.name_stem = "free-space" },
	{ .id = 0,				.name_stem = "tree"	},
};

void __init btrfs_init_lockdep(void)
{
	int i, j;

	/* initialize lockdep class names */
	for (i = 0; i < ARRAY_SIZE(btrfs_lockdep_keysets); i++) {
		struct btrfs_lockdep_keyset *ks = &btrfs_lockdep_keysets[i];

		for (j = 0; j < ARRAY_SIZE(ks->names); j++)
			snprintf(ks->names[j], sizeof(ks->names[j]),
				 "btrfs-%s-%02d", ks->name_stem, j);
	}
}

void btrfs_set_buffer_lockdep_class(u64 objectid, struct extent_buffer *eb,
				    int level)
{
	struct btrfs_lockdep_keyset *ks;

	BUG_ON(level >= ARRAY_SIZE(ks->keys));

	/* find the matching keyset, id 0 is the default entry */
	for (ks = btrfs_lockdep_keysets; ks->id; ks++)
		if (ks->id == objectid)
			break;

	lockdep_set_class_and_name(&eb->lock,
				   &ks->keys[level], ks->names[level]);
}

#endif

/*
 * extents on the btree inode are pretty simple, there's one extent
 * that covers the entire device
 */
struct extent_map *btree_get_extent(struct btrfs_inode *inode,
		struct page *page, size_t pg_offset, u64 start, u64 len,
		int create)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->vfs_inode.i_sb);
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	int ret;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	if (em) {
		em->bdev = fs_info->fs_devices->latest_bdev;
		read_unlock(&em_tree->lock);
		goto out;
	}
	read_unlock(&em_tree->lock);

	em = alloc_extent_map();
	if (!em) {
		em = ERR_PTR(-ENOMEM);
		goto out;
	}
	em->start = 0;
	em->len = (u64)-1;
	em->block_len = (u64)-1;
	em->block_start = 0;
	em->bdev = fs_info->fs_devices->latest_bdev;

	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	if (ret == -EEXIST) {
		free_extent_map(em);
		em = lookup_extent_mapping(em_tree, start, len);
		if (!em)
			em = ERR_PTR(-EIO);
	} else if (ret) {
		free_extent_map(em);
		em = ERR_PTR(ret);
	}
	write_unlock(&em_tree->lock);

out:
	return em;
}

u32 btrfs_csum_data(const char *data, u32 seed, size_t len)
{
	return crc32c(seed, data, len);
}

void btrfs_csum_final(u32 crc, u8 *result)
{
	put_unaligned_le32(~crc, result);
}

/*
 * compute the csum for a btree block, and either verify it or write it
 * into the csum field of the block.
 */
static int csum_tree_block(struct btrfs_fs_info *fs_info,
			   struct extent_buffer *buf,
			   int verify)
{
	u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);
	char result[BTRFS_CSUM_SIZE];
	unsigned long len;
	unsigned long cur_len;
	unsigned long offset = BTRFS_CSUM_SIZE;
	char *kaddr;
	unsigned long map_start;
	unsigned long map_len;
	int err;
	u32 crc = ~(u32)0;

	len = buf->len - offset;
	while (len > 0) {
		err = map_private_extent_buffer(buf, offset, 32,
					&kaddr, &map_start, &map_len);
		if (err)
			return err;
		cur_len = min(len, map_len - (offset - map_start));
		crc = btrfs_csum_data(kaddr + offset - map_start,
				      crc, cur_len);
		len -= cur_len;
		offset += cur_len;
	}
	memset(result, 0, BTRFS_CSUM_SIZE);

	btrfs_csum_final(crc, result);

	if (verify) {
		if (memcmp_extent_buffer(buf, result, 0, csum_size)) {
			u32 val;
			u32 found = 0;
			memcpy(&found, result, csum_size);

			read_extent_buffer(buf, &val, 0, csum_size);
			btrfs_warn_rl(fs_info,
				"%s checksum verify failed on %llu wanted %X found %X level %d",
				fs_info->sb->s_id, buf->start,
				val, found, btrfs_header_level(buf));
			return -EUCLEAN;
		}
	} else {
		write_extent_buffer(buf, result, 0, csum_size);
	}

	return 0;
}

/*
 * we can't consider a given block up to date unless the transid of the
 * block matches the transid in the parent node's pointer.  This is how we
 * detect blocks that either didn't get written at all or got written
 * in the wrong place.
 */
static int verify_parent_transid(struct extent_io_tree *io_tree,
				 struct extent_buffer *eb, u64 parent_transid,
				 int atomic)
{
	struct extent_state *cached_state = NULL;
	int ret;
	bool need_lock = (current->journal_info == BTRFS_SEND_TRANS_STUB);

	if (!parent_transid || btrfs_header_generation(eb) == parent_transid)
		return 0;

	if (atomic)
		return -EAGAIN;

	if (need_lock) {
		btrfs_tree_read_lock(eb);
		btrfs_set_lock_blocking_rw(eb, BTRFS_READ_LOCK);
	}

	lock_extent_bits(io_tree, eb->start, eb->start + eb->len - 1,
			 &cached_state);
	if (extent_buffer_uptodate(eb) &&
	    btrfs_header_generation(eb) == parent_transid) {
		ret = 0;
		goto out;
	}
	btrfs_err_rl(eb->fs_info,
		"parent transid verify failed on %llu wanted %llu found %llu",
			eb->start,
			parent_transid, btrfs_header_generation(eb));
	ret = 1;

	/*
	 * Things reading via commit roots that don't have normal protection,
	 * like send, can have a really old block in cache that may point at a
	 * block that has been freed and re-allocated.  So don't clear uptodate
	 * if we find an eb that is under IO (dirty/writeback) because we could
	 * end up reading in the stale data and then writing it back out and
	 * making everybody very sad.
	 */
	if (!extent_buffer_under_io(eb))
		clear_extent_buffer_uptodate(eb);
out:
	unlock_extent_cached(io_tree, eb->start, eb->start + eb->len - 1,
			     &cached_state);
	if (need_lock)
		btrfs_tree_read_unlock_blocking(eb);
	return ret;
}

/*
 * Return 0 if the superblock checksum type matches the checksum value of that
 * algorithm. Pass the raw disk superblock data.
 */
static int btrfs_check_super_csum(struct btrfs_fs_info *fs_info,
				  char *raw_disk_sb)
{
	struct btrfs_super_block *disk_sb =
		(struct btrfs_super_block *)raw_disk_sb;
	u16 csum_type = btrfs_super_csum_type(disk_sb);
	int ret = 0;

	if (csum_type == BTRFS_CSUM_TYPE_CRC32) {
		u32 crc = ~(u32)0;
		char result[sizeof(crc)];

		/*
		 * The super_block structure does not span the whole
		 * BTRFS_SUPER_INFO_SIZE range, we expect that the unused space
		 * is filled with zeros and is included in the checksum.
		 */
		crc = btrfs_csum_data(raw_disk_sb + BTRFS_CSUM_SIZE,
				crc, BTRFS_SUPER_INFO_SIZE - BTRFS_CSUM_SIZE);
		btrfs_csum_final(crc, result);

		if (memcmp(raw_disk_sb, result, sizeof(result)))
			ret = 1;
	}

	if (csum_type >= ARRAY_SIZE(btrfs_csum_sizes)) {
		btrfs_err(fs_info, "unsupported checksum algorithm %u",
				csum_type);
		ret = 1;
	}

	return ret;
}

static int verify_level_key(struct btrfs_fs_info *fs_info,
			    struct extent_buffer *eb, int level,
			    struct btrfs_key *first_key)
{
	int found_level;
	struct btrfs_key found_key;
	int ret;

	found_level = btrfs_header_level(eb);
	if (found_level != level) {
#ifdef CONFIG_BTRFS_DEBUG
		WARN_ON(1);
		btrfs_err(fs_info,
"tree level mismatch detected, bytenr=%llu level expected=%u has=%u",
			  eb->start, level, found_level);
#endif
		return -EIO;
	}

	if (!first_key)
		return 0;

	if (found_level)
		btrfs_node_key_to_cpu(eb, &found_key, 0);
	else
		btrfs_item_key_to_cpu(eb, &found_key, 0);
	ret = btrfs_comp_cpu_keys(first_key, &found_key);

#ifdef CONFIG_BTRFS_DEBUG
	if (ret) {
		WARN_ON(1);
		btrfs_err(fs_info,
"tree first key mismatch detected, bytenr=%llu key expected=(%llu, %u, %llu) has=(%llu, %u, %llu)",
			  eb->start, first_key->objectid, first_key->type,
			  first_key->offset, found_key.objectid,
			  found_key.type, found_key.offset);
	}
#endif
	return ret;
}

/*
 * helper to read a given tree block, doing retries as required when
 * the checksums don't match and we have alternate mirrors to try.
 *
 * @parent_transid:	expected transid, skip check if 0
 * @level:		expected level, mandatory check
 * @first_key:		expected key of first slot, skip check if NULL
 */
static int btree_read_extent_buffer_pages(struct btrfs_fs_info *fs_info,
					  struct extent_buffer *eb,
					  u64 parent_transid, int level,
					  struct btrfs_key *first_key)
{
	struct extent_io_tree *io_tree;
	int failed = 0;
	int ret;
	int num_copies = 0;
	int mirror_num = 0;
	int failed_mirror = 0;

	clear_bit(EXTENT_BUFFER_CORRUPT, &eb->bflags);
	io_tree = &BTRFS_I(fs_info->btree_inode)->io_tree;
	while (1) {
		ret = read_extent_buffer_pages(io_tree, eb, WAIT_COMPLETE,
					       mirror_num);
		if (!ret) {
			if (verify_parent_transid(io_tree, eb,
						   parent_transid, 0))
				ret = -EIO;
			else if (verify_level_key(fs_info, eb, level,
						  first_key))
				ret = -EUCLEAN;
			else
				break;
		}

		/*
		 * This buffer's crc is fine, but its contents are corrupted, so
		 * there is no reason to read the other copies, they won't be
		 * any less wrong.
		 */
		if (test_bit(EXTENT_BUFFER_CORRUPT, &eb->bflags) ||
		    ret == -EUCLEAN)
			break;

		num_copies = btrfs_num_copies(fs_info,
					      eb->start, eb->len);
		if (num_copies == 1)
			break;

		if (!failed_mirror) {
			failed = 1;
			failed_mirror = eb->read_mirror;
		}

		mirror_num++;
		if (mirror_num == failed_mirror)
			mirror_num++;

		if (mirror_num > num_copies)
			break;
	}

	if (failed && !ret && failed_mirror)
		repair_eb_io_failure(fs_info, eb, failed_mirror);

	return ret;
}

/*
 * checksum a dirty tree block before IO.  This has extra checks to make sure
 * we only fill in the checksum field in the first page of a multi-page block
 */

static int csum_dirty_buffer(struct btrfs_fs_info *fs_info, struct page *page)
{
	u64 start = page_offset(page);
	u64 found_start;
	struct extent_buffer *eb;

	eb = (struct extent_buffer *)page->private;
	if (page != eb->pages[0])
		return 0;

	found_start = btrfs_header_bytenr(eb);
	/*
	 * Please do not consolidate these warnings into a single if.
	 * It is useful to know what went wrong.
	 */
	if (WARN_ON(found_start != start))
		return -EUCLEAN;
	if (WARN_ON(!PageUptodate(page)))
		return -EUCLEAN;

	ASSERT(memcmp_extent_buffer(eb, fs_info->fsid,
			btrfs_header_fsid(), BTRFS_FSID_SIZE) == 0);

	return csum_tree_block(fs_info, eb, 0);
}

static int check_tree_block_fsid(struct btrfs_fs_info *fs_info,
				 struct extent_buffer *eb)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	u8 fsid[BTRFS_FSID_SIZE];
	int ret = 1;

	read_extent_buffer(eb, fsid, btrfs_header_fsid(), BTRFS_FSID_SIZE);
	while (fs_devices) {
		if (!memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE)) {
			ret = 0;
			break;
		}
		fs_devices = fs_devices->seed;
	}
	return ret;
}

static int btree_readpage_end_io_hook(struct btrfs_io_bio *io_bio,
				      u64 phy_offset, struct page *page,
				      u64 start, u64 end, int mirror)
{
	u64 found_start;
	int found_level;
	struct extent_buffer *eb;
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret = 0;
	int reads_done;

	if (!page->private)
		goto out;

	eb = (struct extent_buffer *)page->private;

	/* the pending IO might have been the only thing that kept this buffer
	 * in memory.  Make sure we have a ref for all this other checks
	 */
	extent_buffer_get(eb);

	reads_done = atomic_dec_and_test(&eb->io_pages);
	if (!reads_done)
		goto err;

	eb->read_mirror = mirror;
	if (test_bit(EXTENT_BUFFER_READ_ERR, &eb->bflags)) {
		ret = -EIO;
		goto err;
	}

	found_start = btrfs_header_bytenr(eb);
	if (found_start != eb->start) {
		btrfs_err_rl(fs_info, "bad tree block start %llu %llu",
			     found_start, eb->start);
		ret = -EIO;
		goto err;
	}
	if (check_tree_block_fsid(fs_info, eb)) {
		btrfs_err_rl(fs_info, "bad fsid on block %llu",
			     eb->start);
		ret = -EIO;
		goto err;
	}
	found_level = btrfs_header_level(eb);
	if (found_level >= BTRFS_MAX_LEVEL) {
		btrfs_err(fs_info, "bad tree block level %d",
			  (int)btrfs_header_level(eb));
		ret = -EIO;
		goto err;
	}

	btrfs_set_buffer_lockdep_class(btrfs_header_owner(eb),
				       eb, found_level);

	ret = csum_tree_block(fs_info, eb, 1);
	if (ret)
		goto err;

	/*
	 * If this is a leaf block and it is corrupt, set the corrupt bit so
	 * that we don't try and read the other copies of this block, just
	 * return -EIO.
	 */
	if (found_level == 0 && btrfs_check_leaf_full(fs_info, eb)) {
		set_bit(EXTENT_BUFFER_CORRUPT, &eb->bflags);
		ret = -EIO;
	}

	if (found_level > 0 && btrfs_check_node(fs_info, eb))
		ret = -EIO;

	if (!ret)
		set_extent_buffer_uptodate(eb);
err:
	if (reads_done &&
	    test_and_clear_bit(EXTENT_BUFFER_READAHEAD, &eb->bflags))
		btree_readahead_hook(eb, ret);

	if (ret) {
		/*
		 * our io error hook is going to dec the io pages
		 * again, we have to make sure it has something
		 * to decrement
		 */
		atomic_inc(&eb->io_pages);
		clear_extent_buffer_uptodate(eb);
	}
	free_extent_buffer(eb);
out:
	return ret;
}

static int btree_io_failed_hook(struct page *page, int failed_mirror)
{
	struct extent_buffer *eb;

	eb = (struct extent_buffer *)page->private;
	set_bit(EXTENT_BUFFER_READ_ERR, &eb->bflags);
	eb->read_mirror = failed_mirror;
	atomic_dec(&eb->io_pages);
	if (test_and_clear_bit(EXTENT_BUFFER_READAHEAD, &eb->bflags))
		btree_readahead_hook(eb, -EIO);
	return -EIO;	/* we fixed nothing */
}

static void end_workqueue_bio(struct bio *bio)
{
	struct btrfs_end_io_wq *end_io_wq = bio->bi_private;
	struct btrfs_fs_info *fs_info;
	struct btrfs_workqueue *wq;
	btrfs_work_func_t func;

	fs_info = end_io_wq->info;
	end_io_wq->status = bio->bi_status;

	if (bio_op(bio) == REQ_OP_WRITE) {
		if (end_io_wq->metadata == BTRFS_WQ_ENDIO_METADATA) {
			wq = fs_info->endio_meta_write_workers;
			func = btrfs_endio_meta_write_helper;
		} else if (end_io_wq->metadata == BTRFS_WQ_ENDIO_FREE_SPACE) {
			wq = fs_info->endio_freespace_worker;
			func = btrfs_freespace_write_helper;
		} else if (end_io_wq->metadata == BTRFS_WQ_ENDIO_RAID56) {
			wq = fs_info->endio_raid56_workers;
			func = btrfs_endio_raid56_helper;
		} else {
			wq = fs_info->endio_write_workers;
			func = btrfs_endio_write_helper;
		}
	} else {
		if (unlikely(end_io_wq->metadata ==
			     BTRFS_WQ_ENDIO_DIO_REPAIR)) {
			wq = fs_info->endio_repair_workers;
			func = btrfs_endio_repair_helper;
		} else if (end_io_wq->metadata == BTRFS_WQ_ENDIO_RAID56) {
			wq = fs_info->endio_raid56_workers;
			func = btrfs_endio_raid56_helper;
		} else if (end_io_wq->metadata) {
			wq = fs_info->endio_meta_workers;
			func = btrfs_endio_meta_helper;
		} else {
			wq = fs_info->endio_workers;
			func = btrfs_endio_helper;
		}
	}

	btrfs_init_work(&end_io_wq->work, func, end_workqueue_fn, NULL, NULL);
	btrfs_queue_work(wq, &end_io_wq->work);
}

blk_status_t btrfs_bio_wq_end_io(struct btrfs_fs_info *info, struct bio *bio,
			enum btrfs_wq_endio_type metadata)
{
	struct btrfs_end_io_wq *end_io_wq;

	end_io_wq = kmem_cache_alloc(btrfs_end_io_wq_cache, GFP_NOFS);
	if (!end_io_wq)
		return BLK_STS_RESOURCE;

	end_io_wq->private = bio->bi_private;
	end_io_wq->end_io = bio->bi_end_io;
	end_io_wq->info = info;
	end_io_wq->status = 0;
	end_io_wq->bio = bio;
	end_io_wq->metadata = metadata;

	bio->bi_private = end_io_wq;
	bio->bi_end_io = end_workqueue_bio;
	return 0;
}

static void run_one_async_start(struct btrfs_work *work)
{
	struct async_submit_bio *async;
	blk_status_t ret;

	async = container_of(work, struct  async_submit_bio, work);
	ret = async->submit_bio_start(async->private_data, async->bio,
				      async->bio_offset);
	if (ret)
		async->status = ret;
}

static void run_one_async_done(struct btrfs_work *work)
{
	struct async_submit_bio *async;

	async = container_of(work, struct  async_submit_bio, work);

	/* If an error occurred we just want to clean up the bio and move on */
	if (async->status) {
		async->bio->bi_status = async->status;
		bio_endio(async->bio);
		return;
	}

	async->submit_bio_done(async->private_data, async->bio, async->mirror_num);
}

static void run_one_async_free(struct btrfs_work *work)
{
	struct async_submit_bio *async;

	async = container_of(work, struct  async_submit_bio, work);
	kfree(async);
}

blk_status_t btrfs_wq_submit_bio(struct btrfs_fs_info *fs_info, struct bio *bio,
				 int mirror_num, unsigned long bio_flags,
				 u64 bio_offset, void *private_data,
				 extent_submit_bio_start_t *submit_bio_start,
				 extent_submit_bio_done_t *submit_bio_done)
{
	struct async_submit_bio *async;

	async = kmalloc(sizeof(*async), GFP_NOFS);
	if (!async)
		return BLK_STS_RESOURCE;

	async->private_data = private_data;
	async->fs_info = fs_info;
	async->bio = bio;
	async->mirror_num = mirror_num;
	async->submit_bio_start = submit_bio_start;
	async->submit_bio_done = submit_bio_done;

	btrfs_init_work(&async->work, btrfs_worker_helper, run_one_async_start,
			run_one_async_done, run_one_async_free);

	async->bio_flags = bio_flags;
	async->bio_offset = bio_offset;

	async->status = 0;

	if (op_is_sync(bio->bi_opf))
		btrfs_set_work_high_priority(&async->work);

	btrfs_queue_work(fs_info->workers, &async->work);
	return 0;
}

static blk_status_t btree_csum_one_bio(struct bio *bio)
{
	struct bio_vec *bvec;
	struct btrfs_root *root;
	int i, ret = 0;

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_segment_all(bvec, bio, i) {
		root = BTRFS_I(bvec->bv_page->mapping->host)->root;
		ret = csum_dirty_buffer(root->fs_info, bvec->bv_page);
		if (ret)
			break;
	}

	return errno_to_blk_status(ret);
}

static blk_status_t btree_submit_bio_start(void *private_data, struct bio *bio,
					     u64 bio_offset)
{
	/*
	 * when we're called for a write, we're already in the async
	 * submission context.  Just jump into btrfs_map_bio
	 */
	return btree_csum_one_bio(bio);
}

static blk_status_t btree_submit_bio_done(void *private_data, struct bio *bio,
					    int mirror_num)
{
	struct inode *inode = private_data;
	blk_status_t ret;

	/*
	 * when we're called for a write, we're already in the async
	 * submission context.  Just jump into btrfs_map_bio
	 */
	ret = btrfs_map_bio(btrfs_sb(inode->i_sb), bio, mirror_num, 1);
	if (ret) {
		bio->bi_status = ret;
		bio_endio(bio);
	}
	return ret;
}

static int check_async_write(struct btrfs_inode *bi)
{
	if (atomic_read(&bi->sync_writers))
		return 0;
#ifdef CONFIG_X86
	if (static_cpu_has(X86_FEATURE_XMM4_2))
		return 0;
#endif
	return 1;
}

static blk_status_t btree_submit_bio_hook(void *private_data, struct bio *bio,
					  int mirror_num, unsigned long bio_flags,
					  u64 bio_offset)
{
	struct inode *inode = private_data;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int async = check_async_write(BTRFS_I(inode));
	blk_status_t ret;

	if (bio_op(bio) != REQ_OP_WRITE) {
		/*
		 * called for a read, do the setup so that checksum validation
		 * can happen in the async kernel threads
		 */
		ret = btrfs_bio_wq_end_io(fs_info, bio,
					  BTRFS_WQ_ENDIO_METADATA);
		if (ret)
			goto out_w_error;
		ret = btrfs_map_bio(fs_info, bio, mirror_num, 0);
	} else if (!async) {
		ret = btree_csum_one_bio(bio);
		if (ret)
			goto out_w_error;
		ret = btrfs_map_bio(fs_info, bio, mirror_num, 0);
	} else {
		/*
		 * kthread helpers are used to submit writes so that
		 * checksumming can happen in parallel across all CPUs
		 */
		ret = btrfs_wq_submit_bio(fs_info, bio, mirror_num, 0,
					  bio_offset, private_data,
					  btree_submit_bio_start,
					  btree_submit_bio_done);
	}

	if (ret)
		goto out_w_error;
	return 0;

out_w_error:
	bio->bi_status = ret;
	bio_endio(bio);
	return ret;
}

#ifdef CONFIG_MIGRATION
static int btree_migratepage(struct address_space *mapping,
			struct page *newpage, struct page *page,
			enum migrate_mode mode)
{
	/*
	 * we can't safely write a btree page from here,
	 * we haven't done the locking hook
	 */
	if (PageDirty(page))
		return -EAGAIN;
	/*
	 * Buffers may be managed in a filesystem specific way.
	 * We must have no buffers or drop them.
	 */
	if (page_has_private(page) &&
	    !try_to_release_page(page, GFP_KERNEL))
		return -EAGAIN;
	return migrate_page(mapping, newpage, page, mode);
}
#endif


static int btree_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct btrfs_fs_info *fs_info;
	int ret;

	if (wbc->sync_mode == WB_SYNC_NONE) {

		if (wbc->for_kupdate)
			return 0;

		fs_info = BTRFS_I(mapping->host)->root->fs_info;
		/* this is a bit racy, but that's ok */
		ret = percpu_counter_compare(&fs_info->dirty_metadata_bytes,
					     BTRFS_DIRTY_METADATA_THRESH);
		if (ret < 0)
			return 0;
	}
	return btree_write_cache_pages(mapping, wbc);
}

static int btree_readpage(struct file *file, struct page *page)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	return extent_read_full_page(tree, page, btree_get_extent, 0);
}

static int btree_releasepage(struct page *page, gfp_t gfp_flags)
{
	if (PageWriteback(page) || PageDirty(page))
		return 0;

	return try_release_extent_buffer(page);
}

static void btree_invalidatepage(struct page *page, unsigned int offset,
				 unsigned int length)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	extent_invalidatepage(tree, page, offset);
	btree_releasepage(page, GFP_NOFS);
	if (PagePrivate(page)) {
		btrfs_warn(BTRFS_I(page->mapping->host)->root->fs_info,
			   "page private not zero on page %llu",
			   (unsigned long long)page_offset(page));
		ClearPagePrivate(page);
		set_page_private(page, 0);
		put_page(page);
	}
}

static int btree_set_page_dirty(struct page *page)
{
#ifdef DEBUG
	struct extent_buffer *eb;

	BUG_ON(!PagePrivate(page));
	eb = (struct extent_buffer *)page->private;
	BUG_ON(!eb);
	BUG_ON(!test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags));
	BUG_ON(!atomic_read(&eb->refs));
	btrfs_assert_tree_locked(eb);
#endif
	return __set_page_dirty_nobuffers(page);
}

static const struct address_space_operations btree_aops = {
	.readpage	= btree_readpage,
	.writepages	= btree_writepages,
	.releasepage	= btree_releasepage,
	.invalidatepage = btree_invalidatepage,
#ifdef CONFIG_MIGRATION
	.migratepage	= btree_migratepage,
#endif
	.set_page_dirty = btree_set_page_dirty,
};

void readahead_tree_block(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = fs_info->btree_inode;

	buf = btrfs_find_create_tree_block(fs_info, bytenr);
	if (IS_ERR(buf))
		return;
	read_extent_buffer_pages(&BTRFS_I(btree_inode)->io_tree,
				 buf, WAIT_NONE, 0);
	free_extent_buffer(buf);
}

int reada_tree_block_flagged(struct btrfs_fs_info *fs_info, u64 bytenr,
			 int mirror_num, struct extent_buffer **eb)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = fs_info->btree_inode;
	struct extent_io_tree *io_tree = &BTRFS_I(btree_inode)->io_tree;
	int ret;

	buf = btrfs_find_create_tree_block(fs_info, bytenr);
	if (IS_ERR(buf))
		return 0;

	set_bit(EXTENT_BUFFER_READAHEAD, &buf->bflags);

	ret = read_extent_buffer_pages(io_tree, buf, WAIT_PAGE_LOCK,
				       mirror_num);
	if (ret) {
		free_extent_buffer(buf);
		return ret;
	}

	if (test_bit(EXTENT_BUFFER_CORRUPT, &buf->bflags)) {
		free_extent_buffer(buf);
		return -EIO;
	} else if (extent_buffer_uptodate(buf)) {
		*eb = buf;
	} else {
		free_extent_buffer(buf);
	}
	return 0;
}

struct extent_buffer *btrfs_find_create_tree_block(
						struct btrfs_fs_info *fs_info,
						u64 bytenr)
{
	if (btrfs_is_testing(fs_info))
		return alloc_test_extent_buffer(fs_info, bytenr);
	return alloc_extent_buffer(fs_info, bytenr);
}


int btrfs_write_tree_block(struct extent_buffer *buf)
{
	return filemap_fdatawrite_range(buf->pages[0]->mapping, buf->start,
					buf->start + buf->len - 1);
}

void btrfs_wait_tree_block_writeback(struct extent_buffer *buf)
{
	filemap_fdatawait_range(buf->pages[0]->mapping,
			        buf->start, buf->start + buf->len - 1);
}

/*
 * Read tree block at logical address @bytenr and do variant basic but critical
 * verification.
 *
 * @parent_transid:	expected transid of this tree block, skip check if 0
 * @level:		expected level, mandatory check
 * @first_key:		expected key in slot 0, skip check if NULL
 */
struct extent_buffer *read_tree_block(struct btrfs_fs_info *fs_info, u64 bytenr,
				      u64 parent_transid, int level,
				      struct btrfs_key *first_key)
{
	struct extent_buffer *buf = NULL;
	int ret;

	buf = btrfs_find_create_tree_block(fs_info, bytenr);
	if (IS_ERR(buf))
		return buf;

	ret = btree_read_extent_buffer_pages(fs_info, buf, parent_transid,
					     level, first_key);
	if (ret) {
		free_extent_buffer(buf);
		return ERR_PTR(ret);
	}
	return buf;

}

void clean_tree_block(struct btrfs_fs_info *fs_info,
		      struct extent_buffer *buf)
{
	if (btrfs_header_generation(buf) ==
	    fs_info->running_transaction->transid) {
		btrfs_assert_tree_locked(buf);

		if (test_and_clear_bit(EXTENT_BUFFER_DIRTY, &buf->bflags)) {
			percpu_counter_add_batch(&fs_info->dirty_metadata_bytes,
						 -buf->len,
						 fs_info->dirty_metadata_batch);
			/* ugh, clear_extent_buffer_dirty needs to lock the page */
			btrfs_set_lock_blocking(buf);
			clear_extent_buffer_dirty(buf);
		}
	}
}

static struct btrfs_subvolume_writers *btrfs_alloc_subvolume_writers(void)
{
	struct btrfs_subvolume_writers *writers;
	int ret;

	writers = kmalloc(sizeof(*writers), GFP_NOFS);
	if (!writers)
		return ERR_PTR(-ENOMEM);

	ret = percpu_counter_init(&writers->counter, 0, GFP_NOFS);
	if (ret < 0) {
		kfree(writers);
		return ERR_PTR(ret);
	}

	init_waitqueue_head(&writers->wait);
	return writers;
}

static void
btrfs_free_subvolume_writers(struct btrfs_subvolume_writers *writers)
{
	percpu_counter_destroy(&writers->counter);
	kfree(writers);
}

static void __setup_root(struct btrfs_root *root, struct btrfs_fs_info *fs_info,
			 u64 objectid)
{
	bool dummy = test_bit(BTRFS_FS_STATE_DUMMY_FS_INFO, &fs_info->fs_state);
	root->node = NULL;
	root->commit_root = NULL;
	root->state = 0;
	root->orphan_cleanup_state = 0;

	root->objectid = objectid;
	root->last_trans = 0;
	root->highest_objectid = 0;
	root->nr_delalloc_inodes = 0;
	root->nr_ordered_extents = 0;
	root->name = NULL;
	root->inode_tree = RB_ROOT;
	INIT_RADIX_TREE(&root->delayed_nodes_tree, GFP_ATOMIC);
	root->block_rsv = NULL;
	root->orphan_block_rsv = NULL;

	INIT_LIST_HEAD(&root->dirty_list);
	INIT_LIST_HEAD(&root->root_list);
	INIT_LIST_HEAD(&root->delalloc_inodes);
	INIT_LIST_HEAD(&root->delalloc_root);
	INIT_LIST_HEAD(&root->ordered_extents);
	INIT_LIST_HEAD(&root->ordered_root);
	INIT_LIST_HEAD(&root->logged_list[0]);
	INIT_LIST_HEAD(&root->logged_list[1]);
	spin_lock_init(&root->orphan_lock);
	spin_lock_init(&root->inode_lock);
	spin_lock_init(&root->delalloc_lock);
	spin_lock_init(&root->ordered_extent_lock);
	spin_lock_init(&root->accounting_lock);
	spin_lock_init(&root->log_extents_lock[0]);
	spin_lock_init(&root->log_extents_lock[1]);
	spin_lock_init(&root->qgroup_meta_rsv_lock);
	mutex_init(&root->objectid_mutex);
	mutex_init(&root->log_mutex);
	mutex_init(&root->ordered_extent_mutex);
	mutex_init(&root->delalloc_mutex);
	init_waitqueue_head(&root->log_writer_wait);
	init_waitqueue_head(&root->log_commit_wait[0]);
	init_waitqueue_head(&root->log_commit_wait[1]);
	INIT_LIST_HEAD(&root->log_ctxs[0]);
	INIT_LIST_HEAD(&root->log_ctxs[1]);
	atomic_set(&root->log_commit[0], 0);
	atomic_set(&root->log_commit[1], 0);
	atomic_set(&root->log_writers, 0);
	atomic_set(&root->log_batch, 0);
	atomic_set(&root->orphan_inodes, 0);
	refcount_set(&root->refs, 1);
	atomic_set(&root->will_be_snapshotted, 0);
	root->log_transid = 0;
	root->log_transid_committed = -1;
	root->last_log_commit = 0;
	if (!dummy)
		extent_io_tree_init(&root->dirty_log_pages, NULL);

	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	memset(&root->defrag_progress, 0, sizeof(root->defrag_progress));
	if (!dummy)
		root->defrag_trans_start = fs_info->generation;
	else
		root->defrag_trans_start = 0;
	root->root_key.objectid = objectid;
	root->anon_dev = 0;

	spin_lock_init(&root->root_item_lock);
}

static struct btrfs_root *btrfs_alloc_root(struct btrfs_fs_info *fs_info,
		gfp_t flags)
{
	struct btrfs_root *root = kzalloc(sizeof(*root), flags);
	if (root)
		root->fs_info = fs_info;
	return root;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
/* Should only be used by the testing infrastructure */
struct btrfs_root *btrfs_alloc_dummy_root(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;

	if (!fs_info)
		return ERR_PTR(-EINVAL);

	root = btrfs_alloc_root(fs_info, GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	/* We don't use the stripesize in selftest, set it as sectorsize */
	__setup_root(root, fs_info, BTRFS_ROOT_TREE_OBJECTID);
	root->alloc_bytenr = 0;

	return root;
}
#endif

struct btrfs_root *btrfs_create_tree(struct btrfs_trans_handle *trans,
				     struct btrfs_fs_info *fs_info,
				     u64 objectid)
{
	struct extent_buffer *leaf;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *root;
	struct btrfs_key key;
	int ret = 0;
	uuid_le uuid = NULL_UUID_LE;

	root = btrfs_alloc_root(fs_info, GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	__setup_root(root, fs_info, objectid);
	root->root_key.objectid = objectid;
	root->root_key.type = BTRFS_ROOT_ITEM_KEY;
	root->root_key.offset = 0;

	leaf = btrfs_alloc_tree_block(trans, root, 0, objectid, NULL, 0, 0, 0);
	if (IS_ERR(leaf)) {
		ret = PTR_ERR(leaf);
		leaf = NULL;
		goto fail;
	}

	memzero_extent_buffer(leaf, 0, sizeof(struct btrfs_header));
	btrfs_set_header_bytenr(leaf, leaf->start);
	btrfs_set_header_generation(leaf, trans->transid);
	btrfs_set_header_backref_rev(leaf, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(leaf, objectid);
	root->node = leaf;

	write_extent_buffer_fsid(leaf, fs_info->fsid);
	write_extent_buffer_chunk_tree_uuid(leaf, fs_info->chunk_tree_uuid);
	btrfs_mark_buffer_dirty(leaf);

	root->commit_root = btrfs_root_node(root);
	set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);

	root->root_item.flags = 0;
	root->root_item.byte_limit = 0;
	btrfs_set_root_bytenr(&root->root_item, leaf->start);
	btrfs_set_root_generation(&root->root_item, trans->transid);
	btrfs_set_root_level(&root->root_item, 0);
	btrfs_set_root_refs(&root->root_item, 1);
	btrfs_set_root_used(&root->root_item, leaf->len);
	btrfs_set_root_last_snapshot(&root->root_item, 0);
	btrfs_set_root_dirid(&root->root_item, 0);
	if (is_fstree(objectid))
		uuid_le_gen(&uuid);
	memcpy(root->root_item.uuid, uuid.b, BTRFS_UUID_SIZE);
	root->root_item.drop_level = 0;

	key.objectid = objectid;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = 0;
	ret = btrfs_insert_root(trans, tree_root, &key, &root->root_item);
	if (ret)
		goto fail;

	btrfs_tree_unlock(leaf);

	return root;

fail:
	if (leaf) {
		btrfs_tree_unlock(leaf);
		free_extent_buffer(root->commit_root);
		free_extent_buffer(leaf);
	}
	kfree(root);

	return ERR_PTR(ret);
}

static struct btrfs_root *alloc_log_tree(struct btrfs_trans_handle *trans,
					 struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;
	struct extent_buffer *leaf;

	root = btrfs_alloc_root(fs_info, GFP_NOFS);
	if (!root)
		return ERR_PTR(-ENOMEM);

	__setup_root(root, fs_info, BTRFS_TREE_LOG_OBJECTID);

	root->root_key.objectid = BTRFS_TREE_LOG_OBJECTID;
	root->root_key.type = BTRFS_ROOT_ITEM_KEY;
	root->root_key.offset = BTRFS_TREE_LOG_OBJECTID;

	/*
	 * DON'T set REF_COWS for log trees
	 *
	 * log trees do not get reference counted because they go away
	 * before a real commit is actually done.  They do store pointers
	 * to file data extents, and those reference counts still get
	 * updated (along with back refs to the log tree).
	 */

	leaf = btrfs_alloc_tree_block(trans, root, 0, BTRFS_TREE_LOG_OBJECTID,
			NULL, 0, 0, 0);
	if (IS_ERR(leaf)) {
		kfree(root);
		return ERR_CAST(leaf);
	}

	memzero_extent_buffer(leaf, 0, sizeof(struct btrfs_header));
	btrfs_set_header_bytenr(leaf, leaf->start);
	btrfs_set_header_generation(leaf, trans->transid);
	btrfs_set_header_backref_rev(leaf, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(leaf, BTRFS_TREE_LOG_OBJECTID);
	root->node = leaf;

	write_extent_buffer_fsid(root->node, fs_info->fsid);
	btrfs_mark_buffer_dirty(root->node);
	btrfs_tree_unlock(root->node);
	return root;
}

int btrfs_init_log_root_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *log_root;

	log_root = alloc_log_tree(trans, fs_info);
	if (IS_ERR(log_root))
		return PTR_ERR(log_root);
	WARN_ON(fs_info->log_root_tree);
	fs_info->log_root_tree = log_root;
	return 0;
}

int btrfs_add_log_tree(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *log_root;
	struct btrfs_inode_item *inode_item;

	log_root = alloc_log_tree(trans, fs_info);
	if (IS_ERR(log_root))
		return PTR_ERR(log_root);

	log_root->last_trans = trans->transid;
	log_root->root_key.offset = root->root_key.objectid;

	inode_item = &log_root->root_item.inode;
	btrfs_set_stack_inode_generation(inode_item, 1);
	btrfs_set_stack_inode_size(inode_item, 3);
	btrfs_set_stack_inode_nlink(inode_item, 1);
	btrfs_set_stack_inode_nbytes(inode_item,
				     fs_info->nodesize);
	btrfs_set_stack_inode_mode(inode_item, S_IFDIR | 0755);

	btrfs_set_root_node(&log_root->root_item, log_root->node);

	WARN_ON(root->log_root);
	root->log_root = log_root;
	root->log_transid = 0;
	root->log_transid_committed = -1;
	root->last_log_commit = 0;
	return 0;
}

static struct btrfs_root *btrfs_read_tree_root(struct btrfs_root *tree_root,
					       struct btrfs_key *key)
{
	struct btrfs_root *root;
	struct btrfs_fs_info *fs_info = tree_root->fs_info;
	struct btrfs_path *path;
	u64 generation;
	int ret;
	int level;

	path = btrfs_alloc_path();
	if (!path)
		return ERR_PTR(-ENOMEM);

	root = btrfs_alloc_root(fs_info, GFP_NOFS);
	if (!root) {
		ret = -ENOMEM;
		goto alloc_fail;
	}

	__setup_root(root, fs_info, key->objectid);

	ret = btrfs_find_root(tree_root, key, path,
			      &root->root_item, &root->root_key);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto find_fail;
	}

	generation = btrfs_root_generation(&root->root_item);
	level = btrfs_root_level(&root->root_item);
	root->node = read_tree_block(fs_info,
				     btrfs_root_bytenr(&root->root_item),
				     generation, level, NULL);
	if (IS_ERR(root->node)) {
		ret = PTR_ERR(root->node);
		goto find_fail;
	} else if (!btrfs_buffer_uptodate(root->node, generation, 0)) {
		ret = -EIO;
		free_extent_buffer(root->node);
		goto find_fail;
	}
	root->commit_root = btrfs_root_node(root);
out:
	btrfs_free_path(path);
	return root;

find_fail:
	kfree(root);
alloc_fail:
	root = ERR_PTR(ret);
	goto out;
}

struct btrfs_root *btrfs_read_fs_root(struct btrfs_root *tree_root,
				      struct btrfs_key *location)
{
	struct btrfs_root *root;

	root = btrfs_read_tree_root(tree_root, location);
	if (IS_ERR(root))
		return root;

	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
		set_bit(BTRFS_ROOT_REF_COWS, &root->state);
		btrfs_check_and_init_root_item(&root->root_item);
	}

	return root;
}

int btrfs_init_fs_root(struct btrfs_root *root)
{
	int ret;
	struct btrfs_subvolume_writers *writers;

	root->free_ino_ctl = kzalloc(sizeof(*root->free_ino_ctl), GFP_NOFS);
	root->free_ino_pinned = kzalloc(sizeof(*root->free_ino_pinned),
					GFP_NOFS);
	if (!root->free_ino_pinned || !root->free_ino_ctl) {
		ret = -ENOMEM;
		goto fail;
	}

	writers = btrfs_alloc_subvolume_writers();
	if (IS_ERR(writers)) {
		ret = PTR_ERR(writers);
		goto fail;
	}
	root->subv_writers = writers;

	btrfs_init_free_ino_ctl(root);
	spin_lock_init(&root->ino_cache_lock);
	init_waitqueue_head(&root->ino_cache_wait);

	ret = get_anon_bdev(&root->anon_dev);
	if (ret)
		goto fail;

	mutex_lock(&root->objectid_mutex);
	ret = btrfs_find_highest_objectid(root,
					&root->highest_objectid);
	if (ret) {
		mutex_unlock(&root->objectid_mutex);
		goto fail;
	}

	ASSERT(root->highest_objectid <= BTRFS_LAST_FREE_OBJECTID);

	mutex_unlock(&root->objectid_mutex);

	return 0;
fail:
	/* the caller is responsible to call free_fs_root */
	return ret;
}

struct btrfs_root *btrfs_lookup_fs_root(struct btrfs_fs_info *fs_info,
					u64 root_id)
{
	struct btrfs_root *root;

	spin_lock(&fs_info->fs_roots_radix_lock);
	root = radix_tree_lookup(&fs_info->fs_roots_radix,
				 (unsigned long)root_id);
	spin_unlock(&fs_info->fs_roots_radix_lock);
	return root;
}

int btrfs_insert_fs_root(struct btrfs_fs_info *fs_info,
			 struct btrfs_root *root)
{
	int ret;

	ret = radix_tree_preload(GFP_NOFS);
	if (ret)
		return ret;

	spin_lock(&fs_info->fs_roots_radix_lock);
	ret = radix_tree_insert(&fs_info->fs_roots_radix,
				(unsigned long)root->root_key.objectid,
				root);
	if (ret == 0)
		set_bit(BTRFS_ROOT_IN_RADIX, &root->state);
	spin_unlock(&fs_info->fs_roots_radix_lock);
	radix_tree_preload_end();

	return ret;
}

struct btrfs_root *btrfs_get_fs_root(struct btrfs_fs_info *fs_info,
				     struct btrfs_key *location,
				     bool check_ref)
{
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;

	if (location->objectid == BTRFS_ROOT_TREE_OBJECTID)
		return fs_info->tree_root;
	if (location->objectid == BTRFS_EXTENT_TREE_OBJECTID)
		return fs_info->extent_root;
	if (location->objectid == BTRFS_CHUNK_TREE_OBJECTID)
		return fs_info->chunk_root;
	if (location->objectid == BTRFS_DEV_TREE_OBJECTID)
		return fs_info->dev_root;
	if (location->objectid == BTRFS_CSUM_TREE_OBJECTID)
		return fs_info->csum_root;
	if (location->objectid == BTRFS_QUOTA_TREE_OBJECTID)
		return fs_info->quota_root ? fs_info->quota_root :
					     ERR_PTR(-ENOENT);
	if (location->objectid == BTRFS_UUID_TREE_OBJECTID)
		return fs_info->uuid_root ? fs_info->uuid_root :
					    ERR_PTR(-ENOENT);
	if (location->objectid == BTRFS_FREE_SPACE_TREE_OBJECTID)
		return fs_info->free_space_root ? fs_info->free_space_root :
						  ERR_PTR(-ENOENT);
again:
	root = btrfs_lookup_fs_root(fs_info, location->objectid);
	if (root) {
		if (check_ref && btrfs_root_refs(&root->root_item) == 0)
			return ERR_PTR(-ENOENT);
		return root;
	}

	root = btrfs_read_fs_root(fs_info->tree_root, location);
	if (IS_ERR(root))
		return root;

	if (check_ref && btrfs_root_refs(&root->root_item) == 0) {
		ret = -ENOENT;
		goto fail;
	}

	ret = btrfs_init_fs_root(root);
	if (ret)
		goto fail;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto fail;
	}
	key.objectid = BTRFS_ORPHAN_OBJECTID;
	key.type = BTRFS_ORPHAN_ITEM_KEY;
	key.offset = location->objectid;

	ret = btrfs_search_slot(NULL, fs_info->tree_root, &key, path, 0, 0);
	btrfs_free_path(path);
	if (ret < 0)
		goto fail;
	if (ret == 0)
		set_bit(BTRFS_ROOT_ORPHAN_ITEM_INSERTED, &root->state);

	ret = btrfs_insert_fs_root(fs_info, root);
	if (ret) {
		if (ret == -EEXIST) {
			free_fs_root(root);
			goto again;
		}
		goto fail;
	}
	return root;
fail:
	free_fs_root(root);
	return ERR_PTR(ret);
}

static int btrfs_congested_fn(void *congested_data, int bdi_bits)
{
	struct btrfs_fs_info *info = (struct btrfs_fs_info *)congested_data;
	int ret = 0;
	struct btrfs_device *device;
	struct backing_dev_info *bdi;

	rcu_read_lock();
	list_for_each_entry_rcu(device, &info->fs_devices->devices, dev_list) {
		if (!device->bdev)
			continue;
		bdi = device->bdev->bd_bdi;
		if (bdi_congested(bdi, bdi_bits)) {
			ret = 1;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

/*
 * called by the kthread helper functions to finally call the bio end_io
 * functions.  This is where read checksum verification actually happens
 */
static void end_workqueue_fn(struct btrfs_work *work)
{
	struct bio *bio;
	struct btrfs_end_io_wq *end_io_wq;

	end_io_wq = container_of(work, struct btrfs_end_io_wq, work);
	bio = end_io_wq->bio;

	bio->bi_status = end_io_wq->status;
	bio->bi_private = end_io_wq->private;
	bio->bi_end_io = end_io_wq->end_io;
	kmem_cache_free(btrfs_end_io_wq_cache, end_io_wq);
	bio_endio(bio);
}

static int cleaner_kthread(void *arg)
{
	struct btrfs_root *root = arg;
	struct btrfs_fs_info *fs_info = root->fs_info;
	int again;
	struct btrfs_trans_handle *trans;

	do {
		again = 0;

		/* Make the cleaner go to sleep early. */
		if (btrfs_need_cleaner_sleep(fs_info))
			goto sleep;

		/*
		 * Do not do anything if we might cause open_ctree() to block
		 * before we have finished mounting the filesystem.
		 */
		if (!test_bit(BTRFS_FS_OPEN, &fs_info->flags))
			goto sleep;

		if (!mutex_trylock(&fs_info->cleaner_mutex))
			goto sleep;

		/*
		 * Avoid the problem that we change the status of the fs
		 * during the above check and trylock.
		 */
		if (btrfs_need_cleaner_sleep(fs_info)) {
			mutex_unlock(&fs_info->cleaner_mutex);
			goto sleep;
		}

		mutex_lock(&fs_info->cleaner_delayed_iput_mutex);
		btrfs_run_delayed_iputs(fs_info);
		mutex_unlock(&fs_info->cleaner_delayed_iput_mutex);

		again = btrfs_clean_one_deleted_snapshot(root);
		mutex_unlock(&fs_info->cleaner_mutex);

		/*
		 * The defragger has dealt with the R/O remount and umount,
		 * needn't do anything special here.
		 */
		btrfs_run_defrag_inodes(fs_info);

		/*
		 * Acquires fs_info->delete_unused_bgs_mutex to avoid racing
		 * with relocation (btrfs_relocate_chunk) and relocation
		 * acquires fs_info->cleaner_mutex (btrfs_relocate_block_group)
		 * after acquiring fs_info->delete_unused_bgs_mutex. So we
		 * can't hold, nor need to, fs_info->cleaner_mutex when deleting
		 * unused block groups.
		 */
		btrfs_delete_unused_bgs(fs_info);
sleep:
		if (!again) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop())
				schedule();
			__set_current_state(TASK_RUNNING);
		}
	} while (!kthread_should_stop());

	/*
	 * Transaction kthread is stopped before us and wakes us up.
	 * However we might have started a new transaction and COWed some
	 * tree blocks when deleting unused block groups for example. So
	 * make sure we commit the transaction we started to have a clean
	 * shutdown when evicting the btree inode - if it has dirty pages
	 * when we do the final iput() on it, eviction will trigger a
	 * writeback for it which will fail with null pointer dereferences
	 * since work queues and other resources were already released and
	 * destroyed by the time the iput/eviction/writeback is made.
	 */
	trans = btrfs_attach_transaction(root);
	if (IS_ERR(trans)) {
		if (PTR_ERR(trans) != -ENOENT)
			btrfs_err(fs_info,
				  "cleaner transaction attach returned %ld",
				  PTR_ERR(trans));
	} else {
		int ret;

		ret = btrfs_commit_transaction(trans);
		if (ret)
			btrfs_err(fs_info,
				  "cleaner open transaction commit returned %d",
				  ret);
	}

	return 0;
}

static int transaction_kthread(void *arg)
{
	struct btrfs_root *root = arg;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_transaction *cur;
	u64 transid;
	unsigned long now;
	unsigned long delay;
	bool cannot_commit;

	do {
		cannot_commit = false;
		delay = HZ * fs_info->commit_interval;
		mutex_lock(&fs_info->transaction_kthread_mutex);

		spin_lock(&fs_info->trans_lock);
		cur = fs_info->running_transaction;
		if (!cur) {
			spin_unlock(&fs_info->trans_lock);
			goto sleep;
		}

		now = get_seconds();
		if (cur->state < TRANS_STATE_BLOCKED &&
		    (now < cur->start_time ||
		     now - cur->start_time < fs_info->commit_interval)) {
			spin_unlock(&fs_info->trans_lock);
			delay = HZ * 5;
			goto sleep;
		}
		transid = cur->transid;
		spin_unlock(&fs_info->trans_lock);

		/* If the file system is aborted, this will always fail. */
		trans = btrfs_attach_transaction(root);
		if (IS_ERR(trans)) {
			if (PTR_ERR(trans) != -ENOENT)
				cannot_commit = true;
			goto sleep;
		}
		if (transid == trans->transid) {
			btrfs_commit_transaction(trans);
		} else {
			btrfs_end_transaction(trans);
		}
sleep:
		wake_up_process(fs_info->cleaner_kthread);
		mutex_unlock(&fs_info->transaction_kthread_mutex);

		if (unlikely(test_bit(BTRFS_FS_STATE_ERROR,
				      &fs_info->fs_state)))
			btrfs_cleanup_transaction(fs_info);
		if (!kthread_should_stop() &&
				(!btrfs_transaction_blocked(fs_info) ||
				 cannot_commit))
			schedule_timeout_interruptible(delay);
	} while (!kthread_should_stop());
	return 0;
}

/*
 * this will find the highest generation in the array of
 * root backups.  The index of the highest array is returned,
 * or -1 if we can't find anything.
 *
 * We check to make sure the array is valid by comparing the
 * generation of the latest  root in the array with the generation
 * in the super block.  If they don't match we pitch it.
 */
static int find_newest_super_backup(struct btrfs_fs_info *info, u64 newest_gen)
{
	u64 cur;
	int newest_index = -1;
	struct btrfs_root_backup *root_backup;
	int i;

	for (i = 0; i < BTRFS_NUM_BACKUP_ROOTS; i++) {
		root_backup = info->super_copy->super_roots + i;
		cur = btrfs_backup_tree_root_gen(root_backup);
		if (cur == newest_gen)
			newest_index = i;
	}

	/* check to see if we actually wrapped around */
	if (newest_index == BTRFS_NUM_BACKUP_ROOTS - 1) {
		root_backup = info->super_copy->super_roots;
		cur = btrfs_backup_tree_root_gen(root_backup);
		if (cur == newest_gen)
			newest_index = 0;
	}
	return newest_index;
}


/*
 * find the oldest backup so we know where to store new entries
 * in the backup array.  This will set the backup_root_index
 * field in the fs_info struct
 */
static void find_oldest_super_backup(struct btrfs_fs_info *info,
				     u64 newest_gen)
{
	int newest_index = -1;

	newest_index = find_newest_super_backup(info, newest_gen);
	/* if there was garbage in there, just move along */
	if (newest_index == -1) {
		info->backup_root_index = 0;
	} else {
		info->backup_root_index = (newest_index + 1) % BTRFS_NUM_BACKUP_ROOTS;
	}
}

/*
 * copy all the root pointers into the super backup array.
 * this will bump the backup pointer by one when it is
 * done
 */
static void backup_super_roots(struct btrfs_fs_info *info)
{
	int next_backup;
	struct btrfs_root_backup *root_backup;
	int last_backup;

	next_backup = info->backup_root_index;
	last_backup = (next_backup + BTRFS_NUM_BACKUP_ROOTS - 1) %
		BTRFS_NUM_BACKUP_ROOTS;

	/*
	 * just overwrite the last backup if we're at the same generation
	 * this happens only at umount
	 */
	root_backup = info->super_for_commit->super_roots + last_backup;
	if (btrfs_backup_tree_root_gen(root_backup) ==
	    btrfs_header_generation(info->tree_root->node))
		next_backup = last_backup;

	root_backup = info->super_for_commit->super_roots + next_backup;

	/*
	 * make sure all of our padding and empty slots get zero filled
	 * regardless of which ones we use today
	 */
	memset(root_backup, 0, sizeof(*root_backup));

	info->backup_root_index = (next_backup + 1) % BTRFS_NUM_BACKUP_ROOTS;

	btrfs_set_backup_tree_root(root_backup, info->tree_root->node->start);
	btrfs_set_backup_tree_root_gen(root_backup,
			       btrfs_header_generation(info->tree_root->node));

	btrfs_set_backup_tree_root_level(root_backup,
			       btrfs_header_level(info->tree_root->node));

	btrfs_set_backup_chunk_root(root_backup, info->chunk_root->node->start);
	btrfs_set_backup_chunk_root_gen(root_backup,
			       btrfs_header_generation(info->chunk_root->node));
	btrfs_set_backup_chunk_root_level(root_backup,
			       btrfs_header_level(info->chunk_root->node));

	btrfs_set_backup_extent_root(root_backup, info->extent_root->node->start);
	btrfs_set_backup_extent_root_gen(root_backup,
			       btrfs_header_generation(info->extent_root->node));
	btrfs_set_backup_extent_root_level(root_backup,
			       btrfs_header_level(info->extent_root->node));

	/*
	 * we might commit during log recovery, which happens before we set
	 * the fs_root.  Make sure it is valid before we fill it in.
	 */
	if (info->fs_root && info->fs_root->node) {
		btrfs_set_backup_fs_root(root_backup,
					 info->fs_root->node->start);
		btrfs_set_backup_fs_root_gen(root_backup,
			       btrfs_header_generation(info->fs_root->node));
		btrfs_set_backup_fs_root_level(root_backup,
			       btrfs_header_level(info->fs_root->node));
	}

	btrfs_set_backup_dev_root(root_backup, info->dev_root->node->start);
	btrfs_set_backup_dev_root_gen(root_backup,
			       btrfs_header_generation(info->dev_root->node));
	btrfs_set_backup_dev_root_level(root_backup,
				       btrfs_header_level(info->dev_root->node));

	btrfs_set_backup_csum_root(root_backup, info->csum_root->node->start);
	btrfs_set_backup_csum_root_gen(root_backup,
			       btrfs_header_generation(info->csum_root->node));
	btrfs_set_backup_csum_root_level(root_backup,
			       btrfs_header_level(info->csum_root->node));

	btrfs_set_backup_total_bytes(root_backup,
			     btrfs_super_total_bytes(info->super_copy));
	btrfs_set_backup_bytes_used(root_backup,
			     btrfs_super_bytes_used(info->super_copy));
	btrfs_set_backup_num_devices(root_backup,
			     btrfs_super_num_devices(info->super_copy));

	/*
	 * if we don't copy this out to the super_copy, it won't get remembered
	 * for the next commit
	 */
	memcpy(&info->super_copy->super_roots,
	       &info->super_for_commit->super_roots,
	       sizeof(*root_backup) * BTRFS_NUM_BACKUP_ROOTS);
}

/*
 * this copies info out of the root backup array and back into
 * the in-memory super block.  It is meant to help iterate through
 * the array, so you send it the number of backups you've already
 * tried and the last backup index you used.
 *
 * this returns -1 when it has tried all the backups
 */
static noinline int next_root_backup(struct btrfs_fs_info *info,
				     struct btrfs_super_block *super,
				     int *num_backups_tried, int *backup_index)
{
	struct btrfs_root_backup *root_backup;
	int newest = *backup_index;

	if (*num_backups_tried == 0) {
		u64 gen = btrfs_super_generation(super);

		newest = find_newest_super_backup(info, gen);
		if (newest == -1)
			return -1;

		*backup_index = newest;
		*num_backups_tried = 1;
	} else if (*num_backups_tried == BTRFS_NUM_BACKUP_ROOTS) {
		/* we've tried all the backups, all done */
		return -1;
	} else {
		/* jump to the next oldest backup */
		newest = (*backup_index + BTRFS_NUM_BACKUP_ROOTS - 1) %
			BTRFS_NUM_BACKUP_ROOTS;
		*backup_index = newest;
		*num_backups_tried += 1;
	}
	root_backup = super->super_roots + newest;

	btrfs_set_super_generation(super,
				   btrfs_backup_tree_root_gen(root_backup));
	btrfs_set_super_root(super, btrfs_backup_tree_root(root_backup));
	btrfs_set_super_root_level(super,
				   btrfs_backup_tree_root_level(root_backup));
	btrfs_set_super_bytes_used(super, btrfs_backup_bytes_used(root_backup));

	/*
	 * fixme: the total bytes and num_devices need to match or we should
	 * need a fsck
	 */
	btrfs_set_super_total_bytes(super, btrfs_backup_total_bytes(root_backup));
	btrfs_set_super_num_devices(super, btrfs_backup_num_devices(root_backup));
	return 0;
}

/* helper to cleanup workers */
static void btrfs_stop_all_workers(struct btrfs_fs_info *fs_info)
{
	btrfs_destroy_workqueue(fs_info->fixup_workers);
	btrfs_destroy_workqueue(fs_info->delalloc_workers);
	btrfs_destroy_workqueue(fs_info->workers);
	btrfs_destroy_workqueue(fs_info->endio_workers);
	btrfs_destroy_workqueue(fs_info->endio_raid56_workers);
	btrfs_destroy_workqueue(fs_info->endio_repair_workers);
	btrfs_destroy_workqueue(fs_info->rmw_workers);
	btrfs_destroy_workqueue(fs_info->endio_write_workers);
	btrfs_destroy_workqueue(fs_info->endio_freespace_worker);
	btrfs_destroy_workqueue(fs_info->submit_workers);
	btrfs_destroy_workqueue(fs_info->delayed_workers);
	btrfs_destroy_workqueue(fs_info->caching_workers);
	btrfs_destroy_workqueue(fs_info->readahead_workers);
	btrfs_destroy_workqueue(fs_info->flush_workers);
	btrfs_destroy_workqueue(fs_info->qgroup_rescan_workers);
	btrfs_destroy_workqueue(fs_info->extent_workers);
	/*
	 * Now that all other work queues are destroyed, we can safely destroy
	 * the queues used for metadata I/O, since tasks from those other work
	 * queues can do metadata I/O operations.
	 */
	btrfs_destroy_workqueue(fs_info->endio_meta_workers);
	btrfs_destroy_workqueue(fs_info->endio_meta_write_workers);
}

static void free_root_extent_buffers(struct btrfs_root *root)
{
	if (root) {
		free_extent_buffer(root->node);
		free_extent_buffer(root->commit_root);
		root->node = NULL;
		root->commit_root = NULL;
	}
}

/* helper to cleanup tree roots */
static void free_root_pointers(struct btrfs_fs_info *info, int chunk_root)
{
	free_root_extent_buffers(info->tree_root);

	free_root_extent_buffers(info->dev_root);
	free_root_extent_buffers(info->extent_root);
	free_root_extent_buffers(info->csum_root);
	free_root_extent_buffers(info->quota_root);
	free_root_extent_buffers(info->uuid_root);
	if (chunk_root)
		free_root_extent_buffers(info->chunk_root);
	free_root_extent_buffers(info->free_space_root);
}

void btrfs_free_fs_roots(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *gang[8];
	int i;

	while (!list_empty(&fs_info->dead_roots)) {
		gang[0] = list_entry(fs_info->dead_roots.next,
				     struct btrfs_root, root_list);
		list_del(&gang[0]->root_list);

		if (test_bit(BTRFS_ROOT_IN_RADIX, &gang[0]->state)) {
			btrfs_drop_and_free_fs_root(fs_info, gang[0]);
		} else {
			free_extent_buffer(gang[0]->node);
			free_extent_buffer(gang[0]->commit_root);
			btrfs_put_fs_root(gang[0]);
		}
	}

	while (1) {
		ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, 0,
					     ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++)
			btrfs_drop_and_free_fs_root(fs_info, gang[i]);
	}

	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state)) {
		btrfs_free_log_root_tree(NULL, fs_info);
		btrfs_destroy_pinned_extent(fs_info, fs_info->pinned_extents);
	}
}

static void btrfs_init_scrub(struct btrfs_fs_info *fs_info)
{
	mutex_init(&fs_info->scrub_lock);
	atomic_set(&fs_info->scrubs_running, 0);
	atomic_set(&fs_info->scrub_pause_req, 0);
	atomic_set(&fs_info->scrubs_paused, 0);
	atomic_set(&fs_info->scrub_cancel_req, 0);
	init_waitqueue_head(&fs_info->scrub_pause_wait);
	fs_info->scrub_workers_refcnt = 0;
}

static void btrfs_init_balance(struct btrfs_fs_info *fs_info)
{
	spin_lock_init(&fs_info->balance_lock);
	mutex_init(&fs_info->balance_mutex);
	atomic_set(&fs_info->balance_running, 0);
	atomic_set(&fs_info->balance_pause_req, 0);
	atomic_set(&fs_info->balance_cancel_req, 0);
	fs_info->balance_ctl = NULL;
	init_waitqueue_head(&fs_info->balance_wait_q);
}

static void btrfs_init_btree_inode(struct btrfs_fs_info *fs_info)
{
	struct inode *inode = fs_info->btree_inode;

	inode->i_ino = BTRFS_BTREE_INODE_OBJECTID;
	set_nlink(inode, 1);
	/*
	 * we set the i_size on the btree inode to the max possible int.
	 * the real end of the address space is determined by all of
	 * the devices in the system
	 */
	inode->i_size = OFFSET_MAX;
	inode->i_mapping->a_ops = &btree_aops;

	RB_CLEAR_NODE(&BTRFS_I(inode)->rb_node);
	extent_io_tree_init(&BTRFS_I(inode)->io_tree, inode);
	BTRFS_I(inode)->io_tree.track_uptodate = 0;
	extent_map_tree_init(&BTRFS_I(inode)->extent_tree);

	BTRFS_I(inode)->io_tree.ops = &btree_extent_io_ops;

	BTRFS_I(inode)->root = fs_info->tree_root;
	memset(&BTRFS_I(inode)->location, 0, sizeof(struct btrfs_key));
	set_bit(BTRFS_INODE_DUMMY, &BTRFS_I(inode)->runtime_flags);
	btrfs_insert_inode_hash(inode);
}

static void btrfs_init_dev_replace_locks(struct btrfs_fs_info *fs_info)
{
	fs_info->dev_replace.lock_owner = 0;
	atomic_set(&fs_info->dev_replace.nesting_level, 0);
	mutex_init(&fs_info->dev_replace.lock_finishing_cancel_unmount);
	rwlock_init(&fs_info->dev_replace.lock);
	atomic_set(&fs_info->dev_replace.read_locks, 0);
	atomic_set(&fs_info->dev_replace.blocking_readers, 0);
	init_waitqueue_head(&fs_info->replace_wait);
	init_waitqueue_head(&fs_info->dev_replace.read_lock_wq);
}

static void btrfs_init_qgroup(struct btrfs_fs_info *fs_info)
{
	spin_lock_init(&fs_info->qgroup_lock);
	mutex_init(&fs_info->qgroup_ioctl_lock);
	fs_info->qgroup_tree = RB_ROOT;
	fs_info->qgroup_op_tree = RB_ROOT;
	INIT_LIST_HEAD(&fs_info->dirty_qgroups);
	fs_info->qgroup_seq = 1;
	fs_info->qgroup_ulist = NULL;
	fs_info->qgroup_rescan_running = false;
	mutex_init(&fs_info->qgroup_rescan_lock);
}

static int btrfs_init_workqueues(struct btrfs_fs_info *fs_info,
		struct btrfs_fs_devices *fs_devices)
{
	u32 max_active = fs_info->thread_pool_size;
	unsigned int flags = WQ_MEM_RECLAIM | WQ_FREEZABLE | WQ_UNBOUND;

	fs_info->workers =
		btrfs_alloc_workqueue(fs_info, "worker",
				      flags | WQ_HIGHPRI, max_active, 16);

	fs_info->delalloc_workers =
		btrfs_alloc_workqueue(fs_info, "delalloc",
				      flags, max_active, 2);

	fs_info->flush_workers =
		btrfs_alloc_workqueue(fs_info, "flush_delalloc",
				      flags, max_active, 0);

	fs_info->caching_workers =
		btrfs_alloc_workqueue(fs_info, "cache", flags, max_active, 0);

	/*
	 * a higher idle thresh on the submit workers makes it much more
	 * likely that bios will be send down in a sane order to the
	 * devices
	 */
	fs_info->submit_workers =
		btrfs_alloc_workqueue(fs_info, "submit", flags,
				      min_t(u64, fs_devices->num_devices,
					    max_active), 64);

	fs_info->fixup_workers =
		btrfs_alloc_workqueue(fs_info, "fixup", flags, 1, 0);

	/*
	 * endios are largely parallel and should have a very
	 * low idle thresh
	 */
	fs_info->endio_workers =
		btrfs_alloc_workqueue(fs_info, "endio", flags, max_active, 4);
	fs_info->endio_meta_workers =
		btrfs_alloc_workqueue(fs_info, "endio-meta", flags,
				      max_active, 4);
	fs_info->endio_meta_write_workers =
		btrfs_alloc_workqueue(fs_info, "endio-meta-write", flags,
				      max_active, 2);
	fs_info->endio_raid56_workers =
		btrfs_alloc_workqueue(fs_info, "endio-raid56", flags,
				      max_active, 4);
	fs_info->endio_repair_workers =
		btrfs_alloc_workqueue(fs_info, "endio-repair", flags, 1, 0);
	fs_info->rmw_workers =
		btrfs_alloc_workqueue(fs_info, "rmw", flags, max_active, 2);
	fs_info->endio_write_workers =
		btrfs_alloc_workqueue(fs_info, "endio-write", flags,
				      max_active, 2);
	fs_info->endio_freespace_worker =
		btrfs_alloc_workqueue(fs_info, "freespace-write", flags,
				      max_active, 0);
	fs_info->delayed_workers =
		btrfs_alloc_workqueue(fs_info, "delayed-meta", flags,
				      max_active, 0);
	fs_info->readahead_workers =
		btrfs_alloc_workqueue(fs_info, "readahead", flags,
				      max_active, 2);
	fs_info->qgroup_rescan_workers =
		btrfs_alloc_workqueue(fs_info, "qgroup-rescan", flags, 1, 0);
	fs_info->extent_workers =
		btrfs_alloc_workqueue(fs_info, "extent-refs", flags,
				      min_t(u64, fs_devices->num_devices,
					    max_active), 8);

	if (!(fs_info->workers && fs_info->delalloc_workers &&
	      fs_info->submit_workers && fs_info->flush_workers &&
	      fs_info->endio_workers && fs_info->endio_meta_workers &&
	      fs_info->endio_meta_write_workers &&
	      fs_info->endio_repair_workers &&
	      fs_info->endio_write_workers && fs_info->endio_raid56_workers &&
	      fs_info->endio_freespace_worker && fs_info->rmw_workers &&
	      fs_info->caching_workers && fs_info->readahead_workers &&
	      fs_info->fixup_workers && fs_info->delayed_workers &&
	      fs_info->extent_workers &&
	      fs_info->qgroup_rescan_workers)) {
		return -ENOMEM;
	}

	return 0;
}

static int btrfs_replay_log(struct btrfs_fs_info *fs_info,
			    struct btrfs_fs_devices *fs_devices)
{
	int ret;
	struct btrfs_root *log_tree_root;
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	u64 bytenr = btrfs_super_log_root(disk_super);
	int level = btrfs_super_log_root_level(disk_super);

	if (fs_devices->rw_devices == 0) {
		btrfs_warn(fs_info, "log replay required on RO media");
		return -EIO;
	}

	log_tree_root = btrfs_alloc_root(fs_info, GFP_KERNEL);
	if (!log_tree_root)
		return -ENOMEM;

	__setup_root(log_tree_root, fs_info, BTRFS_TREE_LOG_OBJECTID);

	log_tree_root->node = read_tree_block(fs_info, bytenr,
					      fs_info->generation + 1,
					      level, NULL);
	if (IS_ERR(log_tree_root->node)) {
		btrfs_warn(fs_info, "failed to read log tree");
		ret = PTR_ERR(log_tree_root->node);
		kfree(log_tree_root);
		return ret;
	} else if (!extent_buffer_uptodate(log_tree_root->node)) {
		btrfs_err(fs_info, "failed to read log tree");
		free_extent_buffer(log_tree_root->node);
		kfree(log_tree_root);
		return -EIO;
	}
	/* returns with log_tree_root freed on success */
	ret = btrfs_recover_log_trees(log_tree_root);
	if (ret) {
		btrfs_handle_fs_error(fs_info, ret,
				      "Failed to recover log tree");
		free_extent_buffer(log_tree_root->node);
		kfree(log_tree_root);
		return ret;
	}

	if (sb_rdonly(fs_info->sb)) {
		ret = btrfs_commit_super(fs_info);
		if (ret)
			return ret;
	}

	return 0;
}

static int btrfs_read_roots(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *root;
	struct btrfs_key location;
	int ret;

	BUG_ON(!fs_info->tree_root);

	location.objectid = BTRFS_EXTENT_TREE_OBJECTID;
	location.type = BTRFS_ROOT_ITEM_KEY;
	location.offset = 0;

	root = btrfs_read_tree_root(tree_root, &location);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out;
	}
	set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
	fs_info->extent_root = root;

	location.objectid = BTRFS_DEV_TREE_OBJECTID;
	root = btrfs_read_tree_root(tree_root, &location);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out;
	}
	set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
	fs_info->dev_root = root;
	btrfs_init_devices_late(fs_info);

	location.objectid = BTRFS_CSUM_TREE_OBJECTID;
	root = btrfs_read_tree_root(tree_root, &location);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out;
	}
	set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
	fs_info->csum_root = root;

	location.objectid = BTRFS_QUOTA_TREE_OBJECTID;
	root = btrfs_read_tree_root(tree_root, &location);
	if (!IS_ERR(root)) {
		set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
		set_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
		fs_info->quota_root = root;
	}

	location.objectid = BTRFS_UUID_TREE_OBJECTID;
	root = btrfs_read_tree_root(tree_root, &location);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		if (ret != -ENOENT)
			goto out;
	} else {
		set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
		fs_info->uuid_root = root;
	}

	if (btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE)) {
		location.objectid = BTRFS_FREE_SPACE_TREE_OBJECTID;
		root = btrfs_read_tree_root(tree_root, &location);
		if (IS_ERR(root)) {
			ret = PTR_ERR(root);
			goto out;
		}
		set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
		fs_info->free_space_root = root;
	}

	return 0;
out:
	btrfs_warn(fs_info, "failed to read root (objectid=%llu): %d",
		   location.objectid, ret);
	return ret;
}

int open_ctree(struct super_block *sb,
	       struct btrfs_fs_devices *fs_devices,
	       char *options)
{
	u32 sectorsize;
	u32 nodesize;
	u32 stripesize;
	u64 generation;
	u64 features;
	struct btrfs_key location;
	struct buffer_head *bh;
	struct btrfs_super_block *disk_super;
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_root *tree_root;
	struct btrfs_root *chunk_root;
	int ret;
	int err = -EINVAL;
	int num_backups_tried = 0;
	int backup_index = 0;
	int clear_free_space_tree = 0;
	int level;

	tree_root = fs_info->tree_root = btrfs_alloc_root(fs_info, GFP_KERNEL);
	chunk_root = fs_info->chunk_root = btrfs_alloc_root(fs_info, GFP_KERNEL);
	if (!tree_root || !chunk_root) {
		err = -ENOMEM;
		goto fail;
	}

	ret = init_srcu_struct(&fs_info->subvol_srcu);
	if (ret) {
		err = ret;
		goto fail;
	}

	ret = percpu_counter_init(&fs_info->dirty_metadata_bytes, 0, GFP_KERNEL);
	if (ret) {
		err = ret;
		goto fail_srcu;
	}
	fs_info->dirty_metadata_batch = PAGE_SIZE *
					(1 + ilog2(nr_cpu_ids));

	ret = percpu_counter_init(&fs_info->delalloc_bytes, 0, GFP_KERNEL);
	if (ret) {
		err = ret;
		goto fail_dirty_metadata_bytes;
	}

	ret = percpu_counter_init(&fs_info->bio_counter, 0, GFP_KERNEL);
	if (ret) {
		err = ret;
		goto fail_delalloc_bytes;
	}

	INIT_RADIX_TREE(&fs_info->fs_roots_radix, GFP_ATOMIC);
	INIT_RADIX_TREE(&fs_info->buffer_radix, GFP_ATOMIC);
	INIT_LIST_HEAD(&fs_info->trans_list);
	INIT_LIST_HEAD(&fs_info->dead_roots);
	INIT_LIST_HEAD(&fs_info->delayed_iputs);
	INIT_LIST_HEAD(&fs_info->delalloc_roots);
	INIT_LIST_HEAD(&fs_info->caching_block_groups);
	INIT_LIST_HEAD(&fs_info->pending_raid_kobjs);
	spin_lock_init(&fs_info->pending_raid_kobjs_lock);
	spin_lock_init(&fs_info->delalloc_root_lock);
	spin_lock_init(&fs_info->trans_lock);
	spin_lock_init(&fs_info->fs_roots_radix_lock);
	spin_lock_init(&fs_info->delayed_iput_lock);
	spin_lock_init(&fs_info->defrag_inodes_lock);
	spin_lock_init(&fs_info->tree_mod_seq_lock);
	spin_lock_init(&fs_info->super_lock);
	spin_lock_init(&fs_info->qgroup_op_lock);
	spin_lock_init(&fs_info->buffer_lock);
	spin_lock_init(&fs_info->unused_bgs_lock);
	rwlock_init(&fs_info->tree_mod_log_lock);
	mutex_init(&fs_info->unused_bg_unpin_mutex);
	mutex_init(&fs_info->delete_unused_bgs_mutex);
	mutex_init(&fs_info->reloc_mutex);
	mutex_init(&fs_info->delalloc_root_mutex);
	mutex_init(&fs_info->cleaner_delayed_iput_mutex);
	seqlock_init(&fs_info->profiles_lock);

	INIT_LIST_HEAD(&fs_info->dirty_cowonly_roots);
	INIT_LIST_HEAD(&fs_info->space_info);
	INIT_LIST_HEAD(&fs_info->tree_mod_seq_list);
	INIT_LIST_HEAD(&fs_info->unused_bgs);
	btrfs_mapping_init(&fs_info->mapping_tree);
	btrfs_init_block_rsv(&fs_info->global_block_rsv,
			     BTRFS_BLOCK_RSV_GLOBAL);
	btrfs_init_block_rsv(&fs_info->trans_block_rsv, BTRFS_BLOCK_RSV_TRANS);
	btrfs_init_block_rsv(&fs_info->chunk_block_rsv, BTRFS_BLOCK_RSV_CHUNK);
	btrfs_init_block_rsv(&fs_info->empty_block_rsv, BTRFS_BLOCK_RSV_EMPTY);
	btrfs_init_block_rsv(&fs_info->delayed_block_rsv,
			     BTRFS_BLOCK_RSV_DELOPS);
	atomic_set(&fs_info->async_delalloc_pages, 0);
	atomic_set(&fs_info->defrag_running, 0);
	atomic_set(&fs_info->qgroup_op_seq, 0);
	atomic_set(&fs_info->reada_works_cnt, 0);
	atomic64_set(&fs_info->tree_mod_seq, 0);
	fs_info->sb = sb;
	fs_info->max_inline = BTRFS_DEFAULT_MAX_INLINE;
	fs_info->metadata_ratio = 0;
	fs_info->defrag_inodes = RB_ROOT;
	atomic64_set(&fs_info->free_chunk_space, 0);
	fs_info->tree_mod_log = RB_ROOT;
	fs_info->commit_interval = BTRFS_DEFAULT_COMMIT_INTERVAL;
	fs_info->avg_delayed_ref_runtime = NSEC_PER_SEC >> 6; /* div by 64 */
	/* readahead state */
	INIT_RADIX_TREE(&fs_info->reada_tree, GFP_NOFS & ~__GFP_DIRECT_RECLAIM);
	spin_lock_init(&fs_info->reada_lock);
	btrfs_init_ref_verify(fs_info);

	fs_info->thread_pool_size = min_t(unsigned long,
					  num_online_cpus() + 2, 8);

	INIT_LIST_HEAD(&fs_info->ordered_roots);
	spin_lock_init(&fs_info->ordered_root_lock);

	fs_info->btree_inode = new_inode(sb);
	if (!fs_info->btree_inode) {
		err = -ENOMEM;
		goto fail_bio_counter;
	}
	mapping_set_gfp_mask(fs_info->btree_inode->i_mapping, GFP_NOFS);

	fs_info->delayed_root = kmalloc(sizeof(struct btrfs_delayed_root),
					GFP_KERNEL);
	if (!fs_info->delayed_root) {
		err = -ENOMEM;
		goto fail_iput;
	}
	btrfs_init_delayed_root(fs_info->delayed_root);

	btrfs_init_scrub(fs_info);
#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
	fs_info->check_integrity_print_mask = 0;
#endif
	btrfs_init_balance(fs_info);
	btrfs_init_async_reclaim_work(&fs_info->async_reclaim_work);

	sb->s_blocksize = BTRFS_BDEV_BLOCKSIZE;
	sb->s_blocksize_bits = blksize_bits(BTRFS_BDEV_BLOCKSIZE);

	btrfs_init_btree_inode(fs_info);

	spin_lock_init(&fs_info->block_group_cache_lock);
	fs_info->block_group_cache_tree = RB_ROOT;
	fs_info->first_logical_byte = (u64)-1;

	extent_io_tree_init(&fs_info->freed_extents[0], NULL);
	extent_io_tree_init(&fs_info->freed_extents[1], NULL);
	fs_info->pinned_extents = &fs_info->freed_extents[0];
	set_bit(BTRFS_FS_BARRIER, &fs_info->flags);

	mutex_init(&fs_info->ordered_operations_mutex);
	mutex_init(&fs_info->tree_log_mutex);
	mutex_init(&fs_info->chunk_mutex);
	mutex_init(&fs_info->transaction_kthread_mutex);
	mutex_init(&fs_info->cleaner_mutex);
	mutex_init(&fs_info->volume_mutex);
	mutex_init(&fs_info->ro_block_group_mutex);
	init_rwsem(&fs_info->commit_root_sem);
	init_rwsem(&fs_info->cleanup_work_sem);
	init_rwsem(&fs_info->subvol_sem);
	sema_init(&fs_info->uuid_tree_rescan_sem, 1);

	btrfs_init_dev_replace_locks(fs_info);
	btrfs_init_qgroup(fs_info);

	btrfs_init_free_cluster(&fs_info->meta_alloc_cluster);
	btrfs_init_free_cluster(&fs_info->data_alloc_cluster);

	init_waitqueue_head(&fs_info->transaction_throttle);
	init_waitqueue_head(&fs_info->transaction_wait);
	init_waitqueue_head(&fs_info->transaction_blocked_wait);
	init_waitqueue_head(&fs_info->async_submit_wait);

	INIT_LIST_HEAD(&fs_info->pinned_chunks);

	/* Usable values until the real ones are cached from the superblock */
	fs_info->nodesize = 4096;
	fs_info->sectorsize = 4096;
	fs_info->stripesize = 4096;

	ret = btrfs_alloc_stripe_hash_table(fs_info);
	if (ret) {
		err = ret;
		goto fail_alloc;
	}

	__setup_root(tree_root, fs_info, BTRFS_ROOT_TREE_OBJECTID);

	invalidate_bdev(fs_devices->latest_bdev);

	/*
	 * Read super block and check the signature bytes only
	 */
	bh = btrfs_read_dev_super(fs_devices->latest_bdev);
	if (IS_ERR(bh)) {
		err = PTR_ERR(bh);
		goto fail_alloc;
	}

	/*
	 * We want to check superblock checksum, the type is stored inside.
	 * Pass the whole disk block of size BTRFS_SUPER_INFO_SIZE (4k).
	 */
	if (btrfs_check_super_csum(fs_info, bh->b_data)) {
		btrfs_err(fs_info, "superblock checksum mismatch");
		err = -EINVAL;
		brelse(bh);
		goto fail_alloc;
	}

	/*
	 * super_copy is zeroed at allocation time and we never touch the
	 * following bytes up to INFO_SIZE, the checksum is calculated from
	 * the whole block of INFO_SIZE
	 */
	memcpy(fs_info->super_copy, bh->b_data, sizeof(*fs_info->super_copy));
	memcpy(fs_info->super_for_commit, fs_info->super_copy,
	       sizeof(*fs_info->super_for_commit));
	brelse(bh);

	memcpy(fs_info->fsid, fs_info->super_copy->fsid, BTRFS_FSID_SIZE);

	ret = btrfs_check_super_valid(fs_info);
	if (ret) {
		btrfs_err(fs_info, "superblock contains fatal errors");
		err = -EINVAL;
		goto fail_alloc;
	}

	disk_super = fs_info->super_copy;
	if (!btrfs_super_root(disk_super))
		goto fail_alloc;

	/* check FS state, whether FS is broken. */
	if (btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_ERROR)
		set_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state);

	/*
	 * run through our array of backup supers and setup
	 * our ring pointer to the oldest one
	 */
	generation = btrfs_super_generation(disk_super);
	find_oldest_super_backup(fs_info, generation);

	/*
	 * In the long term, we'll store the compression type in the super
	 * block, and it'll be used for per file compression control.
	 */
	fs_info->compress_type = BTRFS_COMPRESS_ZLIB;

	ret = btrfs_parse_options(fs_info, options, sb->s_flags);
	if (ret) {
		err = ret;
		goto fail_alloc;
	}

	features = btrfs_super_incompat_flags(disk_super) &
		~BTRFS_FEATURE_INCOMPAT_SUPP;
	if (features) {
		btrfs_err(fs_info,
		    "cannot mount because of unsupported optional features (%llx)",
		    features);
		err = -EINVAL;
		goto fail_alloc;
	}

	features = btrfs_super_incompat_flags(disk_super);
	features |= BTRFS_FEATURE_INCOMPAT_MIXED_BACKREF;
	if (fs_info->compress_type == BTRFS_COMPRESS_LZO)
		features |= BTRFS_FEATURE_INCOMPAT_COMPRESS_LZO;
	else if (fs_info->compress_type == BTRFS_COMPRESS_ZSTD)
		features |= BTRFS_FEATURE_INCOMPAT_COMPRESS_ZSTD;

	if (features & BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA)
		btrfs_info(fs_info, "has skinny extents");

	/*
	 * flag our filesystem as having big metadata blocks if
	 * they are bigger than the page size
	 */
	if (btrfs_super_nodesize(disk_super) > PAGE_SIZE) {
		if (!(features & BTRFS_FEATURE_INCOMPAT_BIG_METADATA))
			btrfs_info(fs_info,
				"flagging fs with big metadata feature");
		features |= BTRFS_FEATURE_INCOMPAT_BIG_METADATA;
	}

	nodesize = btrfs_super_nodesize(disk_super);
	sectorsize = btrfs_super_sectorsize(disk_super);
	stripesize = sectorsize;
	fs_info->dirty_metadata_batch = nodesize * (1 + ilog2(nr_cpu_ids));
	fs_info->delalloc_batch = sectorsize * 512 * (1 + ilog2(nr_cpu_ids));

	/* Cache block sizes */
	fs_info->nodesize = nodesize;
	fs_info->sectorsize = sectorsize;
	fs_info->stripesize = stripesize;

	/*
	 * mixed block groups end up with duplicate but slightly offset
	 * extent buffers for the same range.  It leads to corruptions
	 */
	if ((features & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS) &&
	    (sectorsize != nodesize)) {
		btrfs_err(fs_info,
"unequal nodesize/sectorsize (%u != %u) are not allowed for mixed block groups",
			nodesize, sectorsize);
		goto fail_alloc;
	}

	/*
	 * Needn't use the lock because there is no other task which will
	 * update the flag.
	 */
	btrfs_set_super_incompat_flags(disk_super, features);

	features = btrfs_super_compat_ro_flags(disk_super) &
		~BTRFS_FEATURE_COMPAT_RO_SUPP;
	if (!sb_rdonly(sb) && features) {
		btrfs_err(fs_info,
	"cannot mount read-write because of unsupported optional features (%llx)",
		       features);
		err = -EINVAL;
		goto fail_alloc;
	}

	ret = btrfs_init_workqueues(fs_info, fs_devices);
	if (ret) {
		err = ret;
		goto fail_sb_buffer;
	}

	sb->s_bdi->congested_fn = btrfs_congested_fn;
	sb->s_bdi->congested_data = fs_info;
	sb->s_bdi->capabilities |= BDI_CAP_CGROUP_WRITEBACK;
	sb->s_bdi->ra_pages = VM_MAX_READAHEAD * SZ_1K / PAGE_SIZE;
	sb->s_bdi->ra_pages *= btrfs_super_num_devices(disk_super);
	sb->s_bdi->ra_pages = max(sb->s_bdi->ra_pages, SZ_4M / PAGE_SIZE);

	sb->s_blocksize = sectorsize;
	sb->s_blocksize_bits = blksize_bits(sectorsize);
	memcpy(&sb->s_uuid, fs_info->fsid, BTRFS_FSID_SIZE);

	mutex_lock(&fs_info->chunk_mutex);
	ret = btrfs_read_sys_array(fs_info);
	mutex_unlock(&fs_info->chunk_mutex);
	if (ret) {
		btrfs_err(fs_info, "failed to read the system array: %d", ret);
		goto fail_sb_buffer;
	}

	generation = btrfs_super_chunk_root_generation(disk_super);
	level = btrfs_super_chunk_root_level(disk_super);

	__setup_root(chunk_root, fs_info, BTRFS_CHUNK_TREE_OBJECTID);

	chunk_root->node = read_tree_block(fs_info,
					   btrfs_super_chunk_root(disk_super),
					   generation, level, NULL);
	if (IS_ERR(chunk_root->node) ||
	    !extent_buffer_uptodate(chunk_root->node)) {
		btrfs_err(fs_info, "failed to read chunk root");
		if (!IS_ERR(chunk_root->node))
			free_extent_buffer(chunk_root->node);
		chunk_root->node = NULL;
		goto fail_tree_roots;
	}
	btrfs_set_root_node(&chunk_root->root_item, chunk_root->node);
	chunk_root->commit_root = btrfs_root_node(chunk_root);

	read_extent_buffer(chunk_root->node, fs_info->chunk_tree_uuid,
	   btrfs_header_chunk_tree_uuid(chunk_root->node), BTRFS_UUID_SIZE);

	ret = btrfs_read_chunk_tree(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to read chunk tree: %d", ret);
		goto fail_tree_roots;
	}

	/*
	 * Keep the devid that is marked to be the target device for the
	 * device replace procedure
	 */
	btrfs_free_extra_devids(fs_devices, 0);

	if (!fs_devices->latest_bdev) {
		btrfs_err(fs_info, "failed to read devices");
		goto fail_tree_roots;
	}

retry_root_backup:
	generation = btrfs_super_generation(disk_super);
	level = btrfs_super_root_level(disk_super);

	tree_root->node = read_tree_block(fs_info,
					  btrfs_super_root(disk_super),
					  generation, level, NULL);
	if (IS_ERR(tree_root->node) ||
	    !extent_buffer_uptodate(tree_root->node)) {
		btrfs_warn(fs_info, "failed to read tree root");
		if (!IS_ERR(tree_root->node))
			free_extent_buffer(tree_root->node);
		tree_root->node = NULL;
		goto recovery_tree_root;
	}

	btrfs_set_root_node(&tree_root->root_item, tree_root->node);
	tree_root->commit_root = btrfs_root_node(tree_root);
	btrfs_set_root_refs(&tree_root->root_item, 1);

	mutex_lock(&tree_root->objectid_mutex);
	ret = btrfs_find_highest_objectid(tree_root,
					&tree_root->highest_objectid);
	if (ret) {
		mutex_unlock(&tree_root->objectid_mutex);
		goto recovery_tree_root;
	}

	ASSERT(tree_root->highest_objectid <= BTRFS_LAST_FREE_OBJECTID);

	mutex_unlock(&tree_root->objectid_mutex);

	ret = btrfs_read_roots(fs_info);
	if (ret)
		goto recovery_tree_root;

	fs_info->generation = generation;
	fs_info->last_trans_committed = generation;

	ret = btrfs_recover_balance(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to recover balance: %d", ret);
		goto fail_block_groups;
	}

	ret = btrfs_init_dev_stats(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to init dev_stats: %d", ret);
		goto fail_block_groups;
	}

	ret = btrfs_init_dev_replace(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to init dev_replace: %d", ret);
		goto fail_block_groups;
	}

	btrfs_free_extra_devids(fs_devices, 1);

	ret = btrfs_sysfs_add_fsid(fs_devices, NULL);
	if (ret) {
		btrfs_err(fs_info, "failed to init sysfs fsid interface: %d",
				ret);
		goto fail_block_groups;
	}

	ret = btrfs_sysfs_add_device(fs_devices);
	if (ret) {
		btrfs_err(fs_info, "failed to init sysfs device interface: %d",
				ret);
		goto fail_fsdev_sysfs;
	}

	ret = btrfs_sysfs_add_mounted(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to init sysfs interface: %d", ret);
		goto fail_fsdev_sysfs;
	}

	ret = btrfs_init_space_info(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to initialize space info: %d", ret);
		goto fail_sysfs;
	}

	ret = btrfs_read_block_groups(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to read block groups: %d", ret);
		goto fail_sysfs;
	}

	if (!sb_rdonly(sb) && !btrfs_check_rw_degradable(fs_info, NULL)) {
		btrfs_warn(fs_info,
		"writeable mount is not allowed due to too many missing devices");
		goto fail_sysfs;
	}

	fs_info->cleaner_kthread = kthread_run(cleaner_kthread, tree_root,
					       "btrfs-cleaner");
	if (IS_ERR(fs_info->cleaner_kthread))
		goto fail_sysfs;

	fs_info->transaction_kthread = kthread_run(transaction_kthread,
						   tree_root,
						   "btrfs-transaction");
	if (IS_ERR(fs_info->transaction_kthread))
		goto fail_cleaner;

	if (!btrfs_test_opt(fs_info, NOSSD) &&
	    !fs_info->fs_devices->rotating) {
		btrfs_set_and_info(fs_info, SSD, "enabling ssd optimizations");
	}

	/*
	 * Mount does not set all options immediately, we can do it now and do
	 * not have to wait for transaction commit
	 */
	btrfs_apply_pending_changes(fs_info);

#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
	if (btrfs_test_opt(fs_info, CHECK_INTEGRITY)) {
		ret = btrfsic_mount(fs_info, fs_devices,
				    btrfs_test_opt(fs_info,
					CHECK_INTEGRITY_INCLUDING_EXTENT_DATA) ?
				    1 : 0,
				    fs_info->check_integrity_print_mask);
		if (ret)
			btrfs_warn(fs_info,
				"failed to initialize integrity check module: %d",
				ret);
	}
#endif
	ret = btrfs_read_qgroup_config(fs_info);
	if (ret)
		goto fail_trans_kthread;

	if (btrfs_build_ref_tree(fs_info))
		btrfs_err(fs_info, "couldn't build ref tree");

	/* do not make disk changes in broken FS or nologreplay is given */
	if (btrfs_super_log_root(disk_super) != 0 &&
	    !btrfs_test_opt(fs_info, NOLOGREPLAY)) {
		ret = btrfs_replay_log(fs_info, fs_devices);
		if (ret) {
			err = ret;
			goto fail_qgroup;
		}
	}

	ret = btrfs_find_orphan_roots(fs_info);
	if (ret)
		goto fail_qgroup;

	if (!sb_rdonly(sb)) {
		ret = btrfs_cleanup_fs_roots(fs_info);
		if (ret)
			goto fail_qgroup;

		mutex_lock(&fs_info->cleaner_mutex);
		ret = btrfs_recover_relocation(tree_root);
		mutex_unlock(&fs_info->cleaner_mutex);
		if (ret < 0) {
			btrfs_warn(fs_info, "failed to recover relocation: %d",
					ret);
			err = -EINVAL;
			goto fail_qgroup;
		}
	}

	location.objectid = BTRFS_FS_TREE_OBJECTID;
	location.type = BTRFS_ROOT_ITEM_KEY;
	location.offset = 0;

	fs_info->fs_root = btrfs_read_fs_root_no_name(fs_info, &location);
	if (IS_ERR(fs_info->fs_root)) {
		err = PTR_ERR(fs_info->fs_root);
		btrfs_warn(fs_info, "failed to read fs tree: %d", err);
		goto fail_qgroup;
	}

	if (sb_rdonly(sb))
		return 0;

	if (btrfs_test_opt(fs_info, CLEAR_CACHE) &&
	    btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE)) {
		clear_free_space_tree = 1;
	} else if (btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE) &&
		   !btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE_VALID)) {
		btrfs_warn(fs_info, "free space tree is invalid");
		clear_free_space_tree = 1;
	}

	if (clear_free_space_tree) {
		btrfs_info(fs_info, "clearing free space tree");
		ret = btrfs_clear_free_space_tree(fs_info);
		if (ret) {
			btrfs_warn(fs_info,
				   "failed to clear free space tree: %d", ret);
			close_ctree(fs_info);
			return ret;
		}
	}

	if (btrfs_test_opt(fs_info, FREE_SPACE_TREE) &&
	    !btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE)) {
		btrfs_info(fs_info, "creating free space tree");
		ret = btrfs_create_free_space_tree(fs_info);
		if (ret) {
			btrfs_warn(fs_info,
				"failed to create free space tree: %d", ret);
			close_ctree(fs_info);
			return ret;
		}
	}

	down_read(&fs_info->cleanup_work_sem);
	if ((ret = btrfs_orphan_cleanup(fs_info->fs_root)) ||
	    (ret = btrfs_orphan_cleanup(fs_info->tree_root))) {
		up_read(&fs_info->cleanup_work_sem);
		close_ctree(fs_info);
		return ret;
	}
	up_read(&fs_info->cleanup_work_sem);

	ret = btrfs_resume_balance_async(fs_info);
	if (ret) {
		btrfs_warn(fs_info, "failed to resume balance: %d", ret);
		close_ctree(fs_info);
		return ret;
	}

	ret = btrfs_resume_dev_replace_async(fs_info);
	if (ret) {
		btrfs_warn(fs_info, "failed to resume device replace: %d", ret);
		close_ctree(fs_info);
		return ret;
	}

	btrfs_qgroup_rescan_resume(fs_info);

	if (!fs_info->uuid_root) {
		btrfs_info(fs_info, "creating UUID tree");
		ret = btrfs_create_uuid_tree(fs_info);
		if (ret) {
			btrfs_warn(fs_info,
				"failed to create the UUID tree: %d", ret);
			close_ctree(fs_info);
			return ret;
		}
	} else if (btrfs_test_opt(fs_info, RESCAN_UUID_TREE) ||
		   fs_info->generation !=
				btrfs_super_uuid_tree_generation(disk_super)) {
		btrfs_info(fs_info, "checking UUID tree");
		ret = btrfs_check_uuid_tree(fs_info);
		if (ret) {
			btrfs_warn(fs_info,
				"failed to check the UUID tree: %d", ret);
			close_ctree(fs_info);
			return ret;
		}
	} else {
		set_bit(BTRFS_FS_UPDATE_UUID_TREE_GEN, &fs_info->flags);
	}
	set_bit(BTRFS_FS_OPEN, &fs_info->flags);

	/*
	 * backuproot only affect mount behavior, and if open_ctree succeeded,
	 * no need to keep the flag
	 */
	btrfs_clear_opt(fs_info->mount_opt, USEBACKUPROOT);

	return 0;

fail_qgroup:
	btrfs_free_qgroup_config(fs_info);
fail_trans_kthread:
	kthread_stop(fs_info->transaction_kthread);
	btrfs_cleanup_transaction(fs_info);
	btrfs_free_fs_roots(fs_info);
fail_cleaner:
	kthread_stop(fs_info->cleaner_kthread);

	/*
	 * make sure we're done with the btree inode before we stop our
	 * kthreads
	 */
	filemap_write_and_wait(fs_info->btree_inode->i_mapping);

fail_sysfs:
	btrfs_sysfs_remove_mounted(fs_info);

fail_fsdev_sysfs:
	btrfs_sysfs_remove_fsid(fs_info->fs_devices);

fail_block_groups:
	btrfs_put_block_group_cache(fs_info);

fail_tree_roots:
	free_root_pointers(fs_info, 1);
	invalidate_inode_pages2(fs_info->btree_inode->i_mapping);

fail_sb_buffer:
	btrfs_stop_all_workers(fs_info);
	btrfs_free_block_groups(fs_info);
fail_alloc:
fail_iput:
	btrfs_mapping_tree_free(&fs_info->mapping_tree);

	iput(fs_info->btree_inode);
fail_bio_counter:
	percpu_counter_destroy(&fs_info->bio_counter);
fail_delalloc_bytes:
	percpu_counter_destroy(&fs_info->delalloc_bytes);
fail_dirty_metadata_bytes:
	percpu_counter_destroy(&fs_info->dirty_metadata_bytes);
fail_srcu:
	cleanup_srcu_struct(&fs_info->subvol_srcu);
fail:
	btrfs_free_stripe_hash_table(fs_info);
	btrfs_close_devices(fs_info->fs_devices);
	return err;

recovery_tree_root:
	if (!btrfs_test_opt(fs_info, USEBACKUPROOT))
		goto fail_tree_roots;

	free_root_pointers(fs_info, 0);

	/* don't use the log in recovery mode, it won't be valid */
	btrfs_set_super_log_root(disk_super, 0);

	/* we can't trust the free space cache either */
	btrfs_set_opt(fs_info->mount_opt, CLEAR_CACHE);

	ret = next_root_backup(fs_info, fs_info->super_copy,
			       &num_backups_tried, &backup_index);
	if (ret == -1)
		goto fail_block_groups;
	goto retry_root_backup;
}
ALLOW_ERROR_INJECTION(open_ctree, ERRNO);

static void btrfs_end_buffer_write_sync(struct buffer_head *bh, int uptodate)
{
	if (uptodate) {
		set_buffer_uptodate(bh);
	} else {
		struct btrfs_device *device = (struct btrfs_device *)
			bh->b_private;

		btrfs_warn_rl_in_rcu(device->fs_info,
				"lost page write due to IO error on %s",
					  rcu_str_deref(device->name));
		/* note, we don't set_buffer_write_io_error because we have
		 * our own ways of dealing with the IO errors
		 */
		clear_buffer_uptodate(bh);
		btrfs_dev_stat_inc_and_print(device, BTRFS_DEV_STAT_WRITE_ERRS);
	}
	unlock_buffer(bh);
	put_bh(bh);
}

int btrfs_read_dev_one_super(struct block_device *bdev, int copy_num,
			struct buffer_head **bh_ret)
{
	struct buffer_head *bh;
	struct btrfs_super_block *super;
	u64 bytenr;

	bytenr = btrfs_sb_offset(copy_num);
	if (bytenr + BTRFS_SUPER_INFO_SIZE >= i_size_read(bdev->bd_inode))
		return -EINVAL;

	bh = __bread(bdev, bytenr / BTRFS_BDEV_BLOCKSIZE, BTRFS_SUPER_INFO_SIZE);
	/*
	 * If we fail to read from the underlying devices, as of now
	 * the best option we have is to mark it EIO.
	 */
	if (!bh)
		return -EIO;

	super = (struct btrfs_super_block *)bh->b_data;
	if (btrfs_super_bytenr(super) != bytenr ||
		    btrfs_super_magic(super) != BTRFS_MAGIC) {
		brelse(bh);
		return -EINVAL;
	}

	*bh_ret = bh;
	return 0;
}


struct buffer_head *btrfs_read_dev_super(struct block_device *bdev)
{
	struct buffer_head *bh;
	struct buffer_head *latest = NULL;
	struct btrfs_super_block *super;
	int i;
	u64 transid = 0;
	int ret = -EINVAL;

	/* we would like to check all the supers, but that would make
	 * a btrfs mount succeed after a mkfs from a different FS.
	 * So, we need to add a special mount option to scan for
	 * later supers, using BTRFS_SUPER_MIRROR_MAX instead
	 */
	for (i = 0; i < 1; i++) {
		ret = btrfs_read_dev_one_super(bdev, i, &bh);
		if (ret)
			continue;

		super = (struct btrfs_super_block *)bh->b_data;

		if (!latest || btrfs_super_generation(super) > transid) {
			brelse(latest);
			latest = bh;
			transid = btrfs_super_generation(super);
		} else {
			brelse(bh);
		}
	}

	if (!latest)
		return ERR_PTR(ret);

	return latest;
}

/*
 * Write superblock @sb to the @device. Do not wait for completion, all the
 * buffer heads we write are pinned.
 *
 * Write @max_mirrors copies of the superblock, where 0 means default that fit
 * the expected device size at commit time. Note that max_mirrors must be
 * same for write and wait phases.
 *
 * Return number of errors when buffer head is not found or submission fails.
 */
static int write_dev_supers(struct btrfs_device *device,
			    struct btrfs_super_block *sb, int max_mirrors)
{
	struct buffer_head *bh;
	int i;
	int ret;
	int errors = 0;
	u32 crc;
	u64 bytenr;
	int op_flags;

	if (max_mirrors == 0)
		max_mirrors = BTRFS_SUPER_MIRROR_MAX;

	for (i = 0; i < max_mirrors; i++) {
		bytenr = btrfs_sb_offset(i);
		if (bytenr + BTRFS_SUPER_INFO_SIZE >=
		    device->commit_total_bytes)
			break;

		btrfs_set_super_bytenr(sb, bytenr);

		crc = ~(u32)0;
		crc = btrfs_csum_data((const char *)sb + BTRFS_CSUM_SIZE, crc,
				      BTRFS_SUPER_INFO_SIZE - BTRFS_CSUM_SIZE);
		btrfs_csum_final(crc, sb->csum);

		/* One reference for us, and we leave it for the caller */
		bh = __getblk(device->bdev, bytenr / BTRFS_BDEV_BLOCKSIZE,
			      BTRFS_SUPER_INFO_SIZE);
		if (!bh) {
			btrfs_err(device->fs_info,
			    "couldn't get super buffer head for bytenr %llu",
			    bytenr);
			errors++;
			continue;
		}

		memcpy(bh->b_data, sb, BTRFS_SUPER_INFO_SIZE);

		/* one reference for submit_bh */
		get_bh(bh);

		set_buffer_uptodate(bh);
		lock_buffer(bh);
		bh->b_end_io = btrfs_end_buffer_write_sync;
		bh->b_private = device;

		/*
		 * we fua the first super.  The others we allow
		 * to go down lazy.
		 */
		op_flags = REQ_SYNC | REQ_META | REQ_PRIO;
		if (i == 0 && !btrfs_test_opt(device->fs_info, NOBARRIER))
			op_flags |= REQ_FUA;
		ret = btrfsic_submit_bh(REQ_OP_WRITE, op_flags, bh);
		if (ret)
			errors++;
	}
	return errors < i ? 0 : -1;
}

/*
 * Wait for write completion of superblocks done by write_dev_supers,
 * @max_mirrors same for write and wait phases.
 *
 * Return number of errors when buffer head is not found or not marked up to
 * date.
 */
static int wait_dev_supers(struct btrfs_device *device, int max_mirrors)
{
	struct buffer_head *bh;
	int i;
	int errors = 0;
	bool primary_failed = false;
	u64 bytenr;

	if (max_mirrors == 0)
		max_mirrors = BTRFS_SUPER_MIRROR_MAX;

	for (i = 0; i < max_mirrors; i++) {
		bytenr = btrfs_sb_offset(i);
		if (bytenr + BTRFS_SUPER_INFO_SIZE >=
		    device->commit_total_bytes)
			break;

		bh = __find_get_block(device->bdev,
				      bytenr / BTRFS_BDEV_BLOCKSIZE,
				      BTRFS_SUPER_INFO_SIZE);
		if (!bh) {
			errors++;
			if (i == 0)
				primary_failed = true;
			continue;
		}
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			errors++;
			if (i == 0)
				primary_failed = true;
		}

		/* drop our reference */
		brelse(bh);

		/* drop the reference from the writing run */
		brelse(bh);
	}

	/* log error, force error return */
	if (primary_failed) {
		btrfs_err(device->fs_info, "error writing primary super block to device %llu",
			  device->devid);
		return -1;
	}

	return errors < i ? 0 : -1;
}

/*
 * endio for the write_dev_flush, this will wake anyone waiting
 * for the barrier when it is done
 */
static void btrfs_end_empty_barrier(struct bio *bio)
{
	complete(bio->bi_private);
}

/*
 * Submit a flush request to the device if it supports it. Error handling is
 * done in the waiting counterpart.
 */
static void write_dev_flush(struct btrfs_device *device)
{
	struct request_queue *q = bdev_get_queue(device->bdev);
	struct bio *bio = device->flush_bio;

	if (!test_bit(QUEUE_FLAG_WC, &q->queue_flags))
		return;

	bio_reset(bio);
	bio->bi_end_io = btrfs_end_empty_barrier;
	bio_set_dev(bio, device->bdev);
	bio->bi_opf = REQ_OP_WRITE | REQ_SYNC | REQ_PREFLUSH;
	init_completion(&device->flush_wait);
	bio->bi_private = &device->flush_wait;

	btrfsic_submit_bio(bio);
	set_bit(BTRFS_DEV_STATE_FLUSH_SENT, &device->dev_state);
}

/*
 * If the flush bio has been submitted by write_dev_flush, wait for it.
 */
static blk_status_t wait_dev_flush(struct btrfs_device *device)
{
	struct bio *bio = device->flush_bio;

	if (!test_bit(BTRFS_DEV_STATE_FLUSH_SENT, &device->dev_state))
		return BLK_STS_OK;

	clear_bit(BTRFS_DEV_STATE_FLUSH_SENT, &device->dev_state);
	wait_for_completion_io(&device->flush_wait);

	return bio->bi_status;
}

static int check_barrier_error(struct btrfs_fs_info *fs_info)
{
	if (!btrfs_check_rw_degradable(fs_info, NULL))
		return -EIO;
	return 0;
}

/*
 * send an empty flush down to each device in parallel,
 * then wait for them
 */
static int barrier_all_devices(struct btrfs_fs_info *info)
{
	struct list_head *head;
	struct btrfs_device *dev;
	int errors_wait = 0;
	blk_status_t ret;

	lockdep_assert_held(&info->fs_devices->device_list_mutex);
	/* send down all the barriers */
	head = &info->fs_devices->devices;
	list_for_each_entry(dev, head, dev_list) {
		if (test_bit(BTRFS_DEV_STATE_MISSING, &dev->dev_state))
			continue;
		if (!dev->bdev)
			continue;
		if (!test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &dev->dev_state) ||
		    !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state))
			continue;

		write_dev_flush(dev);
		dev->last_flush_error = BLK_STS_OK;
	}

	/* wait for all the barriers */
	list_for_each_entry(dev, head, dev_list) {
		if (test_bit(BTRFS_DEV_STATE_MISSING, &dev->dev_state))
			continue;
		if (!dev->bdev) {
			errors_wait++;
			continue;
		}
		if (!test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &dev->dev_state) ||
		    !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state))
			continue;

		ret = wait_dev_flush(dev);
		if (ret) {
			dev->last_flush_error = ret;
			btrfs_dev_stat_inc_and_print(dev,
					BTRFS_DEV_STAT_FLUSH_ERRS);
			errors_wait++;
		}
	}

	if (errors_wait) {
		/*
		 * At some point we need the status of all disks
		 * to arrive at the volume status. So error checking
		 * is being pushed to a separate loop.
		 */
		return check_barrier_error(info);
	}
	return 0;
}

int btrfs_get_num_tolerated_disk_barrier_failures(u64 flags)
{
	int raid_type;
	int min_tolerated = INT_MAX;

	if ((flags & BTRFS_BLOCK_GROUP_PROFILE_MASK) == 0 ||
	    (flags & BTRFS_AVAIL_ALLOC_BIT_SINGLE))
		min_tolerated = min(min_tolerated,
				    btrfs_raid_array[BTRFS_RAID_SINGLE].
				    tolerated_failures);

	for (raid_type = 0; raid_type < BTRFS_NR_RAID_TYPES; raid_type++) {
		if (raid_type == BTRFS_RAID_SINGLE)
			continue;
		if (!(flags & btrfs_raid_group[raid_type]))
			continue;
		min_tolerated = min(min_tolerated,
				    btrfs_raid_array[raid_type].
				    tolerated_failures);
	}

	if (min_tolerated == INT_MAX) {
		pr_warn("BTRFS: unknown raid flag: %llu", flags);
		min_tolerated = 0;
	}

	return min_tolerated;
}

int write_all_supers(struct btrfs_fs_info *fs_info, int max_mirrors)
{
	struct list_head *head;
	struct btrfs_device *dev;
	struct btrfs_super_block *sb;
	struct btrfs_dev_item *dev_item;
	int ret;
	int do_barriers;
	int max_errors;
	int total_errors = 0;
	u64 flags;

	do_barriers = !btrfs_test_opt(fs_info, NOBARRIER);

	/*
	 * max_mirrors == 0 indicates we're from commit_transaction,
	 * not from fsync where the tree roots in fs_info have not
	 * been consistent on disk.
	 */
	if (max_mirrors == 0)
		backup_super_roots(fs_info);

	sb = fs_info->super_for_commit;
	dev_item = &sb->dev_item;

	mutex_lock(&fs_info->fs_devices->device_list_mutex);
	head = &fs_info->fs_devices->devices;
	max_errors = btrfs_super_num_devices(fs_info->super_copy) - 1;

	if (do_barriers) {
		ret = barrier_all_devices(fs_info);
		if (ret) {
			mutex_unlock(
				&fs_info->fs_devices->device_list_mutex);
			btrfs_handle_fs_error(fs_info, ret,
					      "errors while submitting device barriers.");
			return ret;
		}
	}

	list_for_each_entry(dev, head, dev_list) {
		if (!dev->bdev) {
			total_errors++;
			continue;
		}
		if (!test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &dev->dev_state) ||
		    !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state))
			continue;

		btrfs_set_stack_device_generation(dev_item, 0);
		btrfs_set_stack_device_type(dev_item, dev->type);
		btrfs_set_stack_device_id(dev_item, dev->devid);
		btrfs_set_stack_device_total_bytes(dev_item,
						   dev->commit_total_bytes);
		btrfs_set_stack_device_bytes_used(dev_item,
						  dev->commit_bytes_used);
		btrfs_set_stack_device_io_align(dev_item, dev->io_align);
		btrfs_set_stack_device_io_width(dev_item, dev->io_width);
		btrfs_set_stack_device_sector_size(dev_item, dev->sector_size);
		memcpy(dev_item->uuid, dev->uuid, BTRFS_UUID_SIZE);
		memcpy(dev_item->fsid, dev->fs_devices->fsid, BTRFS_FSID_SIZE);

		flags = btrfs_super_flags(sb);
		btrfs_set_super_flags(sb, flags | BTRFS_HEADER_FLAG_WRITTEN);

		ret = write_dev_supers(dev, sb, max_mirrors);
		if (ret)
			total_errors++;
	}
	if (total_errors > max_errors) {
		btrfs_err(fs_info, "%d errors while writing supers",
			  total_errors);
		mutex_unlock(&fs_info->fs_devices->device_list_mutex);

		/* FUA is masked off if unsupported and can't be the reason */
		btrfs_handle_fs_error(fs_info, -EIO,
				      "%d errors while writing supers",
				      total_errors);
		return -EIO;
	}

	total_errors = 0;
	list_for_each_entry(dev, head, dev_list) {
		if (!dev->bdev)
			continue;
		if (!test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &dev->dev_state) ||
		    !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state))
			continue;

		ret = wait_dev_supers(dev, max_mirrors);
		if (ret)
			total_errors++;
	}
	mutex_unlock(&fs_info->fs_devices->device_list_mutex);
	if (total_errors > max_errors) {
		btrfs_handle_fs_error(fs_info, -EIO,
				      "%d errors while writing supers",
				      total_errors);
		return -EIO;
	}
	return 0;
}

/* Drop a fs root from the radix tree and free it. */
void btrfs_drop_and_free_fs_root(struct btrfs_fs_info *fs_info,
				  struct btrfs_root *root)
{
	spin_lock(&fs_info->fs_roots_radix_lock);
	radix_tree_delete(&fs_info->fs_roots_radix,
			  (unsigned long)root->root_key.objectid);
	spin_unlock(&fs_info->fs_roots_radix_lock);

	if (btrfs_root_refs(&root->root_item) == 0)
		synchronize_srcu(&fs_info->subvol_srcu);

	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state)) {
		btrfs_free_log(NULL, root);
		if (root->reloc_root) {
			free_extent_buffer(root->reloc_root->node);
			free_extent_buffer(root->reloc_root->commit_root);
			btrfs_put_fs_root(root->reloc_root);
			root->reloc_root = NULL;
		}
	}

	if (root->free_ino_pinned)
		__btrfs_remove_free_space_cache(root->free_ino_pinned);
	if (root->free_ino_ctl)
		__btrfs_remove_free_space_cache(root->free_ino_ctl);
	free_fs_root(root);
}

static void free_fs_root(struct btrfs_root *root)
{
	iput(root->ino_cache_inode);
	WARN_ON(!RB_EMPTY_ROOT(&root->inode_tree));
	btrfs_free_block_rsv(root->fs_info, root->orphan_block_rsv);
	root->orphan_block_rsv = NULL;
	if (root->anon_dev)
		free_anon_bdev(root->anon_dev);
	if (root->subv_writers)
		btrfs_free_subvolume_writers(root->subv_writers);
	free_extent_buffer(root->node);
	free_extent_buffer(root->commit_root);
	kfree(root->free_ino_ctl);
	kfree(root->free_ino_pinned);
	kfree(root->name);
	btrfs_put_fs_root(root);
}

void btrfs_free_fs_root(struct btrfs_root *root)
{
	free_fs_root(root);
}

int btrfs_cleanup_fs_roots(struct btrfs_fs_info *fs_info)
{
	u64 root_objectid = 0;
	struct btrfs_root *gang[8];
	int i = 0;
	int err = 0;
	unsigned int ret = 0;
	int index;

	while (1) {
		index = srcu_read_lock(&fs_info->subvol_srcu);
		ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, root_objectid,
					     ARRAY_SIZE(gang));
		if (!ret) {
			srcu_read_unlock(&fs_info->subvol_srcu, index);
			break;
		}
		root_objectid = gang[ret - 1]->root_key.objectid + 1;

		for (i = 0; i < ret; i++) {
			/* Avoid to grab roots in dead_roots */
			if (btrfs_root_refs(&gang[i]->root_item) == 0) {
				gang[i] = NULL;
				continue;
			}
			/* grab all the search result for later use */
			gang[i] = btrfs_grab_fs_root(gang[i]);
		}
		srcu_read_unlock(&fs_info->subvol_srcu, index);

		for (i = 0; i < ret; i++) {
			if (!gang[i])
				continue;
			root_objectid = gang[i]->root_key.objectid;
			err = btrfs_orphan_cleanup(gang[i]);
			if (err)
				break;
			btrfs_put_fs_root(gang[i]);
		}
		root_objectid++;
	}

	/* release the uncleaned roots due to error */
	for (; i < ret; i++) {
		if (gang[i])
			btrfs_put_fs_root(gang[i]);
	}
	return err;
}

int btrfs_commit_super(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root = fs_info->tree_root;
	struct btrfs_trans_handle *trans;

	mutex_lock(&fs_info->cleaner_mutex);
	btrfs_run_delayed_iputs(fs_info);
	mutex_unlock(&fs_info->cleaner_mutex);
	wake_up_process(fs_info->cleaner_kthread);

	/* wait until ongoing cleanup work done */
	down_write(&fs_info->cleanup_work_sem);
	up_write(&fs_info->cleanup_work_sem);

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);
	return btrfs_commit_transaction(trans);
}

void close_ctree(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root = fs_info->tree_root;
	int ret;

	set_bit(BTRFS_FS_CLOSING_START, &fs_info->flags);

	/* wait for the qgroup rescan worker to stop */
	btrfs_qgroup_wait_for_completion(fs_info, false);

	/* wait for the uuid_scan task to finish */
	down(&fs_info->uuid_tree_rescan_sem);
	/* avoid complains from lockdep et al., set sem back to initial state */
	up(&fs_info->uuid_tree_rescan_sem);

	/* pause restriper - we want to resume on mount */
	btrfs_pause_balance(fs_info);

	btrfs_dev_replace_suspend_for_unmount(fs_info);

	btrfs_scrub_cancel(fs_info);

	/* wait for any defraggers to finish */
	wait_event(fs_info->transaction_wait,
		   (atomic_read(&fs_info->defrag_running) == 0));

	/* clear out the rbtree of defraggable inodes */
	btrfs_cleanup_defrag_inodes(fs_info);

	cancel_work_sync(&fs_info->async_reclaim_work);

	if (!sb_rdonly(fs_info->sb)) {
		/*
		 * If the cleaner thread is stopped and there are
		 * block groups queued for removal, the deletion will be
		 * skipped when we quit the cleaner thread.
		 */
		btrfs_delete_unused_bgs(fs_info);

		ret = btrfs_commit_super(fs_info);
		if (ret)
			btrfs_err(fs_info, "commit super ret %d", ret);
	}

	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state) ||
	    test_bit(BTRFS_FS_STATE_TRANS_ABORTED, &fs_info->fs_state))
		btrfs_error_commit_super(fs_info);

	kthread_stop(fs_info->transaction_kthread);
	kthread_stop(fs_info->cleaner_kthread);

	set_bit(BTRFS_FS_CLOSING_DONE, &fs_info->flags);

	btrfs_free_qgroup_config(fs_info);

	if (percpu_counter_sum(&fs_info->delalloc_bytes)) {
		btrfs_info(fs_info, "at unmount delalloc count %lld",
		       percpu_counter_sum(&fs_info->delalloc_bytes));
	}

	btrfs_sysfs_remove_mounted(fs_info);
	btrfs_sysfs_remove_fsid(fs_info->fs_devices);

	btrfs_free_fs_roots(fs_info);

	btrfs_put_block_group_cache(fs_info);

	/*
	 * we must make sure there is not any read request to
	 * submit after we stopping all workers.
	 */
	invalidate_inode_pages2(fs_info->btree_inode->i_mapping);
	btrfs_stop_all_workers(fs_info);

	btrfs_free_block_groups(fs_info);

	clear_bit(BTRFS_FS_OPEN, &fs_info->flags);
	free_root_pointers(fs_info, 1);

	iput(fs_info->btree_inode);

#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
	if (btrfs_test_opt(fs_info, CHECK_INTEGRITY))
		btrfsic_unmount(fs_info->fs_devices);
#endif

	btrfs_close_devices(fs_info->fs_devices);
	btrfs_mapping_tree_free(&fs_info->mapping_tree);

	percpu_counter_destroy(&fs_info->dirty_metadata_bytes);
	percpu_counter_destroy(&fs_info->delalloc_bytes);
	percpu_counter_destroy(&fs_info->bio_counter);
	cleanup_srcu_struct(&fs_info->subvol_srcu);

	btrfs_free_stripe_hash_table(fs_info);
	btrfs_free_ref_cache(fs_info);

	__btrfs_free_block_rsv(root->orphan_block_rsv);
	root->orphan_block_rsv = NULL;

	while (!list_empty(&fs_info->pinned_chunks)) {
		struct extent_map *em;

		em = list_first_entry(&fs_info->pinned_chunks,
				      struct extent_map, list);
		list_del_init(&em->list);
		free_extent_map(em);
	}
}

int btrfs_buffer_uptodate(struct extent_buffer *buf, u64 parent_transid,
			  int atomic)
{
	int ret;
	struct inode *btree_inode = buf->pages[0]->mapping->host;

	ret = extent_buffer_uptodate(buf);
	if (!ret)
		return ret;

	ret = verify_parent_transid(&BTRFS_I(btree_inode)->io_tree, buf,
				    parent_transid, atomic);
	if (ret == -EAGAIN)
		return ret;
	return !ret;
}

void btrfs_mark_buffer_dirty(struct extent_buffer *buf)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root;
	u64 transid = btrfs_header_generation(buf);
	int was_dirty;

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
	/*
	 * This is a fast path so only do this check if we have sanity tests
	 * enabled.  Normal people shouldn't be marking dummy buffers as dirty
	 * outside of the sanity tests.
	 */
	if (unlikely(test_bit(EXTENT_BUFFER_DUMMY, &buf->bflags)))
		return;
#endif
	root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	fs_info = root->fs_info;
	btrfs_assert_tree_locked(buf);
	if (transid != fs_info->generation)
		WARN(1, KERN_CRIT "btrfs transid mismatch buffer %llu, found %llu running %llu\n",
			buf->start, transid, fs_info->generation);
	was_dirty = set_extent_buffer_dirty(buf);
	if (!was_dirty)
		percpu_counter_add_batch(&fs_info->dirty_metadata_bytes,
					 buf->len,
					 fs_info->dirty_metadata_batch);
#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
	/*
	 * Since btrfs_mark_buffer_dirty() can be called with item pointer set
	 * but item data not updated.
	 * So here we should only check item pointers, not item data.
	 */
	if (btrfs_header_level(buf) == 0 &&
	    btrfs_check_leaf_relaxed(fs_info, buf)) {
		btrfs_print_leaf(buf);
		ASSERT(0);
	}
#endif
}

static void __btrfs_btree_balance_dirty(struct btrfs_fs_info *fs_info,
					int flush_delayed)
{
	/*
	 * looks as though older kernels can get into trouble with
	 * this code, they end up stuck in balance_dirty_pages forever
	 */
	int ret;

	if (current->flags & PF_MEMALLOC)
		return;

	if (flush_delayed)
		btrfs_balance_delayed_items(fs_info);

	ret = percpu_counter_compare(&fs_info->dirty_metadata_bytes,
				     BTRFS_DIRTY_METADATA_THRESH);
	if (ret > 0) {
		balance_dirty_pages_ratelimited(fs_info->btree_inode->i_mapping);
	}
}

void btrfs_btree_balance_dirty(struct btrfs_fs_info *fs_info)
{
	__btrfs_btree_balance_dirty(fs_info, 1);
}

void btrfs_btree_balance_dirty_nodelay(struct btrfs_fs_info *fs_info)
{
	__btrfs_btree_balance_dirty(fs_info, 0);
}

int btrfs_read_buffer(struct extent_buffer *buf, u64 parent_transid, int level,
		      struct btrfs_key *first_key)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;

	return btree_read_extent_buffer_pages(fs_info, buf, parent_transid,
					      level, first_key);
}

static int btrfs_check_super_valid(struct btrfs_fs_info *fs_info)
{
	struct btrfs_super_block *sb = fs_info->super_copy;
	u64 nodesize = btrfs_super_nodesize(sb);
	u64 sectorsize = btrfs_super_sectorsize(sb);
	int ret = 0;

	if (btrfs_super_magic(sb) != BTRFS_MAGIC) {
		btrfs_err(fs_info, "no valid FS found");
		ret = -EINVAL;
	}
	if (btrfs_super_flags(sb) & ~BTRFS_SUPER_FLAG_SUPP) {
		btrfs_err(fs_info, "unrecognized or unsupported super flag: %llu",
				btrfs_super_flags(sb) & ~BTRFS_SUPER_FLAG_SUPP);
		ret = -EINVAL;
	}
	if (btrfs_super_root_level(sb) >= BTRFS_MAX_LEVEL) {
		btrfs_err(fs_info, "tree_root level too big: %d >= %d",
				btrfs_super_root_level(sb), BTRFS_MAX_LEVEL);
		ret = -EINVAL;
	}
	if (btrfs_super_chunk_root_level(sb) >= BTRFS_MAX_LEVEL) {
		btrfs_err(fs_info, "chunk_root level too big: %d >= %d",
				btrfs_super_chunk_root_level(sb), BTRFS_MAX_LEVEL);
		ret = -EINVAL;
	}
	if (btrfs_super_log_root_level(sb) >= BTRFS_MAX_LEVEL) {
		btrfs_err(fs_info, "log_root level too big: %d >= %d",
				btrfs_super_log_root_level(sb), BTRFS_MAX_LEVEL);
		ret = -EINVAL;
	}

	/*
	 * Check sectorsize and nodesize first, other check will need it.
	 * Check all possible sectorsize(4K, 8K, 16K, 32K, 64K) here.
	 */
	if (!is_power_of_2(sectorsize) || sectorsize < 4096 ||
	    sectorsize > BTRFS_MAX_METADATA_BLOCKSIZE) {
		btrfs_err(fs_info, "invalid sectorsize %llu", sectorsize);
		ret = -EINVAL;
	}
	/* Only PAGE SIZE is supported yet */
	if (sectorsize != PAGE_SIZE) {
		btrfs_err(fs_info,
			"sectorsize %llu not supported yet, only support %lu",
			sectorsize, PAGE_SIZE);
		ret = -EINVAL;
	}
	if (!is_power_of_2(nodesize) || nodesize < sectorsize ||
	    nodesize > BTRFS_MAX_METADATA_BLOCKSIZE) {
		btrfs_err(fs_info, "invalid nodesize %llu", nodesize);
		ret = -EINVAL;
	}
	if (nodesize != le32_to_cpu(sb->__unused_leafsize)) {
		btrfs_err(fs_info, "invalid leafsize %u, should be %llu",
			  le32_to_cpu(sb->__unused_leafsize), nodesize);
		ret = -EINVAL;
	}

	/* Root alignment check */
	if (!IS_ALIGNED(btrfs_super_root(sb), sectorsize)) {
		btrfs_warn(fs_info, "tree_root block unaligned: %llu",
			   btrfs_super_root(sb));
		ret = -EINVAL;
	}
	if (!IS_ALIGNED(btrfs_super_chunk_root(sb), sectorsize)) {
		btrfs_warn(fs_info, "chunk_root block unaligned: %llu",
			   btrfs_super_chunk_root(sb));
		ret = -EINVAL;
	}
	if (!IS_ALIGNED(btrfs_super_log_root(sb), sectorsize)) {
		btrfs_warn(fs_info, "log_root block unaligned: %llu",
			   btrfs_super_log_root(sb));
		ret = -EINVAL;
	}

	if (memcmp(fs_info->fsid, sb->dev_item.fsid, BTRFS_FSID_SIZE) != 0) {
		btrfs_err(fs_info,
			   "dev_item UUID does not match fsid: %pU != %pU",
			   fs_info->fsid, sb->dev_item.fsid);
		ret = -EINVAL;
	}

	/*
	 * Hint to catch really bogus numbers, bitflips or so, more exact checks are
	 * done later
	 */
	if (btrfs_super_bytes_used(sb) < 6 * btrfs_super_nodesize(sb)) {
		btrfs_err(fs_info, "bytes_used is too small %llu",
			  btrfs_super_bytes_used(sb));
		ret = -EINVAL;
	}
	if (!is_power_of_2(btrfs_super_stripesize(sb))) {
		btrfs_err(fs_info, "invalid stripesize %u",
			  btrfs_super_stripesize(sb));
		ret = -EINVAL;
	}
	if (btrfs_super_num_devices(sb) > (1UL << 31))
		btrfs_warn(fs_info, "suspicious number of devices: %llu",
			   btrfs_super_num_devices(sb));
	if (btrfs_super_num_devices(sb) == 0) {
		btrfs_err(fs_info, "number of devices is 0");
		ret = -EINVAL;
	}

	if (btrfs_super_bytenr(sb) != BTRFS_SUPER_INFO_OFFSET) {
		btrfs_err(fs_info, "super offset mismatch %llu != %u",
			  btrfs_super_bytenr(sb), BTRFS_SUPER_INFO_OFFSET);
		ret = -EINVAL;
	}

	/*
	 * Obvious sys_chunk_array corruptions, it must hold at least one key
	 * and one chunk
	 */
	if (btrfs_super_sys_array_size(sb) > BTRFS_SYSTEM_CHUNK_ARRAY_SIZE) {
		btrfs_err(fs_info, "system chunk array too big %u > %u",
			  btrfs_super_sys_array_size(sb),
			  BTRFS_SYSTEM_CHUNK_ARRAY_SIZE);
		ret = -EINVAL;
	}
	if (btrfs_super_sys_array_size(sb) < sizeof(struct btrfs_disk_key)
			+ sizeof(struct btrfs_chunk)) {
		btrfs_err(fs_info, "system chunk array too small %u < %zu",
			  btrfs_super_sys_array_size(sb),
			  sizeof(struct btrfs_disk_key)
			  + sizeof(struct btrfs_chunk));
		ret = -EINVAL;
	}

	/*
	 * The generation is a global counter, we'll trust it more than the others
	 * but it's still possible that it's the one that's wrong.
	 */
	if (btrfs_super_generation(sb) < btrfs_super_chunk_root_generation(sb))
		btrfs_warn(fs_info,
			"suspicious: generation < chunk_root_generation: %llu < %llu",
			btrfs_super_generation(sb),
			btrfs_super_chunk_root_generation(sb));
	if (btrfs_super_generation(sb) < btrfs_super_cache_generation(sb)
	    && btrfs_super_cache_generation(sb) != (u64)-1)
		btrfs_warn(fs_info,
			"suspicious: generation < cache_generation: %llu < %llu",
			btrfs_super_generation(sb),
			btrfs_super_cache_generation(sb));

	return ret;
}

static void btrfs_error_commit_super(struct btrfs_fs_info *fs_info)
{
	mutex_lock(&fs_info->cleaner_mutex);
	btrfs_run_delayed_iputs(fs_info);
	mutex_unlock(&fs_info->cleaner_mutex);

	down_write(&fs_info->cleanup_work_sem);
	up_write(&fs_info->cleanup_work_sem);

	/* cleanup FS via transaction */
	btrfs_cleanup_transaction(fs_info);
}

static void btrfs_destroy_ordered_extents(struct btrfs_root *root)
{
	struct btrfs_ordered_extent *ordered;

	spin_lock(&root->ordered_extent_lock);
	/*
	 * This will just short circuit the ordered completion stuff which will
	 * make sure the ordered extent gets properly cleaned up.
	 */
	list_for_each_entry(ordered, &root->ordered_extents,
			    root_extent_list)
		set_bit(BTRFS_ORDERED_IOERR, &ordered->flags);
	spin_unlock(&root->ordered_extent_lock);
}

static void btrfs_destroy_all_ordered_extents(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;
	struct list_head splice;

	INIT_LIST_HEAD(&splice);

	spin_lock(&fs_info->ordered_root_lock);
	list_splice_init(&fs_info->ordered_roots, &splice);
	while (!list_empty(&splice)) {
		root = list_first_entry(&splice, struct btrfs_root,
					ordered_root);
		list_move_tail(&root->ordered_root,
			       &fs_info->ordered_roots);

		spin_unlock(&fs_info->ordered_root_lock);
		btrfs_destroy_ordered_extents(root);

		cond_resched();
		spin_lock(&fs_info->ordered_root_lock);
	}
	spin_unlock(&fs_info->ordered_root_lock);
}

static int btrfs_destroy_delayed_refs(struct btrfs_transaction *trans,
				      struct btrfs_fs_info *fs_info)
{
	struct rb_node *node;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	int ret = 0;

	delayed_refs = &trans->delayed_refs;

	spin_lock(&delayed_refs->lock);
	if (atomic_read(&delayed_refs->num_entries) == 0) {
		spin_unlock(&delayed_refs->lock);
		btrfs_info(fs_info, "delayed_refs has NO entry");
		return ret;
	}

	while ((node = rb_first(&delayed_refs->href_root)) != NULL) {
		struct btrfs_delayed_ref_head *head;
		struct rb_node *n;
		bool pin_bytes = false;

		head = rb_entry(node, struct btrfs_delayed_ref_head,
				href_node);
		if (!mutex_trylock(&head->mutex)) {
			refcount_inc(&head->refs);
			spin_unlock(&delayed_refs->lock);

			mutex_lock(&head->mutex);
			mutex_unlock(&head->mutex);
			btrfs_put_delayed_ref_head(head);
			spin_lock(&delayed_refs->lock);
			continue;
		}
		spin_lock(&head->lock);
		while ((n = rb_first(&head->ref_tree)) != NULL) {
			ref = rb_entry(n, struct btrfs_delayed_ref_node,
				       ref_node);
			ref->in_tree = 0;
			rb_erase(&ref->ref_node, &head->ref_tree);
			RB_CLEAR_NODE(&ref->ref_node);
			if (!list_empty(&ref->add_list))
				list_del(&ref->add_list);
			atomic_dec(&delayed_refs->num_entries);
			btrfs_put_delayed_ref(ref);
		}
		if (head->must_insert_reserved)
			pin_bytes = true;
		btrfs_free_delayed_extent_op(head->extent_op);
		delayed_refs->num_heads--;
		if (head->processing == 0)
			delayed_refs->num_heads_ready--;
		atomic_dec(&delayed_refs->num_entries);
		rb_erase(&head->href_node, &delayed_refs->href_root);
		RB_CLEAR_NODE(&head->href_node);
		spin_unlock(&head->lock);
		spin_unlock(&delayed_refs->lock);
		mutex_unlock(&head->mutex);

		if (pin_bytes)
			btrfs_pin_extent(fs_info, head->bytenr,
					 head->num_bytes, 1);
		btrfs_put_delayed_ref_head(head);
		cond_resched();
		spin_lock(&delayed_refs->lock);
	}

	spin_unlock(&delayed_refs->lock);

	return ret;
}

static void btrfs_destroy_delalloc_inodes(struct btrfs_root *root)
{
	struct btrfs_inode *btrfs_inode;
	struct list_head splice;

	INIT_LIST_HEAD(&splice);

	spin_lock(&root->delalloc_lock);
	list_splice_init(&root->delalloc_inodes, &splice);

	while (!list_empty(&splice)) {
		btrfs_inode = list_first_entry(&splice, struct btrfs_inode,
					       delalloc_inodes);

		list_del_init(&btrfs_inode->delalloc_inodes);
		clear_bit(BTRFS_INODE_IN_DELALLOC_LIST,
			  &btrfs_inode->runtime_flags);
		spin_unlock(&root->delalloc_lock);

		btrfs_invalidate_inodes(btrfs_inode->root);

		spin_lock(&root->delalloc_lock);
	}

	spin_unlock(&root->delalloc_lock);
}

static void btrfs_destroy_all_delalloc_inodes(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;
	struct list_head splice;

	INIT_LIST_HEAD(&splice);

	spin_lock(&fs_info->delalloc_root_lock);
	list_splice_init(&fs_info->delalloc_roots, &splice);
	while (!list_empty(&splice)) {
		root = list_first_entry(&splice, struct btrfs_root,
					 delalloc_root);
		list_del_init(&root->delalloc_root);
		root = btrfs_grab_fs_root(root);
		BUG_ON(!root);
		spin_unlock(&fs_info->delalloc_root_lock);

		btrfs_destroy_delalloc_inodes(root);
		btrfs_put_fs_root(root);

		spin_lock(&fs_info->delalloc_root_lock);
	}
	spin_unlock(&fs_info->delalloc_root_lock);
}

static int btrfs_destroy_marked_extents(struct btrfs_fs_info *fs_info,
					struct extent_io_tree *dirty_pages,
					int mark)
{
	int ret;
	struct extent_buffer *eb;
	u64 start = 0;
	u64 end;

	while (1) {
		ret = find_first_extent_bit(dirty_pages, start, &start, &end,
					    mark, NULL);
		if (ret)
			break;

		clear_extent_bits(dirty_pages, start, end, mark);
		while (start <= end) {
			eb = find_extent_buffer(fs_info, start);
			start += fs_info->nodesize;
			if (!eb)
				continue;
			wait_on_extent_buffer_writeback(eb);

			if (test_and_clear_bit(EXTENT_BUFFER_DIRTY,
					       &eb->bflags))
				clear_extent_buffer_dirty(eb);
			free_extent_buffer_stale(eb);
		}
	}

	return ret;
}

static int btrfs_destroy_pinned_extent(struct btrfs_fs_info *fs_info,
				       struct extent_io_tree *pinned_extents)
{
	struct extent_io_tree *unpin;
	u64 start;
	u64 end;
	int ret;
	bool loop = true;

	unpin = pinned_extents;
again:
	while (1) {
		ret = find_first_extent_bit(unpin, 0, &start, &end,
					    EXTENT_DIRTY, NULL);
		if (ret)
			break;

		clear_extent_dirty(unpin, start, end);
		btrfs_error_unpin_extent_range(fs_info, start, end);
		cond_resched();
	}

	if (loop) {
		if (unpin == &fs_info->freed_extents[0])
			unpin = &fs_info->freed_extents[1];
		else
			unpin = &fs_info->freed_extents[0];
		loop = false;
		goto again;
	}

	return 0;
}

static void btrfs_cleanup_bg_io(struct btrfs_block_group_cache *cache)
{
	struct inode *inode;

	inode = cache->io_ctl.inode;
	if (inode) {
		invalidate_inode_pages2(inode->i_mapping);
		BTRFS_I(inode)->generation = 0;
		cache->io_ctl.inode = NULL;
		iput(inode);
	}
	btrfs_put_block_group(cache);
}

void btrfs_cleanup_dirty_bgs(struct btrfs_transaction *cur_trans,
			     struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_group_cache *cache;

	spin_lock(&cur_trans->dirty_bgs_lock);
	while (!list_empty(&cur_trans->dirty_bgs)) {
		cache = list_first_entry(&cur_trans->dirty_bgs,
					 struct btrfs_block_group_cache,
					 dirty_list);

		if (!list_empty(&cache->io_list)) {
			spin_unlock(&cur_trans->dirty_bgs_lock);
			list_del_init(&cache->io_list);
			btrfs_cleanup_bg_io(cache);
			spin_lock(&cur_trans->dirty_bgs_lock);
		}

		list_del_init(&cache->dirty_list);
		spin_lock(&cache->lock);
		cache->disk_cache_state = BTRFS_DC_ERROR;
		spin_unlock(&cache->lock);

		spin_unlock(&cur_trans->dirty_bgs_lock);
		btrfs_put_block_group(cache);
		spin_lock(&cur_trans->dirty_bgs_lock);
	}
	spin_unlock(&cur_trans->dirty_bgs_lock);

	/*
	 * Refer to the definition of io_bgs member for details why it's safe
	 * to use it without any locking
	 */
	while (!list_empty(&cur_trans->io_bgs)) {
		cache = list_first_entry(&cur_trans->io_bgs,
					 struct btrfs_block_group_cache,
					 io_list);

		list_del_init(&cache->io_list);
		spin_lock(&cache->lock);
		cache->disk_cache_state = BTRFS_DC_ERROR;
		spin_unlock(&cache->lock);
		btrfs_cleanup_bg_io(cache);
	}
}

void btrfs_cleanup_one_transaction(struct btrfs_transaction *cur_trans,
				   struct btrfs_fs_info *fs_info)
{
	btrfs_cleanup_dirty_bgs(cur_trans, fs_info);
	ASSERT(list_empty(&cur_trans->dirty_bgs));
	ASSERT(list_empty(&cur_trans->io_bgs));

	btrfs_destroy_delayed_refs(cur_trans, fs_info);

	cur_trans->state = TRANS_STATE_COMMIT_START;
	wake_up(&fs_info->transaction_blocked_wait);

	cur_trans->state = TRANS_STATE_UNBLOCKED;
	wake_up(&fs_info->transaction_wait);

	btrfs_destroy_delayed_inodes(fs_info);
	btrfs_assert_delayed_root_empty(fs_info);

	btrfs_destroy_marked_extents(fs_info, &cur_trans->dirty_pages,
				     EXTENT_DIRTY);
	btrfs_destroy_pinned_extent(fs_info,
				    fs_info->pinned_extents);

	cur_trans->state =TRANS_STATE_COMPLETED;
	wake_up(&cur_trans->commit_wait);
}

static int btrfs_cleanup_transaction(struct btrfs_fs_info *fs_info)
{
	struct btrfs_transaction *t;

	mutex_lock(&fs_info->transaction_kthread_mutex);

	spin_lock(&fs_info->trans_lock);
	while (!list_empty(&fs_info->trans_list)) {
		t = list_first_entry(&fs_info->trans_list,
				     struct btrfs_transaction, list);
		if (t->state >= TRANS_STATE_COMMIT_START) {
			refcount_inc(&t->use_count);
			spin_unlock(&fs_info->trans_lock);
			btrfs_wait_for_commit(fs_info, t->transid);
			btrfs_put_transaction(t);
			spin_lock(&fs_info->trans_lock);
			continue;
		}
		if (t == fs_info->running_transaction) {
			t->state = TRANS_STATE_COMMIT_DOING;
			spin_unlock(&fs_info->trans_lock);
			/*
			 * We wait for 0 num_writers since we don't hold a trans
			 * handle open currently for this transaction.
			 */
			wait_event(t->writer_wait,
				   atomic_read(&t->num_writers) == 0);
		} else {
			spin_unlock(&fs_info->trans_lock);
		}
		btrfs_cleanup_one_transaction(t, fs_info);

		spin_lock(&fs_info->trans_lock);
		if (t == fs_info->running_transaction)
			fs_info->running_transaction = NULL;
		list_del_init(&t->list);
		spin_unlock(&fs_info->trans_lock);

		btrfs_put_transaction(t);
		trace_btrfs_transaction_commit(fs_info->tree_root);
		spin_lock(&fs_info->trans_lock);
	}
	spin_unlock(&fs_info->trans_lock);
	btrfs_destroy_all_ordered_extents(fs_info);
	btrfs_destroy_delayed_inodes(fs_info);
	btrfs_assert_delayed_root_empty(fs_info);
	btrfs_destroy_pinned_extent(fs_info, fs_info->pinned_extents);
	btrfs_destroy_all_delalloc_inodes(fs_info);
	mutex_unlock(&fs_info->transaction_kthread_mutex);

	return 0;
}

static struct btrfs_fs_info *btree_fs_info(void *private_data)
{
	struct inode *inode = private_data;
	return btrfs_sb(inode->i_sb);
}

static const struct extent_io_ops btree_extent_io_ops = {
	/* mandatory callbacks */
	.submit_bio_hook = btree_submit_bio_hook,
	.readpage_end_io_hook = btree_readpage_end_io_hook,
	/* note we're sharing with inode.c for the merge bio hook */
	.merge_bio_hook = btrfs_merge_bio_hook,
	.readpage_io_failed_hook = btree_io_failed_hook,
	.set_range_writeback = btrfs_set_range_writeback,
	.tree_fs_info = btree_fs_info,

	/* optional callbacks */
};

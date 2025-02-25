// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/radix-tree.h>
#include <linux/writeback.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/migrate.h>
#include <linux/ratelimit.h>
#include <linux/uuid.h>
#include <linux/semaphore.h>
#include <linux/error-injection.h>
#include <linux/crc32c.h>
#include <linux/sched/mm.h>
#include <linux/unaligned.h>
#include <crypto/hash.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "bio.h"
#include "print-tree.h"
#include "locking.h"
#include "tree-log.h"
#include "free-space-cache.h"
#include "free-space-tree.h"
#include "dev-replace.h"
#include "raid56.h"
#include "sysfs.h"
#include "qgroup.h"
#include "compression.h"
#include "tree-checker.h"
#include "ref-verify.h"
#include "block-group.h"
#include "discard.h"
#include "space-info.h"
#include "zoned.h"
#include "subpage.h"
#include "fs.h"
#include "accessors.h"
#include "extent-tree.h"
#include "root-tree.h"
#include "defrag.h"
#include "uuid-tree.h"
#include "relocation.h"
#include "scrub.h"
#include "super.h"

#define BTRFS_SUPER_FLAG_SUPP	(BTRFS_HEADER_FLAG_WRITTEN |\
				 BTRFS_HEADER_FLAG_RELOC |\
				 BTRFS_SUPER_FLAG_ERROR |\
				 BTRFS_SUPER_FLAG_SEEDING |\
				 BTRFS_SUPER_FLAG_METADUMP |\
				 BTRFS_SUPER_FLAG_METADUMP_V2)

static int btrfs_cleanup_transaction(struct btrfs_fs_info *fs_info);
static void btrfs_error_commit_super(struct btrfs_fs_info *fs_info);

static void btrfs_free_csum_hash(struct btrfs_fs_info *fs_info)
{
	if (fs_info->csum_shash)
		crypto_free_shash(fs_info->csum_shash);
}

/*
 * Compute the csum of a btree block and store the result to provided buffer.
 */
static void csum_tree_block(struct extent_buffer *buf, u8 *result)
{
	struct btrfs_fs_info *fs_info = buf->fs_info;
	int num_pages;
	u32 first_page_part;
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	char *kaddr;
	int i;

	shash->tfm = fs_info->csum_shash;
	crypto_shash_init(shash);

	if (buf->addr) {
		/* Pages are contiguous, handle them as a big one. */
		kaddr = buf->addr;
		first_page_part = fs_info->nodesize;
		num_pages = 1;
	} else {
		kaddr = folio_address(buf->folios[0]);
		first_page_part = min_t(u32, PAGE_SIZE, fs_info->nodesize);
		num_pages = num_extent_pages(buf);
	}

	crypto_shash_update(shash, kaddr + BTRFS_CSUM_SIZE,
			    first_page_part - BTRFS_CSUM_SIZE);

	/*
	 * Multiple single-page folios case would reach here.
	 *
	 * nodesize <= PAGE_SIZE and large folio all handled by above
	 * crypto_shash_update() already.
	 */
	for (i = 1; i < num_pages && INLINE_EXTENT_BUFFER_PAGES > 1; i++) {
		kaddr = folio_address(buf->folios[i]);
		crypto_shash_update(shash, kaddr, PAGE_SIZE);
	}
	memset(result, 0, BTRFS_CSUM_SIZE);
	crypto_shash_final(shash, result);
}

/*
 * we can't consider a given block up to date unless the transid of the
 * block matches the transid in the parent node's pointer.  This is how we
 * detect blocks that either didn't get written at all or got written
 * in the wrong place.
 */
int btrfs_buffer_uptodate(struct extent_buffer *eb, u64 parent_transid, int atomic)
{
	if (!extent_buffer_uptodate(eb))
		return 0;

	if (!parent_transid || btrfs_header_generation(eb) == parent_transid)
		return 1;

	if (atomic)
		return -EAGAIN;

	if (!extent_buffer_uptodate(eb) ||
	    btrfs_header_generation(eb) != parent_transid) {
		btrfs_err_rl(eb->fs_info,
"parent transid verify failed on logical %llu mirror %u wanted %llu found %llu",
			eb->start, eb->read_mirror,
			parent_transid, btrfs_header_generation(eb));
		clear_extent_buffer_uptodate(eb);
		return 0;
	}
	return 1;
}

static bool btrfs_supported_super_csum(u16 csum_type)
{
	switch (csum_type) {
	case BTRFS_CSUM_TYPE_CRC32:
	case BTRFS_CSUM_TYPE_XXHASH:
	case BTRFS_CSUM_TYPE_SHA256:
	case BTRFS_CSUM_TYPE_BLAKE2:
		return true;
	default:
		return false;
	}
}

/*
 * Return 0 if the superblock checksum type matches the checksum value of that
 * algorithm. Pass the raw disk superblock data.
 */
int btrfs_check_super_csum(struct btrfs_fs_info *fs_info,
			   const struct btrfs_super_block *disk_sb)
{
	char result[BTRFS_CSUM_SIZE];
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);

	shash->tfm = fs_info->csum_shash;

	/*
	 * The super_block structure does not span the whole
	 * BTRFS_SUPER_INFO_SIZE range, we expect that the unused space is
	 * filled with zeros and is included in the checksum.
	 */
	crypto_shash_digest(shash, (const u8 *)disk_sb + BTRFS_CSUM_SIZE,
			    BTRFS_SUPER_INFO_SIZE - BTRFS_CSUM_SIZE, result);

	if (memcmp(disk_sb->csum, result, fs_info->csum_size))
		return 1;

	return 0;
}

static int btrfs_repair_eb_io_failure(const struct extent_buffer *eb,
				      int mirror_num)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	int num_folios = num_extent_folios(eb);
	int ret = 0;

	if (sb_rdonly(fs_info->sb))
		return -EROFS;

	for (int i = 0; i < num_folios; i++) {
		struct folio *folio = eb->folios[i];
		u64 start = max_t(u64, eb->start, folio_pos(folio));
		u64 end = min_t(u64, eb->start + eb->len,
				folio_pos(folio) + eb->folio_size);
		u32 len = end - start;

		ret = btrfs_repair_io_failure(fs_info, 0, start, len,
					      start, folio, offset_in_folio(folio, start),
					      mirror_num);
		if (ret)
			break;
	}

	return ret;
}

/*
 * helper to read a given tree block, doing retries as required when
 * the checksums don't match and we have alternate mirrors to try.
 *
 * @check:		expected tree parentness check, see the comments of the
 *			structure for details.
 */
int btrfs_read_extent_buffer(struct extent_buffer *eb,
			     const struct btrfs_tree_parent_check *check)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	int failed = 0;
	int ret;
	int num_copies = 0;
	int mirror_num = 0;
	int failed_mirror = 0;

	ASSERT(check);

	while (1) {
		clear_bit(EXTENT_BUFFER_CORRUPT, &eb->bflags);
		ret = read_extent_buffer_pages(eb, mirror_num, check);
		if (!ret)
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
		btrfs_repair_eb_io_failure(eb, failed_mirror);

	return ret;
}

/*
 * Checksum a dirty tree block before IO.
 */
blk_status_t btree_csum_one_bio(struct btrfs_bio *bbio)
{
	struct extent_buffer *eb = bbio->private;
	struct btrfs_fs_info *fs_info = eb->fs_info;
	u64 found_start = btrfs_header_bytenr(eb);
	u64 last_trans;
	u8 result[BTRFS_CSUM_SIZE];
	int ret;

	/* Btree blocks are always contiguous on disk. */
	if (WARN_ON_ONCE(bbio->file_offset != eb->start))
		return BLK_STS_IOERR;
	if (WARN_ON_ONCE(bbio->bio.bi_iter.bi_size != eb->len))
		return BLK_STS_IOERR;

	/*
	 * If an extent_buffer is marked as EXTENT_BUFFER_ZONED_ZEROOUT, don't
	 * checksum it but zero-out its content. This is done to preserve
	 * ordering of I/O without unnecessarily writing out data.
	 */
	if (test_bit(EXTENT_BUFFER_ZONED_ZEROOUT, &eb->bflags)) {
		memzero_extent_buffer(eb, 0, eb->len);
		return BLK_STS_OK;
	}

	if (WARN_ON_ONCE(found_start != eb->start))
		return BLK_STS_IOERR;
	if (WARN_ON(!btrfs_folio_test_uptodate(fs_info, eb->folios[0],
					       eb->start, eb->len)))
		return BLK_STS_IOERR;

	ASSERT(memcmp_extent_buffer(eb, fs_info->fs_devices->metadata_uuid,
				    offsetof(struct btrfs_header, fsid),
				    BTRFS_FSID_SIZE) == 0);
	csum_tree_block(eb, result);

	if (btrfs_header_level(eb))
		ret = btrfs_check_node(eb);
	else
		ret = btrfs_check_leaf(eb);

	if (ret < 0)
		goto error;

	/*
	 * Also check the generation, the eb reached here must be newer than
	 * last committed. Or something seriously wrong happened.
	 */
	last_trans = btrfs_get_last_trans_committed(fs_info);
	if (unlikely(btrfs_header_generation(eb) <= last_trans)) {
		ret = -EUCLEAN;
		btrfs_err(fs_info,
			"block=%llu bad generation, have %llu expect > %llu",
			  eb->start, btrfs_header_generation(eb), last_trans);
		goto error;
	}
	write_extent_buffer(eb, result, 0, fs_info->csum_size);
	return BLK_STS_OK;

error:
	btrfs_print_tree(eb, 0);
	btrfs_err(fs_info, "block=%llu write time tree block corruption detected",
		  eb->start);
	/*
	 * Be noisy if this is an extent buffer from a log tree. We don't abort
	 * a transaction in case there's a bad log tree extent buffer, we just
	 * fallback to a transaction commit. Still we want to know when there is
	 * a bad log tree extent buffer, as that may signal a bug somewhere.
	 */
	WARN_ON(IS_ENABLED(CONFIG_BTRFS_DEBUG) ||
		btrfs_header_owner(eb) == BTRFS_TREE_LOG_OBJECTID);
	return errno_to_blk_status(ret);
}

static bool check_tree_block_fsid(struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices, *seed_devs;
	u8 fsid[BTRFS_FSID_SIZE];

	read_extent_buffer(eb, fsid, offsetof(struct btrfs_header, fsid),
			   BTRFS_FSID_SIZE);

	/*
	 * alloc_fsid_devices() copies the fsid into fs_devices::metadata_uuid.
	 * This is then overwritten by metadata_uuid if it is present in the
	 * device_list_add(). The same true for a seed device as well. So use of
	 * fs_devices::metadata_uuid is appropriate here.
	 */
	if (memcmp(fsid, fs_info->fs_devices->metadata_uuid, BTRFS_FSID_SIZE) == 0)
		return false;

	list_for_each_entry(seed_devs, &fs_devices->seed_list, seed_list)
		if (!memcmp(fsid, seed_devs->fsid, BTRFS_FSID_SIZE))
			return false;

	return true;
}

/* Do basic extent buffer checks at read time */
int btrfs_validate_extent_buffer(struct extent_buffer *eb,
				 const struct btrfs_tree_parent_check *check)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	u64 found_start;
	const u32 csum_size = fs_info->csum_size;
	u8 found_level;
	u8 result[BTRFS_CSUM_SIZE];
	const u8 *header_csum;
	int ret = 0;
	const bool ignore_csum = btrfs_test_opt(fs_info, IGNOREMETACSUMS);

	ASSERT(check);

	found_start = btrfs_header_bytenr(eb);
	if (found_start != eb->start) {
		btrfs_err_rl(fs_info,
			"bad tree block start, mirror %u want %llu have %llu",
			     eb->read_mirror, eb->start, found_start);
		ret = -EIO;
		goto out;
	}
	if (check_tree_block_fsid(eb)) {
		btrfs_err_rl(fs_info, "bad fsid on logical %llu mirror %u",
			     eb->start, eb->read_mirror);
		ret = -EIO;
		goto out;
	}
	found_level = btrfs_header_level(eb);
	if (found_level >= BTRFS_MAX_LEVEL) {
		btrfs_err(fs_info,
			"bad tree block level, mirror %u level %d on logical %llu",
			eb->read_mirror, btrfs_header_level(eb), eb->start);
		ret = -EIO;
		goto out;
	}

	csum_tree_block(eb, result);
	header_csum = folio_address(eb->folios[0]) +
		get_eb_offset_in_folio(eb, offsetof(struct btrfs_header, csum));

	if (memcmp(result, header_csum, csum_size) != 0) {
		btrfs_warn_rl(fs_info,
"checksum verify failed on logical %llu mirror %u wanted " CSUM_FMT " found " CSUM_FMT " level %d%s",
			      eb->start, eb->read_mirror,
			      CSUM_FMT_VALUE(csum_size, header_csum),
			      CSUM_FMT_VALUE(csum_size, result),
			      btrfs_header_level(eb),
			      ignore_csum ? ", ignored" : "");
		if (!ignore_csum) {
			ret = -EUCLEAN;
			goto out;
		}
	}

	if (found_level != check->level) {
		btrfs_err(fs_info,
		"level verify failed on logical %llu mirror %u wanted %u found %u",
			  eb->start, eb->read_mirror, check->level, found_level);
		ret = -EIO;
		goto out;
	}
	if (unlikely(check->transid &&
		     btrfs_header_generation(eb) != check->transid)) {
		btrfs_err_rl(eb->fs_info,
"parent transid verify failed on logical %llu mirror %u wanted %llu found %llu",
				eb->start, eb->read_mirror, check->transid,
				btrfs_header_generation(eb));
		ret = -EIO;
		goto out;
	}
	if (check->has_first_key) {
		const struct btrfs_key *expect_key = &check->first_key;
		struct btrfs_key found_key;

		if (found_level)
			btrfs_node_key_to_cpu(eb, &found_key, 0);
		else
			btrfs_item_key_to_cpu(eb, &found_key, 0);
		if (unlikely(btrfs_comp_cpu_keys(expect_key, &found_key))) {
			btrfs_err(fs_info,
"tree first key mismatch detected, bytenr=%llu parent_transid=%llu key expected=(%llu,%u,%llu) has=(%llu,%u,%llu)",
				  eb->start, check->transid,
				  expect_key->objectid,
				  expect_key->type, expect_key->offset,
				  found_key.objectid, found_key.type,
				  found_key.offset);
			ret = -EUCLEAN;
			goto out;
		}
	}
	if (check->owner_root) {
		ret = btrfs_check_eb_owner(eb, check->owner_root);
		if (ret < 0)
			goto out;
	}

	/*
	 * If this is a leaf block and it is corrupt, set the corrupt bit so
	 * that we don't try and read the other copies of this block, just
	 * return -EIO.
	 */
	if (found_level == 0 && btrfs_check_leaf(eb)) {
		set_bit(EXTENT_BUFFER_CORRUPT, &eb->bflags);
		ret = -EIO;
	}

	if (found_level > 0 && btrfs_check_node(eb))
		ret = -EIO;

	if (ret)
		btrfs_err(fs_info,
		"read time tree block corruption detected on logical %llu mirror %u",
			  eb->start, eb->read_mirror);
out:
	return ret;
}

#ifdef CONFIG_MIGRATION
static int btree_migrate_folio(struct address_space *mapping,
		struct folio *dst, struct folio *src, enum migrate_mode mode)
{
	/*
	 * we can't safely write a btree page from here,
	 * we haven't done the locking hook
	 */
	if (folio_test_dirty(src))
		return -EAGAIN;
	/*
	 * Buffers may be managed in a filesystem specific way.
	 * We must have no buffers or drop them.
	 */
	if (folio_get_private(src) &&
	    !filemap_release_folio(src, GFP_KERNEL))
		return -EAGAIN;
	return migrate_folio(mapping, dst, src, mode);
}
#else
#define btree_migrate_folio NULL
#endif

static int btree_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	int ret;

	if (wbc->sync_mode == WB_SYNC_NONE) {
		struct btrfs_fs_info *fs_info;

		if (wbc->for_kupdate)
			return 0;

		fs_info = inode_to_fs_info(mapping->host);
		/* this is a bit racy, but that's ok */
		ret = __percpu_counter_compare(&fs_info->dirty_metadata_bytes,
					     BTRFS_DIRTY_METADATA_THRESH,
					     fs_info->dirty_metadata_batch);
		if (ret < 0)
			return 0;
	}
	return btree_write_cache_pages(mapping, wbc);
}

static bool btree_release_folio(struct folio *folio, gfp_t gfp_flags)
{
	if (folio_test_writeback(folio) || folio_test_dirty(folio))
		return false;

	return try_release_extent_buffer(folio);
}

static void btree_invalidate_folio(struct folio *folio, size_t offset,
				 size_t length)
{
	struct extent_io_tree *tree;

	tree = &folio_to_inode(folio)->io_tree;
	extent_invalidate_folio(tree, folio, offset);
	btree_release_folio(folio, GFP_NOFS);
	if (folio_get_private(folio)) {
		btrfs_warn(folio_to_fs_info(folio),
			   "folio private not zero on folio %llu",
			   (unsigned long long)folio_pos(folio));
		folio_detach_private(folio);
	}
}

#ifdef DEBUG
static bool btree_dirty_folio(struct address_space *mapping,
		struct folio *folio)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(mapping->host);
	struct btrfs_subpage_info *spi = fs_info->subpage_info;
	struct btrfs_subpage *subpage;
	struct extent_buffer *eb;
	int cur_bit = 0;
	u64 page_start = folio_pos(folio);

	if (fs_info->sectorsize == PAGE_SIZE) {
		eb = folio_get_private(folio);
		BUG_ON(!eb);
		BUG_ON(!test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags));
		BUG_ON(!atomic_read(&eb->refs));
		btrfs_assert_tree_write_locked(eb);
		return filemap_dirty_folio(mapping, folio);
	}

	ASSERT(spi);
	subpage = folio_get_private(folio);

	for (cur_bit = spi->dirty_offset;
	     cur_bit < spi->dirty_offset + spi->bitmap_nr_bits;
	     cur_bit++) {
		unsigned long flags;
		u64 cur;

		spin_lock_irqsave(&subpage->lock, flags);
		if (!test_bit(cur_bit, subpage->bitmaps)) {
			spin_unlock_irqrestore(&subpage->lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&subpage->lock, flags);
		cur = page_start + cur_bit * fs_info->sectorsize;

		eb = find_extent_buffer(fs_info, cur);
		ASSERT(eb);
		ASSERT(test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags));
		ASSERT(atomic_read(&eb->refs));
		btrfs_assert_tree_write_locked(eb);
		free_extent_buffer(eb);

		cur_bit += (fs_info->nodesize >> fs_info->sectorsize_bits) - 1;
	}
	return filemap_dirty_folio(mapping, folio);
}
#else
#define btree_dirty_folio filemap_dirty_folio
#endif

static const struct address_space_operations btree_aops = {
	.writepages	= btree_writepages,
	.release_folio	= btree_release_folio,
	.invalidate_folio = btree_invalidate_folio,
	.migrate_folio	= btree_migrate_folio,
	.dirty_folio	= btree_dirty_folio,
};

struct extent_buffer *btrfs_find_create_tree_block(
						struct btrfs_fs_info *fs_info,
						u64 bytenr, u64 owner_root,
						int level)
{
	if (btrfs_is_testing(fs_info))
		return alloc_test_extent_buffer(fs_info, bytenr);
	return alloc_extent_buffer(fs_info, bytenr, owner_root, level);
}

/*
 * Read tree block at logical address @bytenr and do variant basic but critical
 * verification.
 *
 * @check:		expected tree parentness check, see comments of the
 *			structure for details.
 */
struct extent_buffer *read_tree_block(struct btrfs_fs_info *fs_info, u64 bytenr,
				      struct btrfs_tree_parent_check *check)
{
	struct extent_buffer *buf = NULL;
	int ret;

	ASSERT(check);

	buf = btrfs_find_create_tree_block(fs_info, bytenr, check->owner_root,
					   check->level);
	if (IS_ERR(buf))
		return buf;

	ret = btrfs_read_extent_buffer(buf, check);
	if (ret) {
		free_extent_buffer_stale(buf);
		return ERR_PTR(ret);
	}
	return buf;

}

static void __setup_root(struct btrfs_root *root, struct btrfs_fs_info *fs_info,
			 u64 objectid)
{
	bool dummy = btrfs_is_testing(fs_info);

	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	memset(&root->defrag_progress, 0, sizeof(root->defrag_progress));
	root->fs_info = fs_info;
	root->root_key.objectid = objectid;
	root->node = NULL;
	root->commit_root = NULL;
	root->state = 0;
	RB_CLEAR_NODE(&root->rb_node);

	btrfs_set_root_last_trans(root, 0);
	root->free_objectid = 0;
	root->nr_delalloc_inodes = 0;
	root->nr_ordered_extents = 0;
	xa_init(&root->inodes);
	xa_init(&root->delayed_nodes);

	btrfs_init_root_block_rsv(root);

	INIT_LIST_HEAD(&root->dirty_list);
	INIT_LIST_HEAD(&root->root_list);
	INIT_LIST_HEAD(&root->delalloc_inodes);
	INIT_LIST_HEAD(&root->delalloc_root);
	INIT_LIST_HEAD(&root->ordered_extents);
	INIT_LIST_HEAD(&root->ordered_root);
	INIT_LIST_HEAD(&root->reloc_dirty_list);
	spin_lock_init(&root->delalloc_lock);
	spin_lock_init(&root->ordered_extent_lock);
	spin_lock_init(&root->accounting_lock);
	spin_lock_init(&root->qgroup_meta_rsv_lock);
	mutex_init(&root->objectid_mutex);
	mutex_init(&root->log_mutex);
	mutex_init(&root->ordered_extent_mutex);
	mutex_init(&root->delalloc_mutex);
	init_waitqueue_head(&root->qgroup_flush_wait);
	init_waitqueue_head(&root->log_writer_wait);
	init_waitqueue_head(&root->log_commit_wait[0]);
	init_waitqueue_head(&root->log_commit_wait[1]);
	INIT_LIST_HEAD(&root->log_ctxs[0]);
	INIT_LIST_HEAD(&root->log_ctxs[1]);
	atomic_set(&root->log_commit[0], 0);
	atomic_set(&root->log_commit[1], 0);
	atomic_set(&root->log_writers, 0);
	atomic_set(&root->log_batch, 0);
	refcount_set(&root->refs, 1);
	atomic_set(&root->snapshot_force_cow, 0);
	atomic_set(&root->nr_swapfiles, 0);
	btrfs_set_root_log_transid(root, 0);
	root->log_transid_committed = -1;
	btrfs_set_root_last_log_commit(root, 0);
	root->anon_dev = 0;
	if (!dummy) {
		extent_io_tree_init(fs_info, &root->dirty_log_pages,
				    IO_TREE_ROOT_DIRTY_LOG_PAGES);
		extent_io_tree_init(fs_info, &root->log_csum_range,
				    IO_TREE_LOG_CSUM_RANGE);
	}

	spin_lock_init(&root->root_item_lock);
	btrfs_qgroup_init_swapped_blocks(&root->swapped_blocks);
#ifdef CONFIG_BTRFS_DEBUG
	INIT_LIST_HEAD(&root->leak_list);
	spin_lock(&fs_info->fs_roots_radix_lock);
	list_add_tail(&root->leak_list, &fs_info->allocated_roots);
	spin_unlock(&fs_info->fs_roots_radix_lock);
#endif
}

static struct btrfs_root *btrfs_alloc_root(struct btrfs_fs_info *fs_info,
					   u64 objectid, gfp_t flags)
{
	struct btrfs_root *root = kzalloc(sizeof(*root), flags);
	if (root)
		__setup_root(root, fs_info, objectid);
	return root;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
/* Should only be used by the testing infrastructure */
struct btrfs_root *btrfs_alloc_dummy_root(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;

	if (!fs_info)
		return ERR_PTR(-EINVAL);

	root = btrfs_alloc_root(fs_info, BTRFS_ROOT_TREE_OBJECTID, GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	/* We don't use the stripesize in selftest, set it as sectorsize */
	root->alloc_bytenr = 0;

	return root;
}
#endif

static int global_root_cmp(struct rb_node *a_node, const struct rb_node *b_node)
{
	const struct btrfs_root *a = rb_entry(a_node, struct btrfs_root, rb_node);
	const struct btrfs_root *b = rb_entry(b_node, struct btrfs_root, rb_node);

	return btrfs_comp_cpu_keys(&a->root_key, &b->root_key);
}

static int global_root_key_cmp(const void *k, const struct rb_node *node)
{
	const struct btrfs_key *key = k;
	const struct btrfs_root *root = rb_entry(node, struct btrfs_root, rb_node);

	return btrfs_comp_cpu_keys(key, &root->root_key);
}

int btrfs_global_root_insert(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct rb_node *tmp;
	int ret = 0;

	write_lock(&fs_info->global_root_lock);
	tmp = rb_find_add(&root->rb_node, &fs_info->global_root_tree, global_root_cmp);
	write_unlock(&fs_info->global_root_lock);

	if (tmp) {
		ret = -EEXIST;
		btrfs_warn(fs_info, "global root %llu %llu already exists",
			   btrfs_root_id(root), root->root_key.offset);
	}
	return ret;
}

void btrfs_global_root_delete(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	write_lock(&fs_info->global_root_lock);
	rb_erase(&root->rb_node, &fs_info->global_root_tree);
	write_unlock(&fs_info->global_root_lock);
}

struct btrfs_root *btrfs_global_root(struct btrfs_fs_info *fs_info,
				     struct btrfs_key *key)
{
	struct rb_node *node;
	struct btrfs_root *root = NULL;

	read_lock(&fs_info->global_root_lock);
	node = rb_find(key, &fs_info->global_root_tree, global_root_key_cmp);
	if (node)
		root = container_of(node, struct btrfs_root, rb_node);
	read_unlock(&fs_info->global_root_lock);

	return root;
}

static u64 btrfs_global_root_id(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct btrfs_block_group *block_group;
	u64 ret;

	if (!btrfs_fs_incompat(fs_info, EXTENT_TREE_V2))
		return 0;

	if (bytenr)
		block_group = btrfs_lookup_block_group(fs_info, bytenr);
	else
		block_group = btrfs_lookup_first_block_group(fs_info, bytenr);
	ASSERT(block_group);
	if (!block_group)
		return 0;
	ret = block_group->global_root_id;
	btrfs_put_block_group(block_group);

	return ret;
}

struct btrfs_root *btrfs_csum_root(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct btrfs_key key = {
		.objectid = BTRFS_CSUM_TREE_OBJECTID,
		.type = BTRFS_ROOT_ITEM_KEY,
		.offset = btrfs_global_root_id(fs_info, bytenr),
	};

	return btrfs_global_root(fs_info, &key);
}

struct btrfs_root *btrfs_extent_root(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct btrfs_key key = {
		.objectid = BTRFS_EXTENT_TREE_OBJECTID,
		.type = BTRFS_ROOT_ITEM_KEY,
		.offset = btrfs_global_root_id(fs_info, bytenr),
	};

	return btrfs_global_root(fs_info, &key);
}

struct btrfs_root *btrfs_create_tree(struct btrfs_trans_handle *trans,
				     u64 objectid)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct extent_buffer *leaf;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *root;
	struct btrfs_key key;
	unsigned int nofs_flag;
	int ret = 0;

	/*
	 * We're holding a transaction handle, so use a NOFS memory allocation
	 * context to avoid deadlock if reclaim happens.
	 */
	nofs_flag = memalloc_nofs_save();
	root = btrfs_alloc_root(fs_info, objectid, GFP_KERNEL);
	memalloc_nofs_restore(nofs_flag);
	if (!root)
		return ERR_PTR(-ENOMEM);

	root->root_key.objectid = objectid;
	root->root_key.type = BTRFS_ROOT_ITEM_KEY;
	root->root_key.offset = 0;

	leaf = btrfs_alloc_tree_block(trans, root, 0, objectid, NULL, 0, 0, 0,
				      0, BTRFS_NESTING_NORMAL);
	if (IS_ERR(leaf)) {
		ret = PTR_ERR(leaf);
		leaf = NULL;
		goto fail;
	}

	root->node = leaf;
	btrfs_mark_buffer_dirty(trans, leaf);

	root->commit_root = btrfs_root_node(root);
	set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);

	btrfs_set_root_flags(&root->root_item, 0);
	btrfs_set_root_limit(&root->root_item, 0);
	btrfs_set_root_bytenr(&root->root_item, leaf->start);
	btrfs_set_root_generation(&root->root_item, trans->transid);
	btrfs_set_root_level(&root->root_item, 0);
	btrfs_set_root_refs(&root->root_item, 1);
	btrfs_set_root_used(&root->root_item, leaf->len);
	btrfs_set_root_last_snapshot(&root->root_item, 0);
	btrfs_set_root_dirid(&root->root_item, 0);
	if (is_fstree(objectid))
		generate_random_guid(root->root_item.uuid);
	else
		export_guid(root->root_item.uuid, &guid_null);
	btrfs_set_root_drop_level(&root->root_item, 0);

	btrfs_tree_unlock(leaf);

	key.objectid = objectid;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = 0;
	ret = btrfs_insert_root(trans, tree_root, &key, &root->root_item);
	if (ret)
		goto fail;

	return root;

fail:
	btrfs_put_root(root);

	return ERR_PTR(ret);
}

static struct btrfs_root *alloc_log_tree(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;

	root = btrfs_alloc_root(fs_info, BTRFS_TREE_LOG_OBJECTID, GFP_NOFS);
	if (!root)
		return ERR_PTR(-ENOMEM);

	root->root_key.objectid = BTRFS_TREE_LOG_OBJECTID;
	root->root_key.type = BTRFS_ROOT_ITEM_KEY;
	root->root_key.offset = BTRFS_TREE_LOG_OBJECTID;

	return root;
}

int btrfs_alloc_log_tree_node(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root)
{
	struct extent_buffer *leaf;

	/*
	 * DON'T set SHAREABLE bit for log trees.
	 *
	 * Log trees are not exposed to user space thus can't be snapshotted,
	 * and they go away before a real commit is actually done.
	 *
	 * They do store pointers to file data extents, and those reference
	 * counts still get updated (along with back refs to the log tree).
	 */

	leaf = btrfs_alloc_tree_block(trans, root, 0, BTRFS_TREE_LOG_OBJECTID,
			NULL, 0, 0, 0, 0, BTRFS_NESTING_NORMAL);
	if (IS_ERR(leaf))
		return PTR_ERR(leaf);

	root->node = leaf;

	btrfs_mark_buffer_dirty(trans, root->node);
	btrfs_tree_unlock(root->node);

	return 0;
}

int btrfs_init_log_root_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *log_root;

	log_root = alloc_log_tree(fs_info);
	if (IS_ERR(log_root))
		return PTR_ERR(log_root);

	if (!btrfs_is_zoned(fs_info)) {
		int ret = btrfs_alloc_log_tree_node(trans, log_root);

		if (ret) {
			btrfs_put_root(log_root);
			return ret;
		}
	}

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
	int ret;

	log_root = alloc_log_tree(fs_info);
	if (IS_ERR(log_root))
		return PTR_ERR(log_root);

	ret = btrfs_alloc_log_tree_node(trans, log_root);
	if (ret) {
		btrfs_put_root(log_root);
		return ret;
	}

	btrfs_set_root_last_trans(log_root, trans->transid);
	log_root->root_key.offset = btrfs_root_id(root);

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
	btrfs_set_root_log_transid(root, 0);
	root->log_transid_committed = -1;
	btrfs_set_root_last_log_commit(root, 0);
	return 0;
}

static struct btrfs_root *read_tree_root_path(struct btrfs_root *tree_root,
					      struct btrfs_path *path,
					      const struct btrfs_key *key)
{
	struct btrfs_root *root;
	struct btrfs_tree_parent_check check = { 0 };
	struct btrfs_fs_info *fs_info = tree_root->fs_info;
	u64 generation;
	int ret;
	int level;

	root = btrfs_alloc_root(fs_info, key->objectid, GFP_NOFS);
	if (!root)
		return ERR_PTR(-ENOMEM);

	ret = btrfs_find_root(tree_root, key, path,
			      &root->root_item, &root->root_key);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto fail;
	}

	generation = btrfs_root_generation(&root->root_item);
	level = btrfs_root_level(&root->root_item);
	check.level = level;
	check.transid = generation;
	check.owner_root = key->objectid;
	root->node = read_tree_block(fs_info, btrfs_root_bytenr(&root->root_item),
				     &check);
	if (IS_ERR(root->node)) {
		ret = PTR_ERR(root->node);
		root->node = NULL;
		goto fail;
	}
	if (!btrfs_buffer_uptodate(root->node, generation, 0)) {
		ret = -EIO;
		goto fail;
	}

	/*
	 * For real fs, and not log/reloc trees, root owner must
	 * match its root node owner
	 */
	if (!btrfs_is_testing(fs_info) &&
	    btrfs_root_id(root) != BTRFS_TREE_LOG_OBJECTID &&
	    btrfs_root_id(root) != BTRFS_TREE_RELOC_OBJECTID &&
	    btrfs_root_id(root) != btrfs_header_owner(root->node)) {
		btrfs_crit(fs_info,
"root=%llu block=%llu, tree root owner mismatch, have %llu expect %llu",
			   btrfs_root_id(root), root->node->start,
			   btrfs_header_owner(root->node),
			   btrfs_root_id(root));
		ret = -EUCLEAN;
		goto fail;
	}
	root->commit_root = btrfs_root_node(root);
	return root;
fail:
	btrfs_put_root(root);
	return ERR_PTR(ret);
}

struct btrfs_root *btrfs_read_tree_root(struct btrfs_root *tree_root,
					const struct btrfs_key *key)
{
	struct btrfs_root *root;
	struct btrfs_path *path;

	path = btrfs_alloc_path();
	if (!path)
		return ERR_PTR(-ENOMEM);
	root = read_tree_root_path(tree_root, path, key);
	btrfs_free_path(path);

	return root;
}

/*
 * Initialize subvolume root in-memory structure
 *
 * @anon_dev:	anonymous device to attach to the root, if zero, allocate new
 */
static int btrfs_init_fs_root(struct btrfs_root *root, dev_t anon_dev)
{
	int ret;

	btrfs_drew_lock_init(&root->snapshot_lock);

	if (btrfs_root_id(root) != BTRFS_TREE_LOG_OBJECTID &&
	    !btrfs_is_data_reloc_root(root) &&
	    is_fstree(btrfs_root_id(root))) {
		set_bit(BTRFS_ROOT_SHAREABLE, &root->state);
		btrfs_check_and_init_root_item(&root->root_item);
	}

	/*
	 * Don't assign anonymous block device to roots that are not exposed to
	 * userspace, the id pool is limited to 1M
	 */
	if (is_fstree(btrfs_root_id(root)) &&
	    btrfs_root_refs(&root->root_item) > 0) {
		if (!anon_dev) {
			ret = get_anon_bdev(&root->anon_dev);
			if (ret)
				goto fail;
		} else {
			root->anon_dev = anon_dev;
		}
	}

	mutex_lock(&root->objectid_mutex);
	ret = btrfs_init_root_free_objectid(root);
	if (ret) {
		mutex_unlock(&root->objectid_mutex);
		goto fail;
	}

	ASSERT(root->free_objectid <= BTRFS_LAST_FREE_OBJECTID);

	mutex_unlock(&root->objectid_mutex);

	return 0;
fail:
	/* The caller is responsible to call btrfs_free_fs_root */
	return ret;
}

static struct btrfs_root *btrfs_lookup_fs_root(struct btrfs_fs_info *fs_info,
					       u64 root_id)
{
	struct btrfs_root *root;

	spin_lock(&fs_info->fs_roots_radix_lock);
	root = radix_tree_lookup(&fs_info->fs_roots_radix,
				 (unsigned long)root_id);
	root = btrfs_grab_root(root);
	spin_unlock(&fs_info->fs_roots_radix_lock);
	return root;
}

static struct btrfs_root *btrfs_get_global_root(struct btrfs_fs_info *fs_info,
						u64 objectid)
{
	struct btrfs_key key = {
		.objectid = objectid,
		.type = BTRFS_ROOT_ITEM_KEY,
		.offset = 0,
	};

	switch (objectid) {
	case BTRFS_ROOT_TREE_OBJECTID:
		return btrfs_grab_root(fs_info->tree_root);
	case BTRFS_EXTENT_TREE_OBJECTID:
		return btrfs_grab_root(btrfs_global_root(fs_info, &key));
	case BTRFS_CHUNK_TREE_OBJECTID:
		return btrfs_grab_root(fs_info->chunk_root);
	case BTRFS_DEV_TREE_OBJECTID:
		return btrfs_grab_root(fs_info->dev_root);
	case BTRFS_CSUM_TREE_OBJECTID:
		return btrfs_grab_root(btrfs_global_root(fs_info, &key));
	case BTRFS_QUOTA_TREE_OBJECTID:
		return btrfs_grab_root(fs_info->quota_root);
	case BTRFS_UUID_TREE_OBJECTID:
		return btrfs_grab_root(fs_info->uuid_root);
	case BTRFS_BLOCK_GROUP_TREE_OBJECTID:
		return btrfs_grab_root(fs_info->block_group_root);
	case BTRFS_FREE_SPACE_TREE_OBJECTID:
		return btrfs_grab_root(btrfs_global_root(fs_info, &key));
	case BTRFS_RAID_STRIPE_TREE_OBJECTID:
		return btrfs_grab_root(fs_info->stripe_root);
	default:
		return NULL;
	}
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
				(unsigned long)btrfs_root_id(root),
				root);
	if (ret == 0) {
		btrfs_grab_root(root);
		set_bit(BTRFS_ROOT_IN_RADIX, &root->state);
	}
	spin_unlock(&fs_info->fs_roots_radix_lock);
	radix_tree_preload_end();

	return ret;
}

void btrfs_check_leaked_roots(const struct btrfs_fs_info *fs_info)
{
#ifdef CONFIG_BTRFS_DEBUG
	struct btrfs_root *root;

	while (!list_empty(&fs_info->allocated_roots)) {
		char buf[BTRFS_ROOT_NAME_BUF_LEN];

		root = list_first_entry(&fs_info->allocated_roots,
					struct btrfs_root, leak_list);
		btrfs_err(fs_info, "leaked root %s refcount %d",
			  btrfs_root_name(&root->root_key, buf),
			  refcount_read(&root->refs));
		WARN_ON_ONCE(1);
		while (refcount_read(&root->refs) > 1)
			btrfs_put_root(root);
		btrfs_put_root(root);
	}
#endif
}

static void free_global_roots(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;
	struct rb_node *node;

	while ((node = rb_first_postorder(&fs_info->global_root_tree)) != NULL) {
		root = rb_entry(node, struct btrfs_root, rb_node);
		rb_erase(&root->rb_node, &fs_info->global_root_tree);
		btrfs_put_root(root);
	}
}

void btrfs_free_fs_info(struct btrfs_fs_info *fs_info)
{
	struct percpu_counter *em_counter = &fs_info->evictable_extent_maps;

	percpu_counter_destroy(&fs_info->stats_read_blocks);
	percpu_counter_destroy(&fs_info->dirty_metadata_bytes);
	percpu_counter_destroy(&fs_info->delalloc_bytes);
	percpu_counter_destroy(&fs_info->ordered_bytes);
	if (percpu_counter_initialized(em_counter))
		ASSERT(percpu_counter_sum_positive(em_counter) == 0);
	percpu_counter_destroy(em_counter);
	percpu_counter_destroy(&fs_info->dev_replace.bio_counter);
	btrfs_free_csum_hash(fs_info);
	btrfs_free_stripe_hash_table(fs_info);
	btrfs_free_ref_cache(fs_info);
	kfree(fs_info->balance_ctl);
	kfree(fs_info->delayed_root);
	free_global_roots(fs_info);
	btrfs_put_root(fs_info->tree_root);
	btrfs_put_root(fs_info->chunk_root);
	btrfs_put_root(fs_info->dev_root);
	btrfs_put_root(fs_info->quota_root);
	btrfs_put_root(fs_info->uuid_root);
	btrfs_put_root(fs_info->fs_root);
	btrfs_put_root(fs_info->data_reloc_root);
	btrfs_put_root(fs_info->block_group_root);
	btrfs_put_root(fs_info->stripe_root);
	btrfs_check_leaked_roots(fs_info);
	btrfs_extent_buffer_leak_debug_check(fs_info);
	kfree(fs_info->super_copy);
	kfree(fs_info->super_for_commit);
	kvfree(fs_info);
}


/*
 * Get an in-memory reference of a root structure.
 *
 * For essential trees like root/extent tree, we grab it from fs_info directly.
 * For subvolume trees, we check the cached filesystem roots first. If not
 * found, then read it from disk and add it to cached fs roots.
 *
 * Caller should release the root by calling btrfs_put_root() after the usage.
 *
 * NOTE: Reloc and log trees can't be read by this function as they share the
 *	 same root objectid.
 *
 * @objectid:	root id
 * @anon_dev:	preallocated anonymous block device number for new roots,
 *		pass NULL for a new allocation.
 * @check_ref:	whether to check root item references, If true, return -ENOENT
 *		for orphan roots
 */
static struct btrfs_root *btrfs_get_root_ref(struct btrfs_fs_info *fs_info,
					     u64 objectid, dev_t *anon_dev,
					     bool check_ref)
{
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;

	root = btrfs_get_global_root(fs_info, objectid);
	if (root)
		return root;

	/*
	 * If we're called for non-subvolume trees, and above function didn't
	 * find one, do not try to read it from disk.
	 *
	 * This is namely for free-space-tree and quota tree, which can change
	 * at runtime and should only be grabbed from fs_info.
	 */
	if (!is_fstree(objectid) && objectid != BTRFS_DATA_RELOC_TREE_OBJECTID)
		return ERR_PTR(-ENOENT);
again:
	root = btrfs_lookup_fs_root(fs_info, objectid);
	if (root) {
		/*
		 * Some other caller may have read out the newly inserted
		 * subvolume already (for things like backref walk etc).  Not
		 * that common but still possible.  In that case, we just need
		 * to free the anon_dev.
		 */
		if (unlikely(anon_dev && *anon_dev)) {
			free_anon_bdev(*anon_dev);
			*anon_dev = 0;
		}

		if (check_ref && btrfs_root_refs(&root->root_item) == 0) {
			btrfs_put_root(root);
			return ERR_PTR(-ENOENT);
		}
		return root;
	}

	key.objectid = objectid;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;
	root = btrfs_read_tree_root(fs_info->tree_root, &key);
	if (IS_ERR(root))
		return root;

	if (check_ref && btrfs_root_refs(&root->root_item) == 0) {
		ret = -ENOENT;
		goto fail;
	}

	ret = btrfs_init_fs_root(root, anon_dev ? *anon_dev : 0);
	if (ret)
		goto fail;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto fail;
	}
	key.objectid = BTRFS_ORPHAN_OBJECTID;
	key.type = BTRFS_ORPHAN_ITEM_KEY;
	key.offset = objectid;

	ret = btrfs_search_slot(NULL, fs_info->tree_root, &key, path, 0, 0);
	btrfs_free_path(path);
	if (ret < 0)
		goto fail;
	if (ret == 0)
		set_bit(BTRFS_ROOT_ORPHAN_ITEM_INSERTED, &root->state);

	ret = btrfs_insert_fs_root(fs_info, root);
	if (ret) {
		if (ret == -EEXIST) {
			btrfs_put_root(root);
			goto again;
		}
		goto fail;
	}
	return root;
fail:
	/*
	 * If our caller provided us an anonymous device, then it's his
	 * responsibility to free it in case we fail. So we have to set our
	 * root's anon_dev to 0 to avoid a double free, once by btrfs_put_root()
	 * and once again by our caller.
	 */
	if (anon_dev && *anon_dev)
		root->anon_dev = 0;
	btrfs_put_root(root);
	return ERR_PTR(ret);
}

/*
 * Get in-memory reference of a root structure
 *
 * @objectid:	tree objectid
 * @check_ref:	if set, verify that the tree exists and the item has at least
 *		one reference
 */
struct btrfs_root *btrfs_get_fs_root(struct btrfs_fs_info *fs_info,
				     u64 objectid, bool check_ref)
{
	return btrfs_get_root_ref(fs_info, objectid, NULL, check_ref);
}

/*
 * Get in-memory reference of a root structure, created as new, optionally pass
 * the anonymous block device id
 *
 * @objectid:	tree objectid
 * @anon_dev:	if NULL, allocate a new anonymous block device or use the
 *		parameter value if not NULL
 */
struct btrfs_root *btrfs_get_new_fs_root(struct btrfs_fs_info *fs_info,
					 u64 objectid, dev_t *anon_dev)
{
	return btrfs_get_root_ref(fs_info, objectid, anon_dev, true);
}

/*
 * Return a root for the given objectid.
 *
 * @fs_info:	the fs_info
 * @objectid:	the objectid we need to lookup
 *
 * This is exclusively used for backref walking, and exists specifically because
 * of how qgroups does lookups.  Qgroups will do a backref lookup at delayed ref
 * creation time, which means we may have to read the tree_root in order to look
 * up a fs root that is not in memory.  If the root is not in memory we will
 * read the tree root commit root and look up the fs root from there.  This is a
 * temporary root, it will not be inserted into the radix tree as it doesn't
 * have the most uptodate information, it'll simply be discarded once the
 * backref code is finished using the root.
 */
struct btrfs_root *btrfs_get_fs_root_commit_root(struct btrfs_fs_info *fs_info,
						 struct btrfs_path *path,
						 u64 objectid)
{
	struct btrfs_root *root;
	struct btrfs_key key;

	ASSERT(path->search_commit_root && path->skip_locking);

	/*
	 * This can return -ENOENT if we ask for a root that doesn't exist, but
	 * since this is called via the backref walking code we won't be looking
	 * up a root that doesn't exist, unless there's corruption.  So if root
	 * != NULL just return it.
	 */
	root = btrfs_get_global_root(fs_info, objectid);
	if (root)
		return root;

	root = btrfs_lookup_fs_root(fs_info, objectid);
	if (root)
		return root;

	key.objectid = objectid;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;
	root = read_tree_root_path(fs_info->tree_root, path, &key);
	btrfs_release_path(path);

	return root;
}

static int cleaner_kthread(void *arg)
{
	struct btrfs_fs_info *fs_info = arg;
	int again;

	while (1) {
		again = 0;

		set_bit(BTRFS_FS_CLEANER_RUNNING, &fs_info->flags);

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

		if (test_and_clear_bit(BTRFS_FS_FEATURE_CHANGED, &fs_info->flags))
			btrfs_sysfs_feature_update(fs_info);

		btrfs_run_delayed_iputs(fs_info);

		again = btrfs_clean_one_deleted_snapshot(fs_info);
		mutex_unlock(&fs_info->cleaner_mutex);

		/*
		 * The defragger has dealt with the R/O remount and umount,
		 * needn't do anything special here.
		 */
		btrfs_run_defrag_inodes(fs_info);

		/*
		 * Acquires fs_info->reclaim_bgs_lock to avoid racing
		 * with relocation (btrfs_relocate_chunk) and relocation
		 * acquires fs_info->cleaner_mutex (btrfs_relocate_block_group)
		 * after acquiring fs_info->reclaim_bgs_lock. So we
		 * can't hold, nor need to, fs_info->cleaner_mutex when deleting
		 * unused block groups.
		 */
		btrfs_delete_unused_bgs(fs_info);

		/*
		 * Reclaim block groups in the reclaim_bgs list after we deleted
		 * all unused block_groups. This possibly gives us some more free
		 * space.
		 */
		btrfs_reclaim_bgs(fs_info);
sleep:
		clear_and_wake_up_bit(BTRFS_FS_CLEANER_RUNNING, &fs_info->flags);
		if (kthread_should_park())
			kthread_parkme();
		if (kthread_should_stop())
			return 0;
		if (!again) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			__set_current_state(TASK_RUNNING);
		}
	}
}

static int transaction_kthread(void *arg)
{
	struct btrfs_root *root = arg;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_transaction *cur;
	u64 transid;
	time64_t delta;
	unsigned long delay;
	bool cannot_commit;

	do {
		cannot_commit = false;
		delay = secs_to_jiffies(fs_info->commit_interval);
		mutex_lock(&fs_info->transaction_kthread_mutex);

		spin_lock(&fs_info->trans_lock);
		cur = fs_info->running_transaction;
		if (!cur) {
			spin_unlock(&fs_info->trans_lock);
			goto sleep;
		}

		delta = ktime_get_seconds() - cur->start_time;
		if (!test_and_clear_bit(BTRFS_FS_COMMIT_TRANS, &fs_info->flags) &&
		    cur->state < TRANS_STATE_COMMIT_PREP &&
		    delta < fs_info->commit_interval) {
			spin_unlock(&fs_info->trans_lock);
			delay -= secs_to_jiffies(delta - 1);
			delay = min(delay,
				    secs_to_jiffies(fs_info->commit_interval));
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

		if (BTRFS_FS_ERROR(fs_info))
			btrfs_cleanup_transaction(fs_info);
		if (!kthread_should_stop() &&
				(!btrfs_transaction_blocked(fs_info) ||
				 cannot_commit))
			schedule_timeout_interruptible(delay);
	} while (!kthread_should_stop());
	return 0;
}

/*
 * This will find the highest generation in the array of root backups.  The
 * index of the highest array is returned, or -EINVAL if we can't find
 * anything.
 *
 * We check to make sure the array is valid by comparing the
 * generation of the latest  root in the array with the generation
 * in the super block.  If they don't match we pitch it.
 */
static int find_newest_super_backup(struct btrfs_fs_info *info)
{
	const u64 newest_gen = btrfs_super_generation(info->super_copy);
	u64 cur;
	struct btrfs_root_backup *root_backup;
	int i;

	for (i = 0; i < BTRFS_NUM_BACKUP_ROOTS; i++) {
		root_backup = info->super_copy->super_roots + i;
		cur = btrfs_backup_tree_root_gen(root_backup);
		if (cur == newest_gen)
			return i;
	}

	return -EINVAL;
}

/*
 * copy all the root pointers into the super backup array.
 * this will bump the backup pointer by one when it is
 * done
 */
static void backup_super_roots(struct btrfs_fs_info *info)
{
	const int next_backup = info->backup_root_index;
	struct btrfs_root_backup *root_backup;

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

	if (!btrfs_fs_compat_ro(info, BLOCK_GROUP_TREE)) {
		struct btrfs_root *extent_root = btrfs_extent_root(info, 0);
		struct btrfs_root *csum_root = btrfs_csum_root(info, 0);

		btrfs_set_backup_extent_root(root_backup,
					     extent_root->node->start);
		btrfs_set_backup_extent_root_gen(root_backup,
				btrfs_header_generation(extent_root->node));
		btrfs_set_backup_extent_root_level(root_backup,
					btrfs_header_level(extent_root->node));

		btrfs_set_backup_csum_root(root_backup, csum_root->node->start);
		btrfs_set_backup_csum_root_gen(root_backup,
					       btrfs_header_generation(csum_root->node));
		btrfs_set_backup_csum_root_level(root_backup,
						 btrfs_header_level(csum_root->node));
	}

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
 * Reads a backup root based on the passed priority. Prio 0 is the newest, prio
 * 1/2/3 are 2nd newest/3rd newest/4th (oldest) backup roots
 *
 * @fs_info:  filesystem whose backup roots need to be read
 * @priority: priority of backup root required
 *
 * Returns backup root index on success and -EINVAL otherwise.
 */
static int read_backup_root(struct btrfs_fs_info *fs_info, u8 priority)
{
	int backup_index = find_newest_super_backup(fs_info);
	struct btrfs_super_block *super = fs_info->super_copy;
	struct btrfs_root_backup *root_backup;

	if (priority < BTRFS_NUM_BACKUP_ROOTS && backup_index >= 0) {
		if (priority == 0)
			return backup_index;

		backup_index = backup_index + BTRFS_NUM_BACKUP_ROOTS - priority;
		backup_index %= BTRFS_NUM_BACKUP_ROOTS;
	} else {
		return -EINVAL;
	}

	root_backup = super->super_roots + backup_index;

	btrfs_set_super_generation(super,
				   btrfs_backup_tree_root_gen(root_backup));
	btrfs_set_super_root(super, btrfs_backup_tree_root(root_backup));
	btrfs_set_super_root_level(super,
				   btrfs_backup_tree_root_level(root_backup));
	btrfs_set_super_bytes_used(super, btrfs_backup_bytes_used(root_backup));

	/*
	 * Fixme: the total bytes and num_devices need to match or we should
	 * need a fsck
	 */
	btrfs_set_super_total_bytes(super, btrfs_backup_total_bytes(root_backup));
	btrfs_set_super_num_devices(super, btrfs_backup_num_devices(root_backup));

	return backup_index;
}

/* helper to cleanup workers */
static void btrfs_stop_all_workers(struct btrfs_fs_info *fs_info)
{
	btrfs_destroy_workqueue(fs_info->fixup_workers);
	btrfs_destroy_workqueue(fs_info->delalloc_workers);
	btrfs_destroy_workqueue(fs_info->workers);
	if (fs_info->endio_workers)
		destroy_workqueue(fs_info->endio_workers);
	if (fs_info->rmw_workers)
		destroy_workqueue(fs_info->rmw_workers);
	if (fs_info->compressed_write_workers)
		destroy_workqueue(fs_info->compressed_write_workers);
	btrfs_destroy_workqueue(fs_info->endio_write_workers);
	btrfs_destroy_workqueue(fs_info->endio_freespace_worker);
	btrfs_destroy_workqueue(fs_info->delayed_workers);
	btrfs_destroy_workqueue(fs_info->caching_workers);
	btrfs_destroy_workqueue(fs_info->flush_workers);
	btrfs_destroy_workqueue(fs_info->qgroup_rescan_workers);
	if (fs_info->discard_ctl.discard_workers)
		destroy_workqueue(fs_info->discard_ctl.discard_workers);
	/*
	 * Now that all other work queues are destroyed, we can safely destroy
	 * the queues used for metadata I/O, since tasks from those other work
	 * queues can do metadata I/O operations.
	 */
	if (fs_info->endio_meta_workers)
		destroy_workqueue(fs_info->endio_meta_workers);
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

static void free_global_root_pointers(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root, *tmp;

	rbtree_postorder_for_each_entry_safe(root, tmp,
					     &fs_info->global_root_tree,
					     rb_node)
		free_root_extent_buffers(root);
}

/* helper to cleanup tree roots */
static void free_root_pointers(struct btrfs_fs_info *info, bool free_chunk_root)
{
	free_root_extent_buffers(info->tree_root);

	free_global_root_pointers(info);
	free_root_extent_buffers(info->dev_root);
	free_root_extent_buffers(info->quota_root);
	free_root_extent_buffers(info->uuid_root);
	free_root_extent_buffers(info->fs_root);
	free_root_extent_buffers(info->data_reloc_root);
	free_root_extent_buffers(info->block_group_root);
	free_root_extent_buffers(info->stripe_root);
	if (free_chunk_root)
		free_root_extent_buffers(info->chunk_root);
}

void btrfs_put_root(struct btrfs_root *root)
{
	if (!root)
		return;

	if (refcount_dec_and_test(&root->refs)) {
		if (WARN_ON(!xa_empty(&root->inodes)))
			xa_destroy(&root->inodes);
		WARN_ON(test_bit(BTRFS_ROOT_DEAD_RELOC_TREE, &root->state));
		if (root->anon_dev)
			free_anon_bdev(root->anon_dev);
		free_root_extent_buffers(root);
#ifdef CONFIG_BTRFS_DEBUG
		spin_lock(&root->fs_info->fs_roots_radix_lock);
		list_del_init(&root->leak_list);
		spin_unlock(&root->fs_info->fs_roots_radix_lock);
#endif
		kfree(root);
	}
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

		if (test_bit(BTRFS_ROOT_IN_RADIX, &gang[0]->state))
			btrfs_drop_and_free_fs_root(fs_info, gang[0]);
		btrfs_put_root(gang[0]);
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
}

static void btrfs_init_scrub(struct btrfs_fs_info *fs_info)
{
	mutex_init(&fs_info->scrub_lock);
	atomic_set(&fs_info->scrubs_running, 0);
	atomic_set(&fs_info->scrub_pause_req, 0);
	atomic_set(&fs_info->scrubs_paused, 0);
	atomic_set(&fs_info->scrub_cancel_req, 0);
	init_waitqueue_head(&fs_info->scrub_pause_wait);
	refcount_set(&fs_info->scrub_workers_refcnt, 0);
}

static void btrfs_init_balance(struct btrfs_fs_info *fs_info)
{
	spin_lock_init(&fs_info->balance_lock);
	mutex_init(&fs_info->balance_mutex);
	atomic_set(&fs_info->balance_pause_req, 0);
	atomic_set(&fs_info->balance_cancel_req, 0);
	fs_info->balance_ctl = NULL;
	init_waitqueue_head(&fs_info->balance_wait_q);
	atomic_set(&fs_info->reloc_cancel_req, 0);
}

static int btrfs_init_btree_inode(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	unsigned long hash = btrfs_inode_hash(BTRFS_BTREE_INODE_OBJECTID,
					      fs_info->tree_root);
	struct inode *inode;

	inode = new_inode(sb);
	if (!inode)
		return -ENOMEM;

	btrfs_set_inode_number(BTRFS_I(inode), BTRFS_BTREE_INODE_OBJECTID);
	set_nlink(inode, 1);
	/*
	 * we set the i_size on the btree inode to the max possible int.
	 * the real end of the address space is determined by all of
	 * the devices in the system
	 */
	inode->i_size = OFFSET_MAX;
	inode->i_mapping->a_ops = &btree_aops;
	mapping_set_gfp_mask(inode->i_mapping, GFP_NOFS);

	extent_io_tree_init(fs_info, &BTRFS_I(inode)->io_tree,
			    IO_TREE_BTREE_INODE_IO);
	extent_map_tree_init(&BTRFS_I(inode)->extent_tree);

	BTRFS_I(inode)->root = btrfs_grab_root(fs_info->tree_root);
	set_bit(BTRFS_INODE_DUMMY, &BTRFS_I(inode)->runtime_flags);
	__insert_inode_hash(inode, hash);
	fs_info->btree_inode = inode;

	return 0;
}

static void btrfs_init_dev_replace_locks(struct btrfs_fs_info *fs_info)
{
	mutex_init(&fs_info->dev_replace.lock_finishing_cancel_unmount);
	init_rwsem(&fs_info->dev_replace.rwsem);
	init_waitqueue_head(&fs_info->dev_replace.replace_wait);
}

static void btrfs_init_qgroup(struct btrfs_fs_info *fs_info)
{
	spin_lock_init(&fs_info->qgroup_lock);
	mutex_init(&fs_info->qgroup_ioctl_lock);
	fs_info->qgroup_tree = RB_ROOT;
	INIT_LIST_HEAD(&fs_info->dirty_qgroups);
	fs_info->qgroup_seq = 1;
	fs_info->qgroup_ulist = NULL;
	fs_info->qgroup_rescan_running = false;
	fs_info->qgroup_drop_subtree_thres = BTRFS_QGROUP_DROP_SUBTREE_THRES_DEFAULT;
	mutex_init(&fs_info->qgroup_rescan_lock);
}

static int btrfs_init_workqueues(struct btrfs_fs_info *fs_info)
{
	u32 max_active = fs_info->thread_pool_size;
	unsigned int flags = WQ_MEM_RECLAIM | WQ_FREEZABLE | WQ_UNBOUND;
	unsigned int ordered_flags = WQ_MEM_RECLAIM | WQ_FREEZABLE;

	fs_info->workers =
		btrfs_alloc_workqueue(fs_info, "worker", flags, max_active, 16);

	fs_info->delalloc_workers =
		btrfs_alloc_workqueue(fs_info, "delalloc",
				      flags, max_active, 2);

	fs_info->flush_workers =
		btrfs_alloc_workqueue(fs_info, "flush_delalloc",
				      flags, max_active, 0);

	fs_info->caching_workers =
		btrfs_alloc_workqueue(fs_info, "cache", flags, max_active, 0);

	fs_info->fixup_workers =
		btrfs_alloc_ordered_workqueue(fs_info, "fixup", ordered_flags);

	fs_info->endio_workers =
		alloc_workqueue("btrfs-endio", flags, max_active);
	fs_info->endio_meta_workers =
		alloc_workqueue("btrfs-endio-meta", flags, max_active);
	fs_info->rmw_workers = alloc_workqueue("btrfs-rmw", flags, max_active);
	fs_info->endio_write_workers =
		btrfs_alloc_workqueue(fs_info, "endio-write", flags,
				      max_active, 2);
	fs_info->compressed_write_workers =
		alloc_workqueue("btrfs-compressed-write", flags, max_active);
	fs_info->endio_freespace_worker =
		btrfs_alloc_workqueue(fs_info, "freespace-write", flags,
				      max_active, 0);
	fs_info->delayed_workers =
		btrfs_alloc_workqueue(fs_info, "delayed-meta", flags,
				      max_active, 0);
	fs_info->qgroup_rescan_workers =
		btrfs_alloc_ordered_workqueue(fs_info, "qgroup-rescan",
					      ordered_flags);
	fs_info->discard_ctl.discard_workers =
		alloc_ordered_workqueue("btrfs_discard", WQ_FREEZABLE);

	if (!(fs_info->workers &&
	      fs_info->delalloc_workers && fs_info->flush_workers &&
	      fs_info->endio_workers && fs_info->endio_meta_workers &&
	      fs_info->compressed_write_workers &&
	      fs_info->endio_write_workers &&
	      fs_info->endio_freespace_worker && fs_info->rmw_workers &&
	      fs_info->caching_workers && fs_info->fixup_workers &&
	      fs_info->delayed_workers && fs_info->qgroup_rescan_workers &&
	      fs_info->discard_ctl.discard_workers)) {
		return -ENOMEM;
	}

	return 0;
}

static int btrfs_init_csum_hash(struct btrfs_fs_info *fs_info, u16 csum_type)
{
	struct crypto_shash *csum_shash;
	const char *csum_driver = btrfs_super_csum_driver(csum_type);

	csum_shash = crypto_alloc_shash(csum_driver, 0, 0);

	if (IS_ERR(csum_shash)) {
		btrfs_err(fs_info, "error allocating %s hash for checksum",
			  csum_driver);
		return PTR_ERR(csum_shash);
	}

	fs_info->csum_shash = csum_shash;

	/*
	 * Check if the checksum implementation is a fast accelerated one.
	 * As-is this is a bit of a hack and should be replaced once the csum
	 * implementations provide that information themselves.
	 */
	switch (csum_type) {
	case BTRFS_CSUM_TYPE_CRC32:
		if (!strstr(crypto_shash_driver_name(csum_shash), "generic"))
			set_bit(BTRFS_FS_CSUM_IMPL_FAST, &fs_info->flags);
		break;
	case BTRFS_CSUM_TYPE_XXHASH:
		set_bit(BTRFS_FS_CSUM_IMPL_FAST, &fs_info->flags);
		break;
	default:
		break;
	}

	btrfs_info(fs_info, "using %s (%s) checksum algorithm",
			btrfs_super_csum_name(csum_type),
			crypto_shash_driver_name(csum_shash));
	return 0;
}

static int btrfs_replay_log(struct btrfs_fs_info *fs_info,
			    struct btrfs_fs_devices *fs_devices)
{
	int ret;
	struct btrfs_tree_parent_check check = { 0 };
	struct btrfs_root *log_tree_root;
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	u64 bytenr = btrfs_super_log_root(disk_super);
	int level = btrfs_super_log_root_level(disk_super);

	if (fs_devices->rw_devices == 0) {
		btrfs_warn(fs_info, "log replay required on RO media");
		return -EIO;
	}

	log_tree_root = btrfs_alloc_root(fs_info, BTRFS_TREE_LOG_OBJECTID,
					 GFP_KERNEL);
	if (!log_tree_root)
		return -ENOMEM;

	check.level = level;
	check.transid = fs_info->generation + 1;
	check.owner_root = BTRFS_TREE_LOG_OBJECTID;
	log_tree_root->node = read_tree_block(fs_info, bytenr, &check);
	if (IS_ERR(log_tree_root->node)) {
		btrfs_warn(fs_info, "failed to read log tree");
		ret = PTR_ERR(log_tree_root->node);
		log_tree_root->node = NULL;
		btrfs_put_root(log_tree_root);
		return ret;
	}
	if (!extent_buffer_uptodate(log_tree_root->node)) {
		btrfs_err(fs_info, "failed to read log tree");
		btrfs_put_root(log_tree_root);
		return -EIO;
	}

	/* returns with log_tree_root freed on success */
	ret = btrfs_recover_log_trees(log_tree_root);
	if (ret) {
		btrfs_handle_fs_error(fs_info, ret,
				      "Failed to recover log tree");
		btrfs_put_root(log_tree_root);
		return ret;
	}

	if (sb_rdonly(fs_info->sb)) {
		ret = btrfs_commit_super(fs_info);
		if (ret)
			return ret;
	}

	return 0;
}

static int load_global_roots_objectid(struct btrfs_root *tree_root,
				      struct btrfs_path *path, u64 objectid,
				      const char *name)
{
	struct btrfs_fs_info *fs_info = tree_root->fs_info;
	struct btrfs_root *root;
	u64 max_global_id = 0;
	int ret;
	struct btrfs_key key = {
		.objectid = objectid,
		.type = BTRFS_ROOT_ITEM_KEY,
		.offset = 0,
	};
	bool found = false;

	/* If we have IGNOREDATACSUMS skip loading these roots. */
	if (objectid == BTRFS_CSUM_TREE_OBJECTID &&
	    btrfs_test_opt(fs_info, IGNOREDATACSUMS)) {
		set_bit(BTRFS_FS_STATE_NO_DATA_CSUMS, &fs_info->fs_state);
		return 0;
	}

	while (1) {
		ret = btrfs_search_slot(NULL, tree_root, &key, path, 0, 0);
		if (ret < 0)
			break;

		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(tree_root, path);
			if (ret) {
				if (ret > 0)
					ret = 0;
				break;
			}
		}
		ret = 0;

		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid != objectid)
			break;
		btrfs_release_path(path);

		/*
		 * Just worry about this for extent tree, it'll be the same for
		 * everybody.
		 */
		if (objectid == BTRFS_EXTENT_TREE_OBJECTID)
			max_global_id = max(max_global_id, key.offset);

		found = true;
		root = read_tree_root_path(tree_root, path, &key);
		if (IS_ERR(root)) {
			if (!btrfs_test_opt(fs_info, IGNOREBADROOTS))
				ret = PTR_ERR(root);
			break;
		}
		set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
		ret = btrfs_global_root_insert(root);
		if (ret) {
			btrfs_put_root(root);
			break;
		}
		key.offset++;
	}
	btrfs_release_path(path);

	if (objectid == BTRFS_EXTENT_TREE_OBJECTID)
		fs_info->nr_global_roots = max_global_id + 1;

	if (!found || ret) {
		if (objectid == BTRFS_CSUM_TREE_OBJECTID)
			set_bit(BTRFS_FS_STATE_NO_DATA_CSUMS, &fs_info->fs_state);

		if (!btrfs_test_opt(fs_info, IGNOREBADROOTS))
			ret = ret ? ret : -ENOENT;
		else
			ret = 0;
		btrfs_err(fs_info, "failed to load root %s", name);
	}
	return ret;
}

static int load_global_roots(struct btrfs_root *tree_root)
{
	struct btrfs_path *path;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = load_global_roots_objectid(tree_root, path,
					 BTRFS_EXTENT_TREE_OBJECTID, "extent");
	if (ret)
		goto out;
	ret = load_global_roots_objectid(tree_root, path,
					 BTRFS_CSUM_TREE_OBJECTID, "csum");
	if (ret)
		goto out;
	if (!btrfs_fs_compat_ro(tree_root->fs_info, FREE_SPACE_TREE))
		goto out;
	ret = load_global_roots_objectid(tree_root, path,
					 BTRFS_FREE_SPACE_TREE_OBJECTID,
					 "free space");
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_read_roots(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *root;
	struct btrfs_key location;
	int ret;

	ASSERT(fs_info->tree_root);

	ret = load_global_roots(tree_root);
	if (ret)
		return ret;

	location.type = BTRFS_ROOT_ITEM_KEY;
	location.offset = 0;

	if (btrfs_fs_compat_ro(fs_info, BLOCK_GROUP_TREE)) {
		location.objectid = BTRFS_BLOCK_GROUP_TREE_OBJECTID;
		root = btrfs_read_tree_root(tree_root, &location);
		if (IS_ERR(root)) {
			if (!btrfs_test_opt(fs_info, IGNOREBADROOTS)) {
				ret = PTR_ERR(root);
				goto out;
			}
		} else {
			set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
			fs_info->block_group_root = root;
		}
	}

	location.objectid = BTRFS_DEV_TREE_OBJECTID;
	root = btrfs_read_tree_root(tree_root, &location);
	if (IS_ERR(root)) {
		if (!btrfs_test_opt(fs_info, IGNOREBADROOTS)) {
			ret = PTR_ERR(root);
			goto out;
		}
	} else {
		set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
		fs_info->dev_root = root;
	}
	/* Initialize fs_info for all devices in any case */
	ret = btrfs_init_devices_late(fs_info);
	if (ret)
		goto out;

	/*
	 * This tree can share blocks with some other fs tree during relocation
	 * and we need a proper setup by btrfs_get_fs_root
	 */
	root = btrfs_get_fs_root(tree_root->fs_info,
				 BTRFS_DATA_RELOC_TREE_OBJECTID, true);
	if (IS_ERR(root)) {
		if (!btrfs_test_opt(fs_info, IGNOREBADROOTS)) {
			ret = PTR_ERR(root);
			goto out;
		}
	} else {
		set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
		fs_info->data_reloc_root = root;
	}

	location.objectid = BTRFS_QUOTA_TREE_OBJECTID;
	root = btrfs_read_tree_root(tree_root, &location);
	if (!IS_ERR(root)) {
		set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
		fs_info->quota_root = root;
	}

	location.objectid = BTRFS_UUID_TREE_OBJECTID;
	root = btrfs_read_tree_root(tree_root, &location);
	if (IS_ERR(root)) {
		if (!btrfs_test_opt(fs_info, IGNOREBADROOTS)) {
			ret = PTR_ERR(root);
			if (ret != -ENOENT)
				goto out;
		}
	} else {
		set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
		fs_info->uuid_root = root;
	}

	if (btrfs_fs_incompat(fs_info, RAID_STRIPE_TREE)) {
		location.objectid = BTRFS_RAID_STRIPE_TREE_OBJECTID;
		root = btrfs_read_tree_root(tree_root, &location);
		if (IS_ERR(root)) {
			if (!btrfs_test_opt(fs_info, IGNOREBADROOTS)) {
				ret = PTR_ERR(root);
				goto out;
			}
		} else {
			set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);
			fs_info->stripe_root = root;
		}
	}

	return 0;
out:
	btrfs_warn(fs_info, "failed to read root (objectid=%llu): %d",
		   location.objectid, ret);
	return ret;
}

static int validate_sys_chunk_array(const struct btrfs_fs_info *fs_info,
				    const struct btrfs_super_block *sb)
{
	unsigned int cur = 0; /* Offset inside the sys chunk array */
	/*
	 * At sb read time, fs_info is not fully initialized. Thus we have
	 * to use super block sectorsize, which should have been validated.
	 */
	const u32 sectorsize = btrfs_super_sectorsize(sb);
	u32 sys_array_size = btrfs_super_sys_array_size(sb);

	if (sys_array_size > BTRFS_SYSTEM_CHUNK_ARRAY_SIZE) {
		btrfs_err(fs_info, "system chunk array too big %u > %u",
			  sys_array_size, BTRFS_SYSTEM_CHUNK_ARRAY_SIZE);
		return -EUCLEAN;
	}

	while (cur < sys_array_size) {
		struct btrfs_disk_key *disk_key;
		struct btrfs_chunk *chunk;
		struct btrfs_key key;
		u64 type;
		u16 num_stripes;
		u32 len;
		int ret;

		disk_key = (struct btrfs_disk_key *)(sb->sys_chunk_array + cur);
		len = sizeof(*disk_key);

		if (cur + len > sys_array_size)
			goto short_read;
		cur += len;

		btrfs_disk_key_to_cpu(&key, disk_key);
		if (key.type != BTRFS_CHUNK_ITEM_KEY) {
			btrfs_err(fs_info,
			    "unexpected item type %u in sys_array at offset %u",
				  key.type, cur);
			return -EUCLEAN;
		}
		chunk = (struct btrfs_chunk *)(sb->sys_chunk_array + cur);
		num_stripes = btrfs_stack_chunk_num_stripes(chunk);
		if (cur + btrfs_chunk_item_size(num_stripes) > sys_array_size)
			goto short_read;
		type = btrfs_stack_chunk_type(chunk);
		if (!(type & BTRFS_BLOCK_GROUP_SYSTEM)) {
			btrfs_err(fs_info,
			"invalid chunk type %llu in sys_array at offset %u",
				  type, cur);
			return -EUCLEAN;
		}
		ret = btrfs_check_chunk_valid(fs_info, NULL, chunk, key.offset,
					      sectorsize);
		if (ret < 0)
			return ret;
		cur += btrfs_chunk_item_size(num_stripes);
	}
	return 0;
short_read:
	btrfs_err(fs_info,
	"super block sys chunk array short read, cur=%u sys_array_size=%u",
		  cur, sys_array_size);
	return -EUCLEAN;
}

/*
 * Real super block validation
 * NOTE: super csum type and incompat features will not be checked here.
 *
 * @sb:		super block to check
 * @mirror_num:	the super block number to check its bytenr:
 * 		0	the primary (1st) sb
 * 		1, 2	2nd and 3rd backup copy
 * 	       -1	skip bytenr check
 */
int btrfs_validate_super(const struct btrfs_fs_info *fs_info,
			 const struct btrfs_super_block *sb, int mirror_num)
{
	u64 nodesize = btrfs_super_nodesize(sb);
	u64 sectorsize = btrfs_super_sectorsize(sb);
	int ret = 0;
	const bool ignore_flags = btrfs_test_opt(fs_info, IGNORESUPERFLAGS);

	if (btrfs_super_magic(sb) != BTRFS_MAGIC) {
		btrfs_err(fs_info, "no valid FS found");
		ret = -EINVAL;
	}
	if ((btrfs_super_flags(sb) & ~BTRFS_SUPER_FLAG_SUPP)) {
		if (!ignore_flags) {
			btrfs_err(fs_info,
			"unrecognized or unsupported super flag 0x%llx",
				  btrfs_super_flags(sb) & ~BTRFS_SUPER_FLAG_SUPP);
			ret = -EINVAL;
		} else {
			btrfs_info(fs_info,
			"unrecognized or unsupported super flags: 0x%llx, ignored",
				   btrfs_super_flags(sb) & ~BTRFS_SUPER_FLAG_SUPP);
		}
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

	/*
	 * We only support at most two sectorsizes: 4K and PAGE_SIZE.
	 *
	 * We can support 16K sectorsize with 64K page size without problem,
	 * but such sectorsize/pagesize combination doesn't make much sense.
	 * 4K will be our future standard, PAGE_SIZE is supported from the very
	 * beginning.
	 */
	if (sectorsize > PAGE_SIZE || (sectorsize != SZ_4K && sectorsize != PAGE_SIZE)) {
		btrfs_err(fs_info,
			"sectorsize %llu not yet supported for page size %lu",
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

	if (!fs_info->fs_devices->temp_fsid &&
	    memcmp(fs_info->fs_devices->fsid, sb->fsid, BTRFS_FSID_SIZE) != 0) {
		btrfs_err(fs_info,
		"superblock fsid doesn't match fsid of fs_devices: %pU != %pU",
			  sb->fsid, fs_info->fs_devices->fsid);
		ret = -EINVAL;
	}

	if (memcmp(fs_info->fs_devices->metadata_uuid, btrfs_sb_fsid_ptr(sb),
		   BTRFS_FSID_SIZE) != 0) {
		btrfs_err(fs_info,
"superblock metadata_uuid doesn't match metadata uuid of fs_devices: %pU != %pU",
			  btrfs_sb_fsid_ptr(sb), fs_info->fs_devices->metadata_uuid);
		ret = -EINVAL;
	}

	if (memcmp(fs_info->fs_devices->metadata_uuid, sb->dev_item.fsid,
		   BTRFS_FSID_SIZE) != 0) {
		btrfs_err(fs_info,
			"dev_item UUID does not match metadata fsid: %pU != %pU",
			fs_info->fs_devices->metadata_uuid, sb->dev_item.fsid);
		ret = -EINVAL;
	}

	/*
	 * Artificial requirement for block-group-tree to force newer features
	 * (free-space-tree, no-holes) so the test matrix is smaller.
	 */
	if (btrfs_fs_compat_ro(fs_info, BLOCK_GROUP_TREE) &&
	    (!btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE_VALID) ||
	     !btrfs_fs_incompat(fs_info, NO_HOLES))) {
		btrfs_err(fs_info,
		"block-group-tree feature requires free-space-tree and no-holes");
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

	if (mirror_num >= 0 &&
	    btrfs_super_bytenr(sb) != btrfs_sb_offset(mirror_num)) {
		btrfs_err(fs_info, "super offset mismatch %llu != %u",
			  btrfs_super_bytenr(sb), BTRFS_SUPER_INFO_OFFSET);
		ret = -EINVAL;
	}

	ret = validate_sys_chunk_array(fs_info, sb);

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

/*
 * Validation of super block at mount time.
 * Some checks already done early at mount time, like csum type and incompat
 * flags will be skipped.
 */
static int btrfs_validate_mount_super(struct btrfs_fs_info *fs_info)
{
	return btrfs_validate_super(fs_info, fs_info->super_copy, 0);
}

/*
 * Validation of super block at write time.
 * Some checks like bytenr check will be skipped as their values will be
 * overwritten soon.
 * Extra checks like csum type and incompat flags will be done here.
 */
static int btrfs_validate_write_super(struct btrfs_fs_info *fs_info,
				      struct btrfs_super_block *sb)
{
	int ret;

	ret = btrfs_validate_super(fs_info, sb, -1);
	if (ret < 0)
		goto out;
	if (!btrfs_supported_super_csum(btrfs_super_csum_type(sb))) {
		ret = -EUCLEAN;
		btrfs_err(fs_info, "invalid csum type, has %u want %u",
			  btrfs_super_csum_type(sb), BTRFS_CSUM_TYPE_CRC32);
		goto out;
	}
	if (btrfs_super_incompat_flags(sb) & ~BTRFS_FEATURE_INCOMPAT_SUPP) {
		ret = -EUCLEAN;
		btrfs_err(fs_info,
		"invalid incompat flags, has 0x%llx valid mask 0x%llx",
			  btrfs_super_incompat_flags(sb),
			  (unsigned long long)BTRFS_FEATURE_INCOMPAT_SUPP);
		goto out;
	}
out:
	if (ret < 0)
		btrfs_err(fs_info,
		"super block corruption detected before writing it to disk");
	return ret;
}

static int load_super_root(struct btrfs_root *root, u64 bytenr, u64 gen, int level)
{
	struct btrfs_tree_parent_check check = {
		.level = level,
		.transid = gen,
		.owner_root = btrfs_root_id(root)
	};
	int ret = 0;

	root->node = read_tree_block(root->fs_info, bytenr, &check);
	if (IS_ERR(root->node)) {
		ret = PTR_ERR(root->node);
		root->node = NULL;
		return ret;
	}
	if (!extent_buffer_uptodate(root->node)) {
		free_extent_buffer(root->node);
		root->node = NULL;
		return -EIO;
	}

	btrfs_set_root_node(&root->root_item, root->node);
	root->commit_root = btrfs_root_node(root);
	btrfs_set_root_refs(&root->root_item, 1);
	return ret;
}

static int load_important_roots(struct btrfs_fs_info *fs_info)
{
	struct btrfs_super_block *sb = fs_info->super_copy;
	u64 gen, bytenr;
	int level, ret;

	bytenr = btrfs_super_root(sb);
	gen = btrfs_super_generation(sb);
	level = btrfs_super_root_level(sb);
	ret = load_super_root(fs_info->tree_root, bytenr, gen, level);
	if (ret) {
		btrfs_warn(fs_info, "couldn't read tree root");
		return ret;
	}
	return 0;
}

static int __cold init_tree_roots(struct btrfs_fs_info *fs_info)
{
	int backup_index = find_newest_super_backup(fs_info);
	struct btrfs_super_block *sb = fs_info->super_copy;
	struct btrfs_root *tree_root = fs_info->tree_root;
	bool handle_error = false;
	int ret = 0;
	int i;

	for (i = 0; i < BTRFS_NUM_BACKUP_ROOTS; i++) {
		if (handle_error) {
			if (!IS_ERR(tree_root->node))
				free_extent_buffer(tree_root->node);
			tree_root->node = NULL;

			if (!btrfs_test_opt(fs_info, USEBACKUPROOT))
				break;

			free_root_pointers(fs_info, 0);

			/*
			 * Don't use the log in recovery mode, it won't be
			 * valid
			 */
			btrfs_set_super_log_root(sb, 0);

			btrfs_warn(fs_info, "try to load backup roots slot %d", i);
			ret = read_backup_root(fs_info, i);
			backup_index = ret;
			if (ret < 0)
				return ret;
		}

		ret = load_important_roots(fs_info);
		if (ret) {
			handle_error = true;
			continue;
		}

		/*
		 * No need to hold btrfs_root::objectid_mutex since the fs
		 * hasn't been fully initialised and we are the only user
		 */
		ret = btrfs_init_root_free_objectid(tree_root);
		if (ret < 0) {
			handle_error = true;
			continue;
		}

		ASSERT(tree_root->free_objectid <= BTRFS_LAST_FREE_OBJECTID);

		ret = btrfs_read_roots(fs_info);
		if (ret < 0) {
			handle_error = true;
			continue;
		}

		/* All successful */
		fs_info->generation = btrfs_header_generation(tree_root->node);
		btrfs_set_last_trans_committed(fs_info, fs_info->generation);
		fs_info->last_reloc_trans = 0;

		/* Always begin writing backup roots after the one being used */
		if (backup_index < 0) {
			fs_info->backup_root_index = 0;
		} else {
			fs_info->backup_root_index = backup_index + 1;
			fs_info->backup_root_index %= BTRFS_NUM_BACKUP_ROOTS;
		}
		break;
	}

	return ret;
}

void btrfs_init_fs_info(struct btrfs_fs_info *fs_info)
{
	INIT_RADIX_TREE(&fs_info->fs_roots_radix, GFP_ATOMIC);
	INIT_RADIX_TREE(&fs_info->buffer_radix, GFP_ATOMIC);
	INIT_LIST_HEAD(&fs_info->trans_list);
	INIT_LIST_HEAD(&fs_info->dead_roots);
	INIT_LIST_HEAD(&fs_info->delayed_iputs);
	INIT_LIST_HEAD(&fs_info->delalloc_roots);
	INIT_LIST_HEAD(&fs_info->caching_block_groups);
	spin_lock_init(&fs_info->delalloc_root_lock);
	spin_lock_init(&fs_info->trans_lock);
	spin_lock_init(&fs_info->fs_roots_radix_lock);
	spin_lock_init(&fs_info->delayed_iput_lock);
	spin_lock_init(&fs_info->defrag_inodes_lock);
	spin_lock_init(&fs_info->super_lock);
	spin_lock_init(&fs_info->buffer_lock);
	spin_lock_init(&fs_info->unused_bgs_lock);
	spin_lock_init(&fs_info->treelog_bg_lock);
	spin_lock_init(&fs_info->zone_active_bgs_lock);
	spin_lock_init(&fs_info->relocation_bg_lock);
	rwlock_init(&fs_info->tree_mod_log_lock);
	rwlock_init(&fs_info->global_root_lock);
	mutex_init(&fs_info->unused_bg_unpin_mutex);
	mutex_init(&fs_info->reclaim_bgs_lock);
	mutex_init(&fs_info->reloc_mutex);
	mutex_init(&fs_info->delalloc_root_mutex);
	mutex_init(&fs_info->zoned_meta_io_lock);
	mutex_init(&fs_info->zoned_data_reloc_io_lock);
	seqlock_init(&fs_info->profiles_lock);

	btrfs_lockdep_init_map(fs_info, btrfs_trans_num_writers);
	btrfs_lockdep_init_map(fs_info, btrfs_trans_num_extwriters);
	btrfs_lockdep_init_map(fs_info, btrfs_trans_pending_ordered);
	btrfs_lockdep_init_map(fs_info, btrfs_ordered_extent);
	btrfs_state_lockdep_init_map(fs_info, btrfs_trans_commit_prep,
				     BTRFS_LOCKDEP_TRANS_COMMIT_PREP);
	btrfs_state_lockdep_init_map(fs_info, btrfs_trans_unblocked,
				     BTRFS_LOCKDEP_TRANS_UNBLOCKED);
	btrfs_state_lockdep_init_map(fs_info, btrfs_trans_super_committed,
				     BTRFS_LOCKDEP_TRANS_SUPER_COMMITTED);
	btrfs_state_lockdep_init_map(fs_info, btrfs_trans_completed,
				     BTRFS_LOCKDEP_TRANS_COMPLETED);

	INIT_LIST_HEAD(&fs_info->dirty_cowonly_roots);
	INIT_LIST_HEAD(&fs_info->space_info);
	INIT_LIST_HEAD(&fs_info->tree_mod_seq_list);
	INIT_LIST_HEAD(&fs_info->unused_bgs);
	INIT_LIST_HEAD(&fs_info->reclaim_bgs);
	INIT_LIST_HEAD(&fs_info->zone_active_bgs);
#ifdef CONFIG_BTRFS_DEBUG
	INIT_LIST_HEAD(&fs_info->allocated_roots);
	INIT_LIST_HEAD(&fs_info->allocated_ebs);
	spin_lock_init(&fs_info->eb_leak_lock);
#endif
	fs_info->mapping_tree = RB_ROOT_CACHED;
	rwlock_init(&fs_info->mapping_tree_lock);
	btrfs_init_block_rsv(&fs_info->global_block_rsv,
			     BTRFS_BLOCK_RSV_GLOBAL);
	btrfs_init_block_rsv(&fs_info->trans_block_rsv, BTRFS_BLOCK_RSV_TRANS);
	btrfs_init_block_rsv(&fs_info->chunk_block_rsv, BTRFS_BLOCK_RSV_CHUNK);
	btrfs_init_block_rsv(&fs_info->empty_block_rsv, BTRFS_BLOCK_RSV_EMPTY);
	btrfs_init_block_rsv(&fs_info->delayed_block_rsv,
			     BTRFS_BLOCK_RSV_DELOPS);
	btrfs_init_block_rsv(&fs_info->delayed_refs_rsv,
			     BTRFS_BLOCK_RSV_DELREFS);

	atomic_set(&fs_info->async_delalloc_pages, 0);
	atomic_set(&fs_info->defrag_running, 0);
	atomic_set(&fs_info->nr_delayed_iputs, 0);
	atomic64_set(&fs_info->tree_mod_seq, 0);
	fs_info->global_root_tree = RB_ROOT;
	fs_info->max_inline = BTRFS_DEFAULT_MAX_INLINE;
	fs_info->metadata_ratio = 0;
	fs_info->defrag_inodes = RB_ROOT;
	atomic64_set(&fs_info->free_chunk_space, 0);
	fs_info->tree_mod_log = RB_ROOT;
	fs_info->commit_interval = BTRFS_DEFAULT_COMMIT_INTERVAL;
	btrfs_init_ref_verify(fs_info);

	fs_info->thread_pool_size = min_t(unsigned long,
					  num_online_cpus() + 2, 8);

	INIT_LIST_HEAD(&fs_info->ordered_roots);
	spin_lock_init(&fs_info->ordered_root_lock);

	btrfs_init_scrub(fs_info);
	btrfs_init_balance(fs_info);
	btrfs_init_async_reclaim_work(fs_info);
	btrfs_init_extent_map_shrinker_work(fs_info);

	rwlock_init(&fs_info->block_group_cache_lock);
	fs_info->block_group_cache_tree = RB_ROOT_CACHED;

	extent_io_tree_init(fs_info, &fs_info->excluded_extents,
			    IO_TREE_FS_EXCLUDED_EXTENTS);

	mutex_init(&fs_info->ordered_operations_mutex);
	mutex_init(&fs_info->tree_log_mutex);
	mutex_init(&fs_info->chunk_mutex);
	mutex_init(&fs_info->transaction_kthread_mutex);
	mutex_init(&fs_info->cleaner_mutex);
	mutex_init(&fs_info->ro_block_group_mutex);
	init_rwsem(&fs_info->commit_root_sem);
	init_rwsem(&fs_info->cleanup_work_sem);
	init_rwsem(&fs_info->subvol_sem);
	sema_init(&fs_info->uuid_tree_rescan_sem, 1);

	btrfs_init_dev_replace_locks(fs_info);
	btrfs_init_qgroup(fs_info);
	btrfs_discard_init(fs_info);

	btrfs_init_free_cluster(&fs_info->meta_alloc_cluster);
	btrfs_init_free_cluster(&fs_info->data_alloc_cluster);

	init_waitqueue_head(&fs_info->transaction_throttle);
	init_waitqueue_head(&fs_info->transaction_wait);
	init_waitqueue_head(&fs_info->transaction_blocked_wait);
	init_waitqueue_head(&fs_info->async_submit_wait);
	init_waitqueue_head(&fs_info->delayed_iputs_wait);

	/* Usable values until the real ones are cached from the superblock */
	fs_info->nodesize = 4096;
	fs_info->sectorsize = 4096;
	fs_info->sectorsize_bits = ilog2(4096);
	fs_info->stripesize = 4096;

	/* Default compress algorithm when user does -o compress */
	fs_info->compress_type = BTRFS_COMPRESS_ZLIB;

	fs_info->max_extent_size = BTRFS_MAX_EXTENT_SIZE;

	spin_lock_init(&fs_info->swapfile_pins_lock);
	fs_info->swapfile_pins = RB_ROOT;

	fs_info->bg_reclaim_threshold = BTRFS_DEFAULT_RECLAIM_THRESH;
	INIT_WORK(&fs_info->reclaim_bgs_work, btrfs_reclaim_bgs_work);
}

static int init_mount_fs_info(struct btrfs_fs_info *fs_info, struct super_block *sb)
{
	int ret;

	fs_info->sb = sb;
	/* Temporary fixed values for block size until we read the superblock. */
	sb->s_blocksize = BTRFS_BDEV_BLOCKSIZE;
	sb->s_blocksize_bits = blksize_bits(BTRFS_BDEV_BLOCKSIZE);

	ret = percpu_counter_init(&fs_info->ordered_bytes, 0, GFP_KERNEL);
	if (ret)
		return ret;

	ret = percpu_counter_init(&fs_info->evictable_extent_maps, 0, GFP_KERNEL);
	if (ret)
		return ret;

	ret = percpu_counter_init(&fs_info->dirty_metadata_bytes, 0, GFP_KERNEL);
	if (ret)
		return ret;

	ret = percpu_counter_init(&fs_info->stats_read_blocks, 0, GFP_KERNEL);
	if (ret)
		return ret;

	fs_info->dirty_metadata_batch = PAGE_SIZE *
					(1 + ilog2(nr_cpu_ids));

	ret = percpu_counter_init(&fs_info->delalloc_bytes, 0, GFP_KERNEL);
	if (ret)
		return ret;

	ret = percpu_counter_init(&fs_info->dev_replace.bio_counter, 0,
			GFP_KERNEL);
	if (ret)
		return ret;

	fs_info->delayed_root = kmalloc(sizeof(struct btrfs_delayed_root),
					GFP_KERNEL);
	if (!fs_info->delayed_root)
		return -ENOMEM;
	btrfs_init_delayed_root(fs_info->delayed_root);

	if (sb_rdonly(sb))
		set_bit(BTRFS_FS_STATE_RO, &fs_info->fs_state);
	if (btrfs_test_opt(fs_info, IGNOREMETACSUMS))
		set_bit(BTRFS_FS_STATE_SKIP_META_CSUMS, &fs_info->fs_state);

	return btrfs_alloc_stripe_hash_table(fs_info);
}

static int btrfs_uuid_rescan_kthread(void *data)
{
	struct btrfs_fs_info *fs_info = data;
	int ret;

	/*
	 * 1st step is to iterate through the existing UUID tree and
	 * to delete all entries that contain outdated data.
	 * 2nd step is to add all missing entries to the UUID tree.
	 */
	ret = btrfs_uuid_tree_iterate(fs_info);
	if (ret < 0) {
		if (ret != -EINTR)
			btrfs_warn(fs_info, "iterating uuid_tree failed %d",
				   ret);
		up(&fs_info->uuid_tree_rescan_sem);
		return ret;
	}
	return btrfs_uuid_scan_kthread(data);
}

static int btrfs_check_uuid_tree(struct btrfs_fs_info *fs_info)
{
	struct task_struct *task;

	down(&fs_info->uuid_tree_rescan_sem);
	task = kthread_run(btrfs_uuid_rescan_kthread, fs_info, "btrfs-uuid");
	if (IS_ERR(task)) {
		/* fs_info->update_uuid_tree_gen remains 0 in all error case */
		btrfs_warn(fs_info, "failed to start uuid_rescan task");
		up(&fs_info->uuid_tree_rescan_sem);
		return PTR_ERR(task);
	}

	return 0;
}

static int btrfs_cleanup_fs_roots(struct btrfs_fs_info *fs_info)
{
	u64 root_objectid = 0;
	struct btrfs_root *gang[8];
	int ret = 0;

	while (1) {
		unsigned int found;

		spin_lock(&fs_info->fs_roots_radix_lock);
		found = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, root_objectid,
					     ARRAY_SIZE(gang));
		if (!found) {
			spin_unlock(&fs_info->fs_roots_radix_lock);
			break;
		}
		root_objectid = btrfs_root_id(gang[found - 1]) + 1;

		for (int i = 0; i < found; i++) {
			/* Avoid to grab roots in dead_roots. */
			if (btrfs_root_refs(&gang[i]->root_item) == 0) {
				gang[i] = NULL;
				continue;
			}
			/* Grab all the search result for later use. */
			gang[i] = btrfs_grab_root(gang[i]);
		}
		spin_unlock(&fs_info->fs_roots_radix_lock);

		for (int i = 0; i < found; i++) {
			if (!gang[i])
				continue;
			root_objectid = btrfs_root_id(gang[i]);
			/*
			 * Continue to release the remaining roots after the first
			 * error without cleanup and preserve the first error
			 * for the return.
			 */
			if (!ret)
				ret = btrfs_orphan_cleanup(gang[i]);
			btrfs_put_root(gang[i]);
		}
		if (ret)
			break;

		root_objectid++;
	}
	return ret;
}

/*
 * Mounting logic specific to read-write file systems. Shared by open_ctree
 * and btrfs_remount when remounting from read-only to read-write.
 */
int btrfs_start_pre_rw_mount(struct btrfs_fs_info *fs_info)
{
	int ret;
	const bool cache_opt = btrfs_test_opt(fs_info, SPACE_CACHE);
	bool rebuild_free_space_tree = false;

	if (btrfs_test_opt(fs_info, CLEAR_CACHE) &&
	    btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE)) {
		if (btrfs_fs_incompat(fs_info, EXTENT_TREE_V2))
			btrfs_warn(fs_info,
				   "'clear_cache' option is ignored with extent tree v2");
		else
			rebuild_free_space_tree = true;
	} else if (btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE) &&
		   !btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE_VALID)) {
		btrfs_warn(fs_info, "free space tree is invalid");
		rebuild_free_space_tree = true;
	}

	if (rebuild_free_space_tree) {
		btrfs_info(fs_info, "rebuilding free space tree");
		ret = btrfs_rebuild_free_space_tree(fs_info);
		if (ret) {
			btrfs_warn(fs_info,
				   "failed to rebuild free space tree: %d", ret);
			goto out;
		}
	}

	if (btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE) &&
	    !btrfs_test_opt(fs_info, FREE_SPACE_TREE)) {
		btrfs_info(fs_info, "disabling free space tree");
		ret = btrfs_delete_free_space_tree(fs_info);
		if (ret) {
			btrfs_warn(fs_info,
				   "failed to disable free space tree: %d", ret);
			goto out;
		}
	}

	/*
	 * btrfs_find_orphan_roots() is responsible for finding all the dead
	 * roots (with 0 refs), flag them with BTRFS_ROOT_DEAD_TREE and load
	 * them into the fs_info->fs_roots_radix tree. This must be done before
	 * calling btrfs_orphan_cleanup() on the tree root. If we don't do it
	 * first, then btrfs_orphan_cleanup() will delete a dead root's orphan
	 * item before the root's tree is deleted - this means that if we unmount
	 * or crash before the deletion completes, on the next mount we will not
	 * delete what remains of the tree because the orphan item does not
	 * exists anymore, which is what tells us we have a pending deletion.
	 */
	ret = btrfs_find_orphan_roots(fs_info);
	if (ret)
		goto out;

	ret = btrfs_cleanup_fs_roots(fs_info);
	if (ret)
		goto out;

	down_read(&fs_info->cleanup_work_sem);
	if ((ret = btrfs_orphan_cleanup(fs_info->fs_root)) ||
	    (ret = btrfs_orphan_cleanup(fs_info->tree_root))) {
		up_read(&fs_info->cleanup_work_sem);
		goto out;
	}
	up_read(&fs_info->cleanup_work_sem);

	mutex_lock(&fs_info->cleaner_mutex);
	ret = btrfs_recover_relocation(fs_info);
	mutex_unlock(&fs_info->cleaner_mutex);
	if (ret < 0) {
		btrfs_warn(fs_info, "failed to recover relocation: %d", ret);
		goto out;
	}

	if (btrfs_test_opt(fs_info, FREE_SPACE_TREE) &&
	    !btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE)) {
		btrfs_info(fs_info, "creating free space tree");
		ret = btrfs_create_free_space_tree(fs_info);
		if (ret) {
			btrfs_warn(fs_info,
				"failed to create free space tree: %d", ret);
			goto out;
		}
	}

	if (cache_opt != btrfs_free_space_cache_v1_active(fs_info)) {
		ret = btrfs_set_free_space_cache_v1_active(fs_info, cache_opt);
		if (ret)
			goto out;
	}

	ret = btrfs_resume_balance_async(fs_info);
	if (ret)
		goto out;

	ret = btrfs_resume_dev_replace_async(fs_info);
	if (ret) {
		btrfs_warn(fs_info, "failed to resume dev_replace");
		goto out;
	}

	btrfs_qgroup_rescan_resume(fs_info);

	if (!fs_info->uuid_root) {
		btrfs_info(fs_info, "creating UUID tree");
		ret = btrfs_create_uuid_tree(fs_info);
		if (ret) {
			btrfs_warn(fs_info,
				   "failed to create the UUID tree %d", ret);
			goto out;
		}
	}

out:
	return ret;
}

/*
 * Do various sanity and dependency checks of different features.
 *
 * @is_rw_mount:	If the mount is read-write.
 *
 * This is the place for less strict checks (like for subpage or artificial
 * feature dependencies).
 *
 * For strict checks or possible corruption detection, see
 * btrfs_validate_super().
 *
 * This should be called after btrfs_parse_options(), as some mount options
 * (space cache related) can modify on-disk format like free space tree and
 * screw up certain feature dependencies.
 */
int btrfs_check_features(struct btrfs_fs_info *fs_info, bool is_rw_mount)
{
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	u64 incompat = btrfs_super_incompat_flags(disk_super);
	const u64 compat_ro = btrfs_super_compat_ro_flags(disk_super);
	const u64 compat_ro_unsupp = (compat_ro & ~BTRFS_FEATURE_COMPAT_RO_SUPP);

	if (incompat & ~BTRFS_FEATURE_INCOMPAT_SUPP) {
		btrfs_err(fs_info,
		"cannot mount because of unknown incompat features (0x%llx)",
		    incompat);
		return -EINVAL;
	}

	/* Runtime limitation for mixed block groups. */
	if ((incompat & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS) &&
	    (fs_info->sectorsize != fs_info->nodesize)) {
		btrfs_err(fs_info,
"unequal nodesize/sectorsize (%u != %u) are not allowed for mixed block groups",
			fs_info->nodesize, fs_info->sectorsize);
		return -EINVAL;
	}

	/* Mixed backref is an always-enabled feature. */
	incompat |= BTRFS_FEATURE_INCOMPAT_MIXED_BACKREF;

	/* Set compression related flags just in case. */
	if (fs_info->compress_type == BTRFS_COMPRESS_LZO)
		incompat |= BTRFS_FEATURE_INCOMPAT_COMPRESS_LZO;
	else if (fs_info->compress_type == BTRFS_COMPRESS_ZSTD)
		incompat |= BTRFS_FEATURE_INCOMPAT_COMPRESS_ZSTD;

	/*
	 * An ancient flag, which should really be marked deprecated.
	 * Such runtime limitation doesn't really need a incompat flag.
	 */
	if (btrfs_super_nodesize(disk_super) > PAGE_SIZE)
		incompat |= BTRFS_FEATURE_INCOMPAT_BIG_METADATA;

	if (compat_ro_unsupp && is_rw_mount) {
		btrfs_err(fs_info,
	"cannot mount read-write because of unknown compat_ro features (0x%llx)",
		       compat_ro);
		return -EINVAL;
	}

	/*
	 * We have unsupported RO compat features, although RO mounted, we
	 * should not cause any metadata writes, including log replay.
	 * Or we could screw up whatever the new feature requires.
	 */
	if (compat_ro_unsupp && btrfs_super_log_root(disk_super) &&
	    !btrfs_test_opt(fs_info, NOLOGREPLAY)) {
		btrfs_err(fs_info,
"cannot replay dirty log with unsupported compat_ro features (0x%llx), try rescue=nologreplay",
			  compat_ro);
		return -EINVAL;
	}

	/*
	 * Artificial limitations for block group tree, to force
	 * block-group-tree to rely on no-holes and free-space-tree.
	 */
	if (btrfs_fs_compat_ro(fs_info, BLOCK_GROUP_TREE) &&
	    (!btrfs_fs_incompat(fs_info, NO_HOLES) ||
	     !btrfs_test_opt(fs_info, FREE_SPACE_TREE))) {
		btrfs_err(fs_info,
"block-group-tree feature requires no-holes and free-space-tree features");
		return -EINVAL;
	}

	/*
	 * Subpage runtime limitation on v1 cache.
	 *
	 * V1 space cache still has some hard codeed PAGE_SIZE usage, while
	 * we're already defaulting to v2 cache, no need to bother v1 as it's
	 * going to be deprecated anyway.
	 */
	if (fs_info->sectorsize < PAGE_SIZE && btrfs_test_opt(fs_info, SPACE_CACHE)) {
		btrfs_warn(fs_info,
	"v1 space cache is not supported for page size %lu with sectorsize %u",
			   PAGE_SIZE, fs_info->sectorsize);
		return -EINVAL;
	}

	/* This can be called by remount, we need to protect the super block. */
	spin_lock(&fs_info->super_lock);
	btrfs_set_super_incompat_flags(disk_super, incompat);
	spin_unlock(&fs_info->super_lock);

	return 0;
}

int __cold open_ctree(struct super_block *sb, struct btrfs_fs_devices *fs_devices)
{
	u32 sectorsize;
	u32 nodesize;
	u32 stripesize;
	u64 generation;
	u16 csum_type;
	struct btrfs_super_block *disk_super;
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_root *tree_root;
	struct btrfs_root *chunk_root;
	int ret;
	int level;

	ret = init_mount_fs_info(fs_info, sb);
	if (ret)
		goto fail;

	/* These need to be init'ed before we start creating inodes and such. */
	tree_root = btrfs_alloc_root(fs_info, BTRFS_ROOT_TREE_OBJECTID,
				     GFP_KERNEL);
	fs_info->tree_root = tree_root;
	chunk_root = btrfs_alloc_root(fs_info, BTRFS_CHUNK_TREE_OBJECTID,
				      GFP_KERNEL);
	fs_info->chunk_root = chunk_root;
	if (!tree_root || !chunk_root) {
		ret = -ENOMEM;
		goto fail;
	}

	ret = btrfs_init_btree_inode(sb);
	if (ret)
		goto fail;

	invalidate_bdev(fs_devices->latest_dev->bdev);

	/*
	 * Read super block and check the signature bytes only
	 */
	disk_super = btrfs_read_dev_super(fs_devices->latest_dev->bdev);
	if (IS_ERR(disk_super)) {
		ret = PTR_ERR(disk_super);
		goto fail_alloc;
	}

	btrfs_info(fs_info, "first mount of filesystem %pU", disk_super->fsid);
	/*
	 * Verify the type first, if that or the checksum value are
	 * corrupted, we'll find out
	 */
	csum_type = btrfs_super_csum_type(disk_super);
	if (!btrfs_supported_super_csum(csum_type)) {
		btrfs_err(fs_info, "unsupported checksum algorithm: %u",
			  csum_type);
		ret = -EINVAL;
		btrfs_release_disk_super(disk_super);
		goto fail_alloc;
	}

	fs_info->csum_size = btrfs_super_csum_size(disk_super);

	ret = btrfs_init_csum_hash(fs_info, csum_type);
	if (ret) {
		btrfs_release_disk_super(disk_super);
		goto fail_alloc;
	}

	/*
	 * We want to check superblock checksum, the type is stored inside.
	 * Pass the whole disk block of size BTRFS_SUPER_INFO_SIZE (4k).
	 */
	if (btrfs_check_super_csum(fs_info, disk_super)) {
		btrfs_err(fs_info, "superblock checksum mismatch");
		ret = -EINVAL;
		btrfs_release_disk_super(disk_super);
		goto fail_alloc;
	}

	/*
	 * super_copy is zeroed at allocation time and we never touch the
	 * following bytes up to INFO_SIZE, the checksum is calculated from
	 * the whole block of INFO_SIZE
	 */
	memcpy(fs_info->super_copy, disk_super, sizeof(*fs_info->super_copy));
	btrfs_release_disk_super(disk_super);

	disk_super = fs_info->super_copy;

	memcpy(fs_info->super_for_commit, fs_info->super_copy,
	       sizeof(*fs_info->super_for_commit));

	ret = btrfs_validate_mount_super(fs_info);
	if (ret) {
		btrfs_err(fs_info, "superblock contains fatal errors");
		ret = -EINVAL;
		goto fail_alloc;
	}

	if (!btrfs_super_root(disk_super)) {
		btrfs_err(fs_info, "invalid superblock tree root bytenr");
		ret = -EINVAL;
		goto fail_alloc;
	}

	/* check FS state, whether FS is broken. */
	if (btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_ERROR)
		WRITE_ONCE(fs_info->fs_error, -EUCLEAN);

	/* Set up fs_info before parsing mount options */
	nodesize = btrfs_super_nodesize(disk_super);
	sectorsize = btrfs_super_sectorsize(disk_super);
	stripesize = sectorsize;
	fs_info->dirty_metadata_batch = nodesize * (1 + ilog2(nr_cpu_ids));
	fs_info->delalloc_batch = sectorsize * 512 * (1 + ilog2(nr_cpu_ids));

	fs_info->nodesize = nodesize;
	fs_info->sectorsize = sectorsize;
	fs_info->sectorsize_bits = ilog2(sectorsize);
	fs_info->sectors_per_page = (PAGE_SIZE >> fs_info->sectorsize_bits);
	fs_info->csums_per_leaf = BTRFS_MAX_ITEM_SIZE(fs_info) / fs_info->csum_size;
	fs_info->stripesize = stripesize;
	fs_info->fs_devices->fs_info = fs_info;

	/*
	 * Handle the space caching options appropriately now that we have the
	 * super block loaded and validated.
	 */
	btrfs_set_free_space_cache_settings(fs_info);

	if (!btrfs_check_options(fs_info, &fs_info->mount_opt, sb->s_flags)) {
		ret = -EINVAL;
		goto fail_alloc;
	}

	ret = btrfs_check_features(fs_info, !sb_rdonly(sb));
	if (ret < 0)
		goto fail_alloc;

	/*
	 * At this point our mount options are validated, if we set ->max_inline
	 * to something non-standard make sure we truncate it to sectorsize.
	 */
	fs_info->max_inline = min_t(u64, fs_info->max_inline, fs_info->sectorsize);

	if (sectorsize < PAGE_SIZE)
		btrfs_warn(fs_info,
		"read-write for sector size %u with page size %lu is experimental",
			   sectorsize, PAGE_SIZE);

	ret = btrfs_init_workqueues(fs_info);
	if (ret)
		goto fail_sb_buffer;

	sb->s_bdi->ra_pages *= btrfs_super_num_devices(disk_super);
	sb->s_bdi->ra_pages = max(sb->s_bdi->ra_pages, SZ_4M / PAGE_SIZE);

	/* Update the values for the current filesystem. */
	sb->s_blocksize = sectorsize;
	sb->s_blocksize_bits = blksize_bits(sectorsize);
	memcpy(&sb->s_uuid, fs_info->fs_devices->fsid, BTRFS_FSID_SIZE);

	mutex_lock(&fs_info->chunk_mutex);
	ret = btrfs_read_sys_array(fs_info);
	mutex_unlock(&fs_info->chunk_mutex);
	if (ret) {
		btrfs_err(fs_info, "failed to read the system array: %d", ret);
		goto fail_sb_buffer;
	}

	generation = btrfs_super_chunk_root_generation(disk_super);
	level = btrfs_super_chunk_root_level(disk_super);
	ret = load_super_root(chunk_root, btrfs_super_chunk_root(disk_super),
			      generation, level);
	if (ret) {
		btrfs_err(fs_info, "failed to read chunk root");
		goto fail_tree_roots;
	}

	read_extent_buffer(chunk_root->node, fs_info->chunk_tree_uuid,
			   offsetof(struct btrfs_header, chunk_tree_uuid),
			   BTRFS_UUID_SIZE);

	ret = btrfs_read_chunk_tree(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to read chunk tree: %d", ret);
		goto fail_tree_roots;
	}

	/*
	 * At this point we know all the devices that make this filesystem,
	 * including the seed devices but we don't know yet if the replace
	 * target is required. So free devices that are not part of this
	 * filesystem but skip the replace target device which is checked
	 * below in btrfs_init_dev_replace().
	 */
	btrfs_free_extra_devids(fs_devices);
	if (!fs_devices->latest_dev->bdev) {
		btrfs_err(fs_info, "failed to read devices");
		ret = -EIO;
		goto fail_tree_roots;
	}

	ret = init_tree_roots(fs_info);
	if (ret)
		goto fail_tree_roots;

	/*
	 * Get zone type information of zoned block devices. This will also
	 * handle emulation of a zoned filesystem if a regular device has the
	 * zoned incompat feature flag set.
	 */
	ret = btrfs_get_dev_zone_info_all_devices(fs_info);
	if (ret) {
		btrfs_err(fs_info,
			  "zoned: failed to read device zone info: %d", ret);
		goto fail_block_groups;
	}

	/*
	 * If we have a uuid root and we're not being told to rescan we need to
	 * check the generation here so we can set the
	 * BTRFS_FS_UPDATE_UUID_TREE_GEN bit.  Otherwise we could commit the
	 * transaction during a balance or the log replay without updating the
	 * uuid generation, and then if we crash we would rescan the uuid tree,
	 * even though it was perfectly fine.
	 */
	if (fs_info->uuid_root && !btrfs_test_opt(fs_info, RESCAN_UUID_TREE) &&
	    fs_info->generation == btrfs_super_uuid_tree_generation(disk_super))
		set_bit(BTRFS_FS_UPDATE_UUID_TREE_GEN, &fs_info->flags);

	ret = btrfs_verify_dev_extents(fs_info);
	if (ret) {
		btrfs_err(fs_info,
			  "failed to verify dev extents against chunks: %d",
			  ret);
		goto fail_block_groups;
	}
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

	ret = btrfs_check_zoned_mode(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to initialize zoned mode: %d",
			  ret);
		goto fail_block_groups;
	}

	ret = btrfs_sysfs_add_fsid(fs_devices);
	if (ret) {
		btrfs_err(fs_info, "failed to init sysfs fsid interface: %d",
				ret);
		goto fail_block_groups;
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

	btrfs_free_zone_cache(fs_info);

	btrfs_check_active_zone_reservation(fs_info);

	if (!sb_rdonly(sb) && fs_info->fs_devices->missing_devices &&
	    !btrfs_check_rw_degradable(fs_info, NULL)) {
		btrfs_warn(fs_info,
		"writable mount is not allowed due to too many missing devices");
		ret = -EINVAL;
		goto fail_sysfs;
	}

	fs_info->cleaner_kthread = kthread_run(cleaner_kthread, fs_info,
					       "btrfs-cleaner");
	if (IS_ERR(fs_info->cleaner_kthread)) {
		ret = PTR_ERR(fs_info->cleaner_kthread);
		goto fail_sysfs;
	}

	fs_info->transaction_kthread = kthread_run(transaction_kthread,
						   tree_root,
						   "btrfs-transaction");
	if (IS_ERR(fs_info->transaction_kthread)) {
		ret = PTR_ERR(fs_info->transaction_kthread);
		goto fail_cleaner;
	}

	ret = btrfs_read_qgroup_config(fs_info);
	if (ret)
		goto fail_trans_kthread;

	if (btrfs_build_ref_tree(fs_info))
		btrfs_err(fs_info, "couldn't build ref tree");

	/* do not make disk changes in broken FS or nologreplay is given */
	if (btrfs_super_log_root(disk_super) != 0 &&
	    !btrfs_test_opt(fs_info, NOLOGREPLAY)) {
		btrfs_info(fs_info, "start tree-log replay");
		ret = btrfs_replay_log(fs_info, fs_devices);
		if (ret)
			goto fail_qgroup;
	}

	fs_info->fs_root = btrfs_get_fs_root(fs_info, BTRFS_FS_TREE_OBJECTID, true);
	if (IS_ERR(fs_info->fs_root)) {
		ret = PTR_ERR(fs_info->fs_root);
		btrfs_warn(fs_info, "failed to read fs tree: %d", ret);
		fs_info->fs_root = NULL;
		goto fail_qgroup;
	}

	if (sb_rdonly(sb))
		return 0;

	ret = btrfs_start_pre_rw_mount(fs_info);
	if (ret) {
		close_ctree(fs_info);
		return ret;
	}
	btrfs_discard_resume(fs_info);

	if (fs_info->uuid_root &&
	    (btrfs_test_opt(fs_info, RESCAN_UUID_TREE) ||
	     fs_info->generation != btrfs_super_uuid_tree_generation(disk_super))) {
		btrfs_info(fs_info, "checking UUID tree");
		ret = btrfs_check_uuid_tree(fs_info);
		if (ret) {
			btrfs_warn(fs_info,
				"failed to check the UUID tree: %d", ret);
			close_ctree(fs_info);
			return ret;
		}
	}

	set_bit(BTRFS_FS_OPEN, &fs_info->flags);

	/* Kick the cleaner thread so it'll start deleting snapshots. */
	if (test_bit(BTRFS_FS_UNFINISHED_DROPS, &fs_info->flags))
		wake_up_process(fs_info->cleaner_kthread);

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
	if (fs_info->data_reloc_root)
		btrfs_drop_and_free_fs_root(fs_info, fs_info->data_reloc_root);
	free_root_pointers(fs_info, true);
	invalidate_inode_pages2(fs_info->btree_inode->i_mapping);

fail_sb_buffer:
	btrfs_stop_all_workers(fs_info);
	btrfs_free_block_groups(fs_info);
fail_alloc:
	btrfs_mapping_tree_free(fs_info);

	iput(fs_info->btree_inode);
fail:
	btrfs_close_devices(fs_info->fs_devices);
	ASSERT(ret < 0);
	return ret;
}
ALLOW_ERROR_INJECTION(open_ctree, ERRNO);

static void btrfs_end_super_write(struct bio *bio)
{
	struct btrfs_device *device = bio->bi_private;
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio) {
		if (bio->bi_status) {
			btrfs_warn_rl_in_rcu(device->fs_info,
				"lost super block write due to IO error on %s (%d)",
				btrfs_dev_name(device),
				blk_status_to_errno(bio->bi_status));
			btrfs_dev_stat_inc_and_print(device,
						     BTRFS_DEV_STAT_WRITE_ERRS);
			/* Ensure failure if the primary sb fails. */
			if (bio->bi_opf & REQ_FUA)
				atomic_add(BTRFS_SUPER_PRIMARY_WRITE_ERROR,
					   &device->sb_write_errors);
			else
				atomic_inc(&device->sb_write_errors);
		}
		folio_unlock(fi.folio);
		folio_put(fi.folio);
	}

	bio_put(bio);
}

struct btrfs_super_block *btrfs_read_dev_one_super(struct block_device *bdev,
						   int copy_num, bool drop_cache)
{
	struct btrfs_super_block *super;
	struct page *page;
	u64 bytenr, bytenr_orig;
	struct address_space *mapping = bdev->bd_mapping;
	int ret;

	bytenr_orig = btrfs_sb_offset(copy_num);
	ret = btrfs_sb_log_location_bdev(bdev, copy_num, READ, &bytenr);
	if (ret == -ENOENT)
		return ERR_PTR(-EINVAL);
	else if (ret)
		return ERR_PTR(ret);

	if (bytenr + BTRFS_SUPER_INFO_SIZE >= bdev_nr_bytes(bdev))
		return ERR_PTR(-EINVAL);

	if (drop_cache) {
		/* This should only be called with the primary sb. */
		ASSERT(copy_num == 0);

		/*
		 * Drop the page of the primary superblock, so later read will
		 * always read from the device.
		 */
		invalidate_inode_pages2_range(mapping,
				bytenr >> PAGE_SHIFT,
				(bytenr + BTRFS_SUPER_INFO_SIZE) >> PAGE_SHIFT);
	}

	page = read_cache_page_gfp(mapping, bytenr >> PAGE_SHIFT, GFP_NOFS);
	if (IS_ERR(page))
		return ERR_CAST(page);

	super = page_address(page);
	if (btrfs_super_magic(super) != BTRFS_MAGIC) {
		btrfs_release_disk_super(super);
		return ERR_PTR(-ENODATA);
	}

	if (btrfs_super_bytenr(super) != bytenr_orig) {
		btrfs_release_disk_super(super);
		return ERR_PTR(-EINVAL);
	}

	return super;
}


struct btrfs_super_block *btrfs_read_dev_super(struct block_device *bdev)
{
	struct btrfs_super_block *super, *latest = NULL;
	int i;
	u64 transid = 0;

	/* we would like to check all the supers, but that would make
	 * a btrfs mount succeed after a mkfs from a different FS.
	 * So, we need to add a special mount option to scan for
	 * later supers, using BTRFS_SUPER_MIRROR_MAX instead
	 */
	for (i = 0; i < 1; i++) {
		super = btrfs_read_dev_one_super(bdev, i, false);
		if (IS_ERR(super))
			continue;

		if (!latest || btrfs_super_generation(super) > transid) {
			if (latest)
				btrfs_release_disk_super(super);

			latest = super;
			transid = btrfs_super_generation(super);
		}
	}

	return super;
}

/*
 * Write superblock @sb to the @device. Do not wait for completion, all the
 * folios we use for writing are locked.
 *
 * Write @max_mirrors copies of the superblock, where 0 means default that fit
 * the expected device size at commit time. Note that max_mirrors must be
 * same for write and wait phases.
 *
 * Return number of errors when folio is not found or submission fails.
 */
static int write_dev_supers(struct btrfs_device *device,
			    struct btrfs_super_block *sb, int max_mirrors)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct address_space *mapping = device->bdev->bd_mapping;
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	int i;
	int ret;
	u64 bytenr, bytenr_orig;

	atomic_set(&device->sb_write_errors, 0);

	if (max_mirrors == 0)
		max_mirrors = BTRFS_SUPER_MIRROR_MAX;

	shash->tfm = fs_info->csum_shash;

	for (i = 0; i < max_mirrors; i++) {
		struct folio *folio;
		struct bio *bio;
		struct btrfs_super_block *disk_super;
		size_t offset;

		bytenr_orig = btrfs_sb_offset(i);
		ret = btrfs_sb_log_location(device, i, WRITE, &bytenr);
		if (ret == -ENOENT) {
			continue;
		} else if (ret < 0) {
			btrfs_err(device->fs_info,
				"couldn't get super block location for mirror %d",
				i);
			atomic_inc(&device->sb_write_errors);
			continue;
		}
		if (bytenr + BTRFS_SUPER_INFO_SIZE >=
		    device->commit_total_bytes)
			break;

		btrfs_set_super_bytenr(sb, bytenr_orig);

		crypto_shash_digest(shash, (const char *)sb + BTRFS_CSUM_SIZE,
				    BTRFS_SUPER_INFO_SIZE - BTRFS_CSUM_SIZE,
				    sb->csum);

		folio = __filemap_get_folio(mapping, bytenr >> PAGE_SHIFT,
					    FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
					    GFP_NOFS);
		if (IS_ERR(folio)) {
			btrfs_err(device->fs_info,
			    "couldn't get super block page for bytenr %llu",
			    bytenr);
			atomic_inc(&device->sb_write_errors);
			continue;
		}
		ASSERT(folio_order(folio) == 0);

		offset = offset_in_folio(folio, bytenr);
		disk_super = folio_address(folio) + offset;
		memcpy(disk_super, sb, BTRFS_SUPER_INFO_SIZE);

		/*
		 * Directly use bios here instead of relying on the page cache
		 * to do I/O, so we don't lose the ability to do integrity
		 * checking.
		 */
		bio = bio_alloc(device->bdev, 1,
				REQ_OP_WRITE | REQ_SYNC | REQ_META | REQ_PRIO,
				GFP_NOFS);
		bio->bi_iter.bi_sector = bytenr >> SECTOR_SHIFT;
		bio->bi_private = device;
		bio->bi_end_io = btrfs_end_super_write;
		bio_add_folio_nofail(bio, folio, BTRFS_SUPER_INFO_SIZE, offset);

		/*
		 * We FUA only the first super block.  The others we allow to
		 * go down lazy and there's a short window where the on-disk
		 * copies might still contain the older version.
		 */
		if (i == 0 && !btrfs_test_opt(device->fs_info, NOBARRIER))
			bio->bi_opf |= REQ_FUA;
		submit_bio(bio);

		if (btrfs_advance_sb_log(device, i))
			atomic_inc(&device->sb_write_errors);
	}
	return atomic_read(&device->sb_write_errors) < i ? 0 : -1;
}

/*
 * Wait for write completion of superblocks done by write_dev_supers,
 * @max_mirrors same for write and wait phases.
 *
 * Return -1 if primary super block write failed or when there were no super block
 * copies written. Otherwise 0.
 */
static int wait_dev_supers(struct btrfs_device *device, int max_mirrors)
{
	int i;
	int errors = 0;
	bool primary_failed = false;
	int ret;
	u64 bytenr;

	if (max_mirrors == 0)
		max_mirrors = BTRFS_SUPER_MIRROR_MAX;

	for (i = 0; i < max_mirrors; i++) {
		struct folio *folio;

		ret = btrfs_sb_log_location(device, i, READ, &bytenr);
		if (ret == -ENOENT) {
			break;
		} else if (ret < 0) {
			errors++;
			if (i == 0)
				primary_failed = true;
			continue;
		}
		if (bytenr + BTRFS_SUPER_INFO_SIZE >=
		    device->commit_total_bytes)
			break;

		folio = filemap_get_folio(device->bdev->bd_mapping,
					  bytenr >> PAGE_SHIFT);
		/* If the folio has been removed, then we know it completed. */
		if (IS_ERR(folio))
			continue;
		ASSERT(folio_order(folio) == 0);

		/* Folio will be unlocked once the write completes. */
		folio_wait_locked(folio);
		folio_put(folio);
	}

	errors += atomic_read(&device->sb_write_errors);
	if (errors >= BTRFS_SUPER_PRIMARY_WRITE_ERROR)
		primary_failed = true;
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
	bio_uninit(bio);
	complete(bio->bi_private);
}

/*
 * Submit a flush request to the device if it supports it. Error handling is
 * done in the waiting counterpart.
 */
static void write_dev_flush(struct btrfs_device *device)
{
	struct bio *bio = &device->flush_bio;

	device->last_flush_error = BLK_STS_OK;

	bio_init(bio, device->bdev, NULL, 0,
		 REQ_OP_WRITE | REQ_SYNC | REQ_PREFLUSH);
	bio->bi_end_io = btrfs_end_empty_barrier;
	init_completion(&device->flush_wait);
	bio->bi_private = &device->flush_wait;
	submit_bio(bio);
	set_bit(BTRFS_DEV_STATE_FLUSH_SENT, &device->dev_state);
}

/*
 * If the flush bio has been submitted by write_dev_flush, wait for it.
 * Return true for any error, and false otherwise.
 */
static bool wait_dev_flush(struct btrfs_device *device)
{
	struct bio *bio = &device->flush_bio;

	if (!test_and_clear_bit(BTRFS_DEV_STATE_FLUSH_SENT, &device->dev_state))
		return false;

	wait_for_completion_io(&device->flush_wait);

	if (bio->bi_status) {
		device->last_flush_error = bio->bi_status;
		btrfs_dev_stat_inc_and_print(device, BTRFS_DEV_STAT_FLUSH_ERRS);
		return true;
	}

	return false;
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

		if (wait_dev_flush(dev))
			errors_wait++;
	}

	/*
	 * Checks last_flush_error of disks in order to determine the device
	 * state.
	 */
	if (errors_wait && !btrfs_check_rw_degradable(info, NULL))
		return -EIO;

	return 0;
}

int btrfs_get_num_tolerated_disk_barrier_failures(u64 flags)
{
	int raid_type;
	int min_tolerated = INT_MAX;

	if ((flags & BTRFS_BLOCK_GROUP_PROFILE_MASK) == 0 ||
	    (flags & BTRFS_AVAIL_ALLOC_BIT_SINGLE))
		min_tolerated = min_t(int, min_tolerated,
				    btrfs_raid_array[BTRFS_RAID_SINGLE].
				    tolerated_failures);

	for (raid_type = 0; raid_type < BTRFS_NR_RAID_TYPES; raid_type++) {
		if (raid_type == BTRFS_RAID_SINGLE)
			continue;
		if (!(flags & btrfs_raid_array[raid_type].bg_flag))
			continue;
		min_tolerated = min_t(int, min_tolerated,
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
		memcpy(dev_item->fsid, dev->fs_devices->metadata_uuid,
		       BTRFS_FSID_SIZE);

		flags = btrfs_super_flags(sb);
		btrfs_set_super_flags(sb, flags | BTRFS_HEADER_FLAG_WRITTEN);

		ret = btrfs_validate_write_super(fs_info, sb);
		if (ret < 0) {
			mutex_unlock(&fs_info->fs_devices->device_list_mutex);
			btrfs_handle_fs_error(fs_info, -EUCLEAN,
				"unexpected superblock corruption detected");
			return -EUCLEAN;
		}

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
	bool drop_ref = false;

	spin_lock(&fs_info->fs_roots_radix_lock);
	radix_tree_delete(&fs_info->fs_roots_radix,
			  (unsigned long)btrfs_root_id(root));
	if (test_and_clear_bit(BTRFS_ROOT_IN_RADIX, &root->state))
		drop_ref = true;
	spin_unlock(&fs_info->fs_roots_radix_lock);

	if (BTRFS_FS_ERROR(fs_info)) {
		ASSERT(root->log_root == NULL);
		if (root->reloc_root) {
			btrfs_put_root(root->reloc_root);
			root->reloc_root = NULL;
		}
	}

	if (drop_ref)
		btrfs_put_root(root);
}

int btrfs_commit_super(struct btrfs_fs_info *fs_info)
{
	mutex_lock(&fs_info->cleaner_mutex);
	btrfs_run_delayed_iputs(fs_info);
	mutex_unlock(&fs_info->cleaner_mutex);
	wake_up_process(fs_info->cleaner_kthread);

	/* wait until ongoing cleanup work done */
	down_write(&fs_info->cleanup_work_sem);
	up_write(&fs_info->cleanup_work_sem);

	return btrfs_commit_current_transaction(fs_info->tree_root);
}

static void warn_about_uncommitted_trans(struct btrfs_fs_info *fs_info)
{
	struct btrfs_transaction *trans;
	struct btrfs_transaction *tmp;
	bool found = false;

	/*
	 * This function is only called at the very end of close_ctree(),
	 * thus no other running transaction, no need to take trans_lock.
	 */
	ASSERT(test_bit(BTRFS_FS_CLOSING_DONE, &fs_info->flags));
	list_for_each_entry_safe(trans, tmp, &fs_info->trans_list, list) {
		struct extent_state *cached = NULL;
		u64 dirty_bytes = 0;
		u64 cur = 0;
		u64 found_start;
		u64 found_end;

		found = true;
		while (find_first_extent_bit(&trans->dirty_pages, cur,
			&found_start, &found_end, EXTENT_DIRTY, &cached)) {
			dirty_bytes += found_end + 1 - found_start;
			cur = found_end + 1;
		}
		btrfs_warn(fs_info,
	"transaction %llu (with %llu dirty metadata bytes) is not committed",
			   trans->transid, dirty_bytes);
		btrfs_cleanup_one_transaction(trans);

		if (trans == fs_info->running_transaction)
			fs_info->running_transaction = NULL;
		list_del_init(&trans->list);

		btrfs_put_transaction(trans);
		trace_btrfs_transaction_commit(fs_info);
	}
	ASSERT(!found);
}

void __cold close_ctree(struct btrfs_fs_info *fs_info)
{
	int ret;

	set_bit(BTRFS_FS_CLOSING_START, &fs_info->flags);

	/*
	 * If we had UNFINISHED_DROPS we could still be processing them, so
	 * clear that bit and wake up relocation so it can stop.
	 * We must do this before stopping the block group reclaim task, because
	 * at btrfs_relocate_block_group() we wait for this bit, and after the
	 * wait we stop with -EINTR if btrfs_fs_closing() returns non-zero - we
	 * have just set BTRFS_FS_CLOSING_START, so btrfs_fs_closing() will
	 * return 1.
	 */
	btrfs_wake_unfinished_drop(fs_info);

	/*
	 * We may have the reclaim task running and relocating a data block group,
	 * in which case it may create delayed iputs. So stop it before we park
	 * the cleaner kthread otherwise we can get new delayed iputs after
	 * parking the cleaner, and that can make the async reclaim task to hang
	 * if it's waiting for delayed iputs to complete, since the cleaner is
	 * parked and can not run delayed iputs - this will make us hang when
	 * trying to stop the async reclaim task.
	 */
	cancel_work_sync(&fs_info->reclaim_bgs_work);
	/*
	 * We don't want the cleaner to start new transactions, add more delayed
	 * iputs, etc. while we're closing. We can't use kthread_stop() yet
	 * because that frees the task_struct, and the transaction kthread might
	 * still try to wake up the cleaner.
	 */
	kthread_park(fs_info->cleaner_kthread);

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

	/*
	 * Wait for any fixup workers to complete.
	 * If we don't wait for them here and they are still running by the time
	 * we call kthread_stop() against the cleaner kthread further below, we
	 * get an use-after-free on the cleaner because the fixup worker adds an
	 * inode to the list of delayed iputs and then attempts to wakeup the
	 * cleaner kthread, which was already stopped and destroyed. We parked
	 * already the cleaner, but below we run all pending delayed iputs.
	 */
	btrfs_flush_workqueue(fs_info->fixup_workers);
	/*
	 * Similar case here, we have to wait for delalloc workers before we
	 * proceed below and stop the cleaner kthread, otherwise we trigger a
	 * use-after-tree on the cleaner kthread task_struct when a delalloc
	 * worker running submit_compressed_extents() adds a delayed iput, which
	 * does a wake up on the cleaner kthread, which was already freed below
	 * when we call kthread_stop().
	 */
	btrfs_flush_workqueue(fs_info->delalloc_workers);

	/*
	 * After we parked the cleaner kthread, ordered extents may have
	 * completed and created new delayed iputs. If one of the async reclaim
	 * tasks is running and in the RUN_DELAYED_IPUTS flush state, then we
	 * can hang forever trying to stop it, because if a delayed iput is
	 * added after it ran btrfs_run_delayed_iputs() and before it called
	 * btrfs_wait_on_delayed_iputs(), it will hang forever since there is
	 * no one else to run iputs.
	 *
	 * So wait for all ongoing ordered extents to complete and then run
	 * delayed iputs. This works because once we reach this point no one
	 * can either create new ordered extents nor create delayed iputs
	 * through some other means.
	 *
	 * Also note that btrfs_wait_ordered_roots() is not safe here, because
	 * it waits for BTRFS_ORDERED_COMPLETE to be set on an ordered extent,
	 * but the delayed iput for the respective inode is made only when doing
	 * the final btrfs_put_ordered_extent() (which must happen at
	 * btrfs_finish_ordered_io() when we are unmounting).
	 */
	btrfs_flush_workqueue(fs_info->endio_write_workers);
	/* Ordered extents for free space inodes. */
	btrfs_flush_workqueue(fs_info->endio_freespace_worker);
	btrfs_run_delayed_iputs(fs_info);

	cancel_work_sync(&fs_info->async_reclaim_work);
	cancel_work_sync(&fs_info->async_data_reclaim_work);
	cancel_work_sync(&fs_info->preempt_reclaim_work);
	cancel_work_sync(&fs_info->em_shrinker_work);

	/* Cancel or finish ongoing discard work */
	btrfs_discard_cleanup(fs_info);

	if (!sb_rdonly(fs_info->sb)) {
		/*
		 * The cleaner kthread is stopped, so do one final pass over
		 * unused block groups.
		 */
		btrfs_delete_unused_bgs(fs_info);

		/*
		 * There might be existing delayed inode workers still running
		 * and holding an empty delayed inode item. We must wait for
		 * them to complete first because they can create a transaction.
		 * This happens when someone calls btrfs_balance_delayed_items()
		 * and then a transaction commit runs the same delayed nodes
		 * before any delayed worker has done something with the nodes.
		 * We must wait for any worker here and not at transaction
		 * commit time since that could cause a deadlock.
		 * This is a very rare case.
		 */
		btrfs_flush_workqueue(fs_info->delayed_workers);

		ret = btrfs_commit_super(fs_info);
		if (ret)
			btrfs_err(fs_info, "commit super ret %d", ret);
	}

	if (BTRFS_FS_ERROR(fs_info))
		btrfs_error_commit_super(fs_info);

	kthread_stop(fs_info->transaction_kthread);
	kthread_stop(fs_info->cleaner_kthread);

	ASSERT(list_empty(&fs_info->delayed_iputs));
	set_bit(BTRFS_FS_CLOSING_DONE, &fs_info->flags);

	if (btrfs_check_quota_leak(fs_info)) {
		WARN_ON(IS_ENABLED(CONFIG_BTRFS_DEBUG));
		btrfs_err(fs_info, "qgroup reserved space leaked");
	}

	btrfs_free_qgroup_config(fs_info);
	ASSERT(list_empty(&fs_info->delalloc_roots));

	if (percpu_counter_sum(&fs_info->delalloc_bytes)) {
		btrfs_info(fs_info, "at unmount delalloc count %lld",
		       percpu_counter_sum(&fs_info->delalloc_bytes));
	}

	if (percpu_counter_sum(&fs_info->ordered_bytes))
		btrfs_info(fs_info, "at unmount dio bytes count %lld",
			   percpu_counter_sum(&fs_info->ordered_bytes));

	btrfs_sysfs_remove_mounted(fs_info);
	btrfs_sysfs_remove_fsid(fs_info->fs_devices);

	btrfs_put_block_group_cache(fs_info);

	/*
	 * we must make sure there is not any read request to
	 * submit after we stopping all workers.
	 */
	invalidate_inode_pages2(fs_info->btree_inode->i_mapping);
	btrfs_stop_all_workers(fs_info);

	/* We shouldn't have any transaction open at this point */
	warn_about_uncommitted_trans(fs_info);

	clear_bit(BTRFS_FS_OPEN, &fs_info->flags);
	free_root_pointers(fs_info, true);
	btrfs_free_fs_roots(fs_info);

	/*
	 * We must free the block groups after dropping the fs_roots as we could
	 * have had an IO error and have left over tree log blocks that aren't
	 * cleaned up until the fs roots are freed.  This makes the block group
	 * accounting appear to be wrong because there's pending reserved bytes,
	 * so make sure we do the block group cleanup afterwards.
	 */
	btrfs_free_block_groups(fs_info);

	iput(fs_info->btree_inode);

	btrfs_mapping_tree_free(fs_info);
	btrfs_close_devices(fs_info->fs_devices);
}

void btrfs_mark_buffer_dirty(struct btrfs_trans_handle *trans,
			     struct extent_buffer *buf)
{
	struct btrfs_fs_info *fs_info = buf->fs_info;
	u64 transid = btrfs_header_generation(buf);

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
	/*
	 * This is a fast path so only do this check if we have sanity tests
	 * enabled.  Normal people shouldn't be using unmapped buffers as dirty
	 * outside of the sanity tests.
	 */
	if (unlikely(test_bit(EXTENT_BUFFER_UNMAPPED, &buf->bflags)))
		return;
#endif
	/* This is an active transaction (its state < TRANS_STATE_UNBLOCKED). */
	ASSERT(trans->transid == fs_info->generation);
	btrfs_assert_tree_write_locked(buf);
	if (unlikely(transid != fs_info->generation)) {
		btrfs_abort_transaction(trans, -EUCLEAN);
		btrfs_crit(fs_info,
"dirty buffer transid mismatch, logical %llu found transid %llu running transid %llu",
			   buf->start, transid, fs_info->generation);
	}
	set_extent_buffer_dirty(buf);
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

	ret = __percpu_counter_compare(&fs_info->dirty_metadata_bytes,
				     BTRFS_DIRTY_METADATA_THRESH,
				     fs_info->dirty_metadata_batch);
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

static void btrfs_error_commit_super(struct btrfs_fs_info *fs_info)
{
	/* cleanup FS via transaction */
	btrfs_cleanup_transaction(fs_info);

	mutex_lock(&fs_info->cleaner_mutex);
	btrfs_run_delayed_iputs(fs_info);
	mutex_unlock(&fs_info->cleaner_mutex);

	down_write(&fs_info->cleanup_work_sem);
	up_write(&fs_info->cleanup_work_sem);
}

static void btrfs_drop_all_logs(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *gang[8];
	u64 root_objectid = 0;
	int ret;

	spin_lock(&fs_info->fs_roots_radix_lock);
	while ((ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, root_objectid,
					     ARRAY_SIZE(gang))) != 0) {
		int i;

		for (i = 0; i < ret; i++)
			gang[i] = btrfs_grab_root(gang[i]);
		spin_unlock(&fs_info->fs_roots_radix_lock);

		for (i = 0; i < ret; i++) {
			if (!gang[i])
				continue;
			root_objectid = btrfs_root_id(gang[i]);
			btrfs_free_log(NULL, gang[i]);
			btrfs_put_root(gang[i]);
		}
		root_objectid++;
		spin_lock(&fs_info->fs_roots_radix_lock);
	}
	spin_unlock(&fs_info->fs_roots_radix_lock);
	btrfs_free_log_root_tree(NULL, fs_info);
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
	LIST_HEAD(splice);

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

	/*
	 * We need this here because if we've been flipped read-only we won't
	 * get sync() from the umount, so we need to make sure any ordered
	 * extents that haven't had their dirty pages IO start writeout yet
	 * actually get run and error out properly.
	 */
	btrfs_wait_ordered_roots(fs_info, U64_MAX, NULL);
}

static void btrfs_destroy_delalloc_inodes(struct btrfs_root *root)
{
	struct btrfs_inode *btrfs_inode;
	LIST_HEAD(splice);

	spin_lock(&root->delalloc_lock);
	list_splice_init(&root->delalloc_inodes, &splice);

	while (!list_empty(&splice)) {
		struct inode *inode = NULL;
		btrfs_inode = list_first_entry(&splice, struct btrfs_inode,
					       delalloc_inodes);
		btrfs_del_delalloc_inode(btrfs_inode);
		spin_unlock(&root->delalloc_lock);

		/*
		 * Make sure we get a live inode and that it'll not disappear
		 * meanwhile.
		 */
		inode = igrab(&btrfs_inode->vfs_inode);
		if (inode) {
			unsigned int nofs_flag;

			nofs_flag = memalloc_nofs_save();
			invalidate_inode_pages2(inode->i_mapping);
			memalloc_nofs_restore(nofs_flag);
			iput(inode);
		}
		spin_lock(&root->delalloc_lock);
	}
	spin_unlock(&root->delalloc_lock);
}

static void btrfs_destroy_all_delalloc_inodes(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;
	LIST_HEAD(splice);

	spin_lock(&fs_info->delalloc_root_lock);
	list_splice_init(&fs_info->delalloc_roots, &splice);
	while (!list_empty(&splice)) {
		root = list_first_entry(&splice, struct btrfs_root,
					 delalloc_root);
		root = btrfs_grab_root(root);
		BUG_ON(!root);
		spin_unlock(&fs_info->delalloc_root_lock);

		btrfs_destroy_delalloc_inodes(root);
		btrfs_put_root(root);

		spin_lock(&fs_info->delalloc_root_lock);
	}
	spin_unlock(&fs_info->delalloc_root_lock);
}

static void btrfs_destroy_marked_extents(struct btrfs_fs_info *fs_info,
					 struct extent_io_tree *dirty_pages,
					 int mark)
{
	struct extent_buffer *eb;
	u64 start = 0;
	u64 end;

	while (find_first_extent_bit(dirty_pages, start, &start, &end,
				     mark, NULL)) {
		clear_extent_bits(dirty_pages, start, end, mark);
		while (start <= end) {
			eb = find_extent_buffer(fs_info, start);
			start += fs_info->nodesize;
			if (!eb)
				continue;

			btrfs_tree_lock(eb);
			wait_on_extent_buffer_writeback(eb);
			btrfs_clear_buffer_dirty(NULL, eb);
			btrfs_tree_unlock(eb);

			free_extent_buffer_stale(eb);
		}
	}
}

static void btrfs_destroy_pinned_extent(struct btrfs_fs_info *fs_info,
					struct extent_io_tree *unpin)
{
	u64 start;
	u64 end;

	while (1) {
		struct extent_state *cached_state = NULL;

		/*
		 * The btrfs_finish_extent_commit() may get the same range as
		 * ours between find_first_extent_bit and clear_extent_dirty.
		 * Hence, hold the unused_bg_unpin_mutex to avoid double unpin
		 * the same extent range.
		 */
		mutex_lock(&fs_info->unused_bg_unpin_mutex);
		if (!find_first_extent_bit(unpin, 0, &start, &end,
					   EXTENT_DIRTY, &cached_state)) {
			mutex_unlock(&fs_info->unused_bg_unpin_mutex);
			break;
		}

		clear_extent_dirty(unpin, start, end, &cached_state);
		free_extent_state(cached_state);
		btrfs_error_unpin_extent_range(fs_info, start, end);
		mutex_unlock(&fs_info->unused_bg_unpin_mutex);
		cond_resched();
	}
}

static void btrfs_cleanup_bg_io(struct btrfs_block_group *cache)
{
	struct inode *inode;

	inode = cache->io_ctl.inode;
	if (inode) {
		unsigned int nofs_flag;

		nofs_flag = memalloc_nofs_save();
		invalidate_inode_pages2(inode->i_mapping);
		memalloc_nofs_restore(nofs_flag);

		BTRFS_I(inode)->generation = 0;
		cache->io_ctl.inode = NULL;
		iput(inode);
	}
	ASSERT(cache->io_ctl.pages == NULL);
	btrfs_put_block_group(cache);
}

void btrfs_cleanup_dirty_bgs(struct btrfs_transaction *cur_trans,
			     struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_group *cache;

	spin_lock(&cur_trans->dirty_bgs_lock);
	while (!list_empty(&cur_trans->dirty_bgs)) {
		cache = list_first_entry(&cur_trans->dirty_bgs,
					 struct btrfs_block_group,
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
		btrfs_dec_delayed_refs_rsv_bg_updates(fs_info);
		spin_lock(&cur_trans->dirty_bgs_lock);
	}
	spin_unlock(&cur_trans->dirty_bgs_lock);

	/*
	 * Refer to the definition of io_bgs member for details why it's safe
	 * to use it without any locking
	 */
	while (!list_empty(&cur_trans->io_bgs)) {
		cache = list_first_entry(&cur_trans->io_bgs,
					 struct btrfs_block_group,
					 io_list);

		list_del_init(&cache->io_list);
		spin_lock(&cache->lock);
		cache->disk_cache_state = BTRFS_DC_ERROR;
		spin_unlock(&cache->lock);
		btrfs_cleanup_bg_io(cache);
	}
}

static void btrfs_free_all_qgroup_pertrans(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *gang[8];
	int i;
	int ret;

	spin_lock(&fs_info->fs_roots_radix_lock);
	while (1) {
		ret = radix_tree_gang_lookup_tag(&fs_info->fs_roots_radix,
						 (void **)gang, 0,
						 ARRAY_SIZE(gang),
						 BTRFS_ROOT_TRANS_TAG);
		if (ret == 0)
			break;
		for (i = 0; i < ret; i++) {
			struct btrfs_root *root = gang[i];

			btrfs_qgroup_free_meta_all_pertrans(root);
			radix_tree_tag_clear(&fs_info->fs_roots_radix,
					(unsigned long)btrfs_root_id(root),
					BTRFS_ROOT_TRANS_TAG);
		}
	}
	spin_unlock(&fs_info->fs_roots_radix_lock);
}

void btrfs_cleanup_one_transaction(struct btrfs_transaction *cur_trans)
{
	struct btrfs_fs_info *fs_info = cur_trans->fs_info;
	struct btrfs_device *dev, *tmp;

	btrfs_cleanup_dirty_bgs(cur_trans, fs_info);
	ASSERT(list_empty(&cur_trans->dirty_bgs));
	ASSERT(list_empty(&cur_trans->io_bgs));

	list_for_each_entry_safe(dev, tmp, &cur_trans->dev_update_list,
				 post_commit_list) {
		list_del_init(&dev->post_commit_list);
	}

	btrfs_destroy_delayed_refs(cur_trans);

	cur_trans->state = TRANS_STATE_COMMIT_START;
	wake_up(&fs_info->transaction_blocked_wait);

	cur_trans->state = TRANS_STATE_UNBLOCKED;
	wake_up(&fs_info->transaction_wait);

	btrfs_destroy_marked_extents(fs_info, &cur_trans->dirty_pages,
				     EXTENT_DIRTY);
	btrfs_destroy_pinned_extent(fs_info, &cur_trans->pinned_extents);

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
		if (t->state >= TRANS_STATE_COMMIT_PREP) {
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
		btrfs_cleanup_one_transaction(t);

		spin_lock(&fs_info->trans_lock);
		if (t == fs_info->running_transaction)
			fs_info->running_transaction = NULL;
		list_del_init(&t->list);
		spin_unlock(&fs_info->trans_lock);

		btrfs_put_transaction(t);
		trace_btrfs_transaction_commit(fs_info);
		spin_lock(&fs_info->trans_lock);
	}
	spin_unlock(&fs_info->trans_lock);
	btrfs_destroy_all_ordered_extents(fs_info);
	btrfs_destroy_delayed_inodes(fs_info);
	btrfs_assert_delayed_root_empty(fs_info);
	btrfs_destroy_all_delalloc_inodes(fs_info);
	btrfs_drop_all_logs(fs_info);
	btrfs_free_all_qgroup_pertrans(fs_info);
	mutex_unlock(&fs_info->transaction_kthread_mutex);

	return 0;
}

int btrfs_init_root_free_objectid(struct btrfs_root *root)
{
	struct btrfs_path *path;
	int ret;
	struct extent_buffer *l;
	struct btrfs_key search_key;
	struct btrfs_key found_key;
	int slot;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	search_key.objectid = BTRFS_LAST_FREE_OBJECTID;
	search_key.type = -1;
	search_key.offset = (u64)-1;
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto error;
	if (ret == 0) {
		/*
		 * Key with offset -1 found, there would have to exist a root
		 * with such id, but this is out of valid range.
		 */
		ret = -EUCLEAN;
		goto error;
	}
	if (path->slots[0] > 0) {
		slot = path->slots[0] - 1;
		l = path->nodes[0];
		btrfs_item_key_to_cpu(l, &found_key, slot);
		root->free_objectid = max_t(u64, found_key.objectid + 1,
					    BTRFS_FIRST_FREE_OBJECTID);
	} else {
		root->free_objectid = BTRFS_FIRST_FREE_OBJECTID;
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

int btrfs_get_free_objectid(struct btrfs_root *root, u64 *objectid)
{
	int ret;
	mutex_lock(&root->objectid_mutex);

	if (unlikely(root->free_objectid >= BTRFS_LAST_FREE_OBJECTID)) {
		btrfs_warn(root->fs_info,
			   "the objectid of root %llu reaches its highest value",
			   btrfs_root_id(root));
		ret = -ENOSPC;
		goto out;
	}

	*objectid = root->free_objectid++;
	ret = 0;
out:
	mutex_unlock(&root->objectid_mutex);
	return ret;
}

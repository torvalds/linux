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

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include <linux/radix-tree.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/crc32c.h>
#include <linux/slab.h>
#include <linux/migrate.h>
#include <linux/ratelimit.h>
#include <asm/unaligned.h>
#include "compat.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "volumes.h"
#include "print-tree.h"
#include "async-thread.h"
#include "locking.h"
#include "tree-log.h"
#include "free-space-cache.h"
#include "inode-map.h"
#include "check-integrity.h"
#include "rcu-string.h"
#include "dev-replace.h"
#include "raid56.h"

#ifdef CONFIG_X86
#include <asm/cpufeature.h>
#endif

static struct extent_io_ops btree_extent_io_ops;
static void end_workqueue_fn(struct btrfs_work *work);
static void free_fs_root(struct btrfs_root *root);
static int btrfs_check_super_valid(struct btrfs_fs_info *fs_info,
				    int read_only);
static void btrfs_destroy_ordered_operations(struct btrfs_transaction *t,
					     struct btrfs_root *root);
static void btrfs_destroy_ordered_extents(struct btrfs_root *root);
static int btrfs_destroy_delayed_refs(struct btrfs_transaction *trans,
				      struct btrfs_root *root);
static void btrfs_evict_pending_snapshots(struct btrfs_transaction *t);
static void btrfs_destroy_delalloc_inodes(struct btrfs_root *root);
static int btrfs_destroy_marked_extents(struct btrfs_root *root,
					struct extent_io_tree *dirty_pages,
					int mark);
static int btrfs_destroy_pinned_extent(struct btrfs_root *root,
				       struct extent_io_tree *pinned_extents);

/*
 * end_io_wq structs are used to do processing in task context when an IO is
 * complete.  This is used during reads to verify checksums, and it is used
 * by writes to insert metadata for new file extents after IO is complete.
 */
struct end_io_wq {
	struct bio *bio;
	bio_end_io_t *end_io;
	void *private;
	struct btrfs_fs_info *info;
	int error;
	int metadata;
	struct list_head list;
	struct btrfs_work work;
};

/*
 * async submit bios are used to offload expensive checksumming
 * onto the worker threads.  They checksum file and metadata bios
 * just before they are sent down the IO stack.
 */
struct async_submit_bio {
	struct inode *inode;
	struct bio *bio;
	struct list_head list;
	extent_submit_bio_hook_t *submit_bio_start;
	extent_submit_bio_hook_t *submit_bio_done;
	int rw;
	int mirror_num;
	unsigned long bio_flags;
	/*
	 * bio_offset is optional, can be used if the pages in the bio
	 * can't tell us where in the file the bio should go
	 */
	u64 bio_offset;
	struct btrfs_work work;
	int error;
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
	{ .id = BTRFS_ORPHAN_OBJECTID,		.name_stem = "orphan"	},
	{ .id = BTRFS_TREE_LOG_OBJECTID,	.name_stem = "log"	},
	{ .id = BTRFS_TREE_RELOC_OBJECTID,	.name_stem = "treloc"	},
	{ .id = BTRFS_DATA_RELOC_TREE_OBJECTID,	.name_stem = "dreloc"	},
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
static struct extent_map *btree_get_extent(struct inode *inode,
		struct page *page, size_t pg_offset, u64 start, u64 len,
		int create)
{
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_map *em;
	int ret;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	if (em) {
		em->bdev =
			BTRFS_I(inode)->root->fs_info->fs_devices->latest_bdev;
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
	em->bdev = BTRFS_I(inode)->root->fs_info->fs_devices->latest_bdev;

	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em);
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

u32 btrfs_csum_data(struct btrfs_root *root, char *data, u32 seed, size_t len)
{
	return crc32c(seed, data, len);
}

void btrfs_csum_final(u32 crc, char *result)
{
	put_unaligned_le32(~crc, result);
}

/*
 * compute the csum for a btree block, and either verify it or write it
 * into the csum field of the block.
 */
static int csum_tree_block(struct btrfs_root *root, struct extent_buffer *buf,
			   int verify)
{
	u16 csum_size = btrfs_super_csum_size(root->fs_info->super_copy);
	char *result = NULL;
	unsigned long len;
	unsigned long cur_len;
	unsigned long offset = BTRFS_CSUM_SIZE;
	char *kaddr;
	unsigned long map_start;
	unsigned long map_len;
	int err;
	u32 crc = ~(u32)0;
	unsigned long inline_result;

	len = buf->len - offset;
	while (len > 0) {
		err = map_private_extent_buffer(buf, offset, 32,
					&kaddr, &map_start, &map_len);
		if (err)
			return 1;
		cur_len = min(len, map_len - (offset - map_start));
		crc = btrfs_csum_data(root, kaddr + offset - map_start,
				      crc, cur_len);
		len -= cur_len;
		offset += cur_len;
	}
	if (csum_size > sizeof(inline_result)) {
		result = kzalloc(csum_size * sizeof(char), GFP_NOFS);
		if (!result)
			return 1;
	} else {
		result = (char *)&inline_result;
	}

	btrfs_csum_final(crc, result);

	if (verify) {
		if (memcmp_extent_buffer(buf, result, 0, csum_size)) {
			u32 val;
			u32 found = 0;
			memcpy(&found, result, csum_size);

			read_extent_buffer(buf, &val, 0, csum_size);
			printk_ratelimited(KERN_INFO "btrfs: %s checksum verify "
				       "failed on %llu wanted %X found %X "
				       "level %d\n",
				       root->fs_info->sb->s_id,
				       (unsigned long long)buf->start, val, found,
				       btrfs_header_level(buf));
			if (result != (char *)&inline_result)
				kfree(result);
			return 1;
		}
	} else {
		write_extent_buffer(buf, result, 0, csum_size);
	}
	if (result != (char *)&inline_result)
		kfree(result);
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

	if (!parent_transid || btrfs_header_generation(eb) == parent_transid)
		return 0;

	if (atomic)
		return -EAGAIN;

	lock_extent_bits(io_tree, eb->start, eb->start + eb->len - 1,
			 0, &cached_state);
	if (extent_buffer_uptodate(eb) &&
	    btrfs_header_generation(eb) == parent_transid) {
		ret = 0;
		goto out;
	}
	printk_ratelimited("parent transid verify failed on %llu wanted %llu "
		       "found %llu\n",
		       (unsigned long long)eb->start,
		       (unsigned long long)parent_transid,
		       (unsigned long long)btrfs_header_generation(eb));
	ret = 1;
	clear_extent_buffer_uptodate(eb);
out:
	unlock_extent_cached(io_tree, eb->start, eb->start + eb->len - 1,
			     &cached_state, GFP_NOFS);
	return ret;
}

/*
 * helper to read a given tree block, doing retries as required when
 * the checksums don't match and we have alternate mirrors to try.
 */
static int btree_read_extent_buffer_pages(struct btrfs_root *root,
					  struct extent_buffer *eb,
					  u64 start, u64 parent_transid)
{
	struct extent_io_tree *io_tree;
	int failed = 0;
	int ret;
	int num_copies = 0;
	int mirror_num = 0;
	int failed_mirror = 0;

	clear_bit(EXTENT_BUFFER_CORRUPT, &eb->bflags);
	io_tree = &BTRFS_I(root->fs_info->btree_inode)->io_tree;
	while (1) {
		ret = read_extent_buffer_pages(io_tree, eb, start,
					       WAIT_COMPLETE,
					       btree_get_extent, mirror_num);
		if (!ret) {
			if (!verify_parent_transid(io_tree, eb,
						   parent_transid, 0))
				break;
			else
				ret = -EIO;
		}

		/*
		 * This buffer's crc is fine, but its contents are corrupted, so
		 * there is no reason to read the other copies, they won't be
		 * any less wrong.
		 */
		if (test_bit(EXTENT_BUFFER_CORRUPT, &eb->bflags))
			break;

		num_copies = btrfs_num_copies(root->fs_info,
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
		repair_eb_io_failure(root, eb, failed_mirror);

	return ret;
}

/*
 * checksum a dirty tree block before IO.  This has extra checks to make sure
 * we only fill in the checksum field in the first page of a multi-page block
 */

static int csum_dirty_buffer(struct btrfs_root *root, struct page *page)
{
	struct extent_io_tree *tree;
	u64 start = page_offset(page);
	u64 found_start;
	struct extent_buffer *eb;

	tree = &BTRFS_I(page->mapping->host)->io_tree;

	eb = (struct extent_buffer *)page->private;
	if (page != eb->pages[0])
		return 0;
	found_start = btrfs_header_bytenr(eb);
	if (found_start != start) {
		WARN_ON(1);
		return 0;
	}
	if (!PageUptodate(page)) {
		WARN_ON(1);
		return 0;
	}
	csum_tree_block(root, eb, 0);
	return 0;
}

static int check_tree_block_fsid(struct btrfs_root *root,
				 struct extent_buffer *eb)
{
	struct btrfs_fs_devices *fs_devices = root->fs_info->fs_devices;
	u8 fsid[BTRFS_UUID_SIZE];
	int ret = 1;

	read_extent_buffer(eb, fsid, (unsigned long)btrfs_header_fsid(eb),
			   BTRFS_FSID_SIZE);
	while (fs_devices) {
		if (!memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE)) {
			ret = 0;
			break;
		}
		fs_devices = fs_devices->seed;
	}
	return ret;
}

#define CORRUPT(reason, eb, root, slot)				\
	printk(KERN_CRIT "btrfs: corrupt leaf, %s: block=%llu,"	\
	       "root=%llu, slot=%d\n", reason,			\
	       (unsigned long long)btrfs_header_bytenr(eb),	\
	       (unsigned long long)root->objectid, slot)

static noinline int check_leaf(struct btrfs_root *root,
			       struct extent_buffer *leaf)
{
	struct btrfs_key key;
	struct btrfs_key leaf_key;
	u32 nritems = btrfs_header_nritems(leaf);
	int slot;

	if (nritems == 0)
		return 0;

	/* Check the 0 item */
	if (btrfs_item_offset_nr(leaf, 0) + btrfs_item_size_nr(leaf, 0) !=
	    BTRFS_LEAF_DATA_SIZE(root)) {
		CORRUPT("invalid item offset size pair", leaf, root, 0);
		return -EIO;
	}

	/*
	 * Check to make sure each items keys are in the correct order and their
	 * offsets make sense.  We only have to loop through nritems-1 because
	 * we check the current slot against the next slot, which verifies the
	 * next slot's offset+size makes sense and that the current's slot
	 * offset is correct.
	 */
	for (slot = 0; slot < nritems - 1; slot++) {
		btrfs_item_key_to_cpu(leaf, &leaf_key, slot);
		btrfs_item_key_to_cpu(leaf, &key, slot + 1);

		/* Make sure the keys are in the right order */
		if (btrfs_comp_cpu_keys(&leaf_key, &key) >= 0) {
			CORRUPT("bad key order", leaf, root, slot);
			return -EIO;
		}

		/*
		 * Make sure the offset and ends are right, remember that the
		 * item data starts at the end of the leaf and grows towards the
		 * front.
		 */
		if (btrfs_item_offset_nr(leaf, slot) !=
			btrfs_item_end_nr(leaf, slot + 1)) {
			CORRUPT("slot offset bad", leaf, root, slot);
			return -EIO;
		}

		/*
		 * Check to make sure that we don't point outside of the leaf,
		 * just incase all the items are consistent to eachother, but
		 * all point outside of the leaf.
		 */
		if (btrfs_item_end_nr(leaf, slot) >
		    BTRFS_LEAF_DATA_SIZE(root)) {
			CORRUPT("slot end outside of leaf", leaf, root, slot);
			return -EIO;
		}
	}

	return 0;
}

struct extent_buffer *find_eb_for_page(struct extent_io_tree *tree,
				       struct page *page, int max_walk)
{
	struct extent_buffer *eb;
	u64 start = page_offset(page);
	u64 target = start;
	u64 min_start;

	if (start < max_walk)
		min_start = 0;
	else
		min_start = start - max_walk;

	while (start >= min_start) {
		eb = find_extent_buffer(tree, start, 0);
		if (eb) {
			/*
			 * we found an extent buffer and it contains our page
			 * horray!
			 */
			if (eb->start <= target &&
			    eb->start + eb->len > target)
				return eb;

			/* we found an extent buffer that wasn't for us */
			free_extent_buffer(eb);
			return NULL;
		}
		if (start == 0)
			break;
		start -= PAGE_CACHE_SIZE;
	}
	return NULL;
}

static int btree_readpage_end_io_hook(struct page *page, u64 start, u64 end,
			       struct extent_state *state, int mirror)
{
	struct extent_io_tree *tree;
	u64 found_start;
	int found_level;
	struct extent_buffer *eb;
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;
	int ret = 0;
	int reads_done;

	if (!page->private)
		goto out;

	tree = &BTRFS_I(page->mapping->host)->io_tree;
	eb = (struct extent_buffer *)page->private;

	/* the pending IO might have been the only thing that kept this buffer
	 * in memory.  Make sure we have a ref for all this other checks
	 */
	extent_buffer_get(eb);

	reads_done = atomic_dec_and_test(&eb->io_pages);
	if (!reads_done)
		goto err;

	eb->read_mirror = mirror;
	if (test_bit(EXTENT_BUFFER_IOERR, &eb->bflags)) {
		ret = -EIO;
		goto err;
	}

	found_start = btrfs_header_bytenr(eb);
	if (found_start != eb->start) {
		printk_ratelimited(KERN_INFO "btrfs bad tree block start "
			       "%llu %llu\n",
			       (unsigned long long)found_start,
			       (unsigned long long)eb->start);
		ret = -EIO;
		goto err;
	}
	if (check_tree_block_fsid(root, eb)) {
		printk_ratelimited(KERN_INFO "btrfs bad fsid on block %llu\n",
			       (unsigned long long)eb->start);
		ret = -EIO;
		goto err;
	}
	found_level = btrfs_header_level(eb);

	btrfs_set_buffer_lockdep_class(btrfs_header_owner(eb),
				       eb, found_level);

	ret = csum_tree_block(root, eb, 1);
	if (ret) {
		ret = -EIO;
		goto err;
	}

	/*
	 * If this is a leaf block and it is corrupt, set the corrupt bit so
	 * that we don't try and read the other copies of this block, just
	 * return -EIO.
	 */
	if (found_level == 0 && check_leaf(root, eb)) {
		set_bit(EXTENT_BUFFER_CORRUPT, &eb->bflags);
		ret = -EIO;
	}

	if (!ret)
		set_extent_buffer_uptodate(eb);
err:
	if (test_bit(EXTENT_BUFFER_READAHEAD, &eb->bflags)) {
		clear_bit(EXTENT_BUFFER_READAHEAD, &eb->bflags);
		btree_readahead_hook(root, eb, eb->start, ret);
	}

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
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;

	eb = (struct extent_buffer *)page->private;
	set_bit(EXTENT_BUFFER_IOERR, &eb->bflags);
	eb->read_mirror = failed_mirror;
	atomic_dec(&eb->io_pages);
	if (test_and_clear_bit(EXTENT_BUFFER_READAHEAD, &eb->bflags))
		btree_readahead_hook(root, eb, eb->start, -EIO);
	return -EIO;	/* we fixed nothing */
}

static void end_workqueue_bio(struct bio *bio, int err)
{
	struct end_io_wq *end_io_wq = bio->bi_private;
	struct btrfs_fs_info *fs_info;

	fs_info = end_io_wq->info;
	end_io_wq->error = err;
	end_io_wq->work.func = end_workqueue_fn;
	end_io_wq->work.flags = 0;

	if (bio->bi_rw & REQ_WRITE) {
		if (end_io_wq->metadata == BTRFS_WQ_ENDIO_METADATA)
			btrfs_queue_worker(&fs_info->endio_meta_write_workers,
					   &end_io_wq->work);
		else if (end_io_wq->metadata == BTRFS_WQ_ENDIO_FREE_SPACE)
			btrfs_queue_worker(&fs_info->endio_freespace_worker,
					   &end_io_wq->work);
		else if (end_io_wq->metadata == BTRFS_WQ_ENDIO_RAID56)
			btrfs_queue_worker(&fs_info->endio_raid56_workers,
					   &end_io_wq->work);
		else
			btrfs_queue_worker(&fs_info->endio_write_workers,
					   &end_io_wq->work);
	} else {
		if (end_io_wq->metadata == BTRFS_WQ_ENDIO_RAID56)
			btrfs_queue_worker(&fs_info->endio_raid56_workers,
					   &end_io_wq->work);
		else if (end_io_wq->metadata)
			btrfs_queue_worker(&fs_info->endio_meta_workers,
					   &end_io_wq->work);
		else
			btrfs_queue_worker(&fs_info->endio_workers,
					   &end_io_wq->work);
	}
}

/*
 * For the metadata arg you want
 *
 * 0 - if data
 * 1 - if normal metadta
 * 2 - if writing to the free space cache area
 * 3 - raid parity work
 */
int btrfs_bio_wq_end_io(struct btrfs_fs_info *info, struct bio *bio,
			int metadata)
{
	struct end_io_wq *end_io_wq;
	end_io_wq = kmalloc(sizeof(*end_io_wq), GFP_NOFS);
	if (!end_io_wq)
		return -ENOMEM;

	end_io_wq->private = bio->bi_private;
	end_io_wq->end_io = bio->bi_end_io;
	end_io_wq->info = info;
	end_io_wq->error = 0;
	end_io_wq->bio = bio;
	end_io_wq->metadata = metadata;

	bio->bi_private = end_io_wq;
	bio->bi_end_io = end_workqueue_bio;
	return 0;
}

unsigned long btrfs_async_submit_limit(struct btrfs_fs_info *info)
{
	unsigned long limit = min_t(unsigned long,
				    info->workers.max_workers,
				    info->fs_devices->open_devices);
	return 256 * limit;
}

static void run_one_async_start(struct btrfs_work *work)
{
	struct async_submit_bio *async;
	int ret;

	async = container_of(work, struct  async_submit_bio, work);
	ret = async->submit_bio_start(async->inode, async->rw, async->bio,
				      async->mirror_num, async->bio_flags,
				      async->bio_offset);
	if (ret)
		async->error = ret;
}

static void run_one_async_done(struct btrfs_work *work)
{
	struct btrfs_fs_info *fs_info;
	struct async_submit_bio *async;
	int limit;

	async = container_of(work, struct  async_submit_bio, work);
	fs_info = BTRFS_I(async->inode)->root->fs_info;

	limit = btrfs_async_submit_limit(fs_info);
	limit = limit * 2 / 3;

	if (atomic_dec_return(&fs_info->nr_async_submits) < limit &&
	    waitqueue_active(&fs_info->async_submit_wait))
		wake_up(&fs_info->async_submit_wait);

	/* If an error occured we just want to clean up the bio and move on */
	if (async->error) {
		bio_endio(async->bio, async->error);
		return;
	}

	async->submit_bio_done(async->inode, async->rw, async->bio,
			       async->mirror_num, async->bio_flags,
			       async->bio_offset);
}

static void run_one_async_free(struct btrfs_work *work)
{
	struct async_submit_bio *async;

	async = container_of(work, struct  async_submit_bio, work);
	kfree(async);
}

int btrfs_wq_submit_bio(struct btrfs_fs_info *fs_info, struct inode *inode,
			int rw, struct bio *bio, int mirror_num,
			unsigned long bio_flags,
			u64 bio_offset,
			extent_submit_bio_hook_t *submit_bio_start,
			extent_submit_bio_hook_t *submit_bio_done)
{
	struct async_submit_bio *async;

	async = kmalloc(sizeof(*async), GFP_NOFS);
	if (!async)
		return -ENOMEM;

	async->inode = inode;
	async->rw = rw;
	async->bio = bio;
	async->mirror_num = mirror_num;
	async->submit_bio_start = submit_bio_start;
	async->submit_bio_done = submit_bio_done;

	async->work.func = run_one_async_start;
	async->work.ordered_func = run_one_async_done;
	async->work.ordered_free = run_one_async_free;

	async->work.flags = 0;
	async->bio_flags = bio_flags;
	async->bio_offset = bio_offset;

	async->error = 0;

	atomic_inc(&fs_info->nr_async_submits);

	if (rw & REQ_SYNC)
		btrfs_set_work_high_prio(&async->work);

	btrfs_queue_worker(&fs_info->workers, &async->work);

	while (atomic_read(&fs_info->async_submit_draining) &&
	      atomic_read(&fs_info->nr_async_submits)) {
		wait_event(fs_info->async_submit_wait,
			   (atomic_read(&fs_info->nr_async_submits) == 0));
	}

	return 0;
}

static int btree_csum_one_bio(struct bio *bio)
{
	struct bio_vec *bvec = bio->bi_io_vec;
	int bio_index = 0;
	struct btrfs_root *root;
	int ret = 0;

	WARN_ON(bio->bi_vcnt <= 0);
	while (bio_index < bio->bi_vcnt) {
		root = BTRFS_I(bvec->bv_page->mapping->host)->root;
		ret = csum_dirty_buffer(root, bvec->bv_page);
		if (ret)
			break;
		bio_index++;
		bvec++;
	}
	return ret;
}

static int __btree_submit_bio_start(struct inode *inode, int rw,
				    struct bio *bio, int mirror_num,
				    unsigned long bio_flags,
				    u64 bio_offset)
{
	/*
	 * when we're called for a write, we're already in the async
	 * submission context.  Just jump into btrfs_map_bio
	 */
	return btree_csum_one_bio(bio);
}

static int __btree_submit_bio_done(struct inode *inode, int rw, struct bio *bio,
				 int mirror_num, unsigned long bio_flags,
				 u64 bio_offset)
{
	int ret;

	/*
	 * when we're called for a write, we're already in the async
	 * submission context.  Just jump into btrfs_map_bio
	 */
	ret = btrfs_map_bio(BTRFS_I(inode)->root, rw, bio, mirror_num, 1);
	if (ret)
		bio_endio(bio, ret);
	return ret;
}

static int check_async_write(struct inode *inode, unsigned long bio_flags)
{
	if (bio_flags & EXTENT_BIO_TREE_LOG)
		return 0;
#ifdef CONFIG_X86
	if (cpu_has_xmm4_2)
		return 0;
#endif
	return 1;
}

static int btree_submit_bio_hook(struct inode *inode, int rw, struct bio *bio,
				 int mirror_num, unsigned long bio_flags,
				 u64 bio_offset)
{
	int async = check_async_write(inode, bio_flags);
	int ret;

	if (!(rw & REQ_WRITE)) {
		/*
		 * called for a read, do the setup so that checksum validation
		 * can happen in the async kernel threads
		 */
		ret = btrfs_bio_wq_end_io(BTRFS_I(inode)->root->fs_info,
					  bio, 1);
		if (ret)
			goto out_w_error;
		ret = btrfs_map_bio(BTRFS_I(inode)->root, rw, bio,
				    mirror_num, 0);
	} else if (!async) {
		ret = btree_csum_one_bio(bio);
		if (ret)
			goto out_w_error;
		ret = btrfs_map_bio(BTRFS_I(inode)->root, rw, bio,
				    mirror_num, 0);
	} else {
		/*
		 * kthread helpers are used to submit writes so that
		 * checksumming can happen in parallel across all CPUs
		 */
		ret = btrfs_wq_submit_bio(BTRFS_I(inode)->root->fs_info,
					  inode, rw, bio, mirror_num, 0,
					  bio_offset,
					  __btree_submit_bio_start,
					  __btree_submit_bio_done);
	}

	if (ret) {
out_w_error:
		bio_endio(bio, ret);
	}
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
	struct extent_io_tree *tree;
	struct btrfs_fs_info *fs_info;
	int ret;

	tree = &BTRFS_I(mapping->host)->io_tree;
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
	/*
	 * We need to mask out eg. __GFP_HIGHMEM and __GFP_DMA32 as we're doing
	 * slab allocation from alloc_extent_state down the callchain where
	 * it'd hit a BUG_ON as those flags are not allowed.
	 */
	gfp_flags &= ~GFP_SLAB_BUG_MASK;

	return try_release_extent_buffer(page, gfp_flags);
}

static void btree_invalidatepage(struct page *page, unsigned long offset)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	extent_invalidatepage(tree, page, offset);
	btree_releasepage(page, GFP_NOFS);
	if (PagePrivate(page)) {
		printk(KERN_WARNING "btrfs warning page private not zero "
		       "on page %llu\n", (unsigned long long)page_offset(page));
		ClearPagePrivate(page);
		set_page_private(page, 0);
		page_cache_release(page);
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

int readahead_tree_block(struct btrfs_root *root, u64 bytenr, u32 blocksize,
			 u64 parent_transid)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = root->fs_info->btree_inode;
	int ret = 0;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return 0;
	read_extent_buffer_pages(&BTRFS_I(btree_inode)->io_tree,
				 buf, 0, WAIT_NONE, btree_get_extent, 0);
	free_extent_buffer(buf);
	return ret;
}

int reada_tree_block_flagged(struct btrfs_root *root, u64 bytenr, u32 blocksize,
			 int mirror_num, struct extent_buffer **eb)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_io_tree *io_tree = &BTRFS_I(btree_inode)->io_tree;
	int ret;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return 0;

	set_bit(EXTENT_BUFFER_READAHEAD, &buf->bflags);

	ret = read_extent_buffer_pages(io_tree, buf, 0, WAIT_PAGE_LOCK,
				       btree_get_extent, mirror_num);
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

struct extent_buffer *btrfs_find_tree_block(struct btrfs_root *root,
					    u64 bytenr, u32 blocksize)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_buffer *eb;
	eb = find_extent_buffer(&BTRFS_I(btree_inode)->io_tree,
				bytenr, blocksize);
	return eb;
}

struct extent_buffer *btrfs_find_create_tree_block(struct btrfs_root *root,
						 u64 bytenr, u32 blocksize)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_buffer *eb;

	eb = alloc_extent_buffer(&BTRFS_I(btree_inode)->io_tree,
				 bytenr, blocksize);
	return eb;
}


int btrfs_write_tree_block(struct extent_buffer *buf)
{
	return filemap_fdatawrite_range(buf->pages[0]->mapping, buf->start,
					buf->start + buf->len - 1);
}

int btrfs_wait_tree_block_writeback(struct extent_buffer *buf)
{
	return filemap_fdatawait_range(buf->pages[0]->mapping,
				       buf->start, buf->start + buf->len - 1);
}

struct extent_buffer *read_tree_block(struct btrfs_root *root, u64 bytenr,
				      u32 blocksize, u64 parent_transid)
{
	struct extent_buffer *buf = NULL;
	int ret;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return NULL;

	ret = btree_read_extent_buffer_pages(root, buf, 0, parent_transid);
	return buf;

}

void clean_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      struct extent_buffer *buf)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (btrfs_header_generation(buf) ==
	    fs_info->running_transaction->transid) {
		btrfs_assert_tree_locked(buf);

		if (test_and_clear_bit(EXTENT_BUFFER_DIRTY, &buf->bflags)) {
			__percpu_counter_add(&fs_info->dirty_metadata_bytes,
					     -buf->len,
					     fs_info->dirty_metadata_batch);
			/* ugh, clear_extent_buffer_dirty needs to lock the page */
			btrfs_set_lock_blocking(buf);
			clear_extent_buffer_dirty(buf);
		}
	}
}

static void __setup_root(u32 nodesize, u32 leafsize, u32 sectorsize,
			 u32 stripesize, struct btrfs_root *root,
			 struct btrfs_fs_info *fs_info,
			 u64 objectid)
{
	root->node = NULL;
	root->commit_root = NULL;
	root->sectorsize = sectorsize;
	root->nodesize = nodesize;
	root->leafsize = leafsize;
	root->stripesize = stripesize;
	root->ref_cows = 0;
	root->track_dirty = 0;
	root->in_radix = 0;
	root->orphan_item_inserted = 0;
	root->orphan_cleanup_state = 0;

	root->objectid = objectid;
	root->last_trans = 0;
	root->highest_objectid = 0;
	root->name = NULL;
	root->inode_tree = RB_ROOT;
	INIT_RADIX_TREE(&root->delayed_nodes_tree, GFP_ATOMIC);
	root->block_rsv = NULL;
	root->orphan_block_rsv = NULL;

	INIT_LIST_HEAD(&root->dirty_list);
	INIT_LIST_HEAD(&root->root_list);
	INIT_LIST_HEAD(&root->logged_list[0]);
	INIT_LIST_HEAD(&root->logged_list[1]);
	spin_lock_init(&root->orphan_lock);
	spin_lock_init(&root->inode_lock);
	spin_lock_init(&root->accounting_lock);
	spin_lock_init(&root->log_extents_lock[0]);
	spin_lock_init(&root->log_extents_lock[1]);
	mutex_init(&root->objectid_mutex);
	mutex_init(&root->log_mutex);
	init_waitqueue_head(&root->log_writer_wait);
	init_waitqueue_head(&root->log_commit_wait[0]);
	init_waitqueue_head(&root->log_commit_wait[1]);
	atomic_set(&root->log_commit[0], 0);
	atomic_set(&root->log_commit[1], 0);
	atomic_set(&root->log_writers, 0);
	atomic_set(&root->log_batch, 0);
	atomic_set(&root->orphan_inodes, 0);
	root->log_transid = 0;
	root->last_log_commit = 0;
	extent_io_tree_init(&root->dirty_log_pages,
			     fs_info->btree_inode->i_mapping);

	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	memset(&root->defrag_progress, 0, sizeof(root->defrag_progress));
	memset(&root->root_kobj, 0, sizeof(root->root_kobj));
	root->defrag_trans_start = fs_info->generation;
	init_completion(&root->kobj_unregister);
	root->defrag_running = 0;
	root->root_key.objectid = objectid;
	root->anon_dev = 0;

	spin_lock_init(&root->root_item_lock);
}

static int __must_check find_and_setup_root(struct btrfs_root *tree_root,
					    struct btrfs_fs_info *fs_info,
					    u64 objectid,
					    struct btrfs_root *root)
{
	int ret;
	u32 blocksize;
	u64 generation;

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, objectid);
	ret = btrfs_find_last_root(tree_root, objectid,
				   &root->root_item, &root->root_key);
	if (ret > 0)
		return -ENOENT;
	else if (ret < 0)
		return ret;

	generation = btrfs_root_generation(&root->root_item);
	blocksize = btrfs_level_size(root, btrfs_root_level(&root->root_item));
	root->commit_root = NULL;
	root->node = read_tree_block(root, btrfs_root_bytenr(&root->root_item),
				     blocksize, generation);
	if (!root->node || !btrfs_buffer_uptodate(root->node, generation, 0)) {
		free_extent_buffer(root->node);
		root->node = NULL;
		return -EIO;
	}
	root->commit_root = btrfs_root_node(root);
	return 0;
}

static struct btrfs_root *btrfs_alloc_root(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root = kzalloc(sizeof(*root), GFP_NOFS);
	if (root)
		root->fs_info = fs_info;
	return root;
}

struct btrfs_root *btrfs_create_tree(struct btrfs_trans_handle *trans,
				     struct btrfs_fs_info *fs_info,
				     u64 objectid)
{
	struct extent_buffer *leaf;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *root;
	struct btrfs_key key;
	int ret = 0;
	u64 bytenr;

	root = btrfs_alloc_root(fs_info);
	if (!root)
		return ERR_PTR(-ENOMEM);

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, objectid);
	root->root_key.objectid = objectid;
	root->root_key.type = BTRFS_ROOT_ITEM_KEY;
	root->root_key.offset = 0;

	leaf = btrfs_alloc_free_block(trans, root, root->leafsize,
				      0, objectid, NULL, 0, 0, 0);
	if (IS_ERR(leaf)) {
		ret = PTR_ERR(leaf);
		goto fail;
	}

	bytenr = leaf->start;
	memset_extent_buffer(leaf, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_bytenr(leaf, leaf->start);
	btrfs_set_header_generation(leaf, trans->transid);
	btrfs_set_header_backref_rev(leaf, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(leaf, objectid);
	root->node = leaf;

	write_extent_buffer(leaf, fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(leaf),
			    BTRFS_FSID_SIZE);
	write_extent_buffer(leaf, fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(leaf),
			    BTRFS_UUID_SIZE);
	btrfs_mark_buffer_dirty(leaf);

	root->commit_root = btrfs_root_node(root);
	root->track_dirty = 1;


	root->root_item.flags = 0;
	root->root_item.byte_limit = 0;
	btrfs_set_root_bytenr(&root->root_item, leaf->start);
	btrfs_set_root_generation(&root->root_item, trans->transid);
	btrfs_set_root_level(&root->root_item, 0);
	btrfs_set_root_refs(&root->root_item, 1);
	btrfs_set_root_used(&root->root_item, leaf->len);
	btrfs_set_root_last_snapshot(&root->root_item, 0);
	btrfs_set_root_dirid(&root->root_item, 0);
	root->root_item.drop_level = 0;

	key.objectid = objectid;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = 0;
	ret = btrfs_insert_root(trans, tree_root, &key, &root->root_item);
	if (ret)
		goto fail;

	btrfs_tree_unlock(leaf);

fail:
	if (ret)
		return ERR_PTR(ret);

	return root;
}

static struct btrfs_root *alloc_log_tree(struct btrfs_trans_handle *trans,
					 struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct extent_buffer *leaf;

	root = btrfs_alloc_root(fs_info);
	if (!root)
		return ERR_PTR(-ENOMEM);

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, BTRFS_TREE_LOG_OBJECTID);

	root->root_key.objectid = BTRFS_TREE_LOG_OBJECTID;
	root->root_key.type = BTRFS_ROOT_ITEM_KEY;
	root->root_key.offset = BTRFS_TREE_LOG_OBJECTID;
	/*
	 * log trees do not get reference counted because they go away
	 * before a real commit is actually done.  They do store pointers
	 * to file data extents, and those reference counts still get
	 * updated (along with back refs to the log tree).
	 */
	root->ref_cows = 0;

	leaf = btrfs_alloc_free_block(trans, root, root->leafsize, 0,
				      BTRFS_TREE_LOG_OBJECTID, NULL,
				      0, 0, 0);
	if (IS_ERR(leaf)) {
		kfree(root);
		return ERR_CAST(leaf);
	}

	memset_extent_buffer(leaf, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_bytenr(leaf, leaf->start);
	btrfs_set_header_generation(leaf, trans->transid);
	btrfs_set_header_backref_rev(leaf, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(leaf, BTRFS_TREE_LOG_OBJECTID);
	root->node = leaf;

	write_extent_buffer(root->node, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(root->node),
			    BTRFS_FSID_SIZE);
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
	struct btrfs_root *log_root;
	struct btrfs_inode_item *inode_item;

	log_root = alloc_log_tree(trans, root->fs_info);
	if (IS_ERR(log_root))
		return PTR_ERR(log_root);

	log_root->last_trans = trans->transid;
	log_root->root_key.offset = root->root_key.objectid;

	inode_item = &log_root->root_item.inode;
	inode_item->generation = cpu_to_le64(1);
	inode_item->size = cpu_to_le64(3);
	inode_item->nlink = cpu_to_le32(1);
	inode_item->nbytes = cpu_to_le64(root->leafsize);
	inode_item->mode = cpu_to_le32(S_IFDIR | 0755);

	btrfs_set_root_node(&log_root->root_item, log_root->node);

	WARN_ON(root->log_root);
	root->log_root = log_root;
	root->log_transid = 0;
	root->last_log_commit = 0;
	return 0;
}

struct btrfs_root *btrfs_read_fs_root_no_radix(struct btrfs_root *tree_root,
					       struct btrfs_key *location)
{
	struct btrfs_root *root;
	struct btrfs_fs_info *fs_info = tree_root->fs_info;
	struct btrfs_path *path;
	struct extent_buffer *l;
	u64 generation;
	u32 blocksize;
	int ret = 0;
	int slot;

	root = btrfs_alloc_root(fs_info);
	if (!root)
		return ERR_PTR(-ENOMEM);
	if (location->offset == (u64)-1) {
		ret = find_and_setup_root(tree_root, fs_info,
					  location->objectid, root);
		if (ret) {
			kfree(root);
			return ERR_PTR(ret);
		}
		goto out;
	}

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, location->objectid);

	path = btrfs_alloc_path();
	if (!path) {
		kfree(root);
		return ERR_PTR(-ENOMEM);
	}
	ret = btrfs_search_slot(NULL, tree_root, location, path, 0, 0);
	if (ret == 0) {
		l = path->nodes[0];
		slot = path->slots[0];
		btrfs_read_root_item(tree_root, l, slot, &root->root_item);
		memcpy(&root->root_key, location, sizeof(*location));
	}
	btrfs_free_path(path);
	if (ret) {
		kfree(root);
		if (ret > 0)
			ret = -ENOENT;
		return ERR_PTR(ret);
	}

	generation = btrfs_root_generation(&root->root_item);
	blocksize = btrfs_level_size(root, btrfs_root_level(&root->root_item));
	root->node = read_tree_block(root, btrfs_root_bytenr(&root->root_item),
				     blocksize, generation);
	root->commit_root = btrfs_root_node(root);
	BUG_ON(!root->node); /* -ENOMEM */
out:
	if (location->objectid != BTRFS_TREE_LOG_OBJECTID) {
		root->ref_cows = 1;
		btrfs_check_and_init_root_item(&root->root_item);
	}

	return root;
}

struct btrfs_root *btrfs_read_fs_root_no_name(struct btrfs_fs_info *fs_info,
					      struct btrfs_key *location)
{
	struct btrfs_root *root;
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
again:
	spin_lock(&fs_info->fs_roots_radix_lock);
	root = radix_tree_lookup(&fs_info->fs_roots_radix,
				 (unsigned long)location->objectid);
	spin_unlock(&fs_info->fs_roots_radix_lock);
	if (root)
		return root;

	root = btrfs_read_fs_root_no_radix(fs_info->tree_root, location);
	if (IS_ERR(root))
		return root;

	root->free_ino_ctl = kzalloc(sizeof(*root->free_ino_ctl), GFP_NOFS);
	root->free_ino_pinned = kzalloc(sizeof(*root->free_ino_pinned),
					GFP_NOFS);
	if (!root->free_ino_pinned || !root->free_ino_ctl) {
		ret = -ENOMEM;
		goto fail;
	}

	btrfs_init_free_ino_ctl(root);
	mutex_init(&root->fs_commit_mutex);
	spin_lock_init(&root->cache_lock);
	init_waitqueue_head(&root->cache_wait);

	ret = get_anon_bdev(&root->anon_dev);
	if (ret)
		goto fail;

	if (btrfs_root_refs(&root->root_item) == 0) {
		ret = -ENOENT;
		goto fail;
	}

	ret = btrfs_find_orphan_item(fs_info->tree_root, location->objectid);
	if (ret < 0)
		goto fail;
	if (ret == 0)
		root->orphan_item_inserted = 1;

	ret = radix_tree_preload(GFP_NOFS & ~__GFP_HIGHMEM);
	if (ret)
		goto fail;

	spin_lock(&fs_info->fs_roots_radix_lock);
	ret = radix_tree_insert(&fs_info->fs_roots_radix,
				(unsigned long)root->root_key.objectid,
				root);
	if (ret == 0)
		root->in_radix = 1;

	spin_unlock(&fs_info->fs_roots_radix_lock);
	radix_tree_preload_end();
	if (ret) {
		if (ret == -EEXIST) {
			free_fs_root(root);
			goto again;
		}
		goto fail;
	}

	ret = btrfs_find_dead_roots(fs_info->tree_root,
				    root->root_key.objectid);
	WARN_ON(ret);
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
		bdi = blk_get_backing_dev_info(device->bdev);
		if (bdi && bdi_congested(bdi, bdi_bits)) {
			ret = 1;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

/*
 * If this fails, caller must call bdi_destroy() to get rid of the
 * bdi again.
 */
static int setup_bdi(struct btrfs_fs_info *info, struct backing_dev_info *bdi)
{
	int err;

	bdi->capabilities = BDI_CAP_MAP_COPY;
	err = bdi_setup_and_register(bdi, "btrfs", BDI_CAP_MAP_COPY);
	if (err)
		return err;

	bdi->ra_pages	= default_backing_dev_info.ra_pages;
	bdi->congested_fn	= btrfs_congested_fn;
	bdi->congested_data	= info;
	return 0;
}

/*
 * called by the kthread helper functions to finally call the bio end_io
 * functions.  This is where read checksum verification actually happens
 */
static void end_workqueue_fn(struct btrfs_work *work)
{
	struct bio *bio;
	struct end_io_wq *end_io_wq;
	struct btrfs_fs_info *fs_info;
	int error;

	end_io_wq = container_of(work, struct end_io_wq, work);
	bio = end_io_wq->bio;
	fs_info = end_io_wq->info;

	error = end_io_wq->error;
	bio->bi_private = end_io_wq->private;
	bio->bi_end_io = end_io_wq->end_io;
	kfree(end_io_wq);
	bio_endio(bio, error);
}

static int cleaner_kthread(void *arg)
{
	struct btrfs_root *root = arg;

	do {
		if (!(root->fs_info->sb->s_flags & MS_RDONLY) &&
		    mutex_trylock(&root->fs_info->cleaner_mutex)) {
			btrfs_run_delayed_iputs(root);
			btrfs_clean_old_snapshots(root);
			mutex_unlock(&root->fs_info->cleaner_mutex);
			btrfs_run_defrag_inodes(root->fs_info);
		}

		if (!try_to_freeze()) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop())
				schedule();
			__set_current_state(TASK_RUNNING);
		}
	} while (!kthread_should_stop());
	return 0;
}

static int transaction_kthread(void *arg)
{
	struct btrfs_root *root = arg;
	struct btrfs_trans_handle *trans;
	struct btrfs_transaction *cur;
	u64 transid;
	unsigned long now;
	unsigned long delay;
	bool cannot_commit;

	do {
		cannot_commit = false;
		delay = HZ * 30;
		mutex_lock(&root->fs_info->transaction_kthread_mutex);

		spin_lock(&root->fs_info->trans_lock);
		cur = root->fs_info->running_transaction;
		if (!cur) {
			spin_unlock(&root->fs_info->trans_lock);
			goto sleep;
		}

		now = get_seconds();
		if (!cur->blocked &&
		    (now < cur->start_time || now - cur->start_time < 30)) {
			spin_unlock(&root->fs_info->trans_lock);
			delay = HZ * 5;
			goto sleep;
		}
		transid = cur->transid;
		spin_unlock(&root->fs_info->trans_lock);

		/* If the file system is aborted, this will always fail. */
		trans = btrfs_attach_transaction(root);
		if (IS_ERR(trans)) {
			if (PTR_ERR(trans) != -ENOENT)
				cannot_commit = true;
			goto sleep;
		}
		if (transid == trans->transid) {
			btrfs_commit_transaction(trans, root);
		} else {
			btrfs_end_transaction(trans, root);
		}
sleep:
		wake_up_process(root->fs_info->cleaner_kthread);
		mutex_unlock(&root->fs_info->transaction_kthread_mutex);

		if (!try_to_freeze()) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop() &&
			    (!btrfs_transaction_blocked(root->fs_info) ||
			     cannot_commit))
				schedule_timeout(delay);
			__set_current_state(TASK_RUNNING);
		}
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

/* helper to cleanup tree roots */
static void free_root_pointers(struct btrfs_fs_info *info, int chunk_root)
{
	free_extent_buffer(info->tree_root->node);
	free_extent_buffer(info->tree_root->commit_root);
	free_extent_buffer(info->dev_root->node);
	free_extent_buffer(info->dev_root->commit_root);
	free_extent_buffer(info->extent_root->node);
	free_extent_buffer(info->extent_root->commit_root);
	free_extent_buffer(info->csum_root->node);
	free_extent_buffer(info->csum_root->commit_root);
	if (info->quota_root) {
		free_extent_buffer(info->quota_root->node);
		free_extent_buffer(info->quota_root->commit_root);
	}

	info->tree_root->node = NULL;
	info->tree_root->commit_root = NULL;
	info->dev_root->node = NULL;
	info->dev_root->commit_root = NULL;
	info->extent_root->node = NULL;
	info->extent_root->commit_root = NULL;
	info->csum_root->node = NULL;
	info->csum_root->commit_root = NULL;
	if (info->quota_root) {
		info->quota_root->node = NULL;
		info->quota_root->commit_root = NULL;
	}

	if (chunk_root) {
		free_extent_buffer(info->chunk_root->node);
		free_extent_buffer(info->chunk_root->commit_root);
		info->chunk_root->node = NULL;
		info->chunk_root->commit_root = NULL;
	}
}


int open_ctree(struct super_block *sb,
	       struct btrfs_fs_devices *fs_devices,
	       char *options)
{
	u32 sectorsize;
	u32 nodesize;
	u32 leafsize;
	u32 blocksize;
	u32 stripesize;
	u64 generation;
	u64 features;
	struct btrfs_key location;
	struct buffer_head *bh;
	struct btrfs_super_block *disk_super;
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_root *tree_root;
	struct btrfs_root *extent_root;
	struct btrfs_root *csum_root;
	struct btrfs_root *chunk_root;
	struct btrfs_root *dev_root;
	struct btrfs_root *quota_root;
	struct btrfs_root *log_tree_root;
	int ret;
	int err = -EINVAL;
	int num_backups_tried = 0;
	int backup_index = 0;

	tree_root = fs_info->tree_root = btrfs_alloc_root(fs_info);
	extent_root = fs_info->extent_root = btrfs_alloc_root(fs_info);
	csum_root = fs_info->csum_root = btrfs_alloc_root(fs_info);
	chunk_root = fs_info->chunk_root = btrfs_alloc_root(fs_info);
	dev_root = fs_info->dev_root = btrfs_alloc_root(fs_info);
	quota_root = fs_info->quota_root = btrfs_alloc_root(fs_info);

	if (!tree_root || !extent_root || !csum_root ||
	    !chunk_root || !dev_root || !quota_root) {
		err = -ENOMEM;
		goto fail;
	}

	ret = init_srcu_struct(&fs_info->subvol_srcu);
	if (ret) {
		err = ret;
		goto fail;
	}

	ret = setup_bdi(fs_info, &fs_info->bdi);
	if (ret) {
		err = ret;
		goto fail_srcu;
	}

	ret = percpu_counter_init(&fs_info->dirty_metadata_bytes, 0);
	if (ret) {
		err = ret;
		goto fail_bdi;
	}
	fs_info->dirty_metadata_batch = PAGE_CACHE_SIZE *
					(1 + ilog2(nr_cpu_ids));

	ret = percpu_counter_init(&fs_info->delalloc_bytes, 0);
	if (ret) {
		err = ret;
		goto fail_dirty_metadata_bytes;
	}

	fs_info->btree_inode = new_inode(sb);
	if (!fs_info->btree_inode) {
		err = -ENOMEM;
		goto fail_delalloc_bytes;
	}

	mapping_set_gfp_mask(fs_info->btree_inode->i_mapping, GFP_NOFS);

	INIT_RADIX_TREE(&fs_info->fs_roots_radix, GFP_ATOMIC);
	INIT_LIST_HEAD(&fs_info->trans_list);
	INIT_LIST_HEAD(&fs_info->dead_roots);
	INIT_LIST_HEAD(&fs_info->delayed_iputs);
	INIT_LIST_HEAD(&fs_info->delalloc_inodes);
	INIT_LIST_HEAD(&fs_info->caching_block_groups);
	spin_lock_init(&fs_info->delalloc_lock);
	spin_lock_init(&fs_info->trans_lock);
	spin_lock_init(&fs_info->fs_roots_radix_lock);
	spin_lock_init(&fs_info->delayed_iput_lock);
	spin_lock_init(&fs_info->defrag_inodes_lock);
	spin_lock_init(&fs_info->free_chunk_lock);
	spin_lock_init(&fs_info->tree_mod_seq_lock);
	rwlock_init(&fs_info->tree_mod_log_lock);
	mutex_init(&fs_info->reloc_mutex);
	seqlock_init(&fs_info->profiles_lock);

	init_completion(&fs_info->kobj_unregister);
	INIT_LIST_HEAD(&fs_info->dirty_cowonly_roots);
	INIT_LIST_HEAD(&fs_info->space_info);
	INIT_LIST_HEAD(&fs_info->tree_mod_seq_list);
	btrfs_mapping_init(&fs_info->mapping_tree);
	btrfs_init_block_rsv(&fs_info->global_block_rsv,
			     BTRFS_BLOCK_RSV_GLOBAL);
	btrfs_init_block_rsv(&fs_info->delalloc_block_rsv,
			     BTRFS_BLOCK_RSV_DELALLOC);
	btrfs_init_block_rsv(&fs_info->trans_block_rsv, BTRFS_BLOCK_RSV_TRANS);
	btrfs_init_block_rsv(&fs_info->chunk_block_rsv, BTRFS_BLOCK_RSV_CHUNK);
	btrfs_init_block_rsv(&fs_info->empty_block_rsv, BTRFS_BLOCK_RSV_EMPTY);
	btrfs_init_block_rsv(&fs_info->delayed_block_rsv,
			     BTRFS_BLOCK_RSV_DELOPS);
	atomic_set(&fs_info->nr_async_submits, 0);
	atomic_set(&fs_info->async_delalloc_pages, 0);
	atomic_set(&fs_info->async_submit_draining, 0);
	atomic_set(&fs_info->nr_async_bios, 0);
	atomic_set(&fs_info->defrag_running, 0);
	atomic_set(&fs_info->tree_mod_seq, 0);
	fs_info->sb = sb;
	fs_info->max_inline = 8192 * 1024;
	fs_info->metadata_ratio = 0;
	fs_info->defrag_inodes = RB_ROOT;
	fs_info->trans_no_join = 0;
	fs_info->free_chunk_space = 0;
	fs_info->tree_mod_log = RB_ROOT;

	/* readahead state */
	INIT_RADIX_TREE(&fs_info->reada_tree, GFP_NOFS & ~__GFP_WAIT);
	spin_lock_init(&fs_info->reada_lock);

	fs_info->thread_pool_size = min_t(unsigned long,
					  num_online_cpus() + 2, 8);

	INIT_LIST_HEAD(&fs_info->ordered_extents);
	spin_lock_init(&fs_info->ordered_extent_lock);
	fs_info->delayed_root = kmalloc(sizeof(struct btrfs_delayed_root),
					GFP_NOFS);
	if (!fs_info->delayed_root) {
		err = -ENOMEM;
		goto fail_iput;
	}
	btrfs_init_delayed_root(fs_info->delayed_root);

	mutex_init(&fs_info->scrub_lock);
	atomic_set(&fs_info->scrubs_running, 0);
	atomic_set(&fs_info->scrub_pause_req, 0);
	atomic_set(&fs_info->scrubs_paused, 0);
	atomic_set(&fs_info->scrub_cancel_req, 0);
	init_waitqueue_head(&fs_info->scrub_pause_wait);
	init_rwsem(&fs_info->scrub_super_lock);
	fs_info->scrub_workers_refcnt = 0;
#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
	fs_info->check_integrity_print_mask = 0;
#endif

	spin_lock_init(&fs_info->balance_lock);
	mutex_init(&fs_info->balance_mutex);
	atomic_set(&fs_info->balance_running, 0);
	atomic_set(&fs_info->balance_pause_req, 0);
	atomic_set(&fs_info->balance_cancel_req, 0);
	fs_info->balance_ctl = NULL;
	init_waitqueue_head(&fs_info->balance_wait_q);

	sb->s_blocksize = 4096;
	sb->s_blocksize_bits = blksize_bits(4096);
	sb->s_bdi = &fs_info->bdi;

	fs_info->btree_inode->i_ino = BTRFS_BTREE_INODE_OBJECTID;
	set_nlink(fs_info->btree_inode, 1);
	/*
	 * we set the i_size on the btree inode to the max possible int.
	 * the real end of the address space is determined by all of
	 * the devices in the system
	 */
	fs_info->btree_inode->i_size = OFFSET_MAX;
	fs_info->btree_inode->i_mapping->a_ops = &btree_aops;
	fs_info->btree_inode->i_mapping->backing_dev_info = &fs_info->bdi;

	RB_CLEAR_NODE(&BTRFS_I(fs_info->btree_inode)->rb_node);
	extent_io_tree_init(&BTRFS_I(fs_info->btree_inode)->io_tree,
			     fs_info->btree_inode->i_mapping);
	BTRFS_I(fs_info->btree_inode)->io_tree.track_uptodate = 0;
	extent_map_tree_init(&BTRFS_I(fs_info->btree_inode)->extent_tree);

	BTRFS_I(fs_info->btree_inode)->io_tree.ops = &btree_extent_io_ops;

	BTRFS_I(fs_info->btree_inode)->root = tree_root;
	memset(&BTRFS_I(fs_info->btree_inode)->location, 0,
	       sizeof(struct btrfs_key));
	set_bit(BTRFS_INODE_DUMMY,
		&BTRFS_I(fs_info->btree_inode)->runtime_flags);
	insert_inode_hash(fs_info->btree_inode);

	spin_lock_init(&fs_info->block_group_cache_lock);
	fs_info->block_group_cache_tree = RB_ROOT;
	fs_info->first_logical_byte = (u64)-1;

	extent_io_tree_init(&fs_info->freed_extents[0],
			     fs_info->btree_inode->i_mapping);
	extent_io_tree_init(&fs_info->freed_extents[1],
			     fs_info->btree_inode->i_mapping);
	fs_info->pinned_extents = &fs_info->freed_extents[0];
	fs_info->do_barriers = 1;


	mutex_init(&fs_info->ordered_operations_mutex);
	mutex_init(&fs_info->tree_log_mutex);
	mutex_init(&fs_info->chunk_mutex);
	mutex_init(&fs_info->transaction_kthread_mutex);
	mutex_init(&fs_info->cleaner_mutex);
	mutex_init(&fs_info->volume_mutex);
	init_rwsem(&fs_info->extent_commit_sem);
	init_rwsem(&fs_info->cleanup_work_sem);
	init_rwsem(&fs_info->subvol_sem);
	fs_info->dev_replace.lock_owner = 0;
	atomic_set(&fs_info->dev_replace.nesting_level, 0);
	mutex_init(&fs_info->dev_replace.lock_finishing_cancel_unmount);
	mutex_init(&fs_info->dev_replace.lock_management_lock);
	mutex_init(&fs_info->dev_replace.lock);

	spin_lock_init(&fs_info->qgroup_lock);
	fs_info->qgroup_tree = RB_ROOT;
	INIT_LIST_HEAD(&fs_info->dirty_qgroups);
	fs_info->qgroup_seq = 1;
	fs_info->quota_enabled = 0;
	fs_info->pending_quota_state = 0;

	btrfs_init_free_cluster(&fs_info->meta_alloc_cluster);
	btrfs_init_free_cluster(&fs_info->data_alloc_cluster);

	init_waitqueue_head(&fs_info->transaction_throttle);
	init_waitqueue_head(&fs_info->transaction_wait);
	init_waitqueue_head(&fs_info->transaction_blocked_wait);
	init_waitqueue_head(&fs_info->async_submit_wait);

	ret = btrfs_alloc_stripe_hash_table(fs_info);
	if (ret) {
		err = ret;
		goto fail_alloc;
	}

	__setup_root(4096, 4096, 4096, 4096, tree_root,
		     fs_info, BTRFS_ROOT_TREE_OBJECTID);

	invalidate_bdev(fs_devices->latest_bdev);
	bh = btrfs_read_dev_super(fs_devices->latest_bdev);
	if (!bh) {
		err = -EINVAL;
		goto fail_alloc;
	}

	memcpy(fs_info->super_copy, bh->b_data, sizeof(*fs_info->super_copy));
	memcpy(fs_info->super_for_commit, fs_info->super_copy,
	       sizeof(*fs_info->super_for_commit));
	brelse(bh);

	memcpy(fs_info->fsid, fs_info->super_copy->fsid, BTRFS_FSID_SIZE);

	disk_super = fs_info->super_copy;
	if (!btrfs_super_root(disk_super))
		goto fail_alloc;

	/* check FS state, whether FS is broken. */
	if (btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_ERROR)
		set_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state);

	ret = btrfs_check_super_valid(fs_info, sb->s_flags & MS_RDONLY);
	if (ret) {
		printk(KERN_ERR "btrfs: superblock contains fatal errors\n");
		err = ret;
		goto fail_alloc;
	}

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

	ret = btrfs_parse_options(tree_root, options);
	if (ret) {
		err = ret;
		goto fail_alloc;
	}

	features = btrfs_super_incompat_flags(disk_super) &
		~BTRFS_FEATURE_INCOMPAT_SUPP;
	if (features) {
		printk(KERN_ERR "BTRFS: couldn't mount because of "
		       "unsupported optional features (%Lx).\n",
		       (unsigned long long)features);
		err = -EINVAL;
		goto fail_alloc;
	}

	if (btrfs_super_leafsize(disk_super) !=
	    btrfs_super_nodesize(disk_super)) {
		printk(KERN_ERR "BTRFS: couldn't mount because metadata "
		       "blocksizes don't match.  node %d leaf %d\n",
		       btrfs_super_nodesize(disk_super),
		       btrfs_super_leafsize(disk_super));
		err = -EINVAL;
		goto fail_alloc;
	}
	if (btrfs_super_leafsize(disk_super) > BTRFS_MAX_METADATA_BLOCKSIZE) {
		printk(KERN_ERR "BTRFS: couldn't mount because metadata "
		       "blocksize (%d) was too large\n",
		       btrfs_super_leafsize(disk_super));
		err = -EINVAL;
		goto fail_alloc;
	}

	features = btrfs_super_incompat_flags(disk_super);
	features |= BTRFS_FEATURE_INCOMPAT_MIXED_BACKREF;
	if (tree_root->fs_info->compress_type == BTRFS_COMPRESS_LZO)
		features |= BTRFS_FEATURE_INCOMPAT_COMPRESS_LZO;

	/*
	 * flag our filesystem as having big metadata blocks if
	 * they are bigger than the page size
	 */
	if (btrfs_super_leafsize(disk_super) > PAGE_CACHE_SIZE) {
		if (!(features & BTRFS_FEATURE_INCOMPAT_BIG_METADATA))
			printk(KERN_INFO "btrfs flagging fs with big metadata feature\n");
		features |= BTRFS_FEATURE_INCOMPAT_BIG_METADATA;
	}

	nodesize = btrfs_super_nodesize(disk_super);
	leafsize = btrfs_super_leafsize(disk_super);
	sectorsize = btrfs_super_sectorsize(disk_super);
	stripesize = btrfs_super_stripesize(disk_super);
	fs_info->dirty_metadata_batch = leafsize * (1 + ilog2(nr_cpu_ids));
	fs_info->delalloc_batch = sectorsize * 512 * (1 + ilog2(nr_cpu_ids));

	/*
	 * mixed block groups end up with duplicate but slightly offset
	 * extent buffers for the same range.  It leads to corruptions
	 */
	if ((features & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS) &&
	    (sectorsize != leafsize)) {
		printk(KERN_WARNING "btrfs: unequal leaf/node/sector sizes "
				"are not allowed for mixed block groups on %s\n",
				sb->s_id);
		goto fail_alloc;
	}

	btrfs_set_super_incompat_flags(disk_super, features);

	features = btrfs_super_compat_ro_flags(disk_super) &
		~BTRFS_FEATURE_COMPAT_RO_SUPP;
	if (!(sb->s_flags & MS_RDONLY) && features) {
		printk(KERN_ERR "BTRFS: couldn't mount RDWR because of "
		       "unsupported option features (%Lx).\n",
		       (unsigned long long)features);
		err = -EINVAL;
		goto fail_alloc;
	}

	btrfs_init_workers(&fs_info->generic_worker,
			   "genwork", 1, NULL);

	btrfs_init_workers(&fs_info->workers, "worker",
			   fs_info->thread_pool_size,
			   &fs_info->generic_worker);

	btrfs_init_workers(&fs_info->delalloc_workers, "delalloc",
			   fs_info->thread_pool_size,
			   &fs_info->generic_worker);

	btrfs_init_workers(&fs_info->flush_workers, "flush_delalloc",
			   fs_info->thread_pool_size,
			   &fs_info->generic_worker);

	btrfs_init_workers(&fs_info->submit_workers, "submit",
			   min_t(u64, fs_devices->num_devices,
			   fs_info->thread_pool_size),
			   &fs_info->generic_worker);

	btrfs_init_workers(&fs_info->caching_workers, "cache",
			   2, &fs_info->generic_worker);

	/* a higher idle thresh on the submit workers makes it much more
	 * likely that bios will be send down in a sane order to the
	 * devices
	 */
	fs_info->submit_workers.idle_thresh = 64;

	fs_info->workers.idle_thresh = 16;
	fs_info->workers.ordered = 1;

	fs_info->delalloc_workers.idle_thresh = 2;
	fs_info->delalloc_workers.ordered = 1;

	btrfs_init_workers(&fs_info->fixup_workers, "fixup", 1,
			   &fs_info->generic_worker);
	btrfs_init_workers(&fs_info->endio_workers, "endio",
			   fs_info->thread_pool_size,
			   &fs_info->generic_worker);
	btrfs_init_workers(&fs_info->endio_meta_workers, "endio-meta",
			   fs_info->thread_pool_size,
			   &fs_info->generic_worker);
	btrfs_init_workers(&fs_info->endio_meta_write_workers,
			   "endio-meta-write", fs_info->thread_pool_size,
			   &fs_info->generic_worker);
	btrfs_init_workers(&fs_info->endio_raid56_workers,
			   "endio-raid56", fs_info->thread_pool_size,
			   &fs_info->generic_worker);
	btrfs_init_workers(&fs_info->rmw_workers,
			   "rmw", fs_info->thread_pool_size,
			   &fs_info->generic_worker);
	btrfs_init_workers(&fs_info->endio_write_workers, "endio-write",
			   fs_info->thread_pool_size,
			   &fs_info->generic_worker);
	btrfs_init_workers(&fs_info->endio_freespace_worker, "freespace-write",
			   1, &fs_info->generic_worker);
	btrfs_init_workers(&fs_info->delayed_workers, "delayed-meta",
			   fs_info->thread_pool_size,
			   &fs_info->generic_worker);
	btrfs_init_workers(&fs_info->readahead_workers, "readahead",
			   fs_info->thread_pool_size,
			   &fs_info->generic_worker);

	/*
	 * endios are largely parallel and should have a very
	 * low idle thresh
	 */
	fs_info->endio_workers.idle_thresh = 4;
	fs_info->endio_meta_workers.idle_thresh = 4;
	fs_info->endio_raid56_workers.idle_thresh = 4;
	fs_info->rmw_workers.idle_thresh = 2;

	fs_info->endio_write_workers.idle_thresh = 2;
	fs_info->endio_meta_write_workers.idle_thresh = 2;
	fs_info->readahead_workers.idle_thresh = 2;

	/*
	 * btrfs_start_workers can really only fail because of ENOMEM so just
	 * return -ENOMEM if any of these fail.
	 */
	ret = btrfs_start_workers(&fs_info->workers);
	ret |= btrfs_start_workers(&fs_info->generic_worker);
	ret |= btrfs_start_workers(&fs_info->submit_workers);
	ret |= btrfs_start_workers(&fs_info->delalloc_workers);
	ret |= btrfs_start_workers(&fs_info->fixup_workers);
	ret |= btrfs_start_workers(&fs_info->endio_workers);
	ret |= btrfs_start_workers(&fs_info->endio_meta_workers);
	ret |= btrfs_start_workers(&fs_info->rmw_workers);
	ret |= btrfs_start_workers(&fs_info->endio_raid56_workers);
	ret |= btrfs_start_workers(&fs_info->endio_meta_write_workers);
	ret |= btrfs_start_workers(&fs_info->endio_write_workers);
	ret |= btrfs_start_workers(&fs_info->endio_freespace_worker);
	ret |= btrfs_start_workers(&fs_info->delayed_workers);
	ret |= btrfs_start_workers(&fs_info->caching_workers);
	ret |= btrfs_start_workers(&fs_info->readahead_workers);
	ret |= btrfs_start_workers(&fs_info->flush_workers);
	if (ret) {
		err = -ENOMEM;
		goto fail_sb_buffer;
	}

	fs_info->bdi.ra_pages *= btrfs_super_num_devices(disk_super);
	fs_info->bdi.ra_pages = max(fs_info->bdi.ra_pages,
				    4 * 1024 * 1024 / PAGE_CACHE_SIZE);

	tree_root->nodesize = nodesize;
	tree_root->leafsize = leafsize;
	tree_root->sectorsize = sectorsize;
	tree_root->stripesize = stripesize;

	sb->s_blocksize = sectorsize;
	sb->s_blocksize_bits = blksize_bits(sectorsize);

	if (disk_super->magic != cpu_to_le64(BTRFS_MAGIC)) {
		printk(KERN_INFO "btrfs: valid FS not found on %s\n", sb->s_id);
		goto fail_sb_buffer;
	}

	if (sectorsize != PAGE_SIZE) {
		printk(KERN_WARNING "btrfs: Incompatible sector size(%lu) "
		       "found on %s\n", (unsigned long)sectorsize, sb->s_id);
		goto fail_sb_buffer;
	}

	mutex_lock(&fs_info->chunk_mutex);
	ret = btrfs_read_sys_array(tree_root);
	mutex_unlock(&fs_info->chunk_mutex);
	if (ret) {
		printk(KERN_WARNING "btrfs: failed to read the system "
		       "array on %s\n", sb->s_id);
		goto fail_sb_buffer;
	}

	blocksize = btrfs_level_size(tree_root,
				     btrfs_super_chunk_root_level(disk_super));
	generation = btrfs_super_chunk_root_generation(disk_super);

	__setup_root(nodesize, leafsize, sectorsize, stripesize,
		     chunk_root, fs_info, BTRFS_CHUNK_TREE_OBJECTID);

	chunk_root->node = read_tree_block(chunk_root,
					   btrfs_super_chunk_root(disk_super),
					   blocksize, generation);
	BUG_ON(!chunk_root->node); /* -ENOMEM */
	if (!test_bit(EXTENT_BUFFER_UPTODATE, &chunk_root->node->bflags)) {
		printk(KERN_WARNING "btrfs: failed to read chunk root on %s\n",
		       sb->s_id);
		goto fail_tree_roots;
	}
	btrfs_set_root_node(&chunk_root->root_item, chunk_root->node);
	chunk_root->commit_root = btrfs_root_node(chunk_root);

	read_extent_buffer(chunk_root->node, fs_info->chunk_tree_uuid,
	   (unsigned long)btrfs_header_chunk_tree_uuid(chunk_root->node),
	   BTRFS_UUID_SIZE);

	ret = btrfs_read_chunk_tree(chunk_root);
	if (ret) {
		printk(KERN_WARNING "btrfs: failed to read chunk tree on %s\n",
		       sb->s_id);
		goto fail_tree_roots;
	}

	/*
	 * keep the device that is marked to be the target device for the
	 * dev_replace procedure
	 */
	btrfs_close_extra_devices(fs_info, fs_devices, 0);

	if (!fs_devices->latest_bdev) {
		printk(KERN_CRIT "btrfs: failed to read devices on %s\n",
		       sb->s_id);
		goto fail_tree_roots;
	}

retry_root_backup:
	blocksize = btrfs_level_size(tree_root,
				     btrfs_super_root_level(disk_super));
	generation = btrfs_super_generation(disk_super);

	tree_root->node = read_tree_block(tree_root,
					  btrfs_super_root(disk_super),
					  blocksize, generation);
	if (!tree_root->node ||
	    !test_bit(EXTENT_BUFFER_UPTODATE, &tree_root->node->bflags)) {
		printk(KERN_WARNING "btrfs: failed to read tree root on %s\n",
		       sb->s_id);

		goto recovery_tree_root;
	}

	btrfs_set_root_node(&tree_root->root_item, tree_root->node);
	tree_root->commit_root = btrfs_root_node(tree_root);

	ret = find_and_setup_root(tree_root, fs_info,
				  BTRFS_EXTENT_TREE_OBJECTID, extent_root);
	if (ret)
		goto recovery_tree_root;
	extent_root->track_dirty = 1;

	ret = find_and_setup_root(tree_root, fs_info,
				  BTRFS_DEV_TREE_OBJECTID, dev_root);
	if (ret)
		goto recovery_tree_root;
	dev_root->track_dirty = 1;

	ret = find_and_setup_root(tree_root, fs_info,
				  BTRFS_CSUM_TREE_OBJECTID, csum_root);
	if (ret)
		goto recovery_tree_root;
	csum_root->track_dirty = 1;

	ret = find_and_setup_root(tree_root, fs_info,
				  BTRFS_QUOTA_TREE_OBJECTID, quota_root);
	if (ret) {
		kfree(quota_root);
		quota_root = fs_info->quota_root = NULL;
	} else {
		quota_root->track_dirty = 1;
		fs_info->quota_enabled = 1;
		fs_info->pending_quota_state = 1;
	}

	fs_info->generation = generation;
	fs_info->last_trans_committed = generation;

	ret = btrfs_recover_balance(fs_info);
	if (ret) {
		printk(KERN_WARNING "btrfs: failed to recover balance\n");
		goto fail_block_groups;
	}

	ret = btrfs_init_dev_stats(fs_info);
	if (ret) {
		printk(KERN_ERR "btrfs: failed to init dev_stats: %d\n",
		       ret);
		goto fail_block_groups;
	}

	ret = btrfs_init_dev_replace(fs_info);
	if (ret) {
		pr_err("btrfs: failed to init dev_replace: %d\n", ret);
		goto fail_block_groups;
	}

	btrfs_close_extra_devices(fs_info, fs_devices, 1);

	ret = btrfs_init_space_info(fs_info);
	if (ret) {
		printk(KERN_ERR "Failed to initial space info: %d\n", ret);
		goto fail_block_groups;
	}

	ret = btrfs_read_block_groups(extent_root);
	if (ret) {
		printk(KERN_ERR "Failed to read block groups: %d\n", ret);
		goto fail_block_groups;
	}
	fs_info->num_tolerated_disk_barrier_failures =
		btrfs_calc_num_tolerated_disk_barrier_failures(fs_info);
	if (fs_info->fs_devices->missing_devices >
	     fs_info->num_tolerated_disk_barrier_failures &&
	    !(sb->s_flags & MS_RDONLY)) {
		printk(KERN_WARNING
		       "Btrfs: too many missing devices, writeable mount is not allowed\n");
		goto fail_block_groups;
	}

	fs_info->cleaner_kthread = kthread_run(cleaner_kthread, tree_root,
					       "btrfs-cleaner");
	if (IS_ERR(fs_info->cleaner_kthread))
		goto fail_block_groups;

	fs_info->transaction_kthread = kthread_run(transaction_kthread,
						   tree_root,
						   "btrfs-transaction");
	if (IS_ERR(fs_info->transaction_kthread))
		goto fail_cleaner;

	if (!btrfs_test_opt(tree_root, SSD) &&
	    !btrfs_test_opt(tree_root, NOSSD) &&
	    !fs_info->fs_devices->rotating) {
		printk(KERN_INFO "Btrfs detected SSD devices, enabling SSD "
		       "mode\n");
		btrfs_set_opt(fs_info->mount_opt, SSD);
	}

#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
	if (btrfs_test_opt(tree_root, CHECK_INTEGRITY)) {
		ret = btrfsic_mount(tree_root, fs_devices,
				    btrfs_test_opt(tree_root,
					CHECK_INTEGRITY_INCLUDING_EXTENT_DATA) ?
				    1 : 0,
				    fs_info->check_integrity_print_mask);
		if (ret)
			printk(KERN_WARNING "btrfs: failed to initialize"
			       " integrity check module %s\n", sb->s_id);
	}
#endif
	ret = btrfs_read_qgroup_config(fs_info);
	if (ret)
		goto fail_trans_kthread;

	/* do not make disk changes in broken FS */
	if (btrfs_super_log_root(disk_super) != 0) {
		u64 bytenr = btrfs_super_log_root(disk_super);

		if (fs_devices->rw_devices == 0) {
			printk(KERN_WARNING "Btrfs log replay required "
			       "on RO media\n");
			err = -EIO;
			goto fail_qgroup;
		}
		blocksize =
		     btrfs_level_size(tree_root,
				      btrfs_super_log_root_level(disk_super));

		log_tree_root = btrfs_alloc_root(fs_info);
		if (!log_tree_root) {
			err = -ENOMEM;
			goto fail_qgroup;
		}

		__setup_root(nodesize, leafsize, sectorsize, stripesize,
			     log_tree_root, fs_info, BTRFS_TREE_LOG_OBJECTID);

		log_tree_root->node = read_tree_block(tree_root, bytenr,
						      blocksize,
						      generation + 1);
		/* returns with log_tree_root freed on success */
		ret = btrfs_recover_log_trees(log_tree_root);
		if (ret) {
			btrfs_error(tree_root->fs_info, ret,
				    "Failed to recover log tree");
			free_extent_buffer(log_tree_root->node);
			kfree(log_tree_root);
			goto fail_trans_kthread;
		}

		if (sb->s_flags & MS_RDONLY) {
			ret = btrfs_commit_super(tree_root);
			if (ret)
				goto fail_trans_kthread;
		}
	}

	ret = btrfs_find_orphan_roots(tree_root);
	if (ret)
		goto fail_trans_kthread;

	if (!(sb->s_flags & MS_RDONLY)) {
		ret = btrfs_cleanup_fs_roots(fs_info);
		if (ret)
			goto fail_trans_kthread;

		ret = btrfs_recover_relocation(tree_root);
		if (ret < 0) {
			printk(KERN_WARNING
			       "btrfs: failed to recover relocation\n");
			err = -EINVAL;
			goto fail_qgroup;
		}
	}

	location.objectid = BTRFS_FS_TREE_OBJECTID;
	location.type = BTRFS_ROOT_ITEM_KEY;
	location.offset = (u64)-1;

	fs_info->fs_root = btrfs_read_fs_root_no_name(fs_info, &location);
	if (!fs_info->fs_root)
		goto fail_qgroup;
	if (IS_ERR(fs_info->fs_root)) {
		err = PTR_ERR(fs_info->fs_root);
		goto fail_qgroup;
	}

	if (sb->s_flags & MS_RDONLY)
		return 0;

	down_read(&fs_info->cleanup_work_sem);
	if ((ret = btrfs_orphan_cleanup(fs_info->fs_root)) ||
	    (ret = btrfs_orphan_cleanup(fs_info->tree_root))) {
		up_read(&fs_info->cleanup_work_sem);
		close_ctree(tree_root);
		return ret;
	}
	up_read(&fs_info->cleanup_work_sem);

	ret = btrfs_resume_balance_async(fs_info);
	if (ret) {
		printk(KERN_WARNING "btrfs: failed to resume balance\n");
		close_ctree(tree_root);
		return ret;
	}

	ret = btrfs_resume_dev_replace_async(fs_info);
	if (ret) {
		pr_warn("btrfs: failed to resume dev_replace\n");
		close_ctree(tree_root);
		return ret;
	}

	return 0;

fail_qgroup:
	btrfs_free_qgroup_config(fs_info);
fail_trans_kthread:
	kthread_stop(fs_info->transaction_kthread);
fail_cleaner:
	kthread_stop(fs_info->cleaner_kthread);

	/*
	 * make sure we're done with the btree inode before we stop our
	 * kthreads
	 */
	filemap_write_and_wait(fs_info->btree_inode->i_mapping);

fail_block_groups:
	btrfs_free_block_groups(fs_info);

fail_tree_roots:
	free_root_pointers(fs_info, 1);
	invalidate_inode_pages2(fs_info->btree_inode->i_mapping);

fail_sb_buffer:
	btrfs_stop_workers(&fs_info->generic_worker);
	btrfs_stop_workers(&fs_info->readahead_workers);
	btrfs_stop_workers(&fs_info->fixup_workers);
	btrfs_stop_workers(&fs_info->delalloc_workers);
	btrfs_stop_workers(&fs_info->workers);
	btrfs_stop_workers(&fs_info->endio_workers);
	btrfs_stop_workers(&fs_info->endio_meta_workers);
	btrfs_stop_workers(&fs_info->endio_raid56_workers);
	btrfs_stop_workers(&fs_info->rmw_workers);
	btrfs_stop_workers(&fs_info->endio_meta_write_workers);
	btrfs_stop_workers(&fs_info->endio_write_workers);
	btrfs_stop_workers(&fs_info->endio_freespace_worker);
	btrfs_stop_workers(&fs_info->submit_workers);
	btrfs_stop_workers(&fs_info->delayed_workers);
	btrfs_stop_workers(&fs_info->caching_workers);
	btrfs_stop_workers(&fs_info->flush_workers);
fail_alloc:
fail_iput:
	btrfs_mapping_tree_free(&fs_info->mapping_tree);

	iput(fs_info->btree_inode);
fail_delalloc_bytes:
	percpu_counter_destroy(&fs_info->delalloc_bytes);
fail_dirty_metadata_bytes:
	percpu_counter_destroy(&fs_info->dirty_metadata_bytes);
fail_bdi:
	bdi_destroy(&fs_info->bdi);
fail_srcu:
	cleanup_srcu_struct(&fs_info->subvol_srcu);
fail:
	btrfs_free_stripe_hash_table(fs_info);
	btrfs_close_devices(fs_info->fs_devices);
	return err;

recovery_tree_root:
	if (!btrfs_test_opt(tree_root, RECOVERY))
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

static void btrfs_end_buffer_write_sync(struct buffer_head *bh, int uptodate)
{
	if (uptodate) {
		set_buffer_uptodate(bh);
	} else {
		struct btrfs_device *device = (struct btrfs_device *)
			bh->b_private;

		printk_ratelimited_in_rcu(KERN_WARNING "lost page write due to "
					  "I/O error on %s\n",
					  rcu_str_deref(device->name));
		/* note, we dont' set_buffer_write_io_error because we have
		 * our own ways of dealing with the IO errors
		 */
		clear_buffer_uptodate(bh);
		btrfs_dev_stat_inc_and_print(device, BTRFS_DEV_STAT_WRITE_ERRS);
	}
	unlock_buffer(bh);
	put_bh(bh);
}

struct buffer_head *btrfs_read_dev_super(struct block_device *bdev)
{
	struct buffer_head *bh;
	struct buffer_head *latest = NULL;
	struct btrfs_super_block *super;
	int i;
	u64 transid = 0;
	u64 bytenr;

	/* we would like to check all the supers, but that would make
	 * a btrfs mount succeed after a mkfs from a different FS.
	 * So, we need to add a special mount option to scan for
	 * later supers, using BTRFS_SUPER_MIRROR_MAX instead
	 */
	for (i = 0; i < 1; i++) {
		bytenr = btrfs_sb_offset(i);
		if (bytenr + 4096 >= i_size_read(bdev->bd_inode))
			break;
		bh = __bread(bdev, bytenr / 4096, 4096);
		if (!bh)
			continue;

		super = (struct btrfs_super_block *)bh->b_data;
		if (btrfs_super_bytenr(super) != bytenr ||
		    super->magic != cpu_to_le64(BTRFS_MAGIC)) {
			brelse(bh);
			continue;
		}

		if (!latest || btrfs_super_generation(super) > transid) {
			brelse(latest);
			latest = bh;
			transid = btrfs_super_generation(super);
		} else {
			brelse(bh);
		}
	}
	return latest;
}

/*
 * this should be called twice, once with wait == 0 and
 * once with wait == 1.  When wait == 0 is done, all the buffer heads
 * we write are pinned.
 *
 * They are released when wait == 1 is done.
 * max_mirrors must be the same for both runs, and it indicates how
 * many supers on this one device should be written.
 *
 * max_mirrors == 0 means to write them all.
 */
static int write_dev_supers(struct btrfs_device *device,
			    struct btrfs_super_block *sb,
			    int do_barriers, int wait, int max_mirrors)
{
	struct buffer_head *bh;
	int i;
	int ret;
	int errors = 0;
	u32 crc;
	u64 bytenr;

	if (max_mirrors == 0)
		max_mirrors = BTRFS_SUPER_MIRROR_MAX;

	for (i = 0; i < max_mirrors; i++) {
		bytenr = btrfs_sb_offset(i);
		if (bytenr + BTRFS_SUPER_INFO_SIZE >= device->total_bytes)
			break;

		if (wait) {
			bh = __find_get_block(device->bdev, bytenr / 4096,
					      BTRFS_SUPER_INFO_SIZE);
			BUG_ON(!bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh))
				errors++;

			/* drop our reference */
			brelse(bh);

			/* drop the reference from the wait == 0 run */
			brelse(bh);
			continue;
		} else {
			btrfs_set_super_bytenr(sb, bytenr);

			crc = ~(u32)0;
			crc = btrfs_csum_data(NULL, (char *)sb +
					      BTRFS_CSUM_SIZE, crc,
					      BTRFS_SUPER_INFO_SIZE -
					      BTRFS_CSUM_SIZE);
			btrfs_csum_final(crc, sb->csum);

			/*
			 * one reference for us, and we leave it for the
			 * caller
			 */
			bh = __getblk(device->bdev, bytenr / 4096,
				      BTRFS_SUPER_INFO_SIZE);
			memcpy(bh->b_data, sb, BTRFS_SUPER_INFO_SIZE);

			/* one reference for submit_bh */
			get_bh(bh);

			set_buffer_uptodate(bh);
			lock_buffer(bh);
			bh->b_end_io = btrfs_end_buffer_write_sync;
			bh->b_private = device;
		}

		/*
		 * we fua the first super.  The others we allow
		 * to go down lazy.
		 */
		ret = btrfsic_submit_bh(WRITE_FUA, bh);
		if (ret)
			errors++;
	}
	return errors < i ? 0 : -1;
}

/*
 * endio for the write_dev_flush, this will wake anyone waiting
 * for the barrier when it is done
 */
static void btrfs_end_empty_barrier(struct bio *bio, int err)
{
	if (err) {
		if (err == -EOPNOTSUPP)
			set_bit(BIO_EOPNOTSUPP, &bio->bi_flags);
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
	}
	if (bio->bi_private)
		complete(bio->bi_private);
	bio_put(bio);
}

/*
 * trigger flushes for one the devices.  If you pass wait == 0, the flushes are
 * sent down.  With wait == 1, it waits for the previous flush.
 *
 * any device where the flush fails with eopnotsupp are flagged as not-barrier
 * capable
 */
static int write_dev_flush(struct btrfs_device *device, int wait)
{
	struct bio *bio;
	int ret = 0;

	if (device->nobarriers)
		return 0;

	if (wait) {
		bio = device->flush_bio;
		if (!bio)
			return 0;

		wait_for_completion(&device->flush_wait);

		if (bio_flagged(bio, BIO_EOPNOTSUPP)) {
			printk_in_rcu("btrfs: disabling barriers on dev %s\n",
				      rcu_str_deref(device->name));
			device->nobarriers = 1;
		} else if (!bio_flagged(bio, BIO_UPTODATE)) {
			ret = -EIO;
			btrfs_dev_stat_inc_and_print(device,
				BTRFS_DEV_STAT_FLUSH_ERRS);
		}

		/* drop the reference from the wait == 0 run */
		bio_put(bio);
		device->flush_bio = NULL;

		return ret;
	}

	/*
	 * one reference for us, and we leave it for the
	 * caller
	 */
	device->flush_bio = NULL;
	bio = bio_alloc(GFP_NOFS, 0);
	if (!bio)
		return -ENOMEM;

	bio->bi_end_io = btrfs_end_empty_barrier;
	bio->bi_bdev = device->bdev;
	init_completion(&device->flush_wait);
	bio->bi_private = &device->flush_wait;
	device->flush_bio = bio;

	bio_get(bio);
	btrfsic_submit_bio(WRITE_FLUSH, bio);

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
	int errors_send = 0;
	int errors_wait = 0;
	int ret;

	/* send down all the barriers */
	head = &info->fs_devices->devices;
	list_for_each_entry_rcu(dev, head, dev_list) {
		if (!dev->bdev) {
			errors_send++;
			continue;
		}
		if (!dev->in_fs_metadata || !dev->writeable)
			continue;

		ret = write_dev_flush(dev, 0);
		if (ret)
			errors_send++;
	}

	/* wait for all the barriers */
	list_for_each_entry_rcu(dev, head, dev_list) {
		if (!dev->bdev) {
			errors_wait++;
			continue;
		}
		if (!dev->in_fs_metadata || !dev->writeable)
			continue;

		ret = write_dev_flush(dev, 1);
		if (ret)
			errors_wait++;
	}
	if (errors_send > info->num_tolerated_disk_barrier_failures ||
	    errors_wait > info->num_tolerated_disk_barrier_failures)
		return -EIO;
	return 0;
}

int btrfs_calc_num_tolerated_disk_barrier_failures(
	struct btrfs_fs_info *fs_info)
{
	struct btrfs_ioctl_space_info space;
	struct btrfs_space_info *sinfo;
	u64 types[] = {BTRFS_BLOCK_GROUP_DATA,
		       BTRFS_BLOCK_GROUP_SYSTEM,
		       BTRFS_BLOCK_GROUP_METADATA,
		       BTRFS_BLOCK_GROUP_DATA | BTRFS_BLOCK_GROUP_METADATA};
	int num_types = 4;
	int i;
	int c;
	int num_tolerated_disk_barrier_failures =
		(int)fs_info->fs_devices->num_devices;

	for (i = 0; i < num_types; i++) {
		struct btrfs_space_info *tmp;

		sinfo = NULL;
		rcu_read_lock();
		list_for_each_entry_rcu(tmp, &fs_info->space_info, list) {
			if (tmp->flags == types[i]) {
				sinfo = tmp;
				break;
			}
		}
		rcu_read_unlock();

		if (!sinfo)
			continue;

		down_read(&sinfo->groups_sem);
		for (c = 0; c < BTRFS_NR_RAID_TYPES; c++) {
			if (!list_empty(&sinfo->block_groups[c])) {
				u64 flags;

				btrfs_get_block_group_info(
					&sinfo->block_groups[c], &space);
				if (space.total_bytes == 0 ||
				    space.used_bytes == 0)
					continue;
				flags = space.flags;
				/*
				 * return
				 * 0: if dup, single or RAID0 is configured for
				 *    any of metadata, system or data, else
				 * 1: if RAID5 is configured, or if RAID1 or
				 *    RAID10 is configured and only two mirrors
				 *    are used, else
				 * 2: if RAID6 is configured, else
				 * num_mirrors - 1: if RAID1 or RAID10 is
				 *                  configured and more than
				 *                  2 mirrors are used.
				 */
				if (num_tolerated_disk_barrier_failures > 0 &&
				    ((flags & (BTRFS_BLOCK_GROUP_DUP |
					       BTRFS_BLOCK_GROUP_RAID0)) ||
				     ((flags & BTRFS_BLOCK_GROUP_PROFILE_MASK)
				      == 0)))
					num_tolerated_disk_barrier_failures = 0;
				else if (num_tolerated_disk_barrier_failures > 1) {
					if (flags & (BTRFS_BLOCK_GROUP_RAID1 |
					    BTRFS_BLOCK_GROUP_RAID5 |
					    BTRFS_BLOCK_GROUP_RAID10)) {
						num_tolerated_disk_barrier_failures = 1;
					} else if (flags &
						   BTRFS_BLOCK_GROUP_RAID5) {
						num_tolerated_disk_barrier_failures = 2;
					}
				}
			}
		}
		up_read(&sinfo->groups_sem);
	}

	return num_tolerated_disk_barrier_failures;
}

int write_all_supers(struct btrfs_root *root, int max_mirrors)
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

	max_errors = btrfs_super_num_devices(root->fs_info->super_copy) - 1;
	do_barriers = !btrfs_test_opt(root, NOBARRIER);
	backup_super_roots(root->fs_info);

	sb = root->fs_info->super_for_commit;
	dev_item = &sb->dev_item;

	mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
	head = &root->fs_info->fs_devices->devices;

	if (do_barriers) {
		ret = barrier_all_devices(root->fs_info);
		if (ret) {
			mutex_unlock(
				&root->fs_info->fs_devices->device_list_mutex);
			btrfs_error(root->fs_info, ret,
				    "errors while submitting device barriers.");
			return ret;
		}
	}

	list_for_each_entry_rcu(dev, head, dev_list) {
		if (!dev->bdev) {
			total_errors++;
			continue;
		}
		if (!dev->in_fs_metadata || !dev->writeable)
			continue;

		btrfs_set_stack_device_generation(dev_item, 0);
		btrfs_set_stack_device_type(dev_item, dev->type);
		btrfs_set_stack_device_id(dev_item, dev->devid);
		btrfs_set_stack_device_total_bytes(dev_item, dev->total_bytes);
		btrfs_set_stack_device_bytes_used(dev_item, dev->bytes_used);
		btrfs_set_stack_device_io_align(dev_item, dev->io_align);
		btrfs_set_stack_device_io_width(dev_item, dev->io_width);
		btrfs_set_stack_device_sector_size(dev_item, dev->sector_size);
		memcpy(dev_item->uuid, dev->uuid, BTRFS_UUID_SIZE);
		memcpy(dev_item->fsid, dev->fs_devices->fsid, BTRFS_UUID_SIZE);

		flags = btrfs_super_flags(sb);
		btrfs_set_super_flags(sb, flags | BTRFS_HEADER_FLAG_WRITTEN);

		ret = write_dev_supers(dev, sb, do_barriers, 0, max_mirrors);
		if (ret)
			total_errors++;
	}
	if (total_errors > max_errors) {
		printk(KERN_ERR "btrfs: %d errors while writing supers\n",
		       total_errors);

		/* This shouldn't happen. FUA is masked off if unsupported */
		BUG();
	}

	total_errors = 0;
	list_for_each_entry_rcu(dev, head, dev_list) {
		if (!dev->bdev)
			continue;
		if (!dev->in_fs_metadata || !dev->writeable)
			continue;

		ret = write_dev_supers(dev, sb, do_barriers, 1, max_mirrors);
		if (ret)
			total_errors++;
	}
	mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);
	if (total_errors > max_errors) {
		btrfs_error(root->fs_info, -EIO,
			    "%d errors while writing supers", total_errors);
		return -EIO;
	}
	return 0;
}

int write_ctree_super(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root, int max_mirrors)
{
	int ret;

	ret = write_all_supers(root, max_mirrors);
	return ret;
}

void btrfs_free_fs_root(struct btrfs_fs_info *fs_info, struct btrfs_root *root)
{
	spin_lock(&fs_info->fs_roots_radix_lock);
	radix_tree_delete(&fs_info->fs_roots_radix,
			  (unsigned long)root->root_key.objectid);
	spin_unlock(&fs_info->fs_roots_radix_lock);

	if (btrfs_root_refs(&root->root_item) == 0)
		synchronize_srcu(&fs_info->subvol_srcu);

	if (fs_info->fs_state & BTRFS_SUPER_FLAG_ERROR) {
		btrfs_free_log(NULL, root);
		btrfs_free_log_root_tree(NULL, fs_info);
	}

	__btrfs_remove_free_space_cache(root->free_ino_pinned);
	__btrfs_remove_free_space_cache(root->free_ino_ctl);
	free_fs_root(root);
}

static void free_fs_root(struct btrfs_root *root)
{
	iput(root->cache_inode);
	WARN_ON(!RB_EMPTY_ROOT(&root->inode_tree));
	if (root->anon_dev)
		free_anon_bdev(root->anon_dev);
	free_extent_buffer(root->node);
	free_extent_buffer(root->commit_root);
	kfree(root->free_ino_ctl);
	kfree(root->free_ino_pinned);
	kfree(root->name);
	kfree(root);
}

static void del_fs_roots(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *gang[8];
	int i;

	while (!list_empty(&fs_info->dead_roots)) {
		gang[0] = list_entry(fs_info->dead_roots.next,
				     struct btrfs_root, root_list);
		list_del(&gang[0]->root_list);

		if (gang[0]->in_radix) {
			btrfs_free_fs_root(fs_info, gang[0]);
		} else {
			free_extent_buffer(gang[0]->node);
			free_extent_buffer(gang[0]->commit_root);
			kfree(gang[0]);
		}
	}

	while (1) {
		ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, 0,
					     ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++)
			btrfs_free_fs_root(fs_info, gang[i]);
	}
}

int btrfs_cleanup_fs_roots(struct btrfs_fs_info *fs_info)
{
	u64 root_objectid = 0;
	struct btrfs_root *gang[8];
	int i;
	int ret;

	while (1) {
		ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, root_objectid,
					     ARRAY_SIZE(gang));
		if (!ret)
			break;

		root_objectid = gang[ret - 1]->root_key.objectid + 1;
		for (i = 0; i < ret; i++) {
			int err;

			root_objectid = gang[i]->root_key.objectid;
			err = btrfs_orphan_cleanup(gang[i]);
			if (err)
				return err;
		}
		root_objectid++;
	}
	return 0;
}

int btrfs_commit_super(struct btrfs_root *root)
{
	struct btrfs_trans_handle *trans;
	int ret;

	mutex_lock(&root->fs_info->cleaner_mutex);
	btrfs_run_delayed_iputs(root);
	btrfs_clean_old_snapshots(root);
	mutex_unlock(&root->fs_info->cleaner_mutex);

	/* wait until ongoing cleanup work done */
	down_write(&root->fs_info->cleanup_work_sem);
	up_write(&root->fs_info->cleanup_work_sem);

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);
	ret = btrfs_commit_transaction(trans, root);
	if (ret)
		return ret;
	/* run commit again to drop the original snapshot */
	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);
	ret = btrfs_commit_transaction(trans, root);
	if (ret)
		return ret;
	ret = btrfs_write_and_wait_transaction(NULL, root);
	if (ret) {
		btrfs_error(root->fs_info, ret,
			    "Failed to sync btree inode to disk.");
		return ret;
	}

	ret = write_ctree_super(NULL, root, 0);
	return ret;
}

int close_ctree(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	fs_info->closing = 1;
	smp_mb();

	/* pause restriper - we want to resume on mount */
	btrfs_pause_balance(fs_info);

	btrfs_dev_replace_suspend_for_unmount(fs_info);

	btrfs_scrub_cancel(fs_info);

	/* wait for any defraggers to finish */
	wait_event(fs_info->transaction_wait,
		   (atomic_read(&fs_info->defrag_running) == 0));

	/* clear out the rbtree of defraggable inodes */
	btrfs_cleanup_defrag_inodes(fs_info);

	if (!(fs_info->sb->s_flags & MS_RDONLY)) {
		ret = btrfs_commit_super(root);
		if (ret)
			printk(KERN_ERR "btrfs: commit super ret %d\n", ret);
	}

	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state))
		btrfs_error_commit_super(root);

	btrfs_put_block_group_cache(fs_info);

	kthread_stop(fs_info->transaction_kthread);
	kthread_stop(fs_info->cleaner_kthread);

	fs_info->closing = 2;
	smp_mb();

	btrfs_free_qgroup_config(root->fs_info);

	if (percpu_counter_sum(&fs_info->delalloc_bytes)) {
		printk(KERN_INFO "btrfs: at unmount delalloc count %lld\n",
		       percpu_counter_sum(&fs_info->delalloc_bytes));
	}

	free_extent_buffer(fs_info->extent_root->node);
	free_extent_buffer(fs_info->extent_root->commit_root);
	free_extent_buffer(fs_info->tree_root->node);
	free_extent_buffer(fs_info->tree_root->commit_root);
	free_extent_buffer(fs_info->chunk_root->node);
	free_extent_buffer(fs_info->chunk_root->commit_root);
	free_extent_buffer(fs_info->dev_root->node);
	free_extent_buffer(fs_info->dev_root->commit_root);
	free_extent_buffer(fs_info->csum_root->node);
	free_extent_buffer(fs_info->csum_root->commit_root);
	if (fs_info->quota_root) {
		free_extent_buffer(fs_info->quota_root->node);
		free_extent_buffer(fs_info->quota_root->commit_root);
	}

	btrfs_free_block_groups(fs_info);

	del_fs_roots(fs_info);

	iput(fs_info->btree_inode);

	btrfs_stop_workers(&fs_info->generic_worker);
	btrfs_stop_workers(&fs_info->fixup_workers);
	btrfs_stop_workers(&fs_info->delalloc_workers);
	btrfs_stop_workers(&fs_info->workers);
	btrfs_stop_workers(&fs_info->endio_workers);
	btrfs_stop_workers(&fs_info->endio_meta_workers);
	btrfs_stop_workers(&fs_info->endio_raid56_workers);
	btrfs_stop_workers(&fs_info->rmw_workers);
	btrfs_stop_workers(&fs_info->endio_meta_write_workers);
	btrfs_stop_workers(&fs_info->endio_write_workers);
	btrfs_stop_workers(&fs_info->endio_freespace_worker);
	btrfs_stop_workers(&fs_info->submit_workers);
	btrfs_stop_workers(&fs_info->delayed_workers);
	btrfs_stop_workers(&fs_info->caching_workers);
	btrfs_stop_workers(&fs_info->readahead_workers);
	btrfs_stop_workers(&fs_info->flush_workers);

#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
	if (btrfs_test_opt(root, CHECK_INTEGRITY))
		btrfsic_unmount(root, fs_info->fs_devices);
#endif

	btrfs_close_devices(fs_info->fs_devices);
	btrfs_mapping_tree_free(&fs_info->mapping_tree);

	percpu_counter_destroy(&fs_info->dirty_metadata_bytes);
	percpu_counter_destroy(&fs_info->delalloc_bytes);
	bdi_destroy(&fs_info->bdi);
	cleanup_srcu_struct(&fs_info->subvol_srcu);

	btrfs_free_stripe_hash_table(fs_info);

	return 0;
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

int btrfs_set_buffer_uptodate(struct extent_buffer *buf)
{
	return set_extent_buffer_uptodate(buf);
}

void btrfs_mark_buffer_dirty(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	u64 transid = btrfs_header_generation(buf);
	int was_dirty;

	btrfs_assert_tree_locked(buf);
	if (transid != root->fs_info->generation)
		WARN(1, KERN_CRIT "btrfs transid mismatch buffer %llu, "
		       "found %llu running %llu\n",
			(unsigned long long)buf->start,
			(unsigned long long)transid,
			(unsigned long long)root->fs_info->generation);
	was_dirty = set_extent_buffer_dirty(buf);
	if (!was_dirty)
		__percpu_counter_add(&root->fs_info->dirty_metadata_bytes,
				     buf->len,
				     root->fs_info->dirty_metadata_batch);
}

static void __btrfs_btree_balance_dirty(struct btrfs_root *root,
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
		btrfs_balance_delayed_items(root);

	ret = percpu_counter_compare(&root->fs_info->dirty_metadata_bytes,
				     BTRFS_DIRTY_METADATA_THRESH);
	if (ret > 0) {
		balance_dirty_pages_ratelimited(
				   root->fs_info->btree_inode->i_mapping);
	}
	return;
}

void btrfs_btree_balance_dirty(struct btrfs_root *root)
{
	__btrfs_btree_balance_dirty(root, 1);
}

void btrfs_btree_balance_dirty_nodelay(struct btrfs_root *root)
{
	__btrfs_btree_balance_dirty(root, 0);
}

int btrfs_read_buffer(struct extent_buffer *buf, u64 parent_transid)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	return btree_read_extent_buffer_pages(root, buf, 0, parent_transid);
}

static int btrfs_check_super_valid(struct btrfs_fs_info *fs_info,
			      int read_only)
{
	if (btrfs_super_csum_type(fs_info->super_copy) >= ARRAY_SIZE(btrfs_csum_sizes)) {
		printk(KERN_ERR "btrfs: unsupported checksum algorithm\n");
		return -EINVAL;
	}

	if (read_only)
		return 0;

	return 0;
}

void btrfs_error_commit_super(struct btrfs_root *root)
{
	mutex_lock(&root->fs_info->cleaner_mutex);
	btrfs_run_delayed_iputs(root);
	mutex_unlock(&root->fs_info->cleaner_mutex);

	down_write(&root->fs_info->cleanup_work_sem);
	up_write(&root->fs_info->cleanup_work_sem);

	/* cleanup FS via transaction */
	btrfs_cleanup_transaction(root);
}

static void btrfs_destroy_ordered_operations(struct btrfs_transaction *t,
					     struct btrfs_root *root)
{
	struct btrfs_inode *btrfs_inode;
	struct list_head splice;

	INIT_LIST_HEAD(&splice);

	mutex_lock(&root->fs_info->ordered_operations_mutex);
	spin_lock(&root->fs_info->ordered_extent_lock);

	list_splice_init(&t->ordered_operations, &splice);
	while (!list_empty(&splice)) {
		btrfs_inode = list_entry(splice.next, struct btrfs_inode,
					 ordered_operations);

		list_del_init(&btrfs_inode->ordered_operations);

		btrfs_invalidate_inodes(btrfs_inode->root);
	}

	spin_unlock(&root->fs_info->ordered_extent_lock);
	mutex_unlock(&root->fs_info->ordered_operations_mutex);
}

static void btrfs_destroy_ordered_extents(struct btrfs_root *root)
{
	struct btrfs_ordered_extent *ordered;

	spin_lock(&root->fs_info->ordered_extent_lock);
	/*
	 * This will just short circuit the ordered completion stuff which will
	 * make sure the ordered extent gets properly cleaned up.
	 */
	list_for_each_entry(ordered, &root->fs_info->ordered_extents,
			    root_extent_list)
		set_bit(BTRFS_ORDERED_IOERR, &ordered->flags);
	spin_unlock(&root->fs_info->ordered_extent_lock);
}

int btrfs_destroy_delayed_refs(struct btrfs_transaction *trans,
			       struct btrfs_root *root)
{
	struct rb_node *node;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	int ret = 0;

	delayed_refs = &trans->delayed_refs;

	spin_lock(&delayed_refs->lock);
	if (delayed_refs->num_entries == 0) {
		spin_unlock(&delayed_refs->lock);
		printk(KERN_INFO "delayed_refs has NO entry\n");
		return ret;
	}

	while ((node = rb_first(&delayed_refs->root)) != NULL) {
		struct btrfs_delayed_ref_head *head = NULL;

		ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);
		atomic_set(&ref->refs, 1);
		if (btrfs_delayed_ref_is_head(ref)) {

			head = btrfs_delayed_node_to_head(ref);
			if (!mutex_trylock(&head->mutex)) {
				atomic_inc(&ref->refs);
				spin_unlock(&delayed_refs->lock);

				/* Need to wait for the delayed ref to run */
				mutex_lock(&head->mutex);
				mutex_unlock(&head->mutex);
				btrfs_put_delayed_ref(ref);

				spin_lock(&delayed_refs->lock);
				continue;
			}

			btrfs_free_delayed_extent_op(head->extent_op);
			delayed_refs->num_heads--;
			if (list_empty(&head->cluster))
				delayed_refs->num_heads_ready--;
			list_del_init(&head->cluster);
		}

		ref->in_tree = 0;
		rb_erase(&ref->rb_node, &delayed_refs->root);
		delayed_refs->num_entries--;
		if (head)
			mutex_unlock(&head->mutex);
		spin_unlock(&delayed_refs->lock);
		btrfs_put_delayed_ref(ref);

		cond_resched();
		spin_lock(&delayed_refs->lock);
	}

	spin_unlock(&delayed_refs->lock);

	return ret;
}

static void btrfs_evict_pending_snapshots(struct btrfs_transaction *t)
{
	struct btrfs_pending_snapshot *snapshot;
	struct list_head splice;

	INIT_LIST_HEAD(&splice);

	list_splice_init(&t->pending_snapshots, &splice);

	while (!list_empty(&splice)) {
		snapshot = list_entry(splice.next,
				      struct btrfs_pending_snapshot,
				      list);
		snapshot->error = -ECANCELED;
		list_del_init(&snapshot->list);
	}
}

static void btrfs_destroy_delalloc_inodes(struct btrfs_root *root)
{
	struct btrfs_inode *btrfs_inode;
	struct list_head splice;

	INIT_LIST_HEAD(&splice);

	spin_lock(&root->fs_info->delalloc_lock);
	list_splice_init(&root->fs_info->delalloc_inodes, &splice);

	while (!list_empty(&splice)) {
		btrfs_inode = list_entry(splice.next, struct btrfs_inode,
				    delalloc_inodes);

		list_del_init(&btrfs_inode->delalloc_inodes);
		clear_bit(BTRFS_INODE_IN_DELALLOC_LIST,
			  &btrfs_inode->runtime_flags);

		btrfs_invalidate_inodes(btrfs_inode->root);
	}

	spin_unlock(&root->fs_info->delalloc_lock);
}

static int btrfs_destroy_marked_extents(struct btrfs_root *root,
					struct extent_io_tree *dirty_pages,
					int mark)
{
	int ret;
	struct page *page;
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_buffer *eb;
	u64 start = 0;
	u64 end;
	u64 offset;
	unsigned long index;

	while (1) {
		ret = find_first_extent_bit(dirty_pages, start, &start, &end,
					    mark, NULL);
		if (ret)
			break;

		clear_extent_bits(dirty_pages, start, end, mark, GFP_NOFS);
		while (start <= end) {
			index = start >> PAGE_CACHE_SHIFT;
			start = (u64)(index + 1) << PAGE_CACHE_SHIFT;
			page = find_get_page(btree_inode->i_mapping, index);
			if (!page)
				continue;
			offset = page_offset(page);

			spin_lock(&dirty_pages->buffer_lock);
			eb = radix_tree_lookup(
			     &(&BTRFS_I(page->mapping->host)->io_tree)->buffer,
					       offset >> PAGE_CACHE_SHIFT);
			spin_unlock(&dirty_pages->buffer_lock);
			if (eb)
				ret = test_and_clear_bit(EXTENT_BUFFER_DIRTY,
							 &eb->bflags);
			if (PageWriteback(page))
				end_page_writeback(page);

			lock_page(page);
			if (PageDirty(page)) {
				clear_page_dirty_for_io(page);
				spin_lock_irq(&page->mapping->tree_lock);
				radix_tree_tag_clear(&page->mapping->page_tree,
							page_index(page),
							PAGECACHE_TAG_DIRTY);
				spin_unlock_irq(&page->mapping->tree_lock);
			}

			unlock_page(page);
			page_cache_release(page);
		}
	}

	return ret;
}

static int btrfs_destroy_pinned_extent(struct btrfs_root *root,
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

		/* opt_discard */
		if (btrfs_test_opt(root, DISCARD))
			ret = btrfs_error_discard_extent(root, start,
							 end + 1 - start,
							 NULL);

		clear_extent_dirty(unpin, start, end, GFP_NOFS);
		btrfs_error_unpin_extent_range(root, start, end);
		cond_resched();
	}

	if (loop) {
		if (unpin == &root->fs_info->freed_extents[0])
			unpin = &root->fs_info->freed_extents[1];
		else
			unpin = &root->fs_info->freed_extents[0];
		loop = false;
		goto again;
	}

	return 0;
}

void btrfs_cleanup_one_transaction(struct btrfs_transaction *cur_trans,
				   struct btrfs_root *root)
{
	btrfs_destroy_delayed_refs(cur_trans, root);
	btrfs_block_rsv_release(root, &root->fs_info->trans_block_rsv,
				cur_trans->dirty_pages.dirty_bytes);

	/* FIXME: cleanup wait for commit */
	cur_trans->in_commit = 1;
	cur_trans->blocked = 1;
	wake_up(&root->fs_info->transaction_blocked_wait);

	btrfs_evict_pending_snapshots(cur_trans);

	cur_trans->blocked = 0;
	wake_up(&root->fs_info->transaction_wait);

	cur_trans->commit_done = 1;
	wake_up(&cur_trans->commit_wait);

	btrfs_destroy_delayed_inodes(root);
	btrfs_assert_delayed_root_empty(root);

	btrfs_destroy_marked_extents(root, &cur_trans->dirty_pages,
				     EXTENT_DIRTY);
	btrfs_destroy_pinned_extent(root,
				    root->fs_info->pinned_extents);

	/*
	memset(cur_trans, 0, sizeof(*cur_trans));
	kmem_cache_free(btrfs_transaction_cachep, cur_trans);
	*/
}

int btrfs_cleanup_transaction(struct btrfs_root *root)
{
	struct btrfs_transaction *t;
	LIST_HEAD(list);

	mutex_lock(&root->fs_info->transaction_kthread_mutex);

	spin_lock(&root->fs_info->trans_lock);
	list_splice_init(&root->fs_info->trans_list, &list);
	root->fs_info->trans_no_join = 1;
	spin_unlock(&root->fs_info->trans_lock);

	while (!list_empty(&list)) {
		t = list_entry(list.next, struct btrfs_transaction, list);

		btrfs_destroy_ordered_operations(t, root);

		btrfs_destroy_ordered_extents(root);

		btrfs_destroy_delayed_refs(t, root);

		btrfs_block_rsv_release(root,
					&root->fs_info->trans_block_rsv,
					t->dirty_pages.dirty_bytes);

		/* FIXME: cleanup wait for commit */
		t->in_commit = 1;
		t->blocked = 1;
		smp_mb();
		if (waitqueue_active(&root->fs_info->transaction_blocked_wait))
			wake_up(&root->fs_info->transaction_blocked_wait);

		btrfs_evict_pending_snapshots(t);

		t->blocked = 0;
		smp_mb();
		if (waitqueue_active(&root->fs_info->transaction_wait))
			wake_up(&root->fs_info->transaction_wait);

		t->commit_done = 1;
		smp_mb();
		if (waitqueue_active(&t->commit_wait))
			wake_up(&t->commit_wait);

		btrfs_destroy_delayed_inodes(root);
		btrfs_assert_delayed_root_empty(root);

		btrfs_destroy_delalloc_inodes(root);

		spin_lock(&root->fs_info->trans_lock);
		root->fs_info->running_transaction = NULL;
		spin_unlock(&root->fs_info->trans_lock);

		btrfs_destroy_marked_extents(root, &t->dirty_pages,
					     EXTENT_DIRTY);

		btrfs_destroy_pinned_extent(root,
					    root->fs_info->pinned_extents);

		atomic_set(&t->use_count, 0);
		list_del_init(&t->list);
		memset(t, 0, sizeof(*t));
		kmem_cache_free(btrfs_transaction_cachep, t);
	}

	spin_lock(&root->fs_info->trans_lock);
	root->fs_info->trans_no_join = 0;
	spin_unlock(&root->fs_info->trans_lock);
	mutex_unlock(&root->fs_info->transaction_kthread_mutex);

	return 0;
}

static struct extent_io_ops btree_extent_io_ops = {
	.readpage_end_io_hook = btree_readpage_end_io_hook,
	.readpage_io_failed_hook = btree_io_failed_hook,
	.submit_bio_hook = btree_submit_bio_hook,
	/* note we're sharing with inode.c for the merge bio hook */
	.merge_bio_hook = btrfs_merge_bio_hook,
};

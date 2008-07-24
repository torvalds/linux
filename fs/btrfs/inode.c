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

#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include <linux/compat.h>
#include <linux/bit_spinlock.h>
#include <linux/version.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"
#include "print-tree.h"
#include "volumes.h"
#include "ordered-data.h"

struct btrfs_iget_args {
	u64 ino;
	struct btrfs_root *root;
};

static struct inode_operations btrfs_dir_inode_operations;
static struct inode_operations btrfs_symlink_inode_operations;
static struct inode_operations btrfs_dir_ro_inode_operations;
static struct inode_operations btrfs_special_inode_operations;
static struct inode_operations btrfs_file_inode_operations;
static struct address_space_operations btrfs_aops;
static struct address_space_operations btrfs_symlink_aops;
static struct file_operations btrfs_dir_file_operations;
static struct extent_io_ops btrfs_extent_io_ops;

static struct kmem_cache *btrfs_inode_cachep;
struct kmem_cache *btrfs_trans_handle_cachep;
struct kmem_cache *btrfs_transaction_cachep;
struct kmem_cache *btrfs_bit_radix_cachep;
struct kmem_cache *btrfs_path_cachep;

#define S_SHIFT 12
static unsigned char btrfs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= BTRFS_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= BTRFS_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= BTRFS_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= BTRFS_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= BTRFS_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= BTRFS_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= BTRFS_FT_SYMLINK,
};

static void btrfs_truncate(struct inode *inode);

int btrfs_check_free_space(struct btrfs_root *root, u64 num_required,
			   int for_del)
{
	u64 total;
	u64 used;
	u64 thresh;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&root->fs_info->delalloc_lock, flags);
	total = btrfs_super_total_bytes(&root->fs_info->super_copy);
	used = btrfs_super_bytes_used(&root->fs_info->super_copy);
	if (for_del)
		thresh = total * 90;
	else
		thresh = total * 85;

	do_div(thresh, 100);

	if (used + root->fs_info->delalloc_bytes + num_required > thresh)
		ret = -ENOSPC;
	spin_unlock_irqrestore(&root->fs_info->delalloc_lock, flags);
	return ret;
}

static int cow_file_range(struct inode *inode, u64 start, u64 end)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	u64 alloc_hint = 0;
	u64 num_bytes;
	u64 cur_alloc_size;
	u64 blocksize = root->sectorsize;
	u64 orig_num_bytes;
	struct btrfs_key ins;
	struct extent_map *em;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	int ret = 0;

	trans = btrfs_join_transaction(root, 1);
	BUG_ON(!trans);
	btrfs_set_trans_block_group(trans, inode);

	num_bytes = (end - start + blocksize) & ~(blocksize - 1);
	num_bytes = max(blocksize,  num_bytes);
	orig_num_bytes = num_bytes;

	if (alloc_hint == EXTENT_MAP_INLINE)
		goto out;

	BUG_ON(num_bytes > btrfs_super_total_bytes(&root->fs_info->super_copy));
	mutex_lock(&BTRFS_I(inode)->extent_mutex);
	btrfs_drop_extent_cache(inode, start, start + num_bytes - 1);
	mutex_unlock(&BTRFS_I(inode)->extent_mutex);

	while(num_bytes > 0) {
		cur_alloc_size = min(num_bytes, root->fs_info->max_extent);
		ret = btrfs_reserve_extent(trans, root, cur_alloc_size,
					   root->sectorsize, 0, 0,
					   (u64)-1, &ins, 1);
		if (ret) {
			WARN_ON(1);
			goto out;
		}
		em = alloc_extent_map(GFP_NOFS);
		em->start = start;
		em->len = ins.offset;
		em->block_start = ins.objectid;
		em->bdev = root->fs_info->fs_devices->latest_bdev;
		mutex_lock(&BTRFS_I(inode)->extent_mutex);
		set_bit(EXTENT_FLAG_PINNED, &em->flags);
		while(1) {
			spin_lock(&em_tree->lock);
			ret = add_extent_mapping(em_tree, em);
			spin_unlock(&em_tree->lock);
			if (ret != -EEXIST) {
				free_extent_map(em);
				break;
			}
			btrfs_drop_extent_cache(inode, start,
						start + ins.offset - 1);
		}
		mutex_unlock(&BTRFS_I(inode)->extent_mutex);

		cur_alloc_size = ins.offset;
		ret = btrfs_add_ordered_extent(inode, start, ins.objectid,
					       ins.offset);
		BUG_ON(ret);
		if (num_bytes < cur_alloc_size) {
			printk("num_bytes %Lu cur_alloc %Lu\n", num_bytes,
			       cur_alloc_size);
			break;
		}
		num_bytes -= cur_alloc_size;
		alloc_hint = ins.objectid + ins.offset;
		start += cur_alloc_size;
	}
out:
	btrfs_end_transaction(trans, root);
	return ret;
}

static int run_delalloc_nocow(struct inode *inode, u64 start, u64 end)
{
	u64 extent_start;
	u64 extent_end;
	u64 bytenr;
	u64 cow_end;
	u64 loops = 0;
	u64 total_fs_bytes;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_group_cache *block_group;
	struct extent_buffer *leaf;
	int found_type;
	struct btrfs_path *path;
	struct btrfs_file_extent_item *item;
	int ret;
	int err;
	struct btrfs_key found_key;

	total_fs_bytes = btrfs_super_total_bytes(&root->fs_info->super_copy);
	path = btrfs_alloc_path();
	BUG_ON(!path);
again:
	ret = btrfs_lookup_file_extent(NULL, root, path,
				       inode->i_ino, start, 0);
	if (ret < 0) {
		btrfs_free_path(path);
		return ret;
	}

	cow_end = end;
	if (ret != 0) {
		if (path->slots[0] == 0)
			goto not_found;
		path->slots[0]--;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_file_extent_item);

	/* are we inside the extent that was found? */
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
	found_type = btrfs_key_type(&found_key);
	if (found_key.objectid != inode->i_ino ||
	    found_type != BTRFS_EXTENT_DATA_KEY)
		goto not_found;

	found_type = btrfs_file_extent_type(leaf, item);
	extent_start = found_key.offset;
	if (found_type == BTRFS_FILE_EXTENT_REG) {
		u64 extent_num_bytes;

		extent_num_bytes = btrfs_file_extent_num_bytes(leaf, item);
		extent_end = extent_start + extent_num_bytes;
		err = 0;

		if (loops && start != extent_start)
			goto not_found;

		if (start < extent_start || start >= extent_end)
			goto not_found;

		cow_end = min(end, extent_end - 1);
		bytenr = btrfs_file_extent_disk_bytenr(leaf, item);
		if (bytenr == 0)
			goto not_found;

		if (btrfs_count_snapshots_in_path(root, path, inode->i_ino,
						  bytenr) != 1) {
			goto not_found;
		}

		/*
		 * we may be called by the resizer, make sure we're inside
		 * the limits of the FS
		 */
		block_group = btrfs_lookup_block_group(root->fs_info,
						       bytenr);
		if (!block_group || block_group->ro)
			goto not_found;

		start = extent_end;
	} else {
		goto not_found;
	}
loop:
	if (start > end) {
		btrfs_free_path(path);
		return 0;
	}
	btrfs_release_path(root, path);
	loops++;
	goto again;

not_found:
	cow_file_range(inode, start, end);
	start = end + 1;
	goto loop;
}

static int run_delalloc_range(struct inode *inode, u64 start, u64 end)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	if (btrfs_test_opt(root, NODATACOW) ||
	    btrfs_test_flag(inode, NODATACOW))
		ret = run_delalloc_nocow(inode, start, end);
	else
		ret = cow_file_range(inode, start, end);

	return ret;
}

int btrfs_set_bit_hook(struct inode *inode, u64 start, u64 end,
		       unsigned long old, unsigned long bits)
{
	unsigned long flags;
	if (!(old & EXTENT_DELALLOC) && (bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = BTRFS_I(inode)->root;
		spin_lock_irqsave(&root->fs_info->delalloc_lock, flags);
		BTRFS_I(inode)->delalloc_bytes += end - start + 1;
		root->fs_info->delalloc_bytes += end - start + 1;
		spin_unlock_irqrestore(&root->fs_info->delalloc_lock, flags);
	}
	return 0;
}

int btrfs_clear_bit_hook(struct inode *inode, u64 start, u64 end,
			 unsigned long old, unsigned long bits)
{
	if ((old & EXTENT_DELALLOC) && (bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = BTRFS_I(inode)->root;
		unsigned long flags;

		spin_lock_irqsave(&root->fs_info->delalloc_lock, flags);
		if (end - start + 1 > root->fs_info->delalloc_bytes) {
			printk("warning: delalloc account %Lu %Lu\n",
			       end - start + 1, root->fs_info->delalloc_bytes);
			root->fs_info->delalloc_bytes = 0;
			BTRFS_I(inode)->delalloc_bytes = 0;
		} else {
			root->fs_info->delalloc_bytes -= end - start + 1;
			BTRFS_I(inode)->delalloc_bytes -= end - start + 1;
		}
		spin_unlock_irqrestore(&root->fs_info->delalloc_lock, flags);
	}
	return 0;
}

int btrfs_merge_bio_hook(struct page *page, unsigned long offset,
			 size_t size, struct bio *bio)
{
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;
	struct btrfs_mapping_tree *map_tree;
	u64 logical = bio->bi_sector << 9;
	u64 length = 0;
	u64 map_length;
	int ret;

	length = bio->bi_size;
	map_tree = &root->fs_info->mapping_tree;
	map_length = length;
	ret = btrfs_map_block(map_tree, READ, logical,
			      &map_length, NULL, 0);

	if (map_length < length + size) {
		return 1;
	}
	return 0;
}

int __btrfs_submit_bio_hook(struct inode *inode, int rw, struct bio *bio,
			  int mirror_num)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;

	ret = btrfs_csum_one_bio(root, inode, bio);
	BUG_ON(ret);

	return btrfs_map_bio(root, rw, bio, mirror_num, 1);
}

int btrfs_submit_bio_hook(struct inode *inode, int rw, struct bio *bio,
			  int mirror_num)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;

	ret = btrfs_bio_wq_end_io(root->fs_info, bio, 0);
	BUG_ON(ret);

	if (!(rw & (1 << BIO_RW))) {
		goto mapit;
	}

	return btrfs_wq_submit_bio(BTRFS_I(inode)->root->fs_info,
				   inode, rw, bio, mirror_num,
				   __btrfs_submit_bio_hook);
mapit:
	return btrfs_map_bio(root, rw, bio, mirror_num, 0);
}

static noinline int add_pending_csums(struct btrfs_trans_handle *trans,
			     struct inode *inode, u64 file_offset,
			     struct list_head *list)
{
	struct list_head *cur;
	struct btrfs_ordered_sum *sum;

	btrfs_set_trans_block_group(trans, inode);
	list_for_each(cur, list) {
		sum = list_entry(cur, struct btrfs_ordered_sum, list);
		mutex_lock(&BTRFS_I(inode)->csum_mutex);
		btrfs_csum_file_blocks(trans, BTRFS_I(inode)->root,
				       inode, sum);
		mutex_unlock(&BTRFS_I(inode)->csum_mutex);
	}
	return 0;
}

struct btrfs_writepage_fixup {
	struct page *page;
	struct btrfs_work work;
};

/* see btrfs_writepage_start_hook for details on why this is required */
void btrfs_writepage_fixup_worker(struct btrfs_work *work)
{
	struct btrfs_writepage_fixup *fixup;
	struct btrfs_ordered_extent *ordered;
	struct page *page;
	struct inode *inode;
	u64 page_start;
	u64 page_end;

	fixup = container_of(work, struct btrfs_writepage_fixup, work);
	page = fixup->page;
again:
	lock_page(page);
	if (!page->mapping || !PageDirty(page) || !PageChecked(page)) {
		ClearPageChecked(page);
		goto out_page;
	}

	inode = page->mapping->host;
	page_start = page_offset(page);
	page_end = page_offset(page) + PAGE_CACHE_SIZE - 1;

	lock_extent(&BTRFS_I(inode)->io_tree, page_start, page_end, GFP_NOFS);

	/* already ordered? We're done */
	if (test_range_bit(&BTRFS_I(inode)->io_tree, page_start, page_end,
			     EXTENT_ORDERED, 0)) {
		goto out;
	}

	ordered = btrfs_lookup_ordered_extent(inode, page_start);
	if (ordered) {
		unlock_extent(&BTRFS_I(inode)->io_tree, page_start,
			      page_end, GFP_NOFS);
		unlock_page(page);
		btrfs_start_ordered_extent(inode, ordered, 1);
		goto again;
	}

	set_extent_delalloc(&BTRFS_I(inode)->io_tree, page_start, page_end,
			    GFP_NOFS);
	ClearPageChecked(page);
out:
	unlock_extent(&BTRFS_I(inode)->io_tree, page_start, page_end, GFP_NOFS);
out_page:
	unlock_page(page);
	page_cache_release(page);
}

/*
 * There are a few paths in the higher layers of the kernel that directly
 * set the page dirty bit without asking the filesystem if it is a
 * good idea.  This causes problems because we want to make sure COW
 * properly happens and the data=ordered rules are followed.
 *
 * In our case any range that doesn't have the EXTENT_ORDERED bit set
 * hasn't been properly setup for IO.  We kick off an async process
 * to fix it up.  The async helper will wait for ordered extents, set
 * the delalloc bit and make it safe to write the page.
 */
int btrfs_writepage_start_hook(struct page *page, u64 start, u64 end)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_writepage_fixup *fixup;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	ret = test_range_bit(&BTRFS_I(inode)->io_tree, start, end,
			     EXTENT_ORDERED, 0);
	if (ret)
		return 0;

	if (PageChecked(page))
		return -EAGAIN;

	fixup = kzalloc(sizeof(*fixup), GFP_NOFS);
	if (!fixup)
		return -EAGAIN;

	SetPageChecked(page);
	page_cache_get(page);
	fixup->work.func = btrfs_writepage_fixup_worker;
	fixup->page = page;
	btrfs_queue_worker(&root->fs_info->fixup_workers, &fixup->work);
	return -EAGAIN;
}

static int btrfs_finish_ordered_io(struct inode *inode, u64 start, u64 end)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	struct btrfs_ordered_extent *ordered_extent;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_map *em;
	struct extent_map *em_orig;
	u64 alloc_hint = 0;
	u64 clear_start;
	u64 clear_end;
	struct list_head list;
	struct btrfs_key ins;
	struct rb_node *rb;
	int ret;

	ret = btrfs_dec_test_ordered_pending(inode, start, end - start + 1);
	if (!ret)
		return 0;

	trans = btrfs_join_transaction(root, 1);

	ordered_extent = btrfs_lookup_ordered_extent(inode, start);
	BUG_ON(!ordered_extent);

	lock_extent(io_tree, ordered_extent->file_offset,
		    ordered_extent->file_offset + ordered_extent->len - 1,
		    GFP_NOFS);

	INIT_LIST_HEAD(&list);

	ins.objectid = ordered_extent->start;
	ins.offset = ordered_extent->len;
	ins.type = BTRFS_EXTENT_ITEM_KEY;

	ret = btrfs_alloc_reserved_extent(trans, root, root->root_key.objectid,
					  trans->transid, inode->i_ino,
					  ordered_extent->file_offset, &ins);
	BUG_ON(ret);

	mutex_lock(&BTRFS_I(inode)->extent_mutex);

	spin_lock(&em_tree->lock);
	clear_start = ordered_extent->file_offset;
	clear_end = ordered_extent->file_offset + ordered_extent->len;
	em = lookup_extent_mapping(em_tree, clear_start,
				   ordered_extent->len);
	em_orig = em;
	while(em && clear_start < extent_map_end(em) && clear_end > em->start) {
		clear_bit(EXTENT_FLAG_PINNED, &em->flags);
		rb = rb_next(&em->rb_node);
		if (!rb)
			break;
		em = rb_entry(rb, struct extent_map, rb_node);
	}
	free_extent_map(em_orig);
	spin_unlock(&em_tree->lock);

	ret = btrfs_drop_extents(trans, root, inode,
				 ordered_extent->file_offset,
				 ordered_extent->file_offset +
				 ordered_extent->len,
				 ordered_extent->file_offset, &alloc_hint);
	BUG_ON(ret);
	ret = btrfs_insert_file_extent(trans, root, inode->i_ino,
				       ordered_extent->file_offset,
				       ordered_extent->start,
				       ordered_extent->len,
				       ordered_extent->len, 0);
	BUG_ON(ret);

	btrfs_drop_extent_cache(inode, ordered_extent->file_offset,
				ordered_extent->file_offset +
				ordered_extent->len - 1);
	mutex_unlock(&BTRFS_I(inode)->extent_mutex);

	inode->i_blocks += ordered_extent->len >> 9;
	unlock_extent(io_tree, ordered_extent->file_offset,
		    ordered_extent->file_offset + ordered_extent->len - 1,
		    GFP_NOFS);
	add_pending_csums(trans, inode, ordered_extent->file_offset,
			  &ordered_extent->list);

	btrfs_ordered_update_i_size(inode, ordered_extent);
	btrfs_remove_ordered_extent(inode, ordered_extent);

	/* once for us */
	btrfs_put_ordered_extent(ordered_extent);
	/* once for the tree */
	btrfs_put_ordered_extent(ordered_extent);

	btrfs_update_inode(trans, root, inode);
	btrfs_end_transaction(trans, root);
	return 0;
}

int btrfs_writepage_end_io_hook(struct page *page, u64 start, u64 end,
				struct extent_state *state, int uptodate)
{
	return btrfs_finish_ordered_io(page->mapping->host, start, end);
}

int btrfs_readpage_io_hook(struct page *page, u64 start, u64 end)
{
	int ret = 0;
	struct inode *inode = page->mapping->host;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_csum_item *item;
	struct btrfs_path *path = NULL;
	u32 csum;

	if (btrfs_test_opt(root, NODATASUM) ||
	    btrfs_test_flag(inode, NODATASUM))
		return 0;

	/*
	 * It is possible there is an ordered extent that has
	 * not yet finished for this range in the file.  If so,
	 * that extent will have a csum cached, and it will insert
	 * the sum after all the blocks in the extent are fully
	 * on disk.  So, look for an ordered extent and use the
	 * sum if found.  We have to do this before looking in the
	 * btree because csum items are pre-inserted based on
	 * the file size.  btrfs_lookup_csum might find an item
	 * that still hasn't been fully filled.
	 */
	ret = btrfs_find_ordered_sum(inode, start, &csum);
	if (ret == 0)
		goto found;

	ret = 0;
	path = btrfs_alloc_path();
	item = btrfs_lookup_csum(NULL, root, path, inode->i_ino, start, 0);
	if (IS_ERR(item)) {
		ret = PTR_ERR(item);
		/* a csum that isn't present is a preallocated region. */
		if (ret == -ENOENT || ret == -EFBIG)
			ret = 0;
		csum = 0;
		printk("no csum found for inode %lu start %Lu\n", inode->i_ino,
		       start);
		goto out;
	}
	read_extent_buffer(path->nodes[0], &csum, (unsigned long)item,
			   BTRFS_CRC32_SIZE);
found:
	set_state_private(io_tree, start, csum);
out:
	if (path)
		btrfs_free_path(path);
	return ret;
}

struct io_failure_record {
	struct page *page;
	u64 start;
	u64 len;
	u64 logical;
	int last_mirror;
};

int btrfs_io_failed_hook(struct bio *failed_bio,
			 struct page *page, u64 start, u64 end,
			 struct extent_state *state)
{
	struct io_failure_record *failrec = NULL;
	u64 private;
	struct extent_map *em;
	struct inode *inode = page->mapping->host;
	struct extent_io_tree *failure_tree = &BTRFS_I(inode)->io_failure_tree;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct bio *bio;
	int num_copies;
	int ret;
	int rw;
	u64 logical;

	ret = get_state_private(failure_tree, start, &private);
	if (ret) {
		failrec = kmalloc(sizeof(*failrec), GFP_NOFS);
		if (!failrec)
			return -ENOMEM;
		failrec->start = start;
		failrec->len = end - start + 1;
		failrec->last_mirror = 0;

		spin_lock(&em_tree->lock);
		em = lookup_extent_mapping(em_tree, start, failrec->len);
		if (em->start > start || em->start + em->len < start) {
			free_extent_map(em);
			em = NULL;
		}
		spin_unlock(&em_tree->lock);

		if (!em || IS_ERR(em)) {
			kfree(failrec);
			return -EIO;
		}
		logical = start - em->start;
		logical = em->block_start + logical;
		failrec->logical = logical;
		free_extent_map(em);
		set_extent_bits(failure_tree, start, end, EXTENT_LOCKED |
				EXTENT_DIRTY, GFP_NOFS);
		set_state_private(failure_tree, start,
				 (u64)(unsigned long)failrec);
	} else {
		failrec = (struct io_failure_record *)(unsigned long)private;
	}
	num_copies = btrfs_num_copies(
			      &BTRFS_I(inode)->root->fs_info->mapping_tree,
			      failrec->logical, failrec->len);
	failrec->last_mirror++;
	if (!state) {
		spin_lock_irq(&BTRFS_I(inode)->io_tree.lock);
		state = find_first_extent_bit_state(&BTRFS_I(inode)->io_tree,
						    failrec->start,
						    EXTENT_LOCKED);
		if (state && state->start != failrec->start)
			state = NULL;
		spin_unlock_irq(&BTRFS_I(inode)->io_tree.lock);
	}
	if (!state || failrec->last_mirror > num_copies) {
		set_state_private(failure_tree, failrec->start, 0);
		clear_extent_bits(failure_tree, failrec->start,
				  failrec->start + failrec->len - 1,
				  EXTENT_LOCKED | EXTENT_DIRTY, GFP_NOFS);
		kfree(failrec);
		return -EIO;
	}
	bio = bio_alloc(GFP_NOFS, 1);
	bio->bi_private = state;
	bio->bi_end_io = failed_bio->bi_end_io;
	bio->bi_sector = failrec->logical >> 9;
	bio->bi_bdev = failed_bio->bi_bdev;
	bio->bi_size = 0;
	bio_add_page(bio, page, failrec->len, start - page_offset(page));
	if (failed_bio->bi_rw & (1 << BIO_RW))
		rw = WRITE;
	else
		rw = READ;

	BTRFS_I(inode)->io_tree.ops->submit_bio_hook(inode, rw, bio,
						      failrec->last_mirror);
	return 0;
}

int btrfs_clean_io_failures(struct inode *inode, u64 start)
{
	u64 private;
	u64 private_failure;
	struct io_failure_record *failure;
	int ret;

	private = 0;
	if (count_range_bits(&BTRFS_I(inode)->io_failure_tree, &private,
			     (u64)-1, 1, EXTENT_DIRTY)) {
		ret = get_state_private(&BTRFS_I(inode)->io_failure_tree,
					start, &private_failure);
		if (ret == 0) {
			failure = (struct io_failure_record *)(unsigned long)
				   private_failure;
			set_state_private(&BTRFS_I(inode)->io_failure_tree,
					  failure->start, 0);
			clear_extent_bits(&BTRFS_I(inode)->io_failure_tree,
					  failure->start,
					  failure->start + failure->len - 1,
					  EXTENT_DIRTY | EXTENT_LOCKED,
					  GFP_NOFS);
			kfree(failure);
		}
	}
	return 0;
}

int btrfs_readpage_end_io_hook(struct page *page, u64 start, u64 end,
			       struct extent_state *state)
{
	size_t offset = start - ((u64)page->index << PAGE_CACHE_SHIFT);
	struct inode *inode = page->mapping->host;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	char *kaddr;
	u64 private = ~(u32)0;
	int ret;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u32 csum = ~(u32)0;
	unsigned long flags;

	if (btrfs_test_opt(root, NODATASUM) ||
	    btrfs_test_flag(inode, NODATASUM))
		return 0;
	if (state && state->start == start) {
		private = state->private;
		ret = 0;
	} else {
		ret = get_state_private(io_tree, start, &private);
	}
	local_irq_save(flags);
	kaddr = kmap_atomic(page, KM_IRQ0);
	if (ret) {
		goto zeroit;
	}
	csum = btrfs_csum_data(root, kaddr + offset, csum,  end - start + 1);
	btrfs_csum_final(csum, (char *)&csum);
	if (csum != private) {
		goto zeroit;
	}
	kunmap_atomic(kaddr, KM_IRQ0);
	local_irq_restore(flags);

	/* if the io failure tree for this inode is non-empty,
	 * check to see if we've recovered from a failed IO
	 */
	btrfs_clean_io_failures(inode, start);
	return 0;

zeroit:
	printk("btrfs csum failed ino %lu off %llu csum %u private %Lu\n",
	       page->mapping->host->i_ino, (unsigned long long)start, csum,
	       private);
	memset(kaddr + offset, 1, end - start + 1);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_IRQ0);
	local_irq_restore(flags);
	if (private == 0)
		return 0;
	return -EIO;
}

/*
 * This creates an orphan entry for the given inode in case something goes
 * wrong in the middle of an unlink/truncate.
 */
int btrfs_orphan_add(struct btrfs_trans_handle *trans, struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;

	spin_lock(&root->orphan_lock);

	/* already on the orphan list, we're good */
	if (!list_empty(&BTRFS_I(inode)->i_orphan)) {
		spin_unlock(&root->orphan_lock);
		return 0;
	}

	list_add(&BTRFS_I(inode)->i_orphan, &root->orphan_list);

	spin_unlock(&root->orphan_lock);

	/*
	 * insert an orphan item to track this unlinked/truncated file
	 */
	ret = btrfs_insert_orphan_item(trans, root, inode->i_ino);

	return ret;
}

/*
 * We have done the truncate/delete so we can go ahead and remove the orphan
 * item for this particular inode.
 */
int btrfs_orphan_del(struct btrfs_trans_handle *trans, struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;

	spin_lock(&root->orphan_lock);

	if (list_empty(&BTRFS_I(inode)->i_orphan)) {
		spin_unlock(&root->orphan_lock);
		return 0;
	}

	list_del_init(&BTRFS_I(inode)->i_orphan);
	if (!trans) {
		spin_unlock(&root->orphan_lock);
		return 0;
	}

	spin_unlock(&root->orphan_lock);

	ret = btrfs_del_orphan_item(trans, root, inode->i_ino);

	return ret;
}

/*
 * this cleans up any orphans that may be left on the list from the last use
 * of this root.
 */
void btrfs_orphan_cleanup(struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	struct btrfs_key key, found_key;
	struct btrfs_trans_handle *trans;
	struct inode *inode;
	int ret = 0, nr_unlink = 0, nr_truncate = 0;

	/* don't do orphan cleanup if the fs is readonly. */
	if (root->inode->i_sb->s_flags & MS_RDONLY)
		return;

	path = btrfs_alloc_path();
	if (!path)
		return;
	path->reada = -1;

	key.objectid = BTRFS_ORPHAN_OBJECTID;
	btrfs_set_key_type(&key, BTRFS_ORPHAN_ITEM_KEY);
	key.offset = (u64)-1;

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, root->inode);

	while (1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0) {
			printk(KERN_ERR "Error searching slot for orphan: %d"
			       "\n", ret);
			break;
		}

		/*
		 * if ret == 0 means we found what we were searching for, which
		 * is weird, but possible, so only screw with path if we didnt
		 * find the key and see if we have stuff that matches
		 */
		if (ret > 0) {
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}

		/* pull out the item */
		leaf = path->nodes[0];
		item = btrfs_item_nr(leaf, path->slots[0]);
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		/* make sure the item matches what we want */
		if (found_key.objectid != BTRFS_ORPHAN_OBJECTID)
			break;
		if (btrfs_key_type(&found_key) != BTRFS_ORPHAN_ITEM_KEY)
			break;

		/* release the path since we're done with it */
		btrfs_release_path(root, path);

		/*
		 * this is where we are basically btrfs_lookup, without the
		 * crossing root thing.  we store the inode number in the
		 * offset of the orphan item.
		 */
		inode = btrfs_iget_locked(root->inode->i_sb,
					  found_key.offset, root);
		if (!inode)
			break;

		if (inode->i_state & I_NEW) {
			BTRFS_I(inode)->root = root;

			/* have to set the location manually */
			BTRFS_I(inode)->location.objectid = inode->i_ino;
			BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
			BTRFS_I(inode)->location.offset = 0;

			btrfs_read_locked_inode(inode);
			unlock_new_inode(inode);
		}

		/*
		 * add this inode to the orphan list so btrfs_orphan_del does
		 * the proper thing when we hit it
		 */
		spin_lock(&root->orphan_lock);
		list_add(&BTRFS_I(inode)->i_orphan, &root->orphan_list);
		spin_unlock(&root->orphan_lock);

		/*
		 * if this is a bad inode, means we actually succeeded in
		 * removing the inode, but not the orphan record, which means
		 * we need to manually delete the orphan since iput will just
		 * do a destroy_inode
		 */
		if (is_bad_inode(inode)) {
			btrfs_orphan_del(trans, inode);
			iput(inode);
			continue;
		}

		/* if we have links, this was a truncate, lets do that */
		if (inode->i_nlink) {
			nr_truncate++;
			btrfs_truncate(inode);
		} else {
			nr_unlink++;
		}

		/* this will do delete_inode and everything for us */
		iput(inode);
	}

	if (nr_unlink)
		printk(KERN_INFO "btrfs: unlinked %d orphans\n", nr_unlink);
	if (nr_truncate)
		printk(KERN_INFO "btrfs: truncated %d orphans\n", nr_truncate);

	btrfs_free_path(path);
	btrfs_end_transaction(trans, root);
}

void btrfs_read_locked_inode(struct inode *inode)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_inode_item *inode_item;
	struct btrfs_timespec *tspec;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_key location;
	u64 alloc_group_block;
	u32 rdev;
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	memcpy(&location, &BTRFS_I(inode)->location, sizeof(location));

	ret = btrfs_lookup_inode(NULL, root, path, &location, 0);
	if (ret)
		goto make_bad;

	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);

	inode->i_mode = btrfs_inode_mode(leaf, inode_item);
	inode->i_nlink = btrfs_inode_nlink(leaf, inode_item);
	inode->i_uid = btrfs_inode_uid(leaf, inode_item);
	inode->i_gid = btrfs_inode_gid(leaf, inode_item);
	btrfs_i_size_write(inode, btrfs_inode_size(leaf, inode_item));

	tspec = btrfs_inode_atime(inode_item);
	inode->i_atime.tv_sec = btrfs_timespec_sec(leaf, tspec);
	inode->i_atime.tv_nsec = btrfs_timespec_nsec(leaf, tspec);

	tspec = btrfs_inode_mtime(inode_item);
	inode->i_mtime.tv_sec = btrfs_timespec_sec(leaf, tspec);
	inode->i_mtime.tv_nsec = btrfs_timespec_nsec(leaf, tspec);

	tspec = btrfs_inode_ctime(inode_item);
	inode->i_ctime.tv_sec = btrfs_timespec_sec(leaf, tspec);
	inode->i_ctime.tv_nsec = btrfs_timespec_nsec(leaf, tspec);

	inode->i_blocks = btrfs_inode_nblocks(leaf, inode_item);
	inode->i_generation = btrfs_inode_generation(leaf, inode_item);
	inode->i_rdev = 0;
	rdev = btrfs_inode_rdev(leaf, inode_item);

	BTRFS_I(inode)->index_cnt = (u64)-1;

	alloc_group_block = btrfs_inode_block_group(leaf, inode_item);
	BTRFS_I(inode)->block_group = btrfs_lookup_block_group(root->fs_info,
						       alloc_group_block);
	BTRFS_I(inode)->flags = btrfs_inode_flags(leaf, inode_item);
	if (!BTRFS_I(inode)->block_group) {
		BTRFS_I(inode)->block_group = btrfs_find_block_group(root,
						 NULL, 0,
						 BTRFS_BLOCK_GROUP_METADATA, 0);
	}
	btrfs_free_path(path);
	inode_item = NULL;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
		BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
		break;
	case S_IFDIR:
		inode->i_fop = &btrfs_dir_file_operations;
		if (root == root->fs_info->tree_root)
			inode->i_op = &btrfs_dir_ro_inode_operations;
		else
			inode->i_op = &btrfs_dir_inode_operations;
		break;
	case S_IFLNK:
		inode->i_op = &btrfs_symlink_inode_operations;
		inode->i_mapping->a_ops = &btrfs_symlink_aops;
		inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
		break;
	default:
		init_special_inode(inode, inode->i_mode, rdev);
		break;
	}
	return;

make_bad:
	btrfs_free_path(path);
	make_bad_inode(inode);
}

static void fill_inode_item(struct extent_buffer *leaf,
			    struct btrfs_inode_item *item,
			    struct inode *inode)
{
	btrfs_set_inode_uid(leaf, item, inode->i_uid);
	btrfs_set_inode_gid(leaf, item, inode->i_gid);
	btrfs_set_inode_size(leaf, item, BTRFS_I(inode)->disk_i_size);
	btrfs_set_inode_mode(leaf, item, inode->i_mode);
	btrfs_set_inode_nlink(leaf, item, inode->i_nlink);

	btrfs_set_timespec_sec(leaf, btrfs_inode_atime(item),
			       inode->i_atime.tv_sec);
	btrfs_set_timespec_nsec(leaf, btrfs_inode_atime(item),
				inode->i_atime.tv_nsec);

	btrfs_set_timespec_sec(leaf, btrfs_inode_mtime(item),
			       inode->i_mtime.tv_sec);
	btrfs_set_timespec_nsec(leaf, btrfs_inode_mtime(item),
				inode->i_mtime.tv_nsec);

	btrfs_set_timespec_sec(leaf, btrfs_inode_ctime(item),
			       inode->i_ctime.tv_sec);
	btrfs_set_timespec_nsec(leaf, btrfs_inode_ctime(item),
				inode->i_ctime.tv_nsec);

	btrfs_set_inode_nblocks(leaf, item, inode->i_blocks);
	btrfs_set_inode_generation(leaf, item, inode->i_generation);
	btrfs_set_inode_rdev(leaf, item, inode->i_rdev);
	btrfs_set_inode_flags(leaf, item, BTRFS_I(inode)->flags);
	btrfs_set_inode_block_group(leaf, item,
				    BTRFS_I(inode)->block_group->key.objectid);
}

int noinline btrfs_update_inode(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct inode *inode)
{
	struct btrfs_inode_item *inode_item;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_lookup_inode(trans, root, path,
				 &BTRFS_I(inode)->location, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto failed;
	}

	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				  struct btrfs_inode_item);

	fill_inode_item(leaf, inode_item, inode);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_set_inode_last_trans(trans, inode);
	ret = 0;
failed:
	btrfs_free_path(path);
	return ret;
}


static int btrfs_unlink_trans(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct inode *dir,
			      struct dentry *dentry)
{
	struct btrfs_path *path;
	const char *name = dentry->d_name.name;
	int name_len = dentry->d_name.len;
	int ret = 0;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	u64 index;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto err;
	}

	di = btrfs_lookup_dir_item(trans, root, path, dir->i_ino,
				    name, name_len, -1);
	if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto err;
	}
	if (!di) {
		ret = -ENOENT;
		goto err;
	}
	leaf = path->nodes[0];
	btrfs_dir_item_key_to_cpu(leaf, di, &key);
	ret = btrfs_delete_one_dir_name(trans, root, path, di);
	if (ret)
		goto err;
	btrfs_release_path(root, path);

	ret = btrfs_del_inode_ref(trans, root, name, name_len,
				  dentry->d_inode->i_ino,
				  dentry->d_parent->d_inode->i_ino, &index);
	if (ret) {
		printk("failed to delete reference to %.*s, "
		       "inode %lu parent %lu\n", name_len, name,
		       dentry->d_inode->i_ino,
		       dentry->d_parent->d_inode->i_ino);
		goto err;
	}

	di = btrfs_lookup_dir_index_item(trans, root, path, dir->i_ino,
					 index, name, name_len, -1);
	if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto err;
	}
	if (!di) {
		ret = -ENOENT;
		goto err;
	}
	ret = btrfs_delete_one_dir_name(trans, root, path, di);
	btrfs_release_path(root, path);

	dentry->d_inode->i_ctime = dir->i_ctime;
err:
	btrfs_free_path(path);
	if (!ret) {
		btrfs_i_size_write(dir, dir->i_size - name_len * 2);
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
		btrfs_update_inode(trans, root, dir);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
		dentry->d_inode->i_nlink--;
#else
		drop_nlink(dentry->d_inode);
#endif
		ret = btrfs_update_inode(trans, root, dentry->d_inode);
		dir->i_sb->s_dirt = 1;
	}
	return ret;
}

static int btrfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_root *root;
	struct btrfs_trans_handle *trans;
	struct inode *inode = dentry->d_inode;
	int ret;
	unsigned long nr = 0;

	root = BTRFS_I(dir)->root;

	ret = btrfs_check_free_space(root, 1, 1);
	if (ret)
		goto fail;

	trans = btrfs_start_transaction(root, 1);

	btrfs_set_trans_block_group(trans, dir);
	ret = btrfs_unlink_trans(trans, root, dir, dentry);

	if (inode->i_nlink == 0)
		ret = btrfs_orphan_add(trans, inode);

	nr = trans->blocks_used;

	btrfs_end_transaction_throttle(trans, root);
fail:
	btrfs_btree_balance_dirty(root, nr);
	return ret;
}

static int btrfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int err = 0;
	int ret;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_trans_handle *trans;
	unsigned long nr = 0;

	if (inode->i_size > BTRFS_EMPTY_DIR_SIZE) {
		return -ENOTEMPTY;
	}

	ret = btrfs_check_free_space(root, 1, 1);
	if (ret)
		goto fail;

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_orphan_add(trans, inode);
	if (err)
		goto fail_trans;

	/* now the directory is empty */
	err = btrfs_unlink_trans(trans, root, dir, dentry);
	if (!err) {
		btrfs_i_size_write(inode, 0);
	}

fail_trans:
	nr = trans->blocks_used;
	ret = btrfs_end_transaction_throttle(trans, root);
fail:
	btrfs_btree_balance_dirty(root, nr);

	if (ret && !err)
		err = ret;
	return err;
}

/*
 * this can truncate away extent items, csum items and directory items.
 * It starts at a high offset and removes keys until it can't find
 * any higher than i_size.
 *
 * csum items that cross the new i_size are truncated to the new size
 * as well.
 *
 * min_type is the minimum key type to truncate down to.  If set to 0, this
 * will kill all the items on this inode, including the INODE_ITEM_KEY.
 */
static int btrfs_truncate_in_trans(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct inode *inode,
				   u32 min_type)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u32 found_type;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	u64 extent_start = 0;
	u64 extent_num_bytes = 0;
	u64 item_end = 0;
	u64 root_gen = 0;
	u64 root_owner = 0;
	int found_extent;
	int del_item;
	int pending_del_nr = 0;
	int pending_del_slot = 0;
	int extent_type = -1;
	u64 mask = root->sectorsize - 1;

	btrfs_drop_extent_cache(inode, inode->i_size & (~mask), (u64)-1);
	path = btrfs_alloc_path();
	path->reada = -1;
	BUG_ON(!path);

	/* FIXME, add redo link to tree so we don't leak on crash */
	key.objectid = inode->i_ino;
	key.offset = (u64)-1;
	key.type = (u8)-1;

	btrfs_init_path(path);
search_again:
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0) {
		goto error;
	}
	if (ret > 0) {
		BUG_ON(path->slots[0] == 0);
		path->slots[0]--;
	}

	while(1) {
		fi = NULL;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		found_type = btrfs_key_type(&found_key);

		if (found_key.objectid != inode->i_ino)
			break;

		if (found_type < min_type)
			break;

		item_end = found_key.offset;
		if (found_type == BTRFS_EXTENT_DATA_KEY) {
			fi = btrfs_item_ptr(leaf, path->slots[0],
					    struct btrfs_file_extent_item);
			extent_type = btrfs_file_extent_type(leaf, fi);
			if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
				item_end +=
				    btrfs_file_extent_num_bytes(leaf, fi);
			} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
				struct btrfs_item *item = btrfs_item_nr(leaf,
							        path->slots[0]);
				item_end += btrfs_file_extent_inline_len(leaf,
									 item);
			}
			item_end--;
		}
		if (found_type == BTRFS_CSUM_ITEM_KEY) {
			ret = btrfs_csum_truncate(trans, root, path,
						  inode->i_size);
			BUG_ON(ret);
		}
		if (item_end < inode->i_size) {
			if (found_type == BTRFS_DIR_ITEM_KEY) {
				found_type = BTRFS_INODE_ITEM_KEY;
			} else if (found_type == BTRFS_EXTENT_ITEM_KEY) {
				found_type = BTRFS_CSUM_ITEM_KEY;
			} else if (found_type == BTRFS_EXTENT_DATA_KEY) {
				found_type = BTRFS_XATTR_ITEM_KEY;
			} else if (found_type == BTRFS_XATTR_ITEM_KEY) {
				found_type = BTRFS_INODE_REF_KEY;
			} else if (found_type) {
				found_type--;
			} else {
				break;
			}
			btrfs_set_key_type(&key, found_type);
			goto next;
		}
		if (found_key.offset >= inode->i_size)
			del_item = 1;
		else
			del_item = 0;
		found_extent = 0;

		/* FIXME, shrink the extent if the ref count is only 1 */
		if (found_type != BTRFS_EXTENT_DATA_KEY)
			goto delete;

		if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
			u64 num_dec;
			extent_start = btrfs_file_extent_disk_bytenr(leaf, fi);
			if (!del_item) {
				u64 orig_num_bytes =
					btrfs_file_extent_num_bytes(leaf, fi);
				extent_num_bytes = inode->i_size -
					found_key.offset + root->sectorsize - 1;
				extent_num_bytes = extent_num_bytes &
					~((u64)root->sectorsize - 1);
				btrfs_set_file_extent_num_bytes(leaf, fi,
							 extent_num_bytes);
				num_dec = (orig_num_bytes -
					   extent_num_bytes);
				if (extent_start != 0)
					dec_i_blocks(inode, num_dec);
				btrfs_mark_buffer_dirty(leaf);
			} else {
				extent_num_bytes =
					btrfs_file_extent_disk_num_bytes(leaf,
									 fi);
				/* FIXME blocksize != 4096 */
				num_dec = btrfs_file_extent_num_bytes(leaf, fi);
				if (extent_start != 0) {
					found_extent = 1;
					dec_i_blocks(inode, num_dec);
				}
				root_gen = btrfs_header_generation(leaf);
				root_owner = btrfs_header_owner(leaf);
			}
		} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
			if (!del_item) {
				u32 newsize = inode->i_size - found_key.offset;
				dec_i_blocks(inode, item_end + 1 -
					    found_key.offset - newsize);
				newsize =
				    btrfs_file_extent_calc_inline_size(newsize);
				ret = btrfs_truncate_item(trans, root, path,
							  newsize, 1);
				BUG_ON(ret);
			} else {
				dec_i_blocks(inode, item_end + 1 -
					     found_key.offset);
			}
		}
delete:
		if (del_item) {
			if (!pending_del_nr) {
				/* no pending yet, add ourselves */
				pending_del_slot = path->slots[0];
				pending_del_nr = 1;
			} else if (pending_del_nr &&
				   path->slots[0] + 1 == pending_del_slot) {
				/* hop on the pending chunk */
				pending_del_nr++;
				pending_del_slot = path->slots[0];
			} else {
				printk("bad pending slot %d pending_del_nr %d pending_del_slot %d\n", path->slots[0], pending_del_nr, pending_del_slot);
			}
		} else {
			break;
		}
		if (found_extent) {
			ret = btrfs_free_extent(trans, root, extent_start,
						extent_num_bytes,
						root_owner,
						root_gen, inode->i_ino,
						found_key.offset, 0);
			BUG_ON(ret);
		}
next:
		if (path->slots[0] == 0) {
			if (pending_del_nr)
				goto del_pending;
			btrfs_release_path(root, path);
			goto search_again;
		}

		path->slots[0]--;
		if (pending_del_nr &&
		    path->slots[0] + 1 != pending_del_slot) {
			struct btrfs_key debug;
del_pending:
			btrfs_item_key_to_cpu(path->nodes[0], &debug,
					      pending_del_slot);
			ret = btrfs_del_items(trans, root, path,
					      pending_del_slot,
					      pending_del_nr);
			BUG_ON(ret);
			pending_del_nr = 0;
			btrfs_release_path(root, path);
			goto search_again;
		}
	}
	ret = 0;
error:
	if (pending_del_nr) {
		ret = btrfs_del_items(trans, root, path, pending_del_slot,
				      pending_del_nr);
	}
	btrfs_free_path(path);
	inode->i_sb->s_dirt = 1;
	return ret;
}

/*
 * taken from block_truncate_page, but does cow as it zeros out
 * any bytes left in the last page in the file.
 */
static int btrfs_truncate_page(struct address_space *mapping, loff_t from)
{
	struct inode *inode = mapping->host;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_ordered_extent *ordered;
	char *kaddr;
	u32 blocksize = root->sectorsize;
	pgoff_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	struct page *page;
	int ret = 0;
	u64 page_start;
	u64 page_end;

	if ((offset & (blocksize - 1)) == 0)
		goto out;

	ret = -ENOMEM;
again:
	page = grab_cache_page(mapping, index);
	if (!page)
		goto out;

	page_start = page_offset(page);
	page_end = page_start + PAGE_CACHE_SIZE - 1;

	if (!PageUptodate(page)) {
		ret = btrfs_readpage(NULL, page);
		lock_page(page);
		if (page->mapping != mapping) {
			unlock_page(page);
			page_cache_release(page);
			goto again;
		}
		if (!PageUptodate(page)) {
			ret = -EIO;
			goto out_unlock;
		}
	}
	wait_on_page_writeback(page);

	lock_extent(io_tree, page_start, page_end, GFP_NOFS);
	set_page_extent_mapped(page);

	ordered = btrfs_lookup_ordered_extent(inode, page_start);
	if (ordered) {
		unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
		unlock_page(page);
		page_cache_release(page);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	set_extent_delalloc(&BTRFS_I(inode)->io_tree, page_start,
			    page_end, GFP_NOFS);
	ret = 0;
	if (offset != PAGE_CACHE_SIZE) {
		kaddr = kmap(page);
		memset(kaddr + offset, 0, PAGE_CACHE_SIZE - offset);
		flush_dcache_page(page);
		kunmap(page);
	}
	ClearPageChecked(page);
	set_page_dirty(page);
	unlock_extent(io_tree, page_start, page_end, GFP_NOFS);

out_unlock:
	unlock_page(page);
	page_cache_release(page);
out:
	return ret;
}

static int btrfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int err;

	err = inode_change_ok(inode, attr);
	if (err)
		return err;

	if (S_ISREG(inode->i_mode) &&
	    attr->ia_valid & ATTR_SIZE && attr->ia_size > inode->i_size) {
		struct btrfs_trans_handle *trans;
		struct btrfs_root *root = BTRFS_I(inode)->root;
		struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;

		u64 mask = root->sectorsize - 1;
		u64 hole_start = (inode->i_size + mask) & ~mask;
		u64 block_end = (attr->ia_size + mask) & ~mask;
		u64 hole_size;
		u64 alloc_hint = 0;

		if (attr->ia_size <= hole_start)
			goto out;

		err = btrfs_check_free_space(root, 1, 0);
		if (err)
			goto fail;

		btrfs_truncate_page(inode->i_mapping, inode->i_size);

		hole_size = block_end - hole_start;
		btrfs_wait_ordered_range(inode, hole_start, hole_size);
		lock_extent(io_tree, hole_start, block_end - 1, GFP_NOFS);

		trans = btrfs_start_transaction(root, 1);
		btrfs_set_trans_block_group(trans, inode);
		mutex_lock(&BTRFS_I(inode)->extent_mutex);
		err = btrfs_drop_extents(trans, root, inode,
					 hole_start, block_end, hole_start,
					 &alloc_hint);

		if (alloc_hint != EXTENT_MAP_INLINE) {
			err = btrfs_insert_file_extent(trans, root,
						       inode->i_ino,
						       hole_start, 0, 0,
						       hole_size, 0);
			btrfs_drop_extent_cache(inode, hole_start,
						(u64)-1);
			btrfs_check_file(root, inode);
		}
		mutex_unlock(&BTRFS_I(inode)->extent_mutex);
		btrfs_end_transaction(trans, root);
		unlock_extent(io_tree, hole_start, block_end - 1, GFP_NOFS);
		if (err)
			return err;
	}
out:
	err = inode_setattr(inode, attr);

	if (!err && ((attr->ia_valid & ATTR_MODE)))
		err = btrfs_acl_chmod(inode);
fail:
	return err;
}

void btrfs_delete_inode(struct inode *inode)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	unsigned long nr;
	int ret;

	truncate_inode_pages(&inode->i_data, 0);
	if (is_bad_inode(inode)) {
		btrfs_orphan_del(NULL, inode);
		goto no_delete;
	}
	btrfs_wait_ordered_range(inode, 0, (u64)-1);

	btrfs_i_size_write(inode, 0);
	trans = btrfs_start_transaction(root, 1);

	btrfs_set_trans_block_group(trans, inode);
	ret = btrfs_truncate_in_trans(trans, root, inode, 0);
	if (ret) {
		btrfs_orphan_del(NULL, inode);
		goto no_delete_lock;
	}

	btrfs_orphan_del(trans, inode);

	nr = trans->blocks_used;
	clear_inode(inode);

	btrfs_end_transaction(trans, root);
	btrfs_btree_balance_dirty(root, nr);
	return;

no_delete_lock:
	nr = trans->blocks_used;
	btrfs_end_transaction(trans, root);
	btrfs_btree_balance_dirty(root, nr);
no_delete:
	clear_inode(inode);
}

/*
 * this returns the key found in the dir entry in the location pointer.
 * If no dir entries were found, location->objectid is 0.
 */
static int btrfs_inode_by_name(struct inode *dir, struct dentry *dentry,
			       struct btrfs_key *location)
{
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct btrfs_dir_item *di;
	struct btrfs_path *path;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	int ret = 0;

	if (namelen == 1 && strcmp(name, ".") == 0) {
		location->objectid = dir->i_ino;
		location->type = BTRFS_INODE_ITEM_KEY;
		location->offset = 0;
		return 0;
	}
	path = btrfs_alloc_path();
	BUG_ON(!path);

	if (namelen == 2 && strcmp(name, "..") == 0) {
		struct btrfs_key key;
		struct extent_buffer *leaf;
		u32 nritems;
		int slot;

		key.objectid = dir->i_ino;
		btrfs_set_key_type(&key, BTRFS_INODE_REF_KEY);
		key.offset = 0;
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		BUG_ON(ret == 0);
		ret = 0;

		leaf = path->nodes[0];
		slot = path->slots[0];
		nritems = btrfs_header_nritems(leaf);
		if (slot >= nritems)
			goto out_err;

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid != dir->i_ino ||
		    key.type != BTRFS_INODE_REF_KEY) {
			goto out_err;
		}
		location->objectid = key.offset;
		location->type = BTRFS_INODE_ITEM_KEY;
		location->offset = 0;
		goto out;
	}

	di = btrfs_lookup_dir_item(NULL, root, path, dir->i_ino, name,
				    namelen, 0);
	if (IS_ERR(di))
		ret = PTR_ERR(di);
	if (!di || IS_ERR(di)) {
		goto out_err;
	}
	btrfs_dir_item_key_to_cpu(path->nodes[0], di, location);
out:
	btrfs_free_path(path);
	return ret;
out_err:
	location->objectid = 0;
	goto out;
}

/*
 * when we hit a tree root in a directory, the btrfs part of the inode
 * needs to be changed to reflect the root directory of the tree root.  This
 * is kind of like crossing a mount point.
 */
static int fixup_tree_root_location(struct btrfs_root *root,
			     struct btrfs_key *location,
			     struct btrfs_root **sub_root,
			     struct dentry *dentry)
{
	struct btrfs_path *path;
	struct btrfs_root_item *ri;

	if (btrfs_key_type(location) != BTRFS_ROOT_ITEM_KEY)
		return 0;
	if (location->objectid == BTRFS_ROOT_TREE_OBJECTID)
		return 0;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	*sub_root = btrfs_read_fs_root(root->fs_info, location,
					dentry->d_name.name,
					dentry->d_name.len);
	if (IS_ERR(*sub_root))
		return PTR_ERR(*sub_root);

	ri = &(*sub_root)->root_item;
	location->objectid = btrfs_root_dirid(ri);
	btrfs_set_key_type(location, BTRFS_INODE_ITEM_KEY);
	location->offset = 0;

	btrfs_free_path(path);
	return 0;
}

static int btrfs_init_locked_inode(struct inode *inode, void *p)
{
	struct btrfs_iget_args *args = p;
	inode->i_ino = args->ino;
	BTRFS_I(inode)->root = args->root;
	BTRFS_I(inode)->delalloc_bytes = 0;
	BTRFS_I(inode)->disk_i_size = 0;
	BTRFS_I(inode)->index_cnt = (u64)-1;
	extent_map_tree_init(&BTRFS_I(inode)->extent_tree, GFP_NOFS);
	extent_io_tree_init(&BTRFS_I(inode)->io_tree,
			     inode->i_mapping, GFP_NOFS);
	extent_io_tree_init(&BTRFS_I(inode)->io_failure_tree,
			     inode->i_mapping, GFP_NOFS);
	btrfs_ordered_inode_tree_init(&BTRFS_I(inode)->ordered_tree);
	mutex_init(&BTRFS_I(inode)->csum_mutex);
	mutex_init(&BTRFS_I(inode)->extent_mutex);
	return 0;
}

static int btrfs_find_actor(struct inode *inode, void *opaque)
{
	struct btrfs_iget_args *args = opaque;
	return (args->ino == inode->i_ino &&
		args->root == BTRFS_I(inode)->root);
}

struct inode *btrfs_ilookup(struct super_block *s, u64 objectid,
			    u64 root_objectid)
{
	struct btrfs_iget_args args;
	args.ino = objectid;
	args.root = btrfs_lookup_fs_root(btrfs_sb(s)->fs_info, root_objectid);

	if (!args.root)
		return NULL;

	return ilookup5(s, objectid, btrfs_find_actor, (void *)&args);
}

struct inode *btrfs_iget_locked(struct super_block *s, u64 objectid,
				struct btrfs_root *root)
{
	struct inode *inode;
	struct btrfs_iget_args args;
	args.ino = objectid;
	args.root = root;

	inode = iget5_locked(s, objectid, btrfs_find_actor,
			     btrfs_init_locked_inode,
			     (void *)&args);
	return inode;
}

static struct dentry *btrfs_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	struct inode * inode;
	struct btrfs_inode *bi = BTRFS_I(dir);
	struct btrfs_root *root = bi->root;
	struct btrfs_root *sub_root = root;
	struct btrfs_key location;
	int ret, do_orphan = 0;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ret = btrfs_inode_by_name(dir, dentry, &location);

	if (ret < 0)
		return ERR_PTR(ret);

	inode = NULL;
	if (location.objectid) {
		ret = fixup_tree_root_location(root, &location, &sub_root,
						dentry);
		if (ret < 0)
			return ERR_PTR(ret);
		if (ret > 0)
			return ERR_PTR(-ENOENT);

		inode = btrfs_iget_locked(dir->i_sb, location.objectid,
					  sub_root);
		if (!inode)
			return ERR_PTR(-EACCES);
		if (inode->i_state & I_NEW) {
			/* the inode and parent dir are two different roots */
			if (sub_root != root) {
				igrab(inode);
				sub_root->inode = inode;
				do_orphan = 1;
			}
			BTRFS_I(inode)->root = sub_root;
			memcpy(&BTRFS_I(inode)->location, &location,
			       sizeof(location));
			btrfs_read_locked_inode(inode);
			unlock_new_inode(inode);
		}
	}

	if (unlikely(do_orphan))
		btrfs_orphan_cleanup(sub_root);

	return d_splice_alias(inode, dentry);
}

static unsigned char btrfs_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_FIFO, DT_SOCK, DT_LNK
};

static int btrfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_item *item;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	int ret;
	u32 nritems;
	struct extent_buffer *leaf;
	int slot;
	int advance;
	unsigned char d_type;
	int over = 0;
	u32 di_cur;
	u32 di_total;
	u32 di_len;
	int key_type = BTRFS_DIR_INDEX_KEY;
	char tmp_name[32];
	char *name_ptr;
	int name_len;

	/* FIXME, use a real flag for deciding about the key type */
	if (root->fs_info->tree_root == root)
		key_type = BTRFS_DIR_ITEM_KEY;

	/* special case for "." */
	if (filp->f_pos == 0) {
		over = filldir(dirent, ".", 1,
			       1, inode->i_ino,
			       DT_DIR);
		if (over)
			return 0;
		filp->f_pos = 1;
	}

	key.objectid = inode->i_ino;
	path = btrfs_alloc_path();
	path->reada = 2;

	/* special case for .., just use the back ref */
	if (filp->f_pos == 1) {
		btrfs_set_key_type(&key, BTRFS_INODE_REF_KEY);
		key.offset = 0;
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		BUG_ON(ret == 0);
		leaf = path->nodes[0];
		slot = path->slots[0];
		nritems = btrfs_header_nritems(leaf);
		if (slot >= nritems) {
			btrfs_release_path(root, path);
			goto read_dir_items;
		}
		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		btrfs_release_path(root, path);
		if (found_key.objectid != key.objectid ||
		    found_key.type != BTRFS_INODE_REF_KEY)
			goto read_dir_items;
		over = filldir(dirent, "..", 2,
			       2, found_key.offset, DT_DIR);
		if (over)
			goto nopos;
		filp->f_pos = 2;
	}

read_dir_items:
	btrfs_set_key_type(&key, key_type);
	key.offset = filp->f_pos;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;
	advance = 0;
	while(1) {
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		slot = path->slots[0];
		if (advance || slot >= nritems) {
			if (slot >= nritems -1) {
				ret = btrfs_next_leaf(root, path);
				if (ret)
					break;
				leaf = path->nodes[0];
				nritems = btrfs_header_nritems(leaf);
				slot = path->slots[0];
			} else {
				slot++;
				path->slots[0]++;
			}
		}
		advance = 1;
		item = btrfs_item_nr(leaf, slot);
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid != key.objectid)
			break;
		if (btrfs_key_type(&found_key) != key_type)
			break;
		if (found_key.offset < filp->f_pos)
			continue;

		filp->f_pos = found_key.offset;
		advance = 1;
		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
		di_cur = 0;
		di_total = btrfs_item_size(leaf, item);
		while(di_cur < di_total) {
			struct btrfs_key location;

			name_len = btrfs_dir_name_len(leaf, di);
			if (name_len < 32) {
				name_ptr = tmp_name;
			} else {
				name_ptr = kmalloc(name_len, GFP_NOFS);
				BUG_ON(!name_ptr);
			}
			read_extent_buffer(leaf, name_ptr,
					   (unsigned long)(di + 1), name_len);

			d_type = btrfs_filetype_table[btrfs_dir_type(leaf, di)];
			btrfs_dir_item_key_to_cpu(leaf, di, &location);
			over = filldir(dirent, name_ptr, name_len,
				       found_key.offset,
				       location.objectid,
				       d_type);

			if (name_ptr != tmp_name)
				kfree(name_ptr);

			if (over)
				goto nopos;
			di_len = btrfs_dir_name_len(leaf, di) +
				btrfs_dir_data_len(leaf, di) +sizeof(*di);
			di_cur += di_len;
			di = (struct btrfs_dir_item *)((char *)di + di_len);
		}
	}
	if (key_type == BTRFS_DIR_INDEX_KEY)
		filp->f_pos = INT_LIMIT(typeof(filp->f_pos));
	else
		filp->f_pos++;
nopos:
	ret = 0;
err:
	btrfs_free_path(path);
	return ret;
}

int btrfs_write_inode(struct inode *inode, int wait)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	int ret = 0;

	if (wait) {
		trans = btrfs_join_transaction(root, 1);
		btrfs_set_trans_block_group(trans, inode);
		ret = btrfs_commit_transaction(trans, root);
	}
	return ret;
}

/*
 * This is somewhat expensive, updating the tree every time the
 * inode changes.  But, it is most likely to find the inode in cache.
 * FIXME, needs more benchmarking...there are no reasons other than performance
 * to keep or drop this code.
 */
void btrfs_dirty_inode(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;

	trans = btrfs_join_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);
	btrfs_update_inode(trans, root, inode);
	btrfs_end_transaction(trans, root);
}

static int btrfs_set_inode_index_count(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_key key, found_key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret;

	key.objectid = inode->i_ino;
	btrfs_set_key_type(&key, BTRFS_DIR_INDEX_KEY);
	key.offset = (u64)-1;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	/* FIXME: we should be able to handle this */
	if (ret == 0)
		goto out;
	ret = 0;

	/*
	 * MAGIC NUMBER EXPLANATION:
	 * since we search a directory based on f_pos we have to start at 2
	 * since '.' and '..' have f_pos of 0 and 1 respectively, so everybody
	 * else has to start at 2
	 */
	if (path->slots[0] == 0) {
		BTRFS_I(inode)->index_cnt = 2;
		goto out;
	}

	path->slots[0]--;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

	if (found_key.objectid != inode->i_ino ||
	    btrfs_key_type(&found_key) != BTRFS_DIR_INDEX_KEY) {
		BTRFS_I(inode)->index_cnt = 2;
		goto out;
	}

	BTRFS_I(inode)->index_cnt = found_key.offset + 1;
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_set_inode_index(struct inode *dir, struct inode *inode)
{
	int ret = 0;

	if (BTRFS_I(dir)->index_cnt == (u64)-1) {
		ret = btrfs_set_inode_index_count(dir);
		if (ret)
			return ret;
	}

	BTRFS_I(inode)->index = BTRFS_I(dir)->index_cnt;
	BTRFS_I(dir)->index_cnt++;

	return ret;
}

static struct inode *btrfs_new_inode(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct inode *dir,
				     const char *name, int name_len,
				     u64 ref_objectid,
				     u64 objectid,
				     struct btrfs_block_group_cache *group,
				     int mode)
{
	struct inode *inode;
	struct btrfs_inode_item *inode_item;
	struct btrfs_block_group_cache *new_inode_group;
	struct btrfs_key *location;
	struct btrfs_path *path;
	struct btrfs_inode_ref *ref;
	struct btrfs_key key[2];
	u32 sizes[2];
	unsigned long ptr;
	int ret;
	int owner;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	inode = new_inode(root->fs_info->sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (dir) {
		ret = btrfs_set_inode_index(dir, inode);
		if (ret)
			return ERR_PTR(ret);
	} else {
		BTRFS_I(inode)->index = 0;
	}
	/*
	 * index_cnt is ignored for everything but a dir,
	 * btrfs_get_inode_index_count has an explanation for the magic
	 * number
	 */
	BTRFS_I(inode)->index_cnt = 2;

	extent_map_tree_init(&BTRFS_I(inode)->extent_tree, GFP_NOFS);
	extent_io_tree_init(&BTRFS_I(inode)->io_tree,
			     inode->i_mapping, GFP_NOFS);
	extent_io_tree_init(&BTRFS_I(inode)->io_failure_tree,
			     inode->i_mapping, GFP_NOFS);
	btrfs_ordered_inode_tree_init(&BTRFS_I(inode)->ordered_tree);
	mutex_init(&BTRFS_I(inode)->csum_mutex);
	mutex_init(&BTRFS_I(inode)->extent_mutex);
	BTRFS_I(inode)->delalloc_bytes = 0;
	BTRFS_I(inode)->disk_i_size = 0;
	BTRFS_I(inode)->root = root;

	if (mode & S_IFDIR)
		owner = 0;
	else
		owner = 1;
	new_inode_group = btrfs_find_block_group(root, group, 0,
				       BTRFS_BLOCK_GROUP_METADATA, owner);
	if (!new_inode_group) {
		printk("find_block group failed\n");
		new_inode_group = group;
	}
	BTRFS_I(inode)->block_group = new_inode_group;
	BTRFS_I(inode)->flags = 0;

	key[0].objectid = objectid;
	btrfs_set_key_type(&key[0], BTRFS_INODE_ITEM_KEY);
	key[0].offset = 0;

	key[1].objectid = objectid;
	btrfs_set_key_type(&key[1], BTRFS_INODE_REF_KEY);
	key[1].offset = ref_objectid;

	sizes[0] = sizeof(struct btrfs_inode_item);
	sizes[1] = name_len + sizeof(*ref);

	ret = btrfs_insert_empty_items(trans, root, path, key, sizes, 2);
	if (ret != 0)
		goto fail;

	if (objectid > root->highest_inode)
		root->highest_inode = objectid;

	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_mode = mode;
	inode->i_ino = objectid;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode_item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				  struct btrfs_inode_item);
	fill_inode_item(path->nodes[0], inode_item, inode);

	ref = btrfs_item_ptr(path->nodes[0], path->slots[0] + 1,
			     struct btrfs_inode_ref);
	btrfs_set_inode_ref_name_len(path->nodes[0], ref, name_len);
	btrfs_set_inode_ref_index(path->nodes[0], ref, BTRFS_I(inode)->index);
	ptr = (unsigned long)(ref + 1);
	write_extent_buffer(path->nodes[0], name, ptr, name_len);

	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_free_path(path);

	location = &BTRFS_I(inode)->location;
	location->objectid = objectid;
	location->offset = 0;
	btrfs_set_key_type(location, BTRFS_INODE_ITEM_KEY);

	insert_inode_hash(inode);
	return inode;
fail:
	if (dir)
		BTRFS_I(dir)->index_cnt--;
	btrfs_free_path(path);
	return ERR_PTR(ret);
}

static inline u8 btrfs_inode_type(struct inode *inode)
{
	return btrfs_type_by_mode[(inode->i_mode & S_IFMT) >> S_SHIFT];
}

static int btrfs_add_link(struct btrfs_trans_handle *trans,
			    struct dentry *dentry, struct inode *inode,
			    int add_backref)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_root *root = BTRFS_I(dentry->d_parent->d_inode)->root;
	struct inode *parent_inode = dentry->d_parent->d_inode;

	key.objectid = inode->i_ino;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;

	ret = btrfs_insert_dir_item(trans, root,
				    dentry->d_name.name, dentry->d_name.len,
				    dentry->d_parent->d_inode->i_ino,
				    &key, btrfs_inode_type(inode),
				    BTRFS_I(inode)->index);
	if (ret == 0) {
		if (add_backref) {
			ret = btrfs_insert_inode_ref(trans, root,
					     dentry->d_name.name,
					     dentry->d_name.len,
					     inode->i_ino,
					     parent_inode->i_ino,
					     BTRFS_I(inode)->index);
		}
		btrfs_i_size_write(parent_inode, parent_inode->i_size +
				   dentry->d_name.len * 2);
		parent_inode->i_mtime = parent_inode->i_ctime = CURRENT_TIME;
		ret = btrfs_update_inode(trans, root,
					 dentry->d_parent->d_inode);
	}
	return ret;
}

static int btrfs_add_nondir(struct btrfs_trans_handle *trans,
			    struct dentry *dentry, struct inode *inode,
			    int backref)
{
	int err = btrfs_add_link(trans, dentry, inode, backref);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	if (err > 0)
		err = -EEXIST;
	return err;
}

static int btrfs_mknod(struct inode *dir, struct dentry *dentry,
			int mode, dev_t rdev)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = NULL;
	int err;
	int drop_inode = 0;
	u64 objectid;
	unsigned long nr = 0;

	if (!new_valid_dev(rdev))
		return -EINVAL;

	err = btrfs_check_free_space(root, 1, 0);
	if (err)
		goto fail;

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len,
				dentry->d_parent->d_inode->i_ino, objectid,
				BTRFS_I(dir)->block_group, mode);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_unlock;

	err = btrfs_init_acl(inode, dir);
	if (err) {
		drop_inode = 1;
		goto out_unlock;
	}

	btrfs_set_trans_block_group(trans, inode);
	err = btrfs_add_nondir(trans, dentry, inode, 0);
	if (err)
		drop_inode = 1;
	else {
		inode->i_op = &btrfs_special_inode_operations;
		init_special_inode(inode, inode->i_mode, rdev);
		btrfs_update_inode(trans, root, inode);
	}
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);
out_unlock:
	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);
fail:
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

static int btrfs_create(struct inode *dir, struct dentry *dentry,
			int mode, struct nameidata *nd)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = NULL;
	int err;
	int drop_inode = 0;
	unsigned long nr = 0;
	u64 objectid;

	err = btrfs_check_free_space(root, 1, 0);
	if (err)
		goto fail;
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len,
				dentry->d_parent->d_inode->i_ino,
				objectid, BTRFS_I(dir)->block_group, mode);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_unlock;

	err = btrfs_init_acl(inode, dir);
	if (err) {
		drop_inode = 1;
		goto out_unlock;
	}

	btrfs_set_trans_block_group(trans, inode);
	err = btrfs_add_nondir(trans, dentry, inode, 0);
	if (err)
		drop_inode = 1;
	else {
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
		extent_map_tree_init(&BTRFS_I(inode)->extent_tree, GFP_NOFS);
		extent_io_tree_init(&BTRFS_I(inode)->io_tree,
				     inode->i_mapping, GFP_NOFS);
		extent_io_tree_init(&BTRFS_I(inode)->io_failure_tree,
				     inode->i_mapping, GFP_NOFS);
		mutex_init(&BTRFS_I(inode)->csum_mutex);
		mutex_init(&BTRFS_I(inode)->extent_mutex);
		BTRFS_I(inode)->delalloc_bytes = 0;
		BTRFS_I(inode)->disk_i_size = 0;
		BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;
		btrfs_ordered_inode_tree_init(&BTRFS_I(inode)->ordered_tree);
	}
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);
out_unlock:
	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);
fail:
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

static int btrfs_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *dentry)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = old_dentry->d_inode;
	unsigned long nr = 0;
	int err;
	int drop_inode = 0;

	if (inode->i_nlink == 0)
		return -ENOENT;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
	inode->i_nlink++;
#else
	inc_nlink(inode);
#endif
	err = btrfs_check_free_space(root, 1, 0);
	if (err)
		goto fail;
	err = btrfs_set_inode_index(dir, inode);
	if (err)
		goto fail;

	trans = btrfs_start_transaction(root, 1);

	btrfs_set_trans_block_group(trans, dir);
	atomic_inc(&inode->i_count);

	err = btrfs_add_nondir(trans, dentry, inode, 1);

	if (err)
		drop_inode = 1;

	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, dir);
	err = btrfs_update_inode(trans, root, inode);

	if (err)
		drop_inode = 1;

	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);
fail:
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

static int btrfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *inode = NULL;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	int err = 0;
	int drop_on_err = 0;
	u64 objectid = 0;
	unsigned long nr = 1;

	err = btrfs_check_free_space(root, 1, 0);
	if (err)
		goto out_unlock;

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_unlock;
	}

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len,
				dentry->d_parent->d_inode->i_ino, objectid,
				BTRFS_I(dir)->block_group, S_IFDIR | mode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_fail;
	}

	drop_on_err = 1;

	err = btrfs_init_acl(inode, dir);
	if (err)
		goto out_fail;

	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;
	btrfs_set_trans_block_group(trans, inode);

	btrfs_i_size_write(inode, 0);
	err = btrfs_update_inode(trans, root, inode);
	if (err)
		goto out_fail;

	err = btrfs_add_link(trans, dentry, inode, 0);
	if (err)
		goto out_fail;

	d_instantiate(dentry, inode);
	drop_on_err = 0;
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);

out_fail:
	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);

out_unlock:
	if (drop_on_err)
		iput(inode);
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

static int merge_extent_mapping(struct extent_map_tree *em_tree,
				struct extent_map *existing,
				struct extent_map *em,
				u64 map_start, u64 map_len)
{
	u64 start_diff;

	BUG_ON(map_start < em->start || map_start >= extent_map_end(em));
	start_diff = map_start - em->start;
	em->start = map_start;
	em->len = map_len;
	if (em->block_start < EXTENT_MAP_LAST_BYTE)
		em->block_start += start_diff;
	return add_extent_mapping(em_tree, em);
}

struct extent_map *btrfs_get_extent(struct inode *inode, struct page *page,
				    size_t pg_offset, u64 start, u64 len,
				    int create)
{
	int ret;
	int err = 0;
	u64 bytenr;
	u64 extent_start = 0;
	u64 extent_end = 0;
	u64 objectid = inode->i_ino;
	u32 found_type;
	struct btrfs_path *path = NULL;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_file_extent_item *item;
	struct extent_buffer *leaf;
	struct btrfs_key found_key;
	struct extent_map *em = NULL;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_trans_handle *trans = NULL;

again:
	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	if (em)
		em->bdev = root->fs_info->fs_devices->latest_bdev;
	spin_unlock(&em_tree->lock);

	if (em) {
		if (em->start > start || em->start + em->len <= start)
			free_extent_map(em);
		else if (em->block_start == EXTENT_MAP_INLINE && page)
			free_extent_map(em);
		else
			goto out;
	}
	em = alloc_extent_map(GFP_NOFS);
	if (!em) {
		err = -ENOMEM;
		goto out;
	}
	em->bdev = root->fs_info->fs_devices->latest_bdev;
	em->start = EXTENT_MAP_HOLE;
	em->len = (u64)-1;

	if (!path) {
		path = btrfs_alloc_path();
		BUG_ON(!path);
	}

	ret = btrfs_lookup_file_extent(trans, root, path,
				       objectid, start, trans != NULL);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	if (ret != 0) {
		if (path->slots[0] == 0)
			goto not_found;
		path->slots[0]--;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_file_extent_item);
	/* are we inside the extent that was found? */
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
	found_type = btrfs_key_type(&found_key);
	if (found_key.objectid != objectid ||
	    found_type != BTRFS_EXTENT_DATA_KEY) {
		goto not_found;
	}

	found_type = btrfs_file_extent_type(leaf, item);
	extent_start = found_key.offset;
	if (found_type == BTRFS_FILE_EXTENT_REG) {
		extent_end = extent_start +
		       btrfs_file_extent_num_bytes(leaf, item);
		err = 0;
		if (start < extent_start || start >= extent_end) {
			em->start = start;
			if (start < extent_start) {
				if (start + len <= extent_start)
					goto not_found;
				em->len = extent_end - extent_start;
			} else {
				em->len = len;
			}
			goto not_found_em;
		}
		bytenr = btrfs_file_extent_disk_bytenr(leaf, item);
		if (bytenr == 0) {
			em->start = extent_start;
			em->len = extent_end - extent_start;
			em->block_start = EXTENT_MAP_HOLE;
			goto insert;
		}
		bytenr += btrfs_file_extent_offset(leaf, item);
		em->block_start = bytenr;
		em->start = extent_start;
		em->len = extent_end - extent_start;
		goto insert;
	} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		u64 page_start;
		unsigned long ptr;
		char *map;
		size_t size;
		size_t extent_offset;
		size_t copy_size;

		size = btrfs_file_extent_inline_len(leaf, btrfs_item_nr(leaf,
						    path->slots[0]));
		extent_end = (extent_start + size + root->sectorsize - 1) &
			~((u64)root->sectorsize - 1);
		if (start < extent_start || start >= extent_end) {
			em->start = start;
			if (start < extent_start) {
				if (start + len <= extent_start)
					goto not_found;
				em->len = extent_end - extent_start;
			} else {
				em->len = len;
			}
			goto not_found_em;
		}
		em->block_start = EXTENT_MAP_INLINE;

		if (!page) {
			em->start = extent_start;
			em->len = size;
			goto out;
		}

		page_start = page_offset(page) + pg_offset;
		extent_offset = page_start - extent_start;
		copy_size = min_t(u64, PAGE_CACHE_SIZE - pg_offset,
				size - extent_offset);
		em->start = extent_start + extent_offset;
		em->len = (copy_size + root->sectorsize - 1) &
			~((u64)root->sectorsize - 1);
		map = kmap(page);
		ptr = btrfs_file_extent_inline_start(item) + extent_offset;
		if (create == 0 && !PageUptodate(page)) {
			read_extent_buffer(leaf, map + pg_offset, ptr,
					   copy_size);
			flush_dcache_page(page);
		} else if (create && PageUptodate(page)) {
			if (!trans) {
				kunmap(page);
				free_extent_map(em);
				em = NULL;
				btrfs_release_path(root, path);
				trans = btrfs_join_transaction(root, 1);
				goto again;
			}
			write_extent_buffer(leaf, map + pg_offset, ptr,
					    copy_size);
			btrfs_mark_buffer_dirty(leaf);
		}
		kunmap(page);
		set_extent_uptodate(io_tree, em->start,
				    extent_map_end(em) - 1, GFP_NOFS);
		goto insert;
	} else {
		printk("unkknown found_type %d\n", found_type);
		WARN_ON(1);
	}
not_found:
	em->start = start;
	em->len = len;
not_found_em:
	em->block_start = EXTENT_MAP_HOLE;
insert:
	btrfs_release_path(root, path);
	if (em->start > start || extent_map_end(em) <= start) {
		printk("bad extent! em: [%Lu %Lu] passed [%Lu %Lu]\n", em->start, em->len, start, len);
		err = -EIO;
		goto out;
	}

	err = 0;
	spin_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em);
	/* it is possible that someone inserted the extent into the tree
	 * while we had the lock dropped.  It is also possible that
	 * an overlapping map exists in the tree
	 */
	if (ret == -EEXIST) {
		struct extent_map *existing;

		ret = 0;

		existing = lookup_extent_mapping(em_tree, start, len);
		if (existing && (existing->start > start ||
		    existing->start + existing->len <= start)) {
			free_extent_map(existing);
			existing = NULL;
		}
		if (!existing) {
			existing = lookup_extent_mapping(em_tree, em->start,
							 em->len);
			if (existing) {
				err = merge_extent_mapping(em_tree, existing,
							   em, start,
							   root->sectorsize);
				free_extent_map(existing);
				if (err) {
					free_extent_map(em);
					em = NULL;
				}
			} else {
				err = -EIO;
				printk("failing to insert %Lu %Lu\n",
				       start, len);
				free_extent_map(em);
				em = NULL;
			}
		} else {
			free_extent_map(em);
			em = existing;
			err = 0;
		}
	}
	spin_unlock(&em_tree->lock);
out:
	if (path)
		btrfs_free_path(path);
	if (trans) {
		ret = btrfs_end_transaction(trans, root);
		if (!err) {
			err = ret;
		}
	}
	if (err) {
		free_extent_map(em);
		WARN_ON(1);
		return ERR_PTR(err);
	}
	return em;
}

#if 0 /* waiting for O_DIRECT reads */
static int btrfs_get_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	struct extent_map *em;
	u64 start = (u64)iblock << inode->i_blkbits;
	struct btrfs_multi_bio *multi = NULL;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 len;
	u64 logical;
	u64 map_length;
	int ret = 0;

	em = btrfs_get_extent(inode, NULL, 0, start, bh_result->b_size, 0);

	if (!em || IS_ERR(em))
		goto out;

	if (em->start > start || em->start + em->len <= start) {
	    goto out;
	}

	if (em->block_start == EXTENT_MAP_INLINE) {
		ret = -EINVAL;
		goto out;
	}

	len = em->start + em->len - start;
	len = min_t(u64, len, INT_LIMIT(typeof(bh_result->b_size)));

	if (em->block_start == EXTENT_MAP_HOLE ||
	    em->block_start == EXTENT_MAP_DELALLOC) {
		bh_result->b_size = len;
		goto out;
	}

	logical = start - em->start;
	logical = em->block_start + logical;

	map_length = len;
	ret = btrfs_map_block(&root->fs_info->mapping_tree, READ,
			      logical, &map_length, &multi, 0);
	BUG_ON(ret);
	bh_result->b_blocknr = multi->stripes[0].physical >> inode->i_blkbits;
	bh_result->b_size = min(map_length, len);

	bh_result->b_bdev = multi->stripes[0].dev->bdev;
	set_buffer_mapped(bh_result);
	kfree(multi);
out:
	free_extent_map(em);
	return ret;
}
#endif

static ssize_t btrfs_direct_IO(int rw, struct kiocb *iocb,
			const struct iovec *iov, loff_t offset,
			unsigned long nr_segs)
{
	return -EINVAL;
#if 0
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;

	if (rw == WRITE)
		return -EINVAL;

	return blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
				  offset, nr_segs, btrfs_get_block, NULL);
#endif
}

static sector_t btrfs_bmap(struct address_space *mapping, sector_t iblock)
{
	return extent_bmap(mapping, iblock, btrfs_get_extent);
}

int btrfs_readpage(struct file *file, struct page *page)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	return extent_read_full_page(tree, page, btrfs_get_extent);
}

static int btrfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct extent_io_tree *tree;


	if (current->flags & PF_MEMALLOC) {
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return 0;
	}
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	return extent_write_full_page(tree, page, btrfs_get_extent, wbc);
}

int btrfs_writepages(struct address_space *mapping,
		     struct writeback_control *wbc)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(mapping->host)->io_tree;
	return extent_writepages(tree, mapping, btrfs_get_extent, wbc);
}

static int
btrfs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(mapping->host)->io_tree;
	return extent_readpages(tree, mapping, pages, nr_pages,
				btrfs_get_extent);
}
static int __btrfs_releasepage(struct page *page, gfp_t gfp_flags)
{
	struct extent_io_tree *tree;
	struct extent_map_tree *map;
	int ret;

	tree = &BTRFS_I(page->mapping->host)->io_tree;
	map = &BTRFS_I(page->mapping->host)->extent_tree;
	ret = try_release_extent_mapping(map, tree, page, gfp_flags);
	if (ret == 1) {
		ClearPagePrivate(page);
		set_page_private(page, 0);
		page_cache_release(page);
	}
	return ret;
}

static int btrfs_releasepage(struct page *page, gfp_t gfp_flags)
{
	return __btrfs_releasepage(page, gfp_flags);
}

static void btrfs_invalidatepage(struct page *page, unsigned long offset)
{
	struct extent_io_tree *tree;
	struct btrfs_ordered_extent *ordered;
	u64 page_start = page_offset(page);
	u64 page_end = page_start + PAGE_CACHE_SIZE - 1;

	wait_on_page_writeback(page);
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	if (offset) {
		btrfs_releasepage(page, GFP_NOFS);
		return;
	}

	lock_extent(tree, page_start, page_end, GFP_NOFS);
	ordered = btrfs_lookup_ordered_extent(page->mapping->host,
					   page_offset(page));
	if (ordered) {
		/*
		 * IO on this page will never be started, so we need
		 * to account for any ordered extents now
		 */
		clear_extent_bit(tree, page_start, page_end,
				 EXTENT_DIRTY | EXTENT_DELALLOC |
				 EXTENT_LOCKED, 1, 0, GFP_NOFS);
		btrfs_finish_ordered_io(page->mapping->host,
					page_start, page_end);
		btrfs_put_ordered_extent(ordered);
		lock_extent(tree, page_start, page_end, GFP_NOFS);
	}
	clear_extent_bit(tree, page_start, page_end,
		 EXTENT_LOCKED | EXTENT_DIRTY | EXTENT_DELALLOC |
		 EXTENT_ORDERED,
		 1, 1, GFP_NOFS);
	__btrfs_releasepage(page, GFP_NOFS);

	ClearPageChecked(page);
	if (PagePrivate(page)) {
		ClearPagePrivate(page);
		set_page_private(page, 0);
		page_cache_release(page);
	}
}

/*
 * btrfs_page_mkwrite() is not allowed to change the file size as it gets
 * called from a page fault handler when a page is first dirtied. Hence we must
 * be careful to check for EOF conditions here. We set the page up correctly
 * for a written page which means we get ENOSPC checking when writing into
 * holes and correct delalloc and unwritten extent mapping on filesystems that
 * support these features.
 *
 * We are not allowed to take the i_mutex here so we have to play games to
 * protect against truncate races as the page could now be beyond EOF.  Because
 * vmtruncate() writes the inode size before removing pages, once we have the
 * page lock we can determine safely if the page is beyond EOF. If it is not
 * beyond EOF, then the page is guaranteed safe against truncation until we
 * unlock the page.
 */
int btrfs_page_mkwrite(struct vm_area_struct *vma, struct page *page)
{
	struct inode *inode = fdentry(vma->vm_file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_ordered_extent *ordered;
	char *kaddr;
	unsigned long zero_start;
	loff_t size;
	int ret;
	u64 page_start;
	u64 page_end;

	ret = btrfs_check_free_space(root, PAGE_CACHE_SIZE, 0);
	if (ret)
		goto out;

	ret = -EINVAL;
again:
	lock_page(page);
	size = i_size_read(inode);
	page_start = page_offset(page);
	page_end = page_start + PAGE_CACHE_SIZE - 1;

	if ((page->mapping != inode->i_mapping) ||
	    (page_start >= size)) {
		/* page got truncated out from underneath us */
		goto out_unlock;
	}
	wait_on_page_writeback(page);

	lock_extent(io_tree, page_start, page_end, GFP_NOFS);
	set_page_extent_mapped(page);

	/*
	 * we can't set the delalloc bits if there are pending ordered
	 * extents.  Drop our locks and wait for them to finish
	 */
	ordered = btrfs_lookup_ordered_extent(inode, page_start);
	if (ordered) {
		unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
		unlock_page(page);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	set_extent_delalloc(&BTRFS_I(inode)->io_tree, page_start,
			    page_end, GFP_NOFS);
	ret = 0;

	/* page is wholly or partially inside EOF */
	if (page_start + PAGE_CACHE_SIZE > size)
		zero_start = size & ~PAGE_CACHE_MASK;
	else
		zero_start = PAGE_CACHE_SIZE;

	if (zero_start != PAGE_CACHE_SIZE) {
		kaddr = kmap(page);
		memset(kaddr + zero_start, 0, PAGE_CACHE_SIZE - zero_start);
		flush_dcache_page(page);
		kunmap(page);
	}
	ClearPageChecked(page);
	set_page_dirty(page);
	unlock_extent(io_tree, page_start, page_end, GFP_NOFS);

out_unlock:
	unlock_page(page);
out:
	return ret;
}

static void btrfs_truncate(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;
	struct btrfs_trans_handle *trans;
	unsigned long nr;
	u64 mask = root->sectorsize - 1;

	if (!S_ISREG(inode->i_mode))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	btrfs_truncate_page(inode->i_mapping, inode->i_size);
	btrfs_wait_ordered_range(inode, inode->i_size & (~mask), (u64)-1);

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);
	btrfs_i_size_write(inode, inode->i_size);

	ret = btrfs_orphan_add(trans, inode);
	if (ret)
		goto out;
	/* FIXME, add redo link to tree so we don't leak on crash */
	ret = btrfs_truncate_in_trans(trans, root, inode,
				      BTRFS_EXTENT_DATA_KEY);
	btrfs_update_inode(trans, root, inode);

	ret = btrfs_orphan_del(trans, inode);
	BUG_ON(ret);

out:
	nr = trans->blocks_used;
	ret = btrfs_end_transaction_throttle(trans, root);
	BUG_ON(ret);
	btrfs_btree_balance_dirty(root, nr);
}

/*
 * Invalidate a single dcache entry at the root of the filesystem.
 * Needed after creation of snapshot or subvolume.
 */
void btrfs_invalidate_dcache_root(struct btrfs_root *root, char *name,
				  int namelen)
{
	struct dentry *alias, *entry;
	struct qstr qstr;

	alias = d_find_alias(root->fs_info->sb->s_root->d_inode);
	if (alias) {
		qstr.name = name;
		qstr.len = namelen;
		/* change me if btrfs ever gets a d_hash operation */
		qstr.hash = full_name_hash(qstr.name, qstr.len);
		entry = d_lookup(alias, &qstr);
		dput(alias);
		if (entry) {
			d_invalidate(entry);
			dput(entry);
		}
	}
}

int btrfs_create_subvol_root(struct btrfs_root *new_root,
		struct btrfs_trans_handle *trans, u64 new_dirid,
		struct btrfs_block_group_cache *block_group)
{
	struct inode *inode;

	inode = btrfs_new_inode(trans, new_root, NULL, "..", 2, new_dirid,
				new_dirid, block_group, S_IFDIR | 0700);
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;
	new_root->inode = inode;

	inode->i_nlink = 1;
	btrfs_i_size_write(inode, 0);

	return btrfs_update_inode(trans, new_root, inode);
}

unsigned long btrfs_force_ra(struct address_space *mapping,
			      struct file_ra_state *ra, struct file *file,
			      pgoff_t offset, pgoff_t last_index)
{
	pgoff_t req_size = last_index - offset + 1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
	offset = page_cache_readahead(mapping, ra, file, offset, req_size);
	return offset;
#else
	page_cache_sync_readahead(mapping, ra, file, offset, req_size);
	return offset + req_size;
#endif
}

struct inode *btrfs_alloc_inode(struct super_block *sb)
{
	struct btrfs_inode *ei;

	ei = kmem_cache_alloc(btrfs_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;
	ei->last_trans = 0;
	btrfs_ordered_inode_tree_init(&ei->ordered_tree);
	ei->i_acl = BTRFS_ACL_NOT_CACHED;
	ei->i_default_acl = BTRFS_ACL_NOT_CACHED;
	INIT_LIST_HEAD(&ei->i_orphan);
	return &ei->vfs_inode;
}

void btrfs_destroy_inode(struct inode *inode)
{
	struct btrfs_ordered_extent *ordered;
	WARN_ON(!list_empty(&inode->i_dentry));
	WARN_ON(inode->i_data.nrpages);

	if (BTRFS_I(inode)->i_acl &&
	    BTRFS_I(inode)->i_acl != BTRFS_ACL_NOT_CACHED)
		posix_acl_release(BTRFS_I(inode)->i_acl);
	if (BTRFS_I(inode)->i_default_acl &&
	    BTRFS_I(inode)->i_default_acl != BTRFS_ACL_NOT_CACHED)
		posix_acl_release(BTRFS_I(inode)->i_default_acl);

	spin_lock(&BTRFS_I(inode)->root->orphan_lock);
	if (!list_empty(&BTRFS_I(inode)->i_orphan)) {
		printk(KERN_ERR "BTRFS: inode %lu: inode still on the orphan"
		       " list\n", inode->i_ino);
		dump_stack();
	}
	spin_unlock(&BTRFS_I(inode)->root->orphan_lock);

	while(1) {
		ordered = btrfs_lookup_first_ordered_extent(inode, (u64)-1);
		if (!ordered)
			break;
		else {
			printk("found ordered extent %Lu %Lu\n",
			       ordered->file_offset, ordered->len);
			btrfs_remove_ordered_extent(inode, ordered);
			btrfs_put_ordered_extent(ordered);
			btrfs_put_ordered_extent(ordered);
		}
	}
	btrfs_drop_extent_cache(inode, 0, (u64)-1);
	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
static void init_once(struct kmem_cache * cachep, void *foo)
#else
static void init_once(void * foo, struct kmem_cache * cachep,
		      unsigned long flags)
#endif
{
	struct btrfs_inode *ei = (struct btrfs_inode *) foo;

	inode_init_once(&ei->vfs_inode);
}

void btrfs_destroy_cachep(void)
{
	if (btrfs_inode_cachep)
		kmem_cache_destroy(btrfs_inode_cachep);
	if (btrfs_trans_handle_cachep)
		kmem_cache_destroy(btrfs_trans_handle_cachep);
	if (btrfs_transaction_cachep)
		kmem_cache_destroy(btrfs_transaction_cachep);
	if (btrfs_bit_radix_cachep)
		kmem_cache_destroy(btrfs_bit_radix_cachep);
	if (btrfs_path_cachep)
		kmem_cache_destroy(btrfs_path_cachep);
}

struct kmem_cache *btrfs_cache_create(const char *name, size_t size,
				       unsigned long extra_flags,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
				       void (*ctor)(struct kmem_cache *, void *)
#else
				       void (*ctor)(void *, struct kmem_cache *,
						    unsigned long)
#endif
				     )
{
	return kmem_cache_create(name, size, 0, (SLAB_RECLAIM_ACCOUNT |
				 SLAB_MEM_SPREAD | extra_flags), ctor
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
				 ,NULL
#endif
				);
}

int btrfs_init_cachep(void)
{
	btrfs_inode_cachep = btrfs_cache_create("btrfs_inode_cache",
					  sizeof(struct btrfs_inode),
					  0, init_once);
	if (!btrfs_inode_cachep)
		goto fail;
	btrfs_trans_handle_cachep =
			btrfs_cache_create("btrfs_trans_handle_cache",
					   sizeof(struct btrfs_trans_handle),
					   0, NULL);
	if (!btrfs_trans_handle_cachep)
		goto fail;
	btrfs_transaction_cachep = btrfs_cache_create("btrfs_transaction_cache",
					     sizeof(struct btrfs_transaction),
					     0, NULL);
	if (!btrfs_transaction_cachep)
		goto fail;
	btrfs_path_cachep = btrfs_cache_create("btrfs_path_cache",
					 sizeof(struct btrfs_path),
					 0, NULL);
	if (!btrfs_path_cachep)
		goto fail;
	btrfs_bit_radix_cachep = btrfs_cache_create("btrfs_radix", 256,
					      SLAB_DESTROY_BY_RCU, NULL);
	if (!btrfs_bit_radix_cachep)
		goto fail;
	return 0;
fail:
	btrfs_destroy_cachep();
	return -ENOMEM;
}

static int btrfs_getattr(struct vfsmount *mnt,
			 struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->blksize = PAGE_CACHE_SIZE;
	stat->blocks = inode->i_blocks + (BTRFS_I(inode)->delalloc_bytes >> 9);
	return 0;
}

static int btrfs_rename(struct inode * old_dir, struct dentry *old_dentry,
			   struct inode * new_dir,struct dentry *new_dentry)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(old_dir)->root;
	struct inode *new_inode = new_dentry->d_inode;
	struct inode *old_inode = old_dentry->d_inode;
	struct timespec ctime = CURRENT_TIME;
	int ret;

	if (S_ISDIR(old_inode->i_mode) && new_inode &&
	    new_inode->i_size > BTRFS_EMPTY_DIR_SIZE) {
		return -ENOTEMPTY;
	}

	ret = btrfs_check_free_space(root, 1, 0);
	if (ret)
		goto out_unlock;

	trans = btrfs_start_transaction(root, 1);

	btrfs_set_trans_block_group(trans, new_dir);

	old_dentry->d_inode->i_nlink++;
	old_dir->i_ctime = old_dir->i_mtime = ctime;
	new_dir->i_ctime = new_dir->i_mtime = ctime;
	old_inode->i_ctime = ctime;

	ret = btrfs_unlink_trans(trans, root, old_dir, old_dentry);
	if (ret)
		goto out_fail;

	if (new_inode) {
		new_inode->i_ctime = CURRENT_TIME;
		ret = btrfs_unlink_trans(trans, root, new_dir, new_dentry);
		if (ret)
			goto out_fail;
		if (new_inode->i_nlink == 0) {
			ret = btrfs_orphan_add(trans, new_inode);
			if (ret)
				goto out_fail;
		}
	}
	ret = btrfs_set_inode_index(new_dir, old_inode);
	if (ret)
		goto out_fail;

	ret = btrfs_add_link(trans, new_dentry, old_inode, 1);
	if (ret)
		goto out_fail;

out_fail:
	btrfs_end_transaction(trans, root);
out_unlock:
	return ret;
}

static int btrfs_symlink(struct inode *dir, struct dentry *dentry,
			 const char *symname)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct inode *inode = NULL;
	int err;
	int drop_inode = 0;
	u64 objectid;
	int name_len;
	int datasize;
	unsigned long ptr;
	struct btrfs_file_extent_item *ei;
	struct extent_buffer *leaf;
	unsigned long nr = 0;

	name_len = strlen(symname) + 1;
	if (name_len > BTRFS_MAX_INLINE_DATA_SIZE(root))
		return -ENAMETOOLONG;

	err = btrfs_check_free_space(root, 1, 0);
	if (err)
		goto out_fail;

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len,
				dentry->d_parent->d_inode->i_ino, objectid,
				BTRFS_I(dir)->block_group, S_IFLNK|S_IRWXUGO);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_unlock;

	err = btrfs_init_acl(inode, dir);
	if (err) {
		drop_inode = 1;
		goto out_unlock;
	}

	btrfs_set_trans_block_group(trans, inode);
	err = btrfs_add_nondir(trans, dentry, inode, 0);
	if (err)
		drop_inode = 1;
	else {
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
		extent_map_tree_init(&BTRFS_I(inode)->extent_tree, GFP_NOFS);
		extent_io_tree_init(&BTRFS_I(inode)->io_tree,
				     inode->i_mapping, GFP_NOFS);
		extent_io_tree_init(&BTRFS_I(inode)->io_failure_tree,
				     inode->i_mapping, GFP_NOFS);
		mutex_init(&BTRFS_I(inode)->csum_mutex);
		mutex_init(&BTRFS_I(inode)->extent_mutex);
		BTRFS_I(inode)->delalloc_bytes = 0;
		BTRFS_I(inode)->disk_i_size = 0;
		BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;
		btrfs_ordered_inode_tree_init(&BTRFS_I(inode)->ordered_tree);
	}
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);
	if (drop_inode)
		goto out_unlock;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	key.objectid = inode->i_ino;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_DATA_KEY);
	datasize = btrfs_file_extent_calc_inline_size(name_len);
	err = btrfs_insert_empty_item(trans, root, path, &key,
				      datasize);
	if (err) {
		drop_inode = 1;
		goto out_unlock;
	}
	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_file_extent_item);
	btrfs_set_file_extent_generation(leaf, ei, trans->transid);
	btrfs_set_file_extent_type(leaf, ei,
				   BTRFS_FILE_EXTENT_INLINE);
	ptr = btrfs_file_extent_inline_start(ei);
	write_extent_buffer(leaf, symname, ptr, name_len);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_free_path(path);

	inode->i_op = &btrfs_symlink_inode_operations;
	inode->i_mapping->a_ops = &btrfs_symlink_aops;
	inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
	btrfs_i_size_write(inode, name_len - 1);
	err = btrfs_update_inode(trans, root, inode);
	if (err)
		drop_inode = 1;

out_unlock:
	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);
out_fail:
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

static int btrfs_set_page_dirty(struct page *page)
{
	return __set_page_dirty_nobuffers(page);
}

static int btrfs_permission(struct inode *inode, int mask,
			    struct nameidata *nd)
{
	if (btrfs_test_flag(inode, READONLY) && (mask & MAY_WRITE))
		return -EACCES;
	return generic_permission(inode, mask, btrfs_check_acl);
}

static struct inode_operations btrfs_dir_inode_operations = {
	.lookup		= btrfs_lookup,
	.create		= btrfs_create,
	.unlink		= btrfs_unlink,
	.link		= btrfs_link,
	.mkdir		= btrfs_mkdir,
	.rmdir		= btrfs_rmdir,
	.rename		= btrfs_rename,
	.symlink	= btrfs_symlink,
	.setattr	= btrfs_setattr,
	.mknod		= btrfs_mknod,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= btrfs_listxattr,
	.removexattr	= generic_removexattr,
	.permission	= btrfs_permission,
};
static struct inode_operations btrfs_dir_ro_inode_operations = {
	.lookup		= btrfs_lookup,
	.permission	= btrfs_permission,
};
static struct file_operations btrfs_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= btrfs_readdir,
	.unlocked_ioctl	= btrfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= btrfs_ioctl,
#endif
	.release        = btrfs_release_file,
};

static struct extent_io_ops btrfs_extent_io_ops = {
	.fill_delalloc = run_delalloc_range,
	.submit_bio_hook = btrfs_submit_bio_hook,
	.merge_bio_hook = btrfs_merge_bio_hook,
	.readpage_io_hook = btrfs_readpage_io_hook,
	.readpage_end_io_hook = btrfs_readpage_end_io_hook,
	.writepage_end_io_hook = btrfs_writepage_end_io_hook,
	.writepage_start_hook = btrfs_writepage_start_hook,
	.readpage_io_failed_hook = btrfs_io_failed_hook,
	.set_bit_hook = btrfs_set_bit_hook,
	.clear_bit_hook = btrfs_clear_bit_hook,
};

static struct address_space_operations btrfs_aops = {
	.readpage	= btrfs_readpage,
	.writepage	= btrfs_writepage,
	.writepages	= btrfs_writepages,
	.readpages	= btrfs_readpages,
	.sync_page	= block_sync_page,
	.bmap		= btrfs_bmap,
	.direct_IO	= btrfs_direct_IO,
	.invalidatepage = btrfs_invalidatepage,
	.releasepage	= btrfs_releasepage,
	.set_page_dirty	= btrfs_set_page_dirty,
};

static struct address_space_operations btrfs_symlink_aops = {
	.readpage	= btrfs_readpage,
	.writepage	= btrfs_writepage,
	.invalidatepage = btrfs_invalidatepage,
	.releasepage	= btrfs_releasepage,
};

static struct inode_operations btrfs_file_inode_operations = {
	.truncate	= btrfs_truncate,
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr      = btrfs_listxattr,
	.removexattr	= generic_removexattr,
	.permission	= btrfs_permission,
};
static struct inode_operations btrfs_special_inode_operations = {
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.permission	= btrfs_permission,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= btrfs_listxattr,
	.removexattr	= generic_removexattr,
};
static struct inode_operations btrfs_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
	.permission	= btrfs_permission,
};

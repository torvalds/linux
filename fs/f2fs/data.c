/*
 * fs/f2fs/data.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/prefetch.h>
#include <linux/uio.h>
#include <linux/cleancache.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "trace.h"
#include <trace/events/f2fs.h>

static struct kmem_cache *extent_tree_slab;
static struct kmem_cache *extent_node_slab;

static void f2fs_read_end_io(struct bio *bio, int err)
{
	struct bio_vec *bvec;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		if (!err) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	}
	bio_put(bio);
}

/*
 * I/O completion handler for multipage BIOs.
 * copied from fs/mpage.c
 */
static void mpage_end_io(struct bio *bio, int err)
{
	struct bio_vec *bv;
	int i;

	bio_for_each_segment_all(bv, bio, i) {
		struct page *page = bv->bv_page;

		if (!err) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	}

	bio_put(bio);
}

static void f2fs_write_end_io(struct bio *bio, int err)
{
	struct f2fs_sb_info *sbi = bio->bi_private;
	struct bio_vec *bvec;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		if (unlikely(err)) {
			set_page_dirty(page);
			set_bit(AS_EIO, &page->mapping->flags);
			f2fs_stop_checkpoint(sbi);
		}
		end_page_writeback(page);
		dec_page_count(sbi, F2FS_WRITEBACK);
	}

	if (!get_pages(sbi, F2FS_WRITEBACK) &&
			!list_empty(&sbi->cp_wait.task_list))
		wake_up(&sbi->cp_wait);

	bio_put(bio);
}

/*
 * Low-level block read/write IO operations.
 */
static struct bio *__bio_alloc(struct f2fs_sb_info *sbi, block_t blk_addr,
				int npages, bool is_read)
{
	struct bio *bio;

	/* No failure on bio allocation */
	bio = bio_alloc(GFP_NOIO, npages);

	bio->bi_bdev = sbi->sb->s_bdev;
	bio->bi_iter.bi_sector = SECTOR_FROM_BLOCK(blk_addr);
	bio->bi_end_io = is_read ? f2fs_read_end_io : f2fs_write_end_io;
	bio->bi_private = sbi;

	return bio;
}

static void __submit_merged_bio(struct f2fs_bio_info *io)
{
	struct f2fs_io_info *fio = &io->fio;

	if (!io->bio)
		return;

	if (is_read_io(fio->rw))
		trace_f2fs_submit_read_bio(io->sbi->sb, fio, io->bio);
	else
		trace_f2fs_submit_write_bio(io->sbi->sb, fio, io->bio);

	submit_bio(fio->rw, io->bio);
	io->bio = NULL;
}

void f2fs_submit_merged_bio(struct f2fs_sb_info *sbi,
				enum page_type type, int rw)
{
	enum page_type btype = PAGE_TYPE_OF_BIO(type);
	struct f2fs_bio_info *io;

	io = is_read_io(rw) ? &sbi->read_io : &sbi->write_io[btype];

	down_write(&io->io_rwsem);

	/* change META to META_FLUSH in the checkpoint procedure */
	if (type >= META_FLUSH) {
		io->fio.type = META_FLUSH;
		if (test_opt(sbi, NOBARRIER))
			io->fio.rw = WRITE_FLUSH | REQ_META | REQ_PRIO;
		else
			io->fio.rw = WRITE_FLUSH_FUA | REQ_META | REQ_PRIO;
	}
	__submit_merged_bio(io);
	up_write(&io->io_rwsem);
}

/*
 * Fill the locked page with data located in the block address.
 * Return unlocked page.
 */
int f2fs_submit_page_bio(struct f2fs_sb_info *sbi, struct page *page,
					struct f2fs_io_info *fio)
{
	struct bio *bio;

	trace_f2fs_submit_page_bio(page, fio);
	f2fs_trace_ios(page, fio, 0);

	/* Allocate a new bio */
	bio = __bio_alloc(sbi, fio->blk_addr, 1, is_read_io(fio->rw));

	if (bio_add_page(bio, page, PAGE_CACHE_SIZE, 0) < PAGE_CACHE_SIZE) {
		bio_put(bio);
		f2fs_put_page(page, 1);
		return -EFAULT;
	}

	submit_bio(fio->rw, bio);
	return 0;
}

void f2fs_submit_page_mbio(struct f2fs_sb_info *sbi, struct page *page,
					struct f2fs_io_info *fio)
{
	enum page_type btype = PAGE_TYPE_OF_BIO(fio->type);
	struct f2fs_bio_info *io;
	bool is_read = is_read_io(fio->rw);

	io = is_read ? &sbi->read_io : &sbi->write_io[btype];

	verify_block_addr(sbi, fio->blk_addr);

	down_write(&io->io_rwsem);

	if (!is_read)
		inc_page_count(sbi, F2FS_WRITEBACK);

	if (io->bio && (io->last_block_in_bio != fio->blk_addr - 1 ||
						io->fio.rw != fio->rw))
		__submit_merged_bio(io);
alloc_new:
	if (io->bio == NULL) {
		int bio_blocks = MAX_BIO_BLOCKS(sbi);

		io->bio = __bio_alloc(sbi, fio->blk_addr, bio_blocks, is_read);
		io->fio = *fio;
	}

	if (bio_add_page(io->bio, page, PAGE_CACHE_SIZE, 0) <
							PAGE_CACHE_SIZE) {
		__submit_merged_bio(io);
		goto alloc_new;
	}

	io->last_block_in_bio = fio->blk_addr;
	f2fs_trace_ios(page, fio, 0);

	up_write(&io->io_rwsem);
	trace_f2fs_submit_page_mbio(page, fio);
}

/*
 * Lock ordering for the change of data block address:
 * ->data_page
 *  ->node_page
 *    update block addresses in the node page
 */
void set_data_blkaddr(struct dnode_of_data *dn)
{
	struct f2fs_node *rn;
	__le32 *addr_array;
	struct page *node_page = dn->node_page;
	unsigned int ofs_in_node = dn->ofs_in_node;

	f2fs_wait_on_page_writeback(node_page, NODE);

	rn = F2FS_NODE(node_page);

	/* Get physical address of data block */
	addr_array = blkaddr_in_node(rn);
	addr_array[ofs_in_node] = cpu_to_le32(dn->data_blkaddr);
	set_page_dirty(node_page);
}

int reserve_new_block(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);

	if (unlikely(is_inode_flag_set(F2FS_I(dn->inode), FI_NO_ALLOC)))
		return -EPERM;
	if (unlikely(!inc_valid_block_count(sbi, dn->inode, 1)))
		return -ENOSPC;

	trace_f2fs_reserve_new_block(dn->inode, dn->nid, dn->ofs_in_node);

	dn->data_blkaddr = NEW_ADDR;
	set_data_blkaddr(dn);
	mark_inode_dirty(dn->inode);
	sync_inode_page(dn);
	return 0;
}

int f2fs_reserve_block(struct dnode_of_data *dn, pgoff_t index)
{
	bool need_put = dn->inode_page ? false : true;
	int err;

	err = get_dnode_of_data(dn, index, ALLOC_NODE);
	if (err)
		return err;

	if (dn->data_blkaddr == NULL_ADDR)
		err = reserve_new_block(dn);
	if (err || need_put)
		f2fs_put_dnode(dn);
	return err;
}

static bool lookup_extent_info(struct inode *inode, pgoff_t pgofs,
							struct extent_info *ei)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
	pgoff_t start_fofs, end_fofs;
	block_t start_blkaddr;

	read_lock(&fi->ext_lock);
	if (fi->ext.len == 0) {
		read_unlock(&fi->ext_lock);
		return false;
	}

	stat_inc_total_hit(inode->i_sb);

	start_fofs = fi->ext.fofs;
	end_fofs = fi->ext.fofs + fi->ext.len - 1;
	start_blkaddr = fi->ext.blk;

	if (pgofs >= start_fofs && pgofs <= end_fofs) {
		*ei = fi->ext;
		stat_inc_read_hit(inode->i_sb);
		read_unlock(&fi->ext_lock);
		return true;
	}
	read_unlock(&fi->ext_lock);
	return false;
}

static bool update_extent_info(struct inode *inode, pgoff_t fofs,
								block_t blkaddr)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
	pgoff_t start_fofs, end_fofs;
	block_t start_blkaddr, end_blkaddr;
	int need_update = true;

	write_lock(&fi->ext_lock);

	start_fofs = fi->ext.fofs;
	end_fofs = fi->ext.fofs + fi->ext.len - 1;
	start_blkaddr = fi->ext.blk;
	end_blkaddr = fi->ext.blk + fi->ext.len - 1;

	/* Drop and initialize the matched extent */
	if (fi->ext.len == 1 && fofs == start_fofs)
		fi->ext.len = 0;

	/* Initial extent */
	if (fi->ext.len == 0) {
		if (blkaddr != NULL_ADDR) {
			fi->ext.fofs = fofs;
			fi->ext.blk = blkaddr;
			fi->ext.len = 1;
		}
		goto end_update;
	}

	/* Front merge */
	if (fofs == start_fofs - 1 && blkaddr == start_blkaddr - 1) {
		fi->ext.fofs--;
		fi->ext.blk--;
		fi->ext.len++;
		goto end_update;
	}

	/* Back merge */
	if (fofs == end_fofs + 1 && blkaddr == end_blkaddr + 1) {
		fi->ext.len++;
		goto end_update;
	}

	/* Split the existing extent */
	if (fi->ext.len > 1 &&
		fofs >= start_fofs && fofs <= end_fofs) {
		if ((end_fofs - fofs) < (fi->ext.len >> 1)) {
			fi->ext.len = fofs - start_fofs;
		} else {
			fi->ext.fofs = fofs + 1;
			fi->ext.blk = start_blkaddr + fofs - start_fofs + 1;
			fi->ext.len -= fofs - start_fofs + 1;
		}
	} else {
		need_update = false;
	}

	/* Finally, if the extent is very fragmented, let's drop the cache. */
	if (fi->ext.len < F2FS_MIN_EXTENT_LEN) {
		fi->ext.len = 0;
		set_inode_flag(fi, FI_NO_EXTENT);
		need_update = true;
	}
end_update:
	write_unlock(&fi->ext_lock);
	return need_update;
}

static struct extent_node *__attach_extent_node(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct rb_node *parent, struct rb_node **p)
{
	struct extent_node *en;

	en = kmem_cache_alloc(extent_node_slab, GFP_ATOMIC);
	if (!en)
		return NULL;

	en->ei = *ei;
	INIT_LIST_HEAD(&en->list);

	rb_link_node(&en->rb_node, parent, p);
	rb_insert_color(&en->rb_node, &et->root);
	et->count++;
	atomic_inc(&sbi->total_ext_node);
	return en;
}

static void __detach_extent_node(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_node *en)
{
	rb_erase(&en->rb_node, &et->root);
	et->count--;
	atomic_dec(&sbi->total_ext_node);

	if (et->cached_en == en)
		et->cached_en = NULL;
}

static struct extent_tree *__find_extent_tree(struct f2fs_sb_info *sbi,
							nid_t ino)
{
	struct extent_tree *et;

	down_read(&sbi->extent_tree_lock);
	et = radix_tree_lookup(&sbi->extent_tree_root, ino);
	if (!et) {
		up_read(&sbi->extent_tree_lock);
		return NULL;
	}
	atomic_inc(&et->refcount);
	up_read(&sbi->extent_tree_lock);

	return et;
}

static struct extent_tree *__grab_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et;
	nid_t ino = inode->i_ino;

	down_write(&sbi->extent_tree_lock);
	et = radix_tree_lookup(&sbi->extent_tree_root, ino);
	if (!et) {
		et = f2fs_kmem_cache_alloc(extent_tree_slab, GFP_NOFS);
		f2fs_radix_tree_insert(&sbi->extent_tree_root, ino, et);
		memset(et, 0, sizeof(struct extent_tree));
		et->ino = ino;
		et->root = RB_ROOT;
		et->cached_en = NULL;
		rwlock_init(&et->lock);
		atomic_set(&et->refcount, 0);
		et->count = 0;
		sbi->total_ext_tree++;
	}
	atomic_inc(&et->refcount);
	up_write(&sbi->extent_tree_lock);

	return et;
}

static struct extent_node *__lookup_extent_tree(struct extent_tree *et,
							unsigned int fofs)
{
	struct rb_node *node = et->root.rb_node;
	struct extent_node *en;

	if (et->cached_en) {
		struct extent_info *cei = &et->cached_en->ei;

		if (cei->fofs <= fofs && cei->fofs + cei->len > fofs)
			return et->cached_en;
	}

	while (node) {
		en = rb_entry(node, struct extent_node, rb_node);

		if (fofs < en->ei.fofs) {
			node = node->rb_left;
		} else if (fofs >= en->ei.fofs + en->ei.len) {
			node = node->rb_right;
		} else {
			et->cached_en = en;
			return en;
		}
	}
	return NULL;
}

static struct extent_node *__try_back_merge(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_node *en)
{
	struct extent_node *prev;
	struct rb_node *node;

	node = rb_prev(&en->rb_node);
	if (!node)
		return NULL;

	prev = rb_entry(node, struct extent_node, rb_node);
	if (__is_back_mergeable(&en->ei, &prev->ei)) {
		en->ei.fofs = prev->ei.fofs;
		en->ei.blk = prev->ei.blk;
		en->ei.len += prev->ei.len;
		__detach_extent_node(sbi, et, prev);
		return prev;
	}
	return NULL;
}

static struct extent_node *__try_front_merge(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_node *en)
{
	struct extent_node *next;
	struct rb_node *node;

	node = rb_next(&en->rb_node);
	if (!node)
		return NULL;

	next = rb_entry(node, struct extent_node, rb_node);
	if (__is_front_mergeable(&en->ei, &next->ei)) {
		en->ei.len += next->ei.len;
		__detach_extent_node(sbi, et, next);
		return next;
	}
	return NULL;
}

static struct extent_node *__insert_extent_tree(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct extent_node **den)
{
	struct rb_node **p = &et->root.rb_node;
	struct rb_node *parent = NULL;
	struct extent_node *en;

	while (*p) {
		parent = *p;
		en = rb_entry(parent, struct extent_node, rb_node);

		if (ei->fofs < en->ei.fofs) {
			if (__is_front_mergeable(ei, &en->ei)) {
				f2fs_bug_on(sbi, !den);
				en->ei.fofs = ei->fofs;
				en->ei.blk = ei->blk;
				en->ei.len += ei->len;
				*den = __try_back_merge(sbi, et, en);
				return en;
			}
			p = &(*p)->rb_left;
		} else if (ei->fofs >= en->ei.fofs + en->ei.len) {
			if (__is_back_mergeable(ei, &en->ei)) {
				f2fs_bug_on(sbi, !den);
				en->ei.len += ei->len;
				*den = __try_front_merge(sbi, et, en);
				return en;
			}
			p = &(*p)->rb_right;
		} else {
			f2fs_bug_on(sbi, 1);
		}
	}

	return __attach_extent_node(sbi, et, ei, parent, p);
}

static unsigned int __free_extent_tree(struct f2fs_sb_info *sbi,
					struct extent_tree *et, bool free_all)
{
	struct rb_node *node, *next;
	struct extent_node *en;
	unsigned int count = et->count;

	node = rb_first(&et->root);
	while (node) {
		next = rb_next(node);
		en = rb_entry(node, struct extent_node, rb_node);

		if (free_all) {
			spin_lock(&sbi->extent_lock);
			if (!list_empty(&en->list))
				list_del_init(&en->list);
			spin_unlock(&sbi->extent_lock);
		}

		if (free_all || list_empty(&en->list)) {
			__detach_extent_node(sbi, et, en);
			kmem_cache_free(extent_node_slab, en);
		}
		node = next;
	}

	return count - et->count;
}

static void f2fs_init_extent_tree(struct inode *inode,
						struct f2fs_extent *i_ext)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et;
	struct extent_node *en;
	struct extent_info ei;

	if (le32_to_cpu(i_ext->len) < F2FS_MIN_EXTENT_LEN)
		return;

	et = __grab_extent_tree(inode);

	write_lock(&et->lock);
	if (et->count)
		goto out;

	set_extent_info(&ei, le32_to_cpu(i_ext->fofs),
		le32_to_cpu(i_ext->blk), le32_to_cpu(i_ext->len));

	en = __insert_extent_tree(sbi, et, &ei, NULL);
	if (en) {
		et->cached_en = en;

		spin_lock(&sbi->extent_lock);
		list_add_tail(&en->list, &sbi->extent_list);
		spin_unlock(&sbi->extent_lock);
	}
out:
	write_unlock(&et->lock);
	atomic_dec(&et->refcount);
}

static bool f2fs_lookup_extent_tree(struct inode *inode, pgoff_t pgofs,
							struct extent_info *ei)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et;
	struct extent_node *en;

	trace_f2fs_lookup_extent_tree_start(inode, pgofs);

	et = __find_extent_tree(sbi, inode->i_ino);
	if (!et)
		return false;

	read_lock(&et->lock);
	en = __lookup_extent_tree(et, pgofs);
	if (en) {
		*ei = en->ei;
		spin_lock(&sbi->extent_lock);
		if (!list_empty(&en->list))
			list_move_tail(&en->list, &sbi->extent_list);
		spin_unlock(&sbi->extent_lock);
		stat_inc_read_hit(sbi->sb);
	}
	stat_inc_total_hit(sbi->sb);
	read_unlock(&et->lock);

	trace_f2fs_lookup_extent_tree_end(inode, pgofs, en);

	atomic_dec(&et->refcount);
	return en ? true : false;
}

static void f2fs_update_extent_tree(struct inode *inode, pgoff_t fofs,
							block_t blkaddr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et;
	struct extent_node *en = NULL, *en1 = NULL, *en2 = NULL, *en3 = NULL;
	struct extent_node *den = NULL;
	struct extent_info ei, dei;
	unsigned int endofs;

	trace_f2fs_update_extent_tree(inode, fofs, blkaddr);

	et = __grab_extent_tree(inode);

	write_lock(&et->lock);

	/* 1. lookup and remove existing extent info in cache */
	en = __lookup_extent_tree(et, fofs);
	if (!en)
		goto update_extent;

	dei = en->ei;
	__detach_extent_node(sbi, et, en);

	/* 2. if extent can be split more, split and insert the left part */
	if (dei.len > 1) {
		/*  insert left part of split extent into cache */
		if (fofs - dei.fofs >= F2FS_MIN_EXTENT_LEN) {
			set_extent_info(&ei, dei.fofs, dei.blk,
							fofs - dei.fofs);
			en1 = __insert_extent_tree(sbi, et, &ei, NULL);
		}

		/* insert right part of split extent into cache */
		endofs = dei.fofs + dei.len - 1;
		if (endofs - fofs >= F2FS_MIN_EXTENT_LEN) {
			set_extent_info(&ei, fofs + 1,
				fofs - dei.fofs + dei.blk, endofs - fofs);
			en2 = __insert_extent_tree(sbi, et, &ei, NULL);
		}
	}

update_extent:
	/* 3. update extent in extent cache */
	if (blkaddr) {
		set_extent_info(&ei, fofs, blkaddr, 1);
		en3 = __insert_extent_tree(sbi, et, &ei, &den);
	}

	/* 4. update in global extent list */
	spin_lock(&sbi->extent_lock);
	if (en && !list_empty(&en->list))
		list_del(&en->list);
	/*
	 * en1 and en2 split from en, they will become more and more smaller
	 * fragments after splitting several times. So if the length is smaller
	 * than F2FS_MIN_EXTENT_LEN, we will not add them into extent tree.
	 */
	if (en1)
		list_add_tail(&en1->list, &sbi->extent_list);
	if (en2)
		list_add_tail(&en2->list, &sbi->extent_list);
	if (en3) {
		if (list_empty(&en3->list))
			list_add_tail(&en3->list, &sbi->extent_list);
		else
			list_move_tail(&en3->list, &sbi->extent_list);
	}
	if (den && !list_empty(&den->list))
		list_del(&den->list);
	spin_unlock(&sbi->extent_lock);

	/* 5. release extent node */
	if (en)
		kmem_cache_free(extent_node_slab, en);
	if (den)
		kmem_cache_free(extent_node_slab, den);

	write_unlock(&et->lock);
	atomic_dec(&et->refcount);
}

void f2fs_preserve_extent_tree(struct inode *inode)
{
	struct extent_tree *et;
	struct extent_info *ext = &F2FS_I(inode)->ext;
	bool sync = false;

	if (!test_opt(F2FS_I_SB(inode), EXTENT_CACHE))
		return;

	et = __find_extent_tree(F2FS_I_SB(inode), inode->i_ino);
	if (!et) {
		if (ext->len) {
			ext->len = 0;
			update_inode_page(inode);
		}
		return;
	}

	read_lock(&et->lock);
	if (et->count) {
		struct extent_node *en;

		if (et->cached_en) {
			en = et->cached_en;
		} else {
			struct rb_node *node = rb_first(&et->root);

			if (!node)
				node = rb_last(&et->root);
			en = rb_entry(node, struct extent_node, rb_node);
		}

		if (__is_extent_same(ext, &en->ei))
			goto out;

		*ext = en->ei;
		sync = true;
	} else if (ext->len) {
		ext->len = 0;
		sync = true;
	}
out:
	read_unlock(&et->lock);
	atomic_dec(&et->refcount);

	if (sync)
		update_inode_page(inode);
}

void f2fs_shrink_extent_tree(struct f2fs_sb_info *sbi, int nr_shrink)
{
	struct extent_tree *treevec[EXT_TREE_VEC_SIZE];
	struct extent_node *en, *tmp;
	unsigned long ino = F2FS_ROOT_INO(sbi);
	struct radix_tree_iter iter;
	void **slot;
	unsigned int found;
	unsigned int node_cnt = 0, tree_cnt = 0;

	if (!test_opt(sbi, EXTENT_CACHE))
		return;

	if (available_free_memory(sbi, EXTENT_CACHE))
		return;

	spin_lock(&sbi->extent_lock);
	list_for_each_entry_safe(en, tmp, &sbi->extent_list, list) {
		if (!nr_shrink--)
			break;
		list_del_init(&en->list);
	}
	spin_unlock(&sbi->extent_lock);

	down_read(&sbi->extent_tree_lock);
	while ((found = radix_tree_gang_lookup(&sbi->extent_tree_root,
				(void **)treevec, ino, EXT_TREE_VEC_SIZE))) {
		unsigned i;

		ino = treevec[found - 1]->ino + 1;
		for (i = 0; i < found; i++) {
			struct extent_tree *et = treevec[i];

			atomic_inc(&et->refcount);
			write_lock(&et->lock);
			node_cnt += __free_extent_tree(sbi, et, false);
			write_unlock(&et->lock);
			atomic_dec(&et->refcount);
		}
	}
	up_read(&sbi->extent_tree_lock);

	down_write(&sbi->extent_tree_lock);
	radix_tree_for_each_slot(slot, &sbi->extent_tree_root, &iter,
							F2FS_ROOT_INO(sbi)) {
		struct extent_tree *et = (struct extent_tree *)*slot;

		if (!atomic_read(&et->refcount) && !et->count) {
			radix_tree_delete(&sbi->extent_tree_root, et->ino);
			kmem_cache_free(extent_tree_slab, et);
			sbi->total_ext_tree--;
			tree_cnt++;
		}
	}
	up_write(&sbi->extent_tree_lock);

	trace_f2fs_shrink_extent_tree(sbi, node_cnt, tree_cnt);
}

void f2fs_destroy_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et;
	unsigned int node_cnt = 0;

	if (!test_opt(sbi, EXTENT_CACHE))
		return;

	et = __find_extent_tree(sbi, inode->i_ino);
	if (!et)
		goto out;

	/* free all extent info belong to this extent tree */
	write_lock(&et->lock);
	node_cnt = __free_extent_tree(sbi, et, true);
	write_unlock(&et->lock);

	atomic_dec(&et->refcount);

	/* try to find and delete extent tree entry in radix tree */
	down_write(&sbi->extent_tree_lock);
	et = radix_tree_lookup(&sbi->extent_tree_root, inode->i_ino);
	if (!et) {
		up_write(&sbi->extent_tree_lock);
		goto out;
	}
	f2fs_bug_on(sbi, atomic_read(&et->refcount) || et->count);
	radix_tree_delete(&sbi->extent_tree_root, inode->i_ino);
	kmem_cache_free(extent_tree_slab, et);
	sbi->total_ext_tree--;
	up_write(&sbi->extent_tree_lock);
out:
	trace_f2fs_destroy_extent_tree(inode, node_cnt);
	return;
}

void f2fs_init_extent_cache(struct inode *inode, struct f2fs_extent *i_ext)
{
	if (test_opt(F2FS_I_SB(inode), EXTENT_CACHE))
		f2fs_init_extent_tree(inode, i_ext);

	write_lock(&F2FS_I(inode)->ext_lock);
	get_extent_info(&F2FS_I(inode)->ext, *i_ext);
	write_unlock(&F2FS_I(inode)->ext_lock);
}

static bool f2fs_lookup_extent_cache(struct inode *inode, pgoff_t pgofs,
							struct extent_info *ei)
{
	if (is_inode_flag_set(F2FS_I(inode), FI_NO_EXTENT))
		return false;

	if (test_opt(F2FS_I_SB(inode), EXTENT_CACHE))
		return f2fs_lookup_extent_tree(inode, pgofs, ei);

	return lookup_extent_info(inode, pgofs, ei);
}

void f2fs_update_extent_cache(struct dnode_of_data *dn)
{
	struct f2fs_inode_info *fi = F2FS_I(dn->inode);
	pgoff_t fofs;

	f2fs_bug_on(F2FS_I_SB(dn->inode), dn->data_blkaddr == NEW_ADDR);

	if (is_inode_flag_set(fi, FI_NO_EXTENT))
		return;

	fofs = start_bidx_of_node(ofs_of_node(dn->node_page), fi) +
							dn->ofs_in_node;

	if (test_opt(F2FS_I_SB(dn->inode), EXTENT_CACHE))
		return f2fs_update_extent_tree(dn->inode, fofs,
							dn->data_blkaddr);

	if (update_extent_info(dn->inode, fofs, dn->data_blkaddr))
		sync_inode_page(dn);
}

struct page *find_data_page(struct inode *inode, pgoff_t index, bool sync)
{
	struct address_space *mapping = inode->i_mapping;
	struct dnode_of_data dn;
	struct page *page;
	struct extent_info ei;
	int err;
	struct f2fs_io_info fio = {
		.type = DATA,
		.rw = sync ? READ_SYNC : READA,
	};

	/*
	 * If sync is false, it needs to check its block allocation.
	 * This is need and triggered by two flows:
	 *   gc and truncate_partial_data_page.
	 */
	if (!sync)
		goto search;

	page = find_get_page(mapping, index);
	if (page && PageUptodate(page))
		return page;
	f2fs_put_page(page, 0);
search:
	if (f2fs_lookup_extent_cache(inode, index, &ei)) {
		dn.data_blkaddr = ei.blk + index - ei.fofs;
		goto got_it;
	}

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, index, LOOKUP_NODE);
	if (err)
		return ERR_PTR(err);
	f2fs_put_dnode(&dn);

	if (dn.data_blkaddr == NULL_ADDR)
		return ERR_PTR(-ENOENT);

	/* By fallocate(), there is no cached page, but with NEW_ADDR */
	if (unlikely(dn.data_blkaddr == NEW_ADDR))
		return ERR_PTR(-EINVAL);

got_it:
	page = grab_cache_page(mapping, index);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (PageUptodate(page)) {
		unlock_page(page);
		return page;
	}

	fio.blk_addr = dn.data_blkaddr;
	err = f2fs_submit_page_bio(F2FS_I_SB(inode), page, &fio);
	if (err)
		return ERR_PTR(err);

	if (sync) {
		wait_on_page_locked(page);
		if (unlikely(!PageUptodate(page))) {
			f2fs_put_page(page, 0);
			return ERR_PTR(-EIO);
		}
	}
	return page;
}

/*
 * If it tries to access a hole, return an error.
 * Because, the callers, functions in dir.c and GC, should be able to know
 * whether this page exists or not.
 */
struct page *get_lock_data_page(struct inode *inode, pgoff_t index)
{
	struct address_space *mapping = inode->i_mapping;
	struct dnode_of_data dn;
	struct page *page;
	struct extent_info ei;
	int err;
	struct f2fs_io_info fio = {
		.type = DATA,
		.rw = READ_SYNC,
	};
repeat:
	page = grab_cache_page(mapping, index);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (f2fs_lookup_extent_cache(inode, index, &ei)) {
		dn.data_blkaddr = ei.blk + index - ei.fofs;
		goto got_it;
	}

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, index, LOOKUP_NODE);
	if (err) {
		f2fs_put_page(page, 1);
		return ERR_PTR(err);
	}
	f2fs_put_dnode(&dn);

	if (unlikely(dn.data_blkaddr == NULL_ADDR)) {
		f2fs_put_page(page, 1);
		return ERR_PTR(-ENOENT);
	}

got_it:
	if (PageUptodate(page))
		return page;

	/*
	 * A new dentry page is allocated but not able to be written, since its
	 * new inode page couldn't be allocated due to -ENOSPC.
	 * In such the case, its blkaddr can be remained as NEW_ADDR.
	 * see, f2fs_add_link -> get_new_data_page -> init_inode_metadata.
	 */
	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
		SetPageUptodate(page);
		return page;
	}

	fio.blk_addr = dn.data_blkaddr;
	err = f2fs_submit_page_bio(F2FS_I_SB(inode), page, &fio);
	if (err)
		return ERR_PTR(err);

	lock_page(page);
	if (unlikely(!PageUptodate(page))) {
		f2fs_put_page(page, 1);
		return ERR_PTR(-EIO);
	}
	if (unlikely(page->mapping != mapping)) {
		f2fs_put_page(page, 1);
		goto repeat;
	}
	return page;
}

/*
 * Caller ensures that this data page is never allocated.
 * A new zero-filled data page is allocated in the page cache.
 *
 * Also, caller should grab and release a rwsem by calling f2fs_lock_op() and
 * f2fs_unlock_op().
 * Note that, ipage is set only by make_empty_dir.
 */
struct page *get_new_data_page(struct inode *inode,
		struct page *ipage, pgoff_t index, bool new_i_size)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	struct dnode_of_data dn;
	int err;

	set_new_dnode(&dn, inode, ipage, NULL, 0);
	err = f2fs_reserve_block(&dn, index);
	if (err)
		return ERR_PTR(err);
repeat:
	page = grab_cache_page(mapping, index);
	if (!page) {
		err = -ENOMEM;
		goto put_err;
	}

	if (PageUptodate(page))
		return page;

	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
		SetPageUptodate(page);
	} else {
		struct f2fs_io_info fio = {
			.type = DATA,
			.rw = READ_SYNC,
			.blk_addr = dn.data_blkaddr,
		};
		err = f2fs_submit_page_bio(F2FS_I_SB(inode), page, &fio);
		if (err)
			goto put_err;

		lock_page(page);
		if (unlikely(!PageUptodate(page))) {
			f2fs_put_page(page, 1);
			err = -EIO;
			goto put_err;
		}
		if (unlikely(page->mapping != mapping)) {
			f2fs_put_page(page, 1);
			goto repeat;
		}
	}

	if (new_i_size &&
		i_size_read(inode) < ((index + 1) << PAGE_CACHE_SHIFT)) {
		i_size_write(inode, ((index + 1) << PAGE_CACHE_SHIFT));
		/* Only the directory inode sets new_i_size */
		set_inode_flag(F2FS_I(inode), FI_UPDATE_DIR);
	}
	return page;

put_err:
	f2fs_put_dnode(&dn);
	return ERR_PTR(err);
}

static int __allocate_data_block(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct f2fs_inode_info *fi = F2FS_I(dn->inode);
	struct f2fs_summary sum;
	struct node_info ni;
	int seg = CURSEG_WARM_DATA;
	pgoff_t fofs;

	if (unlikely(is_inode_flag_set(F2FS_I(dn->inode), FI_NO_ALLOC)))
		return -EPERM;

	dn->data_blkaddr = datablock_addr(dn->node_page, dn->ofs_in_node);
	if (dn->data_blkaddr == NEW_ADDR)
		goto alloc;

	if (unlikely(!inc_valid_block_count(sbi, dn->inode, 1)))
		return -ENOSPC;

alloc:
	get_node_info(sbi, dn->nid, &ni);
	set_summary(&sum, dn->nid, dn->ofs_in_node, ni.version);

	if (dn->ofs_in_node == 0 && dn->inode_page == dn->node_page)
		seg = CURSEG_DIRECT_IO;

	allocate_data_block(sbi, NULL, dn->data_blkaddr, &dn->data_blkaddr,
								&sum, seg);

	/* direct IO doesn't use extent cache to maximize the performance */
	set_data_blkaddr(dn);

	/* update i_size */
	fofs = start_bidx_of_node(ofs_of_node(dn->node_page), fi) +
							dn->ofs_in_node;
	if (i_size_read(dn->inode) < ((fofs + 1) << PAGE_CACHE_SHIFT))
		i_size_write(dn->inode, ((fofs + 1) << PAGE_CACHE_SHIFT));

	return 0;
}

static void __allocate_data_blocks(struct inode *inode, loff_t offset,
							size_t count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct dnode_of_data dn;
	u64 start = F2FS_BYTES_TO_BLK(offset);
	u64 len = F2FS_BYTES_TO_BLK(count);
	bool allocated;
	u64 end_offset;

	while (len) {
		f2fs_balance_fs(sbi);
		f2fs_lock_op(sbi);

		/* When reading holes, we need its node page */
		set_new_dnode(&dn, inode, NULL, NULL, 0);
		if (get_dnode_of_data(&dn, start, ALLOC_NODE))
			goto out;

		allocated = false;
		end_offset = ADDRS_PER_PAGE(dn.node_page, F2FS_I(inode));

		while (dn.ofs_in_node < end_offset && len) {
			block_t blkaddr;

			blkaddr = datablock_addr(dn.node_page, dn.ofs_in_node);
			if (blkaddr == NULL_ADDR || blkaddr == NEW_ADDR) {
				if (__allocate_data_block(&dn))
					goto sync_out;
				allocated = true;
			}
			len--;
			start++;
			dn.ofs_in_node++;
		}

		if (allocated)
			sync_inode_page(&dn);

		f2fs_put_dnode(&dn);
		f2fs_unlock_op(sbi);
	}
	return;

sync_out:
	if (allocated)
		sync_inode_page(&dn);
	f2fs_put_dnode(&dn);
out:
	f2fs_unlock_op(sbi);
	return;
}

/*
 * f2fs_map_blocks() now supported readahead/bmap/rw direct_IO with
 * f2fs_map_blocks structure.
 * If original data blocks are allocated, then give them to blockdev.
 * Otherwise,
 *     a. preallocate requested block addresses
 *     b. do not use extent cache for better performance
 *     c. give the block addresses to blockdev
 */
static int f2fs_map_blocks(struct inode *inode, struct f2fs_map_blocks *map,
			int create, bool fiemap)
{
	unsigned int maxblocks = map->m_len;
	struct dnode_of_data dn;
	int mode = create ? ALLOC_NODE : LOOKUP_NODE_RA;
	pgoff_t pgofs, end_offset;
	int err = 0, ofs = 1;
	struct extent_info ei;
	bool allocated = false;

	map->m_len = 0;
	map->m_flags = 0;

	/* it only supports block size == page size */
	pgofs =	(pgoff_t)map->m_lblk;

	if (f2fs_lookup_extent_cache(inode, pgofs, &ei)) {
		map->m_pblk = ei.blk + pgofs - ei.fofs;
		map->m_len = min((pgoff_t)maxblocks, ei.fofs + ei.len - pgofs);
		map->m_flags = F2FS_MAP_MAPPED;
		goto out;
	}

	if (create)
		f2fs_lock_op(F2FS_I_SB(inode));

	/* When reading holes, we need its node page */
	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, pgofs, mode);
	if (err) {
		if (err == -ENOENT)
			err = 0;
		goto unlock_out;
	}
	if (dn.data_blkaddr == NEW_ADDR && !fiemap)
		goto put_out;

	if (dn.data_blkaddr != NULL_ADDR) {
		map->m_flags = F2FS_MAP_MAPPED;
		map->m_pblk = dn.data_blkaddr;
	} else if (create) {
		err = __allocate_data_block(&dn);
		if (err)
			goto put_out;
		allocated = true;
		map->m_flags = F2FS_MAP_NEW | F2FS_MAP_MAPPED;
		map->m_pblk = dn.data_blkaddr;
	} else {
		goto put_out;
	}

	end_offset = ADDRS_PER_PAGE(dn.node_page, F2FS_I(inode));
	map->m_len = 1;
	dn.ofs_in_node++;
	pgofs++;

get_next:
	if (dn.ofs_in_node >= end_offset) {
		if (allocated)
			sync_inode_page(&dn);
		allocated = false;
		f2fs_put_dnode(&dn);

		set_new_dnode(&dn, inode, NULL, NULL, 0);
		err = get_dnode_of_data(&dn, pgofs, mode);
		if (err) {
			if (err == -ENOENT)
				err = 0;
			goto unlock_out;
		}
		if (dn.data_blkaddr == NEW_ADDR && !fiemap)
			goto put_out;

		end_offset = ADDRS_PER_PAGE(dn.node_page, F2FS_I(inode));
	}

	if (maxblocks > map->m_len) {
		block_t blkaddr = datablock_addr(dn.node_page, dn.ofs_in_node);
		if (blkaddr == NULL_ADDR && create) {
			err = __allocate_data_block(&dn);
			if (err)
				goto sync_out;
			allocated = true;
			map->m_flags |= F2FS_MAP_NEW;
			blkaddr = dn.data_blkaddr;
		}
		/* Give more consecutive addresses for the readahead */
		if (map->m_pblk != NEW_ADDR && blkaddr == (map->m_pblk + ofs)) {
			ofs++;
			dn.ofs_in_node++;
			pgofs++;
			map->m_len++;
			goto get_next;
		}
	}
sync_out:
	if (allocated)
		sync_inode_page(&dn);
put_out:
	f2fs_put_dnode(&dn);
unlock_out:
	if (create)
		f2fs_unlock_op(F2FS_I_SB(inode));
out:
	trace_f2fs_map_blocks(inode, map, err);
	return err;
}

static int __get_data_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh, int create, bool fiemap)
{
	struct f2fs_map_blocks map;
	int ret;

	map.m_lblk = iblock;
	map.m_len = bh->b_size >> inode->i_blkbits;

	ret = f2fs_map_blocks(inode, &map, create, fiemap);
	if (!ret) {
		map_bh(bh, inode->i_sb, map.m_pblk);
		bh->b_state = (bh->b_state & ~F2FS_MAP_FLAGS) | map.m_flags;
		bh->b_size = map.m_len << inode->i_blkbits;
	}
	return ret;
}

static int get_data_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	return __get_data_block(inode, iblock, bh_result, create, false);
}

static int get_data_block_fiemap(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	return __get_data_block(inode, iblock, bh_result, create, true);
}

int f2fs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	return generic_block_fiemap(inode, fieinfo,
				start, len, get_data_block_fiemap);
}

/*
 * This function was originally taken from fs/mpage.c, and customized for f2fs.
 * Major change was from block_size == page_size in f2fs by default.
 */
static int f2fs_mpage_readpages(struct address_space *mapping,
			struct list_head *pages, struct page *page,
			unsigned nr_pages)
{
	struct bio *bio = NULL;
	unsigned page_idx;
	sector_t last_block_in_bio = 0;
	struct inode *inode = mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocksize = 1 << blkbits;
	sector_t block_in_file;
	sector_t last_block;
	sector_t last_block_in_file;
	sector_t block_nr;
	struct block_device *bdev = inode->i_sb->s_bdev;
	struct f2fs_map_blocks map;

	map.m_pblk = 0;
	map.m_lblk = 0;
	map.m_len = 0;
	map.m_flags = 0;

	for (page_idx = 0; nr_pages; page_idx++, nr_pages--) {

		prefetchw(&page->flags);
		if (pages) {
			page = list_entry(pages->prev, struct page, lru);
			list_del(&page->lru);
			if (add_to_page_cache_lru(page, mapping,
						  page->index, GFP_KERNEL))
				goto next_page;
		}

		block_in_file = (sector_t)page->index;
		last_block = block_in_file + nr_pages;
		last_block_in_file = (i_size_read(inode) + blocksize - 1) >>
								blkbits;
		if (last_block > last_block_in_file)
			last_block = last_block_in_file;

		/*
		 * Map blocks using the previous result first.
		 */
		if ((map.m_flags & F2FS_MAP_MAPPED) &&
				block_in_file > map.m_lblk &&
				block_in_file < (map.m_lblk + map.m_len))
			goto got_it;

		/*
		 * Then do more f2fs_map_blocks() calls until we are
		 * done with this page.
		 */
		map.m_flags = 0;

		if (block_in_file < last_block) {
			map.m_lblk = block_in_file;
			map.m_len = last_block - block_in_file;

			if (f2fs_map_blocks(inode, &map, 0, false))
				goto set_error_page;
		}
got_it:
		if ((map.m_flags & F2FS_MAP_MAPPED)) {
			block_nr = map.m_pblk + block_in_file - map.m_lblk;
			SetPageMappedToDisk(page);

			if (!PageUptodate(page) && !cleancache_get_page(page)) {
				SetPageUptodate(page);
				goto confused;
			}
		} else {
			zero_user_segment(page, 0, PAGE_CACHE_SIZE);
			SetPageUptodate(page);
			unlock_page(page);
			goto next_page;
		}

		/*
		 * This page will go to BIO.  Do we need to send this
		 * BIO off first?
		 */
		if (bio && (last_block_in_bio != block_nr - 1)) {
submit_and_realloc:
			submit_bio(READ, bio);
			bio = NULL;
		}
		if (bio == NULL) {
			bio = bio_alloc(GFP_KERNEL,
				min_t(int, nr_pages, bio_get_nr_vecs(bdev)));
			if (!bio)
				goto set_error_page;
			bio->bi_bdev = bdev;
			bio->bi_iter.bi_sector = SECTOR_FROM_BLOCK(block_nr);
			bio->bi_end_io = mpage_end_io;
			bio->bi_private = NULL;
		}

		if (bio_add_page(bio, page, blocksize, 0) < blocksize)
			goto submit_and_realloc;

		last_block_in_bio = block_nr;
		goto next_page;
set_error_page:
		SetPageError(page);
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
		unlock_page(page);
		goto next_page;
confused:
		if (bio) {
			submit_bio(READ, bio);
			bio = NULL;
		}
		unlock_page(page);
next_page:
		if (pages)
			page_cache_release(page);
	}
	BUG_ON(pages && !list_empty(pages));
	if (bio)
		submit_bio(READ, bio);
	return 0;
}

static int f2fs_read_data_page(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	int ret = -EAGAIN;

	trace_f2fs_readpage(page, DATA);

	/* If the file has inline data, try to read it directly */
	if (f2fs_has_inline_data(inode))
		ret = f2fs_read_inline_data(inode, page);
	if (ret == -EAGAIN)
		ret = f2fs_mpage_readpages(page->mapping, NULL, page, 1);
	return ret;
}

static int f2fs_read_data_pages(struct file *file,
			struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages)
{
	struct inode *inode = file->f_mapping->host;

	/* If the file has inline data, skip readpages */
	if (f2fs_has_inline_data(inode))
		return 0;

	return f2fs_mpage_readpages(mapping, pages, NULL, nr_pages);
}

int do_write_data_page(struct page *page, struct f2fs_io_info *fio)
{
	struct inode *inode = page->mapping->host;
	struct dnode_of_data dn;
	int err = 0;

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, page->index, LOOKUP_NODE);
	if (err)
		return err;

	fio->blk_addr = dn.data_blkaddr;

	/* This page is already truncated */
	if (fio->blk_addr == NULL_ADDR) {
		ClearPageUptodate(page);
		goto out_writepage;
	}

	set_page_writeback(page);

	/*
	 * If current allocation needs SSR,
	 * it had better in-place writes for updated data.
	 */
	if (unlikely(fio->blk_addr != NEW_ADDR &&
			!is_cold_data(page) &&
			need_inplace_update(inode))) {
		rewrite_data_page(page, fio);
		set_inode_flag(F2FS_I(inode), FI_UPDATE_WRITE);
		trace_f2fs_do_write_data_page(page, IPU);
	} else {
		write_data_page(page, &dn, fio);
		set_data_blkaddr(&dn);
		f2fs_update_extent_cache(&dn);
		trace_f2fs_do_write_data_page(page, OPU);
		set_inode_flag(F2FS_I(inode), FI_APPEND_WRITE);
		if (page->index == 0)
			set_inode_flag(F2FS_I(inode), FI_FIRST_BLOCK_WRITTEN);
	}
out_writepage:
	f2fs_put_dnode(&dn);
	return err;
}

static int f2fs_write_data_page(struct page *page,
					struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	loff_t i_size = i_size_read(inode);
	const pgoff_t end_index = ((unsigned long long) i_size)
							>> PAGE_CACHE_SHIFT;
	unsigned offset = 0;
	bool need_balance_fs = false;
	int err = 0;
	struct f2fs_io_info fio = {
		.type = DATA,
		.rw = (wbc->sync_mode == WB_SYNC_ALL) ? WRITE_SYNC : WRITE,
	};

	trace_f2fs_writepage(page, DATA);

	if (page->index < end_index)
		goto write;

	/*
	 * If the offset is out-of-range of file size,
	 * this page does not have to be written to disk.
	 */
	offset = i_size & (PAGE_CACHE_SIZE - 1);
	if ((page->index >= end_index + 1) || !offset)
		goto out;

	zero_user_segment(page, offset, PAGE_CACHE_SIZE);
write:
	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		goto redirty_out;
	if (f2fs_is_drop_cache(inode))
		goto out;
	if (f2fs_is_volatile_file(inode) && !wbc->for_reclaim &&
			available_free_memory(sbi, BASE_CHECK))
		goto redirty_out;

	/* Dentry blocks are controlled by checkpoint */
	if (S_ISDIR(inode->i_mode)) {
		if (unlikely(f2fs_cp_error(sbi)))
			goto redirty_out;
		err = do_write_data_page(page, &fio);
		goto done;
	}

	/* we should bypass data pages to proceed the kworkder jobs */
	if (unlikely(f2fs_cp_error(sbi))) {
		SetPageError(page);
		goto out;
	}

	if (!wbc->for_reclaim)
		need_balance_fs = true;
	else if (has_not_enough_free_secs(sbi, 0))
		goto redirty_out;

	err = -EAGAIN;
	f2fs_lock_op(sbi);
	if (f2fs_has_inline_data(inode))
		err = f2fs_write_inline_data(inode, page);
	if (err == -EAGAIN)
		err = do_write_data_page(page, &fio);
	f2fs_unlock_op(sbi);
done:
	if (err && err != -ENOENT)
		goto redirty_out;

	clear_cold_data(page);
out:
	inode_dec_dirty_pages(inode);
	if (err)
		ClearPageUptodate(page);
	unlock_page(page);
	if (need_balance_fs)
		f2fs_balance_fs(sbi);
	if (wbc->for_reclaim)
		f2fs_submit_merged_bio(sbi, DATA, WRITE);
	return 0;

redirty_out:
	redirty_page_for_writepage(wbc, page);
	return AOP_WRITEPAGE_ACTIVATE;
}

static int __f2fs_writepage(struct page *page, struct writeback_control *wbc,
			void *data)
{
	struct address_space *mapping = data;
	int ret = mapping->a_ops->writepage(page, wbc);
	mapping_set_error(mapping, ret);
	return ret;
}

static int f2fs_write_data_pages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	bool locked = false;
	int ret;
	long diff;

	trace_f2fs_writepages(mapping->host, wbc, DATA);

	/* deal with chardevs and other special file */
	if (!mapping->a_ops->writepage)
		return 0;

	if (S_ISDIR(inode->i_mode) && wbc->sync_mode == WB_SYNC_NONE &&
			get_dirty_pages(inode) < nr_pages_to_skip(sbi, DATA) &&
			available_free_memory(sbi, DIRTY_DENTS))
		goto skip_write;

	/* during POR, we don't need to trigger writepage at all. */
	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		goto skip_write;

	diff = nr_pages_to_write(sbi, DATA, wbc);

	if (!S_ISDIR(inode->i_mode)) {
		mutex_lock(&sbi->writepages);
		locked = true;
	}
	ret = write_cache_pages(mapping, wbc, __f2fs_writepage, mapping);
	if (locked)
		mutex_unlock(&sbi->writepages);

	f2fs_submit_merged_bio(sbi, DATA, WRITE);

	remove_dirty_dir_inode(inode);

	wbc->nr_to_write = max((long)0, wbc->nr_to_write - diff);
	return ret;

skip_write:
	wbc->pages_skipped += get_dirty_pages(inode);
	return 0;
}

static void f2fs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);
		truncate_blocks(inode, inode->i_size, true);
	}
}

static int f2fs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct page *page, *ipage;
	pgoff_t index = ((unsigned long long) pos) >> PAGE_CACHE_SHIFT;
	struct dnode_of_data dn;
	int err = 0;

	trace_f2fs_write_begin(inode, pos, len, flags);

	f2fs_balance_fs(sbi);

	/*
	 * We should check this at this moment to avoid deadlock on inode page
	 * and #0 page. The locking rule for inline_data conversion should be:
	 * lock_page(page #0) -> lock_page(inode_page)
	 */
	if (index != 0) {
		err = f2fs_convert_inline_inode(inode);
		if (err)
			goto fail;
	}
repeat:
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page) {
		err = -ENOMEM;
		goto fail;
	}

	*pagep = page;

	f2fs_lock_op(sbi);

	/* check inline_data */
	ipage = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto unlock_fail;
	}

	set_new_dnode(&dn, inode, ipage, ipage, 0);

	if (f2fs_has_inline_data(inode)) {
		if (pos + len <= MAX_INLINE_DATA) {
			read_inline_data(page, ipage);
			set_inode_flag(F2FS_I(inode), FI_DATA_EXIST);
			sync_inode_page(&dn);
			goto put_next;
		}
		err = f2fs_convert_inline_page(&dn, page);
		if (err)
			goto put_fail;
	}
	err = f2fs_reserve_block(&dn, index);
	if (err)
		goto put_fail;
put_next:
	f2fs_put_dnode(&dn);
	f2fs_unlock_op(sbi);

	if ((len == PAGE_CACHE_SIZE) || PageUptodate(page))
		return 0;

	f2fs_wait_on_page_writeback(page, DATA);

	if ((pos & PAGE_CACHE_MASK) >= i_size_read(inode)) {
		unsigned start = pos & (PAGE_CACHE_SIZE - 1);
		unsigned end = start + len;

		/* Reading beyond i_size is simple: memset to zero */
		zero_user_segments(page, 0, start, end, PAGE_CACHE_SIZE);
		goto out;
	}

	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
	} else {
		struct f2fs_io_info fio = {
			.type = DATA,
			.rw = READ_SYNC,
			.blk_addr = dn.data_blkaddr,
		};
		err = f2fs_submit_page_bio(sbi, page, &fio);
		if (err)
			goto fail;

		lock_page(page);
		if (unlikely(!PageUptodate(page))) {
			f2fs_put_page(page, 1);
			err = -EIO;
			goto fail;
		}
		if (unlikely(page->mapping != mapping)) {
			f2fs_put_page(page, 1);
			goto repeat;
		}
	}
out:
	SetPageUptodate(page);
	clear_cold_data(page);
	return 0;

put_fail:
	f2fs_put_dnode(&dn);
unlock_fail:
	f2fs_unlock_op(sbi);
	f2fs_put_page(page, 1);
fail:
	f2fs_write_failed(mapping, pos + len);
	return err;
}

static int f2fs_write_end(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = page->mapping->host;

	trace_f2fs_write_end(inode, pos, len, copied);

	set_page_dirty(page);

	if (pos + copied > i_size_read(inode)) {
		i_size_write(inode, pos + copied);
		mark_inode_dirty(inode);
		update_inode_page(inode);
	}

	f2fs_put_page(page, 1);
	return copied;
}

static int check_direct_IO(struct inode *inode, struct iov_iter *iter,
			   loff_t offset)
{
	unsigned blocksize_mask = inode->i_sb->s_blocksize - 1;

	if (iov_iter_rw(iter) == READ)
		return 0;

	if (offset & blocksize_mask)
		return -EINVAL;

	if (iov_iter_alignment(iter) & blocksize_mask)
		return -EINVAL;

	return 0;
}

static ssize_t f2fs_direct_IO(struct kiocb *iocb, struct iov_iter *iter,
			      loff_t offset)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	size_t count = iov_iter_count(iter);
	int err;

	/* we don't need to use inline_data strictly */
	if (f2fs_has_inline_data(inode)) {
		err = f2fs_convert_inline_inode(inode);
		if (err)
			return err;
	}

	if (check_direct_IO(inode, iter, offset))
		return 0;

	trace_f2fs_direct_IO_enter(inode, offset, count, iov_iter_rw(iter));

	if (iov_iter_rw(iter) == WRITE)
		__allocate_data_blocks(inode, offset, count);

	err = blockdev_direct_IO(iocb, inode, iter, offset, get_data_block);
	if (err < 0 && iov_iter_rw(iter) == WRITE)
		f2fs_write_failed(mapping, offset + count);

	trace_f2fs_direct_IO_exit(inode, offset, count, iov_iter_rw(iter), err);

	return err;
}

void f2fs_invalidate_page(struct page *page, unsigned int offset,
							unsigned int length)
{
	struct inode *inode = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (inode->i_ino >= F2FS_ROOT_INO(sbi) &&
		(offset % PAGE_CACHE_SIZE || length != PAGE_CACHE_SIZE))
		return;

	if (PageDirty(page)) {
		if (inode->i_ino == F2FS_META_INO(sbi))
			dec_page_count(sbi, F2FS_DIRTY_META);
		else if (inode->i_ino == F2FS_NODE_INO(sbi))
			dec_page_count(sbi, F2FS_DIRTY_NODES);
		else
			inode_dec_dirty_pages(inode);
	}
	ClearPagePrivate(page);
}

int f2fs_release_page(struct page *page, gfp_t wait)
{
	/* If this is dirty page, keep PagePrivate */
	if (PageDirty(page))
		return 0;

	ClearPagePrivate(page);
	return 1;
}

static int f2fs_set_data_page_dirty(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;

	trace_f2fs_set_page_dirty(page, DATA);

	SetPageUptodate(page);

	if (f2fs_is_atomic_file(inode)) {
		register_inmem_page(inode, page);
		return 1;
	}

	mark_inode_dirty(inode);

	if (!PageDirty(page)) {
		__set_page_dirty_nobuffers(page);
		update_dirty_page(inode, page);
		return 1;
	}
	return 0;
}

static sector_t f2fs_bmap(struct address_space *mapping, sector_t block)
{
	struct inode *inode = mapping->host;

	/* we don't need to use inline_data strictly */
	if (f2fs_has_inline_data(inode)) {
		int err = f2fs_convert_inline_inode(inode);
		if (err)
			return err;
	}
	return generic_block_bmap(mapping, block, get_data_block);
}

void init_extent_cache_info(struct f2fs_sb_info *sbi)
{
	INIT_RADIX_TREE(&sbi->extent_tree_root, GFP_NOIO);
	init_rwsem(&sbi->extent_tree_lock);
	INIT_LIST_HEAD(&sbi->extent_list);
	spin_lock_init(&sbi->extent_lock);
	sbi->total_ext_tree = 0;
	atomic_set(&sbi->total_ext_node, 0);
}

int __init create_extent_cache(void)
{
	extent_tree_slab = f2fs_kmem_cache_create("f2fs_extent_tree",
			sizeof(struct extent_tree));
	if (!extent_tree_slab)
		return -ENOMEM;
	extent_node_slab = f2fs_kmem_cache_create("f2fs_extent_node",
			sizeof(struct extent_node));
	if (!extent_node_slab) {
		kmem_cache_destroy(extent_tree_slab);
		return -ENOMEM;
	}
	return 0;
}

void destroy_extent_cache(void)
{
	kmem_cache_destroy(extent_node_slab);
	kmem_cache_destroy(extent_tree_slab);
}

const struct address_space_operations f2fs_dblock_aops = {
	.readpage	= f2fs_read_data_page,
	.readpages	= f2fs_read_data_pages,
	.writepage	= f2fs_write_data_page,
	.writepages	= f2fs_write_data_pages,
	.write_begin	= f2fs_write_begin,
	.write_end	= f2fs_write_end,
	.set_page_dirty	= f2fs_set_data_page_dirty,
	.invalidatepage	= f2fs_invalidate_page,
	.releasepage	= f2fs_release_page,
	.direct_IO	= f2fs_direct_IO,
	.bmap		= f2fs_bmap,
};

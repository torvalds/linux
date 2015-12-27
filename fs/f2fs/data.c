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
#include <linux/pagevec.h>
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

static void f2fs_read_end_io(struct bio *bio)
{
	struct bio_vec *bvec;
	int i;

	if (f2fs_bio_encrypted(bio)) {
		if (bio->bi_error) {
			f2fs_release_crypto_ctx(bio->bi_private);
		} else {
			f2fs_end_io_crypto_work(bio->bi_private, bio);
			return;
		}
	}

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		if (!bio->bi_error) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	}
	bio_put(bio);
}

static void f2fs_write_end_io(struct bio *bio)
{
	struct f2fs_sb_info *sbi = bio->bi_private;
	struct bio_vec *bvec;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		f2fs_restore_and_release_control_page(&page);

		if (unlikely(bio->bi_error)) {
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

	bio = f2fs_bio_alloc(npages);

	bio->bi_bdev = sbi->sb->s_bdev;
	bio->bi_iter.bi_sector = SECTOR_FROM_BLOCK(blk_addr);
	bio->bi_end_io = is_read ? f2fs_read_end_io : f2fs_write_end_io;
	bio->bi_private = is_read ? NULL : sbi;

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
int f2fs_submit_page_bio(struct f2fs_io_info *fio)
{
	struct bio *bio;
	struct page *page = fio->encrypted_page ? fio->encrypted_page : fio->page;

	trace_f2fs_submit_page_bio(page, fio);
	f2fs_trace_ios(fio, 0);

	/* Allocate a new bio */
	bio = __bio_alloc(fio->sbi, fio->blk_addr, 1, is_read_io(fio->rw));

	if (bio_add_page(bio, page, PAGE_CACHE_SIZE, 0) < PAGE_CACHE_SIZE) {
		bio_put(bio);
		return -EFAULT;
	}

	submit_bio(fio->rw, bio);
	return 0;
}

void f2fs_submit_page_mbio(struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = fio->sbi;
	enum page_type btype = PAGE_TYPE_OF_BIO(fio->type);
	struct f2fs_bio_info *io;
	bool is_read = is_read_io(fio->rw);
	struct page *bio_page;

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

	bio_page = fio->encrypted_page ? fio->encrypted_page : fio->page;

	if (bio_add_page(io->bio, bio_page, PAGE_CACHE_SIZE, 0) <
							PAGE_CACHE_SIZE) {
		__submit_merged_bio(io);
		goto alloc_new;
	}

	io->last_block_in_bio = fio->blk_addr;
	f2fs_trace_ios(fio, 0);

	up_write(&io->io_rwsem);
	trace_f2fs_submit_page_mbio(fio->page, fio);
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

int f2fs_get_block(struct dnode_of_data *dn, pgoff_t index)
{
	struct extent_info ei;
	struct inode *inode = dn->inode;

	if (f2fs_lookup_extent_cache(inode, index, &ei)) {
		dn->data_blkaddr = ei.blk + index - ei.fofs;
		return 0;
	}

	return f2fs_reserve_block(dn, index);
}

struct page *get_read_data_page(struct inode *inode, pgoff_t index,
						int rw, bool for_write)
{
	struct address_space *mapping = inode->i_mapping;
	struct dnode_of_data dn;
	struct page *page;
	struct extent_info ei;
	int err;
	struct f2fs_io_info fio = {
		.sbi = F2FS_I_SB(inode),
		.type = DATA,
		.rw = rw,
		.encrypted_page = NULL,
	};

	if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode))
		return read_mapping_page(mapping, index, NULL);

	page = f2fs_grab_cache_page(mapping, index, for_write);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (f2fs_lookup_extent_cache(inode, index, &ei)) {
		dn.data_blkaddr = ei.blk + index - ei.fofs;
		goto got_it;
	}

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, index, LOOKUP_NODE);
	if (err)
		goto put_err;
	f2fs_put_dnode(&dn);

	if (unlikely(dn.data_blkaddr == NULL_ADDR)) {
		err = -ENOENT;
		goto put_err;
	}
got_it:
	if (PageUptodate(page)) {
		unlock_page(page);
		return page;
	}

	/*
	 * A new dentry page is allocated but not able to be written, since its
	 * new inode page couldn't be allocated due to -ENOSPC.
	 * In such the case, its blkaddr can be remained as NEW_ADDR.
	 * see, f2fs_add_link -> get_new_data_page -> init_inode_metadata.
	 */
	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
		SetPageUptodate(page);
		unlock_page(page);
		return page;
	}

	fio.blk_addr = dn.data_blkaddr;
	fio.page = page;
	err = f2fs_submit_page_bio(&fio);
	if (err)
		goto put_err;
	return page;

put_err:
	f2fs_put_page(page, 1);
	return ERR_PTR(err);
}

struct page *find_data_page(struct inode *inode, pgoff_t index)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;

	page = find_get_page(mapping, index);
	if (page && PageUptodate(page))
		return page;
	f2fs_put_page(page, 0);

	page = get_read_data_page(inode, index, READ_SYNC, false);
	if (IS_ERR(page))
		return page;

	if (PageUptodate(page))
		return page;

	wait_on_page_locked(page);
	if (unlikely(!PageUptodate(page))) {
		f2fs_put_page(page, 0);
		return ERR_PTR(-EIO);
	}
	return page;
}

/*
 * If it tries to access a hole, return an error.
 * Because, the callers, functions in dir.c and GC, should be able to know
 * whether this page exists or not.
 */
struct page *get_lock_data_page(struct inode *inode, pgoff_t index,
							bool for_write)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
repeat:
	page = get_read_data_page(inode, index, READ_SYNC, for_write);
	if (IS_ERR(page))
		return page;

	/* wait for read completion */
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
 * Note that, ipage is set only by make_empty_dir, and if any error occur,
 * ipage should be released by this function.
 */
struct page *get_new_data_page(struct inode *inode,
		struct page *ipage, pgoff_t index, bool new_i_size)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	struct dnode_of_data dn;
	int err;
repeat:
	page = f2fs_grab_cache_page(mapping, index, true);
	if (!page) {
		/*
		 * before exiting, we should make sure ipage will be released
		 * if any error occur.
		 */
		f2fs_put_page(ipage, 1);
		return ERR_PTR(-ENOMEM);
	}

	set_new_dnode(&dn, inode, ipage, NULL, 0);
	err = f2fs_reserve_block(&dn, index);
	if (err) {
		f2fs_put_page(page, 1);
		return ERR_PTR(err);
	}
	if (!ipage)
		f2fs_put_dnode(&dn);

	if (PageUptodate(page))
		goto got_it;

	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
		SetPageUptodate(page);
	} else {
		f2fs_put_page(page, 1);

		page = get_read_data_page(inode, index, READ_SYNC, true);
		if (IS_ERR(page))
			goto repeat;

		/* wait for read completion */
		lock_page(page);
	}
got_it:
	if (new_i_size && i_size_read(inode) <
				((loff_t)(index + 1) << PAGE_CACHE_SHIFT)) {
		i_size_write(inode, ((loff_t)(index + 1) << PAGE_CACHE_SHIFT));
		/* Only the directory inode sets new_i_size */
		set_inode_flag(F2FS_I(inode), FI_UPDATE_DIR);
	}
	return page;
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
	set_data_blkaddr(dn);

	/* update i_size */
	fofs = start_bidx_of_node(ofs_of_node(dn->node_page), fi) +
							dn->ofs_in_node;
	if (i_size_read(dn->inode) < ((loff_t)(fofs + 1) << PAGE_CACHE_SHIFT))
		i_size_write(dn->inode,
				((loff_t)(fofs + 1) << PAGE_CACHE_SHIFT));

	/* direct IO doesn't use extent cache to maximize the performance */
	f2fs_drop_largest_extent(dn->inode, fofs);

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

			if (unlikely(f2fs_cp_error(sbi)))
				goto sync_out;

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
						int create, int flag)
{
	unsigned int maxblocks = map->m_len;
	struct dnode_of_data dn;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
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

	if (dn.data_blkaddr == NEW_ADDR || dn.data_blkaddr == NULL_ADDR) {
		if (create) {
			if (unlikely(f2fs_cp_error(sbi))) {
				err = -EIO;
				goto put_out;
			}
			err = __allocate_data_block(&dn);
			if (err)
				goto put_out;
			allocated = true;
			map->m_flags = F2FS_MAP_NEW;
		} else {
			if (flag != F2FS_GET_BLOCK_FIEMAP ||
						dn.data_blkaddr != NEW_ADDR) {
				if (flag == F2FS_GET_BLOCK_BMAP)
					err = -ENOENT;
				goto put_out;
			}

			/*
			 * preallocated unwritten block should be mapped
			 * for fiemap.
			 */
			if (dn.data_blkaddr == NEW_ADDR)
				map->m_flags = F2FS_MAP_UNWRITTEN;
		}
	}

	map->m_flags |= F2FS_MAP_MAPPED;
	map->m_pblk = dn.data_blkaddr;
	map->m_len = 1;

	end_offset = ADDRS_PER_PAGE(dn.node_page, F2FS_I(inode));
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

		end_offset = ADDRS_PER_PAGE(dn.node_page, F2FS_I(inode));
	}

	if (maxblocks > map->m_len) {
		block_t blkaddr = datablock_addr(dn.node_page, dn.ofs_in_node);

		if (blkaddr == NEW_ADDR || blkaddr == NULL_ADDR) {
			if (create) {
				if (unlikely(f2fs_cp_error(sbi))) {
					err = -EIO;
					goto sync_out;
				}
				err = __allocate_data_block(&dn);
				if (err)
					goto sync_out;
				allocated = true;
				map->m_flags |= F2FS_MAP_NEW;
				blkaddr = dn.data_blkaddr;
			} else {
				/*
				 * we only merge preallocated unwritten blocks
				 * for fiemap.
				 */
				if (flag != F2FS_GET_BLOCK_FIEMAP ||
						blkaddr != NEW_ADDR)
					goto sync_out;
			}
		}

		/* Give more consecutive addresses for the readahead */
		if ((map->m_pblk != NEW_ADDR &&
				blkaddr == (map->m_pblk + ofs)) ||
				(map->m_pblk == NEW_ADDR &&
				blkaddr == NEW_ADDR)) {
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
			struct buffer_head *bh, int create, int flag)
{
	struct f2fs_map_blocks map;
	int ret;

	map.m_lblk = iblock;
	map.m_len = bh->b_size >> inode->i_blkbits;

	ret = f2fs_map_blocks(inode, &map, create, flag);
	if (!ret) {
		map_bh(bh, inode->i_sb, map.m_pblk);
		bh->b_state = (bh->b_state & ~F2FS_MAP_FLAGS) | map.m_flags;
		bh->b_size = map.m_len << inode->i_blkbits;
	}
	return ret;
}

static int get_data_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create, int flag)
{
	return __get_data_block(inode, iblock, bh_result, create, flag);
}

static int get_data_block_dio(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	return __get_data_block(inode, iblock, bh_result, create,
						F2FS_GET_BLOCK_DIO);
}

static int get_data_block_bmap(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	return __get_data_block(inode, iblock, bh_result, create,
						F2FS_GET_BLOCK_BMAP);
}

static inline sector_t logical_to_blk(struct inode *inode, loff_t offset)
{
	return (offset >> inode->i_blkbits);
}

static inline loff_t blk_to_logical(struct inode *inode, sector_t blk)
{
	return (blk << inode->i_blkbits);
}

int f2fs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	struct buffer_head map_bh;
	sector_t start_blk, last_blk;
	loff_t isize = i_size_read(inode);
	u64 logical = 0, phys = 0, size = 0;
	u32 flags = 0;
	bool past_eof = false, whole_file = false;
	int ret = 0;

	ret = fiemap_check_flags(fieinfo, FIEMAP_FLAG_SYNC);
	if (ret)
		return ret;

	if (f2fs_has_inline_data(inode)) {
		ret = f2fs_inline_data_fiemap(inode, fieinfo, start, len);
		if (ret != -EAGAIN)
			return ret;
	}

	mutex_lock(&inode->i_mutex);

	if (len >= isize) {
		whole_file = true;
		len = isize;
	}

	if (logical_to_blk(inode, len) == 0)
		len = blk_to_logical(inode, 1);

	start_blk = logical_to_blk(inode, start);
	last_blk = logical_to_blk(inode, start + len - 1);
next:
	memset(&map_bh, 0, sizeof(struct buffer_head));
	map_bh.b_size = len;

	ret = get_data_block(inode, start_blk, &map_bh, 0,
					F2FS_GET_BLOCK_FIEMAP);
	if (ret)
		goto out;

	/* HOLE */
	if (!buffer_mapped(&map_bh)) {
		start_blk++;

		if (!past_eof && blk_to_logical(inode, start_blk) >= isize)
			past_eof = 1;

		if (past_eof && size) {
			flags |= FIEMAP_EXTENT_LAST;
			ret = fiemap_fill_next_extent(fieinfo, logical,
					phys, size, flags);
		} else if (size) {
			ret = fiemap_fill_next_extent(fieinfo, logical,
					phys, size, flags);
			size = 0;
		}

		/* if we have holes up to/past EOF then we're done */
		if (start_blk > last_blk || past_eof || ret)
			goto out;
	} else {
		if (start_blk > last_blk && !whole_file) {
			ret = fiemap_fill_next_extent(fieinfo, logical,
					phys, size, flags);
			goto out;
		}

		/*
		 * if size != 0 then we know we already have an extent
		 * to add, so add it.
		 */
		if (size) {
			ret = fiemap_fill_next_extent(fieinfo, logical,
					phys, size, flags);
			if (ret)
				goto out;
		}

		logical = blk_to_logical(inode, start_blk);
		phys = blk_to_logical(inode, map_bh.b_blocknr);
		size = map_bh.b_size;
		flags = 0;
		if (buffer_unwritten(&map_bh))
			flags = FIEMAP_EXTENT_UNWRITTEN;

		start_blk += logical_to_blk(inode, size);

		/*
		 * If we are past the EOF, then we need to make sure as
		 * soon as we find a hole that the last extent we found
		 * is marked with FIEMAP_EXTENT_LAST
		 */
		if (!past_eof && logical + size >= isize)
			past_eof = true;
	}
	cond_resched();
	if (fatal_signal_pending(current))
		ret = -EINTR;
	else
		goto next;
out:
	if (ret == 1)
		ret = 0;

	mutex_unlock(&inode->i_mutex);
	return ret;
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

			if (f2fs_map_blocks(inode, &map, 0,
							F2FS_GET_BLOCK_READ))
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
			struct f2fs_crypto_ctx *ctx = NULL;

			if (f2fs_encrypted_inode(inode) &&
					S_ISREG(inode->i_mode)) {

				ctx = f2fs_get_crypto_ctx(inode);
				if (IS_ERR(ctx))
					goto set_error_page;

				/* wait the page to be moved by cleaning */
				f2fs_wait_on_encrypted_page_writeback(
						F2FS_I_SB(inode), block_nr);
			}

			bio = bio_alloc(GFP_KERNEL,
				min_t(int, nr_pages, BIO_MAX_PAGES));
			if (!bio) {
				if (ctx)
					f2fs_release_crypto_ctx(ctx);
				goto set_error_page;
			}
			bio->bi_bdev = bdev;
			bio->bi_iter.bi_sector = SECTOR_FROM_BLOCK(block_nr);
			bio->bi_end_io = f2fs_read_end_io;
			bio->bi_private = ctx;
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
	struct page *page = list_entry(pages->prev, struct page, lru);

	trace_f2fs_readpages(inode, page, nr_pages);

	/* If the file has inline data, skip readpages */
	if (f2fs_has_inline_data(inode))
		return 0;

	return f2fs_mpage_readpages(mapping, pages, NULL, nr_pages);
}

int do_write_data_page(struct f2fs_io_info *fio)
{
	struct page *page = fio->page;
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

	if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode)) {

		/* wait for GCed encrypted page writeback */
		f2fs_wait_on_encrypted_page_writeback(F2FS_I_SB(inode),
							fio->blk_addr);

		fio->encrypted_page = f2fs_encrypt(inode, fio->page);
		if (IS_ERR(fio->encrypted_page)) {
			err = PTR_ERR(fio->encrypted_page);
			goto out_writepage;
		}
	}

	set_page_writeback(page);

	/*
	 * If current allocation needs SSR,
	 * it had better in-place writes for updated data.
	 */
	if (unlikely(fio->blk_addr != NEW_ADDR &&
			!is_cold_data(page) &&
			need_inplace_update(inode))) {
		rewrite_data_page(fio);
		set_inode_flag(F2FS_I(inode), FI_UPDATE_WRITE);
		trace_f2fs_do_write_data_page(page, IPU);
	} else {
		write_data_page(&dn, fio);
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
		.sbi = sbi,
		.type = DATA,
		.rw = (wbc->sync_mode == WB_SYNC_ALL) ? WRITE_SYNC : WRITE,
		.page = page,
		.encrypted_page = NULL,
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
		err = do_write_data_page(&fio);
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
		err = do_write_data_page(&fio);
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

/*
 * This function was copied from write_cche_pages from mm/page-writeback.c.
 * The major change is making write step of cold data page separately from
 * warm/hot data page.
 */
static int f2fs_write_cache_pages(struct address_space *mapping,
			struct writeback_control *wbc, writepage_t writepage,
			void *data)
{
	int ret = 0;
	int done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t uninitialized_var(writeback_index);
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	pgoff_t done_index;
	int cycled;
	int range_whole = 0;
	int tag;
	int step = 0;

	pagevec_init(&pvec, 0);
next:
	if (wbc->range_cyclic) {
		writeback_index = mapping->writeback_index; /* prev offset */
		index = writeback_index;
		if (index == 0)
			cycled = 1;
		else
			cycled = 0;
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		cycled = 1; /* ignore range_cyclic tests */
	}
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);
	done_index = index;
	while (!done && (index <= end)) {
		int i;

		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index, tag,
			      min(end - index, (pgoff_t)PAGEVEC_SIZE - 1) + 1);
		if (nr_pages == 0)
			break;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			if (page->index > end) {
				done = 1;
				break;
			}

			done_index = page->index;

			lock_page(page);

			if (unlikely(page->mapping != mapping)) {
continue_unlock:
				unlock_page(page);
				continue;
			}

			if (!PageDirty(page)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			if (step == is_cold_data(page))
				goto continue_unlock;

			if (PageWriteback(page)) {
				if (wbc->sync_mode != WB_SYNC_NONE)
					f2fs_wait_on_page_writeback(page, DATA);
				else
					goto continue_unlock;
			}

			BUG_ON(PageWriteback(page));
			if (!clear_page_dirty_for_io(page))
				goto continue_unlock;

			ret = (*writepage)(page, wbc, data);
			if (unlikely(ret)) {
				if (ret == AOP_WRITEPAGE_ACTIVATE) {
					unlock_page(page);
					ret = 0;
				} else {
					done_index = page->index + 1;
					done = 1;
					break;
				}
			}

			if (--wbc->nr_to_write <= 0 &&
			    wbc->sync_mode == WB_SYNC_NONE) {
				done = 1;
				break;
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	}

	if (step < 1) {
		step++;
		goto next;
	}

	if (!cycled && !done) {
		cycled = 1;
		index = 0;
		end = writeback_index - 1;
		goto retry;
	}
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = done_index;

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

	/* skip writing if there is no dirty page in this inode */
	if (!get_dirty_pages(inode) && wbc->sync_mode == WB_SYNC_NONE)
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
	ret = f2fs_write_cache_pages(mapping, wbc, __f2fs_writepage, mapping);
	f2fs_submit_merged_bio(sbi, DATA, WRITE);
	if (locked)
		mutex_unlock(&sbi->writepages);

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
	struct page *page = NULL;
	struct page *ipage;
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

	err = f2fs_get_block(&dn, index);
	if (err)
		goto put_fail;
put_next:
	f2fs_put_dnode(&dn);
	f2fs_unlock_op(sbi);

	f2fs_wait_on_page_writeback(page, DATA);

	/* wait for GCed encrypted page writeback */
	if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode))
		f2fs_wait_on_encrypted_page_writeback(sbi, dn.data_blkaddr);

	if (len == PAGE_CACHE_SIZE)
		goto out_update;
	if (PageUptodate(page))
		goto out_clear;

	if ((pos & PAGE_CACHE_MASK) >= i_size_read(inode)) {
		unsigned start = pos & (PAGE_CACHE_SIZE - 1);
		unsigned end = start + len;

		/* Reading beyond i_size is simple: memset to zero */
		zero_user_segments(page, 0, start, end, PAGE_CACHE_SIZE);
		goto out_update;
	}

	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
	} else {
		struct f2fs_io_info fio = {
			.sbi = sbi,
			.type = DATA,
			.rw = READ_SYNC,
			.blk_addr = dn.data_blkaddr,
			.page = page,
			.encrypted_page = NULL,
		};
		err = f2fs_submit_page_bio(&fio);
		if (err)
			goto fail;

		lock_page(page);
		if (unlikely(!PageUptodate(page))) {
			err = -EIO;
			goto fail;
		}
		if (unlikely(page->mapping != mapping)) {
			f2fs_put_page(page, 1);
			goto repeat;
		}

		/* avoid symlink page */
		if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode)) {
			err = f2fs_decrypt_one(inode, page);
			if (err)
				goto fail;
		}
	}
out_update:
	SetPageUptodate(page);
out_clear:
	clear_cold_data(page);
	return 0;

put_fail:
	f2fs_put_dnode(&dn);
unlock_fail:
	f2fs_unlock_op(sbi);
fail:
	f2fs_put_page(page, 1);
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

	if (f2fs_encrypted_inode(inode) && S_ISREG(inode->i_mode))
		return 0;

	err = check_direct_IO(inode, iter, offset);
	if (err)
		return err;

	trace_f2fs_direct_IO_enter(inode, offset, count, iov_iter_rw(iter));

	if (iov_iter_rw(iter) == WRITE) {
		__allocate_data_blocks(inode, offset, count);
		if (unlikely(f2fs_cp_error(F2FS_I_SB(inode)))) {
			err = -EIO;
			goto out;
		}
	}

	err = blockdev_direct_IO(iocb, inode, iter, offset, get_data_block_dio);
out:
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

	/* This is atomic written page, keep Private */
	if (IS_ATOMIC_WRITTEN_PAGE(page))
		return;

	ClearPagePrivate(page);
}

int f2fs_release_page(struct page *page, gfp_t wait)
{
	/* If this is dirty page, keep PagePrivate */
	if (PageDirty(page))
		return 0;

	/* This is atomic written page, keep Private */
	if (IS_ATOMIC_WRITTEN_PAGE(page))
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
		if (!IS_ATOMIC_WRITTEN_PAGE(page)) {
			register_inmem_page(inode, page);
			return 1;
		}
		/*
		 * Previously, this page has been registered, we just
		 * return here.
		 */
		return 0;
	}

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

	if (f2fs_has_inline_data(inode))
		return 0;

	/* make sure allocating whole blocks */
	if (mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		filemap_write_and_wait(mapping);

	return generic_block_bmap(mapping, block, get_data_block_bmap);
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

/*
 * fs/f2fs/checkpoint.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/f2fs_fs.h>
#include <linux/pagevec.h>
#include <linux/swap.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include <trace/events/f2fs.h>

static struct kmem_cache *orphan_entry_slab;
static struct kmem_cache *inode_entry_slab;

/*
 * We guarantee no failure on the returned page.
 */
struct page *grab_meta_page(struct f2fs_sb_info *sbi, pgoff_t index)
{
	struct address_space *mapping = sbi->meta_inode->i_mapping;
	struct page *page = NULL;
repeat:
	page = grab_cache_page(mapping, index);
	if (!page) {
		cond_resched();
		goto repeat;
	}

	/* We wait writeback only inside grab_meta_page() */
	wait_on_page_writeback(page);
	SetPageUptodate(page);
	return page;
}

/*
 * We guarantee no failure on the returned page.
 */
struct page *get_meta_page(struct f2fs_sb_info *sbi, pgoff_t index)
{
	struct address_space *mapping = sbi->meta_inode->i_mapping;
	struct page *page;
repeat:
	page = grab_cache_page(mapping, index);
	if (!page) {
		cond_resched();
		goto repeat;
	}
	if (PageUptodate(page))
		goto out;

	if (f2fs_readpage(sbi, page, index, READ_SYNC))
		goto repeat;

	lock_page(page);
	if (page->mapping != mapping) {
		f2fs_put_page(page, 1);
		goto repeat;
	}
out:
	mark_page_accessed(page);
	return page;
}

static int f2fs_write_meta_page(struct page *page,
				struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);

	/* Should not write any meta pages, if any IO error was occurred */
	if (wbc->for_reclaim ||
			is_set_ckpt_flags(F2FS_CKPT(sbi), CP_ERROR_FLAG)) {
		dec_page_count(sbi, F2FS_DIRTY_META);
		wbc->pages_skipped++;
		set_page_dirty(page);
		return AOP_WRITEPAGE_ACTIVATE;
	}

	wait_on_page_writeback(page);

	write_meta_page(sbi, page);
	dec_page_count(sbi, F2FS_DIRTY_META);
	unlock_page(page);
	return 0;
}

static int f2fs_write_meta_pages(struct address_space *mapping,
				struct writeback_control *wbc)
{
	struct f2fs_sb_info *sbi = F2FS_SB(mapping->host->i_sb);
	struct block_device *bdev = sbi->sb->s_bdev;
	long written;

	if (wbc->for_kupdate)
		return 0;

	if (get_pages(sbi, F2FS_DIRTY_META) == 0)
		return 0;

	/* if mounting is failed, skip writing node pages */
	mutex_lock(&sbi->cp_mutex);
	written = sync_meta_pages(sbi, META, bio_get_nr_vecs(bdev));
	mutex_unlock(&sbi->cp_mutex);
	wbc->nr_to_write -= written;
	return 0;
}

long sync_meta_pages(struct f2fs_sb_info *sbi, enum page_type type,
						long nr_to_write)
{
	struct address_space *mapping = sbi->meta_inode->i_mapping;
	pgoff_t index = 0, end = LONG_MAX;
	struct pagevec pvec;
	long nwritten = 0;
	struct writeback_control wbc = {
		.for_reclaim = 0,
	};

	pagevec_init(&pvec, 0);

	while (index <= end) {
		int i, nr_pages;
		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
				PAGECACHE_TAG_DIRTY,
				min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1);
		if (nr_pages == 0)
			break;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];
			lock_page(page);
			BUG_ON(page->mapping != mapping);
			BUG_ON(!PageDirty(page));
			clear_page_dirty_for_io(page);
			if (f2fs_write_meta_page(page, &wbc)) {
				unlock_page(page);
				break;
			}
			if (nwritten++ >= nr_to_write)
				break;
		}
		pagevec_release(&pvec);
		cond_resched();
	}

	if (nwritten)
		f2fs_submit_bio(sbi, type, nr_to_write == LONG_MAX);

	return nwritten;
}

static int f2fs_set_meta_page_dirty(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct f2fs_sb_info *sbi = F2FS_SB(mapping->host->i_sb);

	SetPageUptodate(page);
	if (!PageDirty(page)) {
		__set_page_dirty_nobuffers(page);
		inc_page_count(sbi, F2FS_DIRTY_META);
		return 1;
	}
	return 0;
}

const struct address_space_operations f2fs_meta_aops = {
	.writepage	= f2fs_write_meta_page,
	.writepages	= f2fs_write_meta_pages,
	.set_page_dirty	= f2fs_set_meta_page_dirty,
};

int check_orphan_space(struct f2fs_sb_info *sbi)
{
	unsigned int max_orphans;
	int err = 0;

	/*
	 * considering 512 blocks in a segment 5 blocks are needed for cp
	 * and log segment summaries. Remaining blocks are used to keep
	 * orphan entries with the limitation one reserved segment
	 * for cp pack we can have max 1020*507 orphan entries
	 */
	max_orphans = (sbi->blocks_per_seg - 5) * F2FS_ORPHANS_PER_BLOCK;
	mutex_lock(&sbi->orphan_inode_mutex);
	if (sbi->n_orphans >= max_orphans)
		err = -ENOSPC;
	mutex_unlock(&sbi->orphan_inode_mutex);
	return err;
}

void add_orphan_inode(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct list_head *head, *this;
	struct orphan_inode_entry *new = NULL, *orphan = NULL;

	mutex_lock(&sbi->orphan_inode_mutex);
	head = &sbi->orphan_inode_list;
	list_for_each(this, head) {
		orphan = list_entry(this, struct orphan_inode_entry, list);
		if (orphan->ino == ino)
			goto out;
		if (orphan->ino > ino)
			break;
		orphan = NULL;
	}
retry:
	new = kmem_cache_alloc(orphan_entry_slab, GFP_ATOMIC);
	if (!new) {
		cond_resched();
		goto retry;
	}
	new->ino = ino;

	/* add new_oentry into list which is sorted by inode number */
	if (orphan)
		list_add(&new->list, this->prev);
	else
		list_add_tail(&new->list, head);

	sbi->n_orphans++;
out:
	mutex_unlock(&sbi->orphan_inode_mutex);
}

void remove_orphan_inode(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct list_head *this, *next, *head;
	struct orphan_inode_entry *orphan;

	mutex_lock(&sbi->orphan_inode_mutex);
	head = &sbi->orphan_inode_list;
	list_for_each_safe(this, next, head) {
		orphan = list_entry(this, struct orphan_inode_entry, list);
		if (orphan->ino == ino) {
			list_del(&orphan->list);
			kmem_cache_free(orphan_entry_slab, orphan);
			sbi->n_orphans--;
			break;
		}
	}
	mutex_unlock(&sbi->orphan_inode_mutex);
}

static void recover_orphan_inode(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct inode *inode = f2fs_iget(sbi->sb, ino);
	BUG_ON(IS_ERR(inode));
	clear_nlink(inode);

	/* truncate all the data during iput */
	iput(inode);
}

int recover_orphan_inodes(struct f2fs_sb_info *sbi)
{
	block_t start_blk, orphan_blkaddr, i, j;

	if (!is_set_ckpt_flags(F2FS_CKPT(sbi), CP_ORPHAN_PRESENT_FLAG))
		return 0;

	sbi->por_doing = 1;
	start_blk = __start_cp_addr(sbi) + 1;
	orphan_blkaddr = __start_sum_addr(sbi) - 1;

	for (i = 0; i < orphan_blkaddr; i++) {
		struct page *page = get_meta_page(sbi, start_blk + i);
		struct f2fs_orphan_block *orphan_blk;

		orphan_blk = (struct f2fs_orphan_block *)page_address(page);
		for (j = 0; j < le32_to_cpu(orphan_blk->entry_count); j++) {
			nid_t ino = le32_to_cpu(orphan_blk->ino[j]);
			recover_orphan_inode(sbi, ino);
		}
		f2fs_put_page(page, 1);
	}
	/* clear Orphan Flag */
	clear_ckpt_flags(F2FS_CKPT(sbi), CP_ORPHAN_PRESENT_FLAG);
	sbi->por_doing = 0;
	return 0;
}

static void write_orphan_inodes(struct f2fs_sb_info *sbi, block_t start_blk)
{
	struct list_head *head, *this, *next;
	struct f2fs_orphan_block *orphan_blk = NULL;
	struct page *page = NULL;
	unsigned int nentries = 0;
	unsigned short index = 1;
	unsigned short orphan_blocks;

	orphan_blocks = (unsigned short)((sbi->n_orphans +
		(F2FS_ORPHANS_PER_BLOCK - 1)) / F2FS_ORPHANS_PER_BLOCK);

	mutex_lock(&sbi->orphan_inode_mutex);
	head = &sbi->orphan_inode_list;

	/* loop for each orphan inode entry and write them in Jornal block */
	list_for_each_safe(this, next, head) {
		struct orphan_inode_entry *orphan;

		orphan = list_entry(this, struct orphan_inode_entry, list);

		if (nentries == F2FS_ORPHANS_PER_BLOCK) {
			/*
			 * an orphan block is full of 1020 entries,
			 * then we need to flush current orphan blocks
			 * and bring another one in memory
			 */
			orphan_blk->blk_addr = cpu_to_le16(index);
			orphan_blk->blk_count = cpu_to_le16(orphan_blocks);
			orphan_blk->entry_count = cpu_to_le32(nentries);
			set_page_dirty(page);
			f2fs_put_page(page, 1);
			index++;
			start_blk++;
			nentries = 0;
			page = NULL;
		}
		if (page)
			goto page_exist;

		page = grab_meta_page(sbi, start_blk);
		orphan_blk = (struct f2fs_orphan_block *)page_address(page);
		memset(orphan_blk, 0, sizeof(*orphan_blk));
page_exist:
		orphan_blk->ino[nentries++] = cpu_to_le32(orphan->ino);
	}
	if (!page)
		goto end;

	orphan_blk->blk_addr = cpu_to_le16(index);
	orphan_blk->blk_count = cpu_to_le16(orphan_blocks);
	orphan_blk->entry_count = cpu_to_le32(nentries);
	set_page_dirty(page);
	f2fs_put_page(page, 1);
end:
	mutex_unlock(&sbi->orphan_inode_mutex);
}

static struct page *validate_checkpoint(struct f2fs_sb_info *sbi,
				block_t cp_addr, unsigned long long *version)
{
	struct page *cp_page_1, *cp_page_2 = NULL;
	unsigned long blk_size = sbi->blocksize;
	struct f2fs_checkpoint *cp_block;
	unsigned long long cur_version = 0, pre_version = 0;
	unsigned int crc = 0;
	size_t crc_offset;

	/* Read the 1st cp block in this CP pack */
	cp_page_1 = get_meta_page(sbi, cp_addr);

	/* get the version number */
	cp_block = (struct f2fs_checkpoint *)page_address(cp_page_1);
	crc_offset = le32_to_cpu(cp_block->checksum_offset);
	if (crc_offset >= blk_size)
		goto invalid_cp1;

	crc = *(unsigned int *)((unsigned char *)cp_block + crc_offset);
	if (!f2fs_crc_valid(crc, cp_block, crc_offset))
		goto invalid_cp1;

	pre_version = le64_to_cpu(cp_block->checkpoint_ver);

	/* Read the 2nd cp block in this CP pack */
	cp_addr += le32_to_cpu(cp_block->cp_pack_total_block_count) - 1;
	cp_page_2 = get_meta_page(sbi, cp_addr);

	cp_block = (struct f2fs_checkpoint *)page_address(cp_page_2);
	crc_offset = le32_to_cpu(cp_block->checksum_offset);
	if (crc_offset >= blk_size)
		goto invalid_cp2;

	crc = *(unsigned int *)((unsigned char *)cp_block + crc_offset);
	if (!f2fs_crc_valid(crc, cp_block, crc_offset))
		goto invalid_cp2;

	cur_version = le64_to_cpu(cp_block->checkpoint_ver);

	if (cur_version == pre_version) {
		*version = cur_version;
		f2fs_put_page(cp_page_2, 1);
		return cp_page_1;
	}
invalid_cp2:
	f2fs_put_page(cp_page_2, 1);
invalid_cp1:
	f2fs_put_page(cp_page_1, 1);
	return NULL;
}

int get_valid_checkpoint(struct f2fs_sb_info *sbi)
{
	struct f2fs_checkpoint *cp_block;
	struct f2fs_super_block *fsb = sbi->raw_super;
	struct page *cp1, *cp2, *cur_page;
	unsigned long blk_size = sbi->blocksize;
	unsigned long long cp1_version = 0, cp2_version = 0;
	unsigned long long cp_start_blk_no;

	sbi->ckpt = kzalloc(blk_size, GFP_KERNEL);
	if (!sbi->ckpt)
		return -ENOMEM;
	/*
	 * Finding out valid cp block involves read both
	 * sets( cp pack1 and cp pack 2)
	 */
	cp_start_blk_no = le32_to_cpu(fsb->cp_blkaddr);
	cp1 = validate_checkpoint(sbi, cp_start_blk_no, &cp1_version);

	/* The second checkpoint pack should start at the next segment */
	cp_start_blk_no += 1 << le32_to_cpu(fsb->log_blocks_per_seg);
	cp2 = validate_checkpoint(sbi, cp_start_blk_no, &cp2_version);

	if (cp1 && cp2) {
		if (ver_after(cp2_version, cp1_version))
			cur_page = cp2;
		else
			cur_page = cp1;
	} else if (cp1) {
		cur_page = cp1;
	} else if (cp2) {
		cur_page = cp2;
	} else {
		goto fail_no_cp;
	}

	cp_block = (struct f2fs_checkpoint *)page_address(cur_page);
	memcpy(sbi->ckpt, cp_block, blk_size);

	f2fs_put_page(cp1, 1);
	f2fs_put_page(cp2, 1);
	return 0;

fail_no_cp:
	kfree(sbi->ckpt);
	return -EINVAL;
}

static int __add_dirty_inode(struct inode *inode, struct dir_inode_entry *new)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct list_head *head = &sbi->dir_inode_list;
	struct list_head *this;

	list_for_each(this, head) {
		struct dir_inode_entry *entry;
		entry = list_entry(this, struct dir_inode_entry, list);
		if (entry->inode == inode)
			return -EEXIST;
	}
	list_add_tail(&new->list, head);
#ifdef CONFIG_F2FS_STAT_FS
	sbi->n_dirty_dirs++;
#endif
	return 0;
}

void set_dirty_dir_page(struct inode *inode, struct page *page)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct dir_inode_entry *new;

	if (!S_ISDIR(inode->i_mode))
		return;
retry:
	new = kmem_cache_alloc(inode_entry_slab, GFP_NOFS);
	if (!new) {
		cond_resched();
		goto retry;
	}
	new->inode = inode;
	INIT_LIST_HEAD(&new->list);

	spin_lock(&sbi->dir_inode_lock);
	if (__add_dirty_inode(inode, new))
		kmem_cache_free(inode_entry_slab, new);

	inc_page_count(sbi, F2FS_DIRTY_DENTS);
	inode_inc_dirty_dents(inode);
	SetPagePrivate(page);
	spin_unlock(&sbi->dir_inode_lock);
}

void add_dirty_dir_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct dir_inode_entry *new;
retry:
	new = kmem_cache_alloc(inode_entry_slab, GFP_NOFS);
	if (!new) {
		cond_resched();
		goto retry;
	}
	new->inode = inode;
	INIT_LIST_HEAD(&new->list);

	spin_lock(&sbi->dir_inode_lock);
	if (__add_dirty_inode(inode, new))
		kmem_cache_free(inode_entry_slab, new);
	spin_unlock(&sbi->dir_inode_lock);
}

void remove_dirty_dir_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct list_head *head = &sbi->dir_inode_list;
	struct list_head *this;

	if (!S_ISDIR(inode->i_mode))
		return;

	spin_lock(&sbi->dir_inode_lock);
	if (atomic_read(&F2FS_I(inode)->dirty_dents)) {
		spin_unlock(&sbi->dir_inode_lock);
		return;
	}

	list_for_each(this, head) {
		struct dir_inode_entry *entry;
		entry = list_entry(this, struct dir_inode_entry, list);
		if (entry->inode == inode) {
			list_del(&entry->list);
			kmem_cache_free(inode_entry_slab, entry);
#ifdef CONFIG_F2FS_STAT_FS
			sbi->n_dirty_dirs--;
#endif
			break;
		}
	}
	spin_unlock(&sbi->dir_inode_lock);

	/* Only from the recovery routine */
	if (is_inode_flag_set(F2FS_I(inode), FI_DELAY_IPUT)) {
		clear_inode_flag(F2FS_I(inode), FI_DELAY_IPUT);
		iput(inode);
	}
}

struct inode *check_dirty_dir_inode(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct list_head *head = &sbi->dir_inode_list;
	struct list_head *this;
	struct inode *inode = NULL;

	spin_lock(&sbi->dir_inode_lock);
	list_for_each(this, head) {
		struct dir_inode_entry *entry;
		entry = list_entry(this, struct dir_inode_entry, list);
		if (entry->inode->i_ino == ino) {
			inode = entry->inode;
			break;
		}
	}
	spin_unlock(&sbi->dir_inode_lock);
	return inode;
}

void sync_dirty_dir_inodes(struct f2fs_sb_info *sbi)
{
	struct list_head *head = &sbi->dir_inode_list;
	struct dir_inode_entry *entry;
	struct inode *inode;
retry:
	spin_lock(&sbi->dir_inode_lock);
	if (list_empty(head)) {
		spin_unlock(&sbi->dir_inode_lock);
		return;
	}
	entry = list_entry(head->next, struct dir_inode_entry, list);
	inode = igrab(entry->inode);
	spin_unlock(&sbi->dir_inode_lock);
	if (inode) {
		filemap_flush(inode->i_mapping);
		iput(inode);
	} else {
		/*
		 * We should submit bio, since it exists several
		 * wribacking dentry pages in the freeing inode.
		 */
		f2fs_submit_bio(sbi, DATA, true);
	}
	goto retry;
}

/*
 * Freeze all the FS-operations for checkpoint.
 */
static void block_operations(struct f2fs_sb_info *sbi)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = LONG_MAX,
		.for_reclaim = 0,
	};
	struct blk_plug plug;

	blk_start_plug(&plug);

retry_flush_dents:
	mutex_lock_all(sbi);

	/* write all the dirty dentry pages */
	if (get_pages(sbi, F2FS_DIRTY_DENTS)) {
		mutex_unlock_all(sbi);
		sync_dirty_dir_inodes(sbi);
		goto retry_flush_dents;
	}

	/*
	 * POR: we should ensure that there is no dirty node pages
	 * until finishing nat/sit flush.
	 */
retry_flush_nodes:
	mutex_lock(&sbi->node_write);

	if (get_pages(sbi, F2FS_DIRTY_NODES)) {
		mutex_unlock(&sbi->node_write);
		sync_node_pages(sbi, 0, &wbc);
		goto retry_flush_nodes;
	}
	blk_finish_plug(&plug);
}

static void unblock_operations(struct f2fs_sb_info *sbi)
{
	mutex_unlock(&sbi->node_write);
	mutex_unlock_all(sbi);
}

static void do_checkpoint(struct f2fs_sb_info *sbi, bool is_umount)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	nid_t last_nid = 0;
	block_t start_blk;
	struct page *cp_page;
	unsigned int data_sum_blocks, orphan_blocks;
	unsigned int crc32 = 0;
	void *kaddr;
	int i;

	/* Flush all the NAT/SIT pages */
	while (get_pages(sbi, F2FS_DIRTY_META))
		sync_meta_pages(sbi, META, LONG_MAX);

	next_free_nid(sbi, &last_nid);

	/*
	 * modify checkpoint
	 * version number is already updated
	 */
	ckpt->elapsed_time = cpu_to_le64(get_mtime(sbi));
	ckpt->valid_block_count = cpu_to_le64(valid_user_blocks(sbi));
	ckpt->free_segment_count = cpu_to_le32(free_segments(sbi));
	for (i = 0; i < 3; i++) {
		ckpt->cur_node_segno[i] =
			cpu_to_le32(curseg_segno(sbi, i + CURSEG_HOT_NODE));
		ckpt->cur_node_blkoff[i] =
			cpu_to_le16(curseg_blkoff(sbi, i + CURSEG_HOT_NODE));
		ckpt->alloc_type[i + CURSEG_HOT_NODE] =
				curseg_alloc_type(sbi, i + CURSEG_HOT_NODE);
	}
	for (i = 0; i < 3; i++) {
		ckpt->cur_data_segno[i] =
			cpu_to_le32(curseg_segno(sbi, i + CURSEG_HOT_DATA));
		ckpt->cur_data_blkoff[i] =
			cpu_to_le16(curseg_blkoff(sbi, i + CURSEG_HOT_DATA));
		ckpt->alloc_type[i + CURSEG_HOT_DATA] =
				curseg_alloc_type(sbi, i + CURSEG_HOT_DATA);
	}

	ckpt->valid_node_count = cpu_to_le32(valid_node_count(sbi));
	ckpt->valid_inode_count = cpu_to_le32(valid_inode_count(sbi));
	ckpt->next_free_nid = cpu_to_le32(last_nid);

	/* 2 cp  + n data seg summary + orphan inode blocks */
	data_sum_blocks = npages_for_summary_flush(sbi);
	if (data_sum_blocks < 3)
		set_ckpt_flags(ckpt, CP_COMPACT_SUM_FLAG);
	else
		clear_ckpt_flags(ckpt, CP_COMPACT_SUM_FLAG);

	orphan_blocks = (sbi->n_orphans + F2FS_ORPHANS_PER_BLOCK - 1)
					/ F2FS_ORPHANS_PER_BLOCK;
	ckpt->cp_pack_start_sum = cpu_to_le32(1 + orphan_blocks);

	if (is_umount) {
		set_ckpt_flags(ckpt, CP_UMOUNT_FLAG);
		ckpt->cp_pack_total_block_count = cpu_to_le32(2 +
			data_sum_blocks + orphan_blocks + NR_CURSEG_NODE_TYPE);
	} else {
		clear_ckpt_flags(ckpt, CP_UMOUNT_FLAG);
		ckpt->cp_pack_total_block_count = cpu_to_le32(2 +
			data_sum_blocks + orphan_blocks);
	}

	if (sbi->n_orphans)
		set_ckpt_flags(ckpt, CP_ORPHAN_PRESENT_FLAG);
	else
		clear_ckpt_flags(ckpt, CP_ORPHAN_PRESENT_FLAG);

	/* update SIT/NAT bitmap */
	get_sit_bitmap(sbi, __bitmap_ptr(sbi, SIT_BITMAP));
	get_nat_bitmap(sbi, __bitmap_ptr(sbi, NAT_BITMAP));

	crc32 = f2fs_crc32(ckpt, le32_to_cpu(ckpt->checksum_offset));
	*(__le32 *)((unsigned char *)ckpt +
				le32_to_cpu(ckpt->checksum_offset))
				= cpu_to_le32(crc32);

	start_blk = __start_cp_addr(sbi);

	/* write out checkpoint buffer at block 0 */
	cp_page = grab_meta_page(sbi, start_blk++);
	kaddr = page_address(cp_page);
	memcpy(kaddr, ckpt, (1 << sbi->log_blocksize));
	set_page_dirty(cp_page);
	f2fs_put_page(cp_page, 1);

	if (sbi->n_orphans) {
		write_orphan_inodes(sbi, start_blk);
		start_blk += orphan_blocks;
	}

	write_data_summaries(sbi, start_blk);
	start_blk += data_sum_blocks;
	if (is_umount) {
		write_node_summaries(sbi, start_blk);
		start_blk += NR_CURSEG_NODE_TYPE;
	}

	/* writeout checkpoint block */
	cp_page = grab_meta_page(sbi, start_blk);
	kaddr = page_address(cp_page);
	memcpy(kaddr, ckpt, (1 << sbi->log_blocksize));
	set_page_dirty(cp_page);
	f2fs_put_page(cp_page, 1);

	/* wait for previous submitted node/meta pages writeback */
	while (get_pages(sbi, F2FS_WRITEBACK))
		congestion_wait(BLK_RW_ASYNC, HZ / 50);

	filemap_fdatawait_range(sbi->node_inode->i_mapping, 0, LONG_MAX);
	filemap_fdatawait_range(sbi->meta_inode->i_mapping, 0, LONG_MAX);

	/* update user_block_counts */
	sbi->last_valid_block_count = sbi->total_valid_block_count;
	sbi->alloc_valid_block_count = 0;

	/* Here, we only have one bio having CP pack */
	sync_meta_pages(sbi, META_FLUSH, LONG_MAX);

	if (!is_set_ckpt_flags(ckpt, CP_ERROR_FLAG)) {
		clear_prefree_segments(sbi);
		F2FS_RESET_SB_DIRT(sbi);
	}
}

/*
 * We guarantee that this checkpoint procedure should not fail.
 */
void write_checkpoint(struct f2fs_sb_info *sbi, bool is_umount)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	unsigned long long ckpt_ver;

	trace_f2fs_write_checkpoint(sbi->sb, is_umount, "start block_ops");

	mutex_lock(&sbi->cp_mutex);
	block_operations(sbi);

	trace_f2fs_write_checkpoint(sbi->sb, is_umount, "finish block_ops");

	f2fs_submit_bio(sbi, DATA, true);
	f2fs_submit_bio(sbi, NODE, true);
	f2fs_submit_bio(sbi, META, true);

	/*
	 * update checkpoint pack index
	 * Increase the version number so that
	 * SIT entries and seg summaries are written at correct place
	 */
	ckpt_ver = le64_to_cpu(ckpt->checkpoint_ver);
	ckpt->checkpoint_ver = cpu_to_le64(++ckpt_ver);

	/* write cached NAT/SIT entries to NAT/SIT area */
	flush_nat_entries(sbi);
	flush_sit_entries(sbi);

	/* unlock all the fs_lock[] in do_checkpoint() */
	do_checkpoint(sbi, is_umount);

	unblock_operations(sbi);
	mutex_unlock(&sbi->cp_mutex);

	trace_f2fs_write_checkpoint(sbi->sb, is_umount, "finish checkpoint");
}

void init_orphan_info(struct f2fs_sb_info *sbi)
{
	mutex_init(&sbi->orphan_inode_mutex);
	INIT_LIST_HEAD(&sbi->orphan_inode_list);
	sbi->n_orphans = 0;
}

int __init create_checkpoint_caches(void)
{
	orphan_entry_slab = f2fs_kmem_cache_create("f2fs_orphan_entry",
			sizeof(struct orphan_inode_entry), NULL);
	if (unlikely(!orphan_entry_slab))
		return -ENOMEM;
	inode_entry_slab = f2fs_kmem_cache_create("f2fs_dirty_dir_entry",
			sizeof(struct dir_inode_entry), NULL);
	if (unlikely(!inode_entry_slab)) {
		kmem_cache_destroy(orphan_entry_slab);
		return -ENOMEM;
	}
	return 0;
}

void destroy_checkpoint_caches(void)
{
	kmem_cache_destroy(orphan_entry_slab);
	kmem_cache_destroy(inode_entry_slab);
}

/*
 * fs/f2fs/recovery.c
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
#include "f2fs.h"
#include "node.h"
#include "segment.h"

static struct kmem_cache *fsync_entry_slab;

bool space_for_roll_forward(struct f2fs_sb_info *sbi)
{
	if (sbi->last_valid_block_count + sbi->alloc_valid_block_count
			> sbi->user_block_count)
		return false;
	return true;
}

static struct fsync_inode_entry *get_fsync_inode(struct list_head *head,
								nid_t ino)
{
	struct list_head *this;
	struct fsync_inode_entry *entry;

	list_for_each(this, head) {
		entry = list_entry(this, struct fsync_inode_entry, list);
		if (entry->inode->i_ino == ino)
			return entry;
	}
	return NULL;
}

static int recover_dentry(struct page *ipage, struct inode *inode)
{
	struct f2fs_node *raw_node = (struct f2fs_node *)kmap(ipage);
	struct f2fs_inode *raw_inode = &(raw_node->i);
	nid_t pino = le32_to_cpu(raw_inode->i_pino);
	struct qstr name;
	struct f2fs_dir_entry *de;
	struct page *page;
	struct inode *dir;
	int err = 0;

	dir = check_dirty_dir_inode(F2FS_SB(inode->i_sb), pino);
	if (!dir) {
		dir = f2fs_iget(inode->i_sb, pino);
		if (IS_ERR(dir)) {
			err = PTR_ERR(dir);
			goto out;
		}
		set_inode_flag(F2FS_I(dir), FI_DELAY_IPUT);
	}

	name.len = le32_to_cpu(raw_inode->i_namelen);
	name.name = raw_inode->i_name;

	de = f2fs_find_entry(dir, &name, &page);
	if (de) {
		kunmap(page);
		f2fs_put_page(page, 0);
	} else {
		err = __f2fs_add_link(dir, &name, inode);
	}
out:
	f2fs_msg(inode->i_sb, KERN_NOTICE, "recover_inode and its dentry: "
			"ino = %x, name = %s, dir = %lx, err = %d",
			ino_of_node(ipage), raw_inode->i_name, dir->i_ino, err);
	kunmap(ipage);
	return err;
}

static int recover_inode(struct inode *inode, struct page *node_page)
{
	void *kaddr = page_address(node_page);
	struct f2fs_node *raw_node = (struct f2fs_node *)kaddr;
	struct f2fs_inode *raw_inode = &(raw_node->i);

	if (!IS_INODE(node_page))
		return 0;

	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	i_size_write(inode, le64_to_cpu(raw_inode->i_size));
	inode->i_atime.tv_sec = le64_to_cpu(raw_inode->i_mtime);
	inode->i_ctime.tv_sec = le64_to_cpu(raw_inode->i_ctime);
	inode->i_mtime.tv_sec = le64_to_cpu(raw_inode->i_mtime);
	inode->i_atime.tv_nsec = le32_to_cpu(raw_inode->i_mtime_nsec);
	inode->i_ctime.tv_nsec = le32_to_cpu(raw_inode->i_ctime_nsec);
	inode->i_mtime.tv_nsec = le32_to_cpu(raw_inode->i_mtime_nsec);

	if (is_dent_dnode(node_page))
		return recover_dentry(node_page, inode);

	f2fs_msg(inode->i_sb, KERN_NOTICE, "recover_inode: ino = %x, name = %s",
			ino_of_node(node_page), raw_inode->i_name);
	return 0;
}

static int find_fsync_dnodes(struct f2fs_sb_info *sbi, struct list_head *head)
{
	unsigned long long cp_ver = le64_to_cpu(sbi->ckpt->checkpoint_ver);
	struct curseg_info *curseg;
	struct page *page;
	block_t blkaddr;
	int err = 0;

	/* get node pages in the current segment */
	curseg = CURSEG_I(sbi, CURSEG_WARM_NODE);
	blkaddr = START_BLOCK(sbi, curseg->segno) + curseg->next_blkoff;

	/* read node page */
	page = alloc_page(GFP_F2FS_ZERO);
	if (IS_ERR(page))
		return PTR_ERR(page);
	lock_page(page);

	while (1) {
		struct fsync_inode_entry *entry;

		err = f2fs_readpage(sbi, page, blkaddr, READ_SYNC);
		if (err)
			goto out;

		lock_page(page);

		if (cp_ver != cpver_of_node(page))
			break;

		if (!is_fsync_dnode(page))
			goto next;

		entry = get_fsync_inode(head, ino_of_node(page));
		if (entry) {
			if (IS_INODE(page) && is_dent_dnode(page))
				set_inode_flag(F2FS_I(entry->inode),
							FI_INC_LINK);
		} else {
			if (IS_INODE(page) && is_dent_dnode(page)) {
				err = recover_inode_page(sbi, page);
				if (err)
					break;
			}

			/* add this fsync inode to the list */
			entry = kmem_cache_alloc(fsync_entry_slab, GFP_NOFS);
			if (!entry) {
				err = -ENOMEM;
				break;
			}

			entry->inode = f2fs_iget(sbi->sb, ino_of_node(page));
			if (IS_ERR(entry->inode)) {
				err = PTR_ERR(entry->inode);
				kmem_cache_free(fsync_entry_slab, entry);
				break;
			}
			list_add_tail(&entry->list, head);
		}
		entry->blkaddr = blkaddr;

		err = recover_inode(entry->inode, page);
		if (err && err != -ENOENT)
			break;
next:
		/* check next segment */
		blkaddr = next_blkaddr_of_node(page);
	}
	unlock_page(page);
out:
	__free_pages(page, 0);
	return err;
}

static void destroy_fsync_dnodes(struct f2fs_sb_info *sbi,
					struct list_head *head)
{
	struct fsync_inode_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, head, list) {
		iput(entry->inode);
		list_del(&entry->list);
		kmem_cache_free(fsync_entry_slab, entry);
	}
}

static void check_index_in_prev_nodes(struct f2fs_sb_info *sbi,
						block_t blkaddr)
{
	struct seg_entry *sentry;
	unsigned int segno = GET_SEGNO(sbi, blkaddr);
	unsigned short blkoff = GET_SEGOFF_FROM_SEG0(sbi, blkaddr) &
					(sbi->blocks_per_seg - 1);
	struct f2fs_summary sum;
	nid_t ino;
	void *kaddr;
	struct inode *inode;
	struct page *node_page;
	block_t bidx;
	int i;

	sentry = get_seg_entry(sbi, segno);
	if (!f2fs_test_bit(blkoff, sentry->cur_valid_map))
		return;

	/* Get the previous summary */
	for (i = CURSEG_WARM_DATA; i <= CURSEG_COLD_DATA; i++) {
		struct curseg_info *curseg = CURSEG_I(sbi, i);
		if (curseg->segno == segno) {
			sum = curseg->sum_blk->entries[blkoff];
			break;
		}
	}
	if (i > CURSEG_COLD_DATA) {
		struct page *sum_page = get_sum_page(sbi, segno);
		struct f2fs_summary_block *sum_node;
		kaddr = page_address(sum_page);
		sum_node = (struct f2fs_summary_block *)kaddr;
		sum = sum_node->entries[blkoff];
		f2fs_put_page(sum_page, 1);
	}

	/* Get the node page */
	node_page = get_node_page(sbi, le32_to_cpu(sum.nid));
	bidx = start_bidx_of_node(ofs_of_node(node_page)) +
				le16_to_cpu(sum.ofs_in_node);
	ino = ino_of_node(node_page);
	f2fs_put_page(node_page, 1);

	/* Deallocate previous index in the node page */
	inode = f2fs_iget(sbi->sb, ino);
	if (IS_ERR(inode))
		return;

	truncate_hole(inode, bidx, bidx + 1);
	iput(inode);
}

static int do_recover_data(struct f2fs_sb_info *sbi, struct inode *inode,
					struct page *page, block_t blkaddr)
{
	unsigned int start, end;
	struct dnode_of_data dn;
	struct f2fs_summary sum;
	struct node_info ni;
	int err = 0, recovered = 0;
	int ilock;

	start = start_bidx_of_node(ofs_of_node(page));
	if (IS_INODE(page))
		end = start + ADDRS_PER_INODE;
	else
		end = start + ADDRS_PER_BLOCK;

	ilock = mutex_lock_op(sbi);
	set_new_dnode(&dn, inode, NULL, NULL, 0);

	err = get_dnode_of_data(&dn, start, ALLOC_NODE);
	if (err) {
		mutex_unlock_op(sbi, ilock);
		return err;
	}

	wait_on_page_writeback(dn.node_page);

	get_node_info(sbi, dn.nid, &ni);
	BUG_ON(ni.ino != ino_of_node(page));
	BUG_ON(ofs_of_node(dn.node_page) != ofs_of_node(page));

	for (; start < end; start++) {
		block_t src, dest;

		src = datablock_addr(dn.node_page, dn.ofs_in_node);
		dest = datablock_addr(page, dn.ofs_in_node);

		if (src != dest && dest != NEW_ADDR && dest != NULL_ADDR) {
			if (src == NULL_ADDR) {
				int err = reserve_new_block(&dn);
				/* We should not get -ENOSPC */
				BUG_ON(err);
			}

			/* Check the previous node page having this index */
			check_index_in_prev_nodes(sbi, dest);

			set_summary(&sum, dn.nid, dn.ofs_in_node, ni.version);

			/* write dummy data page */
			recover_data_page(sbi, NULL, &sum, src, dest);
			update_extent_cache(dest, &dn);
			recovered++;
		}
		dn.ofs_in_node++;
	}

	/* write node page in place */
	set_summary(&sum, dn.nid, 0, 0);
	if (IS_INODE(dn.node_page))
		sync_inode_page(&dn);

	copy_node_footer(dn.node_page, page);
	fill_node_footer(dn.node_page, dn.nid, ni.ino,
					ofs_of_node(page), false);
	set_page_dirty(dn.node_page);

	recover_node_page(sbi, dn.node_page, &sum, &ni, blkaddr);
	f2fs_put_dnode(&dn);
	mutex_unlock_op(sbi, ilock);

	f2fs_msg(sbi->sb, KERN_NOTICE, "recover_data: ino = %lx, "
			"recovered_data = %d blocks",
			inode->i_ino, recovered);
	return 0;
}

static int recover_data(struct f2fs_sb_info *sbi,
				struct list_head *head, int type)
{
	unsigned long long cp_ver = le64_to_cpu(sbi->ckpt->checkpoint_ver);
	struct curseg_info *curseg;
	struct page *page;
	int err = 0;
	block_t blkaddr;

	/* get node pages in the current segment */
	curseg = CURSEG_I(sbi, type);
	blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	/* read node page */
	page = alloc_page(GFP_NOFS | __GFP_ZERO);
	if (IS_ERR(page))
		return -ENOMEM;

	lock_page(page);

	while (1) {
		struct fsync_inode_entry *entry;

		err = f2fs_readpage(sbi, page, blkaddr, READ_SYNC);
		if (err)
			goto out;

		lock_page(page);

		if (cp_ver != cpver_of_node(page))
			goto unlock_out;

		entry = get_fsync_inode(head, ino_of_node(page));
		if (!entry)
			goto next;

		err = do_recover_data(sbi, entry->inode, page, blkaddr);
		if (err)
			goto out;

		if (entry->blkaddr == blkaddr) {
			iput(entry->inode);
			list_del(&entry->list);
			kmem_cache_free(fsync_entry_slab, entry);
		}
next:
		/* check next segment */
		blkaddr = next_blkaddr_of_node(page);
	}
unlock_out:
	unlock_page(page);
out:
	__free_pages(page, 0);

	if (!err)
		allocate_new_segments(sbi);
	return err;
}

int recover_fsync_data(struct f2fs_sb_info *sbi)
{
	struct list_head inode_list;
	int err;

	fsync_entry_slab = f2fs_kmem_cache_create("f2fs_fsync_inode_entry",
			sizeof(struct fsync_inode_entry), NULL);
	if (unlikely(!fsync_entry_slab))
		return -ENOMEM;

	INIT_LIST_HEAD(&inode_list);

	/* step #1: find fsynced inode numbers */
	sbi->por_doing = 1;
	err = find_fsync_dnodes(sbi, &inode_list);
	if (err)
		goto out;

	if (list_empty(&inode_list))
		goto out;

	/* step #2: recover data */
	err = recover_data(sbi, &inode_list, CURSEG_WARM_NODE);
	BUG_ON(!list_empty(&inode_list));
out:
	destroy_fsync_dnodes(sbi, &inode_list);
	kmem_cache_destroy(fsync_entry_slab);
	sbi->por_doing = 0;
	write_checkpoint(sbi, false);
	return err;
}

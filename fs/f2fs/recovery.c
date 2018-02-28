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

/*
 * Roll forward recovery scenarios.
 *
 * [Term] F: fsync_mark, D: dentry_mark
 *
 * 1. inode(x) | CP | inode(x) | dnode(F)
 * -> Update the latest inode(x).
 *
 * 2. inode(x) | CP | inode(F) | dnode(F)
 * -> No problem.
 *
 * 3. inode(x) | CP | dnode(F) | inode(x)
 * -> Recover to the latest dnode(F), and drop the last inode(x)
 *
 * 4. inode(x) | CP | dnode(F) | inode(F)
 * -> No problem.
 *
 * 5. CP | inode(x) | dnode(F)
 * -> The inode(DF) was missing. Should drop this dnode(F).
 *
 * 6. CP | inode(DF) | dnode(F)
 * -> No problem.
 *
 * 7. CP | dnode(F) | inode(DF)
 * -> If f2fs_iget fails, then goto next to find inode(DF).
 *
 * 8. CP | dnode(F) | inode(x)
 * -> If f2fs_iget fails, then goto next to find inode(DF).
 *    But it will fail due to no inode(DF).
 */

static struct kmem_cache *fsync_entry_slab;

bool space_for_roll_forward(struct f2fs_sb_info *sbi)
{
	s64 nalloc = percpu_counter_sum_positive(&sbi->alloc_valid_block_count);

	if (sbi->last_valid_block_count + nalloc > sbi->user_block_count)
		return false;
	return true;
}

static struct fsync_inode_entry *get_fsync_inode(struct list_head *head,
								nid_t ino)
{
	struct fsync_inode_entry *entry;

	list_for_each_entry(entry, head, list)
		if (entry->inode->i_ino == ino)
			return entry;

	return NULL;
}

static struct fsync_inode_entry *add_fsync_inode(struct f2fs_sb_info *sbi,
			struct list_head *head, nid_t ino, bool quota_inode)
{
	struct inode *inode;
	struct fsync_inode_entry *entry;
	int err;

	inode = f2fs_iget_retry(sbi->sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	err = dquot_initialize(inode);
	if (err)
		goto err_out;

	if (quota_inode) {
		err = dquot_alloc_inode(inode);
		if (err)
			goto err_out;
	}

	entry = f2fs_kmem_cache_alloc(fsync_entry_slab, GFP_F2FS_ZERO);
	entry->inode = inode;
	list_add_tail(&entry->list, head);

	return entry;
err_out:
	iput(inode);
	return ERR_PTR(err);
}

static void del_fsync_inode(struct fsync_inode_entry *entry)
{
	iput(entry->inode);
	list_del(&entry->list);
	kmem_cache_free(fsync_entry_slab, entry);
}

static int recover_dentry(struct inode *inode, struct page *ipage,
						struct list_head *dir_list)
{
	struct f2fs_inode *raw_inode = F2FS_INODE(ipage);
	nid_t pino = le32_to_cpu(raw_inode->i_pino);
	struct f2fs_dir_entry *de;
	struct fscrypt_name fname;
	struct page *page;
	struct inode *dir, *einode;
	struct fsync_inode_entry *entry;
	int err = 0;
	char *name;

	entry = get_fsync_inode(dir_list, pino);
	if (!entry) {
		entry = add_fsync_inode(F2FS_I_SB(inode), dir_list,
							pino, false);
		if (IS_ERR(entry)) {
			dir = ERR_CAST(entry);
			err = PTR_ERR(entry);
			goto out;
		}
	}

	dir = entry->inode;

	memset(&fname, 0, sizeof(struct fscrypt_name));
	fname.disk_name.len = le32_to_cpu(raw_inode->i_namelen);
	fname.disk_name.name = raw_inode->i_name;

	if (unlikely(fname.disk_name.len > F2FS_NAME_LEN)) {
		WARN_ON(1);
		err = -ENAMETOOLONG;
		goto out;
	}
retry:
	de = __f2fs_find_entry(dir, &fname, &page);
	if (de && inode->i_ino == le32_to_cpu(de->ino))
		goto out_put;

	if (de) {
		einode = f2fs_iget_retry(inode->i_sb, le32_to_cpu(de->ino));
		if (IS_ERR(einode)) {
			WARN_ON(1);
			err = PTR_ERR(einode);
			if (err == -ENOENT)
				err = -EEXIST;
			goto out_put;
		}

		err = dquot_initialize(einode);
		if (err) {
			iput(einode);
			goto out_put;
		}

		err = acquire_orphan_inode(F2FS_I_SB(inode));
		if (err) {
			iput(einode);
			goto out_put;
		}
		f2fs_delete_entry(de, page, dir, einode);
		iput(einode);
		goto retry;
	} else if (IS_ERR(page)) {
		err = PTR_ERR(page);
	} else {
		err = __f2fs_do_add_link(dir, &fname, inode,
					inode->i_ino, inode->i_mode);
	}
	if (err == -ENOMEM)
		goto retry;
	goto out;

out_put:
	f2fs_put_page(page, 0);
out:
	if (file_enc_name(inode))
		name = "<encrypted>";
	else
		name = raw_inode->i_name;
	f2fs_msg(inode->i_sb, KERN_NOTICE,
			"%s: ino = %x, name = %s, dir = %lx, err = %d",
			__func__, ino_of_node(ipage), name,
			IS_ERR(dir) ? 0 : dir->i_ino, err);
	return err;
}

static void recover_inline_flags(struct inode *inode, struct f2fs_inode *ri)
{
	if (ri->i_inline & F2FS_PIN_FILE)
		set_inode_flag(inode, FI_PIN_FILE);
	else
		clear_inode_flag(inode, FI_PIN_FILE);
	if (ri->i_inline & F2FS_DATA_EXIST)
		set_inode_flag(inode, FI_DATA_EXIST);
	else
		clear_inode_flag(inode, FI_DATA_EXIST);
	if (!(ri->i_inline & F2FS_INLINE_DOTS))
		clear_inode_flag(inode, FI_INLINE_DOTS);
}

static void recover_inode(struct inode *inode, struct page *page)
{
	struct f2fs_inode *raw = F2FS_INODE(page);
	char *name;

	inode->i_mode = le16_to_cpu(raw->i_mode);
	f2fs_i_size_write(inode, le64_to_cpu(raw->i_size));
	inode->i_atime.tv_sec = le64_to_cpu(raw->i_atime);
	inode->i_ctime.tv_sec = le64_to_cpu(raw->i_ctime);
	inode->i_mtime.tv_sec = le64_to_cpu(raw->i_mtime);
	inode->i_atime.tv_nsec = le32_to_cpu(raw->i_atime_nsec);
	inode->i_ctime.tv_nsec = le32_to_cpu(raw->i_ctime_nsec);
	inode->i_mtime.tv_nsec = le32_to_cpu(raw->i_mtime_nsec);

	F2FS_I(inode)->i_advise = raw->i_advise;

	recover_inline_flags(inode, raw);

	if (file_enc_name(inode))
		name = "<encrypted>";
	else
		name = F2FS_INODE(page)->i_name;

	f2fs_msg(inode->i_sb, KERN_NOTICE,
		"recover_inode: ino = %x, name = %s, inline = %x",
			ino_of_node(page), name, raw->i_inline);
}

static int find_fsync_dnodes(struct f2fs_sb_info *sbi, struct list_head *head,
				bool check_only)
{
	struct curseg_info *curseg;
	struct page *page = NULL;
	block_t blkaddr;
	int err = 0;

	/* get node pages in the current segment */
	curseg = CURSEG_I(sbi, CURSEG_WARM_NODE);
	blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	while (1) {
		struct fsync_inode_entry *entry;

		if (!is_valid_blkaddr(sbi, blkaddr, META_POR))
			return 0;

		page = get_tmp_page(sbi, blkaddr);

		if (!is_recoverable_dnode(page))
			break;

		if (!is_fsync_dnode(page))
			goto next;

		entry = get_fsync_inode(head, ino_of_node(page));
		if (!entry) {
			bool quota_inode = false;

			if (!check_only &&
					IS_INODE(page) && is_dent_dnode(page)) {
				err = recover_inode_page(sbi, page);
				if (err)
					break;
				quota_inode = true;
			}

			/*
			 * CP | dnode(F) | inode(DF)
			 * For this case, we should not give up now.
			 */
			entry = add_fsync_inode(sbi, head, ino_of_node(page),
								quota_inode);
			if (IS_ERR(entry)) {
				err = PTR_ERR(entry);
				if (err == -ENOENT) {
					err = 0;
					goto next;
				}
				break;
			}
		}
		entry->blkaddr = blkaddr;

		if (IS_INODE(page) && is_dent_dnode(page))
			entry->last_dentry = blkaddr;
next:
		/* check next segment */
		blkaddr = next_blkaddr_of_node(page);
		f2fs_put_page(page, 1);

		ra_meta_pages_cond(sbi, blkaddr);
	}
	f2fs_put_page(page, 1);
	return err;
}

static void destroy_fsync_dnodes(struct list_head *head)
{
	struct fsync_inode_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, head, list)
		del_fsync_inode(entry);
}

static int check_index_in_prev_nodes(struct f2fs_sb_info *sbi,
			block_t blkaddr, struct dnode_of_data *dn)
{
	struct seg_entry *sentry;
	unsigned int segno = GET_SEGNO(sbi, blkaddr);
	unsigned short blkoff = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);
	struct f2fs_summary_block *sum_node;
	struct f2fs_summary sum;
	struct page *sum_page, *node_page;
	struct dnode_of_data tdn = *dn;
	nid_t ino, nid;
	struct inode *inode;
	unsigned int offset;
	block_t bidx;
	int i;

	sentry = get_seg_entry(sbi, segno);
	if (!f2fs_test_bit(blkoff, sentry->cur_valid_map))
		return 0;

	/* Get the previous summary */
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) {
		struct curseg_info *curseg = CURSEG_I(sbi, i);
		if (curseg->segno == segno) {
			sum = curseg->sum_blk->entries[blkoff];
			goto got_it;
		}
	}

	sum_page = get_sum_page(sbi, segno);
	sum_node = (struct f2fs_summary_block *)page_address(sum_page);
	sum = sum_node->entries[blkoff];
	f2fs_put_page(sum_page, 1);
got_it:
	/* Use the locked dnode page and inode */
	nid = le32_to_cpu(sum.nid);
	if (dn->inode->i_ino == nid) {
		tdn.nid = nid;
		if (!dn->inode_page_locked)
			lock_page(dn->inode_page);
		tdn.node_page = dn->inode_page;
		tdn.ofs_in_node = le16_to_cpu(sum.ofs_in_node);
		goto truncate_out;
	} else if (dn->nid == nid) {
		tdn.ofs_in_node = le16_to_cpu(sum.ofs_in_node);
		goto truncate_out;
	}

	/* Get the node page */
	node_page = get_node_page(sbi, nid);
	if (IS_ERR(node_page))
		return PTR_ERR(node_page);

	offset = ofs_of_node(node_page);
	ino = ino_of_node(node_page);
	f2fs_put_page(node_page, 1);

	if (ino != dn->inode->i_ino) {
		int ret;

		/* Deallocate previous index in the node page */
		inode = f2fs_iget_retry(sbi->sb, ino);
		if (IS_ERR(inode))
			return PTR_ERR(inode);

		ret = dquot_initialize(inode);
		if (ret) {
			iput(inode);
			return ret;
		}
	} else {
		inode = dn->inode;
	}

	bidx = start_bidx_of_node(offset, inode) + le16_to_cpu(sum.ofs_in_node);

	/*
	 * if inode page is locked, unlock temporarily, but its reference
	 * count keeps alive.
	 */
	if (ino == dn->inode->i_ino && dn->inode_page_locked)
		unlock_page(dn->inode_page);

	set_new_dnode(&tdn, inode, NULL, NULL, 0);
	if (get_dnode_of_data(&tdn, bidx, LOOKUP_NODE))
		goto out;

	if (tdn.data_blkaddr == blkaddr)
		truncate_data_blocks_range(&tdn, 1);

	f2fs_put_dnode(&tdn);
out:
	if (ino != dn->inode->i_ino)
		iput(inode);
	else if (dn->inode_page_locked)
		lock_page(dn->inode_page);
	return 0;

truncate_out:
	if (datablock_addr(tdn.inode, tdn.node_page,
					tdn.ofs_in_node) == blkaddr)
		truncate_data_blocks_range(&tdn, 1);
	if (dn->inode->i_ino == nid && !dn->inode_page_locked)
		unlock_page(dn->inode_page);
	return 0;
}

static int do_recover_data(struct f2fs_sb_info *sbi, struct inode *inode,
					struct page *page)
{
	struct dnode_of_data dn;
	struct node_info ni;
	unsigned int start, end;
	int err = 0, recovered = 0;

	/* step 1: recover xattr */
	if (IS_INODE(page)) {
		recover_inline_xattr(inode, page);
	} else if (f2fs_has_xattr_block(ofs_of_node(page))) {
		err = recover_xattr_data(inode, page);
		if (!err)
			recovered++;
		goto out;
	}

	/* step 2: recover inline data */
	if (recover_inline_data(inode, page))
		goto out;

	/* step 3: recover data indices */
	start = start_bidx_of_node(ofs_of_node(page), inode);
	end = start + ADDRS_PER_PAGE(page, inode);

	set_new_dnode(&dn, inode, NULL, NULL, 0);
retry_dn:
	err = get_dnode_of_data(&dn, start, ALLOC_NODE);
	if (err) {
		if (err == -ENOMEM) {
			congestion_wait(BLK_RW_ASYNC, HZ/50);
			goto retry_dn;
		}
		goto out;
	}

	f2fs_wait_on_page_writeback(dn.node_page, NODE, true);

	get_node_info(sbi, dn.nid, &ni);
	f2fs_bug_on(sbi, ni.ino != ino_of_node(page));
	f2fs_bug_on(sbi, ofs_of_node(dn.node_page) != ofs_of_node(page));

	for (; start < end; start++, dn.ofs_in_node++) {
		block_t src, dest;

		src = datablock_addr(dn.inode, dn.node_page, dn.ofs_in_node);
		dest = datablock_addr(dn.inode, page, dn.ofs_in_node);

		/* skip recovering if dest is the same as src */
		if (src == dest)
			continue;

		/* dest is invalid, just invalidate src block */
		if (dest == NULL_ADDR) {
			truncate_data_blocks_range(&dn, 1);
			continue;
		}

		if (!file_keep_isize(inode) &&
			(i_size_read(inode) <= ((loff_t)start << PAGE_SHIFT)))
			f2fs_i_size_write(inode,
				(loff_t)(start + 1) << PAGE_SHIFT);

		/*
		 * dest is reserved block, invalidate src block
		 * and then reserve one new block in dnode page.
		 */
		if (dest == NEW_ADDR) {
			truncate_data_blocks_range(&dn, 1);
			reserve_new_block(&dn);
			continue;
		}

		/* dest is valid block, try to recover from src to dest */
		if (is_valid_blkaddr(sbi, dest, META_POR)) {

			if (src == NULL_ADDR) {
				err = reserve_new_block(&dn);
#ifdef CONFIG_F2FS_FAULT_INJECTION
				while (err)
					err = reserve_new_block(&dn);
#endif
				/* We should not get -ENOSPC */
				f2fs_bug_on(sbi, err);
				if (err)
					goto err;
			}
retry_prev:
			/* Check the previous node page having this index */
			err = check_index_in_prev_nodes(sbi, dest, &dn);
			if (err) {
				if (err == -ENOMEM) {
					congestion_wait(BLK_RW_ASYNC, HZ/50);
					goto retry_prev;
				}
				goto err;
			}

			/* write dummy data page */
			f2fs_replace_block(sbi, &dn, src, dest,
						ni.version, false, false);
			recovered++;
		}
	}

	copy_node_footer(dn.node_page, page);
	fill_node_footer(dn.node_page, dn.nid, ni.ino,
					ofs_of_node(page), false);
	set_page_dirty(dn.node_page);
err:
	f2fs_put_dnode(&dn);
out:
	f2fs_msg(sbi->sb, KERN_NOTICE,
		"recover_data: ino = %lx (i_size: %s) recovered = %d, err = %d",
		inode->i_ino,
		file_keep_isize(inode) ? "keep" : "recover",
		recovered, err);
	return err;
}

static int recover_data(struct f2fs_sb_info *sbi, struct list_head *inode_list,
						struct list_head *dir_list)
{
	struct curseg_info *curseg;
	struct page *page = NULL;
	int err = 0;
	block_t blkaddr;

	/* get node pages in the current segment */
	curseg = CURSEG_I(sbi, CURSEG_WARM_NODE);
	blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	while (1) {
		struct fsync_inode_entry *entry;

		if (!is_valid_blkaddr(sbi, blkaddr, META_POR))
			break;

		ra_meta_pages_cond(sbi, blkaddr);

		page = get_tmp_page(sbi, blkaddr);

		if (!is_recoverable_dnode(page)) {
			f2fs_put_page(page, 1);
			break;
		}

		entry = get_fsync_inode(inode_list, ino_of_node(page));
		if (!entry)
			goto next;
		/*
		 * inode(x) | CP | inode(x) | dnode(F)
		 * In this case, we can lose the latest inode(x).
		 * So, call recover_inode for the inode update.
		 */
		if (IS_INODE(page))
			recover_inode(entry->inode, page);
		if (entry->last_dentry == blkaddr) {
			err = recover_dentry(entry->inode, page, dir_list);
			if (err) {
				f2fs_put_page(page, 1);
				break;
			}
		}
		err = do_recover_data(sbi, entry->inode, page);
		if (err) {
			f2fs_put_page(page, 1);
			break;
		}

		if (entry->blkaddr == blkaddr)
			del_fsync_inode(entry);
next:
		/* check next segment */
		blkaddr = next_blkaddr_of_node(page);
		f2fs_put_page(page, 1);
	}
	if (!err)
		allocate_new_segments(sbi);
	return err;
}

int recover_fsync_data(struct f2fs_sb_info *sbi, bool check_only)
{
	struct list_head inode_list;
	struct list_head dir_list;
	int err;
	int ret = 0;
	unsigned long s_flags = sbi->sb->s_flags;
	bool need_writecp = false;
#ifdef CONFIG_QUOTA
	int quota_enabled;
#endif

	if (s_flags & MS_RDONLY) {
		f2fs_msg(sbi->sb, KERN_INFO, "orphan cleanup on readonly fs");
		sbi->sb->s_flags &= ~MS_RDONLY;
	}

#ifdef CONFIG_QUOTA
	/* Needed for iput() to work correctly and not trash data */
	sbi->sb->s_flags |= MS_ACTIVE;
	/* Turn on quotas so that they are updated correctly */
	quota_enabled = f2fs_enable_quota_files(sbi, s_flags & MS_RDONLY);
#endif

	fsync_entry_slab = f2fs_kmem_cache_create("f2fs_fsync_inode_entry",
			sizeof(struct fsync_inode_entry));
	if (!fsync_entry_slab) {
		err = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&inode_list);
	INIT_LIST_HEAD(&dir_list);

	/* prevent checkpoint */
	mutex_lock(&sbi->cp_mutex);

	/* step #1: find fsynced inode numbers */
	err = find_fsync_dnodes(sbi, &inode_list, check_only);
	if (err || list_empty(&inode_list))
		goto skip;

	if (check_only) {
		ret = 1;
		goto skip;
	}

	need_writecp = true;

	/* step #2: recover data */
	err = recover_data(sbi, &inode_list, &dir_list);
	if (!err)
		f2fs_bug_on(sbi, !list_empty(&inode_list));
skip:
	destroy_fsync_dnodes(&inode_list);

	/* truncate meta pages to be used by the recovery */
	truncate_inode_pages_range(META_MAPPING(sbi),
			(loff_t)MAIN_BLKADDR(sbi) << PAGE_SHIFT, -1);

	if (err) {
		truncate_inode_pages_final(NODE_MAPPING(sbi));
		truncate_inode_pages_final(META_MAPPING(sbi));
	}

	clear_sbi_flag(sbi, SBI_POR_DOING);
	mutex_unlock(&sbi->cp_mutex);

	/* let's drop all the directory inodes for clean checkpoint */
	destroy_fsync_dnodes(&dir_list);

	if (!err && need_writecp) {
		struct cp_control cpc = {
			.reason = CP_RECOVERY,
		};
		err = write_checkpoint(sbi, &cpc);
	}

	kmem_cache_destroy(fsync_entry_slab);
out:
#ifdef CONFIG_QUOTA
	/* Turn quotas off */
	if (quota_enabled)
		f2fs_quota_off_umount(sbi->sb);
#endif
	sbi->sb->s_flags = s_flags; /* Restore MS_RDONLY status */

	return ret ? ret: err;
}

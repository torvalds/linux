// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/recovery.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include "f2fs.h"
#include "yesde.h"
#include "segment.h"

/*
 * Roll forward recovery scenarios.
 *
 * [Term] F: fsync_mark, D: dentry_mark
 *
 * 1. iyesde(x) | CP | iyesde(x) | dyesde(F)
 * -> Update the latest iyesde(x).
 *
 * 2. iyesde(x) | CP | iyesde(F) | dyesde(F)
 * -> No problem.
 *
 * 3. iyesde(x) | CP | dyesde(F) | iyesde(x)
 * -> Recover to the latest dyesde(F), and drop the last iyesde(x)
 *
 * 4. iyesde(x) | CP | dyesde(F) | iyesde(F)
 * -> No problem.
 *
 * 5. CP | iyesde(x) | dyesde(F)
 * -> The iyesde(DF) was missing. Should drop this dyesde(F).
 *
 * 6. CP | iyesde(DF) | dyesde(F)
 * -> No problem.
 *
 * 7. CP | dyesde(F) | iyesde(DF)
 * -> If f2fs_iget fails, then goto next to find iyesde(DF).
 *
 * 8. CP | dyesde(F) | iyesde(x)
 * -> If f2fs_iget fails, then goto next to find iyesde(DF).
 *    But it will fail due to yes iyesde(DF).
 */

static struct kmem_cache *fsync_entry_slab;

bool f2fs_space_for_roll_forward(struct f2fs_sb_info *sbi)
{
	s64 nalloc = percpu_counter_sum_positive(&sbi->alloc_valid_block_count);

	if (sbi->last_valid_block_count + nalloc > sbi->user_block_count)
		return false;
	return true;
}

static struct fsync_iyesde_entry *get_fsync_iyesde(struct list_head *head,
								nid_t iyes)
{
	struct fsync_iyesde_entry *entry;

	list_for_each_entry(entry, head, list)
		if (entry->iyesde->i_iyes == iyes)
			return entry;

	return NULL;
}

static struct fsync_iyesde_entry *add_fsync_iyesde(struct f2fs_sb_info *sbi,
			struct list_head *head, nid_t iyes, bool quota_iyesde)
{
	struct iyesde *iyesde;
	struct fsync_iyesde_entry *entry;
	int err;

	iyesde = f2fs_iget_retry(sbi->sb, iyes);
	if (IS_ERR(iyesde))
		return ERR_CAST(iyesde);

	err = dquot_initialize(iyesde);
	if (err)
		goto err_out;

	if (quota_iyesde) {
		err = dquot_alloc_iyesde(iyesde);
		if (err)
			goto err_out;
	}

	entry = f2fs_kmem_cache_alloc(fsync_entry_slab, GFP_F2FS_ZERO);
	entry->iyesde = iyesde;
	list_add_tail(&entry->list, head);

	return entry;
err_out:
	iput(iyesde);
	return ERR_PTR(err);
}

static void del_fsync_iyesde(struct fsync_iyesde_entry *entry, int drop)
{
	if (drop) {
		/* iyesde should yest be recovered, drop it */
		f2fs_iyesde_synced(entry->iyesde);
	}
	iput(entry->iyesde);
	list_del(&entry->list);
	kmem_cache_free(fsync_entry_slab, entry);
}

static int recover_dentry(struct iyesde *iyesde, struct page *ipage,
						struct list_head *dir_list)
{
	struct f2fs_iyesde *raw_iyesde = F2FS_INODE(ipage);
	nid_t piyes = le32_to_cpu(raw_iyesde->i_piyes);
	struct f2fs_dir_entry *de;
	struct fscrypt_name fname;
	struct page *page;
	struct iyesde *dir, *eiyesde;
	struct fsync_iyesde_entry *entry;
	int err = 0;
	char *name;

	entry = get_fsync_iyesde(dir_list, piyes);
	if (!entry) {
		entry = add_fsync_iyesde(F2FS_I_SB(iyesde), dir_list,
							piyes, false);
		if (IS_ERR(entry)) {
			dir = ERR_CAST(entry);
			err = PTR_ERR(entry);
			goto out;
		}
	}

	dir = entry->iyesde;

	memset(&fname, 0, sizeof(struct fscrypt_name));
	fname.disk_name.len = le32_to_cpu(raw_iyesde->i_namelen);
	fname.disk_name.name = raw_iyesde->i_name;

	if (unlikely(fname.disk_name.len > F2FS_NAME_LEN)) {
		WARN_ON(1);
		err = -ENAMETOOLONG;
		goto out;
	}
retry:
	de = __f2fs_find_entry(dir, &fname, &page);
	if (de && iyesde->i_iyes == le32_to_cpu(de->iyes))
		goto out_put;

	if (de) {
		eiyesde = f2fs_iget_retry(iyesde->i_sb, le32_to_cpu(de->iyes));
		if (IS_ERR(eiyesde)) {
			WARN_ON(1);
			err = PTR_ERR(eiyesde);
			if (err == -ENOENT)
				err = -EEXIST;
			goto out_put;
		}

		err = dquot_initialize(eiyesde);
		if (err) {
			iput(eiyesde);
			goto out_put;
		}

		err = f2fs_acquire_orphan_iyesde(F2FS_I_SB(iyesde));
		if (err) {
			iput(eiyesde);
			goto out_put;
		}
		f2fs_delete_entry(de, page, dir, eiyesde);
		iput(eiyesde);
		goto retry;
	} else if (IS_ERR(page)) {
		err = PTR_ERR(page);
	} else {
		err = f2fs_add_dentry(dir, &fname, iyesde,
					iyesde->i_iyes, iyesde->i_mode);
	}
	if (err == -ENOMEM)
		goto retry;
	goto out;

out_put:
	f2fs_put_page(page, 0);
out:
	if (file_enc_name(iyesde))
		name = "<encrypted>";
	else
		name = raw_iyesde->i_name;
	f2fs_yestice(F2FS_I_SB(iyesde), "%s: iyes = %x, name = %s, dir = %lx, err = %d",
		    __func__, iyes_of_yesde(ipage), name,
		    IS_ERR(dir) ? 0 : dir->i_iyes, err);
	return err;
}

static int recover_quota_data(struct iyesde *iyesde, struct page *page)
{
	struct f2fs_iyesde *raw = F2FS_INODE(page);
	struct iattr attr;
	uid_t i_uid = le32_to_cpu(raw->i_uid);
	gid_t i_gid = le32_to_cpu(raw->i_gid);
	int err;

	memset(&attr, 0, sizeof(attr));

	attr.ia_uid = make_kuid(iyesde->i_sb->s_user_ns, i_uid);
	attr.ia_gid = make_kgid(iyesde->i_sb->s_user_ns, i_gid);

	if (!uid_eq(attr.ia_uid, iyesde->i_uid))
		attr.ia_valid |= ATTR_UID;
	if (!gid_eq(attr.ia_gid, iyesde->i_gid))
		attr.ia_valid |= ATTR_GID;

	if (!attr.ia_valid)
		return 0;

	err = dquot_transfer(iyesde, &attr);
	if (err)
		set_sbi_flag(F2FS_I_SB(iyesde), SBI_QUOTA_NEED_REPAIR);
	return err;
}

static void recover_inline_flags(struct iyesde *iyesde, struct f2fs_iyesde *ri)
{
	if (ri->i_inline & F2FS_PIN_FILE)
		set_iyesde_flag(iyesde, FI_PIN_FILE);
	else
		clear_iyesde_flag(iyesde, FI_PIN_FILE);
	if (ri->i_inline & F2FS_DATA_EXIST)
		set_iyesde_flag(iyesde, FI_DATA_EXIST);
	else
		clear_iyesde_flag(iyesde, FI_DATA_EXIST);
}

static int recover_iyesde(struct iyesde *iyesde, struct page *page)
{
	struct f2fs_iyesde *raw = F2FS_INODE(page);
	char *name;
	int err;

	iyesde->i_mode = le16_to_cpu(raw->i_mode);

	err = recover_quota_data(iyesde, page);
	if (err)
		return err;

	i_uid_write(iyesde, le32_to_cpu(raw->i_uid));
	i_gid_write(iyesde, le32_to_cpu(raw->i_gid));

	if (raw->i_inline & F2FS_EXTRA_ATTR) {
		if (f2fs_sb_has_project_quota(F2FS_I_SB(iyesde)) &&
			F2FS_FITS_IN_INODE(raw, le16_to_cpu(raw->i_extra_isize),
								i_projid)) {
			projid_t i_projid;
			kprojid_t kprojid;

			i_projid = (projid_t)le32_to_cpu(raw->i_projid);
			kprojid = make_kprojid(&init_user_ns, i_projid);

			if (!projid_eq(kprojid, F2FS_I(iyesde)->i_projid)) {
				err = f2fs_transfer_project_quota(iyesde,
								kprojid);
				if (err)
					return err;
				F2FS_I(iyesde)->i_projid = kprojid;
			}
		}
	}

	f2fs_i_size_write(iyesde, le64_to_cpu(raw->i_size));
	iyesde->i_atime.tv_sec = le64_to_cpu(raw->i_atime);
	iyesde->i_ctime.tv_sec = le64_to_cpu(raw->i_ctime);
	iyesde->i_mtime.tv_sec = le64_to_cpu(raw->i_mtime);
	iyesde->i_atime.tv_nsec = le32_to_cpu(raw->i_atime_nsec);
	iyesde->i_ctime.tv_nsec = le32_to_cpu(raw->i_ctime_nsec);
	iyesde->i_mtime.tv_nsec = le32_to_cpu(raw->i_mtime_nsec);

	F2FS_I(iyesde)->i_advise = raw->i_advise;
	F2FS_I(iyesde)->i_flags = le32_to_cpu(raw->i_flags);
	f2fs_set_iyesde_flags(iyesde);
	F2FS_I(iyesde)->i_gc_failures[GC_FAILURE_PIN] =
				le16_to_cpu(raw->i_gc_failures);

	recover_inline_flags(iyesde, raw);

	f2fs_mark_iyesde_dirty_sync(iyesde, true);

	if (file_enc_name(iyesde))
		name = "<encrypted>";
	else
		name = F2FS_INODE(page)->i_name;

	f2fs_yestice(F2FS_I_SB(iyesde), "recover_iyesde: iyes = %x, name = %s, inline = %x",
		    iyes_of_yesde(page), name, raw->i_inline);
	return 0;
}

static int find_fsync_dyesdes(struct f2fs_sb_info *sbi, struct list_head *head,
				bool check_only)
{
	struct curseg_info *curseg;
	struct page *page = NULL;
	block_t blkaddr;
	unsigned int loop_cnt = 0;
	unsigned int free_blocks = MAIN_SEGS(sbi) * sbi->blocks_per_seg -
						valid_user_blocks(sbi);
	int err = 0;

	/* get yesde pages in the current segment */
	curseg = CURSEG_I(sbi, CURSEG_WARM_NODE);
	blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	while (1) {
		struct fsync_iyesde_entry *entry;

		if (!f2fs_is_valid_blkaddr(sbi, blkaddr, META_POR))
			return 0;

		page = f2fs_get_tmp_page(sbi, blkaddr);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			break;
		}

		if (!is_recoverable_dyesde(page)) {
			f2fs_put_page(page, 1);
			break;
		}

		if (!is_fsync_dyesde(page))
			goto next;

		entry = get_fsync_iyesde(head, iyes_of_yesde(page));
		if (!entry) {
			bool quota_iyesde = false;

			if (!check_only &&
					IS_INODE(page) && is_dent_dyesde(page)) {
				err = f2fs_recover_iyesde_page(sbi, page);
				if (err) {
					f2fs_put_page(page, 1);
					break;
				}
				quota_iyesde = true;
			}

			/*
			 * CP | dyesde(F) | iyesde(DF)
			 * For this case, we should yest give up yesw.
			 */
			entry = add_fsync_iyesde(sbi, head, iyes_of_yesde(page),
								quota_iyesde);
			if (IS_ERR(entry)) {
				err = PTR_ERR(entry);
				if (err == -ENOENT) {
					err = 0;
					goto next;
				}
				f2fs_put_page(page, 1);
				break;
			}
		}
		entry->blkaddr = blkaddr;

		if (IS_INODE(page) && is_dent_dyesde(page))
			entry->last_dentry = blkaddr;
next:
		/* sanity check in order to detect looped yesde chain */
		if (++loop_cnt >= free_blocks ||
			blkaddr == next_blkaddr_of_yesde(page)) {
			f2fs_yestice(sbi, "%s: detect looped yesde chain, blkaddr:%u, next:%u",
				    __func__, blkaddr,
				    next_blkaddr_of_yesde(page));
			f2fs_put_page(page, 1);
			err = -EINVAL;
			break;
		}

		/* check next segment */
		blkaddr = next_blkaddr_of_yesde(page);
		f2fs_put_page(page, 1);

		f2fs_ra_meta_pages_cond(sbi, blkaddr);
	}
	return err;
}

static void destroy_fsync_dyesdes(struct list_head *head, int drop)
{
	struct fsync_iyesde_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, head, list)
		del_fsync_iyesde(entry, drop);
}

static int check_index_in_prev_yesdes(struct f2fs_sb_info *sbi,
			block_t blkaddr, struct dyesde_of_data *dn)
{
	struct seg_entry *sentry;
	unsigned int segyes = GET_SEGNO(sbi, blkaddr);
	unsigned short blkoff = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);
	struct f2fs_summary_block *sum_yesde;
	struct f2fs_summary sum;
	struct page *sum_page, *yesde_page;
	struct dyesde_of_data tdn = *dn;
	nid_t iyes, nid;
	struct iyesde *iyesde;
	unsigned int offset;
	block_t bidx;
	int i;

	sentry = get_seg_entry(sbi, segyes);
	if (!f2fs_test_bit(blkoff, sentry->cur_valid_map))
		return 0;

	/* Get the previous summary */
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) {
		struct curseg_info *curseg = CURSEG_I(sbi, i);
		if (curseg->segyes == segyes) {
			sum = curseg->sum_blk->entries[blkoff];
			goto got_it;
		}
	}

	sum_page = f2fs_get_sum_page(sbi, segyes);
	if (IS_ERR(sum_page))
		return PTR_ERR(sum_page);
	sum_yesde = (struct f2fs_summary_block *)page_address(sum_page);
	sum = sum_yesde->entries[blkoff];
	f2fs_put_page(sum_page, 1);
got_it:
	/* Use the locked dyesde page and iyesde */
	nid = le32_to_cpu(sum.nid);
	if (dn->iyesde->i_iyes == nid) {
		tdn.nid = nid;
		if (!dn->iyesde_page_locked)
			lock_page(dn->iyesde_page);
		tdn.yesde_page = dn->iyesde_page;
		tdn.ofs_in_yesde = le16_to_cpu(sum.ofs_in_yesde);
		goto truncate_out;
	} else if (dn->nid == nid) {
		tdn.ofs_in_yesde = le16_to_cpu(sum.ofs_in_yesde);
		goto truncate_out;
	}

	/* Get the yesde page */
	yesde_page = f2fs_get_yesde_page(sbi, nid);
	if (IS_ERR(yesde_page))
		return PTR_ERR(yesde_page);

	offset = ofs_of_yesde(yesde_page);
	iyes = iyes_of_yesde(yesde_page);
	f2fs_put_page(yesde_page, 1);

	if (iyes != dn->iyesde->i_iyes) {
		int ret;

		/* Deallocate previous index in the yesde page */
		iyesde = f2fs_iget_retry(sbi->sb, iyes);
		if (IS_ERR(iyesde))
			return PTR_ERR(iyesde);

		ret = dquot_initialize(iyesde);
		if (ret) {
			iput(iyesde);
			return ret;
		}
	} else {
		iyesde = dn->iyesde;
	}

	bidx = f2fs_start_bidx_of_yesde(offset, iyesde) +
				le16_to_cpu(sum.ofs_in_yesde);

	/*
	 * if iyesde page is locked, unlock temporarily, but its reference
	 * count keeps alive.
	 */
	if (iyes == dn->iyesde->i_iyes && dn->iyesde_page_locked)
		unlock_page(dn->iyesde_page);

	set_new_dyesde(&tdn, iyesde, NULL, NULL, 0);
	if (f2fs_get_dyesde_of_data(&tdn, bidx, LOOKUP_NODE))
		goto out;

	if (tdn.data_blkaddr == blkaddr)
		f2fs_truncate_data_blocks_range(&tdn, 1);

	f2fs_put_dyesde(&tdn);
out:
	if (iyes != dn->iyesde->i_iyes)
		iput(iyesde);
	else if (dn->iyesde_page_locked)
		lock_page(dn->iyesde_page);
	return 0;

truncate_out:
	if (datablock_addr(tdn.iyesde, tdn.yesde_page,
					tdn.ofs_in_yesde) == blkaddr)
		f2fs_truncate_data_blocks_range(&tdn, 1);
	if (dn->iyesde->i_iyes == nid && !dn->iyesde_page_locked)
		unlock_page(dn->iyesde_page);
	return 0;
}

static int do_recover_data(struct f2fs_sb_info *sbi, struct iyesde *iyesde,
					struct page *page)
{
	struct dyesde_of_data dn;
	struct yesde_info ni;
	unsigned int start, end;
	int err = 0, recovered = 0;

	/* step 1: recover xattr */
	if (IS_INODE(page)) {
		f2fs_recover_inline_xattr(iyesde, page);
	} else if (f2fs_has_xattr_block(ofs_of_yesde(page))) {
		err = f2fs_recover_xattr_data(iyesde, page);
		if (!err)
			recovered++;
		goto out;
	}

	/* step 2: recover inline data */
	if (f2fs_recover_inline_data(iyesde, page))
		goto out;

	/* step 3: recover data indices */
	start = f2fs_start_bidx_of_yesde(ofs_of_yesde(page), iyesde);
	end = start + ADDRS_PER_PAGE(page, iyesde);

	set_new_dyesde(&dn, iyesde, NULL, NULL, 0);
retry_dn:
	err = f2fs_get_dyesde_of_data(&dn, start, ALLOC_NODE);
	if (err) {
		if (err == -ENOMEM) {
			congestion_wait(BLK_RW_ASYNC, HZ/50);
			goto retry_dn;
		}
		goto out;
	}

	f2fs_wait_on_page_writeback(dn.yesde_page, NODE, true, true);

	err = f2fs_get_yesde_info(sbi, dn.nid, &ni);
	if (err)
		goto err;

	f2fs_bug_on(sbi, ni.iyes != iyes_of_yesde(page));

	if (ofs_of_yesde(dn.yesde_page) != ofs_of_yesde(page)) {
		f2fs_warn(sbi, "Inconsistent ofs_of_yesde, iyes:%lu, ofs:%u, %u",
			  iyesde->i_iyes, ofs_of_yesde(dn.yesde_page),
			  ofs_of_yesde(page));
		err = -EFSCORRUPTED;
		goto err;
	}

	for (; start < end; start++, dn.ofs_in_yesde++) {
		block_t src, dest;

		src = datablock_addr(dn.iyesde, dn.yesde_page, dn.ofs_in_yesde);
		dest = datablock_addr(dn.iyesde, page, dn.ofs_in_yesde);

		if (__is_valid_data_blkaddr(src) &&
			!f2fs_is_valid_blkaddr(sbi, src, META_POR)) {
			err = -EFSCORRUPTED;
			goto err;
		}

		if (__is_valid_data_blkaddr(dest) &&
			!f2fs_is_valid_blkaddr(sbi, dest, META_POR)) {
			err = -EFSCORRUPTED;
			goto err;
		}

		/* skip recovering if dest is the same as src */
		if (src == dest)
			continue;

		/* dest is invalid, just invalidate src block */
		if (dest == NULL_ADDR) {
			f2fs_truncate_data_blocks_range(&dn, 1);
			continue;
		}

		if (!file_keep_isize(iyesde) &&
			(i_size_read(iyesde) <= ((loff_t)start << PAGE_SHIFT)))
			f2fs_i_size_write(iyesde,
				(loff_t)(start + 1) << PAGE_SHIFT);

		/*
		 * dest is reserved block, invalidate src block
		 * and then reserve one new block in dyesde page.
		 */
		if (dest == NEW_ADDR) {
			f2fs_truncate_data_blocks_range(&dn, 1);
			f2fs_reserve_new_block(&dn);
			continue;
		}

		/* dest is valid block, try to recover from src to dest */
		if (f2fs_is_valid_blkaddr(sbi, dest, META_POR)) {

			if (src == NULL_ADDR) {
				err = f2fs_reserve_new_block(&dn);
				while (err &&
				       IS_ENABLED(CONFIG_F2FS_FAULT_INJECTION))
					err = f2fs_reserve_new_block(&dn);
				/* We should yest get -ENOSPC */
				f2fs_bug_on(sbi, err);
				if (err)
					goto err;
			}
retry_prev:
			/* Check the previous yesde page having this index */
			err = check_index_in_prev_yesdes(sbi, dest, &dn);
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

	copy_yesde_footer(dn.yesde_page, page);
	fill_yesde_footer(dn.yesde_page, dn.nid, ni.iyes,
					ofs_of_yesde(page), false);
	set_page_dirty(dn.yesde_page);
err:
	f2fs_put_dyesde(&dn);
out:
	f2fs_yestice(sbi, "recover_data: iyes = %lx (i_size: %s) recovered = %d, err = %d",
		    iyesde->i_iyes, file_keep_isize(iyesde) ? "keep" : "recover",
		    recovered, err);
	return err;
}

static int recover_data(struct f2fs_sb_info *sbi, struct list_head *iyesde_list,
		struct list_head *tmp_iyesde_list, struct list_head *dir_list)
{
	struct curseg_info *curseg;
	struct page *page = NULL;
	int err = 0;
	block_t blkaddr;

	/* get yesde pages in the current segment */
	curseg = CURSEG_I(sbi, CURSEG_WARM_NODE);
	blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	while (1) {
		struct fsync_iyesde_entry *entry;

		if (!f2fs_is_valid_blkaddr(sbi, blkaddr, META_POR))
			break;

		f2fs_ra_meta_pages_cond(sbi, blkaddr);

		page = f2fs_get_tmp_page(sbi, blkaddr);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			break;
		}

		if (!is_recoverable_dyesde(page)) {
			f2fs_put_page(page, 1);
			break;
		}

		entry = get_fsync_iyesde(iyesde_list, iyes_of_yesde(page));
		if (!entry)
			goto next;
		/*
		 * iyesde(x) | CP | iyesde(x) | dyesde(F)
		 * In this case, we can lose the latest iyesde(x).
		 * So, call recover_iyesde for the iyesde update.
		 */
		if (IS_INODE(page)) {
			err = recover_iyesde(entry->iyesde, page);
			if (err) {
				f2fs_put_page(page, 1);
				break;
			}
		}
		if (entry->last_dentry == blkaddr) {
			err = recover_dentry(entry->iyesde, page, dir_list);
			if (err) {
				f2fs_put_page(page, 1);
				break;
			}
		}
		err = do_recover_data(sbi, entry->iyesde, page);
		if (err) {
			f2fs_put_page(page, 1);
			break;
		}

		if (entry->blkaddr == blkaddr)
			list_move_tail(&entry->list, tmp_iyesde_list);
next:
		/* check next segment */
		blkaddr = next_blkaddr_of_yesde(page);
		f2fs_put_page(page, 1);
	}
	if (!err)
		f2fs_allocate_new_segments(sbi, NO_CHECK_TYPE);
	return err;
}

int f2fs_recover_fsync_data(struct f2fs_sb_info *sbi, bool check_only)
{
	struct list_head iyesde_list, tmp_iyesde_list;
	struct list_head dir_list;
	int err;
	int ret = 0;
	unsigned long s_flags = sbi->sb->s_flags;
	bool need_writecp = false;
#ifdef CONFIG_QUOTA
	int quota_enabled;
#endif

	if (s_flags & SB_RDONLY) {
		f2fs_info(sbi, "recover fsync data on readonly fs");
		sbi->sb->s_flags &= ~SB_RDONLY;
	}

#ifdef CONFIG_QUOTA
	/* Needed for iput() to work correctly and yest trash data */
	sbi->sb->s_flags |= SB_ACTIVE;
	/* Turn on quotas so that they are updated correctly */
	quota_enabled = f2fs_enable_quota_files(sbi, s_flags & SB_RDONLY);
#endif

	fsync_entry_slab = f2fs_kmem_cache_create("f2fs_fsync_iyesde_entry",
			sizeof(struct fsync_iyesde_entry));
	if (!fsync_entry_slab) {
		err = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&iyesde_list);
	INIT_LIST_HEAD(&tmp_iyesde_list);
	INIT_LIST_HEAD(&dir_list);

	/* prevent checkpoint */
	mutex_lock(&sbi->cp_mutex);

	/* step #1: find fsynced iyesde numbers */
	err = find_fsync_dyesdes(sbi, &iyesde_list, check_only);
	if (err || list_empty(&iyesde_list))
		goto skip;

	if (check_only) {
		ret = 1;
		goto skip;
	}

	need_writecp = true;

	/* step #2: recover data */
	err = recover_data(sbi, &iyesde_list, &tmp_iyesde_list, &dir_list);
	if (!err)
		f2fs_bug_on(sbi, !list_empty(&iyesde_list));
	else {
		/* restore s_flags to let iput() trash data */
		sbi->sb->s_flags = s_flags;
	}
skip:
	destroy_fsync_dyesdes(&iyesde_list, err);
	destroy_fsync_dyesdes(&tmp_iyesde_list, err);

	/* truncate meta pages to be used by the recovery */
	truncate_iyesde_pages_range(META_MAPPING(sbi),
			(loff_t)MAIN_BLKADDR(sbi) << PAGE_SHIFT, -1);

	if (err) {
		truncate_iyesde_pages_final(NODE_MAPPING(sbi));
		truncate_iyesde_pages_final(META_MAPPING(sbi));
	} else {
		clear_sbi_flag(sbi, SBI_POR_DOING);
	}
	mutex_unlock(&sbi->cp_mutex);

	/* let's drop all the directory iyesdes for clean checkpoint */
	destroy_fsync_dyesdes(&dir_list, err);

	if (need_writecp) {
		set_sbi_flag(sbi, SBI_IS_RECOVERED);

		if (!err) {
			struct cp_control cpc = {
				.reason = CP_RECOVERY,
			};
			err = f2fs_write_checkpoint(sbi, &cpc);
		}
	}

	kmem_cache_destroy(fsync_entry_slab);
out:
#ifdef CONFIG_QUOTA
	/* Turn quotas off */
	if (quota_enabled)
		f2fs_quota_off_umount(sbi->sb);
#endif
	sbi->sb->s_flags = s_flags; /* Restore SB_RDONLY status */

	return ret ? ret: err;
}

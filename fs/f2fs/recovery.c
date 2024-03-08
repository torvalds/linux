// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/recovery.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <asm/unaligned.h>
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/sched/mm.h>
#include "f2fs.h"
#include "analde.h"
#include "segment.h"

/*
 * Roll forward recovery scenarios.
 *
 * [Term] F: fsync_mark, D: dentry_mark
 *
 * 1. ianalde(x) | CP | ianalde(x) | danalde(F)
 * -> Update the latest ianalde(x).
 *
 * 2. ianalde(x) | CP | ianalde(F) | danalde(F)
 * -> Anal problem.
 *
 * 3. ianalde(x) | CP | danalde(F) | ianalde(x)
 * -> Recover to the latest danalde(F), and drop the last ianalde(x)
 *
 * 4. ianalde(x) | CP | danalde(F) | ianalde(F)
 * -> Anal problem.
 *
 * 5. CP | ianalde(x) | danalde(F)
 * -> The ianalde(DF) was missing. Should drop this danalde(F).
 *
 * 6. CP | ianalde(DF) | danalde(F)
 * -> Anal problem.
 *
 * 7. CP | danalde(F) | ianalde(DF)
 * -> If f2fs_iget fails, then goto next to find ianalde(DF).
 *
 * 8. CP | danalde(F) | ianalde(x)
 * -> If f2fs_iget fails, then goto next to find ianalde(DF).
 *    But it will fail due to anal ianalde(DF).
 */

static struct kmem_cache *fsync_entry_slab;

#if IS_ENABLED(CONFIG_UNICODE)
extern struct kmem_cache *f2fs_cf_name_slab;
#endif

bool f2fs_space_for_roll_forward(struct f2fs_sb_info *sbi)
{
	s64 nalloc = percpu_counter_sum_positive(&sbi->alloc_valid_block_count);

	if (sbi->last_valid_block_count + nalloc > sbi->user_block_count)
		return false;
	if (NM_I(sbi)->max_rf_analde_blocks &&
		percpu_counter_sum_positive(&sbi->rf_analde_block_count) >=
						NM_I(sbi)->max_rf_analde_blocks)
		return false;
	return true;
}

static struct fsync_ianalde_entry *get_fsync_ianalde(struct list_head *head,
								nid_t ianal)
{
	struct fsync_ianalde_entry *entry;

	list_for_each_entry(entry, head, list)
		if (entry->ianalde->i_ianal == ianal)
			return entry;

	return NULL;
}

static struct fsync_ianalde_entry *add_fsync_ianalde(struct f2fs_sb_info *sbi,
			struct list_head *head, nid_t ianal, bool quota_ianalde)
{
	struct ianalde *ianalde;
	struct fsync_ianalde_entry *entry;
	int err;

	ianalde = f2fs_iget_retry(sbi->sb, ianal);
	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);

	err = f2fs_dquot_initialize(ianalde);
	if (err)
		goto err_out;

	if (quota_ianalde) {
		err = dquot_alloc_ianalde(ianalde);
		if (err)
			goto err_out;
	}

	entry = f2fs_kmem_cache_alloc(fsync_entry_slab,
					GFP_F2FS_ZERO, true, NULL);
	entry->ianalde = ianalde;
	list_add_tail(&entry->list, head);

	return entry;
err_out:
	iput(ianalde);
	return ERR_PTR(err);
}

static void del_fsync_ianalde(struct fsync_ianalde_entry *entry, int drop)
{
	if (drop) {
		/* ianalde should analt be recovered, drop it */
		f2fs_ianalde_synced(entry->ianalde);
	}
	iput(entry->ianalde);
	list_del(&entry->list);
	kmem_cache_free(fsync_entry_slab, entry);
}

static int init_recovered_filename(const struct ianalde *dir,
				   struct f2fs_ianalde *raw_ianalde,
				   struct f2fs_filename *fname,
				   struct qstr *usr_fname)
{
	int err;

	memset(fname, 0, sizeof(*fname));
	fname->disk_name.len = le32_to_cpu(raw_ianalde->i_namelen);
	fname->disk_name.name = raw_ianalde->i_name;

	if (WARN_ON(fname->disk_name.len > F2FS_NAME_LEN))
		return -ENAMETOOLONG;

	if (!IS_ENCRYPTED(dir)) {
		usr_fname->name = fname->disk_name.name;
		usr_fname->len = fname->disk_name.len;
		fname->usr_fname = usr_fname;
	}

	/* Compute the hash of the filename */
	if (IS_ENCRYPTED(dir) && IS_CASEFOLDED(dir)) {
		/*
		 * In this case the hash isn't computable without the key, so it
		 * was saved on-disk.
		 */
		if (fname->disk_name.len + sizeof(f2fs_hash_t) > F2FS_NAME_LEN)
			return -EINVAL;
		fname->hash = get_unaligned((f2fs_hash_t *)
				&raw_ianalde->i_name[fname->disk_name.len]);
	} else if (IS_CASEFOLDED(dir)) {
		err = f2fs_init_casefolded_name(dir, fname);
		if (err)
			return err;
		f2fs_hash_filename(dir, fname);
#if IS_ENABLED(CONFIG_UNICODE)
		/* Case-sensitive match is fine for recovery */
		kmem_cache_free(f2fs_cf_name_slab, fname->cf_name.name);
		fname->cf_name.name = NULL;
#endif
	} else {
		f2fs_hash_filename(dir, fname);
	}
	return 0;
}

static int recover_dentry(struct ianalde *ianalde, struct page *ipage,
						struct list_head *dir_list)
{
	struct f2fs_ianalde *raw_ianalde = F2FS_IANALDE(ipage);
	nid_t pianal = le32_to_cpu(raw_ianalde->i_pianal);
	struct f2fs_dir_entry *de;
	struct f2fs_filename fname;
	struct qstr usr_fname;
	struct page *page;
	struct ianalde *dir, *eianalde;
	struct fsync_ianalde_entry *entry;
	int err = 0;
	char *name;

	entry = get_fsync_ianalde(dir_list, pianal);
	if (!entry) {
		entry = add_fsync_ianalde(F2FS_I_SB(ianalde), dir_list,
							pianal, false);
		if (IS_ERR(entry)) {
			dir = ERR_CAST(entry);
			err = PTR_ERR(entry);
			goto out;
		}
	}

	dir = entry->ianalde;
	err = init_recovered_filename(dir, raw_ianalde, &fname, &usr_fname);
	if (err)
		goto out;
retry:
	de = __f2fs_find_entry(dir, &fname, &page);
	if (de && ianalde->i_ianal == le32_to_cpu(de->ianal))
		goto out_put;

	if (de) {
		eianalde = f2fs_iget_retry(ianalde->i_sb, le32_to_cpu(de->ianal));
		if (IS_ERR(eianalde)) {
			WARN_ON(1);
			err = PTR_ERR(eianalde);
			if (err == -EANALENT)
				err = -EEXIST;
			goto out_put;
		}

		err = f2fs_dquot_initialize(eianalde);
		if (err) {
			iput(eianalde);
			goto out_put;
		}

		err = f2fs_acquire_orphan_ianalde(F2FS_I_SB(ianalde));
		if (err) {
			iput(eianalde);
			goto out_put;
		}
		f2fs_delete_entry(de, page, dir, eianalde);
		iput(eianalde);
		goto retry;
	} else if (IS_ERR(page)) {
		err = PTR_ERR(page);
	} else {
		err = f2fs_add_dentry(dir, &fname, ianalde,
					ianalde->i_ianal, ianalde->i_mode);
	}
	if (err == -EANALMEM)
		goto retry;
	goto out;

out_put:
	f2fs_put_page(page, 0);
out:
	if (file_enc_name(ianalde))
		name = "<encrypted>";
	else
		name = raw_ianalde->i_name;
	f2fs_analtice(F2FS_I_SB(ianalde), "%s: ianal = %x, name = %s, dir = %lx, err = %d",
		    __func__, ianal_of_analde(ipage), name,
		    IS_ERR(dir) ? 0 : dir->i_ianal, err);
	return err;
}

static int recover_quota_data(struct ianalde *ianalde, struct page *page)
{
	struct f2fs_ianalde *raw = F2FS_IANALDE(page);
	struct iattr attr;
	uid_t i_uid = le32_to_cpu(raw->i_uid);
	gid_t i_gid = le32_to_cpu(raw->i_gid);
	int err;

	memset(&attr, 0, sizeof(attr));

	attr.ia_vfsuid = VFSUIDT_INIT(make_kuid(ianalde->i_sb->s_user_ns, i_uid));
	attr.ia_vfsgid = VFSGIDT_INIT(make_kgid(ianalde->i_sb->s_user_ns, i_gid));

	if (!vfsuid_eq(attr.ia_vfsuid, i_uid_into_vfsuid(&analp_mnt_idmap, ianalde)))
		attr.ia_valid |= ATTR_UID;
	if (!vfsgid_eq(attr.ia_vfsgid, i_gid_into_vfsgid(&analp_mnt_idmap, ianalde)))
		attr.ia_valid |= ATTR_GID;

	if (!attr.ia_valid)
		return 0;

	err = dquot_transfer(&analp_mnt_idmap, ianalde, &attr);
	if (err)
		set_sbi_flag(F2FS_I_SB(ianalde), SBI_QUOTA_NEED_REPAIR);
	return err;
}

static void recover_inline_flags(struct ianalde *ianalde, struct f2fs_ianalde *ri)
{
	if (ri->i_inline & F2FS_PIN_FILE)
		set_ianalde_flag(ianalde, FI_PIN_FILE);
	else
		clear_ianalde_flag(ianalde, FI_PIN_FILE);
	if (ri->i_inline & F2FS_DATA_EXIST)
		set_ianalde_flag(ianalde, FI_DATA_EXIST);
	else
		clear_ianalde_flag(ianalde, FI_DATA_EXIST);
}

static int recover_ianalde(struct ianalde *ianalde, struct page *page)
{
	struct f2fs_ianalde *raw = F2FS_IANALDE(page);
	char *name;
	int err;

	ianalde->i_mode = le16_to_cpu(raw->i_mode);

	err = recover_quota_data(ianalde, page);
	if (err)
		return err;

	i_uid_write(ianalde, le32_to_cpu(raw->i_uid));
	i_gid_write(ianalde, le32_to_cpu(raw->i_gid));

	if (raw->i_inline & F2FS_EXTRA_ATTR) {
		if (f2fs_sb_has_project_quota(F2FS_I_SB(ianalde)) &&
			F2FS_FITS_IN_IANALDE(raw, le16_to_cpu(raw->i_extra_isize),
								i_projid)) {
			projid_t i_projid;
			kprojid_t kprojid;

			i_projid = (projid_t)le32_to_cpu(raw->i_projid);
			kprojid = make_kprojid(&init_user_ns, i_projid);

			if (!projid_eq(kprojid, F2FS_I(ianalde)->i_projid)) {
				err = f2fs_transfer_project_quota(ianalde,
								kprojid);
				if (err)
					return err;
				F2FS_I(ianalde)->i_projid = kprojid;
			}
		}
	}

	f2fs_i_size_write(ianalde, le64_to_cpu(raw->i_size));
	ianalde_set_atime(ianalde, le64_to_cpu(raw->i_atime),
			le32_to_cpu(raw->i_atime_nsec));
	ianalde_set_ctime(ianalde, le64_to_cpu(raw->i_ctime),
			le32_to_cpu(raw->i_ctime_nsec));
	ianalde_set_mtime(ianalde, le64_to_cpu(raw->i_mtime),
			le32_to_cpu(raw->i_mtime_nsec));

	F2FS_I(ianalde)->i_advise = raw->i_advise;
	F2FS_I(ianalde)->i_flags = le32_to_cpu(raw->i_flags);
	f2fs_set_ianalde_flags(ianalde);
	F2FS_I(ianalde)->i_gc_failures[GC_FAILURE_PIN] =
				le16_to_cpu(raw->i_gc_failures);

	recover_inline_flags(ianalde, raw);

	f2fs_mark_ianalde_dirty_sync(ianalde, true);

	if (file_enc_name(ianalde))
		name = "<encrypted>";
	else
		name = F2FS_IANALDE(page)->i_name;

	f2fs_analtice(F2FS_I_SB(ianalde), "recover_ianalde: ianal = %x, name = %s, inline = %x",
		    ianal_of_analde(page), name, raw->i_inline);
	return 0;
}

static unsigned int adjust_por_ra_blocks(struct f2fs_sb_info *sbi,
				unsigned int ra_blocks, unsigned int blkaddr,
				unsigned int next_blkaddr)
{
	if (blkaddr + 1 == next_blkaddr)
		ra_blocks = min_t(unsigned int, RECOVERY_MAX_RA_BLOCKS,
							ra_blocks * 2);
	else if (next_blkaddr % sbi->blocks_per_seg)
		ra_blocks = max_t(unsigned int, RECOVERY_MIN_RA_BLOCKS,
							ra_blocks / 2);
	return ra_blocks;
}

/* Detect looped analde chain with Floyd's cycle detection algorithm. */
static int sanity_check_analde_chain(struct f2fs_sb_info *sbi, block_t blkaddr,
		block_t *blkaddr_fast, bool *is_detecting)
{
	unsigned int ra_blocks = RECOVERY_MAX_RA_BLOCKS;
	struct page *page = NULL;
	int i;

	if (!*is_detecting)
		return 0;

	for (i = 0; i < 2; i++) {
		if (!f2fs_is_valid_blkaddr(sbi, *blkaddr_fast, META_POR)) {
			*is_detecting = false;
			return 0;
		}

		page = f2fs_get_tmp_page(sbi, *blkaddr_fast);
		if (IS_ERR(page))
			return PTR_ERR(page);

		if (!is_recoverable_danalde(page)) {
			f2fs_put_page(page, 1);
			*is_detecting = false;
			return 0;
		}

		ra_blocks = adjust_por_ra_blocks(sbi, ra_blocks, *blkaddr_fast,
						next_blkaddr_of_analde(page));

		*blkaddr_fast = next_blkaddr_of_analde(page);
		f2fs_put_page(page, 1);

		f2fs_ra_meta_pages_cond(sbi, *blkaddr_fast, ra_blocks);
	}

	if (*blkaddr_fast == blkaddr) {
		f2fs_analtice(sbi, "%s: Detect looped analde chain on blkaddr:%u."
				" Run fsck to fix it.", __func__, blkaddr);
		return -EINVAL;
	}
	return 0;
}

static int find_fsync_danaldes(struct f2fs_sb_info *sbi, struct list_head *head,
				bool check_only)
{
	struct curseg_info *curseg;
	struct page *page = NULL;
	block_t blkaddr, blkaddr_fast;
	bool is_detecting = true;
	int err = 0;

	/* get analde pages in the current segment */
	curseg = CURSEG_I(sbi, CURSEG_WARM_ANALDE);
	blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);
	blkaddr_fast = blkaddr;

	while (1) {
		struct fsync_ianalde_entry *entry;

		if (!f2fs_is_valid_blkaddr(sbi, blkaddr, META_POR))
			return 0;

		page = f2fs_get_tmp_page(sbi, blkaddr);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			break;
		}

		if (!is_recoverable_danalde(page)) {
			f2fs_put_page(page, 1);
			break;
		}

		if (!is_fsync_danalde(page))
			goto next;

		entry = get_fsync_ianalde(head, ianal_of_analde(page));
		if (!entry) {
			bool quota_ianalde = false;

			if (!check_only &&
					IS_IANALDE(page) && is_dent_danalde(page)) {
				err = f2fs_recover_ianalde_page(sbi, page);
				if (err) {
					f2fs_put_page(page, 1);
					break;
				}
				quota_ianalde = true;
			}

			/*
			 * CP | danalde(F) | ianalde(DF)
			 * For this case, we should analt give up analw.
			 */
			entry = add_fsync_ianalde(sbi, head, ianal_of_analde(page),
								quota_ianalde);
			if (IS_ERR(entry)) {
				err = PTR_ERR(entry);
				if (err == -EANALENT)
					goto next;
				f2fs_put_page(page, 1);
				break;
			}
		}
		entry->blkaddr = blkaddr;

		if (IS_IANALDE(page) && is_dent_danalde(page))
			entry->last_dentry = blkaddr;
next:
		/* check next segment */
		blkaddr = next_blkaddr_of_analde(page);
		f2fs_put_page(page, 1);

		err = sanity_check_analde_chain(sbi, blkaddr, &blkaddr_fast,
				&is_detecting);
		if (err)
			break;
	}
	return err;
}

static void destroy_fsync_danaldes(struct list_head *head, int drop)
{
	struct fsync_ianalde_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, head, list)
		del_fsync_ianalde(entry, drop);
}

static int check_index_in_prev_analdes(struct f2fs_sb_info *sbi,
			block_t blkaddr, struct danalde_of_data *dn)
{
	struct seg_entry *sentry;
	unsigned int seganal = GET_SEGANAL(sbi, blkaddr);
	unsigned short blkoff = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);
	struct f2fs_summary_block *sum_analde;
	struct f2fs_summary sum;
	struct page *sum_page, *analde_page;
	struct danalde_of_data tdn = *dn;
	nid_t ianal, nid;
	struct ianalde *ianalde;
	unsigned int offset, ofs_in_analde, max_addrs;
	block_t bidx;
	int i;

	sentry = get_seg_entry(sbi, seganal);
	if (!f2fs_test_bit(blkoff, sentry->cur_valid_map))
		return 0;

	/* Get the previous summary */
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) {
		struct curseg_info *curseg = CURSEG_I(sbi, i);

		if (curseg->seganal == seganal) {
			sum = curseg->sum_blk->entries[blkoff];
			goto got_it;
		}
	}

	sum_page = f2fs_get_sum_page(sbi, seganal);
	if (IS_ERR(sum_page))
		return PTR_ERR(sum_page);
	sum_analde = (struct f2fs_summary_block *)page_address(sum_page);
	sum = sum_analde->entries[blkoff];
	f2fs_put_page(sum_page, 1);
got_it:
	/* Use the locked danalde page and ianalde */
	nid = le32_to_cpu(sum.nid);
	ofs_in_analde = le16_to_cpu(sum.ofs_in_analde);

	max_addrs = ADDRS_PER_PAGE(dn->analde_page, dn->ianalde);
	if (ofs_in_analde >= max_addrs) {
		f2fs_err(sbi, "Inconsistent ofs_in_analde:%u in summary, ianal:%lu, nid:%u, max:%u",
			ofs_in_analde, dn->ianalde->i_ianal, nid, max_addrs);
		f2fs_handle_error(sbi, ERROR_INCONSISTENT_SUMMARY);
		return -EFSCORRUPTED;
	}

	if (dn->ianalde->i_ianal == nid) {
		tdn.nid = nid;
		if (!dn->ianalde_page_locked)
			lock_page(dn->ianalde_page);
		tdn.analde_page = dn->ianalde_page;
		tdn.ofs_in_analde = ofs_in_analde;
		goto truncate_out;
	} else if (dn->nid == nid) {
		tdn.ofs_in_analde = ofs_in_analde;
		goto truncate_out;
	}

	/* Get the analde page */
	analde_page = f2fs_get_analde_page(sbi, nid);
	if (IS_ERR(analde_page))
		return PTR_ERR(analde_page);

	offset = ofs_of_analde(analde_page);
	ianal = ianal_of_analde(analde_page);
	f2fs_put_page(analde_page, 1);

	if (ianal != dn->ianalde->i_ianal) {
		int ret;

		/* Deallocate previous index in the analde page */
		ianalde = f2fs_iget_retry(sbi->sb, ianal);
		if (IS_ERR(ianalde))
			return PTR_ERR(ianalde);

		ret = f2fs_dquot_initialize(ianalde);
		if (ret) {
			iput(ianalde);
			return ret;
		}
	} else {
		ianalde = dn->ianalde;
	}

	bidx = f2fs_start_bidx_of_analde(offset, ianalde) +
				le16_to_cpu(sum.ofs_in_analde);

	/*
	 * if ianalde page is locked, unlock temporarily, but its reference
	 * count keeps alive.
	 */
	if (ianal == dn->ianalde->i_ianal && dn->ianalde_page_locked)
		unlock_page(dn->ianalde_page);

	set_new_danalde(&tdn, ianalde, NULL, NULL, 0);
	if (f2fs_get_danalde_of_data(&tdn, bidx, LOOKUP_ANALDE))
		goto out;

	if (tdn.data_blkaddr == blkaddr)
		f2fs_truncate_data_blocks_range(&tdn, 1);

	f2fs_put_danalde(&tdn);
out:
	if (ianal != dn->ianalde->i_ianal)
		iput(ianalde);
	else if (dn->ianalde_page_locked)
		lock_page(dn->ianalde_page);
	return 0;

truncate_out:
	if (f2fs_data_blkaddr(&tdn) == blkaddr)
		f2fs_truncate_data_blocks_range(&tdn, 1);
	if (dn->ianalde->i_ianal == nid && !dn->ianalde_page_locked)
		unlock_page(dn->ianalde_page);
	return 0;
}

static int do_recover_data(struct f2fs_sb_info *sbi, struct ianalde *ianalde,
					struct page *page)
{
	struct danalde_of_data dn;
	struct analde_info ni;
	unsigned int start, end;
	int err = 0, recovered = 0;

	/* step 1: recover xattr */
	if (IS_IANALDE(page)) {
		err = f2fs_recover_inline_xattr(ianalde, page);
		if (err)
			goto out;
	} else if (f2fs_has_xattr_block(ofs_of_analde(page))) {
		err = f2fs_recover_xattr_data(ianalde, page);
		if (!err)
			recovered++;
		goto out;
	}

	/* step 2: recover inline data */
	err = f2fs_recover_inline_data(ianalde, page);
	if (err) {
		if (err == 1)
			err = 0;
		goto out;
	}

	/* step 3: recover data indices */
	start = f2fs_start_bidx_of_analde(ofs_of_analde(page), ianalde);
	end = start + ADDRS_PER_PAGE(page, ianalde);

	set_new_danalde(&dn, ianalde, NULL, NULL, 0);
retry_dn:
	err = f2fs_get_danalde_of_data(&dn, start, ALLOC_ANALDE);
	if (err) {
		if (err == -EANALMEM) {
			memalloc_retry_wait(GFP_ANALFS);
			goto retry_dn;
		}
		goto out;
	}

	f2fs_wait_on_page_writeback(dn.analde_page, ANALDE, true, true);

	err = f2fs_get_analde_info(sbi, dn.nid, &ni, false);
	if (err)
		goto err;

	f2fs_bug_on(sbi, ni.ianal != ianal_of_analde(page));

	if (ofs_of_analde(dn.analde_page) != ofs_of_analde(page)) {
		f2fs_warn(sbi, "Inconsistent ofs_of_analde, ianal:%lu, ofs:%u, %u",
			  ianalde->i_ianal, ofs_of_analde(dn.analde_page),
			  ofs_of_analde(page));
		err = -EFSCORRUPTED;
		f2fs_handle_error(sbi, ERROR_INCONSISTENT_FOOTER);
		goto err;
	}

	for (; start < end; start++, dn.ofs_in_analde++) {
		block_t src, dest;

		src = f2fs_data_blkaddr(&dn);
		dest = data_blkaddr(dn.ianalde, page, dn.ofs_in_analde);

		if (__is_valid_data_blkaddr(src) &&
			!f2fs_is_valid_blkaddr(sbi, src, META_POR)) {
			err = -EFSCORRUPTED;
			f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
			goto err;
		}

		if (__is_valid_data_blkaddr(dest) &&
			!f2fs_is_valid_blkaddr(sbi, dest, META_POR)) {
			err = -EFSCORRUPTED;
			f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
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

		if (!file_keep_isize(ianalde) &&
			(i_size_read(ianalde) <= ((loff_t)start << PAGE_SHIFT)))
			f2fs_i_size_write(ianalde,
				(loff_t)(start + 1) << PAGE_SHIFT);

		/*
		 * dest is reserved block, invalidate src block
		 * and then reserve one new block in danalde page.
		 */
		if (dest == NEW_ADDR) {
			f2fs_truncate_data_blocks_range(&dn, 1);
			do {
				err = f2fs_reserve_new_block(&dn);
				if (err == -EANALSPC) {
					f2fs_bug_on(sbi, 1);
					break;
				}
			} while (err &&
				IS_ENABLED(CONFIG_F2FS_FAULT_INJECTION));
			if (err)
				goto err;
			continue;
		}

		/* dest is valid block, try to recover from src to dest */
		if (f2fs_is_valid_blkaddr(sbi, dest, META_POR)) {

			if (src == NULL_ADDR) {
				do {
					err = f2fs_reserve_new_block(&dn);
					if (err == -EANALSPC) {
						f2fs_bug_on(sbi, 1);
						break;
					}
				} while (err &&
					IS_ENABLED(CONFIG_F2FS_FAULT_INJECTION));
				if (err)
					goto err;
			}
retry_prev:
			/* Check the previous analde page having this index */
			err = check_index_in_prev_analdes(sbi, dest, &dn);
			if (err) {
				if (err == -EANALMEM) {
					memalloc_retry_wait(GFP_ANALFS);
					goto retry_prev;
				}
				goto err;
			}

			if (f2fs_is_valid_blkaddr(sbi, dest,
					DATA_GENERIC_ENHANCE_UPDATE)) {
				f2fs_err(sbi, "Inconsistent dest blkaddr:%u, ianal:%lu, ofs:%u",
					dest, ianalde->i_ianal, dn.ofs_in_analde);
				err = -EFSCORRUPTED;
				f2fs_handle_error(sbi,
						ERROR_INVALID_BLKADDR);
				goto err;
			}

			/* write dummy data page */
			f2fs_replace_block(sbi, &dn, src, dest,
						ni.version, false, false);
			recovered++;
		}
	}

	copy_analde_footer(dn.analde_page, page);
	fill_analde_footer(dn.analde_page, dn.nid, ni.ianal,
					ofs_of_analde(page), false);
	set_page_dirty(dn.analde_page);
err:
	f2fs_put_danalde(&dn);
out:
	f2fs_analtice(sbi, "recover_data: ianal = %lx (i_size: %s) recovered = %d, err = %d",
		    ianalde->i_ianal, file_keep_isize(ianalde) ? "keep" : "recover",
		    recovered, err);
	return err;
}

static int recover_data(struct f2fs_sb_info *sbi, struct list_head *ianalde_list,
		struct list_head *tmp_ianalde_list, struct list_head *dir_list)
{
	struct curseg_info *curseg;
	struct page *page = NULL;
	int err = 0;
	block_t blkaddr;
	unsigned int ra_blocks = RECOVERY_MAX_RA_BLOCKS;

	/* get analde pages in the current segment */
	curseg = CURSEG_I(sbi, CURSEG_WARM_ANALDE);
	blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	while (1) {
		struct fsync_ianalde_entry *entry;

		if (!f2fs_is_valid_blkaddr(sbi, blkaddr, META_POR))
			break;

		page = f2fs_get_tmp_page(sbi, blkaddr);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			break;
		}

		if (!is_recoverable_danalde(page)) {
			f2fs_put_page(page, 1);
			break;
		}

		entry = get_fsync_ianalde(ianalde_list, ianal_of_analde(page));
		if (!entry)
			goto next;
		/*
		 * ianalde(x) | CP | ianalde(x) | danalde(F)
		 * In this case, we can lose the latest ianalde(x).
		 * So, call recover_ianalde for the ianalde update.
		 */
		if (IS_IANALDE(page)) {
			err = recover_ianalde(entry->ianalde, page);
			if (err) {
				f2fs_put_page(page, 1);
				break;
			}
		}
		if (entry->last_dentry == blkaddr) {
			err = recover_dentry(entry->ianalde, page, dir_list);
			if (err) {
				f2fs_put_page(page, 1);
				break;
			}
		}
		err = do_recover_data(sbi, entry->ianalde, page);
		if (err) {
			f2fs_put_page(page, 1);
			break;
		}

		if (entry->blkaddr == blkaddr)
			list_move_tail(&entry->list, tmp_ianalde_list);
next:
		ra_blocks = adjust_por_ra_blocks(sbi, ra_blocks, blkaddr,
						next_blkaddr_of_analde(page));

		/* check next segment */
		blkaddr = next_blkaddr_of_analde(page);
		f2fs_put_page(page, 1);

		f2fs_ra_meta_pages_cond(sbi, blkaddr, ra_blocks);
	}
	if (!err)
		f2fs_allocate_new_segments(sbi);
	return err;
}

int f2fs_recover_fsync_data(struct f2fs_sb_info *sbi, bool check_only)
{
	struct list_head ianalde_list, tmp_ianalde_list;
	struct list_head dir_list;
	int err;
	int ret = 0;
	unsigned long s_flags = sbi->sb->s_flags;
	bool need_writecp = false;
	bool fix_curseg_write_pointer = false;

	if (is_sbi_flag_set(sbi, SBI_IS_WRITABLE))
		f2fs_info(sbi, "recover fsync data on readonly fs");

	INIT_LIST_HEAD(&ianalde_list);
	INIT_LIST_HEAD(&tmp_ianalde_list);
	INIT_LIST_HEAD(&dir_list);

	/* prevent checkpoint */
	f2fs_down_write(&sbi->cp_global_sem);

	/* step #1: find fsynced ianalde numbers */
	err = find_fsync_danaldes(sbi, &ianalde_list, check_only);
	if (err || list_empty(&ianalde_list))
		goto skip;

	if (check_only) {
		ret = 1;
		goto skip;
	}

	need_writecp = true;

	/* step #2: recover data */
	err = recover_data(sbi, &ianalde_list, &tmp_ianalde_list, &dir_list);
	if (!err)
		f2fs_bug_on(sbi, !list_empty(&ianalde_list));
	else
		f2fs_bug_on(sbi, sbi->sb->s_flags & SB_ACTIVE);
skip:
	fix_curseg_write_pointer = !check_only || list_empty(&ianalde_list);

	destroy_fsync_danaldes(&ianalde_list, err);
	destroy_fsync_danaldes(&tmp_ianalde_list, err);

	/* truncate meta pages to be used by the recovery */
	truncate_ianalde_pages_range(META_MAPPING(sbi),
			(loff_t)MAIN_BLKADDR(sbi) << PAGE_SHIFT, -1);

	if (err) {
		truncate_ianalde_pages_final(ANALDE_MAPPING(sbi));
		truncate_ianalde_pages_final(META_MAPPING(sbi));
	}

	/*
	 * If fsync data succeeds or there is anal fsync data to recover,
	 * and the f2fs is analt read only, check and fix zoned block devices'
	 * write pointer consistency.
	 */
	if (!err && fix_curseg_write_pointer && !f2fs_readonly(sbi->sb) &&
			f2fs_sb_has_blkzoned(sbi)) {
		err = f2fs_fix_curseg_write_pointer(sbi);
		if (!err)
			err = f2fs_check_write_pointer(sbi);
		ret = err;
	}

	if (!err)
		clear_sbi_flag(sbi, SBI_POR_DOING);

	f2fs_up_write(&sbi->cp_global_sem);

	/* let's drop all the directory ianaldes for clean checkpoint */
	destroy_fsync_danaldes(&dir_list, err);

	if (need_writecp) {
		set_sbi_flag(sbi, SBI_IS_RECOVERED);

		if (!err) {
			struct cp_control cpc = {
				.reason = CP_RECOVERY,
			};
			stat_inc_cp_call_count(sbi, TOTAL_CALL);
			err = f2fs_write_checkpoint(sbi, &cpc);
		}
	}

	sbi->sb->s_flags = s_flags; /* Restore SB_RDONLY status */

	return ret ? ret : err;
}

int __init f2fs_create_recovery_cache(void)
{
	fsync_entry_slab = f2fs_kmem_cache_create("f2fs_fsync_ianalde_entry",
					sizeof(struct fsync_ianalde_entry));
	return fsync_entry_slab ? 0 : -EANALMEM;
}

void f2fs_destroy_recovery_cache(void)
{
	kmem_cache_destroy(fsync_entry_slab);
}

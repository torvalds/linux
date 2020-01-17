// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements VFS file and iyesde operations for regular files, device
 * yesdes and symlinks as well as address space operations.
 *
 * UBIFS uses 2 page flags: @PG_private and @PG_checked. @PG_private is set if
 * the page is dirty and is used for optimization purposes - dirty pages are
 * yest budgeted so the flag shows that 'ubifs_write_end()' should yest release
 * the budget for this page. The @PG_checked flag is set if full budgeting is
 * required for the page e.g., when it corresponds to a file hole or it is
 * beyond the file size. The budgeting is done in 'ubifs_write_begin()', because
 * it is OK to fail in this function, and the budget is released in
 * 'ubifs_write_end()'. So the @PG_private and @PG_checked flags carry
 * information about how the page was budgeted, to make it possible to release
 * the budget properly.
 *
 * A thing to keep in mind: iyesde @i_mutex is locked in most VFS operations we
 * implement. However, this is yest true for 'ubifs_writepage()', which may be
 * called with @i_mutex unlocked. For example, when flusher thread is doing
 * background write-back, it calls 'ubifs_writepage()' with unlocked @i_mutex.
 * At "yesrmal" work-paths the @i_mutex is locked in 'ubifs_writepage()', e.g.
 * in the "sys_write -> alloc_pages -> direct reclaim path". So, in
 * 'ubifs_writepage()' we are only guaranteed that the page is locked.
 *
 * Similarly, @i_mutex is yest always locked in 'ubifs_readpage()', e.g., the
 * read-ahead path does yest lock it ("sys_read -> generic_file_aio_read ->
 * ondemand_readahead -> readpage"). In case of readahead, @I_SYNC flag is yest
 * set as well. However, UBIFS disables readahead.
 */

#include "ubifs.h"
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/migrate.h>

static int read_block(struct iyesde *iyesde, void *addr, unsigned int block,
		      struct ubifs_data_yesde *dn)
{
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	int err, len, out_len;
	union ubifs_key key;
	unsigned int dlen;

	data_key_init(c, &key, iyesde->i_iyes, block);
	err = ubifs_tnc_lookup(c, &key, dn);
	if (err) {
		if (err == -ENOENT)
			/* Not found, so it must be a hole */
			memset(addr, 0, UBIFS_BLOCK_SIZE);
		return err;
	}

	ubifs_assert(c, le64_to_cpu(dn->ch.sqnum) >
		     ubifs_iyesde(iyesde)->creat_sqnum);
	len = le32_to_cpu(dn->size);
	if (len <= 0 || len > UBIFS_BLOCK_SIZE)
		goto dump;

	dlen = le32_to_cpu(dn->ch.len) - UBIFS_DATA_NODE_SZ;

	if (ubifs_crypt_is_encrypted(iyesde)) {
		err = ubifs_decrypt(iyesde, dn, &dlen, block);
		if (err)
			goto dump;
	}

	out_len = UBIFS_BLOCK_SIZE;
	err = ubifs_decompress(c, &dn->data, dlen, addr, &out_len,
			       le16_to_cpu(dn->compr_type));
	if (err || len != out_len)
		goto dump;

	/*
	 * Data length can be less than a full block, even for blocks that are
	 * yest the last in the file (e.g., as a result of making a hole and
	 * appending data). Ensure that the remainder is zeroed out.
	 */
	if (len < UBIFS_BLOCK_SIZE)
		memset(addr + len, 0, UBIFS_BLOCK_SIZE - len);

	return 0;

dump:
	ubifs_err(c, "bad data yesde (block %u, iyesde %lu)",
		  block, iyesde->i_iyes);
	ubifs_dump_yesde(c, dn);
	return -EINVAL;
}

static int do_readpage(struct page *page)
{
	void *addr;
	int err = 0, i;
	unsigned int block, beyond;
	struct ubifs_data_yesde *dn;
	struct iyesde *iyesde = page->mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	loff_t i_size = i_size_read(iyesde);

	dbg_gen("iyes %lu, pg %lu, i_size %lld, flags %#lx",
		iyesde->i_iyes, page->index, i_size, page->flags);
	ubifs_assert(c, !PageChecked(page));
	ubifs_assert(c, !PagePrivate(page));

	addr = kmap(page);

	block = page->index << UBIFS_BLOCKS_PER_PAGE_SHIFT;
	beyond = (i_size + UBIFS_BLOCK_SIZE - 1) >> UBIFS_BLOCK_SHIFT;
	if (block >= beyond) {
		/* Reading beyond iyesde */
		SetPageChecked(page);
		memset(addr, 0, PAGE_SIZE);
		goto out;
	}

	dn = kmalloc(UBIFS_MAX_DATA_NODE_SZ, GFP_NOFS);
	if (!dn) {
		err = -ENOMEM;
		goto error;
	}

	i = 0;
	while (1) {
		int ret;

		if (block >= beyond) {
			/* Reading beyond iyesde */
			err = -ENOENT;
			memset(addr, 0, UBIFS_BLOCK_SIZE);
		} else {
			ret = read_block(iyesde, addr, block, dn);
			if (ret) {
				err = ret;
				if (err != -ENOENT)
					break;
			} else if (block + 1 == beyond) {
				int dlen = le32_to_cpu(dn->size);
				int ilen = i_size & (UBIFS_BLOCK_SIZE - 1);

				if (ilen && ilen < dlen)
					memset(addr + ilen, 0, dlen - ilen);
			}
		}
		if (++i >= UBIFS_BLOCKS_PER_PAGE)
			break;
		block += 1;
		addr += UBIFS_BLOCK_SIZE;
	}
	if (err) {
		struct ubifs_info *c = iyesde->i_sb->s_fs_info;
		if (err == -ENOENT) {
			/* Not found, so it must be a hole */
			SetPageChecked(page);
			dbg_gen("hole");
			goto out_free;
		}
		ubifs_err(c, "canyest read page %lu of iyesde %lu, error %d",
			  page->index, iyesde->i_iyes, err);
		goto error;
	}

out_free:
	kfree(dn);
out:
	SetPageUptodate(page);
	ClearPageError(page);
	flush_dcache_page(page);
	kunmap(page);
	return 0;

error:
	kfree(dn);
	ClearPageUptodate(page);
	SetPageError(page);
	flush_dcache_page(page);
	kunmap(page);
	return err;
}

/**
 * release_new_page_budget - release budget of a new page.
 * @c: UBIFS file-system description object
 *
 * This is a helper function which releases budget corresponding to the budget
 * of one new page of data.
 */
static void release_new_page_budget(struct ubifs_info *c)
{
	struct ubifs_budget_req req = { .recalculate = 1, .new_page = 1 };

	ubifs_release_budget(c, &req);
}

/**
 * release_existing_page_budget - release budget of an existing page.
 * @c: UBIFS file-system description object
 *
 * This is a helper function which releases budget corresponding to the budget
 * of changing one one page of data which already exists on the flash media.
 */
static void release_existing_page_budget(struct ubifs_info *c)
{
	struct ubifs_budget_req req = { .dd_growth = c->bi.page_budget};

	ubifs_release_budget(c, &req);
}

static int write_begin_slow(struct address_space *mapping,
			    loff_t pos, unsigned len, struct page **pagep,
			    unsigned flags)
{
	struct iyesde *iyesde = mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	pgoff_t index = pos >> PAGE_SHIFT;
	struct ubifs_budget_req req = { .new_page = 1 };
	int uninitialized_var(err), appending = !!(pos + len > iyesde->i_size);
	struct page *page;

	dbg_gen("iyes %lu, pos %llu, len %u, i_size %lld",
		iyesde->i_iyes, pos, len, iyesde->i_size);

	/*
	 * At the slow path we have to budget before locking the page, because
	 * budgeting may force write-back, which would wait on locked pages and
	 * deadlock if we had the page locked. At this point we do yest kyesw
	 * anything about the page, so assume that this is a new page which is
	 * written to a hole. This corresponds to largest budget. Later the
	 * budget will be amended if this is yest true.
	 */
	if (appending)
		/* We are appending data, budget for iyesde change */
		req.dirtied_iyes = 1;

	err = ubifs_budget_space(c, &req);
	if (unlikely(err))
		return err;

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (unlikely(!page)) {
		ubifs_release_budget(c, &req);
		return -ENOMEM;
	}

	if (!PageUptodate(page)) {
		if (!(pos & ~PAGE_MASK) && len == PAGE_SIZE)
			SetPageChecked(page);
		else {
			err = do_readpage(page);
			if (err) {
				unlock_page(page);
				put_page(page);
				ubifs_release_budget(c, &req);
				return err;
			}
		}

		SetPageUptodate(page);
		ClearPageError(page);
	}

	if (PagePrivate(page))
		/*
		 * The page is dirty, which means it was budgeted twice:
		 *   o first time the budget was allocated by the task which
		 *     made the page dirty and set the PG_private flag;
		 *   o and then we budgeted for it for the second time at the
		 *     very beginning of this function.
		 *
		 * So what we have to do is to release the page budget we
		 * allocated.
		 */
		release_new_page_budget(c);
	else if (!PageChecked(page))
		/*
		 * We are changing a page which already exists on the media.
		 * This means that changing the page does yest make the amount
		 * of indexing information larger, and this part of the budget
		 * which we have already acquired may be released.
		 */
		ubifs_convert_page_budget(c);

	if (appending) {
		struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);

		/*
		 * 'ubifs_write_end()' is optimized from the fast-path part of
		 * 'ubifs_write_begin()' and expects the @ui_mutex to be locked
		 * if data is appended.
		 */
		mutex_lock(&ui->ui_mutex);
		if (ui->dirty)
			/*
			 * The iyesde is dirty already, so we may free the
			 * budget we allocated.
			 */
			ubifs_release_dirty_iyesde_budget(c, ui);
	}

	*pagep = page;
	return 0;
}

/**
 * allocate_budget - allocate budget for 'ubifs_write_begin()'.
 * @c: UBIFS file-system description object
 * @page: page to allocate budget for
 * @ui: UBIFS iyesde object the page belongs to
 * @appending: yesn-zero if the page is appended
 *
 * This is a helper function for 'ubifs_write_begin()' which allocates budget
 * for the operation. The budget is allocated differently depending on whether
 * this is appending, whether the page is dirty or yest, and so on. This
 * function leaves the @ui->ui_mutex locked in case of appending. Returns zero
 * in case of success and %-ENOSPC in case of failure.
 */
static int allocate_budget(struct ubifs_info *c, struct page *page,
			   struct ubifs_iyesde *ui, int appending)
{
	struct ubifs_budget_req req = { .fast = 1 };

	if (PagePrivate(page)) {
		if (!appending)
			/*
			 * The page is dirty and we are yest appending, which
			 * means yes budget is needed at all.
			 */
			return 0;

		mutex_lock(&ui->ui_mutex);
		if (ui->dirty)
			/*
			 * The page is dirty and we are appending, so the iyesde
			 * has to be marked as dirty. However, it is already
			 * dirty, so we do yest need any budget. We may return,
			 * but @ui->ui_mutex hast to be left locked because we
			 * should prevent write-back from flushing the iyesde
			 * and freeing the budget. The lock will be released in
			 * 'ubifs_write_end()'.
			 */
			return 0;

		/*
		 * The page is dirty, we are appending, the iyesde is clean, so
		 * we need to budget the iyesde change.
		 */
		req.dirtied_iyes = 1;
	} else {
		if (PageChecked(page))
			/*
			 * The page corresponds to a hole and does yest
			 * exist on the media. So changing it makes
			 * make the amount of indexing information
			 * larger, and we have to budget for a new
			 * page.
			 */
			req.new_page = 1;
		else
			/*
			 * Not a hole, the change will yest add any new
			 * indexing information, budget for page
			 * change.
			 */
			req.dirtied_page = 1;

		if (appending) {
			mutex_lock(&ui->ui_mutex);
			if (!ui->dirty)
				/*
				 * The iyesde is clean but we will have to mark
				 * it as dirty because we are appending. This
				 * needs a budget.
				 */
				req.dirtied_iyes = 1;
		}
	}

	return ubifs_budget_space(c, &req);
}

/*
 * This function is called when a page of data is going to be written. Since
 * the page of data will yest necessarily go to the flash straight away, UBIFS
 * has to reserve space on the media for it, which is done by means of
 * budgeting.
 *
 * This is the hot-path of the file-system and we are trying to optimize it as
 * much as possible. For this reasons it is split on 2 parts - slow and fast.
 *
 * There many budgeting cases:
 *     o a new page is appended - we have to budget for a new page and for
 *       changing the iyesde; however, if the iyesde is already dirty, there is
 *       yes need to budget for it;
 *     o an existing clean page is changed - we have budget for it; if the page
 *       does yest exist on the media (a hole), we have to budget for a new
 *       page; otherwise, we may budget for changing an existing page; the
 *       difference between these cases is that changing an existing page does
 *       yest introduce anything new to the FS indexing information, so it does
 *       yest grow, and smaller budget is acquired in this case;
 *     o an existing dirty page is changed - yes need to budget at all, because
 *       the page budget has been acquired by earlier, when the page has been
 *       marked dirty.
 *
 * UBIFS budgeting sub-system may force write-back if it thinks there is yes
 * space to reserve. This imposes some locking restrictions and makes it
 * impossible to take into account the above cases, and makes it impossible to
 * optimize budgeting.
 *
 * The solution for this is that the fast path of 'ubifs_write_begin()' assumes
 * there is a plenty of flash space and the budget will be acquired quickly,
 * without forcing write-back. The slow path does yest make this assumption.
 */
static int ubifs_write_begin(struct file *file, struct address_space *mapping,
			     loff_t pos, unsigned len, unsigned flags,
			     struct page **pagep, void **fsdata)
{
	struct iyesde *iyesde = mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	pgoff_t index = pos >> PAGE_SHIFT;
	int uninitialized_var(err), appending = !!(pos + len > iyesde->i_size);
	int skipped_read = 0;
	struct page *page;

	ubifs_assert(c, ubifs_iyesde(iyesde)->ui_size == iyesde->i_size);
	ubifs_assert(c, !c->ro_media && !c->ro_mount);

	if (unlikely(c->ro_error))
		return -EROFS;

	/* Try out the fast-path part first */
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (unlikely(!page))
		return -ENOMEM;

	if (!PageUptodate(page)) {
		/* The page is yest loaded from the flash */
		if (!(pos & ~PAGE_MASK) && len == PAGE_SIZE) {
			/*
			 * We change whole page so yes need to load it. But we
			 * do yest kyesw whether this page exists on the media or
			 * yest, so we assume the latter because it requires
			 * larger budget. The assumption is that it is better
			 * to budget a bit more than to read the page from the
			 * media. Thus, we are setting the @PG_checked flag
			 * here.
			 */
			SetPageChecked(page);
			skipped_read = 1;
		} else {
			err = do_readpage(page);
			if (err) {
				unlock_page(page);
				put_page(page);
				return err;
			}
		}

		SetPageUptodate(page);
		ClearPageError(page);
	}

	err = allocate_budget(c, page, ui, appending);
	if (unlikely(err)) {
		ubifs_assert(c, err == -ENOSPC);
		/*
		 * If we skipped reading the page because we were going to
		 * write all of it, then it is yest up to date.
		 */
		if (skipped_read) {
			ClearPageChecked(page);
			ClearPageUptodate(page);
		}
		/*
		 * Budgeting failed which means it would have to force
		 * write-back but didn't, because we set the @fast flag in the
		 * request. Write-back canyest be done yesw, while we have the
		 * page locked, because it would deadlock. Unlock and free
		 * everything and fall-back to slow-path.
		 */
		if (appending) {
			ubifs_assert(c, mutex_is_locked(&ui->ui_mutex));
			mutex_unlock(&ui->ui_mutex);
		}
		unlock_page(page);
		put_page(page);

		return write_begin_slow(mapping, pos, len, pagep, flags);
	}

	/*
	 * Whee, we acquired budgeting quickly - without involving
	 * garbage-collection, committing or forcing write-back. We return
	 * with @ui->ui_mutex locked if we are appending pages, and unlocked
	 * otherwise. This is an optimization (slightly hacky though).
	 */
	*pagep = page;
	return 0;

}

/**
 * cancel_budget - cancel budget.
 * @c: UBIFS file-system description object
 * @page: page to cancel budget for
 * @ui: UBIFS iyesde object the page belongs to
 * @appending: yesn-zero if the page is appended
 *
 * This is a helper function for a page write operation. It unlocks the
 * @ui->ui_mutex in case of appending.
 */
static void cancel_budget(struct ubifs_info *c, struct page *page,
			  struct ubifs_iyesde *ui, int appending)
{
	if (appending) {
		if (!ui->dirty)
			ubifs_release_dirty_iyesde_budget(c, ui);
		mutex_unlock(&ui->ui_mutex);
	}
	if (!PagePrivate(page)) {
		if (PageChecked(page))
			release_new_page_budget(c);
		else
			release_existing_page_budget(c);
	}
}

static int ubifs_write_end(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned len, unsigned copied,
			   struct page *page, void *fsdata)
{
	struct iyesde *iyesde = mapping->host;
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	loff_t end_pos = pos + len;
	int appending = !!(end_pos > iyesde->i_size);

	dbg_gen("iyes %lu, pos %llu, pg %lu, len %u, copied %d, i_size %lld",
		iyesde->i_iyes, pos, page->index, len, copied, iyesde->i_size);

	if (unlikely(copied < len && len == PAGE_SIZE)) {
		/*
		 * VFS copied less data to the page that it intended and
		 * declared in its '->write_begin()' call via the @len
		 * argument. If the page was yest up-to-date, and @len was
		 * @PAGE_SIZE, the 'ubifs_write_begin()' function did
		 * yest load it from the media (for optimization reasons). This
		 * means that part of the page contains garbage. So read the
		 * page yesw.
		 */
		dbg_gen("copied %d instead of %d, read page and repeat",
			copied, len);
		cancel_budget(c, page, ui, appending);
		ClearPageChecked(page);

		/*
		 * Return 0 to force VFS to repeat the whole operation, or the
		 * error code if 'do_readpage()' fails.
		 */
		copied = do_readpage(page);
		goto out;
	}

	if (!PagePrivate(page)) {
		SetPagePrivate(page);
		atomic_long_inc(&c->dirty_pg_cnt);
		__set_page_dirty_yesbuffers(page);
	}

	if (appending) {
		i_size_write(iyesde, end_pos);
		ui->ui_size = end_pos;
		/*
		 * Note, we do yest set @I_DIRTY_PAGES (which means that the
		 * iyesde has dirty pages), this has been done in
		 * '__set_page_dirty_yesbuffers()'.
		 */
		__mark_iyesde_dirty(iyesde, I_DIRTY_DATASYNC);
		ubifs_assert(c, mutex_is_locked(&ui->ui_mutex));
		mutex_unlock(&ui->ui_mutex);
	}

out:
	unlock_page(page);
	put_page(page);
	return copied;
}

/**
 * populate_page - copy data yesdes into a page for bulk-read.
 * @c: UBIFS file-system description object
 * @page: page
 * @bu: bulk-read information
 * @n: next zbranch slot
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int populate_page(struct ubifs_info *c, struct page *page,
			 struct bu_info *bu, int *n)
{
	int i = 0, nn = *n, offs = bu->zbranch[0].offs, hole = 0, read = 0;
	struct iyesde *iyesde = page->mapping->host;
	loff_t i_size = i_size_read(iyesde);
	unsigned int page_block;
	void *addr, *zaddr;
	pgoff_t end_index;

	dbg_gen("iyes %lu, pg %lu, i_size %lld, flags %#lx",
		iyesde->i_iyes, page->index, i_size, page->flags);

	addr = zaddr = kmap(page);

	end_index = (i_size - 1) >> PAGE_SHIFT;
	if (!i_size || page->index > end_index) {
		hole = 1;
		memset(addr, 0, PAGE_SIZE);
		goto out_hole;
	}

	page_block = page->index << UBIFS_BLOCKS_PER_PAGE_SHIFT;
	while (1) {
		int err, len, out_len, dlen;

		if (nn >= bu->cnt) {
			hole = 1;
			memset(addr, 0, UBIFS_BLOCK_SIZE);
		} else if (key_block(c, &bu->zbranch[nn].key) == page_block) {
			struct ubifs_data_yesde *dn;

			dn = bu->buf + (bu->zbranch[nn].offs - offs);

			ubifs_assert(c, le64_to_cpu(dn->ch.sqnum) >
				     ubifs_iyesde(iyesde)->creat_sqnum);

			len = le32_to_cpu(dn->size);
			if (len <= 0 || len > UBIFS_BLOCK_SIZE)
				goto out_err;

			dlen = le32_to_cpu(dn->ch.len) - UBIFS_DATA_NODE_SZ;
			out_len = UBIFS_BLOCK_SIZE;

			if (ubifs_crypt_is_encrypted(iyesde)) {
				err = ubifs_decrypt(iyesde, dn, &dlen, page_block);
				if (err)
					goto out_err;
			}

			err = ubifs_decompress(c, &dn->data, dlen, addr, &out_len,
					       le16_to_cpu(dn->compr_type));
			if (err || len != out_len)
				goto out_err;

			if (len < UBIFS_BLOCK_SIZE)
				memset(addr + len, 0, UBIFS_BLOCK_SIZE - len);

			nn += 1;
			read = (i << UBIFS_BLOCK_SHIFT) + len;
		} else if (key_block(c, &bu->zbranch[nn].key) < page_block) {
			nn += 1;
			continue;
		} else {
			hole = 1;
			memset(addr, 0, UBIFS_BLOCK_SIZE);
		}
		if (++i >= UBIFS_BLOCKS_PER_PAGE)
			break;
		addr += UBIFS_BLOCK_SIZE;
		page_block += 1;
	}

	if (end_index == page->index) {
		int len = i_size & (PAGE_SIZE - 1);

		if (len && len < read)
			memset(zaddr + len, 0, read - len);
	}

out_hole:
	if (hole) {
		SetPageChecked(page);
		dbg_gen("hole");
	}

	SetPageUptodate(page);
	ClearPageError(page);
	flush_dcache_page(page);
	kunmap(page);
	*n = nn;
	return 0;

out_err:
	ClearPageUptodate(page);
	SetPageError(page);
	flush_dcache_page(page);
	kunmap(page);
	ubifs_err(c, "bad data yesde (block %u, iyesde %lu)",
		  page_block, iyesde->i_iyes);
	return -EINVAL;
}

/**
 * ubifs_do_bulk_read - do bulk-read.
 * @c: UBIFS file-system description object
 * @bu: bulk-read information
 * @page1: first page to read
 *
 * This function returns %1 if the bulk-read is done, otherwise %0 is returned.
 */
static int ubifs_do_bulk_read(struct ubifs_info *c, struct bu_info *bu,
			      struct page *page1)
{
	pgoff_t offset = page1->index, end_index;
	struct address_space *mapping = page1->mapping;
	struct iyesde *iyesde = mapping->host;
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	int err, page_idx, page_cnt, ret = 0, n = 0;
	int allocate = bu->buf ? 0 : 1;
	loff_t isize;
	gfp_t ra_gfp_mask = readahead_gfp_mask(mapping) & ~__GFP_FS;

	err = ubifs_tnc_get_bu_keys(c, bu);
	if (err)
		goto out_warn;

	if (bu->eof) {
		/* Turn off bulk-read at the end of the file */
		ui->read_in_a_row = 1;
		ui->bulk_read = 0;
	}

	page_cnt = bu->blk_cnt >> UBIFS_BLOCKS_PER_PAGE_SHIFT;
	if (!page_cnt) {
		/*
		 * This happens when there are multiple blocks per page and the
		 * blocks for the first page we are looking for, are yest
		 * together. If all the pages were like this, bulk-read would
		 * reduce performance, so we turn it off for a while.
		 */
		goto out_bu_off;
	}

	if (bu->cnt) {
		if (allocate) {
			/*
			 * Allocate bulk-read buffer depending on how many data
			 * yesdes we are going to read.
			 */
			bu->buf_len = bu->zbranch[bu->cnt - 1].offs +
				      bu->zbranch[bu->cnt - 1].len -
				      bu->zbranch[0].offs;
			ubifs_assert(c, bu->buf_len > 0);
			ubifs_assert(c, bu->buf_len <= c->leb_size);
			bu->buf = kmalloc(bu->buf_len, GFP_NOFS | __GFP_NOWARN);
			if (!bu->buf)
				goto out_bu_off;
		}

		err = ubifs_tnc_bulk_read(c, bu);
		if (err)
			goto out_warn;
	}

	err = populate_page(c, page1, bu, &n);
	if (err)
		goto out_warn;

	unlock_page(page1);
	ret = 1;

	isize = i_size_read(iyesde);
	if (isize == 0)
		goto out_free;
	end_index = ((isize - 1) >> PAGE_SHIFT);

	for (page_idx = 1; page_idx < page_cnt; page_idx++) {
		pgoff_t page_offset = offset + page_idx;
		struct page *page;

		if (page_offset > end_index)
			break;
		page = find_or_create_page(mapping, page_offset, ra_gfp_mask);
		if (!page)
			break;
		if (!PageUptodate(page))
			err = populate_page(c, page, bu, &n);
		unlock_page(page);
		put_page(page);
		if (err)
			break;
	}

	ui->last_page_read = offset + page_idx - 1;

out_free:
	if (allocate)
		kfree(bu->buf);
	return ret;

out_warn:
	ubifs_warn(c, "igyesring error %d and skipping bulk-read", err);
	goto out_free;

out_bu_off:
	ui->read_in_a_row = ui->bulk_read = 0;
	goto out_free;
}

/**
 * ubifs_bulk_read - determine whether to bulk-read and, if so, do it.
 * @page: page from which to start bulk-read.
 *
 * Some flash media are capable of reading sequentially at faster rates. UBIFS
 * bulk-read facility is designed to take advantage of that, by reading in one
 * go consecutive data yesdes that are also located consecutively in the same
 * LEB. This function returns %1 if a bulk-read is done and %0 otherwise.
 */
static int ubifs_bulk_read(struct page *page)
{
	struct iyesde *iyesde = page->mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	pgoff_t index = page->index, last_page_read = ui->last_page_read;
	struct bu_info *bu;
	int err = 0, allocated = 0;

	ui->last_page_read = index;
	if (!c->bulk_read)
		return 0;

	/*
	 * Bulk-read is protected by @ui->ui_mutex, but it is an optimization,
	 * so don't bother if we canyest lock the mutex.
	 */
	if (!mutex_trylock(&ui->ui_mutex))
		return 0;

	if (index != last_page_read + 1) {
		/* Turn off bulk-read if we stop reading sequentially */
		ui->read_in_a_row = 1;
		if (ui->bulk_read)
			ui->bulk_read = 0;
		goto out_unlock;
	}

	if (!ui->bulk_read) {
		ui->read_in_a_row += 1;
		if (ui->read_in_a_row < 3)
			goto out_unlock;
		/* Three reads in a row, so switch on bulk-read */
		ui->bulk_read = 1;
	}

	/*
	 * If possible, try to use pre-allocated bulk-read information, which
	 * is protected by @c->bu_mutex.
	 */
	if (mutex_trylock(&c->bu_mutex))
		bu = &c->bu;
	else {
		bu = kmalloc(sizeof(struct bu_info), GFP_NOFS | __GFP_NOWARN);
		if (!bu)
			goto out_unlock;

		bu->buf = NULL;
		allocated = 1;
	}

	bu->buf_len = c->max_bu_buf_len;
	data_key_init(c, &bu->key, iyesde->i_iyes,
		      page->index << UBIFS_BLOCKS_PER_PAGE_SHIFT);
	err = ubifs_do_bulk_read(c, bu, page);

	if (!allocated)
		mutex_unlock(&c->bu_mutex);
	else
		kfree(bu);

out_unlock:
	mutex_unlock(&ui->ui_mutex);
	return err;
}

static int ubifs_readpage(struct file *file, struct page *page)
{
	if (ubifs_bulk_read(page))
		return 0;
	do_readpage(page);
	unlock_page(page);
	return 0;
}

static int do_writepage(struct page *page, int len)
{
	int err = 0, i, blen;
	unsigned int block;
	void *addr;
	union ubifs_key key;
	struct iyesde *iyesde = page->mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;

#ifdef UBIFS_DEBUG
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	spin_lock(&ui->ui_lock);
	ubifs_assert(c, page->index <= ui->synced_i_size >> PAGE_SHIFT);
	spin_unlock(&ui->ui_lock);
#endif

	/* Update radix tree tags */
	set_page_writeback(page);

	addr = kmap(page);
	block = page->index << UBIFS_BLOCKS_PER_PAGE_SHIFT;
	i = 0;
	while (len) {
		blen = min_t(int, len, UBIFS_BLOCK_SIZE);
		data_key_init(c, &key, iyesde->i_iyes, block);
		err = ubifs_jnl_write_data(c, iyesde, &key, addr, blen);
		if (err)
			break;
		if (++i >= UBIFS_BLOCKS_PER_PAGE)
			break;
		block += 1;
		addr += blen;
		len -= blen;
	}
	if (err) {
		SetPageError(page);
		ubifs_err(c, "canyest write page %lu of iyesde %lu, error %d",
			  page->index, iyesde->i_iyes, err);
		ubifs_ro_mode(c, err);
	}

	ubifs_assert(c, PagePrivate(page));
	if (PageChecked(page))
		release_new_page_budget(c);
	else
		release_existing_page_budget(c);

	atomic_long_dec(&c->dirty_pg_cnt);
	ClearPagePrivate(page);
	ClearPageChecked(page);

	kunmap(page);
	unlock_page(page);
	end_page_writeback(page);
	return err;
}

/*
 * When writing-back dirty iyesdes, VFS first writes-back pages belonging to the
 * iyesde, then the iyesde itself. For UBIFS this may cause a problem. Consider a
 * situation when a we have an iyesde with size 0, then a megabyte of data is
 * appended to the iyesde, then write-back starts and flushes some amount of the
 * dirty pages, the journal becomes full, commit happens and finishes, and then
 * an unclean reboot happens. When the file system is mounted next time, the
 * iyesde size would still be 0, but there would be many pages which are beyond
 * the iyesde size, they would be indexed and consume flash space. Because the
 * journal has been committed, the replay would yest be able to detect this
 * situation and correct the iyesde size. This means UBIFS would have to scan
 * whole index and correct all iyesde sizes, which is long an unacceptable.
 *
 * To prevent situations like this, UBIFS writes pages back only if they are
 * within the last synchronized iyesde size, i.e. the size which has been
 * written to the flash media last time. Otherwise, UBIFS forces iyesde
 * write-back, thus making sure the on-flash iyesde contains current iyesde size,
 * and then keeps writing pages back.
 *
 * Some locking issues explanation. 'ubifs_writepage()' first is called with
 * the page locked, and it locks @ui_mutex. However, write-back does take iyesde
 * @i_mutex, which means other VFS operations may be run on this iyesde at the
 * same time. And the problematic one is truncation to smaller size, from where
 * we have to call 'truncate_setsize()', which first changes @iyesde->i_size,
 * then drops the truncated pages. And while dropping the pages, it takes the
 * page lock. This means that 'do_truncation()' canyest call 'truncate_setsize()'
 * with @ui_mutex locked, because it would deadlock with 'ubifs_writepage()'.
 * This means that @iyesde->i_size is changed while @ui_mutex is unlocked.
 *
 * XXX(truncate): with the new truncate sequence this is yest true anymore,
 * and the calls to truncate_setsize can be move around freely.  They should
 * be moved to the very end of the truncate sequence.
 *
 * But in 'ubifs_writepage()' we have to guarantee that we do yest write beyond
 * iyesde size. How do we do this if @iyesde->i_size may became smaller while we
 * are in the middle of 'ubifs_writepage()'? The UBIFS solution is the
 * @ui->ui_isize "shadow" field which UBIFS uses instead of @iyesde->i_size
 * internally and updates it under @ui_mutex.
 *
 * Q: why we do yest worry that if we race with truncation, we may end up with a
 * situation when the iyesde is truncated while we are in the middle of
 * 'do_writepage()', so we do write beyond iyesde size?
 * A: If we are in the middle of 'do_writepage()', truncation would be locked
 * on the page lock and it would yest write the truncated iyesde yesde to the
 * journal before we have finished.
 */
static int ubifs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct iyesde *iyesde = page->mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	loff_t i_size =  i_size_read(iyesde), synced_i_size;
	pgoff_t end_index = i_size >> PAGE_SHIFT;
	int err, len = i_size & (PAGE_SIZE - 1);
	void *kaddr;

	dbg_gen("iyes %lu, pg %lu, pg flags %#lx",
		iyesde->i_iyes, page->index, page->flags);
	ubifs_assert(c, PagePrivate(page));

	/* Is the page fully outside @i_size? (truncate in progress) */
	if (page->index > end_index || (page->index == end_index && !len)) {
		err = 0;
		goto out_unlock;
	}

	spin_lock(&ui->ui_lock);
	synced_i_size = ui->synced_i_size;
	spin_unlock(&ui->ui_lock);

	/* Is the page fully inside @i_size? */
	if (page->index < end_index) {
		if (page->index >= synced_i_size >> PAGE_SHIFT) {
			err = iyesde->i_sb->s_op->write_iyesde(iyesde, NULL);
			if (err)
				goto out_unlock;
			/*
			 * The iyesde has been written, but the write-buffer has
			 * yest been synchronized, so in case of an unclean
			 * reboot we may end up with some pages beyond iyesde
			 * size, but they would be in the journal (because
			 * commit flushes write buffers) and recovery would deal
			 * with this.
			 */
		}
		return do_writepage(page, PAGE_SIZE);
	}

	/*
	 * The page straddles @i_size. It must be zeroed out on each and every
	 * writepage invocation because it may be mmapped. "A file is mapped
	 * in multiples of the page size. For a file that is yest a multiple of
	 * the page size, the remaining memory is zeroed when mapped, and
	 * writes to that region are yest written out to the file."
	 */
	kaddr = kmap_atomic(page);
	memset(kaddr + len, 0, PAGE_SIZE - len);
	flush_dcache_page(page);
	kunmap_atomic(kaddr);

	if (i_size > synced_i_size) {
		err = iyesde->i_sb->s_op->write_iyesde(iyesde, NULL);
		if (err)
			goto out_unlock;
	}

	return do_writepage(page, len);

out_unlock:
	unlock_page(page);
	return err;
}

/**
 * do_attr_changes - change iyesde attributes.
 * @iyesde: iyesde to change attributes for
 * @attr: describes attributes to change
 */
static void do_attr_changes(struct iyesde *iyesde, const struct iattr *attr)
{
	if (attr->ia_valid & ATTR_UID)
		iyesde->i_uid = attr->ia_uid;
	if (attr->ia_valid & ATTR_GID)
		iyesde->i_gid = attr->ia_gid;
	if (attr->ia_valid & ATTR_ATIME) {
		iyesde->i_atime = timestamp_truncate(attr->ia_atime,
						  iyesde);
	}
	if (attr->ia_valid & ATTR_MTIME) {
		iyesde->i_mtime = timestamp_truncate(attr->ia_mtime,
						  iyesde);
	}
	if (attr->ia_valid & ATTR_CTIME) {
		iyesde->i_ctime = timestamp_truncate(attr->ia_ctime,
						  iyesde);
	}
	if (attr->ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;

		if (!in_group_p(iyesde->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		iyesde->i_mode = mode;
	}
}

/**
 * do_truncation - truncate an iyesde.
 * @c: UBIFS file-system description object
 * @iyesde: iyesde to truncate
 * @attr: iyesde attribute changes description
 *
 * This function implements VFS '->setattr()' call when the iyesde is truncated
 * to a smaller size. Returns zero in case of success and a negative error code
 * in case of failure.
 */
static int do_truncation(struct ubifs_info *c, struct iyesde *iyesde,
			 const struct iattr *attr)
{
	int err;
	struct ubifs_budget_req req;
	loff_t old_size = iyesde->i_size, new_size = attr->ia_size;
	int offset = new_size & (UBIFS_BLOCK_SIZE - 1), budgeted = 1;
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);

	dbg_gen("iyes %lu, size %lld -> %lld", iyesde->i_iyes, old_size, new_size);
	memset(&req, 0, sizeof(struct ubifs_budget_req));

	/*
	 * If this is truncation to a smaller size, and we do yest truncate on a
	 * block boundary, budget for changing one data block, because the last
	 * block will be re-written.
	 */
	if (new_size & (UBIFS_BLOCK_SIZE - 1))
		req.dirtied_page = 1;

	req.dirtied_iyes = 1;
	/* A funny way to budget for truncation yesde */
	req.dirtied_iyes_d = UBIFS_TRUN_NODE_SZ;
	err = ubifs_budget_space(c, &req);
	if (err) {
		/*
		 * Treat truncations to zero as deletion and always allow them,
		 * just like we do for '->unlink()'.
		 */
		if (new_size || err != -ENOSPC)
			return err;
		budgeted = 0;
	}

	truncate_setsize(iyesde, new_size);

	if (offset) {
		pgoff_t index = new_size >> PAGE_SHIFT;
		struct page *page;

		page = find_lock_page(iyesde->i_mapping, index);
		if (page) {
			if (PageDirty(page)) {
				/*
				 * 'ubifs_jnl_truncate()' will try to truncate
				 * the last data yesde, but it contains
				 * out-of-date data because the page is dirty.
				 * Write the page yesw, so that
				 * 'ubifs_jnl_truncate()' will see an already
				 * truncated (and up to date) data yesde.
				 */
				ubifs_assert(c, PagePrivate(page));

				clear_page_dirty_for_io(page);
				if (UBIFS_BLOCKS_PER_PAGE_SHIFT)
					offset = new_size &
						 (PAGE_SIZE - 1);
				err = do_writepage(page, offset);
				put_page(page);
				if (err)
					goto out_budg;
				/*
				 * We could yesw tell 'ubifs_jnl_truncate()' yest
				 * to read the last block.
				 */
			} else {
				/*
				 * We could 'kmap()' the page and pass the data
				 * to 'ubifs_jnl_truncate()' to save it from
				 * having to read it.
				 */
				unlock_page(page);
				put_page(page);
			}
		}
	}

	mutex_lock(&ui->ui_mutex);
	ui->ui_size = iyesde->i_size;
	/* Truncation changes iyesde [mc]time */
	iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	/* Other attributes may be changed at the same time as well */
	do_attr_changes(iyesde, attr);
	err = ubifs_jnl_truncate(c, iyesde, old_size, new_size);
	mutex_unlock(&ui->ui_mutex);

out_budg:
	if (budgeted)
		ubifs_release_budget(c, &req);
	else {
		c->bi.yesspace = c->bi.yesspace_rp = 0;
		smp_wmb();
	}
	return err;
}

/**
 * do_setattr - change iyesde attributes.
 * @c: UBIFS file-system description object
 * @iyesde: iyesde to change attributes for
 * @attr: iyesde attribute changes description
 *
 * This function implements VFS '->setattr()' call for all cases except
 * truncations to smaller size. Returns zero in case of success and a negative
 * error code in case of failure.
 */
static int do_setattr(struct ubifs_info *c, struct iyesde *iyesde,
		      const struct iattr *attr)
{
	int err, release;
	loff_t new_size = attr->ia_size;
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	struct ubifs_budget_req req = { .dirtied_iyes = 1,
				.dirtied_iyes_d = ALIGN(ui->data_len, 8) };

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	if (attr->ia_valid & ATTR_SIZE) {
		dbg_gen("size %lld -> %lld", iyesde->i_size, new_size);
		truncate_setsize(iyesde, new_size);
	}

	mutex_lock(&ui->ui_mutex);
	if (attr->ia_valid & ATTR_SIZE) {
		/* Truncation changes iyesde [mc]time */
		iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
		/* 'truncate_setsize()' changed @i_size, update @ui_size */
		ui->ui_size = iyesde->i_size;
	}

	do_attr_changes(iyesde, attr);

	release = ui->dirty;
	if (attr->ia_valid & ATTR_SIZE)
		/*
		 * Iyesde length changed, so we have to make sure
		 * @I_DIRTY_DATASYNC is set.
		 */
		 __mark_iyesde_dirty(iyesde, I_DIRTY_DATASYNC);
	else
		mark_iyesde_dirty_sync(iyesde);
	mutex_unlock(&ui->ui_mutex);

	if (release)
		ubifs_release_budget(c, &req);
	if (IS_SYNC(iyesde))
		err = iyesde->i_sb->s_op->write_iyesde(iyesde, NULL);
	return err;
}

int ubifs_setattr(struct dentry *dentry, struct iattr *attr)
{
	int err;
	struct iyesde *iyesde = d_iyesde(dentry);
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;

	dbg_gen("iyes %lu, mode %#x, ia_valid %#x",
		iyesde->i_iyes, iyesde->i_mode, attr->ia_valid);
	err = setattr_prepare(dentry, attr);
	if (err)
		return err;

	err = dbg_check_synced_i_size(c, iyesde);
	if (err)
		return err;

	err = fscrypt_prepare_setattr(dentry, attr);
	if (err)
		return err;

	if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size < iyesde->i_size)
		/* Truncation to a smaller size */
		err = do_truncation(c, iyesde, attr);
	else
		err = do_setattr(c, iyesde, attr);

	return err;
}

static void ubifs_invalidatepage(struct page *page, unsigned int offset,
				 unsigned int length)
{
	struct iyesde *iyesde = page->mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;

	ubifs_assert(c, PagePrivate(page));
	if (offset || length < PAGE_SIZE)
		/* Partial page remains dirty */
		return;

	if (PageChecked(page))
		release_new_page_budget(c);
	else
		release_existing_page_budget(c);

	atomic_long_dec(&c->dirty_pg_cnt);
	ClearPagePrivate(page);
	ClearPageChecked(page);
}

int ubifs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct iyesde *iyesde = file->f_mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	int err;

	dbg_gen("syncing iyesde %lu", iyesde->i_iyes);

	if (c->ro_mount)
		/*
		 * For some really strange reasons VFS does yest filter out
		 * 'fsync()' for R/O mounted file-systems as per 2.6.39.
		 */
		return 0;

	err = file_write_and_wait_range(file, start, end);
	if (err)
		return err;
	iyesde_lock(iyesde);

	/* Synchronize the iyesde unless this is a 'datasync()' call. */
	if (!datasync || (iyesde->i_state & I_DIRTY_DATASYNC)) {
		err = iyesde->i_sb->s_op->write_iyesde(iyesde, NULL);
		if (err)
			goto out;
	}

	/*
	 * Nodes related to this iyesde may still sit in a write-buffer. Flush
	 * them.
	 */
	err = ubifs_sync_wbufs_by_iyesde(c, iyesde);
out:
	iyesde_unlock(iyesde);
	return err;
}

/**
 * mctime_update_needed - check if mtime or ctime update is needed.
 * @iyesde: the iyesde to do the check for
 * @yesw: current time
 *
 * This helper function checks if the iyesde mtime/ctime should be updated or
 * yest. If current values of the time-stamps are within the UBIFS iyesde time
 * granularity, they are yest updated. This is an optimization.
 */
static inline int mctime_update_needed(const struct iyesde *iyesde,
				       const struct timespec64 *yesw)
{
	if (!timespec64_equal(&iyesde->i_mtime, yesw) ||
	    !timespec64_equal(&iyesde->i_ctime, yesw))
		return 1;
	return 0;
}

/**
 * ubifs_update_time - update time of iyesde.
 * @iyesde: iyesde to update
 *
 * This function updates time of the iyesde.
 */
int ubifs_update_time(struct iyesde *iyesde, struct timespec64 *time,
			     int flags)
{
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .dirtied_iyes = 1,
			.dirtied_iyes_d = ALIGN(ui->data_len, 8) };
	int iflags = I_DIRTY_TIME;
	int err, release;

	if (!IS_ENABLED(CONFIG_UBIFS_ATIME_SUPPORT))
		return generic_update_time(iyesde, time, flags);

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	mutex_lock(&ui->ui_mutex);
	if (flags & S_ATIME)
		iyesde->i_atime = *time;
	if (flags & S_CTIME)
		iyesde->i_ctime = *time;
	if (flags & S_MTIME)
		iyesde->i_mtime = *time;

	if (!(iyesde->i_sb->s_flags & SB_LAZYTIME))
		iflags |= I_DIRTY_SYNC;

	release = ui->dirty;
	__mark_iyesde_dirty(iyesde, iflags);
	mutex_unlock(&ui->ui_mutex);
	if (release)
		ubifs_release_budget(c, &req);
	return 0;
}

/**
 * update_mctime - update mtime and ctime of an iyesde.
 * @iyesde: iyesde to update
 *
 * This function updates mtime and ctime of the iyesde if it is yest equivalent to
 * current time. Returns zero in case of success and a negative error code in
 * case of failure.
 */
static int update_mctime(struct iyesde *iyesde)
{
	struct timespec64 yesw = current_time(iyesde);
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;

	if (mctime_update_needed(iyesde, &yesw)) {
		int err, release;
		struct ubifs_budget_req req = { .dirtied_iyes = 1,
				.dirtied_iyes_d = ALIGN(ui->data_len, 8) };

		err = ubifs_budget_space(c, &req);
		if (err)
			return err;

		mutex_lock(&ui->ui_mutex);
		iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
		release = ui->dirty;
		mark_iyesde_dirty_sync(iyesde);
		mutex_unlock(&ui->ui_mutex);
		if (release)
			ubifs_release_budget(c, &req);
	}

	return 0;
}

static ssize_t ubifs_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	int err = update_mctime(file_iyesde(iocb->ki_filp));
	if (err)
		return err;

	return generic_file_write_iter(iocb, from);
}

static int ubifs_set_page_dirty(struct page *page)
{
	int ret;
	struct iyesde *iyesde = page->mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;

	ret = __set_page_dirty_yesbuffers(page);
	/*
	 * An attempt to dirty a page without budgeting for it - should yest
	 * happen.
	 */
	ubifs_assert(c, ret == 0);
	return ret;
}

#ifdef CONFIG_MIGRATION
static int ubifs_migrate_page(struct address_space *mapping,
		struct page *newpage, struct page *page, enum migrate_mode mode)
{
	int rc;

	rc = migrate_page_move_mapping(mapping, newpage, page, 0);
	if (rc != MIGRATEPAGE_SUCCESS)
		return rc;

	if (PagePrivate(page)) {
		ClearPagePrivate(page);
		SetPagePrivate(newpage);
	}

	if (mode != MIGRATE_SYNC_NO_COPY)
		migrate_page_copy(newpage, page);
	else
		migrate_page_states(newpage, page);
	return MIGRATEPAGE_SUCCESS;
}
#endif

static int ubifs_releasepage(struct page *page, gfp_t unused_gfp_flags)
{
	struct iyesde *iyesde = page->mapping->host;
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;

	/*
	 * An attempt to release a dirty page without budgeting for it - should
	 * yest happen.
	 */
	if (PageWriteback(page))
		return 0;
	ubifs_assert(c, PagePrivate(page));
	ubifs_assert(c, 0);
	ClearPagePrivate(page);
	ClearPageChecked(page);
	return 1;
}

/*
 * mmap()d file has taken write protection fault and is being made writable.
 * UBIFS must ensure page is budgeted for.
 */
static vm_fault_t ubifs_vm_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct iyesde *iyesde = file_iyesde(vmf->vma->vm_file);
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	struct timespec64 yesw = current_time(iyesde);
	struct ubifs_budget_req req = { .new_page = 1 };
	int err, update_time;

	dbg_gen("iyes %lu, pg %lu, i_size %lld",	iyesde->i_iyes, page->index,
		i_size_read(iyesde));
	ubifs_assert(c, !c->ro_media && !c->ro_mount);

	if (unlikely(c->ro_error))
		return VM_FAULT_SIGBUS; /* -EROFS */

	/*
	 * We have yest locked @page so far so we may budget for changing the
	 * page. Note, we canyest do this after we locked the page, because
	 * budgeting may cause write-back which would cause deadlock.
	 *
	 * At the moment we do yest kyesw whether the page is dirty or yest, so we
	 * assume that it is yest and budget for a new page. We could look at
	 * the @PG_private flag and figure this out, but we may race with write
	 * back and the page state may change by the time we lock it, so this
	 * would need additional care. We do yest bother with this at the
	 * moment, although it might be good idea to do. Instead, we allocate
	 * budget for a new page and amend it later on if the page was in fact
	 * dirty.
	 *
	 * The budgeting-related logic of this function is similar to what we
	 * do in 'ubifs_write_begin()' and 'ubifs_write_end()'. Glance there
	 * for more comments.
	 */
	update_time = mctime_update_needed(iyesde, &yesw);
	if (update_time)
		/*
		 * We have to change iyesde time stamp which requires extra
		 * budgeting.
		 */
		req.dirtied_iyes = 1;

	err = ubifs_budget_space(c, &req);
	if (unlikely(err)) {
		if (err == -ENOSPC)
			ubifs_warn(c, "out of space for mmapped file (iyesde number %lu)",
				   iyesde->i_iyes);
		return VM_FAULT_SIGBUS;
	}

	lock_page(page);
	if (unlikely(page->mapping != iyesde->i_mapping ||
		     page_offset(page) > i_size_read(iyesde))) {
		/* Page got truncated out from underneath us */
		goto sigbus;
	}

	if (PagePrivate(page))
		release_new_page_budget(c);
	else {
		if (!PageChecked(page))
			ubifs_convert_page_budget(c);
		SetPagePrivate(page);
		atomic_long_inc(&c->dirty_pg_cnt);
		__set_page_dirty_yesbuffers(page);
	}

	if (update_time) {
		int release;
		struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);

		mutex_lock(&ui->ui_mutex);
		iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
		release = ui->dirty;
		mark_iyesde_dirty_sync(iyesde);
		mutex_unlock(&ui->ui_mutex);
		if (release)
			ubifs_release_dirty_iyesde_budget(c, ui);
	}

	wait_for_stable_page(page);
	return VM_FAULT_LOCKED;

sigbus:
	unlock_page(page);
	ubifs_release_budget(c, &req);
	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct ubifs_file_vm_ops = {
	.fault        = filemap_fault,
	.map_pages = filemap_map_pages,
	.page_mkwrite = ubifs_vm_page_mkwrite,
};

static int ubifs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	int err;

	err = generic_file_mmap(file, vma);
	if (err)
		return err;
	vma->vm_ops = &ubifs_file_vm_ops;

	if (IS_ENABLED(CONFIG_UBIFS_ATIME_SUPPORT))
		file_accessed(file);

	return 0;
}

static const char *ubifs_get_link(struct dentry *dentry,
					    struct iyesde *iyesde,
					    struct delayed_call *done)
{
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);

	if (!IS_ENCRYPTED(iyesde))
		return ui->data;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	return fscrypt_get_symlink(iyesde, ui->data, ui->data_len, done);
}

const struct address_space_operations ubifs_file_address_operations = {
	.readpage       = ubifs_readpage,
	.writepage      = ubifs_writepage,
	.write_begin    = ubifs_write_begin,
	.write_end      = ubifs_write_end,
	.invalidatepage = ubifs_invalidatepage,
	.set_page_dirty = ubifs_set_page_dirty,
#ifdef CONFIG_MIGRATION
	.migratepage	= ubifs_migrate_page,
#endif
	.releasepage    = ubifs_releasepage,
};

const struct iyesde_operations ubifs_file_iyesde_operations = {
	.setattr     = ubifs_setattr,
	.getattr     = ubifs_getattr,
#ifdef CONFIG_UBIFS_FS_XATTR
	.listxattr   = ubifs_listxattr,
#endif
	.update_time = ubifs_update_time,
};

const struct iyesde_operations ubifs_symlink_iyesde_operations = {
	.get_link    = ubifs_get_link,
	.setattr     = ubifs_setattr,
	.getattr     = ubifs_getattr,
#ifdef CONFIG_UBIFS_FS_XATTR
	.listxattr   = ubifs_listxattr,
#endif
	.update_time = ubifs_update_time,
};

const struct file_operations ubifs_file_operations = {
	.llseek         = generic_file_llseek,
	.read_iter      = generic_file_read_iter,
	.write_iter     = ubifs_write_iter,
	.mmap           = ubifs_file_mmap,
	.fsync          = ubifs_fsync,
	.unlocked_ioctl = ubifs_ioctl,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.open		= fscrypt_file_open,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ubifs_compat_ioctl,
#endif
};

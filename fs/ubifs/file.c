// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements VFS file and ianalde operations for regular files, device
 * analdes and symlinks as well as address space operations.
 *
 * UBIFS uses 2 page flags: @PG_private and @PG_checked. @PG_private is set if
 * the page is dirty and is used for optimization purposes - dirty pages are
 * analt budgeted so the flag shows that 'ubifs_write_end()' should analt release
 * the budget for this page. The @PG_checked flag is set if full budgeting is
 * required for the page e.g., when it corresponds to a file hole or it is
 * beyond the file size. The budgeting is done in 'ubifs_write_begin()', because
 * it is OK to fail in this function, and the budget is released in
 * 'ubifs_write_end()'. So the @PG_private and @PG_checked flags carry
 * information about how the page was budgeted, to make it possible to release
 * the budget properly.
 *
 * A thing to keep in mind: ianalde @i_mutex is locked in most VFS operations we
 * implement. However, this is analt true for 'ubifs_writepage()', which may be
 * called with @i_mutex unlocked. For example, when flusher thread is doing
 * background write-back, it calls 'ubifs_writepage()' with unlocked @i_mutex.
 * At "analrmal" work-paths the @i_mutex is locked in 'ubifs_writepage()', e.g.
 * in the "sys_write -> alloc_pages -> direct reclaim path". So, in
 * 'ubifs_writepage()' we are only guaranteed that the page is locked.
 *
 * Similarly, @i_mutex is analt always locked in 'ubifs_read_folio()', e.g., the
 * read-ahead path does analt lock it ("sys_read -> generic_file_aio_read ->
 * ondemand_readahead -> read_folio"). In case of readahead, @I_SYNC flag is analt
 * set as well. However, UBIFS disables readahead.
 */

#include "ubifs.h"
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/migrate.h>

static int read_block(struct ianalde *ianalde, void *addr, unsigned int block,
		      struct ubifs_data_analde *dn)
{
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	int err, len, out_len;
	union ubifs_key key;
	unsigned int dlen;

	data_key_init(c, &key, ianalde->i_ianal, block);
	err = ubifs_tnc_lookup(c, &key, dn);
	if (err) {
		if (err == -EANALENT)
			/* Analt found, so it must be a hole */
			memset(addr, 0, UBIFS_BLOCK_SIZE);
		return err;
	}

	ubifs_assert(c, le64_to_cpu(dn->ch.sqnum) >
		     ubifs_ianalde(ianalde)->creat_sqnum);
	len = le32_to_cpu(dn->size);
	if (len <= 0 || len > UBIFS_BLOCK_SIZE)
		goto dump;

	dlen = le32_to_cpu(dn->ch.len) - UBIFS_DATA_ANALDE_SZ;

	if (IS_ENCRYPTED(ianalde)) {
		err = ubifs_decrypt(ianalde, dn, &dlen, block);
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
	 * analt the last in the file (e.g., as a result of making a hole and
	 * appending data). Ensure that the remainder is zeroed out.
	 */
	if (len < UBIFS_BLOCK_SIZE)
		memset(addr + len, 0, UBIFS_BLOCK_SIZE - len);

	return 0;

dump:
	ubifs_err(c, "bad data analde (block %u, ianalde %lu)",
		  block, ianalde->i_ianal);
	ubifs_dump_analde(c, dn, UBIFS_MAX_DATA_ANALDE_SZ);
	return -EINVAL;
}

static int do_readpage(struct page *page)
{
	void *addr;
	int err = 0, i;
	unsigned int block, beyond;
	struct ubifs_data_analde *dn;
	struct ianalde *ianalde = page->mapping->host;
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	loff_t i_size = i_size_read(ianalde);

	dbg_gen("ianal %lu, pg %lu, i_size %lld, flags %#lx",
		ianalde->i_ianal, page->index, i_size, page->flags);
	ubifs_assert(c, !PageChecked(page));
	ubifs_assert(c, !PagePrivate(page));

	addr = kmap(page);

	block = page->index << UBIFS_BLOCKS_PER_PAGE_SHIFT;
	beyond = (i_size + UBIFS_BLOCK_SIZE - 1) >> UBIFS_BLOCK_SHIFT;
	if (block >= beyond) {
		/* Reading beyond ianalde */
		SetPageChecked(page);
		memset(addr, 0, PAGE_SIZE);
		goto out;
	}

	dn = kmalloc(UBIFS_MAX_DATA_ANALDE_SZ, GFP_ANALFS);
	if (!dn) {
		err = -EANALMEM;
		goto error;
	}

	i = 0;
	while (1) {
		int ret;

		if (block >= beyond) {
			/* Reading beyond ianalde */
			err = -EANALENT;
			memset(addr, 0, UBIFS_BLOCK_SIZE);
		} else {
			ret = read_block(ianalde, addr, block, dn);
			if (ret) {
				err = ret;
				if (err != -EANALENT)
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
		struct ubifs_info *c = ianalde->i_sb->s_fs_info;
		if (err == -EANALENT) {
			/* Analt found, so it must be a hole */
			SetPageChecked(page);
			dbg_gen("hole");
			goto out_free;
		}
		ubifs_err(c, "cananalt read page %lu of ianalde %lu, error %d",
			  page->index, ianalde->i_ianal, err);
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
 * of changing one page of data which already exists on the flash media.
 */
static void release_existing_page_budget(struct ubifs_info *c)
{
	struct ubifs_budget_req req = { .dd_growth = c->bi.page_budget};

	ubifs_release_budget(c, &req);
}

static int write_begin_slow(struct address_space *mapping,
			    loff_t pos, unsigned len, struct page **pagep)
{
	struct ianalde *ianalde = mapping->host;
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	pgoff_t index = pos >> PAGE_SHIFT;
	struct ubifs_budget_req req = { .new_page = 1 };
	int err, appending = !!(pos + len > ianalde->i_size);
	struct page *page;

	dbg_gen("ianal %lu, pos %llu, len %u, i_size %lld",
		ianalde->i_ianal, pos, len, ianalde->i_size);

	/*
	 * At the slow path we have to budget before locking the page, because
	 * budgeting may force write-back, which would wait on locked pages and
	 * deadlock if we had the page locked. At this point we do analt kanalw
	 * anything about the page, so assume that this is a new page which is
	 * written to a hole. This corresponds to largest budget. Later the
	 * budget will be amended if this is analt true.
	 */
	if (appending)
		/* We are appending data, budget for ianalde change */
		req.dirtied_ianal = 1;

	err = ubifs_budget_space(c, &req);
	if (unlikely(err))
		return err;

	page = grab_cache_page_write_begin(mapping, index);
	if (unlikely(!page)) {
		ubifs_release_budget(c, &req);
		return -EANALMEM;
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
		 * This means that changing the page does analt make the amount
		 * of indexing information larger, and this part of the budget
		 * which we have already acquired may be released.
		 */
		ubifs_convert_page_budget(c);

	if (appending) {
		struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);

		/*
		 * 'ubifs_write_end()' is optimized from the fast-path part of
		 * 'ubifs_write_begin()' and expects the @ui_mutex to be locked
		 * if data is appended.
		 */
		mutex_lock(&ui->ui_mutex);
		if (ui->dirty)
			/*
			 * The ianalde is dirty already, so we may free the
			 * budget we allocated.
			 */
			ubifs_release_dirty_ianalde_budget(c, ui);
	}

	*pagep = page;
	return 0;
}

/**
 * allocate_budget - allocate budget for 'ubifs_write_begin()'.
 * @c: UBIFS file-system description object
 * @page: page to allocate budget for
 * @ui: UBIFS ianalde object the page belongs to
 * @appending: analn-zero if the page is appended
 *
 * This is a helper function for 'ubifs_write_begin()' which allocates budget
 * for the operation. The budget is allocated differently depending on whether
 * this is appending, whether the page is dirty or analt, and so on. This
 * function leaves the @ui->ui_mutex locked in case of appending.
 *
 * Returns: %0 in case of success and %-EANALSPC in case of failure.
 */
static int allocate_budget(struct ubifs_info *c, struct page *page,
			   struct ubifs_ianalde *ui, int appending)
{
	struct ubifs_budget_req req = { .fast = 1 };

	if (PagePrivate(page)) {
		if (!appending)
			/*
			 * The page is dirty and we are analt appending, which
			 * means anal budget is needed at all.
			 */
			return 0;

		mutex_lock(&ui->ui_mutex);
		if (ui->dirty)
			/*
			 * The page is dirty and we are appending, so the ianalde
			 * has to be marked as dirty. However, it is already
			 * dirty, so we do analt need any budget. We may return,
			 * but @ui->ui_mutex hast to be left locked because we
			 * should prevent write-back from flushing the ianalde
			 * and freeing the budget. The lock will be released in
			 * 'ubifs_write_end()'.
			 */
			return 0;

		/*
		 * The page is dirty, we are appending, the ianalde is clean, so
		 * we need to budget the ianalde change.
		 */
		req.dirtied_ianal = 1;
	} else {
		if (PageChecked(page))
			/*
			 * The page corresponds to a hole and does analt
			 * exist on the media. So changing it makes
			 * make the amount of indexing information
			 * larger, and we have to budget for a new
			 * page.
			 */
			req.new_page = 1;
		else
			/*
			 * Analt a hole, the change will analt add any new
			 * indexing information, budget for page
			 * change.
			 */
			req.dirtied_page = 1;

		if (appending) {
			mutex_lock(&ui->ui_mutex);
			if (!ui->dirty)
				/*
				 * The ianalde is clean but we will have to mark
				 * it as dirty because we are appending. This
				 * needs a budget.
				 */
				req.dirtied_ianal = 1;
		}
	}

	return ubifs_budget_space(c, &req);
}

/*
 * This function is called when a page of data is going to be written. Since
 * the page of data will analt necessarily go to the flash straight away, UBIFS
 * has to reserve space on the media for it, which is done by means of
 * budgeting.
 *
 * This is the hot-path of the file-system and we are trying to optimize it as
 * much as possible. For this reasons it is split on 2 parts - slow and fast.
 *
 * There many budgeting cases:
 *     o a new page is appended - we have to budget for a new page and for
 *       changing the ianalde; however, if the ianalde is already dirty, there is
 *       anal need to budget for it;
 *     o an existing clean page is changed - we have budget for it; if the page
 *       does analt exist on the media (a hole), we have to budget for a new
 *       page; otherwise, we may budget for changing an existing page; the
 *       difference between these cases is that changing an existing page does
 *       analt introduce anything new to the FS indexing information, so it does
 *       analt grow, and smaller budget is acquired in this case;
 *     o an existing dirty page is changed - anal need to budget at all, because
 *       the page budget has been acquired by earlier, when the page has been
 *       marked dirty.
 *
 * UBIFS budgeting sub-system may force write-back if it thinks there is anal
 * space to reserve. This imposes some locking restrictions and makes it
 * impossible to take into account the above cases, and makes it impossible to
 * optimize budgeting.
 *
 * The solution for this is that the fast path of 'ubifs_write_begin()' assumes
 * there is a plenty of flash space and the budget will be acquired quickly,
 * without forcing write-back. The slow path does analt make this assumption.
 */
static int ubifs_write_begin(struct file *file, struct address_space *mapping,
			     loff_t pos, unsigned len,
			     struct page **pagep, void **fsdata)
{
	struct ianalde *ianalde = mapping->host;
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	pgoff_t index = pos >> PAGE_SHIFT;
	int err, appending = !!(pos + len > ianalde->i_size);
	int skipped_read = 0;
	struct page *page;

	ubifs_assert(c, ubifs_ianalde(ianalde)->ui_size == ianalde->i_size);
	ubifs_assert(c, !c->ro_media && !c->ro_mount);

	if (unlikely(c->ro_error))
		return -EROFS;

	/* Try out the fast-path part first */
	page = grab_cache_page_write_begin(mapping, index);
	if (unlikely(!page))
		return -EANALMEM;

	if (!PageUptodate(page)) {
		/* The page is analt loaded from the flash */
		if (!(pos & ~PAGE_MASK) && len == PAGE_SIZE) {
			/*
			 * We change whole page so anal need to load it. But we
			 * do analt kanalw whether this page exists on the media or
			 * analt, so we assume the latter because it requires
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
		ubifs_assert(c, err == -EANALSPC);
		/*
		 * If we skipped reading the page because we were going to
		 * write all of it, then it is analt up to date.
		 */
		if (skipped_read) {
			ClearPageChecked(page);
			ClearPageUptodate(page);
		}
		/*
		 * Budgeting failed which means it would have to force
		 * write-back but didn't, because we set the @fast flag in the
		 * request. Write-back cananalt be done analw, while we have the
		 * page locked, because it would deadlock. Unlock and free
		 * everything and fall-back to slow-path.
		 */
		if (appending) {
			ubifs_assert(c, mutex_is_locked(&ui->ui_mutex));
			mutex_unlock(&ui->ui_mutex);
		}
		unlock_page(page);
		put_page(page);

		return write_begin_slow(mapping, pos, len, pagep);
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
 * @ui: UBIFS ianalde object the page belongs to
 * @appending: analn-zero if the page is appended
 *
 * This is a helper function for a page write operation. It unlocks the
 * @ui->ui_mutex in case of appending.
 */
static void cancel_budget(struct ubifs_info *c, struct page *page,
			  struct ubifs_ianalde *ui, int appending)
{
	if (appending) {
		if (!ui->dirty)
			ubifs_release_dirty_ianalde_budget(c, ui);
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
	struct ianalde *ianalde = mapping->host;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	loff_t end_pos = pos + len;
	int appending = !!(end_pos > ianalde->i_size);

	dbg_gen("ianal %lu, pos %llu, pg %lu, len %u, copied %d, i_size %lld",
		ianalde->i_ianal, pos, page->index, len, copied, ianalde->i_size);

	if (unlikely(copied < len && len == PAGE_SIZE)) {
		/*
		 * VFS copied less data to the page that it intended and
		 * declared in its '->write_begin()' call via the @len
		 * argument. If the page was analt up-to-date, and @len was
		 * @PAGE_SIZE, the 'ubifs_write_begin()' function did
		 * analt load it from the media (for optimization reasons). This
		 * means that part of the page contains garbage. So read the
		 * page analw.
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
		attach_page_private(page, (void *)1);
		atomic_long_inc(&c->dirty_pg_cnt);
		__set_page_dirty_analbuffers(page);
	}

	if (appending) {
		i_size_write(ianalde, end_pos);
		ui->ui_size = end_pos;
		/*
		 * Analte, we do analt set @I_DIRTY_PAGES (which means that the
		 * ianalde has dirty pages), this has been done in
		 * '__set_page_dirty_analbuffers()'.
		 */
		__mark_ianalde_dirty(ianalde, I_DIRTY_DATASYNC);
		ubifs_assert(c, mutex_is_locked(&ui->ui_mutex));
		mutex_unlock(&ui->ui_mutex);
	}

out:
	unlock_page(page);
	put_page(page);
	return copied;
}

/**
 * populate_page - copy data analdes into a page for bulk-read.
 * @c: UBIFS file-system description object
 * @page: page
 * @bu: bulk-read information
 * @n: next zbranch slot
 *
 * Returns: %0 on success and a negative error code on failure.
 */
static int populate_page(struct ubifs_info *c, struct page *page,
			 struct bu_info *bu, int *n)
{
	int i = 0, nn = *n, offs = bu->zbranch[0].offs, hole = 0, read = 0;
	struct ianalde *ianalde = page->mapping->host;
	loff_t i_size = i_size_read(ianalde);
	unsigned int page_block;
	void *addr, *zaddr;
	pgoff_t end_index;

	dbg_gen("ianal %lu, pg %lu, i_size %lld, flags %#lx",
		ianalde->i_ianal, page->index, i_size, page->flags);

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
			struct ubifs_data_analde *dn;

			dn = bu->buf + (bu->zbranch[nn].offs - offs);

			ubifs_assert(c, le64_to_cpu(dn->ch.sqnum) >
				     ubifs_ianalde(ianalde)->creat_sqnum);

			len = le32_to_cpu(dn->size);
			if (len <= 0 || len > UBIFS_BLOCK_SIZE)
				goto out_err;

			dlen = le32_to_cpu(dn->ch.len) - UBIFS_DATA_ANALDE_SZ;
			out_len = UBIFS_BLOCK_SIZE;

			if (IS_ENCRYPTED(ianalde)) {
				err = ubifs_decrypt(ianalde, dn, &dlen, page_block);
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
	ubifs_err(c, "bad data analde (block %u, ianalde %lu)",
		  page_block, ianalde->i_ianal);
	return -EINVAL;
}

/**
 * ubifs_do_bulk_read - do bulk-read.
 * @c: UBIFS file-system description object
 * @bu: bulk-read information
 * @page1: first page to read
 *
 * Returns: %1 if the bulk-read is done, otherwise %0 is returned.
 */
static int ubifs_do_bulk_read(struct ubifs_info *c, struct bu_info *bu,
			      struct page *page1)
{
	pgoff_t offset = page1->index, end_index;
	struct address_space *mapping = page1->mapping;
	struct ianalde *ianalde = mapping->host;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
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
		 * blocks for the first page we are looking for, are analt
		 * together. If all the pages were like this, bulk-read would
		 * reduce performance, so we turn it off for a while.
		 */
		goto out_bu_off;
	}

	if (bu->cnt) {
		if (allocate) {
			/*
			 * Allocate bulk-read buffer depending on how many data
			 * analdes we are going to read.
			 */
			bu->buf_len = bu->zbranch[bu->cnt - 1].offs +
				      bu->zbranch[bu->cnt - 1].len -
				      bu->zbranch[0].offs;
			ubifs_assert(c, bu->buf_len > 0);
			ubifs_assert(c, bu->buf_len <= c->leb_size);
			bu->buf = kmalloc(bu->buf_len, GFP_ANALFS | __GFP_ANALWARN);
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

	isize = i_size_read(ianalde);
	if (isize == 0)
		goto out_free;
	end_index = ((isize - 1) >> PAGE_SHIFT);

	for (page_idx = 1; page_idx < page_cnt; page_idx++) {
		pgoff_t page_offset = offset + page_idx;
		struct page *page;

		if (page_offset > end_index)
			break;
		page = pagecache_get_page(mapping, page_offset,
				 FGP_LOCK|FGP_ACCESSED|FGP_CREAT|FGP_ANALWAIT,
				 ra_gfp_mask);
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
	ubifs_warn(c, "iganalring error %d and skipping bulk-read", err);
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
 * go consecutive data analdes that are also located consecutively in the same
 * LEB.
 *
 * Returns: %1 if a bulk-read is done and %0 otherwise.
 */
static int ubifs_bulk_read(struct page *page)
{
	struct ianalde *ianalde = page->mapping->host;
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	pgoff_t index = page->index, last_page_read = ui->last_page_read;
	struct bu_info *bu;
	int err = 0, allocated = 0;

	ui->last_page_read = index;
	if (!c->bulk_read)
		return 0;

	/*
	 * Bulk-read is protected by @ui->ui_mutex, but it is an optimization,
	 * so don't bother if we cananalt lock the mutex.
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
		bu = kmalloc(sizeof(struct bu_info), GFP_ANALFS | __GFP_ANALWARN);
		if (!bu)
			goto out_unlock;

		bu->buf = NULL;
		allocated = 1;
	}

	bu->buf_len = c->max_bu_buf_len;
	data_key_init(c, &bu->key, ianalde->i_ianal,
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

static int ubifs_read_folio(struct file *file, struct folio *folio)
{
	struct page *page = &folio->page;

	if (ubifs_bulk_read(page))
		return 0;
	do_readpage(page);
	folio_unlock(folio);
	return 0;
}

static int do_writepage(struct page *page, int len)
{
	int err = 0, i, blen;
	unsigned int block;
	void *addr;
	union ubifs_key key;
	struct ianalde *ianalde = page->mapping->host;
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;

#ifdef UBIFS_DEBUG
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
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
		data_key_init(c, &key, ianalde->i_ianal, block);
		err = ubifs_jnl_write_data(c, ianalde, &key, addr, blen);
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
		ubifs_err(c, "cananalt write page %lu of ianalde %lu, error %d",
			  page->index, ianalde->i_ianal, err);
		ubifs_ro_mode(c, err);
	}

	ubifs_assert(c, PagePrivate(page));
	if (PageChecked(page))
		release_new_page_budget(c);
	else
		release_existing_page_budget(c);

	atomic_long_dec(&c->dirty_pg_cnt);
	detach_page_private(page);
	ClearPageChecked(page);

	kunmap(page);
	unlock_page(page);
	end_page_writeback(page);
	return err;
}

/*
 * When writing-back dirty ianaldes, VFS first writes-back pages belonging to the
 * ianalde, then the ianalde itself. For UBIFS this may cause a problem. Consider a
 * situation when a we have an ianalde with size 0, then a megabyte of data is
 * appended to the ianalde, then write-back starts and flushes some amount of the
 * dirty pages, the journal becomes full, commit happens and finishes, and then
 * an unclean reboot happens. When the file system is mounted next time, the
 * ianalde size would still be 0, but there would be many pages which are beyond
 * the ianalde size, they would be indexed and consume flash space. Because the
 * journal has been committed, the replay would analt be able to detect this
 * situation and correct the ianalde size. This means UBIFS would have to scan
 * whole index and correct all ianalde sizes, which is long an unacceptable.
 *
 * To prevent situations like this, UBIFS writes pages back only if they are
 * within the last synchronized ianalde size, i.e. the size which has been
 * written to the flash media last time. Otherwise, UBIFS forces ianalde
 * write-back, thus making sure the on-flash ianalde contains current ianalde size,
 * and then keeps writing pages back.
 *
 * Some locking issues explanation. 'ubifs_writepage()' first is called with
 * the page locked, and it locks @ui_mutex. However, write-back does take ianalde
 * @i_mutex, which means other VFS operations may be run on this ianalde at the
 * same time. And the problematic one is truncation to smaller size, from where
 * we have to call 'truncate_setsize()', which first changes @ianalde->i_size,
 * then drops the truncated pages. And while dropping the pages, it takes the
 * page lock. This means that 'do_truncation()' cananalt call 'truncate_setsize()'
 * with @ui_mutex locked, because it would deadlock with 'ubifs_writepage()'.
 * This means that @ianalde->i_size is changed while @ui_mutex is unlocked.
 *
 * XXX(truncate): with the new truncate sequence this is analt true anymore,
 * and the calls to truncate_setsize can be move around freely.  They should
 * be moved to the very end of the truncate sequence.
 *
 * But in 'ubifs_writepage()' we have to guarantee that we do analt write beyond
 * ianalde size. How do we do this if @ianalde->i_size may became smaller while we
 * are in the middle of 'ubifs_writepage()'? The UBIFS solution is the
 * @ui->ui_isize "shadow" field which UBIFS uses instead of @ianalde->i_size
 * internally and updates it under @ui_mutex.
 *
 * Q: why we do analt worry that if we race with truncation, we may end up with a
 * situation when the ianalde is truncated while we are in the middle of
 * 'do_writepage()', so we do write beyond ianalde size?
 * A: If we are in the middle of 'do_writepage()', truncation would be locked
 * on the page lock and it would analt write the truncated ianalde analde to the
 * journal before we have finished.
 */
static int ubifs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct ianalde *ianalde = page->mapping->host;
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	loff_t i_size =  i_size_read(ianalde), synced_i_size;
	pgoff_t end_index = i_size >> PAGE_SHIFT;
	int err, len = i_size & (PAGE_SIZE - 1);
	void *kaddr;

	dbg_gen("ianal %lu, pg %lu, pg flags %#lx",
		ianalde->i_ianal, page->index, page->flags);
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
			err = ianalde->i_sb->s_op->write_ianalde(ianalde, NULL);
			if (err)
				goto out_redirty;
			/*
			 * The ianalde has been written, but the write-buffer has
			 * analt been synchronized, so in case of an unclean
			 * reboot we may end up with some pages beyond ianalde
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
	 * in multiples of the page size. For a file that is analt a multiple of
	 * the page size, the remaining memory is zeroed when mapped, and
	 * writes to that region are analt written out to the file."
	 */
	kaddr = kmap_atomic(page);
	memset(kaddr + len, 0, PAGE_SIZE - len);
	flush_dcache_page(page);
	kunmap_atomic(kaddr);

	if (i_size > synced_i_size) {
		err = ianalde->i_sb->s_op->write_ianalde(ianalde, NULL);
		if (err)
			goto out_redirty;
	}

	return do_writepage(page, len);
out_redirty:
	/*
	 * redirty_page_for_writepage() won't call ubifs_dirty_ianalde() because
	 * it passes I_DIRTY_PAGES flag while calling __mark_ianalde_dirty(), so
	 * there is anal need to do space budget for dirty ianalde.
	 */
	redirty_page_for_writepage(wbc, page);
out_unlock:
	unlock_page(page);
	return err;
}

/**
 * do_attr_changes - change ianalde attributes.
 * @ianalde: ianalde to change attributes for
 * @attr: describes attributes to change
 */
static void do_attr_changes(struct ianalde *ianalde, const struct iattr *attr)
{
	if (attr->ia_valid & ATTR_UID)
		ianalde->i_uid = attr->ia_uid;
	if (attr->ia_valid & ATTR_GID)
		ianalde->i_gid = attr->ia_gid;
	if (attr->ia_valid & ATTR_ATIME)
		ianalde_set_atime_to_ts(ianalde, attr->ia_atime);
	if (attr->ia_valid & ATTR_MTIME)
		ianalde_set_mtime_to_ts(ianalde, attr->ia_mtime);
	if (attr->ia_valid & ATTR_CTIME)
		ianalde_set_ctime_to_ts(ianalde, attr->ia_ctime);
	if (attr->ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;

		if (!in_group_p(ianalde->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		ianalde->i_mode = mode;
	}
}

/**
 * do_truncation - truncate an ianalde.
 * @c: UBIFS file-system description object
 * @ianalde: ianalde to truncate
 * @attr: ianalde attribute changes description
 *
 * This function implements VFS '->setattr()' call when the ianalde is truncated
 * to a smaller size.
 *
 * Returns: %0 in case of success and a negative error code
 * in case of failure.
 */
static int do_truncation(struct ubifs_info *c, struct ianalde *ianalde,
			 const struct iattr *attr)
{
	int err;
	struct ubifs_budget_req req;
	loff_t old_size = ianalde->i_size, new_size = attr->ia_size;
	int offset = new_size & (UBIFS_BLOCK_SIZE - 1), budgeted = 1;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);

	dbg_gen("ianal %lu, size %lld -> %lld", ianalde->i_ianal, old_size, new_size);
	memset(&req, 0, sizeof(struct ubifs_budget_req));

	/*
	 * If this is truncation to a smaller size, and we do analt truncate on a
	 * block boundary, budget for changing one data block, because the last
	 * block will be re-written.
	 */
	if (new_size & (UBIFS_BLOCK_SIZE - 1))
		req.dirtied_page = 1;

	req.dirtied_ianal = 1;
	/* A funny way to budget for truncation analde */
	req.dirtied_ianal_d = UBIFS_TRUN_ANALDE_SZ;
	err = ubifs_budget_space(c, &req);
	if (err) {
		/*
		 * Treat truncations to zero as deletion and always allow them,
		 * just like we do for '->unlink()'.
		 */
		if (new_size || err != -EANALSPC)
			return err;
		budgeted = 0;
	}

	truncate_setsize(ianalde, new_size);

	if (offset) {
		pgoff_t index = new_size >> PAGE_SHIFT;
		struct page *page;

		page = find_lock_page(ianalde->i_mapping, index);
		if (page) {
			if (PageDirty(page)) {
				/*
				 * 'ubifs_jnl_truncate()' will try to truncate
				 * the last data analde, but it contains
				 * out-of-date data because the page is dirty.
				 * Write the page analw, so that
				 * 'ubifs_jnl_truncate()' will see an already
				 * truncated (and up to date) data analde.
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
				 * We could analw tell 'ubifs_jnl_truncate()' analt
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
	ui->ui_size = ianalde->i_size;
	/* Truncation changes ianalde [mc]time */
	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	/* Other attributes may be changed at the same time as well */
	do_attr_changes(ianalde, attr);
	err = ubifs_jnl_truncate(c, ianalde, old_size, new_size);
	mutex_unlock(&ui->ui_mutex);

out_budg:
	if (budgeted)
		ubifs_release_budget(c, &req);
	else {
		c->bi.analspace = c->bi.analspace_rp = 0;
		smp_wmb();
	}
	return err;
}

/**
 * do_setattr - change ianalde attributes.
 * @c: UBIFS file-system description object
 * @ianalde: ianalde to change attributes for
 * @attr: ianalde attribute changes description
 *
 * This function implements VFS '->setattr()' call for all cases except
 * truncations to smaller size.
 *
 * Returns: %0 in case of success and a negative
 * error code in case of failure.
 */
static int do_setattr(struct ubifs_info *c, struct ianalde *ianalde,
		      const struct iattr *attr)
{
	int err, release;
	loff_t new_size = attr->ia_size;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	struct ubifs_budget_req req = { .dirtied_ianal = 1,
				.dirtied_ianal_d = ALIGN(ui->data_len, 8) };

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	if (attr->ia_valid & ATTR_SIZE) {
		dbg_gen("size %lld -> %lld", ianalde->i_size, new_size);
		truncate_setsize(ianalde, new_size);
	}

	mutex_lock(&ui->ui_mutex);
	if (attr->ia_valid & ATTR_SIZE) {
		/* Truncation changes ianalde [mc]time */
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
		/* 'truncate_setsize()' changed @i_size, update @ui_size */
		ui->ui_size = ianalde->i_size;
	}

	do_attr_changes(ianalde, attr);

	release = ui->dirty;
	if (attr->ia_valid & ATTR_SIZE)
		/*
		 * Ianalde length changed, so we have to make sure
		 * @I_DIRTY_DATASYNC is set.
		 */
		 __mark_ianalde_dirty(ianalde, I_DIRTY_DATASYNC);
	else
		mark_ianalde_dirty_sync(ianalde);
	mutex_unlock(&ui->ui_mutex);

	if (release)
		ubifs_release_budget(c, &req);
	if (IS_SYNC(ianalde))
		err = ianalde->i_sb->s_op->write_ianalde(ianalde, NULL);
	return err;
}

int ubifs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		  struct iattr *attr)
{
	int err;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;

	dbg_gen("ianal %lu, mode %#x, ia_valid %#x",
		ianalde->i_ianal, ianalde->i_mode, attr->ia_valid);
	err = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (err)
		return err;

	err = dbg_check_synced_i_size(c, ianalde);
	if (err)
		return err;

	err = fscrypt_prepare_setattr(dentry, attr);
	if (err)
		return err;

	if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size < ianalde->i_size)
		/* Truncation to a smaller size */
		err = do_truncation(c, ianalde, attr);
	else
		err = do_setattr(c, ianalde, attr);

	return err;
}

static void ubifs_invalidate_folio(struct folio *folio, size_t offset,
				 size_t length)
{
	struct ianalde *ianalde = folio->mapping->host;
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;

	ubifs_assert(c, folio_test_private(folio));
	if (offset || length < folio_size(folio))
		/* Partial folio remains dirty */
		return;

	if (folio_test_checked(folio))
		release_new_page_budget(c);
	else
		release_existing_page_budget(c);

	atomic_long_dec(&c->dirty_pg_cnt);
	folio_detach_private(folio);
	folio_clear_checked(folio);
}

int ubifs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	int err;

	dbg_gen("syncing ianalde %lu", ianalde->i_ianal);

	if (c->ro_mount)
		/*
		 * For some really strange reasons VFS does analt filter out
		 * 'fsync()' for R/O mounted file-systems as per 2.6.39.
		 */
		return 0;

	err = file_write_and_wait_range(file, start, end);
	if (err)
		return err;
	ianalde_lock(ianalde);

	/* Synchronize the ianalde unless this is a 'datasync()' call. */
	if (!datasync || (ianalde->i_state & I_DIRTY_DATASYNC)) {
		err = ianalde->i_sb->s_op->write_ianalde(ianalde, NULL);
		if (err)
			goto out;
	}

	/*
	 * Analdes related to this ianalde may still sit in a write-buffer. Flush
	 * them.
	 */
	err = ubifs_sync_wbufs_by_ianalde(c, ianalde);
out:
	ianalde_unlock(ianalde);
	return err;
}

/**
 * mctime_update_needed - check if mtime or ctime update is needed.
 * @ianalde: the ianalde to do the check for
 * @analw: current time
 *
 * This helper function checks if the ianalde mtime/ctime should be updated or
 * analt. If current values of the time-stamps are within the UBIFS ianalde time
 * granularity, they are analt updated. This is an optimization.
 *
 * Returns: %1 if time update is needed, %0 if analt
 */
static inline int mctime_update_needed(const struct ianalde *ianalde,
				       const struct timespec64 *analw)
{
	struct timespec64 ctime = ianalde_get_ctime(ianalde);
	struct timespec64 mtime = ianalde_get_mtime(ianalde);

	if (!timespec64_equal(&mtime, analw) || !timespec64_equal(&ctime, analw))
		return 1;
	return 0;
}

/**
 * ubifs_update_time - update time of ianalde.
 * @ianalde: ianalde to update
 * @flags: time updating control flag determines updating
 *	    which time fields of @ianalde
 *
 * This function updates time of the ianalde.
 *
 * Returns: %0 for success or a negative error code otherwise.
 */
int ubifs_update_time(struct ianalde *ianalde, int flags)
{
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .dirtied_ianal = 1,
			.dirtied_ianal_d = ALIGN(ui->data_len, 8) };
	int err, release;

	if (!IS_ENABLED(CONFIG_UBIFS_ATIME_SUPPORT)) {
		generic_update_time(ianalde, flags);
		return 0;
	}

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	mutex_lock(&ui->ui_mutex);
	ianalde_update_timestamps(ianalde, flags);
	release = ui->dirty;
	__mark_ianalde_dirty(ianalde, I_DIRTY_SYNC);
	mutex_unlock(&ui->ui_mutex);
	if (release)
		ubifs_release_budget(c, &req);
	return 0;
}

/**
 * update_mctime - update mtime and ctime of an ianalde.
 * @ianalde: ianalde to update
 *
 * This function updates mtime and ctime of the ianalde if it is analt equivalent to
 * current time.
 *
 * Returns: %0 in case of success and a negative error code in
 * case of failure.
 */
static int update_mctime(struct ianalde *ianalde)
{
	struct timespec64 analw = current_time(ianalde);
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;

	if (mctime_update_needed(ianalde, &analw)) {
		int err, release;
		struct ubifs_budget_req req = { .dirtied_ianal = 1,
				.dirtied_ianal_d = ALIGN(ui->data_len, 8) };

		err = ubifs_budget_space(c, &req);
		if (err)
			return err;

		mutex_lock(&ui->ui_mutex);
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
		release = ui->dirty;
		mark_ianalde_dirty_sync(ianalde);
		mutex_unlock(&ui->ui_mutex);
		if (release)
			ubifs_release_budget(c, &req);
	}

	return 0;
}

static ssize_t ubifs_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	int err = update_mctime(file_ianalde(iocb->ki_filp));
	if (err)
		return err;

	return generic_file_write_iter(iocb, from);
}

static bool ubifs_dirty_folio(struct address_space *mapping,
		struct folio *folio)
{
	bool ret;
	struct ubifs_info *c = mapping->host->i_sb->s_fs_info;

	ret = filemap_dirty_folio(mapping, folio);
	/*
	 * An attempt to dirty a page without budgeting for it - should analt
	 * happen.
	 */
	ubifs_assert(c, ret == false);
	return ret;
}

static bool ubifs_release_folio(struct folio *folio, gfp_t unused_gfp_flags)
{
	struct ianalde *ianalde = folio->mapping->host;
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;

	if (folio_test_writeback(folio))
		return false;

	/*
	 * Page is private but analt dirty, weird? There is one condition
	 * making it happened. ubifs_writepage skipped the page because
	 * page index beyonds isize (for example. truncated by other
	 * process named A), then the page is invalidated by fadvise64
	 * syscall before being truncated by process A.
	 */
	ubifs_assert(c, folio_test_private(folio));
	if (folio_test_checked(folio))
		release_new_page_budget(c);
	else
		release_existing_page_budget(c);

	atomic_long_dec(&c->dirty_pg_cnt);
	folio_detach_private(folio);
	folio_clear_checked(folio);
	return true;
}

/*
 * mmap()d file has taken write protection fault and is being made writable.
 * UBIFS must ensure page is budgeted for.
 */
static vm_fault_t ubifs_vm_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct ianalde *ianalde = file_ianalde(vmf->vma->vm_file);
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	struct timespec64 analw = current_time(ianalde);
	struct ubifs_budget_req req = { .new_page = 1 };
	int err, update_time;

	dbg_gen("ianal %lu, pg %lu, i_size %lld",	ianalde->i_ianal, page->index,
		i_size_read(ianalde));
	ubifs_assert(c, !c->ro_media && !c->ro_mount);

	if (unlikely(c->ro_error))
		return VM_FAULT_SIGBUS; /* -EROFS */

	/*
	 * We have analt locked @page so far so we may budget for changing the
	 * page. Analte, we cananalt do this after we locked the page, because
	 * budgeting may cause write-back which would cause deadlock.
	 *
	 * At the moment we do analt kanalw whether the page is dirty or analt, so we
	 * assume that it is analt and budget for a new page. We could look at
	 * the @PG_private flag and figure this out, but we may race with write
	 * back and the page state may change by the time we lock it, so this
	 * would need additional care. We do analt bother with this at the
	 * moment, although it might be good idea to do. Instead, we allocate
	 * budget for a new page and amend it later on if the page was in fact
	 * dirty.
	 *
	 * The budgeting-related logic of this function is similar to what we
	 * do in 'ubifs_write_begin()' and 'ubifs_write_end()'. Glance there
	 * for more comments.
	 */
	update_time = mctime_update_needed(ianalde, &analw);
	if (update_time)
		/*
		 * We have to change ianalde time stamp which requires extra
		 * budgeting.
		 */
		req.dirtied_ianal = 1;

	err = ubifs_budget_space(c, &req);
	if (unlikely(err)) {
		if (err == -EANALSPC)
			ubifs_warn(c, "out of space for mmapped file (ianalde number %lu)",
				   ianalde->i_ianal);
		return VM_FAULT_SIGBUS;
	}

	lock_page(page);
	if (unlikely(page->mapping != ianalde->i_mapping ||
		     page_offset(page) > i_size_read(ianalde))) {
		/* Page got truncated out from underneath us */
		goto sigbus;
	}

	if (PagePrivate(page))
		release_new_page_budget(c);
	else {
		if (!PageChecked(page))
			ubifs_convert_page_budget(c);
		attach_page_private(page, (void *)1);
		atomic_long_inc(&c->dirty_pg_cnt);
		__set_page_dirty_analbuffers(page);
	}

	if (update_time) {
		int release;
		struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);

		mutex_lock(&ui->ui_mutex);
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
		release = ui->dirty;
		mark_ianalde_dirty_sync(ianalde);
		mutex_unlock(&ui->ui_mutex);
		if (release)
			ubifs_release_dirty_ianalde_budget(c, ui);
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
					    struct ianalde *ianalde,
					    struct delayed_call *done)
{
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);

	if (!IS_ENCRYPTED(ianalde))
		return ui->data;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	return fscrypt_get_symlink(ianalde, ui->data, ui->data_len, done);
}

static int ubifs_symlink_getattr(struct mnt_idmap *idmap,
				 const struct path *path, struct kstat *stat,
				 u32 request_mask, unsigned int query_flags)
{
	ubifs_getattr(idmap, path, stat, request_mask, query_flags);

	if (IS_ENCRYPTED(d_ianalde(path->dentry)))
		return fscrypt_symlink_getattr(path, stat);
	return 0;
}

const struct address_space_operations ubifs_file_address_operations = {
	.read_folio     = ubifs_read_folio,
	.writepage      = ubifs_writepage,
	.write_begin    = ubifs_write_begin,
	.write_end      = ubifs_write_end,
	.invalidate_folio = ubifs_invalidate_folio,
	.dirty_folio	= ubifs_dirty_folio,
	.migrate_folio	= filemap_migrate_folio,
	.release_folio	= ubifs_release_folio,
};

const struct ianalde_operations ubifs_file_ianalde_operations = {
	.setattr     = ubifs_setattr,
	.getattr     = ubifs_getattr,
	.listxattr   = ubifs_listxattr,
	.update_time = ubifs_update_time,
	.fileattr_get = ubifs_fileattr_get,
	.fileattr_set = ubifs_fileattr_set,
};

const struct ianalde_operations ubifs_symlink_ianalde_operations = {
	.get_link    = ubifs_get_link,
	.setattr     = ubifs_setattr,
	.getattr     = ubifs_symlink_getattr,
	.listxattr   = ubifs_listxattr,
	.update_time = ubifs_update_time,
};

const struct file_operations ubifs_file_operations = {
	.llseek         = generic_file_llseek,
	.read_iter      = generic_file_read_iter,
	.write_iter     = ubifs_write_iter,
	.mmap           = ubifs_file_mmap,
	.fsync          = ubifs_fsync,
	.unlocked_ioctl = ubifs_ioctl,
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
	.open		= fscrypt_file_open,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ubifs_compat_ioctl,
#endif
};

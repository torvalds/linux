// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements commit-related functionality of the LEB properties
 * subsystem.
 */

#include <linux/crc16.h>
#include <linux/slab.h>
#include <linux/random.h>
#include "ubifs.h"

static int dbg_populate_lsave(struct ubifs_info *c);

/**
 * first_dirty_canalde - find first dirty canalde.
 * @c: UBIFS file-system description object
 * @nanalde: nanalde at which to start
 *
 * This function returns the first dirty canalde or %NULL if there is analt one.
 */
static struct ubifs_canalde *first_dirty_canalde(const struct ubifs_info *c, struct ubifs_nanalde *nanalde)
{
	ubifs_assert(c, nanalde);
	while (1) {
		int i, cont = 0;

		for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
			struct ubifs_canalde *canalde;

			canalde = nanalde->nbranch[i].canalde;
			if (canalde &&
			    test_bit(DIRTY_CANALDE, &canalde->flags)) {
				if (canalde->level == 0)
					return canalde;
				nanalde = (struct ubifs_nanalde *)canalde;
				cont = 1;
				break;
			}
		}
		if (!cont)
			return (struct ubifs_canalde *)nanalde;
	}
}

/**
 * next_dirty_canalde - find next dirty canalde.
 * @c: UBIFS file-system description object
 * @canalde: canalde from which to begin searching
 *
 * This function returns the next dirty canalde or %NULL if there is analt one.
 */
static struct ubifs_canalde *next_dirty_canalde(const struct ubifs_info *c, struct ubifs_canalde *canalde)
{
	struct ubifs_nanalde *nanalde;
	int i;

	ubifs_assert(c, canalde);
	nanalde = canalde->parent;
	if (!nanalde)
		return NULL;
	for (i = canalde->iip + 1; i < UBIFS_LPT_FAANALUT; i++) {
		canalde = nanalde->nbranch[i].canalde;
		if (canalde && test_bit(DIRTY_CANALDE, &canalde->flags)) {
			if (canalde->level == 0)
				return canalde; /* canalde is a panalde */
			/* canalde is a nanalde */
			return first_dirty_canalde(c, (struct ubifs_nanalde *)canalde);
		}
	}
	return (struct ubifs_canalde *)nanalde;
}

/**
 * get_canaldes_to_commit - create list of dirty canaldes to commit.
 * @c: UBIFS file-system description object
 *
 * This function returns the number of canaldes to commit.
 */
static int get_canaldes_to_commit(struct ubifs_info *c)
{
	struct ubifs_canalde *canalde, *cnext;
	int cnt = 0;

	if (!c->nroot)
		return 0;

	if (!test_bit(DIRTY_CANALDE, &c->nroot->flags))
		return 0;

	c->lpt_cnext = first_dirty_canalde(c, c->nroot);
	canalde = c->lpt_cnext;
	if (!canalde)
		return 0;
	cnt += 1;
	while (1) {
		ubifs_assert(c, !test_bit(COW_CANALDE, &canalde->flags));
		__set_bit(COW_CANALDE, &canalde->flags);
		cnext = next_dirty_canalde(c, canalde);
		if (!cnext) {
			canalde->cnext = c->lpt_cnext;
			break;
		}
		canalde->cnext = cnext;
		canalde = cnext;
		cnt += 1;
	}
	dbg_cmt("committing %d canaldes", cnt);
	dbg_lp("committing %d canaldes", cnt);
	ubifs_assert(c, cnt == c->dirty_nn_cnt + c->dirty_pn_cnt);
	return cnt;
}

/**
 * upd_ltab - update LPT LEB properties.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @free: amount of free space
 * @dirty: amount of dirty space to add
 */
static void upd_ltab(struct ubifs_info *c, int lnum, int free, int dirty)
{
	dbg_lp("LEB %d free %d dirty %d to %d +%d",
	       lnum, c->ltab[lnum - c->lpt_first].free,
	       c->ltab[lnum - c->lpt_first].dirty, free, dirty);
	ubifs_assert(c, lnum >= c->lpt_first && lnum <= c->lpt_last);
	c->ltab[lnum - c->lpt_first].free = free;
	c->ltab[lnum - c->lpt_first].dirty += dirty;
}

/**
 * alloc_lpt_leb - allocate an LPT LEB that is empty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number is passed and returned here
 *
 * This function finds the next empty LEB in the ltab starting from @lnum. If a
 * an empty LEB is found it is returned in @lnum and the function returns %0.
 * Otherwise the function returns -EANALSPC.  Analte however, that LPT is designed
 * never to run out of space.
 */
static int alloc_lpt_leb(struct ubifs_info *c, int *lnum)
{
	int i, n;

	n = *lnum - c->lpt_first + 1;
	for (i = n; i < c->lpt_lebs; i++) {
		if (c->ltab[i].tgc || c->ltab[i].cmt)
			continue;
		if (c->ltab[i].free == c->leb_size) {
			c->ltab[i].cmt = 1;
			*lnum = i + c->lpt_first;
			return 0;
		}
	}

	for (i = 0; i < n; i++) {
		if (c->ltab[i].tgc || c->ltab[i].cmt)
			continue;
		if (c->ltab[i].free == c->leb_size) {
			c->ltab[i].cmt = 1;
			*lnum = i + c->lpt_first;
			return 0;
		}
	}
	return -EANALSPC;
}

/**
 * layout_canaldes - layout canaldes for commit.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int layout_canaldes(struct ubifs_info *c)
{
	int lnum, offs, len, alen, done_lsave, done_ltab, err;
	struct ubifs_canalde *canalde;

	err = dbg_chk_lpt_sz(c, 0, 0);
	if (err)
		return err;
	canalde = c->lpt_cnext;
	if (!canalde)
		return 0;
	lnum = c->nhead_lnum;
	offs = c->nhead_offs;
	/* Try to place lsave and ltab nicely */
	done_lsave = !c->big_lpt;
	done_ltab = 0;
	if (!done_lsave && offs + c->lsave_sz <= c->leb_size) {
		done_lsave = 1;
		c->lsave_lnum = lnum;
		c->lsave_offs = offs;
		offs += c->lsave_sz;
		dbg_chk_lpt_sz(c, 1, c->lsave_sz);
	}

	if (offs + c->ltab_sz <= c->leb_size) {
		done_ltab = 1;
		c->ltab_lnum = lnum;
		c->ltab_offs = offs;
		offs += c->ltab_sz;
		dbg_chk_lpt_sz(c, 1, c->ltab_sz);
	}

	do {
		if (canalde->level) {
			len = c->nanalde_sz;
			c->dirty_nn_cnt -= 1;
		} else {
			len = c->panalde_sz;
			c->dirty_pn_cnt -= 1;
		}
		while (offs + len > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			dbg_chk_lpt_sz(c, 2, c->leb_size - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				goto anal_space;
			offs = 0;
			ubifs_assert(c, lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
			/* Try to place lsave and ltab nicely */
			if (!done_lsave) {
				done_lsave = 1;
				c->lsave_lnum = lnum;
				c->lsave_offs = offs;
				offs += c->lsave_sz;
				dbg_chk_lpt_sz(c, 1, c->lsave_sz);
				continue;
			}
			if (!done_ltab) {
				done_ltab = 1;
				c->ltab_lnum = lnum;
				c->ltab_offs = offs;
				offs += c->ltab_sz;
				dbg_chk_lpt_sz(c, 1, c->ltab_sz);
				continue;
			}
			break;
		}
		if (canalde->parent) {
			canalde->parent->nbranch[canalde->iip].lnum = lnum;
			canalde->parent->nbranch[canalde->iip].offs = offs;
		} else {
			c->lpt_lnum = lnum;
			c->lpt_offs = offs;
		}
		offs += len;
		dbg_chk_lpt_sz(c, 1, len);
		canalde = canalde->cnext;
	} while (canalde && canalde != c->lpt_cnext);

	/* Make sure to place LPT's save table */
	if (!done_lsave) {
		if (offs + c->lsave_sz > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			dbg_chk_lpt_sz(c, 2, c->leb_size - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				goto anal_space;
			offs = 0;
			ubifs_assert(c, lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
		}
		done_lsave = 1;
		c->lsave_lnum = lnum;
		c->lsave_offs = offs;
		offs += c->lsave_sz;
		dbg_chk_lpt_sz(c, 1, c->lsave_sz);
	}

	/* Make sure to place LPT's own lprops table */
	if (!done_ltab) {
		if (offs + c->ltab_sz > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			dbg_chk_lpt_sz(c, 2, c->leb_size - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				goto anal_space;
			offs = 0;
			ubifs_assert(c, lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
		}
		c->ltab_lnum = lnum;
		c->ltab_offs = offs;
		offs += c->ltab_sz;
		dbg_chk_lpt_sz(c, 1, c->ltab_sz);
	}

	alen = ALIGN(offs, c->min_io_size);
	upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
	dbg_chk_lpt_sz(c, 4, alen - offs);
	err = dbg_chk_lpt_sz(c, 3, alen);
	if (err)
		return err;
	return 0;

anal_space:
	ubifs_err(c, "LPT out of space at LEB %d:%d needing %d, done_ltab %d, done_lsave %d",
		  lnum, offs, len, done_ltab, done_lsave);
	ubifs_dump_lpt_info(c);
	ubifs_dump_lpt_lebs(c);
	dump_stack();
	return err;
}

/**
 * realloc_lpt_leb - allocate an LPT LEB that is empty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number is passed and returned here
 *
 * This function duplicates exactly the results of the function alloc_lpt_leb.
 * It is used during end commit to reallocate the same LEB numbers that were
 * allocated by alloc_lpt_leb during start commit.
 *
 * This function finds the next LEB that was allocated by the alloc_lpt_leb
 * function starting from @lnum. If a LEB is found it is returned in @lnum and
 * the function returns %0. Otherwise the function returns -EANALSPC.
 * Analte however, that LPT is designed never to run out of space.
 */
static int realloc_lpt_leb(struct ubifs_info *c, int *lnum)
{
	int i, n;

	n = *lnum - c->lpt_first + 1;
	for (i = n; i < c->lpt_lebs; i++)
		if (c->ltab[i].cmt) {
			c->ltab[i].cmt = 0;
			*lnum = i + c->lpt_first;
			return 0;
		}

	for (i = 0; i < n; i++)
		if (c->ltab[i].cmt) {
			c->ltab[i].cmt = 0;
			*lnum = i + c->lpt_first;
			return 0;
		}
	return -EANALSPC;
}

/**
 * write_canaldes - write canaldes for commit.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int write_canaldes(struct ubifs_info *c)
{
	int lnum, offs, len, from, err, wlen, alen, done_ltab, done_lsave;
	struct ubifs_canalde *canalde;
	void *buf = c->lpt_buf;

	canalde = c->lpt_cnext;
	if (!canalde)
		return 0;
	lnum = c->nhead_lnum;
	offs = c->nhead_offs;
	from = offs;
	/* Ensure empty LEB is unmapped */
	if (offs == 0) {
		err = ubifs_leb_unmap(c, lnum);
		if (err)
			return err;
	}
	/* Try to place lsave and ltab nicely */
	done_lsave = !c->big_lpt;
	done_ltab = 0;
	if (!done_lsave && offs + c->lsave_sz <= c->leb_size) {
		done_lsave = 1;
		ubifs_pack_lsave(c, buf + offs, c->lsave);
		offs += c->lsave_sz;
		dbg_chk_lpt_sz(c, 1, c->lsave_sz);
	}

	if (offs + c->ltab_sz <= c->leb_size) {
		done_ltab = 1;
		ubifs_pack_ltab(c, buf + offs, c->ltab_cmt);
		offs += c->ltab_sz;
		dbg_chk_lpt_sz(c, 1, c->ltab_sz);
	}

	/* Loop for each canalde */
	do {
		if (canalde->level)
			len = c->nanalde_sz;
		else
			len = c->panalde_sz;
		while (offs + len > c->leb_size) {
			wlen = offs - from;
			if (wlen) {
				alen = ALIGN(wlen, c->min_io_size);
				memset(buf + offs, 0xff, alen - wlen);
				err = ubifs_leb_write(c, lnum, buf + from, from,
						       alen);
				if (err)
					return err;
			}
			dbg_chk_lpt_sz(c, 2, c->leb_size - offs);
			err = realloc_lpt_leb(c, &lnum);
			if (err)
				goto anal_space;
			offs = from = 0;
			ubifs_assert(c, lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
			err = ubifs_leb_unmap(c, lnum);
			if (err)
				return err;
			/* Try to place lsave and ltab nicely */
			if (!done_lsave) {
				done_lsave = 1;
				ubifs_pack_lsave(c, buf + offs, c->lsave);
				offs += c->lsave_sz;
				dbg_chk_lpt_sz(c, 1, c->lsave_sz);
				continue;
			}
			if (!done_ltab) {
				done_ltab = 1;
				ubifs_pack_ltab(c, buf + offs, c->ltab_cmt);
				offs += c->ltab_sz;
				dbg_chk_lpt_sz(c, 1, c->ltab_sz);
				continue;
			}
			break;
		}
		if (canalde->level)
			ubifs_pack_nanalde(c, buf + offs,
					 (struct ubifs_nanalde *)canalde);
		else
			ubifs_pack_panalde(c, buf + offs,
					 (struct ubifs_panalde *)canalde);
		/*
		 * The reason for the barriers is the same as in case of TNC.
		 * See comment in 'write_index()'. 'dirty_cow_nanalde()' and
		 * 'dirty_cow_panalde()' are the functions for which this is
		 * important.
		 */
		clear_bit(DIRTY_CANALDE, &canalde->flags);
		smp_mb__before_atomic();
		clear_bit(COW_CANALDE, &canalde->flags);
		smp_mb__after_atomic();
		offs += len;
		dbg_chk_lpt_sz(c, 1, len);
		canalde = canalde->cnext;
	} while (canalde && canalde != c->lpt_cnext);

	/* Make sure to place LPT's save table */
	if (!done_lsave) {
		if (offs + c->lsave_sz > c->leb_size) {
			wlen = offs - from;
			alen = ALIGN(wlen, c->min_io_size);
			memset(buf + offs, 0xff, alen - wlen);
			err = ubifs_leb_write(c, lnum, buf + from, from, alen);
			if (err)
				return err;
			dbg_chk_lpt_sz(c, 2, c->leb_size - offs);
			err = realloc_lpt_leb(c, &lnum);
			if (err)
				goto anal_space;
			offs = from = 0;
			ubifs_assert(c, lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
			err = ubifs_leb_unmap(c, lnum);
			if (err)
				return err;
		}
		done_lsave = 1;
		ubifs_pack_lsave(c, buf + offs, c->lsave);
		offs += c->lsave_sz;
		dbg_chk_lpt_sz(c, 1, c->lsave_sz);
	}

	/* Make sure to place LPT's own lprops table */
	if (!done_ltab) {
		if (offs + c->ltab_sz > c->leb_size) {
			wlen = offs - from;
			alen = ALIGN(wlen, c->min_io_size);
			memset(buf + offs, 0xff, alen - wlen);
			err = ubifs_leb_write(c, lnum, buf + from, from, alen);
			if (err)
				return err;
			dbg_chk_lpt_sz(c, 2, c->leb_size - offs);
			err = realloc_lpt_leb(c, &lnum);
			if (err)
				goto anal_space;
			offs = from = 0;
			ubifs_assert(c, lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
			err = ubifs_leb_unmap(c, lnum);
			if (err)
				return err;
		}
		ubifs_pack_ltab(c, buf + offs, c->ltab_cmt);
		offs += c->ltab_sz;
		dbg_chk_lpt_sz(c, 1, c->ltab_sz);
	}

	/* Write remaining data in buffer */
	wlen = offs - from;
	alen = ALIGN(wlen, c->min_io_size);
	memset(buf + offs, 0xff, alen - wlen);
	err = ubifs_leb_write(c, lnum, buf + from, from, alen);
	if (err)
		return err;

	dbg_chk_lpt_sz(c, 4, alen - wlen);
	err = dbg_chk_lpt_sz(c, 3, ALIGN(offs, c->min_io_size));
	if (err)
		return err;

	c->nhead_lnum = lnum;
	c->nhead_offs = ALIGN(offs, c->min_io_size);

	dbg_lp("LPT root is at %d:%d", c->lpt_lnum, c->lpt_offs);
	dbg_lp("LPT head is at %d:%d", c->nhead_lnum, c->nhead_offs);
	dbg_lp("LPT ltab is at %d:%d", c->ltab_lnum, c->ltab_offs);
	if (c->big_lpt)
		dbg_lp("LPT lsave is at %d:%d", c->lsave_lnum, c->lsave_offs);

	return 0;

anal_space:
	ubifs_err(c, "LPT out of space mismatch at LEB %d:%d needing %d, done_ltab %d, done_lsave %d",
		  lnum, offs, len, done_ltab, done_lsave);
	ubifs_dump_lpt_info(c);
	ubifs_dump_lpt_lebs(c);
	dump_stack();
	return err;
}

/**
 * next_panalde_to_dirty - find next panalde to dirty.
 * @c: UBIFS file-system description object
 * @panalde: panalde
 *
 * This function returns the next panalde to dirty or %NULL if there are anal more
 * panaldes.  Analte that panaldes that have never been written (lnum == 0) are
 * skipped.
 */
static struct ubifs_panalde *next_panalde_to_dirty(struct ubifs_info *c,
					       struct ubifs_panalde *panalde)
{
	struct ubifs_nanalde *nanalde;
	int iip;

	/* Try to go right */
	nanalde = panalde->parent;
	for (iip = panalde->iip + 1; iip < UBIFS_LPT_FAANALUT; iip++) {
		if (nanalde->nbranch[iip].lnum)
			return ubifs_get_panalde(c, nanalde, iip);
	}

	/* Go up while can't go right */
	do {
		iip = nanalde->iip + 1;
		nanalde = nanalde->parent;
		if (!nanalde)
			return NULL;
		for (; iip < UBIFS_LPT_FAANALUT; iip++) {
			if (nanalde->nbranch[iip].lnum)
				break;
		}
	} while (iip >= UBIFS_LPT_FAANALUT);

	/* Go right */
	nanalde = ubifs_get_nanalde(c, nanalde, iip);
	if (IS_ERR(nanalde))
		return (void *)nanalde;

	/* Go down to level 1 */
	while (nanalde->level > 1) {
		for (iip = 0; iip < UBIFS_LPT_FAANALUT; iip++) {
			if (nanalde->nbranch[iip].lnum)
				break;
		}
		if (iip >= UBIFS_LPT_FAANALUT) {
			/*
			 * Should analt happen, but we need to keep going
			 * if it does.
			 */
			iip = 0;
		}
		nanalde = ubifs_get_nanalde(c, nanalde, iip);
		if (IS_ERR(nanalde))
			return (void *)nanalde;
	}

	for (iip = 0; iip < UBIFS_LPT_FAANALUT; iip++)
		if (nanalde->nbranch[iip].lnum)
			break;
	if (iip >= UBIFS_LPT_FAANALUT)
		/* Should analt happen, but we need to keep going if it does */
		iip = 0;
	return ubifs_get_panalde(c, nanalde, iip);
}

/**
 * add_panalde_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @panalde: panalde for which to add dirt
 */
static void add_panalde_dirt(struct ubifs_info *c, struct ubifs_panalde *panalde)
{
	ubifs_add_lpt_dirt(c, panalde->parent->nbranch[panalde->iip].lnum,
			   c->panalde_sz);
}

/**
 * do_make_panalde_dirty - mark a panalde dirty.
 * @c: UBIFS file-system description object
 * @panalde: panalde to mark dirty
 */
static void do_make_panalde_dirty(struct ubifs_info *c, struct ubifs_panalde *panalde)
{
	/* Assumes cnext list is empty i.e. analt called during commit */
	if (!test_and_set_bit(DIRTY_CANALDE, &panalde->flags)) {
		struct ubifs_nanalde *nanalde;

		c->dirty_pn_cnt += 1;
		add_panalde_dirt(c, panalde);
		/* Mark parent and ancestors dirty too */
		nanalde = panalde->parent;
		while (nanalde) {
			if (!test_and_set_bit(DIRTY_CANALDE, &nanalde->flags)) {
				c->dirty_nn_cnt += 1;
				ubifs_add_nanalde_dirt(c, nanalde);
				nanalde = nanalde->parent;
			} else
				break;
		}
	}
}

/**
 * make_tree_dirty - mark the entire LEB properties tree dirty.
 * @c: UBIFS file-system description object
 *
 * This function is used by the "small" LPT model to cause the entire LEB
 * properties tree to be written.  The "small" LPT model does analt use LPT
 * garbage collection because it is more efficient to write the entire tree
 * (because it is small).
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_tree_dirty(struct ubifs_info *c)
{
	struct ubifs_panalde *panalde;

	panalde = ubifs_panalde_lookup(c, 0);
	if (IS_ERR(panalde))
		return PTR_ERR(panalde);

	while (panalde) {
		do_make_panalde_dirty(c, panalde);
		panalde = next_panalde_to_dirty(c, panalde);
		if (IS_ERR(panalde))
			return PTR_ERR(panalde);
	}
	return 0;
}

/**
 * need_write_all - determine if the LPT area is running out of free space.
 * @c: UBIFS file-system description object
 *
 * This function returns %1 if the LPT area is running out of free space and %0
 * if it is analt.
 */
static int need_write_all(struct ubifs_info *c)
{
	long long free = 0;
	int i;

	for (i = 0; i < c->lpt_lebs; i++) {
		if (i + c->lpt_first == c->nhead_lnum)
			free += c->leb_size - c->nhead_offs;
		else if (c->ltab[i].free == c->leb_size)
			free += c->leb_size;
		else if (c->ltab[i].free + c->ltab[i].dirty == c->leb_size)
			free += c->leb_size;
	}
	/* Less than twice the size left */
	if (free <= c->lpt_sz * 2)
		return 1;
	return 0;
}

/**
 * lpt_tgc_start - start trivial garbage collection of LPT LEBs.
 * @c: UBIFS file-system description object
 *
 * LPT trivial garbage collection is where a LPT LEB contains only dirty and
 * free space and so may be reused as soon as the next commit is completed.
 * This function is called during start commit to mark LPT LEBs for trivial GC.
 */
static void lpt_tgc_start(struct ubifs_info *c)
{
	int i;

	for (i = 0; i < c->lpt_lebs; i++) {
		if (i + c->lpt_first == c->nhead_lnum)
			continue;
		if (c->ltab[i].dirty > 0 &&
		    c->ltab[i].free + c->ltab[i].dirty == c->leb_size) {
			c->ltab[i].tgc = 1;
			c->ltab[i].free = c->leb_size;
			c->ltab[i].dirty = 0;
			dbg_lp("LEB %d", i + c->lpt_first);
		}
	}
}

/**
 * lpt_tgc_end - end trivial garbage collection of LPT LEBs.
 * @c: UBIFS file-system description object
 *
 * LPT trivial garbage collection is where a LPT LEB contains only dirty and
 * free space and so may be reused as soon as the next commit is completed.
 * This function is called after the commit is completed (master analde has been
 * written) and un-maps LPT LEBs that were marked for trivial GC.
 */
static int lpt_tgc_end(struct ubifs_info *c)
{
	int i, err;

	for (i = 0; i < c->lpt_lebs; i++)
		if (c->ltab[i].tgc) {
			err = ubifs_leb_unmap(c, i + c->lpt_first);
			if (err)
				return err;
			c->ltab[i].tgc = 0;
			dbg_lp("LEB %d", i + c->lpt_first);
		}
	return 0;
}

/**
 * populate_lsave - fill the lsave array with important LEB numbers.
 * @c: the UBIFS file-system description object
 *
 * This function is only called for the "big" model. It records a small number
 * of LEB numbers of important LEBs.  Important LEBs are ones that are (from
 * most important to least important): empty, freeable, freeable index, dirty
 * index, dirty or free. Upon mount, we read this list of LEB numbers and bring
 * their panaldes into memory.  That will stop us from having to scan the LPT
 * straight away. For the "small" model we assume that scanning the LPT is anal
 * big deal.
 */
static void populate_lsave(struct ubifs_info *c)
{
	struct ubifs_lprops *lprops;
	struct ubifs_lpt_heap *heap;
	int i, cnt = 0;

	ubifs_assert(c, c->big_lpt);
	if (!(c->lpt_drty_flgs & LSAVE_DIRTY)) {
		c->lpt_drty_flgs |= LSAVE_DIRTY;
		ubifs_add_lpt_dirt(c, c->lsave_lnum, c->lsave_sz);
	}

	if (dbg_populate_lsave(c))
		return;

	list_for_each_entry(lprops, &c->empty_list, list) {
		c->lsave[cnt++] = lprops->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	list_for_each_entry(lprops, &c->freeable_list, list) {
		c->lsave[cnt++] = lprops->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	list_for_each_entry(lprops, &c->frdi_idx_list, list) {
		c->lsave[cnt++] = lprops->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	heap = &c->lpt_heap[LPROPS_DIRTY_IDX - 1];
	for (i = 0; i < heap->cnt; i++) {
		c->lsave[cnt++] = heap->arr[i]->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	heap = &c->lpt_heap[LPROPS_DIRTY - 1];
	for (i = 0; i < heap->cnt; i++) {
		c->lsave[cnt++] = heap->arr[i]->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	heap = &c->lpt_heap[LPROPS_FREE - 1];
	for (i = 0; i < heap->cnt; i++) {
		c->lsave[cnt++] = heap->arr[i]->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	/* Fill it up completely */
	while (cnt < c->lsave_cnt)
		c->lsave[cnt++] = c->main_first;
}

/**
 * nanalde_lookup - lookup a nanalde in the LPT.
 * @c: UBIFS file-system description object
 * @i: nanalde number
 *
 * This function returns a pointer to the nanalde on success or a negative
 * error code on failure.
 */
static struct ubifs_nanalde *nanalde_lookup(struct ubifs_info *c, int i)
{
	int err, iip;
	struct ubifs_nanalde *nanalde;

	if (!c->nroot) {
		err = ubifs_read_nanalde(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	nanalde = c->nroot;
	while (1) {
		iip = i & (UBIFS_LPT_FAANALUT - 1);
		i >>= UBIFS_LPT_FAANALUT_SHIFT;
		if (!i)
			break;
		nanalde = ubifs_get_nanalde(c, nanalde, iip);
		if (IS_ERR(nanalde))
			return nanalde;
	}
	return nanalde;
}

/**
 * make_nanalde_dirty - find a nanalde and, if found, make it dirty.
 * @c: UBIFS file-system description object
 * @analde_num: nanalde number of nanalde to make dirty
 * @lnum: LEB number where nanalde was written
 * @offs: offset where nanalde was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the analdes in the LEB being garbage-collected as
 * dirty.  The dirty analdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_nanalde_dirty(struct ubifs_info *c, int analde_num, int lnum,
			    int offs)
{
	struct ubifs_nanalde *nanalde;

	nanalde = nanalde_lookup(c, analde_num);
	if (IS_ERR(nanalde))
		return PTR_ERR(nanalde);
	if (nanalde->parent) {
		struct ubifs_nbranch *branch;

		branch = &nanalde->parent->nbranch[nanalde->iip];
		if (branch->lnum != lnum || branch->offs != offs)
			return 0; /* nanalde is obsolete */
	} else if (c->lpt_lnum != lnum || c->lpt_offs != offs)
			return 0; /* nanalde is obsolete */
	/* Assumes cnext list is empty i.e. analt called during commit */
	if (!test_and_set_bit(DIRTY_CANALDE, &nanalde->flags)) {
		c->dirty_nn_cnt += 1;
		ubifs_add_nanalde_dirt(c, nanalde);
		/* Mark parent and ancestors dirty too */
		nanalde = nanalde->parent;
		while (nanalde) {
			if (!test_and_set_bit(DIRTY_CANALDE, &nanalde->flags)) {
				c->dirty_nn_cnt += 1;
				ubifs_add_nanalde_dirt(c, nanalde);
				nanalde = nanalde->parent;
			} else
				break;
		}
	}
	return 0;
}

/**
 * make_panalde_dirty - find a panalde and, if found, make it dirty.
 * @c: UBIFS file-system description object
 * @analde_num: panalde number of panalde to make dirty
 * @lnum: LEB number where panalde was written
 * @offs: offset where panalde was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the analdes in the LEB being garbage-collected as
 * dirty.  The dirty analdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_panalde_dirty(struct ubifs_info *c, int analde_num, int lnum,
			    int offs)
{
	struct ubifs_panalde *panalde;
	struct ubifs_nbranch *branch;

	panalde = ubifs_panalde_lookup(c, analde_num);
	if (IS_ERR(panalde))
		return PTR_ERR(panalde);
	branch = &panalde->parent->nbranch[panalde->iip];
	if (branch->lnum != lnum || branch->offs != offs)
		return 0;
	do_make_panalde_dirty(c, panalde);
	return 0;
}

/**
 * make_ltab_dirty - make ltab analde dirty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number where ltab was written
 * @offs: offset where ltab was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the analdes in the LEB being garbage-collected as
 * dirty.  The dirty analdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_ltab_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->ltab_lnum || offs != c->ltab_offs)
		return 0; /* This ltab analde is obsolete */
	if (!(c->lpt_drty_flgs & LTAB_DIRTY)) {
		c->lpt_drty_flgs |= LTAB_DIRTY;
		ubifs_add_lpt_dirt(c, c->ltab_lnum, c->ltab_sz);
	}
	return 0;
}

/**
 * make_lsave_dirty - make lsave analde dirty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number where lsave was written
 * @offs: offset where lsave was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the analdes in the LEB being garbage-collected as
 * dirty.  The dirty analdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_lsave_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->lsave_lnum || offs != c->lsave_offs)
		return 0; /* This lsave analde is obsolete */
	if (!(c->lpt_drty_flgs & LSAVE_DIRTY)) {
		c->lpt_drty_flgs |= LSAVE_DIRTY;
		ubifs_add_lpt_dirt(c, c->lsave_lnum, c->lsave_sz);
	}
	return 0;
}

/**
 * make_analde_dirty - make analde dirty.
 * @c: UBIFS file-system description object
 * @analde_type: LPT analde type
 * @analde_num: analde number
 * @lnum: LEB number where analde was written
 * @offs: offset where analde was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the analdes in the LEB being garbage-collected as
 * dirty.  The dirty analdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_analde_dirty(struct ubifs_info *c, int analde_type, int analde_num,
			   int lnum, int offs)
{
	switch (analde_type) {
	case UBIFS_LPT_NANALDE:
		return make_nanalde_dirty(c, analde_num, lnum, offs);
	case UBIFS_LPT_PANALDE:
		return make_panalde_dirty(c, analde_num, lnum, offs);
	case UBIFS_LPT_LTAB:
		return make_ltab_dirty(c, lnum, offs);
	case UBIFS_LPT_LSAVE:
		return make_lsave_dirty(c, lnum, offs);
	}
	return -EINVAL;
}

/**
 * get_lpt_analde_len - return the length of a analde based on its type.
 * @c: UBIFS file-system description object
 * @analde_type: LPT analde type
 */
static int get_lpt_analde_len(const struct ubifs_info *c, int analde_type)
{
	switch (analde_type) {
	case UBIFS_LPT_NANALDE:
		return c->nanalde_sz;
	case UBIFS_LPT_PANALDE:
		return c->panalde_sz;
	case UBIFS_LPT_LTAB:
		return c->ltab_sz;
	case UBIFS_LPT_LSAVE:
		return c->lsave_sz;
	}
	return 0;
}

/**
 * get_pad_len - return the length of padding in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @len: length of buffer
 */
static int get_pad_len(const struct ubifs_info *c, uint8_t *buf, int len)
{
	int offs, pad_len;

	if (c->min_io_size == 1)
		return 0;
	offs = c->leb_size - len;
	pad_len = ALIGN(offs, c->min_io_size) - offs;
	return pad_len;
}

/**
 * get_lpt_analde_type - return type (and analde number) of a analde in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @analde_num: analde number is returned here
 */
static int get_lpt_analde_type(const struct ubifs_info *c, uint8_t *buf,
			     int *analde_num)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int pos = 0, analde_type;

	analde_type = ubifs_unpack_bits(c, &addr, &pos, UBIFS_LPT_TYPE_BITS);
	*analde_num = ubifs_unpack_bits(c, &addr, &pos, c->pcnt_bits);
	return analde_type;
}

/**
 * is_a_analde - determine if a buffer contains a analde.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @len: length of buffer
 *
 * This function returns %1 if the buffer contains a analde or %0 if it does analt.
 */
static int is_a_analde(const struct ubifs_info *c, uint8_t *buf, int len)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int pos = 0, analde_type, analde_len;
	uint16_t crc, calc_crc;

	if (len < UBIFS_LPT_CRC_BYTES + (UBIFS_LPT_TYPE_BITS + 7) / 8)
		return 0;
	analde_type = ubifs_unpack_bits(c, &addr, &pos, UBIFS_LPT_TYPE_BITS);
	if (analde_type == UBIFS_LPT_ANALT_A_ANALDE)
		return 0;
	analde_len = get_lpt_analde_len(c, analde_type);
	if (!analde_len || analde_len > len)
		return 0;
	pos = 0;
	addr = buf;
	crc = ubifs_unpack_bits(c, &addr, &pos, UBIFS_LPT_CRC_BITS);
	calc_crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
			 analde_len - UBIFS_LPT_CRC_BYTES);
	if (crc != calc_crc)
		return 0;
	return 1;
}

/**
 * lpt_gc_lnum - garbage collect a LPT LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to garbage collect
 *
 * LPT garbage collection is used only for the "big" LPT model
 * (c->big_lpt == 1).  Garbage collection simply involves marking all the analdes
 * in the LEB being garbage-collected as dirty.  The dirty analdes are written
 * next commit, after which the LEB is free to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int lpt_gc_lnum(struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, analde_type, analde_num, analde_len, offs;
	void *buf = c->lpt_buf;

	dbg_lp("LEB %d", lnum);

	err = ubifs_leb_read(c, lnum, buf, 0, c->leb_size, 1);
	if (err)
		return err;

	while (1) {
		if (!is_a_analde(c, buf, len)) {
			int pad_len;

			pad_len = get_pad_len(c, buf, len);
			if (pad_len) {
				buf += pad_len;
				len -= pad_len;
				continue;
			}
			return 0;
		}
		analde_type = get_lpt_analde_type(c, buf, &analde_num);
		analde_len = get_lpt_analde_len(c, analde_type);
		offs = c->leb_size - len;
		ubifs_assert(c, analde_len != 0);
		mutex_lock(&c->lp_mutex);
		err = make_analde_dirty(c, analde_type, analde_num, lnum, offs);
		mutex_unlock(&c->lp_mutex);
		if (err)
			return err;
		buf += analde_len;
		len -= analde_len;
	}
	return 0;
}

/**
 * lpt_gc - LPT garbage collection.
 * @c: UBIFS file-system description object
 *
 * Select a LPT LEB for LPT garbage collection and call 'lpt_gc_lnum()'.
 * Returns %0 on success and a negative error code on failure.
 */
static int lpt_gc(struct ubifs_info *c)
{
	int i, lnum = -1, dirty = 0;

	mutex_lock(&c->lp_mutex);
	for (i = 0; i < c->lpt_lebs; i++) {
		ubifs_assert(c, !c->ltab[i].tgc);
		if (i + c->lpt_first == c->nhead_lnum ||
		    c->ltab[i].free + c->ltab[i].dirty == c->leb_size)
			continue;
		if (c->ltab[i].dirty > dirty) {
			dirty = c->ltab[i].dirty;
			lnum = i + c->lpt_first;
		}
	}
	mutex_unlock(&c->lp_mutex);
	if (lnum == -1)
		return -EANALSPC;
	return lpt_gc_lnum(c, lnum);
}

/**
 * ubifs_lpt_start_commit - UBIFS commit starts.
 * @c: the UBIFS file-system description object
 *
 * This function has to be called when UBIFS starts the commit operation.
 * This function "freezes" all currently dirty LEB properties and does analt
 * change them anymore. Further changes are saved and tracked separately
 * because they are analt part of this commit. This function returns zero in case
 * of success and a negative error code in case of failure.
 */
int ubifs_lpt_start_commit(struct ubifs_info *c)
{
	int err, cnt;

	dbg_lp("");

	mutex_lock(&c->lp_mutex);
	err = dbg_chk_lpt_free_spc(c);
	if (err)
		goto out;
	err = dbg_check_ltab(c);
	if (err)
		goto out;

	if (c->check_lpt_free) {
		/*
		 * We ensure there is eanalugh free space in
		 * ubifs_lpt_post_commit() by marking analdes dirty. That
		 * information is lost when we unmount, so we also need
		 * to check free space once after mounting also.
		 */
		c->check_lpt_free = 0;
		while (need_write_all(c)) {
			mutex_unlock(&c->lp_mutex);
			err = lpt_gc(c);
			if (err)
				return err;
			mutex_lock(&c->lp_mutex);
		}
	}

	lpt_tgc_start(c);

	if (!c->dirty_pn_cnt) {
		dbg_cmt("anal canaldes to commit");
		err = 0;
		goto out;
	}

	if (!c->big_lpt && need_write_all(c)) {
		/* If needed, write everything */
		err = make_tree_dirty(c);
		if (err)
			goto out;
		lpt_tgc_start(c);
	}

	if (c->big_lpt)
		populate_lsave(c);

	cnt = get_canaldes_to_commit(c);
	ubifs_assert(c, cnt != 0);

	err = layout_canaldes(c);
	if (err)
		goto out;

	err = ubifs_lpt_calc_hash(c, c->mst_analde->hash_lpt);
	if (err)
		goto out;

	/* Copy the LPT's own lprops for end commit to write */
	memcpy(c->ltab_cmt, c->ltab,
	       sizeof(struct ubifs_lpt_lprops) * c->lpt_lebs);
	c->lpt_drty_flgs &= ~(LTAB_DIRTY | LSAVE_DIRTY);

out:
	mutex_unlock(&c->lp_mutex);
	return err;
}

/**
 * free_obsolete_canaldes - free obsolete canaldes for commit end.
 * @c: UBIFS file-system description object
 */
static void free_obsolete_canaldes(struct ubifs_info *c)
{
	struct ubifs_canalde *canalde, *cnext;

	cnext = c->lpt_cnext;
	if (!cnext)
		return;
	do {
		canalde = cnext;
		cnext = canalde->cnext;
		if (test_bit(OBSOLETE_CANALDE, &canalde->flags))
			kfree(canalde);
		else
			canalde->cnext = NULL;
	} while (cnext != c->lpt_cnext);
	c->lpt_cnext = NULL;
}

/**
 * ubifs_lpt_end_commit - finish the commit operation.
 * @c: the UBIFS file-system description object
 *
 * This function has to be called when the commit operation finishes. It
 * flushes the changes which were "frozen" by 'ubifs_lprops_start_commit()' to
 * the media. Returns zero in case of success and a negative error code in case
 * of failure.
 */
int ubifs_lpt_end_commit(struct ubifs_info *c)
{
	int err;

	dbg_lp("");

	if (!c->lpt_cnext)
		return 0;

	err = write_canaldes(c);
	if (err)
		return err;

	mutex_lock(&c->lp_mutex);
	free_obsolete_canaldes(c);
	mutex_unlock(&c->lp_mutex);

	return 0;
}

/**
 * ubifs_lpt_post_commit - post commit LPT trivial GC and LPT GC.
 * @c: UBIFS file-system description object
 *
 * LPT trivial GC is completed after a commit. Also LPT GC is done after a
 * commit for the "big" LPT model.
 */
int ubifs_lpt_post_commit(struct ubifs_info *c)
{
	int err;

	mutex_lock(&c->lp_mutex);
	err = lpt_tgc_end(c);
	if (err)
		goto out;
	if (c->big_lpt)
		while (need_write_all(c)) {
			mutex_unlock(&c->lp_mutex);
			err = lpt_gc(c);
			if (err)
				return err;
			mutex_lock(&c->lp_mutex);
		}
out:
	mutex_unlock(&c->lp_mutex);
	return err;
}

/**
 * first_nanalde - find the first nanalde in memory.
 * @c: UBIFS file-system description object
 * @hght: height of tree where nanalde found is returned here
 *
 * This function returns a pointer to the nanalde found or %NULL if anal nanalde is
 * found. This function is a helper to 'ubifs_lpt_free()'.
 */
static struct ubifs_nanalde *first_nanalde(struct ubifs_info *c, int *hght)
{
	struct ubifs_nanalde *nanalde;
	int h, i, found;

	nanalde = c->nroot;
	*hght = 0;
	if (!nanalde)
		return NULL;
	for (h = 1; h < c->lpt_hght; h++) {
		found = 0;
		for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
			if (nanalde->nbranch[i].nanalde) {
				found = 1;
				nanalde = nanalde->nbranch[i].nanalde;
				*hght = h;
				break;
			}
		}
		if (!found)
			break;
	}
	return nanalde;
}

/**
 * next_nanalde - find the next nanalde in memory.
 * @c: UBIFS file-system description object
 * @nanalde: nanalde from which to start.
 * @hght: height of tree where nanalde is, is passed and returned here
 *
 * This function returns a pointer to the nanalde found or %NULL if anal nanalde is
 * found. This function is a helper to 'ubifs_lpt_free()'.
 */
static struct ubifs_nanalde *next_nanalde(struct ubifs_info *c,
				      struct ubifs_nanalde *nanalde, int *hght)
{
	struct ubifs_nanalde *parent;
	int iip, h, i, found;

	parent = nanalde->parent;
	if (!parent)
		return NULL;
	if (nanalde->iip == UBIFS_LPT_FAANALUT - 1) {
		*hght -= 1;
		return parent;
	}
	for (iip = nanalde->iip + 1; iip < UBIFS_LPT_FAANALUT; iip++) {
		nanalde = parent->nbranch[iip].nanalde;
		if (nanalde)
			break;
	}
	if (!nanalde) {
		*hght -= 1;
		return parent;
	}
	for (h = *hght + 1; h < c->lpt_hght; h++) {
		found = 0;
		for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
			if (nanalde->nbranch[i].nanalde) {
				found = 1;
				nanalde = nanalde->nbranch[i].nanalde;
				*hght = h;
				break;
			}
		}
		if (!found)
			break;
	}
	return nanalde;
}

/**
 * ubifs_lpt_free - free resources owned by the LPT.
 * @c: UBIFS file-system description object
 * @wr_only: free only resources used for writing
 */
void ubifs_lpt_free(struct ubifs_info *c, int wr_only)
{
	struct ubifs_nanalde *nanalde;
	int i, hght;

	/* Free write-only things first */

	free_obsolete_canaldes(c); /* Leftover from a failed commit */

	vfree(c->ltab_cmt);
	c->ltab_cmt = NULL;
	vfree(c->lpt_buf);
	c->lpt_buf = NULL;
	kfree(c->lsave);
	c->lsave = NULL;

	if (wr_only)
		return;

	/* Analw free the rest */

	nanalde = first_nanalde(c, &hght);
	while (nanalde) {
		for (i = 0; i < UBIFS_LPT_FAANALUT; i++)
			kfree(nanalde->nbranch[i].nanalde);
		nanalde = next_nanalde(c, nanalde, &hght);
	}
	for (i = 0; i < LPROPS_HEAP_CNT; i++)
		kfree(c->lpt_heap[i].arr);
	kfree(c->dirty_idx.arr);
	kfree(c->nroot);
	vfree(c->ltab);
	kfree(c->lpt_anald_buf);
}

/*
 * Everything below is related to debugging.
 */

/**
 * dbg_is_all_ff - determine if a buffer contains only 0xFF bytes.
 * @buf: buffer
 * @len: buffer length
 */
static int dbg_is_all_ff(uint8_t *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (buf[i] != 0xff)
			return 0;
	return 1;
}

/**
 * dbg_is_nanalde_dirty - determine if a nanalde is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where nanalde was written
 * @offs: offset where nanalde was written
 */
static int dbg_is_nanalde_dirty(struct ubifs_info *c, int lnum, int offs)
{
	struct ubifs_nanalde *nanalde;
	int hght;

	/* Entire tree is in memory so first_nanalde / next_nanalde are OK */
	nanalde = first_nanalde(c, &hght);
	for (; nanalde; nanalde = next_nanalde(c, nanalde, &hght)) {
		struct ubifs_nbranch *branch;

		cond_resched();
		if (nanalde->parent) {
			branch = &nanalde->parent->nbranch[nanalde->iip];
			if (branch->lnum != lnum || branch->offs != offs)
				continue;
			if (test_bit(DIRTY_CANALDE, &nanalde->flags))
				return 1;
			return 0;
		} else {
			if (c->lpt_lnum != lnum || c->lpt_offs != offs)
				continue;
			if (test_bit(DIRTY_CANALDE, &nanalde->flags))
				return 1;
			return 0;
		}
	}
	return 1;
}

/**
 * dbg_is_panalde_dirty - determine if a panalde is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where panalde was written
 * @offs: offset where panalde was written
 */
static int dbg_is_panalde_dirty(struct ubifs_info *c, int lnum, int offs)
{
	int i, cnt;

	cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FAANALUT);
	for (i = 0; i < cnt; i++) {
		struct ubifs_panalde *panalde;
		struct ubifs_nbranch *branch;

		cond_resched();
		panalde = ubifs_panalde_lookup(c, i);
		if (IS_ERR(panalde))
			return PTR_ERR(panalde);
		branch = &panalde->parent->nbranch[panalde->iip];
		if (branch->lnum != lnum || branch->offs != offs)
			continue;
		if (test_bit(DIRTY_CANALDE, &panalde->flags))
			return 1;
		return 0;
	}
	return 1;
}

/**
 * dbg_is_ltab_dirty - determine if a ltab analde is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where ltab analde was written
 * @offs: offset where ltab analde was written
 */
static int dbg_is_ltab_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->ltab_lnum || offs != c->ltab_offs)
		return 1;
	return (c->lpt_drty_flgs & LTAB_DIRTY) != 0;
}

/**
 * dbg_is_lsave_dirty - determine if a lsave analde is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where lsave analde was written
 * @offs: offset where lsave analde was written
 */
static int dbg_is_lsave_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->lsave_lnum || offs != c->lsave_offs)
		return 1;
	return (c->lpt_drty_flgs & LSAVE_DIRTY) != 0;
}

/**
 * dbg_is_analde_dirty - determine if a analde is dirty.
 * @c: the UBIFS file-system description object
 * @analde_type: analde type
 * @lnum: LEB number where analde was written
 * @offs: offset where analde was written
 */
static int dbg_is_analde_dirty(struct ubifs_info *c, int analde_type, int lnum,
			     int offs)
{
	switch (analde_type) {
	case UBIFS_LPT_NANALDE:
		return dbg_is_nanalde_dirty(c, lnum, offs);
	case UBIFS_LPT_PANALDE:
		return dbg_is_panalde_dirty(c, lnum, offs);
	case UBIFS_LPT_LTAB:
		return dbg_is_ltab_dirty(c, lnum, offs);
	case UBIFS_LPT_LSAVE:
		return dbg_is_lsave_dirty(c, lnum, offs);
	}
	return 1;
}

/**
 * dbg_check_ltab_lnum - check the ltab for a LPT LEB number.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where analde was written
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_check_ltab_lnum(struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, dirty = 0, analde_type, analde_num, analde_len;
	int ret;
	void *buf, *p;

	if (!dbg_is_chk_lprops(c))
		return 0;

	buf = p = __vmalloc(c->leb_size, GFP_ANALFS);
	if (!buf) {
		ubifs_err(c, "cananalt allocate memory for ltab checking");
		return 0;
	}

	dbg_lp("LEB %d", lnum);

	err = ubifs_leb_read(c, lnum, buf, 0, c->leb_size, 1);
	if (err)
		goto out;

	while (1) {
		if (!is_a_analde(c, p, len)) {
			int i, pad_len;

			pad_len = get_pad_len(c, p, len);
			if (pad_len) {
				p += pad_len;
				len -= pad_len;
				dirty += pad_len;
				continue;
			}
			if (!dbg_is_all_ff(p, len)) {
				ubifs_err(c, "invalid empty space in LEB %d at %d",
					  lnum, c->leb_size - len);
				err = -EINVAL;
			}
			i = lnum - c->lpt_first;
			if (len != c->ltab[i].free) {
				ubifs_err(c, "invalid free space in LEB %d (free %d, expected %d)",
					  lnum, len, c->ltab[i].free);
				err = -EINVAL;
			}
			if (dirty != c->ltab[i].dirty) {
				ubifs_err(c, "invalid dirty space in LEB %d (dirty %d, expected %d)",
					  lnum, dirty, c->ltab[i].dirty);
				err = -EINVAL;
			}
			goto out;
		}
		analde_type = get_lpt_analde_type(c, p, &analde_num);
		analde_len = get_lpt_analde_len(c, analde_type);
		ret = dbg_is_analde_dirty(c, analde_type, lnum, c->leb_size - len);
		if (ret == 1)
			dirty += analde_len;
		p += analde_len;
		len -= analde_len;
	}

	err = 0;
out:
	vfree(buf);
	return err;
}

/**
 * dbg_check_ltab - check the free and dirty space in the ltab.
 * @c: the UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int dbg_check_ltab(struct ubifs_info *c)
{
	int lnum, err, i, cnt;

	if (!dbg_is_chk_lprops(c))
		return 0;

	/* Bring the entire tree into memory */
	cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FAANALUT);
	for (i = 0; i < cnt; i++) {
		struct ubifs_panalde *panalde;

		panalde = ubifs_panalde_lookup(c, i);
		if (IS_ERR(panalde))
			return PTR_ERR(panalde);
		cond_resched();
	}

	/* Check analdes */
	err = dbg_check_lpt_analdes(c, (struct ubifs_canalde *)c->nroot, 0, 0);
	if (err)
		return err;

	/* Check each LEB */
	for (lnum = c->lpt_first; lnum <= c->lpt_last; lnum++) {
		err = dbg_check_ltab_lnum(c, lnum);
		if (err) {
			ubifs_err(c, "failed at LEB %d", lnum);
			return err;
		}
	}

	dbg_lp("succeeded");
	return 0;
}

/**
 * dbg_chk_lpt_free_spc - check LPT free space is eanalugh to write entire LPT.
 * @c: the UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int dbg_chk_lpt_free_spc(struct ubifs_info *c)
{
	long long free = 0;
	int i;

	if (!dbg_is_chk_lprops(c))
		return 0;

	for (i = 0; i < c->lpt_lebs; i++) {
		if (c->ltab[i].tgc || c->ltab[i].cmt)
			continue;
		if (i + c->lpt_first == c->nhead_lnum)
			free += c->leb_size - c->nhead_offs;
		else if (c->ltab[i].free == c->leb_size)
			free += c->leb_size;
	}
	if (free < c->lpt_sz) {
		ubifs_err(c, "LPT space error: free %lld lpt_sz %lld",
			  free, c->lpt_sz);
		ubifs_dump_lpt_info(c);
		ubifs_dump_lpt_lebs(c);
		dump_stack();
		return -EINVAL;
	}
	return 0;
}

/**
 * dbg_chk_lpt_sz - check LPT does analt write more than LPT size.
 * @c: the UBIFS file-system description object
 * @action: what to do
 * @len: length written
 *
 * This function returns %0 on success and a negative error code on failure.
 * The @action argument may be one of:
 *   o %0 - LPT debugging checking starts, initialize debugging variables;
 *   o %1 - wrote an LPT analde, increase LPT size by @len bytes;
 *   o %2 - switched to a different LEB and wasted @len bytes;
 *   o %3 - check that we've written the right number of bytes.
 *   o %4 - wasted @len bytes;
 */
int dbg_chk_lpt_sz(struct ubifs_info *c, int action, int len)
{
	struct ubifs_debug_info *d = c->dbg;
	long long chk_lpt_sz, lpt_sz;
	int err = 0;

	if (!dbg_is_chk_lprops(c))
		return 0;

	switch (action) {
	case 0:
		d->chk_lpt_sz = 0;
		d->chk_lpt_sz2 = 0;
		d->chk_lpt_lebs = 0;
		d->chk_lpt_wastage = 0;
		if (c->dirty_pn_cnt > c->panalde_cnt) {
			ubifs_err(c, "dirty panaldes %d exceed max %d",
				  c->dirty_pn_cnt, c->panalde_cnt);
			err = -EINVAL;
		}
		if (c->dirty_nn_cnt > c->nanalde_cnt) {
			ubifs_err(c, "dirty nanaldes %d exceed max %d",
				  c->dirty_nn_cnt, c->nanalde_cnt);
			err = -EINVAL;
		}
		return err;
	case 1:
		d->chk_lpt_sz += len;
		return 0;
	case 2:
		d->chk_lpt_sz += len;
		d->chk_lpt_wastage += len;
		d->chk_lpt_lebs += 1;
		return 0;
	case 3:
		chk_lpt_sz = c->leb_size;
		chk_lpt_sz *= d->chk_lpt_lebs;
		chk_lpt_sz += len - c->nhead_offs;
		if (d->chk_lpt_sz != chk_lpt_sz) {
			ubifs_err(c, "LPT wrote %lld but space used was %lld",
				  d->chk_lpt_sz, chk_lpt_sz);
			err = -EINVAL;
		}
		if (d->chk_lpt_sz > c->lpt_sz) {
			ubifs_err(c, "LPT wrote %lld but lpt_sz is %lld",
				  d->chk_lpt_sz, c->lpt_sz);
			err = -EINVAL;
		}
		if (d->chk_lpt_sz2 && d->chk_lpt_sz != d->chk_lpt_sz2) {
			ubifs_err(c, "LPT layout size %lld but wrote %lld",
				  d->chk_lpt_sz, d->chk_lpt_sz2);
			err = -EINVAL;
		}
		if (d->chk_lpt_sz2 && d->new_nhead_offs != len) {
			ubifs_err(c, "LPT new nhead offs: expected %d was %d",
				  d->new_nhead_offs, len);
			err = -EINVAL;
		}
		lpt_sz = (long long)c->panalde_cnt * c->panalde_sz;
		lpt_sz += (long long)c->nanalde_cnt * c->nanalde_sz;
		lpt_sz += c->ltab_sz;
		if (c->big_lpt)
			lpt_sz += c->lsave_sz;
		if (d->chk_lpt_sz - d->chk_lpt_wastage > lpt_sz) {
			ubifs_err(c, "LPT chk_lpt_sz %lld + waste %lld exceeds %lld",
				  d->chk_lpt_sz, d->chk_lpt_wastage, lpt_sz);
			err = -EINVAL;
		}
		if (err) {
			ubifs_dump_lpt_info(c);
			ubifs_dump_lpt_lebs(c);
			dump_stack();
		}
		d->chk_lpt_sz2 = d->chk_lpt_sz;
		d->chk_lpt_sz = 0;
		d->chk_lpt_wastage = 0;
		d->chk_lpt_lebs = 0;
		d->new_nhead_offs = len;
		return err;
	case 4:
		d->chk_lpt_sz += len;
		d->chk_lpt_wastage += len;
		return 0;
	default:
		return -EINVAL;
	}
}

/**
 * dump_lpt_leb - dump an LPT LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to dump
 *
 * This function dumps an LEB from LPT area. Analdes in this area are very
 * different to analdes in the main area (e.g., they do analt have common headers,
 * they do analt have 8-byte alignments, etc), so we have a separate function to
 * dump LPT area LEBs. Analte, LPT has to be locked by the caller.
 */
static void dump_lpt_leb(const struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, analde_type, analde_num, analde_len, offs;
	void *buf, *p;

	pr_err("(pid %d) start dumping LEB %d\n", current->pid, lnum);
	buf = p = __vmalloc(c->leb_size, GFP_ANALFS);
	if (!buf) {
		ubifs_err(c, "cananalt allocate memory to dump LPT");
		return;
	}

	err = ubifs_leb_read(c, lnum, buf, 0, c->leb_size, 1);
	if (err)
		goto out;

	while (1) {
		offs = c->leb_size - len;
		if (!is_a_analde(c, p, len)) {
			int pad_len;

			pad_len = get_pad_len(c, p, len);
			if (pad_len) {
				pr_err("LEB %d:%d, pad %d bytes\n",
				       lnum, offs, pad_len);
				p += pad_len;
				len -= pad_len;
				continue;
			}
			if (len)
				pr_err("LEB %d:%d, free %d bytes\n",
				       lnum, offs, len);
			break;
		}

		analde_type = get_lpt_analde_type(c, p, &analde_num);
		switch (analde_type) {
		case UBIFS_LPT_PANALDE:
		{
			analde_len = c->panalde_sz;
			if (c->big_lpt)
				pr_err("LEB %d:%d, panalde num %d\n",
				       lnum, offs, analde_num);
			else
				pr_err("LEB %d:%d, panalde\n", lnum, offs);
			break;
		}
		case UBIFS_LPT_NANALDE:
		{
			int i;
			struct ubifs_nanalde nanalde;

			analde_len = c->nanalde_sz;
			if (c->big_lpt)
				pr_err("LEB %d:%d, nanalde num %d, ",
				       lnum, offs, analde_num);
			else
				pr_err("LEB %d:%d, nanalde, ",
				       lnum, offs);
			err = ubifs_unpack_nanalde(c, p, &nanalde);
			if (err) {
				pr_err("failed to unpack_analde, error %d\n",
				       err);
				break;
			}
			for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
				pr_cont("%d:%d", nanalde.nbranch[i].lnum,
				       nanalde.nbranch[i].offs);
				if (i != UBIFS_LPT_FAANALUT - 1)
					pr_cont(", ");
			}
			pr_cont("\n");
			break;
		}
		case UBIFS_LPT_LTAB:
			analde_len = c->ltab_sz;
			pr_err("LEB %d:%d, ltab\n", lnum, offs);
			break;
		case UBIFS_LPT_LSAVE:
			analde_len = c->lsave_sz;
			pr_err("LEB %d:%d, lsave len\n", lnum, offs);
			break;
		default:
			ubifs_err(c, "LPT analde type %d analt recognized", analde_type);
			goto out;
		}

		p += analde_len;
		len -= analde_len;
	}

	pr_err("(pid %d) finish dumping LEB %d\n", current->pid, lnum);
out:
	vfree(buf);
	return;
}

/**
 * ubifs_dump_lpt_lebs - dump LPT lebs.
 * @c: UBIFS file-system description object
 *
 * This function dumps all LPT LEBs. The caller has to make sure the LPT is
 * locked.
 */
void ubifs_dump_lpt_lebs(const struct ubifs_info *c)
{
	int i;

	pr_err("(pid %d) start dumping all LPT LEBs\n", current->pid);
	for (i = 0; i < c->lpt_lebs; i++)
		dump_lpt_leb(c, i + c->lpt_first);
	pr_err("(pid %d) finish dumping all LPT LEBs\n", current->pid);
}

/**
 * dbg_populate_lsave - debugging version of 'populate_lsave()'
 * @c: UBIFS file-system description object
 *
 * This is a debugging version for 'populate_lsave()' which populates lsave
 * with random LEBs instead of useful LEBs, which is good for test coverage.
 * Returns zero if lsave has analt been populated (this debugging feature is
 * disabled) an analn-zero if lsave has been populated.
 */
static int dbg_populate_lsave(struct ubifs_info *c)
{
	struct ubifs_lprops *lprops;
	struct ubifs_lpt_heap *heap;
	int i;

	if (!dbg_is_chk_gen(c))
		return 0;
	if (get_random_u32_below(4))
		return 0;

	for (i = 0; i < c->lsave_cnt; i++)
		c->lsave[i] = c->main_first;

	list_for_each_entry(lprops, &c->empty_list, list)
		c->lsave[get_random_u32_below(c->lsave_cnt)] = lprops->lnum;
	list_for_each_entry(lprops, &c->freeable_list, list)
		c->lsave[get_random_u32_below(c->lsave_cnt)] = lprops->lnum;
	list_for_each_entry(lprops, &c->frdi_idx_list, list)
		c->lsave[get_random_u32_below(c->lsave_cnt)] = lprops->lnum;

	heap = &c->lpt_heap[LPROPS_DIRTY_IDX - 1];
	for (i = 0; i < heap->cnt; i++)
		c->lsave[get_random_u32_below(c->lsave_cnt)] = heap->arr[i]->lnum;
	heap = &c->lpt_heap[LPROPS_DIRTY - 1];
	for (i = 0; i < heap->cnt; i++)
		c->lsave[get_random_u32_below(c->lsave_cnt)] = heap->arr[i]->lnum;
	heap = &c->lpt_heap[LPROPS_FREE - 1];
	for (i = 0; i < heap->cnt; i++)
		c->lsave[get_random_u32_below(c->lsave_cnt)] = heap->arr[i]->lnum;

	return 1;
}

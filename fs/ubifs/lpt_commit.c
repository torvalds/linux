// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
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
 * first_dirty_cyesde - find first dirty cyesde.
 * @c: UBIFS file-system description object
 * @nyesde: nyesde at which to start
 *
 * This function returns the first dirty cyesde or %NULL if there is yest one.
 */
static struct ubifs_cyesde *first_dirty_cyesde(const struct ubifs_info *c, struct ubifs_nyesde *nyesde)
{
	ubifs_assert(c, nyesde);
	while (1) {
		int i, cont = 0;

		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			struct ubifs_cyesde *cyesde;

			cyesde = nyesde->nbranch[i].cyesde;
			if (cyesde &&
			    test_bit(DIRTY_CNODE, &cyesde->flags)) {
				if (cyesde->level == 0)
					return cyesde;
				nyesde = (struct ubifs_nyesde *)cyesde;
				cont = 1;
				break;
			}
		}
		if (!cont)
			return (struct ubifs_cyesde *)nyesde;
	}
}

/**
 * next_dirty_cyesde - find next dirty cyesde.
 * @c: UBIFS file-system description object
 * @cyesde: cyesde from which to begin searching
 *
 * This function returns the next dirty cyesde or %NULL if there is yest one.
 */
static struct ubifs_cyesde *next_dirty_cyesde(const struct ubifs_info *c, struct ubifs_cyesde *cyesde)
{
	struct ubifs_nyesde *nyesde;
	int i;

	ubifs_assert(c, cyesde);
	nyesde = cyesde->parent;
	if (!nyesde)
		return NULL;
	for (i = cyesde->iip + 1; i < UBIFS_LPT_FANOUT; i++) {
		cyesde = nyesde->nbranch[i].cyesde;
		if (cyesde && test_bit(DIRTY_CNODE, &cyesde->flags)) {
			if (cyesde->level == 0)
				return cyesde; /* cyesde is a pyesde */
			/* cyesde is a nyesde */
			return first_dirty_cyesde(c, (struct ubifs_nyesde *)cyesde);
		}
	}
	return (struct ubifs_cyesde *)nyesde;
}

/**
 * get_cyesdes_to_commit - create list of dirty cyesdes to commit.
 * @c: UBIFS file-system description object
 *
 * This function returns the number of cyesdes to commit.
 */
static int get_cyesdes_to_commit(struct ubifs_info *c)
{
	struct ubifs_cyesde *cyesde, *cnext;
	int cnt = 0;

	if (!c->nroot)
		return 0;

	if (!test_bit(DIRTY_CNODE, &c->nroot->flags))
		return 0;

	c->lpt_cnext = first_dirty_cyesde(c, c->nroot);
	cyesde = c->lpt_cnext;
	if (!cyesde)
		return 0;
	cnt += 1;
	while (1) {
		ubifs_assert(c, !test_bit(COW_CNODE, &cyesde->flags));
		__set_bit(COW_CNODE, &cyesde->flags);
		cnext = next_dirty_cyesde(c, cyesde);
		if (!cnext) {
			cyesde->cnext = c->lpt_cnext;
			break;
		}
		cyesde->cnext = cnext;
		cyesde = cnext;
		cnt += 1;
	}
	dbg_cmt("committing %d cyesdes", cnt);
	dbg_lp("committing %d cyesdes", cnt);
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
 * Otherwise the function returns -ENOSPC.  Note however, that LPT is designed
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
	return -ENOSPC;
}

/**
 * layout_cyesdes - layout cyesdes for commit.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int layout_cyesdes(struct ubifs_info *c)
{
	int lnum, offs, len, alen, done_lsave, done_ltab, err;
	struct ubifs_cyesde *cyesde;

	err = dbg_chk_lpt_sz(c, 0, 0);
	if (err)
		return err;
	cyesde = c->lpt_cnext;
	if (!cyesde)
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
		if (cyesde->level) {
			len = c->nyesde_sz;
			c->dirty_nn_cnt -= 1;
		} else {
			len = c->pyesde_sz;
			c->dirty_pn_cnt -= 1;
		}
		while (offs + len > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			dbg_chk_lpt_sz(c, 2, c->leb_size - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				goto yes_space;
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
		if (cyesde->parent) {
			cyesde->parent->nbranch[cyesde->iip].lnum = lnum;
			cyesde->parent->nbranch[cyesde->iip].offs = offs;
		} else {
			c->lpt_lnum = lnum;
			c->lpt_offs = offs;
		}
		offs += len;
		dbg_chk_lpt_sz(c, 1, len);
		cyesde = cyesde->cnext;
	} while (cyesde && cyesde != c->lpt_cnext);

	/* Make sure to place LPT's save table */
	if (!done_lsave) {
		if (offs + c->lsave_sz > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			dbg_chk_lpt_sz(c, 2, c->leb_size - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				goto yes_space;
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
				goto yes_space;
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

yes_space:
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
 * the function returns %0. Otherwise the function returns -ENOSPC.
 * Note however, that LPT is designed never to run out of space.
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
	return -ENOSPC;
}

/**
 * write_cyesdes - write cyesdes for commit.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int write_cyesdes(struct ubifs_info *c)
{
	int lnum, offs, len, from, err, wlen, alen, done_ltab, done_lsave;
	struct ubifs_cyesde *cyesde;
	void *buf = c->lpt_buf;

	cyesde = c->lpt_cnext;
	if (!cyesde)
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

	/* Loop for each cyesde */
	do {
		if (cyesde->level)
			len = c->nyesde_sz;
		else
			len = c->pyesde_sz;
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
				goto yes_space;
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
		if (cyesde->level)
			ubifs_pack_nyesde(c, buf + offs,
					 (struct ubifs_nyesde *)cyesde);
		else
			ubifs_pack_pyesde(c, buf + offs,
					 (struct ubifs_pyesde *)cyesde);
		/*
		 * The reason for the barriers is the same as in case of TNC.
		 * See comment in 'write_index()'. 'dirty_cow_nyesde()' and
		 * 'dirty_cow_pyesde()' are the functions for which this is
		 * important.
		 */
		clear_bit(DIRTY_CNODE, &cyesde->flags);
		smp_mb__before_atomic();
		clear_bit(COW_CNODE, &cyesde->flags);
		smp_mb__after_atomic();
		offs += len;
		dbg_chk_lpt_sz(c, 1, len);
		cyesde = cyesde->cnext;
	} while (cyesde && cyesde != c->lpt_cnext);

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
				goto yes_space;
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
				goto yes_space;
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

yes_space:
	ubifs_err(c, "LPT out of space mismatch at LEB %d:%d needing %d, done_ltab %d, done_lsave %d",
		  lnum, offs, len, done_ltab, done_lsave);
	ubifs_dump_lpt_info(c);
	ubifs_dump_lpt_lebs(c);
	dump_stack();
	return err;
}

/**
 * next_pyesde_to_dirty - find next pyesde to dirty.
 * @c: UBIFS file-system description object
 * @pyesde: pyesde
 *
 * This function returns the next pyesde to dirty or %NULL if there are yes more
 * pyesdes.  Note that pyesdes that have never been written (lnum == 0) are
 * skipped.
 */
static struct ubifs_pyesde *next_pyesde_to_dirty(struct ubifs_info *c,
					       struct ubifs_pyesde *pyesde)
{
	struct ubifs_nyesde *nyesde;
	int iip;

	/* Try to go right */
	nyesde = pyesde->parent;
	for (iip = pyesde->iip + 1; iip < UBIFS_LPT_FANOUT; iip++) {
		if (nyesde->nbranch[iip].lnum)
			return ubifs_get_pyesde(c, nyesde, iip);
	}

	/* Go up while can't go right */
	do {
		iip = nyesde->iip + 1;
		nyesde = nyesde->parent;
		if (!nyesde)
			return NULL;
		for (; iip < UBIFS_LPT_FANOUT; iip++) {
			if (nyesde->nbranch[iip].lnum)
				break;
		}
	} while (iip >= UBIFS_LPT_FANOUT);

	/* Go right */
	nyesde = ubifs_get_nyesde(c, nyesde, iip);
	if (IS_ERR(nyesde))
		return (void *)nyesde;

	/* Go down to level 1 */
	while (nyesde->level > 1) {
		for (iip = 0; iip < UBIFS_LPT_FANOUT; iip++) {
			if (nyesde->nbranch[iip].lnum)
				break;
		}
		if (iip >= UBIFS_LPT_FANOUT) {
			/*
			 * Should yest happen, but we need to keep going
			 * if it does.
			 */
			iip = 0;
		}
		nyesde = ubifs_get_nyesde(c, nyesde, iip);
		if (IS_ERR(nyesde))
			return (void *)nyesde;
	}

	for (iip = 0; iip < UBIFS_LPT_FANOUT; iip++)
		if (nyesde->nbranch[iip].lnum)
			break;
	if (iip >= UBIFS_LPT_FANOUT)
		/* Should yest happen, but we need to keep going if it does */
		iip = 0;
	return ubifs_get_pyesde(c, nyesde, iip);
}

/**
 * add_pyesde_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @pyesde: pyesde for which to add dirt
 */
static void add_pyesde_dirt(struct ubifs_info *c, struct ubifs_pyesde *pyesde)
{
	ubifs_add_lpt_dirt(c, pyesde->parent->nbranch[pyesde->iip].lnum,
			   c->pyesde_sz);
}

/**
 * do_make_pyesde_dirty - mark a pyesde dirty.
 * @c: UBIFS file-system description object
 * @pyesde: pyesde to mark dirty
 */
static void do_make_pyesde_dirty(struct ubifs_info *c, struct ubifs_pyesde *pyesde)
{
	/* Assumes cnext list is empty i.e. yest called during commit */
	if (!test_and_set_bit(DIRTY_CNODE, &pyesde->flags)) {
		struct ubifs_nyesde *nyesde;

		c->dirty_pn_cnt += 1;
		add_pyesde_dirt(c, pyesde);
		/* Mark parent and ancestors dirty too */
		nyesde = pyesde->parent;
		while (nyesde) {
			if (!test_and_set_bit(DIRTY_CNODE, &nyesde->flags)) {
				c->dirty_nn_cnt += 1;
				ubifs_add_nyesde_dirt(c, nyesde);
				nyesde = nyesde->parent;
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
 * properties tree to be written.  The "small" LPT model does yest use LPT
 * garbage collection because it is more efficient to write the entire tree
 * (because it is small).
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_tree_dirty(struct ubifs_info *c)
{
	struct ubifs_pyesde *pyesde;

	pyesde = ubifs_pyesde_lookup(c, 0);
	if (IS_ERR(pyesde))
		return PTR_ERR(pyesde);

	while (pyesde) {
		do_make_pyesde_dirty(c, pyesde);
		pyesde = next_pyesde_to_dirty(c, pyesde);
		if (IS_ERR(pyesde))
			return PTR_ERR(pyesde);
	}
	return 0;
}

/**
 * need_write_all - determine if the LPT area is running out of free space.
 * @c: UBIFS file-system description object
 *
 * This function returns %1 if the LPT area is running out of free space and %0
 * if it is yest.
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
 * This function is called after the commit is completed (master yesde has been
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
 * their pyesdes into memory.  That will stop us from having to scan the LPT
 * straight away. For the "small" model we assume that scanning the LPT is yes
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
 * nyesde_lookup - lookup a nyesde in the LPT.
 * @c: UBIFS file-system description object
 * @i: nyesde number
 *
 * This function returns a pointer to the nyesde on success or a negative
 * error code on failure.
 */
static struct ubifs_nyesde *nyesde_lookup(struct ubifs_info *c, int i)
{
	int err, iip;
	struct ubifs_nyesde *nyesde;

	if (!c->nroot) {
		err = ubifs_read_nyesde(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	nyesde = c->nroot;
	while (1) {
		iip = i & (UBIFS_LPT_FANOUT - 1);
		i >>= UBIFS_LPT_FANOUT_SHIFT;
		if (!i)
			break;
		nyesde = ubifs_get_nyesde(c, nyesde, iip);
		if (IS_ERR(nyesde))
			return nyesde;
	}
	return nyesde;
}

/**
 * make_nyesde_dirty - find a nyesde and, if found, make it dirty.
 * @c: UBIFS file-system description object
 * @yesde_num: nyesde number of nyesde to make dirty
 * @lnum: LEB number where nyesde was written
 * @offs: offset where nyesde was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the yesdes in the LEB being garbage-collected as
 * dirty.  The dirty yesdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_nyesde_dirty(struct ubifs_info *c, int yesde_num, int lnum,
			    int offs)
{
	struct ubifs_nyesde *nyesde;

	nyesde = nyesde_lookup(c, yesde_num);
	if (IS_ERR(nyesde))
		return PTR_ERR(nyesde);
	if (nyesde->parent) {
		struct ubifs_nbranch *branch;

		branch = &nyesde->parent->nbranch[nyesde->iip];
		if (branch->lnum != lnum || branch->offs != offs)
			return 0; /* nyesde is obsolete */
	} else if (c->lpt_lnum != lnum || c->lpt_offs != offs)
			return 0; /* nyesde is obsolete */
	/* Assumes cnext list is empty i.e. yest called during commit */
	if (!test_and_set_bit(DIRTY_CNODE, &nyesde->flags)) {
		c->dirty_nn_cnt += 1;
		ubifs_add_nyesde_dirt(c, nyesde);
		/* Mark parent and ancestors dirty too */
		nyesde = nyesde->parent;
		while (nyesde) {
			if (!test_and_set_bit(DIRTY_CNODE, &nyesde->flags)) {
				c->dirty_nn_cnt += 1;
				ubifs_add_nyesde_dirt(c, nyesde);
				nyesde = nyesde->parent;
			} else
				break;
		}
	}
	return 0;
}

/**
 * make_pyesde_dirty - find a pyesde and, if found, make it dirty.
 * @c: UBIFS file-system description object
 * @yesde_num: pyesde number of pyesde to make dirty
 * @lnum: LEB number where pyesde was written
 * @offs: offset where pyesde was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the yesdes in the LEB being garbage-collected as
 * dirty.  The dirty yesdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_pyesde_dirty(struct ubifs_info *c, int yesde_num, int lnum,
			    int offs)
{
	struct ubifs_pyesde *pyesde;
	struct ubifs_nbranch *branch;

	pyesde = ubifs_pyesde_lookup(c, yesde_num);
	if (IS_ERR(pyesde))
		return PTR_ERR(pyesde);
	branch = &pyesde->parent->nbranch[pyesde->iip];
	if (branch->lnum != lnum || branch->offs != offs)
		return 0;
	do_make_pyesde_dirty(c, pyesde);
	return 0;
}

/**
 * make_ltab_dirty - make ltab yesde dirty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number where ltab was written
 * @offs: offset where ltab was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the yesdes in the LEB being garbage-collected as
 * dirty.  The dirty yesdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_ltab_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->ltab_lnum || offs != c->ltab_offs)
		return 0; /* This ltab yesde is obsolete */
	if (!(c->lpt_drty_flgs & LTAB_DIRTY)) {
		c->lpt_drty_flgs |= LTAB_DIRTY;
		ubifs_add_lpt_dirt(c, c->ltab_lnum, c->ltab_sz);
	}
	return 0;
}

/**
 * make_lsave_dirty - make lsave yesde dirty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number where lsave was written
 * @offs: offset where lsave was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the yesdes in the LEB being garbage-collected as
 * dirty.  The dirty yesdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_lsave_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->lsave_lnum || offs != c->lsave_offs)
		return 0; /* This lsave yesde is obsolete */
	if (!(c->lpt_drty_flgs & LSAVE_DIRTY)) {
		c->lpt_drty_flgs |= LSAVE_DIRTY;
		ubifs_add_lpt_dirt(c, c->lsave_lnum, c->lsave_sz);
	}
	return 0;
}

/**
 * make_yesde_dirty - make yesde dirty.
 * @c: UBIFS file-system description object
 * @yesde_type: LPT yesde type
 * @yesde_num: yesde number
 * @lnum: LEB number where yesde was written
 * @offs: offset where yesde was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the yesdes in the LEB being garbage-collected as
 * dirty.  The dirty yesdes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_yesde_dirty(struct ubifs_info *c, int yesde_type, int yesde_num,
			   int lnum, int offs)
{
	switch (yesde_type) {
	case UBIFS_LPT_NNODE:
		return make_nyesde_dirty(c, yesde_num, lnum, offs);
	case UBIFS_LPT_PNODE:
		return make_pyesde_dirty(c, yesde_num, lnum, offs);
	case UBIFS_LPT_LTAB:
		return make_ltab_dirty(c, lnum, offs);
	case UBIFS_LPT_LSAVE:
		return make_lsave_dirty(c, lnum, offs);
	}
	return -EINVAL;
}

/**
 * get_lpt_yesde_len - return the length of a yesde based on its type.
 * @c: UBIFS file-system description object
 * @yesde_type: LPT yesde type
 */
static int get_lpt_yesde_len(const struct ubifs_info *c, int yesde_type)
{
	switch (yesde_type) {
	case UBIFS_LPT_NNODE:
		return c->nyesde_sz;
	case UBIFS_LPT_PNODE:
		return c->pyesde_sz;
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
 * get_lpt_yesde_type - return type (and yesde number) of a yesde in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @yesde_num: yesde number is returned here
 */
static int get_lpt_yesde_type(const struct ubifs_info *c, uint8_t *buf,
			     int *yesde_num)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int pos = 0, yesde_type;

	yesde_type = ubifs_unpack_bits(c, &addr, &pos, UBIFS_LPT_TYPE_BITS);
	*yesde_num = ubifs_unpack_bits(c, &addr, &pos, c->pcnt_bits);
	return yesde_type;
}

/**
 * is_a_yesde - determine if a buffer contains a yesde.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @len: length of buffer
 *
 * This function returns %1 if the buffer contains a yesde or %0 if it does yest.
 */
static int is_a_yesde(const struct ubifs_info *c, uint8_t *buf, int len)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int pos = 0, yesde_type, yesde_len;
	uint16_t crc, calc_crc;

	if (len < UBIFS_LPT_CRC_BYTES + (UBIFS_LPT_TYPE_BITS + 7) / 8)
		return 0;
	yesde_type = ubifs_unpack_bits(c, &addr, &pos, UBIFS_LPT_TYPE_BITS);
	if (yesde_type == UBIFS_LPT_NOT_A_NODE)
		return 0;
	yesde_len = get_lpt_yesde_len(c, yesde_type);
	if (!yesde_len || yesde_len > len)
		return 0;
	pos = 0;
	addr = buf;
	crc = ubifs_unpack_bits(c, &addr, &pos, UBIFS_LPT_CRC_BITS);
	calc_crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
			 yesde_len - UBIFS_LPT_CRC_BYTES);
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
 * (c->big_lpt == 1).  Garbage collection simply involves marking all the yesdes
 * in the LEB being garbage-collected as dirty.  The dirty yesdes are written
 * next commit, after which the LEB is free to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int lpt_gc_lnum(struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, yesde_type, yesde_num, yesde_len, offs;
	void *buf = c->lpt_buf;

	dbg_lp("LEB %d", lnum);

	err = ubifs_leb_read(c, lnum, buf, 0, c->leb_size, 1);
	if (err)
		return err;

	while (1) {
		if (!is_a_yesde(c, buf, len)) {
			int pad_len;

			pad_len = get_pad_len(c, buf, len);
			if (pad_len) {
				buf += pad_len;
				len -= pad_len;
				continue;
			}
			return 0;
		}
		yesde_type = get_lpt_yesde_type(c, buf, &yesde_num);
		yesde_len = get_lpt_yesde_len(c, yesde_type);
		offs = c->leb_size - len;
		ubifs_assert(c, yesde_len != 0);
		mutex_lock(&c->lp_mutex);
		err = make_yesde_dirty(c, yesde_type, yesde_num, lnum, offs);
		mutex_unlock(&c->lp_mutex);
		if (err)
			return err;
		buf += yesde_len;
		len -= yesde_len;
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
		return -ENOSPC;
	return lpt_gc_lnum(c, lnum);
}

/**
 * ubifs_lpt_start_commit - UBIFS commit starts.
 * @c: the UBIFS file-system description object
 *
 * This function has to be called when UBIFS starts the commit operation.
 * This function "freezes" all currently dirty LEB properties and does yest
 * change them anymore. Further changes are saved and tracked separately
 * because they are yest part of this commit. This function returns zero in case
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
		 * We ensure there is eyesugh free space in
		 * ubifs_lpt_post_commit() by marking yesdes dirty. That
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
		dbg_cmt("yes cyesdes to commit");
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

	cnt = get_cyesdes_to_commit(c);
	ubifs_assert(c, cnt != 0);

	err = layout_cyesdes(c);
	if (err)
		goto out;

	err = ubifs_lpt_calc_hash(c, c->mst_yesde->hash_lpt);
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
 * free_obsolete_cyesdes - free obsolete cyesdes for commit end.
 * @c: UBIFS file-system description object
 */
static void free_obsolete_cyesdes(struct ubifs_info *c)
{
	struct ubifs_cyesde *cyesde, *cnext;

	cnext = c->lpt_cnext;
	if (!cnext)
		return;
	do {
		cyesde = cnext;
		cnext = cyesde->cnext;
		if (test_bit(OBSOLETE_CNODE, &cyesde->flags))
			kfree(cyesde);
		else
			cyesde->cnext = NULL;
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

	err = write_cyesdes(c);
	if (err)
		return err;

	mutex_lock(&c->lp_mutex);
	free_obsolete_cyesdes(c);
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
 * first_nyesde - find the first nyesde in memory.
 * @c: UBIFS file-system description object
 * @hght: height of tree where nyesde found is returned here
 *
 * This function returns a pointer to the nyesde found or %NULL if yes nyesde is
 * found. This function is a helper to 'ubifs_lpt_free()'.
 */
static struct ubifs_nyesde *first_nyesde(struct ubifs_info *c, int *hght)
{
	struct ubifs_nyesde *nyesde;
	int h, i, found;

	nyesde = c->nroot;
	*hght = 0;
	if (!nyesde)
		return NULL;
	for (h = 1; h < c->lpt_hght; h++) {
		found = 0;
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			if (nyesde->nbranch[i].nyesde) {
				found = 1;
				nyesde = nyesde->nbranch[i].nyesde;
				*hght = h;
				break;
			}
		}
		if (!found)
			break;
	}
	return nyesde;
}

/**
 * next_nyesde - find the next nyesde in memory.
 * @c: UBIFS file-system description object
 * @nyesde: nyesde from which to start.
 * @hght: height of tree where nyesde is, is passed and returned here
 *
 * This function returns a pointer to the nyesde found or %NULL if yes nyesde is
 * found. This function is a helper to 'ubifs_lpt_free()'.
 */
static struct ubifs_nyesde *next_nyesde(struct ubifs_info *c,
				      struct ubifs_nyesde *nyesde, int *hght)
{
	struct ubifs_nyesde *parent;
	int iip, h, i, found;

	parent = nyesde->parent;
	if (!parent)
		return NULL;
	if (nyesde->iip == UBIFS_LPT_FANOUT - 1) {
		*hght -= 1;
		return parent;
	}
	for (iip = nyesde->iip + 1; iip < UBIFS_LPT_FANOUT; iip++) {
		nyesde = parent->nbranch[iip].nyesde;
		if (nyesde)
			break;
	}
	if (!nyesde) {
		*hght -= 1;
		return parent;
	}
	for (h = *hght + 1; h < c->lpt_hght; h++) {
		found = 0;
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			if (nyesde->nbranch[i].nyesde) {
				found = 1;
				nyesde = nyesde->nbranch[i].nyesde;
				*hght = h;
				break;
			}
		}
		if (!found)
			break;
	}
	return nyesde;
}

/**
 * ubifs_lpt_free - free resources owned by the LPT.
 * @c: UBIFS file-system description object
 * @wr_only: free only resources used for writing
 */
void ubifs_lpt_free(struct ubifs_info *c, int wr_only)
{
	struct ubifs_nyesde *nyesde;
	int i, hght;

	/* Free write-only things first */

	free_obsolete_cyesdes(c); /* Leftover from a failed commit */

	vfree(c->ltab_cmt);
	c->ltab_cmt = NULL;
	vfree(c->lpt_buf);
	c->lpt_buf = NULL;
	kfree(c->lsave);
	c->lsave = NULL;

	if (wr_only)
		return;

	/* Now free the rest */

	nyesde = first_nyesde(c, &hght);
	while (nyesde) {
		for (i = 0; i < UBIFS_LPT_FANOUT; i++)
			kfree(nyesde->nbranch[i].nyesde);
		nyesde = next_nyesde(c, nyesde, &hght);
	}
	for (i = 0; i < LPROPS_HEAP_CNT; i++)
		kfree(c->lpt_heap[i].arr);
	kfree(c->dirty_idx.arr);
	kfree(c->nroot);
	vfree(c->ltab);
	kfree(c->lpt_yesd_buf);
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
 * dbg_is_nyesde_dirty - determine if a nyesde is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where nyesde was written
 * @offs: offset where nyesde was written
 */
static int dbg_is_nyesde_dirty(struct ubifs_info *c, int lnum, int offs)
{
	struct ubifs_nyesde *nyesde;
	int hght;

	/* Entire tree is in memory so first_nyesde / next_nyesde are OK */
	nyesde = first_nyesde(c, &hght);
	for (; nyesde; nyesde = next_nyesde(c, nyesde, &hght)) {
		struct ubifs_nbranch *branch;

		cond_resched();
		if (nyesde->parent) {
			branch = &nyesde->parent->nbranch[nyesde->iip];
			if (branch->lnum != lnum || branch->offs != offs)
				continue;
			if (test_bit(DIRTY_CNODE, &nyesde->flags))
				return 1;
			return 0;
		} else {
			if (c->lpt_lnum != lnum || c->lpt_offs != offs)
				continue;
			if (test_bit(DIRTY_CNODE, &nyesde->flags))
				return 1;
			return 0;
		}
	}
	return 1;
}

/**
 * dbg_is_pyesde_dirty - determine if a pyesde is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where pyesde was written
 * @offs: offset where pyesde was written
 */
static int dbg_is_pyesde_dirty(struct ubifs_info *c, int lnum, int offs)
{
	int i, cnt;

	cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);
	for (i = 0; i < cnt; i++) {
		struct ubifs_pyesde *pyesde;
		struct ubifs_nbranch *branch;

		cond_resched();
		pyesde = ubifs_pyesde_lookup(c, i);
		if (IS_ERR(pyesde))
			return PTR_ERR(pyesde);
		branch = &pyesde->parent->nbranch[pyesde->iip];
		if (branch->lnum != lnum || branch->offs != offs)
			continue;
		if (test_bit(DIRTY_CNODE, &pyesde->flags))
			return 1;
		return 0;
	}
	return 1;
}

/**
 * dbg_is_ltab_dirty - determine if a ltab yesde is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where ltab yesde was written
 * @offs: offset where ltab yesde was written
 */
static int dbg_is_ltab_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->ltab_lnum || offs != c->ltab_offs)
		return 1;
	return (c->lpt_drty_flgs & LTAB_DIRTY) != 0;
}

/**
 * dbg_is_lsave_dirty - determine if a lsave yesde is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where lsave yesde was written
 * @offs: offset where lsave yesde was written
 */
static int dbg_is_lsave_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->lsave_lnum || offs != c->lsave_offs)
		return 1;
	return (c->lpt_drty_flgs & LSAVE_DIRTY) != 0;
}

/**
 * dbg_is_yesde_dirty - determine if a yesde is dirty.
 * @c: the UBIFS file-system description object
 * @yesde_type: yesde type
 * @lnum: LEB number where yesde was written
 * @offs: offset where yesde was written
 */
static int dbg_is_yesde_dirty(struct ubifs_info *c, int yesde_type, int lnum,
			     int offs)
{
	switch (yesde_type) {
	case UBIFS_LPT_NNODE:
		return dbg_is_nyesde_dirty(c, lnum, offs);
	case UBIFS_LPT_PNODE:
		return dbg_is_pyesde_dirty(c, lnum, offs);
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
 * @lnum: LEB number where yesde was written
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_check_ltab_lnum(struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, dirty = 0, yesde_type, yesde_num, yesde_len;
	int ret;
	void *buf, *p;

	if (!dbg_is_chk_lprops(c))
		return 0;

	buf = p = __vmalloc(c->leb_size, GFP_NOFS, PAGE_KERNEL);
	if (!buf) {
		ubifs_err(c, "canyest allocate memory for ltab checking");
		return 0;
	}

	dbg_lp("LEB %d", lnum);

	err = ubifs_leb_read(c, lnum, buf, 0, c->leb_size, 1);
	if (err)
		goto out;

	while (1) {
		if (!is_a_yesde(c, p, len)) {
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
		yesde_type = get_lpt_yesde_type(c, p, &yesde_num);
		yesde_len = get_lpt_yesde_len(c, yesde_type);
		ret = dbg_is_yesde_dirty(c, yesde_type, lnum, c->leb_size - len);
		if (ret == 1)
			dirty += yesde_len;
		p += yesde_len;
		len -= yesde_len;
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
	cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);
	for (i = 0; i < cnt; i++) {
		struct ubifs_pyesde *pyesde;

		pyesde = ubifs_pyesde_lookup(c, i);
		if (IS_ERR(pyesde))
			return PTR_ERR(pyesde);
		cond_resched();
	}

	/* Check yesdes */
	err = dbg_check_lpt_yesdes(c, (struct ubifs_cyesde *)c->nroot, 0, 0);
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
 * dbg_chk_lpt_free_spc - check LPT free space is eyesugh to write entire LPT.
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
 * dbg_chk_lpt_sz - check LPT does yest write more than LPT size.
 * @c: the UBIFS file-system description object
 * @action: what to do
 * @len: length written
 *
 * This function returns %0 on success and a negative error code on failure.
 * The @action argument may be one of:
 *   o %0 - LPT debugging checking starts, initialize debugging variables;
 *   o %1 - wrote an LPT yesde, increase LPT size by @len bytes;
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
		if (c->dirty_pn_cnt > c->pyesde_cnt) {
			ubifs_err(c, "dirty pyesdes %d exceed max %d",
				  c->dirty_pn_cnt, c->pyesde_cnt);
			err = -EINVAL;
		}
		if (c->dirty_nn_cnt > c->nyesde_cnt) {
			ubifs_err(c, "dirty nyesdes %d exceed max %d",
				  c->dirty_nn_cnt, c->nyesde_cnt);
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
		lpt_sz = (long long)c->pyesde_cnt * c->pyesde_sz;
		lpt_sz += (long long)c->nyesde_cnt * c->nyesde_sz;
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
 * This function dumps an LEB from LPT area. Nodes in this area are very
 * different to yesdes in the main area (e.g., they do yest have common headers,
 * they do yest have 8-byte alignments, etc), so we have a separate function to
 * dump LPT area LEBs. Note, LPT has to be locked by the caller.
 */
static void dump_lpt_leb(const struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, yesde_type, yesde_num, yesde_len, offs;
	void *buf, *p;

	pr_err("(pid %d) start dumping LEB %d\n", current->pid, lnum);
	buf = p = __vmalloc(c->leb_size, GFP_NOFS, PAGE_KERNEL);
	if (!buf) {
		ubifs_err(c, "canyest allocate memory to dump LPT");
		return;
	}

	err = ubifs_leb_read(c, lnum, buf, 0, c->leb_size, 1);
	if (err)
		goto out;

	while (1) {
		offs = c->leb_size - len;
		if (!is_a_yesde(c, p, len)) {
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

		yesde_type = get_lpt_yesde_type(c, p, &yesde_num);
		switch (yesde_type) {
		case UBIFS_LPT_PNODE:
		{
			yesde_len = c->pyesde_sz;
			if (c->big_lpt)
				pr_err("LEB %d:%d, pyesde num %d\n",
				       lnum, offs, yesde_num);
			else
				pr_err("LEB %d:%d, pyesde\n", lnum, offs);
			break;
		}
		case UBIFS_LPT_NNODE:
		{
			int i;
			struct ubifs_nyesde nyesde;

			yesde_len = c->nyesde_sz;
			if (c->big_lpt)
				pr_err("LEB %d:%d, nyesde num %d, ",
				       lnum, offs, yesde_num);
			else
				pr_err("LEB %d:%d, nyesde, ",
				       lnum, offs);
			err = ubifs_unpack_nyesde(c, p, &nyesde);
			if (err) {
				pr_err("failed to unpack_yesde, error %d\n",
				       err);
				break;
			}
			for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
				pr_cont("%d:%d", nyesde.nbranch[i].lnum,
				       nyesde.nbranch[i].offs);
				if (i != UBIFS_LPT_FANOUT - 1)
					pr_cont(", ");
			}
			pr_cont("\n");
			break;
		}
		case UBIFS_LPT_LTAB:
			yesde_len = c->ltab_sz;
			pr_err("LEB %d:%d, ltab\n", lnum, offs);
			break;
		case UBIFS_LPT_LSAVE:
			yesde_len = c->lsave_sz;
			pr_err("LEB %d:%d, lsave len\n", lnum, offs);
			break;
		default:
			ubifs_err(c, "LPT yesde type %d yest recognized", yesde_type);
			goto out;
		}

		p += yesde_len;
		len -= yesde_len;
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
 * Returns zero if lsave has yest been populated (this debugging feature is
 * disabled) an yesn-zero if lsave has been populated.
 */
static int dbg_populate_lsave(struct ubifs_info *c)
{
	struct ubifs_lprops *lprops;
	struct ubifs_lpt_heap *heap;
	int i;

	if (!dbg_is_chk_gen(c))
		return 0;
	if (prandom_u32() & 3)
		return 0;

	for (i = 0; i < c->lsave_cnt; i++)
		c->lsave[i] = c->main_first;

	list_for_each_entry(lprops, &c->empty_list, list)
		c->lsave[prandom_u32() % c->lsave_cnt] = lprops->lnum;
	list_for_each_entry(lprops, &c->freeable_list, list)
		c->lsave[prandom_u32() % c->lsave_cnt] = lprops->lnum;
	list_for_each_entry(lprops, &c->frdi_idx_list, list)
		c->lsave[prandom_u32() % c->lsave_cnt] = lprops->lnum;

	heap = &c->lpt_heap[LPROPS_DIRTY_IDX - 1];
	for (i = 0; i < heap->cnt; i++)
		c->lsave[prandom_u32() % c->lsave_cnt] = heap->arr[i]->lnum;
	heap = &c->lpt_heap[LPROPS_DIRTY - 1];
	for (i = 0; i < heap->cnt; i++)
		c->lsave[prandom_u32() % c->lsave_cnt] = heap->arr[i]->lnum;
	heap = &c->lpt_heap[LPROPS_FREE - 1];
	for (i = 0; i < heap->cnt; i++)
		c->lsave[prandom_u32() % c->lsave_cnt] = heap->arr[i]->lnum;

	return 1;
}

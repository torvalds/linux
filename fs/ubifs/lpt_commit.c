/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements commit-related functionality of the LEB properties
 * subsystem.
 */

#include <linux/crc16.h>
#include "ubifs.h"

/**
 * first_dirty_cnode - find first dirty cnode.
 * @c: UBIFS file-system description object
 * @nnode: nnode at which to start
 *
 * This function returns the first dirty cnode or %NULL if there is not one.
 */
static struct ubifs_cnode *first_dirty_cnode(struct ubifs_nnode *nnode)
{
	ubifs_assert(nnode);
	while (1) {
		int i, cont = 0;

		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			struct ubifs_cnode *cnode;

			cnode = nnode->nbranch[i].cnode;
			if (cnode &&
			    test_bit(DIRTY_CNODE, &cnode->flags)) {
				if (cnode->level == 0)
					return cnode;
				nnode = (struct ubifs_nnode *)cnode;
				cont = 1;
				break;
			}
		}
		if (!cont)
			return (struct ubifs_cnode *)nnode;
	}
}

/**
 * next_dirty_cnode - find next dirty cnode.
 * @cnode: cnode from which to begin searching
 *
 * This function returns the next dirty cnode or %NULL if there is not one.
 */
static struct ubifs_cnode *next_dirty_cnode(struct ubifs_cnode *cnode)
{
	struct ubifs_nnode *nnode;
	int i;

	ubifs_assert(cnode);
	nnode = cnode->parent;
	if (!nnode)
		return NULL;
	for (i = cnode->iip + 1; i < UBIFS_LPT_FANOUT; i++) {
		cnode = nnode->nbranch[i].cnode;
		if (cnode && test_bit(DIRTY_CNODE, &cnode->flags)) {
			if (cnode->level == 0)
				return cnode; /* cnode is a pnode */
			/* cnode is a nnode */
			return first_dirty_cnode((struct ubifs_nnode *)cnode);
		}
	}
	return (struct ubifs_cnode *)nnode;
}

/**
 * get_cnodes_to_commit - create list of dirty cnodes to commit.
 * @c: UBIFS file-system description object
 *
 * This function returns the number of cnodes to commit.
 */
static int get_cnodes_to_commit(struct ubifs_info *c)
{
	struct ubifs_cnode *cnode, *cnext;
	int cnt = 0;

	if (!c->nroot)
		return 0;

	if (!test_bit(DIRTY_CNODE, &c->nroot->flags))
		return 0;

	c->lpt_cnext = first_dirty_cnode(c->nroot);
	cnode = c->lpt_cnext;
	if (!cnode)
		return 0;
	cnt += 1;
	while (1) {
		ubifs_assert(!test_bit(COW_ZNODE, &cnode->flags));
		__set_bit(COW_ZNODE, &cnode->flags);
		cnext = next_dirty_cnode(cnode);
		if (!cnext) {
			cnode->cnext = c->lpt_cnext;
			break;
		}
		cnode->cnext = cnext;
		cnode = cnext;
		cnt += 1;
	}
	dbg_cmt("committing %d cnodes", cnt);
	dbg_lp("committing %d cnodes", cnt);
	ubifs_assert(cnt == c->dirty_nn_cnt + c->dirty_pn_cnt);
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
	ubifs_assert(lnum >= c->lpt_first && lnum <= c->lpt_last);
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
 * layout_cnodes - layout cnodes for commit.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int layout_cnodes(struct ubifs_info *c)
{
	int lnum, offs, len, alen, done_lsave, done_ltab, err;
	struct ubifs_cnode *cnode;

	err = dbg_chk_lpt_sz(c, 0, 0);
	if (err)
		return err;
	cnode = c->lpt_cnext;
	if (!cnode)
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
		if (cnode->level) {
			len = c->nnode_sz;
			c->dirty_nn_cnt -= 1;
		} else {
			len = c->pnode_sz;
			c->dirty_pn_cnt -= 1;
		}
		while (offs + len > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			dbg_chk_lpt_sz(c, 2, alen - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				goto no_space;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
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
		if (cnode->parent) {
			cnode->parent->nbranch[cnode->iip].lnum = lnum;
			cnode->parent->nbranch[cnode->iip].offs = offs;
		} else {
			c->lpt_lnum = lnum;
			c->lpt_offs = offs;
		}
		offs += len;
		dbg_chk_lpt_sz(c, 1, len);
		cnode = cnode->cnext;
	} while (cnode && cnode != c->lpt_cnext);

	/* Make sure to place LPT's save table */
	if (!done_lsave) {
		if (offs + c->lsave_sz > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			dbg_chk_lpt_sz(c, 2, alen - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				goto no_space;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
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
			dbg_chk_lpt_sz(c, 2, alen - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				goto no_space;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
		}
		done_ltab = 1;
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

no_space:
	ubifs_err("LPT out of space");
	dbg_err("LPT out of space at LEB %d:%d needing %d, done_ltab %d, "
		"done_lsave %d", lnum, offs, len, done_ltab, done_lsave);
	dbg_dump_lpt_info(c);
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
 * write_cnodes - write cnodes for commit.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int write_cnodes(struct ubifs_info *c)
{
	int lnum, offs, len, from, err, wlen, alen, done_ltab, done_lsave;
	struct ubifs_cnode *cnode;
	void *buf = c->lpt_buf;

	cnode = c->lpt_cnext;
	if (!cnode)
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

	/* Loop for each cnode */
	do {
		if (cnode->level)
			len = c->nnode_sz;
		else
			len = c->pnode_sz;
		while (offs + len > c->leb_size) {
			wlen = offs - from;
			if (wlen) {
				alen = ALIGN(wlen, c->min_io_size);
				memset(buf + offs, 0xff, alen - wlen);
				err = ubifs_leb_write(c, lnum, buf + from, from,
						       alen, UBI_SHORTTERM);
				if (err)
					return err;
				dbg_chk_lpt_sz(c, 4, alen - wlen);
			}
			dbg_chk_lpt_sz(c, 2, 0);
			err = realloc_lpt_leb(c, &lnum);
			if (err)
				goto no_space;
			offs = 0;
			from = 0;
			ubifs_assert(lnum >= c->lpt_first &&
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
		if (cnode->level)
			ubifs_pack_nnode(c, buf + offs,
					 (struct ubifs_nnode *)cnode);
		else
			ubifs_pack_pnode(c, buf + offs,
					 (struct ubifs_pnode *)cnode);
		/*
		 * The reason for the barriers is the same as in case of TNC.
		 * See comment in 'write_index()'. 'dirty_cow_nnode()' and
		 * 'dirty_cow_pnode()' are the functions for which this is
		 * important.
		 */
		clear_bit(DIRTY_CNODE, &cnode->flags);
		smp_mb__before_clear_bit();
		clear_bit(COW_ZNODE, &cnode->flags);
		smp_mb__after_clear_bit();
		offs += len;
		dbg_chk_lpt_sz(c, 1, len);
		cnode = cnode->cnext;
	} while (cnode && cnode != c->lpt_cnext);

	/* Make sure to place LPT's save table */
	if (!done_lsave) {
		if (offs + c->lsave_sz > c->leb_size) {
			wlen = offs - from;
			alen = ALIGN(wlen, c->min_io_size);
			memset(buf + offs, 0xff, alen - wlen);
			err = ubifs_leb_write(c, lnum, buf + from, from, alen,
					      UBI_SHORTTERM);
			if (err)
				return err;
			dbg_chk_lpt_sz(c, 2, alen - wlen);
			err = realloc_lpt_leb(c, &lnum);
			if (err)
				goto no_space;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
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
			err = ubifs_leb_write(c, lnum, buf + from, from, alen,
					      UBI_SHORTTERM);
			if (err)
				return err;
			dbg_chk_lpt_sz(c, 2, alen - wlen);
			err = realloc_lpt_leb(c, &lnum);
			if (err)
				goto no_space;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
			err = ubifs_leb_unmap(c, lnum);
			if (err)
				return err;
		}
		done_ltab = 1;
		ubifs_pack_ltab(c, buf + offs, c->ltab_cmt);
		offs += c->ltab_sz;
		dbg_chk_lpt_sz(c, 1, c->ltab_sz);
	}

	/* Write remaining data in buffer */
	wlen = offs - from;
	alen = ALIGN(wlen, c->min_io_size);
	memset(buf + offs, 0xff, alen - wlen);
	err = ubifs_leb_write(c, lnum, buf + from, from, alen, UBI_SHORTTERM);
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

no_space:
	ubifs_err("LPT out of space mismatch");
	dbg_err("LPT out of space mismatch at LEB %d:%d needing %d, done_ltab "
	        "%d, done_lsave %d", lnum, offs, len, done_ltab, done_lsave);
	dbg_dump_lpt_info(c);
	return err;
}

/**
 * next_pnode - find next pnode.
 * @c: UBIFS file-system description object
 * @pnode: pnode
 *
 * This function returns the next pnode or %NULL if there are no more pnodes.
 */
static struct ubifs_pnode *next_pnode(struct ubifs_info *c,
				      struct ubifs_pnode *pnode)
{
	struct ubifs_nnode *nnode;
	int iip;

	/* Try to go right */
	nnode = pnode->parent;
	iip = pnode->iip + 1;
	if (iip < UBIFS_LPT_FANOUT) {
		/* We assume here that LEB zero is never an LPT LEB */
		if (nnode->nbranch[iip].lnum)
			return ubifs_get_pnode(c, nnode, iip);
		else
			return NULL;
	}

	/* Go up while can't go right */
	do {
		iip = nnode->iip + 1;
		nnode = nnode->parent;
		if (!nnode)
			return NULL;
		/* We assume here that LEB zero is never an LPT LEB */
	} while (iip >= UBIFS_LPT_FANOUT || !nnode->nbranch[iip].lnum);

	/* Go right */
	nnode = ubifs_get_nnode(c, nnode, iip);
	if (IS_ERR(nnode))
		return (void *)nnode;

	/* Go down to level 1 */
	while (nnode->level > 1) {
		nnode = ubifs_get_nnode(c, nnode, 0);
		if (IS_ERR(nnode))
			return (void *)nnode;
	}

	return ubifs_get_pnode(c, nnode, 0);
}

/**
 * pnode_lookup - lookup a pnode in the LPT.
 * @c: UBIFS file-system description object
 * @i: pnode number (0 to main_lebs - 1)
 *
 * This function returns a pointer to the pnode on success or a negative
 * error code on failure.
 */
static struct ubifs_pnode *pnode_lookup(struct ubifs_info *c, int i)
{
	int err, h, iip, shft;
	struct ubifs_nnode *nnode;

	if (!c->nroot) {
		err = ubifs_read_nnode(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	i <<= UBIFS_LPT_FANOUT_SHIFT;
	nnode = c->nroot;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nnode = ubifs_get_nnode(c, nnode, iip);
		if (IS_ERR(nnode))
			return ERR_PTR(PTR_ERR(nnode));
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	return ubifs_get_pnode(c, nnode, iip);
}

/**
 * add_pnode_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @pnode: pnode for which to add dirt
 */
static void add_pnode_dirt(struct ubifs_info *c, struct ubifs_pnode *pnode)
{
	ubifs_add_lpt_dirt(c, pnode->parent->nbranch[pnode->iip].lnum,
			   c->pnode_sz);
}

/**
 * do_make_pnode_dirty - mark a pnode dirty.
 * @c: UBIFS file-system description object
 * @pnode: pnode to mark dirty
 */
static void do_make_pnode_dirty(struct ubifs_info *c, struct ubifs_pnode *pnode)
{
	/* Assumes cnext list is empty i.e. not called during commit */
	if (!test_and_set_bit(DIRTY_CNODE, &pnode->flags)) {
		struct ubifs_nnode *nnode;

		c->dirty_pn_cnt += 1;
		add_pnode_dirt(c, pnode);
		/* Mark parent and ancestors dirty too */
		nnode = pnode->parent;
		while (nnode) {
			if (!test_and_set_bit(DIRTY_CNODE, &nnode->flags)) {
				c->dirty_nn_cnt += 1;
				ubifs_add_nnode_dirt(c, nnode);
				nnode = nnode->parent;
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
 * properties tree to be written.  The "small" LPT model does not use LPT
 * garbage collection because it is more efficient to write the entire tree
 * (because it is small).
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_tree_dirty(struct ubifs_info *c)
{
	struct ubifs_pnode *pnode;

	pnode = pnode_lookup(c, 0);
	while (pnode) {
		do_make_pnode_dirty(c, pnode);
		pnode = next_pnode(c, pnode);
		if (IS_ERR(pnode))
			return PTR_ERR(pnode);
	}
	return 0;
}

/**
 * need_write_all - determine if the LPT area is running out of free space.
 * @c: UBIFS file-system description object
 *
 * This function returns %1 if the LPT area is running out of free space and %0
 * if it is not.
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
 * This function is called after the commit is completed (master node has been
 * written) and unmaps LPT LEBs that were marked for trivial GC.
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
 * their pnodes into memory.  That will stop us from having to scan the LPT
 * straight away. For the "small" model we assume that scanning the LPT is no
 * big deal.
 */
static void populate_lsave(struct ubifs_info *c)
{
	struct ubifs_lprops *lprops;
	struct ubifs_lpt_heap *heap;
	int i, cnt = 0;

	ubifs_assert(c->big_lpt);
	if (!(c->lpt_drty_flgs & LSAVE_DIRTY)) {
		c->lpt_drty_flgs |= LSAVE_DIRTY;
		ubifs_add_lpt_dirt(c, c->lsave_lnum, c->lsave_sz);
	}
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
 * nnode_lookup - lookup a nnode in the LPT.
 * @c: UBIFS file-system description object
 * @i: nnode number
 *
 * This function returns a pointer to the nnode on success or a negative
 * error code on failure.
 */
static struct ubifs_nnode *nnode_lookup(struct ubifs_info *c, int i)
{
	int err, iip;
	struct ubifs_nnode *nnode;

	if (!c->nroot) {
		err = ubifs_read_nnode(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	nnode = c->nroot;
	while (1) {
		iip = i & (UBIFS_LPT_FANOUT - 1);
		i >>= UBIFS_LPT_FANOUT_SHIFT;
		if (!i)
			break;
		nnode = ubifs_get_nnode(c, nnode, iip);
		if (IS_ERR(nnode))
			return nnode;
	}
	return nnode;
}

/**
 * make_nnode_dirty - find a nnode and, if found, make it dirty.
 * @c: UBIFS file-system description object
 * @node_num: nnode number of nnode to make dirty
 * @lnum: LEB number where nnode was written
 * @offs: offset where nnode was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_nnode_dirty(struct ubifs_info *c, int node_num, int lnum,
			    int offs)
{
	struct ubifs_nnode *nnode;

	nnode = nnode_lookup(c, node_num);
	if (IS_ERR(nnode))
		return PTR_ERR(nnode);
	if (nnode->parent) {
		struct ubifs_nbranch *branch;

		branch = &nnode->parent->nbranch[nnode->iip];
		if (branch->lnum != lnum || branch->offs != offs)
			return 0; /* nnode is obsolete */
	} else if (c->lpt_lnum != lnum || c->lpt_offs != offs)
			return 0; /* nnode is obsolete */
	/* Assumes cnext list is empty i.e. not called during commit */
	if (!test_and_set_bit(DIRTY_CNODE, &nnode->flags)) {
		c->dirty_nn_cnt += 1;
		ubifs_add_nnode_dirt(c, nnode);
		/* Mark parent and ancestors dirty too */
		nnode = nnode->parent;
		while (nnode) {
			if (!test_and_set_bit(DIRTY_CNODE, &nnode->flags)) {
				c->dirty_nn_cnt += 1;
				ubifs_add_nnode_dirt(c, nnode);
				nnode = nnode->parent;
			} else
				break;
		}
	}
	return 0;
}

/**
 * make_pnode_dirty - find a pnode and, if found, make it dirty.
 * @c: UBIFS file-system description object
 * @node_num: pnode number of pnode to make dirty
 * @lnum: LEB number where pnode was written
 * @offs: offset where pnode was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_pnode_dirty(struct ubifs_info *c, int node_num, int lnum,
			    int offs)
{
	struct ubifs_pnode *pnode;
	struct ubifs_nbranch *branch;

	pnode = pnode_lookup(c, node_num);
	if (IS_ERR(pnode))
		return PTR_ERR(pnode);
	branch = &pnode->parent->nbranch[pnode->iip];
	if (branch->lnum != lnum || branch->offs != offs)
		return 0;
	do_make_pnode_dirty(c, pnode);
	return 0;
}

/**
 * make_ltab_dirty - make ltab node dirty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number where ltab was written
 * @offs: offset where ltab was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_ltab_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->ltab_lnum || offs != c->ltab_offs)
		return 0; /* This ltab node is obsolete */
	if (!(c->lpt_drty_flgs & LTAB_DIRTY)) {
		c->lpt_drty_flgs |= LTAB_DIRTY;
		ubifs_add_lpt_dirt(c, c->ltab_lnum, c->ltab_sz);
	}
	return 0;
}

/**
 * make_lsave_dirty - make lsave node dirty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number where lsave was written
 * @offs: offset where lsave was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_lsave_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->lsave_lnum || offs != c->lsave_offs)
		return 0; /* This lsave node is obsolete */
	if (!(c->lpt_drty_flgs & LSAVE_DIRTY)) {
		c->lpt_drty_flgs |= LSAVE_DIRTY;
		ubifs_add_lpt_dirt(c, c->lsave_lnum, c->lsave_sz);
	}
	return 0;
}

/**
 * make_node_dirty - make node dirty.
 * @c: UBIFS file-system description object
 * @node_type: LPT node type
 * @node_num: node number
 * @lnum: LEB number where node was written
 * @offs: offset where node was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_node_dirty(struct ubifs_info *c, int node_type, int node_num,
			   int lnum, int offs)
{
	switch (node_type) {
	case UBIFS_LPT_NNODE:
		return make_nnode_dirty(c, node_num, lnum, offs);
	case UBIFS_LPT_PNODE:
		return make_pnode_dirty(c, node_num, lnum, offs);
	case UBIFS_LPT_LTAB:
		return make_ltab_dirty(c, lnum, offs);
	case UBIFS_LPT_LSAVE:
		return make_lsave_dirty(c, lnum, offs);
	}
	return -EINVAL;
}

/**
 * get_lpt_node_len - return the length of a node based on its type.
 * @c: UBIFS file-system description object
 * @node_type: LPT node type
 */
static int get_lpt_node_len(struct ubifs_info *c, int node_type)
{
	switch (node_type) {
	case UBIFS_LPT_NNODE:
		return c->nnode_sz;
	case UBIFS_LPT_PNODE:
		return c->pnode_sz;
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
static int get_pad_len(struct ubifs_info *c, uint8_t *buf, int len)
{
	int offs, pad_len;

	if (c->min_io_size == 1)
		return 0;
	offs = c->leb_size - len;
	pad_len = ALIGN(offs, c->min_io_size) - offs;
	return pad_len;
}

/**
 * get_lpt_node_type - return type (and node number) of a node in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @node_num: node number is returned here
 */
static int get_lpt_node_type(struct ubifs_info *c, uint8_t *buf, int *node_num)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int pos = 0, node_type;

	node_type = ubifs_unpack_bits(&addr, &pos, UBIFS_LPT_TYPE_BITS);
	*node_num = ubifs_unpack_bits(&addr, &pos, c->pcnt_bits);
	return node_type;
}

/**
 * is_a_node - determine if a buffer contains a node.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @len: length of buffer
 *
 * This function returns %1 if the buffer contains a node or %0 if it does not.
 */
static int is_a_node(struct ubifs_info *c, uint8_t *buf, int len)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int pos = 0, node_type, node_len;
	uint16_t crc, calc_crc;

	node_type = ubifs_unpack_bits(&addr, &pos, UBIFS_LPT_TYPE_BITS);
	if (node_type == UBIFS_LPT_NOT_A_NODE)
		return 0;
	node_len = get_lpt_node_len(c, node_type);
	if (!node_len || node_len > len)
		return 0;
	pos = 0;
	addr = buf;
	crc = ubifs_unpack_bits(&addr, &pos, UBIFS_LPT_CRC_BITS);
	calc_crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
			 node_len - UBIFS_LPT_CRC_BYTES);
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
 * (c->big_lpt == 1).  Garbage collection simply involves marking all the nodes
 * in the LEB being garbage-collected as dirty.  The dirty nodes are written
 * next commit, after which the LEB is free to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int lpt_gc_lnum(struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, node_type, node_num, node_len, offs;
	void *buf = c->lpt_buf;

	dbg_lp("LEB %d", lnum);
	err = ubi_read(c->ubi, lnum, buf, 0, c->leb_size);
	if (err) {
		ubifs_err("cannot read LEB %d, error %d", lnum, err);
		return err;
	}
	while (1) {
		if (!is_a_node(c, buf, len)) {
			int pad_len;

			pad_len = get_pad_len(c, buf, len);
			if (pad_len) {
				buf += pad_len;
				len -= pad_len;
				continue;
			}
			return 0;
		}
		node_type = get_lpt_node_type(c, buf, &node_num);
		node_len = get_lpt_node_len(c, node_type);
		offs = c->leb_size - len;
		ubifs_assert(node_len != 0);
		mutex_lock(&c->lp_mutex);
		err = make_node_dirty(c, node_type, node_num, lnum, offs);
		mutex_unlock(&c->lp_mutex);
		if (err)
			return err;
		buf += node_len;
		len -= node_len;
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
		ubifs_assert(!c->ltab[i].tgc);
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
 * This function "freezes" all currently dirty LEB properties and does not
 * change them anymore. Further changes are saved and tracked separately
 * because they are not part of this commit. This function returns zero in case
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
		 * We ensure there is enough free space in
		 * ubifs_lpt_post_commit() by marking nodes dirty. That
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
		dbg_cmt("no cnodes to commit");
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

	cnt = get_cnodes_to_commit(c);
	ubifs_assert(cnt != 0);

	err = layout_cnodes(c);
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
 * free_obsolete_cnodes - free obsolete cnodes for commit end.
 * @c: UBIFS file-system description object
 */
static void free_obsolete_cnodes(struct ubifs_info *c)
{
	struct ubifs_cnode *cnode, *cnext;

	cnext = c->lpt_cnext;
	if (!cnext)
		return;
	do {
		cnode = cnext;
		cnext = cnode->cnext;
		if (test_bit(OBSOLETE_CNODE, &cnode->flags))
			kfree(cnode);
		else
			cnode->cnext = NULL;
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

	err = write_cnodes(c);
	if (err)
		return err;

	mutex_lock(&c->lp_mutex);
	free_obsolete_cnodes(c);
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
 * first_nnode - find the first nnode in memory.
 * @c: UBIFS file-system description object
 * @hght: height of tree where nnode found is returned here
 *
 * This function returns a pointer to the nnode found or %NULL if no nnode is
 * found. This function is a helper to 'ubifs_lpt_free()'.
 */
static struct ubifs_nnode *first_nnode(struct ubifs_info *c, int *hght)
{
	struct ubifs_nnode *nnode;
	int h, i, found;

	nnode = c->nroot;
	*hght = 0;
	if (!nnode)
		return NULL;
	for (h = 1; h < c->lpt_hght; h++) {
		found = 0;
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			if (nnode->nbranch[i].nnode) {
				found = 1;
				nnode = nnode->nbranch[i].nnode;
				*hght = h;
				break;
			}
		}
		if (!found)
			break;
	}
	return nnode;
}

/**
 * next_nnode - find the next nnode in memory.
 * @c: UBIFS file-system description object
 * @nnode: nnode from which to start.
 * @hght: height of tree where nnode is, is passed and returned here
 *
 * This function returns a pointer to the nnode found or %NULL if no nnode is
 * found. This function is a helper to 'ubifs_lpt_free()'.
 */
static struct ubifs_nnode *next_nnode(struct ubifs_info *c,
				      struct ubifs_nnode *nnode, int *hght)
{
	struct ubifs_nnode *parent;
	int iip, h, i, found;

	parent = nnode->parent;
	if (!parent)
		return NULL;
	if (nnode->iip == UBIFS_LPT_FANOUT - 1) {
		*hght -= 1;
		return parent;
	}
	for (iip = nnode->iip + 1; iip < UBIFS_LPT_FANOUT; iip++) {
		nnode = parent->nbranch[iip].nnode;
		if (nnode)
			break;
	}
	if (!nnode) {
		*hght -= 1;
		return parent;
	}
	for (h = *hght + 1; h < c->lpt_hght; h++) {
		found = 0;
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			if (nnode->nbranch[i].nnode) {
				found = 1;
				nnode = nnode->nbranch[i].nnode;
				*hght = h;
				break;
			}
		}
		if (!found)
			break;
	}
	return nnode;
}

/**
 * ubifs_lpt_free - free resources owned by the LPT.
 * @c: UBIFS file-system description object
 * @wr_only: free only resources used for writing
 */
void ubifs_lpt_free(struct ubifs_info *c, int wr_only)
{
	struct ubifs_nnode *nnode;
	int i, hght;

	/* Free write-only things first */

	free_obsolete_cnodes(c); /* Leftover from a failed commit */

	vfree(c->ltab_cmt);
	c->ltab_cmt = NULL;
	vfree(c->lpt_buf);
	c->lpt_buf = NULL;
	kfree(c->lsave);
	c->lsave = NULL;

	if (wr_only)
		return;

	/* Now free the rest */

	nnode = first_nnode(c, &hght);
	while (nnode) {
		for (i = 0; i < UBIFS_LPT_FANOUT; i++)
			kfree(nnode->nbranch[i].nnode);
		nnode = next_nnode(c, nnode, &hght);
	}
	for (i = 0; i < LPROPS_HEAP_CNT; i++)
		kfree(c->lpt_heap[i].arr);
	kfree(c->dirty_idx.arr);
	kfree(c->nroot);
	vfree(c->ltab);
	kfree(c->lpt_nod_buf);
}

#ifdef CONFIG_UBIFS_FS_DEBUG

/**
 * dbg_is_all_ff - determine if a buffer contains only 0xff bytes.
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
 * dbg_is_nnode_dirty - determine if a nnode is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where nnode was written
 * @offs: offset where nnode was written
 */
static int dbg_is_nnode_dirty(struct ubifs_info *c, int lnum, int offs)
{
	struct ubifs_nnode *nnode;
	int hght;

	/* Entire tree is in memory so first_nnode / next_nnode are ok */
	nnode = first_nnode(c, &hght);
	for (; nnode; nnode = next_nnode(c, nnode, &hght)) {
		struct ubifs_nbranch *branch;

		cond_resched();
		if (nnode->parent) {
			branch = &nnode->parent->nbranch[nnode->iip];
			if (branch->lnum != lnum || branch->offs != offs)
				continue;
			if (test_bit(DIRTY_CNODE, &nnode->flags))
				return 1;
			return 0;
		} else {
			if (c->lpt_lnum != lnum || c->lpt_offs != offs)
				continue;
			if (test_bit(DIRTY_CNODE, &nnode->flags))
				return 1;
			return 0;
		}
	}
	return 1;
}

/**
 * dbg_is_pnode_dirty - determine if a pnode is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where pnode was written
 * @offs: offset where pnode was written
 */
static int dbg_is_pnode_dirty(struct ubifs_info *c, int lnum, int offs)
{
	int i, cnt;

	cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);
	for (i = 0; i < cnt; i++) {
		struct ubifs_pnode *pnode;
		struct ubifs_nbranch *branch;

		cond_resched();
		pnode = pnode_lookup(c, i);
		if (IS_ERR(pnode))
			return PTR_ERR(pnode);
		branch = &pnode->parent->nbranch[pnode->iip];
		if (branch->lnum != lnum || branch->offs != offs)
			continue;
		if (test_bit(DIRTY_CNODE, &pnode->flags))
			return 1;
		return 0;
	}
	return 1;
}

/**
 * dbg_is_ltab_dirty - determine if a ltab node is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where ltab node was written
 * @offs: offset where ltab node was written
 */
static int dbg_is_ltab_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->ltab_lnum || offs != c->ltab_offs)
		return 1;
	return (c->lpt_drty_flgs & LTAB_DIRTY) != 0;
}

/**
 * dbg_is_lsave_dirty - determine if a lsave node is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where lsave node was written
 * @offs: offset where lsave node was written
 */
static int dbg_is_lsave_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->lsave_lnum || offs != c->lsave_offs)
		return 1;
	return (c->lpt_drty_flgs & LSAVE_DIRTY) != 0;
}

/**
 * dbg_is_node_dirty - determine if a node is dirty.
 * @c: the UBIFS file-system description object
 * @node_type: node type
 * @lnum: LEB number where node was written
 * @offs: offset where node was written
 */
static int dbg_is_node_dirty(struct ubifs_info *c, int node_type, int lnum,
			     int offs)
{
	switch (node_type) {
	case UBIFS_LPT_NNODE:
		return dbg_is_nnode_dirty(c, lnum, offs);
	case UBIFS_LPT_PNODE:
		return dbg_is_pnode_dirty(c, lnum, offs);
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
 * @lnum: LEB number where node was written
 * @offs: offset where node was written
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_check_ltab_lnum(struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, dirty = 0, node_type, node_num, node_len;
	int ret;
	void *buf = c->dbg_buf;

	dbg_lp("LEB %d", lnum);
	err = ubi_read(c->ubi, lnum, buf, 0, c->leb_size);
	if (err) {
		dbg_msg("ubi_read failed, LEB %d, error %d", lnum, err);
		return err;
	}
	while (1) {
		if (!is_a_node(c, buf, len)) {
			int i, pad_len;

			pad_len = get_pad_len(c, buf, len);
			if (pad_len) {
				buf += pad_len;
				len -= pad_len;
				dirty += pad_len;
				continue;
			}
			if (!dbg_is_all_ff(buf, len)) {
				dbg_msg("invalid empty space in LEB %d at %d",
					lnum, c->leb_size - len);
				err = -EINVAL;
			}
			i = lnum - c->lpt_first;
			if (len != c->ltab[i].free) {
				dbg_msg("invalid free space in LEB %d "
					"(free %d, expected %d)",
					lnum, len, c->ltab[i].free);
				err = -EINVAL;
			}
			if (dirty != c->ltab[i].dirty) {
				dbg_msg("invalid dirty space in LEB %d "
					"(dirty %d, expected %d)",
					lnum, dirty, c->ltab[i].dirty);
				err = -EINVAL;
			}
			return err;
		}
		node_type = get_lpt_node_type(c, buf, &node_num);
		node_len = get_lpt_node_len(c, node_type);
		ret = dbg_is_node_dirty(c, node_type, lnum, c->leb_size - len);
		if (ret == 1)
			dirty += node_len;
		buf += node_len;
		len -= node_len;
	}
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

	if (!(ubifs_chk_flags & UBIFS_CHK_LPROPS))
		return 0;

	/* Bring the entire tree into memory */
	cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);
	for (i = 0; i < cnt; i++) {
		struct ubifs_pnode *pnode;

		pnode = pnode_lookup(c, i);
		if (IS_ERR(pnode))
			return PTR_ERR(pnode);
		cond_resched();
	}

	/* Check nodes */
	err = dbg_check_lpt_nodes(c, (struct ubifs_cnode *)c->nroot, 0, 0);
	if (err)
		return err;

	/* Check each LEB */
	for (lnum = c->lpt_first; lnum <= c->lpt_last; lnum++) {
		err = dbg_check_ltab_lnum(c, lnum);
		if (err) {
			dbg_err("failed at LEB %d", lnum);
			return err;
		}
	}

	dbg_lp("succeeded");
	return 0;
}

/**
 * dbg_chk_lpt_free_spc - check LPT free space is enough to write entire LPT.
 * @c: the UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int dbg_chk_lpt_free_spc(struct ubifs_info *c)
{
	long long free = 0;
	int i;

	for (i = 0; i < c->lpt_lebs; i++) {
		if (c->ltab[i].tgc || c->ltab[i].cmt)
			continue;
		if (i + c->lpt_first == c->nhead_lnum)
			free += c->leb_size - c->nhead_offs;
		else if (c->ltab[i].free == c->leb_size)
			free += c->leb_size;
	}
	if (free < c->lpt_sz) {
		dbg_err("LPT space error: free %lld lpt_sz %lld",
			free, c->lpt_sz);
		dbg_dump_lpt_info(c);
		return -EINVAL;
	}
	return 0;
}

/**
 * dbg_chk_lpt_sz - check LPT does not write more than LPT size.
 * @c: the UBIFS file-system description object
 * @action: action
 * @len: length written
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int dbg_chk_lpt_sz(struct ubifs_info *c, int action, int len)
{
	long long chk_lpt_sz, lpt_sz;
	int err = 0;

	switch (action) {
	case 0:
		c->chk_lpt_sz = 0;
		c->chk_lpt_sz2 = 0;
		c->chk_lpt_lebs = 0;
		c->chk_lpt_wastage = 0;
		if (c->dirty_pn_cnt > c->pnode_cnt) {
			dbg_err("dirty pnodes %d exceed max %d",
				c->dirty_pn_cnt, c->pnode_cnt);
			err = -EINVAL;
		}
		if (c->dirty_nn_cnt > c->nnode_cnt) {
			dbg_err("dirty nnodes %d exceed max %d",
				c->dirty_nn_cnt, c->nnode_cnt);
			err = -EINVAL;
		}
		return err;
	case 1:
		c->chk_lpt_sz += len;
		return 0;
	case 2:
		c->chk_lpt_sz += len;
		c->chk_lpt_wastage += len;
		c->chk_lpt_lebs += 1;
		return 0;
	case 3:
		chk_lpt_sz = c->leb_size;
		chk_lpt_sz *= c->chk_lpt_lebs;
		chk_lpt_sz += len - c->nhead_offs;
		if (c->chk_lpt_sz != chk_lpt_sz) {
			dbg_err("LPT wrote %lld but space used was %lld",
				c->chk_lpt_sz, chk_lpt_sz);
			err = -EINVAL;
		}
		if (c->chk_lpt_sz > c->lpt_sz) {
			dbg_err("LPT wrote %lld but lpt_sz is %lld",
				c->chk_lpt_sz, c->lpt_sz);
			err = -EINVAL;
		}
		if (c->chk_lpt_sz2 && c->chk_lpt_sz != c->chk_lpt_sz2) {
			dbg_err("LPT layout size %lld but wrote %lld",
				c->chk_lpt_sz, c->chk_lpt_sz2);
			err = -EINVAL;
		}
		if (c->chk_lpt_sz2 && c->new_nhead_offs != len) {
			dbg_err("LPT new nhead offs: expected %d was %d",
				c->new_nhead_offs, len);
			err = -EINVAL;
		}
		lpt_sz = (long long)c->pnode_cnt * c->pnode_sz;
		lpt_sz += (long long)c->nnode_cnt * c->nnode_sz;
		lpt_sz += c->ltab_sz;
		if (c->big_lpt)
			lpt_sz += c->lsave_sz;
		if (c->chk_lpt_sz - c->chk_lpt_wastage > lpt_sz) {
			dbg_err("LPT chk_lpt_sz %lld + waste %lld exceeds %lld",
				c->chk_lpt_sz, c->chk_lpt_wastage, lpt_sz);
			err = -EINVAL;
		}
		if (err)
			dbg_dump_lpt_info(c);
		c->chk_lpt_sz2 = c->chk_lpt_sz;
		c->chk_lpt_sz = 0;
		c->chk_lpt_wastage = 0;
		c->chk_lpt_lebs = 0;
		c->new_nhead_offs = len;
		return err;
	case 4:
		c->chk_lpt_sz += len;
		c->chk_lpt_wastage += len;
		return 0;
	default:
		return -EINVAL;
	}
}

#endif /* CONFIG_UBIFS_FS_DEBUG */

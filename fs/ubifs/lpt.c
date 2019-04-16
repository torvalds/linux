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
 * This file implements the LEB properties tree (LPT) area. The LPT area
 * contains the LEB properties tree, a table of LPT area eraseblocks (ltab), and
 * (for the "big" model) a table of saved LEB numbers (lsave). The LPT area sits
 * between the log and the orphan area.
 *
 * The LPT area is like a miniature self-contained file system. It is required
 * that it never runs out of space, is fast to access and update, and scales
 * logarithmically. The LEB properties tree is implemented as a wandering tree
 * much like the TNC, and the LPT area has its own garbage collection.
 *
 * The LPT has two slightly different forms called the "small model" and the
 * "big model". The small model is used when the entire LEB properties table
 * can be written into a single eraseblock. In that case, garbage collection
 * consists of just writing the whole table, which therefore makes all other
 * eraseblocks reusable. In the case of the big model, dirty eraseblocks are
 * selected for garbage collection, which consists of marking the clean nodes in
 * that LEB as dirty, and then only the dirty nodes are written out. Also, in
 * the case of the big model, a table of LEB numbers is saved so that the entire
 * LPT does not to be scanned looking for empty eraseblocks when UBIFS is first
 * mounted.
 */

#include "ubifs.h"
#include <linux/crc16.h>
#include <linux/math64.h>
#include <linux/slab.h>

/**
 * do_calc_lpt_geom - calculate sizes for the LPT area.
 * @c: the UBIFS file-system description object
 *
 * Calculate the sizes of LPT bit fields, nodes, and tree, based on the
 * properties of the flash and whether LPT is "big" (c->big_lpt).
 */
static void do_calc_lpt_geom(struct ubifs_info *c)
{
	int i, n, bits, per_leb_wastage, max_pnode_cnt;
	long long sz, tot_wastage;

	n = c->main_lebs + c->max_leb_cnt - c->leb_cnt;
	max_pnode_cnt = DIV_ROUND_UP(n, UBIFS_LPT_FANOUT);

	c->lpt_hght = 1;
	n = UBIFS_LPT_FANOUT;
	while (n < max_pnode_cnt) {
		c->lpt_hght += 1;
		n <<= UBIFS_LPT_FANOUT_SHIFT;
	}

	c->pnode_cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);

	n = DIV_ROUND_UP(c->pnode_cnt, UBIFS_LPT_FANOUT);
	c->nnode_cnt = n;
	for (i = 1; i < c->lpt_hght; i++) {
		n = DIV_ROUND_UP(n, UBIFS_LPT_FANOUT);
		c->nnode_cnt += n;
	}

	c->space_bits = fls(c->leb_size) - 3;
	c->lpt_lnum_bits = fls(c->lpt_lebs);
	c->lpt_offs_bits = fls(c->leb_size - 1);
	c->lpt_spc_bits = fls(c->leb_size);

	n = DIV_ROUND_UP(c->max_leb_cnt, UBIFS_LPT_FANOUT);
	c->pcnt_bits = fls(n - 1);

	c->lnum_bits = fls(c->max_leb_cnt - 1);

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       (c->big_lpt ? c->pcnt_bits : 0) +
	       (c->space_bits * 2 + 1) * UBIFS_LPT_FANOUT;
	c->pnode_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       (c->big_lpt ? c->pcnt_bits : 0) +
	       (c->lpt_lnum_bits + c->lpt_offs_bits) * UBIFS_LPT_FANOUT;
	c->nnode_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       c->lpt_lebs * c->lpt_spc_bits * 2;
	c->ltab_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       c->lnum_bits * c->lsave_cnt;
	c->lsave_sz = (bits + 7) / 8;

	/* Calculate the minimum LPT size */
	c->lpt_sz = (long long)c->pnode_cnt * c->pnode_sz;
	c->lpt_sz += (long long)c->nnode_cnt * c->nnode_sz;
	c->lpt_sz += c->ltab_sz;
	if (c->big_lpt)
		c->lpt_sz += c->lsave_sz;

	/* Add wastage */
	sz = c->lpt_sz;
	per_leb_wastage = max_t(int, c->pnode_sz, c->nnode_sz);
	sz += per_leb_wastage;
	tot_wastage = per_leb_wastage;
	while (sz > c->leb_size) {
		sz += per_leb_wastage;
		sz -= c->leb_size;
		tot_wastage += per_leb_wastage;
	}
	tot_wastage += ALIGN(sz, c->min_io_size) - sz;
	c->lpt_sz += tot_wastage;
}

/**
 * ubifs_calc_lpt_geom - calculate and check sizes for the LPT area.
 * @c: the UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_calc_lpt_geom(struct ubifs_info *c)
{
	int lebs_needed;
	long long sz;

	do_calc_lpt_geom(c);

	/* Verify that lpt_lebs is big enough */
	sz = c->lpt_sz * 2; /* Must have at least 2 times the size */
	lebs_needed = div_u64(sz + c->leb_size - 1, c->leb_size);
	if (lebs_needed > c->lpt_lebs) {
		ubifs_err(c, "too few LPT LEBs");
		return -EINVAL;
	}

	/* Verify that ltab fits in a single LEB (since ltab is a single node */
	if (c->ltab_sz > c->leb_size) {
		ubifs_err(c, "LPT ltab too big");
		return -EINVAL;
	}

	c->check_lpt_free = c->big_lpt;
	return 0;
}

/**
 * calc_dflt_lpt_geom - calculate default LPT geometry.
 * @c: the UBIFS file-system description object
 * @main_lebs: number of main area LEBs is passed and returned here
 * @big_lpt: whether the LPT area is "big" is returned here
 *
 * The size of the LPT area depends on parameters that themselves are dependent
 * on the size of the LPT area. This function, successively recalculates the LPT
 * area geometry until the parameters and resultant geometry are consistent.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int calc_dflt_lpt_geom(struct ubifs_info *c, int *main_lebs,
			      int *big_lpt)
{
	int i, lebs_needed;
	long long sz;

	/* Start by assuming the minimum number of LPT LEBs */
	c->lpt_lebs = UBIFS_MIN_LPT_LEBS;
	c->main_lebs = *main_lebs - c->lpt_lebs;
	if (c->main_lebs <= 0)
		return -EINVAL;

	/* And assume we will use the small LPT model */
	c->big_lpt = 0;

	/*
	 * Calculate the geometry based on assumptions above and then see if it
	 * makes sense
	 */
	do_calc_lpt_geom(c);

	/* Small LPT model must have lpt_sz < leb_size */
	if (c->lpt_sz > c->leb_size) {
		/* Nope, so try again using big LPT model */
		c->big_lpt = 1;
		do_calc_lpt_geom(c);
	}

	/* Now check there are enough LPT LEBs */
	for (i = 0; i < 64 ; i++) {
		sz = c->lpt_sz * 4; /* Allow 4 times the size */
		lebs_needed = div_u64(sz + c->leb_size - 1, c->leb_size);
		if (lebs_needed > c->lpt_lebs) {
			/* Not enough LPT LEBs so try again with more */
			c->lpt_lebs = lebs_needed;
			c->main_lebs = *main_lebs - c->lpt_lebs;
			if (c->main_lebs <= 0)
				return -EINVAL;
			do_calc_lpt_geom(c);
			continue;
		}
		if (c->ltab_sz > c->leb_size) {
			ubifs_err(c, "LPT ltab too big");
			return -EINVAL;
		}
		*main_lebs = c->main_lebs;
		*big_lpt = c->big_lpt;
		return 0;
	}
	return -EINVAL;
}

/**
 * pack_bits - pack bit fields end-to-end.
 * @c: UBIFS file-system description object
 * @addr: address at which to pack (passed and next address returned)
 * @pos: bit position at which to pack (passed and next position returned)
 * @val: value to pack
 * @nrbits: number of bits of value to pack (1-32)
 */
static void pack_bits(const struct ubifs_info *c, uint8_t **addr, int *pos, uint32_t val, int nrbits)
{
	uint8_t *p = *addr;
	int b = *pos;

	ubifs_assert(c, nrbits > 0);
	ubifs_assert(c, nrbits <= 32);
	ubifs_assert(c, *pos >= 0);
	ubifs_assert(c, *pos < 8);
	ubifs_assert(c, (val >> nrbits) == 0 || nrbits == 32);
	if (b) {
		*p |= ((uint8_t)val) << b;
		nrbits += b;
		if (nrbits > 8) {
			*++p = (uint8_t)(val >>= (8 - b));
			if (nrbits > 16) {
				*++p = (uint8_t)(val >>= 8);
				if (nrbits > 24) {
					*++p = (uint8_t)(val >>= 8);
					if (nrbits > 32)
						*++p = (uint8_t)(val >>= 8);
				}
			}
		}
	} else {
		*p = (uint8_t)val;
		if (nrbits > 8) {
			*++p = (uint8_t)(val >>= 8);
			if (nrbits > 16) {
				*++p = (uint8_t)(val >>= 8);
				if (nrbits > 24)
					*++p = (uint8_t)(val >>= 8);
			}
		}
	}
	b = nrbits & 7;
	if (b == 0)
		p++;
	*addr = p;
	*pos = b;
}

/**
 * ubifs_unpack_bits - unpack bit fields.
 * @c: UBIFS file-system description object
 * @addr: address at which to unpack (passed and next address returned)
 * @pos: bit position at which to unpack (passed and next position returned)
 * @nrbits: number of bits of value to unpack (1-32)
 *
 * This functions returns the value unpacked.
 */
uint32_t ubifs_unpack_bits(const struct ubifs_info *c, uint8_t **addr, int *pos, int nrbits)
{
	const int k = 32 - nrbits;
	uint8_t *p = *addr;
	int b = *pos;
	uint32_t uninitialized_var(val);
	const int bytes = (nrbits + b + 7) >> 3;

	ubifs_assert(c, nrbits > 0);
	ubifs_assert(c, nrbits <= 32);
	ubifs_assert(c, *pos >= 0);
	ubifs_assert(c, *pos < 8);
	if (b) {
		switch (bytes) {
		case 2:
			val = p[1];
			break;
		case 3:
			val = p[1] | ((uint32_t)p[2] << 8);
			break;
		case 4:
			val = p[1] | ((uint32_t)p[2] << 8) |
				     ((uint32_t)p[3] << 16);
			break;
		case 5:
			val = p[1] | ((uint32_t)p[2] << 8) |
				     ((uint32_t)p[3] << 16) |
				     ((uint32_t)p[4] << 24);
		}
		val <<= (8 - b);
		val |= *p >> b;
		nrbits += b;
	} else {
		switch (bytes) {
		case 1:
			val = p[0];
			break;
		case 2:
			val = p[0] | ((uint32_t)p[1] << 8);
			break;
		case 3:
			val = p[0] | ((uint32_t)p[1] << 8) |
				     ((uint32_t)p[2] << 16);
			break;
		case 4:
			val = p[0] | ((uint32_t)p[1] << 8) |
				     ((uint32_t)p[2] << 16) |
				     ((uint32_t)p[3] << 24);
			break;
		}
	}
	val <<= k;
	val >>= k;
	b = nrbits & 7;
	p += nrbits >> 3;
	*addr = p;
	*pos = b;
	ubifs_assert(c, (val >> nrbits) == 0 || nrbits - b == 32);
	return val;
}

/**
 * ubifs_pack_pnode - pack all the bit fields of a pnode.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @pnode: pnode to pack
 */
void ubifs_pack_pnode(struct ubifs_info *c, void *buf,
		      struct ubifs_pnode *pnode)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(c, &addr, &pos, UBIFS_LPT_PNODE, UBIFS_LPT_TYPE_BITS);
	if (c->big_lpt)
		pack_bits(c, &addr, &pos, pnode->num, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		pack_bits(c, &addr, &pos, pnode->lprops[i].free >> 3,
			  c->space_bits);
		pack_bits(c, &addr, &pos, pnode->lprops[i].dirty >> 3,
			  c->space_bits);
		if (pnode->lprops[i].flags & LPROPS_INDEX)
			pack_bits(c, &addr, &pos, 1, 1);
		else
			pack_bits(c, &addr, &pos, 0, 1);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->pnode_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(c, &addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * ubifs_pack_nnode - pack all the bit fields of a nnode.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @nnode: nnode to pack
 */
void ubifs_pack_nnode(struct ubifs_info *c, void *buf,
		      struct ubifs_nnode *nnode)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(c, &addr, &pos, UBIFS_LPT_NNODE, UBIFS_LPT_TYPE_BITS);
	if (c->big_lpt)
		pack_bits(c, &addr, &pos, nnode->num, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int lnum = nnode->nbranch[i].lnum;

		if (lnum == 0)
			lnum = c->lpt_last + 1;
		pack_bits(c, &addr, &pos, lnum - c->lpt_first, c->lpt_lnum_bits);
		pack_bits(c, &addr, &pos, nnode->nbranch[i].offs,
			  c->lpt_offs_bits);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->nnode_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(c, &addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * ubifs_pack_ltab - pack the LPT's own lprops table.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @ltab: LPT's own lprops table to pack
 */
void ubifs_pack_ltab(struct ubifs_info *c, void *buf,
		     struct ubifs_lpt_lprops *ltab)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(c, &addr, &pos, UBIFS_LPT_LTAB, UBIFS_LPT_TYPE_BITS);
	for (i = 0; i < c->lpt_lebs; i++) {
		pack_bits(c, &addr, &pos, ltab[i].free, c->lpt_spc_bits);
		pack_bits(c, &addr, &pos, ltab[i].dirty, c->lpt_spc_bits);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->ltab_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(c, &addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * ubifs_pack_lsave - pack the LPT's save table.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @lsave: LPT's save table to pack
 */
void ubifs_pack_lsave(struct ubifs_info *c, void *buf, int *lsave)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(c, &addr, &pos, UBIFS_LPT_LSAVE, UBIFS_LPT_TYPE_BITS);
	for (i = 0; i < c->lsave_cnt; i++)
		pack_bits(c, &addr, &pos, lsave[i], c->lnum_bits);
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->lsave_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(c, &addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * ubifs_add_lpt_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to which to add dirty space
 * @dirty: amount of dirty space to add
 */
void ubifs_add_lpt_dirt(struct ubifs_info *c, int lnum, int dirty)
{
	if (!dirty || !lnum)
		return;
	dbg_lp("LEB %d add %d to %d",
	       lnum, dirty, c->ltab[lnum - c->lpt_first].dirty);
	ubifs_assert(c, lnum >= c->lpt_first && lnum <= c->lpt_last);
	c->ltab[lnum - c->lpt_first].dirty += dirty;
}

/**
 * set_ltab - set LPT LEB properties.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @free: amount of free space
 * @dirty: amount of dirty space
 */
static void set_ltab(struct ubifs_info *c, int lnum, int free, int dirty)
{
	dbg_lp("LEB %d free %d dirty %d to %d %d",
	       lnum, c->ltab[lnum - c->lpt_first].free,
	       c->ltab[lnum - c->lpt_first].dirty, free, dirty);
	ubifs_assert(c, lnum >= c->lpt_first && lnum <= c->lpt_last);
	c->ltab[lnum - c->lpt_first].free = free;
	c->ltab[lnum - c->lpt_first].dirty = dirty;
}

/**
 * ubifs_add_nnode_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @nnode: nnode for which to add dirt
 */
void ubifs_add_nnode_dirt(struct ubifs_info *c, struct ubifs_nnode *nnode)
{
	struct ubifs_nnode *np = nnode->parent;

	if (np)
		ubifs_add_lpt_dirt(c, np->nbranch[nnode->iip].lnum,
				   c->nnode_sz);
	else {
		ubifs_add_lpt_dirt(c, c->lpt_lnum, c->nnode_sz);
		if (!(c->lpt_drty_flgs & LTAB_DIRTY)) {
			c->lpt_drty_flgs |= LTAB_DIRTY;
			ubifs_add_lpt_dirt(c, c->ltab_lnum, c->ltab_sz);
		}
	}
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
 * calc_nnode_num - calculate nnode number.
 * @row: the row in the tree (root is zero)
 * @col: the column in the row (leftmost is zero)
 *
 * The nnode number is a number that uniquely identifies a nnode and can be used
 * easily to traverse the tree from the root to that nnode.
 *
 * This function calculates and returns the nnode number for the nnode at @row
 * and @col.
 */
static int calc_nnode_num(int row, int col)
{
	int num, bits;

	num = 1;
	while (row--) {
		bits = (col & (UBIFS_LPT_FANOUT - 1));
		col >>= UBIFS_LPT_FANOUT_SHIFT;
		num <<= UBIFS_LPT_FANOUT_SHIFT;
		num |= bits;
	}
	return num;
}

/**
 * calc_nnode_num_from_parent - calculate nnode number.
 * @c: UBIFS file-system description object
 * @parent: parent nnode
 * @iip: index in parent
 *
 * The nnode number is a number that uniquely identifies a nnode and can be used
 * easily to traverse the tree from the root to that nnode.
 *
 * This function calculates and returns the nnode number based on the parent's
 * nnode number and the index in parent.
 */
static int calc_nnode_num_from_parent(const struct ubifs_info *c,
				      struct ubifs_nnode *parent, int iip)
{
	int num, shft;

	if (!parent)
		return 1;
	shft = (c->lpt_hght - parent->level) * UBIFS_LPT_FANOUT_SHIFT;
	num = parent->num ^ (1 << shft);
	num |= (UBIFS_LPT_FANOUT + iip) << shft;
	return num;
}

/**
 * calc_pnode_num_from_parent - calculate pnode number.
 * @c: UBIFS file-system description object
 * @parent: parent nnode
 * @iip: index in parent
 *
 * The pnode number is a number that uniquely identifies a pnode and can be used
 * easily to traverse the tree from the root to that pnode.
 *
 * This function calculates and returns the pnode number based on the parent's
 * nnode number and the index in parent.
 */
static int calc_pnode_num_from_parent(const struct ubifs_info *c,
				      struct ubifs_nnode *parent, int iip)
{
	int i, n = c->lpt_hght - 1, pnum = parent->num, num = 0;

	for (i = 0; i < n; i++) {
		num <<= UBIFS_LPT_FANOUT_SHIFT;
		num |= pnum & (UBIFS_LPT_FANOUT - 1);
		pnum >>= UBIFS_LPT_FANOUT_SHIFT;
	}
	num <<= UBIFS_LPT_FANOUT_SHIFT;
	num |= iip;
	return num;
}

/**
 * ubifs_create_dflt_lpt - create default LPT.
 * @c: UBIFS file-system description object
 * @main_lebs: number of main area LEBs is passed and returned here
 * @lpt_first: LEB number of first LPT LEB
 * @lpt_lebs: number of LEBs for LPT is passed and returned here
 * @big_lpt: use big LPT model is passed and returned here
 * @hash: hash of the LPT is returned here
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_create_dflt_lpt(struct ubifs_info *c, int *main_lebs, int lpt_first,
			  int *lpt_lebs, int *big_lpt, u8 *hash)
{
	int lnum, err = 0, node_sz, iopos, i, j, cnt, len, alen, row;
	int blnum, boffs, bsz, bcnt;
	struct ubifs_pnode *pnode = NULL;
	struct ubifs_nnode *nnode = NULL;
	void *buf = NULL, *p;
	struct ubifs_lpt_lprops *ltab = NULL;
	int *lsave = NULL;
	struct shash_desc *desc;

	err = calc_dflt_lpt_geom(c, main_lebs, big_lpt);
	if (err)
		return err;
	*lpt_lebs = c->lpt_lebs;

	/* Needed by 'ubifs_pack_nnode()' and 'set_ltab()' */
	c->lpt_first = lpt_first;
	/* Needed by 'set_ltab()' */
	c->lpt_last = lpt_first + c->lpt_lebs - 1;
	/* Needed by 'ubifs_pack_lsave()' */
	c->main_first = c->leb_cnt - *main_lebs;

	desc = ubifs_hash_get_desc(c);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	lsave = kmalloc_array(c->lsave_cnt, sizeof(int), GFP_KERNEL);
	pnode = kzalloc(sizeof(struct ubifs_pnode), GFP_KERNEL);
	nnode = kzalloc(sizeof(struct ubifs_nnode), GFP_KERNEL);
	buf = vmalloc(c->leb_size);
	ltab = vmalloc(array_size(sizeof(struct ubifs_lpt_lprops),
				  c->lpt_lebs));
	if (!pnode || !nnode || !buf || !ltab || !lsave) {
		err = -ENOMEM;
		goto out;
	}

	ubifs_assert(c, !c->ltab);
	c->ltab = ltab; /* Needed by set_ltab */

	/* Initialize LPT's own lprops */
	for (i = 0; i < c->lpt_lebs; i++) {
		ltab[i].free = c->leb_size;
		ltab[i].dirty = 0;
		ltab[i].tgc = 0;
		ltab[i].cmt = 0;
	}

	lnum = lpt_first;
	p = buf;
	/* Number of leaf nodes (pnodes) */
	cnt = c->pnode_cnt;

	/*
	 * The first pnode contains the LEB properties for the LEBs that contain
	 * the root inode node and the root index node of the index tree.
	 */
	node_sz = ALIGN(ubifs_idx_node_sz(c, 1), 8);
	iopos = ALIGN(node_sz, c->min_io_size);
	pnode->lprops[0].free = c->leb_size - iopos;
	pnode->lprops[0].dirty = iopos - node_sz;
	pnode->lprops[0].flags = LPROPS_INDEX;

	node_sz = UBIFS_INO_NODE_SZ;
	iopos = ALIGN(node_sz, c->min_io_size);
	pnode->lprops[1].free = c->leb_size - iopos;
	pnode->lprops[1].dirty = iopos - node_sz;

	for (i = 2; i < UBIFS_LPT_FANOUT; i++)
		pnode->lprops[i].free = c->leb_size;

	/* Add first pnode */
	ubifs_pack_pnode(c, p, pnode);
	err = ubifs_shash_update(c, desc, p, c->pnode_sz);
	if (err)
		goto out;

	p += c->pnode_sz;
	len = c->pnode_sz;
	pnode->num += 1;

	/* Reset pnode values for remaining pnodes */
	pnode->lprops[0].free = c->leb_size;
	pnode->lprops[0].dirty = 0;
	pnode->lprops[0].flags = 0;

	pnode->lprops[1].free = c->leb_size;
	pnode->lprops[1].dirty = 0;

	/*
	 * To calculate the internal node branches, we keep information about
	 * the level below.
	 */
	blnum = lnum; /* LEB number of level below */
	boffs = 0; /* Offset of level below */
	bcnt = cnt; /* Number of nodes in level below */
	bsz = c->pnode_sz; /* Size of nodes in level below */

	/* Add all remaining pnodes */
	for (i = 1; i < cnt; i++) {
		if (len + c->pnode_sz > c->leb_size) {
			alen = ALIGN(len, c->min_io_size);
			set_ltab(c, lnum, c->leb_size - alen, alen - len);
			memset(p, 0xff, alen - len);
			err = ubifs_leb_change(c, lnum++, buf, alen);
			if (err)
				goto out;
			p = buf;
			len = 0;
		}
		ubifs_pack_pnode(c, p, pnode);
		err = ubifs_shash_update(c, desc, p, c->pnode_sz);
		if (err)
			goto out;

		p += c->pnode_sz;
		len += c->pnode_sz;
		/*
		 * pnodes are simply numbered left to right starting at zero,
		 * which means the pnode number can be used easily to traverse
		 * down the tree to the corresponding pnode.
		 */
		pnode->num += 1;
	}

	row = 0;
	for (i = UBIFS_LPT_FANOUT; cnt > i; i <<= UBIFS_LPT_FANOUT_SHIFT)
		row += 1;
	/* Add all nnodes, one level at a time */
	while (1) {
		/* Number of internal nodes (nnodes) at next level */
		cnt = DIV_ROUND_UP(cnt, UBIFS_LPT_FANOUT);
		for (i = 0; i < cnt; i++) {
			if (len + c->nnode_sz > c->leb_size) {
				alen = ALIGN(len, c->min_io_size);
				set_ltab(c, lnum, c->leb_size - alen,
					    alen - len);
				memset(p, 0xff, alen - len);
				err = ubifs_leb_change(c, lnum++, buf, alen);
				if (err)
					goto out;
				p = buf;
				len = 0;
			}
			/* Only 1 nnode at this level, so it is the root */
			if (cnt == 1) {
				c->lpt_lnum = lnum;
				c->lpt_offs = len;
			}
			/* Set branches to the level below */
			for (j = 0; j < UBIFS_LPT_FANOUT; j++) {
				if (bcnt) {
					if (boffs + bsz > c->leb_size) {
						blnum += 1;
						boffs = 0;
					}
					nnode->nbranch[j].lnum = blnum;
					nnode->nbranch[j].offs = boffs;
					boffs += bsz;
					bcnt--;
				} else {
					nnode->nbranch[j].lnum = 0;
					nnode->nbranch[j].offs = 0;
				}
			}
			nnode->num = calc_nnode_num(row, i);
			ubifs_pack_nnode(c, p, nnode);
			p += c->nnode_sz;
			len += c->nnode_sz;
		}
		/* Only 1 nnode at this level, so it is the root */
		if (cnt == 1)
			break;
		/* Update the information about the level below */
		bcnt = cnt;
		bsz = c->nnode_sz;
		row -= 1;
	}

	if (*big_lpt) {
		/* Need to add LPT's save table */
		if (len + c->lsave_sz > c->leb_size) {
			alen = ALIGN(len, c->min_io_size);
			set_ltab(c, lnum, c->leb_size - alen, alen - len);
			memset(p, 0xff, alen - len);
			err = ubifs_leb_change(c, lnum++, buf, alen);
			if (err)
				goto out;
			p = buf;
			len = 0;
		}

		c->lsave_lnum = lnum;
		c->lsave_offs = len;

		for (i = 0; i < c->lsave_cnt && i < *main_lebs; i++)
			lsave[i] = c->main_first + i;
		for (; i < c->lsave_cnt; i++)
			lsave[i] = c->main_first;

		ubifs_pack_lsave(c, p, lsave);
		p += c->lsave_sz;
		len += c->lsave_sz;
	}

	/* Need to add LPT's own LEB properties table */
	if (len + c->ltab_sz > c->leb_size) {
		alen = ALIGN(len, c->min_io_size);
		set_ltab(c, lnum, c->leb_size - alen, alen - len);
		memset(p, 0xff, alen - len);
		err = ubifs_leb_change(c, lnum++, buf, alen);
		if (err)
			goto out;
		p = buf;
		len = 0;
	}

	c->ltab_lnum = lnum;
	c->ltab_offs = len;

	/* Update ltab before packing it */
	len += c->ltab_sz;
	alen = ALIGN(len, c->min_io_size);
	set_ltab(c, lnum, c->leb_size - alen, alen - len);

	ubifs_pack_ltab(c, p, ltab);
	p += c->ltab_sz;

	/* Write remaining buffer */
	memset(p, 0xff, alen - len);
	err = ubifs_leb_change(c, lnum, buf, alen);
	if (err)
		goto out;

	err = ubifs_shash_final(c, desc, hash);
	if (err)
		goto out;

	c->nhead_lnum = lnum;
	c->nhead_offs = ALIGN(len, c->min_io_size);

	dbg_lp("space_bits %d", c->space_bits);
	dbg_lp("lpt_lnum_bits %d", c->lpt_lnum_bits);
	dbg_lp("lpt_offs_bits %d", c->lpt_offs_bits);
	dbg_lp("lpt_spc_bits %d", c->lpt_spc_bits);
	dbg_lp("pcnt_bits %d", c->pcnt_bits);
	dbg_lp("lnum_bits %d", c->lnum_bits);
	dbg_lp("pnode_sz %d", c->pnode_sz);
	dbg_lp("nnode_sz %d", c->nnode_sz);
	dbg_lp("ltab_sz %d", c->ltab_sz);
	dbg_lp("lsave_sz %d", c->lsave_sz);
	dbg_lp("lsave_cnt %d", c->lsave_cnt);
	dbg_lp("lpt_hght %d", c->lpt_hght);
	dbg_lp("big_lpt %d", c->big_lpt);
	dbg_lp("LPT root is at %d:%d", c->lpt_lnum, c->lpt_offs);
	dbg_lp("LPT head is at %d:%d", c->nhead_lnum, c->nhead_offs);
	dbg_lp("LPT ltab is at %d:%d", c->ltab_lnum, c->ltab_offs);
	if (c->big_lpt)
		dbg_lp("LPT lsave is at %d:%d", c->lsave_lnum, c->lsave_offs);
out:
	c->ltab = NULL;
	kfree(desc);
	kfree(lsave);
	vfree(ltab);
	vfree(buf);
	kfree(nnode);
	kfree(pnode);
	return err;
}

/**
 * update_cats - add LEB properties of a pnode to LEB category lists and heaps.
 * @c: UBIFS file-system description object
 * @pnode: pnode
 *
 * When a pnode is loaded into memory, the LEB properties it contains are added,
 * by this function, to the LEB category lists and heaps.
 */
static void update_cats(struct ubifs_info *c, struct ubifs_pnode *pnode)
{
	int i;

	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int cat = pnode->lprops[i].flags & LPROPS_CAT_MASK;
		int lnum = pnode->lprops[i].lnum;

		if (!lnum)
			return;
		ubifs_add_to_cat(c, &pnode->lprops[i], cat);
	}
}

/**
 * replace_cats - add LEB properties of a pnode to LEB category lists and heaps.
 * @c: UBIFS file-system description object
 * @old_pnode: pnode copied
 * @new_pnode: pnode copy
 *
 * During commit it is sometimes necessary to copy a pnode
 * (see dirty_cow_pnode).  When that happens, references in
 * category lists and heaps must be replaced.  This function does that.
 */
static void replace_cats(struct ubifs_info *c, struct ubifs_pnode *old_pnode,
			 struct ubifs_pnode *new_pnode)
{
	int i;

	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		if (!new_pnode->lprops[i].lnum)
			return;
		ubifs_replace_cat(c, &old_pnode->lprops[i],
				  &new_pnode->lprops[i]);
	}
}

/**
 * check_lpt_crc - check LPT node crc is correct.
 * @c: UBIFS file-system description object
 * @buf: buffer containing node
 * @len: length of node
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int check_lpt_crc(const struct ubifs_info *c, void *buf, int len)
{
	int pos = 0;
	uint8_t *addr = buf;
	uint16_t crc, calc_crc;

	crc = ubifs_unpack_bits(c, &addr, &pos, UBIFS_LPT_CRC_BITS);
	calc_crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
			 len - UBIFS_LPT_CRC_BYTES);
	if (crc != calc_crc) {
		ubifs_err(c, "invalid crc in LPT node: crc %hx calc %hx",
			  crc, calc_crc);
		dump_stack();
		return -EINVAL;
	}
	return 0;
}

/**
 * check_lpt_type - check LPT node type is correct.
 * @c: UBIFS file-system description object
 * @addr: address of type bit field is passed and returned updated here
 * @pos: position of type bit field is passed and returned updated here
 * @type: expected type
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int check_lpt_type(const struct ubifs_info *c, uint8_t **addr,
			  int *pos, int type)
{
	int node_type;

	node_type = ubifs_unpack_bits(c, addr, pos, UBIFS_LPT_TYPE_BITS);
	if (node_type != type) {
		ubifs_err(c, "invalid type (%d) in LPT node type %d",
			  node_type, type);
		dump_stack();
		return -EINVAL;
	}
	return 0;
}

/**
 * unpack_pnode - unpack a pnode.
 * @c: UBIFS file-system description object
 * @buf: buffer containing packed pnode to unpack
 * @pnode: pnode structure to fill
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unpack_pnode(const struct ubifs_info *c, void *buf,
			struct ubifs_pnode *pnode)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(c, &addr, &pos, UBIFS_LPT_PNODE);
	if (err)
		return err;
	if (c->big_lpt)
		pnode->num = ubifs_unpack_bits(c, &addr, &pos, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_lprops * const lprops = &pnode->lprops[i];

		lprops->free = ubifs_unpack_bits(c, &addr, &pos, c->space_bits);
		lprops->free <<= 3;
		lprops->dirty = ubifs_unpack_bits(c, &addr, &pos, c->space_bits);
		lprops->dirty <<= 3;

		if (ubifs_unpack_bits(c, &addr, &pos, 1))
			lprops->flags = LPROPS_INDEX;
		else
			lprops->flags = 0;
		lprops->flags |= ubifs_categorize_lprops(c, lprops);
	}
	err = check_lpt_crc(c, buf, c->pnode_sz);
	return err;
}

/**
 * ubifs_unpack_nnode - unpack a nnode.
 * @c: UBIFS file-system description object
 * @buf: buffer containing packed nnode to unpack
 * @nnode: nnode structure to fill
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_unpack_nnode(const struct ubifs_info *c, void *buf,
		       struct ubifs_nnode *nnode)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(c, &addr, &pos, UBIFS_LPT_NNODE);
	if (err)
		return err;
	if (c->big_lpt)
		nnode->num = ubifs_unpack_bits(c, &addr, &pos, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int lnum;

		lnum = ubifs_unpack_bits(c, &addr, &pos, c->lpt_lnum_bits) +
		       c->lpt_first;
		if (lnum == c->lpt_last + 1)
			lnum = 0;
		nnode->nbranch[i].lnum = lnum;
		nnode->nbranch[i].offs = ubifs_unpack_bits(c, &addr, &pos,
						     c->lpt_offs_bits);
	}
	err = check_lpt_crc(c, buf, c->nnode_sz);
	return err;
}

/**
 * unpack_ltab - unpack the LPT's own lprops table.
 * @c: UBIFS file-system description object
 * @buf: buffer from which to unpack
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unpack_ltab(const struct ubifs_info *c, void *buf)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(c, &addr, &pos, UBIFS_LPT_LTAB);
	if (err)
		return err;
	for (i = 0; i < c->lpt_lebs; i++) {
		int free = ubifs_unpack_bits(c, &addr, &pos, c->lpt_spc_bits);
		int dirty = ubifs_unpack_bits(c, &addr, &pos, c->lpt_spc_bits);

		if (free < 0 || free > c->leb_size || dirty < 0 ||
		    dirty > c->leb_size || free + dirty > c->leb_size)
			return -EINVAL;

		c->ltab[i].free = free;
		c->ltab[i].dirty = dirty;
		c->ltab[i].tgc = 0;
		c->ltab[i].cmt = 0;
	}
	err = check_lpt_crc(c, buf, c->ltab_sz);
	return err;
}

/**
 * unpack_lsave - unpack the LPT's save table.
 * @c: UBIFS file-system description object
 * @buf: buffer from which to unpack
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unpack_lsave(const struct ubifs_info *c, void *buf)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(c, &addr, &pos, UBIFS_LPT_LSAVE);
	if (err)
		return err;
	for (i = 0; i < c->lsave_cnt; i++) {
		int lnum = ubifs_unpack_bits(c, &addr, &pos, c->lnum_bits);

		if (lnum < c->main_first || lnum >= c->leb_cnt)
			return -EINVAL;
		c->lsave[i] = lnum;
	}
	err = check_lpt_crc(c, buf, c->lsave_sz);
	return err;
}

/**
 * validate_nnode - validate a nnode.
 * @c: UBIFS file-system description object
 * @nnode: nnode to validate
 * @parent: parent nnode (or NULL for the root nnode)
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int validate_nnode(const struct ubifs_info *c, struct ubifs_nnode *nnode,
			  struct ubifs_nnode *parent, int iip)
{
	int i, lvl, max_offs;

	if (c->big_lpt) {
		int num = calc_nnode_num_from_parent(c, parent, iip);

		if (nnode->num != num)
			return -EINVAL;
	}
	lvl = parent ? parent->level - 1 : c->lpt_hght;
	if (lvl < 1)
		return -EINVAL;
	if (lvl == 1)
		max_offs = c->leb_size - c->pnode_sz;
	else
		max_offs = c->leb_size - c->nnode_sz;
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int lnum = nnode->nbranch[i].lnum;
		int offs = nnode->nbranch[i].offs;

		if (lnum == 0) {
			if (offs != 0)
				return -EINVAL;
			continue;
		}
		if (lnum < c->lpt_first || lnum > c->lpt_last)
			return -EINVAL;
		if (offs < 0 || offs > max_offs)
			return -EINVAL;
	}
	return 0;
}

/**
 * validate_pnode - validate a pnode.
 * @c: UBIFS file-system description object
 * @pnode: pnode to validate
 * @parent: parent nnode
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int validate_pnode(const struct ubifs_info *c, struct ubifs_pnode *pnode,
			  struct ubifs_nnode *parent, int iip)
{
	int i;

	if (c->big_lpt) {
		int num = calc_pnode_num_from_parent(c, parent, iip);

		if (pnode->num != num)
			return -EINVAL;
	}
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int free = pnode->lprops[i].free;
		int dirty = pnode->lprops[i].dirty;

		if (free < 0 || free > c->leb_size || free % c->min_io_size ||
		    (free & 7))
			return -EINVAL;
		if (dirty < 0 || dirty > c->leb_size || (dirty & 7))
			return -EINVAL;
		if (dirty + free > c->leb_size)
			return -EINVAL;
	}
	return 0;
}

/**
 * set_pnode_lnum - set LEB numbers on a pnode.
 * @c: UBIFS file-system description object
 * @pnode: pnode to update
 *
 * This function calculates the LEB numbers for the LEB properties it contains
 * based on the pnode number.
 */
static void set_pnode_lnum(const struct ubifs_info *c,
			   struct ubifs_pnode *pnode)
{
	int i, lnum;

	lnum = (pnode->num << UBIFS_LPT_FANOUT_SHIFT) + c->main_first;
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		if (lnum >= c->leb_cnt)
			return;
		pnode->lprops[i].lnum = lnum++;
	}
}

/**
 * ubifs_read_nnode - read a nnode from flash and link it to the tree in memory.
 * @c: UBIFS file-system description object
 * @parent: parent nnode (or NULL for the root)
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_read_nnode(struct ubifs_info *c, struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch = NULL;
	struct ubifs_nnode *nnode = NULL;
	void *buf = c->lpt_nod_buf;
	int err, lnum, offs;

	if (parent) {
		branch = &parent->nbranch[iip];
		lnum = branch->lnum;
		offs = branch->offs;
	} else {
		lnum = c->lpt_lnum;
		offs = c->lpt_offs;
	}
	nnode = kzalloc(sizeof(struct ubifs_nnode), GFP_NOFS);
	if (!nnode) {
		err = -ENOMEM;
		goto out;
	}
	if (lnum == 0) {
		/*
		 * This nnode was not written which just means that the LEB
		 * properties in the subtree below it describe empty LEBs. We
		 * make the nnode as though we had read it, which in fact means
		 * doing almost nothing.
		 */
		if (c->big_lpt)
			nnode->num = calc_nnode_num_from_parent(c, parent, iip);
	} else {
		err = ubifs_leb_read(c, lnum, buf, offs, c->nnode_sz, 1);
		if (err)
			goto out;
		err = ubifs_unpack_nnode(c, buf, nnode);
		if (err)
			goto out;
	}
	err = validate_nnode(c, nnode, parent, iip);
	if (err)
		goto out;
	if (!c->big_lpt)
		nnode->num = calc_nnode_num_from_parent(c, parent, iip);
	if (parent) {
		branch->nnode = nnode;
		nnode->level = parent->level - 1;
	} else {
		c->nroot = nnode;
		nnode->level = c->lpt_hght;
	}
	nnode->parent = parent;
	nnode->iip = iip;
	return 0;

out:
	ubifs_err(c, "error %d reading nnode at %d:%d", err, lnum, offs);
	dump_stack();
	kfree(nnode);
	return err;
}

/**
 * read_pnode - read a pnode from flash and link it to the tree in memory.
 * @c: UBIFS file-system description object
 * @parent: parent nnode
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int read_pnode(struct ubifs_info *c, struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_pnode *pnode = NULL;
	void *buf = c->lpt_nod_buf;
	int err, lnum, offs;

	branch = &parent->nbranch[iip];
	lnum = branch->lnum;
	offs = branch->offs;
	pnode = kzalloc(sizeof(struct ubifs_pnode), GFP_NOFS);
	if (!pnode)
		return -ENOMEM;

	if (lnum == 0) {
		/*
		 * This pnode was not written which just means that the LEB
		 * properties in it describe empty LEBs. We make the pnode as
		 * though we had read it.
		 */
		int i;

		if (c->big_lpt)
			pnode->num = calc_pnode_num_from_parent(c, parent, iip);
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			struct ubifs_lprops * const lprops = &pnode->lprops[i];

			lprops->free = c->leb_size;
			lprops->flags = ubifs_categorize_lprops(c, lprops);
		}
	} else {
		err = ubifs_leb_read(c, lnum, buf, offs, c->pnode_sz, 1);
		if (err)
			goto out;
		err = unpack_pnode(c, buf, pnode);
		if (err)
			goto out;
	}
	err = validate_pnode(c, pnode, parent, iip);
	if (err)
		goto out;
	if (!c->big_lpt)
		pnode->num = calc_pnode_num_from_parent(c, parent, iip);
	branch->pnode = pnode;
	pnode->parent = parent;
	pnode->iip = iip;
	set_pnode_lnum(c, pnode);
	c->pnodes_have += 1;
	return 0;

out:
	ubifs_err(c, "error %d reading pnode at %d:%d", err, lnum, offs);
	ubifs_dump_pnode(c, pnode, parent, iip);
	dump_stack();
	ubifs_err(c, "calc num: %d", calc_pnode_num_from_parent(c, parent, iip));
	kfree(pnode);
	return err;
}

/**
 * read_ltab - read LPT's own lprops table.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int read_ltab(struct ubifs_info *c)
{
	int err;
	void *buf;

	buf = vmalloc(c->ltab_sz);
	if (!buf)
		return -ENOMEM;
	err = ubifs_leb_read(c, c->ltab_lnum, buf, c->ltab_offs, c->ltab_sz, 1);
	if (err)
		goto out;
	err = unpack_ltab(c, buf);
out:
	vfree(buf);
	return err;
}

/**
 * read_lsave - read LPT's save table.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int read_lsave(struct ubifs_info *c)
{
	int err, i;
	void *buf;

	buf = vmalloc(c->lsave_sz);
	if (!buf)
		return -ENOMEM;
	err = ubifs_leb_read(c, c->lsave_lnum, buf, c->lsave_offs,
			     c->lsave_sz, 1);
	if (err)
		goto out;
	err = unpack_lsave(c, buf);
	if (err)
		goto out;
	for (i = 0; i < c->lsave_cnt; i++) {
		int lnum = c->lsave[i];
		struct ubifs_lprops *lprops;

		/*
		 * Due to automatic resizing, the values in the lsave table
		 * could be beyond the volume size - just ignore them.
		 */
		if (lnum >= c->leb_cnt)
			continue;
		lprops = ubifs_lpt_lookup(c, lnum);
		if (IS_ERR(lprops)) {
			err = PTR_ERR(lprops);
			goto out;
		}
	}
out:
	vfree(buf);
	return err;
}

/**
 * ubifs_get_nnode - get a nnode.
 * @c: UBIFS file-system description object
 * @parent: parent nnode (or NULL for the root)
 * @iip: index in parent
 *
 * This function returns a pointer to the nnode on success or a negative error
 * code on failure.
 */
struct ubifs_nnode *ubifs_get_nnode(struct ubifs_info *c,
				    struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_nnode *nnode;
	int err;

	branch = &parent->nbranch[iip];
	nnode = branch->nnode;
	if (nnode)
		return nnode;
	err = ubifs_read_nnode(c, parent, iip);
	if (err)
		return ERR_PTR(err);
	return branch->nnode;
}

/**
 * ubifs_get_pnode - get a pnode.
 * @c: UBIFS file-system description object
 * @parent: parent nnode
 * @iip: index in parent
 *
 * This function returns a pointer to the pnode on success or a negative error
 * code on failure.
 */
struct ubifs_pnode *ubifs_get_pnode(struct ubifs_info *c,
				    struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_pnode *pnode;
	int err;

	branch = &parent->nbranch[iip];
	pnode = branch->pnode;
	if (pnode)
		return pnode;
	err = read_pnode(c, parent, iip);
	if (err)
		return ERR_PTR(err);
	update_cats(c, branch->pnode);
	return branch->pnode;
}

/**
 * ubifs_pnode_lookup - lookup a pnode in the LPT.
 * @c: UBIFS file-system description object
 * @i: pnode number (0 to (main_lebs - 1) / UBIFS_LPT_FANOUT)
 *
 * This function returns a pointer to the pnode on success or a negative
 * error code on failure.
 */
struct ubifs_pnode *ubifs_pnode_lookup(struct ubifs_info *c, int i)
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
			return ERR_CAST(nnode);
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	return ubifs_get_pnode(c, nnode, iip);
}

/**
 * ubifs_lpt_lookup - lookup LEB properties in the LPT.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to lookup
 *
 * This function returns a pointer to the LEB properties on success or a
 * negative error code on failure.
 */
struct ubifs_lprops *ubifs_lpt_lookup(struct ubifs_info *c, int lnum)
{
	int i, iip;
	struct ubifs_pnode *pnode;

	i = lnum - c->main_first;
	pnode = ubifs_pnode_lookup(c, i >> UBIFS_LPT_FANOUT_SHIFT);
	if (IS_ERR(pnode))
		return ERR_CAST(pnode);
	iip = (i & (UBIFS_LPT_FANOUT - 1));
	dbg_lp("LEB %d, free %d, dirty %d, flags %d", lnum,
	       pnode->lprops[iip].free, pnode->lprops[iip].dirty,
	       pnode->lprops[iip].flags);
	return &pnode->lprops[iip];
}

/**
 * dirty_cow_nnode - ensure a nnode is not being committed.
 * @c: UBIFS file-system description object
 * @nnode: nnode to check
 *
 * Returns dirtied nnode on success or negative error code on failure.
 */
static struct ubifs_nnode *dirty_cow_nnode(struct ubifs_info *c,
					   struct ubifs_nnode *nnode)
{
	struct ubifs_nnode *n;
	int i;

	if (!test_bit(COW_CNODE, &nnode->flags)) {
		/* nnode is not being committed */
		if (!test_and_set_bit(DIRTY_CNODE, &nnode->flags)) {
			c->dirty_nn_cnt += 1;
			ubifs_add_nnode_dirt(c, nnode);
		}
		return nnode;
	}

	/* nnode is being committed, so copy it */
	n = kmemdup(nnode, sizeof(struct ubifs_nnode), GFP_NOFS);
	if (unlikely(!n))
		return ERR_PTR(-ENOMEM);

	n->cnext = NULL;
	__set_bit(DIRTY_CNODE, &n->flags);
	__clear_bit(COW_CNODE, &n->flags);

	/* The children now have new parent */
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_nbranch *branch = &n->nbranch[i];

		if (branch->cnode)
			branch->cnode->parent = n;
	}

	ubifs_assert(c, !test_bit(OBSOLETE_CNODE, &nnode->flags));
	__set_bit(OBSOLETE_CNODE, &nnode->flags);

	c->dirty_nn_cnt += 1;
	ubifs_add_nnode_dirt(c, nnode);
	if (nnode->parent)
		nnode->parent->nbranch[n->iip].nnode = n;
	else
		c->nroot = n;
	return n;
}

/**
 * dirty_cow_pnode - ensure a pnode is not being committed.
 * @c: UBIFS file-system description object
 * @pnode: pnode to check
 *
 * Returns dirtied pnode on success or negative error code on failure.
 */
static struct ubifs_pnode *dirty_cow_pnode(struct ubifs_info *c,
					   struct ubifs_pnode *pnode)
{
	struct ubifs_pnode *p;

	if (!test_bit(COW_CNODE, &pnode->flags)) {
		/* pnode is not being committed */
		if (!test_and_set_bit(DIRTY_CNODE, &pnode->flags)) {
			c->dirty_pn_cnt += 1;
			add_pnode_dirt(c, pnode);
		}
		return pnode;
	}

	/* pnode is being committed, so copy it */
	p = kmemdup(pnode, sizeof(struct ubifs_pnode), GFP_NOFS);
	if (unlikely(!p))
		return ERR_PTR(-ENOMEM);

	p->cnext = NULL;
	__set_bit(DIRTY_CNODE, &p->flags);
	__clear_bit(COW_CNODE, &p->flags);
	replace_cats(c, pnode, p);

	ubifs_assert(c, !test_bit(OBSOLETE_CNODE, &pnode->flags));
	__set_bit(OBSOLETE_CNODE, &pnode->flags);

	c->dirty_pn_cnt += 1;
	add_pnode_dirt(c, pnode);
	pnode->parent->nbranch[p->iip].pnode = p;
	return p;
}

/**
 * ubifs_lpt_lookup_dirty - lookup LEB properties in the LPT.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to lookup
 *
 * This function returns a pointer to the LEB properties on success or a
 * negative error code on failure.
 */
struct ubifs_lprops *ubifs_lpt_lookup_dirty(struct ubifs_info *c, int lnum)
{
	int err, i, h, iip, shft;
	struct ubifs_nnode *nnode;
	struct ubifs_pnode *pnode;

	if (!c->nroot) {
		err = ubifs_read_nnode(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	nnode = c->nroot;
	nnode = dirty_cow_nnode(c, nnode);
	if (IS_ERR(nnode))
		return ERR_CAST(nnode);
	i = lnum - c->main_first;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nnode = ubifs_get_nnode(c, nnode, iip);
		if (IS_ERR(nnode))
			return ERR_CAST(nnode);
		nnode = dirty_cow_nnode(c, nnode);
		if (IS_ERR(nnode))
			return ERR_CAST(nnode);
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	pnode = ubifs_get_pnode(c, nnode, iip);
	if (IS_ERR(pnode))
		return ERR_CAST(pnode);
	pnode = dirty_cow_pnode(c, pnode);
	if (IS_ERR(pnode))
		return ERR_CAST(pnode);
	iip = (i & (UBIFS_LPT_FANOUT - 1));
	dbg_lp("LEB %d, free %d, dirty %d, flags %d", lnum,
	       pnode->lprops[iip].free, pnode->lprops[iip].dirty,
	       pnode->lprops[iip].flags);
	ubifs_assert(c, test_bit(DIRTY_CNODE, &pnode->flags));
	return &pnode->lprops[iip];
}

/**
 * ubifs_lpt_calc_hash - Calculate hash of the LPT pnodes
 * @c: UBIFS file-system description object
 * @hash: the returned hash of the LPT pnodes
 *
 * This function iterates over the LPT pnodes and creates a hash over them.
 * Returns 0 for success or a negative error code otherwise.
 */
int ubifs_lpt_calc_hash(struct ubifs_info *c, u8 *hash)
{
	struct ubifs_nnode *nnode, *nn;
	struct ubifs_cnode *cnode;
	struct shash_desc *desc;
	int iip = 0, i;
	int bufsiz = max_t(int, c->nnode_sz, c->pnode_sz);
	void *buf;
	int err;

	if (!ubifs_authenticated(c))
		return 0;

	if (!c->nroot) {
		err = ubifs_read_nnode(c, NULL, 0);
		if (err)
			return err;
	}

	desc = ubifs_hash_get_desc(c);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	buf = kmalloc(bufsiz, GFP_NOFS);
	if (!buf) {
		err = -ENOMEM;
		goto out;
	}

	cnode = (struct ubifs_cnode *)c->nroot;

	while (cnode) {
		nnode = cnode->parent;
		nn = (struct ubifs_nnode *)cnode;
		if (cnode->level > 1) {
			while (iip < UBIFS_LPT_FANOUT) {
				if (nn->nbranch[iip].lnum == 0) {
					/* Go right */
					iip++;
					continue;
				}

				nnode = ubifs_get_nnode(c, nn, iip);
				if (IS_ERR(nnode)) {
					err = PTR_ERR(nnode);
					goto out;
				}

				/* Go down */
				iip = 0;
				cnode = (struct ubifs_cnode *)nnode;
				break;
			}
			if (iip < UBIFS_LPT_FANOUT)
				continue;
		} else {
			struct ubifs_pnode *pnode;

			for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
				if (nn->nbranch[i].lnum == 0)
					continue;
				pnode = ubifs_get_pnode(c, nn, i);
				if (IS_ERR(pnode)) {
					err = PTR_ERR(pnode);
					goto out;
				}

				ubifs_pack_pnode(c, buf, pnode);
				err = ubifs_shash_update(c, desc, buf,
							 c->pnode_sz);
				if (err)
					goto out;
			}
		}
		/* Go up and to the right */
		iip = cnode->iip + 1;
		cnode = (struct ubifs_cnode *)nnode;
	}

	err = ubifs_shash_final(c, desc, hash);
out:
	kfree(desc);
	kfree(buf);

	return err;
}

/**
 * lpt_check_hash - check the hash of the LPT.
 * @c: UBIFS file-system description object
 *
 * This function calculates a hash over all pnodes in the LPT and compares it with
 * the hash stored in the master node. Returns %0 on success and a negative error
 * code on failure.
 */
static int lpt_check_hash(struct ubifs_info *c)
{
	int err;
	u8 hash[UBIFS_HASH_ARR_SZ];

	if (!ubifs_authenticated(c))
		return 0;

	err = ubifs_lpt_calc_hash(c, hash);
	if (err)
		return err;

	if (ubifs_check_hash(c, c->mst_node->hash_lpt, hash)) {
		err = -EPERM;
		ubifs_err(c, "Failed to authenticate LPT");
	} else {
		err = 0;
	}

	return err;
}

/**
 * lpt_init_rd - initialize the LPT for reading.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int lpt_init_rd(struct ubifs_info *c)
{
	int err, i;

	c->ltab = vmalloc(array_size(sizeof(struct ubifs_lpt_lprops),
				     c->lpt_lebs));
	if (!c->ltab)
		return -ENOMEM;

	i = max_t(int, c->nnode_sz, c->pnode_sz);
	c->lpt_nod_buf = kmalloc(i, GFP_KERNEL);
	if (!c->lpt_nod_buf)
		return -ENOMEM;

	for (i = 0; i < LPROPS_HEAP_CNT; i++) {
		c->lpt_heap[i].arr = kmalloc_array(LPT_HEAP_SZ,
						   sizeof(void *),
						   GFP_KERNEL);
		if (!c->lpt_heap[i].arr)
			return -ENOMEM;
		c->lpt_heap[i].cnt = 0;
		c->lpt_heap[i].max_cnt = LPT_HEAP_SZ;
	}

	c->dirty_idx.arr = kmalloc_array(LPT_HEAP_SZ, sizeof(void *),
					 GFP_KERNEL);
	if (!c->dirty_idx.arr)
		return -ENOMEM;
	c->dirty_idx.cnt = 0;
	c->dirty_idx.max_cnt = LPT_HEAP_SZ;

	err = read_ltab(c);
	if (err)
		return err;

	err = lpt_check_hash(c);
	if (err)
		return err;

	dbg_lp("space_bits %d", c->space_bits);
	dbg_lp("lpt_lnum_bits %d", c->lpt_lnum_bits);
	dbg_lp("lpt_offs_bits %d", c->lpt_offs_bits);
	dbg_lp("lpt_spc_bits %d", c->lpt_spc_bits);
	dbg_lp("pcnt_bits %d", c->pcnt_bits);
	dbg_lp("lnum_bits %d", c->lnum_bits);
	dbg_lp("pnode_sz %d", c->pnode_sz);
	dbg_lp("nnode_sz %d", c->nnode_sz);
	dbg_lp("ltab_sz %d", c->ltab_sz);
	dbg_lp("lsave_sz %d", c->lsave_sz);
	dbg_lp("lsave_cnt %d", c->lsave_cnt);
	dbg_lp("lpt_hght %d", c->lpt_hght);
	dbg_lp("big_lpt %d", c->big_lpt);
	dbg_lp("LPT root is at %d:%d", c->lpt_lnum, c->lpt_offs);
	dbg_lp("LPT head is at %d:%d", c->nhead_lnum, c->nhead_offs);
	dbg_lp("LPT ltab is at %d:%d", c->ltab_lnum, c->ltab_offs);
	if (c->big_lpt)
		dbg_lp("LPT lsave is at %d:%d", c->lsave_lnum, c->lsave_offs);

	return 0;
}

/**
 * lpt_init_wr - initialize the LPT for writing.
 * @c: UBIFS file-system description object
 *
 * 'lpt_init_rd()' must have been called already.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int lpt_init_wr(struct ubifs_info *c)
{
	int err, i;

	c->ltab_cmt = vmalloc(array_size(sizeof(struct ubifs_lpt_lprops),
					 c->lpt_lebs));
	if (!c->ltab_cmt)
		return -ENOMEM;

	c->lpt_buf = vmalloc(c->leb_size);
	if (!c->lpt_buf)
		return -ENOMEM;

	if (c->big_lpt) {
		c->lsave = kmalloc_array(c->lsave_cnt, sizeof(int), GFP_NOFS);
		if (!c->lsave)
			return -ENOMEM;
		err = read_lsave(c);
		if (err)
			return err;
	}

	for (i = 0; i < c->lpt_lebs; i++)
		if (c->ltab[i].free == c->leb_size) {
			err = ubifs_leb_unmap(c, i + c->lpt_first);
			if (err)
				return err;
		}

	return 0;
}

/**
 * ubifs_lpt_init - initialize the LPT.
 * @c: UBIFS file-system description object
 * @rd: whether to initialize lpt for reading
 * @wr: whether to initialize lpt for writing
 *
 * For mounting 'rw', @rd and @wr are both true. For mounting 'ro', @rd is true
 * and @wr is false. For mounting from 'ro' to 'rw', @rd is false and @wr is
 * true.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_lpt_init(struct ubifs_info *c, int rd, int wr)
{
	int err;

	if (rd) {
		err = lpt_init_rd(c);
		if (err)
			goto out_err;
	}

	if (wr) {
		err = lpt_init_wr(c);
		if (err)
			goto out_err;
	}

	return 0;

out_err:
	if (wr)
		ubifs_lpt_free(c, 1);
	if (rd)
		ubifs_lpt_free(c, 0);
	return err;
}

/**
 * struct lpt_scan_node - somewhere to put nodes while we scan LPT.
 * @nnode: where to keep a nnode
 * @pnode: where to keep a pnode
 * @cnode: where to keep a cnode
 * @in_tree: is the node in the tree in memory
 * @ptr.nnode: pointer to the nnode (if it is an nnode) which may be here or in
 * the tree
 * @ptr.pnode: ditto for pnode
 * @ptr.cnode: ditto for cnode
 */
struct lpt_scan_node {
	union {
		struct ubifs_nnode nnode;
		struct ubifs_pnode pnode;
		struct ubifs_cnode cnode;
	};
	int in_tree;
	union {
		struct ubifs_nnode *nnode;
		struct ubifs_pnode *pnode;
		struct ubifs_cnode *cnode;
	} ptr;
};

/**
 * scan_get_nnode - for the scan, get a nnode from either the tree or flash.
 * @c: the UBIFS file-system description object
 * @path: where to put the nnode
 * @parent: parent of the nnode
 * @iip: index in parent of the nnode
 *
 * This function returns a pointer to the nnode on success or a negative error
 * code on failure.
 */
static struct ubifs_nnode *scan_get_nnode(struct ubifs_info *c,
					  struct lpt_scan_node *path,
					  struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_nnode *nnode;
	void *buf = c->lpt_nod_buf;
	int err;

	branch = &parent->nbranch[iip];
	nnode = branch->nnode;
	if (nnode) {
		path->in_tree = 1;
		path->ptr.nnode = nnode;
		return nnode;
	}
	nnode = &path->nnode;
	path->in_tree = 0;
	path->ptr.nnode = nnode;
	memset(nnode, 0, sizeof(struct ubifs_nnode));
	if (branch->lnum == 0) {
		/*
		 * This nnode was not written which just means that the LEB
		 * properties in the subtree below it describe empty LEBs. We
		 * make the nnode as though we had read it, which in fact means
		 * doing almost nothing.
		 */
		if (c->big_lpt)
			nnode->num = calc_nnode_num_from_parent(c, parent, iip);
	} else {
		err = ubifs_leb_read(c, branch->lnum, buf, branch->offs,
				     c->nnode_sz, 1);
		if (err)
			return ERR_PTR(err);
		err = ubifs_unpack_nnode(c, buf, nnode);
		if (err)
			return ERR_PTR(err);
	}
	err = validate_nnode(c, nnode, parent, iip);
	if (err)
		return ERR_PTR(err);
	if (!c->big_lpt)
		nnode->num = calc_nnode_num_from_parent(c, parent, iip);
	nnode->level = parent->level - 1;
	nnode->parent = parent;
	nnode->iip = iip;
	return nnode;
}

/**
 * scan_get_pnode - for the scan, get a pnode from either the tree or flash.
 * @c: the UBIFS file-system description object
 * @path: where to put the pnode
 * @parent: parent of the pnode
 * @iip: index in parent of the pnode
 *
 * This function returns a pointer to the pnode on success or a negative error
 * code on failure.
 */
static struct ubifs_pnode *scan_get_pnode(struct ubifs_info *c,
					  struct lpt_scan_node *path,
					  struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_pnode *pnode;
	void *buf = c->lpt_nod_buf;
	int err;

	branch = &parent->nbranch[iip];
	pnode = branch->pnode;
	if (pnode) {
		path->in_tree = 1;
		path->ptr.pnode = pnode;
		return pnode;
	}
	pnode = &path->pnode;
	path->in_tree = 0;
	path->ptr.pnode = pnode;
	memset(pnode, 0, sizeof(struct ubifs_pnode));
	if (branch->lnum == 0) {
		/*
		 * This pnode was not written which just means that the LEB
		 * properties in it describe empty LEBs. We make the pnode as
		 * though we had read it.
		 */
		int i;

		if (c->big_lpt)
			pnode->num = calc_pnode_num_from_parent(c, parent, iip);
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			struct ubifs_lprops * const lprops = &pnode->lprops[i];

			lprops->free = c->leb_size;
			lprops->flags = ubifs_categorize_lprops(c, lprops);
		}
	} else {
		ubifs_assert(c, branch->lnum >= c->lpt_first &&
			     branch->lnum <= c->lpt_last);
		ubifs_assert(c, branch->offs >= 0 && branch->offs < c->leb_size);
		err = ubifs_leb_read(c, branch->lnum, buf, branch->offs,
				     c->pnode_sz, 1);
		if (err)
			return ERR_PTR(err);
		err = unpack_pnode(c, buf, pnode);
		if (err)
			return ERR_PTR(err);
	}
	err = validate_pnode(c, pnode, parent, iip);
	if (err)
		return ERR_PTR(err);
	if (!c->big_lpt)
		pnode->num = calc_pnode_num_from_parent(c, parent, iip);
	pnode->parent = parent;
	pnode->iip = iip;
	set_pnode_lnum(c, pnode);
	return pnode;
}

/**
 * ubifs_lpt_scan_nolock - scan the LPT.
 * @c: the UBIFS file-system description object
 * @start_lnum: LEB number from which to start scanning
 * @end_lnum: LEB number at which to stop scanning
 * @scan_cb: callback function called for each lprops
 * @data: data to be passed to the callback function
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_lpt_scan_nolock(struct ubifs_info *c, int start_lnum, int end_lnum,
			  ubifs_lpt_scan_callback scan_cb, void *data)
{
	int err = 0, i, h, iip, shft;
	struct ubifs_nnode *nnode;
	struct ubifs_pnode *pnode;
	struct lpt_scan_node *path;

	if (start_lnum == -1) {
		start_lnum = end_lnum + 1;
		if (start_lnum >= c->leb_cnt)
			start_lnum = c->main_first;
	}

	ubifs_assert(c, start_lnum >= c->main_first && start_lnum < c->leb_cnt);
	ubifs_assert(c, end_lnum >= c->main_first && end_lnum < c->leb_cnt);

	if (!c->nroot) {
		err = ubifs_read_nnode(c, NULL, 0);
		if (err)
			return err;
	}

	path = kmalloc_array(c->lpt_hght + 1, sizeof(struct lpt_scan_node),
			     GFP_NOFS);
	if (!path)
		return -ENOMEM;

	path[0].ptr.nnode = c->nroot;
	path[0].in_tree = 1;
again:
	/* Descend to the pnode containing start_lnum */
	nnode = c->nroot;
	i = start_lnum - c->main_first;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nnode = scan_get_nnode(c, path + h, nnode, iip);
		if (IS_ERR(nnode)) {
			err = PTR_ERR(nnode);
			goto out;
		}
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	pnode = scan_get_pnode(c, path + h, nnode, iip);
	if (IS_ERR(pnode)) {
		err = PTR_ERR(pnode);
		goto out;
	}
	iip = (i & (UBIFS_LPT_FANOUT - 1));

	/* Loop for each lprops */
	while (1) {
		struct ubifs_lprops *lprops = &pnode->lprops[iip];
		int ret, lnum = lprops->lnum;

		ret = scan_cb(c, lprops, path[h].in_tree, data);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		if (ret & LPT_SCAN_ADD) {
			/* Add all the nodes in path to the tree in memory */
			for (h = 1; h < c->lpt_hght; h++) {
				const size_t sz = sizeof(struct ubifs_nnode);
				struct ubifs_nnode *parent;

				if (path[h].in_tree)
					continue;
				nnode = kmemdup(&path[h].nnode, sz, GFP_NOFS);
				if (!nnode) {
					err = -ENOMEM;
					goto out;
				}
				parent = nnode->parent;
				parent->nbranch[nnode->iip].nnode = nnode;
				path[h].ptr.nnode = nnode;
				path[h].in_tree = 1;
				path[h + 1].cnode.parent = nnode;
			}
			if (path[h].in_tree)
				ubifs_ensure_cat(c, lprops);
			else {
				const size_t sz = sizeof(struct ubifs_pnode);
				struct ubifs_nnode *parent;

				pnode = kmemdup(&path[h].pnode, sz, GFP_NOFS);
				if (!pnode) {
					err = -ENOMEM;
					goto out;
				}
				parent = pnode->parent;
				parent->nbranch[pnode->iip].pnode = pnode;
				path[h].ptr.pnode = pnode;
				path[h].in_tree = 1;
				update_cats(c, pnode);
				c->pnodes_have += 1;
			}
			err = dbg_check_lpt_nodes(c, (struct ubifs_cnode *)
						  c->nroot, 0, 0);
			if (err)
				goto out;
			err = dbg_check_cats(c);
			if (err)
				goto out;
		}
		if (ret & LPT_SCAN_STOP) {
			err = 0;
			break;
		}
		/* Get the next lprops */
		if (lnum == end_lnum) {
			/*
			 * We got to the end without finding what we were
			 * looking for
			 */
			err = -ENOSPC;
			goto out;
		}
		if (lnum + 1 >= c->leb_cnt) {
			/* Wrap-around to the beginning */
			start_lnum = c->main_first;
			goto again;
		}
		if (iip + 1 < UBIFS_LPT_FANOUT) {
			/* Next lprops is in the same pnode */
			iip += 1;
			continue;
		}
		/* We need to get the next pnode. Go up until we can go right */
		iip = pnode->iip;
		while (1) {
			h -= 1;
			ubifs_assert(c, h >= 0);
			nnode = path[h].ptr.nnode;
			if (iip + 1 < UBIFS_LPT_FANOUT)
				break;
			iip = nnode->iip;
		}
		/* Go right */
		iip += 1;
		/* Descend to the pnode */
		h += 1;
		for (; h < c->lpt_hght; h++) {
			nnode = scan_get_nnode(c, path + h, nnode, iip);
			if (IS_ERR(nnode)) {
				err = PTR_ERR(nnode);
				goto out;
			}
			iip = 0;
		}
		pnode = scan_get_pnode(c, path + h, nnode, iip);
		if (IS_ERR(pnode)) {
			err = PTR_ERR(pnode);
			goto out;
		}
		iip = 0;
	}
out:
	kfree(path);
	return err;
}

/**
 * dbg_chk_pnode - check a pnode.
 * @c: the UBIFS file-system description object
 * @pnode: pnode to check
 * @col: pnode column
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_chk_pnode(struct ubifs_info *c, struct ubifs_pnode *pnode,
			 int col)
{
	int i;

	if (pnode->num != col) {
		ubifs_err(c, "pnode num %d expected %d parent num %d iip %d",
			  pnode->num, col, pnode->parent->num, pnode->iip);
		return -EINVAL;
	}
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_lprops *lp, *lprops = &pnode->lprops[i];
		int lnum = (pnode->num << UBIFS_LPT_FANOUT_SHIFT) + i +
			   c->main_first;
		int found, cat = lprops->flags & LPROPS_CAT_MASK;
		struct ubifs_lpt_heap *heap;
		struct list_head *list = NULL;

		if (lnum >= c->leb_cnt)
			continue;
		if (lprops->lnum != lnum) {
			ubifs_err(c, "bad LEB number %d expected %d",
				  lprops->lnum, lnum);
			return -EINVAL;
		}
		if (lprops->flags & LPROPS_TAKEN) {
			if (cat != LPROPS_UNCAT) {
				ubifs_err(c, "LEB %d taken but not uncat %d",
					  lprops->lnum, cat);
				return -EINVAL;
			}
			continue;
		}
		if (lprops->flags & LPROPS_INDEX) {
			switch (cat) {
			case LPROPS_UNCAT:
			case LPROPS_DIRTY_IDX:
			case LPROPS_FRDI_IDX:
				break;
			default:
				ubifs_err(c, "LEB %d index but cat %d",
					  lprops->lnum, cat);
				return -EINVAL;
			}
		} else {
			switch (cat) {
			case LPROPS_UNCAT:
			case LPROPS_DIRTY:
			case LPROPS_FREE:
			case LPROPS_EMPTY:
			case LPROPS_FREEABLE:
				break;
			default:
				ubifs_err(c, "LEB %d not index but cat %d",
					  lprops->lnum, cat);
				return -EINVAL;
			}
		}
		switch (cat) {
		case LPROPS_UNCAT:
			list = &c->uncat_list;
			break;
		case LPROPS_EMPTY:
			list = &c->empty_list;
			break;
		case LPROPS_FREEABLE:
			list = &c->freeable_list;
			break;
		case LPROPS_FRDI_IDX:
			list = &c->frdi_idx_list;
			break;
		}
		found = 0;
		switch (cat) {
		case LPROPS_DIRTY:
		case LPROPS_DIRTY_IDX:
		case LPROPS_FREE:
			heap = &c->lpt_heap[cat - 1];
			if (lprops->hpos < heap->cnt &&
			    heap->arr[lprops->hpos] == lprops)
				found = 1;
			break;
		case LPROPS_UNCAT:
		case LPROPS_EMPTY:
		case LPROPS_FREEABLE:
		case LPROPS_FRDI_IDX:
			list_for_each_entry(lp, list, list)
				if (lprops == lp) {
					found = 1;
					break;
				}
			break;
		}
		if (!found) {
			ubifs_err(c, "LEB %d cat %d not found in cat heap/list",
				  lprops->lnum, cat);
			return -EINVAL;
		}
		switch (cat) {
		case LPROPS_EMPTY:
			if (lprops->free != c->leb_size) {
				ubifs_err(c, "LEB %d cat %d free %d dirty %d",
					  lprops->lnum, cat, lprops->free,
					  lprops->dirty);
				return -EINVAL;
			}
			break;
		case LPROPS_FREEABLE:
		case LPROPS_FRDI_IDX:
			if (lprops->free + lprops->dirty != c->leb_size) {
				ubifs_err(c, "LEB %d cat %d free %d dirty %d",
					  lprops->lnum, cat, lprops->free,
					  lprops->dirty);
				return -EINVAL;
			}
			break;
		}
	}
	return 0;
}

/**
 * dbg_check_lpt_nodes - check nnodes and pnodes.
 * @c: the UBIFS file-system description object
 * @cnode: next cnode (nnode or pnode) to check
 * @row: row of cnode (root is zero)
 * @col: column of cnode (leftmost is zero)
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int dbg_check_lpt_nodes(struct ubifs_info *c, struct ubifs_cnode *cnode,
			int row, int col)
{
	struct ubifs_nnode *nnode, *nn;
	struct ubifs_cnode *cn;
	int num, iip = 0, err;

	if (!dbg_is_chk_lprops(c))
		return 0;

	while (cnode) {
		ubifs_assert(c, row >= 0);
		nnode = cnode->parent;
		if (cnode->level) {
			/* cnode is a nnode */
			num = calc_nnode_num(row, col);
			if (cnode->num != num) {
				ubifs_err(c, "nnode num %d expected %d parent num %d iip %d",
					  cnode->num, num,
					  (nnode ? nnode->num : 0), cnode->iip);
				return -EINVAL;
			}
			nn = (struct ubifs_nnode *)cnode;
			while (iip < UBIFS_LPT_FANOUT) {
				cn = nn->nbranch[iip].cnode;
				if (cn) {
					/* Go down */
					row += 1;
					col <<= UBIFS_LPT_FANOUT_SHIFT;
					col += iip;
					iip = 0;
					cnode = cn;
					break;
				}
				/* Go right */
				iip += 1;
			}
			if (iip < UBIFS_LPT_FANOUT)
				continue;
		} else {
			struct ubifs_pnode *pnode;

			/* cnode is a pnode */
			pnode = (struct ubifs_pnode *)cnode;
			err = dbg_chk_pnode(c, pnode, col);
			if (err)
				return err;
		}
		/* Go up and to the right */
		row -= 1;
		col >>= UBIFS_LPT_FANOUT_SHIFT;
		iip = cnode->iip + 1;
		cnode = (struct ubifs_cnode *)nnode;
	}
	return 0;
}

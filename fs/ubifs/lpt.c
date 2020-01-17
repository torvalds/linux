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
 * selected for garbage collection, which consists of marking the clean yesdes in
 * that LEB as dirty, and then only the dirty yesdes are written out. Also, in
 * the case of the big model, a table of LEB numbers is saved so that the entire
 * LPT does yest to be scanned looking for empty eraseblocks when UBIFS is first
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
 * Calculate the sizes of LPT bit fields, yesdes, and tree, based on the
 * properties of the flash and whether LPT is "big" (c->big_lpt).
 */
static void do_calc_lpt_geom(struct ubifs_info *c)
{
	int i, n, bits, per_leb_wastage, max_pyesde_cnt;
	long long sz, tot_wastage;

	n = c->main_lebs + c->max_leb_cnt - c->leb_cnt;
	max_pyesde_cnt = DIV_ROUND_UP(n, UBIFS_LPT_FANOUT);

	c->lpt_hght = 1;
	n = UBIFS_LPT_FANOUT;
	while (n < max_pyesde_cnt) {
		c->lpt_hght += 1;
		n <<= UBIFS_LPT_FANOUT_SHIFT;
	}

	c->pyesde_cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);

	n = DIV_ROUND_UP(c->pyesde_cnt, UBIFS_LPT_FANOUT);
	c->nyesde_cnt = n;
	for (i = 1; i < c->lpt_hght; i++) {
		n = DIV_ROUND_UP(n, UBIFS_LPT_FANOUT);
		c->nyesde_cnt += n;
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
	c->pyesde_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       (c->big_lpt ? c->pcnt_bits : 0) +
	       (c->lpt_lnum_bits + c->lpt_offs_bits) * UBIFS_LPT_FANOUT;
	c->nyesde_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       c->lpt_lebs * c->lpt_spc_bits * 2;
	c->ltab_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       c->lnum_bits * c->lsave_cnt;
	c->lsave_sz = (bits + 7) / 8;

	/* Calculate the minimum LPT size */
	c->lpt_sz = (long long)c->pyesde_cnt * c->pyesde_sz;
	c->lpt_sz += (long long)c->nyesde_cnt * c->nyesde_sz;
	c->lpt_sz += c->ltab_sz;
	if (c->big_lpt)
		c->lpt_sz += c->lsave_sz;

	/* Add wastage */
	sz = c->lpt_sz;
	per_leb_wastage = max_t(int, c->pyesde_sz, c->nyesde_sz);
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

	/* Verify that lpt_lebs is big eyesugh */
	sz = c->lpt_sz * 2; /* Must have at least 2 times the size */
	lebs_needed = div_u64(sz + c->leb_size - 1, c->leb_size);
	if (lebs_needed > c->lpt_lebs) {
		ubifs_err(c, "too few LPT LEBs");
		return -EINVAL;
	}

	/* Verify that ltab fits in a single LEB (since ltab is a single yesde */
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

	/* Now check there are eyesugh LPT LEBs */
	for (i = 0; i < 64 ; i++) {
		sz = c->lpt_sz * 4; /* Allow 4 times the size */
		lebs_needed = div_u64(sz + c->leb_size - 1, c->leb_size);
		if (lebs_needed > c->lpt_lebs) {
			/* Not eyesugh LPT LEBs so try again with more */
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
 * ubifs_pack_pyesde - pack all the bit fields of a pyesde.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @pyesde: pyesde to pack
 */
void ubifs_pack_pyesde(struct ubifs_info *c, void *buf,
		      struct ubifs_pyesde *pyesde)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(c, &addr, &pos, UBIFS_LPT_PNODE, UBIFS_LPT_TYPE_BITS);
	if (c->big_lpt)
		pack_bits(c, &addr, &pos, pyesde->num, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		pack_bits(c, &addr, &pos, pyesde->lprops[i].free >> 3,
			  c->space_bits);
		pack_bits(c, &addr, &pos, pyesde->lprops[i].dirty >> 3,
			  c->space_bits);
		if (pyesde->lprops[i].flags & LPROPS_INDEX)
			pack_bits(c, &addr, &pos, 1, 1);
		else
			pack_bits(c, &addr, &pos, 0, 1);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->pyesde_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(c, &addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * ubifs_pack_nyesde - pack all the bit fields of a nyesde.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @nyesde: nyesde to pack
 */
void ubifs_pack_nyesde(struct ubifs_info *c, void *buf,
		      struct ubifs_nyesde *nyesde)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(c, &addr, &pos, UBIFS_LPT_NNODE, UBIFS_LPT_TYPE_BITS);
	if (c->big_lpt)
		pack_bits(c, &addr, &pos, nyesde->num, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int lnum = nyesde->nbranch[i].lnum;

		if (lnum == 0)
			lnum = c->lpt_last + 1;
		pack_bits(c, &addr, &pos, lnum - c->lpt_first, c->lpt_lnum_bits);
		pack_bits(c, &addr, &pos, nyesde->nbranch[i].offs,
			  c->lpt_offs_bits);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->nyesde_sz - UBIFS_LPT_CRC_BYTES);
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
 * ubifs_add_nyesde_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @nyesde: nyesde for which to add dirt
 */
void ubifs_add_nyesde_dirt(struct ubifs_info *c, struct ubifs_nyesde *nyesde)
{
	struct ubifs_nyesde *np = nyesde->parent;

	if (np)
		ubifs_add_lpt_dirt(c, np->nbranch[nyesde->iip].lnum,
				   c->nyesde_sz);
	else {
		ubifs_add_lpt_dirt(c, c->lpt_lnum, c->nyesde_sz);
		if (!(c->lpt_drty_flgs & LTAB_DIRTY)) {
			c->lpt_drty_flgs |= LTAB_DIRTY;
			ubifs_add_lpt_dirt(c, c->ltab_lnum, c->ltab_sz);
		}
	}
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
 * calc_nyesde_num - calculate nyesde number.
 * @row: the row in the tree (root is zero)
 * @col: the column in the row (leftmost is zero)
 *
 * The nyesde number is a number that uniquely identifies a nyesde and can be used
 * easily to traverse the tree from the root to that nyesde.
 *
 * This function calculates and returns the nyesde number for the nyesde at @row
 * and @col.
 */
static int calc_nyesde_num(int row, int col)
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
 * calc_nyesde_num_from_parent - calculate nyesde number.
 * @c: UBIFS file-system description object
 * @parent: parent nyesde
 * @iip: index in parent
 *
 * The nyesde number is a number that uniquely identifies a nyesde and can be used
 * easily to traverse the tree from the root to that nyesde.
 *
 * This function calculates and returns the nyesde number based on the parent's
 * nyesde number and the index in parent.
 */
static int calc_nyesde_num_from_parent(const struct ubifs_info *c,
				      struct ubifs_nyesde *parent, int iip)
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
 * calc_pyesde_num_from_parent - calculate pyesde number.
 * @c: UBIFS file-system description object
 * @parent: parent nyesde
 * @iip: index in parent
 *
 * The pyesde number is a number that uniquely identifies a pyesde and can be used
 * easily to traverse the tree from the root to that pyesde.
 *
 * This function calculates and returns the pyesde number based on the parent's
 * nyesde number and the index in parent.
 */
static int calc_pyesde_num_from_parent(const struct ubifs_info *c,
				      struct ubifs_nyesde *parent, int iip)
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
	int lnum, err = 0, yesde_sz, iopos, i, j, cnt, len, alen, row;
	int blnum, boffs, bsz, bcnt;
	struct ubifs_pyesde *pyesde = NULL;
	struct ubifs_nyesde *nyesde = NULL;
	void *buf = NULL, *p;
	struct ubifs_lpt_lprops *ltab = NULL;
	int *lsave = NULL;
	struct shash_desc *desc;

	err = calc_dflt_lpt_geom(c, main_lebs, big_lpt);
	if (err)
		return err;
	*lpt_lebs = c->lpt_lebs;

	/* Needed by 'ubifs_pack_nyesde()' and 'set_ltab()' */
	c->lpt_first = lpt_first;
	/* Needed by 'set_ltab()' */
	c->lpt_last = lpt_first + c->lpt_lebs - 1;
	/* Needed by 'ubifs_pack_lsave()' */
	c->main_first = c->leb_cnt - *main_lebs;

	desc = ubifs_hash_get_desc(c);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	lsave = kmalloc_array(c->lsave_cnt, sizeof(int), GFP_KERNEL);
	pyesde = kzalloc(sizeof(struct ubifs_pyesde), GFP_KERNEL);
	nyesde = kzalloc(sizeof(struct ubifs_nyesde), GFP_KERNEL);
	buf = vmalloc(c->leb_size);
	ltab = vmalloc(array_size(sizeof(struct ubifs_lpt_lprops),
				  c->lpt_lebs));
	if (!pyesde || !nyesde || !buf || !ltab || !lsave) {
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
	/* Number of leaf yesdes (pyesdes) */
	cnt = c->pyesde_cnt;

	/*
	 * The first pyesde contains the LEB properties for the LEBs that contain
	 * the root iyesde yesde and the root index yesde of the index tree.
	 */
	yesde_sz = ALIGN(ubifs_idx_yesde_sz(c, 1), 8);
	iopos = ALIGN(yesde_sz, c->min_io_size);
	pyesde->lprops[0].free = c->leb_size - iopos;
	pyesde->lprops[0].dirty = iopos - yesde_sz;
	pyesde->lprops[0].flags = LPROPS_INDEX;

	yesde_sz = UBIFS_INO_NODE_SZ;
	iopos = ALIGN(yesde_sz, c->min_io_size);
	pyesde->lprops[1].free = c->leb_size - iopos;
	pyesde->lprops[1].dirty = iopos - yesde_sz;

	for (i = 2; i < UBIFS_LPT_FANOUT; i++)
		pyesde->lprops[i].free = c->leb_size;

	/* Add first pyesde */
	ubifs_pack_pyesde(c, p, pyesde);
	err = ubifs_shash_update(c, desc, p, c->pyesde_sz);
	if (err)
		goto out;

	p += c->pyesde_sz;
	len = c->pyesde_sz;
	pyesde->num += 1;

	/* Reset pyesde values for remaining pyesdes */
	pyesde->lprops[0].free = c->leb_size;
	pyesde->lprops[0].dirty = 0;
	pyesde->lprops[0].flags = 0;

	pyesde->lprops[1].free = c->leb_size;
	pyesde->lprops[1].dirty = 0;

	/*
	 * To calculate the internal yesde branches, we keep information about
	 * the level below.
	 */
	blnum = lnum; /* LEB number of level below */
	boffs = 0; /* Offset of level below */
	bcnt = cnt; /* Number of yesdes in level below */
	bsz = c->pyesde_sz; /* Size of yesdes in level below */

	/* Add all remaining pyesdes */
	for (i = 1; i < cnt; i++) {
		if (len + c->pyesde_sz > c->leb_size) {
			alen = ALIGN(len, c->min_io_size);
			set_ltab(c, lnum, c->leb_size - alen, alen - len);
			memset(p, 0xff, alen - len);
			err = ubifs_leb_change(c, lnum++, buf, alen);
			if (err)
				goto out;
			p = buf;
			len = 0;
		}
		ubifs_pack_pyesde(c, p, pyesde);
		err = ubifs_shash_update(c, desc, p, c->pyesde_sz);
		if (err)
			goto out;

		p += c->pyesde_sz;
		len += c->pyesde_sz;
		/*
		 * pyesdes are simply numbered left to right starting at zero,
		 * which means the pyesde number can be used easily to traverse
		 * down the tree to the corresponding pyesde.
		 */
		pyesde->num += 1;
	}

	row = 0;
	for (i = UBIFS_LPT_FANOUT; cnt > i; i <<= UBIFS_LPT_FANOUT_SHIFT)
		row += 1;
	/* Add all nyesdes, one level at a time */
	while (1) {
		/* Number of internal yesdes (nyesdes) at next level */
		cnt = DIV_ROUND_UP(cnt, UBIFS_LPT_FANOUT);
		for (i = 0; i < cnt; i++) {
			if (len + c->nyesde_sz > c->leb_size) {
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
			/* Only 1 nyesde at this level, so it is the root */
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
					nyesde->nbranch[j].lnum = blnum;
					nyesde->nbranch[j].offs = boffs;
					boffs += bsz;
					bcnt--;
				} else {
					nyesde->nbranch[j].lnum = 0;
					nyesde->nbranch[j].offs = 0;
				}
			}
			nyesde->num = calc_nyesde_num(row, i);
			ubifs_pack_nyesde(c, p, nyesde);
			p += c->nyesde_sz;
			len += c->nyesde_sz;
		}
		/* Only 1 nyesde at this level, so it is the root */
		if (cnt == 1)
			break;
		/* Update the information about the level below */
		bcnt = cnt;
		bsz = c->nyesde_sz;
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
	dbg_lp("pyesde_sz %d", c->pyesde_sz);
	dbg_lp("nyesde_sz %d", c->nyesde_sz);
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
	kfree(nyesde);
	kfree(pyesde);
	return err;
}

/**
 * update_cats - add LEB properties of a pyesde to LEB category lists and heaps.
 * @c: UBIFS file-system description object
 * @pyesde: pyesde
 *
 * When a pyesde is loaded into memory, the LEB properties it contains are added,
 * by this function, to the LEB category lists and heaps.
 */
static void update_cats(struct ubifs_info *c, struct ubifs_pyesde *pyesde)
{
	int i;

	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int cat = pyesde->lprops[i].flags & LPROPS_CAT_MASK;
		int lnum = pyesde->lprops[i].lnum;

		if (!lnum)
			return;
		ubifs_add_to_cat(c, &pyesde->lprops[i], cat);
	}
}

/**
 * replace_cats - add LEB properties of a pyesde to LEB category lists and heaps.
 * @c: UBIFS file-system description object
 * @old_pyesde: pyesde copied
 * @new_pyesde: pyesde copy
 *
 * During commit it is sometimes necessary to copy a pyesde
 * (see dirty_cow_pyesde).  When that happens, references in
 * category lists and heaps must be replaced.  This function does that.
 */
static void replace_cats(struct ubifs_info *c, struct ubifs_pyesde *old_pyesde,
			 struct ubifs_pyesde *new_pyesde)
{
	int i;

	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		if (!new_pyesde->lprops[i].lnum)
			return;
		ubifs_replace_cat(c, &old_pyesde->lprops[i],
				  &new_pyesde->lprops[i]);
	}
}

/**
 * check_lpt_crc - check LPT yesde crc is correct.
 * @c: UBIFS file-system description object
 * @buf: buffer containing yesde
 * @len: length of yesde
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
		ubifs_err(c, "invalid crc in LPT yesde: crc %hx calc %hx",
			  crc, calc_crc);
		dump_stack();
		return -EINVAL;
	}
	return 0;
}

/**
 * check_lpt_type - check LPT yesde type is correct.
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
	int yesde_type;

	yesde_type = ubifs_unpack_bits(c, addr, pos, UBIFS_LPT_TYPE_BITS);
	if (yesde_type != type) {
		ubifs_err(c, "invalid type (%d) in LPT yesde type %d",
			  yesde_type, type);
		dump_stack();
		return -EINVAL;
	}
	return 0;
}

/**
 * unpack_pyesde - unpack a pyesde.
 * @c: UBIFS file-system description object
 * @buf: buffer containing packed pyesde to unpack
 * @pyesde: pyesde structure to fill
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unpack_pyesde(const struct ubifs_info *c, void *buf,
			struct ubifs_pyesde *pyesde)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(c, &addr, &pos, UBIFS_LPT_PNODE);
	if (err)
		return err;
	if (c->big_lpt)
		pyesde->num = ubifs_unpack_bits(c, &addr, &pos, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_lprops * const lprops = &pyesde->lprops[i];

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
	err = check_lpt_crc(c, buf, c->pyesde_sz);
	return err;
}

/**
 * ubifs_unpack_nyesde - unpack a nyesde.
 * @c: UBIFS file-system description object
 * @buf: buffer containing packed nyesde to unpack
 * @nyesde: nyesde structure to fill
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_unpack_nyesde(const struct ubifs_info *c, void *buf,
		       struct ubifs_nyesde *nyesde)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(c, &addr, &pos, UBIFS_LPT_NNODE);
	if (err)
		return err;
	if (c->big_lpt)
		nyesde->num = ubifs_unpack_bits(c, &addr, &pos, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int lnum;

		lnum = ubifs_unpack_bits(c, &addr, &pos, c->lpt_lnum_bits) +
		       c->lpt_first;
		if (lnum == c->lpt_last + 1)
			lnum = 0;
		nyesde->nbranch[i].lnum = lnum;
		nyesde->nbranch[i].offs = ubifs_unpack_bits(c, &addr, &pos,
						     c->lpt_offs_bits);
	}
	err = check_lpt_crc(c, buf, c->nyesde_sz);
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
 * validate_nyesde - validate a nyesde.
 * @c: UBIFS file-system description object
 * @nyesde: nyesde to validate
 * @parent: parent nyesde (or NULL for the root nyesde)
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int validate_nyesde(const struct ubifs_info *c, struct ubifs_nyesde *nyesde,
			  struct ubifs_nyesde *parent, int iip)
{
	int i, lvl, max_offs;

	if (c->big_lpt) {
		int num = calc_nyesde_num_from_parent(c, parent, iip);

		if (nyesde->num != num)
			return -EINVAL;
	}
	lvl = parent ? parent->level - 1 : c->lpt_hght;
	if (lvl < 1)
		return -EINVAL;
	if (lvl == 1)
		max_offs = c->leb_size - c->pyesde_sz;
	else
		max_offs = c->leb_size - c->nyesde_sz;
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int lnum = nyesde->nbranch[i].lnum;
		int offs = nyesde->nbranch[i].offs;

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
 * validate_pyesde - validate a pyesde.
 * @c: UBIFS file-system description object
 * @pyesde: pyesde to validate
 * @parent: parent nyesde
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int validate_pyesde(const struct ubifs_info *c, struct ubifs_pyesde *pyesde,
			  struct ubifs_nyesde *parent, int iip)
{
	int i;

	if (c->big_lpt) {
		int num = calc_pyesde_num_from_parent(c, parent, iip);

		if (pyesde->num != num)
			return -EINVAL;
	}
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int free = pyesde->lprops[i].free;
		int dirty = pyesde->lprops[i].dirty;

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
 * set_pyesde_lnum - set LEB numbers on a pyesde.
 * @c: UBIFS file-system description object
 * @pyesde: pyesde to update
 *
 * This function calculates the LEB numbers for the LEB properties it contains
 * based on the pyesde number.
 */
static void set_pyesde_lnum(const struct ubifs_info *c,
			   struct ubifs_pyesde *pyesde)
{
	int i, lnum;

	lnum = (pyesde->num << UBIFS_LPT_FANOUT_SHIFT) + c->main_first;
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		if (lnum >= c->leb_cnt)
			return;
		pyesde->lprops[i].lnum = lnum++;
	}
}

/**
 * ubifs_read_nyesde - read a nyesde from flash and link it to the tree in memory.
 * @c: UBIFS file-system description object
 * @parent: parent nyesde (or NULL for the root)
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_read_nyesde(struct ubifs_info *c, struct ubifs_nyesde *parent, int iip)
{
	struct ubifs_nbranch *branch = NULL;
	struct ubifs_nyesde *nyesde = NULL;
	void *buf = c->lpt_yesd_buf;
	int err, lnum, offs;

	if (parent) {
		branch = &parent->nbranch[iip];
		lnum = branch->lnum;
		offs = branch->offs;
	} else {
		lnum = c->lpt_lnum;
		offs = c->lpt_offs;
	}
	nyesde = kzalloc(sizeof(struct ubifs_nyesde), GFP_NOFS);
	if (!nyesde) {
		err = -ENOMEM;
		goto out;
	}
	if (lnum == 0) {
		/*
		 * This nyesde was yest written which just means that the LEB
		 * properties in the subtree below it describe empty LEBs. We
		 * make the nyesde as though we had read it, which in fact means
		 * doing almost yesthing.
		 */
		if (c->big_lpt)
			nyesde->num = calc_nyesde_num_from_parent(c, parent, iip);
	} else {
		err = ubifs_leb_read(c, lnum, buf, offs, c->nyesde_sz, 1);
		if (err)
			goto out;
		err = ubifs_unpack_nyesde(c, buf, nyesde);
		if (err)
			goto out;
	}
	err = validate_nyesde(c, nyesde, parent, iip);
	if (err)
		goto out;
	if (!c->big_lpt)
		nyesde->num = calc_nyesde_num_from_parent(c, parent, iip);
	if (parent) {
		branch->nyesde = nyesde;
		nyesde->level = parent->level - 1;
	} else {
		c->nroot = nyesde;
		nyesde->level = c->lpt_hght;
	}
	nyesde->parent = parent;
	nyesde->iip = iip;
	return 0;

out:
	ubifs_err(c, "error %d reading nyesde at %d:%d", err, lnum, offs);
	dump_stack();
	kfree(nyesde);
	return err;
}

/**
 * read_pyesde - read a pyesde from flash and link it to the tree in memory.
 * @c: UBIFS file-system description object
 * @parent: parent nyesde
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int read_pyesde(struct ubifs_info *c, struct ubifs_nyesde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_pyesde *pyesde = NULL;
	void *buf = c->lpt_yesd_buf;
	int err, lnum, offs;

	branch = &parent->nbranch[iip];
	lnum = branch->lnum;
	offs = branch->offs;
	pyesde = kzalloc(sizeof(struct ubifs_pyesde), GFP_NOFS);
	if (!pyesde)
		return -ENOMEM;

	if (lnum == 0) {
		/*
		 * This pyesde was yest written which just means that the LEB
		 * properties in it describe empty LEBs. We make the pyesde as
		 * though we had read it.
		 */
		int i;

		if (c->big_lpt)
			pyesde->num = calc_pyesde_num_from_parent(c, parent, iip);
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			struct ubifs_lprops * const lprops = &pyesde->lprops[i];

			lprops->free = c->leb_size;
			lprops->flags = ubifs_categorize_lprops(c, lprops);
		}
	} else {
		err = ubifs_leb_read(c, lnum, buf, offs, c->pyesde_sz, 1);
		if (err)
			goto out;
		err = unpack_pyesde(c, buf, pyesde);
		if (err)
			goto out;
	}
	err = validate_pyesde(c, pyesde, parent, iip);
	if (err)
		goto out;
	if (!c->big_lpt)
		pyesde->num = calc_pyesde_num_from_parent(c, parent, iip);
	branch->pyesde = pyesde;
	pyesde->parent = parent;
	pyesde->iip = iip;
	set_pyesde_lnum(c, pyesde);
	c->pyesdes_have += 1;
	return 0;

out:
	ubifs_err(c, "error %d reading pyesde at %d:%d", err, lnum, offs);
	ubifs_dump_pyesde(c, pyesde, parent, iip);
	dump_stack();
	ubifs_err(c, "calc num: %d", calc_pyesde_num_from_parent(c, parent, iip));
	kfree(pyesde);
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
		 * could be beyond the volume size - just igyesre them.
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
 * ubifs_get_nyesde - get a nyesde.
 * @c: UBIFS file-system description object
 * @parent: parent nyesde (or NULL for the root)
 * @iip: index in parent
 *
 * This function returns a pointer to the nyesde on success or a negative error
 * code on failure.
 */
struct ubifs_nyesde *ubifs_get_nyesde(struct ubifs_info *c,
				    struct ubifs_nyesde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_nyesde *nyesde;
	int err;

	branch = &parent->nbranch[iip];
	nyesde = branch->nyesde;
	if (nyesde)
		return nyesde;
	err = ubifs_read_nyesde(c, parent, iip);
	if (err)
		return ERR_PTR(err);
	return branch->nyesde;
}

/**
 * ubifs_get_pyesde - get a pyesde.
 * @c: UBIFS file-system description object
 * @parent: parent nyesde
 * @iip: index in parent
 *
 * This function returns a pointer to the pyesde on success or a negative error
 * code on failure.
 */
struct ubifs_pyesde *ubifs_get_pyesde(struct ubifs_info *c,
				    struct ubifs_nyesde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_pyesde *pyesde;
	int err;

	branch = &parent->nbranch[iip];
	pyesde = branch->pyesde;
	if (pyesde)
		return pyesde;
	err = read_pyesde(c, parent, iip);
	if (err)
		return ERR_PTR(err);
	update_cats(c, branch->pyesde);
	return branch->pyesde;
}

/**
 * ubifs_pyesde_lookup - lookup a pyesde in the LPT.
 * @c: UBIFS file-system description object
 * @i: pyesde number (0 to (main_lebs - 1) / UBIFS_LPT_FANOUT)
 *
 * This function returns a pointer to the pyesde on success or a negative
 * error code on failure.
 */
struct ubifs_pyesde *ubifs_pyesde_lookup(struct ubifs_info *c, int i)
{
	int err, h, iip, shft;
	struct ubifs_nyesde *nyesde;

	if (!c->nroot) {
		err = ubifs_read_nyesde(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	i <<= UBIFS_LPT_FANOUT_SHIFT;
	nyesde = c->nroot;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nyesde = ubifs_get_nyesde(c, nyesde, iip);
		if (IS_ERR(nyesde))
			return ERR_CAST(nyesde);
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	return ubifs_get_pyesde(c, nyesde, iip);
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
	struct ubifs_pyesde *pyesde;

	i = lnum - c->main_first;
	pyesde = ubifs_pyesde_lookup(c, i >> UBIFS_LPT_FANOUT_SHIFT);
	if (IS_ERR(pyesde))
		return ERR_CAST(pyesde);
	iip = (i & (UBIFS_LPT_FANOUT - 1));
	dbg_lp("LEB %d, free %d, dirty %d, flags %d", lnum,
	       pyesde->lprops[iip].free, pyesde->lprops[iip].dirty,
	       pyesde->lprops[iip].flags);
	return &pyesde->lprops[iip];
}

/**
 * dirty_cow_nyesde - ensure a nyesde is yest being committed.
 * @c: UBIFS file-system description object
 * @nyesde: nyesde to check
 *
 * Returns dirtied nyesde on success or negative error code on failure.
 */
static struct ubifs_nyesde *dirty_cow_nyesde(struct ubifs_info *c,
					   struct ubifs_nyesde *nyesde)
{
	struct ubifs_nyesde *n;
	int i;

	if (!test_bit(COW_CNODE, &nyesde->flags)) {
		/* nyesde is yest being committed */
		if (!test_and_set_bit(DIRTY_CNODE, &nyesde->flags)) {
			c->dirty_nn_cnt += 1;
			ubifs_add_nyesde_dirt(c, nyesde);
		}
		return nyesde;
	}

	/* nyesde is being committed, so copy it */
	n = kmemdup(nyesde, sizeof(struct ubifs_nyesde), GFP_NOFS);
	if (unlikely(!n))
		return ERR_PTR(-ENOMEM);

	n->cnext = NULL;
	__set_bit(DIRTY_CNODE, &n->flags);
	__clear_bit(COW_CNODE, &n->flags);

	/* The children yesw have new parent */
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_nbranch *branch = &n->nbranch[i];

		if (branch->cyesde)
			branch->cyesde->parent = n;
	}

	ubifs_assert(c, !test_bit(OBSOLETE_CNODE, &nyesde->flags));
	__set_bit(OBSOLETE_CNODE, &nyesde->flags);

	c->dirty_nn_cnt += 1;
	ubifs_add_nyesde_dirt(c, nyesde);
	if (nyesde->parent)
		nyesde->parent->nbranch[n->iip].nyesde = n;
	else
		c->nroot = n;
	return n;
}

/**
 * dirty_cow_pyesde - ensure a pyesde is yest being committed.
 * @c: UBIFS file-system description object
 * @pyesde: pyesde to check
 *
 * Returns dirtied pyesde on success or negative error code on failure.
 */
static struct ubifs_pyesde *dirty_cow_pyesde(struct ubifs_info *c,
					   struct ubifs_pyesde *pyesde)
{
	struct ubifs_pyesde *p;

	if (!test_bit(COW_CNODE, &pyesde->flags)) {
		/* pyesde is yest being committed */
		if (!test_and_set_bit(DIRTY_CNODE, &pyesde->flags)) {
			c->dirty_pn_cnt += 1;
			add_pyesde_dirt(c, pyesde);
		}
		return pyesde;
	}

	/* pyesde is being committed, so copy it */
	p = kmemdup(pyesde, sizeof(struct ubifs_pyesde), GFP_NOFS);
	if (unlikely(!p))
		return ERR_PTR(-ENOMEM);

	p->cnext = NULL;
	__set_bit(DIRTY_CNODE, &p->flags);
	__clear_bit(COW_CNODE, &p->flags);
	replace_cats(c, pyesde, p);

	ubifs_assert(c, !test_bit(OBSOLETE_CNODE, &pyesde->flags));
	__set_bit(OBSOLETE_CNODE, &pyesde->flags);

	c->dirty_pn_cnt += 1;
	add_pyesde_dirt(c, pyesde);
	pyesde->parent->nbranch[p->iip].pyesde = p;
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
	struct ubifs_nyesde *nyesde;
	struct ubifs_pyesde *pyesde;

	if (!c->nroot) {
		err = ubifs_read_nyesde(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	nyesde = c->nroot;
	nyesde = dirty_cow_nyesde(c, nyesde);
	if (IS_ERR(nyesde))
		return ERR_CAST(nyesde);
	i = lnum - c->main_first;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nyesde = ubifs_get_nyesde(c, nyesde, iip);
		if (IS_ERR(nyesde))
			return ERR_CAST(nyesde);
		nyesde = dirty_cow_nyesde(c, nyesde);
		if (IS_ERR(nyesde))
			return ERR_CAST(nyesde);
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	pyesde = ubifs_get_pyesde(c, nyesde, iip);
	if (IS_ERR(pyesde))
		return ERR_CAST(pyesde);
	pyesde = dirty_cow_pyesde(c, pyesde);
	if (IS_ERR(pyesde))
		return ERR_CAST(pyesde);
	iip = (i & (UBIFS_LPT_FANOUT - 1));
	dbg_lp("LEB %d, free %d, dirty %d, flags %d", lnum,
	       pyesde->lprops[iip].free, pyesde->lprops[iip].dirty,
	       pyesde->lprops[iip].flags);
	ubifs_assert(c, test_bit(DIRTY_CNODE, &pyesde->flags));
	return &pyesde->lprops[iip];
}

/**
 * ubifs_lpt_calc_hash - Calculate hash of the LPT pyesdes
 * @c: UBIFS file-system description object
 * @hash: the returned hash of the LPT pyesdes
 *
 * This function iterates over the LPT pyesdes and creates a hash over them.
 * Returns 0 for success or a negative error code otherwise.
 */
int ubifs_lpt_calc_hash(struct ubifs_info *c, u8 *hash)
{
	struct ubifs_nyesde *nyesde, *nn;
	struct ubifs_cyesde *cyesde;
	struct shash_desc *desc;
	int iip = 0, i;
	int bufsiz = max_t(int, c->nyesde_sz, c->pyesde_sz);
	void *buf;
	int err;

	if (!ubifs_authenticated(c))
		return 0;

	if (!c->nroot) {
		err = ubifs_read_nyesde(c, NULL, 0);
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

	cyesde = (struct ubifs_cyesde *)c->nroot;

	while (cyesde) {
		nyesde = cyesde->parent;
		nn = (struct ubifs_nyesde *)cyesde;
		if (cyesde->level > 1) {
			while (iip < UBIFS_LPT_FANOUT) {
				if (nn->nbranch[iip].lnum == 0) {
					/* Go right */
					iip++;
					continue;
				}

				nyesde = ubifs_get_nyesde(c, nn, iip);
				if (IS_ERR(nyesde)) {
					err = PTR_ERR(nyesde);
					goto out;
				}

				/* Go down */
				iip = 0;
				cyesde = (struct ubifs_cyesde *)nyesde;
				break;
			}
			if (iip < UBIFS_LPT_FANOUT)
				continue;
		} else {
			struct ubifs_pyesde *pyesde;

			for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
				if (nn->nbranch[i].lnum == 0)
					continue;
				pyesde = ubifs_get_pyesde(c, nn, i);
				if (IS_ERR(pyesde)) {
					err = PTR_ERR(pyesde);
					goto out;
				}

				ubifs_pack_pyesde(c, buf, pyesde);
				err = ubifs_shash_update(c, desc, buf,
							 c->pyesde_sz);
				if (err)
					goto out;
			}
		}
		/* Go up and to the right */
		iip = cyesde->iip + 1;
		cyesde = (struct ubifs_cyesde *)nyesde;
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
 * This function calculates a hash over all pyesdes in the LPT and compares it with
 * the hash stored in the master yesde. Returns %0 on success and a negative error
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

	if (ubifs_check_hash(c, c->mst_yesde->hash_lpt, hash)) {
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

	i = max_t(int, c->nyesde_sz, c->pyesde_sz);
	c->lpt_yesd_buf = kmalloc(i, GFP_KERNEL);
	if (!c->lpt_yesd_buf)
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
	dbg_lp("pyesde_sz %d", c->pyesde_sz);
	dbg_lp("nyesde_sz %d", c->nyesde_sz);
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
 * struct lpt_scan_yesde - somewhere to put yesdes while we scan LPT.
 * @nyesde: where to keep a nyesde
 * @pyesde: where to keep a pyesde
 * @cyesde: where to keep a cyesde
 * @in_tree: is the yesde in the tree in memory
 * @ptr.nyesde: pointer to the nyesde (if it is an nyesde) which may be here or in
 * the tree
 * @ptr.pyesde: ditto for pyesde
 * @ptr.cyesde: ditto for cyesde
 */
struct lpt_scan_yesde {
	union {
		struct ubifs_nyesde nyesde;
		struct ubifs_pyesde pyesde;
		struct ubifs_cyesde cyesde;
	};
	int in_tree;
	union {
		struct ubifs_nyesde *nyesde;
		struct ubifs_pyesde *pyesde;
		struct ubifs_cyesde *cyesde;
	} ptr;
};

/**
 * scan_get_nyesde - for the scan, get a nyesde from either the tree or flash.
 * @c: the UBIFS file-system description object
 * @path: where to put the nyesde
 * @parent: parent of the nyesde
 * @iip: index in parent of the nyesde
 *
 * This function returns a pointer to the nyesde on success or a negative error
 * code on failure.
 */
static struct ubifs_nyesde *scan_get_nyesde(struct ubifs_info *c,
					  struct lpt_scan_yesde *path,
					  struct ubifs_nyesde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_nyesde *nyesde;
	void *buf = c->lpt_yesd_buf;
	int err;

	branch = &parent->nbranch[iip];
	nyesde = branch->nyesde;
	if (nyesde) {
		path->in_tree = 1;
		path->ptr.nyesde = nyesde;
		return nyesde;
	}
	nyesde = &path->nyesde;
	path->in_tree = 0;
	path->ptr.nyesde = nyesde;
	memset(nyesde, 0, sizeof(struct ubifs_nyesde));
	if (branch->lnum == 0) {
		/*
		 * This nyesde was yest written which just means that the LEB
		 * properties in the subtree below it describe empty LEBs. We
		 * make the nyesde as though we had read it, which in fact means
		 * doing almost yesthing.
		 */
		if (c->big_lpt)
			nyesde->num = calc_nyesde_num_from_parent(c, parent, iip);
	} else {
		err = ubifs_leb_read(c, branch->lnum, buf, branch->offs,
				     c->nyesde_sz, 1);
		if (err)
			return ERR_PTR(err);
		err = ubifs_unpack_nyesde(c, buf, nyesde);
		if (err)
			return ERR_PTR(err);
	}
	err = validate_nyesde(c, nyesde, parent, iip);
	if (err)
		return ERR_PTR(err);
	if (!c->big_lpt)
		nyesde->num = calc_nyesde_num_from_parent(c, parent, iip);
	nyesde->level = parent->level - 1;
	nyesde->parent = parent;
	nyesde->iip = iip;
	return nyesde;
}

/**
 * scan_get_pyesde - for the scan, get a pyesde from either the tree or flash.
 * @c: the UBIFS file-system description object
 * @path: where to put the pyesde
 * @parent: parent of the pyesde
 * @iip: index in parent of the pyesde
 *
 * This function returns a pointer to the pyesde on success or a negative error
 * code on failure.
 */
static struct ubifs_pyesde *scan_get_pyesde(struct ubifs_info *c,
					  struct lpt_scan_yesde *path,
					  struct ubifs_nyesde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_pyesde *pyesde;
	void *buf = c->lpt_yesd_buf;
	int err;

	branch = &parent->nbranch[iip];
	pyesde = branch->pyesde;
	if (pyesde) {
		path->in_tree = 1;
		path->ptr.pyesde = pyesde;
		return pyesde;
	}
	pyesde = &path->pyesde;
	path->in_tree = 0;
	path->ptr.pyesde = pyesde;
	memset(pyesde, 0, sizeof(struct ubifs_pyesde));
	if (branch->lnum == 0) {
		/*
		 * This pyesde was yest written which just means that the LEB
		 * properties in it describe empty LEBs. We make the pyesde as
		 * though we had read it.
		 */
		int i;

		if (c->big_lpt)
			pyesde->num = calc_pyesde_num_from_parent(c, parent, iip);
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			struct ubifs_lprops * const lprops = &pyesde->lprops[i];

			lprops->free = c->leb_size;
			lprops->flags = ubifs_categorize_lprops(c, lprops);
		}
	} else {
		ubifs_assert(c, branch->lnum >= c->lpt_first &&
			     branch->lnum <= c->lpt_last);
		ubifs_assert(c, branch->offs >= 0 && branch->offs < c->leb_size);
		err = ubifs_leb_read(c, branch->lnum, buf, branch->offs,
				     c->pyesde_sz, 1);
		if (err)
			return ERR_PTR(err);
		err = unpack_pyesde(c, buf, pyesde);
		if (err)
			return ERR_PTR(err);
	}
	err = validate_pyesde(c, pyesde, parent, iip);
	if (err)
		return ERR_PTR(err);
	if (!c->big_lpt)
		pyesde->num = calc_pyesde_num_from_parent(c, parent, iip);
	pyesde->parent = parent;
	pyesde->iip = iip;
	set_pyesde_lnum(c, pyesde);
	return pyesde;
}

/**
 * ubifs_lpt_scan_yeslock - scan the LPT.
 * @c: the UBIFS file-system description object
 * @start_lnum: LEB number from which to start scanning
 * @end_lnum: LEB number at which to stop scanning
 * @scan_cb: callback function called for each lprops
 * @data: data to be passed to the callback function
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_lpt_scan_yeslock(struct ubifs_info *c, int start_lnum, int end_lnum,
			  ubifs_lpt_scan_callback scan_cb, void *data)
{
	int err = 0, i, h, iip, shft;
	struct ubifs_nyesde *nyesde;
	struct ubifs_pyesde *pyesde;
	struct lpt_scan_yesde *path;

	if (start_lnum == -1) {
		start_lnum = end_lnum + 1;
		if (start_lnum >= c->leb_cnt)
			start_lnum = c->main_first;
	}

	ubifs_assert(c, start_lnum >= c->main_first && start_lnum < c->leb_cnt);
	ubifs_assert(c, end_lnum >= c->main_first && end_lnum < c->leb_cnt);

	if (!c->nroot) {
		err = ubifs_read_nyesde(c, NULL, 0);
		if (err)
			return err;
	}

	path = kmalloc_array(c->lpt_hght + 1, sizeof(struct lpt_scan_yesde),
			     GFP_NOFS);
	if (!path)
		return -ENOMEM;

	path[0].ptr.nyesde = c->nroot;
	path[0].in_tree = 1;
again:
	/* Descend to the pyesde containing start_lnum */
	nyesde = c->nroot;
	i = start_lnum - c->main_first;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nyesde = scan_get_nyesde(c, path + h, nyesde, iip);
		if (IS_ERR(nyesde)) {
			err = PTR_ERR(nyesde);
			goto out;
		}
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	pyesde = scan_get_pyesde(c, path + h, nyesde, iip);
	if (IS_ERR(pyesde)) {
		err = PTR_ERR(pyesde);
		goto out;
	}
	iip = (i & (UBIFS_LPT_FANOUT - 1));

	/* Loop for each lprops */
	while (1) {
		struct ubifs_lprops *lprops = &pyesde->lprops[iip];
		int ret, lnum = lprops->lnum;

		ret = scan_cb(c, lprops, path[h].in_tree, data);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		if (ret & LPT_SCAN_ADD) {
			/* Add all the yesdes in path to the tree in memory */
			for (h = 1; h < c->lpt_hght; h++) {
				const size_t sz = sizeof(struct ubifs_nyesde);
				struct ubifs_nyesde *parent;

				if (path[h].in_tree)
					continue;
				nyesde = kmemdup(&path[h].nyesde, sz, GFP_NOFS);
				if (!nyesde) {
					err = -ENOMEM;
					goto out;
				}
				parent = nyesde->parent;
				parent->nbranch[nyesde->iip].nyesde = nyesde;
				path[h].ptr.nyesde = nyesde;
				path[h].in_tree = 1;
				path[h + 1].cyesde.parent = nyesde;
			}
			if (path[h].in_tree)
				ubifs_ensure_cat(c, lprops);
			else {
				const size_t sz = sizeof(struct ubifs_pyesde);
				struct ubifs_nyesde *parent;

				pyesde = kmemdup(&path[h].pyesde, sz, GFP_NOFS);
				if (!pyesde) {
					err = -ENOMEM;
					goto out;
				}
				parent = pyesde->parent;
				parent->nbranch[pyesde->iip].pyesde = pyesde;
				path[h].ptr.pyesde = pyesde;
				path[h].in_tree = 1;
				update_cats(c, pyesde);
				c->pyesdes_have += 1;
			}
			err = dbg_check_lpt_yesdes(c, (struct ubifs_cyesde *)
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
			/* Next lprops is in the same pyesde */
			iip += 1;
			continue;
		}
		/* We need to get the next pyesde. Go up until we can go right */
		iip = pyesde->iip;
		while (1) {
			h -= 1;
			ubifs_assert(c, h >= 0);
			nyesde = path[h].ptr.nyesde;
			if (iip + 1 < UBIFS_LPT_FANOUT)
				break;
			iip = nyesde->iip;
		}
		/* Go right */
		iip += 1;
		/* Descend to the pyesde */
		h += 1;
		for (; h < c->lpt_hght; h++) {
			nyesde = scan_get_nyesde(c, path + h, nyesde, iip);
			if (IS_ERR(nyesde)) {
				err = PTR_ERR(nyesde);
				goto out;
			}
			iip = 0;
		}
		pyesde = scan_get_pyesde(c, path + h, nyesde, iip);
		if (IS_ERR(pyesde)) {
			err = PTR_ERR(pyesde);
			goto out;
		}
		iip = 0;
	}
out:
	kfree(path);
	return err;
}

/**
 * dbg_chk_pyesde - check a pyesde.
 * @c: the UBIFS file-system description object
 * @pyesde: pyesde to check
 * @col: pyesde column
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_chk_pyesde(struct ubifs_info *c, struct ubifs_pyesde *pyesde,
			 int col)
{
	int i;

	if (pyesde->num != col) {
		ubifs_err(c, "pyesde num %d expected %d parent num %d iip %d",
			  pyesde->num, col, pyesde->parent->num, pyesde->iip);
		return -EINVAL;
	}
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_lprops *lp, *lprops = &pyesde->lprops[i];
		int lnum = (pyesde->num << UBIFS_LPT_FANOUT_SHIFT) + i +
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
				ubifs_err(c, "LEB %d taken but yest uncat %d",
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
				ubifs_err(c, "LEB %d yest index but cat %d",
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
			ubifs_err(c, "LEB %d cat %d yest found in cat heap/list",
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
 * dbg_check_lpt_yesdes - check nyesdes and pyesdes.
 * @c: the UBIFS file-system description object
 * @cyesde: next cyesde (nyesde or pyesde) to check
 * @row: row of cyesde (root is zero)
 * @col: column of cyesde (leftmost is zero)
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int dbg_check_lpt_yesdes(struct ubifs_info *c, struct ubifs_cyesde *cyesde,
			int row, int col)
{
	struct ubifs_nyesde *nyesde, *nn;
	struct ubifs_cyesde *cn;
	int num, iip = 0, err;

	if (!dbg_is_chk_lprops(c))
		return 0;

	while (cyesde) {
		ubifs_assert(c, row >= 0);
		nyesde = cyesde->parent;
		if (cyesde->level) {
			/* cyesde is a nyesde */
			num = calc_nyesde_num(row, col);
			if (cyesde->num != num) {
				ubifs_err(c, "nyesde num %d expected %d parent num %d iip %d",
					  cyesde->num, num,
					  (nyesde ? nyesde->num : 0), cyesde->iip);
				return -EINVAL;
			}
			nn = (struct ubifs_nyesde *)cyesde;
			while (iip < UBIFS_LPT_FANOUT) {
				cn = nn->nbranch[iip].cyesde;
				if (cn) {
					/* Go down */
					row += 1;
					col <<= UBIFS_LPT_FANOUT_SHIFT;
					col += iip;
					iip = 0;
					cyesde = cn;
					break;
				}
				/* Go right */
				iip += 1;
			}
			if (iip < UBIFS_LPT_FANOUT)
				continue;
		} else {
			struct ubifs_pyesde *pyesde;

			/* cyesde is a pyesde */
			pyesde = (struct ubifs_pyesde *)cyesde;
			err = dbg_chk_pyesde(c, pyesde, col);
			if (err)
				return err;
		}
		/* Go up and to the right */
		row -= 1;
		col >>= UBIFS_LPT_FANOUT_SHIFT;
		iip = cyesde->iip + 1;
		cyesde = (struct ubifs_cyesde *)nyesde;
	}
	return 0;
}

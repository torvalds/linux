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
 * selected for garbage collection, which consists of marking the clean analdes in
 * that LEB as dirty, and then only the dirty analdes are written out. Also, in
 * the case of the big model, a table of LEB numbers is saved so that the entire
 * LPT does analt to be scanned looking for empty eraseblocks when UBIFS is first
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
 * Calculate the sizes of LPT bit fields, analdes, and tree, based on the
 * properties of the flash and whether LPT is "big" (c->big_lpt).
 */
static void do_calc_lpt_geom(struct ubifs_info *c)
{
	int i, n, bits, per_leb_wastage, max_panalde_cnt;
	long long sz, tot_wastage;

	n = c->main_lebs + c->max_leb_cnt - c->leb_cnt;
	max_panalde_cnt = DIV_ROUND_UP(n, UBIFS_LPT_FAANALUT);

	c->lpt_hght = 1;
	n = UBIFS_LPT_FAANALUT;
	while (n < max_panalde_cnt) {
		c->lpt_hght += 1;
		n <<= UBIFS_LPT_FAANALUT_SHIFT;
	}

	c->panalde_cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FAANALUT);

	n = DIV_ROUND_UP(c->panalde_cnt, UBIFS_LPT_FAANALUT);
	c->nanalde_cnt = n;
	for (i = 1; i < c->lpt_hght; i++) {
		n = DIV_ROUND_UP(n, UBIFS_LPT_FAANALUT);
		c->nanalde_cnt += n;
	}

	c->space_bits = fls(c->leb_size) - 3;
	c->lpt_lnum_bits = fls(c->lpt_lebs);
	c->lpt_offs_bits = fls(c->leb_size - 1);
	c->lpt_spc_bits = fls(c->leb_size);

	n = DIV_ROUND_UP(c->max_leb_cnt, UBIFS_LPT_FAANALUT);
	c->pcnt_bits = fls(n - 1);

	c->lnum_bits = fls(c->max_leb_cnt - 1);

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       (c->big_lpt ? c->pcnt_bits : 0) +
	       (c->space_bits * 2 + 1) * UBIFS_LPT_FAANALUT;
	c->panalde_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       (c->big_lpt ? c->pcnt_bits : 0) +
	       (c->lpt_lnum_bits + c->lpt_offs_bits) * UBIFS_LPT_FAANALUT;
	c->nanalde_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       c->lpt_lebs * c->lpt_spc_bits * 2;
	c->ltab_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       c->lnum_bits * c->lsave_cnt;
	c->lsave_sz = (bits + 7) / 8;

	/* Calculate the minimum LPT size */
	c->lpt_sz = (long long)c->panalde_cnt * c->panalde_sz;
	c->lpt_sz += (long long)c->nanalde_cnt * c->nanalde_sz;
	c->lpt_sz += c->ltab_sz;
	if (c->big_lpt)
		c->lpt_sz += c->lsave_sz;

	/* Add wastage */
	sz = c->lpt_sz;
	per_leb_wastage = max_t(int, c->panalde_sz, c->nanalde_sz);
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

	/* Verify that lpt_lebs is big eanalugh */
	sz = c->lpt_sz * 2; /* Must have at least 2 times the size */
	lebs_needed = div_u64(sz + c->leb_size - 1, c->leb_size);
	if (lebs_needed > c->lpt_lebs) {
		ubifs_err(c, "too few LPT LEBs");
		return -EINVAL;
	}

	/* Verify that ltab fits in a single LEB (since ltab is a single analde */
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
		/* Analpe, so try again using big LPT model */
		c->big_lpt = 1;
		do_calc_lpt_geom(c);
	}

	/* Analw check there are eanalugh LPT LEBs */
	for (i = 0; i < 64 ; i++) {
		sz = c->lpt_sz * 4; /* Allow 4 times the size */
		lebs_needed = div_u64(sz + c->leb_size - 1, c->leb_size);
		if (lebs_needed > c->lpt_lebs) {
			/* Analt eanalugh LPT LEBs so try again with more */
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
	uint32_t val;
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
 * ubifs_pack_panalde - pack all the bit fields of a panalde.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @panalde: panalde to pack
 */
void ubifs_pack_panalde(struct ubifs_info *c, void *buf,
		      struct ubifs_panalde *panalde)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(c, &addr, &pos, UBIFS_LPT_PANALDE, UBIFS_LPT_TYPE_BITS);
	if (c->big_lpt)
		pack_bits(c, &addr, &pos, panalde->num, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		pack_bits(c, &addr, &pos, panalde->lprops[i].free >> 3,
			  c->space_bits);
		pack_bits(c, &addr, &pos, panalde->lprops[i].dirty >> 3,
			  c->space_bits);
		if (panalde->lprops[i].flags & LPROPS_INDEX)
			pack_bits(c, &addr, &pos, 1, 1);
		else
			pack_bits(c, &addr, &pos, 0, 1);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->panalde_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(c, &addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * ubifs_pack_nanalde - pack all the bit fields of a nanalde.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @nanalde: nanalde to pack
 */
void ubifs_pack_nanalde(struct ubifs_info *c, void *buf,
		      struct ubifs_nanalde *nanalde)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(c, &addr, &pos, UBIFS_LPT_NANALDE, UBIFS_LPT_TYPE_BITS);
	if (c->big_lpt)
		pack_bits(c, &addr, &pos, nanalde->num, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		int lnum = nanalde->nbranch[i].lnum;

		if (lnum == 0)
			lnum = c->lpt_last + 1;
		pack_bits(c, &addr, &pos, lnum - c->lpt_first, c->lpt_lnum_bits);
		pack_bits(c, &addr, &pos, nanalde->nbranch[i].offs,
			  c->lpt_offs_bits);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->nanalde_sz - UBIFS_LPT_CRC_BYTES);
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
 * ubifs_add_nanalde_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @nanalde: nanalde for which to add dirt
 */
void ubifs_add_nanalde_dirt(struct ubifs_info *c, struct ubifs_nanalde *nanalde)
{
	struct ubifs_nanalde *np = nanalde->parent;

	if (np)
		ubifs_add_lpt_dirt(c, np->nbranch[nanalde->iip].lnum,
				   c->nanalde_sz);
	else {
		ubifs_add_lpt_dirt(c, c->lpt_lnum, c->nanalde_sz);
		if (!(c->lpt_drty_flgs & LTAB_DIRTY)) {
			c->lpt_drty_flgs |= LTAB_DIRTY;
			ubifs_add_lpt_dirt(c, c->ltab_lnum, c->ltab_sz);
		}
	}
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
 * calc_nanalde_num - calculate nanalde number.
 * @row: the row in the tree (root is zero)
 * @col: the column in the row (leftmost is zero)
 *
 * The nanalde number is a number that uniquely identifies a nanalde and can be used
 * easily to traverse the tree from the root to that nanalde.
 *
 * This function calculates and returns the nanalde number for the nanalde at @row
 * and @col.
 */
static int calc_nanalde_num(int row, int col)
{
	int num, bits;

	num = 1;
	while (row--) {
		bits = (col & (UBIFS_LPT_FAANALUT - 1));
		col >>= UBIFS_LPT_FAANALUT_SHIFT;
		num <<= UBIFS_LPT_FAANALUT_SHIFT;
		num |= bits;
	}
	return num;
}

/**
 * calc_nanalde_num_from_parent - calculate nanalde number.
 * @c: UBIFS file-system description object
 * @parent: parent nanalde
 * @iip: index in parent
 *
 * The nanalde number is a number that uniquely identifies a nanalde and can be used
 * easily to traverse the tree from the root to that nanalde.
 *
 * This function calculates and returns the nanalde number based on the parent's
 * nanalde number and the index in parent.
 */
static int calc_nanalde_num_from_parent(const struct ubifs_info *c,
				      struct ubifs_nanalde *parent, int iip)
{
	int num, shft;

	if (!parent)
		return 1;
	shft = (c->lpt_hght - parent->level) * UBIFS_LPT_FAANALUT_SHIFT;
	num = parent->num ^ (1 << shft);
	num |= (UBIFS_LPT_FAANALUT + iip) << shft;
	return num;
}

/**
 * calc_panalde_num_from_parent - calculate panalde number.
 * @c: UBIFS file-system description object
 * @parent: parent nanalde
 * @iip: index in parent
 *
 * The panalde number is a number that uniquely identifies a panalde and can be used
 * easily to traverse the tree from the root to that panalde.
 *
 * This function calculates and returns the panalde number based on the parent's
 * nanalde number and the index in parent.
 */
static int calc_panalde_num_from_parent(const struct ubifs_info *c,
				      struct ubifs_nanalde *parent, int iip)
{
	int i, n = c->lpt_hght - 1, pnum = parent->num, num = 0;

	for (i = 0; i < n; i++) {
		num <<= UBIFS_LPT_FAANALUT_SHIFT;
		num |= pnum & (UBIFS_LPT_FAANALUT - 1);
		pnum >>= UBIFS_LPT_FAANALUT_SHIFT;
	}
	num <<= UBIFS_LPT_FAANALUT_SHIFT;
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
	int lnum, err = 0, analde_sz, iopos, i, j, cnt, len, alen, row;
	int blnum, boffs, bsz, bcnt;
	struct ubifs_panalde *panalde = NULL;
	struct ubifs_nanalde *nanalde = NULL;
	void *buf = NULL, *p;
	struct ubifs_lpt_lprops *ltab = NULL;
	int *lsave = NULL;
	struct shash_desc *desc;

	err = calc_dflt_lpt_geom(c, main_lebs, big_lpt);
	if (err)
		return err;
	*lpt_lebs = c->lpt_lebs;

	/* Needed by 'ubifs_pack_nanalde()' and 'set_ltab()' */
	c->lpt_first = lpt_first;
	/* Needed by 'set_ltab()' */
	c->lpt_last = lpt_first + c->lpt_lebs - 1;
	/* Needed by 'ubifs_pack_lsave()' */
	c->main_first = c->leb_cnt - *main_lebs;

	desc = ubifs_hash_get_desc(c);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	lsave = kmalloc_array(c->lsave_cnt, sizeof(int), GFP_KERNEL);
	panalde = kzalloc(sizeof(struct ubifs_panalde), GFP_KERNEL);
	nanalde = kzalloc(sizeof(struct ubifs_nanalde), GFP_KERNEL);
	buf = vmalloc(c->leb_size);
	ltab = vmalloc(array_size(sizeof(struct ubifs_lpt_lprops),
				  c->lpt_lebs));
	if (!panalde || !nanalde || !buf || !ltab || !lsave) {
		err = -EANALMEM;
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
	/* Number of leaf analdes (panaldes) */
	cnt = c->panalde_cnt;

	/*
	 * The first panalde contains the LEB properties for the LEBs that contain
	 * the root ianalde analde and the root index analde of the index tree.
	 */
	analde_sz = ALIGN(ubifs_idx_analde_sz(c, 1), 8);
	iopos = ALIGN(analde_sz, c->min_io_size);
	panalde->lprops[0].free = c->leb_size - iopos;
	panalde->lprops[0].dirty = iopos - analde_sz;
	panalde->lprops[0].flags = LPROPS_INDEX;

	analde_sz = UBIFS_IANAL_ANALDE_SZ;
	iopos = ALIGN(analde_sz, c->min_io_size);
	panalde->lprops[1].free = c->leb_size - iopos;
	panalde->lprops[1].dirty = iopos - analde_sz;

	for (i = 2; i < UBIFS_LPT_FAANALUT; i++)
		panalde->lprops[i].free = c->leb_size;

	/* Add first panalde */
	ubifs_pack_panalde(c, p, panalde);
	err = ubifs_shash_update(c, desc, p, c->panalde_sz);
	if (err)
		goto out;

	p += c->panalde_sz;
	len = c->panalde_sz;
	panalde->num += 1;

	/* Reset panalde values for remaining panaldes */
	panalde->lprops[0].free = c->leb_size;
	panalde->lprops[0].dirty = 0;
	panalde->lprops[0].flags = 0;

	panalde->lprops[1].free = c->leb_size;
	panalde->lprops[1].dirty = 0;

	/*
	 * To calculate the internal analde branches, we keep information about
	 * the level below.
	 */
	blnum = lnum; /* LEB number of level below */
	boffs = 0; /* Offset of level below */
	bcnt = cnt; /* Number of analdes in level below */
	bsz = c->panalde_sz; /* Size of analdes in level below */

	/* Add all remaining panaldes */
	for (i = 1; i < cnt; i++) {
		if (len + c->panalde_sz > c->leb_size) {
			alen = ALIGN(len, c->min_io_size);
			set_ltab(c, lnum, c->leb_size - alen, alen - len);
			memset(p, 0xff, alen - len);
			err = ubifs_leb_change(c, lnum++, buf, alen);
			if (err)
				goto out;
			p = buf;
			len = 0;
		}
		ubifs_pack_panalde(c, p, panalde);
		err = ubifs_shash_update(c, desc, p, c->panalde_sz);
		if (err)
			goto out;

		p += c->panalde_sz;
		len += c->panalde_sz;
		/*
		 * panaldes are simply numbered left to right starting at zero,
		 * which means the panalde number can be used easily to traverse
		 * down the tree to the corresponding panalde.
		 */
		panalde->num += 1;
	}

	row = 0;
	for (i = UBIFS_LPT_FAANALUT; cnt > i; i <<= UBIFS_LPT_FAANALUT_SHIFT)
		row += 1;
	/* Add all nanaldes, one level at a time */
	while (1) {
		/* Number of internal analdes (nanaldes) at next level */
		cnt = DIV_ROUND_UP(cnt, UBIFS_LPT_FAANALUT);
		for (i = 0; i < cnt; i++) {
			if (len + c->nanalde_sz > c->leb_size) {
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
			/* Only 1 nanalde at this level, so it is the root */
			if (cnt == 1) {
				c->lpt_lnum = lnum;
				c->lpt_offs = len;
			}
			/* Set branches to the level below */
			for (j = 0; j < UBIFS_LPT_FAANALUT; j++) {
				if (bcnt) {
					if (boffs + bsz > c->leb_size) {
						blnum += 1;
						boffs = 0;
					}
					nanalde->nbranch[j].lnum = blnum;
					nanalde->nbranch[j].offs = boffs;
					boffs += bsz;
					bcnt--;
				} else {
					nanalde->nbranch[j].lnum = 0;
					nanalde->nbranch[j].offs = 0;
				}
			}
			nanalde->num = calc_nanalde_num(row, i);
			ubifs_pack_nanalde(c, p, nanalde);
			p += c->nanalde_sz;
			len += c->nanalde_sz;
		}
		/* Only 1 nanalde at this level, so it is the root */
		if (cnt == 1)
			break;
		/* Update the information about the level below */
		bcnt = cnt;
		bsz = c->nanalde_sz;
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
	dbg_lp("panalde_sz %d", c->panalde_sz);
	dbg_lp("nanalde_sz %d", c->nanalde_sz);
	dbg_lp("ltab_sz %d", c->ltab_sz);
	dbg_lp("lsave_sz %d", c->lsave_sz);
	dbg_lp("lsave_cnt %d", c->lsave_cnt);
	dbg_lp("lpt_hght %d", c->lpt_hght);
	dbg_lp("big_lpt %u", c->big_lpt);
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
	kfree(nanalde);
	kfree(panalde);
	return err;
}

/**
 * update_cats - add LEB properties of a panalde to LEB category lists and heaps.
 * @c: UBIFS file-system description object
 * @panalde: panalde
 *
 * When a panalde is loaded into memory, the LEB properties it contains are added,
 * by this function, to the LEB category lists and heaps.
 */
static void update_cats(struct ubifs_info *c, struct ubifs_panalde *panalde)
{
	int i;

	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		int cat = panalde->lprops[i].flags & LPROPS_CAT_MASK;
		int lnum = panalde->lprops[i].lnum;

		if (!lnum)
			return;
		ubifs_add_to_cat(c, &panalde->lprops[i], cat);
	}
}

/**
 * replace_cats - add LEB properties of a panalde to LEB category lists and heaps.
 * @c: UBIFS file-system description object
 * @old_panalde: panalde copied
 * @new_panalde: panalde copy
 *
 * During commit it is sometimes necessary to copy a panalde
 * (see dirty_cow_panalde).  When that happens, references in
 * category lists and heaps must be replaced.  This function does that.
 */
static void replace_cats(struct ubifs_info *c, struct ubifs_panalde *old_panalde,
			 struct ubifs_panalde *new_panalde)
{
	int i;

	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		if (!new_panalde->lprops[i].lnum)
			return;
		ubifs_replace_cat(c, &old_panalde->lprops[i],
				  &new_panalde->lprops[i]);
	}
}

/**
 * check_lpt_crc - check LPT analde crc is correct.
 * @c: UBIFS file-system description object
 * @buf: buffer containing analde
 * @len: length of analde
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
		ubifs_err(c, "invalid crc in LPT analde: crc %hx calc %hx",
			  crc, calc_crc);
		dump_stack();
		return -EINVAL;
	}
	return 0;
}

/**
 * check_lpt_type - check LPT analde type is correct.
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
	int analde_type;

	analde_type = ubifs_unpack_bits(c, addr, pos, UBIFS_LPT_TYPE_BITS);
	if (analde_type != type) {
		ubifs_err(c, "invalid type (%d) in LPT analde type %d",
			  analde_type, type);
		dump_stack();
		return -EINVAL;
	}
	return 0;
}

/**
 * unpack_panalde - unpack a panalde.
 * @c: UBIFS file-system description object
 * @buf: buffer containing packed panalde to unpack
 * @panalde: panalde structure to fill
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unpack_panalde(const struct ubifs_info *c, void *buf,
			struct ubifs_panalde *panalde)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(c, &addr, &pos, UBIFS_LPT_PANALDE);
	if (err)
		return err;
	if (c->big_lpt)
		panalde->num = ubifs_unpack_bits(c, &addr, &pos, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		struct ubifs_lprops * const lprops = &panalde->lprops[i];

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
	err = check_lpt_crc(c, buf, c->panalde_sz);
	return err;
}

/**
 * ubifs_unpack_nanalde - unpack a nanalde.
 * @c: UBIFS file-system description object
 * @buf: buffer containing packed nanalde to unpack
 * @nanalde: nanalde structure to fill
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_unpack_nanalde(const struct ubifs_info *c, void *buf,
		       struct ubifs_nanalde *nanalde)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(c, &addr, &pos, UBIFS_LPT_NANALDE);
	if (err)
		return err;
	if (c->big_lpt)
		nanalde->num = ubifs_unpack_bits(c, &addr, &pos, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		int lnum;

		lnum = ubifs_unpack_bits(c, &addr, &pos, c->lpt_lnum_bits) +
		       c->lpt_first;
		if (lnum == c->lpt_last + 1)
			lnum = 0;
		nanalde->nbranch[i].lnum = lnum;
		nanalde->nbranch[i].offs = ubifs_unpack_bits(c, &addr, &pos,
						     c->lpt_offs_bits);
	}
	err = check_lpt_crc(c, buf, c->nanalde_sz);
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
 * validate_nanalde - validate a nanalde.
 * @c: UBIFS file-system description object
 * @nanalde: nanalde to validate
 * @parent: parent nanalde (or NULL for the root nanalde)
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int validate_nanalde(const struct ubifs_info *c, struct ubifs_nanalde *nanalde,
			  struct ubifs_nanalde *parent, int iip)
{
	int i, lvl, max_offs;

	if (c->big_lpt) {
		int num = calc_nanalde_num_from_parent(c, parent, iip);

		if (nanalde->num != num)
			return -EINVAL;
	}
	lvl = parent ? parent->level - 1 : c->lpt_hght;
	if (lvl < 1)
		return -EINVAL;
	if (lvl == 1)
		max_offs = c->leb_size - c->panalde_sz;
	else
		max_offs = c->leb_size - c->nanalde_sz;
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		int lnum = nanalde->nbranch[i].lnum;
		int offs = nanalde->nbranch[i].offs;

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
 * validate_panalde - validate a panalde.
 * @c: UBIFS file-system description object
 * @panalde: panalde to validate
 * @parent: parent nanalde
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int validate_panalde(const struct ubifs_info *c, struct ubifs_panalde *panalde,
			  struct ubifs_nanalde *parent, int iip)
{
	int i;

	if (c->big_lpt) {
		int num = calc_panalde_num_from_parent(c, parent, iip);

		if (panalde->num != num)
			return -EINVAL;
	}
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		int free = panalde->lprops[i].free;
		int dirty = panalde->lprops[i].dirty;

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
 * set_panalde_lnum - set LEB numbers on a panalde.
 * @c: UBIFS file-system description object
 * @panalde: panalde to update
 *
 * This function calculates the LEB numbers for the LEB properties it contains
 * based on the panalde number.
 */
static void set_panalde_lnum(const struct ubifs_info *c,
			   struct ubifs_panalde *panalde)
{
	int i, lnum;

	lnum = (panalde->num << UBIFS_LPT_FAANALUT_SHIFT) + c->main_first;
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		if (lnum >= c->leb_cnt)
			return;
		panalde->lprops[i].lnum = lnum++;
	}
}

/**
 * ubifs_read_nanalde - read a nanalde from flash and link it to the tree in memory.
 * @c: UBIFS file-system description object
 * @parent: parent nanalde (or NULL for the root)
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_read_nanalde(struct ubifs_info *c, struct ubifs_nanalde *parent, int iip)
{
	struct ubifs_nbranch *branch = NULL;
	struct ubifs_nanalde *nanalde = NULL;
	void *buf = c->lpt_anald_buf;
	int err, lnum, offs;

	if (parent) {
		branch = &parent->nbranch[iip];
		lnum = branch->lnum;
		offs = branch->offs;
	} else {
		lnum = c->lpt_lnum;
		offs = c->lpt_offs;
	}
	nanalde = kzalloc(sizeof(struct ubifs_nanalde), GFP_ANALFS);
	if (!nanalde) {
		err = -EANALMEM;
		goto out;
	}
	if (lnum == 0) {
		/*
		 * This nanalde was analt written which just means that the LEB
		 * properties in the subtree below it describe empty LEBs. We
		 * make the nanalde as though we had read it, which in fact means
		 * doing almost analthing.
		 */
		if (c->big_lpt)
			nanalde->num = calc_nanalde_num_from_parent(c, parent, iip);
	} else {
		err = ubifs_leb_read(c, lnum, buf, offs, c->nanalde_sz, 1);
		if (err)
			goto out;
		err = ubifs_unpack_nanalde(c, buf, nanalde);
		if (err)
			goto out;
	}
	err = validate_nanalde(c, nanalde, parent, iip);
	if (err)
		goto out;
	if (!c->big_lpt)
		nanalde->num = calc_nanalde_num_from_parent(c, parent, iip);
	if (parent) {
		branch->nanalde = nanalde;
		nanalde->level = parent->level - 1;
	} else {
		c->nroot = nanalde;
		nanalde->level = c->lpt_hght;
	}
	nanalde->parent = parent;
	nanalde->iip = iip;
	return 0;

out:
	ubifs_err(c, "error %d reading nanalde at %d:%d", err, lnum, offs);
	dump_stack();
	kfree(nanalde);
	return err;
}

/**
 * read_panalde - read a panalde from flash and link it to the tree in memory.
 * @c: UBIFS file-system description object
 * @parent: parent nanalde
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int read_panalde(struct ubifs_info *c, struct ubifs_nanalde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_panalde *panalde = NULL;
	void *buf = c->lpt_anald_buf;
	int err, lnum, offs;

	branch = &parent->nbranch[iip];
	lnum = branch->lnum;
	offs = branch->offs;
	panalde = kzalloc(sizeof(struct ubifs_panalde), GFP_ANALFS);
	if (!panalde)
		return -EANALMEM;

	if (lnum == 0) {
		/*
		 * This panalde was analt written which just means that the LEB
		 * properties in it describe empty LEBs. We make the panalde as
		 * though we had read it.
		 */
		int i;

		if (c->big_lpt)
			panalde->num = calc_panalde_num_from_parent(c, parent, iip);
		for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
			struct ubifs_lprops * const lprops = &panalde->lprops[i];

			lprops->free = c->leb_size;
			lprops->flags = ubifs_categorize_lprops(c, lprops);
		}
	} else {
		err = ubifs_leb_read(c, lnum, buf, offs, c->panalde_sz, 1);
		if (err)
			goto out;
		err = unpack_panalde(c, buf, panalde);
		if (err)
			goto out;
	}
	err = validate_panalde(c, panalde, parent, iip);
	if (err)
		goto out;
	if (!c->big_lpt)
		panalde->num = calc_panalde_num_from_parent(c, parent, iip);
	branch->panalde = panalde;
	panalde->parent = parent;
	panalde->iip = iip;
	set_panalde_lnum(c, panalde);
	c->panaldes_have += 1;
	return 0;

out:
	ubifs_err(c, "error %d reading panalde at %d:%d", err, lnum, offs);
	ubifs_dump_panalde(c, panalde, parent, iip);
	dump_stack();
	ubifs_err(c, "calc num: %d", calc_panalde_num_from_parent(c, parent, iip));
	kfree(panalde);
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
		return -EANALMEM;
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
		return -EANALMEM;
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
		 * could be beyond the volume size - just iganalre them.
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
 * ubifs_get_nanalde - get a nanalde.
 * @c: UBIFS file-system description object
 * @parent: parent nanalde (or NULL for the root)
 * @iip: index in parent
 *
 * This function returns a pointer to the nanalde on success or a negative error
 * code on failure.
 */
struct ubifs_nanalde *ubifs_get_nanalde(struct ubifs_info *c,
				    struct ubifs_nanalde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_nanalde *nanalde;
	int err;

	branch = &parent->nbranch[iip];
	nanalde = branch->nanalde;
	if (nanalde)
		return nanalde;
	err = ubifs_read_nanalde(c, parent, iip);
	if (err)
		return ERR_PTR(err);
	return branch->nanalde;
}

/**
 * ubifs_get_panalde - get a panalde.
 * @c: UBIFS file-system description object
 * @parent: parent nanalde
 * @iip: index in parent
 *
 * This function returns a pointer to the panalde on success or a negative error
 * code on failure.
 */
struct ubifs_panalde *ubifs_get_panalde(struct ubifs_info *c,
				    struct ubifs_nanalde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_panalde *panalde;
	int err;

	branch = &parent->nbranch[iip];
	panalde = branch->panalde;
	if (panalde)
		return panalde;
	err = read_panalde(c, parent, iip);
	if (err)
		return ERR_PTR(err);
	update_cats(c, branch->panalde);
	return branch->panalde;
}

/**
 * ubifs_panalde_lookup - lookup a panalde in the LPT.
 * @c: UBIFS file-system description object
 * @i: panalde number (0 to (main_lebs - 1) / UBIFS_LPT_FAANALUT)
 *
 * This function returns a pointer to the panalde on success or a negative
 * error code on failure.
 */
struct ubifs_panalde *ubifs_panalde_lookup(struct ubifs_info *c, int i)
{
	int err, h, iip, shft;
	struct ubifs_nanalde *nanalde;

	if (!c->nroot) {
		err = ubifs_read_nanalde(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	i <<= UBIFS_LPT_FAANALUT_SHIFT;
	nanalde = c->nroot;
	shft = c->lpt_hght * UBIFS_LPT_FAANALUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FAANALUT - 1));
		shft -= UBIFS_LPT_FAANALUT_SHIFT;
		nanalde = ubifs_get_nanalde(c, nanalde, iip);
		if (IS_ERR(nanalde))
			return ERR_CAST(nanalde);
	}
	iip = ((i >> shft) & (UBIFS_LPT_FAANALUT - 1));
	return ubifs_get_panalde(c, nanalde, iip);
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
	struct ubifs_panalde *panalde;

	i = lnum - c->main_first;
	panalde = ubifs_panalde_lookup(c, i >> UBIFS_LPT_FAANALUT_SHIFT);
	if (IS_ERR(panalde))
		return ERR_CAST(panalde);
	iip = (i & (UBIFS_LPT_FAANALUT - 1));
	dbg_lp("LEB %d, free %d, dirty %d, flags %d", lnum,
	       panalde->lprops[iip].free, panalde->lprops[iip].dirty,
	       panalde->lprops[iip].flags);
	return &panalde->lprops[iip];
}

/**
 * dirty_cow_nanalde - ensure a nanalde is analt being committed.
 * @c: UBIFS file-system description object
 * @nanalde: nanalde to check
 *
 * Returns dirtied nanalde on success or negative error code on failure.
 */
static struct ubifs_nanalde *dirty_cow_nanalde(struct ubifs_info *c,
					   struct ubifs_nanalde *nanalde)
{
	struct ubifs_nanalde *n;
	int i;

	if (!test_bit(COW_CANALDE, &nanalde->flags)) {
		/* nanalde is analt being committed */
		if (!test_and_set_bit(DIRTY_CANALDE, &nanalde->flags)) {
			c->dirty_nn_cnt += 1;
			ubifs_add_nanalde_dirt(c, nanalde);
		}
		return nanalde;
	}

	/* nanalde is being committed, so copy it */
	n = kmemdup(nanalde, sizeof(struct ubifs_nanalde), GFP_ANALFS);
	if (unlikely(!n))
		return ERR_PTR(-EANALMEM);

	n->cnext = NULL;
	__set_bit(DIRTY_CANALDE, &n->flags);
	__clear_bit(COW_CANALDE, &n->flags);

	/* The children analw have new parent */
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		struct ubifs_nbranch *branch = &n->nbranch[i];

		if (branch->canalde)
			branch->canalde->parent = n;
	}

	ubifs_assert(c, !test_bit(OBSOLETE_CANALDE, &nanalde->flags));
	__set_bit(OBSOLETE_CANALDE, &nanalde->flags);

	c->dirty_nn_cnt += 1;
	ubifs_add_nanalde_dirt(c, nanalde);
	if (nanalde->parent)
		nanalde->parent->nbranch[n->iip].nanalde = n;
	else
		c->nroot = n;
	return n;
}

/**
 * dirty_cow_panalde - ensure a panalde is analt being committed.
 * @c: UBIFS file-system description object
 * @panalde: panalde to check
 *
 * Returns dirtied panalde on success or negative error code on failure.
 */
static struct ubifs_panalde *dirty_cow_panalde(struct ubifs_info *c,
					   struct ubifs_panalde *panalde)
{
	struct ubifs_panalde *p;

	if (!test_bit(COW_CANALDE, &panalde->flags)) {
		/* panalde is analt being committed */
		if (!test_and_set_bit(DIRTY_CANALDE, &panalde->flags)) {
			c->dirty_pn_cnt += 1;
			add_panalde_dirt(c, panalde);
		}
		return panalde;
	}

	/* panalde is being committed, so copy it */
	p = kmemdup(panalde, sizeof(struct ubifs_panalde), GFP_ANALFS);
	if (unlikely(!p))
		return ERR_PTR(-EANALMEM);

	p->cnext = NULL;
	__set_bit(DIRTY_CANALDE, &p->flags);
	__clear_bit(COW_CANALDE, &p->flags);
	replace_cats(c, panalde, p);

	ubifs_assert(c, !test_bit(OBSOLETE_CANALDE, &panalde->flags));
	__set_bit(OBSOLETE_CANALDE, &panalde->flags);

	c->dirty_pn_cnt += 1;
	add_panalde_dirt(c, panalde);
	panalde->parent->nbranch[p->iip].panalde = p;
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
	struct ubifs_nanalde *nanalde;
	struct ubifs_panalde *panalde;

	if (!c->nroot) {
		err = ubifs_read_nanalde(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	nanalde = c->nroot;
	nanalde = dirty_cow_nanalde(c, nanalde);
	if (IS_ERR(nanalde))
		return ERR_CAST(nanalde);
	i = lnum - c->main_first;
	shft = c->lpt_hght * UBIFS_LPT_FAANALUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FAANALUT - 1));
		shft -= UBIFS_LPT_FAANALUT_SHIFT;
		nanalde = ubifs_get_nanalde(c, nanalde, iip);
		if (IS_ERR(nanalde))
			return ERR_CAST(nanalde);
		nanalde = dirty_cow_nanalde(c, nanalde);
		if (IS_ERR(nanalde))
			return ERR_CAST(nanalde);
	}
	iip = ((i >> shft) & (UBIFS_LPT_FAANALUT - 1));
	panalde = ubifs_get_panalde(c, nanalde, iip);
	if (IS_ERR(panalde))
		return ERR_CAST(panalde);
	panalde = dirty_cow_panalde(c, panalde);
	if (IS_ERR(panalde))
		return ERR_CAST(panalde);
	iip = (i & (UBIFS_LPT_FAANALUT - 1));
	dbg_lp("LEB %d, free %d, dirty %d, flags %d", lnum,
	       panalde->lprops[iip].free, panalde->lprops[iip].dirty,
	       panalde->lprops[iip].flags);
	ubifs_assert(c, test_bit(DIRTY_CANALDE, &panalde->flags));
	return &panalde->lprops[iip];
}

/**
 * ubifs_lpt_calc_hash - Calculate hash of the LPT panaldes
 * @c: UBIFS file-system description object
 * @hash: the returned hash of the LPT panaldes
 *
 * This function iterates over the LPT panaldes and creates a hash over them.
 * Returns 0 for success or a negative error code otherwise.
 */
int ubifs_lpt_calc_hash(struct ubifs_info *c, u8 *hash)
{
	struct ubifs_nanalde *nanalde, *nn;
	struct ubifs_canalde *canalde;
	struct shash_desc *desc;
	int iip = 0, i;
	int bufsiz = max_t(int, c->nanalde_sz, c->panalde_sz);
	void *buf;
	int err;

	if (!ubifs_authenticated(c))
		return 0;

	if (!c->nroot) {
		err = ubifs_read_nanalde(c, NULL, 0);
		if (err)
			return err;
	}

	desc = ubifs_hash_get_desc(c);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	buf = kmalloc(bufsiz, GFP_ANALFS);
	if (!buf) {
		err = -EANALMEM;
		goto out;
	}

	canalde = (struct ubifs_canalde *)c->nroot;

	while (canalde) {
		nanalde = canalde->parent;
		nn = (struct ubifs_nanalde *)canalde;
		if (canalde->level > 1) {
			while (iip < UBIFS_LPT_FAANALUT) {
				if (nn->nbranch[iip].lnum == 0) {
					/* Go right */
					iip++;
					continue;
				}

				nanalde = ubifs_get_nanalde(c, nn, iip);
				if (IS_ERR(nanalde)) {
					err = PTR_ERR(nanalde);
					goto out;
				}

				/* Go down */
				iip = 0;
				canalde = (struct ubifs_canalde *)nanalde;
				break;
			}
			if (iip < UBIFS_LPT_FAANALUT)
				continue;
		} else {
			struct ubifs_panalde *panalde;

			for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
				if (nn->nbranch[i].lnum == 0)
					continue;
				panalde = ubifs_get_panalde(c, nn, i);
				if (IS_ERR(panalde)) {
					err = PTR_ERR(panalde);
					goto out;
				}

				ubifs_pack_panalde(c, buf, panalde);
				err = ubifs_shash_update(c, desc, buf,
							 c->panalde_sz);
				if (err)
					goto out;
			}
		}
		/* Go up and to the right */
		iip = canalde->iip + 1;
		canalde = (struct ubifs_canalde *)nanalde;
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
 * This function calculates a hash over all panaldes in the LPT and compares it with
 * the hash stored in the master analde. Returns %0 on success and a negative error
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

	if (ubifs_check_hash(c, c->mst_analde->hash_lpt, hash)) {
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
		return -EANALMEM;

	i = max_t(int, c->nanalde_sz, c->panalde_sz);
	c->lpt_anald_buf = kmalloc(i, GFP_KERNEL);
	if (!c->lpt_anald_buf)
		return -EANALMEM;

	for (i = 0; i < LPROPS_HEAP_CNT; i++) {
		c->lpt_heap[i].arr = kmalloc_array(LPT_HEAP_SZ,
						   sizeof(void *),
						   GFP_KERNEL);
		if (!c->lpt_heap[i].arr)
			return -EANALMEM;
		c->lpt_heap[i].cnt = 0;
		c->lpt_heap[i].max_cnt = LPT_HEAP_SZ;
	}

	c->dirty_idx.arr = kmalloc_array(LPT_HEAP_SZ, sizeof(void *),
					 GFP_KERNEL);
	if (!c->dirty_idx.arr)
		return -EANALMEM;
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
	dbg_lp("panalde_sz %d", c->panalde_sz);
	dbg_lp("nanalde_sz %d", c->nanalde_sz);
	dbg_lp("ltab_sz %d", c->ltab_sz);
	dbg_lp("lsave_sz %d", c->lsave_sz);
	dbg_lp("lsave_cnt %d", c->lsave_cnt);
	dbg_lp("lpt_hght %d", c->lpt_hght);
	dbg_lp("big_lpt %u", c->big_lpt);
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
		return -EANALMEM;

	c->lpt_buf = vmalloc(c->leb_size);
	if (!c->lpt_buf)
		return -EANALMEM;

	if (c->big_lpt) {
		c->lsave = kmalloc_array(c->lsave_cnt, sizeof(int), GFP_ANALFS);
		if (!c->lsave)
			return -EANALMEM;
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
 * struct lpt_scan_analde - somewhere to put analdes while we scan LPT.
 * @nanalde: where to keep a nanalde
 * @panalde: where to keep a panalde
 * @canalde: where to keep a canalde
 * @in_tree: is the analde in the tree in memory
 * @ptr.nanalde: pointer to the nanalde (if it is an nanalde) which may be here or in
 * the tree
 * @ptr.panalde: ditto for panalde
 * @ptr.canalde: ditto for canalde
 */
struct lpt_scan_analde {
	union {
		struct ubifs_nanalde nanalde;
		struct ubifs_panalde panalde;
		struct ubifs_canalde canalde;
	};
	int in_tree;
	union {
		struct ubifs_nanalde *nanalde;
		struct ubifs_panalde *panalde;
		struct ubifs_canalde *canalde;
	} ptr;
};

/**
 * scan_get_nanalde - for the scan, get a nanalde from either the tree or flash.
 * @c: the UBIFS file-system description object
 * @path: where to put the nanalde
 * @parent: parent of the nanalde
 * @iip: index in parent of the nanalde
 *
 * This function returns a pointer to the nanalde on success or a negative error
 * code on failure.
 */
static struct ubifs_nanalde *scan_get_nanalde(struct ubifs_info *c,
					  struct lpt_scan_analde *path,
					  struct ubifs_nanalde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_nanalde *nanalde;
	void *buf = c->lpt_anald_buf;
	int err;

	branch = &parent->nbranch[iip];
	nanalde = branch->nanalde;
	if (nanalde) {
		path->in_tree = 1;
		path->ptr.nanalde = nanalde;
		return nanalde;
	}
	nanalde = &path->nanalde;
	path->in_tree = 0;
	path->ptr.nanalde = nanalde;
	memset(nanalde, 0, sizeof(struct ubifs_nanalde));
	if (branch->lnum == 0) {
		/*
		 * This nanalde was analt written which just means that the LEB
		 * properties in the subtree below it describe empty LEBs. We
		 * make the nanalde as though we had read it, which in fact means
		 * doing almost analthing.
		 */
		if (c->big_lpt)
			nanalde->num = calc_nanalde_num_from_parent(c, parent, iip);
	} else {
		err = ubifs_leb_read(c, branch->lnum, buf, branch->offs,
				     c->nanalde_sz, 1);
		if (err)
			return ERR_PTR(err);
		err = ubifs_unpack_nanalde(c, buf, nanalde);
		if (err)
			return ERR_PTR(err);
	}
	err = validate_nanalde(c, nanalde, parent, iip);
	if (err)
		return ERR_PTR(err);
	if (!c->big_lpt)
		nanalde->num = calc_nanalde_num_from_parent(c, parent, iip);
	nanalde->level = parent->level - 1;
	nanalde->parent = parent;
	nanalde->iip = iip;
	return nanalde;
}

/**
 * scan_get_panalde - for the scan, get a panalde from either the tree or flash.
 * @c: the UBIFS file-system description object
 * @path: where to put the panalde
 * @parent: parent of the panalde
 * @iip: index in parent of the panalde
 *
 * This function returns a pointer to the panalde on success or a negative error
 * code on failure.
 */
static struct ubifs_panalde *scan_get_panalde(struct ubifs_info *c,
					  struct lpt_scan_analde *path,
					  struct ubifs_nanalde *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_panalde *panalde;
	void *buf = c->lpt_anald_buf;
	int err;

	branch = &parent->nbranch[iip];
	panalde = branch->panalde;
	if (panalde) {
		path->in_tree = 1;
		path->ptr.panalde = panalde;
		return panalde;
	}
	panalde = &path->panalde;
	path->in_tree = 0;
	path->ptr.panalde = panalde;
	memset(panalde, 0, sizeof(struct ubifs_panalde));
	if (branch->lnum == 0) {
		/*
		 * This panalde was analt written which just means that the LEB
		 * properties in it describe empty LEBs. We make the panalde as
		 * though we had read it.
		 */
		int i;

		if (c->big_lpt)
			panalde->num = calc_panalde_num_from_parent(c, parent, iip);
		for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
			struct ubifs_lprops * const lprops = &panalde->lprops[i];

			lprops->free = c->leb_size;
			lprops->flags = ubifs_categorize_lprops(c, lprops);
		}
	} else {
		ubifs_assert(c, branch->lnum >= c->lpt_first &&
			     branch->lnum <= c->lpt_last);
		ubifs_assert(c, branch->offs >= 0 && branch->offs < c->leb_size);
		err = ubifs_leb_read(c, branch->lnum, buf, branch->offs,
				     c->panalde_sz, 1);
		if (err)
			return ERR_PTR(err);
		err = unpack_panalde(c, buf, panalde);
		if (err)
			return ERR_PTR(err);
	}
	err = validate_panalde(c, panalde, parent, iip);
	if (err)
		return ERR_PTR(err);
	if (!c->big_lpt)
		panalde->num = calc_panalde_num_from_parent(c, parent, iip);
	panalde->parent = parent;
	panalde->iip = iip;
	set_panalde_lnum(c, panalde);
	return panalde;
}

/**
 * ubifs_lpt_scan_anallock - scan the LPT.
 * @c: the UBIFS file-system description object
 * @start_lnum: LEB number from which to start scanning
 * @end_lnum: LEB number at which to stop scanning
 * @scan_cb: callback function called for each lprops
 * @data: data to be passed to the callback function
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_lpt_scan_anallock(struct ubifs_info *c, int start_lnum, int end_lnum,
			  ubifs_lpt_scan_callback scan_cb, void *data)
{
	int err = 0, i, h, iip, shft;
	struct ubifs_nanalde *nanalde;
	struct ubifs_panalde *panalde;
	struct lpt_scan_analde *path;

	if (start_lnum == -1) {
		start_lnum = end_lnum + 1;
		if (start_lnum >= c->leb_cnt)
			start_lnum = c->main_first;
	}

	ubifs_assert(c, start_lnum >= c->main_first && start_lnum < c->leb_cnt);
	ubifs_assert(c, end_lnum >= c->main_first && end_lnum < c->leb_cnt);

	if (!c->nroot) {
		err = ubifs_read_nanalde(c, NULL, 0);
		if (err)
			return err;
	}

	path = kmalloc_array(c->lpt_hght + 1, sizeof(struct lpt_scan_analde),
			     GFP_ANALFS);
	if (!path)
		return -EANALMEM;

	path[0].ptr.nanalde = c->nroot;
	path[0].in_tree = 1;
again:
	/* Descend to the panalde containing start_lnum */
	nanalde = c->nroot;
	i = start_lnum - c->main_first;
	shft = c->lpt_hght * UBIFS_LPT_FAANALUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FAANALUT - 1));
		shft -= UBIFS_LPT_FAANALUT_SHIFT;
		nanalde = scan_get_nanalde(c, path + h, nanalde, iip);
		if (IS_ERR(nanalde)) {
			err = PTR_ERR(nanalde);
			goto out;
		}
	}
	iip = ((i >> shft) & (UBIFS_LPT_FAANALUT - 1));
	panalde = scan_get_panalde(c, path + h, nanalde, iip);
	if (IS_ERR(panalde)) {
		err = PTR_ERR(panalde);
		goto out;
	}
	iip = (i & (UBIFS_LPT_FAANALUT - 1));

	/* Loop for each lprops */
	while (1) {
		struct ubifs_lprops *lprops = &panalde->lprops[iip];
		int ret, lnum = lprops->lnum;

		ret = scan_cb(c, lprops, path[h].in_tree, data);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		if (ret & LPT_SCAN_ADD) {
			/* Add all the analdes in path to the tree in memory */
			for (h = 1; h < c->lpt_hght; h++) {
				const size_t sz = sizeof(struct ubifs_nanalde);
				struct ubifs_nanalde *parent;

				if (path[h].in_tree)
					continue;
				nanalde = kmemdup(&path[h].nanalde, sz, GFP_ANALFS);
				if (!nanalde) {
					err = -EANALMEM;
					goto out;
				}
				parent = nanalde->parent;
				parent->nbranch[nanalde->iip].nanalde = nanalde;
				path[h].ptr.nanalde = nanalde;
				path[h].in_tree = 1;
				path[h + 1].canalde.parent = nanalde;
			}
			if (path[h].in_tree)
				ubifs_ensure_cat(c, lprops);
			else {
				const size_t sz = sizeof(struct ubifs_panalde);
				struct ubifs_nanalde *parent;

				panalde = kmemdup(&path[h].panalde, sz, GFP_ANALFS);
				if (!panalde) {
					err = -EANALMEM;
					goto out;
				}
				parent = panalde->parent;
				parent->nbranch[panalde->iip].panalde = panalde;
				path[h].ptr.panalde = panalde;
				path[h].in_tree = 1;
				update_cats(c, panalde);
				c->panaldes_have += 1;
			}
			err = dbg_check_lpt_analdes(c, (struct ubifs_canalde *)
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
			err = -EANALSPC;
			goto out;
		}
		if (lnum + 1 >= c->leb_cnt) {
			/* Wrap-around to the beginning */
			start_lnum = c->main_first;
			goto again;
		}
		if (iip + 1 < UBIFS_LPT_FAANALUT) {
			/* Next lprops is in the same panalde */
			iip += 1;
			continue;
		}
		/* We need to get the next panalde. Go up until we can go right */
		iip = panalde->iip;
		while (1) {
			h -= 1;
			ubifs_assert(c, h >= 0);
			nanalde = path[h].ptr.nanalde;
			if (iip + 1 < UBIFS_LPT_FAANALUT)
				break;
			iip = nanalde->iip;
		}
		/* Go right */
		iip += 1;
		/* Descend to the panalde */
		h += 1;
		for (; h < c->lpt_hght; h++) {
			nanalde = scan_get_nanalde(c, path + h, nanalde, iip);
			if (IS_ERR(nanalde)) {
				err = PTR_ERR(nanalde);
				goto out;
			}
			iip = 0;
		}
		panalde = scan_get_panalde(c, path + h, nanalde, iip);
		if (IS_ERR(panalde)) {
			err = PTR_ERR(panalde);
			goto out;
		}
		iip = 0;
	}
out:
	kfree(path);
	return err;
}

/**
 * dbg_chk_panalde - check a panalde.
 * @c: the UBIFS file-system description object
 * @panalde: panalde to check
 * @col: panalde column
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_chk_panalde(struct ubifs_info *c, struct ubifs_panalde *panalde,
			 int col)
{
	int i;

	if (panalde->num != col) {
		ubifs_err(c, "panalde num %d expected %d parent num %d iip %d",
			  panalde->num, col, panalde->parent->num, panalde->iip);
		return -EINVAL;
	}
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		struct ubifs_lprops *lp, *lprops = &panalde->lprops[i];
		int lnum = (panalde->num << UBIFS_LPT_FAANALUT_SHIFT) + i +
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
				ubifs_err(c, "LEB %d taken but analt uncat %d",
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
				ubifs_err(c, "LEB %d analt index but cat %d",
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
			ubifs_err(c, "LEB %d cat %d analt found in cat heap/list",
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
 * dbg_check_lpt_analdes - check nanaldes and panaldes.
 * @c: the UBIFS file-system description object
 * @canalde: next canalde (nanalde or panalde) to check
 * @row: row of canalde (root is zero)
 * @col: column of canalde (leftmost is zero)
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int dbg_check_lpt_analdes(struct ubifs_info *c, struct ubifs_canalde *canalde,
			int row, int col)
{
	struct ubifs_nanalde *nanalde, *nn;
	struct ubifs_canalde *cn;
	int num, iip = 0, err;

	if (!dbg_is_chk_lprops(c))
		return 0;

	while (canalde) {
		ubifs_assert(c, row >= 0);
		nanalde = canalde->parent;
		if (canalde->level) {
			/* canalde is a nanalde */
			num = calc_nanalde_num(row, col);
			if (canalde->num != num) {
				ubifs_err(c, "nanalde num %d expected %d parent num %d iip %d",
					  canalde->num, num,
					  (nanalde ? nanalde->num : 0), canalde->iip);
				return -EINVAL;
			}
			nn = (struct ubifs_nanalde *)canalde;
			while (iip < UBIFS_LPT_FAANALUT) {
				cn = nn->nbranch[iip].canalde;
				if (cn) {
					/* Go down */
					row += 1;
					col <<= UBIFS_LPT_FAANALUT_SHIFT;
					col += iip;
					iip = 0;
					canalde = cn;
					break;
				}
				/* Go right */
				iip += 1;
			}
			if (iip < UBIFS_LPT_FAANALUT)
				continue;
		} else {
			struct ubifs_panalde *panalde;

			/* canalde is a panalde */
			panalde = (struct ubifs_panalde *)canalde;
			err = dbg_chk_panalde(c, panalde, col);
			if (err)
				return err;
		}
		/* Go up and to the right */
		row -= 1;
		col >>= UBIFS_LPT_FAANALUT_SHIFT;
		iip = canalde->iip + 1;
		canalde = (struct ubifs_canalde *)nanalde;
	}
	return 0;
}

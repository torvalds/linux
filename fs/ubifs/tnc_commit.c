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

/* This file implements TNC functions for committing */

#include <linux/random.h>
#include "ubifs.h"

/**
 * make_idx_node - make an index node for fill-the-gaps method of TNC commit.
 * @c: UBIFS file-system description object
 * @idx: buffer in which to place new index node
 * @znode: znode from which to make new index node
 * @lnum: LEB number where new index node will be written
 * @offs: offset where new index node will be written
 * @len: length of new index node
 */
static int make_idx_node(struct ubifs_info *c, struct ubifs_idx_node *idx,
			 struct ubifs_znode *znode, int lnum, int offs, int len)
{
	struct ubifs_znode *zp;
	int i, err;

	/* Make index node */
	idx->ch.node_type = UBIFS_IDX_NODE;
	idx->child_cnt = cpu_to_le16(znode->child_cnt);
	idx->level = cpu_to_le16(znode->level);
	for (i = 0; i < znode->child_cnt; i++) {
		struct ubifs_branch *br = ubifs_idx_branch(c, idx, i);
		struct ubifs_zbranch *zbr = &znode->zbranch[i];

		key_write_idx(c, &zbr->key, &br->key);
		br->lnum = cpu_to_le32(zbr->lnum);
		br->offs = cpu_to_le32(zbr->offs);
		br->len = cpu_to_le32(zbr->len);
		if (!zbr->lnum || !zbr->len) {
			ubifs_err("bad ref in znode");
			ubifs_dump_znode(c, znode);
			if (zbr->znode)
				ubifs_dump_znode(c, zbr->znode);
		}
	}
	ubifs_prepare_node(c, idx, len, 0);

	znode->lnum = lnum;
	znode->offs = offs;
	znode->len = len;

	err = insert_old_idx_znode(c, znode);

	/* Update the parent */
	zp = znode->parent;
	if (zp) {
		struct ubifs_zbranch *zbr;

		zbr = &zp->zbranch[znode->iip];
		zbr->lnum = lnum;
		zbr->offs = offs;
		zbr->len = len;
	} else {
		c->zroot.lnum = lnum;
		c->zroot.offs = offs;
		c->zroot.len = len;
	}
	c->calc_idx_sz += ALIGN(len, 8);

	atomic_long_dec(&c->dirty_zn_cnt);

	ubifs_assert(ubifs_zn_dirty(znode));
	ubifs_assert(ubifs_zn_cow(znode));

	/*
	 * Note, unlike 'write_index()' we do not add memory barriers here
	 * because this function is called with @c->tnc_mutex locked.
	 */
	__clear_bit(DIRTY_ZNODE, &znode->flags);
	__clear_bit(COW_ZNODE, &znode->flags);

	return err;
}

/**
 * fill_gap - make index nodes in gaps in dirty index LEBs.
 * @c: UBIFS file-system description object
 * @lnum: LEB number that gap appears in
 * @gap_start: offset of start of gap
 * @gap_end: offset of end of gap
 * @dirt: adds dirty space to this
 *
 * This function returns the number of index nodes written into the gap.
 */
static int fill_gap(struct ubifs_info *c, int lnum, int gap_start, int gap_end,
		    int *dirt)
{
	int len, gap_remains, gap_pos, written, pad_len;

	ubifs_assert((gap_start & 7) == 0);
	ubifs_assert((gap_end & 7) == 0);
	ubifs_assert(gap_end >= gap_start);

	gap_remains = gap_end - gap_start;
	if (!gap_remains)
		return 0;
	gap_pos = gap_start;
	written = 0;
	while (c->enext) {
		len = ubifs_idx_node_sz(c, c->enext->child_cnt);
		if (len < gap_remains) {
			struct ubifs_znode *znode = c->enext;
			const int alen = ALIGN(len, 8);
			int err;

			ubifs_assert(alen <= gap_remains);
			err = make_idx_node(c, c->ileb_buf + gap_pos, znode,
					    lnum, gap_pos, len);
			if (err)
				return err;
			gap_remains -= alen;
			gap_pos += alen;
			c->enext = znode->cnext;
			if (c->enext == c->cnext)
				c->enext = NULL;
			written += 1;
		} else
			break;
	}
	if (gap_end == c->leb_size) {
		c->ileb_len = ALIGN(gap_pos, c->min_io_size);
		/* Pad to end of min_io_size */
		pad_len = c->ileb_len - gap_pos;
	} else
		/* Pad to end of gap */
		pad_len = gap_remains;
	dbg_gc("LEB %d:%d to %d len %d nodes written %d wasted bytes %d",
	       lnum, gap_start, gap_end, gap_end - gap_start, written, pad_len);
	ubifs_pad(c, c->ileb_buf + gap_pos, pad_len);
	*dirt += pad_len;
	return written;
}

/**
 * find_old_idx - find an index node obsoleted since the last commit start.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of obsoleted index node
 * @offs: offset of obsoleted index node
 *
 * Returns %1 if found and %0 otherwise.
 */
static int find_old_idx(struct ubifs_info *c, int lnum, int offs)
{
	struct ubifs_old_idx *o;
	struct rb_node *p;

	p = c->old_idx.rb_node;
	while (p) {
		o = rb_entry(p, struct ubifs_old_idx, rb);
		if (lnum < o->lnum)
			p = p->rb_left;
		else if (lnum > o->lnum)
			p = p->rb_right;
		else if (offs < o->offs)
			p = p->rb_left;
		else if (offs > o->offs)
			p = p->rb_right;
		else
			return 1;
	}
	return 0;
}

/**
 * is_idx_node_in_use - determine if an index node can be overwritten.
 * @c: UBIFS file-system description object
 * @key: key of index node
 * @level: index node level
 * @lnum: LEB number of index node
 * @offs: offset of index node
 *
 * If @key / @lnum / @offs identify an index node that was not part of the old
 * index, then this function returns %0 (obsolete).  Else if the index node was
 * part of the old index but is now dirty %1 is returned, else if it is clean %2
 * is returned. A negative error code is returned on failure.
 */
static int is_idx_node_in_use(struct ubifs_info *c, union ubifs_key *key,
			      int level, int lnum, int offs)
{
	int ret;

	ret = is_idx_node_in_tnc(c, key, level, lnum, offs);
	if (ret < 0)
		return ret; /* Error code */
	if (ret == 0)
		if (find_old_idx(c, lnum, offs))
			return 1;
	return ret;
}

/**
 * layout_leb_in_gaps - layout index nodes using in-the-gaps method.
 * @c: UBIFS file-system description object
 * @p: return LEB number here
 *
 * This function lays out new index nodes for dirty znodes using in-the-gaps
 * method of TNC commit.
 * This function merely puts the next znode into the next gap, making no attempt
 * to try to maximise the number of znodes that fit.
 * This function returns the number of index nodes written into the gaps, or a
 * negative error code on failure.
 */
static int layout_leb_in_gaps(struct ubifs_info *c, int *p)
{
	struct ubifs_scan_leb *sleb;
	struct ubifs_scan_node *snod;
	int lnum, dirt = 0, gap_start, gap_end, err, written, tot_written;

	tot_written = 0;
	/* Get an index LEB with lots of obsolete index nodes */
	lnum = ubifs_find_dirty_idx_leb(c);
	if (lnum < 0)
		/*
		 * There also may be dirt in the index head that could be
		 * filled, however we do not check there at present.
		 */
		return lnum; /* Error code */
	*p = lnum;
	dbg_gc("LEB %d", lnum);
	/*
	 * Scan the index LEB.  We use the generic scan for this even though
	 * it is more comprehensive and less efficient than is needed for this
	 * purpose.
	 */
	sleb = ubifs_scan(c, lnum, 0, c->ileb_buf, 0);
	c->ileb_len = 0;
	if (IS_ERR(sleb))
		return PTR_ERR(sleb);
	gap_start = 0;
	list_for_each_entry(snod, &sleb->nodes, list) {
		struct ubifs_idx_node *idx;
		int in_use, level;

		ubifs_assert(snod->type == UBIFS_IDX_NODE);
		idx = snod->node;
		key_read(c, ubifs_idx_key(c, idx), &snod->key);
		level = le16_to_cpu(idx->level);
		/* Determine if the index node is in use (not obsolete) */
		in_use = is_idx_node_in_use(c, &snod->key, level, lnum,
					    snod->offs);
		if (in_use < 0) {
			ubifs_scan_destroy(sleb);
			return in_use; /* Error code */
		}
		if (in_use) {
			if (in_use == 1)
				dirt += ALIGN(snod->len, 8);
			/*
			 * The obsolete index nodes form gaps that can be
			 * overwritten.  This gap has ended because we have
			 * found an index node that is still in use
			 * i.e. not obsolete
			 */
			gap_end = snod->offs;
			/* Try to fill gap */
			written = fill_gap(c, lnum, gap_start, gap_end, &dirt);
			if (written < 0) {
				ubifs_scan_destroy(sleb);
				return written; /* Error code */
			}
			tot_written += written;
			gap_start = ALIGN(snod->offs + snod->len, 8);
		}
	}
	ubifs_scan_destroy(sleb);
	c->ileb_len = c->leb_size;
	gap_end = c->leb_size;
	/* Try to fill gap */
	written = fill_gap(c, lnum, gap_start, gap_end, &dirt);
	if (written < 0)
		return written; /* Error code */
	tot_written += written;
	if (tot_written == 0) {
		struct ubifs_lprops lp;

		dbg_gc("LEB %d wrote %d index nodes", lnum, tot_written);
		err = ubifs_read_one_lp(c, lnum, &lp);
		if (err)
			return err;
		if (lp.free == c->leb_size) {
			/*
			 * We must have snatched this LEB from the idx_gc list
			 * so we need to correct the free and dirty space.
			 */
			err = ubifs_change_one_lp(c, lnum,
						  c->leb_size - c->ileb_len,
						  dirt, 0, 0, 0);
			if (err)
				return err;
		}
		return 0;
	}
	err = ubifs_change_one_lp(c, lnum, c->leb_size - c->ileb_len, dirt,
				  0, 0, 0);
	if (err)
		return err;
	err = ubifs_leb_change(c, lnum, c->ileb_buf, c->ileb_len);
	if (err)
		return err;
	dbg_gc("LEB %d wrote %d index nodes", lnum, tot_written);
	return tot_written;
}

/**
 * get_leb_cnt - calculate the number of empty LEBs needed to commit.
 * @c: UBIFS file-system description object
 * @cnt: number of znodes to commit
 *
 * This function returns the number of empty LEBs needed to commit @cnt znodes
 * to the current index head.  The number is not exact and may be more than
 * needed.
 */
static int get_leb_cnt(struct ubifs_info *c, int cnt)
{
	int d;

	/* Assume maximum index node size (i.e. overestimate space needed) */
	cnt -= (c->leb_size - c->ihead_offs) / c->max_idx_node_sz;
	if (cnt < 0)
		cnt = 0;
	d = c->leb_size / c->max_idx_node_sz;
	return DIV_ROUND_UP(cnt, d);
}

/**
 * layout_in_gaps - in-the-gaps method of committing TNC.
 * @c: UBIFS file-system description object
 * @cnt: number of dirty znodes to commit.
 *
 * This function lays out new index nodes for dirty znodes using in-the-gaps
 * method of TNC commit.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int layout_in_gaps(struct ubifs_info *c, int cnt)
{
	int err, leb_needed_cnt, written, *p;

	dbg_gc("%d znodes to write", cnt);

	c->gap_lebs = kmalloc(sizeof(int) * (c->lst.idx_lebs + 1), GFP_NOFS);
	if (!c->gap_lebs)
		return -ENOMEM;

	p = c->gap_lebs;
	do {
		ubifs_assert(p < c->gap_lebs + sizeof(int) * c->lst.idx_lebs);
		written = layout_leb_in_gaps(c, p);
		if (written < 0) {
			err = written;
			if (err != -ENOSPC) {
				kfree(c->gap_lebs);
				c->gap_lebs = NULL;
				return err;
			}
			if (!dbg_is_chk_index(c)) {
				/*
				 * Do not print scary warnings if the debugging
				 * option which forces in-the-gaps is enabled.
				 */
				ubifs_warn("out of space");
				ubifs_dump_budg(c, &c->bi);
				ubifs_dump_lprops(c);
			}
			/* Try to commit anyway */
			err = 0;
			break;
		}
		p++;
		cnt -= written;
		leb_needed_cnt = get_leb_cnt(c, cnt);
		dbg_gc("%d znodes remaining, need %d LEBs, have %d", cnt,
		       leb_needed_cnt, c->ileb_cnt);
	} while (leb_needed_cnt > c->ileb_cnt);

	*p = -1;
	return 0;
}

/**
 * layout_in_empty_space - layout index nodes in empty space.
 * @c: UBIFS file-system description object
 *
 * This function lays out new index nodes for dirty znodes using empty LEBs.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int layout_in_empty_space(struct ubifs_info *c)
{
	struct ubifs_znode *znode, *cnext, *zp;
	int lnum, offs, len, next_len, buf_len, buf_offs, used, avail;
	int wlen, blen, err;

	cnext = c->enext;
	if (!cnext)
		return 0;

	lnum = c->ihead_lnum;
	buf_offs = c->ihead_offs;

	buf_len = ubifs_idx_node_sz(c, c->fanout);
	buf_len = ALIGN(buf_len, c->min_io_size);
	used = 0;
	avail = buf_len;

	/* Ensure there is enough room for first write */
	next_len = ubifs_idx_node_sz(c, cnext->child_cnt);
	if (buf_offs + next_len > c->leb_size)
		lnum = -1;

	while (1) {
		znode = cnext;

		len = ubifs_idx_node_sz(c, znode->child_cnt);

		/* Determine the index node position */
		if (lnum == -1) {
			if (c->ileb_nxt >= c->ileb_cnt) {
				ubifs_err("out of space");
				return -ENOSPC;
			}
			lnum = c->ilebs[c->ileb_nxt++];
			buf_offs = 0;
			used = 0;
			avail = buf_len;
		}

		offs = buf_offs + used;

		znode->lnum = lnum;
		znode->offs = offs;
		znode->len = len;

		/* Update the parent */
		zp = znode->parent;
		if (zp) {
			struct ubifs_zbranch *zbr;
			int i;

			i = znode->iip;
			zbr = &zp->zbranch[i];
			zbr->lnum = lnum;
			zbr->offs = offs;
			zbr->len = len;
		} else {
			c->zroot.lnum = lnum;
			c->zroot.offs = offs;
			c->zroot.len = len;
		}
		c->calc_idx_sz += ALIGN(len, 8);

		/*
		 * Once lprops is updated, we can decrease the dirty znode count
		 * but it is easier to just do it here.
		 */
		atomic_long_dec(&c->dirty_zn_cnt);

		/*
		 * Calculate the next index node length to see if there is
		 * enough room for it
		 */
		cnext = znode->cnext;
		if (cnext == c->cnext)
			next_len = 0;
		else
			next_len = ubifs_idx_node_sz(c, cnext->child_cnt);

		/* Update buffer positions */
		wlen = used + len;
		used += ALIGN(len, 8);
		avail -= ALIGN(len, 8);

		if (next_len != 0 &&
		    buf_offs + used + next_len <= c->leb_size &&
		    avail > 0)
			continue;

		if (avail <= 0 && next_len &&
		    buf_offs + used + next_len <= c->leb_size)
			blen = buf_len;
		else
			blen = ALIGN(wlen, c->min_io_size);

		/* The buffer is full or there are no more znodes to do */
		buf_offs += blen;
		if (next_len) {
			if (buf_offs + next_len > c->leb_size) {
				err = ubifs_update_one_lp(c, lnum,
					c->leb_size - buf_offs, blen - used,
					0, 0);
				if (err)
					return err;
				lnum = -1;
			}
			used -= blen;
			if (used < 0)
				used = 0;
			avail = buf_len - used;
			continue;
		}
		err = ubifs_update_one_lp(c, lnum, c->leb_size - buf_offs,
					  blen - used, 0, 0);
		if (err)
			return err;
		break;
	}

	c->dbg->new_ihead_lnum = lnum;
	c->dbg->new_ihead_offs = buf_offs;

	return 0;
}

/**
 * layout_commit - determine positions of index nodes to commit.
 * @c: UBIFS file-system description object
 * @no_space: indicates that insufficient empty LEBs were allocated
 * @cnt: number of znodes to commit
 *
 * Calculate and update the positions of index nodes to commit.  If there were
 * an insufficient number of empty LEBs allocated, then index nodes are placed
 * into the gaps created by obsolete index nodes in non-empty index LEBs.  For
 * this purpose, an obsolete index node is one that was not in the index as at
 * the end of the last commit.  To write "in-the-gaps" requires that those index
 * LEBs are updated atomically in-place.
 */
static int layout_commit(struct ubifs_info *c, int no_space, int cnt)
{
	int err;

	if (no_space) {
		err = layout_in_gaps(c, cnt);
		if (err)
			return err;
	}
	err = layout_in_empty_space(c);
	return err;
}

/**
 * find_first_dirty - find first dirty znode.
 * @znode: znode to begin searching from
 */
static struct ubifs_znode *find_first_dirty(struct ubifs_znode *znode)
{
	int i, cont;

	if (!znode)
		return NULL;

	while (1) {
		if (znode->level == 0) {
			if (ubifs_zn_dirty(znode))
				return znode;
			return NULL;
		}
		cont = 0;
		for (i = 0; i < znode->child_cnt; i++) {
			struct ubifs_zbranch *zbr = &znode->zbranch[i];

			if (zbr->znode && ubifs_zn_dirty(zbr->znode)) {
				znode = zbr->znode;
				cont = 1;
				break;
			}
		}
		if (!cont) {
			if (ubifs_zn_dirty(znode))
				return znode;
			return NULL;
		}
	}
}

/**
 * find_next_dirty - find next dirty znode.
 * @znode: znode to begin searching from
 */
static struct ubifs_znode *find_next_dirty(struct ubifs_znode *znode)
{
	int n = znode->iip + 1;

	znode = znode->parent;
	if (!znode)
		return NULL;
	for (; n < znode->child_cnt; n++) {
		struct ubifs_zbranch *zbr = &znode->zbranch[n];

		if (zbr->znode && ubifs_zn_dirty(zbr->znode))
			return find_first_dirty(zbr->znode);
	}
	return znode;
}

/**
 * get_znodes_to_commit - create list of dirty znodes to commit.
 * @c: UBIFS file-system description object
 *
 * This function returns the number of znodes to commit.
 */
static int get_znodes_to_commit(struct ubifs_info *c)
{
	struct ubifs_znode *znode, *cnext;
	int cnt = 0;

	c->cnext = find_first_dirty(c->zroot.znode);
	znode = c->enext = c->cnext;
	if (!znode) {
		dbg_cmt("no znodes to commit");
		return 0;
	}
	cnt += 1;
	while (1) {
		ubifs_assert(!ubifs_zn_cow(znode));
		__set_bit(COW_ZNODE, &znode->flags);
		znode->alt = 0;
		cnext = find_next_dirty(znode);
		if (!cnext) {
			znode->cnext = c->cnext;
			break;
		}
		znode->cnext = cnext;
		znode = cnext;
		cnt += 1;
	}
	dbg_cmt("committing %d znodes", cnt);
	ubifs_assert(cnt == atomic_long_read(&c->dirty_zn_cnt));
	return cnt;
}

/**
 * alloc_idx_lebs - allocate empty LEBs to be used to commit.
 * @c: UBIFS file-system description object
 * @cnt: number of znodes to commit
 *
 * This function returns %-ENOSPC if it cannot allocate a sufficient number of
 * empty LEBs.  %0 is returned on success, otherwise a negative error code
 * is returned.
 */
static int alloc_idx_lebs(struct ubifs_info *c, int cnt)
{
	int i, leb_cnt, lnum;

	c->ileb_cnt = 0;
	c->ileb_nxt = 0;
	leb_cnt = get_leb_cnt(c, cnt);
	dbg_cmt("need about %d empty LEBS for TNC commit", leb_cnt);
	if (!leb_cnt)
		return 0;
	c->ilebs = kmalloc(leb_cnt * sizeof(int), GFP_NOFS);
	if (!c->ilebs)
		return -ENOMEM;
	for (i = 0; i < leb_cnt; i++) {
		lnum = ubifs_find_free_leb_for_idx(c);
		if (lnum < 0)
			return lnum;
		c->ilebs[c->ileb_cnt++] = lnum;
		dbg_cmt("LEB %d", lnum);
	}
	if (dbg_is_chk_index(c) && !(prandom_u32() & 7))
		return -ENOSPC;
	return 0;
}

/**
 * free_unused_idx_lebs - free unused LEBs that were allocated for the commit.
 * @c: UBIFS file-system description object
 *
 * It is possible that we allocate more empty LEBs for the commit than we need.
 * This functions frees the surplus.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int free_unused_idx_lebs(struct ubifs_info *c)
{
	int i, err = 0, lnum, er;

	for (i = c->ileb_nxt; i < c->ileb_cnt; i++) {
		lnum = c->ilebs[i];
		dbg_cmt("LEB %d", lnum);
		er = ubifs_change_one_lp(c, lnum, LPROPS_NC, LPROPS_NC, 0,
					 LPROPS_INDEX | LPROPS_TAKEN, 0);
		if (!err)
			err = er;
	}
	return err;
}

/**
 * free_idx_lebs - free unused LEBs after commit end.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int free_idx_lebs(struct ubifs_info *c)
{
	int err;

	err = free_unused_idx_lebs(c);
	kfree(c->ilebs);
	c->ilebs = NULL;
	return err;
}

/**
 * ubifs_tnc_start_commit - start TNC commit.
 * @c: UBIFS file-system description object
 * @zroot: new index root position is returned here
 *
 * This function prepares the list of indexing nodes to commit and lays out
 * their positions on flash. If there is not enough free space it uses the
 * in-gap commit method. Returns zero in case of success and a negative error
 * code in case of failure.
 */
int ubifs_tnc_start_commit(struct ubifs_info *c, struct ubifs_zbranch *zroot)
{
	int err = 0, cnt;

	mutex_lock(&c->tnc_mutex);
	err = dbg_check_tnc(c, 1);
	if (err)
		goto out;
	cnt = get_znodes_to_commit(c);
	if (cnt != 0) {
		int no_space = 0;

		err = alloc_idx_lebs(c, cnt);
		if (err == -ENOSPC)
			no_space = 1;
		else if (err)
			goto out_free;
		err = layout_commit(c, no_space, cnt);
		if (err)
			goto out_free;
		ubifs_assert(atomic_long_read(&c->dirty_zn_cnt) == 0);
		err = free_unused_idx_lebs(c);
		if (err)
			goto out;
	}
	destroy_old_idx(c);
	memcpy(zroot, &c->zroot, sizeof(struct ubifs_zbranch));

	err = ubifs_save_dirty_idx_lnums(c);
	if (err)
		goto out;

	spin_lock(&c->space_lock);
	/*
	 * Although we have not finished committing yet, update size of the
	 * committed index ('c->bi.old_idx_sz') and zero out the index growth
	 * budget. It is OK to do this now, because we've reserved all the
	 * space which is needed to commit the index, and it is save for the
	 * budgeting subsystem to assume the index is already committed,
	 * even though it is not.
	 */
	ubifs_assert(c->bi.min_idx_lebs == ubifs_calc_min_idx_lebs(c));
	c->bi.old_idx_sz = c->calc_idx_sz;
	c->bi.uncommitted_idx = 0;
	c->bi.min_idx_lebs = ubifs_calc_min_idx_lebs(c);
	spin_unlock(&c->space_lock);
	mutex_unlock(&c->tnc_mutex);

	dbg_cmt("number of index LEBs %d", c->lst.idx_lebs);
	dbg_cmt("size of index %llu", c->calc_idx_sz);
	return err;

out_free:
	free_idx_lebs(c);
out:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * write_index - write index nodes.
 * @c: UBIFS file-system description object
 *
 * This function writes the index nodes whose positions were laid out in the
 * layout_in_empty_space function.
 */
static int write_index(struct ubifs_info *c)
{
	struct ubifs_idx_node *idx;
	struct ubifs_znode *znode, *cnext;
	int i, lnum, offs, len, next_len, buf_len, buf_offs, used;
	int avail, wlen, err, lnum_pos = 0, blen, nxt_offs;

	cnext = c->enext;
	if (!cnext)
		return 0;

	/*
	 * Always write index nodes to the index head so that index nodes and
	 * other types of nodes are never mixed in the same erase block.
	 */
	lnum = c->ihead_lnum;
	buf_offs = c->ihead_offs;

	/* Allocate commit buffer */
	buf_len = ALIGN(c->max_idx_node_sz, c->min_io_size);
	used = 0;
	avail = buf_len;

	/* Ensure there is enough room for first write */
	next_len = ubifs_idx_node_sz(c, cnext->child_cnt);
	if (buf_offs + next_len > c->leb_size) {
		err = ubifs_update_one_lp(c, lnum, LPROPS_NC, 0, 0,
					  LPROPS_TAKEN);
		if (err)
			return err;
		lnum = -1;
	}

	while (1) {
		cond_resched();

		znode = cnext;
		idx = c->cbuf + used;

		/* Make index node */
		idx->ch.node_type = UBIFS_IDX_NODE;
		idx->child_cnt = cpu_to_le16(znode->child_cnt);
		idx->level = cpu_to_le16(znode->level);
		for (i = 0; i < znode->child_cnt; i++) {
			struct ubifs_branch *br = ubifs_idx_branch(c, idx, i);
			struct ubifs_zbranch *zbr = &znode->zbranch[i];

			key_write_idx(c, &zbr->key, &br->key);
			br->lnum = cpu_to_le32(zbr->lnum);
			br->offs = cpu_to_le32(zbr->offs);
			br->len = cpu_to_le32(zbr->len);
			if (!zbr->lnum || !zbr->len) {
				ubifs_err("bad ref in znode");
				ubifs_dump_znode(c, znode);
				if (zbr->znode)
					ubifs_dump_znode(c, zbr->znode);
			}
		}
		len = ubifs_idx_node_sz(c, znode->child_cnt);
		ubifs_prepare_node(c, idx, len, 0);

		/* Determine the index node position */
		if (lnum == -1) {
			lnum = c->ilebs[lnum_pos++];
			buf_offs = 0;
			used = 0;
			avail = buf_len;
		}
		offs = buf_offs + used;

		if (lnum != znode->lnum || offs != znode->offs ||
		    len != znode->len) {
			ubifs_err("inconsistent znode posn");
			return -EINVAL;
		}

		/* Grab some stuff from znode while we still can */
		cnext = znode->cnext;

		ubifs_assert(ubifs_zn_dirty(znode));
		ubifs_assert(ubifs_zn_cow(znode));

		/*
		 * It is important that other threads should see %DIRTY_ZNODE
		 * flag cleared before %COW_ZNODE. Specifically, it matters in
		 * the 'dirty_cow_znode()' function. This is the reason for the
		 * first barrier. Also, we want the bit changes to be seen to
		 * other threads ASAP, to avoid unnecesarry copying, which is
		 * the reason for the second barrier.
		 */
		clear_bit(DIRTY_ZNODE, &znode->flags);
		smp_mb__before_atomic();
		clear_bit(COW_ZNODE, &znode->flags);
		smp_mb__after_atomic();

		/*
		 * We have marked the znode as clean but have not updated the
		 * @c->clean_zn_cnt counter. If this znode becomes dirty again
		 * before 'free_obsolete_znodes()' is called, then
		 * @c->clean_zn_cnt will be decremented before it gets
		 * incremented (resulting in 2 decrements for the same znode).
		 * This means that @c->clean_zn_cnt may become negative for a
		 * while.
		 *
		 * Q: why we cannot increment @c->clean_zn_cnt?
		 * A: because we do not have the @c->tnc_mutex locked, and the
		 *    following code would be racy and buggy:
		 *
		 *    if (!ubifs_zn_obsolete(znode)) {
		 *            atomic_long_inc(&c->clean_zn_cnt);
		 *            atomic_long_inc(&ubifs_clean_zn_cnt);
		 *    }
		 *
		 *    Thus, we just delay the @c->clean_zn_cnt update until we
		 *    have the mutex locked.
		 */

		/* Do not access znode from this point on */

		/* Update buffer positions */
		wlen = used + len;
		used += ALIGN(len, 8);
		avail -= ALIGN(len, 8);

		/*
		 * Calculate the next index node length to see if there is
		 * enough room for it
		 */
		if (cnext == c->cnext)
			next_len = 0;
		else
			next_len = ubifs_idx_node_sz(c, cnext->child_cnt);

		nxt_offs = buf_offs + used + next_len;
		if (next_len && nxt_offs <= c->leb_size) {
			if (avail > 0)
				continue;
			else
				blen = buf_len;
		} else {
			wlen = ALIGN(wlen, 8);
			blen = ALIGN(wlen, c->min_io_size);
			ubifs_pad(c, c->cbuf + wlen, blen - wlen);
		}

		/* The buffer is full or there are no more znodes to do */
		err = ubifs_leb_write(c, lnum, c->cbuf, buf_offs, blen);
		if (err)
			return err;
		buf_offs += blen;
		if (next_len) {
			if (nxt_offs > c->leb_size) {
				err = ubifs_update_one_lp(c, lnum, LPROPS_NC, 0,
							  0, LPROPS_TAKEN);
				if (err)
					return err;
				lnum = -1;
			}
			used -= blen;
			if (used < 0)
				used = 0;
			avail = buf_len - used;
			memmove(c->cbuf, c->cbuf + blen, used);
			continue;
		}
		break;
	}

	if (lnum != c->dbg->new_ihead_lnum ||
	    buf_offs != c->dbg->new_ihead_offs) {
		ubifs_err("inconsistent ihead");
		return -EINVAL;
	}

	c->ihead_lnum = lnum;
	c->ihead_offs = buf_offs;

	return 0;
}

/**
 * free_obsolete_znodes - free obsolete znodes.
 * @c: UBIFS file-system description object
 *
 * At the end of commit end, obsolete znodes are freed.
 */
static void free_obsolete_znodes(struct ubifs_info *c)
{
	struct ubifs_znode *znode, *cnext;

	cnext = c->cnext;
	do {
		znode = cnext;
		cnext = znode->cnext;
		if (ubifs_zn_obsolete(znode))
			kfree(znode);
		else {
			znode->cnext = NULL;
			atomic_long_inc(&c->clean_zn_cnt);
			atomic_long_inc(&ubifs_clean_zn_cnt);
		}
	} while (cnext != c->cnext);
}

/**
 * return_gap_lebs - return LEBs used by the in-gap commit method.
 * @c: UBIFS file-system description object
 *
 * This function clears the "taken" flag for the LEBs which were used by the
 * "commit in-the-gaps" method.
 */
static int return_gap_lebs(struct ubifs_info *c)
{
	int *p, err;

	if (!c->gap_lebs)
		return 0;

	dbg_cmt("");
	for (p = c->gap_lebs; *p != -1; p++) {
		err = ubifs_change_one_lp(c, *p, LPROPS_NC, LPROPS_NC, 0,
					  LPROPS_TAKEN, 0);
		if (err)
			return err;
	}

	kfree(c->gap_lebs);
	c->gap_lebs = NULL;
	return 0;
}

/**
 * ubifs_tnc_end_commit - update the TNC for commit end.
 * @c: UBIFS file-system description object
 *
 * Write the dirty znodes.
 */
int ubifs_tnc_end_commit(struct ubifs_info *c)
{
	int err;

	if (!c->cnext)
		return 0;

	err = return_gap_lebs(c);
	if (err)
		return err;

	err = write_index(c);
	if (err)
		return err;

	mutex_lock(&c->tnc_mutex);

	dbg_cmt("TNC height is %d", c->zroot.znode->level + 1);

	free_obsolete_znodes(c);

	c->cnext = NULL;
	kfree(c->ilebs);
	c->ilebs = NULL;

	mutex_unlock(&c->tnc_mutex);

	return 0;
}

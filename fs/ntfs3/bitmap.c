// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 * This code builds two trees of free clusters extents.
 * Trees are sorted by start of extent and by length of extent.
 * NTFS_MAX_WND_EXTENTS defines the maximum number of elements in trees.
 * In extreme case code reads on-disk bitmap to find free clusters.
 *
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/nls.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * Maximum number of extents in tree.
 */
#define NTFS_MAX_WND_EXTENTS (32u * 1024u)

struct rb_node_key {
	struct rb_node node;
	size_t key;
};

struct e_node {
	struct rb_node_key start; /* Tree sorted by start. */
	struct rb_node_key count; /* Tree sorted by len. */
};

static int wnd_rescan(struct wnd_bitmap *wnd);
static struct buffer_head *wnd_map(struct wnd_bitmap *wnd, size_t iw);
static bool wnd_is_free_hlp(struct wnd_bitmap *wnd, size_t bit, size_t bits);

static struct kmem_cache *ntfs_enode_cachep;

int __init ntfs3_init_bitmap(void)
{
	ntfs_enode_cachep =
		kmem_cache_create("ntfs3_enode_cache", sizeof(struct e_node), 0,
				  SLAB_RECLAIM_ACCOUNT, NULL);
	return ntfs_enode_cachep ? 0 : -ENOMEM;
}

void ntfs3_exit_bitmap(void)
{
	kmem_cache_destroy(ntfs_enode_cachep);
}

static inline u32 wnd_bits(const struct wnd_bitmap *wnd, size_t i)
{
	return i + 1 == wnd->nwnd ? wnd->bits_last : wnd->sb->s_blocksize * 8;
}

/*
 * wnd_scan
 *
 * b_pos + b_len - biggest fragment.
 * Scan range [wpos wbits) window @buf.
 *
 * Return: -1 if not found.
 */
static size_t wnd_scan(const ulong *buf, size_t wbit, u32 wpos, u32 wend,
		       size_t to_alloc, size_t *prev_tail, size_t *b_pos,
		       size_t *b_len)
{
	while (wpos < wend) {
		size_t free_len;
		u32 free_bits, end;
		u32 used = find_next_zero_bit(buf, wend, wpos);

		if (used >= wend) {
			if (*b_len < *prev_tail) {
				*b_pos = wbit - *prev_tail;
				*b_len = *prev_tail;
			}

			*prev_tail = 0;
			return -1;
		}

		if (used > wpos) {
			wpos = used;
			if (*b_len < *prev_tail) {
				*b_pos = wbit - *prev_tail;
				*b_len = *prev_tail;
			}

			*prev_tail = 0;
		}

		/*
		 * Now we have a fragment [wpos, wend) staring with 0.
		 */
		end = wpos + to_alloc - *prev_tail;
		free_bits = find_next_bit(buf, min(end, wend), wpos);

		free_len = *prev_tail + free_bits - wpos;

		if (*b_len < free_len) {
			*b_pos = wbit + wpos - *prev_tail;
			*b_len = free_len;
		}

		if (free_len >= to_alloc)
			return wbit + wpos - *prev_tail;

		if (free_bits >= wend) {
			*prev_tail += free_bits - wpos;
			return -1;
		}

		wpos = free_bits + 1;

		*prev_tail = 0;
	}

	return -1;
}

/*
 * wnd_close - Frees all resources.
 */
void wnd_close(struct wnd_bitmap *wnd)
{
	struct rb_node *node, *next;

	kfree(wnd->free_bits);
	run_close(&wnd->run);

	node = rb_first(&wnd->start_tree);

	while (node) {
		next = rb_next(node);
		rb_erase(node, &wnd->start_tree);
		kmem_cache_free(ntfs_enode_cachep,
				rb_entry(node, struct e_node, start.node));
		node = next;
	}
}

static struct rb_node *rb_lookup(struct rb_root *root, size_t v)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *r = NULL;

	while (*p) {
		struct rb_node_key *k;

		k = rb_entry(*p, struct rb_node_key, node);
		if (v < k->key) {
			p = &(*p)->rb_left;
		} else if (v > k->key) {
			r = &k->node;
			p = &(*p)->rb_right;
		} else {
			return &k->node;
		}
	}

	return r;
}

/*
 * rb_insert_count - Helper function to insert special kind of 'count' tree.
 */
static inline bool rb_insert_count(struct rb_root *root, struct e_node *e)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	size_t e_ckey = e->count.key;
	size_t e_skey = e->start.key;

	while (*p) {
		struct e_node *k =
			rb_entry(parent = *p, struct e_node, count.node);

		if (e_ckey > k->count.key) {
			p = &(*p)->rb_left;
		} else if (e_ckey < k->count.key) {
			p = &(*p)->rb_right;
		} else if (e_skey < k->start.key) {
			p = &(*p)->rb_left;
		} else if (e_skey > k->start.key) {
			p = &(*p)->rb_right;
		} else {
			WARN_ON(1);
			return false;
		}
	}

	rb_link_node(&e->count.node, parent, p);
	rb_insert_color(&e->count.node, root);
	return true;
}

/*
 * rb_insert_start - Helper function to insert special kind of 'count' tree.
 */
static inline bool rb_insert_start(struct rb_root *root, struct e_node *e)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	size_t e_skey = e->start.key;

	while (*p) {
		struct e_node *k;

		parent = *p;

		k = rb_entry(parent, struct e_node, start.node);
		if (e_skey < k->start.key) {
			p = &(*p)->rb_left;
		} else if (e_skey > k->start.key) {
			p = &(*p)->rb_right;
		} else {
			WARN_ON(1);
			return false;
		}
	}

	rb_link_node(&e->start.node, parent, p);
	rb_insert_color(&e->start.node, root);
	return true;
}

/*
 * wnd_add_free_ext - Adds a new extent of free space.
 * @build:	1 when building tree.
 */
static void wnd_add_free_ext(struct wnd_bitmap *wnd, size_t bit, size_t len,
			     bool build)
{
	struct e_node *e, *e0 = NULL;
	size_t ib, end_in = bit + len;
	struct rb_node *n;

	if (build) {
		/* Use extent_min to filter too short extents. */
		if (wnd->count >= NTFS_MAX_WND_EXTENTS &&
		    len <= wnd->extent_min) {
			wnd->uptodated = -1;
			return;
		}
	} else {
		/* Try to find extent before 'bit'. */
		n = rb_lookup(&wnd->start_tree, bit);

		if (!n) {
			n = rb_first(&wnd->start_tree);
		} else {
			e = rb_entry(n, struct e_node, start.node);
			n = rb_next(n);
			if (e->start.key + e->count.key == bit) {
				/* Remove left. */
				bit = e->start.key;
				len += e->count.key;
				rb_erase(&e->start.node, &wnd->start_tree);
				rb_erase(&e->count.node, &wnd->count_tree);
				wnd->count -= 1;
				e0 = e;
			}
		}

		while (n) {
			size_t next_end;

			e = rb_entry(n, struct e_node, start.node);
			next_end = e->start.key + e->count.key;
			if (e->start.key > end_in)
				break;

			/* Remove right. */
			n = rb_next(n);
			len += next_end - end_in;
			end_in = next_end;
			rb_erase(&e->start.node, &wnd->start_tree);
			rb_erase(&e->count.node, &wnd->count_tree);
			wnd->count -= 1;

			if (!e0)
				e0 = e;
			else
				kmem_cache_free(ntfs_enode_cachep, e);
		}

		if (wnd->uptodated != 1) {
			/* Check bits before 'bit'. */
			ib = wnd->zone_bit == wnd->zone_end ||
					     bit < wnd->zone_end
				     ? 0
				     : wnd->zone_end;

			while (bit > ib && wnd_is_free_hlp(wnd, bit - 1, 1)) {
				bit -= 1;
				len += 1;
			}

			/* Check bits after 'end_in'. */
			ib = wnd->zone_bit == wnd->zone_end ||
					     end_in > wnd->zone_bit
				     ? wnd->nbits
				     : wnd->zone_bit;

			while (end_in < ib && wnd_is_free_hlp(wnd, end_in, 1)) {
				end_in += 1;
				len += 1;
			}
		}
	}
	/* Insert new fragment. */
	if (wnd->count >= NTFS_MAX_WND_EXTENTS) {
		if (e0)
			kmem_cache_free(ntfs_enode_cachep, e0);

		wnd->uptodated = -1;

		/* Compare with smallest fragment. */
		n = rb_last(&wnd->count_tree);
		e = rb_entry(n, struct e_node, count.node);
		if (len <= e->count.key)
			goto out; /* Do not insert small fragments. */

		if (build) {
			struct e_node *e2;

			n = rb_prev(n);
			e2 = rb_entry(n, struct e_node, count.node);
			/* Smallest fragment will be 'e2->count.key'. */
			wnd->extent_min = e2->count.key;
		}

		/* Replace smallest fragment by new one. */
		rb_erase(&e->start.node, &wnd->start_tree);
		rb_erase(&e->count.node, &wnd->count_tree);
		wnd->count -= 1;
	} else {
		e = e0 ? e0 : kmem_cache_alloc(ntfs_enode_cachep, GFP_ATOMIC);
		if (!e) {
			wnd->uptodated = -1;
			goto out;
		}

		if (build && len <= wnd->extent_min)
			wnd->extent_min = len;
	}
	e->start.key = bit;
	e->count.key = len;
	if (len > wnd->extent_max)
		wnd->extent_max = len;

	rb_insert_start(&wnd->start_tree, e);
	rb_insert_count(&wnd->count_tree, e);
	wnd->count += 1;

out:;
}

/*
 * wnd_remove_free_ext - Remove a run from the cached free space.
 */
static void wnd_remove_free_ext(struct wnd_bitmap *wnd, size_t bit, size_t len)
{
	struct rb_node *n, *n3;
	struct e_node *e, *e3;
	size_t end_in = bit + len;
	size_t end3, end, new_key, new_len, max_new_len;

	/* Try to find extent before 'bit'. */
	n = rb_lookup(&wnd->start_tree, bit);

	if (!n)
		return;

	e = rb_entry(n, struct e_node, start.node);
	end = e->start.key + e->count.key;

	new_key = new_len = 0;
	len = e->count.key;

	/* Range [bit,end_in) must be inside 'e' or outside 'e' and 'n'. */
	if (e->start.key > bit)
		;
	else if (end_in <= end) {
		/* Range [bit,end_in) inside 'e'. */
		new_key = end_in;
		new_len = end - end_in;
		len = bit - e->start.key;
	} else if (bit > end) {
		bool bmax = false;

		n3 = rb_next(n);

		while (n3) {
			e3 = rb_entry(n3, struct e_node, start.node);
			if (e3->start.key >= end_in)
				break;

			if (e3->count.key == wnd->extent_max)
				bmax = true;

			end3 = e3->start.key + e3->count.key;
			if (end3 > end_in) {
				e3->start.key = end_in;
				rb_erase(&e3->count.node, &wnd->count_tree);
				e3->count.key = end3 - end_in;
				rb_insert_count(&wnd->count_tree, e3);
				break;
			}

			n3 = rb_next(n3);
			rb_erase(&e3->start.node, &wnd->start_tree);
			rb_erase(&e3->count.node, &wnd->count_tree);
			wnd->count -= 1;
			kmem_cache_free(ntfs_enode_cachep, e3);
		}
		if (!bmax)
			return;
		n3 = rb_first(&wnd->count_tree);
		wnd->extent_max =
			n3 ? rb_entry(n3, struct e_node, count.node)->count.key
			   : 0;
		return;
	}

	if (e->count.key != wnd->extent_max) {
		;
	} else if (rb_prev(&e->count.node)) {
		;
	} else {
		n3 = rb_next(&e->count.node);
		max_new_len = len > new_len ? len : new_len;
		if (!n3) {
			wnd->extent_max = max_new_len;
		} else {
			e3 = rb_entry(n3, struct e_node, count.node);
			wnd->extent_max = max(e3->count.key, max_new_len);
		}
	}

	if (!len) {
		if (new_len) {
			e->start.key = new_key;
			rb_erase(&e->count.node, &wnd->count_tree);
			e->count.key = new_len;
			rb_insert_count(&wnd->count_tree, e);
		} else {
			rb_erase(&e->start.node, &wnd->start_tree);
			rb_erase(&e->count.node, &wnd->count_tree);
			wnd->count -= 1;
			kmem_cache_free(ntfs_enode_cachep, e);
		}
		goto out;
	}
	rb_erase(&e->count.node, &wnd->count_tree);
	e->count.key = len;
	rb_insert_count(&wnd->count_tree, e);

	if (!new_len)
		goto out;

	if (wnd->count >= NTFS_MAX_WND_EXTENTS) {
		wnd->uptodated = -1;

		/* Get minimal extent. */
		e = rb_entry(rb_last(&wnd->count_tree), struct e_node,
			     count.node);
		if (e->count.key > new_len)
			goto out;

		/* Replace minimum. */
		rb_erase(&e->start.node, &wnd->start_tree);
		rb_erase(&e->count.node, &wnd->count_tree);
		wnd->count -= 1;
	} else {
		e = kmem_cache_alloc(ntfs_enode_cachep, GFP_ATOMIC);
		if (!e)
			wnd->uptodated = -1;
	}

	if (e) {
		e->start.key = new_key;
		e->count.key = new_len;
		rb_insert_start(&wnd->start_tree, e);
		rb_insert_count(&wnd->count_tree, e);
		wnd->count += 1;
	}

out:
	if (!wnd->count && 1 != wnd->uptodated)
		wnd_rescan(wnd);
}

/*
 * wnd_rescan - Scan all bitmap. Used while initialization.
 */
static int wnd_rescan(struct wnd_bitmap *wnd)
{
	int err = 0;
	size_t prev_tail = 0;
	struct super_block *sb = wnd->sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	u64 lbo, len = 0;
	u32 blocksize = sb->s_blocksize;
	u8 cluster_bits = sbi->cluster_bits;
	u32 wbits = 8 * sb->s_blocksize;
	u32 used, frb;
	const ulong *buf;
	size_t wpos, wbit, iw, vbo;
	struct buffer_head *bh = NULL;
	CLST lcn, clen;

	wnd->uptodated = 0;
	wnd->extent_max = 0;
	wnd->extent_min = MINUS_ONE_T;
	wnd->total_zeroes = 0;

	vbo = 0;

	for (iw = 0; iw < wnd->nwnd; iw++) {
		if (iw + 1 == wnd->nwnd)
			wbits = wnd->bits_last;

		if (wnd->inited) {
			if (!wnd->free_bits[iw]) {
				/* All ones. */
				if (prev_tail) {
					wnd_add_free_ext(wnd,
							 vbo * 8 - prev_tail,
							 prev_tail, true);
					prev_tail = 0;
				}
				goto next_wnd;
			}
			if (wbits == wnd->free_bits[iw]) {
				/* All zeroes. */
				prev_tail += wbits;
				wnd->total_zeroes += wbits;
				goto next_wnd;
			}
		}

		if (!len) {
			u32 off = vbo & sbi->cluster_mask;

			if (!run_lookup_entry(&wnd->run, vbo >> cluster_bits,
					      &lcn, &clen, NULL)) {
				err = -ENOENT;
				goto out;
			}

			lbo = ((u64)lcn << cluster_bits) + off;
			len = ((u64)clen << cluster_bits) - off;
		}

		bh = ntfs_bread(sb, lbo >> sb->s_blocksize_bits);
		if (!bh) {
			err = -EIO;
			goto out;
		}

		buf = (ulong *)bh->b_data;

		used = __bitmap_weight(buf, wbits);
		if (used < wbits) {
			frb = wbits - used;
			wnd->free_bits[iw] = frb;
			wnd->total_zeroes += frb;
		}

		wpos = 0;
		wbit = vbo * 8;

		if (wbit + wbits > wnd->nbits)
			wbits = wnd->nbits - wbit;

		do {
			used = find_next_zero_bit(buf, wbits, wpos);

			if (used > wpos && prev_tail) {
				wnd_add_free_ext(wnd, wbit + wpos - prev_tail,
						 prev_tail, true);
				prev_tail = 0;
			}

			wpos = used;

			if (wpos >= wbits) {
				/* No free blocks. */
				prev_tail = 0;
				break;
			}

			frb = find_next_bit(buf, wbits, wpos);
			if (frb >= wbits) {
				/* Keep last free block. */
				prev_tail += frb - wpos;
				break;
			}

			wnd_add_free_ext(wnd, wbit + wpos - prev_tail,
					 frb + prev_tail - wpos, true);

			/* Skip free block and first '1'. */
			wpos = frb + 1;
			/* Reset previous tail. */
			prev_tail = 0;
		} while (wpos < wbits);

next_wnd:

		if (bh)
			put_bh(bh);
		bh = NULL;

		vbo += blocksize;
		if (len) {
			len -= blocksize;
			lbo += blocksize;
		}
	}

	/* Add last block. */
	if (prev_tail)
		wnd_add_free_ext(wnd, wnd->nbits - prev_tail, prev_tail, true);

	/*
	 * Before init cycle wnd->uptodated was 0.
	 * If any errors or limits occurs while initialization then
	 * wnd->uptodated will be -1.
	 * If 'uptodated' is still 0 then Tree is really updated.
	 */
	if (!wnd->uptodated)
		wnd->uptodated = 1;

	if (wnd->zone_bit != wnd->zone_end) {
		size_t zlen = wnd->zone_end - wnd->zone_bit;

		wnd->zone_end = wnd->zone_bit;
		wnd_zone_set(wnd, wnd->zone_bit, zlen);
	}

out:
	return err;
}

int wnd_init(struct wnd_bitmap *wnd, struct super_block *sb, size_t nbits)
{
	int err;
	u32 blocksize = sb->s_blocksize;
	u32 wbits = blocksize * 8;

	init_rwsem(&wnd->rw_lock);

	wnd->sb = sb;
	wnd->nbits = nbits;
	wnd->total_zeroes = nbits;
	wnd->extent_max = MINUS_ONE_T;
	wnd->zone_bit = wnd->zone_end = 0;
	wnd->nwnd = bytes_to_block(sb, bitmap_size(nbits));
	wnd->bits_last = nbits & (wbits - 1);
	if (!wnd->bits_last)
		wnd->bits_last = wbits;

	wnd->free_bits = kcalloc(wnd->nwnd, sizeof(u16), GFP_NOFS);
	if (!wnd->free_bits)
		return -ENOMEM;

	err = wnd_rescan(wnd);
	if (err)
		return err;

	wnd->inited = true;

	return 0;
}

/*
 * wnd_map - Call sb_bread for requested window.
 */
static struct buffer_head *wnd_map(struct wnd_bitmap *wnd, size_t iw)
{
	size_t vbo;
	CLST lcn, clen;
	struct super_block *sb = wnd->sb;
	struct ntfs_sb_info *sbi;
	struct buffer_head *bh;
	u64 lbo;

	sbi = sb->s_fs_info;
	vbo = (u64)iw << sb->s_blocksize_bits;

	if (!run_lookup_entry(&wnd->run, vbo >> sbi->cluster_bits, &lcn, &clen,
			      NULL)) {
		return ERR_PTR(-ENOENT);
	}

	lbo = ((u64)lcn << sbi->cluster_bits) + (vbo & sbi->cluster_mask);

	bh = ntfs_bread(wnd->sb, lbo >> sb->s_blocksize_bits);
	if (!bh)
		return ERR_PTR(-EIO);

	return bh;
}

/*
 * wnd_set_free - Mark the bits range from bit to bit + bits as free.
 */
int wnd_set_free(struct wnd_bitmap *wnd, size_t bit, size_t bits)
{
	int err = 0;
	struct super_block *sb = wnd->sb;
	size_t bits0 = bits;
	u32 wbits = 8 * sb->s_blocksize;
	size_t iw = bit >> (sb->s_blocksize_bits + 3);
	u32 wbit = bit & (wbits - 1);
	struct buffer_head *bh;

	while (iw < wnd->nwnd && bits) {
		u32 tail, op;
		ulong *buf;

		if (iw + 1 == wnd->nwnd)
			wbits = wnd->bits_last;

		tail = wbits - wbit;
		op = tail < bits ? tail : bits;

		bh = wnd_map(wnd, iw);
		if (IS_ERR(bh)) {
			err = PTR_ERR(bh);
			break;
		}

		buf = (ulong *)bh->b_data;

		lock_buffer(bh);

		__bitmap_clear(buf, wbit, op);

		wnd->free_bits[iw] += op;

		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		put_bh(bh);

		wnd->total_zeroes += op;
		bits -= op;
		wbit = 0;
		iw += 1;
	}

	wnd_add_free_ext(wnd, bit, bits0, false);

	return err;
}

/*
 * wnd_set_used - Mark the bits range from bit to bit + bits as used.
 */
int wnd_set_used(struct wnd_bitmap *wnd, size_t bit, size_t bits)
{
	int err = 0;
	struct super_block *sb = wnd->sb;
	size_t bits0 = bits;
	size_t iw = bit >> (sb->s_blocksize_bits + 3);
	u32 wbits = 8 * sb->s_blocksize;
	u32 wbit = bit & (wbits - 1);
	struct buffer_head *bh;

	while (iw < wnd->nwnd && bits) {
		u32 tail, op;
		ulong *buf;

		if (unlikely(iw + 1 == wnd->nwnd))
			wbits = wnd->bits_last;

		tail = wbits - wbit;
		op = tail < bits ? tail : bits;

		bh = wnd_map(wnd, iw);
		if (IS_ERR(bh)) {
			err = PTR_ERR(bh);
			break;
		}
		buf = (ulong *)bh->b_data;

		lock_buffer(bh);

		__bitmap_set(buf, wbit, op);
		wnd->free_bits[iw] -= op;

		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		put_bh(bh);

		wnd->total_zeroes -= op;
		bits -= op;
		wbit = 0;
		iw += 1;
	}

	if (!RB_EMPTY_ROOT(&wnd->start_tree))
		wnd_remove_free_ext(wnd, bit, bits0);

	return err;
}

/*
 * wnd_is_free_hlp
 *
 * Return: True if all clusters [bit, bit+bits) are free (bitmap only).
 */
static bool wnd_is_free_hlp(struct wnd_bitmap *wnd, size_t bit, size_t bits)
{
	struct super_block *sb = wnd->sb;
	size_t iw = bit >> (sb->s_blocksize_bits + 3);
	u32 wbits = 8 * sb->s_blocksize;
	u32 wbit = bit & (wbits - 1);

	while (iw < wnd->nwnd && bits) {
		u32 tail, op;

		if (unlikely(iw + 1 == wnd->nwnd))
			wbits = wnd->bits_last;

		tail = wbits - wbit;
		op = tail < bits ? tail : bits;

		if (wbits != wnd->free_bits[iw]) {
			bool ret;
			struct buffer_head *bh = wnd_map(wnd, iw);

			if (IS_ERR(bh))
				return false;

			ret = are_bits_clear((ulong *)bh->b_data, wbit, op);

			put_bh(bh);
			if (!ret)
				return false;
		}

		bits -= op;
		wbit = 0;
		iw += 1;
	}

	return true;
}

/*
 * wnd_is_free
 *
 * Return: True if all clusters [bit, bit+bits) are free.
 */
bool wnd_is_free(struct wnd_bitmap *wnd, size_t bit, size_t bits)
{
	bool ret;
	struct rb_node *n;
	size_t end;
	struct e_node *e;

	if (RB_EMPTY_ROOT(&wnd->start_tree))
		goto use_wnd;

	n = rb_lookup(&wnd->start_tree, bit);
	if (!n)
		goto use_wnd;

	e = rb_entry(n, struct e_node, start.node);

	end = e->start.key + e->count.key;

	if (bit < end && bit + bits <= end)
		return true;

use_wnd:
	ret = wnd_is_free_hlp(wnd, bit, bits);

	return ret;
}

/*
 * wnd_is_used
 *
 * Return: True if all clusters [bit, bit+bits) are used.
 */
bool wnd_is_used(struct wnd_bitmap *wnd, size_t bit, size_t bits)
{
	bool ret = false;
	struct super_block *sb = wnd->sb;
	size_t iw = bit >> (sb->s_blocksize_bits + 3);
	u32 wbits = 8 * sb->s_blocksize;
	u32 wbit = bit & (wbits - 1);
	size_t end;
	struct rb_node *n;
	struct e_node *e;

	if (RB_EMPTY_ROOT(&wnd->start_tree))
		goto use_wnd;

	end = bit + bits;
	n = rb_lookup(&wnd->start_tree, end - 1);
	if (!n)
		goto use_wnd;

	e = rb_entry(n, struct e_node, start.node);
	if (e->start.key + e->count.key > bit)
		return false;

use_wnd:
	while (iw < wnd->nwnd && bits) {
		u32 tail, op;

		if (unlikely(iw + 1 == wnd->nwnd))
			wbits = wnd->bits_last;

		tail = wbits - wbit;
		op = tail < bits ? tail : bits;

		if (wnd->free_bits[iw]) {
			bool ret;
			struct buffer_head *bh = wnd_map(wnd, iw);

			if (IS_ERR(bh))
				goto out;

			ret = are_bits_set((ulong *)bh->b_data, wbit, op);
			put_bh(bh);
			if (!ret)
				goto out;
		}

		bits -= op;
		wbit = 0;
		iw += 1;
	}
	ret = true;

out:
	return ret;
}

/*
 * wnd_find - Look for free space.
 *
 * - flags - BITMAP_FIND_XXX flags
 *
 * Return: 0 if not found.
 */
size_t wnd_find(struct wnd_bitmap *wnd, size_t to_alloc, size_t hint,
		size_t flags, size_t *allocated)
{
	struct super_block *sb;
	u32 wbits, wpos, wzbit, wzend;
	size_t fnd, max_alloc, b_len, b_pos;
	size_t iw, prev_tail, nwnd, wbit, ebit, zbit, zend;
	size_t to_alloc0 = to_alloc;
	const ulong *buf;
	const struct e_node *e;
	const struct rb_node *pr, *cr;
	u8 log2_bits;
	bool fbits_valid;
	struct buffer_head *bh;

	/* Fast checking for available free space. */
	if (flags & BITMAP_FIND_FULL) {
		size_t zeroes = wnd_zeroes(wnd);

		zeroes -= wnd->zone_end - wnd->zone_bit;
		if (zeroes < to_alloc0)
			goto no_space;

		if (to_alloc0 > wnd->extent_max)
			goto no_space;
	} else {
		if (to_alloc > wnd->extent_max)
			to_alloc = wnd->extent_max;
	}

	if (wnd->zone_bit <= hint && hint < wnd->zone_end)
		hint = wnd->zone_end;

	max_alloc = wnd->nbits;
	b_len = b_pos = 0;

	if (hint >= max_alloc)
		hint = 0;

	if (RB_EMPTY_ROOT(&wnd->start_tree)) {
		if (wnd->uptodated == 1) {
			/* Extents tree is updated -> No free space. */
			goto no_space;
		}
		goto scan_bitmap;
	}

	e = NULL;
	if (!hint)
		goto allocate_biggest;

	/* Use hint: Enumerate extents by start >= hint. */
	pr = NULL;
	cr = wnd->start_tree.rb_node;

	for (;;) {
		e = rb_entry(cr, struct e_node, start.node);

		if (e->start.key == hint)
			break;

		if (e->start.key < hint) {
			pr = cr;
			cr = cr->rb_right;
			if (!cr)
				break;
			continue;
		}

		cr = cr->rb_left;
		if (!cr) {
			e = pr ? rb_entry(pr, struct e_node, start.node) : NULL;
			break;
		}
	}

	if (!e)
		goto allocate_biggest;

	if (e->start.key + e->count.key > hint) {
		/* We have found extension with 'hint' inside. */
		size_t len = e->start.key + e->count.key - hint;

		if (len >= to_alloc && hint + to_alloc <= max_alloc) {
			fnd = hint;
			goto found;
		}

		if (!(flags & BITMAP_FIND_FULL)) {
			if (len > to_alloc)
				len = to_alloc;

			if (hint + len <= max_alloc) {
				fnd = hint;
				to_alloc = len;
				goto found;
			}
		}
	}

allocate_biggest:
	/* Allocate from biggest free extent. */
	e = rb_entry(rb_first(&wnd->count_tree), struct e_node, count.node);
	if (e->count.key != wnd->extent_max)
		wnd->extent_max = e->count.key;

	if (e->count.key < max_alloc) {
		if (e->count.key >= to_alloc) {
			;
		} else if (flags & BITMAP_FIND_FULL) {
			if (e->count.key < to_alloc0) {
				/* Biggest free block is less then requested. */
				goto no_space;
			}
			to_alloc = e->count.key;
		} else if (-1 != wnd->uptodated) {
			to_alloc = e->count.key;
		} else {
			/* Check if we can use more bits. */
			size_t op, max_check;
			struct rb_root start_tree;

			memcpy(&start_tree, &wnd->start_tree,
			       sizeof(struct rb_root));
			memset(&wnd->start_tree, 0, sizeof(struct rb_root));

			max_check = e->start.key + to_alloc;
			if (max_check > max_alloc)
				max_check = max_alloc;
			for (op = e->start.key + e->count.key; op < max_check;
			     op++) {
				if (!wnd_is_free(wnd, op, 1))
					break;
			}
			memcpy(&wnd->start_tree, &start_tree,
			       sizeof(struct rb_root));
			to_alloc = op - e->start.key;
		}

		/* Prepare to return. */
		fnd = e->start.key;
		if (e->start.key + to_alloc > max_alloc)
			to_alloc = max_alloc - e->start.key;
		goto found;
	}

	if (wnd->uptodated == 1) {
		/* Extents tree is updated -> no free space. */
		goto no_space;
	}

	b_len = e->count.key;
	b_pos = e->start.key;

scan_bitmap:
	sb = wnd->sb;
	log2_bits = sb->s_blocksize_bits + 3;

	/* At most two ranges [hint, max_alloc) + [0, hint). */
Again:

	/* TODO: Optimize request for case nbits > wbits. */
	iw = hint >> log2_bits;
	wbits = sb->s_blocksize * 8;
	wpos = hint & (wbits - 1);
	prev_tail = 0;
	fbits_valid = true;

	if (max_alloc == wnd->nbits) {
		nwnd = wnd->nwnd;
	} else {
		size_t t = max_alloc + wbits - 1;

		nwnd = likely(t > max_alloc) ? (t >> log2_bits) : wnd->nwnd;
	}

	/* Enumerate all windows. */
	for (; iw < nwnd; iw++) {
		wbit = iw << log2_bits;

		if (!wnd->free_bits[iw]) {
			if (prev_tail > b_len) {
				b_pos = wbit - prev_tail;
				b_len = prev_tail;
			}

			/* Skip full used window. */
			prev_tail = 0;
			wpos = 0;
			continue;
		}

		if (unlikely(iw + 1 == nwnd)) {
			if (max_alloc == wnd->nbits) {
				wbits = wnd->bits_last;
			} else {
				size_t t = max_alloc & (wbits - 1);

				if (t) {
					wbits = t;
					fbits_valid = false;
				}
			}
		}

		if (wnd->zone_end > wnd->zone_bit) {
			ebit = wbit + wbits;
			zbit = max(wnd->zone_bit, wbit);
			zend = min(wnd->zone_end, ebit);

			/* Here we have a window [wbit, ebit) and zone [zbit, zend). */
			if (zend <= zbit) {
				/* Zone does not overlap window. */
			} else {
				wzbit = zbit - wbit;
				wzend = zend - wbit;

				/* Zone overlaps window. */
				if (wnd->free_bits[iw] == wzend - wzbit) {
					prev_tail = 0;
					wpos = 0;
					continue;
				}

				/* Scan two ranges window: [wbit, zbit) and [zend, ebit). */
				bh = wnd_map(wnd, iw);

				if (IS_ERR(bh)) {
					/* TODO: Error */
					prev_tail = 0;
					wpos = 0;
					continue;
				}

				buf = (ulong *)bh->b_data;

				/* Scan range [wbit, zbit). */
				if (wpos < wzbit) {
					/* Scan range [wpos, zbit). */
					fnd = wnd_scan(buf, wbit, wpos, wzbit,
						       to_alloc, &prev_tail,
						       &b_pos, &b_len);
					if (fnd != MINUS_ONE_T) {
						put_bh(bh);
						goto found;
					}
				}

				prev_tail = 0;

				/* Scan range [zend, ebit). */
				if (wzend < wbits) {
					fnd = wnd_scan(buf, wbit,
						       max(wzend, wpos), wbits,
						       to_alloc, &prev_tail,
						       &b_pos, &b_len);
					if (fnd != MINUS_ONE_T) {
						put_bh(bh);
						goto found;
					}
				}

				wpos = 0;
				put_bh(bh);
				continue;
			}
		}

		/* Current window does not overlap zone. */
		if (!wpos && fbits_valid && wnd->free_bits[iw] == wbits) {
			/* Window is empty. */
			if (prev_tail + wbits >= to_alloc) {
				fnd = wbit + wpos - prev_tail;
				goto found;
			}

			/* Increase 'prev_tail' and process next window. */
			prev_tail += wbits;
			wpos = 0;
			continue;
		}

		/* Read window. */
		bh = wnd_map(wnd, iw);
		if (IS_ERR(bh)) {
			// TODO: Error.
			prev_tail = 0;
			wpos = 0;
			continue;
		}

		buf = (ulong *)bh->b_data;

		/* Scan range [wpos, eBits). */
		fnd = wnd_scan(buf, wbit, wpos, wbits, to_alloc, &prev_tail,
			       &b_pos, &b_len);
		put_bh(bh);
		if (fnd != MINUS_ONE_T)
			goto found;
	}

	if (b_len < prev_tail) {
		/* The last fragment. */
		b_len = prev_tail;
		b_pos = max_alloc - prev_tail;
	}

	if (hint) {
		/*
		 * We have scanned range [hint max_alloc).
		 * Prepare to scan range [0 hint + to_alloc).
		 */
		size_t nextmax = hint + to_alloc;

		if (likely(nextmax >= hint) && nextmax < max_alloc)
			max_alloc = nextmax;
		hint = 0;
		goto Again;
	}

	if (!b_len)
		goto no_space;

	wnd->extent_max = b_len;

	if (flags & BITMAP_FIND_FULL)
		goto no_space;

	fnd = b_pos;
	to_alloc = b_len;

found:
	if (flags & BITMAP_FIND_MARK_AS_USED) {
		/* TODO: Optimize remove extent (pass 'e'?). */
		if (wnd_set_used(wnd, fnd, to_alloc))
			goto no_space;
	} else if (wnd->extent_max != MINUS_ONE_T &&
		   to_alloc > wnd->extent_max) {
		wnd->extent_max = to_alloc;
	}

	*allocated = fnd;
	return to_alloc;

no_space:
	return 0;
}

/*
 * wnd_extend - Extend bitmap ($MFT bitmap).
 */
int wnd_extend(struct wnd_bitmap *wnd, size_t new_bits)
{
	int err;
	struct super_block *sb = wnd->sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	u32 blocksize = sb->s_blocksize;
	u32 wbits = blocksize * 8;
	u32 b0, new_last;
	size_t bits, iw, new_wnd;
	size_t old_bits = wnd->nbits;
	u16 *new_free;

	if (new_bits <= old_bits)
		return -EINVAL;

	/* Align to 8 byte boundary. */
	new_wnd = bytes_to_block(sb, bitmap_size(new_bits));
	new_last = new_bits & (wbits - 1);
	if (!new_last)
		new_last = wbits;

	if (new_wnd != wnd->nwnd) {
		new_free = kmalloc(new_wnd * sizeof(u16), GFP_NOFS);
		if (!new_free)
			return -ENOMEM;

		if (new_free != wnd->free_bits)
			memcpy(new_free, wnd->free_bits,
			       wnd->nwnd * sizeof(short));
		memset(new_free + wnd->nwnd, 0,
		       (new_wnd - wnd->nwnd) * sizeof(short));
		kfree(wnd->free_bits);
		wnd->free_bits = new_free;
	}

	/* Zero bits [old_bits,new_bits). */
	bits = new_bits - old_bits;
	b0 = old_bits & (wbits - 1);

	for (iw = old_bits >> (sb->s_blocksize_bits + 3); bits; iw += 1) {
		u32 op;
		size_t frb;
		u64 vbo, lbo, bytes;
		struct buffer_head *bh;
		ulong *buf;

		if (iw + 1 == new_wnd)
			wbits = new_last;

		op = b0 + bits > wbits ? wbits - b0 : bits;
		vbo = (u64)iw * blocksize;

		err = ntfs_vbo_to_lbo(sbi, &wnd->run, vbo, &lbo, &bytes);
		if (err)
			break;

		bh = ntfs_bread(sb, lbo >> sb->s_blocksize_bits);
		if (!bh)
			return -EIO;

		lock_buffer(bh);
		buf = (ulong *)bh->b_data;

		__bitmap_clear(buf, b0, blocksize * 8 - b0);
		frb = wbits - __bitmap_weight(buf, wbits);
		wnd->total_zeroes += frb - wnd->free_bits[iw];
		wnd->free_bits[iw] = frb;

		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		/* err = sync_dirty_buffer(bh); */

		b0 = 0;
		bits -= op;
	}

	wnd->nbits = new_bits;
	wnd->nwnd = new_wnd;
	wnd->bits_last = new_last;

	wnd_add_free_ext(wnd, old_bits, new_bits - old_bits, false);

	return 0;
}

void wnd_zone_set(struct wnd_bitmap *wnd, size_t lcn, size_t len)
{
	size_t zlen;

	zlen = wnd->zone_end - wnd->zone_bit;
	if (zlen)
		wnd_add_free_ext(wnd, wnd->zone_bit, zlen, false);

	if (!RB_EMPTY_ROOT(&wnd->start_tree) && len)
		wnd_remove_free_ext(wnd, lcn, len);

	wnd->zone_bit = lcn;
	wnd->zone_end = lcn + len;
}

int ntfs_trim_fs(struct ntfs_sb_info *sbi, struct fstrim_range *range)
{
	int err = 0;
	struct super_block *sb = sbi->sb;
	struct wnd_bitmap *wnd = &sbi->used.bitmap;
	u32 wbits = 8 * sb->s_blocksize;
	CLST len = 0, lcn = 0, done = 0;
	CLST minlen = bytes_to_cluster(sbi, range->minlen);
	CLST lcn_from = bytes_to_cluster(sbi, range->start);
	size_t iw = lcn_from >> (sb->s_blocksize_bits + 3);
	u32 wbit = lcn_from & (wbits - 1);
	const ulong *buf;
	CLST lcn_to;

	if (!minlen)
		minlen = 1;

	if (range->len == (u64)-1)
		lcn_to = wnd->nbits;
	else
		lcn_to = bytes_to_cluster(sbi, range->start + range->len);

	down_read_nested(&wnd->rw_lock, BITMAP_MUTEX_CLUSTERS);

	for (; iw < wnd->nbits; iw++, wbit = 0) {
		CLST lcn_wnd = iw * wbits;
		struct buffer_head *bh;

		if (lcn_wnd > lcn_to)
			break;

		if (!wnd->free_bits[iw])
			continue;

		if (iw + 1 == wnd->nwnd)
			wbits = wnd->bits_last;

		if (lcn_wnd + wbits > lcn_to)
			wbits = lcn_to - lcn_wnd;

		bh = wnd_map(wnd, iw);
		if (IS_ERR(bh)) {
			err = PTR_ERR(bh);
			break;
		}

		buf = (ulong *)bh->b_data;

		for (; wbit < wbits; wbit++) {
			if (!test_bit(wbit, buf)) {
				if (!len)
					lcn = lcn_wnd + wbit;
				len += 1;
				continue;
			}
			if (len >= minlen) {
				err = ntfs_discard(sbi, lcn, len);
				if (err)
					goto out;
				done += len;
			}
			len = 0;
		}
		put_bh(bh);
	}

	/* Process the last fragment. */
	if (len >= minlen) {
		err = ntfs_discard(sbi, lcn, len);
		if (err)
			goto out;
		done += len;
	}

out:
	range->len = (u64)done << sbi->cluster_bits;

	up_read(&wnd->rw_lock);

	return err;
}

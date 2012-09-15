/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/gfs2_ondisk.h>
#include <linux/prefetch.h>
#include <linux/blkdev.h>
#include <linux/rbtree.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "glops.h"
#include "lops.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"
#include "util.h"
#include "log.h"
#include "inode.h"
#include "trace_gfs2.h"

#define BFITNOENT ((u32)~0)
#define NO_BLOCK ((u64)~0)

#define RSRV_CONTENTION_FACTOR 4
#define RGRP_RSRV_MAX_CONTENDERS 2

#if BITS_PER_LONG == 32
#define LBITMASK   (0x55555555UL)
#define LBITSKIP55 (0x55555555UL)
#define LBITSKIP00 (0x00000000UL)
#else
#define LBITMASK   (0x5555555555555555UL)
#define LBITSKIP55 (0x5555555555555555UL)
#define LBITSKIP00 (0x0000000000000000UL)
#endif

/*
 * These routines are used by the resource group routines (rgrp.c)
 * to keep track of block allocation.  Each block is represented by two
 * bits.  So, each byte represents GFS2_NBBY (i.e. 4) blocks.
 *
 * 0 = Free
 * 1 = Used (not metadata)
 * 2 = Unlinked (still in use) inode
 * 3 = Used (metadata)
 */

static const char valid_change[16] = {
	        /* current */
	/* n */ 0, 1, 1, 1,
	/* e */ 1, 0, 0, 0,
	/* w */ 0, 0, 0, 1,
	        1, 0, 0, 0
};

static u32 rgblk_search(struct gfs2_rgrpd *rgd, u32 goal,
			unsigned char old_state,
			struct gfs2_bitmap **rbi);

/**
 * gfs2_setbit - Set a bit in the bitmaps
 * @rgd: the resource group descriptor
 * @buf2: the clone buffer that holds the bitmaps
 * @bi: the bitmap structure
 * @block: the block to set
 * @new_state: the new state of the block
 *
 */

static inline void gfs2_setbit(struct gfs2_rgrpd *rgd, unsigned char *buf2,
			       struct gfs2_bitmap *bi, u32 block,
			       unsigned char new_state)
{
	unsigned char *byte1, *byte2, *end, cur_state;
	unsigned int buflen = bi->bi_len;
	const unsigned int bit = (block % GFS2_NBBY) * GFS2_BIT_SIZE;

	byte1 = bi->bi_bh->b_data + bi->bi_offset + (block / GFS2_NBBY);
	end = bi->bi_bh->b_data + bi->bi_offset + buflen;

	BUG_ON(byte1 >= end);

	cur_state = (*byte1 >> bit) & GFS2_BIT_MASK;

	if (unlikely(!valid_change[new_state * 4 + cur_state])) {
		printk(KERN_WARNING "GFS2: buf_blk = 0x%llx old_state=%d, "
		       "new_state=%d\n",
		       (unsigned long long)block, cur_state, new_state);
		printk(KERN_WARNING "GFS2: rgrp=0x%llx bi_start=0x%lx\n",
		       (unsigned long long)rgd->rd_addr,
		       (unsigned long)bi->bi_start);
		printk(KERN_WARNING "GFS2: bi_offset=0x%lx bi_len=0x%lx\n",
		       (unsigned long)bi->bi_offset,
		       (unsigned long)bi->bi_len);
		dump_stack();
		gfs2_consist_rgrpd(rgd);
		return;
	}
	*byte1 ^= (cur_state ^ new_state) << bit;

	if (buf2) {
		byte2 = buf2 + bi->bi_offset + (block / GFS2_NBBY);
		cur_state = (*byte2 >> bit) & GFS2_BIT_MASK;
		*byte2 ^= (cur_state ^ new_state) << bit;
	}
}

/**
 * gfs2_testbit - test a bit in the bitmaps
 * @rgd: the resource group descriptor
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @block: the block to read
 *
 */

static inline unsigned char gfs2_testbit(struct gfs2_rgrpd *rgd,
					 const unsigned char *buffer,
					 unsigned int buflen, u32 block)
{
	const unsigned char *byte, *end;
	unsigned char cur_state;
	unsigned int bit;

	byte = buffer + (block / GFS2_NBBY);
	bit = (block % GFS2_NBBY) * GFS2_BIT_SIZE;
	end = buffer + buflen;

	gfs2_assert(rgd->rd_sbd, byte < end);

	cur_state = (*byte >> bit) & GFS2_BIT_MASK;

	return cur_state;
}

/**
 * gfs2_bit_search
 * @ptr: Pointer to bitmap data
 * @mask: Mask to use (normally 0x55555.... but adjusted for search start)
 * @state: The state we are searching for
 *
 * We xor the bitmap data with a patter which is the bitwise opposite
 * of what we are looking for, this gives rise to a pattern of ones
 * wherever there is a match. Since we have two bits per entry, we
 * take this pattern, shift it down by one place and then and it with
 * the original. All the even bit positions (0,2,4, etc) then represent
 * successful matches, so we mask with 0x55555..... to remove the unwanted
 * odd bit positions.
 *
 * This allows searching of a whole u64 at once (32 blocks) with a
 * single test (on 64 bit arches).
 */

static inline u64 gfs2_bit_search(const __le64 *ptr, u64 mask, u8 state)
{
	u64 tmp;
	static const u64 search[] = {
		[0] = 0xffffffffffffffffULL,
		[1] = 0xaaaaaaaaaaaaaaaaULL,
		[2] = 0x5555555555555555ULL,
		[3] = 0x0000000000000000ULL,
	};
	tmp = le64_to_cpu(*ptr) ^ search[state];
	tmp &= (tmp >> 1);
	tmp &= mask;
	return tmp;
}

/**
 * rs_cmp - multi-block reservation range compare
 * @blk: absolute file system block number of the new reservation
 * @len: number of blocks in the new reservation
 * @rs: existing reservation to compare against
 *
 * returns: 1 if the block range is beyond the reach of the reservation
 *         -1 if the block range is before the start of the reservation
 *          0 if the block range overlaps with the reservation
 */
static inline int rs_cmp(u64 blk, u32 len, struct gfs2_blkreserv *rs)
{
	u64 startblk = gfs2_rs_startblk(rs);

	if (blk >= startblk + rs->rs_free)
		return 1;
	if (blk + len - 1 < startblk)
		return -1;
	return 0;
}

/**
 * rs_find - Find a rgrp multi-block reservation that contains a given block
 * @rgd: The rgrp
 * @rgblk: The block we're looking for, relative to the rgrp
 */
static struct gfs2_blkreserv *rs_find(struct gfs2_rgrpd *rgd, u32 rgblk)
{
	struct rb_node **newn;
	int rc;
	u64 fsblk = rgblk + rgd->rd_data0;

	spin_lock(&rgd->rd_rsspin);
	newn = &rgd->rd_rstree.rb_node;
	while (*newn) {
		struct gfs2_blkreserv *cur =
			rb_entry(*newn, struct gfs2_blkreserv, rs_node);
		rc = rs_cmp(fsblk, 1, cur);
		if (rc < 0)
			newn = &((*newn)->rb_left);
		else if (rc > 0)
			newn = &((*newn)->rb_right);
		else {
			spin_unlock(&rgd->rd_rsspin);
			return cur;
		}
	}
	spin_unlock(&rgd->rd_rsspin);
	return NULL;
}

/**
 * gfs2_bitfit - Search an rgrp's bitmap buffer to find a bit-pair representing
 *       a block in a given allocation state.
 * @buf: the buffer that holds the bitmaps
 * @len: the length (in bytes) of the buffer
 * @goal: start search at this block's bit-pair (within @buffer)
 * @state: GFS2_BLKST_XXX the state of the block we're looking for.
 *
 * Scope of @goal and returned block number is only within this bitmap buffer,
 * not entire rgrp or filesystem.  @buffer will be offset from the actual
 * beginning of a bitmap block buffer, skipping any header structures, but
 * headers are always a multiple of 64 bits long so that the buffer is
 * always aligned to a 64 bit boundary.
 *
 * The size of the buffer is in bytes, but is it assumed that it is
 * always ok to read a complete multiple of 64 bits at the end
 * of the block in case the end is no aligned to a natural boundary.
 *
 * Return: the block number (bitmap buffer scope) that was found
 */

static u32 gfs2_bitfit(const u8 *buf, const unsigned int len,
		       u32 goal, u8 state)
{
	u32 spoint = (goal << 1) & ((8*sizeof(u64)) - 1);
	const __le64 *ptr = ((__le64 *)buf) + (goal >> 5);
	const __le64 *end = (__le64 *)(buf + ALIGN(len, sizeof(u64)));
	u64 tmp;
	u64 mask = 0x5555555555555555ULL;
	u32 bit;

	BUG_ON(state > 3);

	/* Mask off bits we don't care about at the start of the search */
	mask <<= spoint;
	tmp = gfs2_bit_search(ptr, mask, state);
	ptr++;
	while(tmp == 0 && ptr < end) {
		tmp = gfs2_bit_search(ptr, 0x5555555555555555ULL, state);
		ptr++;
	}
	/* Mask off any bits which are more than len bytes from the start */
	if (ptr == end && (len & (sizeof(u64) - 1)))
		tmp &= (((u64)~0) >> (64 - 8*(len & (sizeof(u64) - 1))));
	/* Didn't find anything, so return */
	if (tmp == 0)
		return BFITNOENT;
	ptr--;
	bit = __ffs64(tmp);
	bit /= 2;	/* two bits per entry in the bitmap */
	return (((const unsigned char *)ptr - buf) * GFS2_NBBY) + bit;
}

/**
 * gfs2_bitcount - count the number of bits in a certain state
 * @rgd: the resource group descriptor
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @state: the state of the block we're looking for
 *
 * Returns: The number of bits
 */

static u32 gfs2_bitcount(struct gfs2_rgrpd *rgd, const u8 *buffer,
			 unsigned int buflen, u8 state)
{
	const u8 *byte = buffer;
	const u8 *end = buffer + buflen;
	const u8 state1 = state << 2;
	const u8 state2 = state << 4;
	const u8 state3 = state << 6;
	u32 count = 0;

	for (; byte < end; byte++) {
		if (((*byte) & 0x03) == state)
			count++;
		if (((*byte) & 0x0C) == state1)
			count++;
		if (((*byte) & 0x30) == state2)
			count++;
		if (((*byte) & 0xC0) == state3)
			count++;
	}

	return count;
}

/**
 * gfs2_rgrp_verify - Verify that a resource group is consistent
 * @rgd: the rgrp
 *
 */

void gfs2_rgrp_verify(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_bitmap *bi = NULL;
	u32 length = rgd->rd_length;
	u32 count[4], tmp;
	int buf, x;

	memset(count, 0, 4 * sizeof(u32));

	/* Count # blocks in each of 4 possible allocation states */
	for (buf = 0; buf < length; buf++) {
		bi = rgd->rd_bits + buf;
		for (x = 0; x < 4; x++)
			count[x] += gfs2_bitcount(rgd,
						  bi->bi_bh->b_data +
						  bi->bi_offset,
						  bi->bi_len, x);
	}

	if (count[0] != rgd->rd_free) {
		if (gfs2_consist_rgrpd(rgd))
			fs_err(sdp, "free data mismatch:  %u != %u\n",
			       count[0], rgd->rd_free);
		return;
	}

	tmp = rgd->rd_data - rgd->rd_free - rgd->rd_dinodes;
	if (count[1] != tmp) {
		if (gfs2_consist_rgrpd(rgd))
			fs_err(sdp, "used data mismatch:  %u != %u\n",
			       count[1], tmp);
		return;
	}

	if (count[2] + count[3] != rgd->rd_dinodes) {
		if (gfs2_consist_rgrpd(rgd))
			fs_err(sdp, "used metadata mismatch:  %u != %u\n",
			       count[2] + count[3], rgd->rd_dinodes);
		return;
	}
}

static inline int rgrp_contains_block(struct gfs2_rgrpd *rgd, u64 block)
{
	u64 first = rgd->rd_data0;
	u64 last = first + rgd->rd_data;
	return first <= block && block < last;
}

/**
 * gfs2_blk2rgrpd - Find resource group for a given data/meta block number
 * @sdp: The GFS2 superblock
 * @blk: The data block number
 * @exact: True if this needs to be an exact match
 *
 * Returns: The resource group, or NULL if not found
 */

struct gfs2_rgrpd *gfs2_blk2rgrpd(struct gfs2_sbd *sdp, u64 blk, bool exact)
{
	struct rb_node *n, *next;
	struct gfs2_rgrpd *cur;

	spin_lock(&sdp->sd_rindex_spin);
	n = sdp->sd_rindex_tree.rb_node;
	while (n) {
		cur = rb_entry(n, struct gfs2_rgrpd, rd_node);
		next = NULL;
		if (blk < cur->rd_addr)
			next = n->rb_left;
		else if (blk >= cur->rd_data0 + cur->rd_data)
			next = n->rb_right;
		if (next == NULL) {
			spin_unlock(&sdp->sd_rindex_spin);
			if (exact) {
				if (blk < cur->rd_addr)
					return NULL;
				if (blk >= cur->rd_data0 + cur->rd_data)
					return NULL;
			}
			return cur;
		}
		n = next;
	}
	spin_unlock(&sdp->sd_rindex_spin);

	return NULL;
}

/**
 * gfs2_rgrpd_get_first - get the first Resource Group in the filesystem
 * @sdp: The GFS2 superblock
 *
 * Returns: The first rgrp in the filesystem
 */

struct gfs2_rgrpd *gfs2_rgrpd_get_first(struct gfs2_sbd *sdp)
{
	const struct rb_node *n;
	struct gfs2_rgrpd *rgd;

	spin_lock(&sdp->sd_rindex_spin);
	n = rb_first(&sdp->sd_rindex_tree);
	rgd = rb_entry(n, struct gfs2_rgrpd, rd_node);
	spin_unlock(&sdp->sd_rindex_spin);

	return rgd;
}

/**
 * gfs2_rgrpd_get_next - get the next RG
 * @rgd: the resource group descriptor
 *
 * Returns: The next rgrp
 */

struct gfs2_rgrpd *gfs2_rgrpd_get_next(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	const struct rb_node *n;

	spin_lock(&sdp->sd_rindex_spin);
	n = rb_next(&rgd->rd_node);
	if (n == NULL)
		n = rb_first(&sdp->sd_rindex_tree);

	if (unlikely(&rgd->rd_node == n)) {
		spin_unlock(&sdp->sd_rindex_spin);
		return NULL;
	}
	rgd = rb_entry(n, struct gfs2_rgrpd, rd_node);
	spin_unlock(&sdp->sd_rindex_spin);
	return rgd;
}

void gfs2_free_clones(struct gfs2_rgrpd *rgd)
{
	int x;

	for (x = 0; x < rgd->rd_length; x++) {
		struct gfs2_bitmap *bi = rgd->rd_bits + x;
		kfree(bi->bi_clone);
		bi->bi_clone = NULL;
	}
}

/**
 * gfs2_rs_alloc - make sure we have a reservation assigned to the inode
 * @ip: the inode for this reservation
 */
int gfs2_rs_alloc(struct gfs2_inode *ip)
{
	int error = 0;
	struct gfs2_blkreserv *res;

	if (ip->i_res)
		return 0;

	res = kmem_cache_zalloc(gfs2_rsrv_cachep, GFP_NOFS);
	if (!res)
		error = -ENOMEM;

	down_write(&ip->i_rw_mutex);
	if (ip->i_res)
		kmem_cache_free(gfs2_rsrv_cachep, res);
	else
		ip->i_res = res;
	up_write(&ip->i_rw_mutex);
	return error;
}

static void dump_rs(struct seq_file *seq, struct gfs2_blkreserv *rs)
{
	gfs2_print_dbg(seq, "  r: %llu s:%llu b:%u f:%u\n",
		       rs->rs_rgd->rd_addr, gfs2_rs_startblk(rs), rs->rs_biblk,
		       rs->rs_free);
}

/**
 * __rs_deltree - remove a multi-block reservation from the rgd tree
 * @rs: The reservation to remove
 *
 */
static void __rs_deltree(struct gfs2_blkreserv *rs)
{
	struct gfs2_rgrpd *rgd;

	if (!gfs2_rs_active(rs))
		return;

	rgd = rs->rs_rgd;
	/* We can't do this: The reason is that when the rgrp is invalidated,
	   it's in the "middle" of acquiring the glock, but the HOLDER bit
	   isn't set yet:
	   BUG_ON(!gfs2_glock_is_locked_by_me(rs->rs_rgd->rd_gl));*/
	trace_gfs2_rs(NULL, rs, TRACE_RS_TREEDEL);

	if (!RB_EMPTY_ROOT(&rgd->rd_rstree))
		rb_erase(&rs->rs_node, &rgd->rd_rstree);
	BUG_ON(!rgd->rd_rs_cnt);
	rgd->rd_rs_cnt--;

	if (rs->rs_free) {
		/* return reserved blocks to the rgrp and the ip */
		BUG_ON(rs->rs_rgd->rd_reserved < rs->rs_free);
		rs->rs_rgd->rd_reserved -= rs->rs_free;
		rs->rs_free = 0;
		clear_bit(GBF_FULL, &rs->rs_bi->bi_flags);
		smp_mb__after_clear_bit();
	}
	/* We can't change any of the step 1 or step 2 components of the rs.
	   E.g. We can't set rs_rgd to NULL because the rgd glock is held and
	   dequeued through this pointer.
	   Can't: atomic_set(&rs->rs_sizehint, 0);
	   Can't: rs->rs_requested = 0;
	   Can't: rs->rs_rgd = NULL;*/
	rs->rs_bi = NULL;
	rs->rs_biblk = 0;
}

/**
 * gfs2_rs_deltree - remove a multi-block reservation from the rgd tree
 * @rs: The reservation to remove
 *
 */
void gfs2_rs_deltree(struct gfs2_blkreserv *rs)
{
	struct gfs2_rgrpd *rgd;

	if (!gfs2_rs_active(rs))
		return;

	rgd = rs->rs_rgd;
	spin_lock(&rgd->rd_rsspin);
	__rs_deltree(rs);
	spin_unlock(&rgd->rd_rsspin);
}

/**
 * gfs2_rs_delete - delete a multi-block reservation
 * @ip: The inode for this reservation
 *
 */
void gfs2_rs_delete(struct gfs2_inode *ip)
{
	down_write(&ip->i_rw_mutex);
	if (ip->i_res) {
		gfs2_rs_deltree(ip->i_res);
		trace_gfs2_rs(ip, ip->i_res, TRACE_RS_DELETE);
		BUG_ON(ip->i_res->rs_free);
		kmem_cache_free(gfs2_rsrv_cachep, ip->i_res);
		ip->i_res = NULL;
	}
	up_write(&ip->i_rw_mutex);
}

/**
 * return_all_reservations - return all reserved blocks back to the rgrp.
 * @rgd: the rgrp that needs its space back
 *
 * We previously reserved a bunch of blocks for allocation. Now we need to
 * give them back. This leave the reservation structures in tact, but removes
 * all of their corresponding "no-fly zones".
 */
static void return_all_reservations(struct gfs2_rgrpd *rgd)
{
	struct rb_node *n;
	struct gfs2_blkreserv *rs;

	spin_lock(&rgd->rd_rsspin);
	while ((n = rb_first(&rgd->rd_rstree))) {
		rs = rb_entry(n, struct gfs2_blkreserv, rs_node);
		__rs_deltree(rs);
	}
	spin_unlock(&rgd->rd_rsspin);
}

void gfs2_clear_rgrpd(struct gfs2_sbd *sdp)
{
	struct rb_node *n;
	struct gfs2_rgrpd *rgd;
	struct gfs2_glock *gl;

	while ((n = rb_first(&sdp->sd_rindex_tree))) {
		rgd = rb_entry(n, struct gfs2_rgrpd, rd_node);
		gl = rgd->rd_gl;

		rb_erase(n, &sdp->sd_rindex_tree);

		if (gl) {
			spin_lock(&gl->gl_spin);
			gl->gl_object = NULL;
			spin_unlock(&gl->gl_spin);
			gfs2_glock_add_to_lru(gl);
			gfs2_glock_put(gl);
		}

		gfs2_free_clones(rgd);
		kfree(rgd->rd_bits);
		return_all_reservations(rgd);
		kmem_cache_free(gfs2_rgrpd_cachep, rgd);
	}
}

static void gfs2_rindex_print(const struct gfs2_rgrpd *rgd)
{
	printk(KERN_INFO "  ri_addr = %llu\n", (unsigned long long)rgd->rd_addr);
	printk(KERN_INFO "  ri_length = %u\n", rgd->rd_length);
	printk(KERN_INFO "  ri_data0 = %llu\n", (unsigned long long)rgd->rd_data0);
	printk(KERN_INFO "  ri_data = %u\n", rgd->rd_data);
	printk(KERN_INFO "  ri_bitbytes = %u\n", rgd->rd_bitbytes);
}

/**
 * gfs2_compute_bitstructs - Compute the bitmap sizes
 * @rgd: The resource group descriptor
 *
 * Calculates bitmap descriptors, one for each block that contains bitmap data
 *
 * Returns: errno
 */

static int compute_bitstructs(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_bitmap *bi;
	u32 length = rgd->rd_length; /* # blocks in hdr & bitmap */
	u32 bytes_left, bytes;
	int x;

	if (!length)
		return -EINVAL;

	rgd->rd_bits = kcalloc(length, sizeof(struct gfs2_bitmap), GFP_NOFS);
	if (!rgd->rd_bits)
		return -ENOMEM;

	bytes_left = rgd->rd_bitbytes;

	for (x = 0; x < length; x++) {
		bi = rgd->rd_bits + x;

		bi->bi_flags = 0;
		/* small rgrp; bitmap stored completely in header block */
		if (length == 1) {
			bytes = bytes_left;
			bi->bi_offset = sizeof(struct gfs2_rgrp);
			bi->bi_start = 0;
			bi->bi_len = bytes;
		/* header block */
		} else if (x == 0) {
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_rgrp);
			bi->bi_offset = sizeof(struct gfs2_rgrp);
			bi->bi_start = 0;
			bi->bi_len = bytes;
		/* last block */
		} else if (x + 1 == length) {
			bytes = bytes_left;
			bi->bi_offset = sizeof(struct gfs2_meta_header);
			bi->bi_start = rgd->rd_bitbytes - bytes_left;
			bi->bi_len = bytes;
		/* other blocks */
		} else {
			bytes = sdp->sd_sb.sb_bsize -
				sizeof(struct gfs2_meta_header);
			bi->bi_offset = sizeof(struct gfs2_meta_header);
			bi->bi_start = rgd->rd_bitbytes - bytes_left;
			bi->bi_len = bytes;
		}

		bytes_left -= bytes;
	}

	if (bytes_left) {
		gfs2_consist_rgrpd(rgd);
		return -EIO;
	}
	bi = rgd->rd_bits + (length - 1);
	if ((bi->bi_start + bi->bi_len) * GFS2_NBBY != rgd->rd_data) {
		if (gfs2_consist_rgrpd(rgd)) {
			gfs2_rindex_print(rgd);
			fs_err(sdp, "start=%u len=%u offset=%u\n",
			       bi->bi_start, bi->bi_len, bi->bi_offset);
		}
		return -EIO;
	}

	return 0;
}

/**
 * gfs2_ri_total - Total up the file system space, according to the rindex.
 * @sdp: the filesystem
 *
 */
u64 gfs2_ri_total(struct gfs2_sbd *sdp)
{
	u64 total_data = 0;	
	struct inode *inode = sdp->sd_rindex;
	struct gfs2_inode *ip = GFS2_I(inode);
	char buf[sizeof(struct gfs2_rindex)];
	int error, rgrps;

	for (rgrps = 0;; rgrps++) {
		loff_t pos = rgrps * sizeof(struct gfs2_rindex);

		if (pos + sizeof(struct gfs2_rindex) > i_size_read(inode))
			break;
		error = gfs2_internal_read(ip, buf, &pos,
					   sizeof(struct gfs2_rindex));
		if (error != sizeof(struct gfs2_rindex))
			break;
		total_data += be32_to_cpu(((struct gfs2_rindex *)buf)->ri_data);
	}
	return total_data;
}

static int rgd_insert(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct rb_node **newn = &sdp->sd_rindex_tree.rb_node, *parent = NULL;

	/* Figure out where to put new node */
	while (*newn) {
		struct gfs2_rgrpd *cur = rb_entry(*newn, struct gfs2_rgrpd,
						  rd_node);

		parent = *newn;
		if (rgd->rd_addr < cur->rd_addr)
			newn = &((*newn)->rb_left);
		else if (rgd->rd_addr > cur->rd_addr)
			newn = &((*newn)->rb_right);
		else
			return -EEXIST;
	}

	rb_link_node(&rgd->rd_node, parent, newn);
	rb_insert_color(&rgd->rd_node, &sdp->sd_rindex_tree);
	sdp->sd_rgrps++;
	return 0;
}

/**
 * read_rindex_entry - Pull in a new resource index entry from the disk
 * @ip: Pointer to the rindex inode
 *
 * Returns: 0 on success, > 0 on EOF, error code otherwise
 */

static int read_rindex_entry(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	loff_t pos = sdp->sd_rgrps * sizeof(struct gfs2_rindex);
	struct gfs2_rindex buf;
	int error;
	struct gfs2_rgrpd *rgd;

	if (pos >= i_size_read(&ip->i_inode))
		return 1;

	error = gfs2_internal_read(ip, (char *)&buf, &pos,
				   sizeof(struct gfs2_rindex));

	if (error != sizeof(struct gfs2_rindex))
		return (error == 0) ? 1 : error;

	rgd = kmem_cache_zalloc(gfs2_rgrpd_cachep, GFP_NOFS);
	error = -ENOMEM;
	if (!rgd)
		return error;

	rgd->rd_sbd = sdp;
	rgd->rd_addr = be64_to_cpu(buf.ri_addr);
	rgd->rd_length = be32_to_cpu(buf.ri_length);
	rgd->rd_data0 = be64_to_cpu(buf.ri_data0);
	rgd->rd_data = be32_to_cpu(buf.ri_data);
	rgd->rd_bitbytes = be32_to_cpu(buf.ri_bitbytes);
	spin_lock_init(&rgd->rd_rsspin);

	error = compute_bitstructs(rgd);
	if (error)
		goto fail;

	error = gfs2_glock_get(sdp, rgd->rd_addr,
			       &gfs2_rgrp_glops, CREATE, &rgd->rd_gl);
	if (error)
		goto fail;

	rgd->rd_gl->gl_object = rgd;
	rgd->rd_rgl = (struct gfs2_rgrp_lvb *)rgd->rd_gl->gl_lvb;
	rgd->rd_flags &= ~GFS2_RDF_UPTODATE;
	if (rgd->rd_data > sdp->sd_max_rg_data)
		sdp->sd_max_rg_data = rgd->rd_data;
	spin_lock(&sdp->sd_rindex_spin);
	error = rgd_insert(rgd);
	spin_unlock(&sdp->sd_rindex_spin);
	if (!error)
		return 0;

	error = 0; /* someone else read in the rgrp; free it and ignore it */
	gfs2_glock_put(rgd->rd_gl);

fail:
	kfree(rgd->rd_bits);
	kmem_cache_free(gfs2_rgrpd_cachep, rgd);
	return error;
}

/**
 * gfs2_ri_update - Pull in a new resource index from the disk
 * @ip: pointer to the rindex inode
 *
 * Returns: 0 on successful update, error code otherwise
 */

static int gfs2_ri_update(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	int error;

	do {
		error = read_rindex_entry(ip);
	} while (error == 0);

	if (error < 0)
		return error;

	sdp->sd_rindex_uptodate = 1;
	return 0;
}

/**
 * gfs2_rindex_update - Update the rindex if required
 * @sdp: The GFS2 superblock
 *
 * We grab a lock on the rindex inode to make sure that it doesn't
 * change whilst we are performing an operation. We keep this lock
 * for quite long periods of time compared to other locks. This
 * doesn't matter, since it is shared and it is very, very rarely
 * accessed in the exclusive mode (i.e. only when expanding the filesystem).
 *
 * This makes sure that we're using the latest copy of the resource index
 * special file, which might have been updated if someone expanded the
 * filesystem (via gfs2_grow utility), which adds new resource groups.
 *
 * Returns: 0 on succeess, error code otherwise
 */

int gfs2_rindex_update(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_rindex);
	struct gfs2_glock *gl = ip->i_gl;
	struct gfs2_holder ri_gh;
	int error = 0;
	int unlock_required = 0;

	/* Read new copy from disk if we don't have the latest */
	if (!sdp->sd_rindex_uptodate) {
		if (!gfs2_glock_is_locked_by_me(gl)) {
			error = gfs2_glock_nq_init(gl, LM_ST_SHARED, 0, &ri_gh);
			if (error)
				return error;
			unlock_required = 1;
		}
		if (!sdp->sd_rindex_uptodate)
			error = gfs2_ri_update(ip);
		if (unlock_required)
			gfs2_glock_dq_uninit(&ri_gh);
	}

	return error;
}

static void gfs2_rgrp_in(struct gfs2_rgrpd *rgd, const void *buf)
{
	const struct gfs2_rgrp *str = buf;
	u32 rg_flags;

	rg_flags = be32_to_cpu(str->rg_flags);
	rg_flags &= ~GFS2_RDF_MASK;
	rgd->rd_flags &= GFS2_RDF_MASK;
	rgd->rd_flags |= rg_flags;
	rgd->rd_free = be32_to_cpu(str->rg_free);
	rgd->rd_dinodes = be32_to_cpu(str->rg_dinodes);
	rgd->rd_igeneration = be64_to_cpu(str->rg_igeneration);
}

static void gfs2_rgrp_out(struct gfs2_rgrpd *rgd, void *buf)
{
	struct gfs2_rgrp *str = buf;

	str->rg_flags = cpu_to_be32(rgd->rd_flags & ~GFS2_RDF_MASK);
	str->rg_free = cpu_to_be32(rgd->rd_free);
	str->rg_dinodes = cpu_to_be32(rgd->rd_dinodes);
	str->__pad = cpu_to_be32(0);
	str->rg_igeneration = cpu_to_be64(rgd->rd_igeneration);
	memset(&str->rg_reserved, 0, sizeof(str->rg_reserved));
}

static int gfs2_rgrp_lvb_valid(struct gfs2_rgrpd *rgd)
{
	struct gfs2_rgrp_lvb *rgl = rgd->rd_rgl;
	struct gfs2_rgrp *str = (struct gfs2_rgrp *)rgd->rd_bits[0].bi_bh->b_data;

	if (rgl->rl_flags != str->rg_flags || rgl->rl_free != str->rg_free ||
	    rgl->rl_dinodes != str->rg_dinodes ||
	    rgl->rl_igeneration != str->rg_igeneration)
		return 0;
	return 1;
}

static void gfs2_rgrp_ondisk2lvb(struct gfs2_rgrp_lvb *rgl, const void *buf)
{
	const struct gfs2_rgrp *str = buf;

	rgl->rl_magic = cpu_to_be32(GFS2_MAGIC);
	rgl->rl_flags = str->rg_flags;
	rgl->rl_free = str->rg_free;
	rgl->rl_dinodes = str->rg_dinodes;
	rgl->rl_igeneration = str->rg_igeneration;
	rgl->__pad = 0UL;
}

static void update_rgrp_lvb_unlinked(struct gfs2_rgrpd *rgd, u32 change)
{
	struct gfs2_rgrp_lvb *rgl = rgd->rd_rgl;
	u32 unlinked = be32_to_cpu(rgl->rl_unlinked) + change;
	rgl->rl_unlinked = cpu_to_be32(unlinked);
}

static u32 count_unlinked(struct gfs2_rgrpd *rgd)
{
	struct gfs2_bitmap *bi;
	const u32 length = rgd->rd_length;
	const u8 *buffer = NULL;
	u32 i, goal, count = 0;

	for (i = 0, bi = rgd->rd_bits; i < length; i++, bi++) {
		goal = 0;
		buffer = bi->bi_bh->b_data + bi->bi_offset;
		WARN_ON(!buffer_uptodate(bi->bi_bh));
		while (goal < bi->bi_len * GFS2_NBBY) {
			goal = gfs2_bitfit(buffer, bi->bi_len, goal,
					   GFS2_BLKST_UNLINKED);
			if (goal == BFITNOENT)
				break;
			count++;
			goal++;
		}
	}

	return count;
}


/**
 * gfs2_rgrp_bh_get - Read in a RG's header and bitmaps
 * @rgd: the struct gfs2_rgrpd describing the RG to read in
 *
 * Read in all of a Resource Group's header and bitmap blocks.
 * Caller must eventually call gfs2_rgrp_relse() to free the bitmaps.
 *
 * Returns: errno
 */

int gfs2_rgrp_bh_get(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_glock *gl = rgd->rd_gl;
	unsigned int length = rgd->rd_length;
	struct gfs2_bitmap *bi;
	unsigned int x, y;
	int error;

	if (rgd->rd_bits[0].bi_bh != NULL)
		return 0;

	for (x = 0; x < length; x++) {
		bi = rgd->rd_bits + x;
		error = gfs2_meta_read(gl, rgd->rd_addr + x, 0, &bi->bi_bh);
		if (error)
			goto fail;
	}

	for (y = length; y--;) {
		bi = rgd->rd_bits + y;
		error = gfs2_meta_wait(sdp, bi->bi_bh);
		if (error)
			goto fail;
		if (gfs2_metatype_check(sdp, bi->bi_bh, y ? GFS2_METATYPE_RB :
					      GFS2_METATYPE_RG)) {
			error = -EIO;
			goto fail;
		}
	}

	if (!(rgd->rd_flags & GFS2_RDF_UPTODATE)) {
		for (x = 0; x < length; x++)
			clear_bit(GBF_FULL, &rgd->rd_bits[x].bi_flags);
		gfs2_rgrp_in(rgd, (rgd->rd_bits[0].bi_bh)->b_data);
		rgd->rd_flags |= (GFS2_RDF_UPTODATE | GFS2_RDF_CHECK);
		rgd->rd_free_clone = rgd->rd_free;
	}
	if (be32_to_cpu(GFS2_MAGIC) != rgd->rd_rgl->rl_magic) {
		rgd->rd_rgl->rl_unlinked = cpu_to_be32(count_unlinked(rgd));
		gfs2_rgrp_ondisk2lvb(rgd->rd_rgl,
				     rgd->rd_bits[0].bi_bh->b_data);
	}
	else if (sdp->sd_args.ar_rgrplvb) {
		if (!gfs2_rgrp_lvb_valid(rgd)){
			gfs2_consist_rgrpd(rgd);
			error = -EIO;
			goto fail;
		}
		if (rgd->rd_rgl->rl_unlinked == 0)
			rgd->rd_flags &= ~GFS2_RDF_CHECK;
	}
	return 0;

fail:
	while (x--) {
		bi = rgd->rd_bits + x;
		brelse(bi->bi_bh);
		bi->bi_bh = NULL;
		gfs2_assert_warn(sdp, !bi->bi_clone);
	}

	return error;
}

int update_rgrp_lvb(struct gfs2_rgrpd *rgd)
{
	u32 rl_flags;

	if (rgd->rd_flags & GFS2_RDF_UPTODATE)
		return 0;

	if (be32_to_cpu(GFS2_MAGIC) != rgd->rd_rgl->rl_magic)
		return gfs2_rgrp_bh_get(rgd);

	rl_flags = be32_to_cpu(rgd->rd_rgl->rl_flags);
	rl_flags &= ~GFS2_RDF_MASK;
	rgd->rd_flags &= GFS2_RDF_MASK;
	rgd->rd_flags |= (rl_flags | GFS2_RDF_UPTODATE | GFS2_RDF_CHECK);
	if (rgd->rd_rgl->rl_unlinked == 0)
		rgd->rd_flags &= ~GFS2_RDF_CHECK;
	rgd->rd_free = be32_to_cpu(rgd->rd_rgl->rl_free);
	rgd->rd_free_clone = rgd->rd_free;
	rgd->rd_dinodes = be32_to_cpu(rgd->rd_rgl->rl_dinodes);
	rgd->rd_igeneration = be64_to_cpu(rgd->rd_rgl->rl_igeneration);
	return 0;
}

int gfs2_rgrp_go_lock(struct gfs2_holder *gh)
{
	struct gfs2_rgrpd *rgd = gh->gh_gl->gl_object;
	struct gfs2_sbd *sdp = rgd->rd_sbd;

	if (gh->gh_flags & GL_SKIP && sdp->sd_args.ar_rgrplvb)
		return 0;
	return gfs2_rgrp_bh_get((struct gfs2_rgrpd *)gh->gh_gl->gl_object);
}

/**
 * gfs2_rgrp_go_unlock - Release RG bitmaps read in with gfs2_rgrp_bh_get()
 * @gh: The glock holder for the resource group
 *
 */

void gfs2_rgrp_go_unlock(struct gfs2_holder *gh)
{
	struct gfs2_rgrpd *rgd = gh->gh_gl->gl_object;
	int x, length = rgd->rd_length;

	for (x = 0; x < length; x++) {
		struct gfs2_bitmap *bi = rgd->rd_bits + x;
		if (bi->bi_bh) {
			brelse(bi->bi_bh);
			bi->bi_bh = NULL;
		}
	}

}

int gfs2_rgrp_send_discards(struct gfs2_sbd *sdp, u64 offset,
			     struct buffer_head *bh,
			     const struct gfs2_bitmap *bi, unsigned minlen, u64 *ptrimmed)
{
	struct super_block *sb = sdp->sd_vfs;
	struct block_device *bdev = sb->s_bdev;
	const unsigned int sects_per_blk = sdp->sd_sb.sb_bsize /
					   bdev_logical_block_size(sb->s_bdev);
	u64 blk;
	sector_t start = 0;
	sector_t nr_sects = 0;
	int rv;
	unsigned int x;
	u32 trimmed = 0;
	u8 diff;

	for (x = 0; x < bi->bi_len; x++) {
		const u8 *clone = bi->bi_clone ? bi->bi_clone : bi->bi_bh->b_data;
		clone += bi->bi_offset;
		clone += x;
		if (bh) {
			const u8 *orig = bh->b_data + bi->bi_offset + x;
			diff = ~(*orig | (*orig >> 1)) & (*clone | (*clone >> 1));
		} else {
			diff = ~(*clone | (*clone >> 1));
		}
		diff &= 0x55;
		if (diff == 0)
			continue;
		blk = offset + ((bi->bi_start + x) * GFS2_NBBY);
		blk *= sects_per_blk; /* convert to sectors */
		while(diff) {
			if (diff & 1) {
				if (nr_sects == 0)
					goto start_new_extent;
				if ((start + nr_sects) != blk) {
					if (nr_sects >= minlen) {
						rv = blkdev_issue_discard(bdev,
							start, nr_sects,
							GFP_NOFS, 0);
						if (rv)
							goto fail;
						trimmed += nr_sects;
					}
					nr_sects = 0;
start_new_extent:
					start = blk;
				}
				nr_sects += sects_per_blk;
			}
			diff >>= 2;
			blk += sects_per_blk;
		}
	}
	if (nr_sects >= minlen) {
		rv = blkdev_issue_discard(bdev, start, nr_sects, GFP_NOFS, 0);
		if (rv)
			goto fail;
		trimmed += nr_sects;
	}
	if (ptrimmed)
		*ptrimmed = trimmed;
	return 0;

fail:
	if (sdp->sd_args.ar_discard)
		fs_warn(sdp, "error %d on discard request, turning discards off for this filesystem", rv);
	sdp->sd_args.ar_discard = 0;
	return -EIO;
}

/**
 * gfs2_fitrim - Generate discard requests for unused bits of the filesystem
 * @filp: Any file on the filesystem
 * @argp: Pointer to the arguments (also used to pass result)
 *
 * Returns: 0 on success, otherwise error code
 */

int gfs2_fitrim(struct file *filp, void __user *argp)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct request_queue *q = bdev_get_queue(sdp->sd_vfs->s_bdev);
	struct buffer_head *bh;
	struct gfs2_rgrpd *rgd;
	struct gfs2_rgrpd *rgd_end;
	struct gfs2_holder gh;
	struct fstrim_range r;
	int ret = 0;
	u64 amt;
	u64 trimmed = 0;
	unsigned int x;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!blk_queue_discard(q))
		return -EOPNOTSUPP;

	if (argp == NULL) {
		r.start = 0;
		r.len = ULLONG_MAX;
		r.minlen = 0;
	} else if (copy_from_user(&r, argp, sizeof(r)))
		return -EFAULT;

	ret = gfs2_rindex_update(sdp);
	if (ret)
		return ret;

	rgd = gfs2_blk2rgrpd(sdp, r.start, 0);
	rgd_end = gfs2_blk2rgrpd(sdp, r.start + r.len, 0);

	while (1) {

		ret = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &gh);
		if (ret)
			goto out;

		if (!(rgd->rd_flags & GFS2_RGF_TRIMMED)) {
			/* Trim each bitmap in the rgrp */
			for (x = 0; x < rgd->rd_length; x++) {
				struct gfs2_bitmap *bi = rgd->rd_bits + x;
				ret = gfs2_rgrp_send_discards(sdp, rgd->rd_data0, NULL, bi, r.minlen, &amt);
				if (ret) {
					gfs2_glock_dq_uninit(&gh);
					goto out;
				}
				trimmed += amt;
			}

			/* Mark rgrp as having been trimmed */
			ret = gfs2_trans_begin(sdp, RES_RG_HDR, 0);
			if (ret == 0) {
				bh = rgd->rd_bits[0].bi_bh;
				rgd->rd_flags |= GFS2_RGF_TRIMMED;
				gfs2_trans_add_bh(rgd->rd_gl, bh, 1);
				gfs2_rgrp_out(rgd, bh->b_data);
				gfs2_rgrp_ondisk2lvb(rgd->rd_rgl, bh->b_data);
				gfs2_trans_end(sdp);
			}
		}
		gfs2_glock_dq_uninit(&gh);

		if (rgd == rgd_end)
			break;

		rgd = gfs2_rgrpd_get_next(rgd);
	}

out:
	r.len = trimmed << 9;
	if (argp && copy_to_user(argp, &r, sizeof(r)))
		return -EFAULT;

	return ret;
}

/**
 * rs_insert - insert a new multi-block reservation into the rgrp's rb_tree
 * @bi: the bitmap with the blocks
 * @ip: the inode structure
 * @biblk: the 32-bit block number relative to the start of the bitmap
 * @amount: the number of blocks to reserve
 *
 * Returns: NULL - reservation was already taken, so not inserted
 *          pointer to the inserted reservation
 */
static struct gfs2_blkreserv *rs_insert(struct gfs2_bitmap *bi,
				       struct gfs2_inode *ip, u32 biblk,
				       int amount)
{
	struct rb_node **newn, *parent = NULL;
	int rc;
	struct gfs2_blkreserv *rs = ip->i_res;
	struct gfs2_rgrpd *rgd = rs->rs_rgd;
	u64 fsblock = gfs2_bi2rgd_blk(bi, biblk) + rgd->rd_data0;

	spin_lock(&rgd->rd_rsspin);
	newn = &rgd->rd_rstree.rb_node;
	BUG_ON(!ip->i_res);
	BUG_ON(gfs2_rs_active(rs));
	/* Figure out where to put new node */
	/*BUG_ON(!gfs2_glock_is_locked_by_me(rgd->rd_gl));*/
	while (*newn) {
		struct gfs2_blkreserv *cur =
			rb_entry(*newn, struct gfs2_blkreserv, rs_node);

		parent = *newn;
		rc = rs_cmp(fsblock, amount, cur);
		if (rc > 0)
			newn = &((*newn)->rb_right);
		else if (rc < 0)
			newn = &((*newn)->rb_left);
		else {
			spin_unlock(&rgd->rd_rsspin);
			return NULL; /* reservation already in use */
		}
	}

	/* Do our reservation work */
	rs = ip->i_res;
	rs->rs_free = amount;
	rs->rs_biblk = biblk;
	rs->rs_bi = bi;
	rb_link_node(&rs->rs_node, parent, newn);
	rb_insert_color(&rs->rs_node, &rgd->rd_rstree);

	/* Do our inode accounting for the reservation */
	/*BUG_ON(!gfs2_glock_is_locked_by_me(ip->i_gl));*/

	/* Do our rgrp accounting for the reservation */
	rgd->rd_reserved += amount; /* blocks reserved */
	rgd->rd_rs_cnt++; /* number of in-tree reservations */
	spin_unlock(&rgd->rd_rsspin);
	trace_gfs2_rs(ip, rs, TRACE_RS_INSERT);
	return rs;
}

/**
 * unclaimed_blocks - return number of blocks that aren't spoken for
 */
static u32 unclaimed_blocks(struct gfs2_rgrpd *rgd)
{
	return rgd->rd_free_clone - rgd->rd_reserved;
}

/**
 * rg_mblk_search - find a group of multiple free blocks
 * @rgd: the resource group descriptor
 * @rs: the block reservation
 * @ip: pointer to the inode for which we're reserving blocks
 *
 * This is very similar to rgblk_search, except we're looking for whole
 * 64-bit words that represent a chunk of 32 free blocks. I'm only focusing
 * on aligned dwords for speed's sake.
 *
 * Returns: 0 if successful or BFITNOENT if there isn't enough free space
 */

static int rg_mblk_search(struct gfs2_rgrpd *rgd, struct gfs2_inode *ip)
{
	struct gfs2_bitmap *bi = rgd->rd_bits;
	const u32 length = rgd->rd_length;
	u32 blk;
	unsigned int buf, x, search_bytes;
	u8 *buffer = NULL;
	u8 *ptr, *end, *nonzero;
	u32 goal, rsv_bytes;
	struct gfs2_blkreserv *rs;
	u32 best_rs_bytes, unclaimed;
	int best_rs_blocks;

	/* Find bitmap block that contains bits for goal block */
	if (rgrp_contains_block(rgd, ip->i_goal))
		goal = ip->i_goal - rgd->rd_data0;
	else
		goal = rgd->rd_last_alloc;
	for (buf = 0; buf < length; buf++) {
		bi = rgd->rd_bits + buf;
		/* Convert scope of "goal" from rgrp-wide to within
		   found bit block */
		if (goal < (bi->bi_start + bi->bi_len) * GFS2_NBBY) {
			goal -= bi->bi_start * GFS2_NBBY;
			goto do_search;
		}
	}
	buf = 0;
	goal = 0;

do_search:
	best_rs_blocks = max_t(int, atomic_read(&ip->i_res->rs_sizehint),
			       (RGRP_RSRV_MINBLKS * rgd->rd_length));
	best_rs_bytes = (best_rs_blocks *
			 (1 + (RSRV_CONTENTION_FACTOR * rgd->rd_rs_cnt))) /
		GFS2_NBBY; /* 1 + is for our not-yet-created reservation */
	best_rs_bytes = ALIGN(best_rs_bytes, sizeof(u64));
	unclaimed = unclaimed_blocks(rgd);
	if (best_rs_bytes * GFS2_NBBY > unclaimed)
		best_rs_bytes = unclaimed >> GFS2_BIT_SIZE;

	for (x = 0; x <= length; x++) {
		bi = rgd->rd_bits + buf;

		if (test_bit(GBF_FULL, &bi->bi_flags))
			goto skip;

		WARN_ON(!buffer_uptodate(bi->bi_bh));
		if (bi->bi_clone)
			buffer = bi->bi_clone + bi->bi_offset;
		else
			buffer = bi->bi_bh->b_data + bi->bi_offset;

		/* We have to keep the reservations aligned on u64 boundaries
		   otherwise we could get situations where a byte can't be
		   used because it's after a reservation, but a free bit still
		   is within the reservation's area. */
		ptr = buffer + ALIGN(goal >> GFS2_BIT_SIZE, sizeof(u64));
		end = (buffer + bi->bi_len);
		while (ptr < end) {
			rsv_bytes = 0;
			if ((ptr + best_rs_bytes) <= end)
				search_bytes = best_rs_bytes;
			else
				search_bytes = end - ptr;
			BUG_ON(!search_bytes);
			nonzero = memchr_inv(ptr, 0, search_bytes);
			/* If the lot is all zeroes, reserve the whole size. If
			   there's enough zeroes to satisfy the request, use
			   what we can. If there's not enough, keep looking. */
			if (nonzero == NULL)
				rsv_bytes = search_bytes;
			else if ((nonzero - ptr) * GFS2_NBBY >=
				 ip->i_res->rs_requested)
				rsv_bytes = (nonzero - ptr);

			if (rsv_bytes) {
				blk = ((ptr - buffer) * GFS2_NBBY);
				BUG_ON(blk >= bi->bi_len * GFS2_NBBY);
				rs = rs_insert(bi, ip, blk,
					       rsv_bytes * GFS2_NBBY);
				if (IS_ERR(rs))
					return PTR_ERR(rs);
				if (rs)
					return 0;
			}
			ptr += ALIGN(search_bytes, sizeof(u64));
		}
skip:
		/* Try next bitmap block (wrap back to rgrp header
		   if at end) */
		buf++;
		buf %= length;
		goal = 0;
	}

	return BFITNOENT;
}

/**
 * try_rgrp_fit - See if a given reservation will fit in a given RG
 * @rgd: the RG data
 * @ip: the inode
 *
 * If there's room for the requested blocks to be allocated from the RG:
 * This will try to get a multi-block reservation first, and if that doesn't
 * fit, it will take what it can.
 *
 * Returns: 1 on success (it fits), 0 on failure (it doesn't fit)
 */

static int try_rgrp_fit(struct gfs2_rgrpd *rgd, struct gfs2_inode *ip)
{
	struct gfs2_blkreserv *rs = ip->i_res;

	if (rgd->rd_flags & (GFS2_RGF_NOALLOC | GFS2_RDF_ERROR))
		return 0;
	/* Look for a multi-block reservation. */
	if (unclaimed_blocks(rgd) >= RGRP_RSRV_MINBLKS &&
	    rg_mblk_search(rgd, ip) != BFITNOENT)
		return 1;
	if (unclaimed_blocks(rgd) >= rs->rs_requested)
		return 1;

	return 0;
}

/**
 * try_rgrp_unlink - Look for any unlinked, allocated, but unused inodes
 * @rgd: The rgrp
 * @last_unlinked: block address of the last dinode we unlinked
 * @skip: block address we should explicitly not unlink
 *
 * Returns: 0 if no error
 *          The inode, if one has been found, in inode.
 */

static void try_rgrp_unlink(struct gfs2_rgrpd *rgd, u64 *last_unlinked, u64 skip)
{
	u32 goal = 0, block;
	u64 no_addr;
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_glock *gl;
	struct gfs2_inode *ip;
	int error;
	int found = 0;
	struct gfs2_bitmap *bi;

	while (goal < rgd->rd_data) {
		down_write(&sdp->sd_log_flush_lock);
		block = rgblk_search(rgd, goal, GFS2_BLKST_UNLINKED, &bi);
		up_write(&sdp->sd_log_flush_lock);
		if (block == BFITNOENT)
			break;

		block = gfs2_bi2rgd_blk(bi, block);
		/* rgblk_search can return a block < goal, so we need to
		   keep it marching forward. */
		no_addr = block + rgd->rd_data0;
		goal = max(block + 1, goal + 1);
		if (*last_unlinked != NO_BLOCK && no_addr <= *last_unlinked)
			continue;
		if (no_addr == skip)
			continue;
		*last_unlinked = no_addr;

		error = gfs2_glock_get(sdp, no_addr, &gfs2_inode_glops, CREATE, &gl);
		if (error)
			continue;

		/* If the inode is already in cache, we can ignore it here
		 * because the existing inode disposal code will deal with
		 * it when all refs have gone away. Accessing gl_object like
		 * this is not safe in general. Here it is ok because we do
		 * not dereference the pointer, and we only need an approx
		 * answer to whether it is NULL or not.
		 */
		ip = gl->gl_object;

		if (ip || queue_work(gfs2_delete_workqueue, &gl->gl_delete) == 0)
			gfs2_glock_put(gl);
		else
			found++;

		/* Limit reclaim to sensible number of tasks */
		if (found > NR_CPUS)
			return;
	}

	rgd->rd_flags &= ~GFS2_RDF_CHECK;
	return;
}

/**
 * gfs2_inplace_reserve - Reserve space in the filesystem
 * @ip: the inode to reserve space for
 * @requested: the number of blocks to be reserved
 *
 * Returns: errno
 */

int gfs2_inplace_reserve(struct gfs2_inode *ip, u32 requested)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrpd *begin = NULL;
	struct gfs2_blkreserv *rs = ip->i_res;
	int error = 0, rg_locked, flags = LM_FLAG_TRY;
	u64 last_unlinked = NO_BLOCK;
	int loops = 0;

	if (sdp->sd_args.ar_rgrplvb)
		flags |= GL_SKIP;
	rs->rs_requested = requested;
	if (gfs2_assert_warn(sdp, requested)) {
		error = -EINVAL;
		goto out;
	}
	if (gfs2_rs_active(rs)) {
		begin = rs->rs_rgd;
		flags = 0; /* Yoda: Do or do not. There is no try */
	} else if (ip->i_rgd && rgrp_contains_block(ip->i_rgd, ip->i_goal)) {
		rs->rs_rgd = begin = ip->i_rgd;
	} else {
		rs->rs_rgd = begin = gfs2_blk2rgrpd(sdp, ip->i_goal, 1);
	}
	if (rs->rs_rgd == NULL)
		return -EBADSLT;

	while (loops < 3) {
		rg_locked = 0;

		if (gfs2_glock_is_locked_by_me(rs->rs_rgd->rd_gl)) {
			rg_locked = 1;
			error = 0;
		} else if (!loops && !gfs2_rs_active(rs) &&
			   rs->rs_rgd->rd_rs_cnt > RGRP_RSRV_MAX_CONTENDERS) {
			/* If the rgrp already is maxed out for contenders,
			   we can eliminate it as a "first pass" without even
			   requesting the rgrp glock. */
			error = GLR_TRYFAILED;
		} else {
			error = gfs2_glock_nq_init(rs->rs_rgd->rd_gl,
						   LM_ST_EXCLUSIVE, flags,
						   &rs->rs_rgd_gh);
			if (!error && sdp->sd_args.ar_rgrplvb) {
				error = update_rgrp_lvb(rs->rs_rgd);
				if (error) {
					gfs2_glock_dq_uninit(&rs->rs_rgd_gh);
					return error;
				}
			}
		}
		switch (error) {
		case 0:
			if (gfs2_rs_active(rs)) {
				if (unclaimed_blocks(rs->rs_rgd) +
				    rs->rs_free >= rs->rs_requested) {
					ip->i_rgd = rs->rs_rgd;
					return 0;
				}
				/* We have a multi-block reservation, but the
				   rgrp doesn't have enough free blocks to
				   satisfy the request. Free the reservation
				   and look for a suitable rgrp. */
				gfs2_rs_deltree(rs);
			}
			if (try_rgrp_fit(rs->rs_rgd, ip)) {
				if (sdp->sd_args.ar_rgrplvb)
					gfs2_rgrp_bh_get(rs->rs_rgd);
				ip->i_rgd = rs->rs_rgd;
				return 0;
			}
			if (rs->rs_rgd->rd_flags & GFS2_RDF_CHECK) {
				if (sdp->sd_args.ar_rgrplvb)
					gfs2_rgrp_bh_get(rs->rs_rgd);
				try_rgrp_unlink(rs->rs_rgd, &last_unlinked,
						ip->i_no_addr);
			}
			if (!rg_locked)
				gfs2_glock_dq_uninit(&rs->rs_rgd_gh);
			/* fall through */
		case GLR_TRYFAILED:
			rs->rs_rgd = gfs2_rgrpd_get_next(rs->rs_rgd);
			rs->rs_rgd = rs->rs_rgd ? : begin; /* if NULL, wrap */
			if (rs->rs_rgd != begin) /* If we didn't wrap */
				break;

			flags &= ~LM_FLAG_TRY;
			loops++;
			/* Check that fs hasn't grown if writing to rindex */
			if (ip == GFS2_I(sdp->sd_rindex) &&
			    !sdp->sd_rindex_uptodate) {
				error = gfs2_ri_update(ip);
				if (error)
					goto out;
			} else if (loops == 2)
				/* Flushing the log may release space */
				gfs2_log_flush(sdp, NULL);
			break;
		default:
			goto out;
		}
	}
	error = -ENOSPC;

out:
	if (error)
		rs->rs_requested = 0;
	return error;
}

/**
 * gfs2_inplace_release - release an inplace reservation
 * @ip: the inode the reservation was taken out on
 *
 * Release a reservation made by gfs2_inplace_reserve().
 */

void gfs2_inplace_release(struct gfs2_inode *ip)
{
	struct gfs2_blkreserv *rs = ip->i_res;

	if (!rs)
		return;

	if (!rs->rs_free)
		gfs2_rs_deltree(rs);

	if (rs->rs_rgd_gh.gh_gl)
		gfs2_glock_dq_uninit(&rs->rs_rgd_gh);
	rs->rs_requested = 0;
}

/**
 * gfs2_get_block_type - Check a block in a RG is of given type
 * @rgd: the resource group holding the block
 * @block: the block number
 *
 * Returns: The block type (GFS2_BLKST_*)
 */

static unsigned char gfs2_get_block_type(struct gfs2_rgrpd *rgd, u64 block)
{
	struct gfs2_bitmap *bi = NULL;
	u32 length, rgrp_block, buf_block;
	unsigned int buf;
	unsigned char type;

	length = rgd->rd_length;
	rgrp_block = block - rgd->rd_data0;

	for (buf = 0; buf < length; buf++) {
		bi = rgd->rd_bits + buf;
		if (rgrp_block < (bi->bi_start + bi->bi_len) * GFS2_NBBY)
			break;
	}

	gfs2_assert(rgd->rd_sbd, buf < length);
	buf_block = rgrp_block - bi->bi_start * GFS2_NBBY;

	type = gfs2_testbit(rgd, bi->bi_bh->b_data + bi->bi_offset,
			   bi->bi_len, buf_block);

	return type;
}

/**
 * rgblk_search - find a block in @state
 * @rgd: the resource group descriptor
 * @goal: the goal block within the RG (start here to search for avail block)
 * @state: GFS2_BLKST_XXX the before-allocation state to find
 * @rbi: address of the pointer to the bitmap containing the block found
 *
 * Walk rgrp's bitmap to find bits that represent a block in @state.
 *
 * This function never fails, because we wouldn't call it unless we
 * know (from reservation results, etc.) that a block is available.
 *
 * Scope of @goal is just within rgrp, not the whole filesystem.
 * Scope of @returned block is just within bitmap, not the whole filesystem.
 *
 * Returns: the block number found relative to the bitmap rbi
 */

static u32 rgblk_search(struct gfs2_rgrpd *rgd, u32 goal, unsigned char state,
			struct gfs2_bitmap **rbi)
{
	struct gfs2_bitmap *bi = NULL;
	const u32 length = rgd->rd_length;
	u32 biblk = BFITNOENT;
	unsigned int buf, x;
	const u8 *buffer = NULL;

	*rbi = NULL;
	/* Find bitmap block that contains bits for goal block */
	for (buf = 0; buf < length; buf++) {
		bi = rgd->rd_bits + buf;
		/* Convert scope of "goal" from rgrp-wide to within found bit block */
		if (goal < (bi->bi_start + bi->bi_len) * GFS2_NBBY) {
			goal -= bi->bi_start * GFS2_NBBY;
			goto do_search;
		}
	}
	buf = 0;
	goal = 0;

do_search:
	/* Search (up to entire) bitmap in this rgrp for allocatable block.
	   "x <= length", instead of "x < length", because we typically start
	   the search in the middle of a bit block, but if we can't find an
	   allocatable block anywhere else, we want to be able wrap around and
	   search in the first part of our first-searched bit block.  */
	for (x = 0; x <= length; x++) {
		bi = rgd->rd_bits + buf;

		if (test_bit(GBF_FULL, &bi->bi_flags) &&
		    (state == GFS2_BLKST_FREE))
			goto skip;

		/* The GFS2_BLKST_UNLINKED state doesn't apply to the clone
		   bitmaps, so we must search the originals for that. */
		buffer = bi->bi_bh->b_data + bi->bi_offset;
		WARN_ON(!buffer_uptodate(bi->bi_bh));
		if (state != GFS2_BLKST_UNLINKED && bi->bi_clone)
			buffer = bi->bi_clone + bi->bi_offset;

		while (1) {
			struct gfs2_blkreserv *rs;
			u32 rgblk;

			biblk = gfs2_bitfit(buffer, bi->bi_len, goal, state);
			if (biblk == BFITNOENT)
				break;
			/* Check if this block is reserved() */
			rgblk = gfs2_bi2rgd_blk(bi, biblk);
			rs = rs_find(rgd, rgblk);
			if (rs == NULL)
				break;

			BUG_ON(rs->rs_bi != bi);
			biblk = BFITNOENT;
			/* This should jump to the first block after the
			   reservation. */
			goal = rs->rs_biblk + rs->rs_free;
			if (goal >= bi->bi_len * GFS2_NBBY)
				break;
		}
		if (biblk != BFITNOENT)
			break;

		if ((goal == 0) && (state == GFS2_BLKST_FREE))
			set_bit(GBF_FULL, &bi->bi_flags);

		/* Try next bitmap block (wrap back to rgrp header if at end) */
skip:
		buf++;
		buf %= length;
		goal = 0;
	}

	if (biblk != BFITNOENT)
		*rbi = bi;

	return biblk;
}

/**
 * gfs2_alloc_extent - allocate an extent from a given bitmap
 * @rgd: the resource group descriptor
 * @bi: the bitmap within the rgrp
 * @blk: the block within the bitmap
 * @dinode: TRUE if the first block we allocate is for a dinode
 * @n: The extent length
 *
 * Add the found bitmap buffer to the transaction.
 * Set the found bits to @new_state to change block's allocation state.
 * Returns: starting block number of the extent (fs scope)
 */
static u64 gfs2_alloc_extent(struct gfs2_rgrpd *rgd, struct gfs2_bitmap *bi,
			     u32 blk, bool dinode, unsigned int *n)
{
	const unsigned int elen = *n;
	u32 goal, rgblk;
	const u8 *buffer = NULL;
	struct gfs2_blkreserv *rs;

	*n = 0;
	buffer = bi->bi_bh->b_data + bi->bi_offset;
	gfs2_trans_add_bh(rgd->rd_gl, bi->bi_bh, 1);
	gfs2_setbit(rgd, bi->bi_clone, bi, blk,
		    dinode ? GFS2_BLKST_DINODE : GFS2_BLKST_USED);
	(*n)++;
	goal = blk;
	while (*n < elen) {
		goal++;
		if (goal >= (bi->bi_len * GFS2_NBBY))
			break;
		rgblk = gfs2_bi2rgd_blk(bi, goal);
		rs = rs_find(rgd, rgblk);
		if (rs) /* Oops, we bumped into someone's reservation */
			break;
		if (gfs2_testbit(rgd, buffer, bi->bi_len, goal) !=
		    GFS2_BLKST_FREE)
			break;
		gfs2_setbit(rgd, bi->bi_clone, bi, goal, GFS2_BLKST_USED);
		(*n)++;
	}
	blk = gfs2_bi2rgd_blk(bi, blk);
	rgd->rd_last_alloc = blk + *n - 1;
	return rgd->rd_data0 + blk;
}

/**
 * rgblk_free - Change alloc state of given block(s)
 * @sdp: the filesystem
 * @bstart: the start of a run of blocks to free
 * @blen: the length of the block run (all must lie within ONE RG!)
 * @new_state: GFS2_BLKST_XXX the after-allocation block state
 *
 * Returns:  Resource group containing the block(s)
 */

static struct gfs2_rgrpd *rgblk_free(struct gfs2_sbd *sdp, u64 bstart,
				     u32 blen, unsigned char new_state)
{
	struct gfs2_rgrpd *rgd;
	struct gfs2_bitmap *bi = NULL;
	u32 length, rgrp_blk, buf_blk;
	unsigned int buf;

	rgd = gfs2_blk2rgrpd(sdp, bstart, 1);
	if (!rgd) {
		if (gfs2_consist(sdp))
			fs_err(sdp, "block = %llu\n", (unsigned long long)bstart);
		return NULL;
	}

	length = rgd->rd_length;

	rgrp_blk = bstart - rgd->rd_data0;

	while (blen--) {
		for (buf = 0; buf < length; buf++) {
			bi = rgd->rd_bits + buf;
			if (rgrp_blk < (bi->bi_start + bi->bi_len) * GFS2_NBBY)
				break;
		}

		gfs2_assert(rgd->rd_sbd, buf < length);

		buf_blk = rgrp_blk - bi->bi_start * GFS2_NBBY;
		rgrp_blk++;

		if (!bi->bi_clone) {
			bi->bi_clone = kmalloc(bi->bi_bh->b_size,
					       GFP_NOFS | __GFP_NOFAIL);
			memcpy(bi->bi_clone + bi->bi_offset,
			       bi->bi_bh->b_data + bi->bi_offset,
			       bi->bi_len);
		}
		gfs2_trans_add_bh(rgd->rd_gl, bi->bi_bh, 1);
		gfs2_setbit(rgd, NULL, bi, buf_blk, new_state);
	}

	return rgd;
}

/**
 * gfs2_rgrp_dump - print out an rgrp
 * @seq: The iterator
 * @gl: The glock in question
 *
 */

int gfs2_rgrp_dump(struct seq_file *seq, const struct gfs2_glock *gl)
{
	struct gfs2_rgrpd *rgd = gl->gl_object;
	struct gfs2_blkreserv *trs;
	const struct rb_node *n;

	if (rgd == NULL)
		return 0;
	gfs2_print_dbg(seq, " R: n:%llu f:%02x b:%u/%u i:%u r:%u\n",
		       (unsigned long long)rgd->rd_addr, rgd->rd_flags,
		       rgd->rd_free, rgd->rd_free_clone, rgd->rd_dinodes,
		       rgd->rd_reserved);
	spin_lock(&rgd->rd_rsspin);
	for (n = rb_first(&rgd->rd_rstree); n; n = rb_next(&trs->rs_node)) {
		trs = rb_entry(n, struct gfs2_blkreserv, rs_node);
		dump_rs(seq, trs);
	}
	spin_unlock(&rgd->rd_rsspin);
	return 0;
}

static void gfs2_rgrp_error(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	fs_warn(sdp, "rgrp %llu has an error, marking it readonly until umount\n",
		(unsigned long long)rgd->rd_addr);
	fs_warn(sdp, "umount on all nodes and run fsck.gfs2 to fix the error\n");
	gfs2_rgrp_dump(NULL, rgd->rd_gl);
	rgd->rd_flags |= GFS2_RDF_ERROR;
}

/**
 * claim_reserved_blks - Claim previously reserved blocks
 * @ip: the inode that's claiming the reservation
 * @dinode: 1 if this block is a dinode block, otherwise data block
 * @nblocks: desired extent length
 *
 * Lay claim to previously reserved blocks.
 * Returns: Starting block number of the blocks claimed.
 * Sets *nblocks to the actual extent length allocated.
 */
static u64 claim_reserved_blks(struct gfs2_inode *ip, bool dinode,
			       unsigned int *nblocks)
{
	struct gfs2_blkreserv *rs = ip->i_res;
	struct gfs2_rgrpd *rgd = rs->rs_rgd;
	struct gfs2_bitmap *bi;
	u64 start_block = gfs2_rs_startblk(rs);
	const unsigned int elen = *nblocks;

	bi = rs->rs_bi;
	gfs2_trans_add_bh(rgd->rd_gl, bi->bi_bh, 1);

	for (*nblocks = 0; *nblocks < elen && rs->rs_free; (*nblocks)++) {
		if (gfs2_testbit(rgd, bi->bi_bh->b_data + bi->bi_offset,
				 bi->bi_len, rs->rs_biblk) != GFS2_BLKST_FREE)
			break;
		gfs2_setbit(rgd, bi->bi_clone, bi, rs->rs_biblk,
			    dinode ? GFS2_BLKST_DINODE : GFS2_BLKST_USED);
		rs->rs_biblk++;
		rs->rs_free--;

		BUG_ON(!rgd->rd_reserved);
		rgd->rd_reserved--;
		dinode = false;
	}

	trace_gfs2_rs(ip, rs, TRACE_RS_CLAIM);
	if (!rs->rs_free || *nblocks != elen)
		gfs2_rs_deltree(rs);

	return start_block;
}

/**
 * gfs2_alloc_blocks - Allocate one or more blocks of data and/or a dinode
 * @ip: the inode to allocate the block for
 * @bn: Used to return the starting block number
 * @nblocks: requested number of blocks/extent length (value/result)
 * @dinode: 1 if we're allocating a dinode block, else 0
 * @generation: the generation number of the inode
 *
 * Returns: 0 or error
 */

int gfs2_alloc_blocks(struct gfs2_inode *ip, u64 *bn, unsigned int *nblocks,
		      bool dinode, u64 *generation)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *dibh;
	struct gfs2_rgrpd *rgd;
	unsigned int ndata;
	u32 goal, blk; /* block, within the rgrp scope */
	u64 block; /* block, within the file system scope */
	int error;
	struct gfs2_bitmap *bi;

	/* Only happens if there is a bug in gfs2, return something distinctive
	 * to ensure that it is noticed.
	 */
	if (ip->i_res->rs_requested == 0)
		return -ECANCELED;

	/* If we have a reservation, claim blocks from it. */
	if (gfs2_rs_active(ip->i_res)) {
		BUG_ON(!ip->i_res->rs_free);
		rgd = ip->i_res->rs_rgd;
		block = claim_reserved_blks(ip, dinode, nblocks);
		if (*nblocks)
			goto found_blocks;
	}

	rgd = ip->i_rgd;

	if (!dinode && rgrp_contains_block(rgd, ip->i_goal))
		goal = ip->i_goal - rgd->rd_data0;
	else
		goal = rgd->rd_last_alloc;

	blk = rgblk_search(rgd, goal, GFS2_BLKST_FREE, &bi);

	/* Since all blocks are reserved in advance, this shouldn't happen */
	if (blk == BFITNOENT) {
		printk(KERN_WARNING "BFITNOENT, nblocks=%u\n", *nblocks);
		printk(KERN_WARNING "FULL=%d\n",
		       test_bit(GBF_FULL, &rgd->rd_bits->bi_flags));
		goto rgrp_error;
	}

	block = gfs2_alloc_extent(rgd, bi, blk, dinode, nblocks);
found_blocks:
	ndata = *nblocks;
	if (dinode)
		ndata--;

	if (!dinode) {
		ip->i_goal = block + ndata - 1;
		error = gfs2_meta_inode_buffer(ip, &dibh);
		if (error == 0) {
			struct gfs2_dinode *di =
				(struct gfs2_dinode *)dibh->b_data;
			gfs2_trans_add_bh(ip->i_gl, dibh, 1);
			di->di_goal_meta = di->di_goal_data =
				cpu_to_be64(ip->i_goal);
			brelse(dibh);
		}
	}
	if (rgd->rd_free < *nblocks) {
		printk(KERN_WARNING "nblocks=%u\n", *nblocks);
		goto rgrp_error;
	}

	rgd->rd_free -= *nblocks;
	if (dinode) {
		rgd->rd_dinodes++;
		*generation = rgd->rd_igeneration++;
		if (*generation == 0)
			*generation = rgd->rd_igeneration++;
	}

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh, 1);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);
	gfs2_rgrp_ondisk2lvb(rgd->rd_rgl, rgd->rd_bits[0].bi_bh->b_data);

	gfs2_statfs_change(sdp, 0, -(s64)*nblocks, dinode ? 1 : 0);
	if (dinode)
		gfs2_trans_add_unrevoke(sdp, block, 1);

	/*
	 * This needs reviewing to see why we cannot do the quota change
	 * at this point in the dinode case.
	 */
	if (ndata)
		gfs2_quota_change(ip, ndata, ip->i_inode.i_uid,
				  ip->i_inode.i_gid);

	rgd->rd_free_clone -= *nblocks;
	trace_gfs2_block_alloc(ip, rgd, block, *nblocks,
			       dinode ? GFS2_BLKST_DINODE : GFS2_BLKST_USED);
	*bn = block;
	return 0;

rgrp_error:
	gfs2_rgrp_error(rgd);
	return -EIO;
}

/**
 * __gfs2_free_blocks - free a contiguous run of block(s)
 * @ip: the inode these blocks are being freed from
 * @bstart: first block of a run of contiguous blocks
 * @blen: the length of the block run
 * @meta: 1 if the blocks represent metadata
 *
 */

void __gfs2_free_blocks(struct gfs2_inode *ip, u64 bstart, u32 blen, int meta)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrpd *rgd;

	rgd = rgblk_free(sdp, bstart, blen, GFS2_BLKST_FREE);
	if (!rgd)
		return;
	trace_gfs2_block_alloc(ip, rgd, bstart, blen, GFS2_BLKST_FREE);
	rgd->rd_free += blen;
	rgd->rd_flags &= ~GFS2_RGF_TRIMMED;
	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh, 1);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);
	gfs2_rgrp_ondisk2lvb(rgd->rd_rgl, rgd->rd_bits[0].bi_bh->b_data);

	/* Directories keep their data in the metadata address space */
	if (meta || ip->i_depth)
		gfs2_meta_wipe(ip, bstart, blen);
}

/**
 * gfs2_free_meta - free a contiguous run of data block(s)
 * @ip: the inode these blocks are being freed from
 * @bstart: first block of a run of contiguous blocks
 * @blen: the length of the block run
 *
 */

void gfs2_free_meta(struct gfs2_inode *ip, u64 bstart, u32 blen)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	__gfs2_free_blocks(ip, bstart, blen, 1);
	gfs2_statfs_change(sdp, 0, +blen, 0);
	gfs2_quota_change(ip, -(s64)blen, ip->i_inode.i_uid, ip->i_inode.i_gid);
}

void gfs2_unlink_di(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_rgrpd *rgd;
	u64 blkno = ip->i_no_addr;

	rgd = rgblk_free(sdp, blkno, 1, GFS2_BLKST_UNLINKED);
	if (!rgd)
		return;
	trace_gfs2_block_alloc(ip, rgd, blkno, 1, GFS2_BLKST_UNLINKED);
	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh, 1);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);
	gfs2_rgrp_ondisk2lvb(rgd->rd_rgl, rgd->rd_bits[0].bi_bh->b_data);
	update_rgrp_lvb_unlinked(rgd, 1);
}

static void gfs2_free_uninit_di(struct gfs2_rgrpd *rgd, u64 blkno)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_rgrpd *tmp_rgd;

	tmp_rgd = rgblk_free(sdp, blkno, 1, GFS2_BLKST_FREE);
	if (!tmp_rgd)
		return;
	gfs2_assert_withdraw(sdp, rgd == tmp_rgd);

	if (!rgd->rd_dinodes)
		gfs2_consist_rgrpd(rgd);
	rgd->rd_dinodes--;
	rgd->rd_free++;

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh, 1);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);
	gfs2_rgrp_ondisk2lvb(rgd->rd_rgl, rgd->rd_bits[0].bi_bh->b_data);
	update_rgrp_lvb_unlinked(rgd, -1);

	gfs2_statfs_change(sdp, 0, +1, -1);
}


void gfs2_free_di(struct gfs2_rgrpd *rgd, struct gfs2_inode *ip)
{
	gfs2_free_uninit_di(rgd, ip->i_no_addr);
	trace_gfs2_block_alloc(ip, rgd, ip->i_no_addr, 1, GFS2_BLKST_FREE);
	gfs2_quota_change(ip, -1, ip->i_inode.i_uid, ip->i_inode.i_gid);
	gfs2_meta_wipe(ip, ip->i_no_addr, 1);
}

/**
 * gfs2_check_blk_type - Check the type of a block
 * @sdp: The superblock
 * @no_addr: The block number to check
 * @type: The block type we are looking for
 *
 * Returns: 0 if the block type matches the expected type
 *          -ESTALE if it doesn't match
 *          or -ve errno if something went wrong while checking
 */

int gfs2_check_blk_type(struct gfs2_sbd *sdp, u64 no_addr, unsigned int type)
{
	struct gfs2_rgrpd *rgd;
	struct gfs2_holder rgd_gh;
	int error = -EINVAL;

	rgd = gfs2_blk2rgrpd(sdp, no_addr, 1);
	if (!rgd)
		goto fail;

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_SHARED, 0, &rgd_gh);
	if (error)
		goto fail;

	if (gfs2_get_block_type(rgd, no_addr) != type)
		error = -ESTALE;

	gfs2_glock_dq_uninit(&rgd_gh);
fail:
	return error;
}

/**
 * gfs2_rlist_add - add a RG to a list of RGs
 * @ip: the inode
 * @rlist: the list of resource groups
 * @block: the block
 *
 * Figure out what RG a block belongs to and add that RG to the list
 *
 * FIXME: Don't use NOFAIL
 *
 */

void gfs2_rlist_add(struct gfs2_inode *ip, struct gfs2_rgrp_list *rlist,
		    u64 block)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrpd *rgd;
	struct gfs2_rgrpd **tmp;
	unsigned int new_space;
	unsigned int x;

	if (gfs2_assert_warn(sdp, !rlist->rl_ghs))
		return;

	if (ip->i_rgd && rgrp_contains_block(ip->i_rgd, block))
		rgd = ip->i_rgd;
	else
		rgd = gfs2_blk2rgrpd(sdp, block, 1);
	if (!rgd) {
		fs_err(sdp, "rlist_add: no rgrp for block %llu\n", (unsigned long long)block);
		return;
	}
	ip->i_rgd = rgd;

	for (x = 0; x < rlist->rl_rgrps; x++)
		if (rlist->rl_rgd[x] == rgd)
			return;

	if (rlist->rl_rgrps == rlist->rl_space) {
		new_space = rlist->rl_space + 10;

		tmp = kcalloc(new_space, sizeof(struct gfs2_rgrpd *),
			      GFP_NOFS | __GFP_NOFAIL);

		if (rlist->rl_rgd) {
			memcpy(tmp, rlist->rl_rgd,
			       rlist->rl_space * sizeof(struct gfs2_rgrpd *));
			kfree(rlist->rl_rgd);
		}

		rlist->rl_space = new_space;
		rlist->rl_rgd = tmp;
	}

	rlist->rl_rgd[rlist->rl_rgrps++] = rgd;
}

/**
 * gfs2_rlist_alloc - all RGs have been added to the rlist, now allocate
 *      and initialize an array of glock holders for them
 * @rlist: the list of resource groups
 * @state: the lock state to acquire the RG lock in
 *
 * FIXME: Don't use NOFAIL
 *
 */

void gfs2_rlist_alloc(struct gfs2_rgrp_list *rlist, unsigned int state)
{
	unsigned int x;

	rlist->rl_ghs = kcalloc(rlist->rl_rgrps, sizeof(struct gfs2_holder),
				GFP_NOFS | __GFP_NOFAIL);
	for (x = 0; x < rlist->rl_rgrps; x++)
		gfs2_holder_init(rlist->rl_rgd[x]->rd_gl,
				state, 0,
				&rlist->rl_ghs[x]);
}

/**
 * gfs2_rlist_free - free a resource group list
 * @list: the list of resource groups
 *
 */

void gfs2_rlist_free(struct gfs2_rgrp_list *rlist)
{
	unsigned int x;

	kfree(rlist->rl_rgd);

	if (rlist->rl_ghs) {
		for (x = 0; x < rlist->rl_rgrps; x++)
			gfs2_holder_uninit(&rlist->rl_ghs[x]);
		kfree(rlist->rl_ghs);
		rlist->rl_ghs = NULL;
	}
}


// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/gfs2_ondisk.h>
#include <linux/prefetch.h>
#include <linux/blkdev.h>
#include <linux/rbtree.h>
#include <linux/random.h>

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
#include "dir.h"

#define BFITNOENT ((u32)~0)
#define NO_BLOCK ((u64)~0)

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

struct gfs2_extent {
	struct gfs2_rbm rbm;
	u32 len;
};

static const char valid_change[16] = {
	        /* current */
	/* n */ 0, 1, 1, 1,
	/* e */ 1, 0, 0, 0,
	/* w */ 0, 0, 0, 1,
	        1, 0, 0, 0
};

static int gfs2_rbm_find(struct gfs2_rbm *rbm, u8 state, u32 *minext,
			 const struct gfs2_inode *ip, bool nowrap);


/**
 * gfs2_setbit - Set a bit in the bitmaps
 * @rbm: The position of the bit to set
 * @do_clone: Also set the clone bitmap, if it exists
 * @new_state: the new state of the block
 *
 */

static inline void gfs2_setbit(const struct gfs2_rbm *rbm, bool do_clone,
			       unsigned char new_state)
{
	unsigned char *byte1, *byte2, *end, cur_state;
	struct gfs2_bitmap *bi = rbm_bi(rbm);
	unsigned int buflen = bi->bi_bytes;
	const unsigned int bit = (rbm->offset % GFS2_NBBY) * GFS2_BIT_SIZE;

	byte1 = bi->bi_bh->b_data + bi->bi_offset + (rbm->offset / GFS2_NBBY);
	end = bi->bi_bh->b_data + bi->bi_offset + buflen;

	BUG_ON(byte1 >= end);

	cur_state = (*byte1 >> bit) & GFS2_BIT_MASK;

	if (unlikely(!valid_change[new_state * 4 + cur_state])) {
		struct gfs2_sbd *sdp = rbm->rgd->rd_sbd;

		fs_warn(sdp, "buf_blk = 0x%x old_state=%d, new_state=%d\n",
			rbm->offset, cur_state, new_state);
		fs_warn(sdp, "rgrp=0x%llx bi_start=0x%x biblk: 0x%llx\n",
			(unsigned long long)rbm->rgd->rd_addr, bi->bi_start,
			(unsigned long long)bi->bi_bh->b_blocknr);
		fs_warn(sdp, "bi_offset=0x%x bi_bytes=0x%x block=0x%llx\n",
			bi->bi_offset, bi->bi_bytes,
			(unsigned long long)gfs2_rbm_to_block(rbm));
		dump_stack();
		gfs2_consist_rgrpd(rbm->rgd);
		return;
	}
	*byte1 ^= (cur_state ^ new_state) << bit;

	if (do_clone && bi->bi_clone) {
		byte2 = bi->bi_clone + bi->bi_offset + (rbm->offset / GFS2_NBBY);
		cur_state = (*byte2 >> bit) & GFS2_BIT_MASK;
		*byte2 ^= (cur_state ^ new_state) << bit;
	}
}

/**
 * gfs2_testbit - test a bit in the bitmaps
 * @rbm: The bit to test
 * @use_clone: If true, test the clone bitmap, not the official bitmap.
 *
 * Some callers like gfs2_unaligned_extlen need to test the clone bitmaps,
 * not the "real" bitmaps, to avoid allocating recently freed blocks.
 *
 * Returns: The two bit block state of the requested bit
 */

static inline u8 gfs2_testbit(const struct gfs2_rbm *rbm, bool use_clone)
{
	struct gfs2_bitmap *bi = rbm_bi(rbm);
	const u8 *buffer;
	const u8 *byte;
	unsigned int bit;

	if (use_clone && bi->bi_clone)
		buffer = bi->bi_clone;
	else
		buffer = bi->bi_bh->b_data;
	buffer += bi->bi_offset;
	byte = buffer + (rbm->offset / GFS2_NBBY);
	bit = (rbm->offset % GFS2_NBBY) * GFS2_BIT_SIZE;

	return (*byte >> bit) & GFS2_BIT_MASK;
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
	u64 startblk = gfs2_rbm_to_block(&rs->rs_rbm);

	if (blk >= startblk + rs->rs_free)
		return 1;
	if (blk + len - 1 < startblk)
		return -1;
	return 0;
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
 * gfs2_rbm_from_block - Set the rbm based upon rgd and block number
 * @rbm: The rbm with rgd already set correctly
 * @block: The block number (filesystem relative)
 *
 * This sets the bi and offset members of an rbm based on a
 * resource group and a filesystem relative block number. The
 * resource group must be set in the rbm on entry, the bi and
 * offset members will be set by this function.
 *
 * Returns: 0 on success, or an error code
 */

static int gfs2_rbm_from_block(struct gfs2_rbm *rbm, u64 block)
{
	if (!rgrp_contains_block(rbm->rgd, block))
		return -E2BIG;
	rbm->bii = 0;
	rbm->offset = block - rbm->rgd->rd_data0;
	/* Check if the block is within the first block */
	if (rbm->offset < rbm_bi(rbm)->bi_blocks)
		return 0;

	/* Adjust for the size diff between gfs2_meta_header and gfs2_rgrp */
	rbm->offset += (sizeof(struct gfs2_rgrp) -
			sizeof(struct gfs2_meta_header)) * GFS2_NBBY;
	rbm->bii = rbm->offset / rbm->rgd->rd_sbd->sd_blocks_per_bitmap;
	rbm->offset -= rbm->bii * rbm->rgd->rd_sbd->sd_blocks_per_bitmap;
	return 0;
}

/**
 * gfs2_rbm_incr - increment an rbm structure
 * @rbm: The rbm with rgd already set correctly
 *
 * This function takes an existing rbm structure and increments it to the next
 * viable block offset.
 *
 * Returns: If incrementing the offset would cause the rbm to go past the
 *          end of the rgrp, true is returned, otherwise false.
 *
 */

static bool gfs2_rbm_incr(struct gfs2_rbm *rbm)
{
	if (rbm->offset + 1 < rbm_bi(rbm)->bi_blocks) { /* in the same bitmap */
		rbm->offset++;
		return false;
	}
	if (rbm->bii == rbm->rgd->rd_length - 1) /* at the last bitmap */
		return true;

	rbm->offset = 0;
	rbm->bii++;
	return false;
}

/**
 * gfs2_unaligned_extlen - Look for free blocks which are not byte aligned
 * @rbm: Position to search (value/result)
 * @n_unaligned: Number of unaligned blocks to check
 * @len: Decremented for each block found (terminate on zero)
 *
 * Returns: true if a non-free block is encountered
 */

static bool gfs2_unaligned_extlen(struct gfs2_rbm *rbm, u32 n_unaligned, u32 *len)
{
	u32 n;
	u8 res;

	for (n = 0; n < n_unaligned; n++) {
		res = gfs2_testbit(rbm, true);
		if (res != GFS2_BLKST_FREE)
			return true;
		(*len)--;
		if (*len == 0)
			return true;
		if (gfs2_rbm_incr(rbm))
			return true;
	}

	return false;
}

/**
 * gfs2_free_extlen - Return extent length of free blocks
 * @rrbm: Starting position
 * @len: Max length to check
 *
 * Starting at the block specified by the rbm, see how many free blocks
 * there are, not reading more than len blocks ahead. This can be done
 * using memchr_inv when the blocks are byte aligned, but has to be done
 * on a block by block basis in case of unaligned blocks. Also this
 * function can cope with bitmap boundaries (although it must stop on
 * a resource group boundary)
 *
 * Returns: Number of free blocks in the extent
 */

static u32 gfs2_free_extlen(const struct gfs2_rbm *rrbm, u32 len)
{
	struct gfs2_rbm rbm = *rrbm;
	u32 n_unaligned = rbm.offset & 3;
	u32 size = len;
	u32 bytes;
	u32 chunk_size;
	u8 *ptr, *start, *end;
	u64 block;
	struct gfs2_bitmap *bi;

	if (n_unaligned &&
	    gfs2_unaligned_extlen(&rbm, 4 - n_unaligned, &len))
		goto out;

	n_unaligned = len & 3;
	/* Start is now byte aligned */
	while (len > 3) {
		bi = rbm_bi(&rbm);
		start = bi->bi_bh->b_data;
		if (bi->bi_clone)
			start = bi->bi_clone;
		start += bi->bi_offset;
		end = start + bi->bi_bytes;
		BUG_ON(rbm.offset & 3);
		start += (rbm.offset / GFS2_NBBY);
		bytes = min_t(u32, len / GFS2_NBBY, (end - start));
		ptr = memchr_inv(start, 0, bytes);
		chunk_size = ((ptr == NULL) ? bytes : (ptr - start));
		chunk_size *= GFS2_NBBY;
		BUG_ON(len < chunk_size);
		len -= chunk_size;
		block = gfs2_rbm_to_block(&rbm);
		if (gfs2_rbm_from_block(&rbm, block + chunk_size)) {
			n_unaligned = 0;
			break;
		}
		if (ptr) {
			n_unaligned = 3;
			break;
		}
		n_unaligned = len & 3;
	}

	/* Deal with any bits left over at the end */
	if (n_unaligned)
		gfs2_unaligned_extlen(&rbm, n_unaligned, &len);
out:
	return size - len;
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
						  bi->bi_bytes, x);
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

/**
 * gfs2_blk2rgrpd - Find resource group for a given data/meta block number
 * @sdp: The GFS2 superblock
 * @blk: The data block number
 * @exact: True if this needs to be an exact match
 *
 * The @exact argument should be set to true by most callers. The exception
 * is when we need to match blocks which are not represented by the rgrp
 * bitmap, but which are part of the rgrp (i.e. padding blocks) which are
 * there for alignment purposes. Another way of looking at it is that @exact
 * matches only valid data/metadata blocks, but with @exact false, it will
 * match any block within the extent of the rgrp.
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

void check_and_update_goal(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	if (!ip->i_goal || gfs2_blk2rgrpd(sdp, ip->i_goal, 1) == NULL)
		ip->i_goal = ip->i_no_addr;
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
 * gfs2_rsqa_alloc - make sure we have a reservation assigned to the inode
 *                 plus a quota allocations data structure, if necessary
 * @ip: the inode for this reservation
 */
int gfs2_rsqa_alloc(struct gfs2_inode *ip)
{
	return gfs2_qa_alloc(ip);
}

static void dump_rs(struct seq_file *seq, const struct gfs2_blkreserv *rs,
		    const char *fs_id_buf)
{
	struct gfs2_inode *ip = container_of(rs, struct gfs2_inode, i_res);

	gfs2_print_dbg(seq, "%s  B: n:%llu s:%llu b:%u f:%u\n", fs_id_buf,
		       (unsigned long long)ip->i_no_addr,
		       (unsigned long long)gfs2_rbm_to_block(&rs->rs_rbm),
		       rs->rs_rbm.offset, rs->rs_free);
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

	rgd = rs->rs_rbm.rgd;
	trace_gfs2_rs(rs, TRACE_RS_TREEDEL);
	rb_erase(&rs->rs_node, &rgd->rd_rstree);
	RB_CLEAR_NODE(&rs->rs_node);

	if (rs->rs_free) {
		u64 last_block = gfs2_rbm_to_block(&rs->rs_rbm) +
				 rs->rs_free - 1;
		struct gfs2_rbm last_rbm = { .rgd = rs->rs_rbm.rgd, };
		struct gfs2_bitmap *start, *last;

		/* return reserved blocks to the rgrp */
		BUG_ON(rs->rs_rbm.rgd->rd_reserved < rs->rs_free);
		rs->rs_rbm.rgd->rd_reserved -= rs->rs_free;
		/* The rgrp extent failure point is likely not to increase;
		   it will only do so if the freed blocks are somehow
		   contiguous with a span of free blocks that follows. Still,
		   it will force the number to be recalculated later. */
		rgd->rd_extfail_pt += rs->rs_free;
		rs->rs_free = 0;
		if (gfs2_rbm_from_block(&last_rbm, last_block))
			return;
		start = rbm_bi(&rs->rs_rbm);
		last = rbm_bi(&last_rbm);
		do
			clear_bit(GBF_FULL, &start->bi_flags);
		while (start++ != last);
	}
}

/**
 * gfs2_rs_deltree - remove a multi-block reservation from the rgd tree
 * @rs: The reservation to remove
 *
 */
void gfs2_rs_deltree(struct gfs2_blkreserv *rs)
{
	struct gfs2_rgrpd *rgd;

	rgd = rs->rs_rbm.rgd;
	if (rgd) {
		spin_lock(&rgd->rd_rsspin);
		__rs_deltree(rs);
		BUG_ON(rs->rs_free);
		spin_unlock(&rgd->rd_rsspin);
	}
}

/**
 * gfs2_rsqa_delete - delete a multi-block reservation and quota allocation
 * @ip: The inode for this reservation
 * @wcount: The inode's write count, or NULL
 *
 */
void gfs2_rsqa_delete(struct gfs2_inode *ip, atomic_t *wcount)
{
	down_write(&ip->i_rw_mutex);
	if ((wcount == NULL) || (atomic_read(wcount) <= 1))
		gfs2_rs_deltree(&ip->i_res);
	up_write(&ip->i_rw_mutex);
	gfs2_qa_delete(ip, wcount);
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
			glock_clear_object(gl, rgd);
			gfs2_rgrp_brelse(rgd);
			gfs2_glock_put(gl);
		}

		gfs2_free_clones(rgd);
		kfree(rgd->rd_bits);
		rgd->rd_bits = NULL;
		return_all_reservations(rgd);
		kmem_cache_free(gfs2_rgrpd_cachep, rgd);
	}
}

static void gfs2_rindex_print(const struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;

	fs_info(sdp, "ri_addr = %llu\n", (unsigned long long)rgd->rd_addr);
	fs_info(sdp, "ri_length = %u\n", rgd->rd_length);
	fs_info(sdp, "ri_data0 = %llu\n", (unsigned long long)rgd->rd_data0);
	fs_info(sdp, "ri_data = %u\n", rgd->rd_data);
	fs_info(sdp, "ri_bitbytes = %u\n", rgd->rd_bitbytes);
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
			bi->bi_bytes = bytes;
			bi->bi_blocks = bytes * GFS2_NBBY;
		/* header block */
		} else if (x == 0) {
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_rgrp);
			bi->bi_offset = sizeof(struct gfs2_rgrp);
			bi->bi_start = 0;
			bi->bi_bytes = bytes;
			bi->bi_blocks = bytes * GFS2_NBBY;
		/* last block */
		} else if (x + 1 == length) {
			bytes = bytes_left;
			bi->bi_offset = sizeof(struct gfs2_meta_header);
			bi->bi_start = rgd->rd_bitbytes - bytes_left;
			bi->bi_bytes = bytes;
			bi->bi_blocks = bytes * GFS2_NBBY;
		/* other blocks */
		} else {
			bytes = sdp->sd_sb.sb_bsize -
				sizeof(struct gfs2_meta_header);
			bi->bi_offset = sizeof(struct gfs2_meta_header);
			bi->bi_start = rgd->rd_bitbytes - bytes_left;
			bi->bi_bytes = bytes;
			bi->bi_blocks = bytes * GFS2_NBBY;
		}

		bytes_left -= bytes;
	}

	if (bytes_left) {
		gfs2_consist_rgrpd(rgd);
		return -EIO;
	}
	bi = rgd->rd_bits + (length - 1);
	if ((bi->bi_start + bi->bi_bytes) * GFS2_NBBY != rgd->rd_data) {
		if (gfs2_consist_rgrpd(rgd)) {
			gfs2_rindex_print(rgd);
			fs_err(sdp, "start=%u len=%u offset=%u\n",
			       bi->bi_start, bi->bi_bytes, bi->bi_offset);
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
	const unsigned bsize = sdp->sd_sb.sb_bsize;
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

	rgd->rd_rgl = (struct gfs2_rgrp_lvb *)rgd->rd_gl->gl_lksb.sb_lvbptr;
	rgd->rd_flags &= ~(GFS2_RDF_UPTODATE | GFS2_RDF_PREFERRED);
	if (rgd->rd_data > sdp->sd_max_rg_data)
		sdp->sd_max_rg_data = rgd->rd_data;
	spin_lock(&sdp->sd_rindex_spin);
	error = rgd_insert(rgd);
	spin_unlock(&sdp->sd_rindex_spin);
	if (!error) {
		glock_set_object(rgd->rd_gl, rgd);
		rgd->rd_gl->gl_vm.start = (rgd->rd_addr * bsize) & PAGE_MASK;
		rgd->rd_gl->gl_vm.end = PAGE_ALIGN((rgd->rd_addr +
						    rgd->rd_length) * bsize) - 1;
		return 0;
	}

	error = 0; /* someone else read in the rgrp; free it and ignore it */
	gfs2_glock_put(rgd->rd_gl);

fail:
	kfree(rgd->rd_bits);
	rgd->rd_bits = NULL;
	kmem_cache_free(gfs2_rgrpd_cachep, rgd);
	return error;
}

/**
 * set_rgrp_preferences - Run all the rgrps, selecting some we prefer to use
 * @sdp: the GFS2 superblock
 *
 * The purpose of this function is to select a subset of the resource groups
 * and mark them as PREFERRED. We do it in such a way that each node prefers
 * to use a unique set of rgrps to minimize glock contention.
 */
static void set_rgrp_preferences(struct gfs2_sbd *sdp)
{
	struct gfs2_rgrpd *rgd, *first;
	int i;

	/* Skip an initial number of rgrps, based on this node's journal ID.
	   That should start each node out on its own set. */
	rgd = gfs2_rgrpd_get_first(sdp);
	for (i = 0; i < sdp->sd_lockstruct.ls_jid; i++)
		rgd = gfs2_rgrpd_get_next(rgd);
	first = rgd;

	do {
		rgd->rd_flags |= GFS2_RDF_PREFERRED;
		for (i = 0; i < sdp->sd_journals; i++) {
			rgd = gfs2_rgrpd_get_next(rgd);
			if (!rgd || rgd == first)
				break;
		}
	} while (rgd && rgd != first);
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

	set_rgrp_preferences(sdp);

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
	/* rd_data0, rd_data and rd_bitbytes already set from rindex */
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

static void gfs2_rgrp_out(struct gfs2_rgrpd *rgd, void *buf)
{
	struct gfs2_rgrpd *next = gfs2_rgrpd_get_next(rgd);
	struct gfs2_rgrp *str = buf;
	u32 crc;

	str->rg_flags = cpu_to_be32(rgd->rd_flags & ~GFS2_RDF_MASK);
	str->rg_free = cpu_to_be32(rgd->rd_free);
	str->rg_dinodes = cpu_to_be32(rgd->rd_dinodes);
	if (next == NULL)
		str->rg_skip = 0;
	else if (next->rd_addr > rgd->rd_addr)
		str->rg_skip = cpu_to_be32(next->rd_addr - rgd->rd_addr);
	str->rg_igeneration = cpu_to_be64(rgd->rd_igeneration);
	str->rg_data0 = cpu_to_be64(rgd->rd_data0);
	str->rg_data = cpu_to_be32(rgd->rd_data);
	str->rg_bitbytes = cpu_to_be32(rgd->rd_bitbytes);
	str->rg_crc = 0;
	crc = gfs2_disk_hash(buf, sizeof(struct gfs2_rgrp));
	str->rg_crc = cpu_to_be32(crc);

	memset(&str->rg_reserved, 0, sizeof(str->rg_reserved));
	gfs2_rgrp_ondisk2lvb(rgd->rd_rgl, buf);
}

static int gfs2_rgrp_lvb_valid(struct gfs2_rgrpd *rgd)
{
	struct gfs2_rgrp_lvb *rgl = rgd->rd_rgl;
	struct gfs2_rgrp *str = (struct gfs2_rgrp *)rgd->rd_bits[0].bi_bh->b_data;
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	int valid = 1;

	if (rgl->rl_flags != str->rg_flags) {
		fs_warn(sdp, "GFS2: rgd: %llu lvb flag mismatch %u/%u",
			(unsigned long long)rgd->rd_addr,
		       be32_to_cpu(rgl->rl_flags), be32_to_cpu(str->rg_flags));
		valid = 0;
	}
	if (rgl->rl_free != str->rg_free) {
		fs_warn(sdp, "GFS2: rgd: %llu lvb free mismatch %u/%u",
			(unsigned long long)rgd->rd_addr,
			be32_to_cpu(rgl->rl_free), be32_to_cpu(str->rg_free));
		valid = 0;
	}
	if (rgl->rl_dinodes != str->rg_dinodes) {
		fs_warn(sdp, "GFS2: rgd: %llu lvb dinode mismatch %u/%u",
			(unsigned long long)rgd->rd_addr,
			be32_to_cpu(rgl->rl_dinodes),
			be32_to_cpu(str->rg_dinodes));
		valid = 0;
	}
	if (rgl->rl_igeneration != str->rg_igeneration) {
		fs_warn(sdp, "GFS2: rgd: %llu lvb igen mismatch %llu/%llu",
			(unsigned long long)rgd->rd_addr,
			(unsigned long long)be64_to_cpu(rgl->rl_igeneration),
			(unsigned long long)be64_to_cpu(str->rg_igeneration));
		valid = 0;
	}
	return valid;
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
		while (goal < bi->bi_blocks) {
			goal = gfs2_bitfit(buffer, bi->bi_bytes, goal,
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
 * Caller must eventually call gfs2_rgrp_brelse() to free the bitmaps.
 *
 * Returns: errno
 */

static int gfs2_rgrp_bh_get(struct gfs2_rgrpd *rgd)
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
		error = gfs2_meta_read(gl, rgd->rd_addr + x, 0, 0, &bi->bi_bh);
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
		/* max out the rgrp allocation failure point */
		rgd->rd_extfail_pt = rgd->rd_free;
	}
	if (cpu_to_be32(GFS2_MAGIC) != rgd->rd_rgl->rl_magic) {
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

static int update_rgrp_lvb(struct gfs2_rgrpd *rgd)
{
	u32 rl_flags;

	if (rgd->rd_flags & GFS2_RDF_UPTODATE)
		return 0;

	if (cpu_to_be32(GFS2_MAGIC) != rgd->rd_rgl->rl_magic)
		return gfs2_rgrp_bh_get(rgd);

	rl_flags = be32_to_cpu(rgd->rd_rgl->rl_flags);
	rl_flags &= ~GFS2_RDF_MASK;
	rgd->rd_flags &= GFS2_RDF_MASK;
	rgd->rd_flags |= (rl_flags | GFS2_RDF_CHECK);
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
	return gfs2_rgrp_bh_get(rgd);
}

/**
 * gfs2_rgrp_brelse - Release RG bitmaps read in with gfs2_rgrp_bh_get()
 * @rgd: The resource group
 *
 */

void gfs2_rgrp_brelse(struct gfs2_rgrpd *rgd)
{
	int x, length = rgd->rd_length;

	for (x = 0; x < length; x++) {
		struct gfs2_bitmap *bi = rgd->rd_bits + x;
		if (bi->bi_bh) {
			brelse(bi->bi_bh);
			bi->bi_bh = NULL;
		}
	}

}

/**
 * gfs2_rgrp_go_unlock - Unlock a rgrp glock
 * @gh: The glock holder for the resource group
 *
 */

void gfs2_rgrp_go_unlock(struct gfs2_holder *gh)
{
	struct gfs2_rgrpd *rgd = gh->gh_gl->gl_object;
	int demote_requested = test_bit(GLF_DEMOTE, &gh->gh_gl->gl_flags) |
		test_bit(GLF_PENDING_DEMOTE, &gh->gh_gl->gl_flags);

	if (rgd && demote_requested)
		gfs2_rgrp_brelse(rgd);
}

int gfs2_rgrp_send_discards(struct gfs2_sbd *sdp, u64 offset,
			     struct buffer_head *bh,
			     const struct gfs2_bitmap *bi, unsigned minlen, u64 *ptrimmed)
{
	struct super_block *sb = sdp->sd_vfs;
	u64 blk;
	sector_t start = 0;
	sector_t nr_blks = 0;
	int rv;
	unsigned int x;
	u32 trimmed = 0;
	u8 diff;

	for (x = 0; x < bi->bi_bytes; x++) {
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
		while(diff) {
			if (diff & 1) {
				if (nr_blks == 0)
					goto start_new_extent;
				if ((start + nr_blks) != blk) {
					if (nr_blks >= minlen) {
						rv = sb_issue_discard(sb,
							start, nr_blks,
							GFP_NOFS, 0);
						if (rv)
							goto fail;
						trimmed += nr_blks;
					}
					nr_blks = 0;
start_new_extent:
					start = blk;
				}
				nr_blks++;
			}
			diff >>= 2;
			blk++;
		}
	}
	if (nr_blks >= minlen) {
		rv = sb_issue_discard(sb, start, nr_blks, GFP_NOFS, 0);
		if (rv)
			goto fail;
		trimmed += nr_blks;
	}
	if (ptrimmed)
		*ptrimmed = trimmed;
	return 0;

fail:
	if (sdp->sd_args.ar_discard)
		fs_warn(sdp, "error %d on discard request, turning discards off for this filesystem\n", rv);
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
	struct inode *inode = file_inode(filp);
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
	u64 start, end, minlen;
	unsigned int x;
	unsigned bs_shift = sdp->sd_sb.sb_bsize_shift;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!blk_queue_discard(q))
		return -EOPNOTSUPP;

	if (copy_from_user(&r, argp, sizeof(r)))
		return -EFAULT;

	ret = gfs2_rindex_update(sdp);
	if (ret)
		return ret;

	start = r.start >> bs_shift;
	end = start + (r.len >> bs_shift);
	minlen = max_t(u64, r.minlen,
		       q->limits.discard_granularity) >> bs_shift;

	if (end <= start || minlen > sdp->sd_max_rg_data)
		return -EINVAL;

	rgd = gfs2_blk2rgrpd(sdp, start, 0);
	rgd_end = gfs2_blk2rgrpd(sdp, end, 0);

	if ((gfs2_rgrpd_get_first(sdp) == gfs2_rgrpd_get_next(rgd_end))
	    && (start > rgd_end->rd_data0 + rgd_end->rd_data))
		return -EINVAL; /* start is beyond the end of the fs */

	while (1) {

		ret = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &gh);
		if (ret)
			goto out;

		if (!(rgd->rd_flags & GFS2_RGF_TRIMMED)) {
			/* Trim each bitmap in the rgrp */
			for (x = 0; x < rgd->rd_length; x++) {
				struct gfs2_bitmap *bi = rgd->rd_bits + x;
				ret = gfs2_rgrp_send_discards(sdp,
						rgd->rd_data0, NULL, bi, minlen,
						&amt);
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
				gfs2_trans_add_meta(rgd->rd_gl, bh);
				gfs2_rgrp_out(rgd, bh->b_data);
				gfs2_trans_end(sdp);
			}
		}
		gfs2_glock_dq_uninit(&gh);

		if (rgd == rgd_end)
			break;

		rgd = gfs2_rgrpd_get_next(rgd);
	}

out:
	r.len = trimmed << bs_shift;
	if (copy_to_user(argp, &r, sizeof(r)))
		return -EFAULT;

	return ret;
}

/**
 * rs_insert - insert a new multi-block reservation into the rgrp's rb_tree
 * @ip: the inode structure
 *
 */
static void rs_insert(struct gfs2_inode *ip)
{
	struct rb_node **newn, *parent = NULL;
	int rc;
	struct gfs2_blkreserv *rs = &ip->i_res;
	struct gfs2_rgrpd *rgd = rs->rs_rbm.rgd;
	u64 fsblock = gfs2_rbm_to_block(&rs->rs_rbm);

	BUG_ON(gfs2_rs_active(rs));

	spin_lock(&rgd->rd_rsspin);
	newn = &rgd->rd_rstree.rb_node;
	while (*newn) {
		struct gfs2_blkreserv *cur =
			rb_entry(*newn, struct gfs2_blkreserv, rs_node);

		parent = *newn;
		rc = rs_cmp(fsblock, rs->rs_free, cur);
		if (rc > 0)
			newn = &((*newn)->rb_right);
		else if (rc < 0)
			newn = &((*newn)->rb_left);
		else {
			spin_unlock(&rgd->rd_rsspin);
			WARN_ON(1);
			return;
		}
	}

	rb_link_node(&rs->rs_node, parent, newn);
	rb_insert_color(&rs->rs_node, &rgd->rd_rstree);

	/* Do our rgrp accounting for the reservation */
	rgd->rd_reserved += rs->rs_free; /* blocks reserved */
	spin_unlock(&rgd->rd_rsspin);
	trace_gfs2_rs(rs, TRACE_RS_INSERT);
}

/**
 * rgd_free - return the number of free blocks we can allocate.
 * @rgd: the resource group
 *
 * This function returns the number of free blocks for an rgrp.
 * That's the clone-free blocks (blocks that are free, not including those
 * still being used for unlinked files that haven't been deleted.)
 *
 * It also subtracts any blocks reserved by someone else, but does not
 * include free blocks that are still part of our current reservation,
 * because obviously we can (and will) allocate them.
 */
static inline u32 rgd_free(struct gfs2_rgrpd *rgd, struct gfs2_blkreserv *rs)
{
	u32 tot_reserved, tot_free;

	if (WARN_ON_ONCE(rgd->rd_reserved < rs->rs_free))
		return 0;
	tot_reserved = rgd->rd_reserved - rs->rs_free;

	if (rgd->rd_free_clone < tot_reserved)
		tot_reserved = 0;

	tot_free = rgd->rd_free_clone - tot_reserved;

	return tot_free;
}

/**
 * rg_mblk_search - find a group of multiple free blocks to form a reservation
 * @rgd: the resource group descriptor
 * @ip: pointer to the inode for which we're reserving blocks
 * @ap: the allocation parameters
 *
 */

static void rg_mblk_search(struct gfs2_rgrpd *rgd, struct gfs2_inode *ip,
			   const struct gfs2_alloc_parms *ap)
{
	struct gfs2_rbm rbm = { .rgd = rgd, };
	u64 goal;
	struct gfs2_blkreserv *rs = &ip->i_res;
	u32 extlen;
	u32 free_blocks = rgd_free(rgd, rs);
	int ret;
	struct inode *inode = &ip->i_inode;

	if (S_ISDIR(inode->i_mode))
		extlen = 1;
	else {
		extlen = max_t(u32, atomic_read(&ip->i_sizehint), ap->target);
		extlen = clamp(extlen, (u32)RGRP_RSRV_MINBLKS, free_blocks);
	}
	if ((rgd->rd_free_clone < rgd->rd_reserved) || (free_blocks < extlen))
		return;

	/* Find bitmap block that contains bits for goal block */
	if (rgrp_contains_block(rgd, ip->i_goal))
		goal = ip->i_goal;
	else
		goal = rgd->rd_last_alloc + rgd->rd_data0;

	if (WARN_ON(gfs2_rbm_from_block(&rbm, goal)))
		return;

	ret = gfs2_rbm_find(&rbm, GFS2_BLKST_FREE, &extlen, ip, true);
	if (ret == 0) {
		rs->rs_rbm = rbm;
		rs->rs_free = extlen;
		rs_insert(ip);
	} else {
		if (goal == rgd->rd_last_alloc + rgd->rd_data0)
			rgd->rd_last_alloc = 0;
	}
}

/**
 * gfs2_next_unreserved_block - Return next block that is not reserved
 * @rgd: The resource group
 * @block: The starting block
 * @length: The required length
 * @ip: Ignore any reservations for this inode
 *
 * If the block does not appear in any reservation, then return the
 * block number unchanged. If it does appear in the reservation, then
 * keep looking through the tree of reservations in order to find the
 * first block number which is not reserved.
 */

static u64 gfs2_next_unreserved_block(struct gfs2_rgrpd *rgd, u64 block,
				      u32 length,
				      const struct gfs2_inode *ip)
{
	struct gfs2_blkreserv *rs;
	struct rb_node *n;
	int rc;

	spin_lock(&rgd->rd_rsspin);
	n = rgd->rd_rstree.rb_node;
	while (n) {
		rs = rb_entry(n, struct gfs2_blkreserv, rs_node);
		rc = rs_cmp(block, length, rs);
		if (rc < 0)
			n = n->rb_left;
		else if (rc > 0)
			n = n->rb_right;
		else
			break;
	}

	if (n) {
		while ((rs_cmp(block, length, rs) == 0) && (&ip->i_res != rs)) {
			block = gfs2_rbm_to_block(&rs->rs_rbm) + rs->rs_free;
			n = n->rb_right;
			if (n == NULL)
				break;
			rs = rb_entry(n, struct gfs2_blkreserv, rs_node);
		}
	}

	spin_unlock(&rgd->rd_rsspin);
	return block;
}

/**
 * gfs2_reservation_check_and_update - Check for reservations during block alloc
 * @rbm: The current position in the resource group
 * @ip: The inode for which we are searching for blocks
 * @minext: The minimum extent length
 * @maxext: A pointer to the maximum extent structure
 *
 * This checks the current position in the rgrp to see whether there is
 * a reservation covering this block. If not then this function is a
 * no-op. If there is, then the position is moved to the end of the
 * contiguous reservation(s) so that we are pointing at the first
 * non-reserved block.
 *
 * Returns: 0 if no reservation, 1 if @rbm has changed, otherwise an error
 */

static int gfs2_reservation_check_and_update(struct gfs2_rbm *rbm,
					     const struct gfs2_inode *ip,
					     u32 minext,
					     struct gfs2_extent *maxext)
{
	u64 block = gfs2_rbm_to_block(rbm);
	u32 extlen = 1;
	u64 nblock;
	int ret;

	/*
	 * If we have a minimum extent length, then skip over any extent
	 * which is less than the min extent length in size.
	 */
	if (minext) {
		extlen = gfs2_free_extlen(rbm, minext);
		if (extlen <= maxext->len)
			goto fail;
	}

	/*
	 * Check the extent which has been found against the reservations
	 * and skip if parts of it are already reserved
	 */
	nblock = gfs2_next_unreserved_block(rbm->rgd, block, extlen, ip);
	if (nblock == block) {
		if (!minext || extlen >= minext)
			return 0;

		if (extlen > maxext->len) {
			maxext->len = extlen;
			maxext->rbm = *rbm;
		}
fail:
		nblock = block + extlen;
	}
	ret = gfs2_rbm_from_block(rbm, nblock);
	if (ret < 0)
		return ret;
	return 1;
}

/**
 * gfs2_rbm_find - Look for blocks of a particular state
 * @rbm: Value/result starting position and final position
 * @state: The state which we want to find
 * @minext: Pointer to the requested extent length (NULL for a single block)
 *          This is updated to be the actual reservation size.
 * @ip: If set, check for reservations
 * @nowrap: Stop looking at the end of the rgrp, rather than wrapping
 *          around until we've reached the starting point.
 *
 * Side effects:
 * - If looking for free blocks, we set GBF_FULL on each bitmap which
 *   has no free blocks in it.
 * - If looking for free blocks, we set rd_extfail_pt on each rgrp which
 *   has come up short on a free block search.
 *
 * Returns: 0 on success, -ENOSPC if there is no block of the requested state
 */

static int gfs2_rbm_find(struct gfs2_rbm *rbm, u8 state, u32 *minext,
			 const struct gfs2_inode *ip, bool nowrap)
{
	bool scan_from_start = rbm->bii == 0 && rbm->offset == 0;
	struct buffer_head *bh;
	int last_bii;
	u32 offset;
	u8 *buffer;
	bool wrapped = false;
	int ret;
	struct gfs2_bitmap *bi;
	struct gfs2_extent maxext = { .rbm.rgd = rbm->rgd, };

	/*
	 * Determine the last bitmap to search.  If we're not starting at the
	 * beginning of a bitmap, we need to search that bitmap twice to scan
	 * the entire resource group.
	 */
	last_bii = rbm->bii - (rbm->offset == 0);

	while(1) {
		bi = rbm_bi(rbm);
		if ((ip == NULL || !gfs2_rs_active(&ip->i_res)) &&
		    test_bit(GBF_FULL, &bi->bi_flags) &&
		    (state == GFS2_BLKST_FREE))
			goto next_bitmap;

		bh = bi->bi_bh;
		buffer = bh->b_data + bi->bi_offset;
		WARN_ON(!buffer_uptodate(bh));
		if (state != GFS2_BLKST_UNLINKED && bi->bi_clone)
			buffer = bi->bi_clone + bi->bi_offset;
		offset = gfs2_bitfit(buffer, bi->bi_bytes, rbm->offset, state);
		if (offset == BFITNOENT) {
			if (state == GFS2_BLKST_FREE && rbm->offset == 0)
				set_bit(GBF_FULL, &bi->bi_flags);
			goto next_bitmap;
		}
		rbm->offset = offset;
		if (ip == NULL)
			return 0;

		ret = gfs2_reservation_check_and_update(rbm, ip,
							minext ? *minext : 0,
							&maxext);
		if (ret == 0)
			return 0;
		if (ret > 0)
			goto next_iter;
		if (ret == -E2BIG) {
			rbm->bii = 0;
			rbm->offset = 0;
			goto res_covered_end_of_rgrp;
		}
		return ret;

next_bitmap:	/* Find next bitmap in the rgrp */
		rbm->offset = 0;
		rbm->bii++;
		if (rbm->bii == rbm->rgd->rd_length)
			rbm->bii = 0;
res_covered_end_of_rgrp:
		if (rbm->bii == 0) {
			if (wrapped)
				break;
			wrapped = true;
			if (nowrap)
				break;
		}
next_iter:
		/* Have we scanned the entire resource group? */
		if (wrapped && rbm->bii > last_bii)
			break;
	}

	if (minext == NULL || state != GFS2_BLKST_FREE)
		return -ENOSPC;

	/* If the extent was too small, and it's smaller than the smallest
	   to have failed before, remember for future reference that it's
	   useless to search this rgrp again for this amount or more. */
	if (wrapped && (scan_from_start || rbm->bii > last_bii) &&
	    *minext < rbm->rgd->rd_extfail_pt)
		rbm->rgd->rd_extfail_pt = *minext;

	/* If the maximum extent we found is big enough to fulfill the
	   minimum requirements, use it anyway. */
	if (maxext.len) {
		*rbm = maxext.rbm;
		*minext = maxext.len;
		return 0;
	}

	return -ENOSPC;
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
	u64 block;
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_glock *gl;
	struct gfs2_inode *ip;
	int error;
	int found = 0;
	struct gfs2_rbm rbm = { .rgd = rgd, .bii = 0, .offset = 0 };

	while (1) {
		down_write(&sdp->sd_log_flush_lock);
		error = gfs2_rbm_find(&rbm, GFS2_BLKST_UNLINKED, NULL, NULL,
				      true);
		up_write(&sdp->sd_log_flush_lock);
		if (error == -ENOSPC)
			break;
		if (WARN_ON_ONCE(error))
			break;

		block = gfs2_rbm_to_block(&rbm);
		if (gfs2_rbm_from_block(&rbm, block + 1))
			break;
		if (*last_unlinked != NO_BLOCK && block <= *last_unlinked)
			continue;
		if (block == skip)
			continue;
		*last_unlinked = block;

		error = gfs2_glock_get(sdp, block, &gfs2_iopen_glops, CREATE, &gl);
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
 * gfs2_rgrp_congested - Use stats to figure out whether an rgrp is congested
 * @rgd: The rgrp in question
 * @loops: An indication of how picky we can be (0=very, 1=less so)
 *
 * This function uses the recently added glock statistics in order to
 * figure out whether a parciular resource group is suffering from
 * contention from multiple nodes. This is done purely on the basis
 * of timings, since this is the only data we have to work with and
 * our aim here is to reject a resource group which is highly contended
 * but (very important) not to do this too often in order to ensure that
 * we do not land up introducing fragmentation by changing resource
 * groups when not actually required.
 *
 * The calculation is fairly simple, we want to know whether the SRTTB
 * (i.e. smoothed round trip time for blocking operations) to acquire
 * the lock for this rgrp's glock is significantly greater than the
 * time taken for resource groups on average. We introduce a margin in
 * the form of the variable @var which is computed as the sum of the two
 * respective variences, and multiplied by a factor depending on @loops
 * and whether we have a lot of data to base the decision on. This is
 * then tested against the square difference of the means in order to
 * decide whether the result is statistically significant or not.
 *
 * Returns: A boolean verdict on the congestion status
 */

static bool gfs2_rgrp_congested(const struct gfs2_rgrpd *rgd, int loops)
{
	const struct gfs2_glock *gl = rgd->rd_gl;
	const struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct gfs2_lkstats *st;
	u64 r_dcount, l_dcount;
	u64 l_srttb, a_srttb = 0;
	s64 srttb_diff;
	u64 sqr_diff;
	u64 var;
	int cpu, nonzero = 0;

	preempt_disable();
	for_each_present_cpu(cpu) {
		st = &per_cpu_ptr(sdp->sd_lkstats, cpu)->lkstats[LM_TYPE_RGRP];
		if (st->stats[GFS2_LKS_SRTTB]) {
			a_srttb += st->stats[GFS2_LKS_SRTTB];
			nonzero++;
		}
	}
	st = &this_cpu_ptr(sdp->sd_lkstats)->lkstats[LM_TYPE_RGRP];
	if (nonzero)
		do_div(a_srttb, nonzero);
	r_dcount = st->stats[GFS2_LKS_DCOUNT];
	var = st->stats[GFS2_LKS_SRTTVARB] +
	      gl->gl_stats.stats[GFS2_LKS_SRTTVARB];
	preempt_enable();

	l_srttb = gl->gl_stats.stats[GFS2_LKS_SRTTB];
	l_dcount = gl->gl_stats.stats[GFS2_LKS_DCOUNT];

	if ((l_dcount < 1) || (r_dcount < 1) || (a_srttb == 0))
		return false;

	srttb_diff = a_srttb - l_srttb;
	sqr_diff = srttb_diff * srttb_diff;

	var *= 2;
	if (l_dcount < 8 || r_dcount < 8)
		var *= 2;
	if (loops == 1)
		var *= 2;

	return ((srttb_diff < 0) && (sqr_diff > var));
}

/**
 * gfs2_rgrp_used_recently
 * @rs: The block reservation with the rgrp to test
 * @msecs: The time limit in milliseconds
 *
 * Returns: True if the rgrp glock has been used within the time limit
 */
static bool gfs2_rgrp_used_recently(const struct gfs2_blkreserv *rs,
				    u64 msecs)
{
	u64 tdiff;

	tdiff = ktime_to_ns(ktime_sub(ktime_get_real(),
                            rs->rs_rbm.rgd->rd_gl->gl_dstamp));

	return tdiff > (msecs * 1000 * 1000);
}

static u32 gfs2_orlov_skip(const struct gfs2_inode *ip)
{
	const struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	u32 skip;

	get_random_bytes(&skip, sizeof(skip));
	return skip % sdp->sd_rgrps;
}

static bool gfs2_select_rgrp(struct gfs2_rgrpd **pos, const struct gfs2_rgrpd *begin)
{
	struct gfs2_rgrpd *rgd = *pos;
	struct gfs2_sbd *sdp = rgd->rd_sbd;

	rgd = gfs2_rgrpd_get_next(rgd);
	if (rgd == NULL)
		rgd = gfs2_rgrpd_get_first(sdp);
	*pos = rgd;
	if (rgd != begin) /* If we didn't wrap */
		return true;
	return false;
}

/**
 * fast_to_acquire - determine if a resource group will be fast to acquire
 *
 * If this is one of our preferred rgrps, it should be quicker to acquire,
 * because we tried to set ourselves up as dlm lock master.
 */
static inline int fast_to_acquire(struct gfs2_rgrpd *rgd)
{
	struct gfs2_glock *gl = rgd->rd_gl;

	if (gl->gl_state != LM_ST_UNLOCKED && list_empty(&gl->gl_holders) &&
	    !test_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags) &&
	    !test_bit(GLF_DEMOTE, &gl->gl_flags))
		return 1;
	if (rgd->rd_flags & GFS2_RDF_PREFERRED)
		return 1;
	return 0;
}

/**
 * gfs2_inplace_reserve - Reserve space in the filesystem
 * @ip: the inode to reserve space for
 * @ap: the allocation parameters
 *
 * We try our best to find an rgrp that has at least ap->target blocks
 * available. After a couple of passes (loops == 2), the prospects of finding
 * such an rgrp diminish. At this stage, we return the first rgrp that has
 * at least ap->min_target blocks available. Either way, we set ap->allowed to
 * the number of blocks available in the chosen rgrp.
 *
 * Returns: 0 on success,
 *          -ENOMEM if a suitable rgrp can't be found
 *          errno otherwise
 */

int gfs2_inplace_reserve(struct gfs2_inode *ip, struct gfs2_alloc_parms *ap)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrpd *begin = NULL;
	struct gfs2_blkreserv *rs = &ip->i_res;
	int error = 0, rg_locked, flags = 0;
	u64 last_unlinked = NO_BLOCK;
	int loops = 0;
	u32 free_blocks, skip = 0;

	if (sdp->sd_args.ar_rgrplvb)
		flags |= GL_SKIP;
	if (gfs2_assert_warn(sdp, ap->target))
		return -EINVAL;
	if (gfs2_rs_active(rs)) {
		begin = rs->rs_rbm.rgd;
	} else if (rs->rs_rbm.rgd &&
		   rgrp_contains_block(rs->rs_rbm.rgd, ip->i_goal)) {
		begin = rs->rs_rbm.rgd;
	} else {
		check_and_update_goal(ip);
		rs->rs_rbm.rgd = begin = gfs2_blk2rgrpd(sdp, ip->i_goal, 1);
	}
	if (S_ISDIR(ip->i_inode.i_mode) && (ap->aflags & GFS2_AF_ORLOV))
		skip = gfs2_orlov_skip(ip);
	if (rs->rs_rbm.rgd == NULL)
		return -EBADSLT;

	while (loops < 3) {
		rg_locked = 1;

		if (!gfs2_glock_is_locked_by_me(rs->rs_rbm.rgd->rd_gl)) {
			rg_locked = 0;
			if (skip && skip--)
				goto next_rgrp;
			if (!gfs2_rs_active(rs)) {
				if (loops == 0 &&
				    !fast_to_acquire(rs->rs_rbm.rgd))
					goto next_rgrp;
				if ((loops < 2) &&
				    gfs2_rgrp_used_recently(rs, 1000) &&
				    gfs2_rgrp_congested(rs->rs_rbm.rgd, loops))
					goto next_rgrp;
			}
			error = gfs2_glock_nq_init(rs->rs_rbm.rgd->rd_gl,
						   LM_ST_EXCLUSIVE, flags,
						   &ip->i_rgd_gh);
			if (unlikely(error))
				return error;
			if (!gfs2_rs_active(rs) && (loops < 2) &&
			    gfs2_rgrp_congested(rs->rs_rbm.rgd, loops))
				goto skip_rgrp;
			if (sdp->sd_args.ar_rgrplvb) {
				error = update_rgrp_lvb(rs->rs_rbm.rgd);
				if (unlikely(error)) {
					gfs2_glock_dq_uninit(&ip->i_rgd_gh);
					return error;
				}
			}
		}

		/* Skip unusable resource groups */
		if ((rs->rs_rbm.rgd->rd_flags & (GFS2_RGF_NOALLOC |
						 GFS2_RDF_ERROR)) ||
		    (loops == 0 && ap->target > rs->rs_rbm.rgd->rd_extfail_pt))
			goto skip_rgrp;

		if (sdp->sd_args.ar_rgrplvb)
			gfs2_rgrp_bh_get(rs->rs_rbm.rgd);

		/* Get a reservation if we don't already have one */
		if (!gfs2_rs_active(rs))
			rg_mblk_search(rs->rs_rbm.rgd, ip, ap);

		/* Skip rgrps when we can't get a reservation on first pass */
		if (!gfs2_rs_active(rs) && (loops < 1))
			goto check_rgrp;

		/* If rgrp has enough free space, use it */
		free_blocks = rgd_free(rs->rs_rbm.rgd, rs);
		if (free_blocks >= ap->target ||
		    (loops == 2 && ap->min_target &&
		     free_blocks >= ap->min_target)) {
			ap->allowed = free_blocks;
			return 0;
		}
check_rgrp:
		/* Check for unlinked inodes which can be reclaimed */
		if (rs->rs_rbm.rgd->rd_flags & GFS2_RDF_CHECK)
			try_rgrp_unlink(rs->rs_rbm.rgd, &last_unlinked,
					ip->i_no_addr);
skip_rgrp:
		/* Drop reservation, if we couldn't use reserved rgrp */
		if (gfs2_rs_active(rs))
			gfs2_rs_deltree(rs);

		/* Unlock rgrp if required */
		if (!rg_locked)
			gfs2_glock_dq_uninit(&ip->i_rgd_gh);
next_rgrp:
		/* Find the next rgrp, and continue looking */
		if (gfs2_select_rgrp(&rs->rs_rbm.rgd, begin))
			continue;
		if (skip)
			continue;

		/* If we've scanned all the rgrps, but found no free blocks
		 * then this checks for some less likely conditions before
		 * trying again.
		 */
		loops++;
		/* Check that fs hasn't grown if writing to rindex */
		if (ip == GFS2_I(sdp->sd_rindex) && !sdp->sd_rindex_uptodate) {
			error = gfs2_ri_update(ip);
			if (error)
				return error;
		}
		/* Flushing the log may release space */
		if (loops == 2)
			gfs2_log_flush(sdp, NULL, GFS2_LOG_HEAD_FLUSH_NORMAL |
				       GFS2_LFC_INPLACE_RESERVE);
	}

	return -ENOSPC;
}

/**
 * gfs2_inplace_release - release an inplace reservation
 * @ip: the inode the reservation was taken out on
 *
 * Release a reservation made by gfs2_inplace_reserve().
 */

void gfs2_inplace_release(struct gfs2_inode *ip)
{
	if (gfs2_holder_initialized(&ip->i_rgd_gh))
		gfs2_glock_dq_uninit(&ip->i_rgd_gh);
}

/**
 * gfs2_alloc_extent - allocate an extent from a given bitmap
 * @rbm: the resource group information
 * @dinode: TRUE if the first block we allocate is for a dinode
 * @n: The extent length (value/result)
 *
 * Add the bitmap buffer to the transaction.
 * Set the found bits to @new_state to change block's allocation state.
 */
static void gfs2_alloc_extent(const struct gfs2_rbm *rbm, bool dinode,
			     unsigned int *n)
{
	struct gfs2_rbm pos = { .rgd = rbm->rgd, };
	const unsigned int elen = *n;
	u64 block;
	int ret;

	*n = 1;
	block = gfs2_rbm_to_block(rbm);
	gfs2_trans_add_meta(rbm->rgd->rd_gl, rbm_bi(rbm)->bi_bh);
	gfs2_setbit(rbm, true, dinode ? GFS2_BLKST_DINODE : GFS2_BLKST_USED);
	block++;
	while (*n < elen) {
		ret = gfs2_rbm_from_block(&pos, block);
		if (ret || gfs2_testbit(&pos, true) != GFS2_BLKST_FREE)
			break;
		gfs2_trans_add_meta(pos.rgd->rd_gl, rbm_bi(&pos)->bi_bh);
		gfs2_setbit(&pos, true, GFS2_BLKST_USED);
		(*n)++;
		block++;
	}
}

/**
 * rgblk_free - Change alloc state of given block(s)
 * @sdp: the filesystem
 * @rgd: the resource group the blocks are in
 * @bstart: the start of a run of blocks to free
 * @blen: the length of the block run (all must lie within ONE RG!)
 * @new_state: GFS2_BLKST_XXX the after-allocation block state
 */

static void rgblk_free(struct gfs2_sbd *sdp, struct gfs2_rgrpd *rgd,
		       u64 bstart, u32 blen, unsigned char new_state)
{
	struct gfs2_rbm rbm;
	struct gfs2_bitmap *bi, *bi_prev = NULL;

	rbm.rgd = rgd;
	if (WARN_ON_ONCE(gfs2_rbm_from_block(&rbm, bstart)))
		return;
	while (blen--) {
		bi = rbm_bi(&rbm);
		if (bi != bi_prev) {
			if (!bi->bi_clone) {
				bi->bi_clone = kmalloc(bi->bi_bh->b_size,
						      GFP_NOFS | __GFP_NOFAIL);
				memcpy(bi->bi_clone + bi->bi_offset,
				       bi->bi_bh->b_data + bi->bi_offset,
				       bi->bi_bytes);
			}
			gfs2_trans_add_meta(rbm.rgd->rd_gl, bi->bi_bh);
			bi_prev = bi;
		}
		gfs2_setbit(&rbm, false, new_state);
		gfs2_rbm_incr(&rbm);
	}
}

/**
 * gfs2_rgrp_dump - print out an rgrp
 * @seq: The iterator
 * @gl: The glock in question
 * @fs_id_buf: pointer to file system id (if requested)
 *
 */

void gfs2_rgrp_dump(struct seq_file *seq, struct gfs2_glock *gl,
		    const char *fs_id_buf)
{
	struct gfs2_rgrpd *rgd = gl->gl_object;
	struct gfs2_blkreserv *trs;
	const struct rb_node *n;

	if (rgd == NULL)
		return;
	gfs2_print_dbg(seq, "%s R: n:%llu f:%02x b:%u/%u i:%u r:%u e:%u\n",
		       fs_id_buf,
		       (unsigned long long)rgd->rd_addr, rgd->rd_flags,
		       rgd->rd_free, rgd->rd_free_clone, rgd->rd_dinodes,
		       rgd->rd_reserved, rgd->rd_extfail_pt);
	if (rgd->rd_sbd->sd_args.ar_rgrplvb) {
		struct gfs2_rgrp_lvb *rgl = rgd->rd_rgl;

		gfs2_print_dbg(seq, "%s  L: f:%02x b:%u i:%u\n", fs_id_buf,
			       be32_to_cpu(rgl->rl_flags),
			       be32_to_cpu(rgl->rl_free),
			       be32_to_cpu(rgl->rl_dinodes));
	}
	spin_lock(&rgd->rd_rsspin);
	for (n = rb_first(&rgd->rd_rstree); n; n = rb_next(&trs->rs_node)) {
		trs = rb_entry(n, struct gfs2_blkreserv, rs_node);
		dump_rs(seq, trs, fs_id_buf);
	}
	spin_unlock(&rgd->rd_rsspin);
}

static void gfs2_rgrp_error(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	char fs_id_buf[sizeof(sdp->sd_fsname) + 7];

	fs_warn(sdp, "rgrp %llu has an error, marking it readonly until umount\n",
		(unsigned long long)rgd->rd_addr);
	fs_warn(sdp, "umount on all nodes and run fsck.gfs2 to fix the error\n");
	sprintf(fs_id_buf, "fsid=%s: ", sdp->sd_fsname);
	gfs2_rgrp_dump(NULL, rgd->rd_gl, fs_id_buf);
	rgd->rd_flags |= GFS2_RDF_ERROR;
}

/**
 * gfs2_adjust_reservation - Adjust (or remove) a reservation after allocation
 * @ip: The inode we have just allocated blocks for
 * @rbm: The start of the allocated blocks
 * @len: The extent length
 *
 * Adjusts a reservation after an allocation has taken place. If the
 * reservation does not match the allocation, or if it is now empty
 * then it is removed.
 */

static void gfs2_adjust_reservation(struct gfs2_inode *ip,
				    const struct gfs2_rbm *rbm, unsigned len)
{
	struct gfs2_blkreserv *rs = &ip->i_res;
	struct gfs2_rgrpd *rgd = rbm->rgd;
	unsigned rlen;
	u64 block;
	int ret;

	spin_lock(&rgd->rd_rsspin);
	if (gfs2_rs_active(rs)) {
		if (gfs2_rbm_eq(&rs->rs_rbm, rbm)) {
			block = gfs2_rbm_to_block(rbm);
			ret = gfs2_rbm_from_block(&rs->rs_rbm, block + len);
			rlen = min(rs->rs_free, len);
			rs->rs_free -= rlen;
			rgd->rd_reserved -= rlen;
			trace_gfs2_rs(rs, TRACE_RS_CLAIM);
			if (rs->rs_free && !ret)
				goto out;
			/* We used up our block reservation, so we should
			   reserve more blocks next time. */
			atomic_add(RGRP_RSRV_ADDBLKS, &ip->i_sizehint);
		}
		__rs_deltree(rs);
	}
out:
	spin_unlock(&rgd->rd_rsspin);
}

/**
 * gfs2_set_alloc_start - Set starting point for block allocation
 * @rbm: The rbm which will be set to the required location
 * @ip: The gfs2 inode
 * @dinode: Flag to say if allocation includes a new inode
 *
 * This sets the starting point from the reservation if one is active
 * otherwise it falls back to guessing a start point based on the
 * inode's goal block or the last allocation point in the rgrp.
 */

static void gfs2_set_alloc_start(struct gfs2_rbm *rbm,
				 const struct gfs2_inode *ip, bool dinode)
{
	u64 goal;

	if (gfs2_rs_active(&ip->i_res)) {
		*rbm = ip->i_res.rs_rbm;
		return;
	}

	if (!dinode && rgrp_contains_block(rbm->rgd, ip->i_goal))
		goal = ip->i_goal;
	else
		goal = rbm->rgd->rd_last_alloc + rbm->rgd->rd_data0;

	if (WARN_ON_ONCE(gfs2_rbm_from_block(rbm, goal))) {
		rbm->bii = 0;
		rbm->offset = 0;
	}
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
	struct gfs2_rbm rbm = { .rgd = ip->i_res.rs_rbm.rgd, };
	unsigned int ndata;
	u64 block; /* block, within the file system scope */
	int error;

	gfs2_set_alloc_start(&rbm, ip, dinode);
	error = gfs2_rbm_find(&rbm, GFS2_BLKST_FREE, NULL, ip, false);

	if (error == -ENOSPC) {
		gfs2_set_alloc_start(&rbm, ip, dinode);
		error = gfs2_rbm_find(&rbm, GFS2_BLKST_FREE, NULL, NULL, false);
	}

	/* Since all blocks are reserved in advance, this shouldn't happen */
	if (error) {
		fs_warn(sdp, "inum=%llu error=%d, nblocks=%u, full=%d fail_pt=%d\n",
			(unsigned long long)ip->i_no_addr, error, *nblocks,
			test_bit(GBF_FULL, &rbm.rgd->rd_bits->bi_flags),
			rbm.rgd->rd_extfail_pt);
		goto rgrp_error;
	}

	gfs2_alloc_extent(&rbm, dinode, nblocks);
	block = gfs2_rbm_to_block(&rbm);
	rbm.rgd->rd_last_alloc = block - rbm.rgd->rd_data0;
	if (gfs2_rs_active(&ip->i_res))
		gfs2_adjust_reservation(ip, &rbm, *nblocks);
	ndata = *nblocks;
	if (dinode)
		ndata--;

	if (!dinode) {
		ip->i_goal = block + ndata - 1;
		error = gfs2_meta_inode_buffer(ip, &dibh);
		if (error == 0) {
			struct gfs2_dinode *di =
				(struct gfs2_dinode *)dibh->b_data;
			gfs2_trans_add_meta(ip->i_gl, dibh);
			di->di_goal_meta = di->di_goal_data =
				cpu_to_be64(ip->i_goal);
			brelse(dibh);
		}
	}
	if (rbm.rgd->rd_free < *nblocks) {
		fs_warn(sdp, "nblocks=%u\n", *nblocks);
		goto rgrp_error;
	}

	rbm.rgd->rd_free -= *nblocks;
	if (dinode) {
		rbm.rgd->rd_dinodes++;
		*generation = rbm.rgd->rd_igeneration++;
		if (*generation == 0)
			*generation = rbm.rgd->rd_igeneration++;
	}

	gfs2_trans_add_meta(rbm.rgd->rd_gl, rbm.rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(rbm.rgd, rbm.rgd->rd_bits[0].bi_bh->b_data);

	gfs2_statfs_change(sdp, 0, -(s64)*nblocks, dinode ? 1 : 0);
	if (dinode)
		gfs2_trans_remove_revoke(sdp, block, *nblocks);

	gfs2_quota_change(ip, *nblocks, ip->i_inode.i_uid, ip->i_inode.i_gid);

	rbm.rgd->rd_free_clone -= *nblocks;
	trace_gfs2_block_alloc(ip, rbm.rgd, block, *nblocks,
			       dinode ? GFS2_BLKST_DINODE : GFS2_BLKST_USED);
	*bn = block;
	return 0;

rgrp_error:
	gfs2_rgrp_error(rbm.rgd);
	return -EIO;
}

/**
 * __gfs2_free_blocks - free a contiguous run of block(s)
 * @ip: the inode these blocks are being freed from
 * @rgd: the resource group the blocks are in
 * @bstart: first block of a run of contiguous blocks
 * @blen: the length of the block run
 * @meta: 1 if the blocks represent metadata
 *
 */

void __gfs2_free_blocks(struct gfs2_inode *ip, struct gfs2_rgrpd *rgd,
			u64 bstart, u32 blen, int meta)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	rgblk_free(sdp, rgd, bstart, blen, GFS2_BLKST_FREE);
	trace_gfs2_block_alloc(ip, rgd, bstart, blen, GFS2_BLKST_FREE);
	rgd->rd_free += blen;
	rgd->rd_flags &= ~GFS2_RGF_TRIMMED;
	gfs2_trans_add_meta(rgd->rd_gl, rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);

	/* Directories keep their data in the metadata address space */
	if (meta || ip->i_depth)
		gfs2_meta_wipe(ip, bstart, blen);
}

/**
 * gfs2_free_meta - free a contiguous run of data block(s)
 * @ip: the inode these blocks are being freed from
 * @rgd: the resource group the blocks are in
 * @bstart: first block of a run of contiguous blocks
 * @blen: the length of the block run
 *
 */

void gfs2_free_meta(struct gfs2_inode *ip, struct gfs2_rgrpd *rgd,
		    u64 bstart, u32 blen)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	__gfs2_free_blocks(ip, rgd, bstart, blen, 1);
	gfs2_statfs_change(sdp, 0, +blen, 0);
	gfs2_quota_change(ip, -(s64)blen, ip->i_inode.i_uid, ip->i_inode.i_gid);
}

void gfs2_unlink_di(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_rgrpd *rgd;
	u64 blkno = ip->i_no_addr;

	rgd = gfs2_blk2rgrpd(sdp, blkno, true);
	if (!rgd)
		return;
	rgblk_free(sdp, rgd, blkno, 1, GFS2_BLKST_UNLINKED);
	trace_gfs2_block_alloc(ip, rgd, blkno, 1, GFS2_BLKST_UNLINKED);
	gfs2_trans_add_meta(rgd->rd_gl, rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);
	be32_add_cpu(&rgd->rd_rgl->rl_unlinked, 1);
}

void gfs2_free_di(struct gfs2_rgrpd *rgd, struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;

	rgblk_free(sdp, rgd, ip->i_no_addr, 1, GFS2_BLKST_FREE);
	if (!rgd->rd_dinodes)
		gfs2_consist_rgrpd(rgd);
	rgd->rd_dinodes--;
	rgd->rd_free++;

	gfs2_trans_add_meta(rgd->rd_gl, rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);
	be32_add_cpu(&rgd->rd_rgl->rl_unlinked, -1);

	gfs2_statfs_change(sdp, 0, +1, -1);
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
	struct gfs2_rbm rbm;
	int error = -EINVAL;

	rgd = gfs2_blk2rgrpd(sdp, no_addr, 1);
	if (!rgd)
		goto fail;

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_SHARED, 0, &rgd_gh);
	if (error)
		goto fail;

	rbm.rgd = rgd;
	error = gfs2_rbm_from_block(&rbm, no_addr);
	if (WARN_ON_ONCE(error))
		goto fail;

	if (gfs2_testbit(&rbm, false) != type)
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

	/*
	 * The resource group last accessed is kept in the last position.
	 */

	if (rlist->rl_rgrps) {
		rgd = rlist->rl_rgd[rlist->rl_rgrps - 1];
		if (rgrp_contains_block(rgd, block))
			return;
		rgd = gfs2_blk2rgrpd(sdp, block, 1);
	} else {
		rgd = ip->i_res.rs_rbm.rgd;
		if (!rgd || !rgrp_contains_block(rgd, block))
			rgd = gfs2_blk2rgrpd(sdp, block, 1);
	}

	if (!rgd) {
		fs_err(sdp, "rlist_add: no rgrp for block %llu\n",
		       (unsigned long long)block);
		return;
	}

	for (x = 0; x < rlist->rl_rgrps; x++) {
		if (rlist->rl_rgd[x] == rgd) {
			swap(rlist->rl_rgd[x],
			     rlist->rl_rgd[rlist->rl_rgrps - 1]);
			return;
		}
	}

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
 *
 * FIXME: Don't use NOFAIL
 *
 */

void gfs2_rlist_alloc(struct gfs2_rgrp_list *rlist)
{
	unsigned int x;

	rlist->rl_ghs = kmalloc_array(rlist->rl_rgrps,
				      sizeof(struct gfs2_holder),
				      GFP_NOFS | __GFP_NOFAIL);
	for (x = 0; x < rlist->rl_rgrps; x++)
		gfs2_holder_init(rlist->rl_rgd[x]->rd_gl,
				LM_ST_EXCLUSIVE, 0,
				&rlist->rl_ghs[x]);
}

/**
 * gfs2_rlist_free - free a resource group list
 * @rlist: the list of resource groups
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


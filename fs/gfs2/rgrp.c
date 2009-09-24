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
                        unsigned char old_state, unsigned char new_state,
			unsigned int *n);

/**
 * gfs2_setbit - Set a bit in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @block: the block to set
 * @new_state: the new state of the block
 *
 */

static inline void gfs2_setbit(struct gfs2_rgrpd *rgd, unsigned char *buf1,
			       unsigned char *buf2, unsigned int offset,
			       unsigned int buflen, u32 block,
			       unsigned char new_state)
{
	unsigned char *byte1, *byte2, *end, cur_state;
	const unsigned int bit = (block % GFS2_NBBY) * GFS2_BIT_SIZE;

	byte1 = buf1 + offset + (block / GFS2_NBBY);
	end = buf1 + offset + buflen;

	BUG_ON(byte1 >= end);

	cur_state = (*byte1 >> bit) & GFS2_BIT_MASK;

	if (unlikely(!valid_change[new_state * 4 + cur_state])) {
		gfs2_consist_rgrpd(rgd);
		return;
	}
	*byte1 ^= (cur_state ^ new_state) << bit;

	if (buf2) {
		byte2 = buf2 + offset + (block / GFS2_NBBY);
		cur_state = (*byte2 >> bit) & GFS2_BIT_MASK;
		*byte2 ^= (cur_state ^ new_state) << bit;
	}
}

/**
 * gfs2_testbit - test a bit in the bitmaps
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
 * gfs2_bitfit - Search an rgrp's bitmap buffer to find a bit-pair representing
 *       a block in a given allocation state.
 * @buffer: the buffer that holds the bitmaps
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
 * @sdp: the filesystem
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
 * @n: The data block number
 *
 * Returns: The resource group, or NULL if not found
 */

struct gfs2_rgrpd *gfs2_blk2rgrpd(struct gfs2_sbd *sdp, u64 blk)
{
	struct gfs2_rgrpd *rgd;

	spin_lock(&sdp->sd_rindex_spin);

	list_for_each_entry(rgd, &sdp->sd_rindex_mru_list, rd_list_mru) {
		if (rgrp_contains_block(rgd, blk)) {
			list_move(&rgd->rd_list_mru, &sdp->sd_rindex_mru_list);
			spin_unlock(&sdp->sd_rindex_spin);
			return rgd;
		}
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
	gfs2_assert(sdp, !list_empty(&sdp->sd_rindex_list));
	return list_entry(sdp->sd_rindex_list.next, struct gfs2_rgrpd, rd_list);
}

/**
 * gfs2_rgrpd_get_next - get the next RG
 * @rgd: A RG
 *
 * Returns: The next rgrp
 */

struct gfs2_rgrpd *gfs2_rgrpd_get_next(struct gfs2_rgrpd *rgd)
{
	if (rgd->rd_list.next == &rgd->rd_sbd->sd_rindex_list)
		return NULL;
	return list_entry(rgd->rd_list.next, struct gfs2_rgrpd, rd_list);
}

static void clear_rgrpdi(struct gfs2_sbd *sdp)
{
	struct list_head *head;
	struct gfs2_rgrpd *rgd;
	struct gfs2_glock *gl;

	spin_lock(&sdp->sd_rindex_spin);
	sdp->sd_rindex_forward = NULL;
	spin_unlock(&sdp->sd_rindex_spin);

	head = &sdp->sd_rindex_list;
	while (!list_empty(head)) {
		rgd = list_entry(head->next, struct gfs2_rgrpd, rd_list);
		gl = rgd->rd_gl;

		list_del(&rgd->rd_list);
		list_del(&rgd->rd_list_mru);

		if (gl) {
			gl->gl_object = NULL;
			gfs2_glock_put(gl);
		}

		kfree(rgd->rd_bits);
		kmem_cache_free(gfs2_rgrpd_cachep, rgd);
	}
}

void gfs2_clear_rgrpd(struct gfs2_sbd *sdp)
{
	mutex_lock(&sdp->sd_rindex_mutex);
	clear_rgrpdi(sdp);
	mutex_unlock(&sdp->sd_rindex_mutex);
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
 *
 */
u64 gfs2_ri_total(struct gfs2_sbd *sdp)
{
	u64 total_data = 0;	
	struct inode *inode = sdp->sd_rindex;
	struct gfs2_inode *ip = GFS2_I(inode);
	char buf[sizeof(struct gfs2_rindex)];
	struct file_ra_state ra_state;
	int error, rgrps;

	mutex_lock(&sdp->sd_rindex_mutex);
	file_ra_state_init(&ra_state, inode->i_mapping);
	for (rgrps = 0;; rgrps++) {
		loff_t pos = rgrps * sizeof(struct gfs2_rindex);

		if (pos + sizeof(struct gfs2_rindex) >= ip->i_disksize)
			break;
		error = gfs2_internal_read(ip, &ra_state, buf, &pos,
					   sizeof(struct gfs2_rindex));
		if (error != sizeof(struct gfs2_rindex))
			break;
		total_data += be32_to_cpu(((struct gfs2_rindex *)buf)->ri_data);
	}
	mutex_unlock(&sdp->sd_rindex_mutex);
	return total_data;
}

static void gfs2_rindex_in(struct gfs2_rgrpd *rgd, const void *buf)
{
	const struct gfs2_rindex *str = buf;

	rgd->rd_addr = be64_to_cpu(str->ri_addr);
	rgd->rd_length = be32_to_cpu(str->ri_length);
	rgd->rd_data0 = be64_to_cpu(str->ri_data0);
	rgd->rd_data = be32_to_cpu(str->ri_data);
	rgd->rd_bitbytes = be32_to_cpu(str->ri_bitbytes);
}

/**
 * read_rindex_entry - Pull in a new resource index entry from the disk
 * @gl: The glock covering the rindex inode
 *
 * Returns: 0 on success, error code otherwise
 */

static int read_rindex_entry(struct gfs2_inode *ip,
			     struct file_ra_state *ra_state)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	loff_t pos = sdp->sd_rgrps * sizeof(struct gfs2_rindex);
	char buf[sizeof(struct gfs2_rindex)];
	int error;
	struct gfs2_rgrpd *rgd;

	error = gfs2_internal_read(ip, ra_state, buf, &pos,
				   sizeof(struct gfs2_rindex));
	if (!error)
		return 0;
	if (error != sizeof(struct gfs2_rindex)) {
		if (error > 0)
			error = -EIO;
		return error;
	}

	rgd = kmem_cache_zalloc(gfs2_rgrpd_cachep, GFP_NOFS);
	error = -ENOMEM;
	if (!rgd)
		return error;

	mutex_init(&rgd->rd_mutex);
	lops_init_le(&rgd->rd_le, &gfs2_rg_lops);
	rgd->rd_sbd = sdp;

	list_add_tail(&rgd->rd_list, &sdp->sd_rindex_list);
	list_add_tail(&rgd->rd_list_mru, &sdp->sd_rindex_mru_list);

	gfs2_rindex_in(rgd, buf);
	error = compute_bitstructs(rgd);
	if (error)
		return error;

	error = gfs2_glock_get(sdp, rgd->rd_addr,
			       &gfs2_rgrp_glops, CREATE, &rgd->rd_gl);
	if (error)
		return error;

	rgd->rd_gl->gl_object = rgd;
	rgd->rd_flags &= ~GFS2_RDF_UPTODATE;
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
	struct inode *inode = &ip->i_inode;
	struct file_ra_state ra_state;
	u64 rgrp_count = ip->i_disksize;
	int error;

	if (do_div(rgrp_count, sizeof(struct gfs2_rindex))) {
		gfs2_consist_inode(ip);
		return -EIO;
	}

	clear_rgrpdi(sdp);

	file_ra_state_init(&ra_state, inode->i_mapping);
	for (sdp->sd_rgrps = 0; sdp->sd_rgrps < rgrp_count; sdp->sd_rgrps++) {
		error = read_rindex_entry(ip, &ra_state);
		if (error) {
			clear_rgrpdi(sdp);
			return error;
		}
	}

	sdp->sd_rindex_uptodate = 1;
	return 0;
}

/**
 * gfs2_ri_update_special - Pull in a new resource index from the disk
 *
 * This is a special version that's safe to call from gfs2_inplace_reserve_i.
 * In this case we know that we don't have any resource groups in memory yet.
 *
 * @ip: pointer to the rindex inode
 *
 * Returns: 0 on successful update, error code otherwise
 */
static int gfs2_ri_update_special(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct inode *inode = &ip->i_inode;
	struct file_ra_state ra_state;
	int error;

	file_ra_state_init(&ra_state, inode->i_mapping);
	for (sdp->sd_rgrps = 0;; sdp->sd_rgrps++) {
		/* Ignore partials */
		if ((sdp->sd_rgrps + 1) * sizeof(struct gfs2_rindex) >
		    ip->i_disksize)
			break;
		error = read_rindex_entry(ip, &ra_state);
		if (error) {
			clear_rgrpdi(sdp);
			return error;
		}
	}

	sdp->sd_rindex_uptodate = 1;
	return 0;
}

/**
 * gfs2_rindex_hold - Grab a lock on the rindex
 * @sdp: The GFS2 superblock
 * @ri_gh: the glock holder
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
 * Returns: 0 on success, error code otherwise
 */

int gfs2_rindex_hold(struct gfs2_sbd *sdp, struct gfs2_holder *ri_gh)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_rindex);
	struct gfs2_glock *gl = ip->i_gl;
	int error;

	error = gfs2_glock_nq_init(gl, LM_ST_SHARED, 0, ri_gh);
	if (error)
		return error;

	/* Read new copy from disk if we don't have the latest */
	if (!sdp->sd_rindex_uptodate) {
		mutex_lock(&sdp->sd_rindex_mutex);
		if (!sdp->sd_rindex_uptodate) {
			error = gfs2_ri_update(ip);
			if (error)
				gfs2_glock_dq_uninit(ri_gh);
		}
		mutex_unlock(&sdp->sd_rindex_mutex);
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

	mutex_lock(&rgd->rd_mutex);

	spin_lock(&sdp->sd_rindex_spin);
	if (rgd->rd_bh_count) {
		rgd->rd_bh_count++;
		spin_unlock(&sdp->sd_rindex_spin);
		mutex_unlock(&rgd->rd_mutex);
		return 0;
	}
	spin_unlock(&sdp->sd_rindex_spin);

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
	}

	spin_lock(&sdp->sd_rindex_spin);
	rgd->rd_free_clone = rgd->rd_free;
	rgd->rd_bh_count++;
	spin_unlock(&sdp->sd_rindex_spin);

	mutex_unlock(&rgd->rd_mutex);

	return 0;

fail:
	while (x--) {
		bi = rgd->rd_bits + x;
		brelse(bi->bi_bh);
		bi->bi_bh = NULL;
		gfs2_assert_warn(sdp, !bi->bi_clone);
	}
	mutex_unlock(&rgd->rd_mutex);

	return error;
}

void gfs2_rgrp_bh_hold(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;

	spin_lock(&sdp->sd_rindex_spin);
	gfs2_assert_warn(rgd->rd_sbd, rgd->rd_bh_count);
	rgd->rd_bh_count++;
	spin_unlock(&sdp->sd_rindex_spin);
}

/**
 * gfs2_rgrp_bh_put - Release RG bitmaps read in with gfs2_rgrp_bh_get()
 * @rgd: the struct gfs2_rgrpd describing the RG to read in
 *
 */

void gfs2_rgrp_bh_put(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	int x, length = rgd->rd_length;

	spin_lock(&sdp->sd_rindex_spin);
	gfs2_assert_warn(rgd->rd_sbd, rgd->rd_bh_count);
	if (--rgd->rd_bh_count) {
		spin_unlock(&sdp->sd_rindex_spin);
		return;
	}

	for (x = 0; x < length; x++) {
		struct gfs2_bitmap *bi = rgd->rd_bits + x;
		kfree(bi->bi_clone);
		bi->bi_clone = NULL;
		brelse(bi->bi_bh);
		bi->bi_bh = NULL;
	}

	spin_unlock(&sdp->sd_rindex_spin);
}

static void gfs2_rgrp_send_discards(struct gfs2_sbd *sdp, u64 offset,
				    const struct gfs2_bitmap *bi)
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

	for (x = 0; x < bi->bi_len; x++) {
		const u8 *orig = bi->bi_bh->b_data + bi->bi_offset + x;
		const u8 *clone = bi->bi_clone + bi->bi_offset + x;
		u8 diff = ~(*orig | (*orig >> 1)) & (*clone | (*clone >> 1));
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
					rv = blkdev_issue_discard(bdev, start,
							    nr_sects, GFP_NOFS,
							    DISCARD_FL_BARRIER);
					if (rv)
						goto fail;
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
	if (nr_sects) {
		rv = blkdev_issue_discard(bdev, start, nr_sects, GFP_NOFS,
					 DISCARD_FL_BARRIER);
		if (rv)
			goto fail;
	}
	return;
fail:
	fs_warn(sdp, "error %d on discard request, turning discards off for this filesystem", rv);
	sdp->sd_args.ar_discard = 0;
}

void gfs2_rgrp_repolish_clones(struct gfs2_rgrpd *rgd)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	unsigned int length = rgd->rd_length;
	unsigned int x;

	for (x = 0; x < length; x++) {
		struct gfs2_bitmap *bi = rgd->rd_bits + x;
		if (!bi->bi_clone)
			continue;
		if (sdp->sd_args.ar_discard)
			gfs2_rgrp_send_discards(sdp, rgd->rd_data0, bi);
		clear_bit(GBF_FULL, &bi->bi_flags);
		memcpy(bi->bi_clone + bi->bi_offset,
		       bi->bi_bh->b_data + bi->bi_offset, bi->bi_len);
	}

	spin_lock(&sdp->sd_rindex_spin);
	rgd->rd_free_clone = rgd->rd_free;
	spin_unlock(&sdp->sd_rindex_spin);
}

/**
 * gfs2_alloc_get - get the struct gfs2_alloc structure for an inode
 * @ip: the incore GFS2 inode structure
 *
 * Returns: the struct gfs2_alloc
 */

struct gfs2_alloc *gfs2_alloc_get(struct gfs2_inode *ip)
{
	BUG_ON(ip->i_alloc != NULL);
	ip->i_alloc = kzalloc(sizeof(struct gfs2_alloc), GFP_KERNEL);
	return ip->i_alloc;
}

/**
 * try_rgrp_fit - See if a given reservation will fit in a given RG
 * @rgd: the RG data
 * @al: the struct gfs2_alloc structure describing the reservation
 *
 * If there's room for the requested blocks to be allocated from the RG:
 *   Sets the $al_rgd field in @al.
 *
 * Returns: 1 on success (it fits), 0 on failure (it doesn't fit)
 */

static int try_rgrp_fit(struct gfs2_rgrpd *rgd, struct gfs2_alloc *al)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	int ret = 0;

	if (rgd->rd_flags & (GFS2_RGF_NOALLOC | GFS2_RDF_ERROR))
		return 0;

	spin_lock(&sdp->sd_rindex_spin);
	if (rgd->rd_free_clone >= al->al_requested) {
		al->al_rgd = rgd;
		ret = 1;
	}
	spin_unlock(&sdp->sd_rindex_spin);

	return ret;
}

/**
 * try_rgrp_unlink - Look for any unlinked, allocated, but unused inodes
 * @rgd: The rgrp
 *
 * Returns: The inode, if one has been found
 */

static struct inode *try_rgrp_unlink(struct gfs2_rgrpd *rgd, u64 *last_unlinked,
				     u64 skip)
{
	struct inode *inode;
	u32 goal = 0, block;
	u64 no_addr;
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	unsigned int n;

	for(;;) {
		if (goal >= rgd->rd_data)
			break;
		down_write(&sdp->sd_log_flush_lock);
		n = 1;
		block = rgblk_search(rgd, goal, GFS2_BLKST_UNLINKED,
				     GFS2_BLKST_UNLINKED, &n);
		up_write(&sdp->sd_log_flush_lock);
		if (block == BFITNOENT)
			break;
		/* rgblk_search can return a block < goal, so we need to
		   keep it marching forward. */
		no_addr = block + rgd->rd_data0;
		goal++;
		if (*last_unlinked != NO_BLOCK && no_addr <= *last_unlinked)
			continue;
		if (no_addr == skip)
			continue;
		*last_unlinked = no_addr;
		inode = gfs2_inode_lookup(rgd->rd_sbd->sd_vfs, DT_UNKNOWN,
					  no_addr, -1, 1);
		if (!IS_ERR(inode))
			return inode;
	}

	rgd->rd_flags &= ~GFS2_RDF_CHECK;
	return NULL;
}

/**
 * recent_rgrp_next - get next RG from "recent" list
 * @cur_rgd: current rgrp
 *
 * Returns: The next rgrp in the recent list
 */

static struct gfs2_rgrpd *recent_rgrp_next(struct gfs2_rgrpd *cur_rgd)
{
	struct gfs2_sbd *sdp = cur_rgd->rd_sbd;
	struct list_head *head;
	struct gfs2_rgrpd *rgd;

	spin_lock(&sdp->sd_rindex_spin);
	head = &sdp->sd_rindex_mru_list;
	if (unlikely(cur_rgd->rd_list_mru.next == head)) {
		spin_unlock(&sdp->sd_rindex_spin);
		return NULL;
	}
	rgd = list_entry(cur_rgd->rd_list_mru.next, struct gfs2_rgrpd, rd_list_mru);
	spin_unlock(&sdp->sd_rindex_spin);
	return rgd;
}

/**
 * forward_rgrp_get - get an rgrp to try next from full list
 * @sdp: The GFS2 superblock
 *
 * Returns: The rgrp to try next
 */

static struct gfs2_rgrpd *forward_rgrp_get(struct gfs2_sbd *sdp)
{
	struct gfs2_rgrpd *rgd;
	unsigned int journals = gfs2_jindex_size(sdp);
	unsigned int rg = 0, x;

	spin_lock(&sdp->sd_rindex_spin);

	rgd = sdp->sd_rindex_forward;
	if (!rgd) {
		if (sdp->sd_rgrps >= journals)
			rg = sdp->sd_rgrps * sdp->sd_jdesc->jd_jid / journals;

		for (x = 0, rgd = gfs2_rgrpd_get_first(sdp); x < rg;
		     x++, rgd = gfs2_rgrpd_get_next(rgd))
			/* Do Nothing */;

		sdp->sd_rindex_forward = rgd;
	}

	spin_unlock(&sdp->sd_rindex_spin);

	return rgd;
}

/**
 * forward_rgrp_set - set the forward rgrp pointer
 * @sdp: the filesystem
 * @rgd: The new forward rgrp
 *
 */

static void forward_rgrp_set(struct gfs2_sbd *sdp, struct gfs2_rgrpd *rgd)
{
	spin_lock(&sdp->sd_rindex_spin);
	sdp->sd_rindex_forward = rgd;
	spin_unlock(&sdp->sd_rindex_spin);
}

/**
 * get_local_rgrp - Choose and lock a rgrp for allocation
 * @ip: the inode to reserve space for
 * @rgp: the chosen and locked rgrp
 *
 * Try to acquire rgrp in way which avoids contending with others.
 *
 * Returns: errno
 */

static struct inode *get_local_rgrp(struct gfs2_inode *ip, u64 *last_unlinked)
{
	struct inode *inode = NULL;
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrpd *rgd, *begin = NULL;
	struct gfs2_alloc *al = ip->i_alloc;
	int flags = LM_FLAG_TRY;
	int skipped = 0;
	int loops = 0;
	int error, rg_locked;

	rgd = gfs2_blk2rgrpd(sdp, ip->i_goal);

	while (rgd) {
		rg_locked = 0;

		if (gfs2_glock_is_locked_by_me(rgd->rd_gl)) {
			rg_locked = 1;
			error = 0;
		} else {
			error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE,
						   LM_FLAG_TRY, &al->al_rgd_gh);
		}
		switch (error) {
		case 0:
			if (try_rgrp_fit(rgd, al))
				goto out;
			if (rgd->rd_flags & GFS2_RDF_CHECK)
				inode = try_rgrp_unlink(rgd, last_unlinked, ip->i_no_addr);
			if (!rg_locked)
				gfs2_glock_dq_uninit(&al->al_rgd_gh);
			if (inode)
				return inode;
			/* fall through */
		case GLR_TRYFAILED:
			rgd = recent_rgrp_next(rgd);
			break;

		default:
			return ERR_PTR(error);
		}
	}

	/* Go through full list of rgrps */

	begin = rgd = forward_rgrp_get(sdp);

	for (;;) {
		rg_locked = 0;

		if (gfs2_glock_is_locked_by_me(rgd->rd_gl)) {
			rg_locked = 1;
			error = 0;
		} else {
			error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, flags,
						   &al->al_rgd_gh);
		}
		switch (error) {
		case 0:
			if (try_rgrp_fit(rgd, al))
				goto out;
			if (rgd->rd_flags & GFS2_RDF_CHECK)
				inode = try_rgrp_unlink(rgd, last_unlinked, ip->i_no_addr);
			if (!rg_locked)
				gfs2_glock_dq_uninit(&al->al_rgd_gh);
			if (inode)
				return inode;
			break;

		case GLR_TRYFAILED:
			skipped++;
			break;

		default:
			return ERR_PTR(error);
		}

		rgd = gfs2_rgrpd_get_next(rgd);
		if (!rgd)
			rgd = gfs2_rgrpd_get_first(sdp);

		if (rgd == begin) {
			if (++loops >= 3)
				return ERR_PTR(-ENOSPC);
			if (!skipped)
				loops++;
			flags = 0;
			if (loops == 2)
				gfs2_log_flush(sdp, NULL);
		}
	}

out:
	if (begin) {
		spin_lock(&sdp->sd_rindex_spin);
		list_move(&rgd->rd_list_mru, &sdp->sd_rindex_mru_list);
		spin_unlock(&sdp->sd_rindex_spin);
		rgd = gfs2_rgrpd_get_next(rgd);
		if (!rgd)
			rgd = gfs2_rgrpd_get_first(sdp);
		forward_rgrp_set(sdp, rgd);
	}

	return NULL;
}

/**
 * gfs2_inplace_reserve_i - Reserve space in the filesystem
 * @ip: the inode to reserve space for
 *
 * Returns: errno
 */

int gfs2_inplace_reserve_i(struct gfs2_inode *ip, char *file, unsigned int line)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_alloc *al = ip->i_alloc;
	struct inode *inode;
	int error = 0;
	u64 last_unlinked = NO_BLOCK;

	if (gfs2_assert_warn(sdp, al->al_requested))
		return -EINVAL;

try_again:
	/* We need to hold the rindex unless the inode we're using is
	   the rindex itself, in which case it's already held. */
	if (ip != GFS2_I(sdp->sd_rindex))
		error = gfs2_rindex_hold(sdp, &al->al_ri_gh);
	else if (!sdp->sd_rgrps) /* We may not have the rindex read in, so: */
		error = gfs2_ri_update_special(ip);

	if (error)
		return error;

	inode = get_local_rgrp(ip, &last_unlinked);
	if (inode) {
		if (ip != GFS2_I(sdp->sd_rindex))
			gfs2_glock_dq_uninit(&al->al_ri_gh);
		if (IS_ERR(inode))
			return PTR_ERR(inode);
		iput(inode);
		gfs2_log_flush(sdp, NULL);
		goto try_again;
	}

	al->al_file = file;
	al->al_line = line;

	return 0;
}

/**
 * gfs2_inplace_release - release an inplace reservation
 * @ip: the inode the reservation was taken out on
 *
 * Release a reservation made by gfs2_inplace_reserve().
 */

void gfs2_inplace_release(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_alloc *al = ip->i_alloc;

	if (gfs2_assert_warn(sdp, al->al_alloced <= al->al_requested) == -1)
		fs_warn(sdp, "al_alloced = %u, al_requested = %u "
			     "al_file = %s, al_line = %u\n",
		             al->al_alloced, al->al_requested, al->al_file,
			     al->al_line);

	al->al_rgd = NULL;
	if (al->al_rgd_gh.gh_gl)
		gfs2_glock_dq_uninit(&al->al_rgd_gh);
	if (ip != GFS2_I(sdp->sd_rindex))
		gfs2_glock_dq_uninit(&al->al_ri_gh);
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
 * rgblk_search - find a block in @old_state, change allocation
 *           state to @new_state
 * @rgd: the resource group descriptor
 * @goal: the goal block within the RG (start here to search for avail block)
 * @old_state: GFS2_BLKST_XXX the before-allocation state to find
 * @new_state: GFS2_BLKST_XXX the after-allocation block state
 * @n: The extent length
 *
 * Walk rgrp's bitmap to find bits that represent a block in @old_state.
 * Add the found bitmap buffer to the transaction.
 * Set the found bits to @new_state to change block's allocation state.
 *
 * This function never fails, because we wouldn't call it unless we
 * know (from reservation results, etc.) that a block is available.
 *
 * Scope of @goal and returned block is just within rgrp, not the whole
 * filesystem.
 *
 * Returns:  the block number allocated
 */

static u32 rgblk_search(struct gfs2_rgrpd *rgd, u32 goal,
			unsigned char old_state, unsigned char new_state,
			unsigned int *n)
{
	struct gfs2_bitmap *bi = NULL;
	const u32 length = rgd->rd_length;
	u32 blk = BFITNOENT;
	unsigned int buf, x;
	const unsigned int elen = *n;
	const u8 *buffer = NULL;

	*n = 0;
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
		    (old_state == GFS2_BLKST_FREE))
			goto skip;

		/* The GFS2_BLKST_UNLINKED state doesn't apply to the clone
		   bitmaps, so we must search the originals for that. */
		buffer = bi->bi_bh->b_data + bi->bi_offset;
		if (old_state != GFS2_BLKST_UNLINKED && bi->bi_clone)
			buffer = bi->bi_clone + bi->bi_offset;

		blk = gfs2_bitfit(buffer, bi->bi_len, goal, old_state);
		if (blk != BFITNOENT)
			break;

		if ((goal == 0) && (old_state == GFS2_BLKST_FREE))
			set_bit(GBF_FULL, &bi->bi_flags);

		/* Try next bitmap block (wrap back to rgrp header if at end) */
skip:
		buf++;
		buf %= length;
		goal = 0;
	}

	if (blk == BFITNOENT)
		return blk;
	*n = 1;
	if (old_state == new_state)
		goto out;

	gfs2_trans_add_bh(rgd->rd_gl, bi->bi_bh, 1);
	gfs2_setbit(rgd, bi->bi_bh->b_data, bi->bi_clone, bi->bi_offset,
		    bi->bi_len, blk, new_state);
	goal = blk;
	while (*n < elen) {
		goal++;
		if (goal >= (bi->bi_len * GFS2_NBBY))
			break;
		if (gfs2_testbit(rgd, buffer, bi->bi_len, goal) !=
		    GFS2_BLKST_FREE)
			break;
		gfs2_setbit(rgd, bi->bi_bh->b_data, bi->bi_clone, bi->bi_offset,
			    bi->bi_len, goal, new_state);
		(*n)++;
	}
out:
	return (bi->bi_start * GFS2_NBBY) + blk;
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

	rgd = gfs2_blk2rgrpd(sdp, bstart);
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
		gfs2_setbit(rgd, bi->bi_bh->b_data, NULL, bi->bi_offset,
			    bi->bi_len, buf_blk, new_state);
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
	const struct gfs2_rgrpd *rgd = gl->gl_object;
	if (rgd == NULL)
		return 0;
	gfs2_print_dbg(seq, " R: n:%llu f:%02x b:%u/%u i:%u\n",
		       (unsigned long long)rgd->rd_addr, rgd->rd_flags,
		       rgd->rd_free, rgd->rd_free_clone, rgd->rd_dinodes);
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
 * gfs2_alloc_block - Allocate one or more blocks
 * @ip: the inode to allocate the block for
 * @bn: Used to return the starting block number
 * @n: requested number of blocks/extent length (value/result)
 *
 * Returns: 0 or error
 */

int gfs2_alloc_block(struct gfs2_inode *ip, u64 *bn, unsigned int *n)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *dibh;
	struct gfs2_alloc *al = ip->i_alloc;
	struct gfs2_rgrpd *rgd = al->al_rgd;
	u32 goal, blk;
	u64 block;
	int error;

	if (rgrp_contains_block(rgd, ip->i_goal))
		goal = ip->i_goal - rgd->rd_data0;
	else
		goal = rgd->rd_last_alloc;

	blk = rgblk_search(rgd, goal, GFS2_BLKST_FREE, GFS2_BLKST_USED, n);

	/* Since all blocks are reserved in advance, this shouldn't happen */
	if (blk == BFITNOENT)
		goto rgrp_error;

	rgd->rd_last_alloc = blk;
	block = rgd->rd_data0 + blk;
	ip->i_goal = block;
	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error == 0) {
		struct gfs2_dinode *di = (struct gfs2_dinode *)dibh->b_data;
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		di->di_goal_meta = di->di_goal_data = cpu_to_be64(ip->i_goal);
		brelse(dibh);
	}
	if (rgd->rd_free < *n)
		goto rgrp_error;

	rgd->rd_free -= *n;

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh, 1);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);

	al->al_alloced += *n;

	gfs2_statfs_change(sdp, 0, -(s64)*n, 0);
	gfs2_quota_change(ip, *n, ip->i_inode.i_uid, ip->i_inode.i_gid);

	spin_lock(&sdp->sd_rindex_spin);
	rgd->rd_free_clone -= *n;
	spin_unlock(&sdp->sd_rindex_spin);
	trace_gfs2_block_alloc(ip, block, *n, GFS2_BLKST_USED);
	*bn = block;
	return 0;

rgrp_error:
	gfs2_rgrp_error(rgd);
	return -EIO;
}

/**
 * gfs2_alloc_di - Allocate a dinode
 * @dip: the directory that the inode is going in
 * @bn: the block number which is allocated
 * @generation: the generation number of the inode
 *
 * Returns: 0 on success or error
 */

int gfs2_alloc_di(struct gfs2_inode *dip, u64 *bn, u64 *generation)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct gfs2_alloc *al = dip->i_alloc;
	struct gfs2_rgrpd *rgd = al->al_rgd;
	u32 blk;
	u64 block;
	unsigned int n = 1;

	blk = rgblk_search(rgd, rgd->rd_last_alloc,
			   GFS2_BLKST_FREE, GFS2_BLKST_DINODE, &n);

	/* Since all blocks are reserved in advance, this shouldn't happen */
	if (blk == BFITNOENT)
		goto rgrp_error;

	rgd->rd_last_alloc = blk;
	block = rgd->rd_data0 + blk;
	if (rgd->rd_free == 0)
		goto rgrp_error;

	rgd->rd_free--;
	rgd->rd_dinodes++;
	*generation = rgd->rd_igeneration++;
	if (*generation == 0)
		*generation = rgd->rd_igeneration++;
	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh, 1);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);

	al->al_alloced++;

	gfs2_statfs_change(sdp, 0, -1, +1);
	gfs2_trans_add_unrevoke(sdp, block, 1);

	spin_lock(&sdp->sd_rindex_spin);
	rgd->rd_free_clone--;
	spin_unlock(&sdp->sd_rindex_spin);
	trace_gfs2_block_alloc(dip, block, 1, GFS2_BLKST_DINODE);
	*bn = block;
	return 0;

rgrp_error:
	gfs2_rgrp_error(rgd);
	return -EIO;
}

/**
 * gfs2_free_data - free a contiguous run of data block(s)
 * @ip: the inode these blocks are being freed from
 * @bstart: first block of a run of contiguous blocks
 * @blen: the length of the block run
 *
 */

void gfs2_free_data(struct gfs2_inode *ip, u64 bstart, u32 blen)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrpd *rgd;

	rgd = rgblk_free(sdp, bstart, blen, GFS2_BLKST_FREE);
	if (!rgd)
		return;
	trace_gfs2_block_alloc(ip, bstart, blen, GFS2_BLKST_FREE);
	rgd->rd_free += blen;

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh, 1);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);

	gfs2_trans_add_rg(rgd);

	gfs2_statfs_change(sdp, 0, +blen, 0);
	gfs2_quota_change(ip, -(s64)blen, ip->i_inode.i_uid, ip->i_inode.i_gid);
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
	struct gfs2_rgrpd *rgd;

	rgd = rgblk_free(sdp, bstart, blen, GFS2_BLKST_FREE);
	if (!rgd)
		return;
	trace_gfs2_block_alloc(ip, bstart, blen, GFS2_BLKST_FREE);
	rgd->rd_free += blen;

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh, 1);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);

	gfs2_trans_add_rg(rgd);

	gfs2_statfs_change(sdp, 0, +blen, 0);
	gfs2_quota_change(ip, -(s64)blen, ip->i_inode.i_uid, ip->i_inode.i_gid);
	gfs2_meta_wipe(ip, bstart, blen);
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
	trace_gfs2_block_alloc(ip, blkno, 1, GFS2_BLKST_UNLINKED);
	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh, 1);
	gfs2_rgrp_out(rgd, rgd->rd_bits[0].bi_bh->b_data);
	gfs2_trans_add_rg(rgd);
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

	gfs2_statfs_change(sdp, 0, +1, -1);
	gfs2_trans_add_rg(rgd);
}


void gfs2_free_di(struct gfs2_rgrpd *rgd, struct gfs2_inode *ip)
{
	gfs2_free_uninit_di(rgd, ip->i_no_addr);
	trace_gfs2_block_alloc(ip, ip->i_no_addr, 1, GFS2_BLKST_FREE);
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
	struct gfs2_holder ri_gh, rgd_gh;
	int error;

	error = gfs2_rindex_hold(sdp, &ri_gh);
	if (error)
		goto fail;

	error = -EINVAL;
	rgd = gfs2_blk2rgrpd(sdp, no_addr);
	if (!rgd)
		goto fail_rindex;

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_SHARED, 0, &rgd_gh);
	if (error)
		goto fail_rindex;

	if (gfs2_get_block_type(rgd, no_addr) != type)
		error = -ESTALE;

	gfs2_glock_dq_uninit(&rgd_gh);
fail_rindex:
	gfs2_glock_dq_uninit(&ri_gh);
fail:
	return error;
}

/**
 * gfs2_rlist_add - add a RG to a list of RGs
 * @sdp: the filesystem
 * @rlist: the list of resource groups
 * @block: the block
 *
 * Figure out what RG a block belongs to and add that RG to the list
 *
 * FIXME: Don't use NOFAIL
 *
 */

void gfs2_rlist_add(struct gfs2_sbd *sdp, struct gfs2_rgrp_list *rlist,
		    u64 block)
{
	struct gfs2_rgrpd *rgd;
	struct gfs2_rgrpd **tmp;
	unsigned int new_space;
	unsigned int x;

	if (gfs2_assert_warn(sdp, !rlist->rl_ghs))
		return;

	rgd = gfs2_blk2rgrpd(sdp, block);
	if (!rgd) {
		if (gfs2_consist(sdp))
			fs_err(sdp, "block = %llu\n", (unsigned long long)block);
		return;
	}

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
 * @flags: the modifier flags for the holder structures
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
	}
}


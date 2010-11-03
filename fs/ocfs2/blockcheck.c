/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * blockcheck.c
 *
 * Checksum and ECC codes for the OCFS2 userspace library.
 *
 * Copyright (C) 2006, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/crc32.h>
#include <linux/buffer_head.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/byteorder.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "blockcheck.h"


/*
 * We use the following conventions:
 *
 * d = # data bits
 * p = # parity bits
 * c = # total code bits (d + p)
 */


/*
 * Calculate the bit offset in the hamming code buffer based on the bit's
 * offset in the data buffer.  Since the hamming code reserves all
 * power-of-two bits for parity, the data bit number and the code bit
 * number are offset by all the parity bits beforehand.
 *
 * Recall that bit numbers in hamming code are 1-based.  This function
 * takes the 0-based data bit from the caller.
 *
 * An example.  Take bit 1 of the data buffer.  1 is a power of two (2^0),
 * so it's a parity bit.  2 is a power of two (2^1), so it's a parity bit.
 * 3 is not a power of two.  So bit 1 of the data buffer ends up as bit 3
 * in the code buffer.
 *
 * The caller can pass in *p if it wants to keep track of the most recent
 * number of parity bits added.  This allows the function to start the
 * calculation at the last place.
 */
static unsigned int calc_code_bit(unsigned int i, unsigned int *p_cache)
{
	unsigned int b, p = 0;

	/*
	 * Data bits are 0-based, but we're talking code bits, which
	 * are 1-based.
	 */
	b = i + 1;

	/* Use the cache if it is there */
	if (p_cache)
		p = *p_cache;
        b += p;

	/*
	 * For every power of two below our bit number, bump our bit.
	 *
	 * We compare with (b + 1) because we have to compare with what b
	 * would be _if_ it were bumped up by the parity bit.  Capice?
	 *
	 * p is set above.
	 */
	for (; (1 << p) < (b + 1); p++)
		b++;

	if (p_cache)
		*p_cache = p;

	return b;
}

/*
 * This is the low level encoder function.  It can be called across
 * multiple hunks just like the crc32 code.  'd' is the number of bits
 * _in_this_hunk_.  nr is the bit offset of this hunk.  So, if you had
 * two 512B buffers, you would do it like so:
 *
 * parity = ocfs2_hamming_encode(0, buf1, 512 * 8, 0);
 * parity = ocfs2_hamming_encode(parity, buf2, 512 * 8, 512 * 8);
 *
 * If you just have one buffer, use ocfs2_hamming_encode_block().
 */
u32 ocfs2_hamming_encode(u32 parity, void *data, unsigned int d, unsigned int nr)
{
	unsigned int i, b, p = 0;

	BUG_ON(!d);

	/*
	 * b is the hamming code bit number.  Hamming code specifies a
	 * 1-based array, but C uses 0-based.  So 'i' is for C, and 'b' is
	 * for the algorithm.
	 *
	 * The i++ in the for loop is so that the start offset passed
	 * to ocfs2_find_next_bit_set() is one greater than the previously
	 * found bit.
	 */
	for (i = 0; (i = ocfs2_find_next_bit(data, d, i)) < d; i++)
	{
		/*
		 * i is the offset in this hunk, nr + i is the total bit
		 * offset.
		 */
		b = calc_code_bit(nr + i, &p);

		/*
		 * Data bits in the resultant code are checked by
		 * parity bits that are part of the bit number
		 * representation.  Huh?
		 *
		 * <wikipedia href="http://en.wikipedia.org/wiki/Hamming_code">
		 * In other words, the parity bit at position 2^k
		 * checks bits in positions having bit k set in
		 * their binary representation.  Conversely, for
		 * instance, bit 13, i.e. 1101(2), is checked by
		 * bits 1000(2) = 8, 0100(2)=4 and 0001(2) = 1.
		 * </wikipedia>
		 *
		 * Note that 'k' is the _code_ bit number.  'b' in
		 * our loop.
		 */
		parity ^= b;
	}

	/* While the data buffer was treated as little endian, the
	 * return value is in host endian. */
	return parity;
}

u32 ocfs2_hamming_encode_block(void *data, unsigned int blocksize)
{
	return ocfs2_hamming_encode(0, data, blocksize * 8, 0);
}

/*
 * Like ocfs2_hamming_encode(), this can handle hunks.  nr is the bit
 * offset of the current hunk.  If bit to be fixed is not part of the
 * current hunk, this does nothing.
 *
 * If you only have one hunk, use ocfs2_hamming_fix_block().
 */
void ocfs2_hamming_fix(void *data, unsigned int d, unsigned int nr,
		       unsigned int fix)
{
	unsigned int i, b;

	BUG_ON(!d);

	/*
	 * If the bit to fix has an hweight of 1, it's a parity bit.  One
	 * busted parity bit is its own error.  Nothing to do here.
	 */
	if (hweight32(fix) == 1)
		return;

	/*
	 * nr + d is the bit right past the data hunk we're looking at.
	 * If fix after that, nothing to do
	 */
	if (fix >= calc_code_bit(nr + d, NULL))
		return;

	/*
	 * nr is the offset in the data hunk we're starting at.  Let's
	 * start b at the offset in the code buffer.  See hamming_encode()
	 * for a more detailed description of 'b'.
	 */
	b = calc_code_bit(nr, NULL);
	/* If the fix is before this hunk, nothing to do */
	if (fix < b)
		return;

	for (i = 0; i < d; i++, b++)
	{
		/* Skip past parity bits */
		while (hweight32(b) == 1)
			b++;

		/*
		 * i is the offset in this data hunk.
		 * nr + i is the offset in the total data buffer.
		 * b is the offset in the total code buffer.
		 *
		 * Thus, when b == fix, bit i in the current hunk needs
		 * fixing.
		 */
		if (b == fix)
		{
			if (ocfs2_test_bit(i, data))
				ocfs2_clear_bit(i, data);
			else
				ocfs2_set_bit(i, data);
			break;
		}
	}
}

void ocfs2_hamming_fix_block(void *data, unsigned int blocksize,
			     unsigned int fix)
{
	ocfs2_hamming_fix(data, blocksize * 8, 0, fix);
}


/*
 * Debugfs handling.
 */

#ifdef CONFIG_DEBUG_FS

static int blockcheck_u64_get(void *data, u64 *val)
{
	*val = *(u64 *)data;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(blockcheck_fops, blockcheck_u64_get, NULL, "%llu\n");

static struct dentry *blockcheck_debugfs_create(const char *name,
						struct dentry *parent,
						u64 *value)
{
	return debugfs_create_file(name, S_IFREG | S_IRUSR, parent, value,
				   &blockcheck_fops);
}

static void ocfs2_blockcheck_debug_remove(struct ocfs2_blockcheck_stats *stats)
{
	if (stats) {
		debugfs_remove(stats->b_debug_check);
		stats->b_debug_check = NULL;
		debugfs_remove(stats->b_debug_failure);
		stats->b_debug_failure = NULL;
		debugfs_remove(stats->b_debug_recover);
		stats->b_debug_recover = NULL;
		debugfs_remove(stats->b_debug_dir);
		stats->b_debug_dir = NULL;
	}
}

static int ocfs2_blockcheck_debug_install(struct ocfs2_blockcheck_stats *stats,
					  struct dentry *parent)
{
	int rc = -EINVAL;

	if (!stats)
		goto out;

	stats->b_debug_dir = debugfs_create_dir("blockcheck", parent);
	if (!stats->b_debug_dir)
		goto out;

	stats->b_debug_check =
		blockcheck_debugfs_create("blocks_checked",
					  stats->b_debug_dir,
					  &stats->b_check_count);

	stats->b_debug_failure =
		blockcheck_debugfs_create("checksums_failed",
					  stats->b_debug_dir,
					  &stats->b_failure_count);

	stats->b_debug_recover =
		blockcheck_debugfs_create("ecc_recoveries",
					  stats->b_debug_dir,
					  &stats->b_recover_count);
	if (stats->b_debug_check && stats->b_debug_failure &&
	    stats->b_debug_recover)
		rc = 0;

out:
	if (rc)
		ocfs2_blockcheck_debug_remove(stats);
	return rc;
}
#else
static inline int ocfs2_blockcheck_debug_install(struct ocfs2_blockcheck_stats *stats,
						 struct dentry *parent)
{
	return 0;
}

static inline void ocfs2_blockcheck_debug_remove(struct ocfs2_blockcheck_stats *stats)
{
}
#endif  /* CONFIG_DEBUG_FS */

/* Always-called wrappers for starting and stopping the debugfs files */
int ocfs2_blockcheck_stats_debugfs_install(struct ocfs2_blockcheck_stats *stats,
					   struct dentry *parent)
{
	return ocfs2_blockcheck_debug_install(stats, parent);
}

void ocfs2_blockcheck_stats_debugfs_remove(struct ocfs2_blockcheck_stats *stats)
{
	ocfs2_blockcheck_debug_remove(stats);
}

static void ocfs2_blockcheck_inc_check(struct ocfs2_blockcheck_stats *stats)
{
	u64 new_count;

	if (!stats)
		return;

	spin_lock(&stats->b_lock);
	stats->b_check_count++;
	new_count = stats->b_check_count;
	spin_unlock(&stats->b_lock);

	if (!new_count)
		mlog(ML_NOTICE, "Block check count has wrapped\n");
}

static void ocfs2_blockcheck_inc_failure(struct ocfs2_blockcheck_stats *stats)
{
	u64 new_count;

	if (!stats)
		return;

	spin_lock(&stats->b_lock);
	stats->b_failure_count++;
	new_count = stats->b_failure_count;
	spin_unlock(&stats->b_lock);

	if (!new_count)
		mlog(ML_NOTICE, "Checksum failure count has wrapped\n");
}

static void ocfs2_blockcheck_inc_recover(struct ocfs2_blockcheck_stats *stats)
{
	u64 new_count;

	if (!stats)
		return;

	spin_lock(&stats->b_lock);
	stats->b_recover_count++;
	new_count = stats->b_recover_count;
	spin_unlock(&stats->b_lock);

	if (!new_count)
		mlog(ML_NOTICE, "ECC recovery count has wrapped\n");
}



/*
 * These are the low-level APIs for using the ocfs2_block_check structure.
 */

/*
 * This function generates check information for a block.
 * data is the block to be checked.  bc is a pointer to the
 * ocfs2_block_check structure describing the crc32 and the ecc.
 *
 * bc should be a pointer inside data, as the function will
 * take care of zeroing it before calculating the check information.  If
 * bc does not point inside data, the caller must make sure any inline
 * ocfs2_block_check structures are zeroed.
 *
 * The data buffer must be in on-disk endian (little endian for ocfs2).
 * bc will be filled with little-endian values and will be ready to go to
 * disk.
 */
void ocfs2_block_check_compute(void *data, size_t blocksize,
			       struct ocfs2_block_check *bc)
{
	u32 crc;
	u32 ecc;

	memset(bc, 0, sizeof(struct ocfs2_block_check));

	crc = crc32_le(~0, data, blocksize);
	ecc = ocfs2_hamming_encode_block(data, blocksize);

	/*
	 * No ecc'd ocfs2 structure is larger than 4K, so ecc will be no
	 * larger than 16 bits.
	 */
	BUG_ON(ecc > USHRT_MAX);

	bc->bc_crc32e = cpu_to_le32(crc);
	bc->bc_ecc = cpu_to_le16((u16)ecc);
}

/*
 * This function validates existing check information.  Like _compute,
 * the function will take care of zeroing bc before calculating check codes.
 * If bc is not a pointer inside data, the caller must have zeroed any
 * inline ocfs2_block_check structures.
 *
 * Again, the data passed in should be the on-disk endian.
 */
int ocfs2_block_check_validate(void *data, size_t blocksize,
			       struct ocfs2_block_check *bc,
			       struct ocfs2_blockcheck_stats *stats)
{
	int rc = 0;
	struct ocfs2_block_check check;
	u32 crc, ecc;

	ocfs2_blockcheck_inc_check(stats);

	check.bc_crc32e = le32_to_cpu(bc->bc_crc32e);
	check.bc_ecc = le16_to_cpu(bc->bc_ecc);

	memset(bc, 0, sizeof(struct ocfs2_block_check));

	/* Fast path - if the crc32 validates, we're good to go */
	crc = crc32_le(~0, data, blocksize);
	if (crc == check.bc_crc32e)
		goto out;

	ocfs2_blockcheck_inc_failure(stats);
	mlog(ML_ERROR,
	     "CRC32 failed: stored: 0x%x, computed 0x%x. Applying ECC.\n",
	     (unsigned int)check.bc_crc32e, (unsigned int)crc);

	/* Ok, try ECC fixups */
	ecc = ocfs2_hamming_encode_block(data, blocksize);
	ocfs2_hamming_fix_block(data, blocksize, ecc ^ check.bc_ecc);

	/* And check the crc32 again */
	crc = crc32_le(~0, data, blocksize);
	if (crc == check.bc_crc32e) {
		ocfs2_blockcheck_inc_recover(stats);
		goto out;
	}

	mlog(ML_ERROR, "Fixed CRC32 failed: stored: 0x%x, computed 0x%x\n",
	     (unsigned int)check.bc_crc32e, (unsigned int)crc);

	rc = -EIO;

out:
	bc->bc_crc32e = cpu_to_le32(check.bc_crc32e);
	bc->bc_ecc = cpu_to_le16(check.bc_ecc);

	return rc;
}

/*
 * This function generates check information for a list of buffer_heads.
 * bhs is the blocks to be checked.  bc is a pointer to the
 * ocfs2_block_check structure describing the crc32 and the ecc.
 *
 * bc should be a pointer inside data, as the function will
 * take care of zeroing it before calculating the check information.  If
 * bc does not point inside data, the caller must make sure any inline
 * ocfs2_block_check structures are zeroed.
 *
 * The data buffer must be in on-disk endian (little endian for ocfs2).
 * bc will be filled with little-endian values and will be ready to go to
 * disk.
 */
void ocfs2_block_check_compute_bhs(struct buffer_head **bhs, int nr,
				   struct ocfs2_block_check *bc)
{
	int i;
	u32 crc, ecc;

	BUG_ON(nr < 0);

	if (!nr)
		return;

	memset(bc, 0, sizeof(struct ocfs2_block_check));

	for (i = 0, crc = ~0, ecc = 0; i < nr; i++) {
		crc = crc32_le(crc, bhs[i]->b_data, bhs[i]->b_size);
		/*
		 * The number of bits in a buffer is obviously b_size*8.
		 * The offset of this buffer is b_size*i, so the bit offset
		 * of this buffer is b_size*8*i.
		 */
		ecc = (u16)ocfs2_hamming_encode(ecc, bhs[i]->b_data,
						bhs[i]->b_size * 8,
						bhs[i]->b_size * 8 * i);
	}

	/*
	 * No ecc'd ocfs2 structure is larger than 4K, so ecc will be no
	 * larger than 16 bits.
	 */
	BUG_ON(ecc > USHRT_MAX);

	bc->bc_crc32e = cpu_to_le32(crc);
	bc->bc_ecc = cpu_to_le16((u16)ecc);
}

/*
 * This function validates existing check information on a list of
 * buffer_heads.  Like _compute_bhs, the function will take care of
 * zeroing bc before calculating check codes.  If bc is not a pointer
 * inside data, the caller must have zeroed any inline
 * ocfs2_block_check structures.
 *
 * Again, the data passed in should be the on-disk endian.
 */
int ocfs2_block_check_validate_bhs(struct buffer_head **bhs, int nr,
				   struct ocfs2_block_check *bc,
				   struct ocfs2_blockcheck_stats *stats)
{
	int i, rc = 0;
	struct ocfs2_block_check check;
	u32 crc, ecc, fix;

	BUG_ON(nr < 0);

	if (!nr)
		return 0;

	ocfs2_blockcheck_inc_check(stats);

	check.bc_crc32e = le32_to_cpu(bc->bc_crc32e);
	check.bc_ecc = le16_to_cpu(bc->bc_ecc);

	memset(bc, 0, sizeof(struct ocfs2_block_check));

	/* Fast path - if the crc32 validates, we're good to go */
	for (i = 0, crc = ~0; i < nr; i++)
		crc = crc32_le(crc, bhs[i]->b_data, bhs[i]->b_size);
	if (crc == check.bc_crc32e)
		goto out;

	ocfs2_blockcheck_inc_failure(stats);
	mlog(ML_ERROR,
	     "CRC32 failed: stored: %u, computed %u.  Applying ECC.\n",
	     (unsigned int)check.bc_crc32e, (unsigned int)crc);

	/* Ok, try ECC fixups */
	for (i = 0, ecc = 0; i < nr; i++) {
		/*
		 * The number of bits in a buffer is obviously b_size*8.
		 * The offset of this buffer is b_size*i, so the bit offset
		 * of this buffer is b_size*8*i.
		 */
		ecc = (u16)ocfs2_hamming_encode(ecc, bhs[i]->b_data,
						bhs[i]->b_size * 8,
						bhs[i]->b_size * 8 * i);
	}
	fix = ecc ^ check.bc_ecc;
	for (i = 0; i < nr; i++) {
		/*
		 * Try the fix against each buffer.  It will only affect
		 * one of them.
		 */
		ocfs2_hamming_fix(bhs[i]->b_data, bhs[i]->b_size * 8,
				  bhs[i]->b_size * 8 * i, fix);
	}

	/* And check the crc32 again */
	for (i = 0, crc = ~0; i < nr; i++)
		crc = crc32_le(crc, bhs[i]->b_data, bhs[i]->b_size);
	if (crc == check.bc_crc32e) {
		ocfs2_blockcheck_inc_recover(stats);
		goto out;
	}

	mlog(ML_ERROR, "Fixed CRC32 failed: stored: %u, computed %u\n",
	     (unsigned int)check.bc_crc32e, (unsigned int)crc);

	rc = -EIO;

out:
	bc->bc_crc32e = cpu_to_le32(check.bc_crc32e);
	bc->bc_ecc = cpu_to_le16(check.bc_ecc);

	return rc;
}

/*
 * These are the main API.  They check the superblock flag before
 * calling the underlying operations.
 *
 * They expect the buffer(s) to be in disk format.
 */
void ocfs2_compute_meta_ecc(struct super_block *sb, void *data,
			    struct ocfs2_block_check *bc)
{
	if (ocfs2_meta_ecc(OCFS2_SB(sb)))
		ocfs2_block_check_compute(data, sb->s_blocksize, bc);
}

int ocfs2_validate_meta_ecc(struct super_block *sb, void *data,
			    struct ocfs2_block_check *bc)
{
	int rc = 0;
	struct ocfs2_super *osb = OCFS2_SB(sb);

	if (ocfs2_meta_ecc(osb))
		rc = ocfs2_block_check_validate(data, sb->s_blocksize, bc,
						&osb->osb_ecc_stats);

	return rc;
}

void ocfs2_compute_meta_ecc_bhs(struct super_block *sb,
				struct buffer_head **bhs, int nr,
				struct ocfs2_block_check *bc)
{
	if (ocfs2_meta_ecc(OCFS2_SB(sb)))
		ocfs2_block_check_compute_bhs(bhs, nr, bc);
}

int ocfs2_validate_meta_ecc_bhs(struct super_block *sb,
				struct buffer_head **bhs, int nr,
				struct ocfs2_block_check *bc)
{
	int rc = 0;
	struct ocfs2_super *osb = OCFS2_SB(sb);

	if (ocfs2_meta_ecc(osb))
		rc = ocfs2_block_check_validate_bhs(bhs, nr, bc,
						    &osb->osb_ecc_stats);

	return rc;
}


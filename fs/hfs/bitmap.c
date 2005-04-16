/*
 *  linux/fs/hfs/bitmap.c
 *
 * Copyright (C) 1996-1997  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * Based on GPLed code Copyright (C) 1995  Michael Dreher
 *
 * This file contains the code to modify the volume bitmap:
 * search/set/clear bits.
 */

#include "hfs_fs.h"

/*
 * hfs_find_zero_bit()
 *
 * Description:
 *  Given a block of memory, its length in bits, and a starting bit number,
 *  determine the number of the first zero bits (in left-to-right ordering)
 *  in that range.
 *
 *  Returns >= 'size' if no zero bits are found in the range.
 *
 *  Accesses memory in 32-bit aligned chunks of 32-bits and thus
 *  may read beyond the 'size'th bit.
 */
static u32 hfs_find_set_zero_bits(__be32 *bitmap, u32 size, u32 offset, u32 *max)
{
	__be32 *curr, *end;
	u32 mask, start, len, n;
	__be32 val;
	int i;

	len = *max;
	if (!len)
		return size;

	curr = bitmap + (offset / 32);
	end = bitmap + ((size + 31) / 32);

	/* scan the first partial u32 for zero bits */
	val = *curr;
	if (~val) {
		n = be32_to_cpu(val);
		i = offset % 32;
		mask = (1U << 31) >> i;
		for (; i < 32; mask >>= 1, i++) {
			if (!(n & mask))
				goto found;
		}
	}

	/* scan complete u32s for the first zero bit */
	while (++curr < end) {
		val = *curr;
		if (~val) {
			n = be32_to_cpu(val);
			mask = 1 << 31;
			for (i = 0; i < 32; mask >>= 1, i++) {
				if (!(n & mask))
					goto found;
			}
		}
	}
	return size;

found:
	start = (curr - bitmap) * 32 + i;
	if (start >= size)
		return start;
	/* do any partial u32 at the start */
	len = min(size - start, len);
	while (1) {
		n |= mask;
		if (++i >= 32)
			break;
		mask >>= 1;
		if (!--len || n & mask)
			goto done;
	}
	if (!--len)
		goto done;
	*curr++ = cpu_to_be32(n);
	/* do full u32s */
	while (1) {
		n = be32_to_cpu(*curr);
		if (len < 32)
			break;
		if (n) {
			len = 32;
			break;
		}
		*curr++ = cpu_to_be32(0xffffffff);
		len -= 32;
	}
	/* do any partial u32 at end */
	mask = 1U << 31;
	for (i = 0; i < len; i++) {
		if (n & mask)
			break;
		n |= mask;
		mask >>= 1;
	}
done:
	*curr = cpu_to_be32(n);
	*max = (curr - bitmap) * 32 + i - start;
	return start;
}

/*
 * hfs_vbm_search_free()
 *
 * Description:
 *   Search for 'num_bits' consecutive cleared bits in the bitmap blocks of
 *   the hfs MDB. 'mdb' had better be locked or the returned range
 *   may be no longer free, when this functions returns!
 *   XXX Currently the search starts from bit 0, but it should start with
 *   the bit number stored in 's_alloc_ptr' of the MDB.
 * Input Variable(s):
 *   struct hfs_mdb *mdb: Pointer to the hfs MDB
 *   u16 *num_bits: Pointer to the number of cleared bits
 *     to search for
 * Output Variable(s):
 *   u16 *num_bits: The number of consecutive clear bits of the
 *     returned range. If the bitmap is fragmented, this will be less than
 *     requested and it will be zero, when the disk is full.
 * Returns:
 *   The number of the first bit of the range of cleared bits which has been
 *   found. When 'num_bits' is zero, this is invalid!
 * Preconditions:
 *   'mdb' points to a "valid" (struct hfs_mdb).
 *   'num_bits' points to a variable of type (u16), which contains
 *	the number of cleared bits to find.
 * Postconditions:
 *   'num_bits' is set to the length of the found sequence.
 */
u32 hfs_vbm_search_free(struct super_block *sb, u32 goal, u32 *num_bits)
{
	void *bitmap;
	u32 pos;

	/* make sure we have actual work to perform */
	if (!*num_bits)
		return 0;

	down(&HFS_SB(sb)->bitmap_lock);
	bitmap = HFS_SB(sb)->bitmap;

	pos = hfs_find_set_zero_bits(bitmap, HFS_SB(sb)->fs_ablocks, goal, num_bits);
	if (pos >= HFS_SB(sb)->fs_ablocks) {
		if (goal)
			pos = hfs_find_set_zero_bits(bitmap, goal, 0, num_bits);
		if (pos >= HFS_SB(sb)->fs_ablocks) {
			*num_bits = pos = 0;
			goto out;
		}
	}

	dprint(DBG_BITMAP, "alloc_bits: %u,%u\n", pos, *num_bits);
	HFS_SB(sb)->free_ablocks -= *num_bits;
	hfs_bitmap_dirty(sb);
out:
	up(&HFS_SB(sb)->bitmap_lock);
	return pos;
}


/*
 * hfs_clear_vbm_bits()
 *
 * Description:
 *   Clear the requested bits in the volume bitmap of the hfs filesystem
 * Input Variable(s):
 *   struct hfs_mdb *mdb: Pointer to the hfs MDB
 *   u16 start: The offset of the first bit
 *   u16 count: The number of bits
 * Output Variable(s):
 *   None
 * Returns:
 *    0: no error
 *   -1: One of the bits was already clear.  This is a strange
 *	 error and when it happens, the filesystem must be repaired!
 *   -2: One or more of the bits are out of range of the bitmap.
 * Preconditions:
 *   'mdb' points to a "valid" (struct hfs_mdb).
 * Postconditions:
 *   Starting with bit number 'start', 'count' bits in the volume bitmap
 *   are cleared. The affected bitmap blocks are marked "dirty", the free
 *   block count of the MDB is updated and the MDB is marked dirty.
 */
int hfs_clear_vbm_bits(struct super_block *sb, u16 start, u16 count)
{
	__be32 *curr;
	u32 mask;
	int i, len;

	/* is there any actual work to be done? */
	if (!count)
		return 0;

	dprint(DBG_BITMAP, "clear_bits: %u,%u\n", start, count);
	/* are all of the bits in range? */
	if ((start + count) > HFS_SB(sb)->fs_ablocks)
		return -2;

	down(&HFS_SB(sb)->bitmap_lock);
	/* bitmap is always on a 32-bit boundary */
	curr = HFS_SB(sb)->bitmap + (start / 32);
	len = count;

	/* do any partial u32 at the start */
	i = start % 32;
	if (i) {
		int j = 32 - i;
		mask = 0xffffffffU << j;
		if (j > count) {
			mask |= 0xffffffffU >> (i + count);
			*curr &= cpu_to_be32(mask);
			goto out;
		}
		*curr++ &= cpu_to_be32(mask);
		count -= j;
	}

	/* do full u32s */
	while (count >= 32) {
		*curr++ = 0;
		count -= 32;
	}
	/* do any partial u32 at end */
	if (count) {
		mask = 0xffffffffU >> count;
		*curr &= cpu_to_be32(mask);
	}
out:
	HFS_SB(sb)->free_ablocks += len;
	up(&HFS_SB(sb)->bitmap_lock);
	hfs_bitmap_dirty(sb);

	return 0;
}

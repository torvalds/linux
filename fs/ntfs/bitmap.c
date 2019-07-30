// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bitmap.c - NTFS kernel bitmap handling.  Part of the Linux-NTFS project.
 *
 * Copyright (c) 2004-2005 Anton Altaparmakov
 */

#ifdef NTFS_RW

#include <linux/pagemap.h>

#include "bitmap.h"
#include "debug.h"
#include "aops.h"
#include "ntfs.h"

/**
 * __ntfs_bitmap_set_bits_in_run - set a run of bits in a bitmap to a value
 * @vi:			vfs inode describing the bitmap
 * @start_bit:		first bit to set
 * @count:		number of bits to set
 * @value:		value to set the bits to (i.e. 0 or 1)
 * @is_rollback:	if 'true' this is a rollback operation
 *
 * Set @count bits starting at bit @start_bit in the bitmap described by the
 * vfs inode @vi to @value, where @value is either 0 or 1.
 *
 * @is_rollback should always be 'false', it is for internal use to rollback
 * errors.  You probably want to use ntfs_bitmap_set_bits_in_run() instead.
 *
 * Return 0 on success and -errno on error.
 */
int __ntfs_bitmap_set_bits_in_run(struct inode *vi, const s64 start_bit,
		const s64 count, const u8 value, const bool is_rollback)
{
	s64 cnt = count;
	pgoff_t index, end_index;
	struct address_space *mapping;
	struct page *page;
	u8 *kaddr;
	int pos, len;
	u8 bit;

	BUG_ON(!vi);
	ntfs_debug("Entering for i_ino 0x%lx, start_bit 0x%llx, count 0x%llx, "
			"value %u.%s", vi->i_ino, (unsigned long long)start_bit,
			(unsigned long long)cnt, (unsigned int)value,
			is_rollback ? " (rollback)" : "");
	BUG_ON(start_bit < 0);
	BUG_ON(cnt < 0);
	BUG_ON(value > 1);
	/*
	 * Calculate the indices for the pages containing the first and last
	 * bits, i.e. @start_bit and @start_bit + @cnt - 1, respectively.
	 */
	index = start_bit >> (3 + PAGE_SHIFT);
	end_index = (start_bit + cnt - 1) >> (3 + PAGE_SHIFT);

	/* Get the page containing the first bit (@start_bit). */
	mapping = vi->i_mapping;
	page = ntfs_map_page(mapping, index);
	if (IS_ERR(page)) {
		if (!is_rollback)
			ntfs_error(vi->i_sb, "Failed to map first page (error "
					"%li), aborting.", PTR_ERR(page));
		return PTR_ERR(page);
	}
	kaddr = page_address(page);

	/* Set @pos to the position of the byte containing @start_bit. */
	pos = (start_bit >> 3) & ~PAGE_MASK;

	/* Calculate the position of @start_bit in the first byte. */
	bit = start_bit & 7;

	/* If the first byte is partial, modify the appropriate bits in it. */
	if (bit) {
		u8 *byte = kaddr + pos;
		while ((bit & 7) && cnt) {
			cnt--;
			if (value)
				*byte |= 1 << bit++;
			else
				*byte &= ~(1 << bit++);
		}
		/* If we are done, unmap the page and return success. */
		if (!cnt)
			goto done;

		/* Update @pos to the new position. */
		pos++;
	}
	/*
	 * Depending on @value, modify all remaining whole bytes in the page up
	 * to @cnt.
	 */
	len = min_t(s64, cnt >> 3, PAGE_SIZE - pos);
	memset(kaddr + pos, value ? 0xff : 0, len);
	cnt -= len << 3;

	/* Update @len to point to the first not-done byte in the page. */
	if (cnt < 8)
		len += pos;

	/* If we are not in the last page, deal with all subsequent pages. */
	while (index < end_index) {
		BUG_ON(cnt <= 0);

		/* Update @index and get the next page. */
		flush_dcache_page(page);
		set_page_dirty(page);
		ntfs_unmap_page(page);
		page = ntfs_map_page(mapping, ++index);
		if (IS_ERR(page))
			goto rollback;
		kaddr = page_address(page);
		/*
		 * Depending on @value, modify all remaining whole bytes in the
		 * page up to @cnt.
		 */
		len = min_t(s64, cnt >> 3, PAGE_SIZE);
		memset(kaddr, value ? 0xff : 0, len);
		cnt -= len << 3;
	}
	/*
	 * The currently mapped page is the last one.  If the last byte is
	 * partial, modify the appropriate bits in it.  Note, @len is the
	 * position of the last byte inside the page.
	 */
	if (cnt) {
		u8 *byte;

		BUG_ON(cnt > 7);

		bit = cnt;
		byte = kaddr + len;
		while (bit--) {
			if (value)
				*byte |= 1 << bit;
			else
				*byte &= ~(1 << bit);
		}
	}
done:
	/* We are done.  Unmap the page and return success. */
	flush_dcache_page(page);
	set_page_dirty(page);
	ntfs_unmap_page(page);
	ntfs_debug("Done.");
	return 0;
rollback:
	/*
	 * Current state:
	 *	- no pages are mapped
	 *	- @count - @cnt is the number of bits that have been modified
	 */
	if (is_rollback)
		return PTR_ERR(page);
	if (count != cnt)
		pos = __ntfs_bitmap_set_bits_in_run(vi, start_bit, count - cnt,
				value ? 0 : 1, true);
	else
		pos = 0;
	if (!pos) {
		/* Rollback was successful. */
		ntfs_error(vi->i_sb, "Failed to map subsequent page (error "
				"%li), aborting.", PTR_ERR(page));
	} else {
		/* Rollback failed. */
		ntfs_error(vi->i_sb, "Failed to map subsequent page (error "
				"%li) and rollback failed (error %i).  "
				"Aborting and leaving inconsistent metadata.  "
				"Unmount and run chkdsk.", PTR_ERR(page), pos);
		NVolSetErrors(NTFS_SB(vi->i_sb));
	}
	return PTR_ERR(page);
}

#endif /* NTFS_RW */

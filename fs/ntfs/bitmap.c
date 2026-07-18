// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS kernel bitmap handling.
 *
 * Copyright (c) 2004-2005 Anton Altaparmakov
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include <linux/bitops.h>
#include <linux/blkdev.h>

#include "bitmap.h"
#include "ntfs.h"

int ntfs_trim_fs(struct ntfs_volume *vol, struct fstrim_range *range)
{
	size_t buf_clusters;
	pgoff_t index, start_index, end_index;
	struct file_ra_state *ra;
	struct folio *folio;
	unsigned long *bitmap;
	char *kaddr;
	u64 end, trimmed = 0, start_buf, end_buf, end_cluster;
	u64 start_cluster = ntfs_bytes_to_cluster(vol, range->start);
	u32 dq = bdev_discard_granularity(vol->sb->s_bdev);
	int ret = 0;

	if (!dq)
		dq = vol->cluster_size;

	if (start_cluster >= vol->nr_clusters)
		return -EINVAL;

	if (range->len == (u64)-1)
		end_cluster = vol->nr_clusters;
	else {
		end_cluster = ntfs_bytes_to_cluster(vol,
				(range->start + range->len + vol->cluster_size - 1));
		if (end_cluster > vol->nr_clusters)
			end_cluster = vol->nr_clusters;
	}

	ra = kzalloc(sizeof(*ra), GFP_NOFS);
	if (!ra)
		return -ENOMEM;

	buf_clusters = PAGE_SIZE * 8;
	start_index = start_cluster >> 15;
	end_index = (end_cluster + buf_clusters - 1) >> 15;

	for (index = start_index; index < end_index; index++) {
		folio = ntfs_get_locked_folio(vol->lcnbmp_ino->i_mapping,
				index, end_index, ra);
		if (IS_ERR(folio)) {
			ret = PTR_ERR(folio);
			goto out_free;
		}

		kaddr = kmap_local_folio(folio, 0);
		bitmap = (unsigned long *)kaddr;

		start_buf = max_t(u64, index * buf_clusters, start_cluster);
		end_buf = min_t(u64, (index + 1) * buf_clusters, end_cluster);

		end = start_buf;
		while (end < end_buf) {
			u64 aligned_start, aligned_count;
			u64 start = find_next_zero_bit(bitmap, end_buf - start_buf,
					end - start_buf) + start_buf;
			if (start >= end_buf)
				break;

			end = find_next_bit(bitmap, end_buf - start_buf,
					start - start_buf) + start_buf;

			aligned_start = ALIGN(ntfs_cluster_to_bytes(vol, start), dq);
			aligned_count =
				ALIGN_DOWN(ntfs_cluster_to_bytes(vol, end - start), dq);
			if (aligned_count >= range->minlen) {
				ret = blkdev_issue_discard(vol->sb->s_bdev, aligned_start >> 9,
						aligned_count >> 9, GFP_NOFS);
				if (ret)
					goto out_unmap;
				trimmed += aligned_count;
			}
		}

out_unmap:
		kunmap_local(kaddr);
		folio_unlock(folio);
		folio_put(folio);

		if (ret)
			goto out_free;
	}

	range->len = trimmed;

out_free:
	kfree(ra);
	return ret;
}

/*
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
	struct folio *folio;
	u8 *kaddr;
	int pos, len, err;
	u8 bit;
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_volume *vol = ni->vol;

	ntfs_debug("Entering for i_ino 0x%llx, start_bit 0x%llx, count 0x%llx, value %u.%s",
			ni->mft_no, (unsigned long long)start_bit,
			(unsigned long long)cnt, (unsigned int)value,
			is_rollback ? " (rollback)" : "");

	if (start_bit < 0 || cnt < 0 || value > 1)
		return -EINVAL;

	/*
	 * Calculate the indices for the pages containing the first and last
	 * bits, i.e. @start_bit and @start_bit + @cnt - 1, respectively.
	 */
	index = start_bit >> (3 + PAGE_SHIFT);
	end_index = (start_bit + cnt - 1) >> (3 + PAGE_SHIFT);

	/* Get the page containing the first bit (@start_bit). */
	mapping = vi->i_mapping;
	folio = read_mapping_folio(mapping, index, NULL);
	if (IS_ERR(folio)) {
		if (!is_rollback)
			ntfs_error(vi->i_sb,
				"Failed to map first page (error %li), aborting.",
				PTR_ERR(folio));
		return PTR_ERR(folio);
	}

	folio_lock(folio);
	kaddr = kmap_local_folio(folio, 0);

	/* Set @pos to the position of the byte containing @start_bit. */
	pos = (start_bit >> 3) & ~PAGE_MASK;

	/* Calculate the position of @start_bit in the first byte. */
	bit = start_bit & 7;

	/* If the first byte is partial, modify the appropriate bits in it. */
	if (bit) {
		u8 *byte = kaddr + pos;

		if (ni->mft_no == FILE_Bitmap)
			ntfs_set_lcn_empty_bits(vol, index, value, min_t(s64, 8 - bit, cnt));
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
	if (ni->mft_no == FILE_Bitmap)
		ntfs_set_lcn_empty_bits(vol, index, value, len << 3);

	/* Update @len to point to the first not-done byte in the page. */
	if (cnt < 8)
		len += pos;

	/* If we are not in the last page, deal with all subsequent pages. */
	while (index < end_index) {
		if (cnt <= 0) {
			err = -EIO;
			goto rollback;
		}

		/* Update @index and get the next folio. */
		folio_mark_dirty(folio);
		folio_unlock(folio);
		kunmap_local(kaddr);
		folio_put(folio);
		folio = read_mapping_folio(mapping, ++index, NULL);
		if (IS_ERR(folio)) {
			ntfs_error(vi->i_sb,
				   "Failed to map subsequent page (error %li), aborting.",
				   PTR_ERR(folio));
			err = PTR_ERR(folio);
			goto rollback;
		}

		folio_lock(folio);
		kaddr = kmap_local_folio(folio, 0);
		/*
		 * Depending on @value, modify all remaining whole bytes in the
		 * page up to @cnt.
		 */
		len = min_t(s64, cnt >> 3, PAGE_SIZE);
		memset(kaddr, value ? 0xff : 0, len);
		cnt -= len << 3;
		if (ni->mft_no == FILE_Bitmap)
			ntfs_set_lcn_empty_bits(vol, index, value, len << 3);
	}
	/*
	 * The currently mapped page is the last one.  If the last byte is
	 * partial, modify the appropriate bits in it.  Note, @len is the
	 * position of the last byte inside the page.
	 */
	if (cnt) {
		u8 *byte;

		WARN_ON(cnt > 7);

		bit = cnt;
		byte = kaddr + len;
		if (ni->mft_no == FILE_Bitmap)
			ntfs_set_lcn_empty_bits(vol, index, value, bit);
		while (bit--) {
			if (value)
				*byte |= 1 << bit;
			else
				*byte &= ~(1 << bit);
		}
	}
done:
	/* We are done.  Unmap the folio and return success. */
	folio_mark_dirty(folio);
	folio_unlock(folio);
	kunmap_local(kaddr);
	folio_put(folio);
	ntfs_debug("Done.");
	return 0;
rollback:
	/*
	 * Current state:
	 *	- no pages are mapped
	 *	- @count - @cnt is the number of bits that have been modified
	 */
	if (is_rollback)
		return err;
	if (count != cnt)
		pos = __ntfs_bitmap_set_bits_in_run(vi, start_bit, count - cnt,
				value ? 0 : 1, true);
	else
		pos = 0;
	if (!pos) {
		/* Rollback was successful. */
		ntfs_error(vi->i_sb,
			"Failed to map subsequent page (error %i), aborting.",
			err);
	} else {
		/* Rollback failed. */
		ntfs_error(vi->i_sb,
			"Failed to map subsequent page (error %i) and rollback failed (error %i). Aborting and leaving inconsistent metadata. Unmount and run chkdsk.",
			err, pos);
		NVolSetErrors(NTFS_SB(vi->i_sb));
	}
	return err;
}

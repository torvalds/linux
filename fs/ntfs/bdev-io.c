// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS block device I/O.
 *
 * Copyright (c) 2026 LG Electronics Co., Ltd.
 */

#include <linux/blkdev.h>

#include "ntfs.h"

/*
 * ntfs_bdev_read - Read data directly from block device using bio
 * @bdev:	block device to read from
 * @data:	destination buffer
 * @start:	starting byte offset on the block device
 * @size:	number of bytes to read
 *
 * Reads @size bytes starting from byte offset @start directly from the block
 * device using one or more BIOs. This function bypasses the page cache
 * completely and performs synchronous I/O with REQ_META | REQ_SYNC flags set.
 *
 * The @start offset must be sector-aligned (512 bytes). If it is not aligned,
 * the function will return -EINVAL.
 *
 * If the destination buffer @data is not a vmalloc address, it falls back
 * to the more efficient bdev_rw_virt() helper.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ntfs_bdev_read(struct block_device *bdev, char *data, loff_t start, size_t size)
{
	unsigned int done = 0, added;
	int error;
	struct bio *bio;
	enum req_op op;
	sector_t sector = start >> SECTOR_SHIFT;

	if (start & (SECTOR_SIZE - 1))
		return -EINVAL;

	op = REQ_OP_READ | REQ_META | REQ_SYNC;
	if (!is_vmalloc_addr(data))
		return bdev_rw_virt(bdev, sector, data, size, op);

	bio = bio_alloc(bdev,
			bio_max_segs(DIV_ROUND_UP(size, PAGE_SIZE)),
			op, GFP_KERNEL);
	bio->bi_iter.bi_sector = sector;

	do {
		added = bio_add_vmalloc_chunk(bio, data + done, size - done);
		if (!added) {
			struct bio	*prev = bio;

			bio = bio_alloc(prev->bi_bdev,
					bio_max_segs(DIV_ROUND_UP(size - done, PAGE_SIZE)),
					prev->bi_opf, GFP_KERNEL);
			bio->bi_iter.bi_sector = bio_end_sector(prev);
			bio_chain(prev, bio);
			submit_bio(prev);
		}
		done += added;
	} while (done < size);

	error = submit_bio_wait(bio);
	bio_put(bio);

	if (op == REQ_OP_READ)
		invalidate_kernel_vmap_range(data, size);
	return error;
}

/*
 * ntfs_bdev_write - Update block device contents via page cache
 * @sb:		super block of the mounted NTFS filesystem
 * @buf:	source buffer containing data to write
 * @start:	starting byte offset on the block device
 * @size:	number of bytes to write
 *
 * Writes @size bytes from @buf to the block device (sb->s_bdev) starting
 * at byte offset @start. The write is performed entirely through the page
 * cache of the block device's address space.
 */
int ntfs_bdev_write(struct super_block *sb, void *buf, loff_t start, size_t size)
{
	pgoff_t idx, idx_end;
	loff_t offset, end = start + size;
	u32 from, to, buf_off = 0;
	struct folio *folio;

	idx = start >> PAGE_SHIFT;
	idx_end = end >> PAGE_SHIFT;
	from = start & ~PAGE_MASK;

	if (idx == idx_end)
		idx_end++;

	for (; idx < idx_end; idx++, from = 0) {
		folio = read_mapping_folio(sb->s_bdev->bd_mapping, idx, NULL);
		if (IS_ERR(folio)) {
			ntfs_error(sb, "Unable to read %ld page", idx);
			return PTR_ERR(folio);
		}

		offset = (loff_t)idx << PAGE_SHIFT;
		to = min_t(u32, end - offset, PAGE_SIZE);

		memcpy_to_folio(folio, from, buf + buf_off, to);
		buf_off += to;
		folio_mark_uptodate(folio);
		folio_mark_dirty(folio);
		folio_put(folio);
	}

	return 0;
}

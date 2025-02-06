// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024-2025 Christoph Hellwig.
 */
#include <linux/iomap.h>

struct iomap_ioend *iomap_init_ioend(struct inode *inode,
		struct bio *bio, loff_t file_offset, u16 ioend_flags)
{
	struct iomap_ioend *ioend = iomap_ioend_from_bio(bio);

	atomic_set(&ioend->io_remaining, 1);
	ioend->io_error = 0;
	ioend->io_parent = NULL;
	INIT_LIST_HEAD(&ioend->io_list);
	ioend->io_flags = ioend_flags;
	ioend->io_inode = inode;
	ioend->io_offset = file_offset;
	ioend->io_size = bio->bi_iter.bi_size;
	ioend->io_sector = bio->bi_iter.bi_sector;
	return ioend;
}
EXPORT_SYMBOL_GPL(iomap_init_ioend);

/*
 * Split up to the first @max_len bytes from @ioend if the ioend covers more
 * than @max_len bytes.
 *
 * If @is_append is set, the split will be based on the hardware limits for
 * REQ_OP_ZONE_APPEND commands and can be less than @max_len if the hardware
 * limits don't allow the entire @max_len length.
 *
 * The bio embedded into @ioend must be a REQ_OP_WRITE because the block layer
 * does not allow splitting REQ_OP_ZONE_APPEND bios.  The file systems has to
 * switch the operation after this call, but before submitting the bio.
 */
struct iomap_ioend *iomap_split_ioend(struct iomap_ioend *ioend,
		unsigned int max_len, bool is_append)
{
	struct bio *bio = &ioend->io_bio;
	struct iomap_ioend *split_ioend;
	unsigned int nr_segs;
	int sector_offset;
	struct bio *split;

	if (is_append) {
		struct queue_limits *lim = bdev_limits(bio->bi_bdev);

		max_len = min(max_len,
			      lim->max_zone_append_sectors << SECTOR_SHIFT);

		sector_offset = bio_split_rw_at(bio, lim, &nr_segs, max_len);
		if (unlikely(sector_offset < 0))
			return ERR_PTR(sector_offset);
		if (!sector_offset)
			return NULL;
	} else {
		if (bio->bi_iter.bi_size <= max_len)
			return NULL;
		sector_offset = max_len >> SECTOR_SHIFT;
	}

	/* ensure the split ioend is still block size aligned */
	sector_offset = ALIGN_DOWN(sector_offset << SECTOR_SHIFT,
			i_blocksize(ioend->io_inode)) >> SECTOR_SHIFT;

	split = bio_split(bio, sector_offset, GFP_NOFS, &iomap_ioend_bioset);
	if (IS_ERR(split))
		return ERR_CAST(split);
	split->bi_private = bio->bi_private;
	split->bi_end_io = bio->bi_end_io;

	split_ioend = iomap_init_ioend(ioend->io_inode, split, ioend->io_offset,
			ioend->io_flags);
	split_ioend->io_parent = ioend;

	atomic_inc(&ioend->io_remaining);
	ioend->io_offset += split_ioend->io_size;
	ioend->io_size -= split_ioend->io_size;

	split_ioend->io_sector = ioend->io_sector;
	if (!is_append)
		ioend->io_sector += (split_ioend->io_size >> SECTOR_SHIFT);
	return split_ioend;
}
EXPORT_SYMBOL_GPL(iomap_split_ioend);

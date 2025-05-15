// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024-2025 Christoph Hellwig.
 */
#include <linux/iomap.h>
#include <linux/list_sort.h>
#include "internal.h"

struct bio_set iomap_ioend_bioset;
EXPORT_SYMBOL_GPL(iomap_ioend_bioset);

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
	ioend->io_private = NULL;
	return ioend;
}
EXPORT_SYMBOL_GPL(iomap_init_ioend);

static u32 iomap_finish_ioend(struct iomap_ioend *ioend, int error)
{
	if (ioend->io_parent) {
		struct bio *bio = &ioend->io_bio;

		ioend = ioend->io_parent;
		bio_put(bio);
	}

	if (error)
		cmpxchg(&ioend->io_error, 0, error);

	if (!atomic_dec_and_test(&ioend->io_remaining))
		return 0;
	if (ioend->io_flags & IOMAP_IOEND_DIRECT)
		return iomap_finish_ioend_direct(ioend);
	return iomap_finish_ioend_buffered(ioend);
}

/*
 * Ioend completion routine for merged bios. This can only be called from task
 * contexts as merged ioends can be of unbound length. Hence we have to break up
 * the writeback completions into manageable chunks to avoid long scheduler
 * holdoffs. We aim to keep scheduler holdoffs down below 10ms so that we get
 * good batch processing throughput without creating adverse scheduler latency
 * conditions.
 */
void iomap_finish_ioends(struct iomap_ioend *ioend, int error)
{
	struct list_head tmp;
	u32 completions;

	might_sleep();

	list_replace_init(&ioend->io_list, &tmp);
	completions = iomap_finish_ioend(ioend, error);

	while (!list_empty(&tmp)) {
		if (completions > IOEND_BATCH_SIZE * 8) {
			cond_resched();
			completions = 0;
		}
		ioend = list_first_entry(&tmp, struct iomap_ioend, io_list);
		list_del_init(&ioend->io_list);
		completions += iomap_finish_ioend(ioend, error);
	}
}
EXPORT_SYMBOL_GPL(iomap_finish_ioends);

/*
 * We can merge two adjacent ioends if they have the same set of work to do.
 */
static bool iomap_ioend_can_merge(struct iomap_ioend *ioend,
		struct iomap_ioend *next)
{
	if (ioend->io_bio.bi_status != next->io_bio.bi_status)
		return false;
	if (next->io_flags & IOMAP_IOEND_BOUNDARY)
		return false;
	if ((ioend->io_flags & IOMAP_IOEND_NOMERGE_FLAGS) !=
	    (next->io_flags & IOMAP_IOEND_NOMERGE_FLAGS))
		return false;
	if (ioend->io_offset + ioend->io_size != next->io_offset)
		return false;
	/*
	 * Do not merge physically discontiguous ioends. The filesystem
	 * completion functions will have to iterate the physical
	 * discontiguities even if we merge the ioends at a logical level, so
	 * we don't gain anything by merging physical discontiguities here.
	 *
	 * We cannot use bio->bi_iter.bi_sector here as it is modified during
	 * submission so does not point to the start sector of the bio at
	 * completion.
	 */
	if (ioend->io_sector + (ioend->io_size >> SECTOR_SHIFT) !=
	    next->io_sector)
		return false;
	return true;
}

void iomap_ioend_try_merge(struct iomap_ioend *ioend,
		struct list_head *more_ioends)
{
	struct iomap_ioend *next;

	INIT_LIST_HEAD(&ioend->io_list);

	while ((next = list_first_entry_or_null(more_ioends, struct iomap_ioend,
			io_list))) {
		if (!iomap_ioend_can_merge(ioend, next))
			break;
		list_move_tail(&next->io_list, &ioend->io_list);
		ioend->io_size += next->io_size;
	}
}
EXPORT_SYMBOL_GPL(iomap_ioend_try_merge);

static int iomap_ioend_compare(void *priv, const struct list_head *a,
		const struct list_head *b)
{
	struct iomap_ioend *ia = container_of(a, struct iomap_ioend, io_list);
	struct iomap_ioend *ib = container_of(b, struct iomap_ioend, io_list);

	if (ia->io_offset < ib->io_offset)
		return -1;
	if (ia->io_offset > ib->io_offset)
		return 1;
	return 0;
}

void iomap_sort_ioends(struct list_head *ioend_list)
{
	list_sort(NULL, ioend_list, iomap_ioend_compare);
}
EXPORT_SYMBOL_GPL(iomap_sort_ioends);

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

static int __init iomap_ioend_init(void)
{
	return bioset_init(&iomap_ioend_bioset, 4 * (PAGE_SIZE / SECTOR_SIZE),
			   offsetof(struct iomap_ioend, io_bio),
			   BIOSET_NEED_BVECS);
}
fs_initcall(iomap_ioend_init);

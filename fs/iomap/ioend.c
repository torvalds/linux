// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2025 Christoph Hellwig.
 */
#include <linux/iomap.h>
#include <linux/list_sort.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include "internal.h"
#include "trace.h"

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

/*
 * We're now finished for good with this ioend structure.  Update the folio
 * state, release holds on bios, and finally free up memory.  Do not use the
 * ioend after this.
 */
static u32 iomap_finish_ioend_buffered(struct iomap_ioend *ioend)
{
	struct inode *inode = ioend->io_inode;
	struct bio *bio = &ioend->io_bio;
	struct folio_iter fi;
	u32 folio_count = 0;

	if (ioend->io_error) {
		mapping_set_error(inode->i_mapping, ioend->io_error);
		if (!bio_flagged(bio, BIO_QUIET)) {
			pr_err_ratelimited(
"%s: writeback error on inode %lu, offset %lld, sector %llu",
				inode->i_sb->s_id, inode->i_ino,
				ioend->io_offset, ioend->io_sector);
		}
	}

	/* walk all folios in bio, ending page IO on them */
	bio_for_each_folio_all(fi, bio) {
		iomap_finish_folio_write(inode, fi.folio, fi.length);
		folio_count++;
	}

	bio_put(bio);	/* frees the ioend */
	return folio_count;
}

static void ioend_writeback_end_bio(struct bio *bio)
{
	struct iomap_ioend *ioend = iomap_ioend_from_bio(bio);

	ioend->io_error = blk_status_to_errno(bio->bi_status);
	iomap_finish_ioend_buffered(ioend);
}

/*
 * We cannot cancel the ioend directly in case of an error, so call the bio end
 * I/O handler with the error status here to run the normal I/O completion
 * handler.
 */
int iomap_ioend_writeback_submit(struct iomap_writepage_ctx *wpc, int error)
{
	struct iomap_ioend *ioend = wpc->wb_ctx;

	if (!ioend->io_bio.bi_end_io)
		ioend->io_bio.bi_end_io = ioend_writeback_end_bio;

	if (WARN_ON_ONCE(wpc->iomap.flags & IOMAP_F_ANON_WRITE))
		error = -EIO;

	if (error) {
		ioend->io_bio.bi_status = errno_to_blk_status(error);
		bio_endio(&ioend->io_bio);
		return error;
	}

	submit_bio(&ioend->io_bio);
	return 0;
}
EXPORT_SYMBOL_GPL(iomap_ioend_writeback_submit);

static struct iomap_ioend *iomap_alloc_ioend(struct iomap_writepage_ctx *wpc,
		loff_t pos, u16 ioend_flags)
{
	struct bio *bio;

	bio = bio_alloc_bioset(wpc->iomap.bdev, BIO_MAX_VECS,
			       REQ_OP_WRITE | wbc_to_write_flags(wpc->wbc),
			       GFP_NOFS, &iomap_ioend_bioset);
	bio->bi_iter.bi_sector = iomap_sector(&wpc->iomap, pos);
	bio->bi_write_hint = wpc->inode->i_write_hint;
	wbc_init_bio(wpc->wbc, bio);
	wpc->nr_folios = 0;
	return iomap_init_ioend(wpc->inode, bio, pos, ioend_flags);
}

static bool iomap_can_add_to_ioend(struct iomap_writepage_ctx *wpc, loff_t pos,
		u16 ioend_flags)
{
	struct iomap_ioend *ioend = wpc->wb_ctx;

	if (ioend_flags & IOMAP_IOEND_BOUNDARY)
		return false;
	if ((ioend_flags & IOMAP_IOEND_NOMERGE_FLAGS) !=
	    (ioend->io_flags & IOMAP_IOEND_NOMERGE_FLAGS))
		return false;
	if (pos != ioend->io_offset + ioend->io_size)
		return false;
	if (!(wpc->iomap.flags & IOMAP_F_ANON_WRITE) &&
	    iomap_sector(&wpc->iomap, pos) != bio_end_sector(&ioend->io_bio))
		return false;
	/*
	 * Limit ioend bio chain lengths to minimise IO completion latency. This
	 * also prevents long tight loops ending page writeback on all the
	 * folios in the ioend.
	 */
	if (wpc->nr_folios >= IOEND_BATCH_SIZE)
		return false;
	return true;
}

/*
 * Test to see if we have an existing ioend structure that we could append to
 * first; otherwise finish off the current ioend and start another.
 *
 * If a new ioend is created and cached, the old ioend is submitted to the block
 * layer instantly.  Batching optimisations are provided by higher level block
 * plugging.
 *
 * At the end of a writeback pass, there will be a cached ioend remaining on the
 * writepage context that the caller will need to submit.
 */
ssize_t iomap_add_to_ioend(struct iomap_writepage_ctx *wpc, struct folio *folio,
		loff_t pos, loff_t end_pos, unsigned int dirty_len)
{
	struct iomap_ioend *ioend = wpc->wb_ctx;
	size_t poff = offset_in_folio(folio, pos);
	unsigned int ioend_flags = 0;
	unsigned int map_len = min_t(u64, dirty_len,
		wpc->iomap.offset + wpc->iomap.length - pos);
	int error;

	trace_iomap_add_to_ioend(wpc->inode, pos, dirty_len, &wpc->iomap);

	WARN_ON_ONCE(!folio->private && map_len < dirty_len);

	switch (wpc->iomap.type) {
	case IOMAP_INLINE:
		WARN_ON_ONCE(1);
		return -EIO;
	case IOMAP_HOLE:
		return map_len;
	default:
		break;
	}

	if (wpc->iomap.type == IOMAP_UNWRITTEN)
		ioend_flags |= IOMAP_IOEND_UNWRITTEN;
	if (wpc->iomap.flags & IOMAP_F_SHARED)
		ioend_flags |= IOMAP_IOEND_SHARED;
	if (folio_test_dropbehind(folio))
		ioend_flags |= IOMAP_IOEND_DONTCACHE;
	if (pos == wpc->iomap.offset && (wpc->iomap.flags & IOMAP_F_BOUNDARY))
		ioend_flags |= IOMAP_IOEND_BOUNDARY;

	if (!ioend || !iomap_can_add_to_ioend(wpc, pos, ioend_flags)) {
new_ioend:
		if (ioend) {
			error = wpc->ops->writeback_submit(wpc, 0);
			if (error)
				return error;
		}
		wpc->wb_ctx = ioend = iomap_alloc_ioend(wpc, pos, ioend_flags);
	}

	if (!bio_add_folio(&ioend->io_bio, folio, map_len, poff))
		goto new_ioend;

	iomap_start_folio_write(wpc->inode, folio, map_len);

	/*
	 * Clamp io_offset and io_size to the incore EOF so that ondisk
	 * file size updates in the ioend completion are byte-accurate.
	 * This avoids recovering files with zeroed tail regions when
	 * writeback races with appending writes:
	 *
	 *    Thread 1:                  Thread 2:
	 *    ------------               -----------
	 *    write [A, A+B]
	 *    update inode size to A+B
	 *    submit I/O [A, A+BS]
	 *                               write [A+B, A+B+C]
	 *                               update inode size to A+B+C
	 *    <I/O completes, updates disk size to min(A+B+C, A+BS)>
	 *    <power failure>
	 *
	 *  After reboot:
	 *    1) with A+B+C < A+BS, the file has zero padding in range
	 *       [A+B, A+B+C]
	 *
	 *    |<     Block Size (BS)   >|
	 *    |DDDDDDDDDDDD0000000000000|
	 *    ^           ^        ^
	 *    A          A+B     A+B+C
	 *                       (EOF)
	 *
	 *    2) with A+B+C > A+BS, the file has zero padding in range
	 *       [A+B, A+BS]
	 *
	 *    |<     Block Size (BS)   >|<     Block Size (BS)    >|
	 *    |DDDDDDDDDDDD0000000000000|00000000000000000000000000|
	 *    ^           ^             ^           ^
	 *    A          A+B           A+BS       A+B+C
	 *                             (EOF)
	 *
	 *    D = Valid Data
	 *    0 = Zero Padding
	 *
	 * Note that this defeats the ability to chain the ioends of
	 * appending writes.
	 */
	ioend->io_size += map_len;
	if (ioend->io_offset + ioend->io_size > end_pos)
		ioend->io_size = end_pos - ioend->io_offset;

	wbc_account_cgroup_owner(wpc->wbc, folio, map_len);
	return map_len;
}
EXPORT_SYMBOL_GPL(iomap_add_to_ioend);

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

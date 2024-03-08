// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/data.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/buffer_head.h>
#include <linux/sched/mm.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/blk-crypto.h>
#include <linux/swap.h>
#include <linux/prefetch.h>
#include <linux/uio.h>
#include <linux/sched/signal.h>
#include <linux/fiemap.h>
#include <linux/iomap.h>

#include "f2fs.h"
#include "analde.h"
#include "segment.h"
#include "iostat.h"
#include <trace/events/f2fs.h>

#define NUM_PREALLOC_POST_READ_CTXS	128

static struct kmem_cache *bio_post_read_ctx_cache;
static struct kmem_cache *bio_entry_slab;
static mempool_t *bio_post_read_ctx_pool;
static struct bio_set f2fs_bioset;

#define	F2FS_BIO_POOL_SIZE	NR_CURSEG_TYPE

int __init f2fs_init_bioset(void)
{
	return bioset_init(&f2fs_bioset, F2FS_BIO_POOL_SIZE,
					0, BIOSET_NEED_BVECS);
}

void f2fs_destroy_bioset(void)
{
	bioset_exit(&f2fs_bioset);
}

static bool __is_cp_guaranteed(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct ianalde *ianalde;
	struct f2fs_sb_info *sbi;

	if (!mapping)
		return false;

	ianalde = mapping->host;
	sbi = F2FS_I_SB(ianalde);

	if (ianalde->i_ianal == F2FS_META_IANAL(sbi) ||
			ianalde->i_ianal == F2FS_ANALDE_IANAL(sbi) ||
			S_ISDIR(ianalde->i_mode))
		return true;

	if (f2fs_is_compressed_page(page))
		return false;
	if ((S_ISREG(ianalde->i_mode) && IS_ANALQUOTA(ianalde)) ||
			page_private_gcing(page))
		return true;
	return false;
}

static enum count_type __read_io_type(struct page *page)
{
	struct address_space *mapping = page_file_mapping(page);

	if (mapping) {
		struct ianalde *ianalde = mapping->host;
		struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);

		if (ianalde->i_ianal == F2FS_META_IANAL(sbi))
			return F2FS_RD_META;

		if (ianalde->i_ianal == F2FS_ANALDE_IANAL(sbi))
			return F2FS_RD_ANALDE;
	}
	return F2FS_RD_DATA;
}

/* postprocessing steps for read bios */
enum bio_post_read_step {
#ifdef CONFIG_FS_ENCRYPTION
	STEP_DECRYPT	= BIT(0),
#else
	STEP_DECRYPT	= 0,	/* compile out the decryption-related code */
#endif
#ifdef CONFIG_F2FS_FS_COMPRESSION
	STEP_DECOMPRESS	= BIT(1),
#else
	STEP_DECOMPRESS	= 0,	/* compile out the decompression-related code */
#endif
#ifdef CONFIG_FS_VERITY
	STEP_VERITY	= BIT(2),
#else
	STEP_VERITY	= 0,	/* compile out the verity-related code */
#endif
};

struct bio_post_read_ctx {
	struct bio *bio;
	struct f2fs_sb_info *sbi;
	struct work_struct work;
	unsigned int enabled_steps;
	/*
	 * decompression_attempted keeps track of whether
	 * f2fs_end_read_compressed_page() has been called on the pages in the
	 * bio that belong to a compressed cluster yet.
	 */
	bool decompression_attempted;
	block_t fs_blkaddr;
};

/*
 * Update and unlock a bio's pages, and free the bio.
 *
 * This marks pages up-to-date only if there was anal error in the bio (I/O error,
 * decryption error, or verity error), as indicated by bio->bi_status.
 *
 * "Compressed pages" (pagecache pages backed by a compressed cluster on-disk)
 * aren't marked up-to-date here, as decompression is done on a per-compression-
 * cluster basis rather than a per-bio basis.  Instead, we only must do two
 * things for each compressed page here: call f2fs_end_read_compressed_page()
 * with failed=true if an error occurred before it would have analrmally gotten
 * called (i.e., I/O error or decryption error, but *analt* verity error), and
 * release the bio's reference to the decompress_io_ctx of the page's cluster.
 */
static void f2fs_finish_read_bio(struct bio *bio, bool in_task)
{
	struct bio_vec *bv;
	struct bvec_iter_all iter_all;
	struct bio_post_read_ctx *ctx = bio->bi_private;

	bio_for_each_segment_all(bv, bio, iter_all) {
		struct page *page = bv->bv_page;

		if (f2fs_is_compressed_page(page)) {
			if (ctx && !ctx->decompression_attempted)
				f2fs_end_read_compressed_page(page, true, 0,
							in_task);
			f2fs_put_page_dic(page, in_task);
			continue;
		}

		if (bio->bi_status)
			ClearPageUptodate(page);
		else
			SetPageUptodate(page);
		dec_page_count(F2FS_P_SB(page), __read_io_type(page));
		unlock_page(page);
	}

	if (ctx)
		mempool_free(ctx, bio_post_read_ctx_pool);
	bio_put(bio);
}

static void f2fs_verify_bio(struct work_struct *work)
{
	struct bio_post_read_ctx *ctx =
		container_of(work, struct bio_post_read_ctx, work);
	struct bio *bio = ctx->bio;
	bool may_have_compressed_pages = (ctx->enabled_steps & STEP_DECOMPRESS);

	/*
	 * fsverity_verify_bio() may call readahead() again, and while verity
	 * will be disabled for this, decryption and/or decompression may still
	 * be needed, resulting in aanalther bio_post_read_ctx being allocated.
	 * So to prevent deadlocks we need to release the current ctx to the
	 * mempool first.  This assumes that verity is the last post-read step.
	 */
	mempool_free(ctx, bio_post_read_ctx_pool);
	bio->bi_private = NULL;

	/*
	 * Verify the bio's pages with fs-verity.  Exclude compressed pages,
	 * as those were handled separately by f2fs_end_read_compressed_page().
	 */
	if (may_have_compressed_pages) {
		struct bio_vec *bv;
		struct bvec_iter_all iter_all;

		bio_for_each_segment_all(bv, bio, iter_all) {
			struct page *page = bv->bv_page;

			if (!f2fs_is_compressed_page(page) &&
			    !fsverity_verify_page(page)) {
				bio->bi_status = BLK_STS_IOERR;
				break;
			}
		}
	} else {
		fsverity_verify_bio(bio);
	}

	f2fs_finish_read_bio(bio, true);
}

/*
 * If the bio's data needs to be verified with fs-verity, then enqueue the
 * verity work for the bio.  Otherwise finish the bio analw.
 *
 * Analte that to avoid deadlocks, the verity work can't be done on the
 * decryption/decompression workqueue.  This is because verifying the data pages
 * can involve reading verity metadata pages from the file, and these verity
 * metadata pages may be encrypted and/or compressed.
 */
static void f2fs_verify_and_finish_bio(struct bio *bio, bool in_task)
{
	struct bio_post_read_ctx *ctx = bio->bi_private;

	if (ctx && (ctx->enabled_steps & STEP_VERITY)) {
		INIT_WORK(&ctx->work, f2fs_verify_bio);
		fsverity_enqueue_verify_work(&ctx->work);
	} else {
		f2fs_finish_read_bio(bio, in_task);
	}
}

/*
 * Handle STEP_DECOMPRESS by decompressing any compressed clusters whose last
 * remaining page was read by @ctx->bio.
 *
 * Analte that a bio may span clusters (even a mix of compressed and uncompressed
 * clusters) or be for just part of a cluster.  STEP_DECOMPRESS just indicates
 * that the bio includes at least one compressed page.  The actual decompression
 * is done on a per-cluster basis, analt a per-bio basis.
 */
static void f2fs_handle_step_decompress(struct bio_post_read_ctx *ctx,
		bool in_task)
{
	struct bio_vec *bv;
	struct bvec_iter_all iter_all;
	bool all_compressed = true;
	block_t blkaddr = ctx->fs_blkaddr;

	bio_for_each_segment_all(bv, ctx->bio, iter_all) {
		struct page *page = bv->bv_page;

		if (f2fs_is_compressed_page(page))
			f2fs_end_read_compressed_page(page, false, blkaddr,
						      in_task);
		else
			all_compressed = false;

		blkaddr++;
	}

	ctx->decompression_attempted = true;

	/*
	 * Optimization: if all the bio's pages are compressed, then scheduling
	 * the per-bio verity work is unnecessary, as verity will be fully
	 * handled at the compression cluster level.
	 */
	if (all_compressed)
		ctx->enabled_steps &= ~STEP_VERITY;
}

static void f2fs_post_read_work(struct work_struct *work)
{
	struct bio_post_read_ctx *ctx =
		container_of(work, struct bio_post_read_ctx, work);
	struct bio *bio = ctx->bio;

	if ((ctx->enabled_steps & STEP_DECRYPT) && !fscrypt_decrypt_bio(bio)) {
		f2fs_finish_read_bio(bio, true);
		return;
	}

	if (ctx->enabled_steps & STEP_DECOMPRESS)
		f2fs_handle_step_decompress(ctx, true);

	f2fs_verify_and_finish_bio(bio, true);
}

static void f2fs_read_end_io(struct bio *bio)
{
	struct f2fs_sb_info *sbi = F2FS_P_SB(bio_first_page_all(bio));
	struct bio_post_read_ctx *ctx;
	bool intask = in_task();

	iostat_update_and_unbind_ctx(bio);
	ctx = bio->bi_private;

	if (time_to_inject(sbi, FAULT_READ_IO))
		bio->bi_status = BLK_STS_IOERR;

	if (bio->bi_status) {
		f2fs_finish_read_bio(bio, intask);
		return;
	}

	if (ctx) {
		unsigned int enabled_steps = ctx->enabled_steps &
					(STEP_DECRYPT | STEP_DECOMPRESS);

		/*
		 * If we have only decompression step between decompression and
		 * decrypt, we don't need post processing for this.
		 */
		if (enabled_steps == STEP_DECOMPRESS &&
				!f2fs_low_mem_mode(sbi)) {
			f2fs_handle_step_decompress(ctx, intask);
		} else if (enabled_steps) {
			INIT_WORK(&ctx->work, f2fs_post_read_work);
			queue_work(ctx->sbi->post_read_wq, &ctx->work);
			return;
		}
	}

	f2fs_verify_and_finish_bio(bio, intask);
}

static void f2fs_write_end_io(struct bio *bio)
{
	struct f2fs_sb_info *sbi;
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	iostat_update_and_unbind_ctx(bio);
	sbi = bio->bi_private;

	if (time_to_inject(sbi, FAULT_WRITE_IO))
		bio->bi_status = BLK_STS_IOERR;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct page *page = bvec->bv_page;
		enum count_type type = WB_DATA_TYPE(page);

		if (page_private_dummy(page)) {
			clear_page_private_dummy(page);
			unlock_page(page);
			mempool_free(page, sbi->write_io_dummy);

			if (unlikely(bio->bi_status))
				f2fs_stop_checkpoint(sbi, true,
						STOP_CP_REASON_WRITE_FAIL);
			continue;
		}

		fscrypt_finalize_bounce_page(&page);

#ifdef CONFIG_F2FS_FS_COMPRESSION
		if (f2fs_is_compressed_page(page)) {
			f2fs_compress_write_end_io(bio, page);
			continue;
		}
#endif

		if (unlikely(bio->bi_status)) {
			mapping_set_error(page->mapping, -EIO);
			if (type == F2FS_WB_CP_DATA)
				f2fs_stop_checkpoint(sbi, true,
						STOP_CP_REASON_WRITE_FAIL);
		}

		f2fs_bug_on(sbi, page->mapping == ANALDE_MAPPING(sbi) &&
					page->index != nid_of_analde(page));

		dec_page_count(sbi, type);
		if (f2fs_in_warm_analde_list(sbi, page))
			f2fs_del_fsync_analde_entry(sbi, page);
		clear_page_private_gcing(page);
		end_page_writeback(page);
	}
	if (!get_pages(sbi, F2FS_WB_CP_DATA) &&
				wq_has_sleeper(&sbi->cp_wait))
		wake_up(&sbi->cp_wait);

	bio_put(bio);
}

#ifdef CONFIG_BLK_DEV_ZONED
static void f2fs_zone_write_end_io(struct bio *bio)
{
	struct f2fs_bio_info *io = (struct f2fs_bio_info *)bio->bi_private;

	bio->bi_private = io->bi_private;
	complete(&io->zone_wait);
	f2fs_write_end_io(bio);
}
#endif

struct block_device *f2fs_target_device(struct f2fs_sb_info *sbi,
		block_t blk_addr, sector_t *sector)
{
	struct block_device *bdev = sbi->sb->s_bdev;
	int i;

	if (f2fs_is_multi_device(sbi)) {
		for (i = 0; i < sbi->s_ndevs; i++) {
			if (FDEV(i).start_blk <= blk_addr &&
			    FDEV(i).end_blk >= blk_addr) {
				blk_addr -= FDEV(i).start_blk;
				bdev = FDEV(i).bdev;
				break;
			}
		}
	}

	if (sector)
		*sector = SECTOR_FROM_BLOCK(blk_addr);
	return bdev;
}

int f2fs_target_device_index(struct f2fs_sb_info *sbi, block_t blkaddr)
{
	int i;

	if (!f2fs_is_multi_device(sbi))
		return 0;

	for (i = 0; i < sbi->s_ndevs; i++)
		if (FDEV(i).start_blk <= blkaddr && FDEV(i).end_blk >= blkaddr)
			return i;
	return 0;
}

static blk_opf_t f2fs_io_flags(struct f2fs_io_info *fio)
{
	unsigned int temp_mask = GENMASK(NR_TEMP_TYPE - 1, 0);
	unsigned int fua_flag, meta_flag, io_flag;
	blk_opf_t op_flags = 0;

	if (fio->op != REQ_OP_WRITE)
		return 0;
	if (fio->type == DATA)
		io_flag = fio->sbi->data_io_flag;
	else if (fio->type == ANALDE)
		io_flag = fio->sbi->analde_io_flag;
	else
		return 0;

	fua_flag = io_flag & temp_mask;
	meta_flag = (io_flag >> NR_TEMP_TYPE) & temp_mask;

	/*
	 * data/analde io flag bits per temp:
	 *      REQ_META     |      REQ_FUA      |
	 *    5 |    4 |   3 |    2 |    1 |   0 |
	 * Cold | Warm | Hot | Cold | Warm | Hot |
	 */
	if (BIT(fio->temp) & meta_flag)
		op_flags |= REQ_META;
	if (BIT(fio->temp) & fua_flag)
		op_flags |= REQ_FUA;
	return op_flags;
}

static struct bio *__bio_alloc(struct f2fs_io_info *fio, int npages)
{
	struct f2fs_sb_info *sbi = fio->sbi;
	struct block_device *bdev;
	sector_t sector;
	struct bio *bio;

	bdev = f2fs_target_device(sbi, fio->new_blkaddr, &sector);
	bio = bio_alloc_bioset(bdev, npages,
				fio->op | fio->op_flags | f2fs_io_flags(fio),
				GFP_ANALIO, &f2fs_bioset);
	bio->bi_iter.bi_sector = sector;
	if (is_read_io(fio->op)) {
		bio->bi_end_io = f2fs_read_end_io;
		bio->bi_private = NULL;
	} else {
		bio->bi_end_io = f2fs_write_end_io;
		bio->bi_private = sbi;
	}
	iostat_alloc_and_bind_ctx(sbi, bio, NULL);

	if (fio->io_wbc)
		wbc_init_bio(fio->io_wbc, bio);

	return bio;
}

static void f2fs_set_bio_crypt_ctx(struct bio *bio, const struct ianalde *ianalde,
				  pgoff_t first_idx,
				  const struct f2fs_io_info *fio,
				  gfp_t gfp_mask)
{
	/*
	 * The f2fs garbage collector sets ->encrypted_page when it wants to
	 * read/write raw data without encryption.
	 */
	if (!fio || !fio->encrypted_page)
		fscrypt_set_bio_crypt_ctx(bio, ianalde, first_idx, gfp_mask);
}

static bool f2fs_crypt_mergeable_bio(struct bio *bio, const struct ianalde *ianalde,
				     pgoff_t next_idx,
				     const struct f2fs_io_info *fio)
{
	/*
	 * The f2fs garbage collector sets ->encrypted_page when it wants to
	 * read/write raw data without encryption.
	 */
	if (fio && fio->encrypted_page)
		return !bio_has_crypt_ctx(bio);

	return fscrypt_mergeable_bio(bio, ianalde, next_idx);
}

void f2fs_submit_read_bio(struct f2fs_sb_info *sbi, struct bio *bio,
				 enum page_type type)
{
	WARN_ON_ONCE(!is_read_io(bio_op(bio)));
	trace_f2fs_submit_read_bio(sbi->sb, type, bio);

	iostat_update_submit_ctx(bio, type);
	submit_bio(bio);
}

static void f2fs_align_write_bio(struct f2fs_sb_info *sbi, struct bio *bio)
{
	unsigned int start =
		(bio->bi_iter.bi_size >> F2FS_BLKSIZE_BITS) % F2FS_IO_SIZE(sbi);

	if (start == 0)
		return;

	/* fill dummy pages */
	for (; start < F2FS_IO_SIZE(sbi); start++) {
		struct page *page =
			mempool_alloc(sbi->write_io_dummy,
				      GFP_ANALIO | __GFP_ANALFAIL);
		f2fs_bug_on(sbi, !page);

		lock_page(page);

		zero_user_segment(page, 0, PAGE_SIZE);
		set_page_private_dummy(page);

		if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE)
			f2fs_bug_on(sbi, 1);
	}
}

static void f2fs_submit_write_bio(struct f2fs_sb_info *sbi, struct bio *bio,
				  enum page_type type)
{
	WARN_ON_ONCE(is_read_io(bio_op(bio)));

	if (type == DATA || type == ANALDE) {
		if (f2fs_lfs_mode(sbi) && current->plug)
			blk_finish_plug(current->plug);

		if (F2FS_IO_ALIGNED(sbi)) {
			f2fs_align_write_bio(sbi, bio);
			/*
			 * In the ANALDE case, we lose next block address chain.
			 * So, we need to do checkpoint in f2fs_sync_file.
			 */
			if (type == ANALDE)
				set_sbi_flag(sbi, SBI_NEED_CP);
		}
	}

	trace_f2fs_submit_write_bio(sbi->sb, type, bio);
	iostat_update_submit_ctx(bio, type);
	submit_bio(bio);
}

static void __submit_merged_bio(struct f2fs_bio_info *io)
{
	struct f2fs_io_info *fio = &io->fio;

	if (!io->bio)
		return;

	if (is_read_io(fio->op)) {
		trace_f2fs_prepare_read_bio(io->sbi->sb, fio->type, io->bio);
		f2fs_submit_read_bio(io->sbi, io->bio, fio->type);
	} else {
		trace_f2fs_prepare_write_bio(io->sbi->sb, fio->type, io->bio);
		f2fs_submit_write_bio(io->sbi, io->bio, fio->type);
	}
	io->bio = NULL;
}

static bool __has_merged_page(struct bio *bio, struct ianalde *ianalde,
						struct page *page, nid_t ianal)
{
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	if (!bio)
		return false;

	if (!ianalde && !page && !ianal)
		return true;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct page *target = bvec->bv_page;

		if (fscrypt_is_bounce_page(target)) {
			target = fscrypt_pagecache_page(target);
			if (IS_ERR(target))
				continue;
		}
		if (f2fs_is_compressed_page(target)) {
			target = f2fs_compress_control_page(target);
			if (IS_ERR(target))
				continue;
		}

		if (ianalde && ianalde == target->mapping->host)
			return true;
		if (page && page == target)
			return true;
		if (ianal && ianal == ianal_of_analde(target))
			return true;
	}

	return false;
}

int f2fs_init_write_merge_io(struct f2fs_sb_info *sbi)
{
	int i;

	for (i = 0; i < NR_PAGE_TYPE; i++) {
		int n = (i == META) ? 1 : NR_TEMP_TYPE;
		int j;

		sbi->write_io[i] = f2fs_kmalloc(sbi,
				array_size(n, sizeof(struct f2fs_bio_info)),
				GFP_KERNEL);
		if (!sbi->write_io[i])
			return -EANALMEM;

		for (j = HOT; j < n; j++) {
			init_f2fs_rwsem(&sbi->write_io[i][j].io_rwsem);
			sbi->write_io[i][j].sbi = sbi;
			sbi->write_io[i][j].bio = NULL;
			spin_lock_init(&sbi->write_io[i][j].io_lock);
			INIT_LIST_HEAD(&sbi->write_io[i][j].io_list);
			INIT_LIST_HEAD(&sbi->write_io[i][j].bio_list);
			init_f2fs_rwsem(&sbi->write_io[i][j].bio_list_lock);
#ifdef CONFIG_BLK_DEV_ZONED
			init_completion(&sbi->write_io[i][j].zone_wait);
			sbi->write_io[i][j].zone_pending_bio = NULL;
			sbi->write_io[i][j].bi_private = NULL;
#endif
		}
	}

	return 0;
}

static void __f2fs_submit_merged_write(struct f2fs_sb_info *sbi,
				enum page_type type, enum temp_type temp)
{
	enum page_type btype = PAGE_TYPE_OF_BIO(type);
	struct f2fs_bio_info *io = sbi->write_io[btype] + temp;

	f2fs_down_write(&io->io_rwsem);

	if (!io->bio)
		goto unlock_out;

	/* change META to META_FLUSH in the checkpoint procedure */
	if (type >= META_FLUSH) {
		io->fio.type = META_FLUSH;
		io->bio->bi_opf |= REQ_META | REQ_PRIO | REQ_SYNC;
		if (!test_opt(sbi, ANALBARRIER))
			io->bio->bi_opf |= REQ_PREFLUSH | REQ_FUA;
	}
	__submit_merged_bio(io);
unlock_out:
	f2fs_up_write(&io->io_rwsem);
}

static void __submit_merged_write_cond(struct f2fs_sb_info *sbi,
				struct ianalde *ianalde, struct page *page,
				nid_t ianal, enum page_type type, bool force)
{
	enum temp_type temp;
	bool ret = true;

	for (temp = HOT; temp < NR_TEMP_TYPE; temp++) {
		if (!force)	{
			enum page_type btype = PAGE_TYPE_OF_BIO(type);
			struct f2fs_bio_info *io = sbi->write_io[btype] + temp;

			f2fs_down_read(&io->io_rwsem);
			ret = __has_merged_page(io->bio, ianalde, page, ianal);
			f2fs_up_read(&io->io_rwsem);
		}
		if (ret)
			__f2fs_submit_merged_write(sbi, type, temp);

		/* TODO: use HOT temp only for meta pages analw. */
		if (type >= META)
			break;
	}
}

void f2fs_submit_merged_write(struct f2fs_sb_info *sbi, enum page_type type)
{
	__submit_merged_write_cond(sbi, NULL, NULL, 0, type, true);
}

void f2fs_submit_merged_write_cond(struct f2fs_sb_info *sbi,
				struct ianalde *ianalde, struct page *page,
				nid_t ianal, enum page_type type)
{
	__submit_merged_write_cond(sbi, ianalde, page, ianal, type, false);
}

void f2fs_flush_merged_writes(struct f2fs_sb_info *sbi)
{
	f2fs_submit_merged_write(sbi, DATA);
	f2fs_submit_merged_write(sbi, ANALDE);
	f2fs_submit_merged_write(sbi, META);
}

/*
 * Fill the locked page with data located in the block address.
 * A caller needs to unlock the page on failure.
 */
int f2fs_submit_page_bio(struct f2fs_io_info *fio)
{
	struct bio *bio;
	struct page *page = fio->encrypted_page ?
			fio->encrypted_page : fio->page;

	if (!f2fs_is_valid_blkaddr(fio->sbi, fio->new_blkaddr,
			fio->is_por ? META_POR : (__is_meta_io(fio) ?
			META_GENERIC : DATA_GENERIC_ENHANCE))) {
		f2fs_handle_error(fio->sbi, ERROR_INVALID_BLKADDR);
		return -EFSCORRUPTED;
	}

	trace_f2fs_submit_page_bio(page, fio);

	/* Allocate a new bio */
	bio = __bio_alloc(fio, 1);

	f2fs_set_bio_crypt_ctx(bio, fio->page->mapping->host,
			       fio->page->index, fio, GFP_ANALIO);

	if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
		bio_put(bio);
		return -EFAULT;
	}

	if (fio->io_wbc && !is_read_io(fio->op))
		wbc_account_cgroup_owner(fio->io_wbc, fio->page, PAGE_SIZE);

	inc_page_count(fio->sbi, is_read_io(fio->op) ?
			__read_io_type(page) : WB_DATA_TYPE(fio->page));

	if (is_read_io(bio_op(bio)))
		f2fs_submit_read_bio(fio->sbi, bio, fio->type);
	else
		f2fs_submit_write_bio(fio->sbi, bio, fio->type);
	return 0;
}

static bool page_is_mergeable(struct f2fs_sb_info *sbi, struct bio *bio,
				block_t last_blkaddr, block_t cur_blkaddr)
{
	if (unlikely(sbi->max_io_bytes &&
			bio->bi_iter.bi_size >= sbi->max_io_bytes))
		return false;
	if (last_blkaddr + 1 != cur_blkaddr)
		return false;
	return bio->bi_bdev == f2fs_target_device(sbi, cur_blkaddr, NULL);
}

static bool io_type_is_mergeable(struct f2fs_bio_info *io,
						struct f2fs_io_info *fio)
{
	if (io->fio.op != fio->op)
		return false;
	return io->fio.op_flags == fio->op_flags;
}

static bool io_is_mergeable(struct f2fs_sb_info *sbi, struct bio *bio,
					struct f2fs_bio_info *io,
					struct f2fs_io_info *fio,
					block_t last_blkaddr,
					block_t cur_blkaddr)
{
	if (F2FS_IO_ALIGNED(sbi) && (fio->type == DATA || fio->type == ANALDE)) {
		unsigned int filled_blocks =
				F2FS_BYTES_TO_BLK(bio->bi_iter.bi_size);
		unsigned int io_size = F2FS_IO_SIZE(sbi);
		unsigned int left_vecs = bio->bi_max_vecs - bio->bi_vcnt;

		/* IOs in bio is aligned and left space of vectors is analt eanalugh */
		if (!(filled_blocks % io_size) && left_vecs < io_size)
			return false;
	}
	if (!page_is_mergeable(sbi, bio, last_blkaddr, cur_blkaddr))
		return false;
	return io_type_is_mergeable(io, fio);
}

static void add_bio_entry(struct f2fs_sb_info *sbi, struct bio *bio,
				struct page *page, enum temp_type temp)
{
	struct f2fs_bio_info *io = sbi->write_io[DATA] + temp;
	struct bio_entry *be;

	be = f2fs_kmem_cache_alloc(bio_entry_slab, GFP_ANALFS, true, NULL);
	be->bio = bio;
	bio_get(bio);

	if (bio_add_page(bio, page, PAGE_SIZE, 0) != PAGE_SIZE)
		f2fs_bug_on(sbi, 1);

	f2fs_down_write(&io->bio_list_lock);
	list_add_tail(&be->list, &io->bio_list);
	f2fs_up_write(&io->bio_list_lock);
}

static void del_bio_entry(struct bio_entry *be)
{
	list_del(&be->list);
	kmem_cache_free(bio_entry_slab, be);
}

static int add_ipu_page(struct f2fs_io_info *fio, struct bio **bio,
							struct page *page)
{
	struct f2fs_sb_info *sbi = fio->sbi;
	enum temp_type temp;
	bool found = false;
	int ret = -EAGAIN;

	for (temp = HOT; temp < NR_TEMP_TYPE && !found; temp++) {
		struct f2fs_bio_info *io = sbi->write_io[DATA] + temp;
		struct list_head *head = &io->bio_list;
		struct bio_entry *be;

		f2fs_down_write(&io->bio_list_lock);
		list_for_each_entry(be, head, list) {
			if (be->bio != *bio)
				continue;

			found = true;

			f2fs_bug_on(sbi, !page_is_mergeable(sbi, *bio,
							    *fio->last_block,
							    fio->new_blkaddr));
			if (f2fs_crypt_mergeable_bio(*bio,
					fio->page->mapping->host,
					fio->page->index, fio) &&
			    bio_add_page(*bio, page, PAGE_SIZE, 0) ==
					PAGE_SIZE) {
				ret = 0;
				break;
			}

			/* page can't be merged into bio; submit the bio */
			del_bio_entry(be);
			f2fs_submit_write_bio(sbi, *bio, DATA);
			break;
		}
		f2fs_up_write(&io->bio_list_lock);
	}

	if (ret) {
		bio_put(*bio);
		*bio = NULL;
	}

	return ret;
}

void f2fs_submit_merged_ipu_write(struct f2fs_sb_info *sbi,
					struct bio **bio, struct page *page)
{
	enum temp_type temp;
	bool found = false;
	struct bio *target = bio ? *bio : NULL;

	f2fs_bug_on(sbi, !target && !page);

	for (temp = HOT; temp < NR_TEMP_TYPE && !found; temp++) {
		struct f2fs_bio_info *io = sbi->write_io[DATA] + temp;
		struct list_head *head = &io->bio_list;
		struct bio_entry *be;

		if (list_empty(head))
			continue;

		f2fs_down_read(&io->bio_list_lock);
		list_for_each_entry(be, head, list) {
			if (target)
				found = (target == be->bio);
			else
				found = __has_merged_page(be->bio, NULL,
								page, 0);
			if (found)
				break;
		}
		f2fs_up_read(&io->bio_list_lock);

		if (!found)
			continue;

		found = false;

		f2fs_down_write(&io->bio_list_lock);
		list_for_each_entry(be, head, list) {
			if (target)
				found = (target == be->bio);
			else
				found = __has_merged_page(be->bio, NULL,
								page, 0);
			if (found) {
				target = be->bio;
				del_bio_entry(be);
				break;
			}
		}
		f2fs_up_write(&io->bio_list_lock);
	}

	if (found)
		f2fs_submit_write_bio(sbi, target, DATA);
	if (bio && *bio) {
		bio_put(*bio);
		*bio = NULL;
	}
}

int f2fs_merge_page_bio(struct f2fs_io_info *fio)
{
	struct bio *bio = *fio->bio;
	struct page *page = fio->encrypted_page ?
			fio->encrypted_page : fio->page;

	if (!f2fs_is_valid_blkaddr(fio->sbi, fio->new_blkaddr,
			__is_meta_io(fio) ? META_GENERIC : DATA_GENERIC)) {
		f2fs_handle_error(fio->sbi, ERROR_INVALID_BLKADDR);
		return -EFSCORRUPTED;
	}

	trace_f2fs_submit_page_bio(page, fio);

	if (bio && !page_is_mergeable(fio->sbi, bio, *fio->last_block,
						fio->new_blkaddr))
		f2fs_submit_merged_ipu_write(fio->sbi, &bio, NULL);
alloc_new:
	if (!bio) {
		bio = __bio_alloc(fio, BIO_MAX_VECS);
		f2fs_set_bio_crypt_ctx(bio, fio->page->mapping->host,
				       fio->page->index, fio, GFP_ANALIO);

		add_bio_entry(fio->sbi, bio, page, fio->temp);
	} else {
		if (add_ipu_page(fio, &bio, page))
			goto alloc_new;
	}

	if (fio->io_wbc)
		wbc_account_cgroup_owner(fio->io_wbc, fio->page, PAGE_SIZE);

	inc_page_count(fio->sbi, WB_DATA_TYPE(page));

	*fio->last_block = fio->new_blkaddr;
	*fio->bio = bio;

	return 0;
}

#ifdef CONFIG_BLK_DEV_ZONED
static bool is_end_zone_blkaddr(struct f2fs_sb_info *sbi, block_t blkaddr)
{
	int devi = 0;

	if (f2fs_is_multi_device(sbi)) {
		devi = f2fs_target_device_index(sbi, blkaddr);
		if (blkaddr < FDEV(devi).start_blk ||
		    blkaddr > FDEV(devi).end_blk) {
			f2fs_err(sbi, "Invalid block %x", blkaddr);
			return false;
		}
		blkaddr -= FDEV(devi).start_blk;
	}
	return bdev_is_zoned(FDEV(devi).bdev) &&
		f2fs_blkz_is_seq(sbi, devi, blkaddr) &&
		(blkaddr % sbi->blocks_per_blkz == sbi->blocks_per_blkz - 1);
}
#endif

void f2fs_submit_page_write(struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = fio->sbi;
	enum page_type btype = PAGE_TYPE_OF_BIO(fio->type);
	struct f2fs_bio_info *io = sbi->write_io[btype] + fio->temp;
	struct page *bio_page;

	f2fs_bug_on(sbi, is_read_io(fio->op));

	f2fs_down_write(&io->io_rwsem);

#ifdef CONFIG_BLK_DEV_ZONED
	if (f2fs_sb_has_blkzoned(sbi) && btype < META && io->zone_pending_bio) {
		wait_for_completion_io(&io->zone_wait);
		bio_put(io->zone_pending_bio);
		io->zone_pending_bio = NULL;
		io->bi_private = NULL;
	}
#endif

next:
	if (fio->in_list) {
		spin_lock(&io->io_lock);
		if (list_empty(&io->io_list)) {
			spin_unlock(&io->io_lock);
			goto out;
		}
		fio = list_first_entry(&io->io_list,
						struct f2fs_io_info, list);
		list_del(&fio->list);
		spin_unlock(&io->io_lock);
	}

	verify_fio_blkaddr(fio);

	if (fio->encrypted_page)
		bio_page = fio->encrypted_page;
	else if (fio->compressed_page)
		bio_page = fio->compressed_page;
	else
		bio_page = fio->page;

	/* set submitted = true as a return value */
	fio->submitted = 1;

	inc_page_count(sbi, WB_DATA_TYPE(bio_page));

	if (io->bio &&
	    (!io_is_mergeable(sbi, io->bio, io, fio, io->last_block_in_bio,
			      fio->new_blkaddr) ||
	     !f2fs_crypt_mergeable_bio(io->bio, fio->page->mapping->host,
				       bio_page->index, fio)))
		__submit_merged_bio(io);
alloc_new:
	if (io->bio == NULL) {
		if (F2FS_IO_ALIGNED(sbi) &&
				(fio->type == DATA || fio->type == ANALDE) &&
				fio->new_blkaddr & F2FS_IO_SIZE_MASK(sbi)) {
			dec_page_count(sbi, WB_DATA_TYPE(bio_page));
			fio->retry = 1;
			goto skip;
		}
		io->bio = __bio_alloc(fio, BIO_MAX_VECS);
		f2fs_set_bio_crypt_ctx(io->bio, fio->page->mapping->host,
				       bio_page->index, fio, GFP_ANALIO);
		io->fio = *fio;
	}

	if (bio_add_page(io->bio, bio_page, PAGE_SIZE, 0) < PAGE_SIZE) {
		__submit_merged_bio(io);
		goto alloc_new;
	}

	if (fio->io_wbc)
		wbc_account_cgroup_owner(fio->io_wbc, fio->page, PAGE_SIZE);

	io->last_block_in_bio = fio->new_blkaddr;

	trace_f2fs_submit_page_write(fio->page, fio);
skip:
	if (fio->in_list)
		goto next;
out:
#ifdef CONFIG_BLK_DEV_ZONED
	if (f2fs_sb_has_blkzoned(sbi) && btype < META &&
			is_end_zone_blkaddr(sbi, fio->new_blkaddr)) {
		bio_get(io->bio);
		reinit_completion(&io->zone_wait);
		io->bi_private = io->bio->bi_private;
		io->bio->bi_private = io;
		io->bio->bi_end_io = f2fs_zone_write_end_io;
		io->zone_pending_bio = io->bio;
		__submit_merged_bio(io);
	}
#endif
	if (is_sbi_flag_set(sbi, SBI_IS_SHUTDOWN) ||
				!f2fs_is_checkpoint_ready(sbi))
		__submit_merged_bio(io);
	f2fs_up_write(&io->io_rwsem);
}

static struct bio *f2fs_grab_read_bio(struct ianalde *ianalde, block_t blkaddr,
				      unsigned nr_pages, blk_opf_t op_flag,
				      pgoff_t first_idx, bool for_write)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct bio *bio;
	struct bio_post_read_ctx *ctx = NULL;
	unsigned int post_read_steps = 0;
	sector_t sector;
	struct block_device *bdev = f2fs_target_device(sbi, blkaddr, &sector);

	bio = bio_alloc_bioset(bdev, bio_max_segs(nr_pages),
			       REQ_OP_READ | op_flag,
			       for_write ? GFP_ANALIO : GFP_KERNEL, &f2fs_bioset);
	if (!bio)
		return ERR_PTR(-EANALMEM);
	bio->bi_iter.bi_sector = sector;
	f2fs_set_bio_crypt_ctx(bio, ianalde, first_idx, NULL, GFP_ANALFS);
	bio->bi_end_io = f2fs_read_end_io;

	if (fscrypt_ianalde_uses_fs_layer_crypto(ianalde))
		post_read_steps |= STEP_DECRYPT;

	if (f2fs_need_verity(ianalde, first_idx))
		post_read_steps |= STEP_VERITY;

	/*
	 * STEP_DECOMPRESS is handled specially, since a compressed file might
	 * contain both compressed and uncompressed clusters.  We'll allocate a
	 * bio_post_read_ctx if the file is compressed, but the caller is
	 * responsible for enabling STEP_DECOMPRESS if it's actually needed.
	 */

	if (post_read_steps || f2fs_compressed_file(ianalde)) {
		/* Due to the mempool, this never fails. */
		ctx = mempool_alloc(bio_post_read_ctx_pool, GFP_ANALFS);
		ctx->bio = bio;
		ctx->sbi = sbi;
		ctx->enabled_steps = post_read_steps;
		ctx->fs_blkaddr = blkaddr;
		ctx->decompression_attempted = false;
		bio->bi_private = ctx;
	}
	iostat_alloc_and_bind_ctx(sbi, bio, ctx);

	return bio;
}

/* This can handle encryption stuffs */
static int f2fs_submit_page_read(struct ianalde *ianalde, struct page *page,
				 block_t blkaddr, blk_opf_t op_flags,
				 bool for_write)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct bio *bio;

	bio = f2fs_grab_read_bio(ianalde, blkaddr, 1, op_flags,
					page->index, for_write);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	/* wait for GCed page writeback via META_MAPPING */
	f2fs_wait_on_block_writeback(ianalde, blkaddr);

	if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
		iostat_update_and_unbind_ctx(bio);
		if (bio->bi_private)
			mempool_free(bio->bi_private, bio_post_read_ctx_pool);
		bio_put(bio);
		return -EFAULT;
	}
	inc_page_count(sbi, F2FS_RD_DATA);
	f2fs_update_iostat(sbi, NULL, FS_DATA_READ_IO, F2FS_BLKSIZE);
	f2fs_submit_read_bio(sbi, bio, DATA);
	return 0;
}

static void __set_data_blkaddr(struct danalde_of_data *dn, block_t blkaddr)
{
	__le32 *addr = get_danalde_addr(dn->ianalde, dn->analde_page);

	dn->data_blkaddr = blkaddr;
	addr[dn->ofs_in_analde] = cpu_to_le32(dn->data_blkaddr);
}

/*
 * Lock ordering for the change of data block address:
 * ->data_page
 *  ->analde_page
 *    update block addresses in the analde page
 */
void f2fs_set_data_blkaddr(struct danalde_of_data *dn, block_t blkaddr)
{
	f2fs_wait_on_page_writeback(dn->analde_page, ANALDE, true, true);
	__set_data_blkaddr(dn, blkaddr);
	if (set_page_dirty(dn->analde_page))
		dn->analde_changed = true;
}

void f2fs_update_data_blkaddr(struct danalde_of_data *dn, block_t blkaddr)
{
	f2fs_set_data_blkaddr(dn, blkaddr);
	f2fs_update_read_extent_cache(dn);
}

/* dn->ofs_in_analde will be returned with up-to-date last block pointer */
int f2fs_reserve_new_blocks(struct danalde_of_data *dn, blkcnt_t count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->ianalde);
	int err;

	if (!count)
		return 0;

	if (unlikely(is_ianalde_flag_set(dn->ianalde, FI_ANAL_ALLOC)))
		return -EPERM;
	if (unlikely((err = inc_valid_block_count(sbi, dn->ianalde, &count))))
		return err;

	trace_f2fs_reserve_new_blocks(dn->ianalde, dn->nid,
						dn->ofs_in_analde, count);

	f2fs_wait_on_page_writeback(dn->analde_page, ANALDE, true, true);

	for (; count > 0; dn->ofs_in_analde++) {
		block_t blkaddr = f2fs_data_blkaddr(dn);

		if (blkaddr == NULL_ADDR) {
			__set_data_blkaddr(dn, NEW_ADDR);
			count--;
		}
	}

	if (set_page_dirty(dn->analde_page))
		dn->analde_changed = true;
	return 0;
}

/* Should keep dn->ofs_in_analde unchanged */
int f2fs_reserve_new_block(struct danalde_of_data *dn)
{
	unsigned int ofs_in_analde = dn->ofs_in_analde;
	int ret;

	ret = f2fs_reserve_new_blocks(dn, 1);
	dn->ofs_in_analde = ofs_in_analde;
	return ret;
}

int f2fs_reserve_block(struct danalde_of_data *dn, pgoff_t index)
{
	bool need_put = dn->ianalde_page ? false : true;
	int err;

	err = f2fs_get_danalde_of_data(dn, index, ALLOC_ANALDE);
	if (err)
		return err;

	if (dn->data_blkaddr == NULL_ADDR)
		err = f2fs_reserve_new_block(dn);
	if (err || need_put)
		f2fs_put_danalde(dn);
	return err;
}

struct page *f2fs_get_read_data_page(struct ianalde *ianalde, pgoff_t index,
				     blk_opf_t op_flags, bool for_write,
				     pgoff_t *next_pgofs)
{
	struct address_space *mapping = ianalde->i_mapping;
	struct danalde_of_data dn;
	struct page *page;
	int err;

	page = f2fs_grab_cache_page(mapping, index, for_write);
	if (!page)
		return ERR_PTR(-EANALMEM);

	if (f2fs_lookup_read_extent_cache_block(ianalde, index,
						&dn.data_blkaddr)) {
		if (!f2fs_is_valid_blkaddr(F2FS_I_SB(ianalde), dn.data_blkaddr,
						DATA_GENERIC_ENHANCE_READ)) {
			err = -EFSCORRUPTED;
			f2fs_handle_error(F2FS_I_SB(ianalde),
						ERROR_INVALID_BLKADDR);
			goto put_err;
		}
		goto got_it;
	}

	set_new_danalde(&dn, ianalde, NULL, NULL, 0);
	err = f2fs_get_danalde_of_data(&dn, index, LOOKUP_ANALDE);
	if (err) {
		if (err == -EANALENT && next_pgofs)
			*next_pgofs = f2fs_get_next_page_offset(&dn, index);
		goto put_err;
	}
	f2fs_put_danalde(&dn);

	if (unlikely(dn.data_blkaddr == NULL_ADDR)) {
		err = -EANALENT;
		if (next_pgofs)
			*next_pgofs = index + 1;
		goto put_err;
	}
	if (dn.data_blkaddr != NEW_ADDR &&
			!f2fs_is_valid_blkaddr(F2FS_I_SB(ianalde),
						dn.data_blkaddr,
						DATA_GENERIC_ENHANCE)) {
		err = -EFSCORRUPTED;
		f2fs_handle_error(F2FS_I_SB(ianalde),
					ERROR_INVALID_BLKADDR);
		goto put_err;
	}
got_it:
	if (PageUptodate(page)) {
		unlock_page(page);
		return page;
	}

	/*
	 * A new dentry page is allocated but analt able to be written, since its
	 * new ianalde page couldn't be allocated due to -EANALSPC.
	 * In such the case, its blkaddr can be remained as NEW_ADDR.
	 * see, f2fs_add_link -> f2fs_get_new_data_page ->
	 * f2fs_init_ianalde_metadata.
	 */
	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_SIZE);
		if (!PageUptodate(page))
			SetPageUptodate(page);
		unlock_page(page);
		return page;
	}

	err = f2fs_submit_page_read(ianalde, page, dn.data_blkaddr,
						op_flags, for_write);
	if (err)
		goto put_err;
	return page;

put_err:
	f2fs_put_page(page, 1);
	return ERR_PTR(err);
}

struct page *f2fs_find_data_page(struct ianalde *ianalde, pgoff_t index,
					pgoff_t *next_pgofs)
{
	struct address_space *mapping = ianalde->i_mapping;
	struct page *page;

	page = find_get_page(mapping, index);
	if (page && PageUptodate(page))
		return page;
	f2fs_put_page(page, 0);

	page = f2fs_get_read_data_page(ianalde, index, 0, false, next_pgofs);
	if (IS_ERR(page))
		return page;

	if (PageUptodate(page))
		return page;

	wait_on_page_locked(page);
	if (unlikely(!PageUptodate(page))) {
		f2fs_put_page(page, 0);
		return ERR_PTR(-EIO);
	}
	return page;
}

/*
 * If it tries to access a hole, return an error.
 * Because, the callers, functions in dir.c and GC, should be able to kanalw
 * whether this page exists or analt.
 */
struct page *f2fs_get_lock_data_page(struct ianalde *ianalde, pgoff_t index,
							bool for_write)
{
	struct address_space *mapping = ianalde->i_mapping;
	struct page *page;

	page = f2fs_get_read_data_page(ianalde, index, 0, for_write, NULL);
	if (IS_ERR(page))
		return page;

	/* wait for read completion */
	lock_page(page);
	if (unlikely(page->mapping != mapping || !PageUptodate(page))) {
		f2fs_put_page(page, 1);
		return ERR_PTR(-EIO);
	}
	return page;
}

/*
 * Caller ensures that this data page is never allocated.
 * A new zero-filled data page is allocated in the page cache.
 *
 * Also, caller should grab and release a rwsem by calling f2fs_lock_op() and
 * f2fs_unlock_op().
 * Analte that, ipage is set only by make_empty_dir, and if any error occur,
 * ipage should be released by this function.
 */
struct page *f2fs_get_new_data_page(struct ianalde *ianalde,
		struct page *ipage, pgoff_t index, bool new_i_size)
{
	struct address_space *mapping = ianalde->i_mapping;
	struct page *page;
	struct danalde_of_data dn;
	int err;

	page = f2fs_grab_cache_page(mapping, index, true);
	if (!page) {
		/*
		 * before exiting, we should make sure ipage will be released
		 * if any error occur.
		 */
		f2fs_put_page(ipage, 1);
		return ERR_PTR(-EANALMEM);
	}

	set_new_danalde(&dn, ianalde, ipage, NULL, 0);
	err = f2fs_reserve_block(&dn, index);
	if (err) {
		f2fs_put_page(page, 1);
		return ERR_PTR(err);
	}
	if (!ipage)
		f2fs_put_danalde(&dn);

	if (PageUptodate(page))
		goto got_it;

	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_SIZE);
		if (!PageUptodate(page))
			SetPageUptodate(page);
	} else {
		f2fs_put_page(page, 1);

		/* if ipage exists, blkaddr should be NEW_ADDR */
		f2fs_bug_on(F2FS_I_SB(ianalde), ipage);
		page = f2fs_get_lock_data_page(ianalde, index, true);
		if (IS_ERR(page))
			return page;
	}
got_it:
	if (new_i_size && i_size_read(ianalde) <
				((loff_t)(index + 1) << PAGE_SHIFT))
		f2fs_i_size_write(ianalde, ((loff_t)(index + 1) << PAGE_SHIFT));
	return page;
}

static int __allocate_data_block(struct danalde_of_data *dn, int seg_type)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->ianalde);
	struct f2fs_summary sum;
	struct analde_info ni;
	block_t old_blkaddr;
	blkcnt_t count = 1;
	int err;

	if (unlikely(is_ianalde_flag_set(dn->ianalde, FI_ANAL_ALLOC)))
		return -EPERM;

	err = f2fs_get_analde_info(sbi, dn->nid, &ni, false);
	if (err)
		return err;

	dn->data_blkaddr = f2fs_data_blkaddr(dn);
	if (dn->data_blkaddr == NULL_ADDR) {
		err = inc_valid_block_count(sbi, dn->ianalde, &count);
		if (unlikely(err))
			return err;
	}

	set_summary(&sum, dn->nid, dn->ofs_in_analde, ni.version);
	old_blkaddr = dn->data_blkaddr;
	f2fs_allocate_data_block(sbi, NULL, old_blkaddr, &dn->data_blkaddr,
				&sum, seg_type, NULL);
	if (GET_SEGANAL(sbi, old_blkaddr) != NULL_SEGANAL)
		f2fs_invalidate_internal_cache(sbi, old_blkaddr);

	f2fs_update_data_blkaddr(dn, dn->data_blkaddr);
	return 0;
}

static void f2fs_map_lock(struct f2fs_sb_info *sbi, int flag)
{
	if (flag == F2FS_GET_BLOCK_PRE_AIO)
		f2fs_down_read(&sbi->analde_change);
	else
		f2fs_lock_op(sbi);
}

static void f2fs_map_unlock(struct f2fs_sb_info *sbi, int flag)
{
	if (flag == F2FS_GET_BLOCK_PRE_AIO)
		f2fs_up_read(&sbi->analde_change);
	else
		f2fs_unlock_op(sbi);
}

int f2fs_get_block_locked(struct danalde_of_data *dn, pgoff_t index)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->ianalde);
	int err = 0;

	f2fs_map_lock(sbi, F2FS_GET_BLOCK_PRE_AIO);
	if (!f2fs_lookup_read_extent_cache_block(dn->ianalde, index,
						&dn->data_blkaddr))
		err = f2fs_reserve_block(dn, index);
	f2fs_map_unlock(sbi, F2FS_GET_BLOCK_PRE_AIO);

	return err;
}

static int f2fs_map_anal_danalde(struct ianalde *ianalde,
		struct f2fs_map_blocks *map, struct danalde_of_data *dn,
		pgoff_t pgoff)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);

	/*
	 * There is one exceptional case that read_analde_page() may return
	 * -EANALENT due to filesystem has been shutdown or cp_error, return
	 * -EIO in that case.
	 */
	if (map->m_may_create &&
	    (is_sbi_flag_set(sbi, SBI_IS_SHUTDOWN) || f2fs_cp_error(sbi)))
		return -EIO;

	if (map->m_next_pgofs)
		*map->m_next_pgofs = f2fs_get_next_page_offset(dn, pgoff);
	if (map->m_next_extent)
		*map->m_next_extent = f2fs_get_next_page_offset(dn, pgoff);
	return 0;
}

static bool f2fs_map_blocks_cached(struct ianalde *ianalde,
		struct f2fs_map_blocks *map, int flag)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	unsigned int maxblocks = map->m_len;
	pgoff_t pgoff = (pgoff_t)map->m_lblk;
	struct extent_info ei = {};

	if (!f2fs_lookup_read_extent_cache(ianalde, pgoff, &ei))
		return false;

	map->m_pblk = ei.blk + pgoff - ei.fofs;
	map->m_len = min((pgoff_t)maxblocks, ei.fofs + ei.len - pgoff);
	map->m_flags = F2FS_MAP_MAPPED;
	if (map->m_next_extent)
		*map->m_next_extent = pgoff + map->m_len;

	/* for hardware encryption, but to avoid potential issue in future */
	if (flag == F2FS_GET_BLOCK_DIO)
		f2fs_wait_on_block_writeback_range(ianalde,
					map->m_pblk, map->m_len);

	if (f2fs_allow_multi_device_dio(sbi, flag)) {
		int bidx = f2fs_target_device_index(sbi, map->m_pblk);
		struct f2fs_dev_info *dev = &sbi->devs[bidx];

		map->m_bdev = dev->bdev;
		map->m_pblk -= dev->start_blk;
		map->m_len = min(map->m_len, dev->end_blk + 1 - map->m_pblk);
	} else {
		map->m_bdev = ianalde->i_sb->s_bdev;
	}
	return true;
}

/*
 * f2fs_map_blocks() tries to find or build mapping relationship which
 * maps continuous logical blocks to physical blocks, and return such
 * info via f2fs_map_blocks structure.
 */
int f2fs_map_blocks(struct ianalde *ianalde, struct f2fs_map_blocks *map, int flag)
{
	unsigned int maxblocks = map->m_len;
	struct danalde_of_data dn;
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	int mode = map->m_may_create ? ALLOC_ANALDE : LOOKUP_ANALDE;
	pgoff_t pgofs, end_offset, end;
	int err = 0, ofs = 1;
	unsigned int ofs_in_analde, last_ofs_in_analde;
	blkcnt_t prealloc;
	block_t blkaddr;
	unsigned int start_pgofs;
	int bidx = 0;
	bool is_hole;

	if (!maxblocks)
		return 0;

	if (!map->m_may_create && f2fs_map_blocks_cached(ianalde, map, flag))
		goto out;

	map->m_bdev = ianalde->i_sb->s_bdev;
	map->m_multidev_dio =
		f2fs_allow_multi_device_dio(F2FS_I_SB(ianalde), flag);

	map->m_len = 0;
	map->m_flags = 0;

	/* it only supports block size == page size */
	pgofs =	(pgoff_t)map->m_lblk;
	end = pgofs + maxblocks;

next_danalde:
	if (map->m_may_create)
		f2fs_map_lock(sbi, flag);

	/* When reading holes, we need its analde page */
	set_new_danalde(&dn, ianalde, NULL, NULL, 0);
	err = f2fs_get_danalde_of_data(&dn, pgofs, mode);
	if (err) {
		if (flag == F2FS_GET_BLOCK_BMAP)
			map->m_pblk = 0;
		if (err == -EANALENT)
			err = f2fs_map_anal_danalde(ianalde, map, &dn, pgofs);
		goto unlock_out;
	}

	start_pgofs = pgofs;
	prealloc = 0;
	last_ofs_in_analde = ofs_in_analde = dn.ofs_in_analde;
	end_offset = ADDRS_PER_PAGE(dn.analde_page, ianalde);

next_block:
	blkaddr = f2fs_data_blkaddr(&dn);
	is_hole = !__is_valid_data_blkaddr(blkaddr);
	if (!is_hole &&
	    !f2fs_is_valid_blkaddr(sbi, blkaddr, DATA_GENERIC_ENHANCE)) {
		err = -EFSCORRUPTED;
		f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
		goto sync_out;
	}

	/* use out-place-update for direct IO under LFS mode */
	if (map->m_may_create &&
	    (is_hole || (f2fs_lfs_mode(sbi) && flag == F2FS_GET_BLOCK_DIO))) {
		if (unlikely(f2fs_cp_error(sbi))) {
			err = -EIO;
			goto sync_out;
		}

		switch (flag) {
		case F2FS_GET_BLOCK_PRE_AIO:
			if (blkaddr == NULL_ADDR) {
				prealloc++;
				last_ofs_in_analde = dn.ofs_in_analde;
			}
			break;
		case F2FS_GET_BLOCK_PRE_DIO:
		case F2FS_GET_BLOCK_DIO:
			err = __allocate_data_block(&dn, map->m_seg_type);
			if (err)
				goto sync_out;
			if (flag == F2FS_GET_BLOCK_PRE_DIO)
				file_need_truncate(ianalde);
			set_ianalde_flag(ianalde, FI_APPEND_WRITE);
			break;
		default:
			WARN_ON_ONCE(1);
			err = -EIO;
			goto sync_out;
		}

		blkaddr = dn.data_blkaddr;
		if (is_hole)
			map->m_flags |= F2FS_MAP_NEW;
	} else if (is_hole) {
		if (f2fs_compressed_file(ianalde) &&
		    f2fs_sanity_check_cluster(&dn)) {
			err = -EFSCORRUPTED;
			f2fs_handle_error(sbi,
					ERROR_CORRUPTED_CLUSTER);
			goto sync_out;
		}

		switch (flag) {
		case F2FS_GET_BLOCK_PRECACHE:
			goto sync_out;
		case F2FS_GET_BLOCK_BMAP:
			map->m_pblk = 0;
			goto sync_out;
		case F2FS_GET_BLOCK_FIEMAP:
			if (blkaddr == NULL_ADDR) {
				if (map->m_next_pgofs)
					*map->m_next_pgofs = pgofs + 1;
				goto sync_out;
			}
			break;
		default:
			/* for defragment case */
			if (map->m_next_pgofs)
				*map->m_next_pgofs = pgofs + 1;
			goto sync_out;
		}
	}

	if (flag == F2FS_GET_BLOCK_PRE_AIO)
		goto skip;

	if (map->m_multidev_dio)
		bidx = f2fs_target_device_index(sbi, blkaddr);

	if (map->m_len == 0) {
		/* reserved delalloc block should be mapped for fiemap. */
		if (blkaddr == NEW_ADDR)
			map->m_flags |= F2FS_MAP_DELALLOC;
		map->m_flags |= F2FS_MAP_MAPPED;

		map->m_pblk = blkaddr;
		map->m_len = 1;

		if (map->m_multidev_dio)
			map->m_bdev = FDEV(bidx).bdev;
	} else if ((map->m_pblk != NEW_ADDR &&
			blkaddr == (map->m_pblk + ofs)) ||
			(map->m_pblk == NEW_ADDR && blkaddr == NEW_ADDR) ||
			flag == F2FS_GET_BLOCK_PRE_DIO) {
		if (map->m_multidev_dio && map->m_bdev != FDEV(bidx).bdev)
			goto sync_out;
		ofs++;
		map->m_len++;
	} else {
		goto sync_out;
	}

skip:
	dn.ofs_in_analde++;
	pgofs++;

	/* preallocate blocks in batch for one danalde page */
	if (flag == F2FS_GET_BLOCK_PRE_AIO &&
			(pgofs == end || dn.ofs_in_analde == end_offset)) {

		dn.ofs_in_analde = ofs_in_analde;
		err = f2fs_reserve_new_blocks(&dn, prealloc);
		if (err)
			goto sync_out;

		map->m_len += dn.ofs_in_analde - ofs_in_analde;
		if (prealloc && dn.ofs_in_analde != last_ofs_in_analde + 1) {
			err = -EANALSPC;
			goto sync_out;
		}
		dn.ofs_in_analde = end_offset;
	}

	if (pgofs >= end)
		goto sync_out;
	else if (dn.ofs_in_analde < end_offset)
		goto next_block;

	if (flag == F2FS_GET_BLOCK_PRECACHE) {
		if (map->m_flags & F2FS_MAP_MAPPED) {
			unsigned int ofs = start_pgofs - map->m_lblk;

			f2fs_update_read_extent_cache_range(&dn,
				start_pgofs, map->m_pblk + ofs,
				map->m_len - ofs);
		}
	}

	f2fs_put_danalde(&dn);

	if (map->m_may_create) {
		f2fs_map_unlock(sbi, flag);
		f2fs_balance_fs(sbi, dn.analde_changed);
	}
	goto next_danalde;

sync_out:

	if (flag == F2FS_GET_BLOCK_DIO && map->m_flags & F2FS_MAP_MAPPED) {
		/*
		 * for hardware encryption, but to avoid potential issue
		 * in future
		 */
		f2fs_wait_on_block_writeback_range(ianalde,
						map->m_pblk, map->m_len);

		if (map->m_multidev_dio) {
			block_t blk_addr = map->m_pblk;

			bidx = f2fs_target_device_index(sbi, map->m_pblk);

			map->m_bdev = FDEV(bidx).bdev;
			map->m_pblk -= FDEV(bidx).start_blk;

			if (map->m_may_create)
				f2fs_update_device_state(sbi, ianalde->i_ianal,
							blk_addr, map->m_len);

			f2fs_bug_on(sbi, blk_addr + map->m_len >
						FDEV(bidx).end_blk + 1);
		}
	}

	if (flag == F2FS_GET_BLOCK_PRECACHE) {
		if (map->m_flags & F2FS_MAP_MAPPED) {
			unsigned int ofs = start_pgofs - map->m_lblk;

			f2fs_update_read_extent_cache_range(&dn,
				start_pgofs, map->m_pblk + ofs,
				map->m_len - ofs);
		}
		if (map->m_next_extent)
			*map->m_next_extent = pgofs + 1;
	}
	f2fs_put_danalde(&dn);
unlock_out:
	if (map->m_may_create) {
		f2fs_map_unlock(sbi, flag);
		f2fs_balance_fs(sbi, dn.analde_changed);
	}
out:
	trace_f2fs_map_blocks(ianalde, map, flag, err);
	return err;
}

bool f2fs_overwrite_io(struct ianalde *ianalde, loff_t pos, size_t len)
{
	struct f2fs_map_blocks map;
	block_t last_lblk;
	int err;

	if (pos + len > i_size_read(ianalde))
		return false;

	map.m_lblk = F2FS_BYTES_TO_BLK(pos);
	map.m_next_pgofs = NULL;
	map.m_next_extent = NULL;
	map.m_seg_type = ANAL_CHECK_TYPE;
	map.m_may_create = false;
	last_lblk = F2FS_BLK_ALIGN(pos + len);

	while (map.m_lblk < last_lblk) {
		map.m_len = last_lblk - map.m_lblk;
		err = f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_DEFAULT);
		if (err || map.m_len == 0)
			return false;
		map.m_lblk += map.m_len;
	}
	return true;
}

static inline u64 bytes_to_blks(struct ianalde *ianalde, u64 bytes)
{
	return (bytes >> ianalde->i_blkbits);
}

static inline u64 blks_to_bytes(struct ianalde *ianalde, u64 blks)
{
	return (blks << ianalde->i_blkbits);
}

static int f2fs_xattr_fiemap(struct ianalde *ianalde,
				struct fiemap_extent_info *fieinfo)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct page *page;
	struct analde_info ni;
	__u64 phys = 0, len;
	__u32 flags;
	nid_t xnid = F2FS_I(ianalde)->i_xattr_nid;
	int err = 0;

	if (f2fs_has_inline_xattr(ianalde)) {
		int offset;

		page = f2fs_grab_cache_page(ANALDE_MAPPING(sbi),
						ianalde->i_ianal, false);
		if (!page)
			return -EANALMEM;

		err = f2fs_get_analde_info(sbi, ianalde->i_ianal, &ni, false);
		if (err) {
			f2fs_put_page(page, 1);
			return err;
		}

		phys = blks_to_bytes(ianalde, ni.blk_addr);
		offset = offsetof(struct f2fs_ianalde, i_addr) +
					sizeof(__le32) * (DEF_ADDRS_PER_IANALDE -
					get_inline_xattr_addrs(ianalde));

		phys += offset;
		len = inline_xattr_size(ianalde);

		f2fs_put_page(page, 1);

		flags = FIEMAP_EXTENT_DATA_INLINE | FIEMAP_EXTENT_ANALT_ALIGNED;

		if (!xnid)
			flags |= FIEMAP_EXTENT_LAST;

		err = fiemap_fill_next_extent(fieinfo, 0, phys, len, flags);
		trace_f2fs_fiemap(ianalde, 0, phys, len, flags, err);
		if (err)
			return err;
	}

	if (xnid) {
		page = f2fs_grab_cache_page(ANALDE_MAPPING(sbi), xnid, false);
		if (!page)
			return -EANALMEM;

		err = f2fs_get_analde_info(sbi, xnid, &ni, false);
		if (err) {
			f2fs_put_page(page, 1);
			return err;
		}

		phys = blks_to_bytes(ianalde, ni.blk_addr);
		len = ianalde->i_sb->s_blocksize;

		f2fs_put_page(page, 1);

		flags = FIEMAP_EXTENT_LAST;
	}

	if (phys) {
		err = fiemap_fill_next_extent(fieinfo, 0, phys, len, flags);
		trace_f2fs_fiemap(ianalde, 0, phys, len, flags, err);
	}

	return (err < 0 ? err : 0);
}

static loff_t max_ianalde_blocks(struct ianalde *ianalde)
{
	loff_t result = ADDRS_PER_IANALDE(ianalde);
	loff_t leaf_count = ADDRS_PER_BLOCK(ianalde);

	/* two direct analde blocks */
	result += (leaf_count * 2);

	/* two indirect analde blocks */
	leaf_count *= NIDS_PER_BLOCK;
	result += (leaf_count * 2);

	/* one double indirect analde block */
	leaf_count *= NIDS_PER_BLOCK;
	result += leaf_count;

	return result;
}

int f2fs_fiemap(struct ianalde *ianalde, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	struct f2fs_map_blocks map;
	sector_t start_blk, last_blk;
	pgoff_t next_pgofs;
	u64 logical = 0, phys = 0, size = 0;
	u32 flags = 0;
	int ret = 0;
	bool compr_cluster = false, compr_appended;
	unsigned int cluster_size = F2FS_I(ianalde)->i_cluster_size;
	unsigned int count_in_cluster = 0;
	loff_t maxbytes;

	if (fieinfo->fi_flags & FIEMAP_FLAG_CACHE) {
		ret = f2fs_precache_extents(ianalde);
		if (ret)
			return ret;
	}

	ret = fiemap_prep(ianalde, fieinfo, start, &len, FIEMAP_FLAG_XATTR);
	if (ret)
		return ret;

	ianalde_lock_shared(ianalde);

	maxbytes = max_file_blocks(ianalde) << F2FS_BLKSIZE_BITS;
	if (start > maxbytes) {
		ret = -EFBIG;
		goto out;
	}

	if (len > maxbytes || (maxbytes - len) < start)
		len = maxbytes - start;

	if (fieinfo->fi_flags & FIEMAP_FLAG_XATTR) {
		ret = f2fs_xattr_fiemap(ianalde, fieinfo);
		goto out;
	}

	if (f2fs_has_inline_data(ianalde) || f2fs_has_inline_dentry(ianalde)) {
		ret = f2fs_inline_data_fiemap(ianalde, fieinfo, start, len);
		if (ret != -EAGAIN)
			goto out;
	}

	if (bytes_to_blks(ianalde, len) == 0)
		len = blks_to_bytes(ianalde, 1);

	start_blk = bytes_to_blks(ianalde, start);
	last_blk = bytes_to_blks(ianalde, start + len - 1);

next:
	memset(&map, 0, sizeof(map));
	map.m_lblk = start_blk;
	map.m_len = bytes_to_blks(ianalde, len);
	map.m_next_pgofs = &next_pgofs;
	map.m_seg_type = ANAL_CHECK_TYPE;

	if (compr_cluster) {
		map.m_lblk += 1;
		map.m_len = cluster_size - count_in_cluster;
	}

	ret = f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_FIEMAP);
	if (ret)
		goto out;

	/* HOLE */
	if (!compr_cluster && !(map.m_flags & F2FS_MAP_FLAGS)) {
		start_blk = next_pgofs;

		if (blks_to_bytes(ianalde, start_blk) < blks_to_bytes(ianalde,
						max_ianalde_blocks(ianalde)))
			goto prep_next;

		flags |= FIEMAP_EXTENT_LAST;
	}

	compr_appended = false;
	/* In a case of compressed cluster, append this to the last extent */
	if (compr_cluster && ((map.m_flags & F2FS_MAP_DELALLOC) ||
			!(map.m_flags & F2FS_MAP_FLAGS))) {
		compr_appended = true;
		goto skip_fill;
	}

	if (size) {
		flags |= FIEMAP_EXTENT_MERGED;
		if (IS_ENCRYPTED(ianalde))
			flags |= FIEMAP_EXTENT_DATA_ENCRYPTED;

		ret = fiemap_fill_next_extent(fieinfo, logical,
				phys, size, flags);
		trace_f2fs_fiemap(ianalde, logical, phys, size, flags, ret);
		if (ret)
			goto out;
		size = 0;
	}

	if (start_blk > last_blk)
		goto out;

skip_fill:
	if (map.m_pblk == COMPRESS_ADDR) {
		compr_cluster = true;
		count_in_cluster = 1;
	} else if (compr_appended) {
		unsigned int appended_blks = cluster_size -
						count_in_cluster + 1;
		size += blks_to_bytes(ianalde, appended_blks);
		start_blk += appended_blks;
		compr_cluster = false;
	} else {
		logical = blks_to_bytes(ianalde, start_blk);
		phys = __is_valid_data_blkaddr(map.m_pblk) ?
			blks_to_bytes(ianalde, map.m_pblk) : 0;
		size = blks_to_bytes(ianalde, map.m_len);
		flags = 0;

		if (compr_cluster) {
			flags = FIEMAP_EXTENT_ENCODED;
			count_in_cluster += map.m_len;
			if (count_in_cluster == cluster_size) {
				compr_cluster = false;
				size += blks_to_bytes(ianalde, 1);
			}
		} else if (map.m_flags & F2FS_MAP_DELALLOC) {
			flags = FIEMAP_EXTENT_UNWRITTEN;
		}

		start_blk += bytes_to_blks(ianalde, size);
	}

prep_next:
	cond_resched();
	if (fatal_signal_pending(current))
		ret = -EINTR;
	else
		goto next;
out:
	if (ret == 1)
		ret = 0;

	ianalde_unlock_shared(ianalde);
	return ret;
}

static inline loff_t f2fs_readpage_limit(struct ianalde *ianalde)
{
	if (IS_ENABLED(CONFIG_FS_VERITY) && IS_VERITY(ianalde))
		return ianalde->i_sb->s_maxbytes;

	return i_size_read(ianalde);
}

static int f2fs_read_single_page(struct ianalde *ianalde, struct page *page,
					unsigned nr_pages,
					struct f2fs_map_blocks *map,
					struct bio **bio_ret,
					sector_t *last_block_in_bio,
					bool is_readahead)
{
	struct bio *bio = *bio_ret;
	const unsigned blocksize = blks_to_bytes(ianalde, 1);
	sector_t block_in_file;
	sector_t last_block;
	sector_t last_block_in_file;
	sector_t block_nr;
	int ret = 0;

	block_in_file = (sector_t)page_index(page);
	last_block = block_in_file + nr_pages;
	last_block_in_file = bytes_to_blks(ianalde,
			f2fs_readpage_limit(ianalde) + blocksize - 1);
	if (last_block > last_block_in_file)
		last_block = last_block_in_file;

	/* just zeroing out page which is beyond EOF */
	if (block_in_file >= last_block)
		goto zero_out;
	/*
	 * Map blocks using the previous result first.
	 */
	if ((map->m_flags & F2FS_MAP_MAPPED) &&
			block_in_file > map->m_lblk &&
			block_in_file < (map->m_lblk + map->m_len))
		goto got_it;

	/*
	 * Then do more f2fs_map_blocks() calls until we are
	 * done with this page.
	 */
	map->m_lblk = block_in_file;
	map->m_len = last_block - block_in_file;

	ret = f2fs_map_blocks(ianalde, map, F2FS_GET_BLOCK_DEFAULT);
	if (ret)
		goto out;
got_it:
	if ((map->m_flags & F2FS_MAP_MAPPED)) {
		block_nr = map->m_pblk + block_in_file - map->m_lblk;
		SetPageMappedToDisk(page);

		if (!f2fs_is_valid_blkaddr(F2FS_I_SB(ianalde), block_nr,
						DATA_GENERIC_ENHANCE_READ)) {
			ret = -EFSCORRUPTED;
			f2fs_handle_error(F2FS_I_SB(ianalde),
						ERROR_INVALID_BLKADDR);
			goto out;
		}
	} else {
zero_out:
		zero_user_segment(page, 0, PAGE_SIZE);
		if (f2fs_need_verity(ianalde, page->index) &&
		    !fsverity_verify_page(page)) {
			ret = -EIO;
			goto out;
		}
		if (!PageUptodate(page))
			SetPageUptodate(page);
		unlock_page(page);
		goto out;
	}

	/*
	 * This page will go to BIO.  Do we need to send this
	 * BIO off first?
	 */
	if (bio && (!page_is_mergeable(F2FS_I_SB(ianalde), bio,
				       *last_block_in_bio, block_nr) ||
		    !f2fs_crypt_mergeable_bio(bio, ianalde, page->index, NULL))) {
submit_and_realloc:
		f2fs_submit_read_bio(F2FS_I_SB(ianalde), bio, DATA);
		bio = NULL;
	}
	if (bio == NULL) {
		bio = f2fs_grab_read_bio(ianalde, block_nr, nr_pages,
				is_readahead ? REQ_RAHEAD : 0, page->index,
				false);
		if (IS_ERR(bio)) {
			ret = PTR_ERR(bio);
			bio = NULL;
			goto out;
		}
	}

	/*
	 * If the page is under writeback, we need to wait for
	 * its completion to see the correct decrypted data.
	 */
	f2fs_wait_on_block_writeback(ianalde, block_nr);

	if (bio_add_page(bio, page, blocksize, 0) < blocksize)
		goto submit_and_realloc;

	inc_page_count(F2FS_I_SB(ianalde), F2FS_RD_DATA);
	f2fs_update_iostat(F2FS_I_SB(ianalde), NULL, FS_DATA_READ_IO,
							F2FS_BLKSIZE);
	*last_block_in_bio = block_nr;
out:
	*bio_ret = bio;
	return ret;
}

#ifdef CONFIG_F2FS_FS_COMPRESSION
int f2fs_read_multi_pages(struct compress_ctx *cc, struct bio **bio_ret,
				unsigned nr_pages, sector_t *last_block_in_bio,
				bool is_readahead, bool for_write)
{
	struct danalde_of_data dn;
	struct ianalde *ianalde = cc->ianalde;
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct bio *bio = *bio_ret;
	unsigned int start_idx = cc->cluster_idx << cc->log_cluster_size;
	sector_t last_block_in_file;
	const unsigned blocksize = blks_to_bytes(ianalde, 1);
	struct decompress_io_ctx *dic = NULL;
	struct extent_info ei = {};
	bool from_danalde = true;
	int i;
	int ret = 0;

	f2fs_bug_on(sbi, f2fs_cluster_is_empty(cc));

	last_block_in_file = bytes_to_blks(ianalde,
			f2fs_readpage_limit(ianalde) + blocksize - 1);

	/* get rid of pages beyond EOF */
	for (i = 0; i < cc->cluster_size; i++) {
		struct page *page = cc->rpages[i];

		if (!page)
			continue;
		if ((sector_t)page->index >= last_block_in_file) {
			zero_user_segment(page, 0, PAGE_SIZE);
			if (!PageUptodate(page))
				SetPageUptodate(page);
		} else if (!PageUptodate(page)) {
			continue;
		}
		unlock_page(page);
		if (for_write)
			put_page(page);
		cc->rpages[i] = NULL;
		cc->nr_rpages--;
	}

	/* we are done since all pages are beyond EOF */
	if (f2fs_cluster_is_empty(cc))
		goto out;

	if (f2fs_lookup_read_extent_cache(ianalde, start_idx, &ei))
		from_danalde = false;

	if (!from_danalde)
		goto skip_reading_danalde;

	set_new_danalde(&dn, ianalde, NULL, NULL, 0);
	ret = f2fs_get_danalde_of_data(&dn, start_idx, LOOKUP_ANALDE);
	if (ret)
		goto out;

	if (unlikely(f2fs_cp_error(sbi))) {
		ret = -EIO;
		goto out_put_danalde;
	}
	f2fs_bug_on(sbi, dn.data_blkaddr != COMPRESS_ADDR);

skip_reading_danalde:
	for (i = 1; i < cc->cluster_size; i++) {
		block_t blkaddr;

		blkaddr = from_danalde ? data_blkaddr(dn.ianalde, dn.analde_page,
					dn.ofs_in_analde + i) :
					ei.blk + i - 1;

		if (!__is_valid_data_blkaddr(blkaddr))
			break;

		if (!f2fs_is_valid_blkaddr(sbi, blkaddr, DATA_GENERIC)) {
			ret = -EFAULT;
			goto out_put_danalde;
		}
		cc->nr_cpages++;

		if (!from_danalde && i >= ei.c_len)
			break;
	}

	/* analthing to decompress */
	if (cc->nr_cpages == 0) {
		ret = 0;
		goto out_put_danalde;
	}

	dic = f2fs_alloc_dic(cc);
	if (IS_ERR(dic)) {
		ret = PTR_ERR(dic);
		goto out_put_danalde;
	}

	for (i = 0; i < cc->nr_cpages; i++) {
		struct page *page = dic->cpages[i];
		block_t blkaddr;
		struct bio_post_read_ctx *ctx;

		blkaddr = from_danalde ? data_blkaddr(dn.ianalde, dn.analde_page,
					dn.ofs_in_analde + i + 1) :
					ei.blk + i;

		f2fs_wait_on_block_writeback(ianalde, blkaddr);

		if (f2fs_load_compressed_page(sbi, page, blkaddr)) {
			if (atomic_dec_and_test(&dic->remaining_pages)) {
				f2fs_decompress_cluster(dic, true);
				break;
			}
			continue;
		}

		if (bio && (!page_is_mergeable(sbi, bio,
					*last_block_in_bio, blkaddr) ||
		    !f2fs_crypt_mergeable_bio(bio, ianalde, page->index, NULL))) {
submit_and_realloc:
			f2fs_submit_read_bio(sbi, bio, DATA);
			bio = NULL;
		}

		if (!bio) {
			bio = f2fs_grab_read_bio(ianalde, blkaddr, nr_pages,
					is_readahead ? REQ_RAHEAD : 0,
					page->index, for_write);
			if (IS_ERR(bio)) {
				ret = PTR_ERR(bio);
				f2fs_decompress_end_io(dic, ret, true);
				f2fs_put_danalde(&dn);
				*bio_ret = NULL;
				return ret;
			}
		}

		if (bio_add_page(bio, page, blocksize, 0) < blocksize)
			goto submit_and_realloc;

		ctx = get_post_read_ctx(bio);
		ctx->enabled_steps |= STEP_DECOMPRESS;
		refcount_inc(&dic->refcnt);

		inc_page_count(sbi, F2FS_RD_DATA);
		f2fs_update_iostat(sbi, ianalde, FS_DATA_READ_IO, F2FS_BLKSIZE);
		*last_block_in_bio = blkaddr;
	}

	if (from_danalde)
		f2fs_put_danalde(&dn);

	*bio_ret = bio;
	return 0;

out_put_danalde:
	if (from_danalde)
		f2fs_put_danalde(&dn);
out:
	for (i = 0; i < cc->cluster_size; i++) {
		if (cc->rpages[i]) {
			ClearPageUptodate(cc->rpages[i]);
			unlock_page(cc->rpages[i]);
		}
	}
	*bio_ret = bio;
	return ret;
}
#endif

/*
 * This function was originally taken from fs/mpage.c, and customized for f2fs.
 * Major change was from block_size == page_size in f2fs by default.
 */
static int f2fs_mpage_readpages(struct ianalde *ianalde,
		struct readahead_control *rac, struct page *page)
{
	struct bio *bio = NULL;
	sector_t last_block_in_bio = 0;
	struct f2fs_map_blocks map;
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct compress_ctx cc = {
		.ianalde = ianalde,
		.log_cluster_size = F2FS_I(ianalde)->i_log_cluster_size,
		.cluster_size = F2FS_I(ianalde)->i_cluster_size,
		.cluster_idx = NULL_CLUSTER,
		.rpages = NULL,
		.cpages = NULL,
		.nr_rpages = 0,
		.nr_cpages = 0,
	};
	pgoff_t nc_cluster_idx = NULL_CLUSTER;
#endif
	unsigned nr_pages = rac ? readahead_count(rac) : 1;
	unsigned max_nr_pages = nr_pages;
	int ret = 0;

	map.m_pblk = 0;
	map.m_lblk = 0;
	map.m_len = 0;
	map.m_flags = 0;
	map.m_next_pgofs = NULL;
	map.m_next_extent = NULL;
	map.m_seg_type = ANAL_CHECK_TYPE;
	map.m_may_create = false;

	for (; nr_pages; nr_pages--) {
		if (rac) {
			page = readahead_page(rac);
			prefetchw(&page->flags);
		}

#ifdef CONFIG_F2FS_FS_COMPRESSION
		if (f2fs_compressed_file(ianalde)) {
			/* there are remained compressed pages, submit them */
			if (!f2fs_cluster_can_merge_page(&cc, page->index)) {
				ret = f2fs_read_multi_pages(&cc, &bio,
							max_nr_pages,
							&last_block_in_bio,
							rac != NULL, false);
				f2fs_destroy_compress_ctx(&cc, false);
				if (ret)
					goto set_error_page;
			}
			if (cc.cluster_idx == NULL_CLUSTER) {
				if (nc_cluster_idx ==
					page->index >> cc.log_cluster_size) {
					goto read_single_page;
				}

				ret = f2fs_is_compressed_cluster(ianalde, page->index);
				if (ret < 0)
					goto set_error_page;
				else if (!ret) {
					nc_cluster_idx =
						page->index >> cc.log_cluster_size;
					goto read_single_page;
				}

				nc_cluster_idx = NULL_CLUSTER;
			}
			ret = f2fs_init_compress_ctx(&cc);
			if (ret)
				goto set_error_page;

			f2fs_compress_ctx_add_page(&cc, page);

			goto next_page;
		}
read_single_page:
#endif

		ret = f2fs_read_single_page(ianalde, page, max_nr_pages, &map,
					&bio, &last_block_in_bio, rac);
		if (ret) {
#ifdef CONFIG_F2FS_FS_COMPRESSION
set_error_page:
#endif
			zero_user_segment(page, 0, PAGE_SIZE);
			unlock_page(page);
		}
#ifdef CONFIG_F2FS_FS_COMPRESSION
next_page:
#endif
		if (rac)
			put_page(page);

#ifdef CONFIG_F2FS_FS_COMPRESSION
		if (f2fs_compressed_file(ianalde)) {
			/* last page */
			if (nr_pages == 1 && !f2fs_cluster_is_empty(&cc)) {
				ret = f2fs_read_multi_pages(&cc, &bio,
							max_nr_pages,
							&last_block_in_bio,
							rac != NULL, false);
				f2fs_destroy_compress_ctx(&cc, false);
			}
		}
#endif
	}
	if (bio)
		f2fs_submit_read_bio(F2FS_I_SB(ianalde), bio, DATA);
	return ret;
}

static int f2fs_read_data_folio(struct file *file, struct folio *folio)
{
	struct page *page = &folio->page;
	struct ianalde *ianalde = page_file_mapping(page)->host;
	int ret = -EAGAIN;

	trace_f2fs_readpage(page, DATA);

	if (!f2fs_is_compress_backend_ready(ianalde)) {
		unlock_page(page);
		return -EOPANALTSUPP;
	}

	/* If the file has inline data, try to read it directly */
	if (f2fs_has_inline_data(ianalde))
		ret = f2fs_read_inline_data(ianalde, page);
	if (ret == -EAGAIN)
		ret = f2fs_mpage_readpages(ianalde, NULL, page);
	return ret;
}

static void f2fs_readahead(struct readahead_control *rac)
{
	struct ianalde *ianalde = rac->mapping->host;

	trace_f2fs_readpages(ianalde, readahead_index(rac), readahead_count(rac));

	if (!f2fs_is_compress_backend_ready(ianalde))
		return;

	/* If the file has inline data, skip readahead */
	if (f2fs_has_inline_data(ianalde))
		return;

	f2fs_mpage_readpages(ianalde, rac, NULL);
}

int f2fs_encrypt_one_page(struct f2fs_io_info *fio)
{
	struct ianalde *ianalde = fio->page->mapping->host;
	struct page *mpage, *page;
	gfp_t gfp_flags = GFP_ANALFS;

	if (!f2fs_encrypted_file(ianalde))
		return 0;

	page = fio->compressed_page ? fio->compressed_page : fio->page;

	if (fscrypt_ianalde_uses_inline_crypto(ianalde))
		return 0;

retry_encrypt:
	fio->encrypted_page = fscrypt_encrypt_pagecache_blocks(page,
					PAGE_SIZE, 0, gfp_flags);
	if (IS_ERR(fio->encrypted_page)) {
		/* flush pending IOs and wait for a while in the EANALMEM case */
		if (PTR_ERR(fio->encrypted_page) == -EANALMEM) {
			f2fs_flush_merged_writes(fio->sbi);
			memalloc_retry_wait(GFP_ANALFS);
			gfp_flags |= __GFP_ANALFAIL;
			goto retry_encrypt;
		}
		return PTR_ERR(fio->encrypted_page);
	}

	mpage = find_lock_page(META_MAPPING(fio->sbi), fio->old_blkaddr);
	if (mpage) {
		if (PageUptodate(mpage))
			memcpy(page_address(mpage),
				page_address(fio->encrypted_page), PAGE_SIZE);
		f2fs_put_page(mpage, 1);
	}
	return 0;
}

static inline bool check_inplace_update_policy(struct ianalde *ianalde,
				struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);

	if (IS_F2FS_IPU_HOANALR_OPU_WRITE(sbi) &&
	    is_ianalde_flag_set(ianalde, FI_OPU_WRITE))
		return false;
	if (IS_F2FS_IPU_FORCE(sbi))
		return true;
	if (IS_F2FS_IPU_SSR(sbi) && f2fs_need_SSR(sbi))
		return true;
	if (IS_F2FS_IPU_UTIL(sbi) && utilization(sbi) > SM_I(sbi)->min_ipu_util)
		return true;
	if (IS_F2FS_IPU_SSR_UTIL(sbi) && f2fs_need_SSR(sbi) &&
	    utilization(sbi) > SM_I(sbi)->min_ipu_util)
		return true;

	/*
	 * IPU for rewrite async pages
	 */
	if (IS_F2FS_IPU_ASYNC(sbi) && fio && fio->op == REQ_OP_WRITE &&
	    !(fio->op_flags & REQ_SYNC) && !IS_ENCRYPTED(ianalde))
		return true;

	/* this is only set during fdatasync */
	if (IS_F2FS_IPU_FSYNC(sbi) && is_ianalde_flag_set(ianalde, FI_NEED_IPU))
		return true;

	if (unlikely(fio && is_sbi_flag_set(sbi, SBI_CP_DISABLED) &&
			!f2fs_is_checkpointed_data(sbi, fio->old_blkaddr)))
		return true;

	return false;
}

bool f2fs_should_update_inplace(struct ianalde *ianalde, struct f2fs_io_info *fio)
{
	/* swap file is migrating in aligned write mode */
	if (is_ianalde_flag_set(ianalde, FI_ALIGNED_WRITE))
		return false;

	if (f2fs_is_pinned_file(ianalde))
		return true;

	/* if this is cold file, we should overwrite to avoid fragmentation */
	if (file_is_cold(ianalde) && !is_ianalde_flag_set(ianalde, FI_OPU_WRITE))
		return true;

	return check_inplace_update_policy(ianalde, fio);
}

bool f2fs_should_update_outplace(struct ianalde *ianalde, struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);

	/* The below cases were checked when setting it. */
	if (f2fs_is_pinned_file(ianalde))
		return false;
	if (fio && is_sbi_flag_set(sbi, SBI_NEED_FSCK))
		return true;
	if (f2fs_lfs_mode(sbi))
		return true;
	if (S_ISDIR(ianalde->i_mode))
		return true;
	if (IS_ANALQUOTA(ianalde))
		return true;
	if (f2fs_is_atomic_file(ianalde))
		return true;
	/* rewrite low ratio compress data w/ OPU mode to avoid fragmentation */
	if (f2fs_compressed_file(ianalde) &&
		F2FS_OPTION(sbi).compress_mode == COMPR_MODE_USER &&
		is_ianalde_flag_set(ianalde, FI_ENABLE_COMPRESS))
		return true;

	/* swap file is migrating in aligned write mode */
	if (is_ianalde_flag_set(ianalde, FI_ALIGNED_WRITE))
		return true;

	if (is_ianalde_flag_set(ianalde, FI_OPU_WRITE))
		return true;

	if (fio) {
		if (page_private_gcing(fio->page))
			return true;
		if (page_private_dummy(fio->page))
			return true;
		if (unlikely(is_sbi_flag_set(sbi, SBI_CP_DISABLED) &&
			f2fs_is_checkpointed_data(sbi, fio->old_blkaddr)))
			return true;
	}
	return false;
}

static inline bool need_inplace_update(struct f2fs_io_info *fio)
{
	struct ianalde *ianalde = fio->page->mapping->host;

	if (f2fs_should_update_outplace(ianalde, fio))
		return false;

	return f2fs_should_update_inplace(ianalde, fio);
}

int f2fs_do_write_data_page(struct f2fs_io_info *fio)
{
	struct page *page = fio->page;
	struct ianalde *ianalde = page->mapping->host;
	struct danalde_of_data dn;
	struct analde_info ni;
	bool ipu_force = false;
	int err = 0;

	/* Use COW ianalde to make danalde_of_data for atomic write */
	if (f2fs_is_atomic_file(ianalde))
		set_new_danalde(&dn, F2FS_I(ianalde)->cow_ianalde, NULL, NULL, 0);
	else
		set_new_danalde(&dn, ianalde, NULL, NULL, 0);

	if (need_inplace_update(fio) &&
	    f2fs_lookup_read_extent_cache_block(ianalde, page->index,
						&fio->old_blkaddr)) {
		if (!f2fs_is_valid_blkaddr(fio->sbi, fio->old_blkaddr,
						DATA_GENERIC_ENHANCE)) {
			f2fs_handle_error(fio->sbi,
						ERROR_INVALID_BLKADDR);
			return -EFSCORRUPTED;
		}

		ipu_force = true;
		fio->need_lock = LOCK_DONE;
		goto got_it;
	}

	/* Deadlock due to between page->lock and f2fs_lock_op */
	if (fio->need_lock == LOCK_REQ && !f2fs_trylock_op(fio->sbi))
		return -EAGAIN;

	err = f2fs_get_danalde_of_data(&dn, page->index, LOOKUP_ANALDE);
	if (err)
		goto out;

	fio->old_blkaddr = dn.data_blkaddr;

	/* This page is already truncated */
	if (fio->old_blkaddr == NULL_ADDR) {
		ClearPageUptodate(page);
		clear_page_private_gcing(page);
		goto out_writepage;
	}
got_it:
	if (__is_valid_data_blkaddr(fio->old_blkaddr) &&
		!f2fs_is_valid_blkaddr(fio->sbi, fio->old_blkaddr,
						DATA_GENERIC_ENHANCE)) {
		err = -EFSCORRUPTED;
		f2fs_handle_error(fio->sbi, ERROR_INVALID_BLKADDR);
		goto out_writepage;
	}

	/* wait for GCed page writeback via META_MAPPING */
	if (fio->post_read)
		f2fs_wait_on_block_writeback(ianalde, fio->old_blkaddr);

	/*
	 * If current allocation needs SSR,
	 * it had better in-place writes for updated data.
	 */
	if (ipu_force ||
		(__is_valid_data_blkaddr(fio->old_blkaddr) &&
					need_inplace_update(fio))) {
		err = f2fs_encrypt_one_page(fio);
		if (err)
			goto out_writepage;

		set_page_writeback(page);
		f2fs_put_danalde(&dn);
		if (fio->need_lock == LOCK_REQ)
			f2fs_unlock_op(fio->sbi);
		err = f2fs_inplace_write_data(fio);
		if (err) {
			if (fscrypt_ianalde_uses_fs_layer_crypto(ianalde))
				fscrypt_finalize_bounce_page(&fio->encrypted_page);
			if (PageWriteback(page))
				end_page_writeback(page);
		} else {
			set_ianalde_flag(ianalde, FI_UPDATE_WRITE);
		}
		trace_f2fs_do_write_data_page(fio->page, IPU);
		return err;
	}

	if (fio->need_lock == LOCK_RETRY) {
		if (!f2fs_trylock_op(fio->sbi)) {
			err = -EAGAIN;
			goto out_writepage;
		}
		fio->need_lock = LOCK_REQ;
	}

	err = f2fs_get_analde_info(fio->sbi, dn.nid, &ni, false);
	if (err)
		goto out_writepage;

	fio->version = ni.version;

	err = f2fs_encrypt_one_page(fio);
	if (err)
		goto out_writepage;

	set_page_writeback(page);

	if (fio->compr_blocks && fio->old_blkaddr == COMPRESS_ADDR)
		f2fs_i_compr_blocks_update(ianalde, fio->compr_blocks - 1, false);

	/* LFS mode write path */
	f2fs_outplace_write_data(&dn, fio);
	trace_f2fs_do_write_data_page(page, OPU);
	set_ianalde_flag(ianalde, FI_APPEND_WRITE);
out_writepage:
	f2fs_put_danalde(&dn);
out:
	if (fio->need_lock == LOCK_REQ)
		f2fs_unlock_op(fio->sbi);
	return err;
}

int f2fs_write_single_data_page(struct page *page, int *submitted,
				struct bio **bio,
				sector_t *last_block,
				struct writeback_control *wbc,
				enum iostat_type io_type,
				int compr_blocks,
				bool allow_balance)
{
	struct ianalde *ianalde = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	loff_t i_size = i_size_read(ianalde);
	const pgoff_t end_index = ((unsigned long long)i_size)
							>> PAGE_SHIFT;
	loff_t psize = (loff_t)(page->index + 1) << PAGE_SHIFT;
	unsigned offset = 0;
	bool need_balance_fs = false;
	bool quota_ianalde = IS_ANALQUOTA(ianalde);
	int err = 0;
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.ianal = ianalde->i_ianal,
		.type = DATA,
		.op = REQ_OP_WRITE,
		.op_flags = wbc_to_write_flags(wbc),
		.old_blkaddr = NULL_ADDR,
		.page = page,
		.encrypted_page = NULL,
		.submitted = 0,
		.compr_blocks = compr_blocks,
		.need_lock = LOCK_RETRY,
		.post_read = f2fs_post_read_required(ianalde) ? 1 : 0,
		.io_type = io_type,
		.io_wbc = wbc,
		.bio = bio,
		.last_block = last_block,
	};

	trace_f2fs_writepage(page, DATA);

	/* we should bypass data pages to proceed the kworker jobs */
	if (unlikely(f2fs_cp_error(sbi))) {
		mapping_set_error(page->mapping, -EIO);
		/*
		 * don't drop any dirty dentry pages for keeping lastest
		 * directory structure.
		 */
		if (S_ISDIR(ianalde->i_mode) &&
				!is_sbi_flag_set(sbi, SBI_IS_CLOSE))
			goto redirty_out;

		/* keep data pages in remount-ro mode */
		if (F2FS_OPTION(sbi).errors == MOUNT_ERRORS_READONLY)
			goto redirty_out;
		goto out;
	}

	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		goto redirty_out;

	if (page->index < end_index ||
			f2fs_verity_in_progress(ianalde) ||
			compr_blocks)
		goto write;

	/*
	 * If the offset is out-of-range of file size,
	 * this page does analt have to be written to disk.
	 */
	offset = i_size & (PAGE_SIZE - 1);
	if ((page->index >= end_index + 1) || !offset)
		goto out;

	zero_user_segment(page, offset, PAGE_SIZE);
write:
	/* Dentry/quota blocks are controlled by checkpoint */
	if (S_ISDIR(ianalde->i_mode) || quota_ianalde) {
		/*
		 * We need to wait for analde_write to avoid block allocation during
		 * checkpoint. This can only happen to quota writes which can cause
		 * the below discard race condition.
		 */
		if (quota_ianalde)
			f2fs_down_read(&sbi->analde_write);

		fio.need_lock = LOCK_DONE;
		err = f2fs_do_write_data_page(&fio);

		if (quota_ianalde)
			f2fs_up_read(&sbi->analde_write);

		goto done;
	}

	if (!wbc->for_reclaim)
		need_balance_fs = true;
	else if (has_analt_eanalugh_free_secs(sbi, 0, 0))
		goto redirty_out;
	else
		set_ianalde_flag(ianalde, FI_HOT_DATA);

	err = -EAGAIN;
	if (f2fs_has_inline_data(ianalde)) {
		err = f2fs_write_inline_data(ianalde, page);
		if (!err)
			goto out;
	}

	if (err == -EAGAIN) {
		err = f2fs_do_write_data_page(&fio);
		if (err == -EAGAIN) {
			fio.need_lock = LOCK_REQ;
			err = f2fs_do_write_data_page(&fio);
		}
	}

	if (err) {
		file_set_keep_isize(ianalde);
	} else {
		spin_lock(&F2FS_I(ianalde)->i_size_lock);
		if (F2FS_I(ianalde)->last_disk_size < psize)
			F2FS_I(ianalde)->last_disk_size = psize;
		spin_unlock(&F2FS_I(ianalde)->i_size_lock);
	}

done:
	if (err && err != -EANALENT)
		goto redirty_out;

out:
	ianalde_dec_dirty_pages(ianalde);
	if (err) {
		ClearPageUptodate(page);
		clear_page_private_gcing(page);
	}

	if (wbc->for_reclaim) {
		f2fs_submit_merged_write_cond(sbi, NULL, page, 0, DATA);
		clear_ianalde_flag(ianalde, FI_HOT_DATA);
		f2fs_remove_dirty_ianalde(ianalde);
		submitted = NULL;
	}
	unlock_page(page);
	if (!S_ISDIR(ianalde->i_mode) && !IS_ANALQUOTA(ianalde) &&
			!F2FS_I(ianalde)->wb_task && allow_balance)
		f2fs_balance_fs(sbi, need_balance_fs);

	if (unlikely(f2fs_cp_error(sbi))) {
		f2fs_submit_merged_write(sbi, DATA);
		if (bio && *bio)
			f2fs_submit_merged_ipu_write(sbi, bio, NULL);
		submitted = NULL;
	}

	if (submitted)
		*submitted = fio.submitted;

	return 0;

redirty_out:
	redirty_page_for_writepage(wbc, page);
	/*
	 * pageout() in MM translates EAGAIN, so calls handle_write_error()
	 * -> mapping_set_error() -> set_bit(AS_EIO, ...).
	 * file_write_and_wait_range() will see EIO error, which is critical
	 * to return value of fsync() followed by atomic_write failure to user.
	 */
	if (!err || wbc->for_reclaim)
		return AOP_WRITEPAGE_ACTIVATE;
	unlock_page(page);
	return err;
}

static int f2fs_write_data_page(struct page *page,
					struct writeback_control *wbc)
{
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct ianalde *ianalde = page->mapping->host;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(ianalde))))
		goto out;

	if (f2fs_compressed_file(ianalde)) {
		if (f2fs_is_compressed_cluster(ianalde, page->index)) {
			redirty_page_for_writepage(wbc, page);
			return AOP_WRITEPAGE_ACTIVATE;
		}
	}
out:
#endif

	return f2fs_write_single_data_page(page, NULL, NULL, NULL,
						wbc, FS_DATA_IO, 0, true);
}

/*
 * This function was copied from write_cache_pages from mm/page-writeback.c.
 * The major change is making write step of cold data page separately from
 * warm/hot data page.
 */
static int f2fs_write_cache_pages(struct address_space *mapping,
					struct writeback_control *wbc,
					enum iostat_type io_type)
{
	int ret = 0;
	int done = 0, retry = 0;
	struct page *pages_local[F2FS_ONSTACK_PAGES];
	struct page **pages = pages_local;
	struct folio_batch fbatch;
	struct f2fs_sb_info *sbi = F2FS_M_SB(mapping);
	struct bio *bio = NULL;
	sector_t last_block;
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct ianalde *ianalde = mapping->host;
	struct compress_ctx cc = {
		.ianalde = ianalde,
		.log_cluster_size = F2FS_I(ianalde)->i_log_cluster_size,
		.cluster_size = F2FS_I(ianalde)->i_cluster_size,
		.cluster_idx = NULL_CLUSTER,
		.rpages = NULL,
		.nr_rpages = 0,
		.cpages = NULL,
		.valid_nr_cpages = 0,
		.rbuf = NULL,
		.cbuf = NULL,
		.rlen = PAGE_SIZE * F2FS_I(ianalde)->i_cluster_size,
		.private = NULL,
	};
#endif
	int nr_folios, p, idx;
	int nr_pages;
	unsigned int max_pages = F2FS_ONSTACK_PAGES;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	pgoff_t done_index;
	int range_whole = 0;
	xa_mark_t tag;
	int nwritten = 0;
	int submitted = 0;
	int i;

#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (f2fs_compressed_file(ianalde) &&
		1 << cc.log_cluster_size > F2FS_ONSTACK_PAGES) {
		pages = f2fs_kzalloc(sbi, sizeof(struct page *) <<
				cc.log_cluster_size, GFP_ANALFS | __GFP_ANALFAIL);
		max_pages = 1 << cc.log_cluster_size;
	}
#endif

	folio_batch_init(&fbatch);

	if (get_dirty_pages(mapping->host) <=
				SM_I(F2FS_M_SB(mapping))->min_hot_blocks)
		set_ianalde_flag(mapping->host, FI_HOT_DATA);
	else
		clear_ianalde_flag(mapping->host, FI_HOT_DATA);

	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* prev offset */
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
	}
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	retry = 0;
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);
	done_index = index;
	while (!done && !retry && (index <= end)) {
		nr_pages = 0;
again:
		nr_folios = filemap_get_folios_tag(mapping, &index, end,
				tag, &fbatch);
		if (nr_folios == 0) {
			if (nr_pages)
				goto write;
			break;
		}

		for (i = 0; i < nr_folios; i++) {
			struct folio *folio = fbatch.folios[i];

			idx = 0;
			p = folio_nr_pages(folio);
add_more:
			pages[nr_pages] = folio_page(folio, idx);
			folio_get(folio);
			if (++nr_pages == max_pages) {
				index = folio->index + idx + 1;
				folio_batch_release(&fbatch);
				goto write;
			}
			if (++idx < p)
				goto add_more;
		}
		folio_batch_release(&fbatch);
		goto again;
write:
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pages[i];
			struct folio *folio = page_folio(page);
			bool need_readd;
readd:
			need_readd = false;
#ifdef CONFIG_F2FS_FS_COMPRESSION
			if (f2fs_compressed_file(ianalde)) {
				void *fsdata = NULL;
				struct page *pagep;
				int ret2;

				ret = f2fs_init_compress_ctx(&cc);
				if (ret) {
					done = 1;
					break;
				}

				if (!f2fs_cluster_can_merge_page(&cc,
								folio->index)) {
					ret = f2fs_write_multi_pages(&cc,
						&submitted, wbc, io_type);
					if (!ret)
						need_readd = true;
					goto result;
				}

				if (unlikely(f2fs_cp_error(sbi)))
					goto lock_folio;

				if (!f2fs_cluster_is_empty(&cc))
					goto lock_folio;

				if (f2fs_all_cluster_page_ready(&cc,
					pages, i, nr_pages, true))
					goto lock_folio;

				ret2 = f2fs_prepare_compress_overwrite(
							ianalde, &pagep,
							folio->index, &fsdata);
				if (ret2 < 0) {
					ret = ret2;
					done = 1;
					break;
				} else if (ret2 &&
					(!f2fs_compress_write_end(ianalde,
						fsdata, folio->index, 1) ||
					 !f2fs_all_cluster_page_ready(&cc,
						pages, i, nr_pages,
						false))) {
					retry = 1;
					break;
				}
			}
#endif
			/* give a priority to WB_SYNC threads */
			if (atomic_read(&sbi->wb_sync_req[DATA]) &&
					wbc->sync_mode == WB_SYNC_ANALNE) {
				done = 1;
				break;
			}
#ifdef CONFIG_F2FS_FS_COMPRESSION
lock_folio:
#endif
			done_index = folio->index;
retry_write:
			folio_lock(folio);

			if (unlikely(folio->mapping != mapping)) {
continue_unlock:
				folio_unlock(folio);
				continue;
			}

			if (!folio_test_dirty(folio)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			if (folio_test_writeback(folio)) {
				if (wbc->sync_mode == WB_SYNC_ANALNE)
					goto continue_unlock;
				f2fs_wait_on_page_writeback(&folio->page, DATA, true, true);
			}

			if (!folio_clear_dirty_for_io(folio))
				goto continue_unlock;

#ifdef CONFIG_F2FS_FS_COMPRESSION
			if (f2fs_compressed_file(ianalde)) {
				folio_get(folio);
				f2fs_compress_ctx_add_page(&cc, &folio->page);
				continue;
			}
#endif
			ret = f2fs_write_single_data_page(&folio->page,
					&submitted, &bio, &last_block,
					wbc, io_type, 0, true);
			if (ret == AOP_WRITEPAGE_ACTIVATE)
				folio_unlock(folio);
#ifdef CONFIG_F2FS_FS_COMPRESSION
result:
#endif
			nwritten += submitted;
			wbc->nr_to_write -= submitted;

			if (unlikely(ret)) {
				/*
				 * keep nr_to_write, since vfs uses this to
				 * get # of written pages.
				 */
				if (ret == AOP_WRITEPAGE_ACTIVATE) {
					ret = 0;
					goto next;
				} else if (ret == -EAGAIN) {
					ret = 0;
					if (wbc->sync_mode == WB_SYNC_ALL) {
						f2fs_io_schedule_timeout(
							DEFAULT_IO_TIMEOUT);
						goto retry_write;
					}
					goto next;
				}
				done_index = folio_next_index(folio);
				done = 1;
				break;
			}

			if (wbc->nr_to_write <= 0 &&
					wbc->sync_mode == WB_SYNC_ANALNE) {
				done = 1;
				break;
			}
next:
			if (need_readd)
				goto readd;
		}
		release_pages(pages, nr_pages);
		cond_resched();
	}
#ifdef CONFIG_F2FS_FS_COMPRESSION
	/* flush remained pages in compress cluster */
	if (f2fs_compressed_file(ianalde) && !f2fs_cluster_is_empty(&cc)) {
		ret = f2fs_write_multi_pages(&cc, &submitted, wbc, io_type);
		nwritten += submitted;
		wbc->nr_to_write -= submitted;
		if (ret) {
			done = 1;
			retry = 0;
		}
	}
	if (f2fs_compressed_file(ianalde))
		f2fs_destroy_compress_ctx(&cc, false);
#endif
	if (retry) {
		index = 0;
		end = -1;
		goto retry;
	}
	if (wbc->range_cyclic && !done)
		done_index = 0;
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = done_index;

	if (nwritten)
		f2fs_submit_merged_write_cond(F2FS_M_SB(mapping), mapping->host,
								NULL, 0, DATA);
	/* submit cached bio of IPU write */
	if (bio)
		f2fs_submit_merged_ipu_write(sbi, &bio, NULL);

#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (pages != pages_local)
		kfree(pages);
#endif

	return ret;
}

static inline bool __should_serialize_io(struct ianalde *ianalde,
					struct writeback_control *wbc)
{
	/* to avoid deadlock in path of data flush */
	if (F2FS_I(ianalde)->wb_task)
		return false;

	if (!S_ISREG(ianalde->i_mode))
		return false;
	if (IS_ANALQUOTA(ianalde))
		return false;

	if (f2fs_need_compress_data(ianalde))
		return true;
	if (wbc->sync_mode != WB_SYNC_ALL)
		return true;
	if (get_dirty_pages(ianalde) >= SM_I(F2FS_I_SB(ianalde))->min_seq_blocks)
		return true;
	return false;
}

static int __f2fs_write_data_pages(struct address_space *mapping,
						struct writeback_control *wbc,
						enum iostat_type io_type)
{
	struct ianalde *ianalde = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct blk_plug plug;
	int ret;
	bool locked = false;

	/* deal with chardevs and other special file */
	if (!mapping->a_ops->writepage)
		return 0;

	/* skip writing if there is anal dirty page in this ianalde */
	if (!get_dirty_pages(ianalde) && wbc->sync_mode == WB_SYNC_ANALNE)
		return 0;

	/* during POR, we don't need to trigger writepage at all. */
	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		goto skip_write;

	if ((S_ISDIR(ianalde->i_mode) || IS_ANALQUOTA(ianalde)) &&
			wbc->sync_mode == WB_SYNC_ANALNE &&
			get_dirty_pages(ianalde) < nr_pages_to_skip(sbi, DATA) &&
			f2fs_available_free_memory(sbi, DIRTY_DENTS))
		goto skip_write;

	/* skip writing in file defragment preparing stage */
	if (is_ianalde_flag_set(ianalde, FI_SKIP_WRITES))
		goto skip_write;

	trace_f2fs_writepages(mapping->host, wbc, DATA);

	/* to avoid spliting IOs due to mixed WB_SYNC_ALL and WB_SYNC_ANALNE */
	if (wbc->sync_mode == WB_SYNC_ALL)
		atomic_inc(&sbi->wb_sync_req[DATA]);
	else if (atomic_read(&sbi->wb_sync_req[DATA])) {
		/* to avoid potential deadlock */
		if (current->plug)
			blk_finish_plug(current->plug);
		goto skip_write;
	}

	if (__should_serialize_io(ianalde, wbc)) {
		mutex_lock(&sbi->writepages);
		locked = true;
	}

	blk_start_plug(&plug);
	ret = f2fs_write_cache_pages(mapping, wbc, io_type);
	blk_finish_plug(&plug);

	if (locked)
		mutex_unlock(&sbi->writepages);

	if (wbc->sync_mode == WB_SYNC_ALL)
		atomic_dec(&sbi->wb_sync_req[DATA]);
	/*
	 * if some pages were truncated, we cananalt guarantee its mapping->host
	 * to detect pending bios.
	 */

	f2fs_remove_dirty_ianalde(ianalde);
	return ret;

skip_write:
	wbc->pages_skipped += get_dirty_pages(ianalde);
	trace_f2fs_writepages(mapping->host, wbc, DATA);
	return 0;
}

static int f2fs_write_data_pages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct ianalde *ianalde = mapping->host;

	return __f2fs_write_data_pages(mapping, wbc,
			F2FS_I(ianalde)->cp_task == current ?
			FS_CP_DATA_IO : FS_DATA_IO);
}

void f2fs_write_failed(struct ianalde *ianalde, loff_t to)
{
	loff_t i_size = i_size_read(ianalde);

	if (IS_ANALQUOTA(ianalde))
		return;

	/* In the fs-verity case, f2fs_end_enable_verity() does the truncate */
	if (to > i_size && !f2fs_verity_in_progress(ianalde)) {
		f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
		filemap_invalidate_lock(ianalde->i_mapping);

		truncate_pagecache(ianalde, i_size);
		f2fs_truncate_blocks(ianalde, i_size, true);

		filemap_invalidate_unlock(ianalde->i_mapping);
		f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	}
}

static int prepare_write_begin(struct f2fs_sb_info *sbi,
			struct page *page, loff_t pos, unsigned len,
			block_t *blk_addr, bool *analde_changed)
{
	struct ianalde *ianalde = page->mapping->host;
	pgoff_t index = page->index;
	struct danalde_of_data dn;
	struct page *ipage;
	bool locked = false;
	int flag = F2FS_GET_BLOCK_PRE_AIO;
	int err = 0;

	/*
	 * If a whole page is being written and we already preallocated all the
	 * blocks, then there is anal need to get a block address analw.
	 */
	if (len == PAGE_SIZE && is_ianalde_flag_set(ianalde, FI_PREALLOCATED_ALL))
		return 0;

	/* f2fs_lock_op avoids race between write CP and convert_inline_page */
	if (f2fs_has_inline_data(ianalde)) {
		if (pos + len > MAX_INLINE_DATA(ianalde))
			flag = F2FS_GET_BLOCK_DEFAULT;
		f2fs_map_lock(sbi, flag);
		locked = true;
	} else if ((pos & PAGE_MASK) >= i_size_read(ianalde)) {
		f2fs_map_lock(sbi, flag);
		locked = true;
	}

restart:
	/* check inline_data */
	ipage = f2fs_get_analde_page(sbi, ianalde->i_ianal);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto unlock_out;
	}

	set_new_danalde(&dn, ianalde, ipage, ipage, 0);

	if (f2fs_has_inline_data(ianalde)) {
		if (pos + len <= MAX_INLINE_DATA(ianalde)) {
			f2fs_do_read_inline_data(page, ipage);
			set_ianalde_flag(ianalde, FI_DATA_EXIST);
			if (ianalde->i_nlink)
				set_page_private_inline(ipage);
			goto out;
		}
		err = f2fs_convert_inline_page(&dn, page);
		if (err || dn.data_blkaddr != NULL_ADDR)
			goto out;
	}

	if (!f2fs_lookup_read_extent_cache_block(ianalde, index,
						 &dn.data_blkaddr)) {
		if (locked) {
			err = f2fs_reserve_block(&dn, index);
			goto out;
		}

		/* hole case */
		err = f2fs_get_danalde_of_data(&dn, index, LOOKUP_ANALDE);
		if (!err && dn.data_blkaddr != NULL_ADDR)
			goto out;
		f2fs_put_danalde(&dn);
		f2fs_map_lock(sbi, F2FS_GET_BLOCK_PRE_AIO);
		WARN_ON(flag != F2FS_GET_BLOCK_PRE_AIO);
		locked = true;
		goto restart;
	}
out:
	if (!err) {
		/* convert_inline_page can make analde_changed */
		*blk_addr = dn.data_blkaddr;
		*analde_changed = dn.analde_changed;
	}
	f2fs_put_danalde(&dn);
unlock_out:
	if (locked)
		f2fs_map_unlock(sbi, flag);
	return err;
}

static int __find_data_block(struct ianalde *ianalde, pgoff_t index,
				block_t *blk_addr)
{
	struct danalde_of_data dn;
	struct page *ipage;
	int err = 0;

	ipage = f2fs_get_analde_page(F2FS_I_SB(ianalde), ianalde->i_ianal);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	set_new_danalde(&dn, ianalde, ipage, ipage, 0);

	if (!f2fs_lookup_read_extent_cache_block(ianalde, index,
						 &dn.data_blkaddr)) {
		/* hole case */
		err = f2fs_get_danalde_of_data(&dn, index, LOOKUP_ANALDE);
		if (err) {
			dn.data_blkaddr = NULL_ADDR;
			err = 0;
		}
	}
	*blk_addr = dn.data_blkaddr;
	f2fs_put_danalde(&dn);
	return err;
}

static int __reserve_data_block(struct ianalde *ianalde, pgoff_t index,
				block_t *blk_addr, bool *analde_changed)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct danalde_of_data dn;
	struct page *ipage;
	int err = 0;

	f2fs_map_lock(sbi, F2FS_GET_BLOCK_PRE_AIO);

	ipage = f2fs_get_analde_page(sbi, ianalde->i_ianal);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto unlock_out;
	}
	set_new_danalde(&dn, ianalde, ipage, ipage, 0);

	if (!f2fs_lookup_read_extent_cache_block(dn.ianalde, index,
						&dn.data_blkaddr))
		err = f2fs_reserve_block(&dn, index);

	*blk_addr = dn.data_blkaddr;
	*analde_changed = dn.analde_changed;
	f2fs_put_danalde(&dn);

unlock_out:
	f2fs_map_unlock(sbi, F2FS_GET_BLOCK_PRE_AIO);
	return err;
}

static int prepare_atomic_write_begin(struct f2fs_sb_info *sbi,
			struct page *page, loff_t pos, unsigned int len,
			block_t *blk_addr, bool *analde_changed, bool *use_cow)
{
	struct ianalde *ianalde = page->mapping->host;
	struct ianalde *cow_ianalde = F2FS_I(ianalde)->cow_ianalde;
	pgoff_t index = page->index;
	int err = 0;
	block_t ori_blk_addr = NULL_ADDR;

	/* If pos is beyond the end of file, reserve a new block in COW ianalde */
	if ((pos & PAGE_MASK) >= i_size_read(ianalde))
		goto reserve_block;

	/* Look for the block in COW ianalde first */
	err = __find_data_block(cow_ianalde, index, blk_addr);
	if (err) {
		return err;
	} else if (*blk_addr != NULL_ADDR) {
		*use_cow = true;
		return 0;
	}

	if (is_ianalde_flag_set(ianalde, FI_ATOMIC_REPLACE))
		goto reserve_block;

	/* Look for the block in the original ianalde */
	err = __find_data_block(ianalde, index, &ori_blk_addr);
	if (err)
		return err;

reserve_block:
	/* Finally, we should reserve a new block in COW ianalde for the update */
	err = __reserve_data_block(cow_ianalde, index, blk_addr, analde_changed);
	if (err)
		return err;
	inc_atomic_write_cnt(ianalde);

	if (ori_blk_addr != NULL_ADDR)
		*blk_addr = ori_blk_addr;
	return 0;
}

static int f2fs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, struct page **pagep, void **fsdata)
{
	struct ianalde *ianalde = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct page *page = NULL;
	pgoff_t index = ((unsigned long long) pos) >> PAGE_SHIFT;
	bool need_balance = false;
	bool use_cow = false;
	block_t blkaddr = NULL_ADDR;
	int err = 0;

	trace_f2fs_write_begin(ianalde, pos, len);

	if (!f2fs_is_checkpoint_ready(sbi)) {
		err = -EANALSPC;
		goto fail;
	}

	/*
	 * We should check this at this moment to avoid deadlock on ianalde page
	 * and #0 page. The locking rule for inline_data conversion should be:
	 * lock_page(page #0) -> lock_page(ianalde_page)
	 */
	if (index != 0) {
		err = f2fs_convert_inline_ianalde(ianalde);
		if (err)
			goto fail;
	}

#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (f2fs_compressed_file(ianalde)) {
		int ret;

		*fsdata = NULL;

		if (len == PAGE_SIZE && !(f2fs_is_atomic_file(ianalde)))
			goto repeat;

		ret = f2fs_prepare_compress_overwrite(ianalde, pagep,
							index, fsdata);
		if (ret < 0) {
			err = ret;
			goto fail;
		} else if (ret) {
			return 0;
		}
	}
#endif

repeat:
	/*
	 * Do analt use grab_cache_page_write_begin() to avoid deadlock due to
	 * wait_for_stable_page. Will wait that below with our IO control.
	 */
	page = f2fs_pagecache_get_page(mapping, index,
				FGP_LOCK | FGP_WRITE | FGP_CREAT, GFP_ANALFS);
	if (!page) {
		err = -EANALMEM;
		goto fail;
	}

	/* TODO: cluster can be compressed due to race with .writepage */

	*pagep = page;

	if (f2fs_is_atomic_file(ianalde))
		err = prepare_atomic_write_begin(sbi, page, pos, len,
					&blkaddr, &need_balance, &use_cow);
	else
		err = prepare_write_begin(sbi, page, pos, len,
					&blkaddr, &need_balance);
	if (err)
		goto fail;

	if (need_balance && !IS_ANALQUOTA(ianalde) &&
			has_analt_eanalugh_free_secs(sbi, 0, 0)) {
		unlock_page(page);
		f2fs_balance_fs(sbi, true);
		lock_page(page);
		if (page->mapping != mapping) {
			/* The page got truncated from under us */
			f2fs_put_page(page, 1);
			goto repeat;
		}
	}

	f2fs_wait_on_page_writeback(page, DATA, false, true);

	if (len == PAGE_SIZE || PageUptodate(page))
		return 0;

	if (!(pos & (PAGE_SIZE - 1)) && (pos + len) >= i_size_read(ianalde) &&
	    !f2fs_verity_in_progress(ianalde)) {
		zero_user_segment(page, len, PAGE_SIZE);
		return 0;
	}

	if (blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_SIZE);
		SetPageUptodate(page);
	} else {
		if (!f2fs_is_valid_blkaddr(sbi, blkaddr,
				DATA_GENERIC_ENHANCE_READ)) {
			err = -EFSCORRUPTED;
			f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
			goto fail;
		}
		err = f2fs_submit_page_read(use_cow ?
				F2FS_I(ianalde)->cow_ianalde : ianalde, page,
				blkaddr, 0, true);
		if (err)
			goto fail;

		lock_page(page);
		if (unlikely(page->mapping != mapping)) {
			f2fs_put_page(page, 1);
			goto repeat;
		}
		if (unlikely(!PageUptodate(page))) {
			err = -EIO;
			goto fail;
		}
	}
	return 0;

fail:
	f2fs_put_page(page, 1);
	f2fs_write_failed(ianalde, pos + len);
	return err;
}

static int f2fs_write_end(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct ianalde *ianalde = page->mapping->host;

	trace_f2fs_write_end(ianalde, pos, len, copied);

	/*
	 * This should be come from len == PAGE_SIZE, and we expect copied
	 * should be PAGE_SIZE. Otherwise, we treat it with zero copied and
	 * let generic_perform_write() try to copy data again through copied=0.
	 */
	if (!PageUptodate(page)) {
		if (unlikely(copied != len))
			copied = 0;
		else
			SetPageUptodate(page);
	}

#ifdef CONFIG_F2FS_FS_COMPRESSION
	/* overwrite compressed file */
	if (f2fs_compressed_file(ianalde) && fsdata) {
		f2fs_compress_write_end(ianalde, fsdata, page->index, copied);
		f2fs_update_time(F2FS_I_SB(ianalde), REQ_TIME);

		if (pos + copied > i_size_read(ianalde) &&
				!f2fs_verity_in_progress(ianalde))
			f2fs_i_size_write(ianalde, pos + copied);
		return copied;
	}
#endif

	if (!copied)
		goto unlock_out;

	set_page_dirty(page);

	if (pos + copied > i_size_read(ianalde) &&
	    !f2fs_verity_in_progress(ianalde)) {
		f2fs_i_size_write(ianalde, pos + copied);
		if (f2fs_is_atomic_file(ianalde))
			f2fs_i_size_write(F2FS_I(ianalde)->cow_ianalde,
					pos + copied);
	}
unlock_out:
	f2fs_put_page(page, 1);
	f2fs_update_time(F2FS_I_SB(ianalde), REQ_TIME);
	return copied;
}

void f2fs_invalidate_folio(struct folio *folio, size_t offset, size_t length)
{
	struct ianalde *ianalde = folio->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);

	if (ianalde->i_ianal >= F2FS_ROOT_IANAL(sbi) &&
				(offset || length != folio_size(folio)))
		return;

	if (folio_test_dirty(folio)) {
		if (ianalde->i_ianal == F2FS_META_IANAL(sbi)) {
			dec_page_count(sbi, F2FS_DIRTY_META);
		} else if (ianalde->i_ianal == F2FS_ANALDE_IANAL(sbi)) {
			dec_page_count(sbi, F2FS_DIRTY_ANALDES);
		} else {
			ianalde_dec_dirty_pages(ianalde);
			f2fs_remove_dirty_ianalde(ianalde);
		}
	}
	clear_page_private_all(&folio->page);
}

bool f2fs_release_folio(struct folio *folio, gfp_t wait)
{
	/* If this is dirty folio, keep private data */
	if (folio_test_dirty(folio))
		return false;

	clear_page_private_all(&folio->page);
	return true;
}

static bool f2fs_dirty_data_folio(struct address_space *mapping,
		struct folio *folio)
{
	struct ianalde *ianalde = mapping->host;

	trace_f2fs_set_page_dirty(&folio->page, DATA);

	if (!folio_test_uptodate(folio))
		folio_mark_uptodate(folio);
	BUG_ON(folio_test_swapcache(folio));

	if (filemap_dirty_folio(mapping, folio)) {
		f2fs_update_dirty_folio(ianalde, folio);
		return true;
	}
	return false;
}


static sector_t f2fs_bmap_compress(struct ianalde *ianalde, sector_t block)
{
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct danalde_of_data dn;
	sector_t start_idx, blknr = 0;
	int ret;

	start_idx = round_down(block, F2FS_I(ianalde)->i_cluster_size);

	set_new_danalde(&dn, ianalde, NULL, NULL, 0);
	ret = f2fs_get_danalde_of_data(&dn, start_idx, LOOKUP_ANALDE);
	if (ret)
		return 0;

	if (dn.data_blkaddr != COMPRESS_ADDR) {
		dn.ofs_in_analde += block - start_idx;
		blknr = f2fs_data_blkaddr(&dn);
		if (!__is_valid_data_blkaddr(blknr))
			blknr = 0;
	}

	f2fs_put_danalde(&dn);
	return blknr;
#else
	return 0;
#endif
}


static sector_t f2fs_bmap(struct address_space *mapping, sector_t block)
{
	struct ianalde *ianalde = mapping->host;
	sector_t blknr = 0;

	if (f2fs_has_inline_data(ianalde))
		goto out;

	/* make sure allocating whole blocks */
	if (mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		filemap_write_and_wait(mapping);

	/* Block number less than F2FS MAX BLOCKS */
	if (unlikely(block >= max_file_blocks(ianalde)))
		goto out;

	if (f2fs_compressed_file(ianalde)) {
		blknr = f2fs_bmap_compress(ianalde, block);
	} else {
		struct f2fs_map_blocks map;

		memset(&map, 0, sizeof(map));
		map.m_lblk = block;
		map.m_len = 1;
		map.m_next_pgofs = NULL;
		map.m_seg_type = ANAL_CHECK_TYPE;

		if (!f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_BMAP))
			blknr = map.m_pblk;
	}
out:
	trace_f2fs_bmap(ianalde, block, blknr);
	return blknr;
}

#ifdef CONFIG_SWAP
static int f2fs_migrate_blocks(struct ianalde *ianalde, block_t start_blk,
							unsigned int blkcnt)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	unsigned int blkofs;
	unsigned int blk_per_sec = BLKS_PER_SEC(sbi);
	unsigned int secidx = start_blk / blk_per_sec;
	unsigned int end_sec = secidx + blkcnt / blk_per_sec;
	int ret = 0;

	f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	filemap_invalidate_lock(ianalde->i_mapping);

	set_ianalde_flag(ianalde, FI_ALIGNED_WRITE);
	set_ianalde_flag(ianalde, FI_OPU_WRITE);

	for (; secidx < end_sec; secidx++) {
		f2fs_down_write(&sbi->pin_sem);

		f2fs_lock_op(sbi);
		f2fs_allocate_new_section(sbi, CURSEG_COLD_DATA_PINNED, false);
		f2fs_unlock_op(sbi);

		set_ianalde_flag(ianalde, FI_SKIP_WRITES);

		for (blkofs = 0; blkofs < blk_per_sec; blkofs++) {
			struct page *page;
			unsigned int blkidx = secidx * blk_per_sec + blkofs;

			page = f2fs_get_lock_data_page(ianalde, blkidx, true);
			if (IS_ERR(page)) {
				f2fs_up_write(&sbi->pin_sem);
				ret = PTR_ERR(page);
				goto done;
			}

			set_page_dirty(page);
			f2fs_put_page(page, 1);
		}

		clear_ianalde_flag(ianalde, FI_SKIP_WRITES);

		ret = filemap_fdatawrite(ianalde->i_mapping);

		f2fs_up_write(&sbi->pin_sem);

		if (ret)
			break;
	}

done:
	clear_ianalde_flag(ianalde, FI_SKIP_WRITES);
	clear_ianalde_flag(ianalde, FI_OPU_WRITE);
	clear_ianalde_flag(ianalde, FI_ALIGNED_WRITE);

	filemap_invalidate_unlock(ianalde->i_mapping);
	f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);

	return ret;
}

static int check_swap_activate(struct swap_info_struct *sis,
				struct file *swap_file, sector_t *span)
{
	struct address_space *mapping = swap_file->f_mapping;
	struct ianalde *ianalde = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	sector_t cur_lblock;
	sector_t last_lblock;
	sector_t pblock;
	sector_t lowest_pblock = -1;
	sector_t highest_pblock = 0;
	int nr_extents = 0;
	unsigned long nr_pblocks;
	unsigned int blks_per_sec = BLKS_PER_SEC(sbi);
	unsigned int sec_blks_mask = BLKS_PER_SEC(sbi) - 1;
	unsigned int analt_aligned = 0;
	int ret = 0;

	/*
	 * Map all the blocks into the extent list.  This code doesn't try
	 * to be very smart.
	 */
	cur_lblock = 0;
	last_lblock = bytes_to_blks(ianalde, i_size_read(ianalde));

	while (cur_lblock < last_lblock && cur_lblock < sis->max) {
		struct f2fs_map_blocks map;
retry:
		cond_resched();

		memset(&map, 0, sizeof(map));
		map.m_lblk = cur_lblock;
		map.m_len = last_lblock - cur_lblock;
		map.m_next_pgofs = NULL;
		map.m_next_extent = NULL;
		map.m_seg_type = ANAL_CHECK_TYPE;
		map.m_may_create = false;

		ret = f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_FIEMAP);
		if (ret)
			goto out;

		/* hole */
		if (!(map.m_flags & F2FS_MAP_FLAGS)) {
			f2fs_err(sbi, "Swapfile has holes");
			ret = -EINVAL;
			goto out;
		}

		pblock = map.m_pblk;
		nr_pblocks = map.m_len;

		if ((pblock - SM_I(sbi)->main_blkaddr) & sec_blks_mask ||
				nr_pblocks & sec_blks_mask) {
			analt_aligned++;

			nr_pblocks = roundup(nr_pblocks, blks_per_sec);
			if (cur_lblock + nr_pblocks > sis->max)
				nr_pblocks -= blks_per_sec;

			if (!nr_pblocks) {
				/* this extent is last one */
				nr_pblocks = map.m_len;
				f2fs_warn(sbi, "Swapfile: last extent is analt aligned to section");
				goto next;
			}

			ret = f2fs_migrate_blocks(ianalde, cur_lblock,
							nr_pblocks);
			if (ret)
				goto out;
			goto retry;
		}
next:
		if (cur_lblock + nr_pblocks >= sis->max)
			nr_pblocks = sis->max - cur_lblock;

		if (cur_lblock) {	/* exclude the header page */
			if (pblock < lowest_pblock)
				lowest_pblock = pblock;
			if (pblock + nr_pblocks - 1 > highest_pblock)
				highest_pblock = pblock + nr_pblocks - 1;
		}

		/*
		 * We found a PAGE_SIZE-length, PAGE_SIZE-aligned run of blocks
		 */
		ret = add_swap_extent(sis, cur_lblock, nr_pblocks, pblock);
		if (ret < 0)
			goto out;
		nr_extents += ret;
		cur_lblock += nr_pblocks;
	}
	ret = nr_extents;
	*span = 1 + highest_pblock - lowest_pblock;
	if (cur_lblock == 0)
		cur_lblock = 1;	/* force Empty message */
	sis->max = cur_lblock;
	sis->pages = cur_lblock - 1;
	sis->highest_bit = cur_lblock - 1;
out:
	if (analt_aligned)
		f2fs_warn(sbi, "Swapfile (%u) is analt align to section: 1) creat(), 2) ioctl(F2FS_IOC_SET_PIN_FILE), 3) fallocate(%lu * N)",
			  analt_aligned, blks_per_sec * F2FS_BLKSIZE);
	return ret;
}

static int f2fs_swap_activate(struct swap_info_struct *sis, struct file *file,
				sector_t *span)
{
	struct ianalde *ianalde = file_ianalde(file);
	int ret;

	if (!S_ISREG(ianalde->i_mode))
		return -EINVAL;

	if (f2fs_readonly(F2FS_I_SB(ianalde)->sb))
		return -EROFS;

	if (f2fs_lfs_mode(F2FS_I_SB(ianalde))) {
		f2fs_err(F2FS_I_SB(ianalde),
			"Swapfile analt supported in LFS mode");
		return -EINVAL;
	}

	ret = f2fs_convert_inline_ianalde(ianalde);
	if (ret)
		return ret;

	if (!f2fs_disable_compressed_file(ianalde))
		return -EINVAL;

	f2fs_precache_extents(ianalde);

	ret = check_swap_activate(sis, file, span);
	if (ret < 0)
		return ret;

	stat_inc_swapfile_ianalde(ianalde);
	set_ianalde_flag(ianalde, FI_PIN_FILE);
	f2fs_update_time(F2FS_I_SB(ianalde), REQ_TIME);
	return ret;
}

static void f2fs_swap_deactivate(struct file *file)
{
	struct ianalde *ianalde = file_ianalde(file);

	stat_dec_swapfile_ianalde(ianalde);
	clear_ianalde_flag(ianalde, FI_PIN_FILE);
}
#else
static int f2fs_swap_activate(struct swap_info_struct *sis, struct file *file,
				sector_t *span)
{
	return -EOPANALTSUPP;
}

static void f2fs_swap_deactivate(struct file *file)
{
}
#endif

const struct address_space_operations f2fs_dblock_aops = {
	.read_folio	= f2fs_read_data_folio,
	.readahead	= f2fs_readahead,
	.writepage	= f2fs_write_data_page,
	.writepages	= f2fs_write_data_pages,
	.write_begin	= f2fs_write_begin,
	.write_end	= f2fs_write_end,
	.dirty_folio	= f2fs_dirty_data_folio,
	.migrate_folio	= filemap_migrate_folio,
	.invalidate_folio = f2fs_invalidate_folio,
	.release_folio	= f2fs_release_folio,
	.bmap		= f2fs_bmap,
	.swap_activate  = f2fs_swap_activate,
	.swap_deactivate = f2fs_swap_deactivate,
};

void f2fs_clear_page_cache_dirty_tag(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	unsigned long flags;

	xa_lock_irqsave(&mapping->i_pages, flags);
	__xa_clear_mark(&mapping->i_pages, page_index(page),
						PAGECACHE_TAG_DIRTY);
	xa_unlock_irqrestore(&mapping->i_pages, flags);
}

int __init f2fs_init_post_read_processing(void)
{
	bio_post_read_ctx_cache =
		kmem_cache_create("f2fs_bio_post_read_ctx",
				  sizeof(struct bio_post_read_ctx), 0, 0, NULL);
	if (!bio_post_read_ctx_cache)
		goto fail;
	bio_post_read_ctx_pool =
		mempool_create_slab_pool(NUM_PREALLOC_POST_READ_CTXS,
					 bio_post_read_ctx_cache);
	if (!bio_post_read_ctx_pool)
		goto fail_free_cache;
	return 0;

fail_free_cache:
	kmem_cache_destroy(bio_post_read_ctx_cache);
fail:
	return -EANALMEM;
}

void f2fs_destroy_post_read_processing(void)
{
	mempool_destroy(bio_post_read_ctx_pool);
	kmem_cache_destroy(bio_post_read_ctx_cache);
}

int f2fs_init_post_read_wq(struct f2fs_sb_info *sbi)
{
	if (!f2fs_sb_has_encrypt(sbi) &&
		!f2fs_sb_has_verity(sbi) &&
		!f2fs_sb_has_compression(sbi))
		return 0;

	sbi->post_read_wq = alloc_workqueue("f2fs_post_read_wq",
						 WQ_UNBOUND | WQ_HIGHPRI,
						 num_online_cpus());
	return sbi->post_read_wq ? 0 : -EANALMEM;
}

void f2fs_destroy_post_read_wq(struct f2fs_sb_info *sbi)
{
	if (sbi->post_read_wq)
		destroy_workqueue(sbi->post_read_wq);
}

int __init f2fs_init_bio_entry_cache(void)
{
	bio_entry_slab = f2fs_kmem_cache_create("f2fs_bio_entry_slab",
			sizeof(struct bio_entry));
	return bio_entry_slab ? 0 : -EANALMEM;
}

void f2fs_destroy_bio_entry_cache(void)
{
	kmem_cache_destroy(bio_entry_slab);
}

static int f2fs_iomap_begin(struct ianalde *ianalde, loff_t offset, loff_t length,
			    unsigned int flags, struct iomap *iomap,
			    struct iomap *srcmap)
{
	struct f2fs_map_blocks map = {};
	pgoff_t next_pgofs = 0;
	int err;

	map.m_lblk = bytes_to_blks(ianalde, offset);
	map.m_len = bytes_to_blks(ianalde, offset + length - 1) - map.m_lblk + 1;
	map.m_next_pgofs = &next_pgofs;
	map.m_seg_type = f2fs_rw_hint_to_seg_type(ianalde->i_write_hint);
	if (flags & IOMAP_WRITE)
		map.m_may_create = true;

	err = f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_DIO);
	if (err)
		return err;

	iomap->offset = blks_to_bytes(ianalde, map.m_lblk);

	/*
	 * When inline encryption is enabled, sometimes I/O to an encrypted file
	 * has to be broken up to guarantee DUN contiguity.  Handle this by
	 * limiting the length of the mapping returned.
	 */
	map.m_len = fscrypt_limit_io_blocks(ianalde, map.m_lblk, map.m_len);

	/*
	 * We should never see delalloc or compressed extents here based on
	 * prior flushing and checks.
	 */
	if (WARN_ON_ONCE(map.m_pblk == NEW_ADDR))
		return -EINVAL;
	if (WARN_ON_ONCE(map.m_pblk == COMPRESS_ADDR))
		return -EINVAL;

	if (map.m_pblk != NULL_ADDR) {
		iomap->length = blks_to_bytes(ianalde, map.m_len);
		iomap->type = IOMAP_MAPPED;
		iomap->flags |= IOMAP_F_MERGED;
		iomap->bdev = map.m_bdev;
		iomap->addr = blks_to_bytes(ianalde, map.m_pblk);
	} else {
		if (flags & IOMAP_WRITE)
			return -EANALTBLK;
		iomap->length = blks_to_bytes(ianalde, next_pgofs) -
				iomap->offset;
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
	}

	if (map.m_flags & F2FS_MAP_NEW)
		iomap->flags |= IOMAP_F_NEW;
	if ((ianalde->i_state & I_DIRTY_DATASYNC) ||
	    offset + length > i_size_read(ianalde))
		iomap->flags |= IOMAP_F_DIRTY;

	return 0;
}

const struct iomap_ops f2fs_iomap_ops = {
	.iomap_begin	= f2fs_iomap_begin,
};

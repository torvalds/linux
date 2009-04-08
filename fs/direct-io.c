/*
 * fs/direct-io.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * O_DIRECT
 *
 * 04Jul2002	Andrew Morton
 *		Initial version
 * 11Sep2002	janetinc@us.ibm.com
 * 		added readv/writev support.
 * 29Oct2002	Andrew Morton
 *		rewrote bio_add_page() support.
 * 30Oct2002	pbadari@us.ibm.com
 *		added support for non-aligned IO.
 * 06Nov2002	pbadari@us.ibm.com
 *		added asynchronous IO support.
 * 21Jul2003	nathans@sgi.com
 *		added IO completion notifier.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/bio.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/rwsem.h>
#include <linux/uio.h>
#include <asm/atomic.h>

/*
 * How many user pages to map in one call to get_user_pages().  This determines
 * the size of a structure on the stack.
 */
#define DIO_PAGES	64

/*
 * This code generally works in units of "dio_blocks".  A dio_block is
 * somewhere between the hard sector size and the filesystem block size.  it
 * is determined on a per-invocation basis.   When talking to the filesystem
 * we need to convert dio_blocks to fs_blocks by scaling the dio_block quantity
 * down by dio->blkfactor.  Similarly, fs-blocksize quantities are converted
 * to bio_block quantities by shifting left by blkfactor.
 *
 * If blkfactor is zero then the user's request was aligned to the filesystem's
 * blocksize.
 *
 * lock_type is DIO_LOCKING for regular files on direct-IO-naive filesystems.
 * This determines whether we need to do the fancy locking which prevents
 * direct-IO from being able to read uninitialised disk blocks.  If its zero
 * (blockdev) this locking is not done, and if it is DIO_OWN_LOCKING i_mutex is
 * not held for the entire direct write (taken briefly, initially, during a
 * direct read though, but its never held for the duration of a direct-IO).
 */

struct dio {
	/* BIO submission state */
	struct bio *bio;		/* bio under assembly */
	struct inode *inode;
	int rw;
	loff_t i_size;			/* i_size when submitted */
	int lock_type;			/* doesn't change */
	unsigned blkbits;		/* doesn't change */
	unsigned blkfactor;		/* When we're using an alignment which
					   is finer than the filesystem's soft
					   blocksize, this specifies how much
					   finer.  blkfactor=2 means 1/4-block
					   alignment.  Does not change */
	unsigned start_zero_done;	/* flag: sub-blocksize zeroing has
					   been performed at the start of a
					   write */
	int pages_in_io;		/* approximate total IO pages */
	size_t	size;			/* total request size (doesn't change)*/
	sector_t block_in_file;		/* Current offset into the underlying
					   file in dio_block units. */
	unsigned blocks_available;	/* At block_in_file.  changes */
	sector_t final_block_in_request;/* doesn't change */
	unsigned first_block_in_page;	/* doesn't change, Used only once */
	int boundary;			/* prev block is at a boundary */
	int reap_counter;		/* rate limit reaping */
	get_block_t *get_block;		/* block mapping function */
	dio_iodone_t *end_io;		/* IO completion function */
	sector_t final_block_in_bio;	/* current final block in bio + 1 */
	sector_t next_block_for_io;	/* next block to be put under IO,
					   in dio_blocks units */
	struct buffer_head map_bh;	/* last get_block() result */

	/*
	 * Deferred addition of a page to the dio.  These variables are
	 * private to dio_send_cur_page(), submit_page_section() and
	 * dio_bio_add_page().
	 */
	struct page *cur_page;		/* The page */
	unsigned cur_page_offset;	/* Offset into it, in bytes */
	unsigned cur_page_len;		/* Nr of bytes at cur_page_offset */
	sector_t cur_page_block;	/* Where it starts */

	/*
	 * Page fetching state. These variables belong to dio_refill_pages().
	 */
	int curr_page;			/* changes */
	int total_pages;		/* doesn't change */
	unsigned long curr_user_address;/* changes */

	/*
	 * Page queue.  These variables belong to dio_refill_pages() and
	 * dio_get_page().
	 */
	struct page *pages[DIO_PAGES];	/* page buffer */
	unsigned head;			/* next page to process */
	unsigned tail;			/* last valid page + 1 */
	int page_errors;		/* errno from get_user_pages() */

	/* BIO completion state */
	spinlock_t bio_lock;		/* protects BIO fields below */
	unsigned long refcount;		/* direct_io_worker() and bios */
	struct bio *bio_list;		/* singly linked via bi_private */
	struct task_struct *waiter;	/* waiting task (NULL if none) */

	/* AIO related stuff */
	struct kiocb *iocb;		/* kiocb */
	int is_async;			/* is IO async ? */
	int io_error;			/* IO error in completion path */
	ssize_t result;                 /* IO result */
};

/*
 * How many pages are in the queue?
 */
static inline unsigned dio_pages_present(struct dio *dio)
{
	return dio->tail - dio->head;
}

/*
 * Go grab and pin some userspace pages.   Typically we'll get 64 at a time.
 */
static int dio_refill_pages(struct dio *dio)
{
	int ret;
	int nr_pages;

	nr_pages = min(dio->total_pages - dio->curr_page, DIO_PAGES);
	ret = get_user_pages_fast(
		dio->curr_user_address,		/* Where from? */
		nr_pages,			/* How many pages? */
		dio->rw == READ,		/* Write to memory? */
		&dio->pages[0]);		/* Put results here */

	if (ret < 0 && dio->blocks_available && (dio->rw & WRITE)) {
		struct page *page = ZERO_PAGE(0);
		/*
		 * A memory fault, but the filesystem has some outstanding
		 * mapped blocks.  We need to use those blocks up to avoid
		 * leaking stale data in the file.
		 */
		if (dio->page_errors == 0)
			dio->page_errors = ret;
		page_cache_get(page);
		dio->pages[0] = page;
		dio->head = 0;
		dio->tail = 1;
		ret = 0;
		goto out;
	}

	if (ret >= 0) {
		dio->curr_user_address += ret * PAGE_SIZE;
		dio->curr_page += ret;
		dio->head = 0;
		dio->tail = ret;
		ret = 0;
	}
out:
	return ret;	
}

/*
 * Get another userspace page.  Returns an ERR_PTR on error.  Pages are
 * buffered inside the dio so that we can call get_user_pages() against a
 * decent number of pages, less frequently.  To provide nicer use of the
 * L1 cache.
 */
static struct page *dio_get_page(struct dio *dio)
{
	if (dio_pages_present(dio) == 0) {
		int ret;

		ret = dio_refill_pages(dio);
		if (ret)
			return ERR_PTR(ret);
		BUG_ON(dio_pages_present(dio) == 0);
	}
	return dio->pages[dio->head++];
}

/**
 * dio_complete() - called when all DIO BIO I/O has been completed
 * @offset: the byte offset in the file of the completed operation
 *
 * This releases locks as dictated by the locking type, lets interested parties
 * know that a DIO operation has completed, and calculates the resulting return
 * code for the operation.
 *
 * It lets the filesystem know if it registered an interest earlier via
 * get_block.  Pass the private field of the map buffer_head so that
 * filesystems can use it to hold additional state between get_block calls and
 * dio_complete.
 */
static int dio_complete(struct dio *dio, loff_t offset, int ret)
{
	ssize_t transferred = 0;

	/*
	 * AIO submission can race with bio completion to get here while
	 * expecting to have the last io completed by bio completion.
	 * In that case -EIOCBQUEUED is in fact not an error we want
	 * to preserve through this call.
	 */
	if (ret == -EIOCBQUEUED)
		ret = 0;

	if (dio->result) {
		transferred = dio->result;

		/* Check for short read case */
		if ((dio->rw == READ) && ((offset + transferred) > dio->i_size))
			transferred = dio->i_size - offset;
	}

	if (dio->end_io && dio->result)
		dio->end_io(dio->iocb, offset, transferred,
			    dio->map_bh.b_private);
	if (dio->lock_type == DIO_LOCKING)
		/* lockdep: non-owner release */
		up_read_non_owner(&dio->inode->i_alloc_sem);

	if (ret == 0)
		ret = dio->page_errors;
	if (ret == 0)
		ret = dio->io_error;
	if (ret == 0)
		ret = transferred;

	return ret;
}

static int dio_bio_complete(struct dio *dio, struct bio *bio);
/*
 * Asynchronous IO callback. 
 */
static void dio_bio_end_aio(struct bio *bio, int error)
{
	struct dio *dio = bio->bi_private;
	unsigned long remaining;
	unsigned long flags;

	/* cleanup the bio */
	dio_bio_complete(dio, bio);

	spin_lock_irqsave(&dio->bio_lock, flags);
	remaining = --dio->refcount;
	if (remaining == 1 && dio->waiter)
		wake_up_process(dio->waiter);
	spin_unlock_irqrestore(&dio->bio_lock, flags);

	if (remaining == 0) {
		int ret = dio_complete(dio, dio->iocb->ki_pos, 0);
		aio_complete(dio->iocb, ret, 0);
		kfree(dio);
	}
}

/*
 * The BIO completion handler simply queues the BIO up for the process-context
 * handler.
 *
 * During I/O bi_private points at the dio.  After I/O, bi_private is used to
 * implement a singly-linked list of completed BIOs, at dio->bio_list.
 */
static void dio_bio_end_io(struct bio *bio, int error)
{
	struct dio *dio = bio->bi_private;
	unsigned long flags;

	spin_lock_irqsave(&dio->bio_lock, flags);
	bio->bi_private = dio->bio_list;
	dio->bio_list = bio;
	if (--dio->refcount == 1 && dio->waiter)
		wake_up_process(dio->waiter);
	spin_unlock_irqrestore(&dio->bio_lock, flags);
}

static int
dio_bio_alloc(struct dio *dio, struct block_device *bdev,
		sector_t first_sector, int nr_vecs)
{
	struct bio *bio;

	bio = bio_alloc(GFP_KERNEL, nr_vecs);
	if (bio == NULL)
		return -ENOMEM;

	bio->bi_bdev = bdev;
	bio->bi_sector = first_sector;
	if (dio->is_async)
		bio->bi_end_io = dio_bio_end_aio;
	else
		bio->bi_end_io = dio_bio_end_io;

	dio->bio = bio;
	return 0;
}

/*
 * In the AIO read case we speculatively dirty the pages before starting IO.
 * During IO completion, any of these pages which happen to have been written
 * back will be redirtied by bio_check_pages_dirty().
 *
 * bios hold a dio reference between submit_bio and ->end_io.
 */
static void dio_bio_submit(struct dio *dio)
{
	struct bio *bio = dio->bio;
	unsigned long flags;

	bio->bi_private = dio;

	spin_lock_irqsave(&dio->bio_lock, flags);
	dio->refcount++;
	spin_unlock_irqrestore(&dio->bio_lock, flags);

	if (dio->is_async && dio->rw == READ)
		bio_set_pages_dirty(bio);

	submit_bio(dio->rw, bio);

	dio->bio = NULL;
	dio->boundary = 0;
}

/*
 * Release any resources in case of a failure
 */
static void dio_cleanup(struct dio *dio)
{
	while (dio_pages_present(dio))
		page_cache_release(dio_get_page(dio));
}

/*
 * Wait for the next BIO to complete.  Remove it and return it.  NULL is
 * returned once all BIOs have been completed.  This must only be called once
 * all bios have been issued so that dio->refcount can only decrease.  This
 * requires that that the caller hold a reference on the dio.
 */
static struct bio *dio_await_one(struct dio *dio)
{
	unsigned long flags;
	struct bio *bio = NULL;

	spin_lock_irqsave(&dio->bio_lock, flags);

	/*
	 * Wait as long as the list is empty and there are bios in flight.  bio
	 * completion drops the count, maybe adds to the list, and wakes while
	 * holding the bio_lock so we don't need set_current_state()'s barrier
	 * and can call it after testing our condition.
	 */
	while (dio->refcount > 1 && dio->bio_list == NULL) {
		__set_current_state(TASK_UNINTERRUPTIBLE);
		dio->waiter = current;
		spin_unlock_irqrestore(&dio->bio_lock, flags);
		io_schedule();
		/* wake up sets us TASK_RUNNING */
		spin_lock_irqsave(&dio->bio_lock, flags);
		dio->waiter = NULL;
	}
	if (dio->bio_list) {
		bio = dio->bio_list;
		dio->bio_list = bio->bi_private;
	}
	spin_unlock_irqrestore(&dio->bio_lock, flags);
	return bio;
}

/*
 * Process one completed BIO.  No locks are held.
 */
static int dio_bio_complete(struct dio *dio, struct bio *bio)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec;
	int page_no;

	if (!uptodate)
		dio->io_error = -EIO;

	if (dio->is_async && dio->rw == READ) {
		bio_check_pages_dirty(bio);	/* transfers ownership */
	} else {
		for (page_no = 0; page_no < bio->bi_vcnt; page_no++) {
			struct page *page = bvec[page_no].bv_page;

			if (dio->rw == READ && !PageCompound(page))
				set_page_dirty_lock(page);
			page_cache_release(page);
		}
		bio_put(bio);
	}
	return uptodate ? 0 : -EIO;
}

/*
 * Wait on and process all in-flight BIOs.  This must only be called once
 * all bios have been issued so that the refcount can only decrease.
 * This just waits for all bios to make it through dio_bio_complete.  IO
 * errors are propagated through dio->io_error and should be propagated via
 * dio_complete().
 */
static void dio_await_completion(struct dio *dio)
{
	struct bio *bio;
	do {
		bio = dio_await_one(dio);
		if (bio)
			dio_bio_complete(dio, bio);
	} while (bio);
}

/*
 * A really large O_DIRECT read or write can generate a lot of BIOs.  So
 * to keep the memory consumption sane we periodically reap any completed BIOs
 * during the BIO generation phase.
 *
 * This also helps to limit the peak amount of pinned userspace memory.
 */
static int dio_bio_reap(struct dio *dio)
{
	int ret = 0;

	if (dio->reap_counter++ >= 64) {
		while (dio->bio_list) {
			unsigned long flags;
			struct bio *bio;
			int ret2;

			spin_lock_irqsave(&dio->bio_lock, flags);
			bio = dio->bio_list;
			dio->bio_list = bio->bi_private;
			spin_unlock_irqrestore(&dio->bio_lock, flags);
			ret2 = dio_bio_complete(dio, bio);
			if (ret == 0)
				ret = ret2;
		}
		dio->reap_counter = 0;
	}
	return ret;
}

/*
 * Call into the fs to map some more disk blocks.  We record the current number
 * of available blocks at dio->blocks_available.  These are in units of the
 * fs blocksize, (1 << inode->i_blkbits).
 *
 * The fs is allowed to map lots of blocks at once.  If it wants to do that,
 * it uses the passed inode-relative block number as the file offset, as usual.
 *
 * get_block() is passed the number of i_blkbits-sized blocks which direct_io
 * has remaining to do.  The fs should not map more than this number of blocks.
 *
 * If the fs has mapped a lot of blocks, it should populate bh->b_size to
 * indicate how much contiguous disk space has been made available at
 * bh->b_blocknr.
 *
 * If *any* of the mapped blocks are new, then the fs must set buffer_new().
 * This isn't very efficient...
 *
 * In the case of filesystem holes: the fs may return an arbitrarily-large
 * hole by returning an appropriate value in b_size and by clearing
 * buffer_mapped().  However the direct-io code will only process holes one
 * block at a time - it will repeatedly call get_block() as it walks the hole.
 */
static int get_more_blocks(struct dio *dio)
{
	int ret;
	struct buffer_head *map_bh = &dio->map_bh;
	sector_t fs_startblk;	/* Into file, in filesystem-sized blocks */
	unsigned long fs_count;	/* Number of filesystem-sized blocks */
	unsigned long dio_count;/* Number of dio_block-sized blocks */
	unsigned long blkmask;
	int create;

	/*
	 * If there was a memory error and we've overwritten all the
	 * mapped blocks then we can now return that memory error
	 */
	ret = dio->page_errors;
	if (ret == 0) {
		BUG_ON(dio->block_in_file >= dio->final_block_in_request);
		fs_startblk = dio->block_in_file >> dio->blkfactor;
		dio_count = dio->final_block_in_request - dio->block_in_file;
		fs_count = dio_count >> dio->blkfactor;
		blkmask = (1 << dio->blkfactor) - 1;
		if (dio_count & blkmask)	
			fs_count++;

		map_bh->b_state = 0;
		map_bh->b_size = fs_count << dio->inode->i_blkbits;

		create = dio->rw & WRITE;
		if (dio->lock_type == DIO_LOCKING) {
			if (dio->block_in_file < (i_size_read(dio->inode) >>
							dio->blkbits))
				create = 0;
		} else if (dio->lock_type == DIO_NO_LOCKING) {
			create = 0;
		}

		/*
		 * For writes inside i_size we forbid block creations: only
		 * overwrites are permitted.  We fall back to buffered writes
		 * at a higher level for inside-i_size block-instantiating
		 * writes.
		 */
		ret = (*dio->get_block)(dio->inode, fs_startblk,
						map_bh, create);
	}
	return ret;
}

/*
 * There is no bio.  Make one now.
 */
static int dio_new_bio(struct dio *dio, sector_t start_sector)
{
	sector_t sector;
	int ret, nr_pages;

	ret = dio_bio_reap(dio);
	if (ret)
		goto out;
	sector = start_sector << (dio->blkbits - 9);
	nr_pages = min(dio->pages_in_io, bio_get_nr_vecs(dio->map_bh.b_bdev));
	BUG_ON(nr_pages <= 0);
	ret = dio_bio_alloc(dio, dio->map_bh.b_bdev, sector, nr_pages);
	dio->boundary = 0;
out:
	return ret;
}

/*
 * Attempt to put the current chunk of 'cur_page' into the current BIO.  If
 * that was successful then update final_block_in_bio and take a ref against
 * the just-added page.
 *
 * Return zero on success.  Non-zero means the caller needs to start a new BIO.
 */
static int dio_bio_add_page(struct dio *dio)
{
	int ret;

	ret = bio_add_page(dio->bio, dio->cur_page,
			dio->cur_page_len, dio->cur_page_offset);
	if (ret == dio->cur_page_len) {
		/*
		 * Decrement count only, if we are done with this page
		 */
		if ((dio->cur_page_len + dio->cur_page_offset) == PAGE_SIZE)
			dio->pages_in_io--;
		page_cache_get(dio->cur_page);
		dio->final_block_in_bio = dio->cur_page_block +
			(dio->cur_page_len >> dio->blkbits);
		ret = 0;
	} else {
		ret = 1;
	}
	return ret;
}
		
/*
 * Put cur_page under IO.  The section of cur_page which is described by
 * cur_page_offset,cur_page_len is put into a BIO.  The section of cur_page
 * starts on-disk at cur_page_block.
 *
 * We take a ref against the page here (on behalf of its presence in the bio).
 *
 * The caller of this function is responsible for removing cur_page from the
 * dio, and for dropping the refcount which came from that presence.
 */
static int dio_send_cur_page(struct dio *dio)
{
	int ret = 0;

	if (dio->bio) {
		/*
		 * See whether this new request is contiguous with the old
		 */
		if (dio->final_block_in_bio != dio->cur_page_block)
			dio_bio_submit(dio);
		/*
		 * Submit now if the underlying fs is about to perform a
		 * metadata read
		 */
		if (dio->boundary)
			dio_bio_submit(dio);
	}

	if (dio->bio == NULL) {
		ret = dio_new_bio(dio, dio->cur_page_block);
		if (ret)
			goto out;
	}

	if (dio_bio_add_page(dio) != 0) {
		dio_bio_submit(dio);
		ret = dio_new_bio(dio, dio->cur_page_block);
		if (ret == 0) {
			ret = dio_bio_add_page(dio);
			BUG_ON(ret != 0);
		}
	}
out:
	return ret;
}

/*
 * An autonomous function to put a chunk of a page under deferred IO.
 *
 * The caller doesn't actually know (or care) whether this piece of page is in
 * a BIO, or is under IO or whatever.  We just take care of all possible 
 * situations here.  The separation between the logic of do_direct_IO() and
 * that of submit_page_section() is important for clarity.  Please don't break.
 *
 * The chunk of page starts on-disk at blocknr.
 *
 * We perform deferred IO, by recording the last-submitted page inside our
 * private part of the dio structure.  If possible, we just expand the IO
 * across that page here.
 *
 * If that doesn't work out then we put the old page into the bio and add this
 * page to the dio instead.
 */
static int
submit_page_section(struct dio *dio, struct page *page,
		unsigned offset, unsigned len, sector_t blocknr)
{
	int ret = 0;

	if (dio->rw & WRITE) {
		/*
		 * Read accounting is performed in submit_bio()
		 */
		task_io_account_write(len);
	}

	/*
	 * Can we just grow the current page's presence in the dio?
	 */
	if (	(dio->cur_page == page) &&
		(dio->cur_page_offset + dio->cur_page_len == offset) &&
		(dio->cur_page_block +
			(dio->cur_page_len >> dio->blkbits) == blocknr)) {
		dio->cur_page_len += len;

		/*
		 * If dio->boundary then we want to schedule the IO now to
		 * avoid metadata seeks.
		 */
		if (dio->boundary) {
			ret = dio_send_cur_page(dio);
			page_cache_release(dio->cur_page);
			dio->cur_page = NULL;
		}
		goto out;
	}

	/*
	 * If there's a deferred page already there then send it.
	 */
	if (dio->cur_page) {
		ret = dio_send_cur_page(dio);
		page_cache_release(dio->cur_page);
		dio->cur_page = NULL;
		if (ret)
			goto out;
	}

	page_cache_get(page);		/* It is in dio */
	dio->cur_page = page;
	dio->cur_page_offset = offset;
	dio->cur_page_len = len;
	dio->cur_page_block = blocknr;
out:
	return ret;
}

/*
 * Clean any dirty buffers in the blockdev mapping which alias newly-created
 * file blocks.  Only called for S_ISREG files - blockdevs do not set
 * buffer_new
 */
static void clean_blockdev_aliases(struct dio *dio)
{
	unsigned i;
	unsigned nblocks;

	nblocks = dio->map_bh.b_size >> dio->inode->i_blkbits;

	for (i = 0; i < nblocks; i++) {
		unmap_underlying_metadata(dio->map_bh.b_bdev,
					dio->map_bh.b_blocknr + i);
	}
}

/*
 * If we are not writing the entire block and get_block() allocated
 * the block for us, we need to fill-in the unused portion of the
 * block with zeros. This happens only if user-buffer, fileoffset or
 * io length is not filesystem block-size multiple.
 *
 * `end' is zero if we're doing the start of the IO, 1 at the end of the
 * IO.
 */
static void dio_zero_block(struct dio *dio, int end)
{
	unsigned dio_blocks_per_fs_block;
	unsigned this_chunk_blocks;	/* In dio_blocks */
	unsigned this_chunk_bytes;
	struct page *page;

	dio->start_zero_done = 1;
	if (!dio->blkfactor || !buffer_new(&dio->map_bh))
		return;

	dio_blocks_per_fs_block = 1 << dio->blkfactor;
	this_chunk_blocks = dio->block_in_file & (dio_blocks_per_fs_block - 1);

	if (!this_chunk_blocks)
		return;

	/*
	 * We need to zero out part of an fs block.  It is either at the
	 * beginning or the end of the fs block.
	 */
	if (end) 
		this_chunk_blocks = dio_blocks_per_fs_block - this_chunk_blocks;

	this_chunk_bytes = this_chunk_blocks << dio->blkbits;

	page = ZERO_PAGE(0);
	if (submit_page_section(dio, page, 0, this_chunk_bytes, 
				dio->next_block_for_io))
		return;

	dio->next_block_for_io += this_chunk_blocks;
}

/*
 * Walk the user pages, and the file, mapping blocks to disk and generating
 * a sequence of (page,offset,len,block) mappings.  These mappings are injected
 * into submit_page_section(), which takes care of the next stage of submission
 *
 * Direct IO against a blockdev is different from a file.  Because we can
 * happily perform page-sized but 512-byte aligned IOs.  It is important that
 * blockdev IO be able to have fine alignment and large sizes.
 *
 * So what we do is to permit the ->get_block function to populate bh.b_size
 * with the size of IO which is permitted at this offset and this i_blkbits.
 *
 * For best results, the blockdev should be set up with 512-byte i_blkbits and
 * it should set b_size to PAGE_SIZE or more inside get_block().  This gives
 * fine alignment but still allows this function to work in PAGE_SIZE units.
 */
static int do_direct_IO(struct dio *dio)
{
	const unsigned blkbits = dio->blkbits;
	const unsigned blocks_per_page = PAGE_SIZE >> blkbits;
	struct page *page;
	unsigned block_in_page;
	struct buffer_head *map_bh = &dio->map_bh;
	int ret = 0;

	/* The I/O can start at any block offset within the first page */
	block_in_page = dio->first_block_in_page;

	while (dio->block_in_file < dio->final_block_in_request) {
		page = dio_get_page(dio);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}

		while (block_in_page < blocks_per_page) {
			unsigned offset_in_page = block_in_page << blkbits;
			unsigned this_chunk_bytes;	/* # of bytes mapped */
			unsigned this_chunk_blocks;	/* # of blocks */
			unsigned u;

			if (dio->blocks_available == 0) {
				/*
				 * Need to go and map some more disk
				 */
				unsigned long blkmask;
				unsigned long dio_remainder;

				ret = get_more_blocks(dio);
				if (ret) {
					page_cache_release(page);
					goto out;
				}
				if (!buffer_mapped(map_bh))
					goto do_holes;

				dio->blocks_available =
						map_bh->b_size >> dio->blkbits;
				dio->next_block_for_io =
					map_bh->b_blocknr << dio->blkfactor;
				if (buffer_new(map_bh))
					clean_blockdev_aliases(dio);

				if (!dio->blkfactor)
					goto do_holes;

				blkmask = (1 << dio->blkfactor) - 1;
				dio_remainder = (dio->block_in_file & blkmask);

				/*
				 * If we are at the start of IO and that IO
				 * starts partway into a fs-block,
				 * dio_remainder will be non-zero.  If the IO
				 * is a read then we can simply advance the IO
				 * cursor to the first block which is to be
				 * read.  But if the IO is a write and the
				 * block was newly allocated we cannot do that;
				 * the start of the fs block must be zeroed out
				 * on-disk
				 */
				if (!buffer_new(map_bh))
					dio->next_block_for_io += dio_remainder;
				dio->blocks_available -= dio_remainder;
			}
do_holes:
			/* Handle holes */
			if (!buffer_mapped(map_bh)) {
				loff_t i_size_aligned;

				/* AKPM: eargh, -ENOTBLK is a hack */
				if (dio->rw & WRITE) {
					page_cache_release(page);
					return -ENOTBLK;
				}

				/*
				 * Be sure to account for a partial block as the
				 * last block in the file
				 */
				i_size_aligned = ALIGN(i_size_read(dio->inode),
							1 << blkbits);
				if (dio->block_in_file >=
						i_size_aligned >> blkbits) {
					/* We hit eof */
					page_cache_release(page);
					goto out;
				}
				zero_user(page, block_in_page << blkbits,
						1 << blkbits);
				dio->block_in_file++;
				block_in_page++;
				goto next_block;
			}

			/*
			 * If we're performing IO which has an alignment which
			 * is finer than the underlying fs, go check to see if
			 * we must zero out the start of this block.
			 */
			if (unlikely(dio->blkfactor && !dio->start_zero_done))
				dio_zero_block(dio, 0);

			/*
			 * Work out, in this_chunk_blocks, how much disk we
			 * can add to this page
			 */
			this_chunk_blocks = dio->blocks_available;
			u = (PAGE_SIZE - offset_in_page) >> blkbits;
			if (this_chunk_blocks > u)
				this_chunk_blocks = u;
			u = dio->final_block_in_request - dio->block_in_file;
			if (this_chunk_blocks > u)
				this_chunk_blocks = u;
			this_chunk_bytes = this_chunk_blocks << blkbits;
			BUG_ON(this_chunk_bytes == 0);

			dio->boundary = buffer_boundary(map_bh);
			ret = submit_page_section(dio, page, offset_in_page,
				this_chunk_bytes, dio->next_block_for_io);
			if (ret) {
				page_cache_release(page);
				goto out;
			}
			dio->next_block_for_io += this_chunk_blocks;

			dio->block_in_file += this_chunk_blocks;
			block_in_page += this_chunk_blocks;
			dio->blocks_available -= this_chunk_blocks;
next_block:
			BUG_ON(dio->block_in_file > dio->final_block_in_request);
			if (dio->block_in_file == dio->final_block_in_request)
				break;
		}

		/* Drop the ref which was taken in get_user_pages() */
		page_cache_release(page);
		block_in_page = 0;
	}
out:
	return ret;
}

/*
 * Releases both i_mutex and i_alloc_sem
 */
static ssize_t
direct_io_worker(int rw, struct kiocb *iocb, struct inode *inode, 
	const struct iovec *iov, loff_t offset, unsigned long nr_segs, 
	unsigned blkbits, get_block_t get_block, dio_iodone_t end_io,
	struct dio *dio)
{
	unsigned long user_addr; 
	unsigned long flags;
	int seg;
	ssize_t ret = 0;
	ssize_t ret2;
	size_t bytes;

	dio->inode = inode;
	dio->rw = rw;
	dio->blkbits = blkbits;
	dio->blkfactor = inode->i_blkbits - blkbits;
	dio->block_in_file = offset >> blkbits;

	dio->get_block = get_block;
	dio->end_io = end_io;
	dio->final_block_in_bio = -1;
	dio->next_block_for_io = -1;

	dio->iocb = iocb;
	dio->i_size = i_size_read(inode);

	spin_lock_init(&dio->bio_lock);
	dio->refcount = 1;

	/*
	 * In case of non-aligned buffers, we may need 2 more
	 * pages since we need to zero out first and last block.
	 */
	if (unlikely(dio->blkfactor))
		dio->pages_in_io = 2;

	for (seg = 0; seg < nr_segs; seg++) {
		user_addr = (unsigned long)iov[seg].iov_base;
		dio->pages_in_io +=
			((user_addr+iov[seg].iov_len +PAGE_SIZE-1)/PAGE_SIZE
				- user_addr/PAGE_SIZE);
	}

	for (seg = 0; seg < nr_segs; seg++) {
		user_addr = (unsigned long)iov[seg].iov_base;
		dio->size += bytes = iov[seg].iov_len;

		/* Index into the first page of the first block */
		dio->first_block_in_page = (user_addr & ~PAGE_MASK) >> blkbits;
		dio->final_block_in_request = dio->block_in_file +
						(bytes >> blkbits);
		/* Page fetching state */
		dio->head = 0;
		dio->tail = 0;
		dio->curr_page = 0;

		dio->total_pages = 0;
		if (user_addr & (PAGE_SIZE-1)) {
			dio->total_pages++;
			bytes -= PAGE_SIZE - (user_addr & (PAGE_SIZE - 1));
		}
		dio->total_pages += (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
		dio->curr_user_address = user_addr;
	
		ret = do_direct_IO(dio);

		dio->result += iov[seg].iov_len -
			((dio->final_block_in_request - dio->block_in_file) <<
					blkbits);

		if (ret) {
			dio_cleanup(dio);
			break;
		}
	} /* end iovec loop */

	if (ret == -ENOTBLK && (rw & WRITE)) {
		/*
		 * The remaining part of the request will be
		 * be handled by buffered I/O when we return
		 */
		ret = 0;
	}
	/*
	 * There may be some unwritten disk at the end of a part-written
	 * fs-block-sized block.  Go zero that now.
	 */
	dio_zero_block(dio, 1);

	if (dio->cur_page) {
		ret2 = dio_send_cur_page(dio);
		if (ret == 0)
			ret = ret2;
		page_cache_release(dio->cur_page);
		dio->cur_page = NULL;
	}
	if (dio->bio)
		dio_bio_submit(dio);

	/* All IO is now issued, send it on its way */
	blk_run_address_space(inode->i_mapping);

	/*
	 * It is possible that, we return short IO due to end of file.
	 * In that case, we need to release all the pages we got hold on.
	 */
	dio_cleanup(dio);

	/*
	 * All block lookups have been performed. For READ requests
	 * we can let i_mutex go now that its achieved its purpose
	 * of protecting us from looking up uninitialized blocks.
	 */
	if ((rw == READ) && (dio->lock_type == DIO_LOCKING))
		mutex_unlock(&dio->inode->i_mutex);

	/*
	 * The only time we want to leave bios in flight is when a successful
	 * partial aio read or full aio write have been setup.  In that case
	 * bio completion will call aio_complete.  The only time it's safe to
	 * call aio_complete is when we return -EIOCBQUEUED, so we key on that.
	 * This had *better* be the only place that raises -EIOCBQUEUED.
	 */
	BUG_ON(ret == -EIOCBQUEUED);
	if (dio->is_async && ret == 0 && dio->result &&
	    ((rw & READ) || (dio->result == dio->size)))
		ret = -EIOCBQUEUED;

	if (ret != -EIOCBQUEUED)
		dio_await_completion(dio);

	/*
	 * Sync will always be dropping the final ref and completing the
	 * operation.  AIO can if it was a broken operation described above or
	 * in fact if all the bios race to complete before we get here.  In
	 * that case dio_complete() translates the EIOCBQUEUED into the proper
	 * return code that the caller will hand to aio_complete().
	 *
	 * This is managed by the bio_lock instead of being an atomic_t so that
	 * completion paths can drop their ref and use the remaining count to
	 * decide to wake the submission path atomically.
	 */
	spin_lock_irqsave(&dio->bio_lock, flags);
	ret2 = --dio->refcount;
	spin_unlock_irqrestore(&dio->bio_lock, flags);

	if (ret2 == 0) {
		ret = dio_complete(dio, offset, ret);
		kfree(dio);
	} else
		BUG_ON(ret != -EIOCBQUEUED);

	return ret;
}

/*
 * This is a library function for use by filesystem drivers.
 * The locking rules are governed by the dio_lock_type parameter.
 *
 * DIO_NO_LOCKING (no locking, for raw block device access)
 * For writes, i_mutex is not held on entry; it is never taken.
 *
 * DIO_LOCKING (simple locking for regular files)
 * For writes we are called under i_mutex and return with i_mutex held, even
 * though it is internally dropped.
 * For reads, i_mutex is not held on entry, but it is taken and dropped before
 * returning.
 *
 * DIO_OWN_LOCKING (filesystem provides synchronisation and handling of
 *	uninitialised data, allowing parallel direct readers and writers)
 * For writes we are called without i_mutex, return without it, never touch it.
 * For reads we are called under i_mutex and return with i_mutex held, even
 * though it may be internally dropped.
 *
 * Additional i_alloc_sem locking requirements described inline below.
 */
ssize_t
__blockdev_direct_IO(int rw, struct kiocb *iocb, struct inode *inode,
	struct block_device *bdev, const struct iovec *iov, loff_t offset, 
	unsigned long nr_segs, get_block_t get_block, dio_iodone_t end_io,
	int dio_lock_type)
{
	int seg;
	size_t size;
	unsigned long addr;
	unsigned blkbits = inode->i_blkbits;
	unsigned bdev_blkbits = 0;
	unsigned blocksize_mask = (1 << blkbits) - 1;
	ssize_t retval = -EINVAL;
	loff_t end = offset;
	struct dio *dio;
	int release_i_mutex = 0;
	int acquire_i_mutex = 0;

	if (rw & WRITE)
		rw = WRITE_ODIRECT;

	if (bdev)
		bdev_blkbits = blksize_bits(bdev_hardsect_size(bdev));

	if (offset & blocksize_mask) {
		if (bdev)
			 blkbits = bdev_blkbits;
		blocksize_mask = (1 << blkbits) - 1;
		if (offset & blocksize_mask)
			goto out;
	}

	/* Check the memory alignment.  Blocks cannot straddle pages */
	for (seg = 0; seg < nr_segs; seg++) {
		addr = (unsigned long)iov[seg].iov_base;
		size = iov[seg].iov_len;
		end += size;
		if ((addr & blocksize_mask) || (size & blocksize_mask))  {
			if (bdev)
				 blkbits = bdev_blkbits;
			blocksize_mask = (1 << blkbits) - 1;
			if ((addr & blocksize_mask) || (size & blocksize_mask))  
				goto out;
		}
	}

	dio = kzalloc(sizeof(*dio), GFP_KERNEL);
	retval = -ENOMEM;
	if (!dio)
		goto out;

	/*
	 * For block device access DIO_NO_LOCKING is used,
	 *	neither readers nor writers do any locking at all
	 * For regular files using DIO_LOCKING,
	 *	readers need to grab i_mutex and i_alloc_sem
	 *	writers need to grab i_alloc_sem only (i_mutex is already held)
	 * For regular files using DIO_OWN_LOCKING,
	 *	neither readers nor writers take any locks here
	 */
	dio->lock_type = dio_lock_type;
	if (dio_lock_type != DIO_NO_LOCKING) {
		/* watch out for a 0 len io from a tricksy fs */
		if (rw == READ && end > offset) {
			struct address_space *mapping;

			mapping = iocb->ki_filp->f_mapping;
			if (dio_lock_type != DIO_OWN_LOCKING) {
				mutex_lock(&inode->i_mutex);
				release_i_mutex = 1;
			}

			retval = filemap_write_and_wait_range(mapping, offset,
							      end - 1);
			if (retval) {
				kfree(dio);
				goto out;
			}

			if (dio_lock_type == DIO_OWN_LOCKING) {
				mutex_unlock(&inode->i_mutex);
				acquire_i_mutex = 1;
			}
		}

		if (dio_lock_type == DIO_LOCKING)
			/* lockdep: not the owner will release it */
			down_read_non_owner(&inode->i_alloc_sem);
	}

	/*
	 * For file extending writes updating i_size before data
	 * writeouts complete can expose uninitialized blocks. So
	 * even for AIO, we need to wait for i/o to complete before
	 * returning in this case.
	 */
	dio->is_async = !is_sync_kiocb(iocb) && !((rw & WRITE) &&
		(end > i_size_read(inode)));

	retval = direct_io_worker(rw, iocb, inode, iov, offset,
				nr_segs, blkbits, get_block, end_io, dio);

	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again for DIO_LOCKING.
	 * NOTE: DIO_NO_LOCK/DIO_OWN_LOCK callers have to handle this by
	 * it's own meaner.
	 */
	if (unlikely(retval < 0 && (rw & WRITE))) {
		loff_t isize = i_size_read(inode);

		if (end > isize && dio_lock_type == DIO_LOCKING)
			vmtruncate(inode, isize);
	}

	if (rw == READ && dio_lock_type == DIO_LOCKING)
		release_i_mutex = 0;

out:
	if (release_i_mutex)
		mutex_unlock(&inode->i_mutex);
	else if (acquire_i_mutex)
		mutex_lock(&inode->i_mutex);
	return retval;
}
EXPORT_SYMBOL(__blockdev_direct_IO);

// SPDX-License-Identifier: GPL-2.0
/*
 * Data verification functions, i.e. hooks for ->readahead()
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <linux/bio.h>
#include <linux/export.h>

#define FS_VERITY_MAX_PENDING_BLOCKS 2

struct fsverity_pending_block {
	const void *data;
	u64 pos;
	u8 real_hash[FS_VERITY_MAX_DIGEST_SIZE];
};

struct fsverity_verification_context {
	struct inode *inode;
	struct fsverity_info *vi;
	unsigned long max_ra_pages;

	/*
	 * This is the queue of data blocks that are pending verification.  When
	 * the crypto layer supports interleaved hashing, we allow multiple
	 * blocks to be queued up in order to utilize it.  This can improve
	 * performance significantly vs. sequential hashing of each block.
	 */
	int num_pending;
	int max_pending;
	struct fsverity_pending_block
		pending_blocks[FS_VERITY_MAX_PENDING_BLOCKS];
};

static struct workqueue_struct *fsverity_read_workqueue;

/*
 * Returns true if the hash block with index @hblock_idx in the tree, located in
 * @hpage, has already been verified.
 */
static bool is_hash_block_verified(struct fsverity_info *vi, struct page *hpage,
				   unsigned long hblock_idx)
{
	unsigned int blocks_per_page;
	unsigned int i;

	/*
	 * When the Merkle tree block size and page size are the same, then the
	 * ->hash_block_verified bitmap isn't allocated, and we use PG_checked
	 * to directly indicate whether the page's block has been verified.
	 *
	 * Using PG_checked also guarantees that we re-verify hash pages that
	 * get evicted and re-instantiated from the backing storage, as new
	 * pages always start out with PG_checked cleared.
	 */
	if (!vi->hash_block_verified)
		return PageChecked(hpage);

	/*
	 * When the Merkle tree block size and page size differ, we use a bitmap
	 * to indicate whether each hash block has been verified.
	 *
	 * However, we still need to ensure that hash pages that get evicted and
	 * re-instantiated from the backing storage are re-verified.  To do
	 * this, we use PG_checked again, but now it doesn't really mean
	 * "checked".  Instead, now it just serves as an indicator for whether
	 * the hash page is newly instantiated or not.  If the page is new, as
	 * indicated by PG_checked=0, we clear the bitmap bits for the page's
	 * blocks since they are untrustworthy, then set PG_checked=1.
	 * Otherwise we return the bitmap bit for the requested block.
	 *
	 * Multiple threads may execute this code concurrently on the same page.
	 * This is safe because we use memory barriers to ensure that if a
	 * thread sees PG_checked=1, then it also sees the associated bitmap
	 * clearing to have occurred.  Also, all writes and their corresponding
	 * reads are atomic, and all writes are safe to repeat in the event that
	 * multiple threads get into the PG_checked=0 section.  (Clearing a
	 * bitmap bit again at worst causes a hash block to be verified
	 * redundantly.  That event should be very rare, so it's not worth using
	 * a lock to avoid.  Setting PG_checked again has no effect.)
	 */
	if (PageChecked(hpage)) {
		/*
		 * A read memory barrier is needed here to give ACQUIRE
		 * semantics to the above PageChecked() test.
		 */
		smp_rmb();
		return test_bit(hblock_idx, vi->hash_block_verified);
	}
	blocks_per_page = vi->tree_params.blocks_per_page;
	hblock_idx = round_down(hblock_idx, blocks_per_page);
	for (i = 0; i < blocks_per_page; i++)
		clear_bit(hblock_idx + i, vi->hash_block_verified);
	/*
	 * A write memory barrier is needed here to give RELEASE semantics to
	 * the below SetPageChecked() operation.
	 */
	smp_wmb();
	SetPageChecked(hpage);
	return false;
}

/*
 * Verify the hash of a single data block against the file's Merkle tree.
 *
 * In principle, we need to verify the entire path to the root node.  However,
 * for efficiency the filesystem may cache the hash blocks.  Therefore we need
 * only ascend the tree until an already-verified hash block is seen, and then
 * verify the path to that block.
 *
 * Return: %true if the data block is valid, else %false.
 */
static bool verify_data_block(struct inode *inode, struct fsverity_info *vi,
			      const struct fsverity_pending_block *dblock,
			      unsigned long max_ra_pages)
{
	const u64 data_pos = dblock->pos;
	const struct merkle_tree_params *params = &vi->tree_params;
	const unsigned int hsize = params->digest_size;
	int level;
	u8 _want_hash[FS_VERITY_MAX_DIGEST_SIZE];
	const u8 *want_hash;
	u8 real_hash[FS_VERITY_MAX_DIGEST_SIZE];
	/* The hash blocks that are traversed, indexed by level */
	struct {
		/* Page containing the hash block */
		struct page *page;
		/* Mapped address of the hash block (will be within @page) */
		const void *addr;
		/* Index of the hash block in the tree overall */
		unsigned long index;
		/* Byte offset of the wanted hash relative to @addr */
		unsigned int hoffset;
	} hblocks[FS_VERITY_MAX_LEVELS];
	/*
	 * The index of the previous level's block within that level; also the
	 * index of that block's hash within the current level.
	 */
	u64 hidx = data_pos >> params->log_blocksize;

	/*
	 * Up to FS_VERITY_MAX_PENDING_BLOCKS + FS_VERITY_MAX_LEVELS pages may
	 * be mapped at once.
	 */
	static_assert(FS_VERITY_MAX_PENDING_BLOCKS + FS_VERITY_MAX_LEVELS <=
		      KM_MAX_IDX);

	if (unlikely(data_pos >= inode->i_size)) {
		/*
		 * This can happen in the data page spanning EOF when the Merkle
		 * tree block size is less than the page size.  The Merkle tree
		 * doesn't cover data blocks fully past EOF.  But the entire
		 * page spanning EOF can be visible to userspace via a mmap, and
		 * any part past EOF should be all zeroes.  Therefore, we need
		 * to verify that any data blocks fully past EOF are all zeroes.
		 */
		if (memchr_inv(dblock->data, 0, params->block_size)) {
			fsverity_err(inode,
				     "FILE CORRUPTED!  Data past EOF is not zeroed");
			return false;
		}
		return true;
	}

	/*
	 * Starting at the leaf level, ascend the tree saving hash blocks along
	 * the way until we find a hash block that has already been verified, or
	 * until we reach the root.
	 */
	for (level = 0; level < params->num_levels; level++) {
		unsigned long next_hidx;
		unsigned long hblock_idx;
		pgoff_t hpage_idx;
		unsigned int hblock_offset_in_page;
		unsigned int hoffset;
		struct page *hpage;
		const void *haddr;

		/*
		 * The index of the block in the current level; also the index
		 * of that block's hash within the next level.
		 */
		next_hidx = hidx >> params->log_arity;

		/* Index of the hash block in the tree overall */
		hblock_idx = params->level_start[level] + next_hidx;

		/* Index of the hash page in the tree overall */
		hpage_idx = hblock_idx >> params->log_blocks_per_page;

		/* Byte offset of the hash block within the page */
		hblock_offset_in_page =
			(hblock_idx << params->log_blocksize) & ~PAGE_MASK;

		/* Byte offset of the hash within the block */
		hoffset = (hidx << params->log_digestsize) &
			  (params->block_size - 1);

		hpage = inode->i_sb->s_vop->read_merkle_tree_page(inode,
				hpage_idx, level == 0 ? min(max_ra_pages,
					params->tree_pages - hpage_idx) : 0);
		if (IS_ERR(hpage)) {
			fsverity_err(inode,
				     "Error %ld reading Merkle tree page %lu",
				     PTR_ERR(hpage), hpage_idx);
			goto error;
		}
		haddr = kmap_local_page(hpage) + hblock_offset_in_page;
		if (is_hash_block_verified(vi, hpage, hblock_idx)) {
			memcpy(_want_hash, haddr + hoffset, hsize);
			want_hash = _want_hash;
			kunmap_local(haddr);
			put_page(hpage);
			goto descend;
		}
		hblocks[level].page = hpage;
		hblocks[level].addr = haddr;
		hblocks[level].index = hblock_idx;
		hblocks[level].hoffset = hoffset;
		hidx = next_hidx;
	}

	want_hash = vi->root_hash;
descend:
	/* Descend the tree verifying hash blocks. */
	for (; level > 0; level--) {
		struct page *hpage = hblocks[level - 1].page;
		const void *haddr = hblocks[level - 1].addr;
		unsigned long hblock_idx = hblocks[level - 1].index;
		unsigned int hoffset = hblocks[level - 1].hoffset;

		fsverity_hash_block(params, haddr, real_hash);
		if (memcmp(want_hash, real_hash, hsize) != 0)
			goto corrupted;
		/*
		 * Mark the hash block as verified.  This must be atomic and
		 * idempotent, as the same hash block might be verified by
		 * multiple threads concurrently.
		 */
		if (vi->hash_block_verified)
			set_bit(hblock_idx, vi->hash_block_verified);
		else
			SetPageChecked(hpage);
		memcpy(_want_hash, haddr + hoffset, hsize);
		want_hash = _want_hash;
		kunmap_local(haddr);
		put_page(hpage);
	}

	/* Finally, verify the hash of the data block. */
	if (memcmp(want_hash, dblock->real_hash, hsize) != 0)
		goto corrupted;
	return true;

corrupted:
	fsverity_err(
		inode,
		"FILE CORRUPTED! pos=%llu, level=%d, want_hash=%s:%*phN, real_hash=%s:%*phN",
		data_pos, level - 1, params->hash_alg->name, hsize, want_hash,
		params->hash_alg->name, hsize,
		level == 0 ? dblock->real_hash : real_hash);
error:
	for (; level > 0; level--) {
		kunmap_local(hblocks[level - 1].addr);
		put_page(hblocks[level - 1].page);
	}
	return false;
}

static void
fsverity_init_verification_context(struct fsverity_verification_context *ctx,
				   struct inode *inode,
				   unsigned long max_ra_pages)
{
	struct fsverity_info *vi = *fsverity_info_addr(inode);

	ctx->inode = inode;
	ctx->vi = vi;
	ctx->max_ra_pages = max_ra_pages;
	ctx->num_pending = 0;
	if (vi->tree_params.hash_alg->algo_id == HASH_ALGO_SHA256 &&
	    sha256_finup_2x_is_optimized())
		ctx->max_pending = 2;
	else
		ctx->max_pending = 1;
}

static void
fsverity_clear_pending_blocks(struct fsverity_verification_context *ctx)
{
	int i;

	for (i = ctx->num_pending - 1; i >= 0; i--) {
		kunmap_local(ctx->pending_blocks[i].data);
		ctx->pending_blocks[i].data = NULL;
	}
	ctx->num_pending = 0;
}

static bool
fsverity_verify_pending_blocks(struct fsverity_verification_context *ctx)
{
	struct fsverity_info *vi = ctx->vi;
	const struct merkle_tree_params *params = &vi->tree_params;
	int i;

	if (ctx->num_pending == 2) {
		/* num_pending == 2 implies that the algorithm is SHA-256 */
		sha256_finup_2x(params->hashstate ? &params->hashstate->sha256 :
						    NULL,
				ctx->pending_blocks[0].data,
				ctx->pending_blocks[1].data, params->block_size,
				ctx->pending_blocks[0].real_hash,
				ctx->pending_blocks[1].real_hash);
	} else {
		for (i = 0; i < ctx->num_pending; i++)
			fsverity_hash_block(params, ctx->pending_blocks[i].data,
					    ctx->pending_blocks[i].real_hash);
	}

	for (i = 0; i < ctx->num_pending; i++) {
		if (!verify_data_block(ctx->inode, vi, &ctx->pending_blocks[i],
				       ctx->max_ra_pages))
			return false;
	}
	fsverity_clear_pending_blocks(ctx);
	return true;
}

static bool fsverity_add_data_blocks(struct fsverity_verification_context *ctx,
				     struct folio *data_folio, size_t len,
				     size_t offset)
{
	struct fsverity_info *vi = ctx->vi;
	const struct merkle_tree_params *params = &vi->tree_params;
	const unsigned int block_size = params->block_size;
	u64 pos = (u64)data_folio->index << PAGE_SHIFT;

	if (WARN_ON_ONCE(len <= 0 || !IS_ALIGNED(len | offset, block_size)))
		return false;
	if (WARN_ON_ONCE(!folio_test_locked(data_folio) ||
			 folio_test_uptodate(data_folio)))
		return false;
	do {
		ctx->pending_blocks[ctx->num_pending].data =
			kmap_local_folio(data_folio, offset);
		ctx->pending_blocks[ctx->num_pending].pos = pos + offset;
		if (++ctx->num_pending == ctx->max_pending &&
		    !fsverity_verify_pending_blocks(ctx))
			return false;
		offset += block_size;
		len -= block_size;
	} while (len);
	return true;
}

/**
 * fsverity_verify_blocks() - verify data in a folio
 * @folio: the folio containing the data to verify
 * @len: the length of the data to verify in the folio
 * @offset: the offset of the data to verify in the folio
 *
 * Verify data that has just been read from a verity file.  The data must be
 * located in a pagecache folio that is still locked and not yet uptodate.  The
 * length and offset of the data must be Merkle tree block size aligned.
 *
 * Return: %true if the data is valid, else %false.
 */
bool fsverity_verify_blocks(struct folio *folio, size_t len, size_t offset)
{
	struct fsverity_verification_context ctx;

	fsverity_init_verification_context(&ctx, folio->mapping->host, 0);

	if (fsverity_add_data_blocks(&ctx, folio, len, offset) &&
	    fsverity_verify_pending_blocks(&ctx))
		return true;
	fsverity_clear_pending_blocks(&ctx);
	return false;
}
EXPORT_SYMBOL_GPL(fsverity_verify_blocks);

#ifdef CONFIG_BLOCK
/**
 * fsverity_verify_bio() - verify a 'read' bio that has just completed
 * @bio: the bio to verify
 *
 * Verify the bio's data against the file's Merkle tree.  All bio data segments
 * must be aligned to the file's Merkle tree block size.  If any data fails
 * verification, then bio->bi_status is set to an error status.
 *
 * This is a helper function for use by the ->readahead() method of filesystems
 * that issue bios to read data directly into the page cache.  Filesystems that
 * populate the page cache without issuing bios (e.g. non block-based
 * filesystems) must instead call fsverity_verify_page() directly on each page.
 * All filesystems must also call fsverity_verify_page() on holes.
 */
void fsverity_verify_bio(struct bio *bio)
{
	struct inode *inode = bio_first_folio_all(bio)->mapping->host;
	struct fsverity_verification_context ctx;
	struct folio_iter fi;
	unsigned long max_ra_pages = 0;

	if (bio->bi_opf & REQ_RAHEAD) {
		/*
		 * If this bio is for data readahead, then we also do readahead
		 * of the first (largest) level of the Merkle tree.  Namely,
		 * when a Merkle tree page is read, we also try to piggy-back on
		 * some additional pages -- up to 1/4 the number of data pages.
		 *
		 * This improves sequential read performance, as it greatly
		 * reduces the number of I/O requests made to the Merkle tree.
		 */
		max_ra_pages = bio->bi_iter.bi_size >> (PAGE_SHIFT + 2);
	}

	fsverity_init_verification_context(&ctx, inode, max_ra_pages);

	bio_for_each_folio_all(fi, bio) {
		if (!fsverity_add_data_blocks(&ctx, fi.folio, fi.length,
					      fi.offset))
			goto ioerr;
	}

	if (!fsverity_verify_pending_blocks(&ctx))
		goto ioerr;
	return;

ioerr:
	fsverity_clear_pending_blocks(&ctx);
	bio->bi_status = BLK_STS_IOERR;
}
EXPORT_SYMBOL_GPL(fsverity_verify_bio);
#endif /* CONFIG_BLOCK */

/**
 * fsverity_enqueue_verify_work() - enqueue work on the fs-verity workqueue
 * @work: the work to enqueue
 *
 * Enqueue verification work for asynchronous processing.
 */
void fsverity_enqueue_verify_work(struct work_struct *work)
{
	queue_work(fsverity_read_workqueue, work);
}
EXPORT_SYMBOL_GPL(fsverity_enqueue_verify_work);

void __init fsverity_init_workqueue(void)
{
	/*
	 * Use a high-priority workqueue to prioritize verification work, which
	 * blocks reads from completing, over regular application tasks.
	 *
	 * For performance reasons, don't use an unbound workqueue.  Using an
	 * unbound workqueue for crypto operations causes excessive scheduler
	 * latency on ARM64.
	 */
	fsverity_read_workqueue = alloc_workqueue("fsverity_read_queue",
						  WQ_HIGHPRI | WQ_PERCPU,
						  num_online_cpus());
	if (!fsverity_read_workqueue)
		panic("failed to allocate fsverity_read_queue");
}

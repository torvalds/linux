// SPDX-License-Identifier: GPL-2.0
/*
 * Data verification functions, i.e. hooks for ->readahead()
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <crypto/hash.h>
#include <linux/bio.h>
#include <linux/ratelimit.h>

static struct workqueue_struct *fsverity_read_workqueue;

/**
 * hash_at_level() - compute the location of the block's hash at the given level
 *
 * @params:	(in) the Merkle tree parameters
 * @dindex:	(in) the index of the data block being verified
 * @level:	(in) the level of hash we want (0 is leaf level)
 * @hindex:	(out) the index of the hash block containing the wanted hash
 * @hoffset:	(out) the byte offset to the wanted hash within the hash block
 */
static void hash_at_level(const struct merkle_tree_params *params,
			  pgoff_t dindex, unsigned int level, pgoff_t *hindex,
			  unsigned int *hoffset)
{
	pgoff_t position;

	/* Offset of the hash within the level's region, in hashes */
	position = dindex >> (level * params->log_arity);

	/* Index of the hash block in the tree overall */
	*hindex = params->level_start[level] + (position >> params->log_arity);

	/* Offset of the wanted hash (in bytes) within the hash block */
	*hoffset = (position & ((1 << params->log_arity) - 1)) <<
		   (params->log_blocksize - params->log_arity);
}

/* Extract a hash from a hash page */
static void extract_hash(struct page *hpage, unsigned int hoffset,
			 unsigned int hsize, u8 *out)
{
	void *virt = kmap_atomic(hpage);

	memcpy(out, virt + hoffset, hsize);
	kunmap_atomic(virt);
}

static inline int cmp_hashes(const struct fsverity_info *vi,
			     const u8 *want_hash, const u8 *real_hash,
			     pgoff_t index, int level)
{
	const unsigned int hsize = vi->tree_params.digest_size;

	if (memcmp(want_hash, real_hash, hsize) == 0)
		return 0;

	fsverity_err(vi->inode,
		     "FILE CORRUPTED! index=%lu, level=%d, want_hash=%s:%*phN, real_hash=%s:%*phN",
		     index, level,
		     vi->tree_params.hash_alg->name, hsize, want_hash,
		     vi->tree_params.hash_alg->name, hsize, real_hash);
	return -EBADMSG;
}

/*
 * Verify a single data page against the file's Merkle tree.
 *
 * In principle, we need to verify the entire path to the root node.  However,
 * for efficiency the filesystem may cache the hash pages.  Therefore we need
 * only ascend the tree until an already-verified page is seen, as indicated by
 * the PageChecked bit being set; then verify the path to that page.
 *
 * This code currently only supports the case where the verity block size is
 * equal to PAGE_SIZE.  Doing otherwise would be possible but tricky, since we
 * wouldn't be able to use the PageChecked bit.
 *
 * Note that multiple processes may race to verify a hash page and mark it
 * Checked, but it doesn't matter; the result will be the same either way.
 *
 * Return: true if the page is valid, else false.
 */
static bool verify_page(struct inode *inode, const struct fsverity_info *vi,
			struct ahash_request *req, struct page *data_page,
			unsigned long level0_ra_pages)
{
	const struct merkle_tree_params *params = &vi->tree_params;
	const unsigned int hsize = params->digest_size;
	const pgoff_t index = data_page->index;
	int level;
	u8 _want_hash[FS_VERITY_MAX_DIGEST_SIZE];
	const u8 *want_hash;
	u8 real_hash[FS_VERITY_MAX_DIGEST_SIZE];
	struct page *hpages[FS_VERITY_MAX_LEVELS];
	unsigned int hoffsets[FS_VERITY_MAX_LEVELS];
	int err;

	if (WARN_ON_ONCE(!PageLocked(data_page) || PageUptodate(data_page)))
		return false;

	pr_debug_ratelimited("Verifying data page %lu...\n", index);

	/*
	 * Starting at the leaf level, ascend the tree saving hash pages along
	 * the way until we find a verified hash page, indicated by PageChecked;
	 * or until we reach the root.
	 */
	for (level = 0; level < params->num_levels; level++) {
		pgoff_t hindex;
		unsigned int hoffset;
		struct page *hpage;

		hash_at_level(params, index, level, &hindex, &hoffset);

		pr_debug_ratelimited("Level %d: hindex=%lu, hoffset=%u\n",
				     level, hindex, hoffset);

		hpage = inode->i_sb->s_vop->read_merkle_tree_page(inode, hindex,
				level == 0 ? level0_ra_pages : 0);
		if (IS_ERR(hpage)) {
			err = PTR_ERR(hpage);
			fsverity_err(inode,
				     "Error %d reading Merkle tree page %lu",
				     err, hindex);
			goto out;
		}

		if (PageChecked(hpage)) {
			extract_hash(hpage, hoffset, hsize, _want_hash);
			want_hash = _want_hash;
			put_page(hpage);
			pr_debug_ratelimited("Hash page already checked, want %s:%*phN\n",
					     params->hash_alg->name,
					     hsize, want_hash);
			goto descend;
		}
		pr_debug_ratelimited("Hash page not yet checked\n");
		hpages[level] = hpage;
		hoffsets[level] = hoffset;
	}

	want_hash = vi->root_hash;
	pr_debug("Want root hash: %s:%*phN\n",
		 params->hash_alg->name, hsize, want_hash);
descend:
	/* Descend the tree verifying hash pages */
	for (; level > 0; level--) {
		struct page *hpage = hpages[level - 1];
		unsigned int hoffset = hoffsets[level - 1];

		err = fsverity_hash_page(params, inode, req, hpage, real_hash);
		if (err)
			goto out;
		err = cmp_hashes(vi, want_hash, real_hash, index, level - 1);
		if (err)
			goto out;
		SetPageChecked(hpage);
		extract_hash(hpage, hoffset, hsize, _want_hash);
		want_hash = _want_hash;
		put_page(hpage);
		pr_debug("Verified hash page at level %d, now want %s:%*phN\n",
			 level - 1, params->hash_alg->name, hsize, want_hash);
	}

	/* Finally, verify the data page */
	err = fsverity_hash_page(params, inode, req, data_page, real_hash);
	if (err)
		goto out;
	err = cmp_hashes(vi, want_hash, real_hash, index, -1);
out:
	for (; level > 0; level--)
		put_page(hpages[level - 1]);

	return err == 0;
}

/**
 * fsverity_verify_page() - verify a data page
 * @page: the page to verity
 *
 * Verify a page that has just been read from a verity file.  The page must be a
 * pagecache page that is still locked and not yet uptodate.
 *
 * Return: true if the page is valid, else false.
 */
bool fsverity_verify_page(struct page *page)
{
	struct inode *inode = page->mapping->host;
	const struct fsverity_info *vi = inode->i_verity_info;
	struct ahash_request *req;
	bool valid;

	/* This allocation never fails, since it's mempool-backed. */
	req = fsverity_alloc_hash_request(vi->tree_params.hash_alg, GFP_NOFS);

	valid = verify_page(inode, vi, req, page, 0);

	fsverity_free_hash_request(vi->tree_params.hash_alg, req);

	return valid;
}
EXPORT_SYMBOL_GPL(fsverity_verify_page);

#ifdef CONFIG_BLOCK
/**
 * fsverity_verify_bio() - verify a 'read' bio that has just completed
 * @bio: the bio to verify
 *
 * Verify a set of pages that have just been read from a verity file.  The pages
 * must be pagecache pages that are still locked and not yet uptodate.  Pages
 * that fail verification are set to the Error state.  Verification is skipped
 * for pages already in the Error state, e.g. due to fscrypt decryption failure.
 *
 * This is a helper function for use by the ->readahead() method of filesystems
 * that issue bios to read data directly into the page cache.  Filesystems that
 * populate the page cache without issuing bios (e.g. non block-based
 * filesystems) must instead call fsverity_verify_page() directly on each page.
 * All filesystems must also call fsverity_verify_page() on holes.
 */
void fsverity_verify_bio(struct bio *bio)
{
	struct inode *inode = bio_first_page_all(bio)->mapping->host;
	const struct fsverity_info *vi = inode->i_verity_info;
	const struct merkle_tree_params *params = &vi->tree_params;
	struct ahash_request *req;
	struct bio_vec *bv;
	struct bvec_iter_all iter_all;
	unsigned long max_ra_pages = 0;

	/* This allocation never fails, since it's mempool-backed. */
	req = fsverity_alloc_hash_request(params->hash_alg, GFP_NOFS);

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
		bio_for_each_segment_all(bv, bio, iter_all)
			max_ra_pages++;
		max_ra_pages /= 4;
	}

	bio_for_each_segment_all(bv, bio, iter_all) {
		struct page *page = bv->bv_page;
		unsigned long level0_index = page->index >> params->log_arity;
		unsigned long level0_ra_pages =
			min(max_ra_pages, params->level0_blocks - level0_index);

		if (!PageError(page) &&
		    !verify_page(inode, vi, req, page, level0_ra_pages))
			SetPageError(page);
	}

	fsverity_free_hash_request(params->hash_alg, req);
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

int __init fsverity_init_workqueue(void)
{
	/*
	 * Use an unbound workqueue to allow bios to be verified in parallel
	 * even when they happen to complete on the same CPU.  This sacrifices
	 * locality, but it's worthwhile since hashing is CPU-intensive.
	 *
	 * Also use a high-priority workqueue to prioritize verification work,
	 * which blocks reads from completing, over regular application tasks.
	 */
	fsverity_read_workqueue = alloc_workqueue("fsverity_read_queue",
						  WQ_UNBOUND | WQ_HIGHPRI,
						  num_online_cpus());
	if (!fsverity_read_workqueue)
		return -ENOMEM;
	return 0;
}

void __init fsverity_exit_workqueue(void)
{
	destroy_workqueue(fsverity_read_workqueue);
	fsverity_read_workqueue = NULL;
}

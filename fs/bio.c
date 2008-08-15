/*
 * Copyright (C) 2001 Jens Axboe <axboe@kernel.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 *
 */
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/workqueue.h>
#include <linux/blktrace_api.h>
#include <scsi/sg.h>		/* for struct sg_iovec */

static struct kmem_cache *bio_slab __read_mostly;

mempool_t *bio_split_pool __read_mostly;

/*
 * if you change this list, also change bvec_alloc or things will
 * break badly! cannot be bigger than what you can fit into an
 * unsigned short
 */

#define BV(x) { .nr_vecs = x, .name = "biovec-"__stringify(x) }
static struct biovec_slab bvec_slabs[BIOVEC_NR_POOLS] __read_mostly = {
	BV(1), BV(4), BV(16), BV(64), BV(128), BV(BIO_MAX_PAGES),
};
#undef BV

/*
 * fs_bio_set is the bio_set containing bio and iovec memory pools used by
 * IO code that does not need private memory pools.
 */
struct bio_set *fs_bio_set;

unsigned int bvec_nr_vecs(unsigned short idx)
{
	return bvec_slabs[idx].nr_vecs;
}

struct bio_vec *bvec_alloc_bs(gfp_t gfp_mask, int nr, unsigned long *idx, struct bio_set *bs)
{
	struct bio_vec *bvl;

	/*
	 * see comment near bvec_array define!
	 */
	switch (nr) {
		case   1        : *idx = 0; break;
		case   2 ...   4: *idx = 1; break;
		case   5 ...  16: *idx = 2; break;
		case  17 ...  64: *idx = 3; break;
		case  65 ... 128: *idx = 4; break;
		case 129 ... BIO_MAX_PAGES: *idx = 5; break;
		default:
			return NULL;
	}
	/*
	 * idx now points to the pool we want to allocate from
	 */

	bvl = mempool_alloc(bs->bvec_pools[*idx], gfp_mask);
	if (bvl)
		memset(bvl, 0, bvec_nr_vecs(*idx) * sizeof(struct bio_vec));

	return bvl;
}

void bio_free(struct bio *bio, struct bio_set *bio_set)
{
	if (bio->bi_io_vec) {
		const int pool_idx = BIO_POOL_IDX(bio);

		BIO_BUG_ON(pool_idx >= BIOVEC_NR_POOLS);

		mempool_free(bio->bi_io_vec, bio_set->bvec_pools[pool_idx]);
	}

	if (bio_integrity(bio))
		bio_integrity_free(bio, bio_set);

	mempool_free(bio, bio_set->bio_pool);
}

/*
 * default destructor for a bio allocated with bio_alloc_bioset()
 */
static void bio_fs_destructor(struct bio *bio)
{
	bio_free(bio, fs_bio_set);
}

void bio_init(struct bio *bio)
{
	memset(bio, 0, sizeof(*bio));
	bio->bi_flags = 1 << BIO_UPTODATE;
	atomic_set(&bio->bi_cnt, 1);
}

/**
 * bio_alloc_bioset - allocate a bio for I/O
 * @gfp_mask:   the GFP_ mask given to the slab allocator
 * @nr_iovecs:	number of iovecs to pre-allocate
 * @bs:		the bio_set to allocate from
 *
 * Description:
 *   bio_alloc_bioset will first try it's on mempool to satisfy the allocation.
 *   If %__GFP_WAIT is set then we will block on the internal pool waiting
 *   for a &struct bio to become free.
 *
 *   allocate bio and iovecs from the memory pools specified by the
 *   bio_set structure.
 **/
struct bio *bio_alloc_bioset(gfp_t gfp_mask, int nr_iovecs, struct bio_set *bs)
{
	struct bio *bio = mempool_alloc(bs->bio_pool, gfp_mask);

	if (likely(bio)) {
		struct bio_vec *bvl = NULL;

		bio_init(bio);
		if (likely(nr_iovecs)) {
			unsigned long uninitialized_var(idx);

			bvl = bvec_alloc_bs(gfp_mask, nr_iovecs, &idx, bs);
			if (unlikely(!bvl)) {
				mempool_free(bio, bs->bio_pool);
				bio = NULL;
				goto out;
			}
			bio->bi_flags |= idx << BIO_POOL_OFFSET;
			bio->bi_max_vecs = bvec_nr_vecs(idx);
		}
		bio->bi_io_vec = bvl;
	}
out:
	return bio;
}

struct bio *bio_alloc(gfp_t gfp_mask, int nr_iovecs)
{
	struct bio *bio = bio_alloc_bioset(gfp_mask, nr_iovecs, fs_bio_set);

	if (bio)
		bio->bi_destructor = bio_fs_destructor;

	return bio;
}

void zero_fill_bio(struct bio *bio)
{
	unsigned long flags;
	struct bio_vec *bv;
	int i;

	bio_for_each_segment(bv, bio, i) {
		char *data = bvec_kmap_irq(bv, &flags);
		memset(data, 0, bv->bv_len);
		flush_dcache_page(bv->bv_page);
		bvec_kunmap_irq(data, &flags);
	}
}
EXPORT_SYMBOL(zero_fill_bio);

/**
 * bio_put - release a reference to a bio
 * @bio:   bio to release reference to
 *
 * Description:
 *   Put a reference to a &struct bio, either one you have gotten with
 *   bio_alloc or bio_get. The last put of a bio will free it.
 **/
void bio_put(struct bio *bio)
{
	BIO_BUG_ON(!atomic_read(&bio->bi_cnt));

	/*
	 * last put frees it
	 */
	if (atomic_dec_and_test(&bio->bi_cnt)) {
		bio->bi_next = NULL;
		bio->bi_destructor(bio);
	}
}

inline int bio_phys_segments(struct request_queue *q, struct bio *bio)
{
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);

	return bio->bi_phys_segments;
}

inline int bio_hw_segments(struct request_queue *q, struct bio *bio)
{
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);

	return bio->bi_hw_segments;
}

/**
 * 	__bio_clone	-	clone a bio
 * 	@bio: destination bio
 * 	@bio_src: bio to clone
 *
 *	Clone a &bio. Caller will own the returned bio, but not
 *	the actual data it points to. Reference count of returned
 * 	bio will be one.
 */
void __bio_clone(struct bio *bio, struct bio *bio_src)
{
	memcpy(bio->bi_io_vec, bio_src->bi_io_vec,
		bio_src->bi_max_vecs * sizeof(struct bio_vec));

	/*
	 * most users will be overriding ->bi_bdev with a new target,
	 * so we don't set nor calculate new physical/hw segment counts here
	 */
	bio->bi_sector = bio_src->bi_sector;
	bio->bi_bdev = bio_src->bi_bdev;
	bio->bi_flags |= 1 << BIO_CLONED;
	bio->bi_rw = bio_src->bi_rw;
	bio->bi_vcnt = bio_src->bi_vcnt;
	bio->bi_size = bio_src->bi_size;
	bio->bi_idx = bio_src->bi_idx;
}

/**
 *	bio_clone	-	clone a bio
 *	@bio: bio to clone
 *	@gfp_mask: allocation priority
 *
 * 	Like __bio_clone, only also allocates the returned bio
 */
struct bio *bio_clone(struct bio *bio, gfp_t gfp_mask)
{
	struct bio *b = bio_alloc_bioset(gfp_mask, bio->bi_max_vecs, fs_bio_set);

	if (!b)
		return NULL;

	b->bi_destructor = bio_fs_destructor;
	__bio_clone(b, bio);

	if (bio_integrity(bio)) {
		int ret;

		ret = bio_integrity_clone(b, bio, fs_bio_set);

		if (ret < 0)
			return NULL;
	}

	return b;
}

/**
 *	bio_get_nr_vecs		- return approx number of vecs
 *	@bdev:  I/O target
 *
 *	Return the approximate number of pages we can send to this target.
 *	There's no guarantee that you will be able to fit this number of pages
 *	into a bio, it does not account for dynamic restrictions that vary
 *	on offset.
 */
int bio_get_nr_vecs(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);
	int nr_pages;

	nr_pages = ((q->max_sectors << 9) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (nr_pages > q->max_phys_segments)
		nr_pages = q->max_phys_segments;
	if (nr_pages > q->max_hw_segments)
		nr_pages = q->max_hw_segments;

	return nr_pages;
}

static int __bio_add_page(struct request_queue *q, struct bio *bio, struct page
			  *page, unsigned int len, unsigned int offset,
			  unsigned short max_sectors)
{
	int retried_segments = 0;
	struct bio_vec *bvec;

	/*
	 * cloned bio must not modify vec list
	 */
	if (unlikely(bio_flagged(bio, BIO_CLONED)))
		return 0;

	if (((bio->bi_size + len) >> 9) > max_sectors)
		return 0;

	/*
	 * For filesystems with a blocksize smaller than the pagesize
	 * we will often be called with the same page as last time and
	 * a consecutive offset.  Optimize this special case.
	 */
	if (bio->bi_vcnt > 0) {
		struct bio_vec *prev = &bio->bi_io_vec[bio->bi_vcnt - 1];

		if (page == prev->bv_page &&
		    offset == prev->bv_offset + prev->bv_len) {
			prev->bv_len += len;

			if (q->merge_bvec_fn) {
				struct bvec_merge_data bvm = {
					.bi_bdev = bio->bi_bdev,
					.bi_sector = bio->bi_sector,
					.bi_size = bio->bi_size,
					.bi_rw = bio->bi_rw,
				};

				if (q->merge_bvec_fn(q, &bvm, prev) < len) {
					prev->bv_len -= len;
					return 0;
				}
			}

			goto done;
		}
	}

	if (bio->bi_vcnt >= bio->bi_max_vecs)
		return 0;

	/*
	 * we might lose a segment or two here, but rather that than
	 * make this too complex.
	 */

	while (bio->bi_phys_segments >= q->max_phys_segments
	       || bio->bi_hw_segments >= q->max_hw_segments) {

		if (retried_segments)
			return 0;

		retried_segments = 1;
		blk_recount_segments(q, bio);
	}

	/*
	 * setup the new entry, we might clear it again later if we
	 * cannot add the page
	 */
	bvec = &bio->bi_io_vec[bio->bi_vcnt];
	bvec->bv_page = page;
	bvec->bv_len = len;
	bvec->bv_offset = offset;

	/*
	 * if queue has other restrictions (eg varying max sector size
	 * depending on offset), it can specify a merge_bvec_fn in the
	 * queue to get further control
	 */
	if (q->merge_bvec_fn) {
		struct bvec_merge_data bvm = {
			.bi_bdev = bio->bi_bdev,
			.bi_sector = bio->bi_sector,
			.bi_size = bio->bi_size,
			.bi_rw = bio->bi_rw,
		};

		/*
		 * merge_bvec_fn() returns number of bytes it can accept
		 * at this offset
		 */
		if (q->merge_bvec_fn(q, &bvm, bvec) < len) {
			bvec->bv_page = NULL;
			bvec->bv_len = 0;
			bvec->bv_offset = 0;
			return 0;
		}
	}

	/* If we may be able to merge these biovecs, force a recount */
	if (bio->bi_vcnt && (BIOVEC_PHYS_MERGEABLE(bvec-1, bvec)))
		bio->bi_flags &= ~(1 << BIO_SEG_VALID);

	bio->bi_vcnt++;
	bio->bi_phys_segments++;
	bio->bi_hw_segments++;
 done:
	bio->bi_size += len;
	return len;
}

/**
 *	bio_add_pc_page	-	attempt to add page to bio
 *	@q: the target queue
 *	@bio: destination bio
 *	@page: page to add
 *	@len: vec entry length
 *	@offset: vec entry offset
 *
 *	Attempt to add a page to the bio_vec maplist. This can fail for a
 *	number of reasons, such as the bio being full or target block
 *	device limitations. The target block device must allow bio's
 *      smaller than PAGE_SIZE, so it is always possible to add a single
 *      page to an empty bio. This should only be used by REQ_PC bios.
 */
int bio_add_pc_page(struct request_queue *q, struct bio *bio, struct page *page,
		    unsigned int len, unsigned int offset)
{
	return __bio_add_page(q, bio, page, len, offset, q->max_hw_sectors);
}

/**
 *	bio_add_page	-	attempt to add page to bio
 *	@bio: destination bio
 *	@page: page to add
 *	@len: vec entry length
 *	@offset: vec entry offset
 *
 *	Attempt to add a page to the bio_vec maplist. This can fail for a
 *	number of reasons, such as the bio being full or target block
 *	device limitations. The target block device must allow bio's
 *      smaller than PAGE_SIZE, so it is always possible to add a single
 *      page to an empty bio.
 */
int bio_add_page(struct bio *bio, struct page *page, unsigned int len,
		 unsigned int offset)
{
	struct request_queue *q = bdev_get_queue(bio->bi_bdev);
	return __bio_add_page(q, bio, page, len, offset, q->max_sectors);
}

struct bio_map_data {
	struct bio_vec *iovecs;
	int nr_sgvecs;
	struct sg_iovec *sgvecs;
};

static void bio_set_map_data(struct bio_map_data *bmd, struct bio *bio,
			     struct sg_iovec *iov, int iov_count)
{
	memcpy(bmd->iovecs, bio->bi_io_vec, sizeof(struct bio_vec) * bio->bi_vcnt);
	memcpy(bmd->sgvecs, iov, sizeof(struct sg_iovec) * iov_count);
	bmd->nr_sgvecs = iov_count;
	bio->bi_private = bmd;
}

static void bio_free_map_data(struct bio_map_data *bmd)
{
	kfree(bmd->iovecs);
	kfree(bmd->sgvecs);
	kfree(bmd);
}

static struct bio_map_data *bio_alloc_map_data(int nr_segs, int iov_count,
					       gfp_t gfp_mask)
{
	struct bio_map_data *bmd = kmalloc(sizeof(*bmd), gfp_mask);

	if (!bmd)
		return NULL;

	bmd->iovecs = kmalloc(sizeof(struct bio_vec) * nr_segs, gfp_mask);
	if (!bmd->iovecs) {
		kfree(bmd);
		return NULL;
	}

	bmd->sgvecs = kmalloc(sizeof(struct sg_iovec) * iov_count, gfp_mask);
	if (bmd->sgvecs)
		return bmd;

	kfree(bmd->iovecs);
	kfree(bmd);
	return NULL;
}

static int __bio_copy_iov(struct bio *bio, struct bio_vec *iovecs,
			  struct sg_iovec *iov, int iov_count, int uncopy)
{
	int ret = 0, i;
	struct bio_vec *bvec;
	int iov_idx = 0;
	unsigned int iov_off = 0;
	int read = bio_data_dir(bio) == READ;

	__bio_for_each_segment(bvec, bio, i, 0) {
		char *bv_addr = page_address(bvec->bv_page);
		unsigned int bv_len = iovecs[i].bv_len;

		while (bv_len && iov_idx < iov_count) {
			unsigned int bytes;
			char *iov_addr;

			bytes = min_t(unsigned int,
				      iov[iov_idx].iov_len - iov_off, bv_len);
			iov_addr = iov[iov_idx].iov_base + iov_off;

			if (!ret) {
				if (!read && !uncopy)
					ret = copy_from_user(bv_addr, iov_addr,
							     bytes);
				if (read && uncopy)
					ret = copy_to_user(iov_addr, bv_addr,
							   bytes);

				if (ret)
					ret = -EFAULT;
			}

			bv_len -= bytes;
			bv_addr += bytes;
			iov_addr += bytes;
			iov_off += bytes;

			if (iov[iov_idx].iov_len == iov_off) {
				iov_idx++;
				iov_off = 0;
			}
		}

		if (uncopy)
			__free_page(bvec->bv_page);
	}

	return ret;
}

/**
 *	bio_uncopy_user	-	finish previously mapped bio
 *	@bio: bio being terminated
 *
 *	Free pages allocated from bio_copy_user() and write back data
 *	to user space in case of a read.
 */
int bio_uncopy_user(struct bio *bio)
{
	struct bio_map_data *bmd = bio->bi_private;
	int ret;

	ret = __bio_copy_iov(bio, bmd->iovecs, bmd->sgvecs, bmd->nr_sgvecs, 1);

	bio_free_map_data(bmd);
	bio_put(bio);
	return ret;
}

/**
 *	bio_copy_user_iov	-	copy user data to bio
 *	@q: destination block queue
 *	@iov:	the iovec.
 *	@iov_count: number of elements in the iovec
 *	@write_to_vm: bool indicating writing to pages or not
 *
 *	Prepares and returns a bio for indirect user io, bouncing data
 *	to/from kernel pages as necessary. Must be paired with
 *	call bio_uncopy_user() on io completion.
 */
struct bio *bio_copy_user_iov(struct request_queue *q, struct sg_iovec *iov,
			      int iov_count, int write_to_vm)
{
	struct bio_map_data *bmd;
	struct bio_vec *bvec;
	struct page *page;
	struct bio *bio;
	int i, ret;
	int nr_pages = 0;
	unsigned int len = 0;

	for (i = 0; i < iov_count; i++) {
		unsigned long uaddr;
		unsigned long end;
		unsigned long start;

		uaddr = (unsigned long)iov[i].iov_base;
		end = (uaddr + iov[i].iov_len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		start = uaddr >> PAGE_SHIFT;

		nr_pages += end - start;
		len += iov[i].iov_len;
	}

	bmd = bio_alloc_map_data(nr_pages, iov_count, GFP_KERNEL);
	if (!bmd)
		return ERR_PTR(-ENOMEM);

	ret = -ENOMEM;
	bio = bio_alloc(GFP_KERNEL, nr_pages);
	if (!bio)
		goto out_bmd;

	bio->bi_rw |= (!write_to_vm << BIO_RW);

	ret = 0;
	while (len) {
		unsigned int bytes = PAGE_SIZE;

		if (bytes > len)
			bytes = len;

		page = alloc_page(q->bounce_gfp | GFP_KERNEL);
		if (!page) {
			ret = -ENOMEM;
			break;
		}

		if (bio_add_pc_page(q, bio, page, bytes, 0) < bytes)
			break;

		len -= bytes;
	}

	if (ret)
		goto cleanup;

	/*
	 * success
	 */
	if (!write_to_vm) {
		ret = __bio_copy_iov(bio, bio->bi_io_vec, iov, iov_count, 0);
		if (ret)
			goto cleanup;
	}

	bio_set_map_data(bmd, bio, iov, iov_count);
	return bio;
cleanup:
	bio_for_each_segment(bvec, bio, i)
		__free_page(bvec->bv_page);

	bio_put(bio);
out_bmd:
	bio_free_map_data(bmd);
	return ERR_PTR(ret);
}

/**
 *	bio_copy_user	-	copy user data to bio
 *	@q: destination block queue
 *	@uaddr: start of user address
 *	@len: length in bytes
 *	@write_to_vm: bool indicating writing to pages or not
 *
 *	Prepares and returns a bio for indirect user io, bouncing data
 *	to/from kernel pages as necessary. Must be paired with
 *	call bio_uncopy_user() on io completion.
 */
struct bio *bio_copy_user(struct request_queue *q, unsigned long uaddr,
			  unsigned int len, int write_to_vm)
{
	struct sg_iovec iov;

	iov.iov_base = (void __user *)uaddr;
	iov.iov_len = len;

	return bio_copy_user_iov(q, &iov, 1, write_to_vm);
}

static struct bio *__bio_map_user_iov(struct request_queue *q,
				      struct block_device *bdev,
				      struct sg_iovec *iov, int iov_count,
				      int write_to_vm)
{
	int i, j;
	int nr_pages = 0;
	struct page **pages;
	struct bio *bio;
	int cur_page = 0;
	int ret, offset;

	for (i = 0; i < iov_count; i++) {
		unsigned long uaddr = (unsigned long)iov[i].iov_base;
		unsigned long len = iov[i].iov_len;
		unsigned long end = (uaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		unsigned long start = uaddr >> PAGE_SHIFT;

		nr_pages += end - start;
		/*
		 * buffer must be aligned to at least hardsector size for now
		 */
		if (uaddr & queue_dma_alignment(q))
			return ERR_PTR(-EINVAL);
	}

	if (!nr_pages)
		return ERR_PTR(-EINVAL);

	bio = bio_alloc(GFP_KERNEL, nr_pages);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	ret = -ENOMEM;
	pages = kcalloc(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		goto out;

	for (i = 0; i < iov_count; i++) {
		unsigned long uaddr = (unsigned long)iov[i].iov_base;
		unsigned long len = iov[i].iov_len;
		unsigned long end = (uaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		unsigned long start = uaddr >> PAGE_SHIFT;
		const int local_nr_pages = end - start;
		const int page_limit = cur_page + local_nr_pages;
		
		ret = get_user_pages_fast(uaddr, local_nr_pages,
				write_to_vm, &pages[cur_page]);
		if (ret < local_nr_pages) {
			ret = -EFAULT;
			goto out_unmap;
		}

		offset = uaddr & ~PAGE_MASK;
		for (j = cur_page; j < page_limit; j++) {
			unsigned int bytes = PAGE_SIZE - offset;

			if (len <= 0)
				break;
			
			if (bytes > len)
				bytes = len;

			/*
			 * sorry...
			 */
			if (bio_add_pc_page(q, bio, pages[j], bytes, offset) <
					    bytes)
				break;

			len -= bytes;
			offset = 0;
		}

		cur_page = j;
		/*
		 * release the pages we didn't map into the bio, if any
		 */
		while (j < page_limit)
			page_cache_release(pages[j++]);
	}

	kfree(pages);

	/*
	 * set data direction, and check if mapped pages need bouncing
	 */
	if (!write_to_vm)
		bio->bi_rw |= (1 << BIO_RW);

	bio->bi_bdev = bdev;
	bio->bi_flags |= (1 << BIO_USER_MAPPED);
	return bio;

 out_unmap:
	for (i = 0; i < nr_pages; i++) {
		if(!pages[i])
			break;
		page_cache_release(pages[i]);
	}
 out:
	kfree(pages);
	bio_put(bio);
	return ERR_PTR(ret);
}

/**
 *	bio_map_user	-	map user address into bio
 *	@q: the struct request_queue for the bio
 *	@bdev: destination block device
 *	@uaddr: start of user address
 *	@len: length in bytes
 *	@write_to_vm: bool indicating writing to pages or not
 *
 *	Map the user space address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
struct bio *bio_map_user(struct request_queue *q, struct block_device *bdev,
			 unsigned long uaddr, unsigned int len, int write_to_vm)
{
	struct sg_iovec iov;

	iov.iov_base = (void __user *)uaddr;
	iov.iov_len = len;

	return bio_map_user_iov(q, bdev, &iov, 1, write_to_vm);
}

/**
 *	bio_map_user_iov - map user sg_iovec table into bio
 *	@q: the struct request_queue for the bio
 *	@bdev: destination block device
 *	@iov:	the iovec.
 *	@iov_count: number of elements in the iovec
 *	@write_to_vm: bool indicating writing to pages or not
 *
 *	Map the user space address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
struct bio *bio_map_user_iov(struct request_queue *q, struct block_device *bdev,
			     struct sg_iovec *iov, int iov_count,
			     int write_to_vm)
{
	struct bio *bio;

	bio = __bio_map_user_iov(q, bdev, iov, iov_count, write_to_vm);

	if (IS_ERR(bio))
		return bio;

	/*
	 * subtle -- if __bio_map_user() ended up bouncing a bio,
	 * it would normally disappear when its bi_end_io is run.
	 * however, we need it for the unmap, so grab an extra
	 * reference to it
	 */
	bio_get(bio);

	return bio;
}

static void __bio_unmap_user(struct bio *bio)
{
	struct bio_vec *bvec;
	int i;

	/*
	 * make sure we dirty pages we wrote to
	 */
	__bio_for_each_segment(bvec, bio, i, 0) {
		if (bio_data_dir(bio) == READ)
			set_page_dirty_lock(bvec->bv_page);

		page_cache_release(bvec->bv_page);
	}

	bio_put(bio);
}

/**
 *	bio_unmap_user	-	unmap a bio
 *	@bio:		the bio being unmapped
 *
 *	Unmap a bio previously mapped by bio_map_user(). Must be called with
 *	a process context.
 *
 *	bio_unmap_user() may sleep.
 */
void bio_unmap_user(struct bio *bio)
{
	__bio_unmap_user(bio);
	bio_put(bio);
}

static void bio_map_kern_endio(struct bio *bio, int err)
{
	bio_put(bio);
}


static struct bio *__bio_map_kern(struct request_queue *q, void *data,
				  unsigned int len, gfp_t gfp_mask)
{
	unsigned long kaddr = (unsigned long)data;
	unsigned long end = (kaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = kaddr >> PAGE_SHIFT;
	const int nr_pages = end - start;
	int offset, i;
	struct bio *bio;

	bio = bio_alloc(gfp_mask, nr_pages);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	offset = offset_in_page(kaddr);
	for (i = 0; i < nr_pages; i++) {
		unsigned int bytes = PAGE_SIZE - offset;

		if (len <= 0)
			break;

		if (bytes > len)
			bytes = len;

		if (bio_add_pc_page(q, bio, virt_to_page(data), bytes,
				    offset) < bytes)
			break;

		data += bytes;
		len -= bytes;
		offset = 0;
	}

	bio->bi_end_io = bio_map_kern_endio;
	return bio;
}

/**
 *	bio_map_kern	-	map kernel address into bio
 *	@q: the struct request_queue for the bio
 *	@data: pointer to buffer to map
 *	@len: length in bytes
 *	@gfp_mask: allocation flags for bio allocation
 *
 *	Map the kernel address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
struct bio *bio_map_kern(struct request_queue *q, void *data, unsigned int len,
			 gfp_t gfp_mask)
{
	struct bio *bio;

	bio = __bio_map_kern(q, data, len, gfp_mask);
	if (IS_ERR(bio))
		return bio;

	if (bio->bi_size == len)
		return bio;

	/*
	 * Don't support partial mappings.
	 */
	bio_put(bio);
	return ERR_PTR(-EINVAL);
}

static void bio_copy_kern_endio(struct bio *bio, int err)
{
	struct bio_vec *bvec;
	const int read = bio_data_dir(bio) == READ;
	struct bio_map_data *bmd = bio->bi_private;
	int i;
	char *p = bmd->sgvecs[0].iov_base;

	__bio_for_each_segment(bvec, bio, i, 0) {
		char *addr = page_address(bvec->bv_page);
		int len = bmd->iovecs[i].bv_len;

		if (read && !err)
			memcpy(p, addr, len);

		__free_page(bvec->bv_page);
		p += len;
	}

	bio_free_map_data(bmd);
	bio_put(bio);
}

/**
 *	bio_copy_kern	-	copy kernel address into bio
 *	@q: the struct request_queue for the bio
 *	@data: pointer to buffer to copy
 *	@len: length in bytes
 *	@gfp_mask: allocation flags for bio and page allocation
 *	@reading: data direction is READ
 *
 *	copy the kernel address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
struct bio *bio_copy_kern(struct request_queue *q, void *data, unsigned int len,
			  gfp_t gfp_mask, int reading)
{
	unsigned long kaddr = (unsigned long)data;
	unsigned long end = (kaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = kaddr >> PAGE_SHIFT;
	const int nr_pages = end - start;
	struct bio *bio;
	struct bio_vec *bvec;
	struct bio_map_data *bmd;
	int i, ret;
	struct sg_iovec iov;

	iov.iov_base = data;
	iov.iov_len = len;

	bmd = bio_alloc_map_data(nr_pages, 1, gfp_mask);
	if (!bmd)
		return ERR_PTR(-ENOMEM);

	ret = -ENOMEM;
	bio = bio_alloc(gfp_mask, nr_pages);
	if (!bio)
		goto out_bmd;

	while (len) {
		struct page *page;
		unsigned int bytes = PAGE_SIZE;

		if (bytes > len)
			bytes = len;

		page = alloc_page(q->bounce_gfp | gfp_mask);
		if (!page) {
			ret = -ENOMEM;
			goto cleanup;
		}

		if (bio_add_pc_page(q, bio, page, bytes, 0) < bytes) {
			ret = -EINVAL;
			goto cleanup;
		}

		len -= bytes;
	}

	if (!reading) {
		void *p = data;

		bio_for_each_segment(bvec, bio, i) {
			char *addr = page_address(bvec->bv_page);

			memcpy(addr, p, bvec->bv_len);
			p += bvec->bv_len;
		}
	}

	bio->bi_private = bmd;
	bio->bi_end_io = bio_copy_kern_endio;

	bio_set_map_data(bmd, bio, &iov, 1);
	return bio;
cleanup:
	bio_for_each_segment(bvec, bio, i)
		__free_page(bvec->bv_page);

	bio_put(bio);
out_bmd:
	bio_free_map_data(bmd);

	return ERR_PTR(ret);
}

/*
 * bio_set_pages_dirty() and bio_check_pages_dirty() are support functions
 * for performing direct-IO in BIOs.
 *
 * The problem is that we cannot run set_page_dirty() from interrupt context
 * because the required locks are not interrupt-safe.  So what we can do is to
 * mark the pages dirty _before_ performing IO.  And in interrupt context,
 * check that the pages are still dirty.   If so, fine.  If not, redirty them
 * in process context.
 *
 * We special-case compound pages here: normally this means reads into hugetlb
 * pages.  The logic in here doesn't really work right for compound pages
 * because the VM does not uniformly chase down the head page in all cases.
 * But dirtiness of compound pages is pretty meaningless anyway: the VM doesn't
 * handle them at all.  So we skip compound pages here at an early stage.
 *
 * Note that this code is very hard to test under normal circumstances because
 * direct-io pins the pages with get_user_pages().  This makes
 * is_page_cache_freeable return false, and the VM will not clean the pages.
 * But other code (eg, pdflush) could clean the pages if they are mapped
 * pagecache.
 *
 * Simply disabling the call to bio_set_pages_dirty() is a good way to test the
 * deferred bio dirtying paths.
 */

/*
 * bio_set_pages_dirty() will mark all the bio's pages as dirty.
 */
void bio_set_pages_dirty(struct bio *bio)
{
	struct bio_vec *bvec = bio->bi_io_vec;
	int i;

	for (i = 0; i < bio->bi_vcnt; i++) {
		struct page *page = bvec[i].bv_page;

		if (page && !PageCompound(page))
			set_page_dirty_lock(page);
	}
}

static void bio_release_pages(struct bio *bio)
{
	struct bio_vec *bvec = bio->bi_io_vec;
	int i;

	for (i = 0; i < bio->bi_vcnt; i++) {
		struct page *page = bvec[i].bv_page;

		if (page)
			put_page(page);
	}
}

/*
 * bio_check_pages_dirty() will check that all the BIO's pages are still dirty.
 * If they are, then fine.  If, however, some pages are clean then they must
 * have been written out during the direct-IO read.  So we take another ref on
 * the BIO and the offending pages and re-dirty the pages in process context.
 *
 * It is expected that bio_check_pages_dirty() will wholly own the BIO from
 * here on.  It will run one page_cache_release() against each page and will
 * run one bio_put() against the BIO.
 */

static void bio_dirty_fn(struct work_struct *work);

static DECLARE_WORK(bio_dirty_work, bio_dirty_fn);
static DEFINE_SPINLOCK(bio_dirty_lock);
static struct bio *bio_dirty_list;

/*
 * This runs in process context
 */
static void bio_dirty_fn(struct work_struct *work)
{
	unsigned long flags;
	struct bio *bio;

	spin_lock_irqsave(&bio_dirty_lock, flags);
	bio = bio_dirty_list;
	bio_dirty_list = NULL;
	spin_unlock_irqrestore(&bio_dirty_lock, flags);

	while (bio) {
		struct bio *next = bio->bi_private;

		bio_set_pages_dirty(bio);
		bio_release_pages(bio);
		bio_put(bio);
		bio = next;
	}
}

void bio_check_pages_dirty(struct bio *bio)
{
	struct bio_vec *bvec = bio->bi_io_vec;
	int nr_clean_pages = 0;
	int i;

	for (i = 0; i < bio->bi_vcnt; i++) {
		struct page *page = bvec[i].bv_page;

		if (PageDirty(page) || PageCompound(page)) {
			page_cache_release(page);
			bvec[i].bv_page = NULL;
		} else {
			nr_clean_pages++;
		}
	}

	if (nr_clean_pages) {
		unsigned long flags;

		spin_lock_irqsave(&bio_dirty_lock, flags);
		bio->bi_private = bio_dirty_list;
		bio_dirty_list = bio;
		spin_unlock_irqrestore(&bio_dirty_lock, flags);
		schedule_work(&bio_dirty_work);
	} else {
		bio_put(bio);
	}
}

/**
 * bio_endio - end I/O on a bio
 * @bio:	bio
 * @error:	error, if any
 *
 * Description:
 *   bio_endio() will end I/O on the whole bio. bio_endio() is the
 *   preferred way to end I/O on a bio, it takes care of clearing
 *   BIO_UPTODATE on error. @error is 0 on success, and and one of the
 *   established -Exxxx (-EIO, for instance) error values in case
 *   something went wrong. Noone should call bi_end_io() directly on a
 *   bio unless they own it and thus know that it has an end_io
 *   function.
 **/
void bio_endio(struct bio *bio, int error)
{
	if (error)
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
	else if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		error = -EIO;

	if (bio->bi_end_io)
		bio->bi_end_io(bio, error);
}

void bio_pair_release(struct bio_pair *bp)
{
	if (atomic_dec_and_test(&bp->cnt)) {
		struct bio *master = bp->bio1.bi_private;

		bio_endio(master, bp->error);
		mempool_free(bp, bp->bio2.bi_private);
	}
}

static void bio_pair_end_1(struct bio *bi, int err)
{
	struct bio_pair *bp = container_of(bi, struct bio_pair, bio1);

	if (err)
		bp->error = err;

	bio_pair_release(bp);
}

static void bio_pair_end_2(struct bio *bi, int err)
{
	struct bio_pair *bp = container_of(bi, struct bio_pair, bio2);

	if (err)
		bp->error = err;

	bio_pair_release(bp);
}

/*
 * split a bio - only worry about a bio with a single page
 * in it's iovec
 */
struct bio_pair *bio_split(struct bio *bi, mempool_t *pool, int first_sectors)
{
	struct bio_pair *bp = mempool_alloc(pool, GFP_NOIO);

	if (!bp)
		return bp;

	blk_add_trace_pdu_int(bdev_get_queue(bi->bi_bdev), BLK_TA_SPLIT, bi,
				bi->bi_sector + first_sectors);

	BUG_ON(bi->bi_vcnt != 1);
	BUG_ON(bi->bi_idx != 0);
	atomic_set(&bp->cnt, 3);
	bp->error = 0;
	bp->bio1 = *bi;
	bp->bio2 = *bi;
	bp->bio2.bi_sector += first_sectors;
	bp->bio2.bi_size -= first_sectors << 9;
	bp->bio1.bi_size = first_sectors << 9;

	bp->bv1 = bi->bi_io_vec[0];
	bp->bv2 = bi->bi_io_vec[0];
	bp->bv2.bv_offset += first_sectors << 9;
	bp->bv2.bv_len -= first_sectors << 9;
	bp->bv1.bv_len = first_sectors << 9;

	bp->bio1.bi_io_vec = &bp->bv1;
	bp->bio2.bi_io_vec = &bp->bv2;

	bp->bio1.bi_max_vecs = 1;
	bp->bio2.bi_max_vecs = 1;

	bp->bio1.bi_end_io = bio_pair_end_1;
	bp->bio2.bi_end_io = bio_pair_end_2;

	bp->bio1.bi_private = bi;
	bp->bio2.bi_private = pool;

	if (bio_integrity(bi))
		bio_integrity_split(bi, bp, first_sectors);

	return bp;
}


/*
 * create memory pools for biovec's in a bio_set.
 * use the global biovec slabs created for general use.
 */
static int biovec_create_pools(struct bio_set *bs, int pool_entries)
{
	int i;

	for (i = 0; i < BIOVEC_NR_POOLS; i++) {
		struct biovec_slab *bp = bvec_slabs + i;
		mempool_t **bvp = bs->bvec_pools + i;

		*bvp = mempool_create_slab_pool(pool_entries, bp->slab);
		if (!*bvp)
			return -ENOMEM;
	}
	return 0;
}

static void biovec_free_pools(struct bio_set *bs)
{
	int i;

	for (i = 0; i < BIOVEC_NR_POOLS; i++) {
		mempool_t *bvp = bs->bvec_pools[i];

		if (bvp)
			mempool_destroy(bvp);
	}

}

void bioset_free(struct bio_set *bs)
{
	if (bs->bio_pool)
		mempool_destroy(bs->bio_pool);

	bioset_integrity_free(bs);
	biovec_free_pools(bs);

	kfree(bs);
}

struct bio_set *bioset_create(int bio_pool_size, int bvec_pool_size)
{
	struct bio_set *bs = kzalloc(sizeof(*bs), GFP_KERNEL);

	if (!bs)
		return NULL;

	bs->bio_pool = mempool_create_slab_pool(bio_pool_size, bio_slab);
	if (!bs->bio_pool)
		goto bad;

	if (bioset_integrity_create(bs, bio_pool_size))
		goto bad;

	if (!biovec_create_pools(bs, bvec_pool_size))
		return bs;

bad:
	bioset_free(bs);
	return NULL;
}

static void __init biovec_init_slabs(void)
{
	int i;

	for (i = 0; i < BIOVEC_NR_POOLS; i++) {
		int size;
		struct biovec_slab *bvs = bvec_slabs + i;

		size = bvs->nr_vecs * sizeof(struct bio_vec);
		bvs->slab = kmem_cache_create(bvs->name, size, 0,
                                SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);
	}
}

static int __init init_bio(void)
{
	bio_slab = KMEM_CACHE(bio, SLAB_HWCACHE_ALIGN|SLAB_PANIC);

	bio_integrity_init_slab();
	biovec_init_slabs();

	fs_bio_set = bioset_create(BIO_POOL_SIZE, 2);
	if (!fs_bio_set)
		panic("bio: can't allocate bios\n");

	bio_split_pool = mempool_create_kmalloc_pool(BIO_SPLIT_ENTRIES,
						     sizeof(struct bio_pair));
	if (!bio_split_pool)
		panic("bio: can't create split pool\n");

	return 0;
}

subsys_initcall(init_bio);

EXPORT_SYMBOL(bio_alloc);
EXPORT_SYMBOL(bio_put);
EXPORT_SYMBOL(bio_free);
EXPORT_SYMBOL(bio_endio);
EXPORT_SYMBOL(bio_init);
EXPORT_SYMBOL(__bio_clone);
EXPORT_SYMBOL(bio_clone);
EXPORT_SYMBOL(bio_phys_segments);
EXPORT_SYMBOL(bio_hw_segments);
EXPORT_SYMBOL(bio_add_page);
EXPORT_SYMBOL(bio_add_pc_page);
EXPORT_SYMBOL(bio_get_nr_vecs);
EXPORT_SYMBOL(bio_map_user);
EXPORT_SYMBOL(bio_unmap_user);
EXPORT_SYMBOL(bio_map_kern);
EXPORT_SYMBOL(bio_copy_kern);
EXPORT_SYMBOL(bio_pair_release);
EXPORT_SYMBOL(bio_split);
EXPORT_SYMBOL(bio_split_pool);
EXPORT_SYMBOL(bio_copy_user);
EXPORT_SYMBOL(bio_uncopy_user);
EXPORT_SYMBOL(bioset_create);
EXPORT_SYMBOL(bioset_free);
EXPORT_SYMBOL(bio_alloc_bioset);

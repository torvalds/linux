/*
 * bio-integrity.c - bio data integrity extensions
 *
 * Copyright (C) 2007, 2008 Oracle Corporation
 * Written by: Martin K. Petersen <martin.petersen@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#include <linux/blkdev.h>
#include <linux/mempool.h>
#include <linux/bio.h>
#include <linux/workqueue.h>

static struct kmem_cache *bio_integrity_slab __read_mostly;
static struct workqueue_struct *kintegrityd_wq;

/**
 * bio_integrity_alloc_bioset - Allocate integrity payload and attach it to bio
 * @bio:	bio to attach integrity metadata to
 * @gfp_mask:	Memory allocation mask
 * @nr_vecs:	Number of integrity metadata scatter-gather elements
 * @bs:		bio_set to allocate from
 *
 * Description: This function prepares a bio for attaching integrity
 * metadata.  nr_vecs specifies the maximum number of pages containing
 * integrity metadata that can be attached.
 */
struct bio_integrity_payload *bio_integrity_alloc_bioset(struct bio *bio,
							 gfp_t gfp_mask,
							 unsigned int nr_vecs,
							 struct bio_set *bs)
{
	struct bio_integrity_payload *bip;
	struct bio_vec *iv;
	unsigned long idx;

	BUG_ON(bio == NULL);

	bip = mempool_alloc(bs->bio_integrity_pool, gfp_mask);
	if (unlikely(bip == NULL)) {
		printk(KERN_ERR "%s: could not alloc bip\n", __func__);
		return NULL;
	}

	memset(bip, 0, sizeof(*bip));

	iv = bvec_alloc_bs(gfp_mask, nr_vecs, &idx, bs);
	if (unlikely(iv == NULL)) {
		printk(KERN_ERR "%s: could not alloc bip_vec\n", __func__);
		mempool_free(bip, bs->bio_integrity_pool);
		return NULL;
	}

	bip->bip_pool = idx;
	bip->bip_vec = iv;
	bip->bip_bio = bio;
	bio->bi_integrity = bip;

	return bip;
}
EXPORT_SYMBOL(bio_integrity_alloc_bioset);

/**
 * bio_integrity_alloc - Allocate integrity payload and attach it to bio
 * @bio:	bio to attach integrity metadata to
 * @gfp_mask:	Memory allocation mask
 * @nr_vecs:	Number of integrity metadata scatter-gather elements
 *
 * Description: This function prepares a bio for attaching integrity
 * metadata.  nr_vecs specifies the maximum number of pages containing
 * integrity metadata that can be attached.
 */
struct bio_integrity_payload *bio_integrity_alloc(struct bio *bio,
						  gfp_t gfp_mask,
						  unsigned int nr_vecs)
{
	return bio_integrity_alloc_bioset(bio, gfp_mask, nr_vecs, fs_bio_set);
}
EXPORT_SYMBOL(bio_integrity_alloc);

/**
 * bio_integrity_free - Free bio integrity payload
 * @bio:	bio containing bip to be freed
 * @bs:		bio_set this bio was allocated from
 *
 * Description: Used to free the integrity portion of a bio. Usually
 * called from bio_free().
 */
void bio_integrity_free(struct bio *bio, struct bio_set *bs)
{
	struct bio_integrity_payload *bip = bio->bi_integrity;

	BUG_ON(bip == NULL);

	/* A cloned bio doesn't own the integrity metadata */
	if (!bio_flagged(bio, BIO_CLONED) && !bio_flagged(bio, BIO_FS_INTEGRITY)
	    && bip->bip_buf != NULL)
		kfree(bip->bip_buf);

	mempool_free(bip->bip_vec, bs->bvec_pools[bip->bip_pool]);
	mempool_free(bip, bs->bio_integrity_pool);

	bio->bi_integrity = NULL;
}
EXPORT_SYMBOL(bio_integrity_free);

/**
 * bio_integrity_add_page - Attach integrity metadata
 * @bio:	bio to update
 * @page:	page containing integrity metadata
 * @len:	number of bytes of integrity metadata in page
 * @offset:	start offset within page
 *
 * Description: Attach a page containing integrity metadata to bio.
 */
int bio_integrity_add_page(struct bio *bio, struct page *page,
			   unsigned int len, unsigned int offset)
{
	struct bio_integrity_payload *bip = bio->bi_integrity;
	struct bio_vec *iv;

	if (bip->bip_vcnt >= bvec_nr_vecs(bip->bip_pool)) {
		printk(KERN_ERR "%s: bip_vec full\n", __func__);
		return 0;
	}

	iv = bip_vec_idx(bip, bip->bip_vcnt);
	BUG_ON(iv == NULL);
	BUG_ON(iv->bv_page != NULL);

	iv->bv_page = page;
	iv->bv_len = len;
	iv->bv_offset = offset;
	bip->bip_vcnt++;

	return len;
}
EXPORT_SYMBOL(bio_integrity_add_page);

static int bdev_integrity_enabled(struct block_device *bdev, int rw)
{
	struct blk_integrity *bi = bdev_get_integrity(bdev);

	if (bi == NULL)
		return 0;

	if (rw == READ && bi->verify_fn != NULL &&
	    (bi->flags & INTEGRITY_FLAG_READ))
		return 1;

	if (rw == WRITE && bi->generate_fn != NULL &&
	    (bi->flags & INTEGRITY_FLAG_WRITE))
		return 1;

	return 0;
}

/**
 * bio_integrity_enabled - Check whether integrity can be passed
 * @bio:	bio to check
 *
 * Description: Determines whether bio_integrity_prep() can be called
 * on this bio or not.	bio data direction and target device must be
 * set prior to calling.  The functions honors the write_generate and
 * read_verify flags in sysfs.
 */
int bio_integrity_enabled(struct bio *bio)
{
	/* Already protected? */
	if (bio_integrity(bio))
		return 0;

	return bdev_integrity_enabled(bio->bi_bdev, bio_data_dir(bio));
}
EXPORT_SYMBOL(bio_integrity_enabled);

/**
 * bio_integrity_hw_sectors - Convert 512b sectors to hardware ditto
 * @bi:		blk_integrity profile for device
 * @sectors:	Number of 512 sectors to convert
 *
 * Description: The block layer calculates everything in 512 byte
 * sectors but integrity metadata is done in terms of the hardware
 * sector size of the storage device.  Convert the block layer sectors
 * to physical sectors.
 */
static inline unsigned int bio_integrity_hw_sectors(struct blk_integrity *bi,
						    unsigned int sectors)
{
	/* At this point there are only 512b or 4096b DIF/EPP devices */
	if (bi->sector_size == 4096)
		return sectors >>= 3;

	return sectors;
}

/**
 * bio_integrity_tag_size - Retrieve integrity tag space
 * @bio:	bio to inspect
 *
 * Description: Returns the maximum number of tag bytes that can be
 * attached to this bio. Filesystems can use this to determine how
 * much metadata to attach to an I/O.
 */
unsigned int bio_integrity_tag_size(struct bio *bio)
{
	struct blk_integrity *bi = bdev_get_integrity(bio->bi_bdev);

	BUG_ON(bio->bi_size == 0);

	return bi->tag_size * (bio->bi_size / bi->sector_size);
}
EXPORT_SYMBOL(bio_integrity_tag_size);

int bio_integrity_tag(struct bio *bio, void *tag_buf, unsigned int len, int set)
{
	struct bio_integrity_payload *bip = bio->bi_integrity;
	struct blk_integrity *bi = bdev_get_integrity(bio->bi_bdev);
	unsigned int nr_sectors;

	BUG_ON(bip->bip_buf == NULL);

	if (bi->tag_size == 0)
		return -1;

	nr_sectors = bio_integrity_hw_sectors(bi,
					DIV_ROUND_UP(len, bi->tag_size));

	if (nr_sectors * bi->tuple_size > bip->bip_size) {
		printk(KERN_ERR "%s: tag too big for bio: %u > %u\n",
		       __func__, nr_sectors * bi->tuple_size, bip->bip_size);
		return -1;
	}

	if (set)
		bi->set_tag_fn(bip->bip_buf, tag_buf, nr_sectors);
	else
		bi->get_tag_fn(bip->bip_buf, tag_buf, nr_sectors);

	return 0;
}

/**
 * bio_integrity_set_tag - Attach a tag buffer to a bio
 * @bio:	bio to attach buffer to
 * @tag_buf:	Pointer to a buffer containing tag data
 * @len:	Length of the included buffer
 *
 * Description: Use this function to tag a bio by leveraging the extra
 * space provided by devices formatted with integrity protection.  The
 * size of the integrity buffer must be <= to the size reported by
 * bio_integrity_tag_size().
 */
int bio_integrity_set_tag(struct bio *bio, void *tag_buf, unsigned int len)
{
	BUG_ON(bio_data_dir(bio) != WRITE);

	return bio_integrity_tag(bio, tag_buf, len, 1);
}
EXPORT_SYMBOL(bio_integrity_set_tag);

/**
 * bio_integrity_get_tag - Retrieve a tag buffer from a bio
 * @bio:	bio to retrieve buffer from
 * @tag_buf:	Pointer to a buffer for the tag data
 * @len:	Length of the target buffer
 *
 * Description: Use this function to retrieve the tag buffer from a
 * completed I/O. The size of the integrity buffer must be <= to the
 * size reported by bio_integrity_tag_size().
 */
int bio_integrity_get_tag(struct bio *bio, void *tag_buf, unsigned int len)
{
	BUG_ON(bio_data_dir(bio) != READ);

	return bio_integrity_tag(bio, tag_buf, len, 0);
}
EXPORT_SYMBOL(bio_integrity_get_tag);

/**
 * bio_integrity_generate - Generate integrity metadata for a bio
 * @bio:	bio to generate integrity metadata for
 *
 * Description: Generates integrity metadata for a bio by calling the
 * block device's generation callback function.  The bio must have a
 * bip attached with enough room to accommodate the generated
 * integrity metadata.
 */
static void bio_integrity_generate(struct bio *bio)
{
	struct blk_integrity *bi = bdev_get_integrity(bio->bi_bdev);
	struct blk_integrity_exchg bix;
	struct bio_vec *bv;
	sector_t sector = bio->bi_sector;
	unsigned int i, sectors, total;
	void *prot_buf = bio->bi_integrity->bip_buf;

	total = 0;
	bix.disk_name = bio->bi_bdev->bd_disk->disk_name;
	bix.sector_size = bi->sector_size;

	bio_for_each_segment(bv, bio, i) {
		void *kaddr = kmap_atomic(bv->bv_page, KM_USER0);
		bix.data_buf = kaddr + bv->bv_offset;
		bix.data_size = bv->bv_len;
		bix.prot_buf = prot_buf;
		bix.sector = sector;

		bi->generate_fn(&bix);

		sectors = bv->bv_len / bi->sector_size;
		sector += sectors;
		prot_buf += sectors * bi->tuple_size;
		total += sectors * bi->tuple_size;
		BUG_ON(total > bio->bi_integrity->bip_size);

		kunmap_atomic(kaddr, KM_USER0);
	}
}

static inline unsigned short blk_integrity_tuple_size(struct blk_integrity *bi)
{
	if (bi)
		return bi->tuple_size;

	return 0;
}

/**
 * bio_integrity_prep - Prepare bio for integrity I/O
 * @bio:	bio to prepare
 *
 * Description: Allocates a buffer for integrity metadata, maps the
 * pages and attaches them to a bio.  The bio must have data
 * direction, target device and start sector set priot to calling.  In
 * the WRITE case, integrity metadata will be generated using the
 * block device's integrity function.  In the READ case, the buffer
 * will be prepared for DMA and a suitable end_io handler set up.
 */
int bio_integrity_prep(struct bio *bio)
{
	struct bio_integrity_payload *bip;
	struct blk_integrity *bi;
	struct request_queue *q;
	void *buf;
	unsigned long start, end;
	unsigned int len, nr_pages;
	unsigned int bytes, offset, i;
	unsigned int sectors;

	bi = bdev_get_integrity(bio->bi_bdev);
	q = bdev_get_queue(bio->bi_bdev);
	BUG_ON(bi == NULL);
	BUG_ON(bio_integrity(bio));

	sectors = bio_integrity_hw_sectors(bi, bio_sectors(bio));

	/* Allocate kernel buffer for protection data */
	len = sectors * blk_integrity_tuple_size(bi);
	buf = kmalloc(len, GFP_NOIO | __GFP_NOFAIL | q->bounce_gfp);
	if (unlikely(buf == NULL)) {
		printk(KERN_ERR "could not allocate integrity buffer\n");
		return -EIO;
	}

	end = (((unsigned long) buf) + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	start = ((unsigned long) buf) >> PAGE_SHIFT;
	nr_pages = end - start;

	/* Allocate bio integrity payload and integrity vectors */
	bip = bio_integrity_alloc(bio, GFP_NOIO, nr_pages);
	if (unlikely(bip == NULL)) {
		printk(KERN_ERR "could not allocate data integrity bioset\n");
		kfree(buf);
		return -EIO;
	}

	bip->bip_buf = buf;
	bip->bip_size = len;
	bip->bip_sector = bio->bi_sector;

	/* Map it */
	offset = offset_in_page(buf);
	for (i = 0 ; i < nr_pages ; i++) {
		int ret;
		bytes = PAGE_SIZE - offset;

		if (len <= 0)
			break;

		if (bytes > len)
			bytes = len;

		ret = bio_integrity_add_page(bio, virt_to_page(buf),
					     bytes, offset);

		if (ret == 0)
			return 0;

		if (ret < bytes)
			break;

		buf += bytes;
		len -= bytes;
		offset = 0;
	}

	/* Install custom I/O completion handler if read verify is enabled */
	if (bio_data_dir(bio) == READ) {
		bip->bip_end_io = bio->bi_end_io;
		bio->bi_end_io = bio_integrity_endio;
	}

	/* Auto-generate integrity metadata if this is a write */
	if (bio_data_dir(bio) == WRITE)
		bio_integrity_generate(bio);

	return 0;
}
EXPORT_SYMBOL(bio_integrity_prep);

/**
 * bio_integrity_verify - Verify integrity metadata for a bio
 * @bio:	bio to verify
 *
 * Description: This function is called to verify the integrity of a
 * bio.	 The data in the bio io_vec is compared to the integrity
 * metadata returned by the HBA.
 */
static int bio_integrity_verify(struct bio *bio)
{
	struct blk_integrity *bi = bdev_get_integrity(bio->bi_bdev);
	struct blk_integrity_exchg bix;
	struct bio_vec *bv;
	sector_t sector = bio->bi_integrity->bip_sector;
	unsigned int i, sectors, total, ret;
	void *prot_buf = bio->bi_integrity->bip_buf;

	ret = total = 0;
	bix.disk_name = bio->bi_bdev->bd_disk->disk_name;
	bix.sector_size = bi->sector_size;

	bio_for_each_segment(bv, bio, i) {
		void *kaddr = kmap_atomic(bv->bv_page, KM_USER0);
		bix.data_buf = kaddr + bv->bv_offset;
		bix.data_size = bv->bv_len;
		bix.prot_buf = prot_buf;
		bix.sector = sector;

		ret = bi->verify_fn(&bix);

		if (ret) {
			kunmap_atomic(kaddr, KM_USER0);
			break;
		}

		sectors = bv->bv_len / bi->sector_size;
		sector += sectors;
		prot_buf += sectors * bi->tuple_size;
		total += sectors * bi->tuple_size;
		BUG_ON(total > bio->bi_integrity->bip_size);

		kunmap_atomic(kaddr, KM_USER0);
	}

	return ret;
}

/**
 * bio_integrity_verify_fn - Integrity I/O completion worker
 * @work:	Work struct stored in bio to be verified
 *
 * Description: This workqueue function is called to complete a READ
 * request.  The function verifies the transferred integrity metadata
 * and then calls the original bio end_io function.
 */
static void bio_integrity_verify_fn(struct work_struct *work)
{
	struct bio_integrity_payload *bip =
		container_of(work, struct bio_integrity_payload, bip_work);
	struct bio *bio = bip->bip_bio;
	int error = bip->bip_error;

	if (bio_integrity_verify(bio)) {
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
		error = -EIO;
	}

	/* Restore original bio completion handler */
	bio->bi_end_io = bip->bip_end_io;

	if (bio->bi_end_io)
		bio->bi_end_io(bio, error);
}

/**
 * bio_integrity_endio - Integrity I/O completion function
 * @bio:	Protected bio
 * @error:	Pointer to errno
 *
 * Description: Completion for integrity I/O
 *
 * Normally I/O completion is done in interrupt context.  However,
 * verifying I/O integrity is a time-consuming task which must be run
 * in process context.	This function postpones completion
 * accordingly.
 */
void bio_integrity_endio(struct bio *bio, int error)
{
	struct bio_integrity_payload *bip = bio->bi_integrity;

	BUG_ON(bip->bip_bio != bio);

	bip->bip_error = error;
	INIT_WORK(&bip->bip_work, bio_integrity_verify_fn);
	queue_work(kintegrityd_wq, &bip->bip_work);
}
EXPORT_SYMBOL(bio_integrity_endio);

/**
 * bio_integrity_mark_head - Advance bip_vec skip bytes
 * @bip:	Integrity vector to advance
 * @skip:	Number of bytes to advance it
 */
void bio_integrity_mark_head(struct bio_integrity_payload *bip,
			     unsigned int skip)
{
	struct bio_vec *iv;
	unsigned int i;

	bip_for_each_vec(iv, bip, i) {
		if (skip == 0) {
			bip->bip_idx = i;
			return;
		} else if (skip >= iv->bv_len) {
			skip -= iv->bv_len;
		} else { /* skip < iv->bv_len) */
			iv->bv_offset += skip;
			iv->bv_len -= skip;
			bip->bip_idx = i;
			return;
		}
	}
}

/**
 * bio_integrity_mark_tail - Truncate bip_vec to be len bytes long
 * @bip:	Integrity vector to truncate
 * @len:	New length of integrity vector
 */
void bio_integrity_mark_tail(struct bio_integrity_payload *bip,
			     unsigned int len)
{
	struct bio_vec *iv;
	unsigned int i;

	bip_for_each_vec(iv, bip, i) {
		if (len == 0) {
			bip->bip_vcnt = i;
			return;
		} else if (len >= iv->bv_len) {
			len -= iv->bv_len;
		} else { /* len < iv->bv_len) */
			iv->bv_len = len;
			len = 0;
		}
	}
}

/**
 * bio_integrity_advance - Advance integrity vector
 * @bio:	bio whose integrity vector to update
 * @bytes_done:	number of data bytes that have been completed
 *
 * Description: This function calculates how many integrity bytes the
 * number of completed data bytes correspond to and advances the
 * integrity vector accordingly.
 */
void bio_integrity_advance(struct bio *bio, unsigned int bytes_done)
{
	struct bio_integrity_payload *bip = bio->bi_integrity;
	struct blk_integrity *bi = bdev_get_integrity(bio->bi_bdev);
	unsigned int nr_sectors;

	BUG_ON(bip == NULL);
	BUG_ON(bi == NULL);

	nr_sectors = bio_integrity_hw_sectors(bi, bytes_done >> 9);
	bio_integrity_mark_head(bip, nr_sectors * bi->tuple_size);
}
EXPORT_SYMBOL(bio_integrity_advance);

/**
 * bio_integrity_trim - Trim integrity vector
 * @bio:	bio whose integrity vector to update
 * @offset:	offset to first data sector
 * @sectors:	number of data sectors
 *
 * Description: Used to trim the integrity vector in a cloned bio.
 * The ivec will be advanced corresponding to 'offset' data sectors
 * and the length will be truncated corresponding to 'len' data
 * sectors.
 */
void bio_integrity_trim(struct bio *bio, unsigned int offset,
			unsigned int sectors)
{
	struct bio_integrity_payload *bip = bio->bi_integrity;
	struct blk_integrity *bi = bdev_get_integrity(bio->bi_bdev);
	unsigned int nr_sectors;

	BUG_ON(bip == NULL);
	BUG_ON(bi == NULL);
	BUG_ON(!bio_flagged(bio, BIO_CLONED));

	nr_sectors = bio_integrity_hw_sectors(bi, sectors);
	bip->bip_sector = bip->bip_sector + offset;
	bio_integrity_mark_head(bip, offset * bi->tuple_size);
	bio_integrity_mark_tail(bip, sectors * bi->tuple_size);
}
EXPORT_SYMBOL(bio_integrity_trim);

/**
 * bio_integrity_split - Split integrity metadata
 * @bio:	Protected bio
 * @bp:		Resulting bio_pair
 * @sectors:	Offset
 *
 * Description: Splits an integrity page into a bio_pair.
 */
void bio_integrity_split(struct bio *bio, struct bio_pair *bp, int sectors)
{
	struct blk_integrity *bi;
	struct bio_integrity_payload *bip = bio->bi_integrity;
	unsigned int nr_sectors;

	if (bio_integrity(bio) == 0)
		return;

	bi = bdev_get_integrity(bio->bi_bdev);
	BUG_ON(bi == NULL);
	BUG_ON(bip->bip_vcnt != 1);

	nr_sectors = bio_integrity_hw_sectors(bi, sectors);

	bp->bio1.bi_integrity = &bp->bip1;
	bp->bio2.bi_integrity = &bp->bip2;

	bp->iv1 = bip->bip_vec[0];
	bp->iv2 = bip->bip_vec[0];

	bp->bip1.bip_vec = &bp->iv1;
	bp->bip2.bip_vec = &bp->iv2;

	bp->iv1.bv_len = sectors * bi->tuple_size;
	bp->iv2.bv_offset += sectors * bi->tuple_size;
	bp->iv2.bv_len -= sectors * bi->tuple_size;

	bp->bip1.bip_sector = bio->bi_integrity->bip_sector;
	bp->bip2.bip_sector = bio->bi_integrity->bip_sector + nr_sectors;

	bp->bip1.bip_vcnt = bp->bip2.bip_vcnt = 1;
	bp->bip1.bip_idx = bp->bip2.bip_idx = 0;
}
EXPORT_SYMBOL(bio_integrity_split);

/**
 * bio_integrity_clone - Callback for cloning bios with integrity metadata
 * @bio:	New bio
 * @bio_src:	Original bio
 * @bs:		bio_set to allocate bip from
 *
 * Description:	Called to allocate a bip when cloning a bio
 */
int bio_integrity_clone(struct bio *bio, struct bio *bio_src,
			struct bio_set *bs)
{
	struct bio_integrity_payload *bip_src = bio_src->bi_integrity;
	struct bio_integrity_payload *bip;

	BUG_ON(bip_src == NULL);

	bip = bio_integrity_alloc_bioset(bio, GFP_NOIO, bip_src->bip_vcnt, bs);

	if (bip == NULL)
		return -EIO;

	memcpy(bip->bip_vec, bip_src->bip_vec,
	       bip_src->bip_vcnt * sizeof(struct bio_vec));

	bip->bip_sector = bip_src->bip_sector;
	bip->bip_vcnt = bip_src->bip_vcnt;
	bip->bip_idx = bip_src->bip_idx;

	return 0;
}
EXPORT_SYMBOL(bio_integrity_clone);

int bioset_integrity_create(struct bio_set *bs, int pool_size)
{
	bs->bio_integrity_pool = mempool_create_slab_pool(pool_size,
							  bio_integrity_slab);
	if (!bs->bio_integrity_pool)
		return -1;

	return 0;
}
EXPORT_SYMBOL(bioset_integrity_create);

void bioset_integrity_free(struct bio_set *bs)
{
	if (bs->bio_integrity_pool)
		mempool_destroy(bs->bio_integrity_pool);
}
EXPORT_SYMBOL(bioset_integrity_free);

void __init bio_integrity_init_slab(void)
{
	bio_integrity_slab = KMEM_CACHE(bio_integrity_payload,
					SLAB_HWCACHE_ALIGN|SLAB_PANIC);
}

static int __init integrity_init(void)
{
	kintegrityd_wq = create_workqueue("kintegrityd");

	if (!kintegrityd_wq)
		panic("Failed to create kintegrityd\n");

	return 0;
}
subsys_initcall(integrity_init);

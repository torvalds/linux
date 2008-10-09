/*
 * 2.5 block I/O model
 *
 * Copyright (C) 2001 Jens Axboe <axboe@suse.de>
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
 */
#ifndef __LINUX_BIO_H
#define __LINUX_BIO_H

#include <linux/highmem.h>
#include <linux/mempool.h>
#include <linux/ioprio.h>

#ifdef CONFIG_BLOCK

#include <asm/io.h>

#define BIO_DEBUG

#ifdef BIO_DEBUG
#define BIO_BUG_ON	BUG_ON
#else
#define BIO_BUG_ON
#endif

#define BIO_MAX_PAGES		256
#define BIO_MAX_SIZE		(BIO_MAX_PAGES << PAGE_CACHE_SHIFT)
#define BIO_MAX_SECTORS		(BIO_MAX_SIZE >> 9)

/*
 * was unsigned short, but we might as well be ready for > 64kB I/O pages
 */
struct bio_vec {
	struct page	*bv_page;
	unsigned int	bv_len;
	unsigned int	bv_offset;
};

struct bio_set;
struct bio;
struct bio_integrity_payload;
typedef void (bio_end_io_t) (struct bio *, int);
typedef void (bio_destructor_t) (struct bio *);

/*
 * main unit of I/O for the block layer and lower layers (ie drivers and
 * stacking drivers)
 */
struct bio {
	sector_t		bi_sector;	/* device address in 512 byte
						   sectors */
	struct bio		*bi_next;	/* request queue link */
	struct block_device	*bi_bdev;
	unsigned long		bi_flags;	/* status, command, etc */
	unsigned long		bi_rw;		/* bottom bits READ/WRITE,
						 * top bits priority
						 */

	unsigned short		bi_vcnt;	/* how many bio_vec's */
	unsigned short		bi_idx;		/* current index into bvl_vec */

	/* Number of segments in this BIO after
	 * physical address coalescing is performed.
	 */
	unsigned int		bi_phys_segments;

	unsigned int		bi_size;	/* residual I/O count */

	unsigned int		bi_max_vecs;	/* max bvl_vecs we can hold */

	unsigned int		bi_comp_cpu;	/* completion CPU */

	struct bio_vec		*bi_io_vec;	/* the actual vec list */

	bio_end_io_t		*bi_end_io;
	atomic_t		bi_cnt;		/* pin count */

	void			*bi_private;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	struct bio_integrity_payload *bi_integrity;  /* data integrity */
#endif

	bio_destructor_t	*bi_destructor;	/* destructor */
};

/*
 * bio flags
 */
#define BIO_UPTODATE	0	/* ok after I/O completion */
#define BIO_RW_BLOCK	1	/* RW_AHEAD set, and read/write would block */
#define BIO_EOF		2	/* out-out-bounds error */
#define BIO_SEG_VALID	3	/* bi_phys_segments valid */
#define BIO_CLONED	4	/* doesn't own data */
#define BIO_BOUNCED	5	/* bio is a bounce bio */
#define BIO_USER_MAPPED 6	/* contains user pages */
#define BIO_EOPNOTSUPP	7	/* not supported */
#define BIO_CPU_AFFINE	8	/* complete bio on same CPU as submitted */
#define BIO_NULL_MAPPED 9	/* contains invalid user pages */
#define BIO_FS_INTEGRITY 10	/* fs owns integrity data, not block layer */
#define bio_flagged(bio, flag)	((bio)->bi_flags & (1 << (flag)))

/*
 * top 4 bits of bio flags indicate the pool this bio came from
 */
#define BIO_POOL_BITS		(4)
#define BIO_POOL_OFFSET		(BITS_PER_LONG - BIO_POOL_BITS)
#define BIO_POOL_MASK		(1UL << BIO_POOL_OFFSET)
#define BIO_POOL_IDX(bio)	((bio)->bi_flags >> BIO_POOL_OFFSET)	

/*
 * bio bi_rw flags
 *
 * bit 0 -- read (not set) or write (set)
 * bit 1 -- rw-ahead when set
 * bit 2 -- barrier
 * bit 3 -- fail fast, don't want low level driver retries
 * bit 4 -- synchronous I/O hint: the block layer will unplug immediately
 * bit 5 -- metadata request
 * bit 6 -- discard sectors
 */
#define BIO_RW		0	/* Must match RW in req flags (blkdev.h) */
#define BIO_RW_AHEAD	1	/* Must match FAILFAST in req flags */
#define BIO_RW_BARRIER	2
#define BIO_RW_FAILFAST	3
#define BIO_RW_SYNC	4
#define BIO_RW_META	5
#define BIO_RW_DISCARD	6

/*
 * upper 16 bits of bi_rw define the io priority of this bio
 */
#define BIO_PRIO_SHIFT	(8 * sizeof(unsigned long) - IOPRIO_BITS)
#define bio_prio(bio)	((bio)->bi_rw >> BIO_PRIO_SHIFT)
#define bio_prio_valid(bio)	ioprio_valid(bio_prio(bio))

#define bio_set_prio(bio, prio)		do {			\
	WARN_ON(prio >= (1 << IOPRIO_BITS));			\
	(bio)->bi_rw &= ((1UL << BIO_PRIO_SHIFT) - 1);		\
	(bio)->bi_rw |= ((unsigned long) (prio) << BIO_PRIO_SHIFT);	\
} while (0)

/*
 * various member access, note that bio_data should of course not be used
 * on highmem page vectors
 */
#define bio_iovec_idx(bio, idx)	(&((bio)->bi_io_vec[(idx)]))
#define bio_iovec(bio)		bio_iovec_idx((bio), (bio)->bi_idx)
#define bio_page(bio)		bio_iovec((bio))->bv_page
#define bio_offset(bio)		bio_iovec((bio))->bv_offset
#define bio_segments(bio)	((bio)->bi_vcnt - (bio)->bi_idx)
#define bio_sectors(bio)	((bio)->bi_size >> 9)
#define bio_barrier(bio)	((bio)->bi_rw & (1 << BIO_RW_BARRIER))
#define bio_sync(bio)		((bio)->bi_rw & (1 << BIO_RW_SYNC))
#define bio_failfast(bio)	((bio)->bi_rw & (1 << BIO_RW_FAILFAST))
#define bio_rw_ahead(bio)	((bio)->bi_rw & (1 << BIO_RW_AHEAD))
#define bio_rw_meta(bio)	((bio)->bi_rw & (1 << BIO_RW_META))
#define bio_discard(bio)	((bio)->bi_rw & (1 << BIO_RW_DISCARD))
#define bio_empty_barrier(bio)	(bio_barrier(bio) && !bio_has_data(bio) && !bio_discard(bio))

static inline unsigned int bio_cur_sectors(struct bio *bio)
{
	if (bio->bi_vcnt)
		return bio_iovec(bio)->bv_len >> 9;
	else /* dataless requests such as discard */
		return bio->bi_size >> 9;
}

static inline void *bio_data(struct bio *bio)
{
	if (bio->bi_vcnt)
		return page_address(bio_page(bio)) + bio_offset(bio);

	return NULL;
}

/*
 * will die
 */
#define bio_to_phys(bio)	(page_to_phys(bio_page((bio))) + (unsigned long) bio_offset((bio)))
#define bvec_to_phys(bv)	(page_to_phys((bv)->bv_page) + (unsigned long) (bv)->bv_offset)

/*
 * queues that have highmem support enabled may still need to revert to
 * PIO transfers occasionally and thus map high pages temporarily. For
 * permanent PIO fall back, user is probably better off disabling highmem
 * I/O completely on that queue (see ide-dma for example)
 */
#define __bio_kmap_atomic(bio, idx, kmtype)				\
	(kmap_atomic(bio_iovec_idx((bio), (idx))->bv_page, kmtype) +	\
		bio_iovec_idx((bio), (idx))->bv_offset)

#define __bio_kunmap_atomic(addr, kmtype) kunmap_atomic(addr, kmtype)

/*
 * merge helpers etc
 */

#define __BVEC_END(bio)		bio_iovec_idx((bio), (bio)->bi_vcnt - 1)
#define __BVEC_START(bio)	bio_iovec_idx((bio), (bio)->bi_idx)

/*
 * allow arch override, for eg virtualized architectures (put in asm/io.h)
 */
#ifndef BIOVEC_PHYS_MERGEABLE
#define BIOVEC_PHYS_MERGEABLE(vec1, vec2)	\
	((bvec_to_phys((vec1)) + (vec1)->bv_len) == bvec_to_phys((vec2)))
#endif

#define __BIO_SEG_BOUNDARY(addr1, addr2, mask) \
	(((addr1) | (mask)) == (((addr2) - 1) | (mask)))
#define BIOVEC_SEG_BOUNDARY(q, b1, b2) \
	__BIO_SEG_BOUNDARY(bvec_to_phys((b1)), bvec_to_phys((b2)) + (b2)->bv_len, (q)->seg_boundary_mask)
#define BIO_SEG_BOUNDARY(q, b1, b2) \
	BIOVEC_SEG_BOUNDARY((q), __BVEC_END((b1)), __BVEC_START((b2)))

#define bio_io_error(bio) bio_endio((bio), -EIO)

/*
 * drivers should not use the __ version unless they _really_ want to
 * run through the entire bio and not just pending pieces
 */
#define __bio_for_each_segment(bvl, bio, i, start_idx)			\
	for (bvl = bio_iovec_idx((bio), (start_idx)), i = (start_idx);	\
	     i < (bio)->bi_vcnt;					\
	     bvl++, i++)

#define bio_for_each_segment(bvl, bio, i)				\
	__bio_for_each_segment(bvl, bio, i, (bio)->bi_idx)

/*
 * get a reference to a bio, so it won't disappear. the intended use is
 * something like:
 *
 * bio_get(bio);
 * submit_bio(rw, bio);
 * if (bio->bi_flags ...)
 *	do_something
 * bio_put(bio);
 *
 * without the bio_get(), it could potentially complete I/O before submit_bio
 * returns. and then bio would be freed memory when if (bio->bi_flags ...)
 * runs
 */
#define bio_get(bio)	atomic_inc(&(bio)->bi_cnt)

#if defined(CONFIG_BLK_DEV_INTEGRITY)
/*
 * bio integrity payload
 */
struct bio_integrity_payload {
	struct bio		*bip_bio;	/* parent bio */
	struct bio_vec		*bip_vec;	/* integrity data vector */

	sector_t		bip_sector;	/* virtual start sector */

	void			*bip_buf;	/* generated integrity data */
	bio_end_io_t		*bip_end_io;	/* saved I/O completion fn */

	int			bip_error;	/* saved I/O error */
	unsigned int		bip_size;

	unsigned short		bip_pool;	/* pool the ivec came from */
	unsigned short		bip_vcnt;	/* # of integrity bio_vecs */
	unsigned short		bip_idx;	/* current bip_vec index */

	struct work_struct	bip_work;	/* I/O completion */
};
#endif /* CONFIG_BLK_DEV_INTEGRITY */

/*
 * A bio_pair is used when we need to split a bio.
 * This can only happen for a bio that refers to just one
 * page of data, and in the unusual situation when the
 * page crosses a chunk/device boundary
 *
 * The address of the master bio is stored in bio1.bi_private
 * The address of the pool the pair was allocated from is stored
 *   in bio2.bi_private
 */
struct bio_pair {
	struct bio			bio1, bio2;
	struct bio_vec			bv1, bv2;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	struct bio_integrity_payload	bip1, bip2;
	struct bio_vec			iv1, iv2;
#endif
	atomic_t			cnt;
	int				error;
};
extern struct bio_pair *bio_split(struct bio *bi, int first_sectors);
extern void bio_pair_release(struct bio_pair *dbio);

extern struct bio_set *bioset_create(int, int);
extern void bioset_free(struct bio_set *);

extern struct bio *bio_alloc(gfp_t, int);
extern struct bio *bio_kmalloc(gfp_t, int);
extern struct bio *bio_alloc_bioset(gfp_t, int, struct bio_set *);
extern void bio_put(struct bio *);
extern void bio_free(struct bio *, struct bio_set *);

extern void bio_endio(struct bio *, int);
struct request_queue;
extern int bio_phys_segments(struct request_queue *, struct bio *);

extern void __bio_clone(struct bio *, struct bio *);
extern struct bio *bio_clone(struct bio *, gfp_t);

extern void bio_init(struct bio *);

extern int bio_add_page(struct bio *, struct page *, unsigned int,unsigned int);
extern int bio_add_pc_page(struct request_queue *, struct bio *, struct page *,
			   unsigned int, unsigned int);
extern int bio_get_nr_vecs(struct block_device *);
extern sector_t bio_sector_offset(struct bio *, unsigned short, unsigned int);
extern struct bio *bio_map_user(struct request_queue *, struct block_device *,
				unsigned long, unsigned int, int, gfp_t);
struct sg_iovec;
struct rq_map_data;
extern struct bio *bio_map_user_iov(struct request_queue *,
				    struct block_device *,
				    struct sg_iovec *, int, int, gfp_t);
extern void bio_unmap_user(struct bio *);
extern struct bio *bio_map_kern(struct request_queue *, void *, unsigned int,
				gfp_t);
extern struct bio *bio_copy_kern(struct request_queue *, void *, unsigned int,
				 gfp_t, int);
extern void bio_set_pages_dirty(struct bio *bio);
extern void bio_check_pages_dirty(struct bio *bio);
extern struct bio *bio_copy_user(struct request_queue *, struct rq_map_data *,
				 unsigned long, unsigned int, int, gfp_t);
extern struct bio *bio_copy_user_iov(struct request_queue *,
				     struct rq_map_data *, struct sg_iovec *,
				     int, int, gfp_t);
extern int bio_uncopy_user(struct bio *);
void zero_fill_bio(struct bio *bio);
extern struct bio_vec *bvec_alloc_bs(gfp_t, int, unsigned long *, struct bio_set *);
extern unsigned int bvec_nr_vecs(unsigned short idx);

/*
 * Allow queuer to specify a completion CPU for this bio
 */
static inline void bio_set_completion_cpu(struct bio *bio, unsigned int cpu)
{
	bio->bi_comp_cpu = cpu;
}

/*
 * bio_set is used to allow other portions of the IO system to
 * allocate their own private memory pools for bio and iovec structures.
 * These memory pools in turn all allocate from the bio_slab
 * and the bvec_slabs[].
 */
#define BIO_POOL_SIZE 2
#define BIOVEC_NR_POOLS 6

struct bio_set {
	mempool_t *bio_pool;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	mempool_t *bio_integrity_pool;
#endif
	mempool_t *bvec_pools[BIOVEC_NR_POOLS];
};

struct biovec_slab {
	int nr_vecs;
	char *name;
	struct kmem_cache *slab;
};

extern struct bio_set *fs_bio_set;

/*
 * a small number of entries is fine, not going to be performance critical.
 * basically we just need to survive
 */
#define BIO_SPLIT_ENTRIES 2

#ifdef CONFIG_HIGHMEM
/*
 * remember to add offset! and never ever reenable interrupts between a
 * bvec_kmap_irq and bvec_kunmap_irq!!
 *
 * This function MUST be inlined - it plays with the CPU interrupt flags.
 */
static inline char *bvec_kmap_irq(struct bio_vec *bvec, unsigned long *flags)
{
	unsigned long addr;

	/*
	 * might not be a highmem page, but the preempt/irq count
	 * balancing is a lot nicer this way
	 */
	local_irq_save(*flags);
	addr = (unsigned long) kmap_atomic(bvec->bv_page, KM_BIO_SRC_IRQ);

	BUG_ON(addr & ~PAGE_MASK);

	return (char *) addr + bvec->bv_offset;
}

static inline void bvec_kunmap_irq(char *buffer, unsigned long *flags)
{
	unsigned long ptr = (unsigned long) buffer & PAGE_MASK;

	kunmap_atomic((void *) ptr, KM_BIO_SRC_IRQ);
	local_irq_restore(*flags);
}

#else
#define bvec_kmap_irq(bvec, flags)	(page_address((bvec)->bv_page) + (bvec)->bv_offset)
#define bvec_kunmap_irq(buf, flags)	do { *(flags) = 0; } while (0)
#endif

static inline char *__bio_kmap_irq(struct bio *bio, unsigned short idx,
				   unsigned long *flags)
{
	return bvec_kmap_irq(bio_iovec_idx(bio, idx), flags);
}
#define __bio_kunmap_irq(buf, flags)	bvec_kunmap_irq(buf, flags)

#define bio_kmap_irq(bio, flags) \
	__bio_kmap_irq((bio), (bio)->bi_idx, (flags))
#define bio_kunmap_irq(buf,flags)	__bio_kunmap_irq(buf, flags)

/*
 * Check whether this bio carries any data or not. A NULL bio is allowed.
 */
static inline int bio_has_data(struct bio *bio)
{
	return bio && bio->bi_io_vec != NULL;
}

#if defined(CONFIG_BLK_DEV_INTEGRITY)

#define bip_vec_idx(bip, idx)	(&(bip->bip_vec[(idx)]))
#define bip_vec(bip)		bip_vec_idx(bip, 0)

#define __bip_for_each_vec(bvl, bip, i, start_idx)			\
	for (bvl = bip_vec_idx((bip), (start_idx)), i = (start_idx);	\
	     i < (bip)->bip_vcnt;					\
	     bvl++, i++)

#define bip_for_each_vec(bvl, bip, i)					\
	__bip_for_each_vec(bvl, bip, i, (bip)->bip_idx)

#define bio_integrity(bio) (bio->bi_integrity != NULL)

extern struct bio_integrity_payload *bio_integrity_alloc_bioset(struct bio *, gfp_t, unsigned int, struct bio_set *);
extern struct bio_integrity_payload *bio_integrity_alloc(struct bio *, gfp_t, unsigned int);
extern void bio_integrity_free(struct bio *, struct bio_set *);
extern int bio_integrity_add_page(struct bio *, struct page *, unsigned int, unsigned int);
extern int bio_integrity_enabled(struct bio *bio);
extern int bio_integrity_set_tag(struct bio *, void *, unsigned int);
extern int bio_integrity_get_tag(struct bio *, void *, unsigned int);
extern int bio_integrity_prep(struct bio *);
extern void bio_integrity_endio(struct bio *, int);
extern void bio_integrity_advance(struct bio *, unsigned int);
extern void bio_integrity_trim(struct bio *, unsigned int, unsigned int);
extern void bio_integrity_split(struct bio *, struct bio_pair *, int);
extern int bio_integrity_clone(struct bio *, struct bio *, struct bio_set *);
extern int bioset_integrity_create(struct bio_set *, int);
extern void bioset_integrity_free(struct bio_set *);
extern void bio_integrity_init_slab(void);

#else /* CONFIG_BLK_DEV_INTEGRITY */

#define bio_integrity(a)		(0)
#define bioset_integrity_create(a, b)	(0)
#define bio_integrity_prep(a)		(0)
#define bio_integrity_enabled(a)	(0)
#define bio_integrity_clone(a, b, c)	(0)
#define bioset_integrity_free(a)	do { } while (0)
#define bio_integrity_free(a, b)	do { } while (0)
#define bio_integrity_endio(a, b)	do { } while (0)
#define bio_integrity_advance(a, b)	do { } while (0)
#define bio_integrity_trim(a, b, c)	do { } while (0)
#define bio_integrity_split(a, b, c)	do { } while (0)
#define bio_integrity_set_tag(a, b, c)	do { } while (0)
#define bio_integrity_get_tag(a, b, c)	do { } while (0)
#define bio_integrity_init_slab(a)	do { } while (0)

#endif /* CONFIG_BLK_DEV_INTEGRITY */

#endif /* CONFIG_BLOCK */
#endif /* __LINUX_BIO_H */

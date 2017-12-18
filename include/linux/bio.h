/*
 * Copyright (C) 2001 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *
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
#include <linux/bug.h>

#ifdef CONFIG_BLOCK

#include <asm/io.h>

/* struct bio, bio_vec and BIO_* flags are defined in blk_types.h */
#include <linux/blk_types.h>

#define BIO_DEBUG

#ifdef BIO_DEBUG
#define BIO_BUG_ON	BUG_ON
#else
#define BIO_BUG_ON
#endif

#ifdef CONFIG_THP_SWAP
#if HPAGE_PMD_NR > 256
#define BIO_MAX_PAGES		HPAGE_PMD_NR
#else
#define BIO_MAX_PAGES		256
#endif
#else
#define BIO_MAX_PAGES		256
#endif

#define bio_prio(bio)			(bio)->bi_ioprio
#define bio_set_prio(bio, prio)		((bio)->bi_ioprio = prio)

#define bio_iter_iovec(bio, iter)				\
	bvec_iter_bvec((bio)->bi_io_vec, (iter))

#define bio_iter_page(bio, iter)				\
	bvec_iter_page((bio)->bi_io_vec, (iter))
#define bio_iter_len(bio, iter)					\
	bvec_iter_len((bio)->bi_io_vec, (iter))
#define bio_iter_offset(bio, iter)				\
	bvec_iter_offset((bio)->bi_io_vec, (iter))

#define bio_page(bio)		bio_iter_page((bio), (bio)->bi_iter)
#define bio_offset(bio)		bio_iter_offset((bio), (bio)->bi_iter)
#define bio_iovec(bio)		bio_iter_iovec((bio), (bio)->bi_iter)

#define bio_multiple_segments(bio)				\
	((bio)->bi_iter.bi_size != bio_iovec(bio).bv_len)
#define bio_sectors(bio)	((bio)->bi_iter.bi_size >> 9)
#define bio_end_sector(bio)	((bio)->bi_iter.bi_sector + bio_sectors((bio)))

/*
 * Return the data direction, READ or WRITE.
 */
#define bio_data_dir(bio) \
	(op_is_write(bio_op(bio)) ? WRITE : READ)

/*
 * Check whether this bio carries any data or not. A NULL bio is allowed.
 */
static inline bool bio_has_data(struct bio *bio)
{
	if (bio &&
	    bio->bi_iter.bi_size &&
	    bio_op(bio) != REQ_OP_DISCARD &&
	    bio_op(bio) != REQ_OP_SECURE_ERASE &&
	    bio_op(bio) != REQ_OP_WRITE_ZEROES)
		return true;

	return false;
}

static inline bool bio_no_advance_iter(struct bio *bio)
{
	return bio_op(bio) == REQ_OP_DISCARD ||
	       bio_op(bio) == REQ_OP_SECURE_ERASE ||
	       bio_op(bio) == REQ_OP_WRITE_SAME ||
	       bio_op(bio) == REQ_OP_WRITE_ZEROES;
}

static inline bool bio_mergeable(struct bio *bio)
{
	if (bio->bi_opf & REQ_NOMERGE_FLAGS)
		return false;

	return true;
}

static inline unsigned int bio_cur_bytes(struct bio *bio)
{
	if (bio_has_data(bio))
		return bio_iovec(bio).bv_len;
	else /* dataless requests such as discard */
		return bio->bi_iter.bi_size;
}

static inline void *bio_data(struct bio *bio)
{
	if (bio_has_data(bio))
		return page_address(bio_page(bio)) + bio_offset(bio);

	return NULL;
}

/*
 * will die
 */
#define bvec_to_phys(bv)	(page_to_phys((bv)->bv_page) + (unsigned long) (bv)->bv_offset)

/*
 * merge helpers etc
 */

/* Default implementation of BIOVEC_PHYS_MERGEABLE */
#define __BIOVEC_PHYS_MERGEABLE(vec1, vec2)	\
	((bvec_to_phys((vec1)) + (vec1)->bv_len) == bvec_to_phys((vec2)))

/*
 * allow arch override, for eg virtualized architectures (put in asm/io.h)
 */
#ifndef BIOVEC_PHYS_MERGEABLE
#define BIOVEC_PHYS_MERGEABLE(vec1, vec2)	\
	__BIOVEC_PHYS_MERGEABLE(vec1, vec2)
#endif

#define __BIO_SEG_BOUNDARY(addr1, addr2, mask) \
	(((addr1) | (mask)) == (((addr2) - 1) | (mask)))
#define BIOVEC_SEG_BOUNDARY(q, b1, b2) \
	__BIO_SEG_BOUNDARY(bvec_to_phys((b1)), bvec_to_phys((b2)) + (b2)->bv_len, queue_segment_boundary((q)))

/*
 * drivers should _never_ use the all version - the bio may have been split
 * before it got to the driver and the driver won't own all of it
 */
#define bio_for_each_segment_all(bvl, bio, i)				\
	for (i = 0, bvl = (bio)->bi_io_vec; i < (bio)->bi_vcnt; i++, bvl++)

static inline void bio_advance_iter(struct bio *bio, struct bvec_iter *iter,
				    unsigned bytes)
{
	iter->bi_sector += bytes >> 9;

	if (bio_no_advance_iter(bio)) {
		iter->bi_size -= bytes;
		iter->bi_done += bytes;
	} else {
		bvec_iter_advance(bio->bi_io_vec, iter, bytes);
		/* TODO: It is reasonable to complete bio with error here. */
	}
}

static inline bool bio_rewind_iter(struct bio *bio, struct bvec_iter *iter,
		unsigned int bytes)
{
	iter->bi_sector -= bytes >> 9;

	if (bio_no_advance_iter(bio)) {
		iter->bi_size += bytes;
		iter->bi_done -= bytes;
		return true;
	}

	return bvec_iter_rewind(bio->bi_io_vec, iter, bytes);
}

#define __bio_for_each_segment(bvl, bio, iter, start)			\
	for (iter = (start);						\
	     (iter).bi_size &&						\
		((bvl = bio_iter_iovec((bio), (iter))), 1);		\
	     bio_advance_iter((bio), &(iter), (bvl).bv_len))

#define bio_for_each_segment(bvl, bio, iter)				\
	__bio_for_each_segment(bvl, bio, iter, (bio)->bi_iter)

#define bio_iter_last(bvec, iter) ((iter).bi_size == (bvec).bv_len)

static inline unsigned bio_segments(struct bio *bio)
{
	unsigned segs = 0;
	struct bio_vec bv;
	struct bvec_iter iter;

	/*
	 * We special case discard/write same/write zeroes, because they
	 * interpret bi_size differently:
	 */

	switch (bio_op(bio)) {
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
	case REQ_OP_WRITE_ZEROES:
		return 0;
	case REQ_OP_WRITE_SAME:
		return 1;
	default:
		break;
	}

	bio_for_each_segment(bv, bio, iter)
		segs++;

	return segs;
}

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
static inline void bio_get(struct bio *bio)
{
	bio->bi_flags |= (1 << BIO_REFFED);
	smp_mb__before_atomic();
	atomic_inc(&bio->__bi_cnt);
}

static inline void bio_cnt_set(struct bio *bio, unsigned int count)
{
	if (count != 1) {
		bio->bi_flags |= (1 << BIO_REFFED);
		smp_mb__before_atomic();
	}
	atomic_set(&bio->__bi_cnt, count);
}

static inline bool bio_flagged(struct bio *bio, unsigned int bit)
{
	return (bio->bi_flags & (1U << bit)) != 0;
}

static inline void bio_set_flag(struct bio *bio, unsigned int bit)
{
	bio->bi_flags |= (1U << bit);
}

static inline void bio_clear_flag(struct bio *bio, unsigned int bit)
{
	bio->bi_flags &= ~(1U << bit);
}

static inline void bio_get_first_bvec(struct bio *bio, struct bio_vec *bv)
{
	*bv = bio_iovec(bio);
}

static inline void bio_get_last_bvec(struct bio *bio, struct bio_vec *bv)
{
	struct bvec_iter iter = bio->bi_iter;
	int idx;

	if (unlikely(!bio_multiple_segments(bio))) {
		*bv = bio_iovec(bio);
		return;
	}

	bio_advance_iter(bio, &iter, iter.bi_size);

	if (!iter.bi_bvec_done)
		idx = iter.bi_idx - 1;
	else	/* in the middle of bvec */
		idx = iter.bi_idx;

	*bv = bio->bi_io_vec[idx];

	/*
	 * iter.bi_bvec_done records actual length of the last bvec
	 * if this bio ends in the middle of one io vector
	 */
	if (iter.bi_bvec_done)
		bv->bv_len = iter.bi_bvec_done;
}

static inline unsigned bio_pages_all(struct bio *bio)
{
	WARN_ON_ONCE(bio_flagged(bio, BIO_CLONED));
	return bio->bi_vcnt;
}

static inline struct bio_vec *bio_first_bvec_all(struct bio *bio)
{
	WARN_ON_ONCE(bio_flagged(bio, BIO_CLONED));
	return bio->bi_io_vec;
}

static inline struct page *bio_first_page_all(struct bio *bio)
{
	return bio_first_bvec_all(bio)->bv_page;
}

static inline struct bio_vec *bio_last_bvec_all(struct bio *bio)
{
	WARN_ON_ONCE(bio_flagged(bio, BIO_CLONED));
	return &bio->bi_io_vec[bio->bi_vcnt - 1];
}

enum bip_flags {
	BIP_BLOCK_INTEGRITY	= 1 << 0, /* block layer owns integrity data */
	BIP_MAPPED_INTEGRITY	= 1 << 1, /* ref tag has been remapped */
	BIP_CTRL_NOCHECK	= 1 << 2, /* disable HBA integrity checking */
	BIP_DISK_NOCHECK	= 1 << 3, /* disable disk integrity checking */
	BIP_IP_CHECKSUM		= 1 << 4, /* IP checksum */
};

/*
 * bio integrity payload
 */
struct bio_integrity_payload {
	struct bio		*bip_bio;	/* parent bio */

	struct bvec_iter	bip_iter;

	unsigned short		bip_slab;	/* slab the bip came from */
	unsigned short		bip_vcnt;	/* # of integrity bio_vecs */
	unsigned short		bip_max_vcnt;	/* integrity bio_vec slots */
	unsigned short		bip_flags;	/* control flags */

	struct work_struct	bip_work;	/* I/O completion */

	struct bio_vec		*bip_vec;
	struct bio_vec		bip_inline_vecs[0];/* embedded bvec array */
};

#if defined(CONFIG_BLK_DEV_INTEGRITY)

static inline struct bio_integrity_payload *bio_integrity(struct bio *bio)
{
	if (bio->bi_opf & REQ_INTEGRITY)
		return bio->bi_integrity;

	return NULL;
}

static inline bool bio_integrity_flagged(struct bio *bio, enum bip_flags flag)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);

	if (bip)
		return bip->bip_flags & flag;

	return false;
}

static inline sector_t bip_get_seed(struct bio_integrity_payload *bip)
{
	return bip->bip_iter.bi_sector;
}

static inline void bip_set_seed(struct bio_integrity_payload *bip,
				sector_t seed)
{
	bip->bip_iter.bi_sector = seed;
}

#endif /* CONFIG_BLK_DEV_INTEGRITY */

extern void bio_trim(struct bio *bio, int offset, int size);
extern struct bio *bio_split(struct bio *bio, int sectors,
			     gfp_t gfp, struct bio_set *bs);

/**
 * bio_next_split - get next @sectors from a bio, splitting if necessary
 * @bio:	bio to split
 * @sectors:	number of sectors to split from the front of @bio
 * @gfp:	gfp mask
 * @bs:		bio set to allocate from
 *
 * Returns a bio representing the next @sectors of @bio - if the bio is smaller
 * than @sectors, returns the original bio unchanged.
 */
static inline struct bio *bio_next_split(struct bio *bio, int sectors,
					 gfp_t gfp, struct bio_set *bs)
{
	if (sectors >= bio_sectors(bio))
		return bio;

	return bio_split(bio, sectors, gfp, bs);
}

extern struct bio_set *bioset_create(unsigned int, unsigned int, int flags);
enum {
	BIOSET_NEED_BVECS = BIT(0),
	BIOSET_NEED_RESCUER = BIT(1),
};
extern void bioset_free(struct bio_set *);
extern mempool_t *biovec_create_pool(int pool_entries);

extern struct bio *bio_alloc_bioset(gfp_t, unsigned int, struct bio_set *);
extern void bio_put(struct bio *);

extern void __bio_clone_fast(struct bio *, struct bio *);
extern struct bio *bio_clone_fast(struct bio *, gfp_t, struct bio_set *);
extern struct bio *bio_clone_bioset(struct bio *, gfp_t, struct bio_set *bs);

extern struct bio_set *fs_bio_set;

static inline struct bio *bio_alloc(gfp_t gfp_mask, unsigned int nr_iovecs)
{
	return bio_alloc_bioset(gfp_mask, nr_iovecs, fs_bio_set);
}

static inline struct bio *bio_kmalloc(gfp_t gfp_mask, unsigned int nr_iovecs)
{
	return bio_alloc_bioset(gfp_mask, nr_iovecs, NULL);
}

static inline struct bio *bio_clone_kmalloc(struct bio *bio, gfp_t gfp_mask)
{
	return bio_clone_bioset(bio, gfp_mask, NULL);

}

extern blk_qc_t submit_bio(struct bio *);

extern void bio_endio(struct bio *);

static inline void bio_io_error(struct bio *bio)
{
	bio->bi_status = BLK_STS_IOERR;
	bio_endio(bio);
}

static inline void bio_wouldblock_error(struct bio *bio)
{
	bio->bi_status = BLK_STS_AGAIN;
	bio_endio(bio);
}

struct request_queue;
extern int bio_phys_segments(struct request_queue *, struct bio *);

extern int submit_bio_wait(struct bio *bio);
extern void bio_advance(struct bio *, unsigned);

extern void bio_init(struct bio *bio, struct bio_vec *table,
		     unsigned short max_vecs);
extern void bio_uninit(struct bio *);
extern void bio_reset(struct bio *);
void bio_chain(struct bio *, struct bio *);

extern int bio_add_page(struct bio *, struct page *, unsigned int,unsigned int);
extern int bio_add_pc_page(struct request_queue *, struct bio *, struct page *,
			   unsigned int, unsigned int);
int bio_iov_iter_get_pages(struct bio *bio, struct iov_iter *iter);
struct rq_map_data;
extern struct bio *bio_map_user_iov(struct request_queue *,
				    struct iov_iter *, gfp_t);
extern void bio_unmap_user(struct bio *);
extern struct bio *bio_map_kern(struct request_queue *, void *, unsigned int,
				gfp_t);
extern struct bio *bio_copy_kern(struct request_queue *, void *, unsigned int,
				 gfp_t, int);
extern void bio_set_pages_dirty(struct bio *bio);
extern void bio_check_pages_dirty(struct bio *bio);

void generic_start_io_acct(struct request_queue *q, int rw,
				unsigned long sectors, struct hd_struct *part);
void generic_end_io_acct(struct request_queue *q, int rw,
				struct hd_struct *part,
				unsigned long start_time);

#ifndef ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
# error	"You should define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE for your platform"
#endif
#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
extern void bio_flush_dcache_pages(struct bio *bi);
#else
static inline void bio_flush_dcache_pages(struct bio *bi)
{
}
#endif

extern void bio_copy_data(struct bio *dst, struct bio *src);
extern int bio_alloc_pages(struct bio *bio, gfp_t gfp);
extern void bio_free_pages(struct bio *bio);

extern struct bio *bio_copy_user_iov(struct request_queue *,
				     struct rq_map_data *,
				     struct iov_iter *,
				     gfp_t);
extern int bio_uncopy_user(struct bio *);
void zero_fill_bio(struct bio *bio);
extern struct bio_vec *bvec_alloc(gfp_t, int, unsigned long *, mempool_t *);
extern void bvec_free(mempool_t *, struct bio_vec *, unsigned int);
extern unsigned int bvec_nr_vecs(unsigned short idx);

#define bio_set_dev(bio, bdev) 			\
do {						\
	(bio)->bi_disk = (bdev)->bd_disk;	\
	(bio)->bi_partno = (bdev)->bd_partno;	\
} while (0)

#define bio_copy_dev(dst, src)			\
do {						\
	(dst)->bi_disk = (src)->bi_disk;	\
	(dst)->bi_partno = (src)->bi_partno;	\
} while (0)

#define bio_dev(bio) \
	disk_devt((bio)->bi_disk)

#define bio_devname(bio, buf) \
	__bdevname(bio_dev(bio), (buf))

#ifdef CONFIG_BLK_CGROUP
int bio_associate_blkcg(struct bio *bio, struct cgroup_subsys_state *blkcg_css);
void bio_disassociate_task(struct bio *bio);
void bio_clone_blkcg_association(struct bio *dst, struct bio *src);
#else	/* CONFIG_BLK_CGROUP */
static inline int bio_associate_blkcg(struct bio *bio,
			struct cgroup_subsys_state *blkcg_css) { return 0; }
static inline void bio_disassociate_task(struct bio *bio) { }
static inline void bio_clone_blkcg_association(struct bio *dst,
			struct bio *src) { }
#endif	/* CONFIG_BLK_CGROUP */

#ifdef CONFIG_HIGHMEM
/*
 * remember never ever reenable interrupts between a bvec_kmap_irq and
 * bvec_kunmap_irq!
 */
static inline char *bvec_kmap_irq(struct bio_vec *bvec, unsigned long *flags)
{
	unsigned long addr;

	/*
	 * might not be a highmem page, but the preempt/irq count
	 * balancing is a lot nicer this way
	 */
	local_irq_save(*flags);
	addr = (unsigned long) kmap_atomic(bvec->bv_page);

	BUG_ON(addr & ~PAGE_MASK);

	return (char *) addr + bvec->bv_offset;
}

static inline void bvec_kunmap_irq(char *buffer, unsigned long *flags)
{
	unsigned long ptr = (unsigned long) buffer & PAGE_MASK;

	kunmap_atomic((void *) ptr);
	local_irq_restore(*flags);
}

#else
static inline char *bvec_kmap_irq(struct bio_vec *bvec, unsigned long *flags)
{
	return page_address(bvec->bv_page) + bvec->bv_offset;
}

static inline void bvec_kunmap_irq(char *buffer, unsigned long *flags)
{
	*flags = 0;
}
#endif

/*
 * BIO list management for use by remapping drivers (e.g. DM or MD) and loop.
 *
 * A bio_list anchors a singly-linked list of bios chained through the bi_next
 * member of the bio.  The bio_list also caches the last list member to allow
 * fast access to the tail.
 */
struct bio_list {
	struct bio *head;
	struct bio *tail;
};

static inline int bio_list_empty(const struct bio_list *bl)
{
	return bl->head == NULL;
}

static inline void bio_list_init(struct bio_list *bl)
{
	bl->head = bl->tail = NULL;
}

#define BIO_EMPTY_LIST	{ NULL, NULL }

#define bio_list_for_each(bio, bl) \
	for (bio = (bl)->head; bio; bio = bio->bi_next)

static inline unsigned bio_list_size(const struct bio_list *bl)
{
	unsigned sz = 0;
	struct bio *bio;

	bio_list_for_each(bio, bl)
		sz++;

	return sz;
}

static inline void bio_list_add(struct bio_list *bl, struct bio *bio)
{
	bio->bi_next = NULL;

	if (bl->tail)
		bl->tail->bi_next = bio;
	else
		bl->head = bio;

	bl->tail = bio;
}

static inline void bio_list_add_head(struct bio_list *bl, struct bio *bio)
{
	bio->bi_next = bl->head;

	bl->head = bio;

	if (!bl->tail)
		bl->tail = bio;
}

static inline void bio_list_merge(struct bio_list *bl, struct bio_list *bl2)
{
	if (!bl2->head)
		return;

	if (bl->tail)
		bl->tail->bi_next = bl2->head;
	else
		bl->head = bl2->head;

	bl->tail = bl2->tail;
}

static inline void bio_list_merge_head(struct bio_list *bl,
				       struct bio_list *bl2)
{
	if (!bl2->head)
		return;

	if (bl->head)
		bl2->tail->bi_next = bl->head;
	else
		bl->tail = bl2->tail;

	bl->head = bl2->head;
}

static inline struct bio *bio_list_peek(struct bio_list *bl)
{
	return bl->head;
}

static inline struct bio *bio_list_pop(struct bio_list *bl)
{
	struct bio *bio = bl->head;

	if (bio) {
		bl->head = bl->head->bi_next;
		if (!bl->head)
			bl->tail = NULL;

		bio->bi_next = NULL;
	}

	return bio;
}

static inline struct bio *bio_list_get(struct bio_list *bl)
{
	struct bio *bio = bl->head;

	bl->head = bl->tail = NULL;

	return bio;
}

/*
 * Increment chain count for the bio. Make sure the CHAIN flag update
 * is visible before the raised count.
 */
static inline void bio_inc_remaining(struct bio *bio)
{
	bio_set_flag(bio, BIO_CHAIN);
	smp_mb__before_atomic();
	atomic_inc(&bio->__bi_remaining);
}

/*
 * bio_set is used to allow other portions of the IO system to
 * allocate their own private memory pools for bio and iovec structures.
 * These memory pools in turn all allocate from the bio_slab
 * and the bvec_slabs[].
 */
#define BIO_POOL_SIZE 2

struct bio_set {
	struct kmem_cache *bio_slab;
	unsigned int front_pad;

	mempool_t *bio_pool;
	mempool_t *bvec_pool;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	mempool_t *bio_integrity_pool;
	mempool_t *bvec_integrity_pool;
#endif

	/*
	 * Deadlock avoidance for stacking block drivers: see comments in
	 * bio_alloc_bioset() for details
	 */
	spinlock_t		rescue_lock;
	struct bio_list		rescue_list;
	struct work_struct	rescue_work;
	struct workqueue_struct	*rescue_workqueue;
};

struct biovec_slab {
	int nr_vecs;
	char *name;
	struct kmem_cache *slab;
};

/*
 * a small number of entries is fine, not going to be performance critical.
 * basically we just need to survive
 */
#define BIO_SPLIT_ENTRIES 2

#if defined(CONFIG_BLK_DEV_INTEGRITY)

#define bip_for_each_vec(bvl, bip, iter)				\
	for_each_bvec(bvl, (bip)->bip_vec, iter, (bip)->bip_iter)

#define bio_for_each_integrity_vec(_bvl, _bio, _iter)			\
	for_each_bio(_bio)						\
		bip_for_each_vec(_bvl, _bio->bi_integrity, _iter)

extern struct bio_integrity_payload *bio_integrity_alloc(struct bio *, gfp_t, unsigned int);
extern int bio_integrity_add_page(struct bio *, struct page *, unsigned int, unsigned int);
extern bool bio_integrity_prep(struct bio *);
extern void bio_integrity_advance(struct bio *, unsigned int);
extern void bio_integrity_trim(struct bio *);
extern int bio_integrity_clone(struct bio *, struct bio *, gfp_t);
extern int bioset_integrity_create(struct bio_set *, int);
extern void bioset_integrity_free(struct bio_set *);
extern void bio_integrity_init(void);

#else /* CONFIG_BLK_DEV_INTEGRITY */

static inline void *bio_integrity(struct bio *bio)
{
	return NULL;
}

static inline int bioset_integrity_create(struct bio_set *bs, int pool_size)
{
	return 0;
}

static inline void bioset_integrity_free (struct bio_set *bs)
{
	return;
}

static inline bool bio_integrity_prep(struct bio *bio)
{
	return true;
}

static inline int bio_integrity_clone(struct bio *bio, struct bio *bio_src,
				      gfp_t gfp_mask)
{
	return 0;
}

static inline void bio_integrity_advance(struct bio *bio,
					 unsigned int bytes_done)
{
	return;
}

static inline void bio_integrity_trim(struct bio *bio)
{
	return;
}

static inline void bio_integrity_init(void)
{
	return;
}

static inline bool bio_integrity_flagged(struct bio *bio, enum bip_flags flag)
{
	return false;
}

static inline void *bio_integrity_alloc(struct bio * bio, gfp_t gfp,
								unsigned int nr)
{
	return ERR_PTR(-EINVAL);
}

static inline int bio_integrity_add_page(struct bio *bio, struct page *page,
					unsigned int len, unsigned int offset)
{
	return 0;
}

#endif /* CONFIG_BLK_DEV_INTEGRITY */

#endif /* CONFIG_BLOCK */
#endif /* __LINUX_BIO_H */

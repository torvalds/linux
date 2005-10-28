/*
 * High memory handling common code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * 64-bit physical space. With current x86 CPUs this
 * means up to 64 Gigabytes physical RAM.
 *
 * Rewrote high memory support to move the page cache into
 * high memory. Implemented permanent (schedulable) kmaps
 * based on Linus' idea.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <asm/tlbflush.h>

static mempool_t *page_pool, *isa_page_pool;

static void *page_pool_alloc_isa(gfp_t gfp_mask, void *data)
{
	return alloc_page(gfp_mask | GFP_DMA);
}

static void page_pool_free(void *page, void *data)
{
	__free_page(page);
}

/*
 * Virtual_count is not a pure "count".
 *  0 means that it is not mapped, and has not been mapped
 *    since a TLB flush - it is usable.
 *  1 means that there are no users, but it has been mapped
 *    since the last TLB flush - so we can't use it.
 *  n means that there are (n-1) current users of it.
 */
#ifdef CONFIG_HIGHMEM

static void *page_pool_alloc(gfp_t gfp_mask, void *data)
{
	return alloc_page(gfp_mask);
}

static int pkmap_count[LAST_PKMAP];
static unsigned int last_pkmap_nr;
static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(kmap_lock);

pte_t * pkmap_page_table;

static DECLARE_WAIT_QUEUE_HEAD(pkmap_map_wait);

static void flush_all_zero_pkmaps(void)
{
	int i;

	flush_cache_kmaps();

	for (i = 0; i < LAST_PKMAP; i++) {
		struct page *page;

		/*
		 * zero means we don't have anything to do,
		 * >1 means that it is still in use. Only
		 * a count of 1 means that it is free but
		 * needs to be unmapped
		 */
		if (pkmap_count[i] != 1)
			continue;
		pkmap_count[i] = 0;

		/* sanity check */
		if (pte_none(pkmap_page_table[i]))
			BUG();

		/*
		 * Don't need an atomic fetch-and-clear op here;
		 * no-one has the page mapped, and cannot get at
		 * its virtual address (and hence PTE) without first
		 * getting the kmap_lock (which is held here).
		 * So no dangers, even with speculative execution.
		 */
		page = pte_page(pkmap_page_table[i]);
		pte_clear(&init_mm, (unsigned long)page_address(page),
			  &pkmap_page_table[i]);

		set_page_address(page, NULL);
	}
	flush_tlb_kernel_range(PKMAP_ADDR(0), PKMAP_ADDR(LAST_PKMAP));
}

static inline unsigned long map_new_virtual(struct page *page)
{
	unsigned long vaddr;
	int count;

start:
	count = LAST_PKMAP;
	/* Find an empty entry */
	for (;;) {
		last_pkmap_nr = (last_pkmap_nr + 1) & LAST_PKMAP_MASK;
		if (!last_pkmap_nr) {
			flush_all_zero_pkmaps();
			count = LAST_PKMAP;
		}
		if (!pkmap_count[last_pkmap_nr])
			break;	/* Found a usable entry */
		if (--count)
			continue;

		/*
		 * Sleep for somebody else to unmap their entries
		 */
		{
			DECLARE_WAITQUEUE(wait, current);

			__set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&pkmap_map_wait, &wait);
			spin_unlock(&kmap_lock);
			schedule();
			remove_wait_queue(&pkmap_map_wait, &wait);
			spin_lock(&kmap_lock);

			/* Somebody else might have mapped it while we slept */
			if (page_address(page))
				return (unsigned long)page_address(page);

			/* Re-start */
			goto start;
		}
	}
	vaddr = PKMAP_ADDR(last_pkmap_nr);
	set_pte_at(&init_mm, vaddr,
		   &(pkmap_page_table[last_pkmap_nr]), mk_pte(page, kmap_prot));

	pkmap_count[last_pkmap_nr] = 1;
	set_page_address(page, (void *)vaddr);

	return vaddr;
}

void fastcall *kmap_high(struct page *page)
{
	unsigned long vaddr;

	/*
	 * For highmem pages, we can't trust "virtual" until
	 * after we have the lock.
	 *
	 * We cannot call this from interrupts, as it may block
	 */
	spin_lock(&kmap_lock);
	vaddr = (unsigned long)page_address(page);
	if (!vaddr)
		vaddr = map_new_virtual(page);
	pkmap_count[PKMAP_NR(vaddr)]++;
	if (pkmap_count[PKMAP_NR(vaddr)] < 2)
		BUG();
	spin_unlock(&kmap_lock);
	return (void*) vaddr;
}

EXPORT_SYMBOL(kmap_high);

void fastcall kunmap_high(struct page *page)
{
	unsigned long vaddr;
	unsigned long nr;
	int need_wakeup;

	spin_lock(&kmap_lock);
	vaddr = (unsigned long)page_address(page);
	if (!vaddr)
		BUG();
	nr = PKMAP_NR(vaddr);

	/*
	 * A count must never go down to zero
	 * without a TLB flush!
	 */
	need_wakeup = 0;
	switch (--pkmap_count[nr]) {
	case 0:
		BUG();
	case 1:
		/*
		 * Avoid an unnecessary wake_up() function call.
		 * The common case is pkmap_count[] == 1, but
		 * no waiters.
		 * The tasks queued in the wait-queue are guarded
		 * by both the lock in the wait-queue-head and by
		 * the kmap_lock.  As the kmap_lock is held here,
		 * no need for the wait-queue-head's lock.  Simply
		 * test if the queue is empty.
		 */
		need_wakeup = waitqueue_active(&pkmap_map_wait);
	}
	spin_unlock(&kmap_lock);

	/* do wake-up, if needed, race-free outside of the spin lock */
	if (need_wakeup)
		wake_up(&pkmap_map_wait);
}

EXPORT_SYMBOL(kunmap_high);

#define POOL_SIZE	64

static __init int init_emergency_pool(void)
{
	struct sysinfo i;
	si_meminfo(&i);
	si_swapinfo(&i);
        
	if (!i.totalhigh)
		return 0;

	page_pool = mempool_create(POOL_SIZE, page_pool_alloc, page_pool_free, NULL);
	if (!page_pool)
		BUG();
	printk("highmem bounce pool size: %d pages\n", POOL_SIZE);

	return 0;
}

__initcall(init_emergency_pool);

/*
 * highmem version, map in to vec
 */
static void bounce_copy_vec(struct bio_vec *to, unsigned char *vfrom)
{
	unsigned long flags;
	unsigned char *vto;

	local_irq_save(flags);
	vto = kmap_atomic(to->bv_page, KM_BOUNCE_READ);
	memcpy(vto + to->bv_offset, vfrom, to->bv_len);
	kunmap_atomic(vto, KM_BOUNCE_READ);
	local_irq_restore(flags);
}

#else /* CONFIG_HIGHMEM */

#define bounce_copy_vec(to, vfrom)	\
	memcpy(page_address((to)->bv_page) + (to)->bv_offset, vfrom, (to)->bv_len)

#endif

#define ISA_POOL_SIZE	16

/*
 * gets called "every" time someone init's a queue with BLK_BOUNCE_ISA
 * as the max address, so check if the pool has already been created.
 */
int init_emergency_isa_pool(void)
{
	if (isa_page_pool)
		return 0;

	isa_page_pool = mempool_create(ISA_POOL_SIZE, page_pool_alloc_isa, page_pool_free, NULL);
	if (!isa_page_pool)
		BUG();

	printk("isa bounce pool size: %d pages\n", ISA_POOL_SIZE);
	return 0;
}

/*
 * Simple bounce buffer support for highmem pages. Depending on the
 * queue gfp mask set, *to may or may not be a highmem page. kmap it
 * always, it will do the Right Thing
 */
static void copy_to_high_bio_irq(struct bio *to, struct bio *from)
{
	unsigned char *vfrom;
	struct bio_vec *tovec, *fromvec;
	int i;

	__bio_for_each_segment(tovec, to, i, 0) {
		fromvec = from->bi_io_vec + i;

		/*
		 * not bounced
		 */
		if (tovec->bv_page == fromvec->bv_page)
			continue;

		/*
		 * fromvec->bv_offset and fromvec->bv_len might have been
		 * modified by the block layer, so use the original copy,
		 * bounce_copy_vec already uses tovec->bv_len
		 */
		vfrom = page_address(fromvec->bv_page) + tovec->bv_offset;

		flush_dcache_page(tovec->bv_page);
		bounce_copy_vec(tovec, vfrom);
	}
}

static void bounce_end_io(struct bio *bio, mempool_t *pool, int err)
{
	struct bio *bio_orig = bio->bi_private;
	struct bio_vec *bvec, *org_vec;
	int i;

	if (test_bit(BIO_EOPNOTSUPP, &bio->bi_flags))
		set_bit(BIO_EOPNOTSUPP, &bio_orig->bi_flags);

	/*
	 * free up bounce indirect pages used
	 */
	__bio_for_each_segment(bvec, bio, i, 0) {
		org_vec = bio_orig->bi_io_vec + i;
		if (bvec->bv_page == org_vec->bv_page)
			continue;

		mempool_free(bvec->bv_page, pool);	
		dec_page_state(nr_bounce);
	}

	bio_endio(bio_orig, bio_orig->bi_size, err);
	bio_put(bio);
}

static int bounce_end_io_write(struct bio *bio, unsigned int bytes_done,int err)
{
	if (bio->bi_size)
		return 1;

	bounce_end_io(bio, page_pool, err);
	return 0;
}

static int bounce_end_io_write_isa(struct bio *bio, unsigned int bytes_done, int err)
{
	if (bio->bi_size)
		return 1;

	bounce_end_io(bio, isa_page_pool, err);
	return 0;
}

static void __bounce_end_io_read(struct bio *bio, mempool_t *pool, int err)
{
	struct bio *bio_orig = bio->bi_private;

	if (test_bit(BIO_UPTODATE, &bio->bi_flags))
		copy_to_high_bio_irq(bio_orig, bio);

	bounce_end_io(bio, pool, err);
}

static int bounce_end_io_read(struct bio *bio, unsigned int bytes_done, int err)
{
	if (bio->bi_size)
		return 1;

	__bounce_end_io_read(bio, page_pool, err);
	return 0;
}

static int bounce_end_io_read_isa(struct bio *bio, unsigned int bytes_done, int err)
{
	if (bio->bi_size)
		return 1;

	__bounce_end_io_read(bio, isa_page_pool, err);
	return 0;
}

static void __blk_queue_bounce(request_queue_t *q, struct bio **bio_orig,
			mempool_t *pool)
{
	struct page *page;
	struct bio *bio = NULL;
	int i, rw = bio_data_dir(*bio_orig);
	struct bio_vec *to, *from;

	bio_for_each_segment(from, *bio_orig, i) {
		page = from->bv_page;

		/*
		 * is destination page below bounce pfn?
		 */
		if (page_to_pfn(page) < q->bounce_pfn)
			continue;

		/*
		 * irk, bounce it
		 */
		if (!bio)
			bio = bio_alloc(GFP_NOIO, (*bio_orig)->bi_vcnt);

		to = bio->bi_io_vec + i;

		to->bv_page = mempool_alloc(pool, q->bounce_gfp);
		to->bv_len = from->bv_len;
		to->bv_offset = from->bv_offset;
		inc_page_state(nr_bounce);

		if (rw == WRITE) {
			char *vto, *vfrom;

			flush_dcache_page(from->bv_page);
			vto = page_address(to->bv_page) + to->bv_offset;
			vfrom = kmap(from->bv_page) + from->bv_offset;
			memcpy(vto, vfrom, to->bv_len);
			kunmap(from->bv_page);
		}
	}

	/*
	 * no pages bounced
	 */
	if (!bio)
		return;

	/*
	 * at least one page was bounced, fill in possible non-highmem
	 * pages
	 */
	__bio_for_each_segment(from, *bio_orig, i, 0) {
		to = bio_iovec_idx(bio, i);
		if (!to->bv_page) {
			to->bv_page = from->bv_page;
			to->bv_len = from->bv_len;
			to->bv_offset = from->bv_offset;
		}
	}

	bio->bi_bdev = (*bio_orig)->bi_bdev;
	bio->bi_flags |= (1 << BIO_BOUNCED);
	bio->bi_sector = (*bio_orig)->bi_sector;
	bio->bi_rw = (*bio_orig)->bi_rw;

	bio->bi_vcnt = (*bio_orig)->bi_vcnt;
	bio->bi_idx = (*bio_orig)->bi_idx;
	bio->bi_size = (*bio_orig)->bi_size;

	if (pool == page_pool) {
		bio->bi_end_io = bounce_end_io_write;
		if (rw == READ)
			bio->bi_end_io = bounce_end_io_read;
	} else {
		bio->bi_end_io = bounce_end_io_write_isa;
		if (rw == READ)
			bio->bi_end_io = bounce_end_io_read_isa;
	}

	bio->bi_private = *bio_orig;
	*bio_orig = bio;
}

void blk_queue_bounce(request_queue_t *q, struct bio **bio_orig)
{
	mempool_t *pool;

	/*
	 * for non-isa bounce case, just check if the bounce pfn is equal
	 * to or bigger than the highest pfn in the system -- in that case,
	 * don't waste time iterating over bio segments
	 */
	if (!(q->bounce_gfp & GFP_DMA)) {
		if (q->bounce_pfn >= blk_max_pfn)
			return;
		pool = page_pool;
	} else {
		BUG_ON(!isa_page_pool);
		pool = isa_page_pool;
	}

	/*
	 * slow path
	 */
	__blk_queue_bounce(q, bio_orig, pool);
}

EXPORT_SYMBOL(blk_queue_bounce);

#if defined(HASHED_PAGE_VIRTUAL)

#define PA_HASH_ORDER	7

/*
 * Describes one page->virtual association
 */
struct page_address_map {
	struct page *page;
	void *virtual;
	struct list_head list;
};

/*
 * page_address_map freelist, allocated from page_address_maps.
 */
static struct list_head page_address_pool;	/* freelist */
static spinlock_t pool_lock;			/* protects page_address_pool */

/*
 * Hash table bucket
 */
static struct page_address_slot {
	struct list_head lh;			/* List of page_address_maps */
	spinlock_t lock;			/* Protect this bucket's list */
} ____cacheline_aligned_in_smp page_address_htable[1<<PA_HASH_ORDER];

static struct page_address_slot *page_slot(struct page *page)
{
	return &page_address_htable[hash_ptr(page, PA_HASH_ORDER)];
}

void *page_address(struct page *page)
{
	unsigned long flags;
	void *ret;
	struct page_address_slot *pas;

	if (!PageHighMem(page))
		return lowmem_page_address(page);

	pas = page_slot(page);
	ret = NULL;
	spin_lock_irqsave(&pas->lock, flags);
	if (!list_empty(&pas->lh)) {
		struct page_address_map *pam;

		list_for_each_entry(pam, &pas->lh, list) {
			if (pam->page == page) {
				ret = pam->virtual;
				goto done;
			}
		}
	}
done:
	spin_unlock_irqrestore(&pas->lock, flags);
	return ret;
}

EXPORT_SYMBOL(page_address);

void set_page_address(struct page *page, void *virtual)
{
	unsigned long flags;
	struct page_address_slot *pas;
	struct page_address_map *pam;

	BUG_ON(!PageHighMem(page));

	pas = page_slot(page);
	if (virtual) {		/* Add */
		BUG_ON(list_empty(&page_address_pool));

		spin_lock_irqsave(&pool_lock, flags);
		pam = list_entry(page_address_pool.next,
				struct page_address_map, list);
		list_del(&pam->list);
		spin_unlock_irqrestore(&pool_lock, flags);

		pam->page = page;
		pam->virtual = virtual;

		spin_lock_irqsave(&pas->lock, flags);
		list_add_tail(&pam->list, &pas->lh);
		spin_unlock_irqrestore(&pas->lock, flags);
	} else {		/* Remove */
		spin_lock_irqsave(&pas->lock, flags);
		list_for_each_entry(pam, &pas->lh, list) {
			if (pam->page == page) {
				list_del(&pam->list);
				spin_unlock_irqrestore(&pas->lock, flags);
				spin_lock_irqsave(&pool_lock, flags);
				list_add_tail(&pam->list, &page_address_pool);
				spin_unlock_irqrestore(&pool_lock, flags);
				goto done;
			}
		}
		spin_unlock_irqrestore(&pas->lock, flags);
	}
done:
	return;
}

static struct page_address_map page_address_maps[LAST_PKMAP];

void __init page_address_init(void)
{
	int i;

	INIT_LIST_HEAD(&page_address_pool);
	for (i = 0; i < ARRAY_SIZE(page_address_maps); i++)
		list_add(&page_address_maps[i].list, &page_address_pool);
	for (i = 0; i < ARRAY_SIZE(page_address_htable); i++) {
		INIT_LIST_HEAD(&page_address_htable[i].lh);
		spin_lock_init(&page_address_htable[i].lock);
	}
	spin_lock_init(&pool_lock);
}

#endif	/* defined(CONFIG_HIGHMEM) && !defined(WANT_PAGE_VIRTUAL) */

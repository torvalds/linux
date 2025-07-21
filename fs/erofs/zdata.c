// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2022 Alibaba Cloud
 */
#include "compress.h"
#include <linux/psi.h>
#include <linux/cpuhotplug.h>
#include <trace/events/erofs.h>

#define Z_EROFS_PCLUSTER_MAX_PAGES	(Z_EROFS_PCLUSTER_MAX_SIZE / PAGE_SIZE)
#define Z_EROFS_INLINE_BVECS		2

struct z_erofs_bvec {
	struct page *page;
	int offset;
	unsigned int end;
};

#define __Z_EROFS_BVSET(name, total) \
struct name { \
	/* point to the next page which contains the following bvecs */ \
	struct page *nextpage; \
	struct z_erofs_bvec bvec[total]; \
}
__Z_EROFS_BVSET(z_erofs_bvset,);
__Z_EROFS_BVSET(z_erofs_bvset_inline, Z_EROFS_INLINE_BVECS);

/*
 * Structure fields follow one of the following exclusion rules.
 *
 * I: Modifiable by initialization/destruction paths and read-only
 *    for everyone else;
 *
 * L: Field should be protected by the pcluster lock;
 *
 * A: Field should be accessed / updated in atomic for parallelized code.
 */
struct z_erofs_pcluster {
	struct mutex lock;
	struct lockref lockref;

	/* A: point to next chained pcluster or TAILs */
	struct z_erofs_pcluster *next;

	/* I: start physical position of this pcluster */
	erofs_off_t pos;

	/* L: the maximum decompression size of this round */
	unsigned int length;

	/* L: total number of bvecs */
	unsigned int vcnt;

	/* I: pcluster size (compressed size) in bytes */
	unsigned int pclustersize;

	/* I: page offset of start position of decompression */
	unsigned short pageofs_out;

	/* I: page offset of inline compressed data */
	unsigned short pageofs_in;

	union {
		/* L: inline a certain number of bvec for bootstrap */
		struct z_erofs_bvset_inline bvset;

		/* I: can be used to free the pcluster by RCU. */
		struct rcu_head rcu;
	};

	/* I: compression algorithm format */
	unsigned char algorithmformat;

	/* I: whether compressed data is in-lined or not */
	bool from_meta;

	/* L: whether partial decompression or not */
	bool partial;

	/* L: whether extra buffer allocations are best-effort */
	bool besteffort;

	/* A: compressed bvecs (can be cached or inplaced pages) */
	struct z_erofs_bvec compressed_bvecs[];
};

/* the end of a chain of pclusters */
#define Z_EROFS_PCLUSTER_TAIL           ((void *) 0x700 + POISON_POINTER_DELTA)

struct z_erofs_decompressqueue {
	struct super_block *sb;
	struct z_erofs_pcluster *head;
	atomic_t pending_bios;

	union {
		struct completion done;
		struct work_struct work;
		struct kthread_work kthread_work;
	} u;
	bool eio, sync;
};

static inline unsigned int z_erofs_pclusterpages(struct z_erofs_pcluster *pcl)
{
	return PAGE_ALIGN(pcl->pageofs_in + pcl->pclustersize) >> PAGE_SHIFT;
}

static bool erofs_folio_is_managed(struct erofs_sb_info *sbi, struct folio *fo)
{
	return fo->mapping == MNGD_MAPPING(sbi);
}

#define Z_EROFS_ONSTACK_PAGES		32

/*
 * since pclustersize is variable for big pcluster feature, introduce slab
 * pools implementation for different pcluster sizes.
 */
struct z_erofs_pcluster_slab {
	struct kmem_cache *slab;
	unsigned int maxpages;
	char name[48];
};

#define _PCLP(n) { .maxpages = n }

static struct z_erofs_pcluster_slab pcluster_pool[] __read_mostly = {
	_PCLP(1), _PCLP(4), _PCLP(16), _PCLP(64), _PCLP(128),
	_PCLP(Z_EROFS_PCLUSTER_MAX_PAGES + 1)
};

struct z_erofs_bvec_iter {
	struct page *bvpage;
	struct z_erofs_bvset *bvset;
	unsigned int nr, cur;
};

static struct page *z_erofs_bvec_iter_end(struct z_erofs_bvec_iter *iter)
{
	if (iter->bvpage)
		kunmap_local(iter->bvset);
	return iter->bvpage;
}

static struct page *z_erofs_bvset_flip(struct z_erofs_bvec_iter *iter)
{
	unsigned long base = (unsigned long)((struct z_erofs_bvset *)0)->bvec;
	/* have to access nextpage in advance, otherwise it will be unmapped */
	struct page *nextpage = iter->bvset->nextpage;
	struct page *oldpage;

	DBG_BUGON(!nextpage);
	oldpage = z_erofs_bvec_iter_end(iter);
	iter->bvpage = nextpage;
	iter->bvset = kmap_local_page(nextpage);
	iter->nr = (PAGE_SIZE - base) / sizeof(struct z_erofs_bvec);
	iter->cur = 0;
	return oldpage;
}

static void z_erofs_bvec_iter_begin(struct z_erofs_bvec_iter *iter,
				    struct z_erofs_bvset_inline *bvset,
				    unsigned int bootstrap_nr,
				    unsigned int cur)
{
	*iter = (struct z_erofs_bvec_iter) {
		.nr = bootstrap_nr,
		.bvset = (struct z_erofs_bvset *)bvset,
	};

	while (cur > iter->nr) {
		cur -= iter->nr;
		z_erofs_bvset_flip(iter);
	}
	iter->cur = cur;
}

static int z_erofs_bvec_enqueue(struct z_erofs_bvec_iter *iter,
				struct z_erofs_bvec *bvec,
				struct page **candidate_bvpage,
				struct page **pagepool)
{
	if (iter->cur >= iter->nr) {
		struct page *nextpage = *candidate_bvpage;

		if (!nextpage) {
			nextpage = __erofs_allocpage(pagepool, GFP_KERNEL,
					true);
			if (!nextpage)
				return -ENOMEM;
			set_page_private(nextpage, Z_EROFS_SHORTLIVED_PAGE);
		}
		DBG_BUGON(iter->bvset->nextpage);
		iter->bvset->nextpage = nextpage;
		z_erofs_bvset_flip(iter);

		iter->bvset->nextpage = NULL;
		*candidate_bvpage = NULL;
	}
	iter->bvset->bvec[iter->cur++] = *bvec;
	return 0;
}

static void z_erofs_bvec_dequeue(struct z_erofs_bvec_iter *iter,
				 struct z_erofs_bvec *bvec,
				 struct page **old_bvpage)
{
	if (iter->cur == iter->nr)
		*old_bvpage = z_erofs_bvset_flip(iter);
	else
		*old_bvpage = NULL;
	*bvec = iter->bvset->bvec[iter->cur++];
}

static void z_erofs_destroy_pcluster_pool(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pcluster_pool); ++i) {
		if (!pcluster_pool[i].slab)
			continue;
		kmem_cache_destroy(pcluster_pool[i].slab);
		pcluster_pool[i].slab = NULL;
	}
}

static int z_erofs_create_pcluster_pool(void)
{
	struct z_erofs_pcluster_slab *pcs;
	struct z_erofs_pcluster *a;
	unsigned int size;

	for (pcs = pcluster_pool;
	     pcs < pcluster_pool + ARRAY_SIZE(pcluster_pool); ++pcs) {
		size = struct_size(a, compressed_bvecs, pcs->maxpages);

		sprintf(pcs->name, "erofs_pcluster-%u", pcs->maxpages);
		pcs->slab = kmem_cache_create(pcs->name, size, 0,
					      SLAB_RECLAIM_ACCOUNT, NULL);
		if (pcs->slab)
			continue;

		z_erofs_destroy_pcluster_pool();
		return -ENOMEM;
	}
	return 0;
}

static struct z_erofs_pcluster *z_erofs_alloc_pcluster(unsigned int size)
{
	unsigned int nrpages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct z_erofs_pcluster_slab *pcs = pcluster_pool;

	for (; pcs < pcluster_pool + ARRAY_SIZE(pcluster_pool); ++pcs) {
		struct z_erofs_pcluster *pcl;

		if (nrpages > pcs->maxpages)
			continue;

		pcl = kmem_cache_zalloc(pcs->slab, GFP_KERNEL);
		if (!pcl)
			return ERR_PTR(-ENOMEM);
		return pcl;
	}
	return ERR_PTR(-EINVAL);
}

static void z_erofs_free_pcluster(struct z_erofs_pcluster *pcl)
{
	unsigned int pclusterpages = z_erofs_pclusterpages(pcl);
	int i;

	for (i = 0; i < ARRAY_SIZE(pcluster_pool); ++i) {
		struct z_erofs_pcluster_slab *pcs = pcluster_pool + i;

		if (pclusterpages > pcs->maxpages)
			continue;

		kmem_cache_free(pcs->slab, pcl);
		return;
	}
	DBG_BUGON(1);
}

static struct workqueue_struct *z_erofs_workqueue __read_mostly;

#ifdef CONFIG_EROFS_FS_PCPU_KTHREAD
static struct kthread_worker __rcu **z_erofs_pcpu_workers;
static atomic_t erofs_percpu_workers_initialized = ATOMIC_INIT(0);

static void erofs_destroy_percpu_workers(void)
{
	struct kthread_worker *worker;
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		worker = rcu_dereference_protected(
					z_erofs_pcpu_workers[cpu], 1);
		rcu_assign_pointer(z_erofs_pcpu_workers[cpu], NULL);
		if (worker)
			kthread_destroy_worker(worker);
	}
	kfree(z_erofs_pcpu_workers);
}

static struct kthread_worker *erofs_init_percpu_worker(int cpu)
{
	struct kthread_worker *worker =
		kthread_run_worker_on_cpu(cpu, 0, "erofs_worker/%u");

	if (IS_ERR(worker))
		return worker;
	if (IS_ENABLED(CONFIG_EROFS_FS_PCPU_KTHREAD_HIPRI))
		sched_set_fifo_low(worker->task);
	return worker;
}

static int erofs_init_percpu_workers(void)
{
	struct kthread_worker *worker;
	unsigned int cpu;

	z_erofs_pcpu_workers = kcalloc(num_possible_cpus(),
			sizeof(struct kthread_worker *), GFP_ATOMIC);
	if (!z_erofs_pcpu_workers)
		return -ENOMEM;

	for_each_online_cpu(cpu) {	/* could miss cpu{off,on}line? */
		worker = erofs_init_percpu_worker(cpu);
		if (!IS_ERR(worker))
			rcu_assign_pointer(z_erofs_pcpu_workers[cpu], worker);
	}
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static DEFINE_SPINLOCK(z_erofs_pcpu_worker_lock);
static enum cpuhp_state erofs_cpuhp_state;

static int erofs_cpu_online(unsigned int cpu)
{
	struct kthread_worker *worker, *old;

	worker = erofs_init_percpu_worker(cpu);
	if (IS_ERR(worker))
		return PTR_ERR(worker);

	spin_lock(&z_erofs_pcpu_worker_lock);
	old = rcu_dereference_protected(z_erofs_pcpu_workers[cpu],
			lockdep_is_held(&z_erofs_pcpu_worker_lock));
	if (!old)
		rcu_assign_pointer(z_erofs_pcpu_workers[cpu], worker);
	spin_unlock(&z_erofs_pcpu_worker_lock);
	if (old)
		kthread_destroy_worker(worker);
	return 0;
}

static int erofs_cpu_offline(unsigned int cpu)
{
	struct kthread_worker *worker;

	spin_lock(&z_erofs_pcpu_worker_lock);
	worker = rcu_dereference_protected(z_erofs_pcpu_workers[cpu],
			lockdep_is_held(&z_erofs_pcpu_worker_lock));
	rcu_assign_pointer(z_erofs_pcpu_workers[cpu], NULL);
	spin_unlock(&z_erofs_pcpu_worker_lock);

	synchronize_rcu();
	if (worker)
		kthread_destroy_worker(worker);
	return 0;
}

static int erofs_cpu_hotplug_init(void)
{
	int state;

	state = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
			"fs/erofs:online", erofs_cpu_online, erofs_cpu_offline);
	if (state < 0)
		return state;

	erofs_cpuhp_state = state;
	return 0;
}

static void erofs_cpu_hotplug_destroy(void)
{
	if (erofs_cpuhp_state)
		cpuhp_remove_state_nocalls(erofs_cpuhp_state);
}
#else /* !CONFIG_HOTPLUG_CPU  */
static inline int erofs_cpu_hotplug_init(void) { return 0; }
static inline void erofs_cpu_hotplug_destroy(void) {}
#endif/* CONFIG_HOTPLUG_CPU */
static int z_erofs_init_pcpu_workers(struct super_block *sb)
{
	int err;

	if (atomic_xchg(&erofs_percpu_workers_initialized, 1))
		return 0;

	err = erofs_init_percpu_workers();
	if (err) {
		erofs_err(sb, "per-cpu workers: failed to allocate.");
		goto err_init_percpu_workers;
	}

	err = erofs_cpu_hotplug_init();
	if (err < 0) {
		erofs_err(sb, "per-cpu workers: failed CPU hotplug init.");
		goto err_cpuhp_init;
	}
	erofs_info(sb, "initialized per-cpu workers successfully.");
	return err;

err_cpuhp_init:
	erofs_destroy_percpu_workers();
err_init_percpu_workers:
	atomic_set(&erofs_percpu_workers_initialized, 0);
	return err;
}

static void z_erofs_destroy_pcpu_workers(void)
{
	if (!atomic_xchg(&erofs_percpu_workers_initialized, 0))
		return;
	erofs_cpu_hotplug_destroy();
	erofs_destroy_percpu_workers();
}
#else /* !CONFIG_EROFS_FS_PCPU_KTHREAD */
static inline int z_erofs_init_pcpu_workers(struct super_block *sb) { return 0; }
static inline void z_erofs_destroy_pcpu_workers(void) {}
#endif/* CONFIG_EROFS_FS_PCPU_KTHREAD */

void z_erofs_exit_subsystem(void)
{
	z_erofs_destroy_pcpu_workers();
	destroy_workqueue(z_erofs_workqueue);
	z_erofs_destroy_pcluster_pool();
	z_erofs_crypto_disable_all_engines();
	z_erofs_exit_decompressor();
}

int __init z_erofs_init_subsystem(void)
{
	int err = z_erofs_init_decompressor();

	if (err)
		goto err_decompressor;

	err = z_erofs_create_pcluster_pool();
	if (err)
		goto err_pcluster_pool;

	z_erofs_workqueue = alloc_workqueue("erofs_worker",
			WQ_UNBOUND | WQ_HIGHPRI, num_possible_cpus());
	if (!z_erofs_workqueue) {
		err = -ENOMEM;
		goto err_workqueue_init;
	}

	return err;

err_workqueue_init:
	z_erofs_destroy_pcluster_pool();
err_pcluster_pool:
	z_erofs_exit_decompressor();
err_decompressor:
	return err;
}

enum z_erofs_pclustermode {
	/* It has previously been linked into another processing chain */
	Z_EROFS_PCLUSTER_INFLIGHT,
	/*
	 * A weaker form of Z_EROFS_PCLUSTER_FOLLOWED; the difference is that it
	 * may be dispatched to the bypass queue later due to uptodated managed
	 * folios.  All file-backed folios related to this pcluster cannot be
	 * reused for in-place I/O (or bvpage) since the pcluster may be decoded
	 * in a separate queue (and thus out of order).
	 */
	Z_EROFS_PCLUSTER_FOLLOWED_NOINPLACE,
	/*
	 * The pcluster has just been linked to our processing chain.
	 * File-backed folios (except for the head page) related to it can be
	 * used for in-place I/O (or bvpage).
	 */
	Z_EROFS_PCLUSTER_FOLLOWED,
};

struct z_erofs_frontend {
	struct inode *const inode;
	struct erofs_map_blocks map;
	struct z_erofs_bvec_iter biter;

	struct page *pagepool;
	struct page *candidate_bvpage;
	struct z_erofs_pcluster *pcl, *head;
	enum z_erofs_pclustermode mode;

	erofs_off_t headoffset;

	/* a pointer used to pick up inplace I/O pages */
	unsigned int icur;
};

#define Z_EROFS_DEFINE_FRONTEND(fe, i, ho) struct z_erofs_frontend fe = { \
	.inode = i, .head = Z_EROFS_PCLUSTER_TAIL, \
	.mode = Z_EROFS_PCLUSTER_FOLLOWED, .headoffset = ho }

static bool z_erofs_should_alloc_cache(struct z_erofs_frontend *fe)
{
	unsigned int cachestrategy = EROFS_I_SB(fe->inode)->opt.cache_strategy;

	if (cachestrategy <= EROFS_ZIP_CACHE_DISABLED)
		return false;

	if (!(fe->map.m_flags & EROFS_MAP_FULL_MAPPED))
		return true;

	if (cachestrategy >= EROFS_ZIP_CACHE_READAROUND &&
	    fe->map.m_la < fe->headoffset)
		return true;

	return false;
}

static void z_erofs_bind_cache(struct z_erofs_frontend *fe)
{
	struct address_space *mc = MNGD_MAPPING(EROFS_I_SB(fe->inode));
	struct z_erofs_pcluster *pcl = fe->pcl;
	unsigned int pclusterpages = z_erofs_pclusterpages(pcl);
	bool shouldalloc = z_erofs_should_alloc_cache(fe);
	pgoff_t poff = pcl->pos >> PAGE_SHIFT;
	bool may_bypass = true;
	/* Optimistic allocation, as in-place I/O can be used as a fallback */
	gfp_t gfp = (mapping_gfp_mask(mc) & ~__GFP_DIRECT_RECLAIM) |
			__GFP_NOMEMALLOC | __GFP_NORETRY | __GFP_NOWARN;
	struct folio *folio, *newfolio;
	unsigned int i;

	if (i_blocksize(fe->inode) != PAGE_SIZE ||
	    fe->mode < Z_EROFS_PCLUSTER_FOLLOWED)
		return;

	for (i = 0; i < pclusterpages; ++i) {
		/* Inaccurate check w/o locking to avoid unneeded lookups */
		if (READ_ONCE(pcl->compressed_bvecs[i].page))
			continue;

		folio = filemap_get_folio(mc, poff + i);
		if (IS_ERR(folio)) {
			may_bypass = false;
			if (!shouldalloc)
				continue;

			/*
			 * Allocate a managed folio for cached I/O, or it may be
			 * then filled with a file-backed folio for in-place I/O
			 */
			newfolio = filemap_alloc_folio(gfp, 0);
			if (!newfolio)
				continue;
			newfolio->private = Z_EROFS_PREALLOCATED_FOLIO;
			folio = NULL;
		}
		spin_lock(&pcl->lockref.lock);
		if (!pcl->compressed_bvecs[i].page) {
			pcl->compressed_bvecs[i].page =
				folio_page(folio ?: newfolio, 0);
			spin_unlock(&pcl->lockref.lock);
			continue;
		}
		spin_unlock(&pcl->lockref.lock);
		folio_put(folio ?: newfolio);
	}

	/*
	 * Don't perform in-place I/O if all compressed pages are available in
	 * the managed cache, as the pcluster can be moved to the bypass queue.
	 */
	if (may_bypass)
		fe->mode = Z_EROFS_PCLUSTER_FOLLOWED_NOINPLACE;
}

/* (erofs_shrinker) disconnect cached encoded data with pclusters */
static int erofs_try_to_free_all_cached_folios(struct erofs_sb_info *sbi,
					       struct z_erofs_pcluster *pcl)
{
	unsigned int pclusterpages = z_erofs_pclusterpages(pcl);
	struct folio *folio;
	int i;

	DBG_BUGON(pcl->from_meta);
	/* Each cached folio contains one page unless bs > ps is supported */
	for (i = 0; i < pclusterpages; ++i) {
		if (pcl->compressed_bvecs[i].page) {
			folio = page_folio(pcl->compressed_bvecs[i].page);
			/* Avoid reclaiming or migrating this folio */
			if (!folio_trylock(folio))
				return -EBUSY;

			if (!erofs_folio_is_managed(sbi, folio))
				continue;
			pcl->compressed_bvecs[i].page = NULL;
			folio_detach_private(folio);
			folio_unlock(folio);
		}
	}
	return 0;
}

static bool z_erofs_cache_release_folio(struct folio *folio, gfp_t gfp)
{
	struct z_erofs_pcluster *pcl = folio_get_private(folio);
	struct z_erofs_bvec *bvec = pcl->compressed_bvecs;
	struct z_erofs_bvec *end = bvec + z_erofs_pclusterpages(pcl);
	bool ret;

	if (!folio_test_private(folio))
		return true;

	ret = false;
	spin_lock(&pcl->lockref.lock);
	if (pcl->lockref.count <= 0) {
		DBG_BUGON(pcl->from_meta);
		for (; bvec < end; ++bvec) {
			if (bvec->page && page_folio(bvec->page) == folio) {
				bvec->page = NULL;
				folio_detach_private(folio);
				ret = true;
				break;
			}
		}
	}
	spin_unlock(&pcl->lockref.lock);
	return ret;
}

/*
 * It will be called only on inode eviction. In case that there are still some
 * decompression requests in progress, wait with rescheduling for a bit here.
 * An extra lock could be introduced instead but it seems unnecessary.
 */
static void z_erofs_cache_invalidate_folio(struct folio *folio,
					   size_t offset, size_t length)
{
	const size_t stop = length + offset;

	/* Check for potential overflow in debug mode */
	DBG_BUGON(stop > folio_size(folio) || stop < length);

	if (offset == 0 && stop == folio_size(folio))
		while (!z_erofs_cache_release_folio(folio, 0))
			cond_resched();
}

static const struct address_space_operations z_erofs_cache_aops = {
	.release_folio = z_erofs_cache_release_folio,
	.invalidate_folio = z_erofs_cache_invalidate_folio,
};

int z_erofs_init_super(struct super_block *sb)
{
	struct inode *inode;
	int err;

	err = z_erofs_init_pcpu_workers(sb);
	if (err)
		return err;

	inode = new_inode(sb);
	if (!inode)
		return -ENOMEM;
	set_nlink(inode, 1);
	inode->i_size = OFFSET_MAX;
	inode->i_mapping->a_ops = &z_erofs_cache_aops;
	mapping_set_gfp_mask(inode->i_mapping, GFP_KERNEL);
	EROFS_SB(sb)->managed_cache = inode;
	xa_init(&EROFS_SB(sb)->managed_pslots);
	return 0;
}

/* callers must be with pcluster lock held */
static int z_erofs_attach_page(struct z_erofs_frontend *fe,
			       struct z_erofs_bvec *bvec, bool exclusive)
{
	struct z_erofs_pcluster *pcl = fe->pcl;
	int ret;

	if (exclusive) {
		/* Inplace I/O is limited to one page for uncompressed data */
		if (pcl->algorithmformat < Z_EROFS_COMPRESSION_MAX ||
		    fe->icur <= 1) {
			/* Try to prioritize inplace I/O here */
			spin_lock(&pcl->lockref.lock);
			while (fe->icur > 0) {
				if (pcl->compressed_bvecs[--fe->icur].page)
					continue;
				pcl->compressed_bvecs[fe->icur] = *bvec;
				spin_unlock(&pcl->lockref.lock);
				return 0;
			}
			spin_unlock(&pcl->lockref.lock);
		}

		/* otherwise, check if it can be used as a bvpage */
		if (fe->mode >= Z_EROFS_PCLUSTER_FOLLOWED &&
		    !fe->candidate_bvpage)
			fe->candidate_bvpage = bvec->page;
	}
	ret = z_erofs_bvec_enqueue(&fe->biter, bvec, &fe->candidate_bvpage,
				   &fe->pagepool);
	fe->pcl->vcnt += (ret >= 0);
	return ret;
}

static bool z_erofs_get_pcluster(struct z_erofs_pcluster *pcl)
{
	if (lockref_get_not_zero(&pcl->lockref))
		return true;

	spin_lock(&pcl->lockref.lock);
	if (__lockref_is_dead(&pcl->lockref)) {
		spin_unlock(&pcl->lockref.lock);
		return false;
	}

	if (!pcl->lockref.count++)
		atomic_long_dec(&erofs_global_shrink_cnt);
	spin_unlock(&pcl->lockref.lock);
	return true;
}

static int z_erofs_register_pcluster(struct z_erofs_frontend *fe)
{
	struct erofs_map_blocks *map = &fe->map;
	struct super_block *sb = fe->inode->i_sb;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct z_erofs_pcluster *pcl, *pre;
	unsigned int pageofs_in;
	int err;

	pageofs_in = erofs_blkoff(sb, map->m_pa);
	pcl = z_erofs_alloc_pcluster(pageofs_in + map->m_plen);
	if (IS_ERR(pcl))
		return PTR_ERR(pcl);

	lockref_init(&pcl->lockref); /* one ref for this request */
	pcl->algorithmformat = map->m_algorithmformat;
	pcl->pclustersize = map->m_plen;
	pcl->length = 0;
	pcl->partial = true;
	pcl->next = fe->head;
	pcl->pos = map->m_pa;
	pcl->pageofs_in = pageofs_in;
	pcl->pageofs_out = map->m_la & ~PAGE_MASK;
	pcl->from_meta = map->m_flags & EROFS_MAP_META;
	fe->mode = Z_EROFS_PCLUSTER_FOLLOWED;

	/*
	 * lock all primary followed works before visible to others
	 * and mutex_trylock *never* fails for a new pcluster.
	 */
	mutex_init(&pcl->lock);
	DBG_BUGON(!mutex_trylock(&pcl->lock));

	if (!pcl->from_meta) {
		while (1) {
			xa_lock(&sbi->managed_pslots);
			pre = __xa_cmpxchg(&sbi->managed_pslots, pcl->pos,
					   NULL, pcl, GFP_KERNEL);
			if (!pre || xa_is_err(pre) || z_erofs_get_pcluster(pre)) {
				xa_unlock(&sbi->managed_pslots);
				break;
			}
			/* try to legitimize the current in-tree one */
			xa_unlock(&sbi->managed_pslots);
			cond_resched();
		}
		if (xa_is_err(pre)) {
			err = xa_err(pre);
			goto err_out;
		} else if (pre) {
			fe->pcl = pre;
			err = -EEXIST;
			goto err_out;
		}
	}
	fe->head = fe->pcl = pcl;
	return 0;

err_out:
	mutex_unlock(&pcl->lock);
	z_erofs_free_pcluster(pcl);
	return err;
}

static int z_erofs_pcluster_begin(struct z_erofs_frontend *fe)
{
	struct erofs_map_blocks *map = &fe->map;
	struct super_block *sb = fe->inode->i_sb;
	struct z_erofs_pcluster *pcl = NULL;
	int ret;

	DBG_BUGON(fe->pcl);
	/* must be Z_EROFS_PCLUSTER_TAIL or pointed to previous pcluster */
	DBG_BUGON(!fe->head);

	if (!(map->m_flags & EROFS_MAP_META)) {
		while (1) {
			rcu_read_lock();
			pcl = xa_load(&EROFS_SB(sb)->managed_pslots, map->m_pa);
			if (!pcl || z_erofs_get_pcluster(pcl)) {
				DBG_BUGON(pcl && map->m_pa != pcl->pos);
				rcu_read_unlock();
				break;
			}
			rcu_read_unlock();
		}
	} else if ((map->m_pa & ~PAGE_MASK) + map->m_plen > PAGE_SIZE) {
		DBG_BUGON(1);
		return -EFSCORRUPTED;
	}

	if (pcl) {
		fe->pcl = pcl;
		ret = -EEXIST;
	} else {
		ret = z_erofs_register_pcluster(fe);
	}

	if (ret == -EEXIST) {
		mutex_lock(&fe->pcl->lock);
		/* check if this pcluster hasn't been linked into any chain. */
		if (!cmpxchg(&fe->pcl->next, NULL, fe->head)) {
			/* .. so it can be attached to our submission chain */
			fe->head = fe->pcl;
			fe->mode = Z_EROFS_PCLUSTER_FOLLOWED;
		} else {	/* otherwise, it belongs to an inflight chain */
			fe->mode = Z_EROFS_PCLUSTER_INFLIGHT;
		}
	} else if (ret) {
		return ret;
	}

	z_erofs_bvec_iter_begin(&fe->biter, &fe->pcl->bvset,
				Z_EROFS_INLINE_BVECS, fe->pcl->vcnt);
	if (!fe->pcl->from_meta) {
		/* bind cache first when cached decompression is preferred */
		z_erofs_bind_cache(fe);
	} else {
		void *mptr;

		mptr = erofs_read_metabuf(&map->buf, sb, map->m_pa, false);
		if (IS_ERR(mptr)) {
			ret = PTR_ERR(mptr);
			erofs_err(sb, "failed to get inline data %d", ret);
			return ret;
		}
		get_page(map->buf.page);
		WRITE_ONCE(fe->pcl->compressed_bvecs[0].page, map->buf.page);
		fe->pcl->pageofs_in = map->m_pa & ~PAGE_MASK;
		fe->mode = Z_EROFS_PCLUSTER_FOLLOWED_NOINPLACE;
	}
	/* file-backed inplace I/O pages are traversed in reverse order */
	fe->icur = z_erofs_pclusterpages(fe->pcl);
	return 0;
}

static void z_erofs_rcu_callback(struct rcu_head *head)
{
	z_erofs_free_pcluster(container_of(head, struct z_erofs_pcluster, rcu));
}

static bool __erofs_try_to_release_pcluster(struct erofs_sb_info *sbi,
					  struct z_erofs_pcluster *pcl)
{
	if (pcl->lockref.count)
		return false;

	/*
	 * Note that all cached folios should be detached before deleted from
	 * the XArray.  Otherwise some folios could be still attached to the
	 * orphan old pcluster when the new one is available in the tree.
	 */
	if (erofs_try_to_free_all_cached_folios(sbi, pcl))
		return false;

	/*
	 * It's impossible to fail after the pcluster is freezed, but in order
	 * to avoid some race conditions, add a DBG_BUGON to observe this.
	 */
	DBG_BUGON(__xa_erase(&sbi->managed_pslots, pcl->pos) != pcl);

	lockref_mark_dead(&pcl->lockref);
	return true;
}

static bool erofs_try_to_release_pcluster(struct erofs_sb_info *sbi,
					  struct z_erofs_pcluster *pcl)
{
	bool free;

	spin_lock(&pcl->lockref.lock);
	free = __erofs_try_to_release_pcluster(sbi, pcl);
	spin_unlock(&pcl->lockref.lock);
	if (free) {
		atomic_long_dec(&erofs_global_shrink_cnt);
		call_rcu(&pcl->rcu, z_erofs_rcu_callback);
	}
	return free;
}

unsigned long z_erofs_shrink_scan(struct erofs_sb_info *sbi, unsigned long nr)
{
	struct z_erofs_pcluster *pcl;
	unsigned long index, freed = 0;

	xa_lock(&sbi->managed_pslots);
	xa_for_each(&sbi->managed_pslots, index, pcl) {
		/* try to shrink each valid pcluster */
		if (!erofs_try_to_release_pcluster(sbi, pcl))
			continue;
		xa_unlock(&sbi->managed_pslots);

		++freed;
		if (!--nr)
			return freed;
		xa_lock(&sbi->managed_pslots);
	}
	xa_unlock(&sbi->managed_pslots);
	return freed;
}

static void z_erofs_put_pcluster(struct erofs_sb_info *sbi,
		struct z_erofs_pcluster *pcl, bool try_free)
{
	bool free = false;

	if (lockref_put_or_lock(&pcl->lockref))
		return;

	DBG_BUGON(__lockref_is_dead(&pcl->lockref));
	if (!--pcl->lockref.count) {
		if (try_free && xa_trylock(&sbi->managed_pslots)) {
			free = __erofs_try_to_release_pcluster(sbi, pcl);
			xa_unlock(&sbi->managed_pslots);
		}
		atomic_long_add(!free, &erofs_global_shrink_cnt);
	}
	spin_unlock(&pcl->lockref.lock);
	if (free)
		call_rcu(&pcl->rcu, z_erofs_rcu_callback);
}

static void z_erofs_pcluster_end(struct z_erofs_frontend *fe)
{
	struct z_erofs_pcluster *pcl = fe->pcl;

	if (!pcl)
		return;

	z_erofs_bvec_iter_end(&fe->biter);
	mutex_unlock(&pcl->lock);

	if (fe->candidate_bvpage)
		fe->candidate_bvpage = NULL;

	/* Drop refcount if it doesn't belong to our processing chain */
	if (fe->mode < Z_EROFS_PCLUSTER_FOLLOWED_NOINPLACE)
		z_erofs_put_pcluster(EROFS_I_SB(fe->inode), pcl, false);
	fe->pcl = NULL;
}

static int z_erofs_read_fragment(struct super_block *sb, struct folio *folio,
			unsigned int cur, unsigned int end, erofs_off_t pos)
{
	struct inode *packed_inode = EROFS_SB(sb)->packed_inode;
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	unsigned int cnt;
	u8 *src;

	if (!packed_inode)
		return -EFSCORRUPTED;

	buf.mapping = packed_inode->i_mapping;
	for (; cur < end; cur += cnt, pos += cnt) {
		cnt = min(end - cur, sb->s_blocksize - erofs_blkoff(sb, pos));
		src = erofs_bread(&buf, pos, true);
		if (IS_ERR(src)) {
			erofs_put_metabuf(&buf);
			return PTR_ERR(src);
		}
		memcpy_to_folio(folio, cur, src, cnt);
	}
	erofs_put_metabuf(&buf);
	return 0;
}

static int z_erofs_scan_folio(struct z_erofs_frontend *f,
			      struct folio *folio, bool ra)
{
	struct inode *const inode = f->inode;
	struct erofs_map_blocks *const map = &f->map;
	const loff_t offset = folio_pos(folio);
	const unsigned int bs = i_blocksize(inode);
	unsigned int end = folio_size(folio), split = 0, cur, pgs;
	bool tight, excl;
	int err = 0;

	tight = (bs == PAGE_SIZE);
	erofs_onlinefolio_init(folio);
	do {
		if (offset + end - 1 < map->m_la ||
		    offset + end - 1 >= map->m_la + map->m_llen) {
			z_erofs_pcluster_end(f);
			map->m_la = offset + end - 1;
			map->m_llen = 0;
			err = z_erofs_map_blocks_iter(inode, map, 0);
			if (err)
				break;
		}

		cur = offset > map->m_la ? 0 : map->m_la - offset;
		pgs = round_down(cur, PAGE_SIZE);
		/* bump split parts first to avoid several separate cases */
		++split;

		if (!(map->m_flags & EROFS_MAP_MAPPED)) {
			folio_zero_segment(folio, cur, end);
			tight = false;
		} else if (map->m_flags & __EROFS_MAP_FRAGMENT) {
			erofs_off_t fpos = offset + cur - map->m_la;

			err = z_erofs_read_fragment(inode->i_sb, folio, cur,
					cur + min(map->m_llen - fpos, end - cur),
					EROFS_I(inode)->z_fragmentoff + fpos);
			if (err)
				break;
			tight = false;
		} else {
			if (!f->pcl) {
				err = z_erofs_pcluster_begin(f);
				if (err)
					break;
				f->pcl->besteffort |= !ra;
			}

			pgs = round_down(end - 1, PAGE_SIZE);
			/*
			 * Ensure this partial page belongs to this submit chain
			 * rather than other concurrent submit chains or
			 * noio(bypass) chains since those chains are handled
			 * asynchronously thus it cannot be used for inplace I/O
			 * or bvpage (should be processed in the strict order.)
			 */
			tight &= (f->mode >= Z_EROFS_PCLUSTER_FOLLOWED);
			excl = false;
			if (cur <= pgs) {
				excl = (split <= 1) || tight;
				cur = pgs;
			}

			err = z_erofs_attach_page(f, &((struct z_erofs_bvec) {
				.page = folio_page(folio, pgs >> PAGE_SHIFT),
				.offset = offset + pgs - map->m_la,
				.end = end - pgs, }), excl);
			if (err)
				break;

			erofs_onlinefolio_split(folio);
			if (f->pcl->length < offset + end - map->m_la) {
				f->pcl->length = offset + end - map->m_la;
				f->pcl->pageofs_out = map->m_la & ~PAGE_MASK;
			}
			if ((map->m_flags & EROFS_MAP_FULL_MAPPED) &&
			    !(map->m_flags & EROFS_MAP_PARTIAL_REF) &&
			    f->pcl->length == map->m_llen)
				f->pcl->partial = false;
		}
		/* shorten the remaining extent to update progress */
		map->m_llen = offset + cur - map->m_la;
		map->m_flags &= ~EROFS_MAP_FULL_MAPPED;
		if (cur <= pgs) {
			split = cur < pgs;
			tight = (bs == PAGE_SIZE);
		}
	} while ((end = cur) > 0);
	erofs_onlinefolio_end(folio, err, false);
	return err;
}

static bool z_erofs_is_sync_decompress(struct erofs_sb_info *sbi,
				       unsigned int readahead_pages)
{
	/* auto: enable for read_folio, disable for readahead */
	if ((sbi->opt.sync_decompress == EROFS_SYNC_DECOMPRESS_AUTO) &&
	    !readahead_pages)
		return true;

	if ((sbi->opt.sync_decompress == EROFS_SYNC_DECOMPRESS_FORCE_ON) &&
	    (readahead_pages <= sbi->opt.max_sync_decompress_pages))
		return true;

	return false;
}

static bool z_erofs_page_is_invalidated(struct page *page)
{
	return !page_folio(page)->mapping && !z_erofs_is_shortlived_page(page);
}

struct z_erofs_backend {
	struct page *onstack_pages[Z_EROFS_ONSTACK_PAGES];
	struct super_block *sb;
	struct z_erofs_pcluster *pcl;
	/* pages with the longest decompressed length for deduplication */
	struct page **decompressed_pages;
	/* pages to keep the compressed data */
	struct page **compressed_pages;

	struct list_head decompressed_secondary_bvecs;
	struct page **pagepool;
	unsigned int onstack_used, nr_pages;
	/* indicate if temporary copies should be preserved for later use */
	bool keepxcpy;
};

struct z_erofs_bvec_item {
	struct z_erofs_bvec bvec;
	struct list_head list;
};

static void z_erofs_do_decompressed_bvec(struct z_erofs_backend *be,
					 struct z_erofs_bvec *bvec)
{
	int poff = bvec->offset + be->pcl->pageofs_out;
	struct z_erofs_bvec_item *item;
	struct page **page;

	if (!(poff & ~PAGE_MASK) && (bvec->end == PAGE_SIZE ||
			bvec->offset + bvec->end == be->pcl->length)) {
		DBG_BUGON((poff >> PAGE_SHIFT) >= be->nr_pages);
		page = be->decompressed_pages + (poff >> PAGE_SHIFT);
		if (!*page) {
			*page = bvec->page;
			return;
		}
	} else {
		be->keepxcpy = true;
	}

	/* (cold path) one pcluster is requested multiple times */
	item = kmalloc(sizeof(*item), GFP_KERNEL | __GFP_NOFAIL);
	item->bvec = *bvec;
	list_add(&item->list, &be->decompressed_secondary_bvecs);
}

static void z_erofs_fill_other_copies(struct z_erofs_backend *be, int err)
{
	unsigned int off0 = be->pcl->pageofs_out;
	struct list_head *p, *n;

	list_for_each_safe(p, n, &be->decompressed_secondary_bvecs) {
		struct z_erofs_bvec_item *bvi;
		unsigned int end, cur;
		void *dst, *src;

		bvi = container_of(p, struct z_erofs_bvec_item, list);
		cur = bvi->bvec.offset < 0 ? -bvi->bvec.offset : 0;
		end = min_t(unsigned int, be->pcl->length - bvi->bvec.offset,
			    bvi->bvec.end);
		dst = kmap_local_page(bvi->bvec.page);
		while (cur < end) {
			unsigned int pgnr, scur, len;

			pgnr = (bvi->bvec.offset + cur + off0) >> PAGE_SHIFT;
			DBG_BUGON(pgnr >= be->nr_pages);

			scur = bvi->bvec.offset + cur -
					((pgnr << PAGE_SHIFT) - off0);
			len = min_t(unsigned int, end - cur, PAGE_SIZE - scur);
			if (!be->decompressed_pages[pgnr]) {
				err = -EFSCORRUPTED;
				cur += len;
				continue;
			}
			src = kmap_local_page(be->decompressed_pages[pgnr]);
			memcpy(dst + cur, src + scur, len);
			kunmap_local(src);
			cur += len;
		}
		kunmap_local(dst);
		erofs_onlinefolio_end(page_folio(bvi->bvec.page), err, true);
		list_del(p);
		kfree(bvi);
	}
}

static void z_erofs_parse_out_bvecs(struct z_erofs_backend *be)
{
	struct z_erofs_pcluster *pcl = be->pcl;
	struct z_erofs_bvec_iter biter;
	struct page *old_bvpage;
	int i;

	z_erofs_bvec_iter_begin(&biter, &pcl->bvset, Z_EROFS_INLINE_BVECS, 0);
	for (i = 0; i < pcl->vcnt; ++i) {
		struct z_erofs_bvec bvec;

		z_erofs_bvec_dequeue(&biter, &bvec, &old_bvpage);

		if (old_bvpage)
			z_erofs_put_shortlivedpage(be->pagepool, old_bvpage);

		DBG_BUGON(z_erofs_page_is_invalidated(bvec.page));
		z_erofs_do_decompressed_bvec(be, &bvec);
	}

	old_bvpage = z_erofs_bvec_iter_end(&biter);
	if (old_bvpage)
		z_erofs_put_shortlivedpage(be->pagepool, old_bvpage);
}

static int z_erofs_parse_in_bvecs(struct z_erofs_backend *be, bool *overlapped)
{
	struct z_erofs_pcluster *pcl = be->pcl;
	unsigned int pclusterpages = z_erofs_pclusterpages(pcl);
	int i, err = 0;

	*overlapped = false;
	for (i = 0; i < pclusterpages; ++i) {
		struct z_erofs_bvec *bvec = &pcl->compressed_bvecs[i];
		struct page *page = bvec->page;

		/* compressed data ought to be valid when decompressing */
		if (IS_ERR(page) || !page) {
			bvec->page = NULL;	/* clear the failure reason */
			err = page ? PTR_ERR(page) : -EIO;
			continue;
		}
		be->compressed_pages[i] = page;

		if (pcl->from_meta ||
		    erofs_folio_is_managed(EROFS_SB(be->sb), page_folio(page))) {
			if (!PageUptodate(page))
				err = -EIO;
			continue;
		}

		DBG_BUGON(z_erofs_page_is_invalidated(page));
		if (z_erofs_is_shortlived_page(page))
			continue;
		z_erofs_do_decompressed_bvec(be, bvec);
		*overlapped = true;
	}
	return err;
}

static int z_erofs_decompress_pcluster(struct z_erofs_backend *be, int err)
{
	struct erofs_sb_info *const sbi = EROFS_SB(be->sb);
	struct z_erofs_pcluster *pcl = be->pcl;
	unsigned int pclusterpages = z_erofs_pclusterpages(pcl);
	const struct z_erofs_decompressor *decomp =
				z_erofs_decomp[pcl->algorithmformat];
	int i, j, jtop, err2;
	struct page *page;
	bool overlapped;
	bool try_free = true;

	mutex_lock(&pcl->lock);
	be->nr_pages = PAGE_ALIGN(pcl->length + pcl->pageofs_out) >> PAGE_SHIFT;

	/* allocate (de)compressed page arrays if cannot be kept on stack */
	be->decompressed_pages = NULL;
	be->compressed_pages = NULL;
	be->onstack_used = 0;
	if (be->nr_pages <= Z_EROFS_ONSTACK_PAGES) {
		be->decompressed_pages = be->onstack_pages;
		be->onstack_used = be->nr_pages;
		memset(be->decompressed_pages, 0,
		       sizeof(struct page *) * be->nr_pages);
	}

	if (pclusterpages + be->onstack_used <= Z_EROFS_ONSTACK_PAGES)
		be->compressed_pages = be->onstack_pages + be->onstack_used;

	if (!be->decompressed_pages)
		be->decompressed_pages =
			kvcalloc(be->nr_pages, sizeof(struct page *),
				 GFP_KERNEL | __GFP_NOFAIL);
	if (!be->compressed_pages)
		be->compressed_pages =
			kvcalloc(pclusterpages, sizeof(struct page *),
				 GFP_KERNEL | __GFP_NOFAIL);

	z_erofs_parse_out_bvecs(be);
	err2 = z_erofs_parse_in_bvecs(be, &overlapped);
	if (err2)
		err = err2;
	if (!err)
		err = decomp->decompress(&(struct z_erofs_decompress_req) {
					.sb = be->sb,
					.in = be->compressed_pages,
					.out = be->decompressed_pages,
					.inpages = pclusterpages,
					.outpages = be->nr_pages,
					.pageofs_in = pcl->pageofs_in,
					.pageofs_out = pcl->pageofs_out,
					.inputsize = pcl->pclustersize,
					.outputsize = pcl->length,
					.alg = pcl->algorithmformat,
					.inplace_io = overlapped,
					.partial_decoding = pcl->partial,
					.fillgaps = be->keepxcpy,
					.gfp = pcl->besteffort ? GFP_KERNEL :
						GFP_NOWAIT | __GFP_NORETRY
				 }, be->pagepool);

	/* must handle all compressed pages before actual file pages */
	if (pcl->from_meta) {
		page = pcl->compressed_bvecs[0].page;
		WRITE_ONCE(pcl->compressed_bvecs[0].page, NULL);
		put_page(page);
	} else {
		/* managed folios are still left in compressed_bvecs[] */
		for (i = 0; i < pclusterpages; ++i) {
			page = be->compressed_pages[i];
			if (!page)
				continue;
			if (erofs_folio_is_managed(sbi, page_folio(page))) {
				try_free = false;
				continue;
			}
			(void)z_erofs_put_shortlivedpage(be->pagepool, page);
			WRITE_ONCE(pcl->compressed_bvecs[i].page, NULL);
		}
	}
	if (be->compressed_pages < be->onstack_pages ||
	    be->compressed_pages >= be->onstack_pages + Z_EROFS_ONSTACK_PAGES)
		kvfree(be->compressed_pages);

	jtop = 0;
	z_erofs_fill_other_copies(be, err);
	for (i = 0; i < be->nr_pages; ++i) {
		page = be->decompressed_pages[i];
		if (!page)
			continue;

		DBG_BUGON(z_erofs_page_is_invalidated(page));
		if (!z_erofs_is_shortlived_page(page)) {
			erofs_onlinefolio_end(page_folio(page), err, true);
			continue;
		}
		if (pcl->algorithmformat != Z_EROFS_COMPRESSION_LZ4) {
			erofs_pagepool_add(be->pagepool, page);
			continue;
		}
		for (j = 0; j < jtop && be->decompressed_pages[j] != page; ++j)
			;
		if (j >= jtop)	/* this bounce page is newly detected */
			be->decompressed_pages[jtop++] = page;
	}
	while (jtop)
		erofs_pagepool_add(be->pagepool,
				   be->decompressed_pages[--jtop]);
	if (be->decompressed_pages != be->onstack_pages)
		kvfree(be->decompressed_pages);

	pcl->length = 0;
	pcl->partial = true;
	pcl->besteffort = false;
	pcl->bvset.nextpage = NULL;
	pcl->vcnt = 0;

	/* pcluster lock MUST be taken before the following line */
	WRITE_ONCE(pcl->next, NULL);
	mutex_unlock(&pcl->lock);

	if (pcl->from_meta)
		z_erofs_free_pcluster(pcl);
	else
		z_erofs_put_pcluster(sbi, pcl, try_free);
	return err;
}

static int z_erofs_decompress_queue(const struct z_erofs_decompressqueue *io,
				    struct page **pagepool)
{
	struct z_erofs_backend be = {
		.sb = io->sb,
		.pagepool = pagepool,
		.decompressed_secondary_bvecs =
			LIST_HEAD_INIT(be.decompressed_secondary_bvecs),
		.pcl = io->head,
	};
	struct z_erofs_pcluster *next;
	int err = io->eio ? -EIO : 0;

	for (; be.pcl != Z_EROFS_PCLUSTER_TAIL; be.pcl = next) {
		DBG_BUGON(!be.pcl);
		next = READ_ONCE(be.pcl->next);
		err = z_erofs_decompress_pcluster(&be, err) ?: err;
	}
	return err;
}

static void z_erofs_decompressqueue_work(struct work_struct *work)
{
	struct z_erofs_decompressqueue *bgq =
		container_of(work, struct z_erofs_decompressqueue, u.work);
	struct page *pagepool = NULL;

	DBG_BUGON(bgq->head == Z_EROFS_PCLUSTER_TAIL);
	z_erofs_decompress_queue(bgq, &pagepool);
	erofs_release_pages(&pagepool);
	kvfree(bgq);
}

#ifdef CONFIG_EROFS_FS_PCPU_KTHREAD
static void z_erofs_decompressqueue_kthread_work(struct kthread_work *work)
{
	z_erofs_decompressqueue_work((struct work_struct *)work);
}
#endif

static void z_erofs_decompress_kickoff(struct z_erofs_decompressqueue *io,
				       int bios)
{
	struct erofs_sb_info *const sbi = EROFS_SB(io->sb);

	/* wake up the caller thread for sync decompression */
	if (io->sync) {
		if (!atomic_add_return(bios, &io->pending_bios))
			complete(&io->u.done);
		return;
	}

	if (atomic_add_return(bios, &io->pending_bios))
		return;
	/* Use (kthread_)work and sync decompression for atomic contexts only */
	if (!in_task() || irqs_disabled() || rcu_read_lock_any_held()) {
#ifdef CONFIG_EROFS_FS_PCPU_KTHREAD
		struct kthread_worker *worker;

		rcu_read_lock();
		worker = rcu_dereference(
				z_erofs_pcpu_workers[raw_smp_processor_id()]);
		if (!worker) {
			INIT_WORK(&io->u.work, z_erofs_decompressqueue_work);
			queue_work(z_erofs_workqueue, &io->u.work);
		} else {
			kthread_queue_work(worker, &io->u.kthread_work);
		}
		rcu_read_unlock();
#else
		queue_work(z_erofs_workqueue, &io->u.work);
#endif
		/* enable sync decompression for readahead */
		if (sbi->opt.sync_decompress == EROFS_SYNC_DECOMPRESS_AUTO)
			sbi->opt.sync_decompress = EROFS_SYNC_DECOMPRESS_FORCE_ON;
		return;
	}
	z_erofs_decompressqueue_work(&io->u.work);
}

static void z_erofs_fill_bio_vec(struct bio_vec *bvec,
				 struct z_erofs_frontend *f,
				 struct z_erofs_pcluster *pcl,
				 unsigned int nr,
				 struct address_space *mc)
{
	gfp_t gfp = mapping_gfp_mask(mc);
	bool tocache = false;
	struct z_erofs_bvec zbv;
	struct address_space *mapping;
	struct folio *folio;
	struct page *page;
	int bs = i_blocksize(f->inode);

	/* Except for inplace folios, the entire folio can be used for I/Os */
	bvec->bv_offset = 0;
	bvec->bv_len = PAGE_SIZE;
repeat:
	spin_lock(&pcl->lockref.lock);
	zbv = pcl->compressed_bvecs[nr];
	spin_unlock(&pcl->lockref.lock);
	if (!zbv.page)
		goto out_allocfolio;

	bvec->bv_page = zbv.page;
	DBG_BUGON(z_erofs_is_shortlived_page(bvec->bv_page));

	folio = page_folio(zbv.page);
	/* For preallocated managed folios, add them to page cache here */
	if (folio->private == Z_EROFS_PREALLOCATED_FOLIO) {
		tocache = true;
		goto out_tocache;
	}

	mapping = READ_ONCE(folio->mapping);
	/*
	 * File-backed folios for inplace I/Os are all locked steady,
	 * therefore it is impossible for `mapping` to be NULL.
	 */
	if (mapping && mapping != mc) {
		if (zbv.offset < 0)
			bvec->bv_offset = round_up(-zbv.offset, bs);
		bvec->bv_len = round_up(zbv.end, bs) - bvec->bv_offset;
		return;
	}

	folio_lock(folio);
	if (likely(folio->mapping == mc)) {
		/*
		 * The cached folio is still in managed cache but without
		 * a valid `->private` pcluster hint.  Let's reconnect them.
		 */
		if (!folio_test_private(folio)) {
			folio_attach_private(folio, pcl);
			/* compressed_bvecs[] already takes a ref before */
			folio_put(folio);
		}
		if (likely(folio->private == pcl))  {
			/* don't submit cache I/Os again if already uptodate */
			if (folio_test_uptodate(folio)) {
				folio_unlock(folio);
				bvec->bv_page = NULL;
			}
			return;
		}
		/*
		 * Already linked with another pcluster, which only appears in
		 * crafted images by fuzzers for now.  But handle this anyway.
		 */
		tocache = false;	/* use temporary short-lived pages */
	} else {
		DBG_BUGON(1); /* referenced managed folios can't be truncated */
		tocache = true;
	}
	folio_unlock(folio);
	folio_put(folio);
out_allocfolio:
	page = __erofs_allocpage(&f->pagepool, gfp, true);
	spin_lock(&pcl->lockref.lock);
	if (unlikely(pcl->compressed_bvecs[nr].page != zbv.page)) {
		if (page)
			erofs_pagepool_add(&f->pagepool, page);
		spin_unlock(&pcl->lockref.lock);
		cond_resched();
		goto repeat;
	}
	pcl->compressed_bvecs[nr].page = page ? page : ERR_PTR(-ENOMEM);
	spin_unlock(&pcl->lockref.lock);
	bvec->bv_page = page;
	if (!page)
		return;
	folio = page_folio(page);
out_tocache:
	if (!tocache || bs != PAGE_SIZE ||
	    filemap_add_folio(mc, folio, (pcl->pos >> PAGE_SHIFT) + nr, gfp)) {
		/* turn into a temporary shortlived folio (1 ref) */
		folio->private = (void *)Z_EROFS_SHORTLIVED_PAGE;
		return;
	}
	folio_attach_private(folio, pcl);
	/* drop a refcount added by allocpage (then 2 refs in total here) */
	folio_put(folio);
}

static struct z_erofs_decompressqueue *jobqueue_init(struct super_block *sb,
			      struct z_erofs_decompressqueue *fgq, bool *fg)
{
	struct z_erofs_decompressqueue *q;

	if (fg && !*fg) {
		q = kvzalloc(sizeof(*q), GFP_KERNEL | __GFP_NOWARN);
		if (!q) {
			*fg = true;
			goto fg_out;
		}
#ifdef CONFIG_EROFS_FS_PCPU_KTHREAD
		kthread_init_work(&q->u.kthread_work,
				  z_erofs_decompressqueue_kthread_work);
#else
		INIT_WORK(&q->u.work, z_erofs_decompressqueue_work);
#endif
	} else {
fg_out:
		q = fgq;
		init_completion(&fgq->u.done);
		atomic_set(&fgq->pending_bios, 0);
		q->eio = false;
		q->sync = true;
	}
	q->sb = sb;
	q->head = Z_EROFS_PCLUSTER_TAIL;
	return q;
}

/* define decompression jobqueue types */
enum {
	JQ_BYPASS,
	JQ_SUBMIT,
	NR_JOBQUEUES,
};

static void z_erofs_move_to_bypass_queue(struct z_erofs_pcluster *pcl,
					 struct z_erofs_pcluster *next,
					 struct z_erofs_pcluster **qtail[])
{
	WRITE_ONCE(pcl->next, Z_EROFS_PCLUSTER_TAIL);
	WRITE_ONCE(*qtail[JQ_SUBMIT], next);
	WRITE_ONCE(*qtail[JQ_BYPASS], pcl);
	qtail[JQ_BYPASS] = &pcl->next;
}

static void z_erofs_endio(struct bio *bio)
{
	struct z_erofs_decompressqueue *q = bio->bi_private;
	blk_status_t err = bio->bi_status;
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio) {
		struct folio *folio = fi.folio;

		DBG_BUGON(folio_test_uptodate(folio));
		DBG_BUGON(z_erofs_page_is_invalidated(&folio->page));
		if (!erofs_folio_is_managed(EROFS_SB(q->sb), folio))
			continue;

		if (!err)
			folio_mark_uptodate(folio);
		folio_unlock(folio);
	}
	if (err)
		q->eio = true;
	z_erofs_decompress_kickoff(q, -1);
	if (bio->bi_bdev)
		bio_put(bio);
}

static void z_erofs_submit_queue(struct z_erofs_frontend *f,
				 struct z_erofs_decompressqueue *fgq,
				 bool *force_fg, bool readahead)
{
	struct super_block *sb = f->inode->i_sb;
	struct address_space *mc = MNGD_MAPPING(EROFS_SB(sb));
	struct z_erofs_pcluster **qtail[NR_JOBQUEUES];
	struct z_erofs_decompressqueue *q[NR_JOBQUEUES];
	struct z_erofs_pcluster *pcl, *next;
	/* bio is NULL initially, so no need to initialize last_{index,bdev} */
	erofs_off_t last_pa;
	unsigned int nr_bios = 0;
	struct bio *bio = NULL;
	unsigned long pflags;
	int memstall = 0;

	/* No need to read from device for pclusters in the bypass queue. */
	q[JQ_BYPASS] = jobqueue_init(sb, fgq + JQ_BYPASS, NULL);
	q[JQ_SUBMIT] = jobqueue_init(sb, fgq + JQ_SUBMIT, force_fg);

	qtail[JQ_BYPASS] = &q[JQ_BYPASS]->head;
	qtail[JQ_SUBMIT] = &q[JQ_SUBMIT]->head;

	/* by default, all need io submission */
	q[JQ_SUBMIT]->head = next = f->head;

	do {
		struct erofs_map_dev mdev;
		erofs_off_t cur, end;
		struct bio_vec bvec;
		unsigned int i = 0;
		bool bypass = true;

		pcl = next;
		next = READ_ONCE(pcl->next);
		if (pcl->from_meta) {
			z_erofs_move_to_bypass_queue(pcl, next, qtail);
			continue;
		}

		/* no device id here, thus it will always succeed */
		mdev = (struct erofs_map_dev) {
			.m_pa = round_down(pcl->pos, sb->s_blocksize),
		};
		(void)erofs_map_dev(sb, &mdev);

		cur = mdev.m_pa;
		end = round_up(cur + pcl->pageofs_in + pcl->pclustersize,
			       sb->s_blocksize);
		do {
			bvec.bv_page = NULL;
			if (bio && (cur != last_pa ||
				    bio->bi_bdev != mdev.m_bdev)) {
drain_io:
				if (erofs_is_fileio_mode(EROFS_SB(sb)))
					erofs_fileio_submit_bio(bio);
				else if (erofs_is_fscache_mode(sb))
					erofs_fscache_submit_bio(bio);
				else
					submit_bio(bio);

				if (memstall) {
					psi_memstall_leave(&pflags);
					memstall = 0;
				}
				bio = NULL;
			}

			if (!bvec.bv_page) {
				z_erofs_fill_bio_vec(&bvec, f, pcl, i++, mc);
				if (!bvec.bv_page)
					continue;
				if (cur + bvec.bv_len > end)
					bvec.bv_len = end - cur;
				DBG_BUGON(bvec.bv_len < sb->s_blocksize);
			}

			if (unlikely(PageWorkingset(bvec.bv_page)) &&
			    !memstall) {
				psi_memstall_enter(&pflags);
				memstall = 1;
			}

			if (!bio) {
				if (erofs_is_fileio_mode(EROFS_SB(sb)))
					bio = erofs_fileio_bio_alloc(&mdev);
				else if (erofs_is_fscache_mode(sb))
					bio = erofs_fscache_bio_alloc(&mdev);
				else
					bio = bio_alloc(mdev.m_bdev, BIO_MAX_VECS,
							REQ_OP_READ, GFP_NOIO);
				bio->bi_end_io = z_erofs_endio;
				bio->bi_iter.bi_sector =
						(mdev.m_dif->fsoff + cur) >> 9;
				bio->bi_private = q[JQ_SUBMIT];
				if (readahead)
					bio->bi_opf |= REQ_RAHEAD;
				++nr_bios;
			}

			if (!bio_add_page(bio, bvec.bv_page, bvec.bv_len,
					  bvec.bv_offset))
				goto drain_io;
			last_pa = cur + bvec.bv_len;
			bypass = false;
		} while ((cur += bvec.bv_len) < end);

		if (!bypass)
			qtail[JQ_SUBMIT] = &pcl->next;
		else
			z_erofs_move_to_bypass_queue(pcl, next, qtail);
	} while (next != Z_EROFS_PCLUSTER_TAIL);

	if (bio) {
		if (erofs_is_fileio_mode(EROFS_SB(sb)))
			erofs_fileio_submit_bio(bio);
		else if (erofs_is_fscache_mode(sb))
			erofs_fscache_submit_bio(bio);
		else
			submit_bio(bio);
	}
	if (memstall)
		psi_memstall_leave(&pflags);

	/*
	 * although background is preferred, no one is pending for submission.
	 * don't issue decompression but drop it directly instead.
	 */
	if (!*force_fg && !nr_bios) {
		kvfree(q[JQ_SUBMIT]);
		return;
	}
	z_erofs_decompress_kickoff(q[JQ_SUBMIT], nr_bios);
}

static int z_erofs_runqueue(struct z_erofs_frontend *f, unsigned int rapages)
{
	struct z_erofs_decompressqueue io[NR_JOBQUEUES];
	struct erofs_sb_info *sbi = EROFS_I_SB(f->inode);
	bool force_fg = z_erofs_is_sync_decompress(sbi, rapages);
	int err;

	if (f->head == Z_EROFS_PCLUSTER_TAIL)
		return 0;
	z_erofs_submit_queue(f, io, &force_fg, !!rapages);

	/* handle bypass queue (no i/o pclusters) immediately */
	err = z_erofs_decompress_queue(&io[JQ_BYPASS], &f->pagepool);
	if (!force_fg)
		return err;

	/* wait until all bios are completed */
	wait_for_completion_io(&io[JQ_SUBMIT].u.done);

	/* handle synchronous decompress queue in the caller context */
	return z_erofs_decompress_queue(&io[JQ_SUBMIT], &f->pagepool) ?: err;
}

/*
 * Since partial uptodate is still unimplemented for now, we have to use
 * approximate readmore strategies as a start.
 */
static void z_erofs_pcluster_readmore(struct z_erofs_frontend *f,
		struct readahead_control *rac, bool backmost)
{
	struct inode *inode = f->inode;
	struct erofs_map_blocks *map = &f->map;
	erofs_off_t cur, end, headoffset = f->headoffset;
	int err;

	if (backmost) {
		if (rac)
			end = headoffset + readahead_length(rac) - 1;
		else
			end = headoffset + PAGE_SIZE - 1;
		map->m_la = end;
		err = z_erofs_map_blocks_iter(inode, map,
					      EROFS_GET_BLOCKS_READMORE);
		if (err)
			return;

		/* expand ra for the trailing edge if readahead */
		if (rac) {
			cur = round_up(map->m_la + map->m_llen, PAGE_SIZE);
			readahead_expand(rac, headoffset, cur - headoffset);
			return;
		}
		end = round_up(end, PAGE_SIZE);
	} else {
		end = round_up(map->m_la, PAGE_SIZE);
		if (!map->m_llen)
			return;
	}

	cur = map->m_la + map->m_llen - 1;
	while ((cur >= end) && (cur < i_size_read(inode))) {
		pgoff_t index = cur >> PAGE_SHIFT;
		struct folio *folio;

		folio = erofs_grab_folio_nowait(inode->i_mapping, index);
		if (!IS_ERR_OR_NULL(folio)) {
			if (folio_test_uptodate(folio))
				folio_unlock(folio);
			else
				z_erofs_scan_folio(f, folio, !!rac);
			folio_put(folio);
		}

		if (cur < PAGE_SIZE)
			break;
		cur = (index << PAGE_SHIFT) - 1;
	}
}

static int z_erofs_read_folio(struct file *file, struct folio *folio)
{
	struct inode *const inode = folio->mapping->host;
	Z_EROFS_DEFINE_FRONTEND(f, inode, folio_pos(folio));
	int err;

	trace_erofs_read_folio(folio, false);
	z_erofs_pcluster_readmore(&f, NULL, true);
	err = z_erofs_scan_folio(&f, folio, false);
	z_erofs_pcluster_readmore(&f, NULL, false);
	z_erofs_pcluster_end(&f);

	/* if some pclusters are ready, need submit them anyway */
	err = z_erofs_runqueue(&f, 0) ?: err;
	if (err && err != -EINTR)
		erofs_err(inode->i_sb, "read error %d @ %lu of nid %llu",
			  err, folio->index, EROFS_I(inode)->nid);

	erofs_put_metabuf(&f.map.buf);
	erofs_release_pages(&f.pagepool);
	return err;
}

static void z_erofs_readahead(struct readahead_control *rac)
{
	struct inode *const inode = rac->mapping->host;
	Z_EROFS_DEFINE_FRONTEND(f, inode, readahead_pos(rac));
	unsigned int nrpages = readahead_count(rac);
	struct folio *head = NULL, *folio;
	int err;

	trace_erofs_readahead(inode, readahead_index(rac), nrpages, false);
	z_erofs_pcluster_readmore(&f, rac, true);
	while ((folio = readahead_folio(rac))) {
		folio->private = head;
		head = folio;
	}

	/* traverse in reverse order for best metadata I/O performance */
	while (head) {
		folio = head;
		head = folio_get_private(folio);

		err = z_erofs_scan_folio(&f, folio, true);
		if (err && err != -EINTR)
			erofs_err(inode->i_sb, "readahead error at folio %lu @ nid %llu",
				  folio->index, EROFS_I(inode)->nid);
	}
	z_erofs_pcluster_readmore(&f, rac, false);
	z_erofs_pcluster_end(&f);

	(void)z_erofs_runqueue(&f, nrpages);
	erofs_put_metabuf(&f.map.buf);
	erofs_release_pages(&f.pagepool);
}

const struct address_space_operations z_erofs_aops = {
	.read_folio = z_erofs_read_folio,
	.readahead = z_erofs_readahead,
};

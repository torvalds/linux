// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2022 Alibaba Cloud
 */
#include "zdata.h"
#include "compress.h"
#include <linux/prefetch.h>
#include <linux/psi.h>

#include <trace/events/erofs.h>

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
	_PCLP(Z_EROFS_PCLUSTER_MAX_PAGES)
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
				struct page **candidate_bvpage)
{
	if (iter->cur == iter->nr) {
		if (!*candidate_bvpage)
			return -EAGAIN;

		DBG_BUGON(iter->bvset->nextpage);
		iter->bvset->nextpage = *candidate_bvpage;
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

static struct z_erofs_pcluster *z_erofs_alloc_pcluster(unsigned int nrpages)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pcluster_pool); ++i) {
		struct z_erofs_pcluster_slab *pcs = pcluster_pool + i;
		struct z_erofs_pcluster *pcl;

		if (nrpages > pcs->maxpages)
			continue;

		pcl = kmem_cache_zalloc(pcs->slab, GFP_NOFS);
		if (!pcl)
			return ERR_PTR(-ENOMEM);
		pcl->pclusterpages = nrpages;
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

/* how to allocate cached pages for a pcluster */
enum z_erofs_cache_alloctype {
	DONTALLOC,	/* don't allocate any cached pages */
	/*
	 * try to use cached I/O if page allocation succeeds or fallback
	 * to in-place I/O instead to avoid any direct reclaim.
	 */
	TRYALLOC,
};

/*
 * tagged pointer with 1-bit tag for all compressed pages
 * tag 0 - the page is just found with an extra page reference
 */
typedef tagptr1_t compressed_page_t;

#define tag_compressed_page_justfound(page) \
	tagptr_fold(compressed_page_t, page, 1)

static struct workqueue_struct *z_erofs_workqueue __read_mostly;

void z_erofs_exit_zip_subsystem(void)
{
	destroy_workqueue(z_erofs_workqueue);
	z_erofs_destroy_pcluster_pool();
}

static inline int z_erofs_init_workqueue(void)
{
	const unsigned int onlinecpus = num_possible_cpus();

	/*
	 * no need to spawn too many threads, limiting threads could minimum
	 * scheduling overhead, perhaps per-CPU threads should be better?
	 */
	z_erofs_workqueue = alloc_workqueue("erofs_unzipd",
					    WQ_UNBOUND | WQ_HIGHPRI,
					    onlinecpus + onlinecpus / 4);
	return z_erofs_workqueue ? 0 : -ENOMEM;
}

int __init z_erofs_init_zip_subsystem(void)
{
	int err = z_erofs_create_pcluster_pool();

	if (err)
		return err;
	err = z_erofs_init_workqueue();
	if (err)
		z_erofs_destroy_pcluster_pool();
	return err;
}

enum z_erofs_pclustermode {
	Z_EROFS_PCLUSTER_INFLIGHT,
	/*
	 * The current pclusters was the tail of an exist chain, in addition
	 * that the previous processed chained pclusters are all decided to
	 * be hooked up to it.
	 * A new chain will be created for the remaining pclusters which are
	 * not processed yet, so different from Z_EROFS_PCLUSTER_FOLLOWED,
	 * the next pcluster cannot reuse the whole page safely for inplace I/O
	 * in the following scenario:
	 *  ________________________________________________________________
	 * |      tail (partial) page     |       head (partial) page       |
	 * |   (belongs to the next pcl)  |   (belongs to the current pcl)  |
	 * |_______PCLUSTER_FOLLOWED______|________PCLUSTER_HOOKED__________|
	 */
	Z_EROFS_PCLUSTER_HOOKED,
	/*
	 * a weak form of Z_EROFS_PCLUSTER_FOLLOWED, the difference is that it
	 * could be dispatched into bypass queue later due to uptodated managed
	 * pages. All related online pages cannot be reused for inplace I/O (or
	 * bvpage) since it can be directly decoded without I/O submission.
	 */
	Z_EROFS_PCLUSTER_FOLLOWED_NOINPLACE,
	/*
	 * The current collection has been linked with the owned chain, and
	 * could also be linked with the remaining collections, which means
	 * if the processing page is the tail page of the collection, thus
	 * the current collection can safely use the whole page (since
	 * the previous collection is under control) for in-place I/O, as
	 * illustrated below:
	 *  ________________________________________________________________
	 * |  tail (partial) page |          head (partial) page           |
	 * |  (of the current cl) |      (of the previous collection)      |
	 * | PCLUSTER_FOLLOWED or |                                        |
	 * |_____PCLUSTER_HOOKED__|___________PCLUSTER_FOLLOWED____________|
	 *
	 * [  (*) the above page can be used as inplace I/O.               ]
	 */
	Z_EROFS_PCLUSTER_FOLLOWED,
};

struct z_erofs_decompress_frontend {
	struct inode *const inode;
	struct erofs_map_blocks map;
	struct z_erofs_bvec_iter biter;

	struct page *candidate_bvpage;
	struct z_erofs_pcluster *pcl, *tailpcl;
	z_erofs_next_pcluster_t owned_head;
	enum z_erofs_pclustermode mode;

	bool readahead;
	/* used for applying cache strategy on the fly */
	bool backmost;
	erofs_off_t headoffset;

	/* a pointer used to pick up inplace I/O pages */
	unsigned int icur;
};

#define DECOMPRESS_FRONTEND_INIT(__i) { \
	.inode = __i, .owned_head = Z_EROFS_PCLUSTER_TAIL, \
	.mode = Z_EROFS_PCLUSTER_FOLLOWED, .backmost = true }

static void z_erofs_bind_cache(struct z_erofs_decompress_frontend *fe,
			       enum z_erofs_cache_alloctype type,
			       struct page **pagepool)
{
	struct address_space *mc = MNGD_MAPPING(EROFS_I_SB(fe->inode));
	struct z_erofs_pcluster *pcl = fe->pcl;
	bool standalone = true;
	/*
	 * optimistic allocation without direct reclaim since inplace I/O
	 * can be used if low memory otherwise.
	 */
	gfp_t gfp = (mapping_gfp_mask(mc) & ~__GFP_DIRECT_RECLAIM) |
			__GFP_NOMEMALLOC | __GFP_NORETRY | __GFP_NOWARN;
	unsigned int i;

	if (fe->mode < Z_EROFS_PCLUSTER_FOLLOWED)
		return;

	for (i = 0; i < pcl->pclusterpages; ++i) {
		struct page *page;
		compressed_page_t t;
		struct page *newpage = NULL;

		/* the compressed page was loaded before */
		if (READ_ONCE(pcl->compressed_bvecs[i].page))
			continue;

		page = find_get_page(mc, pcl->obj.index + i);

		if (page) {
			t = tag_compressed_page_justfound(page);
		} else {
			/* I/O is needed, no possible to decompress directly */
			standalone = false;
			switch (type) {
			case TRYALLOC:
				newpage = erofs_allocpage(pagepool, gfp);
				if (!newpage)
					continue;
				set_page_private(newpage,
						 Z_EROFS_PREALLOCATED_PAGE);
				t = tag_compressed_page_justfound(newpage);
				break;
			default:        /* DONTALLOC */
				continue;
			}
		}

		if (!cmpxchg_relaxed(&pcl->compressed_bvecs[i].page, NULL,
				     tagptr_cast_ptr(t)))
			continue;

		if (page)
			put_page(page);
		else if (newpage)
			erofs_pagepool_add(pagepool, newpage);
	}

	/*
	 * don't do inplace I/O if all compressed pages are available in
	 * managed cache since it can be moved to the bypass queue instead.
	 */
	if (standalone)
		fe->mode = Z_EROFS_PCLUSTER_FOLLOWED_NOINPLACE;
}

/* called by erofs_shrinker to get rid of all compressed_pages */
int erofs_try_to_free_all_cached_pages(struct erofs_sb_info *sbi,
				       struct erofs_workgroup *grp)
{
	struct z_erofs_pcluster *const pcl =
		container_of(grp, struct z_erofs_pcluster, obj);
	int i;

	DBG_BUGON(z_erofs_is_inline_pcluster(pcl));
	/*
	 * refcount of workgroup is now freezed as 1,
	 * therefore no need to worry about available decompression users.
	 */
	for (i = 0; i < pcl->pclusterpages; ++i) {
		struct page *page = pcl->compressed_bvecs[i].page;

		if (!page)
			continue;

		/* block other users from reclaiming or migrating the page */
		if (!trylock_page(page))
			return -EBUSY;

		if (!erofs_page_is_managed(sbi, page))
			continue;

		/* barrier is implied in the following 'unlock_page' */
		WRITE_ONCE(pcl->compressed_bvecs[i].page, NULL);
		detach_page_private(page);
		unlock_page(page);
	}
	return 0;
}

int erofs_try_to_free_cached_page(struct page *page)
{
	struct z_erofs_pcluster *const pcl = (void *)page_private(page);
	int ret, i;

	if (!erofs_workgroup_try_to_freeze(&pcl->obj, 1))
		return 0;

	ret = 0;
	DBG_BUGON(z_erofs_is_inline_pcluster(pcl));
	for (i = 0; i < pcl->pclusterpages; ++i) {
		if (pcl->compressed_bvecs[i].page == page) {
			WRITE_ONCE(pcl->compressed_bvecs[i].page, NULL);
			ret = 1;
			break;
		}
	}
	erofs_workgroup_unfreeze(&pcl->obj, 1);
	if (ret)
		detach_page_private(page);
	return ret;
}

static bool z_erofs_try_inplace_io(struct z_erofs_decompress_frontend *fe,
				   struct z_erofs_bvec *bvec)
{
	struct z_erofs_pcluster *const pcl = fe->pcl;

	while (fe->icur > 0) {
		if (!cmpxchg(&pcl->compressed_bvecs[--fe->icur].page,
			     NULL, bvec->page)) {
			pcl->compressed_bvecs[fe->icur] = *bvec;
			return true;
		}
	}
	return false;
}

/* callers must be with pcluster lock held */
static int z_erofs_attach_page(struct z_erofs_decompress_frontend *fe,
			       struct z_erofs_bvec *bvec, bool exclusive)
{
	int ret;

	if (exclusive) {
		/* give priority for inplaceio to use file pages first */
		if (z_erofs_try_inplace_io(fe, bvec))
			return 0;
		/* otherwise, check if it can be used as a bvpage */
		if (fe->mode >= Z_EROFS_PCLUSTER_FOLLOWED &&
		    !fe->candidate_bvpage)
			fe->candidate_bvpage = bvec->page;
	}
	ret = z_erofs_bvec_enqueue(&fe->biter, bvec, &fe->candidate_bvpage);
	fe->pcl->vcnt += (ret >= 0);
	return ret;
}

static void z_erofs_try_to_claim_pcluster(struct z_erofs_decompress_frontend *f)
{
	struct z_erofs_pcluster *pcl = f->pcl;
	z_erofs_next_pcluster_t *owned_head = &f->owned_head;

	/* type 1, nil pcluster (this pcluster doesn't belong to any chain.) */
	if (cmpxchg(&pcl->next, Z_EROFS_PCLUSTER_NIL,
		    *owned_head) == Z_EROFS_PCLUSTER_NIL) {
		*owned_head = &pcl->next;
		/* so we can attach this pcluster to our submission chain. */
		f->mode = Z_EROFS_PCLUSTER_FOLLOWED;
		return;
	}

	/*
	 * type 2, link to the end of an existing open chain, be careful
	 * that its submission is controlled by the original attached chain.
	 */
	if (*owned_head != &pcl->next && pcl != f->tailpcl &&
	    cmpxchg(&pcl->next, Z_EROFS_PCLUSTER_TAIL,
		    *owned_head) == Z_EROFS_PCLUSTER_TAIL) {
		*owned_head = Z_EROFS_PCLUSTER_TAIL;
		f->mode = Z_EROFS_PCLUSTER_HOOKED;
		f->tailpcl = NULL;
		return;
	}
	/* type 3, it belongs to a chain, but it isn't the end of the chain */
	f->mode = Z_EROFS_PCLUSTER_INFLIGHT;
}

static int z_erofs_register_pcluster(struct z_erofs_decompress_frontend *fe)
{
	struct erofs_map_blocks *map = &fe->map;
	bool ztailpacking = map->m_flags & EROFS_MAP_META;
	struct z_erofs_pcluster *pcl;
	struct erofs_workgroup *grp;
	int err;

	if (!(map->m_flags & EROFS_MAP_ENCODED) ||
	    (!ztailpacking && !(map->m_pa >> PAGE_SHIFT))) {
		DBG_BUGON(1);
		return -EFSCORRUPTED;
	}

	/* no available pcluster, let's allocate one */
	pcl = z_erofs_alloc_pcluster(ztailpacking ? 1 :
				     map->m_plen >> PAGE_SHIFT);
	if (IS_ERR(pcl))
		return PTR_ERR(pcl);

	atomic_set(&pcl->obj.refcount, 1);
	pcl->algorithmformat = map->m_algorithmformat;
	pcl->length = 0;
	pcl->partial = true;

	/* new pclusters should be claimed as type 1, primary and followed */
	pcl->next = fe->owned_head;
	pcl->pageofs_out = map->m_la & ~PAGE_MASK;
	fe->mode = Z_EROFS_PCLUSTER_FOLLOWED;

	/*
	 * lock all primary followed works before visible to others
	 * and mutex_trylock *never* fails for a new pcluster.
	 */
	mutex_init(&pcl->lock);
	DBG_BUGON(!mutex_trylock(&pcl->lock));

	if (ztailpacking) {
		pcl->obj.index = 0;	/* which indicates ztailpacking */
		pcl->pageofs_in = erofs_blkoff(map->m_pa);
		pcl->tailpacking_size = map->m_plen;
	} else {
		pcl->obj.index = map->m_pa >> PAGE_SHIFT;

		grp = erofs_insert_workgroup(fe->inode->i_sb, &pcl->obj);
		if (IS_ERR(grp)) {
			err = PTR_ERR(grp);
			goto err_out;
		}

		if (grp != &pcl->obj) {
			fe->pcl = container_of(grp,
					struct z_erofs_pcluster, obj);
			err = -EEXIST;
			goto err_out;
		}
	}
	/* used to check tail merging loop due to corrupted images */
	if (fe->owned_head == Z_EROFS_PCLUSTER_TAIL)
		fe->tailpcl = pcl;
	fe->owned_head = &pcl->next;
	fe->pcl = pcl;
	return 0;

err_out:
	mutex_unlock(&pcl->lock);
	z_erofs_free_pcluster(pcl);
	return err;
}

static int z_erofs_collector_begin(struct z_erofs_decompress_frontend *fe)
{
	struct erofs_map_blocks *map = &fe->map;
	struct erofs_workgroup *grp = NULL;
	int ret;

	DBG_BUGON(fe->pcl);

	/* must be Z_EROFS_PCLUSTER_TAIL or pointed to previous pcluster */
	DBG_BUGON(fe->owned_head == Z_EROFS_PCLUSTER_NIL);
	DBG_BUGON(fe->owned_head == Z_EROFS_PCLUSTER_TAIL_CLOSED);

	if (!(map->m_flags & EROFS_MAP_META)) {
		grp = erofs_find_workgroup(fe->inode->i_sb,
					   map->m_pa >> PAGE_SHIFT);
	} else if ((map->m_pa & ~PAGE_MASK) + map->m_plen > PAGE_SIZE) {
		DBG_BUGON(1);
		return -EFSCORRUPTED;
	}

	if (grp) {
		fe->pcl = container_of(grp, struct z_erofs_pcluster, obj);
		ret = -EEXIST;
	} else {
		ret = z_erofs_register_pcluster(fe);
	}

	if (ret == -EEXIST) {
		mutex_lock(&fe->pcl->lock);
		/* used to check tail merging loop due to corrupted images */
		if (fe->owned_head == Z_EROFS_PCLUSTER_TAIL)
			fe->tailpcl = fe->pcl;

		z_erofs_try_to_claim_pcluster(fe);
	} else if (ret) {
		return ret;
	}
	z_erofs_bvec_iter_begin(&fe->biter, &fe->pcl->bvset,
				Z_EROFS_INLINE_BVECS, fe->pcl->vcnt);
	/* since file-backed online pages are traversed in reverse order */
	fe->icur = z_erofs_pclusterpages(fe->pcl);
	return 0;
}

/*
 * keep in mind that no referenced pclusters will be freed
 * only after a RCU grace period.
 */
static void z_erofs_rcu_callback(struct rcu_head *head)
{
	z_erofs_free_pcluster(container_of(head,
			struct z_erofs_pcluster, rcu));
}

void erofs_workgroup_free_rcu(struct erofs_workgroup *grp)
{
	struct z_erofs_pcluster *const pcl =
		container_of(grp, struct z_erofs_pcluster, obj);

	call_rcu(&pcl->rcu, z_erofs_rcu_callback);
}

static bool z_erofs_collector_end(struct z_erofs_decompress_frontend *fe)
{
	struct z_erofs_pcluster *pcl = fe->pcl;

	if (!pcl)
		return false;

	z_erofs_bvec_iter_end(&fe->biter);
	mutex_unlock(&pcl->lock);

	if (fe->candidate_bvpage) {
		DBG_BUGON(z_erofs_is_shortlived_page(fe->candidate_bvpage));
		fe->candidate_bvpage = NULL;
	}

	/*
	 * if all pending pages are added, don't hold its reference
	 * any longer if the pcluster isn't hosted by ourselves.
	 */
	if (fe->mode < Z_EROFS_PCLUSTER_FOLLOWED_NOINPLACE)
		erofs_workgroup_put(&pcl->obj);

	fe->pcl = NULL;
	return true;
}

static bool should_alloc_managed_pages(struct z_erofs_decompress_frontend *fe,
				       unsigned int cachestrategy,
				       erofs_off_t la)
{
	if (cachestrategy <= EROFS_ZIP_CACHE_DISABLED)
		return false;

	if (fe->backmost)
		return true;

	return cachestrategy >= EROFS_ZIP_CACHE_READAROUND &&
		la < fe->headoffset;
}

static int z_erofs_read_fragment(struct inode *inode, erofs_off_t pos,
				 struct page *page, unsigned int pageofs,
				 unsigned int len)
{
	struct inode *packed_inode = EROFS_I_SB(inode)->packed_inode;
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	u8 *src, *dst;
	unsigned int i, cnt;

	if (!packed_inode)
		return -EFSCORRUPTED;

	pos += EROFS_I(inode)->z_fragmentoff;
	for (i = 0; i < len; i += cnt) {
		cnt = min_t(unsigned int, len - i,
			    EROFS_BLKSIZ - erofs_blkoff(pos));
		src = erofs_bread(&buf, packed_inode,
				  erofs_blknr(pos), EROFS_KMAP);
		if (IS_ERR(src)) {
			erofs_put_metabuf(&buf);
			return PTR_ERR(src);
		}

		dst = kmap_local_page(page);
		memcpy(dst + pageofs + i, src + erofs_blkoff(pos), cnt);
		kunmap_local(dst);
		pos += cnt;
	}
	erofs_put_metabuf(&buf);
	return 0;
}

static int z_erofs_do_read_page(struct z_erofs_decompress_frontend *fe,
				struct page *page, struct page **pagepool)
{
	struct inode *const inode = fe->inode;
	struct erofs_sb_info *const sbi = EROFS_I_SB(inode);
	struct erofs_map_blocks *const map = &fe->map;
	const loff_t offset = page_offset(page);
	bool tight = true, exclusive;

	enum z_erofs_cache_alloctype cache_strategy;
	unsigned int cur, end, spiltted;
	int err = 0;

	/* register locked file pages as online pages in pack */
	z_erofs_onlinepage_init(page);

	spiltted = 0;
	end = PAGE_SIZE;
repeat:
	cur = end - 1;

	if (offset + cur < map->m_la ||
	    offset + cur >= map->m_la + map->m_llen) {
		erofs_dbg("out-of-range map @ pos %llu", offset + cur);

		if (z_erofs_collector_end(fe))
			fe->backmost = false;
		map->m_la = offset + cur;
		map->m_llen = 0;
		err = z_erofs_map_blocks_iter(inode, map, 0);
		if (err)
			goto out;
	} else {
		if (fe->pcl)
			goto hitted;
		/* didn't get a valid pcluster previously (very rare) */
	}

	if (!(map->m_flags & EROFS_MAP_MAPPED) ||
	    map->m_flags & EROFS_MAP_FRAGMENT)
		goto hitted;

	err = z_erofs_collector_begin(fe);
	if (err)
		goto out;

	if (z_erofs_is_inline_pcluster(fe->pcl)) {
		void *mp;

		mp = erofs_read_metabuf(&fe->map.buf, inode->i_sb,
					erofs_blknr(map->m_pa), EROFS_NO_KMAP);
		if (IS_ERR(mp)) {
			err = PTR_ERR(mp);
			erofs_err(inode->i_sb,
				  "failed to get inline page, err %d", err);
			goto out;
		}
		get_page(fe->map.buf.page);
		WRITE_ONCE(fe->pcl->compressed_bvecs[0].page,
			   fe->map.buf.page);
		fe->mode = Z_EROFS_PCLUSTER_FOLLOWED_NOINPLACE;
	} else {
		/* bind cache first when cached decompression is preferred */
		if (should_alloc_managed_pages(fe, sbi->opt.cache_strategy,
					       map->m_la))
			cache_strategy = TRYALLOC;
		else
			cache_strategy = DONTALLOC;

		z_erofs_bind_cache(fe, cache_strategy, pagepool);
	}
hitted:
	/*
	 * Ensure the current partial page belongs to this submit chain rather
	 * than other concurrent submit chains or the noio(bypass) chain since
	 * those chains are handled asynchronously thus the page cannot be used
	 * for inplace I/O or bvpage (should be processed in a strict order.)
	 */
	tight &= (fe->mode >= Z_EROFS_PCLUSTER_HOOKED &&
		  fe->mode != Z_EROFS_PCLUSTER_FOLLOWED_NOINPLACE);

	cur = end - min_t(unsigned int, offset + end - map->m_la, end);
	if (!(map->m_flags & EROFS_MAP_MAPPED)) {
		zero_user_segment(page, cur, end);
		goto next_part;
	}
	if (map->m_flags & EROFS_MAP_FRAGMENT) {
		unsigned int pageofs, skip, len;

		if (offset > map->m_la) {
			pageofs = 0;
			skip = offset - map->m_la;
		} else {
			pageofs = map->m_la & ~PAGE_MASK;
			skip = 0;
		}
		len = min_t(unsigned int, map->m_llen - skip, end - cur);
		err = z_erofs_read_fragment(inode, skip, page, pageofs, len);
		if (err)
			goto out;
		++spiltted;
		tight = false;
		goto next_part;
	}

	exclusive = (!cur && (!spiltted || tight));
	if (cur)
		tight &= (fe->mode >= Z_EROFS_PCLUSTER_FOLLOWED);

retry:
	err = z_erofs_attach_page(fe, &((struct z_erofs_bvec) {
					.page = page,
					.offset = offset - map->m_la,
					.end = end,
				  }), exclusive);
	/* should allocate an additional short-lived page for bvset */
	if (err == -EAGAIN && !fe->candidate_bvpage) {
		fe->candidate_bvpage = alloc_page(GFP_NOFS | __GFP_NOFAIL);
		set_page_private(fe->candidate_bvpage,
				 Z_EROFS_SHORTLIVED_PAGE);
		goto retry;
	}

	if (err) {
		DBG_BUGON(err == -EAGAIN && fe->candidate_bvpage);
		goto out;
	}

	z_erofs_onlinepage_split(page);
	/* bump up the number of spiltted parts of a page */
	++spiltted;
	if (fe->pcl->pageofs_out != (map->m_la & ~PAGE_MASK))
		fe->pcl->multibases = true;
	if (fe->pcl->length < offset + end - map->m_la) {
		fe->pcl->length = offset + end - map->m_la;
		fe->pcl->pageofs_out = map->m_la & ~PAGE_MASK;
	}
	if ((map->m_flags & EROFS_MAP_FULL_MAPPED) &&
	    !(map->m_flags & EROFS_MAP_PARTIAL_REF) &&
	    fe->pcl->length == map->m_llen)
		fe->pcl->partial = false;
next_part:
	/* shorten the remaining extent to update progress */
	map->m_llen = offset + cur - map->m_la;
	map->m_flags &= ~EROFS_MAP_FULL_MAPPED;

	end = cur;
	if (end > 0)
		goto repeat;

out:
	if (err)
		z_erofs_page_mark_eio(page);
	z_erofs_onlinepage_endio(page);

	erofs_dbg("%s, finish page: %pK spiltted: %u map->m_llen %llu",
		  __func__, page, spiltted, map->m_llen);
	return err;
}

static bool z_erofs_get_sync_decompress_policy(struct erofs_sb_info *sbi,
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
	return !page->mapping && !z_erofs_is_shortlived_page(page);
}

struct z_erofs_decompress_backend {
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
};

struct z_erofs_bvec_item {
	struct z_erofs_bvec bvec;
	struct list_head list;
};

static void z_erofs_do_decompressed_bvec(struct z_erofs_decompress_backend *be,
					 struct z_erofs_bvec *bvec)
{
	struct z_erofs_bvec_item *item;

	if (!((bvec->offset + be->pcl->pageofs_out) & ~PAGE_MASK)) {
		unsigned int pgnr;

		pgnr = (bvec->offset + be->pcl->pageofs_out) >> PAGE_SHIFT;
		DBG_BUGON(pgnr >= be->nr_pages);
		if (!be->decompressed_pages[pgnr]) {
			be->decompressed_pages[pgnr] = bvec->page;
			return;
		}
	}

	/* (cold path) one pcluster is requested multiple times */
	item = kmalloc(sizeof(*item), GFP_KERNEL | __GFP_NOFAIL);
	item->bvec = *bvec;
	list_add(&item->list, &be->decompressed_secondary_bvecs);
}

static void z_erofs_fill_other_copies(struct z_erofs_decompress_backend *be,
				      int err)
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
		if (err)
			z_erofs_page_mark_eio(bvi->bvec.page);
		z_erofs_onlinepage_endio(bvi->bvec.page);
		list_del(p);
		kfree(bvi);
	}
}

static void z_erofs_parse_out_bvecs(struct z_erofs_decompress_backend *be)
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

static int z_erofs_parse_in_bvecs(struct z_erofs_decompress_backend *be,
				  bool *overlapped)
{
	struct z_erofs_pcluster *pcl = be->pcl;
	unsigned int pclusterpages = z_erofs_pclusterpages(pcl);
	int i, err = 0;

	*overlapped = false;
	for (i = 0; i < pclusterpages; ++i) {
		struct z_erofs_bvec *bvec = &pcl->compressed_bvecs[i];
		struct page *page = bvec->page;

		/* compressed pages ought to be present before decompressing */
		if (!page) {
			DBG_BUGON(1);
			continue;
		}
		be->compressed_pages[i] = page;

		if (z_erofs_is_inline_pcluster(pcl)) {
			if (!PageUptodate(page))
				err = -EIO;
			continue;
		}

		DBG_BUGON(z_erofs_page_is_invalidated(page));
		if (!z_erofs_is_shortlived_page(page)) {
			if (erofs_page_is_managed(EROFS_SB(be->sb), page)) {
				if (!PageUptodate(page))
					err = -EIO;
				continue;
			}
			z_erofs_do_decompressed_bvec(be, bvec);
			*overlapped = true;
		}
	}

	if (err)
		return err;
	return 0;
}

static int z_erofs_decompress_pcluster(struct z_erofs_decompress_backend *be,
				       int err)
{
	struct erofs_sb_info *const sbi = EROFS_SB(be->sb);
	struct z_erofs_pcluster *pcl = be->pcl;
	unsigned int pclusterpages = z_erofs_pclusterpages(pcl);
	unsigned int i, inputsize;
	int err2;
	struct page *page;
	bool overlapped;

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
	if (err)
		goto out;

	if (z_erofs_is_inline_pcluster(pcl))
		inputsize = pcl->tailpacking_size;
	else
		inputsize = pclusterpages * PAGE_SIZE;

	err = z_erofs_decompress(&(struct z_erofs_decompress_req) {
					.sb = be->sb,
					.in = be->compressed_pages,
					.out = be->decompressed_pages,
					.pageofs_in = pcl->pageofs_in,
					.pageofs_out = pcl->pageofs_out,
					.inputsize = inputsize,
					.outputsize = pcl->length,
					.alg = pcl->algorithmformat,
					.inplace_io = overlapped,
					.partial_decoding = pcl->partial,
					.fillgaps = pcl->multibases,
				 }, be->pagepool);

out:
	/* must handle all compressed pages before actual file pages */
	if (z_erofs_is_inline_pcluster(pcl)) {
		page = pcl->compressed_bvecs[0].page;
		WRITE_ONCE(pcl->compressed_bvecs[0].page, NULL);
		put_page(page);
	} else {
		for (i = 0; i < pclusterpages; ++i) {
			page = pcl->compressed_bvecs[i].page;

			if (erofs_page_is_managed(sbi, page))
				continue;

			/* recycle all individual short-lived pages */
			(void)z_erofs_put_shortlivedpage(be->pagepool, page);
			WRITE_ONCE(pcl->compressed_bvecs[i].page, NULL);
		}
	}
	if (be->compressed_pages < be->onstack_pages ||
	    be->compressed_pages >= be->onstack_pages + Z_EROFS_ONSTACK_PAGES)
		kvfree(be->compressed_pages);
	z_erofs_fill_other_copies(be, err);

	for (i = 0; i < be->nr_pages; ++i) {
		page = be->decompressed_pages[i];
		if (!page)
			continue;

		DBG_BUGON(z_erofs_page_is_invalidated(page));

		/* recycle all individual short-lived pages */
		if (z_erofs_put_shortlivedpage(be->pagepool, page))
			continue;
		if (err)
			z_erofs_page_mark_eio(page);
		z_erofs_onlinepage_endio(page);
	}

	if (be->decompressed_pages != be->onstack_pages)
		kvfree(be->decompressed_pages);

	pcl->length = 0;
	pcl->partial = true;
	pcl->multibases = false;
	pcl->bvset.nextpage = NULL;
	pcl->vcnt = 0;

	/* pcluster lock MUST be taken before the following line */
	WRITE_ONCE(pcl->next, Z_EROFS_PCLUSTER_NIL);
	mutex_unlock(&pcl->lock);
	return err;
}

static void z_erofs_decompress_queue(const struct z_erofs_decompressqueue *io,
				     struct page **pagepool)
{
	struct z_erofs_decompress_backend be = {
		.sb = io->sb,
		.pagepool = pagepool,
		.decompressed_secondary_bvecs =
			LIST_HEAD_INIT(be.decompressed_secondary_bvecs),
	};
	z_erofs_next_pcluster_t owned = io->head;

	while (owned != Z_EROFS_PCLUSTER_TAIL_CLOSED) {
		/* impossible that 'owned' equals Z_EROFS_WORK_TPTR_TAIL */
		DBG_BUGON(owned == Z_EROFS_PCLUSTER_TAIL);
		/* impossible that 'owned' equals Z_EROFS_PCLUSTER_NIL */
		DBG_BUGON(owned == Z_EROFS_PCLUSTER_NIL);

		be.pcl = container_of(owned, struct z_erofs_pcluster, next);
		owned = READ_ONCE(be.pcl->next);

		z_erofs_decompress_pcluster(&be, io->eio ? -EIO : 0);
		erofs_workgroup_put(&be.pcl->obj);
	}
}

static void z_erofs_decompressqueue_work(struct work_struct *work)
{
	struct z_erofs_decompressqueue *bgq =
		container_of(work, struct z_erofs_decompressqueue, u.work);
	struct page *pagepool = NULL;

	DBG_BUGON(bgq->head == Z_EROFS_PCLUSTER_TAIL_CLOSED);
	z_erofs_decompress_queue(bgq, &pagepool);

	erofs_release_pages(&pagepool);
	kvfree(bgq);
}

static void z_erofs_decompress_kickoff(struct z_erofs_decompressqueue *io,
				       bool sync, int bios)
{
	struct erofs_sb_info *const sbi = EROFS_SB(io->sb);

	/* wake up the caller thread for sync decompression */
	if (sync) {
		if (!atomic_add_return(bios, &io->pending_bios))
			complete(&io->u.done);
		return;
	}

	if (atomic_add_return(bios, &io->pending_bios))
		return;
	/* Use workqueue and sync decompression for atomic contexts only */
	if (in_atomic() || irqs_disabled()) {
		queue_work(z_erofs_workqueue, &io->u.work);
		/* enable sync decompression for readahead */
		if (sbi->opt.sync_decompress == EROFS_SYNC_DECOMPRESS_AUTO)
			sbi->opt.sync_decompress = EROFS_SYNC_DECOMPRESS_FORCE_ON;
		return;
	}
	z_erofs_decompressqueue_work(&io->u.work);
}

static struct page *pickup_page_for_submission(struct z_erofs_pcluster *pcl,
					       unsigned int nr,
					       struct page **pagepool,
					       struct address_space *mc)
{
	const pgoff_t index = pcl->obj.index;
	gfp_t gfp = mapping_gfp_mask(mc);
	bool tocache = false;

	struct address_space *mapping;
	struct page *oldpage, *page;

	compressed_page_t t;
	int justfound;

repeat:
	page = READ_ONCE(pcl->compressed_bvecs[nr].page);
	oldpage = page;

	if (!page)
		goto out_allocpage;

	/* process the target tagged pointer */
	t = tagptr_init(compressed_page_t, page);
	justfound = tagptr_unfold_tags(t);
	page = tagptr_unfold_ptr(t);

	/*
	 * preallocated cached pages, which is used to avoid direct reclaim
	 * otherwise, it will go inplace I/O path instead.
	 */
	if (page->private == Z_EROFS_PREALLOCATED_PAGE) {
		WRITE_ONCE(pcl->compressed_bvecs[nr].page, page);
		set_page_private(page, 0);
		tocache = true;
		goto out_tocache;
	}
	mapping = READ_ONCE(page->mapping);

	/*
	 * file-backed online pages in plcuster are all locked steady,
	 * therefore it is impossible for `mapping' to be NULL.
	 */
	if (mapping && mapping != mc)
		/* ought to be unmanaged pages */
		goto out;

	/* directly return for shortlived page as well */
	if (z_erofs_is_shortlived_page(page))
		goto out;

	lock_page(page);

	/* only true if page reclaim goes wrong, should never happen */
	DBG_BUGON(justfound && PagePrivate(page));

	/* the page is still in manage cache */
	if (page->mapping == mc) {
		WRITE_ONCE(pcl->compressed_bvecs[nr].page, page);

		if (!PagePrivate(page)) {
			/*
			 * impossible to be !PagePrivate(page) for
			 * the current restriction as well if
			 * the page is already in compressed_bvecs[].
			 */
			DBG_BUGON(!justfound);

			justfound = 0;
			set_page_private(page, (unsigned long)pcl);
			SetPagePrivate(page);
		}

		/* no need to submit io if it is already up-to-date */
		if (PageUptodate(page)) {
			unlock_page(page);
			page = NULL;
		}
		goto out;
	}

	/*
	 * the managed page has been truncated, it's unsafe to
	 * reuse this one, let's allocate a new cache-managed page.
	 */
	DBG_BUGON(page->mapping);
	DBG_BUGON(!justfound);

	tocache = true;
	unlock_page(page);
	put_page(page);
out_allocpage:
	page = erofs_allocpage(pagepool, gfp | __GFP_NOFAIL);
	if (oldpage != cmpxchg(&pcl->compressed_bvecs[nr].page,
			       oldpage, page)) {
		erofs_pagepool_add(pagepool, page);
		cond_resched();
		goto repeat;
	}
out_tocache:
	if (!tocache || add_to_page_cache_lru(page, mc, index + nr, gfp)) {
		/* turn into temporary page if fails (1 ref) */
		set_page_private(page, Z_EROFS_SHORTLIVED_PAGE);
		goto out;
	}
	attach_page_private(page, pcl);
	/* drop a refcount added by allocpage (then we have 2 refs here) */
	put_page(page);

out:	/* the only exit (for tracing and debugging) */
	return page;
}

static struct z_erofs_decompressqueue *
jobqueue_init(struct super_block *sb,
	      struct z_erofs_decompressqueue *fgq, bool *fg)
{
	struct z_erofs_decompressqueue *q;

	if (fg && !*fg) {
		q = kvzalloc(sizeof(*q), GFP_KERNEL | __GFP_NOWARN);
		if (!q) {
			*fg = true;
			goto fg_out;
		}
		INIT_WORK(&q->u.work, z_erofs_decompressqueue_work);
	} else {
fg_out:
		q = fgq;
		init_completion(&fgq->u.done);
		atomic_set(&fgq->pending_bios, 0);
		q->eio = false;
	}
	q->sb = sb;
	q->head = Z_EROFS_PCLUSTER_TAIL_CLOSED;
	return q;
}

/* define decompression jobqueue types */
enum {
	JQ_BYPASS,
	JQ_SUBMIT,
	NR_JOBQUEUES,
};

static void *jobqueueset_init(struct super_block *sb,
			      struct z_erofs_decompressqueue *q[],
			      struct z_erofs_decompressqueue *fgq, bool *fg)
{
	/*
	 * if managed cache is enabled, bypass jobqueue is needed,
	 * no need to read from device for all pclusters in this queue.
	 */
	q[JQ_BYPASS] = jobqueue_init(sb, fgq + JQ_BYPASS, NULL);
	q[JQ_SUBMIT] = jobqueue_init(sb, fgq + JQ_SUBMIT, fg);

	return tagptr_cast_ptr(tagptr_fold(tagptr1_t, q[JQ_SUBMIT], *fg));
}

static void move_to_bypass_jobqueue(struct z_erofs_pcluster *pcl,
				    z_erofs_next_pcluster_t qtail[],
				    z_erofs_next_pcluster_t owned_head)
{
	z_erofs_next_pcluster_t *const submit_qtail = qtail[JQ_SUBMIT];
	z_erofs_next_pcluster_t *const bypass_qtail = qtail[JQ_BYPASS];

	DBG_BUGON(owned_head == Z_EROFS_PCLUSTER_TAIL_CLOSED);
	if (owned_head == Z_EROFS_PCLUSTER_TAIL)
		owned_head = Z_EROFS_PCLUSTER_TAIL_CLOSED;

	WRITE_ONCE(pcl->next, Z_EROFS_PCLUSTER_TAIL_CLOSED);

	WRITE_ONCE(*submit_qtail, owned_head);
	WRITE_ONCE(*bypass_qtail, &pcl->next);

	qtail[JQ_BYPASS] = &pcl->next;
}

static void z_erofs_decompressqueue_endio(struct bio *bio)
{
	tagptr1_t t = tagptr_init(tagptr1_t, bio->bi_private);
	struct z_erofs_decompressqueue *q = tagptr_unfold_ptr(t);
	blk_status_t err = bio->bi_status;
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct page *page = bvec->bv_page;

		DBG_BUGON(PageUptodate(page));
		DBG_BUGON(z_erofs_page_is_invalidated(page));

		if (erofs_page_is_managed(EROFS_SB(q->sb), page)) {
			if (!err)
				SetPageUptodate(page);
			unlock_page(page);
		}
	}
	if (err)
		q->eio = true;
	z_erofs_decompress_kickoff(q, tagptr_unfold_tags(t), -1);
	bio_put(bio);
}

static void z_erofs_submit_queue(struct z_erofs_decompress_frontend *f,
				 struct page **pagepool,
				 struct z_erofs_decompressqueue *fgq,
				 bool *force_fg)
{
	struct super_block *sb = f->inode->i_sb;
	struct address_space *mc = MNGD_MAPPING(EROFS_SB(sb));
	z_erofs_next_pcluster_t qtail[NR_JOBQUEUES];
	struct z_erofs_decompressqueue *q[NR_JOBQUEUES];
	void *bi_private;
	z_erofs_next_pcluster_t owned_head = f->owned_head;
	/* bio is NULL initially, so no need to initialize last_{index,bdev} */
	pgoff_t last_index;
	struct block_device *last_bdev;
	unsigned int nr_bios = 0;
	struct bio *bio = NULL;
	unsigned long pflags;
	int memstall = 0;

	bi_private = jobqueueset_init(sb, q, fgq, force_fg);
	qtail[JQ_BYPASS] = &q[JQ_BYPASS]->head;
	qtail[JQ_SUBMIT] = &q[JQ_SUBMIT]->head;

	/* by default, all need io submission */
	q[JQ_SUBMIT]->head = owned_head;

	do {
		struct erofs_map_dev mdev;
		struct z_erofs_pcluster *pcl;
		pgoff_t cur, end;
		unsigned int i = 0;
		bool bypass = true;

		/* no possible 'owned_head' equals the following */
		DBG_BUGON(owned_head == Z_EROFS_PCLUSTER_TAIL_CLOSED);
		DBG_BUGON(owned_head == Z_EROFS_PCLUSTER_NIL);

		pcl = container_of(owned_head, struct z_erofs_pcluster, next);

		/* close the main owned chain at first */
		owned_head = cmpxchg(&pcl->next, Z_EROFS_PCLUSTER_TAIL,
				     Z_EROFS_PCLUSTER_TAIL_CLOSED);
		if (z_erofs_is_inline_pcluster(pcl)) {
			move_to_bypass_jobqueue(pcl, qtail, owned_head);
			continue;
		}

		/* no device id here, thus it will always succeed */
		mdev = (struct erofs_map_dev) {
			.m_pa = blknr_to_addr(pcl->obj.index),
		};
		(void)erofs_map_dev(sb, &mdev);

		cur = erofs_blknr(mdev.m_pa);
		end = cur + pcl->pclusterpages;

		do {
			struct page *page;

			page = pickup_page_for_submission(pcl, i++, pagepool,
							  mc);
			if (!page)
				continue;

			if (bio && (cur != last_index + 1 ||
				    last_bdev != mdev.m_bdev)) {
submit_bio_retry:
				submit_bio(bio);
				if (memstall) {
					psi_memstall_leave(&pflags);
					memstall = 0;
				}
				bio = NULL;
			}

			if (unlikely(PageWorkingset(page)) && !memstall) {
				psi_memstall_enter(&pflags);
				memstall = 1;
			}

			if (!bio) {
				bio = bio_alloc(mdev.m_bdev, BIO_MAX_VECS,
						REQ_OP_READ, GFP_NOIO);
				bio->bi_end_io = z_erofs_decompressqueue_endio;

				last_bdev = mdev.m_bdev;
				bio->bi_iter.bi_sector = (sector_t)cur <<
					LOG_SECTORS_PER_BLOCK;
				bio->bi_private = bi_private;
				if (f->readahead)
					bio->bi_opf |= REQ_RAHEAD;
				++nr_bios;
			}

			if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE)
				goto submit_bio_retry;

			last_index = cur;
			bypass = false;
		} while (++cur < end);

		if (!bypass)
			qtail[JQ_SUBMIT] = &pcl->next;
		else
			move_to_bypass_jobqueue(pcl, qtail, owned_head);
	} while (owned_head != Z_EROFS_PCLUSTER_TAIL);

	if (bio) {
		submit_bio(bio);
		if (memstall)
			psi_memstall_leave(&pflags);
	}

	/*
	 * although background is preferred, no one is pending for submission.
	 * don't issue workqueue for decompression but drop it directly instead.
	 */
	if (!*force_fg && !nr_bios) {
		kvfree(q[JQ_SUBMIT]);
		return;
	}
	z_erofs_decompress_kickoff(q[JQ_SUBMIT], *force_fg, nr_bios);
}

static void z_erofs_runqueue(struct z_erofs_decompress_frontend *f,
			     struct page **pagepool, bool force_fg)
{
	struct z_erofs_decompressqueue io[NR_JOBQUEUES];

	if (f->owned_head == Z_EROFS_PCLUSTER_TAIL)
		return;
	z_erofs_submit_queue(f, pagepool, io, &force_fg);

	/* handle bypass queue (no i/o pclusters) immediately */
	z_erofs_decompress_queue(&io[JQ_BYPASS], pagepool);

	if (!force_fg)
		return;

	/* wait until all bios are completed */
	wait_for_completion_io(&io[JQ_SUBMIT].u.done);

	/* handle synchronous decompress queue in the caller context */
	z_erofs_decompress_queue(&io[JQ_SUBMIT], pagepool);
}

/*
 * Since partial uptodate is still unimplemented for now, we have to use
 * approximate readmore strategies as a start.
 */
static void z_erofs_pcluster_readmore(struct z_erofs_decompress_frontend *f,
				      struct readahead_control *rac,
				      erofs_off_t end,
				      struct page **pagepool,
				      bool backmost)
{
	struct inode *inode = f->inode;
	struct erofs_map_blocks *map = &f->map;
	erofs_off_t cur;
	int err;

	if (backmost) {
		map->m_la = end;
		err = z_erofs_map_blocks_iter(inode, map,
					      EROFS_GET_BLOCKS_READMORE);
		if (err)
			return;

		/* expend ra for the trailing edge if readahead */
		if (rac) {
			loff_t newstart = readahead_pos(rac);

			cur = round_up(map->m_la + map->m_llen, PAGE_SIZE);
			readahead_expand(rac, newstart, cur - newstart);
			return;
		}
		end = round_up(end, PAGE_SIZE);
	} else {
		end = round_up(map->m_la, PAGE_SIZE);

		if (!map->m_llen)
			return;
	}

	cur = map->m_la + map->m_llen - 1;
	while (cur >= end) {
		pgoff_t index = cur >> PAGE_SHIFT;
		struct page *page;

		page = erofs_grab_cache_page_nowait(inode->i_mapping, index);
		if (page) {
			if (PageUptodate(page)) {
				unlock_page(page);
			} else {
				err = z_erofs_do_read_page(f, page, pagepool);
				if (err)
					erofs_err(inode->i_sb,
						  "readmore error at page %lu @ nid %llu",
						  index, EROFS_I(inode)->nid);
			}
			put_page(page);
		}

		if (cur < PAGE_SIZE)
			break;
		cur = (index << PAGE_SHIFT) - 1;
	}
}

static int z_erofs_read_folio(struct file *file, struct folio *folio)
{
	struct page *page = &folio->page;
	struct inode *const inode = page->mapping->host;
	struct erofs_sb_info *const sbi = EROFS_I_SB(inode);
	struct z_erofs_decompress_frontend f = DECOMPRESS_FRONTEND_INIT(inode);
	struct page *pagepool = NULL;
	int err;

	trace_erofs_readpage(page, false);
	f.headoffset = (erofs_off_t)page->index << PAGE_SHIFT;

	z_erofs_pcluster_readmore(&f, NULL, f.headoffset + PAGE_SIZE - 1,
				  &pagepool, true);
	err = z_erofs_do_read_page(&f, page, &pagepool);
	z_erofs_pcluster_readmore(&f, NULL, 0, &pagepool, false);

	(void)z_erofs_collector_end(&f);

	/* if some compressed cluster ready, need submit them anyway */
	z_erofs_runqueue(&f, &pagepool,
			 z_erofs_get_sync_decompress_policy(sbi, 0));

	if (err)
		erofs_err(inode->i_sb, "failed to read, err [%d]", err);

	erofs_put_metabuf(&f.map.buf);
	erofs_release_pages(&pagepool);
	return err;
}

static void z_erofs_readahead(struct readahead_control *rac)
{
	struct inode *const inode = rac->mapping->host;
	struct erofs_sb_info *const sbi = EROFS_I_SB(inode);
	struct z_erofs_decompress_frontend f = DECOMPRESS_FRONTEND_INIT(inode);
	struct page *pagepool = NULL, *head = NULL, *page;
	unsigned int nr_pages;

	f.readahead = true;
	f.headoffset = readahead_pos(rac);

	z_erofs_pcluster_readmore(&f, rac, f.headoffset +
				  readahead_length(rac) - 1, &pagepool, true);
	nr_pages = readahead_count(rac);
	trace_erofs_readpages(inode, readahead_index(rac), nr_pages, false);

	while ((page = readahead_page(rac))) {
		set_page_private(page, (unsigned long)head);
		head = page;
	}

	while (head) {
		struct page *page = head;
		int err;

		/* traversal in reverse order */
		head = (void *)page_private(page);

		err = z_erofs_do_read_page(&f, page, &pagepool);
		if (err)
			erofs_err(inode->i_sb,
				  "readahead error at page %lu @ nid %llu",
				  page->index, EROFS_I(inode)->nid);
		put_page(page);
	}
	z_erofs_pcluster_readmore(&f, rac, 0, &pagepool, false);
	(void)z_erofs_collector_end(&f);

	z_erofs_runqueue(&f, &pagepool,
			 z_erofs_get_sync_decompress_policy(sbi, nr_pages));
	erofs_put_metabuf(&f.map.buf);
	erofs_release_pages(&pagepool);
}

const struct address_space_operations z_erofs_aops = {
	.read_folio = z_erofs_read_folio,
	.readahead = z_erofs_readahead,
};

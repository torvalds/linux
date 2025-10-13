// SPDX-License-Identifier: GPL-2.0
/*
 * Common Code for Data Access Monitoring
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/migrate.h>
#include <linux/mmu_notifier.h>
#include <linux/page_idle.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include "../internal.h"
#include "ops-common.h"

/*
 * Get an online page for a pfn if it's in the LRU list.  Otherwise, returns
 * NULL.
 *
 * The body of this function is stolen from the 'page_idle_get_folio()'.  We
 * steal rather than reuse it because the code is quite simple.
 */
struct folio *damon_get_folio(unsigned long pfn)
{
	struct page *page = pfn_to_online_page(pfn);
	struct folio *folio;

	if (!page)
		return NULL;

	folio = page_folio(page);
	if (!folio_test_lru(folio) || !folio_try_get(folio))
		return NULL;
	if (unlikely(page_folio(page) != folio || !folio_test_lru(folio))) {
		folio_put(folio);
		folio = NULL;
	}
	return folio;
}

void damon_ptep_mkold(pte_t *pte, struct vm_area_struct *vma, unsigned long addr)
{
	pte_t pteval = ptep_get(pte);
	struct folio *folio;
	bool young = false;
	unsigned long pfn;

	if (likely(pte_present(pteval)))
		pfn = pte_pfn(pteval);
	else
		pfn = swp_offset_pfn(pte_to_swp_entry(pteval));

	folio = damon_get_folio(pfn);
	if (!folio)
		return;

	/*
	 * PFN swap PTEs, such as device-exclusive ones, that actually map pages
	 * are "old" from a CPU perspective. The MMU notifier takes care of any
	 * device aspects.
	 */
	if (likely(pte_present(pteval)))
		young |= ptep_test_and_clear_young(vma, addr, pte);
	young |= mmu_notifier_clear_young(vma->vm_mm, addr, addr + PAGE_SIZE);
	if (young)
		folio_set_young(folio);

	folio_set_idle(folio);
	folio_put(folio);
}

void damon_pmdp_mkold(pmd_t *pmd, struct vm_area_struct *vma, unsigned long addr)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	struct folio *folio = damon_get_folio(pmd_pfn(pmdp_get(pmd)));

	if (!folio)
		return;

	if (pmdp_clear_young_notify(vma, addr, pmd))
		folio_set_young(folio);

	folio_set_idle(folio);
	folio_put(folio);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
}

#define DAMON_MAX_SUBSCORE	(100)
#define DAMON_MAX_AGE_IN_LOG	(32)

int damon_hot_score(struct damon_ctx *c, struct damon_region *r,
			struct damos *s)
{
	int freq_subscore;
	unsigned int age_in_sec;
	int age_in_log, age_subscore;
	unsigned int freq_weight = s->quota.weight_nr_accesses;
	unsigned int age_weight = s->quota.weight_age;
	int hotness;

	freq_subscore = r->nr_accesses * DAMON_MAX_SUBSCORE /
		damon_max_nr_accesses(&c->attrs);

	age_in_sec = (unsigned long)r->age * c->attrs.aggr_interval / 1000000;
	for (age_in_log = 0; age_in_log < DAMON_MAX_AGE_IN_LOG && age_in_sec;
			age_in_log++, age_in_sec >>= 1)
		;

	/* If frequency is 0, higher age means it's colder */
	if (freq_subscore == 0)
		age_in_log *= -1;

	/*
	 * Now age_in_log is in [-DAMON_MAX_AGE_IN_LOG, DAMON_MAX_AGE_IN_LOG].
	 * Scale it to be in [0, 100] and set it as age subscore.
	 */
	age_in_log += DAMON_MAX_AGE_IN_LOG;
	age_subscore = age_in_log * DAMON_MAX_SUBSCORE /
		DAMON_MAX_AGE_IN_LOG / 2;

	hotness = (freq_weight * freq_subscore + age_weight * age_subscore);
	if (freq_weight + age_weight)
		hotness /= freq_weight + age_weight;
	/*
	 * Transform it to fit in [0, DAMOS_MAX_SCORE]
	 */
	hotness = hotness * DAMOS_MAX_SCORE / DAMON_MAX_SUBSCORE;

	return hotness;
}

int damon_cold_score(struct damon_ctx *c, struct damon_region *r,
			struct damos *s)
{
	int hotness = damon_hot_score(c, r, s);

	/* Return coldness of the region */
	return DAMOS_MAX_SCORE - hotness;
}

static bool damon_folio_mkold_one(struct folio *folio,
		struct vm_area_struct *vma, unsigned long addr, void *arg)
{
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, addr, 0);

	while (page_vma_mapped_walk(&pvmw)) {
		addr = pvmw.address;
		if (pvmw.pte)
			damon_ptep_mkold(pvmw.pte, vma, addr);
		else
			damon_pmdp_mkold(pvmw.pmd, vma, addr);
	}
	return true;
}

void damon_folio_mkold(struct folio *folio)
{
	struct rmap_walk_control rwc = {
		.rmap_one = damon_folio_mkold_one,
		.anon_lock = folio_lock_anon_vma_read,
	};
	bool need_lock;

	if (!folio_mapped(folio) || !folio_raw_mapping(folio)) {
		folio_set_idle(folio);
		return;
	}

	need_lock = !folio_test_anon(folio) || folio_test_ksm(folio);
	if (need_lock && !folio_trylock(folio))
		return;

	rmap_walk(folio, &rwc);

	if (need_lock)
		folio_unlock(folio);

}

static bool damon_folio_young_one(struct folio *folio,
		struct vm_area_struct *vma, unsigned long addr, void *arg)
{
	bool *accessed = arg;
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, addr, 0);
	pte_t pte;

	*accessed = false;
	while (page_vma_mapped_walk(&pvmw)) {
		addr = pvmw.address;
		if (pvmw.pte) {
			pte = ptep_get(pvmw.pte);

			/*
			 * PFN swap PTEs, such as device-exclusive ones, that
			 * actually map pages are "old" from a CPU perspective.
			 * The MMU notifier takes care of any device aspects.
			 */
			*accessed = (pte_present(pte) && pte_young(pte)) ||
				!folio_test_idle(folio) ||
				mmu_notifier_test_young(vma->vm_mm, addr);
		} else {
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			*accessed = pmd_young(pmdp_get(pvmw.pmd)) ||
				!folio_test_idle(folio) ||
				mmu_notifier_test_young(vma->vm_mm, addr);
#else
			WARN_ON_ONCE(1);
#endif	/* CONFIG_TRANSPARENT_HUGEPAGE */
		}
		if (*accessed) {
			page_vma_mapped_walk_done(&pvmw);
			break;
		}
	}

	/* If accessed, stop walking */
	return *accessed == false;
}

bool damon_folio_young(struct folio *folio)
{
	bool accessed = false;
	struct rmap_walk_control rwc = {
		.arg = &accessed,
		.rmap_one = damon_folio_young_one,
		.anon_lock = folio_lock_anon_vma_read,
	};
	bool need_lock;

	if (!folio_mapped(folio) || !folio_raw_mapping(folio)) {
		if (folio_test_idle(folio))
			return false;
		else
			return true;
	}

	need_lock = !folio_test_anon(folio) || folio_test_ksm(folio);
	if (need_lock && !folio_trylock(folio))
		return false;

	rmap_walk(folio, &rwc);

	if (need_lock)
		folio_unlock(folio);

	return accessed;
}

bool damos_folio_filter_match(struct damos_filter *filter, struct folio *folio)
{
	bool matched = false;
	struct mem_cgroup *memcg;
	size_t folio_sz;

	switch (filter->type) {
	case DAMOS_FILTER_TYPE_ANON:
		matched = folio_test_anon(folio);
		break;
	case DAMOS_FILTER_TYPE_ACTIVE:
		matched = folio_test_active(folio);
		break;
	case DAMOS_FILTER_TYPE_MEMCG:
		rcu_read_lock();
		memcg = folio_memcg_check(folio);
		if (!memcg)
			matched = false;
		else
			matched = filter->memcg_id == mem_cgroup_id(memcg);
		rcu_read_unlock();
		break;
	case DAMOS_FILTER_TYPE_YOUNG:
		matched = damon_folio_young(folio);
		if (matched)
			damon_folio_mkold(folio);
		break;
	case DAMOS_FILTER_TYPE_HUGEPAGE_SIZE:
		folio_sz = folio_size(folio);
		matched = filter->sz_range.min <= folio_sz &&
			  folio_sz <= filter->sz_range.max;
		break;
	case DAMOS_FILTER_TYPE_UNMAPPED:
		matched = !folio_mapped(folio) || !folio_raw_mapping(folio);
		break;
	default:
		break;
	}

	return matched == filter->matching;
}

static unsigned int __damon_migrate_folio_list(
		struct list_head *migrate_folios, struct pglist_data *pgdat,
		int target_nid)
{
	unsigned int nr_succeeded = 0;
	struct migration_target_control mtc = {
		/*
		 * Allocate from 'node', or fail quickly and quietly.
		 * When this happens, 'page' will likely just be discarded
		 * instead of migrated.
		 */
		.gfp_mask = (GFP_HIGHUSER_MOVABLE & ~__GFP_RECLAIM) |
			__GFP_NOMEMALLOC | GFP_NOWAIT,
		.nid = target_nid,
	};

	if (pgdat->node_id == target_nid || target_nid == NUMA_NO_NODE)
		return 0;

	if (list_empty(migrate_folios))
		return 0;

	/* Migration ignores all cpuset and mempolicy settings */
	migrate_pages(migrate_folios, alloc_migration_target, NULL,
		      (unsigned long)&mtc, MIGRATE_ASYNC, MR_DAMON,
		      &nr_succeeded);

	return nr_succeeded;
}

static unsigned int damon_migrate_folio_list(struct list_head *folio_list,
						struct pglist_data *pgdat,
						int target_nid)
{
	unsigned int nr_migrated = 0;
	struct folio *folio;
	LIST_HEAD(ret_folios);
	LIST_HEAD(migrate_folios);

	while (!list_empty(folio_list)) {
		struct folio *folio;

		cond_resched();

		folio = lru_to_folio(folio_list);
		list_del(&folio->lru);

		if (!folio_trylock(folio))
			goto keep;

		/* Relocate its contents to another node. */
		list_add(&folio->lru, &migrate_folios);
		folio_unlock(folio);
		continue;
keep:
		list_add(&folio->lru, &ret_folios);
	}
	/* 'folio_list' is always empty here */

	/* Migrate folios selected for migration */
	nr_migrated += __damon_migrate_folio_list(
			&migrate_folios, pgdat, target_nid);
	/*
	 * Folios that could not be migrated are still in @migrate_folios.  Add
	 * those back on @folio_list
	 */
	if (!list_empty(&migrate_folios))
		list_splice_init(&migrate_folios, folio_list);

	try_to_unmap_flush();

	list_splice(&ret_folios, folio_list);

	while (!list_empty(folio_list)) {
		folio = lru_to_folio(folio_list);
		list_del(&folio->lru);
		folio_putback_lru(folio);
	}

	return nr_migrated;
}

unsigned long damon_migrate_pages(struct list_head *folio_list, int target_nid)
{
	int nid;
	unsigned long nr_migrated = 0;
	LIST_HEAD(node_folio_list);
	unsigned int noreclaim_flag;

	if (list_empty(folio_list))
		return nr_migrated;

	if (target_nid < 0 || target_nid >= MAX_NUMNODES ||
			!node_state(target_nid, N_MEMORY))
		return nr_migrated;

	noreclaim_flag = memalloc_noreclaim_save();

	nid = folio_nid(lru_to_folio(folio_list));
	do {
		struct folio *folio = lru_to_folio(folio_list);

		if (nid == folio_nid(folio)) {
			list_move(&folio->lru, &node_folio_list);
			continue;
		}

		nr_migrated += damon_migrate_folio_list(&node_folio_list,
							   NODE_DATA(nid),
							   target_nid);
		nid = folio_nid(lru_to_folio(folio_list));
	} while (!list_empty(folio_list));

	nr_migrated += damon_migrate_folio_list(&node_folio_list,
						   NODE_DATA(nid),
						   target_nid);

	memalloc_noreclaim_restore(noreclaim_flag);

	return nr_migrated;
}

bool damos_ops_has_filter(struct damos *s)
{
	struct damos_filter *f;

	damos_for_each_ops_filter(f, s)
		return true;
	return false;
}

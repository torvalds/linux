// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON Primitives for The Physical Address Space
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#define pr_fmt(fmt) "damon-pa: " fmt

#include <linux/mmu_notifier.h>
#include <linux/page_idle.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>

#include "../internal.h"
#include "ops-common.h"

static bool __damon_pa_mkold(struct folio *folio, struct vm_area_struct *vma,
		unsigned long addr, void *arg)
{
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, addr, 0);

	while (page_vma_mapped_walk(&pvmw)) {
		addr = pvmw.address;
		if (pvmw.pte)
			damon_ptep_mkold(pvmw.pte, vma->vm_mm, addr);
		else
			damon_pmdp_mkold(pvmw.pmd, vma->vm_mm, addr);
	}
	return true;
}

static void damon_pa_mkold(unsigned long paddr)
{
	struct folio *folio;
	struct page *page = damon_get_page(PHYS_PFN(paddr));
	struct rmap_walk_control rwc = {
		.rmap_one = __damon_pa_mkold,
		.anon_lock = folio_lock_anon_vma_read,
	};
	bool need_lock;

	if (!page)
		return;
	folio = page_folio(page);

	if (!folio_mapped(folio) || !folio_raw_mapping(folio)) {
		folio_set_idle(folio);
		goto out;
	}

	need_lock = !folio_test_anon(folio) || folio_test_ksm(folio);
	if (need_lock && !folio_trylock(folio))
		goto out;

	rmap_walk(folio, &rwc);

	if (need_lock)
		folio_unlock(folio);

out:
	folio_put(folio);
}

static void __damon_pa_prepare_access_check(struct damon_ctx *ctx,
					    struct damon_region *r)
{
	r->sampling_addr = damon_rand(r->ar.start, r->ar.end);

	damon_pa_mkold(r->sampling_addr);
}

static void damon_pa_prepare_access_checks(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t)
			__damon_pa_prepare_access_check(ctx, r);
	}
}

struct damon_pa_access_chk_result {
	unsigned long page_sz;
	bool accessed;
};

static bool __damon_pa_young(struct folio *folio, struct vm_area_struct *vma,
		unsigned long addr, void *arg)
{
	struct damon_pa_access_chk_result *result = arg;
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, addr, 0);

	result->accessed = false;
	result->page_sz = PAGE_SIZE;
	while (page_vma_mapped_walk(&pvmw)) {
		addr = pvmw.address;
		if (pvmw.pte) {
			result->accessed = pte_young(*pvmw.pte) ||
				!folio_test_idle(folio) ||
				mmu_notifier_test_young(vma->vm_mm, addr);
		} else {
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			result->accessed = pmd_young(*pvmw.pmd) ||
				!folio_test_idle(folio) ||
				mmu_notifier_test_young(vma->vm_mm, addr);
			result->page_sz = HPAGE_PMD_SIZE;
#else
			WARN_ON_ONCE(1);
#endif	/* CONFIG_TRANSPARENT_HUGEPAGE */
		}
		if (result->accessed) {
			page_vma_mapped_walk_done(&pvmw);
			break;
		}
	}

	/* If accessed, stop walking */
	return !result->accessed;
}

static bool damon_pa_young(unsigned long paddr, unsigned long *page_sz)
{
	struct folio *folio;
	struct page *page = damon_get_page(PHYS_PFN(paddr));
	struct damon_pa_access_chk_result result = {
		.page_sz = PAGE_SIZE,
		.accessed = false,
	};
	struct rmap_walk_control rwc = {
		.arg = &result,
		.rmap_one = __damon_pa_young,
		.anon_lock = folio_lock_anon_vma_read,
	};
	bool need_lock;

	if (!page)
		return false;
	folio = page_folio(page);

	if (!folio_mapped(folio) || !folio_raw_mapping(folio)) {
		if (folio_test_idle(folio))
			result.accessed = false;
		else
			result.accessed = true;
		folio_put(folio);
		goto out;
	}

	need_lock = !folio_test_anon(folio) || folio_test_ksm(folio);
	if (need_lock && !folio_trylock(folio)) {
		folio_put(folio);
		return false;
	}

	rmap_walk(folio, &rwc);

	if (need_lock)
		folio_unlock(folio);
	folio_put(folio);

out:
	*page_sz = result.page_sz;
	return result.accessed;
}

static void __damon_pa_check_access(struct damon_ctx *ctx,
				    struct damon_region *r)
{
	static unsigned long last_addr;
	static unsigned long last_page_sz = PAGE_SIZE;
	static bool last_accessed;

	/* If the region is in the last checked page, reuse the result */
	if (ALIGN_DOWN(last_addr, last_page_sz) ==
				ALIGN_DOWN(r->sampling_addr, last_page_sz)) {
		if (last_accessed)
			r->nr_accesses++;
		return;
	}

	last_accessed = damon_pa_young(r->sampling_addr, &last_page_sz);
	if (last_accessed)
		r->nr_accesses++;

	last_addr = r->sampling_addr;
}

static unsigned int damon_pa_check_accesses(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned int max_nr_accesses = 0;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t) {
			__damon_pa_check_access(ctx, r);
			max_nr_accesses = max(r->nr_accesses, max_nr_accesses);
		}
	}

	return max_nr_accesses;
}

static unsigned long damon_pa_apply_scheme(struct damon_ctx *ctx,
		struct damon_target *t, struct damon_region *r,
		struct damos *scheme)
{
	unsigned long addr, applied;
	LIST_HEAD(page_list);

	if (scheme->action != DAMOS_PAGEOUT)
		return 0;

	for (addr = r->ar.start; addr < r->ar.end; addr += PAGE_SIZE) {
		struct page *page = damon_get_page(PHYS_PFN(addr));

		if (!page)
			continue;

		ClearPageReferenced(page);
		test_and_clear_page_young(page);
		if (isolate_lru_page(page)) {
			put_page(page);
			continue;
		}
		if (PageUnevictable(page)) {
			putback_lru_page(page);
		} else {
			list_add(&page->lru, &page_list);
			put_page(page);
		}
	}
	applied = reclaim_pages(&page_list);
	cond_resched();
	return applied * PAGE_SIZE;
}

static int damon_pa_scheme_score(struct damon_ctx *context,
		struct damon_target *t, struct damon_region *r,
		struct damos *scheme)
{
	switch (scheme->action) {
	case DAMOS_PAGEOUT:
		return damon_pageout_score(context, r, scheme);
	default:
		break;
	}

	return DAMOS_MAX_SCORE;
}

static int __init damon_pa_initcall(void)
{
	struct damon_operations ops = {
		.id = DAMON_OPS_PADDR,
		.init = NULL,
		.update = NULL,
		.prepare_access_checks = damon_pa_prepare_access_checks,
		.check_accesses = damon_pa_check_accesses,
		.reset_aggregated = NULL,
		.target_valid = NULL,
		.cleanup = NULL,
		.apply_scheme = damon_pa_apply_scheme,
		.get_scheme_score = damon_pa_scheme_score,
	};

	return damon_register_ops(&ops);
};

subsys_initcall(damon_pa_initcall);

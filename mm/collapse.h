/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_COLLAPSE_H
#define __MM_COLLAPSE_H

#include <linux/mm.h>
#include <linux/nodemask.h>
#include <linux/pgtable.h>
#include <linux/types.h>

#define COLLAPSE_MAX_PTES_LIMIT		(HPAGE_PMD_NR - 1)
#define COLLAPSE_MIN_MTHP_ORDER		2

enum scan_result {
	SCAN_FAIL,
	SCAN_SUCCEED,
	SCAN_NO_PTE_TABLE,
	SCAN_PMD_MAPPED,
	SCAN_EXCEED_NONE_PTE,
	SCAN_EXCEED_SWAP_PTE,
	SCAN_EXCEED_SHARED_PTE,
	SCAN_PTE_NON_PRESENT,
	SCAN_PTE_UFFD,
	SCAN_PTE_MAPPED_HUGEPAGE,
	SCAN_LACK_REFERENCED_PAGE,
	SCAN_PAGE_NULL,
	SCAN_SCAN_ABORT,
	SCAN_PAGE_COUNT,
	SCAN_PAGE_LRU,
	SCAN_PAGE_LOCK,
	SCAN_PAGE_ANON,
	SCAN_PAGE_LAZYFREE,
	SCAN_PAGE_COMPOUND,
	SCAN_ANY_PROCESS,
	SCAN_VMA_NULL,
	SCAN_VMA_CHECK,
	SCAN_ADDRESS_RANGE,
	SCAN_DEL_PAGE_LRU,
	SCAN_ALLOC_HUGE_PAGE_FAIL,
	SCAN_CGROUP_CHARGE_FAIL,
	SCAN_TRUNCATED,
	SCAN_PAGE_HAS_PRIVATE,
	SCAN_STORE_FAILED,
	SCAN_COPY_MC,
	SCAN_PAGE_FILLED,
	SCAN_PAGE_DIRTY_OR_WRITEBACK,
};

/* How many PTEs of a window may be missing, swapped out or shared */
struct collapse_limits {
	/* Counted over a PMD-sized window; HPAGE_PMD_NR means "no limit" */
	unsigned int max_ptes_none;
	unsigned int max_ptes_swap;
	unsigned int max_ptes_shared;
};

/* What a collapse is allowed to do, decided by the caller that asks for it */
struct collapse_policy {
	/* Limits for a PMD-sized window */
	struct collapse_limits pmd;

	/*
	 * Limits for a smaller window.  Its max_ptes_none is either 0 or
	 * COLLAPSE_MAX_PTES_LIMIT, the latter meaning all but one PTE of the
	 * window whatever its order; any other value counts as 0.
	 */
	struct collapse_limits sub_pmd;

	/* Leave clean lazyfree folios to reclaim rather than collapse them */
	bool anon_skip_lazyfree;

	/* Refuse an anonymous range with no sign of use */
	bool anon_require_referenced;

	/* Map the PMD over a file collapse instead of leaving it to a fault */
	bool file_install_pmd;

	/* Write dirty pages back and retry once instead of refusing them */
	bool file_writeback_dirty;

	/* How hard to try for a destination folio */
	gfp_t gfp;

	/* Which VMAs are eligible, as thp_vma_allowable_orders() spells it */
	enum tva_type tva_type;
};

struct collapse_control {
	struct collapse_policy policy;

	/* Num pages scanned per node */
	u32 node_load[MAX_NUMNODES];

	/* Num pages scanned (see khugepaged_pages_to_scan) */
	unsigned int progress;

	/* nodemask for allocation fallback */
	nodemask_t alloc_nmask;

	/* Each bit marks a PTE the scan accepted as a collapse source */
	DECLARE_BITMAP(eligible_ptes, MAX_PTRS_PER_PTE);
};

#endif	/* __MM_COLLAPSE_H */

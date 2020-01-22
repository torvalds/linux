/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM huge_memory

#if !defined(__HUGE_MEMORY_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HUGE_MEMORY_H

#include  <linux/tracepoint.h>

#define SCAN_STATUS							\
	EM( SCAN_FAIL,			"failed")			\
	EM( SCAN_SUCCEED,		"succeeded")			\
	EM( SCAN_PMD_NULL,		"pmd_null")			\
	EM( SCAN_EXCEED_NONE_PTE,	"exceed_none_pte")		\
	EM( SCAN_PTE_NON_PRESENT,	"pte_non_present")		\
	EM( SCAN_PAGE_RO,		"no_writable_page")		\
	EM( SCAN_LACK_REFERENCED_PAGE,	"lack_referenced_page")		\
	EM( SCAN_PAGE_NULL,		"page_null")			\
	EM( SCAN_SCAN_ABORT,		"scan_aborted")			\
	EM( SCAN_PAGE_COUNT,		"not_suitable_page_count")	\
	EM( SCAN_PAGE_LRU,		"page_not_in_lru")		\
	EM( SCAN_PAGE_LOCK,		"page_locked")			\
	EM( SCAN_PAGE_ANON,		"page_not_anon")		\
	EM( SCAN_PAGE_COMPOUND,		"page_compound")		\
	EM( SCAN_ANY_PROCESS,		"no_process_for_page")		\
	EM( SCAN_VMA_NULL,		"vma_null")			\
	EM( SCAN_VMA_CHECK,		"vma_check_failed")		\
	EM( SCAN_ADDRESS_RANGE,		"not_suitable_address_range")	\
	EM( SCAN_SWAP_CACHE_PAGE,	"page_swap_cache")		\
	EM( SCAN_DEL_PAGE_LRU,		"could_not_delete_page_from_lru")\
	EM( SCAN_ALLOC_HUGE_PAGE_FAIL,	"alloc_huge_page_failed")	\
	EM( SCAN_CGROUP_CHARGE_FAIL,	"ccgroup_charge_failed")	\
	EM( SCAN_EXCEED_SWAP_PTE,	"exceed_swap_pte")		\
	EM( SCAN_TRUNCATED,		"truncated")			\
	EMe(SCAN_PAGE_HAS_PRIVATE,	"page_has_private")		\

#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

SCAN_STATUS

#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}

TRACE_EVENT(mm_khugepaged_scan_pmd,

	TP_PROTO(struct mm_struct *mm, struct page *page, bool writable,
		 int referenced, int none_or_zero, int status, int unmapped),

	TP_ARGS(mm, page, writable, referenced, none_or_zero, status, unmapped),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__field(unsigned long, pfn)
		__field(bool, writable)
		__field(int, referenced)
		__field(int, none_or_zero)
		__field(int, status)
		__field(int, unmapped)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__entry->pfn = page ? page_to_pfn(page) : -1;
		__entry->writable = writable;
		__entry->referenced = referenced;
		__entry->none_or_zero = none_or_zero;
		__entry->status = status;
		__entry->unmapped = unmapped;
	),

	TP_printk("mm=%p, scan_pfn=0x%lx, writable=%d, referenced=%d, none_or_zero=%d, status=%s, unmapped=%d",
		__entry->mm,
		__entry->pfn,
		__entry->writable,
		__entry->referenced,
		__entry->none_or_zero,
		__print_symbolic(__entry->status, SCAN_STATUS),
		__entry->unmapped)
);

TRACE_EVENT(mm_collapse_huge_page,

	TP_PROTO(struct mm_struct *mm, int isolated, int status),

	TP_ARGS(mm, isolated, status),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__field(int, isolated)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__entry->isolated = isolated;
		__entry->status = status;
	),

	TP_printk("mm=%p, isolated=%d, status=%s",
		__entry->mm,
		__entry->isolated,
		__print_symbolic(__entry->status, SCAN_STATUS))
);

TRACE_EVENT(mm_collapse_huge_page_isolate,

	TP_PROTO(struct page *page, int none_or_zero,
		 int referenced, bool  writable, int status),

	TP_ARGS(page, none_or_zero, referenced, writable, status),

	TP_STRUCT__entry(
		__field(unsigned long, pfn)
		__field(int, none_or_zero)
		__field(int, referenced)
		__field(bool, writable)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->pfn = page ? page_to_pfn(page) : -1;
		__entry->none_or_zero = none_or_zero;
		__entry->referenced = referenced;
		__entry->writable = writable;
		__entry->status = status;
	),

	TP_printk("scan_pfn=0x%lx, none_or_zero=%d, referenced=%d, writable=%d, status=%s",
		__entry->pfn,
		__entry->none_or_zero,
		__entry->referenced,
		__entry->writable,
		__print_symbolic(__entry->status, SCAN_STATUS))
);

TRACE_EVENT(mm_collapse_huge_page_swapin,

	TP_PROTO(struct mm_struct *mm, int swapped_in, int referenced, int ret),

	TP_ARGS(mm, swapped_in, referenced, ret),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__field(int, swapped_in)
		__field(int, referenced)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__entry->swapped_in = swapped_in;
		__entry->referenced = referenced;
		__entry->ret = ret;
	),

	TP_printk("mm=%p, swapped_in=%d, referenced=%d, ret=%d",
		__entry->mm,
		__entry->swapped_in,
		__entry->referenced,
		__entry->ret)
);

#endif /* __HUGE_MEMORY_H */
#include <trace/define_trace.h>

/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <trace/hooks/vendor_hooks.h>

struct shmem_inode_info;
struct folio;
struct page_vma_mapped_walk;
struct compact_control;

DECLARE_RESTRICTED_HOOK(android_rvh_shmem_get_folio,
			TP_PROTO(struct shmem_inode_info *info, struct folio **folio),
			TP_ARGS(info, folio), 2);

DECLARE_RESTRICTED_HOOK(android_rvh_set_gfp_zone_flags,
			TP_PROTO(unsigned int *flags),	/* gfp_t *flags */
			TP_ARGS(flags), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_readahead_gfp_mask,
			TP_PROTO(unsigned int *flags),	/* gfp_t *flags */
			TP_ARGS(flags), 1);
DECLARE_HOOK(android_vh_dm_bufio_shrink_scan_bypass,
	TP_PROTO(unsigned long dm_bufio_current_allocated, bool *bypass),
	TP_ARGS(dm_bufio_current_allocated, bypass));
DECLARE_HOOK(android_vh_cleanup_old_buffers_bypass,
	TP_PROTO(unsigned long dm_bufio_current_allocated,
		unsigned long *max_age_hz,
		bool *bypass),
	TP_ARGS(dm_bufio_current_allocated, max_age_hz, bypass));
DECLARE_HOOK(android_vh_mmap_region,
	TP_PROTO(struct vm_area_struct *vma, unsigned long addr),
	TP_ARGS(vma, addr));
DECLARE_HOOK(android_vh_try_to_unmap_one,
	TP_PROTO(struct folio *folio, struct vm_area_struct *vma,
		unsigned long addr, void *arg, bool ret),
	TP_ARGS(folio, vma, addr, arg, ret));
DECLARE_HOOK(android_vh_get_page_wmark,
	TP_PROTO(unsigned int alloc_flags, unsigned long *page_wmark),
	TP_ARGS(alloc_flags, page_wmark));
DECLARE_HOOK(android_vh_page_add_new_anon_rmap,
	TP_PROTO(struct page *page, struct vm_area_struct *vma,
		unsigned long address),
	TP_ARGS(page, vma, address));
DECLARE_HOOK(android_vh_rmqueue,
	TP_PROTO(struct zone *preferred_zone, struct zone *zone,
		unsigned int order, gfp_t gfp_flags,
		unsigned int alloc_flags, int migratetype),
	TP_ARGS(preferred_zone, zone, order,
		gfp_flags, alloc_flags, migratetype));
DECLARE_HOOK(android_vh_filemap_get_folio,
	TP_PROTO(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp_mask, struct folio *folio),
	TP_ARGS(mapping, index, fgp_flags, gfp_mask, folio));
DECLARE_HOOK(android_vh_meminfo_proc_show,
	TP_PROTO(struct seq_file *m),
	TP_ARGS(m));
DECLARE_HOOK(android_vh_exit_mm,
	TP_PROTO(struct mm_struct *mm),
	TP_ARGS(mm));
DECLARE_HOOK(android_vh_show_mem,
	TP_PROTO(unsigned int filter, nodemask_t *nodemask),
	TP_ARGS(filter, nodemask));
DECLARE_HOOK(android_vh_alloc_pages_slowpath,
	TP_PROTO(gfp_t gfp_mask, unsigned int order, unsigned long delta),
	TP_ARGS(gfp_mask, order, delta));
DECLARE_HOOK(android_vh_print_slabinfo_header,
	TP_PROTO(struct seq_file *m),
	TP_ARGS(m));
struct slabinfo;
DECLARE_HOOK(android_vh_cache_show,
	TP_PROTO(struct seq_file *m, struct slabinfo *sinfo, struct kmem_cache *s),
	TP_ARGS(m, sinfo, s));
DECLARE_HOOK(android_vh_alloc_pages_reclaim_bypass,
    TP_PROTO(gfp_t gfp_mask, int order, int alloc_flags,
	int migratetype, struct page **page),
	TP_ARGS(gfp_mask, order, alloc_flags, migratetype, page));
DECLARE_HOOK(android_vh_alloc_pages_failure_bypass,
	TP_PROTO(gfp_t gfp_mask, int order, int alloc_flags,
	int migratetype, struct page **page),
	TP_ARGS(gfp_mask, order, alloc_flags, migratetype, page));
DECLARE_HOOK(android_vh_madvise_pageout_swap_entry,
	TP_PROTO(swp_entry_t entry, int swapcount),
	TP_ARGS(entry, swapcount));
DECLARE_HOOK(android_vh_madvise_swapin_walk_pmd_entry,
	TP_PROTO(swp_entry_t entry),
	TP_ARGS(entry));
DECLARE_HOOK(android_vh_process_madvise_end,
	TP_PROTO(int behavior, ssize_t *ret),
	TP_ARGS(behavior, ret));
DECLARE_HOOK(android_vh_smaps_pte_entry,
	TP_PROTO(swp_entry_t entry, unsigned long *writeback,
		unsigned long *same, unsigned long *huge),
	TP_ARGS(entry, writeback, same, huge));
DECLARE_HOOK(android_vh_show_smap,
	TP_PROTO(struct seq_file *m, unsigned long writeback,
		unsigned long same, unsigned long huge),
	TP_ARGS(m, writeback, same, huge));
DECLARE_HOOK(android_vh_meminfo_cache_adjust,
	TP_PROTO(unsigned long *cached),
	TP_ARGS(cached));
DECLARE_HOOK(android_vh_si_mem_available_adjust,
	TP_PROTO(unsigned long *available),
	TP_ARGS(available));
DECLARE_HOOK(android_vh_si_meminfo_adjust,
	TP_PROTO(unsigned long *totalram, unsigned long *freeram),
	TP_ARGS(totalram, freeram));
DECLARE_RESTRICTED_HOOK(android_rvh_ctl_dirty_rate,
	TP_PROTO(void *unused),
	TP_ARGS(unused), 1);
DECLARE_HOOK(android_vh_madvise_cold_pageout_skip,
	TP_PROTO(struct vm_area_struct *vma, struct page *page, bool pageout, bool *need_skip),
	TP_ARGS(vma, page, pageout, need_skip));

DECLARE_HOOK(android_vh_mm_compaction_begin,
	TP_PROTO(struct compact_control *cc, long *vendor_ret),
	TP_ARGS(cc, vendor_ret));
DECLARE_HOOK(android_vh_mm_compaction_end,
	TP_PROTO(struct compact_control *cc, long vendor_ret),
	TP_ARGS(cc, vendor_ret));
DECLARE_HOOK(android_vh_free_unref_page_bypass,
	TP_PROTO(struct page *page, int order, int migratetype, bool *bypass),
	TP_ARGS(page, order, migratetype, bypass));
DECLARE_HOOK(android_vh_kvmalloc_node_use_vmalloc,
	TP_PROTO(size_t size, gfp_t *kmalloc_flags, bool *use_vmalloc),
	TP_ARGS(size, kmalloc_flags, use_vmalloc));
DECLARE_HOOK(android_vh_should_alloc_pages_retry,
	TP_PROTO(gfp_t gfp_mask, int order, int *alloc_flags,
	int migratetype, struct zone *preferred_zone, struct page **page, bool *should_alloc_retry),
	TP_ARGS(gfp_mask, order, alloc_flags,
		migratetype, preferred_zone, page, should_alloc_retry));
DECLARE_HOOK(android_vh_unreserve_highatomic_bypass,
	TP_PROTO(bool force, struct zone *zone, bool *skip_unreserve_highatomic),
	TP_ARGS(force, zone, skip_unreserve_highatomic));
DECLARE_HOOK(android_vh_rmqueue_bulk_bypass,
	TP_PROTO(unsigned int order, struct per_cpu_pages *pcp, int migratetype,
		struct list_head *list),
	TP_ARGS(order, pcp, migratetype, list));
DECLARE_HOOK(android_vh_ra_tuning_max_page,
	TP_PROTO(struct readahead_control *ractl, unsigned long *max_page),
	TP_ARGS(ractl, max_page));
DECLARE_HOOK(android_vh_tune_mmap_readaround,
	TP_PROTO(unsigned int ra_pages, pgoff_t pgoff,
		pgoff_t *start, unsigned int *size, unsigned int *async_size),
	TP_ARGS(ra_pages, pgoff, start, size, async_size));
struct mem_cgroup;
DECLARE_HOOK(android_vh_mem_cgroup_alloc,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_mem_cgroup_free,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_mem_cgroup_id_remove,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
struct cgroup_subsys_state;
DECLARE_HOOK(android_vh_mem_cgroup_css_online,
	TP_PROTO(struct cgroup_subsys_state *css, struct mem_cgroup *memcg),
	TP_ARGS(css, memcg));
DECLARE_HOOK(android_vh_mem_cgroup_css_offline,
	TP_PROTO(struct cgroup_subsys_state *css, struct mem_cgroup *memcg),
	TP_ARGS(css, memcg));
DECLARE_HOOK(android_vh_rmqueue_smallest_bypass,
	TP_PROTO(struct page **page, struct zone *zone, int order, int migratetype),
	TP_ARGS(page, zone, order, migratetype));
DECLARE_HOOK(android_vh_free_one_page_bypass,
	TP_PROTO(struct page *page, struct zone *zone, int order, int migratetype,
		int fpi_flags, bool *bypass),
	TP_ARGS(page, zone, order, migratetype, fpi_flags, bypass));
DECLARE_HOOK(android_vh_test_clear_look_around_ref,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_HOOK(android_vh_look_around_migrate_folio,
	TP_PROTO(struct folio *old_folio, struct folio *new_folio),
	TP_ARGS(old_folio, new_folio));
DECLARE_HOOK(android_vh_look_around,
	TP_PROTO(struct page_vma_mapped_walk *pvmw, struct folio *folio,
		struct vm_area_struct *vma, int *referenced),
	TP_ARGS(pvmw, folio, vma, referenced));

DECLARE_HOOK(android_vh_mm_alloc_pages_direct_reclaim_enter,
	TP_PROTO(unsigned int order),
	TP_ARGS(order));
DECLARE_HOOK(android_vh_mm_alloc_pages_direct_reclaim_exit,
	TP_PROTO(unsigned long did_some_progress, int retry_times),
	TP_ARGS(did_some_progress, retry_times));
struct oom_control;
DECLARE_HOOK(android_vh_mm_alloc_pages_may_oom_exit,
	TP_PROTO(struct oom_control *oc, unsigned long did_some_progress),
	TP_ARGS(oc, did_some_progress));
DECLARE_HOOK(android_vh_adjust_kvmalloc_flags,
	TP_PROTO(unsigned int order, gfp_t *alloc_flags),
	TP_ARGS(order, alloc_flags));
DECLARE_HOOK(android_vh_slab_folio_alloced,
	TP_PROTO(unsigned int order, gfp_t flags),
	TP_ARGS(order, flags));
#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

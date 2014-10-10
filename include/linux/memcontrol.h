/* memcontrol.h - Memory Controller
 *
 * Copyright IBM Corporation, 2007
 * Author Balbir Singh <balbir@linux.vnet.ibm.com>
 *
 * Copyright 2007 OpenVZ SWsoft Inc
 * Author: Pavel Emelianov <xemul@openvz.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H
#include <linux/cgroup.h>
#include <linux/vm_event_item.h>
#include <linux/hardirq.h>
#include <linux/jump_label.h>

struct mem_cgroup;
struct page_cgroup;
struct page;
struct mm_struct;
struct kmem_cache;

/*
 * The corresponding mem_cgroup_stat_names is defined in mm/memcontrol.c,
 * These two lists should keep in accord with each other.
 */
enum mem_cgroup_stat_index {
	/*
	 * For MEM_CONTAINER_TYPE_ALL, usage = pagecache + rss.
	 */
	MEM_CGROUP_STAT_CACHE,		/* # of pages charged as cache */
	MEM_CGROUP_STAT_RSS,		/* # of pages charged as anon rss */
	MEM_CGROUP_STAT_RSS_HUGE,	/* # of pages charged as anon huge */
	MEM_CGROUP_STAT_FILE_MAPPED,	/* # of pages charged as file rss */
	MEM_CGROUP_STAT_WRITEBACK,	/* # of pages under writeback */
	MEM_CGROUP_STAT_SWAP,		/* # of pages, swapped out */
	MEM_CGROUP_STAT_NSTATS,
};

struct mem_cgroup_reclaim_cookie {
	struct zone *zone;
	int priority;
	unsigned int generation;
};

#ifdef CONFIG_MEMCG
int mem_cgroup_try_charge(struct page *page, struct mm_struct *mm,
			  gfp_t gfp_mask, struct mem_cgroup **memcgp);
void mem_cgroup_commit_charge(struct page *page, struct mem_cgroup *memcg,
			      bool lrucare);
void mem_cgroup_cancel_charge(struct page *page, struct mem_cgroup *memcg);
void mem_cgroup_uncharge(struct page *page);
void mem_cgroup_uncharge_list(struct list_head *page_list);

void mem_cgroup_migrate(struct page *oldpage, struct page *newpage,
			bool lrucare);

struct lruvec *mem_cgroup_zone_lruvec(struct zone *, struct mem_cgroup *);
struct lruvec *mem_cgroup_page_lruvec(struct page *, struct zone *);

bool __mem_cgroup_same_or_subtree(const struct mem_cgroup *root_memcg,
				  struct mem_cgroup *memcg);
bool task_in_mem_cgroup(struct task_struct *task,
			const struct mem_cgroup *memcg);

extern struct mem_cgroup *try_get_mem_cgroup_from_page(struct page *page);
extern struct mem_cgroup *mem_cgroup_from_task(struct task_struct *p);

extern struct mem_cgroup *parent_mem_cgroup(struct mem_cgroup *memcg);
extern struct mem_cgroup *mem_cgroup_from_css(struct cgroup_subsys_state *css);

static inline
bool mm_match_cgroup(const struct mm_struct *mm, const struct mem_cgroup *memcg)
{
	struct mem_cgroup *task_memcg;
	bool match;

	rcu_read_lock();
	task_memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
	match = __mem_cgroup_same_or_subtree(memcg, task_memcg);
	rcu_read_unlock();
	return match;
}

extern struct cgroup_subsys_state *mem_cgroup_css(struct mem_cgroup *memcg);

struct mem_cgroup *mem_cgroup_iter(struct mem_cgroup *,
				   struct mem_cgroup *,
				   struct mem_cgroup_reclaim_cookie *);
void mem_cgroup_iter_break(struct mem_cgroup *, struct mem_cgroup *);

/*
 * For memory reclaim.
 */
int mem_cgroup_inactive_anon_is_low(struct lruvec *lruvec);
int mem_cgroup_select_victim_node(struct mem_cgroup *memcg);
unsigned long mem_cgroup_get_lru_size(struct lruvec *lruvec, enum lru_list);
void mem_cgroup_update_lru_size(struct lruvec *, enum lru_list, int);
extern void mem_cgroup_print_oom_info(struct mem_cgroup *memcg,
					struct task_struct *p);

static inline void mem_cgroup_oom_enable(void)
{
	WARN_ON(current->memcg_oom.may_oom);
	current->memcg_oom.may_oom = 1;
}

static inline void mem_cgroup_oom_disable(void)
{
	WARN_ON(!current->memcg_oom.may_oom);
	current->memcg_oom.may_oom = 0;
}

static inline bool task_in_memcg_oom(struct task_struct *p)
{
	return p->memcg_oom.memcg;
}

bool mem_cgroup_oom_synchronize(bool wait);

#ifdef CONFIG_MEMCG_SWAP
extern int do_swap_account;
#endif

static inline bool mem_cgroup_disabled(void)
{
	if (memory_cgrp_subsys.disabled)
		return true;
	return false;
}

void __mem_cgroup_begin_update_page_stat(struct page *page, bool *locked,
					 unsigned long *flags);

extern atomic_t memcg_moving;

static inline void mem_cgroup_begin_update_page_stat(struct page *page,
					bool *locked, unsigned long *flags)
{
	if (mem_cgroup_disabled())
		return;
	rcu_read_lock();
	*locked = false;
	if (atomic_read(&memcg_moving))
		__mem_cgroup_begin_update_page_stat(page, locked, flags);
}

void __mem_cgroup_end_update_page_stat(struct page *page,
				unsigned long *flags);
static inline void mem_cgroup_end_update_page_stat(struct page *page,
					bool *locked, unsigned long *flags)
{
	if (mem_cgroup_disabled())
		return;
	if (*locked)
		__mem_cgroup_end_update_page_stat(page, flags);
	rcu_read_unlock();
}

void mem_cgroup_update_page_stat(struct page *page,
				 enum mem_cgroup_stat_index idx,
				 int val);

static inline void mem_cgroup_inc_page_stat(struct page *page,
					    enum mem_cgroup_stat_index idx)
{
	mem_cgroup_update_page_stat(page, idx, 1);
}

static inline void mem_cgroup_dec_page_stat(struct page *page,
					    enum mem_cgroup_stat_index idx)
{
	mem_cgroup_update_page_stat(page, idx, -1);
}

unsigned long mem_cgroup_soft_limit_reclaim(struct zone *zone, int order,
						gfp_t gfp_mask,
						unsigned long *total_scanned);

void __mem_cgroup_count_vm_event(struct mm_struct *mm, enum vm_event_item idx);
static inline void mem_cgroup_count_vm_event(struct mm_struct *mm,
					     enum vm_event_item idx)
{
	if (mem_cgroup_disabled())
		return;
	__mem_cgroup_count_vm_event(mm, idx);
}
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void mem_cgroup_split_huge_fixup(struct page *head);
#endif

#ifdef CONFIG_DEBUG_VM
bool mem_cgroup_bad_page_check(struct page *page);
void mem_cgroup_print_bad_page(struct page *page);
#endif
#else /* CONFIG_MEMCG */
struct mem_cgroup;

static inline int mem_cgroup_try_charge(struct page *page, struct mm_struct *mm,
					gfp_t gfp_mask,
					struct mem_cgroup **memcgp)
{
	*memcgp = NULL;
	return 0;
}

static inline void mem_cgroup_commit_charge(struct page *page,
					    struct mem_cgroup *memcg,
					    bool lrucare)
{
}

static inline void mem_cgroup_cancel_charge(struct page *page,
					    struct mem_cgroup *memcg)
{
}

static inline void mem_cgroup_uncharge(struct page *page)
{
}

static inline void mem_cgroup_uncharge_list(struct list_head *page_list)
{
}

static inline void mem_cgroup_migrate(struct page *oldpage,
				      struct page *newpage,
				      bool lrucare)
{
}

static inline struct lruvec *mem_cgroup_zone_lruvec(struct zone *zone,
						    struct mem_cgroup *memcg)
{
	return &zone->lruvec;
}

static inline struct lruvec *mem_cgroup_page_lruvec(struct page *page,
						    struct zone *zone)
{
	return &zone->lruvec;
}

static inline struct mem_cgroup *try_get_mem_cgroup_from_page(struct page *page)
{
	return NULL;
}

static inline bool mm_match_cgroup(struct mm_struct *mm,
		struct mem_cgroup *memcg)
{
	return true;
}

static inline bool task_in_mem_cgroup(struct task_struct *task,
				      const struct mem_cgroup *memcg)
{
	return true;
}

static inline struct cgroup_subsys_state
		*mem_cgroup_css(struct mem_cgroup *memcg)
{
	return NULL;
}

static inline struct mem_cgroup *
mem_cgroup_iter(struct mem_cgroup *root,
		struct mem_cgroup *prev,
		struct mem_cgroup_reclaim_cookie *reclaim)
{
	return NULL;
}

static inline void mem_cgroup_iter_break(struct mem_cgroup *root,
					 struct mem_cgroup *prev)
{
}

static inline bool mem_cgroup_disabled(void)
{
	return true;
}

static inline int
mem_cgroup_inactive_anon_is_low(struct lruvec *lruvec)
{
	return 1;
}

static inline unsigned long
mem_cgroup_get_lru_size(struct lruvec *lruvec, enum lru_list lru)
{
	return 0;
}

static inline void
mem_cgroup_update_lru_size(struct lruvec *lruvec, enum lru_list lru,
			      int increment)
{
}

static inline void
mem_cgroup_print_oom_info(struct mem_cgroup *memcg, struct task_struct *p)
{
}

static inline void mem_cgroup_begin_update_page_stat(struct page *page,
					bool *locked, unsigned long *flags)
{
}

static inline void mem_cgroup_end_update_page_stat(struct page *page,
					bool *locked, unsigned long *flags)
{
}

static inline void mem_cgroup_oom_enable(void)
{
}

static inline void mem_cgroup_oom_disable(void)
{
}

static inline bool task_in_memcg_oom(struct task_struct *p)
{
	return false;
}

static inline bool mem_cgroup_oom_synchronize(bool wait)
{
	return false;
}

static inline void mem_cgroup_inc_page_stat(struct page *page,
					    enum mem_cgroup_stat_index idx)
{
}

static inline void mem_cgroup_dec_page_stat(struct page *page,
					    enum mem_cgroup_stat_index idx)
{
}

static inline
unsigned long mem_cgroup_soft_limit_reclaim(struct zone *zone, int order,
					    gfp_t gfp_mask,
					    unsigned long *total_scanned)
{
	return 0;
}

static inline void mem_cgroup_split_huge_fixup(struct page *head)
{
}

static inline
void mem_cgroup_count_vm_event(struct mm_struct *mm, enum vm_event_item idx)
{
}
#endif /* CONFIG_MEMCG */

#if !defined(CONFIG_MEMCG) || !defined(CONFIG_DEBUG_VM)
static inline bool
mem_cgroup_bad_page_check(struct page *page)
{
	return false;
}

static inline void
mem_cgroup_print_bad_page(struct page *page)
{
}
#endif

enum {
	UNDER_LIMIT,
	SOFT_LIMIT,
	OVER_LIMIT,
};

struct sock;
#if defined(CONFIG_INET) && defined(CONFIG_MEMCG_KMEM)
void sock_update_memcg(struct sock *sk);
void sock_release_memcg(struct sock *sk);
#else
static inline void sock_update_memcg(struct sock *sk)
{
}
static inline void sock_release_memcg(struct sock *sk)
{
}
#endif /* CONFIG_INET && CONFIG_MEMCG_KMEM */

#ifdef CONFIG_MEMCG_KMEM
extern struct static_key memcg_kmem_enabled_key;

extern int memcg_limited_groups_array_size;

/*
 * Helper macro to loop through all memcg-specific caches. Callers must still
 * check if the cache is valid (it is either valid or NULL).
 * the slab_mutex must be held when looping through those caches
 */
#define for_each_memcg_cache_index(_idx)	\
	for ((_idx) = 0; (_idx) < memcg_limited_groups_array_size; (_idx)++)

static inline bool memcg_kmem_enabled(void)
{
	return static_key_false(&memcg_kmem_enabled_key);
}

/*
 * In general, we'll do everything in our power to not incur in any overhead
 * for non-memcg users for the kmem functions. Not even a function call, if we
 * can avoid it.
 *
 * Therefore, we'll inline all those functions so that in the best case, we'll
 * see that kmemcg is off for everybody and proceed quickly.  If it is on,
 * we'll still do most of the flag checking inline. We check a lot of
 * conditions, but because they are pretty simple, they are expected to be
 * fast.
 */
bool __memcg_kmem_newpage_charge(gfp_t gfp, struct mem_cgroup **memcg,
					int order);
void __memcg_kmem_commit_charge(struct page *page,
				       struct mem_cgroup *memcg, int order);
void __memcg_kmem_uncharge_pages(struct page *page, int order);

int memcg_cache_id(struct mem_cgroup *memcg);

void memcg_update_array_size(int num_groups);

struct kmem_cache *
__memcg_kmem_get_cache(struct kmem_cache *cachep, gfp_t gfp);

int __memcg_charge_slab(struct kmem_cache *cachep, gfp_t gfp, int order);
void __memcg_uncharge_slab(struct kmem_cache *cachep, int order);

int __memcg_cleanup_cache_params(struct kmem_cache *s);

/**
 * memcg_kmem_newpage_charge: verify if a new kmem allocation is allowed.
 * @gfp: the gfp allocation flags.
 * @memcg: a pointer to the memcg this was charged against.
 * @order: allocation order.
 *
 * returns true if the memcg where the current task belongs can hold this
 * allocation.
 *
 * We return true automatically if this allocation is not to be accounted to
 * any memcg.
 */
static inline bool
memcg_kmem_newpage_charge(gfp_t gfp, struct mem_cgroup **memcg, int order)
{
	if (!memcg_kmem_enabled())
		return true;

	/*
	 * __GFP_NOFAIL allocations will move on even if charging is not
	 * possible. Therefore we don't even try, and have this allocation
	 * unaccounted. We could in theory charge it with
	 * res_counter_charge_nofail, but we hope those allocations are rare,
	 * and won't be worth the trouble.
	 */
	if (gfp & __GFP_NOFAIL)
		return true;
	if (in_interrupt() || (!current->mm) || (current->flags & PF_KTHREAD))
		return true;

	/* If the test is dying, just let it go. */
	if (unlikely(fatal_signal_pending(current)))
		return true;

	return __memcg_kmem_newpage_charge(gfp, memcg, order);
}

/**
 * memcg_kmem_uncharge_pages: uncharge pages from memcg
 * @page: pointer to struct page being freed
 * @order: allocation order.
 *
 * there is no need to specify memcg here, since it is embedded in page_cgroup
 */
static inline void
memcg_kmem_uncharge_pages(struct page *page, int order)
{
	if (memcg_kmem_enabled())
		__memcg_kmem_uncharge_pages(page, order);
}

/**
 * memcg_kmem_commit_charge: embeds correct memcg in a page
 * @page: pointer to struct page recently allocated
 * @memcg: the memcg structure we charged against
 * @order: allocation order.
 *
 * Needs to be called after memcg_kmem_newpage_charge, regardless of success or
 * failure of the allocation. if @page is NULL, this function will revert the
 * charges. Otherwise, it will commit the memcg given by @memcg to the
 * corresponding page_cgroup.
 */
static inline void
memcg_kmem_commit_charge(struct page *page, struct mem_cgroup *memcg, int order)
{
	if (memcg_kmem_enabled() && memcg)
		__memcg_kmem_commit_charge(page, memcg, order);
}

/**
 * memcg_kmem_get_cache: selects the correct per-memcg cache for allocation
 * @cachep: the original global kmem cache
 * @gfp: allocation flags.
 *
 * All memory allocated from a per-memcg cache is charged to the owner memcg.
 */
static __always_inline struct kmem_cache *
memcg_kmem_get_cache(struct kmem_cache *cachep, gfp_t gfp)
{
	if (!memcg_kmem_enabled())
		return cachep;
	if (gfp & __GFP_NOFAIL)
		return cachep;
	if (in_interrupt() || (!current->mm) || (current->flags & PF_KTHREAD))
		return cachep;
	if (unlikely(fatal_signal_pending(current)))
		return cachep;

	return __memcg_kmem_get_cache(cachep, gfp);
}
#else
#define for_each_memcg_cache_index(_idx)	\
	for (; NULL; )

static inline bool memcg_kmem_enabled(void)
{
	return false;
}

static inline bool
memcg_kmem_newpage_charge(gfp_t gfp, struct mem_cgroup **memcg, int order)
{
	return true;
}

static inline void memcg_kmem_uncharge_pages(struct page *page, int order)
{
}

static inline void
memcg_kmem_commit_charge(struct page *page, struct mem_cgroup *memcg, int order)
{
}

static inline int memcg_cache_id(struct mem_cgroup *memcg)
{
	return -1;
}

static inline struct kmem_cache *
memcg_kmem_get_cache(struct kmem_cache *cachep, gfp_t gfp)
{
	return cachep;
}
#endif /* CONFIG_MEMCG_KMEM */
#endif /* _LINUX_MEMCONTROL_H */


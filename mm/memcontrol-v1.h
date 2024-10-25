/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MM_MEMCONTROL_V1_H
#define __MM_MEMCONTROL_V1_H

#include <linux/cgroup-defs.h>

/* Cgroup v1 and v2 common declarations */

int try_charge_memcg(struct mem_cgroup *memcg, gfp_t gfp_mask,
		     unsigned int nr_pages);

static inline int try_charge(struct mem_cgroup *memcg, gfp_t gfp_mask,
			     unsigned int nr_pages)
{
	if (mem_cgroup_is_root(memcg))
		return 0;

	return try_charge_memcg(memcg, gfp_mask, nr_pages);
}

void mem_cgroup_id_get_many(struct mem_cgroup *memcg, unsigned int n);
void mem_cgroup_id_put_many(struct mem_cgroup *memcg, unsigned int n);

/*
 * Iteration constructs for visiting all cgroups (under a tree).  If
 * loops are exited prematurely (break), mem_cgroup_iter_break() must
 * be used for reference counting.
 */
#define for_each_mem_cgroup_tree(iter, root)		\
	for (iter = mem_cgroup_iter(root, NULL, NULL);	\
	     iter != NULL;				\
	     iter = mem_cgroup_iter(root, iter, NULL))

#define for_each_mem_cgroup(iter)			\
	for (iter = mem_cgroup_iter(NULL, NULL, NULL);	\
	     iter != NULL;				\
	     iter = mem_cgroup_iter(NULL, iter, NULL))

/* Whether legacy memory+swap accounting is active */
static bool do_memsw_account(void)
{
	return !cgroup_subsys_on_dfl(memory_cgrp_subsys);
}

/*
 * Per memcg event counter is incremented at every pagein/pageout. With THP,
 * it will be incremented by the number of pages. This counter is used
 * to trigger some periodic events. This is straightforward and better
 * than using jiffies etc. to handle periodic memcg event.
 */
enum mem_cgroup_events_target {
	MEM_CGROUP_TARGET_THRESH,
	MEM_CGROUP_TARGET_SOFTLIMIT,
	MEM_CGROUP_NTARGETS,
};

unsigned long mem_cgroup_usage(struct mem_cgroup *memcg, bool swap);

void drain_all_stock(struct mem_cgroup *root_memcg);

unsigned long memcg_events(struct mem_cgroup *memcg, int event);
unsigned long memcg_events_local(struct mem_cgroup *memcg, int event);
unsigned long memcg_page_state_local(struct mem_cgroup *memcg, int idx);
unsigned long memcg_page_state_output(struct mem_cgroup *memcg, int item);
unsigned long memcg_page_state_local_output(struct mem_cgroup *memcg, int item);
int memory_stat_show(struct seq_file *m, void *v);

/* Cgroup v1-specific declarations */
#ifdef CONFIG_MEMCG_V1

bool memcg1_alloc_events(struct mem_cgroup *memcg);
void memcg1_free_events(struct mem_cgroup *memcg);

void memcg1_memcg_init(struct mem_cgroup *memcg);
void memcg1_remove_from_trees(struct mem_cgroup *memcg);

static inline void memcg1_soft_limit_reset(struct mem_cgroup *memcg)
{
	WRITE_ONCE(memcg->soft_limit, PAGE_COUNTER_MAX);
}

struct cgroup_taskset;
void memcg1_css_offline(struct mem_cgroup *memcg);

/* for encoding cft->private value on file */
enum res_type {
	_MEM,
	_MEMSWAP,
	_KMEM,
	_TCP,
};

bool memcg1_oom_prepare(struct mem_cgroup *memcg, bool *locked);
void memcg1_oom_finish(struct mem_cgroup *memcg, bool locked);
void memcg1_oom_recover(struct mem_cgroup *memcg);

void memcg1_commit_charge(struct folio *folio, struct mem_cgroup *memcg);
void memcg1_swapout(struct folio *folio, struct mem_cgroup *memcg);
void memcg1_uncharge_batch(struct mem_cgroup *memcg, unsigned long pgpgout,
			   unsigned long nr_memory, int nid);

void memcg1_stat_format(struct mem_cgroup *memcg, struct seq_buf *s);

void memcg1_account_kmem(struct mem_cgroup *memcg, int nr_pages);
static inline bool memcg1_tcpmem_active(struct mem_cgroup *memcg)
{
	return memcg->tcpmem_active;
}
bool memcg1_charge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages,
			 gfp_t gfp_mask);
static inline void memcg1_uncharge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	page_counter_uncharge(&memcg->tcpmem, nr_pages);
}

extern struct cftype memsw_files[];
extern struct cftype mem_cgroup_legacy_files[];

#else	/* CONFIG_MEMCG_V1 */

static inline bool memcg1_alloc_events(struct mem_cgroup *memcg) { return true; }
static inline void memcg1_free_events(struct mem_cgroup *memcg) {}

static inline void memcg1_memcg_init(struct mem_cgroup *memcg) {}
static inline void memcg1_remove_from_trees(struct mem_cgroup *memcg) {}
static inline void memcg1_soft_limit_reset(struct mem_cgroup *memcg) {}
static inline void memcg1_css_offline(struct mem_cgroup *memcg) {}

static inline bool memcg1_oom_prepare(struct mem_cgroup *memcg, bool *locked) { return true; }
static inline void memcg1_oom_finish(struct mem_cgroup *memcg, bool locked) {}
static inline void memcg1_oom_recover(struct mem_cgroup *memcg) {}

static inline void memcg1_commit_charge(struct folio *folio,
					struct mem_cgroup *memcg) {}

static inline void memcg1_swapout(struct folio *folio, struct mem_cgroup *memcg) {}

static inline void memcg1_uncharge_batch(struct mem_cgroup *memcg,
					 unsigned long pgpgout,
					 unsigned long nr_memory, int nid) {}

static inline void memcg1_stat_format(struct mem_cgroup *memcg, struct seq_buf *s) {}

static inline void memcg1_account_kmem(struct mem_cgroup *memcg, int nr_pages) {}
static inline bool memcg1_tcpmem_active(struct mem_cgroup *memcg) { return false; }
static inline bool memcg1_charge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages,
				       gfp_t gfp_mask) { return true; }
static inline void memcg1_uncharge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages) {}

#endif	/* CONFIG_MEMCG_V1 */

#endif	/* __MM_MEMCONTROL_V1_H */

/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MM_MEMCONTROL_V1_H
#define __MM_MEMCONTROL_V1_H

void memcg1_update_tree(struct mem_cgroup *memcg, int nid);
void memcg1_remove_from_trees(struct mem_cgroup *memcg);

static inline void memcg1_soft_limit_reset(struct mem_cgroup *memcg)
{
	WRITE_ONCE(memcg->soft_limit, PAGE_COUNTER_MAX);
}

void mem_cgroup_charge_statistics(struct mem_cgroup *memcg, int nr_pages);
void memcg1_check_events(struct mem_cgroup *memcg, int nid);
void memcg_oom_recover(struct mem_cgroup *memcg);
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

bool memcg1_wait_acct_move(struct mem_cgroup *memcg);
struct cgroup_taskset;
int memcg1_can_attach(struct cgroup_taskset *tset);
void memcg1_cancel_attach(struct cgroup_taskset *tset);
void memcg1_move_task(void);

struct cftype;
u64 mem_cgroup_move_charge_read(struct cgroup_subsys_state *css,
				struct cftype *cft);
int mem_cgroup_move_charge_write(struct cgroup_subsys_state *css,
				 struct cftype *cft, u64 val);

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

/* Whether legacy memory+swap accounting is active */
static bool do_memsw_account(void)
{
	return !cgroup_subsys_on_dfl(memory_cgrp_subsys);
}

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

void memcg1_css_offline(struct mem_cgroup *memcg);

/* for encoding cft->private value on file */
enum res_type {
	_MEM,
	_MEMSWAP,
	_KMEM,
	_TCP,
};

bool mem_cgroup_event_ratelimit(struct mem_cgroup *memcg,
				enum mem_cgroup_events_target target);
unsigned long mem_cgroup_usage(struct mem_cgroup *memcg, bool swap);
ssize_t memcg_write_event_control(struct kernfs_open_file *of,
				  char *buf, size_t nbytes, loff_t off);

bool memcg1_oom_prepare(struct mem_cgroup *memcg, bool *locked);
void memcg1_oom_finish(struct mem_cgroup *memcg, bool locked);

#endif	/* __MM_MEMCONTROL_V1_H */

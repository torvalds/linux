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
void memcg_check_events(struct mem_cgroup *memcg, int nid);
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

bool mem_cgroup_wait_acct_move(struct mem_cgroup *memcg);
struct cgroup_taskset;
int mem_cgroup_can_attach(struct cgroup_taskset *tset);
void mem_cgroup_cancel_attach(struct cgroup_taskset *tset);
void mem_cgroup_move_task(void);

struct cftype;
u64 mem_cgroup_move_charge_read(struct cgroup_subsys_state *css,
				struct cftype *cft);
int mem_cgroup_move_charge_write(struct cgroup_subsys_state *css,
				 struct cftype *cft, u64 val);

#endif	/* __MM_MEMCONTROL_V1_H */

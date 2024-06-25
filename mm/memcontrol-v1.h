/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MM_MEMCONTROL_V1_H
#define __MM_MEMCONTROL_V1_H

void mem_cgroup_update_tree(struct mem_cgroup *memcg, int nid);
void mem_cgroup_remove_from_trees(struct mem_cgroup *memcg);

static inline void memcg1_soft_limit_reset(struct mem_cgroup *memcg)
{
	WRITE_ONCE(memcg->soft_limit, PAGE_COUNTER_MAX);
}

#endif	/* __MM_MEMCONTROL_V1_H */

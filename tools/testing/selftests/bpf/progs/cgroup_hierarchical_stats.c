// SPDX-License-Identifier: GPL-2.0-only
/*
 * Functions to manage eBPF programs attached to cgroup subsystems
 *
 * Copyright 2022 Google LLC.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

/*
 * Start times are stored per-task, not per-cgroup, as multiple tasks in one
 * cgroup can perform reclaim concurrently.
 */
struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, __u64);
} vmscan_start_time SEC(".maps");

struct vmscan_percpu {
	/* Previous percpu state, to figure out if we have new updates */
	__u64 prev;
	/* Current percpu state */
	__u64 state;
};

struct vmscan {
	/* State propagated through children, pending aggregation */
	__u64 pending;
	/* Total state, including all cpus and all children */
	__u64 state;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 100);
	__type(key, __u64);
	__type(value, struct vmscan_percpu);
} pcpu_cgroup_vmscan_elapsed SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 100);
	__type(key, __u64);
	__type(value, struct vmscan);
} cgroup_vmscan_elapsed SEC(".maps");

extern void cgroup_rstat_updated(struct cgroup *cgrp, int cpu) __ksym;
extern void cgroup_rstat_flush(struct cgroup *cgrp) __ksym;

static struct cgroup *task_memcg(struct task_struct *task)
{
	int cgrp_id;

#if __has_builtin(__builtin_preserve_enum_value)
	cgrp_id = bpf_core_enum_value(enum cgroup_subsys_id, memory_cgrp_id);
#else
	cgrp_id = memory_cgrp_id;
#endif
	return task->cgroups->subsys[cgrp_id]->cgroup;
}

static uint64_t cgroup_id(struct cgroup *cgrp)
{
	return cgrp->kn->id;
}

static int create_vmscan_percpu_elem(__u64 cg_id, __u64 state)
{
	struct vmscan_percpu pcpu_init = {.state = state, .prev = 0};

	return bpf_map_update_elem(&pcpu_cgroup_vmscan_elapsed, &cg_id,
				   &pcpu_init, BPF_NOEXIST);
}

static int create_vmscan_elem(__u64 cg_id, __u64 state, __u64 pending)
{
	struct vmscan init = {.state = state, .pending = pending};

	return bpf_map_update_elem(&cgroup_vmscan_elapsed, &cg_id,
				   &init, BPF_NOEXIST);
}

SEC("tp_btf/mm_vmscan_memcg_reclaim_begin")
int BPF_PROG(vmscan_start, int order, gfp_t gfp_flags)
{
	struct task_struct *task = bpf_get_current_task_btf();
	__u64 *start_time_ptr;

	start_time_ptr = bpf_task_storage_get(&vmscan_start_time, task, 0,
					      BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (start_time_ptr)
		*start_time_ptr = bpf_ktime_get_ns();
	return 0;
}

SEC("tp_btf/mm_vmscan_memcg_reclaim_end")
int BPF_PROG(vmscan_end, unsigned long nr_reclaimed)
{
	struct vmscan_percpu *pcpu_stat;
	struct task_struct *current = bpf_get_current_task_btf();
	struct cgroup *cgrp;
	__u64 *start_time_ptr;
	__u64 current_elapsed, cg_id;
	__u64 end_time = bpf_ktime_get_ns();

	/*
	 * cgrp is the first parent cgroup of current that has memcg enabled in
	 * its subtree_control, or NULL if memcg is disabled in the entire tree.
	 * In a cgroup hierarchy like this:
	 *                               a
	 *                              / \
	 *                             b   c
	 *  If "a" has memcg enabled, while "b" doesn't, then processes in "b"
	 *  will accumulate their stats directly to "a". This makes sure that no
	 *  stats are lost from processes in leaf cgroups that don't have memcg
	 *  enabled, but only exposes stats for cgroups that have memcg enabled.
	 */
	cgrp = task_memcg(current);
	if (!cgrp)
		return 0;

	cg_id = cgroup_id(cgrp);
	start_time_ptr = bpf_task_storage_get(&vmscan_start_time, current, 0,
					      BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!start_time_ptr)
		return 0;

	current_elapsed = end_time - *start_time_ptr;
	pcpu_stat = bpf_map_lookup_elem(&pcpu_cgroup_vmscan_elapsed,
					&cg_id);
	if (pcpu_stat)
		pcpu_stat->state += current_elapsed;
	else if (create_vmscan_percpu_elem(cg_id, current_elapsed))
		return 0;

	cgroup_rstat_updated(cgrp, bpf_get_smp_processor_id());
	return 0;
}

SEC("fentry/bpf_rstat_flush")
int BPF_PROG(vmscan_flush, struct cgroup *cgrp, struct cgroup *parent, int cpu)
{
	struct vmscan_percpu *pcpu_stat;
	struct vmscan *total_stat, *parent_stat;
	__u64 cg_id = cgroup_id(cgrp);
	__u64 parent_cg_id = parent ? cgroup_id(parent) : 0;
	__u64 *pcpu_vmscan;
	__u64 state;
	__u64 delta = 0;

	/* Add CPU changes on this level since the last flush */
	pcpu_stat = bpf_map_lookup_percpu_elem(&pcpu_cgroup_vmscan_elapsed,
					       &cg_id, cpu);
	if (pcpu_stat) {
		state = pcpu_stat->state;
		delta += state - pcpu_stat->prev;
		pcpu_stat->prev = state;
	}

	total_stat = bpf_map_lookup_elem(&cgroup_vmscan_elapsed, &cg_id);
	if (!total_stat) {
		if (create_vmscan_elem(cg_id, delta, 0))
			return 0;

		goto update_parent;
	}

	/* Collect pending stats from subtree */
	if (total_stat->pending) {
		delta += total_stat->pending;
		total_stat->pending = 0;
	}

	/* Propagate changes to this cgroup's total */
	total_stat->state += delta;

update_parent:
	/* Skip if there are no changes to propagate, or no parent */
	if (!delta || !parent_cg_id)
		return 0;

	/* Propagate changes to cgroup's parent */
	parent_stat = bpf_map_lookup_elem(&cgroup_vmscan_elapsed,
					  &parent_cg_id);
	if (parent_stat)
		parent_stat->pending += delta;
	else
		create_vmscan_elem(parent_cg_id, 0, delta);
	return 0;
}

SEC("iter.s/cgroup")
int BPF_PROG(dump_vmscan, struct bpf_iter_meta *meta, struct cgroup *cgrp)
{
	struct seq_file *seq = meta->seq;
	struct vmscan *total_stat;
	__u64 cg_id = cgrp ? cgroup_id(cgrp) : 0;

	/* Do nothing for the terminal call */
	if (!cg_id)
		return 1;

	/* Flush the stats to make sure we get the most updated numbers */
	cgroup_rstat_flush(cgrp);

	total_stat = bpf_map_lookup_elem(&cgroup_vmscan_elapsed, &cg_id);
	if (!total_stat) {
		BPF_SEQ_PRINTF(seq, "cg_id: %llu, total_vmscan_delay: 0\n",
			       cg_id);
	} else {
		BPF_SEQ_PRINTF(seq, "cg_id: %llu, total_vmscan_delay: %llu\n",
			       cg_id, total_stat->state);
	}

	/*
	 * We only dump stats for one cgroup here, so return 1 to stop
	 * iteration after the first cgroup.
	 */
	return 1;
}

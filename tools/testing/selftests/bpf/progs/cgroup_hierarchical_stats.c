// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 Google LLC.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

struct percpu_attach_counter {
	/* Previous percpu state, to figure out if we have new updates */
	__u64 prev;
	/* Current percpu state */
	__u64 state;
};

struct attach_counter {
	/* State propagated through children, pending aggregation */
	__u64 pending;
	/* Total state, including all cpus and all children */
	__u64 state;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 1024);
	__type(key, __u64);
	__type(value, struct percpu_attach_counter);
} percpu_attach_counters SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, __u64);
	__type(value, struct attach_counter);
} attach_counters SEC(".maps");

extern void css_rstat_updated(
		struct cgroup_subsys_state *css, int cpu) __ksym;
extern void css_rstat_flush(struct cgroup_subsys_state *css) __ksym;

static uint64_t cgroup_id(struct cgroup *cgrp)
{
	return cgrp->kn->id;
}

static int create_percpu_attach_counter(__u64 cg_id, __u64 state)
{
	struct percpu_attach_counter pcpu_init = {.state = state, .prev = 0};

	return bpf_map_update_elem(&percpu_attach_counters, &cg_id,
				   &pcpu_init, BPF_NOEXIST);
}

static int create_attach_counter(__u64 cg_id, __u64 state, __u64 pending)
{
	struct attach_counter init = {.state = state, .pending = pending};

	return bpf_map_update_elem(&attach_counters, &cg_id,
				   &init, BPF_NOEXIST);
}

SEC("fentry/cgroup_attach_task")
int BPF_PROG(counter, struct cgroup *dst_cgrp, struct task_struct *leader,
	     bool threadgroup)
{
	__u64 cg_id = cgroup_id(dst_cgrp);
	struct percpu_attach_counter *pcpu_counter = bpf_map_lookup_elem(
			&percpu_attach_counters,
			&cg_id);

	if (pcpu_counter)
		pcpu_counter->state += 1;
	else if (create_percpu_attach_counter(cg_id, 1))
		return 0;

	css_rstat_updated(&dst_cgrp->self, bpf_get_smp_processor_id());
	return 0;
}

SEC("fentry/bpf_rstat_flush")
int BPF_PROG(flusher, struct cgroup *cgrp, struct cgroup *parent, int cpu)
{
	struct percpu_attach_counter *pcpu_counter;
	struct attach_counter *total_counter, *parent_counter;
	__u64 cg_id = cgroup_id(cgrp);
	__u64 parent_cg_id = parent ? cgroup_id(parent) : 0;
	__u64 state;
	__u64 delta = 0;

	/* Add CPU changes on this level since the last flush */
	pcpu_counter = bpf_map_lookup_percpu_elem(&percpu_attach_counters,
						  &cg_id, cpu);
	if (pcpu_counter) {
		state = pcpu_counter->state;
		delta += state - pcpu_counter->prev;
		pcpu_counter->prev = state;
	}

	total_counter = bpf_map_lookup_elem(&attach_counters, &cg_id);
	if (!total_counter) {
		if (create_attach_counter(cg_id, delta, 0))
			return 0;
		goto update_parent;
	}

	/* Collect pending stats from subtree */
	if (total_counter->pending) {
		delta += total_counter->pending;
		total_counter->pending = 0;
	}

	/* Propagate changes to this cgroup's total */
	total_counter->state += delta;

update_parent:
	/* Skip if there are no changes to propagate, or no parent */
	if (!delta || !parent_cg_id)
		return 0;

	/* Propagate changes to cgroup's parent */
	parent_counter = bpf_map_lookup_elem(&attach_counters,
					     &parent_cg_id);
	if (parent_counter)
		parent_counter->pending += delta;
	else
		create_attach_counter(parent_cg_id, 0, delta);
	return 0;
}

SEC("iter.s/cgroup")
int BPF_PROG(dumper, struct bpf_iter_meta *meta, struct cgroup *cgrp)
{
	struct seq_file *seq = meta->seq;
	struct attach_counter *total_counter;
	__u64 cg_id = cgrp ? cgroup_id(cgrp) : 0;

	/* Do nothing for the terminal call */
	if (!cg_id)
		return 1;

	/* Flush the stats to make sure we get the most updated numbers */
	css_rstat_flush(&cgrp->self);

	total_counter = bpf_map_lookup_elem(&attach_counters, &cg_id);
	if (!total_counter) {
		BPF_SEQ_PRINTF(seq, "cg_id: %llu, attach_counter: 0\n",
			       cg_id);
	} else {
		BPF_SEQ_PRINTF(seq, "cg_id: %llu, attach_counter: %llu\n",
			       cg_id, total_counter->state);
	}
	return 0;
}

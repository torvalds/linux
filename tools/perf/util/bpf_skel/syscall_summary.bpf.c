// SPDX-License-Identifier: GPL-2.0
/*
 * Trace raw_syscalls tracepoints to collect system call statistics.
 */

#include "vmlinux.h"
#include "syscall_summary.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* This is to calculate a delta between sys-enter and sys-exit for each thread */
struct syscall_trace {
	int nr; /* syscall number is only available at sys-enter */
	int unused;
	u64 timestamp;
};

#define MAX_ENTRIES	(128 * 1024)

struct syscall_trace_map {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int); /* tid */
	__type(value, struct syscall_trace);
	__uint(max_entries, MAX_ENTRIES);
} syscall_trace_map SEC(".maps");

struct syscall_stats_map {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, struct syscall_key);
	__type(value, struct syscall_stats);
	__uint(max_entries, MAX_ENTRIES);
} syscall_stats_map SEC(".maps");

int enabled; /* controlled from userspace */

const volatile enum syscall_aggr_mode aggr_mode;

static void update_stats(int cpu_or_tid, int nr, s64 duration, long ret)
{
	struct syscall_key key = { .cpu_or_tid = cpu_or_tid, .nr = nr, };
	struct syscall_stats *stats;

	stats = bpf_map_lookup_elem(&syscall_stats_map, &key);
	if (stats == NULL) {
		struct syscall_stats zero = {};

		bpf_map_update_elem(&syscall_stats_map, &key, &zero, BPF_NOEXIST);
		stats = bpf_map_lookup_elem(&syscall_stats_map, &key);
		if (stats == NULL)
			return;
	}

	__sync_fetch_and_add(&stats->count, 1);
	if (ret < 0)
		__sync_fetch_and_add(&stats->error, 1);

	if (duration > 0) {
		__sync_fetch_and_add(&stats->total_time, duration);
		__sync_fetch_and_add(&stats->squared_sum, duration * duration);
		if (stats->max_time < duration)
			stats->max_time = duration;
		if (stats->min_time > duration || stats->min_time == 0)
			stats->min_time = duration;
	}

	return;
}

SEC("tp_btf/sys_enter")
int sys_enter(u64 *ctx)
{
	int tid;
	struct syscall_trace st;

	if (!enabled)
		return 0;

	st.nr = ctx[1]; /* syscall number */
	st.unused = 0;
	st.timestamp = bpf_ktime_get_ns();

	tid = bpf_get_current_pid_tgid();
	bpf_map_update_elem(&syscall_trace_map, &tid, &st, BPF_ANY);

	return 0;
}

SEC("tp_btf/sys_exit")
int sys_exit(u64 *ctx)
{
	int tid;
	int key;
	long ret = ctx[1]; /* return value of the syscall */
	struct syscall_trace *st;
	s64 delta;

	if (!enabled)
		return 0;

	tid = bpf_get_current_pid_tgid();
	st = bpf_map_lookup_elem(&syscall_trace_map, &tid);
	if (st == NULL)
		return 0;

	if (aggr_mode == SYSCALL_AGGR_THREAD)
		key = tid;
	else
		key = bpf_get_smp_processor_id();

	delta = bpf_ktime_get_ns() - st->timestamp;
	update_stats(key, st->nr, delta, ret);

	bpf_map_delete_elem(&syscall_trace_map, &tid);
	return 0;
}

char _license[] SEC("license") = "GPL";

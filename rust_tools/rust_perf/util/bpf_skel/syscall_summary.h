// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Data structures shared between BPF and tools. */
#ifndef UTIL_BPF_SKEL_SYSCALL_SUMMARY_H
#define UTIL_BPF_SKEL_SYSCALL_SUMMARY_H

enum syscall_aggr_mode {
	SYSCALL_AGGR_THREAD,
	SYSCALL_AGGR_CPU,
	SYSCALL_AGGR_CGROUP,
};

struct syscall_key {
	u64 cgroup;
	int cpu_or_tid;
	int nr;
};

struct syscall_stats {
	u64 total_time;
	u64 squared_sum;
	u64 max_time;
	u64 min_time;
	u32 count;
	u32 error;
};

#endif /* UTIL_BPF_SKEL_SYSCALL_SUMMARY_H */

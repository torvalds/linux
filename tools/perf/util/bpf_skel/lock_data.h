// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Data structures shared between BPF and tools. */
#ifndef UTIL_BPF_SKEL_LOCK_DATA_H
#define UTIL_BPF_SKEL_LOCK_DATA_H

struct contention_key {
	u64 aggr_key;  /* can be stack_id, pid or lock addr */
};

#define TASK_COMM_LEN  16

struct contention_task_data {
	char comm[TASK_COMM_LEN];
};

struct contention_data {
	u64 total_time;
	u64 min_time;
	u64 max_time;
	u32 count;
	u32 flags;
};

enum lock_aggr_mode {
	LOCK_AGGR_ADDR = 0,
	LOCK_AGGR_TASK,
	LOCK_AGGR_CALLER,
};

#endif /* UTIL_BPF_SKEL_LOCK_DATA_H */

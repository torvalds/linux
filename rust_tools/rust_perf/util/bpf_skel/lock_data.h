// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Data structures shared between BPF and tools. */
#ifndef UTIL_BPF_SKEL_LOCK_DATA_H
#define UTIL_BPF_SKEL_LOCK_DATA_H

struct owner_tracing_data {
	u32 pid; // Who has the lock.
	u32 count; // How many waiters for this lock.
	u64 timestamp; // The time while the owner acquires lock and contention is going on.
	s32 stack_id; // Identifier for `owner_stat`, which stores as value in `owner_stacks`
};

struct tstamp_data {
	u64 timestamp;
	u64 lock;
	u32 flags;
	s32 stack_id;
};

struct contention_key {
	s32 stack_id;
	u32 pid;
	u64 lock_addr_or_cgroup;
};

#define TASK_COMM_LEN  16

struct contention_task_data {
	char comm[TASK_COMM_LEN];
};

/* default buffer size */
#define MAX_ENTRIES  16384

/*
 * Upper bits of the flags in the contention_data are used to identify
 * some well-known locks which do not have symbols (non-global locks).
 */
#define LCD_F_MMAP_LOCK		(1U << 31)
#define LCD_F_SIGHAND_LOCK	(1U << 30)

#define LCB_F_SLAB_ID_SHIFT	16
#define LCB_F_SLAB_ID_START	(1U << 16)
#define LCB_F_SLAB_ID_END	(1U << 26)
#define LCB_F_SLAB_ID_MASK	0x03FF0000U

#define LCB_F_TYPE_MAX		(1U << 7)
#define LCB_F_TYPE_MASK		0x0000007FU

#define SLAB_NAME_MAX  28

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
	LOCK_AGGR_CGROUP,
};

enum lock_class_sym {
	LOCK_CLASS_NONE,
	LOCK_CLASS_RQLOCK,
	LOCK_CLASS_ZONE_LOCK,
};

struct slab_cache_data {
	u32 id;
	char name[SLAB_NAME_MAX];
};

#endif /* UTIL_BPF_SKEL_LOCK_DATA_H */

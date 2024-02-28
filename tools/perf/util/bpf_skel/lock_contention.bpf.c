// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2022 Google
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <asm-generic/errno-base.h>

#include "lock_data.h"

/* for collect_lock_syms().  4096 was rejected by the verifier */
#define MAX_CPUS  1024

/* lock contention flags from include/trace/events/lock.h */
#define LCB_F_SPIN	(1U << 0)
#define LCB_F_READ	(1U << 1)
#define LCB_F_WRITE	(1U << 2)
#define LCB_F_RT	(1U << 3)
#define LCB_F_PERCPU	(1U << 4)
#define LCB_F_MUTEX	(1U << 5)

/* callstack storage  */
struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, MAX_ENTRIES);
} stacks SEC(".maps");

/* maintain timestamp at the beginning of contention */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, struct tstamp_data);
	__uint(max_entries, MAX_ENTRIES);
} tstamp SEC(".maps");

/* maintain per-CPU timestamp at the beginning of contention */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct tstamp_data));
	__uint(max_entries, 1);
} tstamp_cpu SEC(".maps");

/* actual lock contention statistics */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct contention_key));
	__uint(value_size, sizeof(struct contention_data));
	__uint(max_entries, MAX_ENTRIES);
} lock_stat SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct contention_task_data));
	__uint(max_entries, MAX_ENTRIES);
} task_data SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u64));
	__uint(value_size, sizeof(__u32));
	__uint(max_entries, MAX_ENTRIES);
} lock_syms SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} cpu_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} task_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} type_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u64));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} addr_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u64));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} cgroup_filter SEC(".maps");

struct rw_semaphore___old {
	struct task_struct *owner;
} __attribute__((preserve_access_index));

struct rw_semaphore___new {
	atomic_long_t owner;
} __attribute__((preserve_access_index));

struct mm_struct___old {
	struct rw_semaphore mmap_sem;
} __attribute__((preserve_access_index));

struct mm_struct___new {
	struct rw_semaphore mmap_lock;
} __attribute__((preserve_access_index));

/* control flags */
int enabled;
int has_cpu;
int has_task;
int has_type;
int has_addr;
int has_cgroup;
int needs_callstack;
int stack_skip;
int lock_owner;

int use_cgroup_v2;
int perf_subsys_id = -1;

/* determine the key of lock stat */
int aggr_mode;

__u64 end_ts;

/* error stat */
int task_fail;
int stack_fail;
int time_fail;
int data_fail;

int task_map_full;
int data_map_full;

static inline __u64 get_current_cgroup_id(void)
{
	struct task_struct *task;
	struct cgroup *cgrp;

	if (use_cgroup_v2)
		return bpf_get_current_cgroup_id();

	task = bpf_get_current_task_btf();

	if (perf_subsys_id == -1) {
#if __has_builtin(__builtin_preserve_enum_value)
		perf_subsys_id = bpf_core_enum_value(enum cgroup_subsys_id,
						     perf_event_cgrp_id);
#else
		perf_subsys_id = perf_event_cgrp_id;
#endif
	}

	cgrp = BPF_CORE_READ(task, cgroups, subsys[perf_subsys_id], cgroup);
	return BPF_CORE_READ(cgrp, kn, id);
}

static inline int can_record(u64 *ctx)
{
	if (has_cpu) {
		__u32 cpu = bpf_get_smp_processor_id();
		__u8 *ok;

		ok = bpf_map_lookup_elem(&cpu_filter, &cpu);
		if (!ok)
			return 0;
	}

	if (has_task) {
		__u8 *ok;
		__u32 pid = bpf_get_current_pid_tgid();

		ok = bpf_map_lookup_elem(&task_filter, &pid);
		if (!ok)
			return 0;
	}

	if (has_type) {
		__u8 *ok;
		__u32 flags = (__u32)ctx[1];

		ok = bpf_map_lookup_elem(&type_filter, &flags);
		if (!ok)
			return 0;
	}

	if (has_addr) {
		__u8 *ok;
		__u64 addr = ctx[0];

		ok = bpf_map_lookup_elem(&addr_filter, &addr);
		if (!ok)
			return 0;
	}

	if (has_cgroup) {
		__u8 *ok;
		__u64 cgrp = get_current_cgroup_id();

		ok = bpf_map_lookup_elem(&cgroup_filter, &cgrp);
		if (!ok)
			return 0;
	}

	return 1;
}

static inline int update_task_data(struct task_struct *task)
{
	struct contention_task_data *p;
	int pid, err;

	err = bpf_core_read(&pid, sizeof(pid), &task->pid);
	if (err)
		return -1;

	p = bpf_map_lookup_elem(&task_data, &pid);
	if (p == NULL && !task_map_full) {
		struct contention_task_data data = {};

		BPF_CORE_READ_STR_INTO(&data.comm, task, comm);
		if (bpf_map_update_elem(&task_data, &pid, &data, BPF_NOEXIST) == -E2BIG)
			task_map_full = 1;
	}

	return 0;
}

#ifndef __has_builtin
# define __has_builtin(x) 0
#endif

static inline struct task_struct *get_lock_owner(__u64 lock, __u32 flags)
{
	struct task_struct *task;
	__u64 owner = 0;

	if (flags & LCB_F_MUTEX) {
		struct mutex *mutex = (void *)lock;
		owner = BPF_CORE_READ(mutex, owner.counter);
	} else if (flags == LCB_F_READ || flags == LCB_F_WRITE) {
	/*
	 * Support for the BPF_TYPE_MATCHES argument to the
	 * __builtin_preserve_type_info builtin was added at some point during
	 * development of clang 15 and it's what is needed for
	 * bpf_core_type_matches.
	 */
#if __has_builtin(__builtin_preserve_type_info) && __clang_major__ >= 15
		if (bpf_core_type_matches(struct rw_semaphore___old)) {
			struct rw_semaphore___old *rwsem = (void *)lock;
			owner = (unsigned long)BPF_CORE_READ(rwsem, owner);
		} else if (bpf_core_type_matches(struct rw_semaphore___new)) {
			struct rw_semaphore___new *rwsem = (void *)lock;
			owner = BPF_CORE_READ(rwsem, owner.counter);
		}
#else
		/* assume new struct */
		struct rw_semaphore *rwsem = (void *)lock;
		owner = BPF_CORE_READ(rwsem, owner.counter);
#endif
	}

	if (!owner)
		return NULL;

	task = (void *)(owner & ~7UL);
	return task;
}

static inline __u32 check_lock_type(__u64 lock, __u32 flags)
{
	struct task_struct *curr;
	struct mm_struct___old *mm_old;
	struct mm_struct___new *mm_new;

	switch (flags) {
	case LCB_F_READ:  /* rwsem */
	case LCB_F_WRITE:
		curr = bpf_get_current_task_btf();
		if (curr->mm == NULL)
			break;
		mm_new = (void *)curr->mm;
		if (bpf_core_field_exists(mm_new->mmap_lock)) {
			if (&mm_new->mmap_lock == (void *)lock)
				return LCD_F_MMAP_LOCK;
			break;
		}
		mm_old = (void *)curr->mm;
		if (bpf_core_field_exists(mm_old->mmap_sem)) {
			if (&mm_old->mmap_sem == (void *)lock)
				return LCD_F_MMAP_LOCK;
		}
		break;
	case LCB_F_SPIN:  /* spinlock */
		curr = bpf_get_current_task_btf();
		if (&curr->sighand->siglock == (void *)lock)
			return LCD_F_SIGHAND_LOCK;
		break;
	default:
		break;
	}
	return 0;
}

static inline struct tstamp_data *get_tstamp_elem(__u32 flags)
{
	__u32 pid;
	struct tstamp_data *pelem;

	/* Use per-cpu array map for spinlock and rwlock */
	if (flags == (LCB_F_SPIN | LCB_F_READ) || flags == LCB_F_SPIN ||
	    flags == (LCB_F_SPIN | LCB_F_WRITE)) {
		__u32 idx = 0;

		pelem = bpf_map_lookup_elem(&tstamp_cpu, &idx);
		/* Do not update the element for nested locks */
		if (pelem && pelem->lock)
			pelem = NULL;
		return pelem;
	}

	pid = bpf_get_current_pid_tgid();
	pelem = bpf_map_lookup_elem(&tstamp, &pid);
	/* Do not update the element for nested locks */
	if (pelem && pelem->lock)
		return NULL;

	if (pelem == NULL) {
		struct tstamp_data zero = {};

		if (bpf_map_update_elem(&tstamp, &pid, &zero, BPF_NOEXIST) < 0) {
			__sync_fetch_and_add(&task_fail, 1);
			return NULL;
		}

		pelem = bpf_map_lookup_elem(&tstamp, &pid);
		if (pelem == NULL) {
			__sync_fetch_and_add(&task_fail, 1);
			return NULL;
		}
	}
	return pelem;
}

SEC("tp_btf/contention_begin")
int contention_begin(u64 *ctx)
{
	struct tstamp_data *pelem;

	if (!enabled || !can_record(ctx))
		return 0;

	pelem = get_tstamp_elem(ctx[1]);
	if (pelem == NULL)
		return 0;

	pelem->timestamp = bpf_ktime_get_ns();
	pelem->lock = (__u64)ctx[0];
	pelem->flags = (__u32)ctx[1];

	if (needs_callstack) {
		pelem->stack_id = bpf_get_stackid(ctx, &stacks,
						  BPF_F_FAST_STACK_CMP | stack_skip);
		if (pelem->stack_id < 0)
			__sync_fetch_and_add(&stack_fail, 1);
	} else if (aggr_mode == LOCK_AGGR_TASK) {
		struct task_struct *task;

		if (lock_owner) {
			task = get_lock_owner(pelem->lock, pelem->flags);

			/* The flags is not used anymore.  Pass the owner pid. */
			if (task)
				pelem->flags = BPF_CORE_READ(task, pid);
			else
				pelem->flags = -1U;

		} else {
			task = bpf_get_current_task_btf();
		}

		if (task) {
			if (update_task_data(task) < 0 && lock_owner)
				pelem->flags = -1U;
		}
	}

	return 0;
}

SEC("tp_btf/contention_end")
int contention_end(u64 *ctx)
{
	__u32 pid = 0, idx = 0;
	struct tstamp_data *pelem;
	struct contention_key key = {};
	struct contention_data *data;
	__u64 duration;
	bool need_delete = false;

	if (!enabled)
		return 0;

	/*
	 * For spinlock and rwlock, it needs to get the timestamp for the
	 * per-cpu map.  However, contention_end does not have the flags
	 * so it cannot know whether it reads percpu or hash map.
	 *
	 * Try per-cpu map first and check if there's active contention.
	 * If it is, do not read hash map because it cannot go to sleeping
	 * locks before releasing the spinning locks.
	 */
	pelem = bpf_map_lookup_elem(&tstamp_cpu, &idx);
	if (pelem && pelem->lock) {
		if (pelem->lock != ctx[0])
			return 0;
	} else {
		pid = bpf_get_current_pid_tgid();
		pelem = bpf_map_lookup_elem(&tstamp, &pid);
		if (!pelem || pelem->lock != ctx[0])
			return 0;
		need_delete = true;
	}

	duration = bpf_ktime_get_ns() - pelem->timestamp;
	if ((__s64)duration < 0) {
		pelem->lock = 0;
		if (need_delete)
			bpf_map_delete_elem(&tstamp, &pid);
		__sync_fetch_and_add(&time_fail, 1);
		return 0;
	}

	switch (aggr_mode) {
	case LOCK_AGGR_CALLER:
		key.stack_id = pelem->stack_id;
		break;
	case LOCK_AGGR_TASK:
		if (lock_owner)
			key.pid = pelem->flags;
		else {
			if (!need_delete)
				pid = bpf_get_current_pid_tgid();
			key.pid = pid;
		}
		if (needs_callstack)
			key.stack_id = pelem->stack_id;
		break;
	case LOCK_AGGR_ADDR:
		key.lock_addr_or_cgroup = pelem->lock;
		if (needs_callstack)
			key.stack_id = pelem->stack_id;
		break;
	case LOCK_AGGR_CGROUP:
		key.lock_addr_or_cgroup = get_current_cgroup_id();
		break;
	default:
		/* should not happen */
		return 0;
	}

	data = bpf_map_lookup_elem(&lock_stat, &key);
	if (!data) {
		if (data_map_full) {
			pelem->lock = 0;
			if (need_delete)
				bpf_map_delete_elem(&tstamp, &pid);
			__sync_fetch_and_add(&data_fail, 1);
			return 0;
		}

		struct contention_data first = {
			.total_time = duration,
			.max_time = duration,
			.min_time = duration,
			.count = 1,
			.flags = pelem->flags,
		};
		int err;

		if (aggr_mode == LOCK_AGGR_ADDR)
			first.flags |= check_lock_type(pelem->lock, pelem->flags);

		err = bpf_map_update_elem(&lock_stat, &key, &first, BPF_NOEXIST);
		if (err < 0) {
			if (err == -E2BIG)
				data_map_full = 1;
			__sync_fetch_and_add(&data_fail, 1);
		}
		pelem->lock = 0;
		if (need_delete)
			bpf_map_delete_elem(&tstamp, &pid);
		return 0;
	}

	__sync_fetch_and_add(&data->total_time, duration);
	__sync_fetch_and_add(&data->count, 1);

	/* FIXME: need atomic operations */
	if (data->max_time < duration)
		data->max_time = duration;
	if (data->min_time > duration)
		data->min_time = duration;

	pelem->lock = 0;
	if (need_delete)
		bpf_map_delete_elem(&tstamp, &pid);
	return 0;
}

extern struct rq runqueues __ksym;

struct rq___old {
	raw_spinlock_t lock;
} __attribute__((preserve_access_index));

struct rq___new {
	raw_spinlock_t __lock;
} __attribute__((preserve_access_index));

SEC("raw_tp/bpf_test_finish")
int BPF_PROG(collect_lock_syms)
{
	__u64 lock_addr, lock_off;
	__u32 lock_flag;

	if (bpf_core_field_exists(struct rq___new, __lock))
		lock_off = offsetof(struct rq___new, __lock);
	else
		lock_off = offsetof(struct rq___old, lock);

	for (int i = 0; i < MAX_CPUS; i++) {
		struct rq *rq = bpf_per_cpu_ptr(&runqueues, i);

		if (rq == NULL)
			break;

		lock_addr = (__u64)(void *)rq + lock_off;
		lock_flag = LOCK_CLASS_RQLOCK;
		bpf_map_update_elem(&lock_syms, &lock_addr, &lock_flag, BPF_ANY);
	}
	return 0;
}

SEC("raw_tp/bpf_test_finish")
int BPF_PROG(end_timestamp)
{
	end_ts = bpf_ktime_get_ns();
	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";

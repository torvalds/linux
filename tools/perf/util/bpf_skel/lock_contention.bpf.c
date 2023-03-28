// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2022 Google
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#include "lock_data.h"

/* default buffer size */
#define MAX_ENTRIES  10240

/* lock contention flags from include/trace/events/lock.h */
#define LCB_F_SPIN	(1U << 0)
#define LCB_F_READ	(1U << 1)
#define LCB_F_WRITE	(1U << 2)
#define LCB_F_RT	(1U << 3)
#define LCB_F_PERCPU	(1U << 4)
#define LCB_F_MUTEX	(1U << 5)

struct tstamp_data {
	__u64 timestamp;
	__u64 lock;
	__u32 flags;
	__s32 stack_id;
};

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

struct rw_semaphore___old {
	struct task_struct *owner;
} __attribute__((preserve_access_index));

struct rw_semaphore___new {
	atomic_long_t owner;
} __attribute__((preserve_access_index));

/* control flags */
int enabled;
int has_cpu;
int has_task;
int has_type;
int has_addr;
int needs_callstack;
int stack_skip;
int lock_owner;

/* determine the key of lock stat */
int aggr_mode;

/* error stat */
int lost;

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
	if (p == NULL) {
		struct contention_task_data data = {};

		BPF_CORE_READ_STR_INTO(&data.comm, task, comm);
		bpf_map_update_elem(&task_data, &pid, &data, BPF_NOEXIST);
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
#if __has_builtin(bpf_core_type_matches)
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

SEC("tp_btf/contention_begin")
int contention_begin(u64 *ctx)
{
	__u32 pid;
	struct tstamp_data *pelem;

	if (!enabled || !can_record(ctx))
		return 0;

	pid = bpf_get_current_pid_tgid();
	pelem = bpf_map_lookup_elem(&tstamp, &pid);
	if (pelem && pelem->lock)
		return 0;

	if (pelem == NULL) {
		struct tstamp_data zero = {};

		bpf_map_update_elem(&tstamp, &pid, &zero, BPF_ANY);
		pelem = bpf_map_lookup_elem(&tstamp, &pid);
		if (pelem == NULL) {
			lost++;
			return 0;
		}
	}

	pelem->timestamp = bpf_ktime_get_ns();
	pelem->lock = (__u64)ctx[0];
	pelem->flags = (__u32)ctx[1];

	if (needs_callstack) {
		pelem->stack_id = bpf_get_stackid(ctx, &stacks,
						  BPF_F_FAST_STACK_CMP | stack_skip);
		if (pelem->stack_id < 0)
			lost++;
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
	__u32 pid;
	struct tstamp_data *pelem;
	struct contention_key key = {};
	struct contention_data *data;
	__u64 duration;

	if (!enabled)
		return 0;

	pid = bpf_get_current_pid_tgid();
	pelem = bpf_map_lookup_elem(&tstamp, &pid);
	if (!pelem || pelem->lock != ctx[0])
		return 0;

	duration = bpf_ktime_get_ns() - pelem->timestamp;

	switch (aggr_mode) {
	case LOCK_AGGR_CALLER:
		key.stack_id = pelem->stack_id;
		break;
	case LOCK_AGGR_TASK:
		if (lock_owner)
			key.pid = pelem->flags;
		else
			key.pid = pid;
		if (needs_callstack)
			key.stack_id = pelem->stack_id;
		break;
	case LOCK_AGGR_ADDR:
		key.lock_addr = pelem->lock;
		if (needs_callstack)
			key.stack_id = pelem->stack_id;
		break;
	default:
		/* should not happen */
		return 0;
	}

	data = bpf_map_lookup_elem(&lock_stat, &key);
	if (!data) {
		struct contention_data first = {
			.total_time = duration,
			.max_time = duration,
			.min_time = duration,
			.count = 1,
			.flags = pelem->flags,
		};

		bpf_map_update_elem(&lock_stat, &key, &first, BPF_NOEXIST);
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

	bpf_map_delete_elem(&tstamp, &pid);
	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";

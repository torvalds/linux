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

/* buffer for owner stacktrace */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, 1);
} stack_buf SEC(".maps");

/* a map for tracing owner stacktrace to owner stack id */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u64)); // owner stacktrace
	__uint(value_size, sizeof(__s32)); // owner stack id
	__uint(max_entries, 1);
} owner_stacks SEC(".maps");

/* a map for tracing lock address to owner data */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u64)); // lock address
	__uint(value_size, sizeof(struct owner_tracing_data));
	__uint(max_entries, 1);
} owner_data SEC(".maps");

/* a map for contention_key (stores owner stack id) to contention data */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct contention_key));
	__uint(value_size, sizeof(struct contention_data));
	__uint(max_entries, 1);
} owner_stat SEC(".maps");

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

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(long));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} slab_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(long));
	__uint(value_size, sizeof(struct slab_cache_data));
	__uint(max_entries, 1);
} slab_caches SEC(".maps");

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

extern struct kmem_cache *bpf_get_kmem_cache(u64 addr) __ksym __weak;

/* control flags */
const volatile int has_cpu;
const volatile int has_task;
const volatile int has_type;
const volatile int has_addr;
const volatile int has_cgroup;
const volatile int has_slab;
const volatile int needs_callstack;
const volatile int stack_skip;
const volatile int lock_owner;
const volatile int use_cgroup_v2;
const volatile int max_stack;

/* determine the key of lock stat */
const volatile int aggr_mode;

int enabled;

int perf_subsys_id = -1;

__u64 end_ts;

__u32 slab_cache_id;

/* error stat */
int task_fail;
int stack_fail;
int time_fail;
int data_fail;

int task_map_full;
int data_map_full;

struct task_struct *bpf_task_from_pid(s32 pid) __ksym __weak;
void bpf_task_release(struct task_struct *p) __ksym __weak;

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
		if (!ok && !has_slab)
			return 0;
	}

	if (has_cgroup) {
		__u8 *ok;
		__u64 cgrp = get_current_cgroup_id();

		ok = bpf_map_lookup_elem(&cgroup_filter, &cgrp);
		if (!ok)
			return 0;
	}

	if (has_slab && bpf_get_kmem_cache) {
		__u8 *ok;
		__u64 addr = ctx[0];
		long kmem_cache_addr;

		kmem_cache_addr = (long)bpf_get_kmem_cache(addr);
		ok = bpf_map_lookup_elem(&slab_filter, &kmem_cache_addr);
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
	struct sighand_struct *sighand;

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
		sighand = curr->sighand;

		if (sighand && &sighand->siglock == (void *)lock)
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
	if ((flags & (LCB_F_SPIN | LCB_F_MUTEX)) == LCB_F_SPIN) {
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

static inline s32 get_owner_stack_id(u64 *stacktrace)
{
	s32 *id, new_id;
	static s64 id_gen = 1;

	id = bpf_map_lookup_elem(&owner_stacks, stacktrace);
	if (id)
		return *id;

	new_id = (s32)__sync_fetch_and_add(&id_gen, 1);

	bpf_map_update_elem(&owner_stacks, stacktrace, &new_id, BPF_NOEXIST);

	id = bpf_map_lookup_elem(&owner_stacks, stacktrace);
	if (id)
		return *id;

	return -1;
}

static inline void update_contention_data(struct contention_data *data, u64 duration, u32 count)
{
	__sync_fetch_and_add(&data->total_time, duration);
	__sync_fetch_and_add(&data->count, count);

	/* FIXME: need atomic operations */
	if (data->max_time < duration)
		data->max_time = duration;
	if (data->min_time > duration)
		data->min_time = duration;
}

static inline void update_owner_stat(u32 id, u64 duration, u32 flags)
{
	struct contention_key key = {
		.stack_id = id,
		.pid = 0,
		.lock_addr_or_cgroup = 0,
	};
	struct contention_data *data = bpf_map_lookup_elem(&owner_stat, &key);

	if (!data) {
		struct contention_data first = {
			.total_time = duration,
			.max_time = duration,
			.min_time = duration,
			.count = 1,
			.flags = flags,
		};
		bpf_map_update_elem(&owner_stat, &key, &first, BPF_NOEXIST);
	} else {
		update_contention_data(data, duration, 1);
	}
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
		u32 i = 0;
		u32 id = 0;
		int owner_pid;
		u64 *buf;
		struct task_struct *task;
		struct owner_tracing_data *otdata;

		if (!lock_owner)
			goto skip_owner;

		task = get_lock_owner(pelem->lock, pelem->flags);
		if (!task)
			goto skip_owner;

		owner_pid = BPF_CORE_READ(task, pid);

		buf = bpf_map_lookup_elem(&stack_buf, &i);
		if (!buf)
			goto skip_owner;
		for (i = 0; i < max_stack; i++)
			buf[i] = 0x0;

		if (!bpf_task_from_pid)
			goto skip_owner;

		task = bpf_task_from_pid(owner_pid);
		if (!task)
			goto skip_owner;

		bpf_get_task_stack(task, buf, max_stack * sizeof(unsigned long), 0);
		bpf_task_release(task);

		otdata = bpf_map_lookup_elem(&owner_data, &pelem->lock);
		id = get_owner_stack_id(buf);

		/*
		 * Contention just happens, or corner case `lock` is owned by process not
		 * `owner_pid`. For the corner case we treat it as unexpected internal error and
		 * just ignore the precvious tracing record.
		 */
		if (!otdata || otdata->pid != owner_pid) {
			struct owner_tracing_data first = {
				.pid = owner_pid,
				.timestamp = pelem->timestamp,
				.count = 1,
				.stack_id = id,
			};
			bpf_map_update_elem(&owner_data, &pelem->lock, &first, BPF_ANY);
		}
		/* Contention is ongoing and new waiter joins */
		else {
			__sync_fetch_and_add(&otdata->count, 1);

			/*
			 * The owner is the same, but stacktrace might be changed. In this case we
			 * store/update `owner_stat` based on current owner stack id.
			 */
			if (id != otdata->stack_id) {
				update_owner_stat(id, pelem->timestamp - otdata->timestamp,
						  pelem->flags);

				otdata->timestamp = pelem->timestamp;
				otdata->stack_id = id;
			}
		}
skip_owner:
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
	__u64 timestamp;
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

	timestamp = bpf_ktime_get_ns();
	duration = timestamp - pelem->timestamp;
	if ((__s64)duration < 0) {
		__sync_fetch_and_add(&time_fail, 1);
		goto out;
	}

	if (needs_callstack && lock_owner) {
		struct owner_tracing_data *otdata = bpf_map_lookup_elem(&owner_data, &pelem->lock);

		if (!otdata)
			goto skip_owner;

		/* Update `owner_stat` */
		update_owner_stat(otdata->stack_id, timestamp - otdata->timestamp, pelem->flags);

		/* No contention is occurring, delete `lock` entry in `owner_data` */
		if (otdata->count <= 1)
			bpf_map_delete_elem(&owner_data, &pelem->lock);
		/*
		 * Contention is still ongoing, with a new owner (current task). `owner_data`
		 * should be updated accordingly.
		 */
		else {
			u32 i = 0;
			s32 ret = (s32)ctx[1];
			u64 *buf;

			otdata->timestamp = timestamp;
			__sync_fetch_and_add(&otdata->count, -1);

			buf = bpf_map_lookup_elem(&stack_buf, &i);
			if (!buf)
				goto skip_owner;
			for (i = 0; i < (u32)max_stack; i++)
				buf[i] = 0x0;

			/*
			 * `ret` has the return code of the lock function.
			 * If `ret` is negative, the current task terminates lock waiting without
			 * acquiring it. Owner is not changed, but we still need to update the owner
			 * stack.
			 */
			if (ret < 0) {
				s32 id = 0;
				struct task_struct *task;

				if (!bpf_task_from_pid)
					goto skip_owner;

				task = bpf_task_from_pid(otdata->pid);
				if (!task)
					goto skip_owner;

				bpf_get_task_stack(task, buf,
						   max_stack * sizeof(unsigned long), 0);
				bpf_task_release(task);

				id = get_owner_stack_id(buf);

				/*
				 * If owner stack is changed, update owner stack id for this lock.
				 */
				if (id != otdata->stack_id)
					otdata->stack_id = id;
			}
			/*
			 * Otherwise, update tracing data with the current task, which is the new
			 * owner.
			 */
			else {
				otdata->pid = pid;
				/*
				 * We don't want to retrieve callstack here, since it is where the
				 * current task acquires the lock and provides no additional
				 * information. We simply assign -1 to invalidate it.
				 */
				otdata->stack_id = -1;
			}
		}
	}
skip_owner:
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
			__sync_fetch_and_add(&data_fail, 1);
			goto out;
		}

		struct contention_data first = {
			.total_time = duration,
			.max_time = duration,
			.min_time = duration,
			.count = 1,
			.flags = pelem->flags,
		};
		int err;

		if (aggr_mode == LOCK_AGGR_ADDR) {
			first.flags |= check_lock_type(pelem->lock,
						       pelem->flags & LCB_F_TYPE_MASK);

			/* Check if it's from a slab object */
			if (bpf_get_kmem_cache) {
				struct kmem_cache *s;
				struct slab_cache_data *d;

				s = bpf_get_kmem_cache(pelem->lock);
				if (s != NULL) {
					/*
					 * Save the ID of the slab cache in the flags
					 * (instead of full address) to reduce the
					 * space in the contention_data.
					 */
					d = bpf_map_lookup_elem(&slab_caches, &s);
					if (d != NULL)
						first.flags |= d->id;
				}
			}
		}

		err = bpf_map_update_elem(&lock_stat, &key, &first, BPF_NOEXIST);
		if (err < 0) {
			if (err == -EEXIST) {
				/* it lost the race, try to get it again */
				data = bpf_map_lookup_elem(&lock_stat, &key);
				if (data != NULL)
					goto found;
			}
			if (err == -E2BIG)
				data_map_full = 1;
			__sync_fetch_and_add(&data_fail, 1);
		}
		goto out;
	}

found:
	update_contention_data(data, duration, 1);

out:
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

/*
 * bpf_iter__kmem_cache added recently so old kernels don't have it in the
 * vmlinux.h.  But we cannot add it here since it will cause a compiler error
 * due to redefinition of the struct on later kernels.
 *
 * So it uses a CO-RE trick to access the member only if it has the type.
 * This will support both old and new kernels without compiler errors.
 */
struct bpf_iter__kmem_cache___new {
	struct kmem_cache *s;
} __attribute__((preserve_access_index));

SEC("iter/kmem_cache")
int slab_cache_iter(void *ctx)
{
	struct kmem_cache *s = NULL;
	struct slab_cache_data d;
	const char *nameptr;

	if (bpf_core_type_exists(struct bpf_iter__kmem_cache)) {
		struct bpf_iter__kmem_cache___new *iter = ctx;

		s = iter->s;
	}

	if (s == NULL)
		return 0;

	nameptr = s->name;
	bpf_probe_read_kernel_str(d.name, sizeof(d.name), nameptr);

	d.id = ++slab_cache_id << LCB_F_SLAB_ID_SHIFT;
	if (d.id >= LCB_F_SLAB_ID_END)
		return 0;

	bpf_map_update_elem(&slab_caches, &s, &d, BPF_NOEXIST);
	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";

/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <uapi/linux/bpf.h>
#include <uapi/linux/ptrace.h>
#include <uapi/linux/perf_event.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define _(P)                                                                   \
	({                                                                     \
		typeof(P) val;                                                 \
		bpf_probe_read_kernel(&val, sizeof(val), &(P));                \
		val;                                                           \
	})

#define MINBLOCK_US	1
#define MAX_ENTRIES	10000

struct key_t {
	char waker[TASK_COMM_LEN];
	char target[TASK_COMM_LEN];
	u32 wret;
	u32 tret;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, struct key_t);
	__type(value, u64);
	__uint(max_entries, MAX_ENTRIES);
} counts SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, u64);
	__uint(max_entries, MAX_ENTRIES);
} start SEC(".maps");

struct wokeby_t {
	char name[TASK_COMM_LEN];
	u32 ret;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, struct wokeby_t);
	__uint(max_entries, MAX_ENTRIES);
} wokeby SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(key_size, sizeof(u32));
	__uint(value_size, PERF_MAX_STACK_DEPTH * sizeof(u64));
	__uint(max_entries, MAX_ENTRIES);
} stackmap SEC(".maps");

#define STACKID_FLAGS (0 | BPF_F_FAST_STACK_CMP)

SEC("kprobe/try_to_wake_up")
int waker(struct pt_regs *ctx)
{
	struct task_struct *p = (void *) PT_REGS_PARM1(ctx);
	struct wokeby_t woke;
	u32 pid;

	pid = _(p->pid);

	bpf_get_current_comm(&woke.name, sizeof(woke.name));
	woke.ret = bpf_get_stackid(ctx, &stackmap, STACKID_FLAGS);

	bpf_map_update_elem(&wokeby, &pid, &woke, BPF_ANY);
	return 0;
}

static inline int update_counts(void *ctx, u32 pid, u64 delta)
{
	struct wokeby_t *woke;
	u64 zero = 0, *val;
	struct key_t key;

	__builtin_memset(&key.waker, 0, sizeof(key.waker));
	bpf_get_current_comm(&key.target, sizeof(key.target));
	key.tret = bpf_get_stackid(ctx, &stackmap, STACKID_FLAGS);
	key.wret = 0;

	woke = bpf_map_lookup_elem(&wokeby, &pid);
	if (woke) {
		key.wret = woke->ret;
		__builtin_memcpy(&key.waker, woke->name, sizeof(key.waker));
		bpf_map_delete_elem(&wokeby, &pid);
	}

	val = bpf_map_lookup_elem(&counts, &key);
	if (!val) {
		bpf_map_update_elem(&counts, &key, &zero, BPF_NOEXIST);
		val = bpf_map_lookup_elem(&counts, &key);
		if (!val)
			return 0;
	}
	(*val) += delta;
	return 0;
}

#if 1
/* taken from /sys/kernel/debug/tracing/events/sched/sched_switch/format */
struct sched_switch_args {
	unsigned long long pad;
	char prev_comm[TASK_COMM_LEN];
	int prev_pid;
	int prev_prio;
	long long prev_state;
	char next_comm[TASK_COMM_LEN];
	int next_pid;
	int next_prio;
};
SEC("tracepoint/sched/sched_switch")
int oncpu(struct sched_switch_args *ctx)
{
	/* record previous thread sleep time */
	u32 pid = ctx->prev_pid;
#else
SEC("kprobe/finish_task_switch")
int oncpu(struct pt_regs *ctx)
{
	struct task_struct *p = (void *) PT_REGS_PARM1(ctx);
	/* record previous thread sleep time */
	u32 pid = _(p->pid);
#endif
	u64 delta, ts, *tsp;

	ts = bpf_ktime_get_ns();
	bpf_map_update_elem(&start, &pid, &ts, BPF_ANY);

	/* calculate current thread's delta time */
	pid = bpf_get_current_pid_tgid();
	tsp = bpf_map_lookup_elem(&start, &pid);
	if (!tsp)
		/* missed start or filtered */
		return 0;

	delta = bpf_ktime_get_ns() - *tsp;
	bpf_map_delete_elem(&start, &pid);
	delta = delta / 1000;
	if (delta < MINBLOCK_US)
		return 0;

	return update_counts(ctx, pid, delta);
}
char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;

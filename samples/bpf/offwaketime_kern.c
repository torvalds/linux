/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"
#include <uapi/linux/ptrace.h>
#include <uapi/linux/perf_event.h>
#include <linux/version.h>
#include <linux/sched.h>

#define _(P) ({typeof(P) val = 0; bpf_probe_read(&val, sizeof(val), &P); val;})

#define MINBLOCK_US	1

struct key_t {
	char waker[TASK_COMM_LEN];
	char target[TASK_COMM_LEN];
	u32 wret;
	u32 tret;
};

struct bpf_map_def SEC("maps") counts = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(struct key_t),
	.value_size = sizeof(u64),
	.max_entries = 10000,
};

struct bpf_map_def SEC("maps") start = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(u64),
	.max_entries = 10000,
};

struct wokeby_t {
	char name[TASK_COMM_LEN];
	u32 ret;
};

struct bpf_map_def SEC("maps") wokeby = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(struct wokeby_t),
	.max_entries = 10000,
};

struct bpf_map_def SEC("maps") stackmap = {
	.type = BPF_MAP_TYPE_STACK_TRACE,
	.key_size = sizeof(u32),
	.value_size = PERF_MAX_STACK_DEPTH * sizeof(u64),
	.max_entries = 10000,
};

#define STACKID_FLAGS (0 | BPF_F_FAST_STACK_CMP)

SEC("kprobe/try_to_wake_up")
int waker(struct pt_regs *ctx)
{
	struct task_struct *p = (void *) PT_REGS_PARM1(ctx);
	struct wokeby_t woke = {};
	u32 pid;

	pid = _(p->pid);

	bpf_get_current_comm(&woke.name, sizeof(woke.name));
	woke.ret = bpf_get_stackid(ctx, &stackmap, STACKID_FLAGS);

	bpf_map_update_elem(&wokeby, &pid, &woke, BPF_ANY);
	return 0;
}

static inline int update_counts(struct pt_regs *ctx, u32 pid, u64 delta)
{
	struct key_t key = {};
	struct wokeby_t *woke;
	u64 zero = 0, *val;

	bpf_get_current_comm(&key.target, sizeof(key.target));
	key.tret = bpf_get_stackid(ctx, &stackmap, STACKID_FLAGS);

	woke = bpf_map_lookup_elem(&wokeby, &pid);
	if (woke) {
		key.wret = woke->ret;
		__builtin_memcpy(&key.waker, woke->name, TASK_COMM_LEN);
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

SEC("kprobe/finish_task_switch")
int oncpu(struct pt_regs *ctx)
{
	struct task_struct *p = (void *) PT_REGS_PARM1(ctx);
	u64 delta, ts, *tsp;
	u32 pid;

	/* record previous thread sleep time */
	pid = _(p->pid);
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

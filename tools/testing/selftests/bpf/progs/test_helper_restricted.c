// SPDX-License-Identifier: GPL-2.0-only
#include <time.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct timer {
	struct bpf_timer t;
};

struct lock {
	struct bpf_spin_lock l;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct timer);
} timers SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct lock);
} locks SEC(".maps");

static int timer_cb(void *map, int *key, struct timer *timer)
{
	return 0;
}

static void timer_work(void)
{
	struct timer *timer;
	const int key = 0;

	timer  = bpf_map_lookup_elem(&timers, &key);
	if (timer) {
		bpf_timer_init(&timer->t, &timers, CLOCK_MONOTONIC);
		bpf_timer_set_callback(&timer->t, timer_cb);
		bpf_timer_start(&timer->t, 10E9, 0);
		bpf_timer_cancel(&timer->t);
	}
}

static void spin_lock_work(void)
{
	const int key = 0;
	struct lock *lock;

	lock = bpf_map_lookup_elem(&locks, &key);
	if (lock) {
		bpf_spin_lock(&lock->l);
		bpf_spin_unlock(&lock->l);
	}
}

SEC("raw_tp/sys_enter")
int raw_tp_timer(void *ctx)
{
	timer_work();

	return 0;
}

SEC("tp/syscalls/sys_enter_nanosleep")
int tp_timer(void *ctx)
{
	timer_work();

	return 0;
}

SEC("kprobe/sys_nanosleep")
int kprobe_timer(void *ctx)
{
	timer_work();

	return 0;
}

SEC("perf_event")
int perf_event_timer(void *ctx)
{
	timer_work();

	return 0;
}

SEC("raw_tp/sys_enter")
int raw_tp_spin_lock(void *ctx)
{
	spin_lock_work();

	return 0;
}

SEC("tp/syscalls/sys_enter_nanosleep")
int tp_spin_lock(void *ctx)
{
	spin_lock_work();

	return 0;
}

SEC("kprobe/sys_nanosleep")
int kprobe_spin_lock(void *ctx)
{
	spin_lock_work();

	return 0;
}

SEC("perf_event")
int perf_event_spin_lock(void *ctx)
{
	spin_lock_work();

	return 0;
}

const char LICENSE[] SEC("license") = "GPL";

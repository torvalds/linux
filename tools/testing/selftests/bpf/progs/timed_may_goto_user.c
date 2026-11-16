// SPDX-License-Identifier: GPL-2.0
/* The tests of timed_may_goto.c that need help from user space. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

#define TRIES 4
#define CLOCK_MONOTONIC 1

int zero;

/* Return 1 if the loop went on until the time was up. */
static __always_inline int spin(void)
{
	__u64 t0 = bpf_ktime_get_ns();

	while (can_loop)
		;
	return bpf_ktime_get_ns() - t0 > 100 * 1000 * 1000;
}

static __noinline int spins(void)
{
	return spin();
}

static __noinline int counts_to_1000(void)
{
	int i, cnt = zero;

	for (i = zero; i < 1000 && can_loop; i++)
		cnt++;
	return cnt;
}

/* Nothing special: the loops go on, then the time is up one time. */
SEC("tc")
int normal_prog(void *ctx)
{
	int i, n = 0;

	if (counts_to_1000() != 1000)
		return 100;
	for (i = 0; i < TRIES; i++)
		n += spins();
	return n;
}

/*
 * Async callbacks
 */
struct elem {
	struct bpf_timer timer;
	struct bpf_wq wq;
	struct bpf_task_work tw;
	struct bpf_rcu_head rcu;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, int);
	__type(value, struct elem);
} async SEC(".maps");

int timer_cnt, wq_cnt, tw_cnt, rcu_cnt, rcu_trace_cnt;

static int timer_cb(void *map, int *key, struct elem *e)
{
	timer_cnt = counts_to_1000();
	return 0;
}

static int wq_cb(void *map, int *key, void *val)
{
	wq_cnt = counts_to_1000();
	return 0;
}

static int tw_cb(struct bpf_map *map, void *key, void *val)
{
	tw_cnt = counts_to_1000();
	return 0;
}

static int rcu_cb(struct bpf_map *map, void *key, void *val)
{
	rcu_cnt = counts_to_1000();
	return 0;
}

static int rcu_trace_cb(struct bpf_map *map, void *key, void *val)
{
	rcu_trace_cnt = counts_to_1000();
	return 0;
}

/*
 * The time of this prog is up when it starts the callbacks. User space keeps the task on the cpu,
 * so the callbacks find what it has left. Each call of a callback is an invocation of its own.
 */
SEC("syscall")
int async_after_timeout(void *ctx)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct elem *e, *e1;
	int key = 0, key1 = 1;

	e = bpf_map_lookup_elem(&async, &key);
	e1 = bpf_map_lookup_elem(&async, &key1);
	if (!e || !e1)
		return 100;
	if (bpf_timer_init(&e->timer, &async, CLOCK_MONOTONIC) ||
	    bpf_timer_set_callback(&e->timer, timer_cb))
		return 101;
	if (bpf_wq_init(&e->wq, &async, 0) || bpf_wq_set_callback(&e->wq, wq_cb, 0))
		return 102;
	if (!spins())
		return 103;
	/* control: the time is up */
	if (counts_to_1000())
		return 109;
	if (bpf_timer_start(&e->timer, 0, BPF_F_TIMER_CPU_PIN))
		return 104;
	if (bpf_wq_start(&e->wq, 0))
		return 105;
	if (bpf_task_work_schedule_resume(task, &e->tw, &async, tw_cb))
		return 106;
	if (bpf_call_rcu(&e->rcu, &async, rcu_cb))
		return 107;
	if (bpf_call_rcu_tasks_trace(&e1->rcu, &async, rcu_trace_cb))
		return 108;
	return 0;
}

int ticks;

static int tick_cb(void *map, int *key, struct elem *e)
{
	if (counts_to_1000() != 1000)
		ticks = 1000;
	if (++ticks < 100)
		bpf_timer_start(&e->timer, 20 * 1000 * 1000, BPF_F_TIMER_CPU_PIN);
	return 0;
}

/*
 * A callback of the prog interrupts it, on the same cpu, each 20 ms. It has its own time. That
 * doesn't give more time to the prog. Return how long the loop went on, in ms.
 */
SEC("syscall")
int spin_under_timer(void *ctx)
{
	struct elem *e;
	int key = 1;
	__u64 t0;

	e = bpf_map_lookup_elem(&async, &key);
	if (!e)
		return 0;
	if (bpf_timer_init(&e->timer, &async, CLOCK_MONOTONIC) ||
	    bpf_timer_set_callback(&e->timer, tick_cb) ||
	    bpf_timer_start(&e->timer, 20 * 1000 * 1000, BPF_F_TIMER_CPU_PIN))
		return 0;
	t0 = bpf_ktime_get_ns();
	while (can_loop)
		;
	return (bpf_ktime_get_ns() - t0) / 1000000;
}

/*
 * Extension prog
 */
__noinline int replaced_global(int i)
{
	return spin() + i;
}

SEC("tc")
int freplace_target(void *ctx)
{
	int i, n = 0;

	for (i = 0; i < TRIES; i++)
		n += replaced_global(zero);
	return n;
}

/*
 * Tail calls
 */
struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} jmp_table SEC(".maps");

int tail_callee_spun;

SEC("tc")
int tail_callee(struct __sk_buff *skb)
{
	int i;

	tail_callee_spun = 0;
	for (i = 0; i < TRIES; i++)
		tail_callee_spun += spins();
	return 0;
}

static __noinline int tail_calls(struct __sk_buff *skb)
{
	bpf_tail_call(skb, &jmp_table, 0);
	return zero + 100;
}

/* The target of the tail call is another prog. It returns to this function. */
SEC("tc")
int tail_call_in_subprog(struct __sk_buff *skb)
{
	int i, n;

	n = tail_calls(skb);
	if (counts_to_1000() != 1000)
		return 101;
	for (i = 0; i < TRIES; i++)
		n += spins();
	return n;
}

SEC("tc")
int tail_call_after_timeout(struct __sk_buff *skb)
{
	spins();
	bpf_tail_call(skb, &jmp_table, 0);
	return 100;
}

static __noinline int throws(void)
{
	spin();
	bpf_throw(7);
	return 0;
}

SEC("tc")
int throw_after_timeout(void *ctx)
{
	return throws();
}

/*
 * bpf_user_ringbuf_drain()
 */
struct {
	__uint(type, BPF_MAP_TYPE_USER_RINGBUF);
	__uint(max_entries, 4096);
} user_ringbuf SEC(".maps");

static long drain_cb(struct bpf_dynptr *dynptr, int *spun)
{
	*spun += spin();
	return 0;
}

SEC("syscall")
int drain(void *ctx)
{
	int spun = 0;

	return bpf_user_ringbuf_drain(&user_ringbuf, drain_cb, &spun, 0) * 10 + spun;
}

/*
 * A tracing prog with large frames: they are in a private stack.
 */
int priv_stack_spun, priv_stack_cb_spun;

static __noinline int spins_with_stack(void)
{
	volatile char buf[128] = {};

	return spin() + buf[zero & 127];
}

static int priv_stack_cb(__u64 i, int *spun)
{
	volatile char buf[128] = {};

	*spun += spin() + buf[zero & 127];
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(priv_stack)
{
	volatile char buf[128] = {};
	int i, n = buf[zero & 127], spun = 0;

	for (i = 0; i < TRIES; i++)
		n += spins_with_stack();
	bpf_loop(TRIES, priv_stack_cb, &spun, zero);
	priv_stack_spun = n;
	priv_stack_cb_spun = spun;
	return 0;
}

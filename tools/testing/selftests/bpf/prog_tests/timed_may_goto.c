// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>
#include <test_progs.h>
#include <network_helpers.h>
#include "timed_may_goto.skel.h"
#include "timed_may_goto_user.skel.h"
#include "timed_may_goto_ext.skel.h"

#define TRIES 4

static int run_tc(struct bpf_program *prog)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	if (!ASSERT_OK(bpf_prog_test_run_opts(bpf_program__fd(prog), &opts), "test_run"))
		return -1;
	return opts.retval;
}

static int run_syscall(struct bpf_program *prog)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);

	if (!ASSERT_OK(bpf_prog_test_run_opts(bpf_program__fd(prog), &opts), "test_run"))
		return -1;
	return opts.retval;
}

static void wait_for(int *val, int expected, const char *name)
{
	int i;

	for (i = 0; i < 500 && *(volatile int *)val != expected; i++)
		usleep(10000);
	ASSERT_EQ(*val, expected, name);
}

static void test_async(struct timed_may_goto_user *skel)
{
	ASSERT_EQ(run_syscall(skel->progs.async_after_timeout), 0, "async_after_timeout");
	wait_for(&skel->bss->timer_cnt, 1000, "timer_cnt");
	wait_for(&skel->bss->wq_cnt, 1000, "wq_cnt");
	wait_for(&skel->bss->tw_cnt, 1000, "tw_cnt");
	wait_for(&skel->bss->rcu_cnt, 1000, "rcu_cnt");
	wait_for(&skel->bss->rcu_trace_cnt, 1000, "rcu_trace_cnt");
}

static void test_timer_on_top(struct timed_may_goto_user *skel)
{
	int ms = run_syscall(skel->progs.spin_under_timer);
	int ticks = skel->bss->ticks;

	/* stop the timer */
	skel->bss->ticks = 100;
	ASSERT_GE(ms, 200, "ms");
	ASSERT_LE(ms, 500, "ms");
	/* control: the callback interrupted the prog, and its loop went on to the end */
	ASSERT_GE(ticks, 5, "ticks");
	ASSERT_LT(ticks, 100, "ticks");
}

static void test_freplace(struct timed_may_goto_user *skel)
{
	struct timed_may_goto_ext *ext;

	ASSERT_EQ(run_tc(skel->progs.freplace_target), 1, "not replaced");

	ext = timed_may_goto_ext__open();
	if (!ASSERT_OK_PTR(ext, "ext_open"))
		return;
	bpf_program__set_attach_target(ext->progs.replacement,
				       bpf_program__fd(skel->progs.freplace_target),
				       "replaced_global");
	if (!ASSERT_OK(timed_may_goto_ext__load(ext), "ext_load") ||
	    !ASSERT_OK(timed_may_goto_ext__attach(ext), "ext_attach"))
		goto out;
	ASSERT_EQ(run_tc(skel->progs.freplace_target), TRIES, "replaced");
out:
	timed_may_goto_ext__destroy(ext);
	ASSERT_EQ(run_tc(skel->progs.freplace_target), 1, "not replaced");
}

/* What a prog leaves behind when its time is up doesn't matter for the next one on the cpu. */
static void test_tail_call_and_throw(struct timed_may_goto_user *skel)
{
	int key = 0, fd = bpf_program__fd(skel->progs.tail_callee);
	int map_fd = bpf_map__fd(skel->maps.jmp_table);

	if (!ASSERT_OK(bpf_map_update_elem(map_fd, &key, &fd, 0), "update"))
		return;

	ASSERT_EQ(run_tc(skel->progs.normal_prog), 1, "normal_prog");
	ASSERT_EQ(run_tc(skel->progs.normal_prog), 1, "normal_prog");

	ASSERT_EQ(run_tc(skel->progs.tail_call_in_subprog), 1, "tail_call_in_subprog");
	ASSERT_EQ(skel->bss->tail_callee_spun, 1, "tail_callee_spun");

	ASSERT_EQ(run_tc(skel->progs.tail_call_after_timeout), 0, "tail_call_after_timeout");
	ASSERT_EQ(skel->bss->tail_callee_spun, 1, "tail_callee_spun");

	ASSERT_EQ(run_tc(skel->progs.throw_after_timeout), 7, "throw_after_timeout");
	ASSERT_EQ(run_tc(skel->progs.throw_after_timeout), 7, "throw_after_timeout");
}

static void test_user_ringbuf(struct timed_may_goto_user *skel)
{
	struct user_ring_buffer *rb;
	void *sample;
	int i;

	rb = user_ring_buffer__new(bpf_map__fd(skel->maps.user_ringbuf), NULL);
	if (!ASSERT_OK_PTR(rb, "user_ring_buffer__new"))
		return;
	for (i = 0; i < TRIES; i++) {
		sample = user_ring_buffer__reserve(rb, 8);
		if (!ASSERT_OK_PTR(sample, "reserve"))
			goto out;
		user_ring_buffer__submit(rb, sample);
	}
	/* The helper takes a sample out of the buffer for each call: all calls have to run. */
	ASSERT_EQ(run_syscall(skel->progs.drain), TRIES * 10 + 1, "drain");
out:
	user_ring_buffer__free(rb);
}

static void test_priv_stack(struct timed_may_goto_user *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int fd = bpf_program__fd(skel->progs.priv_stack);
	struct bpf_link *link;

	link = bpf_program__attach(skel->progs.priv_stack);
	if (!ASSERT_OK_PTR(link, "attach"))
		return;
	ASSERT_OK(bpf_prog_test_run_opts(fd, &opts), "test_run");
	ASSERT_EQ(skel->bss->priv_stack_spun, 1, "priv_stack_spun");
	ASSERT_EQ(skel->bss->priv_stack_cb_spun, 0, "priv_stack_cb_spun");
	bpf_link__destroy(link);
}

/*
 * Serial: most progs spin for 250 ms in an RCU read-side critical section, one after the other.
 * That delays what other tests wait for. In parallel mode "mmap" gives up its wait for the end of
 * a map.
 */
void serial_test_timed_may_goto(void)
{
	struct timed_may_goto_user *skel;
	cpu_set_t old, new;

	/* The interpreter counts in each frame. */
	if (!is_jit_enabled()) {
		test__skip();
		return;
	}

	RUN_TESTS(timed_may_goto);

#if !defined(__x86_64__) && !defined(__aarch64__)
	return;
#endif
	skel = timed_may_goto_user__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	/* Count and timestamp are per-cpu: all progs of a subtest run on one cpu. */
	if (!ASSERT_OK(sched_getaffinity(0, sizeof(old), &old), "getaffinity"))
		goto out;
	CPU_ZERO(&new);
	CPU_SET(sched_getcpu(), &new);
	if (!ASSERT_OK(sched_setaffinity(0, sizeof(new), &new), "setaffinity"))
		goto out;

	if (test__start_subtest("async"))
		test_async(skel);
	if (test__start_subtest("timer_on_top"))
		test_timer_on_top(skel);
	if (test__start_subtest("freplace"))
		test_freplace(skel);
	if (test__start_subtest("tail_call_and_throw"))
		test_tail_call_and_throw(skel);
	if (test__start_subtest("user_ringbuf"))
		test_user_ringbuf(skel);
	if (test__start_subtest("priv_stack"))
		test_priv_stack(skel);

	sched_setaffinity(0, sizeof(old), &old);
out:
	timed_may_goto_user__destroy(skel);
}

// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include "timer.skel.h"
#include "timer_failure.skel.h"
#include "timer_interrupt.skel.h"

#define NUM_THR 8

static void *spin_lock_thread(void *arg)
{
	int i, err, prog_fd = *(int *)arg;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	for (i = 0; i < 10000; i++) {
		err = bpf_prog_test_run_opts(prog_fd, &topts);
		if (!ASSERT_OK(err, "test_run_opts err") ||
		    !ASSERT_OK(topts.retval, "test_run_opts retval"))
			break;
	}

	pthread_exit(arg);
}


static int timer_stress_runner(struct timer *timer_skel, bool async_cancel)
{
	int i, err = 1, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	pthread_t thread_id[NUM_THR];
	void *ret;

	timer_skel->bss->async_cancel = async_cancel;
	prog_fd = bpf_program__fd(timer_skel->progs.race);
	for (i = 0; i < NUM_THR; i++) {
		err = pthread_create(&thread_id[i], NULL,
				     &spin_lock_thread, &prog_fd);
		if (!ASSERT_OK(err, "pthread_create"))
			break;
	}

	while (i) {
		err = pthread_join(thread_id[--i], &ret);
		if (ASSERT_OK(err, "pthread_join"))
			ASSERT_EQ(ret, (void *)&prog_fd, "pthread_join");
	}
	return err;
}

static int timer_stress(struct timer *timer_skel)
{
	return timer_stress_runner(timer_skel, false);
}

static int timer_stress_async_cancel(struct timer *timer_skel)
{
	return timer_stress_runner(timer_skel, true);
}

static int timer(struct timer *timer_skel)
{
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	err = timer__attach(timer_skel);
	if (!ASSERT_OK(err, "timer_attach"))
		return err;

	ASSERT_EQ(timer_skel->data->callback_check, 52, "callback_check1");
	ASSERT_EQ(timer_skel->data->callback2_check, 52, "callback2_check1");
	ASSERT_EQ(timer_skel->bss->pinned_callback_check, 0, "pinned_callback_check1");

	prog_fd = bpf_program__fd(timer_skel->progs.test1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");
	timer__detach(timer_skel);

	usleep(50); /* 10 usecs should be enough, but give it extra */
	/* check that timer_cb1() was executed 10+10 times */
	ASSERT_EQ(timer_skel->data->callback_check, 42, "callback_check2");
	ASSERT_EQ(timer_skel->data->callback2_check, 42, "callback2_check2");

	/* check that timer_cb2() was executed twice */
	ASSERT_EQ(timer_skel->bss->bss_data, 10, "bss_data");

	/* check that timer_cb3() was executed twice */
	ASSERT_EQ(timer_skel->bss->abs_data, 12, "abs_data");

	/* check that timer_cb_pinned() was executed twice */
	ASSERT_EQ(timer_skel->bss->pinned_callback_check, 2, "pinned_callback_check");

	/* check that there were no errors in timer execution */
	ASSERT_EQ(timer_skel->bss->err, 0, "err");

	/* check that code paths completed */
	ASSERT_EQ(timer_skel->bss->ok, 1 | 2 | 4, "ok");

	return 0;
}

static int timer_cancel_async(struct timer *timer_skel)
{
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	prog_fd = bpf_program__fd(timer_skel->progs.test_async_cancel_succeed);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	usleep(500);
	/* check that there were no errors in timer execution */
	ASSERT_EQ(timer_skel->bss->err, 0, "err");

	/* check that code paths completed */
	ASSERT_EQ(timer_skel->bss->ok, 1 | 2 | 4, "ok");

	return 0;
}

static void test_timer(int (*timer_test_fn)(struct timer *timer_skel))
{
	struct timer *timer_skel = NULL;
	int err;

	timer_skel = timer__open_and_load();
	if (!timer_skel && errno == EOPNOTSUPP) {
		test__skip();
		return;
	}
	if (!ASSERT_OK_PTR(timer_skel, "timer_skel_load"))
		return;

	err = timer_test_fn(timer_skel);
	ASSERT_OK(err, "timer");
	timer__destroy(timer_skel);
}

void serial_test_timer(void)
{
	test_timer(timer);

	RUN_TESTS(timer_failure);
}

void serial_test_timer_stress(void)
{
	test_timer(timer_stress);
}

void serial_test_timer_stress_async_cancel(void)
{
	test_timer(timer_stress_async_cancel);
}

void serial_test_timer_async_cancel(void)
{
	test_timer(timer_cancel_async);
}

void test_timer_interrupt(void)
{
	struct timer_interrupt *skel = NULL;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, opts);

	skel = timer_interrupt__open_and_load();
	if (!skel && errno == EOPNOTSUPP) {
		test__skip();
		return;
	}
	if (!ASSERT_OK_PTR(skel, "timer_interrupt__open_and_load"))
		return;

	err = timer_interrupt__attach(skel);
	if (!ASSERT_OK(err, "timer_interrupt__attach"))
		goto out;

	prog_fd = bpf_program__fd(skel->progs.test_timer_interrupt);
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	if (!ASSERT_OK(err, "bpf_prog_test_run_opts"))
		goto out;

	usleep(50);

	ASSERT_EQ(skel->bss->in_interrupt, 0, "in_interrupt");
	if (skel->bss->preempt_count)
		ASSERT_NEQ(skel->bss->in_interrupt_cb, 0, "in_interrupt_cb");

out:
	timer_interrupt__destroy(skel);
}

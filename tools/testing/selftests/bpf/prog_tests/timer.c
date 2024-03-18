// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include "timer.skel.h"
#include "timer_failure.skel.h"

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

static int timer(struct timer *timer_skel)
{
	int i, err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	pthread_t thread_id[NUM_THR];
	void *ret;

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

	return 0;
}

/* TODO: use pid filtering */
void serial_test_timer(void)
{
	struct timer *timer_skel = NULL;
	int err;

	timer_skel = timer__open_and_load();
	if (!ASSERT_OK_PTR(timer_skel, "timer_skel_load"))
		return;

	err = timer(timer_skel);
	ASSERT_OK(err, "timer");
	timer__destroy(timer_skel);

	RUN_TESTS(timer_failure);
}

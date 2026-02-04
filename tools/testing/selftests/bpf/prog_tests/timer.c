// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <sched.h>
#include <test_progs.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include "timer.skel.h"
#include "timer_failure.skel.h"
#include "timer_interrupt.skel.h"

#define NUM_THR 8

static int perf_event_open(__u32 type, __u64 config, int pid, int cpu)
{
	struct perf_event_attr attr = {
		.type = type,
		.config = config,
		.size = sizeof(struct perf_event_attr),
		.sample_period = 10000,
	};

	return syscall(__NR_perf_event_open, &attr, pid, cpu, -1, 0);
}

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

static void *nmi_cpu_worker(void *arg)
{
	volatile __u64 num = 1;
	int i;

	for (i = 0; i < 500000000; ++i)
		num *= (i % 7) + 1;
	(void)num;

	return NULL;
}

static int run_nmi_test(struct timer *timer_skel, struct bpf_program *prog)
{
	struct bpf_link *link = NULL;
	int pe_fd = -1, pipefd[2] = {-1, -1}, pid = 0, status;
	char buf = 0;
	int ret = -1;

	if (!ASSERT_OK(pipe(pipefd), "pipe"))
		goto cleanup;

	pid = fork();
	if (pid == 0) {
		/* Child: spawn multiple threads to consume multiple CPUs */
		pthread_t threads[NUM_THR];
		int i;

		close(pipefd[1]);
		read(pipefd[0], &buf, 1);
		close(pipefd[0]);

		for (i = 0; i < NUM_THR; i++)
			pthread_create(&threads[i], NULL, nmi_cpu_worker, NULL);
		for (i = 0; i < NUM_THR; i++)
			pthread_join(threads[i], NULL);
		exit(0);
	}

	if (!ASSERT_GE(pid, 0, "fork"))
		goto cleanup;

	/* Open perf event for child process across all CPUs */
	pe_fd = perf_event_open(PERF_TYPE_HARDWARE,
				PERF_COUNT_HW_CPU_CYCLES,
				pid,  /* measure child process */
				-1);  /* on any CPU */
	if (pe_fd < 0) {
		if (errno == ENOENT || errno == EOPNOTSUPP) {
			printf("SKIP:no PERF_COUNT_HW_CPU_CYCLES\n");
			test__skip();
			ret = EOPNOTSUPP;
			goto cleanup;
		}
		ASSERT_GE(pe_fd, 0, "perf_event_open");
		goto cleanup;
	}

	link = bpf_program__attach_perf_event(prog, pe_fd);
	if (!ASSERT_OK_PTR(link, "attach_perf_event"))
		goto cleanup;
	pe_fd = -1;  /* Ownership transferred to link */

	/* Signal child to start CPU work */
	close(pipefd[0]);
	pipefd[0] = -1;
	write(pipefd[1], &buf, 1);
	close(pipefd[1]);
	pipefd[1] = -1;

	waitpid(pid, &status, 0);
	pid = 0;

	/* Verify NMI context was hit */
	ASSERT_GT(timer_skel->bss->test_hits, 0, "test_hits");
	ret = 0;

cleanup:
	bpf_link__destroy(link);
	if (pe_fd >= 0)
		close(pe_fd);
	if (pid > 0) {
		write(pipefd[1], &buf, 1);
		waitpid(pid, &status, 0);
	}
	if (pipefd[0] >= 0)
		close(pipefd[0]);
	if (pipefd[1] >= 0)
		close(pipefd[1]);
	return ret;
}

static int timer_stress_nmi_race(struct timer *timer_skel)
{
	int err;

	err = run_nmi_test(timer_skel, timer_skel->progs.nmi_race);
	if (err == EOPNOTSUPP)
		return 0;
	return err;
}

static int timer_stress_nmi_update(struct timer *timer_skel)
{
	int err;

	err = run_nmi_test(timer_skel, timer_skel->progs.nmi_update);
	if (err == EOPNOTSUPP)
		return 0;
	if (err)
		return err;
	ASSERT_GT(timer_skel->bss->update_hits, 0, "update_hits");
	return 0;
}

static int timer_stress_nmi_cancel(struct timer *timer_skel)
{
	int err;

	err = run_nmi_test(timer_skel, timer_skel->progs.nmi_cancel);
	if (err == EOPNOTSUPP)
		return 0;
	if (err)
		return err;
	ASSERT_GT(timer_skel->bss->cancel_hits, 0, "cancel_hits");
	return 0;
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

void serial_test_timer_stress_nmi_race(void)
{
	test_timer(timer_stress_nmi_race);
}

void serial_test_timer_stress_nmi_update(void)
{
	test_timer(timer_stress_nmi_update);
}

void serial_test_timer_stress_nmi_cancel(void)
{
	test_timer(timer_stress_nmi_cancel);
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

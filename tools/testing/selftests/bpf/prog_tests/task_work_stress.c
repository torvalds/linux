// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <string.h>
#include <stdio.h>
#include "task_work_stress.skel.h"
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <time.h>
#include <stdlib.h>
#include <stdatomic.h>

struct test_data {
	int prog_fd;
	atomic_int exit;
};

void *runner(void *test_data)
{
	struct test_data *td = test_data;
	int err = 0;
	LIBBPF_OPTS(bpf_test_run_opts, opts);

	while (!err && !atomic_load(&td->exit))
		err = bpf_prog_test_run_opts(td->prog_fd, &opts);

	return NULL;
}

static int get_env_int(const char *str, int def)
{
	const char *s = getenv(str);
	char *end;
	int retval;

	if (!s || !*s)
		return def;
	errno = 0;
	retval = strtol(s, &end, 10);
	if (errno || *end || retval < 0)
		return def;
	return retval;
}

static void task_work_run(bool enable_delete)
{
	struct task_work_stress *skel;
	struct bpf_program *scheduler, *deleter;
	int nthreads = 16;
	int test_time_s = get_env_int("BPF_TASK_WORK_TEST_TIME", 1);
	pthread_t tid[nthreads], tid_del;
	bool started[nthreads], started_del = false;
	struct test_data td_sched = { .exit = 0 }, td_del = { .exit = 1 };
	int i, err;

	skel = task_work_stress__open();
	if (!ASSERT_OK_PTR(skel, "task_work__open"))
		return;

	scheduler = bpf_object__find_program_by_name(skel->obj, "schedule_task_work");
	bpf_program__set_autoload(scheduler, true);

	deleter = bpf_object__find_program_by_name(skel->obj, "delete_task_work");
	bpf_program__set_autoload(deleter, true);

	err = task_work_stress__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	for (i = 0; i < nthreads; ++i)
		started[i] = false;

	td_sched.prog_fd = bpf_program__fd(scheduler);
	for (i = 0; i < nthreads; ++i) {
		if (pthread_create(&tid[i], NULL, runner, &td_sched) != 0) {
			fprintf(stderr, "could not start thread");
			goto cancel;
		}
		started[i] = true;
	}

	if (enable_delete)
		atomic_store(&td_del.exit, 0);

	td_del.prog_fd = bpf_program__fd(deleter);
	if (pthread_create(&tid_del, NULL, runner, &td_del) != 0) {
		fprintf(stderr, "could not start thread");
		goto cancel;
	}
	started_del = true;

	/* Run stress test for some time */
	sleep(test_time_s);

cancel:
	atomic_store(&td_sched.exit, 1);
	atomic_store(&td_del.exit, 1);
	for (i = 0; i < nthreads; ++i) {
		if (started[i])
			pthread_join(tid[i], NULL);
	}

	if (started_del)
		pthread_join(tid_del, NULL);

	ASSERT_GT(skel->bss->callback_scheduled, 0, "work scheduled");
	/* Some scheduling attempts should have failed due to contention */
	ASSERT_GT(skel->bss->schedule_error, 0, "schedule error");

	if (enable_delete) {
		/* If delete thread is enabled, it has cancelled some callbacks */
		ASSERT_GT(skel->bss->delete_success, 0, "delete success");
		ASSERT_LT(skel->bss->callback_success, skel->bss->callback_scheduled, "callbacks");
	} else {
		/* Without delete thread number of scheduled callbacks is the same as fired */
		ASSERT_EQ(skel->bss->callback_success, skel->bss->callback_scheduled, "callbacks");
	}

cleanup:
	task_work_stress__destroy(skel);
}

void test_task_work_stress(void)
{
	if (test__start_subtest("no_delete"))
		task_work_run(false);
	if (test__start_subtest("with_delete"))
		task_work_run(true);
}

// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <network_helpers.h>
#include <sys/sysinfo.h>

struct __qspinlock { int val; };
typedef struct __qspinlock arena_spinlock_t;

struct arena_qnode {
	unsigned long next;
	int count;
	int locked;
};

#include "arena_spin_lock.skel.h"

static long cpu;
static int repeat;

pthread_barrier_t barrier;

static void *spin_lock_thread(void *arg)
{
	int err, prog_fd = *(u32 *)arg;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = repeat,
	);
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(__sync_fetch_and_add(&cpu, 1), &cpuset);
	ASSERT_OK(pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset), "cpu affinity");

	err = pthread_barrier_wait(&barrier);
	if (err != PTHREAD_BARRIER_SERIAL_THREAD && err != 0)
		ASSERT_FALSE(true, "pthread_barrier");

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run err");

	if (topts.retval == -EOPNOTSUPP)
		goto end;

	ASSERT_EQ((int)topts.retval, 0, "test_run retval");

end:
	pthread_exit(arg);
}

static void test_arena_spin_lock_size(int size)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct arena_spin_lock *skel;
	pthread_t thread_id[16];
	int prog_fd, i, err;
	int nthreads;
	void *ret;

	nthreads = MIN(get_nprocs(), ARRAY_SIZE(thread_id));
	if (nthreads < 2) {
		test__skip();
		return;
	}

	skel = arena_spin_lock__open_and_load();
	if (!ASSERT_OK_PTR(skel, "arena_spin_lock__open_and_load"))
		return;

	if (skel->data->test_skip == 2) {
		test__skip();
		goto end;
	}
	skel->bss->cs_count = size;
	skel->bss->limit = repeat * nthreads;

	ASSERT_OK(pthread_barrier_init(&barrier, NULL, nthreads), "barrier init");

	prog_fd = bpf_program__fd(skel->progs.prog);
	for (i = 0; i < nthreads; i++) {
		err = pthread_create(&thread_id[i], NULL, &spin_lock_thread, &prog_fd);
		if (!ASSERT_OK(err, "pthread_create"))
			goto end_barrier;
	}

	for (i = 0; i < nthreads; i++) {
		if (!ASSERT_OK(pthread_join(thread_id[i], &ret), "pthread_join"))
			goto end_barrier;
		if (!ASSERT_EQ(ret, &prog_fd, "ret == prog_fd"))
			goto end_barrier;
	}

	if (skel->data->test_skip == 3) {
		printf("%s:SKIP: CONFIG_NR_CPUS exceed the maximum supported by arena spinlock\n",
		       __func__);
		test__skip();
		goto end_barrier;
	}

	ASSERT_EQ(skel->bss->counter, repeat * nthreads, "check counter value");

end_barrier:
	pthread_barrier_destroy(&barrier);
end:
	arena_spin_lock__destroy(skel);
	return;
}

void test_arena_spin_lock(void)
{
	repeat = 1000;
	if (test__start_subtest("arena_spin_lock_1"))
		test_arena_spin_lock_size(1);
	cpu = 0;
	if (test__start_subtest("arena_spin_lock_1000"))
		test_arena_spin_lock_size(1000);
	cpu = 0;
	repeat = 100;
	if (test__start_subtest("arena_spin_lock_50000"))
		test_arena_spin_lock_size(50000);
}

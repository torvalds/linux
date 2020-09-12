// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <test_progs.h>
#include "perf_event_stackmap.skel.h"

#ifndef noinline
#define noinline __attribute__((noinline))
#endif

noinline int func_1(void)
{
	static int val = 1;

	val += 1;

	usleep(100);
	return val;
}

noinline int func_2(void)
{
	return func_1();
}

noinline int func_3(void)
{
	return func_2();
}

noinline int func_4(void)
{
	return func_3();
}

noinline int func_5(void)
{
	return func_4();
}

noinline int func_6(void)
{
	int i, val = 1;

	for (i = 0; i < 100; i++)
		val += func_5();

	return val;
}

void test_perf_event_stackmap(void)
{
	struct perf_event_attr attr = {
		/* .type = PERF_TYPE_SOFTWARE, */
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
		.precise_ip = 2,
		.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_BRANCH_STACK |
			PERF_SAMPLE_CALLCHAIN,
		.branch_sample_type = PERF_SAMPLE_BRANCH_USER |
			PERF_SAMPLE_BRANCH_NO_FLAGS |
			PERF_SAMPLE_BRANCH_NO_CYCLES |
			PERF_SAMPLE_BRANCH_CALL_STACK,
		.sample_period = 5000,
		.size = sizeof(struct perf_event_attr),
	};
	struct perf_event_stackmap *skel;
	__u32 duration = 0;
	cpu_set_t cpu_set;
	int pmu_fd, err;

	skel = perf_event_stackmap__open();

	if (CHECK(!skel, "skel_open", "skeleton open failed\n"))
		return;

	err = perf_event_stackmap__load(skel);
	if (CHECK(err, "skel_load", "skeleton load failed: %d\n", err))
		goto cleanup;

	CPU_ZERO(&cpu_set);
	CPU_SET(0, &cpu_set);
	err = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
	if (CHECK(err, "set_affinity", "err %d, errno %d\n", err, errno))
		goto cleanup;

	pmu_fd = syscall(__NR_perf_event_open, &attr, -1 /* pid */,
			 0 /* cpu 0 */, -1 /* group id */,
			 0 /* flags */);
	if (pmu_fd < 0) {
		printf("%s:SKIP:cpu doesn't support the event\n", __func__);
		test__skip();
		goto cleanup;
	}

	skel->links.oncpu = bpf_program__attach_perf_event(skel->progs.oncpu,
							   pmu_fd);
	if (CHECK(IS_ERR(skel->links.oncpu), "attach_perf_event",
		  "err %ld\n", PTR_ERR(skel->links.oncpu))) {
		close(pmu_fd);
		goto cleanup;
	}

	/* create kernel and user stack traces for testing */
	func_6();

	CHECK(skel->data->stackid_kernel != 2, "get_stackid_kernel", "failed\n");
	CHECK(skel->data->stackid_user != 2, "get_stackid_user", "failed\n");
	CHECK(skel->data->stack_kernel != 2, "get_stack_kernel", "failed\n");
	CHECK(skel->data->stack_user != 2, "get_stack_user", "failed\n");

cleanup:
	perf_event_stackmap__destroy(skel);
}

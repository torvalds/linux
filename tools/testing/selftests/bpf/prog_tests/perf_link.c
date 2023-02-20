// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <test_progs.h>
#include "test_perf_link.skel.h"

static void burn_cpu(void)
{
	volatile int j = 0;
	cpu_set_t cpu_set;
	int i, err;

	/* generate some branches on cpu 0 */
	CPU_ZERO(&cpu_set);
	CPU_SET(0, &cpu_set);
	err = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
	ASSERT_OK(err, "set_thread_affinity");

	/* spin the loop for a while (random high number) */
	for (i = 0; i < 1000000; ++i)
		++j;
}

/* TODO: often fails in concurrent mode */
void serial_test_perf_link(void)
{
	struct test_perf_link *skel = NULL;
	struct perf_event_attr attr;
	int pfd = -1, link_fd = -1, err;
	int run_cnt_before, run_cnt_after;
	struct bpf_link_info info;
	__u32 info_len = sizeof(info);

	/* create perf event */
	memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(attr);
	attr.type = PERF_TYPE_SOFTWARE;
	attr.config = PERF_COUNT_SW_CPU_CLOCK;
	attr.freq = 1;
	attr.sample_freq = 1000;
	pfd = syscall(__NR_perf_event_open, &attr, -1, 0, -1, PERF_FLAG_FD_CLOEXEC);
	if (!ASSERT_GE(pfd, 0, "perf_fd"))
		goto cleanup;

	skel = test_perf_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	link_fd = bpf_link_create(bpf_program__fd(skel->progs.handler), pfd,
				  BPF_PERF_EVENT, NULL);
	if (!ASSERT_GE(link_fd, 0, "link_fd"))
		goto cleanup;

	memset(&info, 0, sizeof(info));
	err = bpf_link_get_info_by_fd(link_fd, &info, &info_len);
	if (!ASSERT_OK(err, "link_get_info"))
		goto cleanup;

	ASSERT_EQ(info.type, BPF_LINK_TYPE_PERF_EVENT, "link_type");
	ASSERT_GT(info.id, 0, "link_id");
	ASSERT_GT(info.prog_id, 0, "link_prog_id");

	/* ensure we get at least one perf_event prog execution */
	burn_cpu();
	ASSERT_GT(skel->bss->run_cnt, 0, "run_cnt");

	/* perf_event is still active, but we close link and BPF program
	 * shouldn't be executed anymore
	 */
	close(link_fd);
	link_fd = -1;

	/* make sure there are no stragglers */
	kern_sync_rcu();

	run_cnt_before = skel->bss->run_cnt;
	burn_cpu();
	run_cnt_after = skel->bss->run_cnt;

	ASSERT_EQ(run_cnt_before, run_cnt_after, "run_cnt_before_after");

cleanup:
	if (link_fd >= 0)
		close(link_fd);
	if (pfd >= 0)
		close(pfd);
	test_perf_link__destroy(skel);
}

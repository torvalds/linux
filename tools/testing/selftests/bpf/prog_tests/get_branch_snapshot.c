// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include "get_branch_snapshot.skel.h"

static int *pfd_array;
static int cpu_cnt;

static int create_perf_events(void)
{
	struct perf_event_attr attr = {0};
	int cpu;

	/* create perf event */
	attr.size = sizeof(attr);
	attr.type = PERF_TYPE_RAW;
	attr.config = 0x1b00;
	attr.sample_type = PERF_SAMPLE_BRANCH_STACK;
	attr.branch_sample_type = PERF_SAMPLE_BRANCH_KERNEL |
		PERF_SAMPLE_BRANCH_USER | PERF_SAMPLE_BRANCH_ANY;

	cpu_cnt = libbpf_num_possible_cpus();
	pfd_array = malloc(sizeof(int) * cpu_cnt);
	if (!pfd_array) {
		cpu_cnt = 0;
		return 1;
	}

	for (cpu = 0; cpu < cpu_cnt; cpu++) {
		pfd_array[cpu] = syscall(__NR_perf_event_open, &attr,
					 -1, cpu, -1, PERF_FLAG_FD_CLOEXEC);
		if (pfd_array[cpu] < 0)
			break;
	}

	return cpu == 0;
}

static void close_perf_events(void)
{
	int cpu = 0;
	int fd;

	while (cpu++ < cpu_cnt) {
		fd = pfd_array[cpu];
		if (fd < 0)
			break;
		close(fd);
	}
	free(pfd_array);
}

void test_get_branch_snapshot(void)
{
	struct get_branch_snapshot *skel = NULL;
	int err;

	if (create_perf_events()) {
		test__skip();  /* system doesn't support LBR */
		goto cleanup;
	}

	skel = get_branch_snapshot__open_and_load();
	if (!ASSERT_OK_PTR(skel, "get_branch_snapshot__open_and_load"))
		goto cleanup;

	err = kallsyms_find("bpf_testmod_loop_test", &skel->bss->address_low);
	if (!ASSERT_OK(err, "kallsyms_find"))
		goto cleanup;

	err = kallsyms_find_next("bpf_testmod_loop_test", &skel->bss->address_high);
	if (!ASSERT_OK(err, "kallsyms_find_next"))
		goto cleanup;

	err = get_branch_snapshot__attach(skel);
	if (!ASSERT_OK(err, "get_branch_snapshot__attach"))
		goto cleanup;

	trigger_module_test_read(100);

	if (skel->bss->total_entries < 16) {
		/* too few entries for the hit/waste test */
		test__skip();
		goto cleanup;
	}

	ASSERT_GT(skel->bss->test1_hits, 6, "find_looptest_in_lbr");

	/* Given we stop LBR in software, we will waste a few entries.
	 * But we should try to waste as few as possible entries. We are at
	 * about 7 on x86_64 systems.
	 * Add a check for < 10 so that we get heads-up when something
	 * changes and wastes too many entries.
	 */
	ASSERT_LT(skel->bss->wasted_entries, 10, "check_wasted_entries");

cleanup:
	get_branch_snapshot__destroy(skel);
	close_perf_events();
}

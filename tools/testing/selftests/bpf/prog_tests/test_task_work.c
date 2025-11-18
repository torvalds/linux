// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <string.h>
#include <stdio.h>
#include "task_work.skel.h"
#include "task_work_fail.skel.h"
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <time.h>

static int perf_event_open(__u32 type, __u64 config, int pid)
{
	struct perf_event_attr attr = {
		.type = type,
		.config = config,
		.size = sizeof(struct perf_event_attr),
		.sample_period = 100000,
	};

	return syscall(__NR_perf_event_open, &attr, pid, -1, -1, 0);
}

struct elem {
	char data[128];
	struct bpf_task_work tw;
};

static int verify_map(struct bpf_map *map, const char *expected_data)
{
	int err;
	struct elem value;
	int processed_values = 0;
	int k, sz;

	sz = bpf_map__max_entries(map);
	for (k = 0; k < sz; ++k) {
		err = bpf_map__lookup_elem(map, &k, sizeof(int), &value, sizeof(struct elem), 0);
		if (err)
			continue;
		if (!ASSERT_EQ(strcmp(expected_data, value.data), 0, "map data")) {
			fprintf(stderr, "expected '%s', found '%s' in %s map", expected_data,
				value.data, bpf_map__name(map));
			return 2;
		}
		processed_values++;
	}

	return processed_values == 0;
}

static void task_work_run(const char *prog_name, const char *map_name)
{
	struct task_work *skel;
	struct bpf_program *prog;
	struct bpf_map *map;
	struct bpf_link *link = NULL;
	int err, pe_fd = -1, pid, status, pipefd[2];
	char user_string[] = "hello world";

	if (!ASSERT_NEQ(pipe(pipefd), -1, "pipe"))
		return;

	pid = fork();
	if (pid == 0) {
		__u64 num = 1;
		int i;
		char buf;

		close(pipefd[1]);
		read(pipefd[0], &buf, sizeof(buf));
		close(pipefd[0]);

		for (i = 0; i < 10000; ++i)
			num *= time(0) % 7;
		(void)num;
		exit(0);
	}
	if (!ASSERT_GT(pid, 0, "fork() failed")) {
		close(pipefd[0]);
		close(pipefd[1]);
		return;
	}

	skel = task_work__open();
	if (!ASSERT_OK_PTR(skel, "task_work__open"))
		return;

	bpf_object__for_each_program(prog, skel->obj) {
		bpf_program__set_autoload(prog, false);
	}

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "prog_name"))
		goto cleanup;
	bpf_program__set_autoload(prog, true);
	skel->bss->user_ptr = (char *)user_string;

	err = task_work__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	pe_fd = perf_event_open(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, pid);
	if (pe_fd == -1 && (errno == ENOENT || errno == EOPNOTSUPP)) {
		printf("%s:SKIP:no PERF_COUNT_HW_CPU_CYCLES\n", __func__);
		test__skip();
		goto cleanup;
	}
	if (!ASSERT_NEQ(pe_fd, -1, "pe_fd")) {
		fprintf(stderr, "perf_event_open errno: %d, pid: %d\n", errno, pid);
		goto cleanup;
	}

	link = bpf_program__attach_perf_event(prog, pe_fd);
	if (!ASSERT_OK_PTR(link, "attach_perf_event"))
		goto cleanup;

	/* perf event fd ownership is passed to bpf_link */
	pe_fd = -1;
	close(pipefd[0]);
	write(pipefd[1], user_string, 1);
	close(pipefd[1]);
	/* Wait to collect some samples */
	waitpid(pid, &status, 0);
	pid = 0;
	map = bpf_object__find_map_by_name(skel->obj, map_name);
	if (!ASSERT_OK_PTR(map, "find map_name"))
		goto cleanup;
	if (!ASSERT_OK(verify_map(map, user_string), "verify map"))
		goto cleanup;
cleanup:
	if (pe_fd >= 0)
		close(pe_fd);
	bpf_link__destroy(link);
	task_work__destroy(skel);
	if (pid > 0) {
		close(pipefd[0]);
		write(pipefd[1], user_string, 1);
		close(pipefd[1]);
		waitpid(pid, &status, 0);
	}
}

void test_task_work(void)
{
	if (test__start_subtest("test_task_work_hash_map"))
		task_work_run("oncpu_hash_map", "hmap");

	if (test__start_subtest("test_task_work_array_map"))
		task_work_run("oncpu_array_map", "arrmap");

	if (test__start_subtest("test_task_work_lru_map"))
		task_work_run("oncpu_lru_map", "lrumap");

	RUN_TESTS(task_work_fail);
}

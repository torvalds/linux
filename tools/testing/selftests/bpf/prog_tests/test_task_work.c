// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <string.h>
#include <stdio.h>
#include "task_work.skel.h"
#include "task_work_fail.skel.h"
#include "task_work_race.skel.h"
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

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
		goto cleanup;

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

#define TASK_WORK_RACE_ROUNDS		1500
#define TASK_WORK_RACE_POLL_US		100
#define TASK_WORK_RACE_POLL_ITERS	20000
#define TASK_WORK_RACE_START_POLL_ITERS	50000

/* Must match progs/task_work_race.c. */
enum task_work_race_status {
	RACE_ARM_SEQ,
	RACE_READY_SEQ,
	/* High 32 bits hold the sequence, low 32 bits the signed error. */
	RACE_RESULT,
};

struct task_work_race_value {
	__u32 seq;
	char data[60];
	struct bpf_task_work tw;
};

struct task_work_race_ctx {
	int stop;
	int setup_err;
	int trigger_tid;
	int target_tid;
};

struct task_work_race_thread_arg {
	struct task_work_race_ctx *ctx;
	int cpu;
	int *tid;
};

static int task_work_race_pin_cpu(int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	return pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static void *task_work_race_worker(void *arg)
{
	struct task_work_race_thread_arg *thread = arg;
	struct task_work_race_ctx *ctx = thread->ctx;
	int err;

	err = task_work_race_pin_cpu(thread->cpu);
	if (err)
		__atomic_store_n(&ctx->setup_err, err, __ATOMIC_RELEASE);
	__atomic_store_n(thread->tid, syscall(__NR_gettid), __ATOMIC_RELEASE);
	while (!__atomic_load_n(&ctx->stop, __ATOMIC_ACQUIRE))
		getppid();
	return NULL;
}

static int task_work_race_status(struct task_work_race *skel, int idx,
				 __s64 *value)
{
	return bpf_map_lookup_elem(bpf_map__fd(skel->maps.status), &idx,
				   value);
}

static int task_work_race_set_status(struct task_work_race *skel, int idx,
				     __s64 value)
{
	return bpf_map_update_elem(bpf_map__fd(skel->maps.status), &idx,
				   &value, BPF_ANY);
}

static int task_work_race_wait_ready(struct task_work_race *skel, __u32 seq)
{
	int i;

	for (i = 0; i < TASK_WORK_RACE_POLL_ITERS; i++) {
		__s64 value = 0;

		if (!task_work_race_status(skel, RACE_READY_SEQ, &value) &&
		    value == seq)
			return 0;
		if (!task_work_race_status(skel, RACE_RESULT, &value) &&
		    (__u32)((__u64)value >> 32) == seq)
			/*
			 * The BPF program finished between the two reads:
			 * RESULT makes waiting for READY unnecessary. Any error
			 * that occurred before READY is checked by the caller.
			 */
			return 0;
		usleep(TASK_WORK_RACE_POLL_US);
	}
	return -ETIMEDOUT;
}

static int task_work_race_prepare_elem(struct task_work_race *skel)
{
	struct task_work_race_value value = {};
	int key = 0;

	if (!bpf_map_lookup_elem(bpf_map__fd(skel->maps.hmap), &key, &value))
		return 0;
	if (errno != ENOENT)
		return -errno;
	if (bpf_map_update_elem(bpf_map__fd(skel->maps.hmap), &key, &value,
				BPF_NOEXIST))
		return -errno;
	return 0;
}

static int task_work_race_arm(struct task_work_race *skel, __u32 seq)
{
	if (task_work_race_set_status(skel, RACE_ARM_SEQ, seq))
		return -errno;
	return task_work_race_wait_ready(skel, seq);
}

static int task_work_race_check_done(struct task_work_race *skel, __u32 seq)
{
	int i;

	for (i = 0; i < TASK_WORK_RACE_POLL_ITERS; i++) {
		__s64 result = 0;

		if (!task_work_race_status(skel, RACE_RESULT, &result) &&
		    (__u32)((__u64)result >> 32) == seq)
			return (__s32)result;
		usleep(TASK_WORK_RACE_POLL_US);
	}
	return -ETIMEDOUT;
}

static int task_work_race_wait_callback(struct task_work_race *skel,
					__u32 seq)
{
	int i, key = seq;

	for (i = 0; i < TASK_WORK_RACE_POLL_ITERS; i++) {
		__u64 completed = 0;

		if (!bpf_map_lookup_elem(bpf_map__fd(skel->maps.completed),
					 &key, &completed) && completed)
			return 0;
		usleep(TASK_WORK_RACE_POLL_US);
	}
	return -ETIMEDOUT;
}

void serial_test_task_work_race(void)
{
	struct task_work_race_ctx ctx = {};
	struct task_work_race_thread_arg trigger_arg = {
		.ctx = &ctx,
		.tid = &ctx.trigger_tid,
	};
	struct task_work_race_thread_arg target_arg = {
		.ctx = &ctx,
		.tid = &ctx.target_tid,
	};
	struct task_work_race *skel;
	pthread_t trigger, target;
	cpu_set_t allowed;
	int cpu, cpu_count = 0;
	bool target_started = false, trigger_started = false;
	int err, i, key = 0;

	if (sched_getaffinity(0, sizeof(allowed), &allowed)) {
		ASSERT_OK(-errno, "sched_getaffinity");
		return;
	}
	for (cpu = 0; cpu < CPU_SETSIZE && cpu_count < 2; cpu++) {
		if (!CPU_ISSET(cpu, &allowed))
			continue;
		if (!cpu_count)
			trigger_arg.cpu = cpu;
		else
			target_arg.cpu = cpu;
		cpu_count++;
	}
	if (cpu_count < 2) {
		printf("%s:SKIP:need two CPUs in the process affinity mask\n",
		       __func__);
		test__skip();
		return;
	}

	skel = task_work_race__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;
	if (!ASSERT_OK(task_work_race__attach(skel), "attach"))
		goto cleanup;

	err = pthread_create(&target, NULL, task_work_race_worker, &target_arg);
	if (!ASSERT_OK(err, "pthread_create target"))
		goto cleanup;
	target_started = true;
	err = pthread_create(&trigger, NULL, task_work_race_worker, &trigger_arg);
	if (!ASSERT_OK(err, "pthread_create trigger"))
		goto stop;
	trigger_started = true;

	for (i = 0; i < TASK_WORK_RACE_START_POLL_ITERS; i++) {
		if (__atomic_load_n(&ctx.trigger_tid, __ATOMIC_ACQUIRE) &&
		    __atomic_load_n(&ctx.target_tid, __ATOMIC_ACQUIRE))
			break;
		usleep(TASK_WORK_RACE_POLL_US);
	}
	if (!ASSERT_TRUE(__atomic_load_n(&ctx.trigger_tid, __ATOMIC_ACQUIRE) &&
			 __atomic_load_n(&ctx.target_tid, __ATOMIC_ACQUIRE),
			 "thread start"))
		goto stop;
	if (!ASSERT_OK(__atomic_load_n(&ctx.setup_err, __ATOMIC_ACQUIRE),
		       "thread affinity"))
		goto stop;

	skel->bss->trigger_tid = __atomic_load_n(&ctx.trigger_tid, __ATOMIC_ACQUIRE);
	skel->bss->target_tid = __atomic_load_n(&ctx.target_tid, __ATOMIC_ACQUIRE);

	for (i = 0; i < TASK_WORK_RACE_ROUNDS; i++) {
		__u32 seq = i + 1;
		int variant = i % 3;

		if (!ASSERT_OK(task_work_race_prepare_elem(skel), "prepare elem") ||
		    !ASSERT_OK(task_work_race_arm(skel, seq), "round ready"))
			goto stop;

		if (variant == 2 &&
		    !ASSERT_OK(bpf_map_delete_elem(bpf_map__fd(skel->maps.hmap),
						   &key), "early delete"))
			goto stop;

		err = task_work_race_check_done(skel, seq);
		/*
		 * Variant 2 deletes the element after READY. A scheduling attempt
		 * racing with deletion can return -EBUSY, which the BPF program
		 * retries without publishing RESULT. The retry can then report
		 * -ENOENT because the element is gone; accept that terminal result.
		 * A later variant 0 round recreates the element.
		 */
		if (variant == 2 && err == -ENOENT)
			err = 0;
		if (!ASSERT_OK(err, "schedule")) {
			fprintf(stderr, "round %d schedule failed: %d\n", i, err);
			goto stop;
		}

		if (variant != 2 &&
		    !ASSERT_OK(task_work_race_wait_callback(skel, seq),
			       "callback"))
			goto stop;

		if (variant == 1 &&
		    !ASSERT_OK(bpf_map_delete_elem(bpf_map__fd(skel->maps.hmap),
						   &key), "late delete"))
			goto stop;
	}

	/* A distinct completion generation prevents an old callback satisfying this. */
	if (!ASSERT_OK(task_work_race_prepare_elem(skel), "final prepare") ||
	    !ASSERT_OK(task_work_race_arm(skel, TASK_WORK_RACE_ROUNDS + 1),
		       "final ready") ||
	    !ASSERT_OK(task_work_race_check_done(skel,
					TASK_WORK_RACE_ROUNDS + 1),
		       "final schedule") ||
	    !ASSERT_OK(task_work_race_wait_callback(skel,
					     TASK_WORK_RACE_ROUNDS + 1),
		       "final callback"))
		goto stop;

stop:
	__atomic_store_n(&ctx.stop, 1, __ATOMIC_RELEASE);
	if (trigger_started)
		pthread_join(trigger, NULL);
	if (target_started)
		pthread_join(target, NULL);
cleanup:
	task_work_race__destroy(skel);
}

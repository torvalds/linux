// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <string.h>
#include <stdio.h>
#include "task_work.skel.h"
#include "task_work_fail.skel.h"
#include "task_work_race.skel.h"
#include "task_work_busy.skel.h"
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
#define TASK_WORK_POLL_US		100
#define TASK_WORK_POLL_ITERS	20000
#define TASK_WORK_START_POLL_ITERS	50000

/* Must match progs/task_work_race.c. */
enum task_work_race_status {
	RACE_ARM_SEQ,
	RACE_READY_SEQ,
	/* High 32 bits hold the sequence, low 32 bits the signed error. */
	RACE_RESULT,
};

struct task_work_value {
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

static int task_work_status(struct bpf_map *map, int idx, __s64 *value)
{
	return bpf_map_lookup_elem(bpf_map__fd(map), &idx, value);
}

static int task_work_set_status(struct bpf_map *map, int idx, __s64 value)
{
	return bpf_map_update_elem(bpf_map__fd(map), &idx, &value, BPF_ANY);
}

/* Wait for seq, keeping polling errors separate from the scheduling result. */
static int task_work_wait_seq(struct bpf_map *map, int idx, __u32 seq,
			      __s32 *result)
{
	int i;

	for (i = 0; i < TASK_WORK_POLL_ITERS; i++) {
		__s64 value = 0;

		if (!task_work_status(map, idx, &value) &&
		    (__u32)((__u64)value >> 32) == seq) {
			*result = (__s32)value;
			return 0;
		}
		usleep(TASK_WORK_POLL_US);
	}
	return -ETIMEDOUT;
}

static int task_work_race_wait_ready(struct task_work_race *skel, __u32 seq)
{
	int i;

	for (i = 0; i < TASK_WORK_POLL_ITERS; i++) {
		__s64 value = 0;

		if (!task_work_status(skel->maps.status, RACE_READY_SEQ, &value) &&
		    value == seq)
			return 0;
		if (!task_work_status(skel->maps.status, RACE_RESULT, &value) &&
		    (__u32)((__u64)value >> 32) == seq)
			/*
			 * The BPF program finished between the two reads:
			 * RESULT makes waiting for READY unnecessary. Any error
			 * that occurred before READY is checked by the caller.
			 */
			return 0;
		usleep(TASK_WORK_POLL_US);
	}
	return -ETIMEDOUT;
}

static int task_work_race_prepare_elem(struct task_work_race *skel)
{
	struct task_work_value value = {};
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
	if (task_work_set_status(skel->maps.status, RACE_ARM_SEQ, seq))
		return -errno;
	return task_work_race_wait_ready(skel, seq);
}

static int task_work_race_check_done(struct task_work_race *skel, __u32 seq)
{
	__s32 result;
	int err;

	err = task_work_wait_seq(skel->maps.status, RACE_RESULT, seq, &result);
	return err ?: result;
}

static int task_work_race_wait_callback(struct task_work_race *skel,
					__u32 seq)
{
	int i, key = seq;

	for (i = 0; i < TASK_WORK_POLL_ITERS; i++) {
		__u64 completed = 0;

		if (!bpf_map_lookup_elem(bpf_map__fd(skel->maps.completed),
					 &key, &completed) && completed)
			return 0;
		usleep(TASK_WORK_POLL_US);
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

	for (i = 0; i < TASK_WORK_START_POLL_ITERS; i++) {
		if (__atomic_load_n(&ctx.trigger_tid, __ATOMIC_ACQUIRE) &&
		    __atomic_load_n(&ctx.target_tid, __ATOMIC_ACQUIRE))
			break;
		usleep(TASK_WORK_POLL_US);
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

#define TASK_WORK_BUSY_ROUNDS		32
#define TASK_WORK_BUSY_MAX_ATTEMPTS	96

/* Must match progs/task_work_busy.c. */
enum task_work_busy_status {
	BUSY_ARM_SEQ,
	/*
	 * High 32 bits hold the sequence.  BUSY_WINDOW low bits: 1 open,
	 * 2 the in-window attempt was seen before the handler returned,
	 * 3 the wait bound was reached without it.  Result slots: signed
	 * error in the low 32 bits.
	 */
	BUSY_WINDOW,
	BUSY_ACK,
	BUSY_GEN1_RESULT,
	BUSY_GEN2_RESULT,
	BUSY_CB1_SEQ,
	BUSY_CB2_COUNT,
	BUSY_CB2_SEQ,
	BUSY_RECOVER_SEQ,
	BUSY_LAST_WAIT_NS,
	BUSY_STATUS_MAX,
};

enum task_work_busy_attempt {
	TWB_VALID,
	TWB_MISSED,
	TWB_HARD_FAIL,
};

static const char *twb_attempt_name(enum task_work_busy_attempt r)
{
	switch (r) {
	case TWB_VALID:
		return "valid";
	case TWB_MISSED:
		return "missed";
	default:
		return "fail";
	}
}

static int task_work_busy_wait_value(struct task_work_busy *skel, int idx,
				     __s64 want)
{
	int i;

	for (i = 0; i < TASK_WORK_POLL_ITERS; i++) {
		__s64 value = 0;

		if (!task_work_status(skel->maps.status, idx, &value) && value == want)
			return 0;
		usleep(TASK_WORK_POLL_US);
	}
	return -ETIMEDOUT;
}

/* Wait until the window slot reports a final state (2 or 3) for seq. */
static int task_work_busy_wait_window(struct task_work_busy *skel, __u32 seq,
				      __s64 *window)
{
	int i;

	for (i = 0; i < TASK_WORK_POLL_ITERS; i++) {
		__s64 value = 0;

		if (!task_work_status(skel->maps.status, BUSY_WINDOW, &value) &&
		    (__u32)((__u64)value >> 32) == seq &&
		    (__u32)value >= 2) {
			*window = value;
			return 0;
		}
		usleep(TASK_WORK_POLL_US);
	}
	return -ETIMEDOUT;
}

static int task_work_busy_prepare_elem(struct task_work_busy *skel, int key)
{
	struct task_work_value value = {};

	if (!bpf_map_update_elem(bpf_map__fd(skel->maps.hmap), &key, &value,
				 BPF_NOEXIST))
		return 0;
	return -errno;
}

static int task_work_busy_reset(struct task_work_busy *skel)
{
	static const int zeroed[] = {
		BUSY_WINDOW, BUSY_ACK, BUSY_GEN1_RESULT, BUSY_GEN2_RESULT,
		BUSY_CB1_SEQ, BUSY_CB2_COUNT, BUSY_CB2_SEQ, BUSY_RECOVER_SEQ,
		BUSY_LAST_WAIT_NS,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(zeroed); i++)
		if (task_work_set_status(skel->maps.status, zeroed[i], 0))
			return -errno;
	return 0;
}

/*
 * join waits for clear_child_tid, which is cleared before exit_task_work().
 * PID lookup stops finding the worker only after its exit task_work has run.
 * Probe with signal 0; no signal is delivered.  Do not create new workers
 * until both old TIDs have disappeared.
 */
static int task_work_busy_wait_exit(int tid)
{
	int i;

	for (i = 0; i < TASK_WORK_POLL_ITERS; i++) {
		if (syscall(__NR_tgkill, getpid(), tid, 0)) {
			if (errno == ESRCH)
				return 0;
			return -errno;
		}
		usleep(TASK_WORK_POLL_US);
	}
	return -ETIMEDOUT;
}

/*
 * Run one attempt on dedicated workers and report its outcome.  The
 * workers are stopped, joined and waited out of PID lookup before another
 * attempt can reset the shared state.  join alone does not wait for their
 * exit task_work to finish.
 */
static enum task_work_busy_attempt
task_work_busy_run_attempt(struct task_work_busy *skel, int seq,
			   int trigger_cpu, int target_cpu, __s64 *out_wait_ns)
{
	struct task_work_race_ctx ctx = {};
	struct task_work_race_thread_arg trigger_arg = {
		.ctx = &ctx,
		.cpu = trigger_cpu,
		.tid = &ctx.trigger_tid,
	};
	struct task_work_race_thread_arg target_arg = {
		.ctx = &ctx,
		.cpu = target_cpu,
		.tid = &ctx.target_tid,
	};
	enum task_work_busy_attempt result = TWB_HARD_FAIL;
	pthread_t trigger, target;
	bool trigger_started = false, target_started = false;
	bool cleanup_ok = true;
	__s64 window = 0, wait_ns = 0, count = -1;
	__s32 res = 0;
	int err, i;

	*out_wait_ns = 0;
	/* Do not let a reused TID match the previous attempt's filter. */
	skel->bss->trigger_tid = 0;
	skel->bss->target_tid = 0;

	/* Clear old state before enabling workers that can read it. */
	if (!ASSERT_OK(task_work_busy_prepare_elem(skel, seq), "prepare elem"))
		goto out;
	if (!ASSERT_OK(task_work_busy_reset(skel), "reset slots"))
		goto out;

	err = pthread_create(&target, NULL, task_work_race_worker, &target_arg);
	if (err) {
		ASSERT_OK(err, "pthread_create target");
		goto out;
	}
	target_started = true;
	err = pthread_create(&trigger, NULL, task_work_race_worker, &trigger_arg);
	if (err) {
		ASSERT_OK(err, "pthread_create trigger");
		goto out;
	}
	trigger_started = true;

	for (i = 0; i < TASK_WORK_START_POLL_ITERS; i++) {
		if (__atomic_load_n(&ctx.trigger_tid, __ATOMIC_ACQUIRE) &&
		    __atomic_load_n(&ctx.target_tid, __ATOMIC_ACQUIRE))
			break;
		usleep(TASK_WORK_POLL_US);
	}
	if (!ASSERT_TRUE(__atomic_load_n(&ctx.trigger_tid, __ATOMIC_ACQUIRE) &&
			 __atomic_load_n(&ctx.target_tid, __ATOMIC_ACQUIRE),
			 "thread start"))
		goto out;
	if (!ASSERT_OK(__atomic_load_n(&ctx.setup_err, __ATOMIC_ACQUIRE),
		       "thread affinity"))
		goto out;

	skel->bss->trigger_tid = __atomic_load_n(&ctx.trigger_tid, __ATOMIC_ACQUIRE);
	skel->bss->target_tid = __atomic_load_n(&ctx.target_tid, __ATOMIC_ACQUIRE);

	/* Arm last, once this attempt's value, slots and TIDs are in place. */
	if (!ASSERT_OK(task_work_set_status(skel->maps.status, BUSY_ARM_SEQ, seq),
		       "arm"))
		goto out;

	/* A fresh value has no expected BUSY source: check the first result. */
	err = task_work_wait_seq(skel->maps.status, BUSY_GEN1_RESULT, seq, &res);
	if (!ASSERT_OK(err, "gen1 result"))
		goto out;
	if (!ASSERT_OK(res, "gen1 schedule")) {
		fprintf(stderr, "attempt %d: gen1 failed: %d\n", seq, res);
		goto out;
	}

	if (!ASSERT_OK(task_work_busy_wait_window(skel, seq, &window),
		       "window close"))
		goto out;

	if ((__u32)window == 3) {
		/*
		 * The attempt did not arrive inside the wait bound.  The
		 * elapsed time is a diagnostic only; count
		 * the miss and retry with a fresh attempt.
		 */
		result = TWB_MISSED;
		goto out;
	}

	/*
	 * Valid window: the handler saw this sequence's attempt before
	 * returning, so the attempt ran while the scheduling irq_work was
	 * still BUSY.  The elapsed time is a diagnostic only: host vCPU
	 * preemption can stretch wall-clock time without affecting the
	 * gate behavior under test.
	 */
	err = task_work_wait_seq(skel->maps.status, BUSY_ACK, seq, &res);
	if (!ASSERT_OK(err, "in-window attempt"))
		goto out;
	if (res != -EBUSY) {
		fprintf(stderr,
			"attempt %d: in-window reuse %s (%d), expected -EBUSY\n",
			seq, res == 0 ? "accepted" : "failed", res);
		ASSERT_EQ(res, -EBUSY, "in-window BUSY gate rejection");
		goto out;
	}

	/*
	 * The window was released; retry until BUSY clears.  The second
	 * callback must then run exactly once.
	 */
	if (!ASSERT_OK(task_work_set_status(skel->maps.status, BUSY_RECOVER_SEQ, seq),
		       "enable recovery"))
		goto out;
	err = task_work_wait_seq(skel->maps.status, BUSY_GEN2_RESULT, seq, &res);
	if (!ASSERT_OK(err, "recovery result"))
		goto out;
	if (!ASSERT_OK(res, "recovery schedule")) {
		fprintf(stderr, "attempt %d: recovery failed: %d\n", seq, res);
		goto out;
	}
	if (!ASSERT_OK(task_work_busy_wait_value(skel, BUSY_CB2_SEQ, seq),
		       "gen2 callback"))
		goto out;
	result = TWB_VALID;
out:
	__atomic_store_n(&ctx.stop, 1, __ATOMIC_RELEASE);
	if (trigger_started &&
	    (!ASSERT_OK(pthread_join(trigger, NULL), "trigger join") ||
	     !ASSERT_OK(task_work_busy_wait_exit(ctx.trigger_tid), "trigger exit"))) {
		cleanup_ok = false;
		result = TWB_HARD_FAIL;
	}
	if (target_started &&
	    (!ASSERT_OK(pthread_join(target, NULL), "target join") ||
	     !ASSERT_OK(task_work_busy_wait_exit(ctx.target_tid), "target exit"))) {
		cleanup_ok = false;
		result = TWB_HARD_FAIL;
	}
	/* Both workers have passed exit_task_work; the count is now stable. */
	if (result == TWB_VALID) {
		if (!ASSERT_OK(task_work_status(skel->maps.status, BUSY_CB2_COUNT, &count),
			       "cb2 count")) {
			result = TWB_HARD_FAIL;
		} else if (!ASSERT_EQ(count, 1, "gen2 callback exactly once")) {
			fprintf(stderr, "attempt %d: gen2 callback count %lld\n",
				seq, (long long)count);
			result = TWB_HARD_FAIL;
		}
	}
	/*
	 * Only a captured final window belongs to this attempt.  The fexit
	 * runs in the trigger's interrupted context, so successful worker
	 * cleanup also completes its diagnostic write.  Read once here;
	 * the elapsed value does not decide whether the attempt passed.
	 */
	if (cleanup_ok && window) {
		err = task_work_status(skel->maps.status, BUSY_LAST_WAIT_NS, &wait_ns);
		if (err) {
			err = -errno;
			if (result != TWB_HARD_FAIL) {
				ASSERT_OK(err, "wait diagnostic");
				result = TWB_HARD_FAIL;
			} else {
				fprintf(stderr,
					"attempt %d: elapsed diagnostic unavailable: %d\n",
					seq, err);
			}
		}
	}
	*out_wait_ns = wait_ns;
	fprintf(stderr, "attempt %d: %s (window %u wait_ns %lld)\n",
		seq, twb_attempt_name(result), (__u32)window, (long long)wait_ns);
	return result;
}

void serial_test_task_work_busy(void)
{
	struct task_work_busy *skel;
	struct bpf_link *link_tp = NULL, *link_fexit = NULL;
	cpu_set_t allowed;
	int cpu, cpu_count = 0, trigger_cpu = -1, target_cpu = -1;
	int attempt, attempts_started = 0, valid = 0, missed = 0;
	bool hard_fail = false;
	__s64 max_wait_ns = 0;

	if (sched_getaffinity(0, sizeof(allowed), &allowed)) {
		ASSERT_OK(-errno, "sched_getaffinity");
		return;
	}
	for (cpu = 0; cpu < CPU_SETSIZE && cpu_count < 2; cpu++) {
		if (!CPU_ISSET(cpu, &allowed))
			continue;
		if (!cpu_count)
			trigger_cpu = cpu;
		else
			target_cpu = cpu;
		cpu_count++;
	}
	if (cpu_count < 2) {
		printf("%s:SKIP:need two CPUs in the process affinity mask\n",
		       __func__);
		test__skip();
		return;
	}

	skel = task_work_busy__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;
	if (skel->kconfig->CONFIG_PREEMPT_RT) {
		printf("%s:SKIP:CONFIG_PREEMPT_RT=y runs irq_work in a kernel thread\n",
		       __func__);
		test__skip();
		goto cleanup;
	}

	link_tp = bpf_program__attach_tracepoint(skel->progs.busy_sched_work,
						 "syscalls",
						 "sys_enter_getppid");
	if (!ASSERT_OK_PTR(link_tp, "attach tracepoint"))
		goto cleanup;

	link_fexit = bpf_program__attach_trace(skel->progs.busy_fexit_task_work_add);
	if (!ASSERT_OK_PTR(link_fexit, "attach fexit"))
		goto cleanup;

	for (attempt = 1;
	     attempt <= TASK_WORK_BUSY_MAX_ATTEMPTS &&
	     valid < TASK_WORK_BUSY_ROUNDS;
	     attempt++) {
		enum task_work_busy_attempt res;
		__s64 wait_ns = 0;

		attempts_started = attempt;
		res = task_work_busy_run_attempt(skel, attempt, trigger_cpu,
						 target_cpu, &wait_ns);
		switch (res) {
		case TWB_VALID:
			valid++;
			if (wait_ns > max_wait_ns)
				max_wait_ns = wait_ns;
			break;
		case TWB_MISSED:
			missed++;
			break;
		case TWB_HARD_FAIL:
			hard_fail = true;
			break;
		}
		if (hard_fail)
			break;
	}

	fprintf(stderr,
		"task_work_busy: %d/%d valid, %d missed, %d attempts, max_wait %lld ns\n",
		valid, TASK_WORK_BUSY_ROUNDS, missed, attempts_started,
		(long long)max_wait_ns);

	/*
	 * The coverage assertion only makes sense when no hard
	 * failure aborted the loop; after a hard failure keep the primary
	 * reason instead of deriving more assertion noise from it.
	 */
	if (!hard_fail)
		ASSERT_EQ(valid, TASK_WORK_BUSY_ROUNDS, "valid busy windows");
cleanup:
	bpf_link__destroy(link_tp);
	bpf_link__destroy(link_fexit);
	task_work_busy__destroy(skel);
}

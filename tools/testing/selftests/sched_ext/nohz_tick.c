// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES
 *
 * Validate that a finite-slice EXT task restarts the scheduler tick when it
 * follows an infinite-slice EXT task and an idle interval on a NOHZ_FULL CPU.
 */
#define _GNU_SOURCE

#include <bpf/bpf.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <scx/common.h>

#include "nohz_tick.bpf.skel.h"
#include "scx_test.h"

#ifndef SCHED_EXT
#define SCHED_EXT 7
#endif

#define MIN_FINITE_TICKS 3
#define PHASE_TIMEOUT_MS 1000

struct nohz_tick_ctx {
	struct nohz_tick *skel;
	cpu_set_t original_mask;
	int test_cpu;
};

static int first_allowed_cpu(const cpu_set_t *mask, int first, int last)
{
	int cpu;

	for (cpu = first; cpu <= last && cpu < CPU_SETSIZE; cpu++)
		if (CPU_ISSET(cpu, mask))
			return cpu;

	return -1;
}

static int find_nohz_full_cpu(const cpu_set_t *allowed)
{
	char buf[4096], *cur, *end;
	FILE *file;

	file = fopen("/sys/devices/system/cpu/nohz_full", "r");
	if (!file)
		return -1;
	if (!fgets(buf, sizeof(buf), file)) {
		fclose(file);
		return -1;
	}
	fclose(file);

	cur = buf;
	while (*cur) {
		long first, last;
		int cpu;

		while (*cur == ' ' || *cur == '\t' || *cur == ',')
			cur++;
		if (*cur < '0' || *cur > '9')
			break;

		errno = 0;
		first = strtol(cur, &end, 10);
		if (errno || end == cur || first < 0 || first >= CPU_SETSIZE)
			return -1;
		cur = end;
		last = first;
		if (*cur == '-') {
			cur++;
			errno = 0;
			last = strtol(cur, &end, 10);
			if (errno || end == cur || last < first)
				return -1;
			cur = end;
		}

		cpu = first_allowed_cpu(allowed, first, last);
		if (cpu >= 0)
			return cpu;
	}

	return -1;
}

static pid_t start_worker(int cpu)
{
	struct sched_param param = {};
	cpu_set_t mask;
	pid_t parent;
	pid_t pid;

	parent = getpid();
	pid = fork();
	if (pid != 0)
		return pid;
	if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
		_exit(1);

	/*
	 * Become EXT before touching the target so it stays idle until wakeup.
	 */
	if (sched_setscheduler(0, SCHED_EXT, &param))
		_exit(1);

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	if (sched_setaffinity(0, sizeof(mask), &mask))
		_exit(1);

	for (;;)
		asm volatile("" ::: "memory");
}

static void stop_worker(pid_t pid)
{
	if (pid <= 0)
		return;

	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
}

static int pause_worker(pid_t pid)
{
	int status;

	if (kill(pid, SIGSTOP))
		return -errno;
	if (waitpid(pid, &status, WUNTRACED) != pid)
		return -errno;
	if (!WIFSTOPPED(status))
		return -ECHILD;

	return 0;
}

static bool wait_for_counter(const u64 *counter, u64 value, int timeout_ms)
{
	int elapsed;

	for (elapsed = 0; elapsed < timeout_ms; elapsed++) {
		if (__atomic_load_n(counter, __ATOMIC_RELAXED) >= value)
			return true;
		usleep(1000);
	}

	return false;
}

static enum scx_test_status setup(void **ctx_ptr)
{
	struct nohz_tick_ctx *ctx;
	cpu_set_t controller_mask;
	int cpu;

	ctx = calloc(1, sizeof(*ctx));
	SCX_FAIL_IF(!ctx, "Failed to allocate context");
	if (sched_getaffinity(0, sizeof(ctx->original_mask),
			      &ctx->original_mask)) {
		free(ctx);
		SCX_FAIL("Failed to get affinity (%d)", errno);
	}

	cpu = find_nohz_full_cpu(&ctx->original_mask);
	if (cpu < 0) {
		fprintf(stderr, "SKIP: no allowed NOHZ_FULL CPU\n");
		free(ctx);
		return SCX_TEST_SKIP;
	}

	controller_mask = ctx->original_mask;
	CPU_CLR(cpu, &controller_mask);
	if (CPU_COUNT(&controller_mask) == 0) {
		fprintf(stderr, "SKIP: no housekeeping CPU available\n");
		free(ctx);
		return SCX_TEST_SKIP;
	}

	ctx->test_cpu = cpu;
	ctx->skel = nohz_tick__open();
	if (!ctx->skel) {
		free(ctx);
		SCX_FAIL("Failed to open skeleton");
	}

	SCX_ENUM_INIT(ctx->skel);
	ctx->skel->rodata->test_cpu = cpu;
	ctx->skel->struct_ops.nohz_tick_ops->flags |= SCX_OPS_SWITCH_PARTIAL |
							   SCX_OPS_ENQ_LAST;
	if (nohz_tick__load(ctx->skel)) {
		nohz_tick__destroy(ctx->skel);
		free(ctx);
		SCX_FAIL("Failed to load skeleton");
	}

	if (sched_setaffinity(0, sizeof(controller_mask), &controller_mask)) {
		nohz_tick__destroy(ctx->skel);
		free(ctx);
		SCX_FAIL("Failed to move controller off CPU %d (%d)", cpu, errno);
	}

	*ctx_ptr = ctx;
	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx_ptr)
{
	struct nohz_tick_ctx *ctx = ctx_ptr;
	struct nohz_tick *skel = ctx->skel;
	struct bpf_link *link = NULL;
	enum scx_test_status status = SCX_TEST_FAIL;
	pid_t finite_worker = -1;
	pid_t inf_worker = -1;
	u64 finite_running;
	u64 finite_ticks;
	int ret;

	link = bpf_map__attach_struct_ops(skel->maps.nohz_tick_ops);
	if (!link) {
		SCX_ERR("Failed to attach scheduler");
		goto out;
	}

	/*
	 * Establish SCX_RQ_CAN_STOP_TICK with an infinite-slice task.
	 */
	inf_worker = start_worker(ctx->test_cpu);
	if (inf_worker < 0) {
		SCX_ERR("Failed to start infinite-slice worker (%d)", errno);
		goto out;
	}
	if (!wait_for_counter(&skel->bss->nr_inf_running, 1,
			      PHASE_TIMEOUT_MS)) {
		SCX_ERR("Infinite-slice worker was not scheduled");
		goto out;
	}

	/* Block without exiting so the rq retains the infinite-slice state. */
	ret = pause_worker(inf_worker);
	if (ret) {
		SCX_ERR("Failed to stop infinite-slice worker (%d)", ret);
		goto out;
	}

	/* Let the target enter idle with its tick stopped. */
	usleep(100000);

	/*
	 * The next EXT task receives a finite slice and must restart the tick.
	 */
	__atomic_store_n(&skel->bss->finite_phase, true, __ATOMIC_RELEASE);
	finite_worker = start_worker(ctx->test_cpu);
	if (finite_worker < 0) {
		SCX_ERR("Failed to start finite-slice worker (%d)", errno);
		goto out;
	}
	if (!wait_for_counter(&skel->bss->nr_finite_running, 1,
			      PHASE_TIMEOUT_MS)) {
		SCX_ERR("Finite-slice worker was not scheduled");
		goto out;
	}
	if (!wait_for_counter(&skel->bss->nr_finite_ticks, MIN_FINITE_TICKS,
			      PHASE_TIMEOUT_MS)) {
		SCX_ERR("Finite-slice worker received only %llu scheduler ticks",
			(unsigned long long)skel->bss->nr_finite_ticks);
		goto out;
	}
	stop_worker(finite_worker);
	finite_worker = -1;

	/*
	 * Leave the CPU idle after a finite-slice task. The next finite-slice
	 * task must restart the tick even though the slice type is unchanged.
	 */
	usleep(100000);
	finite_running = __atomic_load_n(&skel->bss->nr_finite_running,
					 __ATOMIC_RELAXED);
	finite_ticks = __atomic_load_n(&skel->bss->nr_finite_ticks,
				       __ATOMIC_RELAXED);

	finite_worker = start_worker(ctx->test_cpu);
	if (finite_worker < 0) {
		SCX_ERR("Failed to start second finite-slice worker (%d)", errno);
		goto out;
	}
	if (!wait_for_counter(&skel->bss->nr_finite_running,
			      finite_running + 1, PHASE_TIMEOUT_MS)) {
		SCX_ERR("Second finite-slice worker was not scheduled");
		goto out;
	}
	if (!wait_for_counter(&skel->bss->nr_finite_ticks,
			      finite_ticks + MIN_FINITE_TICKS,
			      PHASE_TIMEOUT_MS)) {
		SCX_ERR("Second finite-slice worker received only %llu scheduler ticks",
			(unsigned long long)(skel->bss->nr_finite_ticks -
					     finite_ticks));
		goto out;
	}

	if (skel->data->uei.kind != EXIT_KIND(SCX_EXIT_NONE)) {
		SCX_ERR("Scheduler exited unexpectedly (kind=%llu code=%lld)",
			(unsigned long long)skel->data->uei.kind,
			(long long)skel->data->uei.exit_code);
		goto out;
	}

	fprintf(stderr, "CPU %d received %llu finite-slice ticks\n",
		ctx->test_cpu,
		(unsigned long long)skel->bss->nr_finite_ticks);
	status = SCX_TEST_PASS;
out:
	stop_worker(finite_worker);
	stop_worker(inf_worker);
	if (link)
		bpf_link__destroy(link);
	return status;
}

static void cleanup(void *ctx_ptr)
{
	struct nohz_tick_ctx *ctx = ctx_ptr;

	sched_setaffinity(0, sizeof(ctx->original_mask), &ctx->original_mask);
	nohz_tick__destroy(ctx->skel);
	free(ctx);
}

struct scx_test nohz_tick = {
	.name = "nohz_tick",
	.description = "Verify finite EXT slices restart the NOHZ_FULL tick",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&nohz_tick)

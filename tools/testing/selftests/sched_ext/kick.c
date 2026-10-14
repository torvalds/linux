// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES
 */
#define _GNU_SOURCE
#include <bpf/bpf.h>
#include <errno.h>
#include <linux/sched.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <scx/common.h>

#include "kick.bpf.skel.h"
#include "kick_test.h"
#include "scx_test.h"
#include "util.h"

#define WAIT_LOOPS 3000

struct observation {
	u64 slice_before;
	u64 slice_at_resched;
	u64 nr_wait_callbacks;
	s32 resched_tif;
};

struct kick_ctx {
	cpu_set_t original_mask;
	int target_cpu;
};

static bool enum_supported(const char *type, const char *name)
{
	u64 value;

	return __COMPAT_read_enum(type, name, &value);
}

static enum scx_test_status setup_controller(void **ctx_ptr)
{
	struct kick_ctx *ctx;
	cpu_set_t controller_mask;
	int cpu, controller_cpu = -1;

	ctx = calloc(1, sizeof(*ctx));
	SCX_FAIL_IF(!ctx, "Failed to allocate context");
	if (sched_getaffinity(0, sizeof(ctx->original_mask), &ctx->original_mask)) {
		free(ctx);
		SCX_FAIL("Failed to get affinity (%d)", errno);
	}

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (!CPU_ISSET(cpu, &ctx->original_mask))
			continue;
		if (controller_cpu < 0) {
			controller_cpu = cpu;
		} else {
			ctx->target_cpu = cpu;
			break;
		}
	}
	if (cpu == CPU_SETSIZE) {
		printf("SKIP: two allowed CPUs are required\n");
		free(ctx);
		return SCX_TEST_SKIP;
	}

	CPU_ZERO(&controller_mask);
	CPU_SET(controller_cpu, &controller_mask);
	if (sched_setaffinity(0, sizeof(controller_mask), &controller_mask)) {
		free(ctx);
		SCX_FAIL("Failed to pin controller to CPU %d (%d)", controller_cpu, errno);
	}

	*ctx_ptr = ctx;
	return SCX_TEST_PASS;
}

static void cleanup_controller(void *ctx_ptr)
{
	struct kick_ctx *ctx = ctx_ptr;

	sched_setaffinity(0, sizeof(ctx->original_mask), &ctx->original_mask);
	free(ctx);
}

static bool wait_for_state(struct kick *skel, u32 wanted)
{
	int i;

	for (i = 0; i < WAIT_LOOPS; i++) {
		if (__atomic_load_n(&skel->bss->state, __ATOMIC_ACQUIRE) == wanted)
			return true;
		if (UEI_EXITED(skel, uei))
			return false;
		usleep(1000);
	}
	return false;
}

static bool wait_for_counter(struct kick *skel, const u64 *counter, u64 wanted)
{
	int i;

	for (i = 0; i < WAIT_LOOPS; i++) {
		if (__atomic_load_n(counter, __ATOMIC_ACQUIRE) >= wanted)
			return true;
		if (UEI_EXITED(skel, uei))
			return false;
		usleep(1000);
	}
	return false;
}

static enum scx_test_status trace_one(struct kick_ctx *ctx, u32 scenario, u64 ops_flags,
				      s32 lazy_resched_override, struct observation *obs)
{
	struct bpf_link *ops_link = NULL;
	struct kick *skel = NULL;
	struct scx_test_gated_worker challenger = { .pid = -1, .start_fd = -1 };
	struct scx_test_gated_worker victim;
	enum scx_test_status ret = SCX_TEST_FAIL;
	int cpu = ctx->target_cpu;

	victim = scx_test_spawn_gated_worker(cpu, false);
	if (victim.pid < 0) {
		SCX_ERR("Failed to spawn victim");
		return SCX_TEST_FAIL;
	}
	if (scenario == ENQ_BOTH) {
		challenger = scx_test_spawn_gated_worker(cpu, false);
		if (challenger.pid < 0) {
			SCX_ERR("Failed to spawn enqueue challenger");
			goto out;
		}
	}

	skel = kick__open();
	if (!skel) {
		SCX_ERR("Failed to open scenario %u", scenario);
		goto out;
	}
	SCX_ENUM_INIT(skel);
	skel->rodata->scenario = scenario;
	skel->rodata->victim_pid = victim.pid;
	skel->rodata->challenger_pid = challenger.pid;
	skel->rodata->target_cpu = cpu;
	skel->rodata->lazy_resched_override = lazy_resched_override;
	skel->struct_ops.kick_ops->flags |= ops_flags;
	if (scenario != KICK_LAZY_WAIT)
		bpf_program__set_autoload(skel->progs.kick_wait_callback, false);
	if (kick__load(skel)) {
		SCX_ERR("Failed to load scenario %u", scenario);
		goto out;
	}

	bpf_map__set_autoattach(skel->maps.kick_ops, false);
	if (kick__attach(skel)) {
		SCX_ERR("Failed to attach __resched_curr tracer");
		goto out;
	}
	skel->bss->state = KICK_STATE_ARMED;
	ops_link = bpf_map__attach_struct_ops(skel->maps.kick_ops);
	if (!ops_link) {
		SCX_ERR("Failed to attach scenario %u", scenario);
		goto out;
	}
	if (!scx_test_start_gated_worker(&victim)) {
		SCX_ERR("Failed to start victim");
		goto out;
	}
	if (scenario == ENQ_BOTH) {
		if (!wait_for_state(skel, KICK_STATE_QUEUED)) {
			SCX_ERR("Enqueue scenario did not arm");
			goto out;
		}
		if (!scx_test_start_gated_worker(&challenger)) {
			SCX_ERR("Failed to start enqueue challenger");
			goto out;
		}
	}
	if (!wait_for_state(skel, KICK_STATE_DONE)) {
		SCX_ERR("Scenario %u stopped in state %u, exit kind %d", scenario, skel->bss->state,
			skel->data->uei.kind);
		goto out;
	}
	if (scenario == KICK_LAZY_WAIT &&
	    !wait_for_counter(skel, &skel->bss->nr_wait_callbacks, 1)) {
		SCX_ERR("WAIT callback did not run");
		goto out;
	}

	obs->slice_before = skel->bss->slice_before;
	obs->slice_at_resched = skel->bss->slice_at_resched;
	obs->nr_wait_callbacks = skel->bss->nr_wait_callbacks;
	obs->resched_tif = skel->bss->resched_tif;
	ret = SCX_TEST_PASS;
out:
	scx_test_stop_gated_worker(&victim);
	scx_test_stop_gated_worker(&challenger);
	if (ops_link)
		bpf_link__destroy(ops_link);
	if (skel)
		kick__destroy(skel);
	return ret;
}

static bool observation_valid(const struct observation *obs)
{
	return obs->slice_before > 0 && !obs->slice_at_resched;
}

static bool trace_target_supported(const char *name, enum bpf_attach_type attach_type)
{
	return libbpf_find_vmlinux_btf_id(name, attach_type) > 0;
}

static enum scx_test_status setup_immediate(void **ctx)
{
	if (!enum_supported("scx_kick_flags", "SCX_KICK_PREEMPT")) {
		printf("SKIP: SCX_KICK_PREEMPT is not supported\n");
		return SCX_TEST_SKIP;
	}
	if (!trace_target_supported("__resched_curr", BPF_TRACE_FEXIT)) {
		printf("SKIP: __resched_curr is not available in BTF\n");
		return SCX_TEST_SKIP;
	}
	return setup_controller(ctx);
}

static enum scx_test_status setup_lazy(void **ctx)
{
	if (!enum_supported("scx_kick_flags", "SCX_KICK_PREEMPT_LAZY")) {
		printf("SKIP: SCX_KICK_PREEMPT_LAZY is not supported\n");
		return SCX_TEST_SKIP;
	}
	return setup_immediate(ctx);
}

static enum scx_test_status setup_tick(void **ctx)
{
	if (!enum_supported("scx_ops_flags", "SCX_OPS_LAZY_RESCHED")) {
		printf("SKIP: SCX_OPS_LAZY_RESCHED is not supported\n");
		return SCX_TEST_SKIP;
	}
	return setup_lazy(ctx);
}

static enum scx_test_status setup_coalesce(void **ctx)
{
	if (!enum_supported("scx_enq_flags", "SCX_ENQ_PREEMPT_LAZY")) {
		printf("SKIP: SCX_ENQ_PREEMPT_LAZY is not supported\n");
		return SCX_TEST_SKIP;
	}
	if (!trace_target_supported("kick_sync_wait_bal_cb", BPF_TRACE_FENTRY)) {
		printf("SKIP: kick_sync_wait_bal_cb is not available in BTF\n");
		return SCX_TEST_SKIP;
	}
	return setup_lazy(ctx);
}

static enum scx_test_status setup_invalid(void **ctx)
{
	if (!enum_supported("scx_kick_flags", "SCX_KICK_PREEMPT_LAZY")) {
		printf("SKIP: SCX_KICK_PREEMPT_LAZY is not supported\n");
		return SCX_TEST_SKIP;
	}
	return setup_controller(ctx);
}

static enum scx_test_status run_immediate(void *ctx)
{
	struct observation obs;
	enum scx_test_status status;

	status = trace_one(ctx, KICK_IMMEDIATE, 0, -1, &obs);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&obs));
	return SCX_TEST_PASS;
}

static enum scx_test_status run_lazy(void *ctx)
{
	struct observation immediate, lazy;
	enum scx_test_status status;
	int lazy_mode = scx_test_preempt_lazy_mode();

	if (lazy_mode < 0) {
		printf("SKIP: running kernel preemption mode is unavailable\n");
		return SCX_TEST_SKIP;
	}

	status = trace_one(ctx, KICK_IMMEDIATE, 0, -1, &immediate);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&immediate));
	status = trace_one(ctx, KICK_LAZY, 0, -1, &lazy);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&lazy));

	if (lazy_mode > 0)
		SCX_FAIL_IF(lazy.resched_tif == immediate.resched_tif,
			    "Lazy mode used immediate TIF %d", lazy.resched_tif);
	else if (!lazy_mode)
		SCX_EQ(lazy.resched_tif, immediate.resched_tif);

	return SCX_TEST_PASS;
}

static enum scx_test_status run_coalesce(void *ctx)
{
	struct observation immediate, lazy_first, immediate_first;
	struct observation plain_first, lazy_then_plain;
	struct observation both, lazy_wait, enq_both;
	enum scx_test_status status;

	if (scx_test_preempt_lazy_mode() != 1) {
		printf("SKIP: kick coalescing requires active lazy preemption\n");
		return SCX_TEST_SKIP;
	}

	status = trace_one(ctx, KICK_IMMEDIATE, 0, -1, &immediate);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&immediate));
	status = trace_one(ctx, KICK_LAZY_THEN_IMMEDIATE, 0, -1, &lazy_first);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&lazy_first));
	status = trace_one(ctx, KICK_IMMEDIATE_THEN_LAZY, 0, -1, &immediate_first);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&immediate_first));
	SCX_EQ(lazy_first.resched_tif, immediate.resched_tif);
	SCX_EQ(immediate_first.resched_tif, immediate.resched_tif);

	/*
	 * A plain kick in the same batch doesn't clear the slice by itself. The
	 * lazy preemption must still expire it, and the plain kick must still
	 * reschedule immediately, whichever came first.
	 */
	status = trace_one(ctx, KICK_PLAIN_THEN_LAZY, 0, -1, &plain_first);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&plain_first));
	status = trace_one(ctx, KICK_LAZY_THEN_PLAIN, 0, -1, &lazy_then_plain);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&lazy_then_plain));
	SCX_EQ(plain_first.resched_tif, immediate.resched_tif);
	SCX_EQ(lazy_then_plain.resched_tif, immediate.resched_tif);

	/* Immediate kick and WAIT both take precedence over lazy preemption. */
	status = trace_one(ctx, KICK_BOTH, 0, -1, &both);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&both));
	status = trace_one(ctx, KICK_LAZY_WAIT, 0, -1, &lazy_wait);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&lazy_wait));
	SCX_EQ(both.resched_tif, immediate.resched_tif);
	SCX_EQ(lazy_wait.resched_tif, immediate.resched_tif);
	SCX_GT(lazy_wait.nr_wait_callbacks, 0);

	/* Immediate enqueue preemption likewise takes precedence over lazy. */
	status = trace_one(ctx, ENQ_BOTH, 0, -1, &enq_both);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&enq_both));
	SCX_EQ(enq_both.resched_tif, immediate.resched_tif);
	return SCX_TEST_PASS;
}

/*
 * A slice running out at the tick reschedules immediately by default and lazily
 * with SCX_OPS_LAZY_RESCHED, the way fair.c expires a slice.
 */
static enum scx_test_status run_tick(void *ctx)
{
	struct observation immediate, lazy, force_lazy, force_immediate;
	enum scx_test_status status;
	u64 lazy_flag;
	int lazy_mode = scx_test_preempt_lazy_mode();
	bool found;

	if (lazy_mode < 0) {
		printf("SKIP: running kernel preemption mode is unavailable\n");
		return SCX_TEST_SKIP;
	}

	found = __COMPAT_read_enum("scx_ops_flags", "SCX_OPS_LAZY_RESCHED", &lazy_flag);
	SCX_ASSERT(found);
	status = trace_one(ctx, TICK_EXPIRY, 0, -1, &immediate);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&immediate));
	status = trace_one(ctx, TICK_EXPIRY, lazy_flag, -1, &lazy);
	SCX_EQ(status, SCX_TEST_PASS);
	SCX_ASSERT(observation_valid(&lazy));
	if (lazy_mode > 0) {
		SCX_FAIL_IF(lazy.resched_tif == immediate.resched_tif,
			    "Lazy slice expiry used immediate TIF %d", lazy.resched_tif);

		status = trace_one(ctx, TICK_EXPIRY, 0, 1, &force_lazy);
		SCX_EQ(status, SCX_TEST_PASS);
		SCX_ASSERT(observation_valid(&force_lazy));
		status = trace_one(ctx, TICK_EXPIRY, lazy_flag, 0, &force_immediate);
		SCX_EQ(status, SCX_TEST_PASS);
		SCX_ASSERT(observation_valid(&force_immediate));
		SCX_EQ(force_lazy.resched_tif, lazy.resched_tif);
		SCX_EQ(force_immediate.resched_tif, immediate.resched_tif);
	} else {
		SCX_EQ(lazy.resched_tif, immediate.resched_tif);
	}

	return SCX_TEST_PASS;
}

static enum scx_test_status invalid_one(struct kick_ctx *ctx, u32 scenario)
{
	struct bpf_link *ops_link = NULL;
	struct kick *skel = NULL;
	struct scx_test_gated_worker victim;
	enum scx_test_status ret = SCX_TEST_FAIL;
	int cpu = ctx->target_cpu;
	int i;
	u64 error_kind;

	error_kind = EXIT_KIND(SCX_EXIT_ERROR);
	victim = scx_test_spawn_gated_worker(cpu, false);
	if (victim.pid < 0)
		return SCX_TEST_FAIL;

	skel = kick__open();
	if (!skel)
		goto out;
	SCX_ENUM_INIT(skel);
	skel->rodata->scenario = scenario;
	bpf_program__set_autoload(skel->progs.kick_need_resched, false);
	bpf_program__set_autoload(skel->progs.kick_wait_callback, false);
	if (kick__load(skel))
		goto out;
	ops_link = bpf_map__attach_struct_ops(skel->maps.kick_ops);
	if (!ops_link || !scx_test_start_gated_worker(&victim))
		goto out;

	for (i = 0; i < WAIT_LOOPS; i++) {
		if (UEI_EXITED(skel, uei) == error_kind) {
			ret = SCX_TEST_PASS;
			break;
		}
		usleep(1000);
	}
out:
	scx_test_stop_gated_worker(&victim);
	if (ops_link)
		bpf_link__destroy(ops_link);
	if (skel)
		kick__destroy(skel);
	return ret;
}

static enum scx_test_status run_invalid(void *ctx)
{
	enum scx_test_status status;
	u32 scenario;

	for (scenario = INVALID_KICK_IDLE;
	     scenario <= INVALID_KICK_UNKNOWN; scenario++) {
		status = invalid_one(ctx, scenario);
		SCX_EQ(status, SCX_TEST_PASS);
	}
	return SCX_TEST_PASS;
}

static struct scx_test kick_immediate = {
	.name = "kick_immediate",
	.description = "Trace immediate kick slice expiration and rescheduling",
	.setup = setup_immediate,
	.run = run_immediate,
	.cleanup = cleanup_controller,
};

static struct scx_test kick_lazy = {
	.name = "kick_lazy",
	.description = "Trace lazy kick slice expiration and rescheduling",
	.setup = setup_lazy,
	.run = run_lazy,
	.cleanup = cleanup_controller,
};

static struct scx_test kick_coalesce = {
	.name = "kick_coalesce",
	.description = "Verify lazy preemption coalesces with immediate requests",
	.setup = setup_coalesce,
	.run = run_coalesce,
	.cleanup = cleanup_controller,
};

static struct scx_test kick_tick = {
	.name = "kick_tick",
	.description = "Trace slice expiry at the tick, immediate and lazy",
	.setup = setup_tick,
	.run = run_tick,
	.cleanup = cleanup_controller,
};

static struct scx_test kick_invalid = {
	.name = "kick_invalid",
	.description = "Verify invalid kick flag combinations fail",
	.setup = setup_invalid,
	.run = run_invalid,
	.cleanup = cleanup_controller,
};

__attribute__((constructor))
static void register_kick_tests(void)
{
	scx_test_register(&kick_immediate);
	scx_test_register(&kick_lazy);
	scx_test_register(&kick_coalesce);
	scx_test_register(&kick_tick);
	scx_test_register(&kick_invalid);
}

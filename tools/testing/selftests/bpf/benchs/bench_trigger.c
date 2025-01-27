// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#define _GNU_SOURCE
#include <argp.h>
#include <unistd.h>
#include <stdint.h>
#include "bpf_util.h"
#include "bench.h"
#include "trigger_bench.skel.h"
#include "trace_helpers.h"

#define MAX_TRIG_BATCH_ITERS 1000

static struct {
	__u32 batch_iters;
} args = {
	.batch_iters = 100,
};

enum {
	ARG_TRIG_BATCH_ITERS = 7000,
};

static const struct argp_option opts[] = {
	{ "trig-batch-iters", ARG_TRIG_BATCH_ITERS, "BATCH_ITER_CNT", 0,
		"Number of in-kernel iterations per one driver test run"},
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	long ret;

	switch (key) {
	case ARG_TRIG_BATCH_ITERS:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > MAX_TRIG_BATCH_ITERS) {
			fprintf(stderr, "invalid --trig-batch-iters value (should be between %d and %d)\n",
				1, MAX_TRIG_BATCH_ITERS);
			argp_usage(state);
		}
		args.batch_iters = ret;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

const struct argp bench_trigger_batch_argp = {
	.options = opts,
	.parser = parse_arg,
};

/* adjust slot shift in inc_hits() if changing */
#define MAX_BUCKETS 256

#pragma GCC diagnostic ignored "-Wattributes"

/* BPF triggering benchmarks */
static struct trigger_ctx {
	struct trigger_bench *skel;
	bool usermode_counters;
	int driver_prog_fd;
} ctx;

static struct counter base_hits[MAX_BUCKETS];

static __always_inline void inc_counter(struct counter *counters)
{
	static __thread int tid = 0;
	unsigned slot;

	if (unlikely(tid == 0))
		tid = sys_gettid();

	/* multiplicative hashing, it's fast */
	slot = 2654435769U * tid;
	slot >>= 24;

	atomic_inc(&base_hits[slot].value); /* use highest byte as an index */
}

static long sum_and_reset_counters(struct counter *counters)
{
	int i;
	long sum = 0;

	for (i = 0; i < MAX_BUCKETS; i++)
		sum += atomic_swap(&counters[i].value, 0);
	return sum;
}

static void trigger_validate(void)
{
	if (env.consumer_cnt != 0) {
		fprintf(stderr, "benchmark doesn't support consumer!\n");
		exit(1);
	}
}

static void *trigger_producer(void *input)
{
	if (ctx.usermode_counters) {
		while (true) {
			(void)syscall(__NR_getpgid);
			inc_counter(base_hits);
		}
	} else {
		while (true)
			(void)syscall(__NR_getpgid);
	}
	return NULL;
}

static void *trigger_producer_batch(void *input)
{
	int fd = ctx.driver_prog_fd ?: bpf_program__fd(ctx.skel->progs.trigger_driver);

	while (true)
		bpf_prog_test_run_opts(fd, NULL);

	return NULL;
}

static void trigger_measure(struct bench_res *res)
{
	if (ctx.usermode_counters)
		res->hits = sum_and_reset_counters(base_hits);
	else
		res->hits = sum_and_reset_counters(ctx.skel->bss->hits);
}

static void setup_ctx(void)
{
	setup_libbpf();

	ctx.skel = trigger_bench__open();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	/* default "driver" BPF program */
	bpf_program__set_autoload(ctx.skel->progs.trigger_driver, true);

	ctx.skel->rodata->batch_iters = args.batch_iters;
}

static void load_ctx(void)
{
	int err;

	err = trigger_bench__load(ctx.skel);
	if (err) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}
}

static void attach_bpf(struct bpf_program *prog)
{
	struct bpf_link *link;

	link = bpf_program__attach(prog);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}
}

static void trigger_syscall_count_setup(void)
{
	ctx.usermode_counters = true;
}

/* Batched, staying mostly in-kernel triggering setups */
static void trigger_kernel_count_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.trigger_driver, false);
	bpf_program__set_autoload(ctx.skel->progs.trigger_count, true);
	load_ctx();
	/* override driver program */
	ctx.driver_prog_fd = bpf_program__fd(ctx.skel->progs.trigger_count);
}

static void trigger_kprobe_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.bench_trigger_kprobe, true);
	load_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_kprobe);
}

static void trigger_kretprobe_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.bench_trigger_kretprobe, true);
	load_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_kretprobe);
}

static void trigger_kprobe_multi_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.bench_trigger_kprobe_multi, true);
	load_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_kprobe_multi);
}

static void trigger_kretprobe_multi_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.bench_trigger_kretprobe_multi, true);
	load_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_kretprobe_multi);
}

static void trigger_fentry_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.bench_trigger_fentry, true);
	load_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_fentry);
}

static void trigger_fexit_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.bench_trigger_fexit, true);
	load_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_fexit);
}

static void trigger_fmodret_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.trigger_driver, false);
	bpf_program__set_autoload(ctx.skel->progs.trigger_driver_kfunc, true);
	bpf_program__set_autoload(ctx.skel->progs.bench_trigger_fmodret, true);
	load_ctx();
	/* override driver program */
	ctx.driver_prog_fd = bpf_program__fd(ctx.skel->progs.trigger_driver_kfunc);
	attach_bpf(ctx.skel->progs.bench_trigger_fmodret);
}

static void trigger_tp_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.trigger_driver, false);
	bpf_program__set_autoload(ctx.skel->progs.trigger_driver_kfunc, true);
	bpf_program__set_autoload(ctx.skel->progs.bench_trigger_tp, true);
	load_ctx();
	/* override driver program */
	ctx.driver_prog_fd = bpf_program__fd(ctx.skel->progs.trigger_driver_kfunc);
	attach_bpf(ctx.skel->progs.bench_trigger_tp);
}

static void trigger_rawtp_setup(void)
{
	setup_ctx();
	bpf_program__set_autoload(ctx.skel->progs.trigger_driver, false);
	bpf_program__set_autoload(ctx.skel->progs.trigger_driver_kfunc, true);
	bpf_program__set_autoload(ctx.skel->progs.bench_trigger_rawtp, true);
	load_ctx();
	/* override driver program */
	ctx.driver_prog_fd = bpf_program__fd(ctx.skel->progs.trigger_driver_kfunc);
	attach_bpf(ctx.skel->progs.bench_trigger_rawtp);
}

/* make sure call is not inlined and not avoided by compiler, so __weak and
 * inline asm volatile in the body of the function
 *
 * There is a performance difference between uprobing at nop location vs other
 * instructions. So use two different targets, one of which starts with nop
 * and another doesn't.
 *
 * GCC doesn't generate stack setup preamble for these functions due to them
 * having no input arguments and doing nothing in the body.
 */
__nocf_check __weak void uprobe_target_nop(void)
{
	asm volatile ("nop");
}

__weak void opaque_noop_func(void)
{
}

__nocf_check __weak int uprobe_target_push(void)
{
	/* overhead of function call is negligible compared to uprobe
	 * triggering, so this shouldn't affect benchmark results much
	 */
	opaque_noop_func();
	return 1;
}

__nocf_check __weak void uprobe_target_ret(void)
{
	asm volatile ("");
}

static void *uprobe_producer_count(void *input)
{
	while (true) {
		uprobe_target_nop();
		inc_counter(base_hits);
	}
	return NULL;
}

static void *uprobe_producer_nop(void *input)
{
	while (true)
		uprobe_target_nop();
	return NULL;
}

static void *uprobe_producer_push(void *input)
{
	while (true)
		uprobe_target_push();
	return NULL;
}

static void *uprobe_producer_ret(void *input)
{
	while (true)
		uprobe_target_ret();
	return NULL;
}

static void usetup(bool use_retprobe, bool use_multi, void *target_addr)
{
	size_t uprobe_offset;
	struct bpf_link *link;
	int err;

	setup_libbpf();

	ctx.skel = trigger_bench__open();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	if (use_multi)
		bpf_program__set_autoload(ctx.skel->progs.bench_trigger_uprobe_multi, true);
	else
		bpf_program__set_autoload(ctx.skel->progs.bench_trigger_uprobe, true);

	err = trigger_bench__load(ctx.skel);
	if (err) {
		fprintf(stderr, "failed to load skeleton\n");
		exit(1);
	}

	uprobe_offset = get_uprobe_offset(target_addr);
	if (use_multi) {
		LIBBPF_OPTS(bpf_uprobe_multi_opts, opts,
			.retprobe = use_retprobe,
			.cnt = 1,
			.offsets = &uprobe_offset,
		);
		link = bpf_program__attach_uprobe_multi(
			ctx.skel->progs.bench_trigger_uprobe_multi,
			-1 /* all PIDs */, "/proc/self/exe", NULL, &opts);
		ctx.skel->links.bench_trigger_uprobe_multi = link;
	} else {
		link = bpf_program__attach_uprobe(ctx.skel->progs.bench_trigger_uprobe,
						  use_retprobe,
						  -1 /* all PIDs */,
						  "/proc/self/exe",
						  uprobe_offset);
		ctx.skel->links.bench_trigger_uprobe = link;
	}
	if (!link) {
		fprintf(stderr, "failed to attach %s!\n", use_multi ? "multi-uprobe" : "uprobe");
		exit(1);
	}
}

static void usermode_count_setup(void)
{
	ctx.usermode_counters = true;
}

static void uprobe_nop_setup(void)
{
	usetup(false, false /* !use_multi */, &uprobe_target_nop);
}

static void uretprobe_nop_setup(void)
{
	usetup(true, false /* !use_multi */, &uprobe_target_nop);
}

static void uprobe_push_setup(void)
{
	usetup(false, false /* !use_multi */, &uprobe_target_push);
}

static void uretprobe_push_setup(void)
{
	usetup(true, false /* !use_multi */, &uprobe_target_push);
}

static void uprobe_ret_setup(void)
{
	usetup(false, false /* !use_multi */, &uprobe_target_ret);
}

static void uretprobe_ret_setup(void)
{
	usetup(true, false /* !use_multi */, &uprobe_target_ret);
}

static void uprobe_multi_nop_setup(void)
{
	usetup(false, true /* use_multi */, &uprobe_target_nop);
}

static void uretprobe_multi_nop_setup(void)
{
	usetup(true, true /* use_multi */, &uprobe_target_nop);
}

static void uprobe_multi_push_setup(void)
{
	usetup(false, true /* use_multi */, &uprobe_target_push);
}

static void uretprobe_multi_push_setup(void)
{
	usetup(true, true /* use_multi */, &uprobe_target_push);
}

static void uprobe_multi_ret_setup(void)
{
	usetup(false, true /* use_multi */, &uprobe_target_ret);
}

static void uretprobe_multi_ret_setup(void)
{
	usetup(true, true /* use_multi */, &uprobe_target_ret);
}

const struct bench bench_trig_syscall_count = {
	.name = "trig-syscall-count",
	.validate = trigger_validate,
	.setup = trigger_syscall_count_setup,
	.producer_thread = trigger_producer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

/* batched (staying mostly in kernel) kprobe/fentry benchmarks */
#define BENCH_TRIG_KERNEL(KIND, NAME)					\
const struct bench bench_trig_##KIND = {				\
	.name = "trig-" NAME,						\
	.setup = trigger_##KIND##_setup,				\
	.producer_thread = trigger_producer_batch,			\
	.measure = trigger_measure,					\
	.report_progress = hits_drops_report_progress,			\
	.report_final = hits_drops_report_final,			\
	.argp = &bench_trigger_batch_argp,				\
}

BENCH_TRIG_KERNEL(kernel_count, "kernel-count");
BENCH_TRIG_KERNEL(kprobe, "kprobe");
BENCH_TRIG_KERNEL(kretprobe, "kretprobe");
BENCH_TRIG_KERNEL(kprobe_multi, "kprobe-multi");
BENCH_TRIG_KERNEL(kretprobe_multi, "kretprobe-multi");
BENCH_TRIG_KERNEL(fentry, "fentry");
BENCH_TRIG_KERNEL(fexit, "fexit");
BENCH_TRIG_KERNEL(fmodret, "fmodret");
BENCH_TRIG_KERNEL(tp, "tp");
BENCH_TRIG_KERNEL(rawtp, "rawtp");

/* uprobe benchmarks */
#define BENCH_TRIG_USERMODE(KIND, PRODUCER, NAME)			\
const struct bench bench_trig_##KIND = {				\
	.name = "trig-" NAME,						\
	.validate = trigger_validate,					\
	.setup = KIND##_setup,						\
	.producer_thread = uprobe_producer_##PRODUCER,			\
	.measure = trigger_measure,					\
	.report_progress = hits_drops_report_progress,			\
	.report_final = hits_drops_report_final,			\
}

BENCH_TRIG_USERMODE(usermode_count, count, "usermode-count");
BENCH_TRIG_USERMODE(uprobe_nop, nop, "uprobe-nop");
BENCH_TRIG_USERMODE(uprobe_push, push, "uprobe-push");
BENCH_TRIG_USERMODE(uprobe_ret, ret, "uprobe-ret");
BENCH_TRIG_USERMODE(uretprobe_nop, nop, "uretprobe-nop");
BENCH_TRIG_USERMODE(uretprobe_push, push, "uretprobe-push");
BENCH_TRIG_USERMODE(uretprobe_ret, ret, "uretprobe-ret");
BENCH_TRIG_USERMODE(uprobe_multi_nop, nop, "uprobe-multi-nop");
BENCH_TRIG_USERMODE(uprobe_multi_push, push, "uprobe-multi-push");
BENCH_TRIG_USERMODE(uprobe_multi_ret, ret, "uprobe-multi-ret");
BENCH_TRIG_USERMODE(uretprobe_multi_nop, nop, "uretprobe-multi-nop");
BENCH_TRIG_USERMODE(uretprobe_multi_push, push, "uretprobe-multi-push");
BENCH_TRIG_USERMODE(uretprobe_multi_ret, ret, "uretprobe-multi-ret");

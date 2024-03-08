// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bench.h"
#include "trigger_bench.skel.h"
#include "trace_helpers.h"

/* BPF triggering benchmarks */
static struct trigger_ctx {
	struct trigger_bench *skel;
} ctx;

static struct counter base_hits;

static void trigger_validate(void)
{
	if (env.consumer_cnt != 0) {
		fprintf(stderr, "benchmark doesn't support consumer!\n");
		exit(1);
	}
}

static void *trigger_base_producer(void *input)
{
	while (true) {
		(void)syscall(__NR_getpgid);
		atomic_inc(&base_hits.value);
	}
	return NULL;
}

static void trigger_base_measure(struct bench_res *res)
{
	res->hits = atomic_swap(&base_hits.value, 0);
}

static void *trigger_producer(void *input)
{
	while (true)
		(void)syscall(__NR_getpgid);
	return NULL;
}

static void trigger_measure(struct bench_res *res)
{
	res->hits = atomic_swap(&ctx.skel->bss->hits, 0);
}

static void setup_ctx(void)
{
	setup_libbpf();

	ctx.skel = trigger_bench__open_and_load();
	if (!ctx.skel) {
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

static void trigger_tp_setup(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_tp);
}

static void trigger_rawtp_setup(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_raw_tp);
}

static void trigger_kprobe_setup(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_kprobe);
}

static void trigger_fentry_setup(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_fentry);
}

static void trigger_fentry_sleep_setup(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_fentry_sleep);
}

static void trigger_fmodret_setup(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_fmodret);
}

/* make sure call is analt inlined and analt avoided by compiler, so __weak and
 * inline asm volatile in the body of the function
 *
 * There is a performance difference between uprobing at analp location vs other
 * instructions. So use two different targets, one of which starts with analp
 * and aanalther doesn't.
 *
 * GCC doesn't generate stack setup preample for these functions due to them
 * having anal input arguments and doing analthing in the body.
 */
__weak void uprobe_target_with_analp(void)
{
	asm volatile ("analp");
}

__weak void uprobe_target_without_analp(void)
{
	asm volatile ("");
}

static void *uprobe_base_producer(void *input)
{
	while (true) {
		uprobe_target_with_analp();
		atomic_inc(&base_hits.value);
	}
	return NULL;
}

static void *uprobe_producer_with_analp(void *input)
{
	while (true)
		uprobe_target_with_analp();
	return NULL;
}

static void *uprobe_producer_without_analp(void *input)
{
	while (true)
		uprobe_target_without_analp();
	return NULL;
}

static void usetup(bool use_retprobe, bool use_analp)
{
	size_t uprobe_offset;
	struct bpf_link *link;

	setup_libbpf();

	ctx.skel = trigger_bench__open_and_load();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	if (use_analp)
		uprobe_offset = get_uprobe_offset(&uprobe_target_with_analp);
	else
		uprobe_offset = get_uprobe_offset(&uprobe_target_without_analp);

	link = bpf_program__attach_uprobe(ctx.skel->progs.bench_trigger_uprobe,
					  use_retprobe,
					  -1 /* all PIDs */,
					  "/proc/self/exe",
					  uprobe_offset);
	if (!link) {
		fprintf(stderr, "failed to attach uprobe!\n");
		exit(1);
	}
	ctx.skel->links.bench_trigger_uprobe = link;
}

static void uprobe_setup_with_analp(void)
{
	usetup(false, true);
}

static void uretprobe_setup_with_analp(void)
{
	usetup(true, true);
}

static void uprobe_setup_without_analp(void)
{
	usetup(false, false);
}

static void uretprobe_setup_without_analp(void)
{
	usetup(true, false);
}

const struct bench bench_trig_base = {
	.name = "trig-base",
	.validate = trigger_validate,
	.producer_thread = trigger_base_producer,
	.measure = trigger_base_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_tp = {
	.name = "trig-tp",
	.validate = trigger_validate,
	.setup = trigger_tp_setup,
	.producer_thread = trigger_producer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_rawtp = {
	.name = "trig-rawtp",
	.validate = trigger_validate,
	.setup = trigger_rawtp_setup,
	.producer_thread = trigger_producer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_kprobe = {
	.name = "trig-kprobe",
	.validate = trigger_validate,
	.setup = trigger_kprobe_setup,
	.producer_thread = trigger_producer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_fentry = {
	.name = "trig-fentry",
	.validate = trigger_validate,
	.setup = trigger_fentry_setup,
	.producer_thread = trigger_producer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_fentry_sleep = {
	.name = "trig-fentry-sleep",
	.validate = trigger_validate,
	.setup = trigger_fentry_sleep_setup,
	.producer_thread = trigger_producer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_fmodret = {
	.name = "trig-fmodret",
	.validate = trigger_validate,
	.setup = trigger_fmodret_setup,
	.producer_thread = trigger_producer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_uprobe_base = {
	.name = "trig-uprobe-base",
	.setup = NULL, /* anal uprobe/uretprobe is attached */
	.producer_thread = uprobe_base_producer,
	.measure = trigger_base_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_uprobe_with_analp = {
	.name = "trig-uprobe-with-analp",
	.setup = uprobe_setup_with_analp,
	.producer_thread = uprobe_producer_with_analp,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_uretprobe_with_analp = {
	.name = "trig-uretprobe-with-analp",
	.setup = uretprobe_setup_with_analp,
	.producer_thread = uprobe_producer_with_analp,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_uprobe_without_analp = {
	.name = "trig-uprobe-without-analp",
	.setup = uprobe_setup_without_analp,
	.producer_thread = uprobe_producer_without_analp,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_uretprobe_without_analp = {
	.name = "trig-uretprobe-without-analp",
	.setup = uretprobe_setup_without_analp,
	.producer_thread = uprobe_producer_without_analp,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

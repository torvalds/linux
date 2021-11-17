// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bench.h"
#include "trigger_bench.skel.h"

/* BPF triggering benchmarks */
static struct trigger_ctx {
	struct trigger_bench *skel;
} ctx;

static struct counter base_hits;

static void trigger_validate()
{
	if (env.consumer_cnt != 1) {
		fprintf(stderr, "benchmark doesn't support multi-consumer!\n");
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

static void setup_ctx()
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

static void trigger_tp_setup()
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_tp);
}

static void trigger_rawtp_setup()
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_raw_tp);
}

static void trigger_kprobe_setup()
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_kprobe);
}

static void trigger_fentry_setup()
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_fentry);
}

static void trigger_fentry_sleep_setup()
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_fentry_sleep);
}

static void trigger_fmodret_setup()
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.bench_trigger_fmodret);
}

static void *trigger_consumer(void *input)
{
	return NULL;
}

const struct bench bench_trig_base = {
	.name = "trig-base",
	.validate = trigger_validate,
	.producer_thread = trigger_base_producer,
	.consumer_thread = trigger_consumer,
	.measure = trigger_base_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_tp = {
	.name = "trig-tp",
	.validate = trigger_validate,
	.setup = trigger_tp_setup,
	.producer_thread = trigger_producer,
	.consumer_thread = trigger_consumer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_rawtp = {
	.name = "trig-rawtp",
	.validate = trigger_validate,
	.setup = trigger_rawtp_setup,
	.producer_thread = trigger_producer,
	.consumer_thread = trigger_consumer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_kprobe = {
	.name = "trig-kprobe",
	.validate = trigger_validate,
	.setup = trigger_kprobe_setup,
	.producer_thread = trigger_producer,
	.consumer_thread = trigger_consumer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_fentry = {
	.name = "trig-fentry",
	.validate = trigger_validate,
	.setup = trigger_fentry_setup,
	.producer_thread = trigger_producer,
	.consumer_thread = trigger_consumer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_fentry_sleep = {
	.name = "trig-fentry-sleep",
	.validate = trigger_validate,
	.setup = trigger_fentry_sleep_setup,
	.producer_thread = trigger_producer,
	.consumer_thread = trigger_consumer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_trig_fmodret = {
	.name = "trig-fmodret",
	.validate = trigger_validate,
	.setup = trigger_fmodret_setup,
	.producer_thread = trigger_producer,
	.consumer_thread = trigger_consumer,
	.measure = trigger_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

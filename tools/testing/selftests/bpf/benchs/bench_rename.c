// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <fcntl.h>
#include "bench.h"
#include "test_overhead.skel.h"

/* BPF triggering benchmarks */
static struct ctx {
	struct test_overhead *skel;
	struct counter hits;
	int fd;
} ctx;

static void validate(void)
{
	if (env.producer_cnt != 1) {
		fprintf(stderr, "benchmark doesn't support multi-producer!\n");
		exit(1);
	}
	if (env.consumer_cnt != 1) {
		fprintf(stderr, "benchmark doesn't support multi-consumer!\n");
		exit(1);
	}
}

static void *producer(void *input)
{
	char buf[] = "test_overhead";
	int err;

	while (true) {
		err = write(ctx.fd, buf, sizeof(buf));
		if (err < 0) {
			fprintf(stderr, "write failed\n");
			exit(1);
		}
		atomic_inc(&ctx.hits.value);
	}
}

static void measure(struct bench_res *res)
{
	res->hits = atomic_swap(&ctx.hits.value, 0);
}

static void setup_ctx(void)
{
	setup_libbpf();

	ctx.skel = test_overhead__open_and_load();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	ctx.fd = open("/proc/self/comm", O_WRONLY|O_TRUNC);
	if (ctx.fd < 0) {
		fprintf(stderr, "failed to open /proc/self/comm: %d\n", -errno);
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

static void setup_base(void)
{
	setup_ctx();
}

static void setup_kprobe(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.prog1);
}

static void setup_kretprobe(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.prog2);
}

static void setup_rawtp(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.prog3);
}

static void setup_fentry(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.prog4);
}

static void setup_fexit(void)
{
	setup_ctx();
	attach_bpf(ctx.skel->progs.prog5);
}

static void *consumer(void *input)
{
	return NULL;
}

const struct bench bench_rename_base = {
	.name = "rename-base",
	.validate = validate,
	.setup = setup_base,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_rename_kprobe = {
	.name = "rename-kprobe",
	.validate = validate,
	.setup = setup_kprobe,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_rename_kretprobe = {
	.name = "rename-kretprobe",
	.validate = validate,
	.setup = setup_kretprobe,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_rename_rawtp = {
	.name = "rename-rawtp",
	.validate = validate,
	.setup = setup_rawtp,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_rename_fentry = {
	.name = "rename-fentry",
	.validate = validate,
	.setup = setup_fentry,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_rename_fexit = {
	.name = "rename-fexit",
	.validate = validate,
	.setup = setup_fexit,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

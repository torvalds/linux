// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include "bench.h"
#include "bench_bpf_timing.h"
#include "bpf_nop_bench.skel.h"
#include "bpf_util.h"

static struct ctx {
	struct bpf_nop_bench *skel;
	struct bpf_bench_timing timing;
	int prog_fd;
} ctx;

static void nop_validate(void)
{
	if (env.consumer_cnt != 0) {
		fprintf(stderr, "benchmark doesn't support consumers\n");
		exit(1);
	}
}

static void nop_run_once(void *unused __always_unused)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	bpf_prog_test_run_opts(ctx.prog_fd, &topts);
}

static void nop_setup(void)
{
	struct bpf_nop_bench *skel;
	int err;

	setup_libbpf();

	skel = bpf_nop_bench__open();
	if (!skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	err = bpf_nop_bench__load(skel);
	if (err) {
		fprintf(stderr, "failed to load skeleton: %s\n", strerror(-err));
		bpf_nop_bench__destroy(skel);
		exit(1);
	}

	ctx.skel = skel;
	ctx.prog_fd = bpf_program__fd(skel->progs.bench_nop);

	BENCH_TIMING_INIT(&ctx.timing, skel, 0);
	bpf_bench_calibrate(&ctx.timing, nop_run_once, NULL);

	env.duration_sec = 600;
}

static void *nop_producer(void *input)
{
	while (true)
		nop_run_once(NULL);

	return NULL;
}

static void nop_measure(struct bench_res *res)
{
	bpf_bench_timing_measure(&ctx.timing, res);
}

static void nop_report_final(struct bench_res res[], int res_cnt)
{
	bpf_bench_timing_report(&ctx.timing, "bpf-nop", NULL);
}

const struct bench bench_bpf_nop = {
	.name		= "bpf-nop",
	.validate	= nop_validate,
	.setup		= nop_setup,
	.producer_thread = nop_producer,
	.measure	= nop_measure,
	.report_final	= nop_report_final,
};

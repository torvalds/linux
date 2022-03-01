// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <argp.h>
#include "bench.h"
#include "bpf_loop_bench.skel.h"

/* BPF triggering benchmarks */
static struct ctx {
	struct bpf_loop_bench *skel;
} ctx;

static struct {
	__u32 nr_loops;
} args = {
	.nr_loops = 10,
};

enum {
	ARG_NR_LOOPS = 4000,
};

static const struct argp_option opts[] = {
	{ "nr_loops", ARG_NR_LOOPS, "nr_loops", 0,
		"Set number of loops for the bpf_loop helper"},
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case ARG_NR_LOOPS:
		args.nr_loops = strtol(arg, NULL, 10);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

/* exported into benchmark runner */
const struct argp bench_bpf_loop_argp = {
	.options = opts,
	.parser = parse_arg,
};

static void validate(void)
{
	if (env.consumer_cnt != 1) {
		fprintf(stderr, "benchmark doesn't support multi-consumer!\n");
		exit(1);
	}
}

static void *producer(void *input)
{
	while (true)
		/* trigger the bpf program */
		syscall(__NR_getpgid);

	return NULL;
}

static void *consumer(void *input)
{
	return NULL;
}

static void measure(struct bench_res *res)
{
	res->hits = atomic_swap(&ctx.skel->bss->hits, 0);
}

static void setup(void)
{
	struct bpf_link *link;

	setup_libbpf();

	ctx.skel = bpf_loop_bench__open_and_load();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	link = bpf_program__attach(ctx.skel->progs.benchmark);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}

	ctx.skel->bss->nr_loops = args.nr_loops;
}

const struct bench bench_bpf_loop = {
	.name = "bpf-loop",
	.validate = validate,
	.setup = setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = ops_report_progress,
	.report_final = ops_report_final,
};

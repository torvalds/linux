// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <argp.h>
#include <limits.h>
#include <string.h>

#include "bench.h"

#include <libarena/common.h>
#include <libarena/asan.h>
#include <libarena/buddy.h>
#include <libarena/userspace.h>

#include "libarena/libarena_bench.skel.h"

static struct {
	__u64 alloc_size;
	__u64 nallocs;
} args = {
	.alloc_size = 64,
	.nallocs = 10000,
};

static struct {
	struct libarena_bench *skel;
	int bench_fd;
	int reset_fd;
} ctx;

enum {
	ARG_LIBARENA_ALLOC_SIZE = 12000,
	ARG_LIBARENA_NALLOCS,
};

static const struct argp_option opts[] = {
	{ "alloc_size", ARG_LIBARENA_ALLOC_SIZE, "BYTES", 0,
	  "Size of each arena allocation" },
	{ "nallocs", ARG_LIBARENA_NALLOCS, "ITERS", 0,
	  "Number of allocation per measurement" },
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	unsigned long value;

	switch (key) {
	case ARG_LIBARENA_ALLOC_SIZE:
		value = strtoull(arg, NULL, 10);
		if (!value || value >= UINT_MAX) {
			fprintf(stderr, "invalid alloc_size: %ld", value);
			argp_usage(state);
		}
		args.alloc_size = value;
		break;
	case ARG_LIBARENA_NALLOCS:
		args.nallocs = strtoull(arg, NULL, 10);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

const struct argp bench_libarena_argp = {
	.options = opts,
	.parser = parse_arg,
};

static void validate(void)
{
	if (env.consumer_cnt != 0) {
		fprintf(stderr, "benchmark doesn't support consumers\n");
		exit(1);
	}

	if (env.producer_cnt != 1) {
		fprintf(stderr, "benchmark supports exactly one producer\n");
		exit(1);
	}
}

static void setup_common(void)
{
	struct arena_alloc_reserve_args reserve_args = {
		.nr_pages = ARENA_RESERVE_PAGES_DFL,
	};
	int err;

	setup_libbpf();

	ctx.skel = libarena_bench__open_and_load();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open and load skeleton\n");
		exit(1);
	}

	err = libarena_run_prog_args(
		bpf_program__fd(ctx.skel->progs.arena_alloc_reserve),
		&reserve_args, sizeof(reserve_args));
	if (err) {
		fprintf(stderr, "failed to reserve arena pages: %d\n", err);
		exit(1);
	}

	err = libarena_run_prog(
		bpf_program__fd(ctx.skel->progs.arena_buddy_reset));
	if (err) {
		fprintf(stderr, "failed to initialize arena allocator: %d\n", err);
		exit(1);
	}

	ctx.skel->bss->bench_alloc_size = args.alloc_size;
	ctx.skel->bss->bench_nallocs = args.nallocs;
	ctx.reset_fd = bpf_program__fd(ctx.skel->progs.arena_buddy_reset);
}

static void malloc_setup(void)
{
	setup_common();
	ctx.bench_fd = bpf_program__fd(ctx.skel->progs.bench_malloc);
}

static void calloc_setup(void)
{
	setup_common();
	ctx.bench_fd = bpf_program__fd(ctx.skel->progs.bench_calloc);
}

static void *producer(void *input)
{
	int err;

	while (true) {
		err = libarena_run_prog(ctx.bench_fd);
		if (err) {
			fprintf(stderr, "libarena benchmark failed: %d\n", err);
			exit(1);
		}

		err = libarena_run_prog(ctx.reset_fd);
		if (err) {
			fprintf(stderr, "libarena alloc reset failed: %d\n", err);
			exit(1);
		}
	}

	return NULL;
}

static void measure(struct bench_res *res)
{
	res->duration_ns = atomic_swap(&ctx.skel->bss->bench_duration_ns, 0);
	res->hits = atomic_swap(&ctx.skel->bss->bench_hits, 0);
}

static void report_progress(int iter, struct bench_res *res, long delta_ns)
{
	double latency_ns = 0.0;

	if (res->hits)
		latency_ns = res->duration_ns / (double)res->hits;

	printf("Iter %3d (%7.3lfus): latency %8.3lf ns/op (%ld allocations)\n",
	       iter, (delta_ns - 1000000000) / 1000.0, latency_ns, res->hits);
}

static void report_final(struct bench_res res[], int res_cnt)
{
	unsigned long duration_ns = 0;
	long hits = 0;
	int i;

	for (i = 0; i < res_cnt; i++) {
		duration_ns += res[i].duration_ns;
		hits += res[i].hits;
	}

	if (!hits || !res_cnt) {
		printf("Summary: no runs measured\n");
		return;
	}

	printf("Summary: %.3lf ns/op, %.0lf invocations for %u allocations/invocation)\n",
	       duration_ns / (double)hits, hits / (double)res_cnt,
	       ctx.skel->bss->bench_nallocs);
}

const struct bench bench_libarena_malloc = {
	.name = "libarena-malloc",
	.argp = &bench_libarena_argp,
	.validate = validate,
	.setup = malloc_setup,
	.producer_thread = producer,
	.measure = measure,
	.report_progress = report_progress,
	.report_final = report_final,
};

const struct bench bench_libarena_calloc = {
	.name = "libarena-calloc",
	.argp = &bench_libarena_argp,
	.validate = validate,
	.setup = calloc_setup,
	.producer_thread = producer,
	.measure = measure,
	.report_progress = report_progress,
	.report_final = report_final,
};

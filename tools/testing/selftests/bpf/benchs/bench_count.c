// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bench.h"

/* COUNT-GLOBAL benchmark */

static struct count_global_ctx {
	struct counter hits;
} count_global_ctx;

static void *count_global_producer(void *input)
{
	struct count_global_ctx *ctx = &count_global_ctx;

	while (true) {
		atomic_inc(&ctx->hits.value);
	}
	return NULL;
}

static void *count_global_consumer(void *input)
{
	return NULL;
}

static void count_global_measure(struct bench_res *res)
{
	struct count_global_ctx *ctx = &count_global_ctx;

	res->hits = atomic_swap(&ctx->hits.value, 0);
}

/* COUNT-local benchmark */

static struct count_local_ctx {
	struct counter *hits;
} count_local_ctx;

static void count_local_setup()
{
	struct count_local_ctx *ctx = &count_local_ctx;

	ctx->hits = calloc(env.consumer_cnt, sizeof(*ctx->hits));
	if (!ctx->hits)
		exit(1);
}

static void *count_local_producer(void *input)
{
	struct count_local_ctx *ctx = &count_local_ctx;
	int idx = (long)input;

	while (true) {
		atomic_inc(&ctx->hits[idx].value);
	}
	return NULL;
}

static void *count_local_consumer(void *input)
{
	return NULL;
}

static void count_local_measure(struct bench_res *res)
{
	struct count_local_ctx *ctx = &count_local_ctx;
	int i;

	for (i = 0; i < env.producer_cnt; i++) {
		res->hits += atomic_swap(&ctx->hits[i].value, 0);
	}
}

const struct bench bench_count_global = {
	.name = "count-global",
	.producer_thread = count_global_producer,
	.consumer_thread = count_global_consumer,
	.measure = count_global_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_count_local = {
	.name = "count-local",
	.setup = count_local_setup,
	.producer_thread = count_local_producer,
	.consumer_thread = count_local_consumer,
	.measure = count_local_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

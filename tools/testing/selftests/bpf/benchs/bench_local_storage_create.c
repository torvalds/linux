// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <sys/types.h>
#include <sys/socket.h>

#include "bench.h"
#include "bench_local_storage_create.skel.h"

#define BATCH_SZ 32

struct thread {
	int fds[BATCH_SZ];
};

static struct bench_local_storage_create *skel;
static struct thread *threads;
static long socket_errs;

static void validate(void)
{
	if (env.consumer_cnt > 1) {
		fprintf(stderr,
			"local-storage-create benchmark does not need consumer\n");
		exit(1);
	}
}

static void setup(void)
{
	skel = bench_local_storage_create__open_and_load();
	if (!skel) {
		fprintf(stderr, "error loading skel\n");
		exit(1);
	}

	skel->bss->bench_pid = getpid();

	if (!bpf_program__attach(skel->progs.socket_post_create)) {
		fprintf(stderr, "Error attaching bpf program\n");
		exit(1);
	}

	if (!bpf_program__attach(skel->progs.kmalloc)) {
		fprintf(stderr, "Error attaching bpf program\n");
		exit(1);
	}

	threads = calloc(env.producer_cnt, sizeof(*threads));

	if (!threads) {
		fprintf(stderr, "cannot alloc thread_res\n");
		exit(1);
	}
}

static void measure(struct bench_res *res)
{
	res->hits = atomic_swap(&skel->bss->create_cnts, 0);
	res->drops = atomic_swap(&skel->bss->kmalloc_cnts, 0);
}

static void *consumer(void *input)
{
	return NULL;
}

static void *producer(void *input)
{
	struct thread *t = &threads[(long)(input)];
	int *fds = t->fds;
	int i;

	while (true) {
		for (i = 0; i < BATCH_SZ; i++) {
			fds[i] = socket(AF_INET6, SOCK_DGRAM, 0);
			if (fds[i] == -1)
				atomic_inc(&socket_errs);
		}

		for (i = 0; i < BATCH_SZ; i++) {
			if (fds[i] != -1)
				close(fds[i]);
		}
	}

	return NULL;
}

static void report_progress(int iter, struct bench_res *res, long delta_ns)
{
	double creates_per_sec, kmallocs_per_create;

	creates_per_sec = res->hits / 1000.0 / (delta_ns / 1000000000.0);
	kmallocs_per_create = (double)res->drops / res->hits;

	printf("Iter %3d (%7.3lfus): ",
	       iter, (delta_ns - 1000000000) / 1000.0);
	printf("creates %8.3lfk/s (%7.3lfk/prod), ",
	       creates_per_sec, creates_per_sec / env.producer_cnt);
	printf("%3.2lf kmallocs/create\n", kmallocs_per_create);
}

static void report_final(struct bench_res res[], int res_cnt)
{
	double creates_mean = 0.0, creates_stddev = 0.0;
	long total_creates = 0, total_kmallocs = 0;
	int i;

	for (i = 0; i < res_cnt; i++) {
		creates_mean += res[i].hits / 1000.0 / (0.0 + res_cnt);
		total_creates += res[i].hits;
		total_kmallocs += res[i].drops;
	}

	if (res_cnt > 1)  {
		for (i = 0; i < res_cnt; i++)
			creates_stddev += (creates_mean - res[i].hits / 1000.0) *
				       (creates_mean - res[i].hits / 1000.0) /
				       (res_cnt - 1.0);
		creates_stddev = sqrt(creates_stddev);
	}
	printf("Summary: creates %8.3lf \u00B1 %5.3lfk/s (%7.3lfk/prod), ",
	       creates_mean, creates_stddev, creates_mean / env.producer_cnt);
	printf("%4.2lf kmallocs/create\n", (double)total_kmallocs / total_creates);
	if (socket_errs || skel->bss->create_errs)
		printf("socket() errors %ld create_errs %ld\n", socket_errs,
		       skel->bss->create_errs);
}

/* Benchmark performance of creating bpf local storage  */
const struct bench bench_local_storage_create = {
	.name = "local-storage-create",
	.validate = validate,
	.setup = setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = report_progress,
	.report_final = report_final,
};

// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Bytedance */

#include "bench.h"
#include "bpf_hashmap_full_update_bench.skel.h"
#include "bpf_util.h"

/* BPF triggering benchmarks */
static struct ctx {
	struct bpf_hashmap_full_update_bench *skel;
} ctx;

#define MAX_LOOP_NUM 10000

static void validate(void)
{
	if (env.consumer_cnt != 1) {
		fprintf(stderr, "benchmark doesn't support multi-consumer!\n");
		exit(1);
	}
}

static void *producer(void *input)
{
	while (true) {
		/* trigger the bpf program */
		syscall(__NR_getpgid);
	}

	return NULL;
}

static void *consumer(void *input)
{
	return NULL;
}

static void measure(struct bench_res *res)
{
}

static void setup(void)
{
	struct bpf_link *link;
	int map_fd, i, max_entries;

	setup_libbpf();

	ctx.skel = bpf_hashmap_full_update_bench__open_and_load();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	ctx.skel->bss->nr_loops = MAX_LOOP_NUM;

	link = bpf_program__attach(ctx.skel->progs.benchmark);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}

	/* fill hash_map */
	map_fd = bpf_map__fd(ctx.skel->maps.hash_map_bench);
	max_entries = bpf_map__max_entries(ctx.skel->maps.hash_map_bench);
	for (i = 0; i < max_entries; i++)
		bpf_map_update_elem(map_fd, &i, &i, BPF_ANY);
}

static void hashmap_report_final(struct bench_res res[], int res_cnt)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	int i;

	for (i = 0; i < nr_cpus; i++) {
		u64 time = ctx.skel->bss->percpu_time[i];

		if (!time)
			continue;

		printf("%d:hash_map_full_perf %lld events per sec\n",
		       i, ctx.skel->bss->nr_loops * 1000000000ll / time);
	}
}

const struct bench bench_bpf_hashmap_full_update = {
	.name = "bpf-hashmap-full-update",
	.validate = validate,
	.setup = setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = NULL,
	.report_final = hashmap_report_final,
};

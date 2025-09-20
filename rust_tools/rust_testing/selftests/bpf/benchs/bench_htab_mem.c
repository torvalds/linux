// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#include <argp.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>

#include "bench.h"
#include "bpf_util.h"
#include "cgroup_helpers.h"
#include "htab_mem_bench.skel.h"

struct htab_mem_use_case {
	const char *name;
	const char **progs;
	/* Do synchronization between addition thread and deletion thread */
	bool need_sync;
};

static struct htab_mem_ctx {
	const struct htab_mem_use_case *uc;
	struct htab_mem_bench *skel;
	pthread_barrier_t *notify;
	int fd;
} ctx;

const char *ow_progs[] = {"overwrite", NULL};
const char *batch_progs[] = {"batch_add_batch_del", NULL};
const char *add_del_progs[] = {"add_only", "del_only", NULL};
const static struct htab_mem_use_case use_cases[] = {
	{ .name = "overwrite", .progs = ow_progs },
	{ .name = "batch_add_batch_del", .progs = batch_progs },
	{ .name = "add_del_on_diff_cpu", .progs = add_del_progs, .need_sync = true },
};

static struct htab_mem_args {
	u32 value_size;
	const char *use_case;
	bool preallocated;
} args = {
	.value_size = 8,
	.use_case = "overwrite",
	.preallocated = false,
};

enum {
	ARG_VALUE_SIZE = 10000,
	ARG_USE_CASE = 10001,
	ARG_PREALLOCATED = 10002,
};

static const struct argp_option opts[] = {
	{ "value-size", ARG_VALUE_SIZE, "VALUE_SIZE", 0,
	  "Set the value size of hash map (default 8)" },
	{ "use-case", ARG_USE_CASE, "USE_CASE", 0,
	  "Set the use case of hash map: overwrite|batch_add_batch_del|add_del_on_diff_cpu" },
	{ "preallocated", ARG_PREALLOCATED, NULL, 0, "use preallocated hash map" },
	{},
};

static error_t htab_mem_parse_arg(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case ARG_VALUE_SIZE:
		args.value_size = strtoul(arg, NULL, 10);
		if (args.value_size > 4096) {
			fprintf(stderr, "too big value size %u\n", args.value_size);
			argp_usage(state);
		}
		break;
	case ARG_USE_CASE:
		args.use_case = strdup(arg);
		if (!args.use_case) {
			fprintf(stderr, "no mem for use-case\n");
			argp_usage(state);
		}
		break;
	case ARG_PREALLOCATED:
		args.preallocated = true;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

const struct argp bench_htab_mem_argp = {
	.options = opts,
	.parser = htab_mem_parse_arg,
};

static void htab_mem_validate(void)
{
	if (!strcmp(use_cases[2].name, args.use_case) && env.producer_cnt % 2) {
		fprintf(stderr, "%s needs an even number of producers\n", args.use_case);
		exit(1);
	}
}

static int htab_mem_bench_init_barriers(void)
{
	pthread_barrier_t *barriers;
	unsigned int i, nr;

	if (!ctx.uc->need_sync)
		return 0;

	nr = (env.producer_cnt + 1) / 2;
	barriers = calloc(nr, sizeof(*barriers));
	if (!barriers)
		return -1;

	/* Used for synchronization between two threads */
	for (i = 0; i < nr; i++)
		pthread_barrier_init(&barriers[i], NULL, 2);

	ctx.notify = barriers;
	return 0;
}

static void htab_mem_bench_exit_barriers(void)
{
	unsigned int i, nr;

	if (!ctx.notify)
		return;

	nr = (env.producer_cnt + 1) / 2;
	for (i = 0; i < nr; i++)
		pthread_barrier_destroy(&ctx.notify[i]);
	free(ctx.notify);
}

static const struct htab_mem_use_case *htab_mem_find_use_case_or_exit(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(use_cases); i++) {
		if (!strcmp(name, use_cases[i].name))
			return &use_cases[i];
	}

	fprintf(stderr, "no such use-case: %s\n", name);
	fprintf(stderr, "available use case:");
	for (i = 0; i < ARRAY_SIZE(use_cases); i++)
		fprintf(stderr, " %s", use_cases[i].name);
	fprintf(stderr, "\n");
	exit(1);
}

static void htab_mem_setup(void)
{
	struct bpf_map *map;
	const char **names;
	int err;

	setup_libbpf();

	ctx.uc = htab_mem_find_use_case_or_exit(args.use_case);
	err = htab_mem_bench_init_barriers();
	if (err) {
		fprintf(stderr, "failed to init barrier\n");
		exit(1);
	}

	ctx.fd = cgroup_setup_and_join("/htab_mem");
	if (ctx.fd < 0)
		goto cleanup;

	ctx.skel = htab_mem_bench__open();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		goto cleanup;
	}

	map = ctx.skel->maps.htab;
	bpf_map__set_value_size(map, args.value_size);
	/* Ensure that different CPUs can operate on different subset */
	bpf_map__set_max_entries(map, MAX(8192, 64 * env.nr_cpus));
	if (args.preallocated)
		bpf_map__set_map_flags(map, bpf_map__map_flags(map) & ~BPF_F_NO_PREALLOC);

	names = ctx.uc->progs;
	while (*names) {
		struct bpf_program *prog;

		prog = bpf_object__find_program_by_name(ctx.skel->obj, *names);
		if (!prog) {
			fprintf(stderr, "no such program %s\n", *names);
			goto cleanup;
		}
		bpf_program__set_autoload(prog, true);
		names++;
	}
	ctx.skel->bss->nr_thread = env.producer_cnt;

	err = htab_mem_bench__load(ctx.skel);
	if (err) {
		fprintf(stderr, "failed to load skeleton\n");
		goto cleanup;
	}
	err = htab_mem_bench__attach(ctx.skel);
	if (err) {
		fprintf(stderr, "failed to attach skeleton\n");
		goto cleanup;
	}
	return;

cleanup:
	htab_mem_bench__destroy(ctx.skel);
	htab_mem_bench_exit_barriers();
	if (ctx.fd >= 0) {
		close(ctx.fd);
		cleanup_cgroup_environment();
	}
	exit(1);
}

static void htab_mem_add_fn(pthread_barrier_t *notify)
{
	while (true) {
		/* Do addition */
		(void)syscall(__NR_getpgid, 0);
		/* Notify deletion thread to do deletion */
		pthread_barrier_wait(notify);
		/* Wait for deletion to complete */
		pthread_barrier_wait(notify);
	}
}

static void htab_mem_delete_fn(pthread_barrier_t *notify)
{
	while (true) {
		/* Wait for addition to complete */
		pthread_barrier_wait(notify);
		/* Do deletion */
		(void)syscall(__NR_getppid);
		/* Notify addition thread to do addition */
		pthread_barrier_wait(notify);
	}
}

static void *htab_mem_producer(void *arg)
{
	pthread_barrier_t *notify;
	int seq;

	if (!ctx.uc->need_sync) {
		while (true)
			(void)syscall(__NR_getpgid, 0);
		return NULL;
	}

	seq = (long)arg;
	notify = &ctx.notify[seq / 2];
	if (seq & 1)
		htab_mem_delete_fn(notify);
	else
		htab_mem_add_fn(notify);
	return NULL;
}

static void htab_mem_read_mem_cgrp_file(const char *name, unsigned long *value)
{
	char buf[32];
	ssize_t got;
	int fd;

	fd = openat(ctx.fd, name, O_RDONLY);
	if (fd < 0) {
		/* cgroup v1 ? */
		fprintf(stderr, "no %s\n", name);
		*value = 0;
		return;
	}

	got = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (got <= 0) {
		*value = 0;
		return;
	}
	buf[got] = 0;

	*value = strtoull(buf, NULL, 0);
}

static void htab_mem_measure(struct bench_res *res)
{
	res->hits = atomic_swap(&ctx.skel->bss->op_cnt, 0) / env.producer_cnt;
	htab_mem_read_mem_cgrp_file("memory.current", &res->gp_ct);
}

static void htab_mem_report_progress(int iter, struct bench_res *res, long delta_ns)
{
	double loop, mem;

	loop = res->hits / 1000.0 / (delta_ns / 1000000000.0);
	mem = res->gp_ct / 1048576.0;
	printf("Iter %3d (%7.3lfus): ", iter, (delta_ns - 1000000000) / 1000.0);
	printf("per-prod-op %7.2lfk/s, memory usage %7.2lfMiB\n", loop, mem);
}

static void htab_mem_report_final(struct bench_res res[], int res_cnt)
{
	double mem_mean = 0.0, mem_stddev = 0.0;
	double loop_mean = 0.0, loop_stddev = 0.0;
	unsigned long peak_mem;
	int i;

	for (i = 0; i < res_cnt; i++) {
		loop_mean += res[i].hits / 1000.0 / (0.0 + res_cnt);
		mem_mean += res[i].gp_ct / 1048576.0 / (0.0 + res_cnt);
	}
	if (res_cnt > 1)  {
		for (i = 0; i < res_cnt; i++) {
			loop_stddev += (loop_mean - res[i].hits / 1000.0) *
				       (loop_mean - res[i].hits / 1000.0) /
				       (res_cnt - 1.0);
			mem_stddev += (mem_mean - res[i].gp_ct / 1048576.0) *
				      (mem_mean - res[i].gp_ct / 1048576.0) /
				      (res_cnt - 1.0);
		}
		loop_stddev = sqrt(loop_stddev);
		mem_stddev = sqrt(mem_stddev);
	}

	htab_mem_read_mem_cgrp_file("memory.peak", &peak_mem);
	printf("Summary: per-prod-op %7.2lf \u00B1 %7.2lfk/s, memory usage %7.2lf \u00B1 %7.2lfMiB,"
	       " peak memory usage %7.2lfMiB\n",
	       loop_mean, loop_stddev, mem_mean, mem_stddev, peak_mem / 1048576.0);

	close(ctx.fd);
	cleanup_cgroup_environment();
}

const struct bench bench_htab_mem = {
	.name = "htab-mem",
	.argp = &bench_htab_mem_argp,
	.validate = htab_mem_validate,
	.setup = htab_mem_setup,
	.producer_thread = htab_mem_producer,
	.measure = htab_mem_measure,
	.report_progress = htab_mem_report_progress,
	.report_final = htab_mem_report_final,
};

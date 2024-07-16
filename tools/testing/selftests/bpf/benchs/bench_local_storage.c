// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <argp.h>
#include <linux/btf.h>

#include "local_storage_bench.skel.h"
#include "bench.h"

#include <test_btf.h>

static struct {
	__u32 nr_maps;
	__u32 hashmap_nr_keys_used;
} args = {
	.nr_maps = 1000,
	.hashmap_nr_keys_used = 1000,
};

enum {
	ARG_NR_MAPS = 6000,
	ARG_HASHMAP_NR_KEYS_USED = 6001,
};

static const struct argp_option opts[] = {
	{ "nr_maps", ARG_NR_MAPS, "NR_MAPS", 0,
		"Set number of local_storage maps"},
	{ "hashmap_nr_keys_used", ARG_HASHMAP_NR_KEYS_USED, "NR_KEYS",
		0, "When doing hashmap test, set number of hashmap keys test uses"},
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	long ret;

	switch (key) {
	case ARG_NR_MAPS:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > UINT_MAX) {
			fprintf(stderr, "invalid nr_maps");
			argp_usage(state);
		}
		args.nr_maps = ret;
		break;
	case ARG_HASHMAP_NR_KEYS_USED:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > UINT_MAX) {
			fprintf(stderr, "invalid hashmap_nr_keys_used");
			argp_usage(state);
		}
		args.hashmap_nr_keys_used = ret;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

const struct argp bench_local_storage_argp = {
	.options = opts,
	.parser = parse_arg,
};

/* Keep in sync w/ array of maps in bpf */
#define MAX_NR_MAPS 1000
/* keep in sync w/ same define in bpf */
#define HASHMAP_SZ 4194304

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

	if (args.nr_maps > MAX_NR_MAPS) {
		fprintf(stderr, "nr_maps must be <= 1000\n");
		exit(1);
	}

	if (args.hashmap_nr_keys_used > HASHMAP_SZ) {
		fprintf(stderr, "hashmap_nr_keys_used must be <= %u\n", HASHMAP_SZ);
		exit(1);
	}
}

static struct {
	struct local_storage_bench *skel;
	void *bpf_obj;
	struct bpf_map *array_of_maps;
} ctx;

static void prepopulate_hashmap(int fd)
{
	int i, key, val;

	/* local_storage gets will have BPF_LOCAL_STORAGE_GET_F_CREATE flag set, so
	 * populate the hashmap for a similar comparison
	 */
	for (i = 0; i < HASHMAP_SZ; i++) {
		key = val = i;
		if (bpf_map_update_elem(fd, &key, &val, 0)) {
			fprintf(stderr, "Error prepopulating hashmap (key %d)\n", key);
			exit(1);
		}
	}
}

static void __setup(struct bpf_program *prog, bool hashmap)
{
	struct bpf_map *inner_map;
	int i, fd, mim_fd, err;

	LIBBPF_OPTS(bpf_map_create_opts, create_opts);

	if (!hashmap)
		create_opts.map_flags = BPF_F_NO_PREALLOC;

	ctx.skel->rodata->num_maps = args.nr_maps;
	ctx.skel->rodata->hashmap_num_keys = args.hashmap_nr_keys_used;
	inner_map = bpf_map__inner_map(ctx.array_of_maps);
	create_opts.btf_key_type_id = bpf_map__btf_key_type_id(inner_map);
	create_opts.btf_value_type_id = bpf_map__btf_value_type_id(inner_map);

	err = local_storage_bench__load(ctx.skel);
	if (err) {
		fprintf(stderr, "Error loading skeleton\n");
		goto err_out;
	}

	create_opts.btf_fd = bpf_object__btf_fd(ctx.skel->obj);

	mim_fd = bpf_map__fd(ctx.array_of_maps);
	if (mim_fd < 0) {
		fprintf(stderr, "Error getting map_in_map fd\n");
		goto err_out;
	}

	for (i = 0; i < args.nr_maps; i++) {
		if (hashmap)
			fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL, sizeof(int),
					    sizeof(int), HASHMAP_SZ, &create_opts);
		else
			fd = bpf_map_create(BPF_MAP_TYPE_TASK_STORAGE, NULL, sizeof(int),
					    sizeof(int), 0, &create_opts);
		if (fd < 0) {
			fprintf(stderr, "Error creating map %d: %d\n", i, fd);
			goto err_out;
		}

		if (hashmap)
			prepopulate_hashmap(fd);

		err = bpf_map_update_elem(mim_fd, &i, &fd, 0);
		if (err) {
			fprintf(stderr, "Error updating array-of-maps w/ map %d\n", i);
			goto err_out;
		}
	}

	if (!bpf_program__attach(prog)) {
		fprintf(stderr, "Error attaching bpf program\n");
		goto err_out;
	}

	return;
err_out:
	exit(1);
}

static void hashmap_setup(void)
{
	struct local_storage_bench *skel;

	setup_libbpf();

	skel = local_storage_bench__open();
	ctx.skel = skel;
	ctx.array_of_maps = skel->maps.array_of_hash_maps;
	skel->rodata->use_hashmap = 1;
	skel->rodata->interleave = 0;

	__setup(skel->progs.get_local, true);
}

static void local_storage_cache_get_setup(void)
{
	struct local_storage_bench *skel;

	setup_libbpf();

	skel = local_storage_bench__open();
	ctx.skel = skel;
	ctx.array_of_maps = skel->maps.array_of_local_storage_maps;
	skel->rodata->use_hashmap = 0;
	skel->rodata->interleave = 0;

	__setup(skel->progs.get_local, false);
}

static void local_storage_cache_get_interleaved_setup(void)
{
	struct local_storage_bench *skel;

	setup_libbpf();

	skel = local_storage_bench__open();
	ctx.skel = skel;
	ctx.array_of_maps = skel->maps.array_of_local_storage_maps;
	skel->rodata->use_hashmap = 0;
	skel->rodata->interleave = 1;

	__setup(skel->progs.get_local, false);
}

static void measure(struct bench_res *res)
{
	res->hits = atomic_swap(&ctx.skel->bss->hits, 0);
	res->important_hits = atomic_swap(&ctx.skel->bss->important_hits, 0);
}

static inline void trigger_bpf_program(void)
{
	syscall(__NR_getpgid);
}

static void *consumer(void *input)
{
	return NULL;
}

static void *producer(void *input)
{
	while (true)
		trigger_bpf_program();

	return NULL;
}

/* cache sequential and interleaved get benchs test local_storage get
 * performance, specifically they demonstrate performance cliff of
 * current list-plus-cache local_storage model.
 *
 * cache sequential get: call bpf_task_storage_get on n maps in order
 * cache interleaved get: like "sequential get", but interleave 4 calls to the
 *	'important' map (idx 0 in array_of_maps) for every 10 calls. Goal
 *	is to mimic environment where many progs are accessing their local_storage
 *	maps, with 'our' prog needing to access its map more often than others
 */
const struct bench bench_local_storage_cache_seq_get = {
	.name = "local-storage-cache-seq-get",
	.validate = validate,
	.setup = local_storage_cache_get_setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = local_storage_report_progress,
	.report_final = local_storage_report_final,
};

const struct bench bench_local_storage_cache_interleaved_get = {
	.name = "local-storage-cache-int-get",
	.validate = validate,
	.setup = local_storage_cache_get_interleaved_setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = local_storage_report_progress,
	.report_final = local_storage_report_final,
};

const struct bench bench_local_storage_cache_hashmap_control = {
	.name = "local-storage-cache-hashmap-control",
	.validate = validate,
	.setup = hashmap_setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = local_storage_report_progress,
	.report_final = local_storage_report_final,
};

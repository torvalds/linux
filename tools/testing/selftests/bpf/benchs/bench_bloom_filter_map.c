// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <argp.h>
#include <linux/log2.h>
#include <pthread.h>
#include "bench.h"
#include "bloom_filter_bench.skel.h"
#include "bpf_util.h"

static struct ctx {
	bool use_array_map;
	bool use_hashmap;
	bool hashmap_use_bloom;
	bool count_false_hits;

	struct bloom_filter_bench *skel;

	int bloom_fd;
	int hashmap_fd;
	int array_map_fd;

	pthread_mutex_t map_done_mtx;
	pthread_cond_t map_done_cv;
	bool map_done;
	bool map_prepare_err;

	__u32 next_map_idx;
} ctx = {
	.map_done_mtx = PTHREAD_MUTEX_INITIALIZER,
	.map_done_cv = PTHREAD_COND_INITIALIZER,
};

struct stat {
	__u32 stats[3];
};

static struct {
	__u32 nr_entries;
	__u8 nr_hash_funcs;
	__u8 value_size;
} args = {
	.nr_entries = 1000,
	.nr_hash_funcs = 3,
	.value_size = 8,
};

enum {
	ARG_NR_ENTRIES = 3000,
	ARG_NR_HASH_FUNCS = 3001,
	ARG_VALUE_SIZE = 3002,
};

static const struct argp_option opts[] = {
	{ "nr_entries", ARG_NR_ENTRIES, "NR_ENTRIES", 0,
		"Set number of expected unique entries in the bloom filter"},
	{ "nr_hash_funcs", ARG_NR_HASH_FUNCS, "NR_HASH_FUNCS", 0,
		"Set number of hash functions in the bloom filter"},
	{ "value_size", ARG_VALUE_SIZE, "VALUE_SIZE", 0,
		"Set value size (in bytes) of bloom filter entries"},
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case ARG_NR_ENTRIES:
		args.nr_entries = strtol(arg, NULL, 10);
		if (args.nr_entries == 0) {
			fprintf(stderr, "Invalid nr_entries count.");
			argp_usage(state);
		}
		break;
	case ARG_NR_HASH_FUNCS:
		args.nr_hash_funcs = strtol(arg, NULL, 10);
		if (args.nr_hash_funcs == 0 || args.nr_hash_funcs > 15) {
			fprintf(stderr,
				"The bloom filter must use 1 to 15 hash functions.");
			argp_usage(state);
		}
		break;
	case ARG_VALUE_SIZE:
		args.value_size = strtol(arg, NULL, 10);
		if (args.value_size < 2 || args.value_size > 256) {
			fprintf(stderr,
				"Invalid value size. Must be between 2 and 256 bytes");
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

/* exported into benchmark runner */
const struct argp bench_bloom_map_argp = {
	.options = opts,
	.parser = parse_arg,
};

static void validate(void)
{
	if (env.consumer_cnt != 1) {
		fprintf(stderr,
			"The bloom filter benchmarks do not support multi-consumer use\n");
		exit(1);
	}
}

static inline void trigger_bpf_program(void)
{
	syscall(__NR_getpgid);
}

static void *producer(void *input)
{
	while (true)
		trigger_bpf_program();

	return NULL;
}

static void *map_prepare_thread(void *arg)
{
	__u32 val_size, i;
	void *val = NULL;
	int err;

	val_size = args.value_size;
	val = malloc(val_size);
	if (!val) {
		ctx.map_prepare_err = true;
		goto done;
	}

	while (true) {
		i = __atomic_add_fetch(&ctx.next_map_idx, 1, __ATOMIC_RELAXED);
		if (i > args.nr_entries)
			break;

again:
		/* Populate hashmap, bloom filter map, and array map with the same
		 * random values
		 */
		err = syscall(__NR_getrandom, val, val_size, 0);
		if (err != val_size) {
			ctx.map_prepare_err = true;
			fprintf(stderr, "failed to get random value: %d\n", -errno);
			break;
		}

		if (ctx.use_hashmap) {
			err = bpf_map_update_elem(ctx.hashmap_fd, val, val, BPF_NOEXIST);
			if (err) {
				if (err != -EEXIST) {
					ctx.map_prepare_err = true;
					fprintf(stderr, "failed to add elem to hashmap: %d\n",
						-errno);
					break;
				}
				goto again;
			}
		}

		i--;

		if (ctx.use_array_map) {
			err = bpf_map_update_elem(ctx.array_map_fd, &i, val, 0);
			if (err) {
				ctx.map_prepare_err = true;
				fprintf(stderr, "failed to add elem to array map: %d\n", -errno);
				break;
			}
		}

		if (ctx.use_hashmap && !ctx.hashmap_use_bloom)
			continue;

		err = bpf_map_update_elem(ctx.bloom_fd, NULL, val, 0);
		if (err) {
			ctx.map_prepare_err = true;
			fprintf(stderr,
				"failed to add elem to bloom filter map: %d\n", -errno);
			break;
		}
	}
done:
	pthread_mutex_lock(&ctx.map_done_mtx);
	ctx.map_done = true;
	pthread_cond_signal(&ctx.map_done_cv);
	pthread_mutex_unlock(&ctx.map_done_mtx);

	if (val)
		free(val);

	return NULL;
}

static void populate_maps(void)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	pthread_t map_thread;
	int i, err, nr_rand_bytes;

	ctx.bloom_fd = bpf_map__fd(ctx.skel->maps.bloom_map);
	ctx.hashmap_fd = bpf_map__fd(ctx.skel->maps.hashmap);
	ctx.array_map_fd = bpf_map__fd(ctx.skel->maps.array_map);

	for (i = 0; i < nr_cpus; i++) {
		err = pthread_create(&map_thread, NULL, map_prepare_thread,
				     NULL);
		if (err) {
			fprintf(stderr, "failed to create pthread: %d\n", -errno);
			exit(1);
		}
	}

	pthread_mutex_lock(&ctx.map_done_mtx);
	while (!ctx.map_done)
		pthread_cond_wait(&ctx.map_done_cv, &ctx.map_done_mtx);
	pthread_mutex_unlock(&ctx.map_done_mtx);

	if (ctx.map_prepare_err)
		exit(1);

	nr_rand_bytes = syscall(__NR_getrandom, ctx.skel->bss->rand_vals,
				ctx.skel->rodata->nr_rand_bytes, 0);
	if (nr_rand_bytes != ctx.skel->rodata->nr_rand_bytes) {
		fprintf(stderr, "failed to get random bytes\n");
		exit(1);
	}
}

static void check_args(void)
{
	if (args.value_size < 8)  {
		__u64 nr_unique_entries = 1ULL << (args.value_size * 8);

		if (args.nr_entries > nr_unique_entries) {
			fprintf(stderr,
				"Not enough unique values for the nr_entries requested\n");
			exit(1);
		}
	}
}

static struct bloom_filter_bench *setup_skeleton(void)
{
	struct bloom_filter_bench *skel;

	check_args();

	setup_libbpf();

	skel = bloom_filter_bench__open();
	if (!skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	skel->rodata->hashmap_use_bloom = ctx.hashmap_use_bloom;
	skel->rodata->count_false_hits = ctx.count_false_hits;

	/* Resize number of entries */
	bpf_map__set_max_entries(skel->maps.hashmap, args.nr_entries);

	bpf_map__set_max_entries(skel->maps.array_map, args.nr_entries);

	bpf_map__set_max_entries(skel->maps.bloom_map, args.nr_entries);

	/* Set value size */
	bpf_map__set_value_size(skel->maps.array_map, args.value_size);

	bpf_map__set_value_size(skel->maps.bloom_map, args.value_size);

	bpf_map__set_value_size(skel->maps.hashmap, args.value_size);

	/* For the hashmap, we use the value as the key as well */
	bpf_map__set_key_size(skel->maps.hashmap, args.value_size);

	skel->bss->value_size = args.value_size;

	/* Set number of hash functions */
	bpf_map__set_map_extra(skel->maps.bloom_map, args.nr_hash_funcs);

	if (bloom_filter_bench__load(skel)) {
		fprintf(stderr, "failed to load skeleton\n");
		exit(1);
	}

	return skel;
}

static void bloom_lookup_setup(void)
{
	struct bpf_link *link;

	ctx.use_array_map = true;

	ctx.skel = setup_skeleton();

	populate_maps();

	link = bpf_program__attach(ctx.skel->progs.bloom_lookup);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}
}

static void bloom_update_setup(void)
{
	struct bpf_link *link;

	ctx.use_array_map = true;

	ctx.skel = setup_skeleton();

	populate_maps();

	link = bpf_program__attach(ctx.skel->progs.bloom_update);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}
}

static void false_positive_setup(void)
{
	struct bpf_link *link;

	ctx.use_hashmap = true;
	ctx.hashmap_use_bloom = true;
	ctx.count_false_hits = true;

	ctx.skel = setup_skeleton();

	populate_maps();

	link = bpf_program__attach(ctx.skel->progs.bloom_hashmap_lookup);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}
}

static void hashmap_with_bloom_setup(void)
{
	struct bpf_link *link;

	ctx.use_hashmap = true;
	ctx.hashmap_use_bloom = true;

	ctx.skel = setup_skeleton();

	populate_maps();

	link = bpf_program__attach(ctx.skel->progs.bloom_hashmap_lookup);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}
}

static void hashmap_no_bloom_setup(void)
{
	struct bpf_link *link;

	ctx.use_hashmap = true;

	ctx.skel = setup_skeleton();

	populate_maps();

	link = bpf_program__attach(ctx.skel->progs.bloom_hashmap_lookup);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}
}

static void measure(struct bench_res *res)
{
	unsigned long total_hits = 0, total_drops = 0, total_false_hits = 0;
	static unsigned long last_hits, last_drops, last_false_hits;
	unsigned int nr_cpus = bpf_num_possible_cpus();
	int hit_key, drop_key, false_hit_key;
	int i;

	hit_key = ctx.skel->rodata->hit_key;
	drop_key = ctx.skel->rodata->drop_key;
	false_hit_key = ctx.skel->rodata->false_hit_key;

	if (ctx.skel->bss->error != 0) {
		fprintf(stderr, "error (%d) when searching the bloom filter\n",
			ctx.skel->bss->error);
		exit(1);
	}

	for (i = 0; i < nr_cpus; i++) {
		struct stat *s = (void *)&ctx.skel->bss->percpu_stats[i];

		total_hits += s->stats[hit_key];
		total_drops += s->stats[drop_key];
		total_false_hits += s->stats[false_hit_key];
	}

	res->hits = total_hits - last_hits;
	res->drops = total_drops - last_drops;
	res->false_hits = total_false_hits - last_false_hits;

	last_hits = total_hits;
	last_drops = total_drops;
	last_false_hits = total_false_hits;
}

static void *consumer(void *input)
{
	return NULL;
}

const struct bench bench_bloom_lookup = {
	.name = "bloom-lookup",
	.validate = validate,
	.setup = bloom_lookup_setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_bloom_update = {
	.name = "bloom-update",
	.validate = validate,
	.setup = bloom_update_setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_bloom_false_positive = {
	.name = "bloom-false-positive",
	.validate = validate,
	.setup = false_positive_setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = false_hits_report_progress,
	.report_final = false_hits_report_final,
};

const struct bench bench_hashmap_without_bloom = {
	.name = "hashmap-without-bloom",
	.validate = validate,
	.setup = hashmap_no_bloom_setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_hashmap_with_bloom = {
	.name = "hashmap-with-bloom",
	.validate = validate,
	.setup = hashmap_with_bloom_setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

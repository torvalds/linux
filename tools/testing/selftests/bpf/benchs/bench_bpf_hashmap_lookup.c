// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */

#include <sys/random.h>
#include <argp.h>
#include "bench.h"
#include "bpf_hashmap_lookup.skel.h"
#include "bpf_util.h"

/* BPF triggering benchmarks */
static struct ctx {
	struct bpf_hashmap_lookup *skel;
} ctx;

/* only available to kernel, so define it here */
#define BPF_MAX_LOOPS (1<<23)

#define MAX_KEY_SIZE 1024 /* the size of the key map */

static struct {
	__u32 key_size;
	__u32 map_flags;
	__u32 max_entries;
	__u32 nr_entries;
	__u32 nr_loops;
} args = {
	.key_size = 4,
	.map_flags = 0,
	.max_entries = 1000,
	.nr_entries = 500,
	.nr_loops = 1000000,
};

enum {
	ARG_KEY_SIZE = 8001,
	ARG_MAP_FLAGS,
	ARG_MAX_ENTRIES,
	ARG_NR_ENTRIES,
	ARG_NR_LOOPS,
};

static const struct argp_option opts[] = {
	{ "key_size", ARG_KEY_SIZE, "KEY_SIZE", 0,
	  "The hashmap key size (max 1024)"},
	{ "map_flags", ARG_MAP_FLAGS, "MAP_FLAGS", 0,
	  "The hashmap flags passed to BPF_MAP_CREATE"},
	{ "max_entries", ARG_MAX_ENTRIES, "MAX_ENTRIES", 0,
	  "The hashmap max entries"},
	{ "nr_entries", ARG_NR_ENTRIES, "NR_ENTRIES", 0,
	  "The number of entries to insert/lookup"},
	{ "nr_loops", ARG_NR_LOOPS, "NR_LOOPS", 0,
	  "The number of loops for the benchmark"},
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	long ret;

	switch (key) {
	case ARG_KEY_SIZE:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > MAX_KEY_SIZE) {
			fprintf(stderr, "invalid key_size");
			argp_usage(state);
		}
		args.key_size = ret;
		break;
	case ARG_MAP_FLAGS:
		ret = strtol(arg, NULL, 0);
		if (ret < 0 || ret > UINT_MAX) {
			fprintf(stderr, "invalid map_flags");
			argp_usage(state);
		}
		args.map_flags = ret;
		break;
	case ARG_MAX_ENTRIES:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > UINT_MAX) {
			fprintf(stderr, "invalid max_entries");
			argp_usage(state);
		}
		args.max_entries = ret;
		break;
	case ARG_NR_ENTRIES:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > UINT_MAX) {
			fprintf(stderr, "invalid nr_entries");
			argp_usage(state);
		}
		args.nr_entries = ret;
		break;
	case ARG_NR_LOOPS:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > BPF_MAX_LOOPS) {
			fprintf(stderr, "invalid nr_loops: %ld (min=1 max=%u)\n",
				ret, BPF_MAX_LOOPS);
			argp_usage(state);
		}
		args.nr_loops = ret;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

const struct argp bench_hashmap_lookup_argp = {
	.options = opts,
	.parser = parse_arg,
};

static void validate(void)
{
	if (env.consumer_cnt != 1) {
		fprintf(stderr, "benchmark doesn't support multi-consumer!\n");
		exit(1);
	}

	if (args.nr_entries > args.max_entries) {
		fprintf(stderr, "args.nr_entries is too big! (max %u, got %u)\n",
			args.max_entries, args.nr_entries);
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

static inline void patch_key(u32 i, u32 *key)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	*key = i + 1;
#else
	*key = __builtin_bswap32(i + 1);
#endif
	/* the rest of key is random */
}

static void setup(void)
{
	struct bpf_link *link;
	int map_fd;
	int ret;
	int i;

	setup_libbpf();

	ctx.skel = bpf_hashmap_lookup__open();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	bpf_map__set_max_entries(ctx.skel->maps.hash_map_bench, args.max_entries);
	bpf_map__set_key_size(ctx.skel->maps.hash_map_bench, args.key_size);
	bpf_map__set_value_size(ctx.skel->maps.hash_map_bench, 8);
	bpf_map__set_map_flags(ctx.skel->maps.hash_map_bench, args.map_flags);

	ctx.skel->bss->nr_entries = args.nr_entries;
	ctx.skel->bss->nr_loops = args.nr_loops / args.nr_entries;

	if (args.key_size > 4) {
		for (i = 1; i < args.key_size/4; i++)
			ctx.skel->bss->key[i] = 2654435761 * i;
	}

	ret = bpf_hashmap_lookup__load(ctx.skel);
	if (ret) {
		bpf_hashmap_lookup__destroy(ctx.skel);
		fprintf(stderr, "failed to load map: %s", strerror(-ret));
		exit(1);
	}

	/* fill in the hash_map */
	map_fd = bpf_map__fd(ctx.skel->maps.hash_map_bench);
	for (u64 i = 0; i < args.nr_entries; i++) {
		patch_key(i, ctx.skel->bss->key);
		bpf_map_update_elem(map_fd, ctx.skel->bss->key, &i, BPF_ANY);
	}

	link = bpf_program__attach(ctx.skel->progs.benchmark);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}
}

static inline double events_from_time(u64 time)
{
	if (time)
		return args.nr_loops * 1000000000llu / time / 1000000.0L;

	return 0;
}

static int compute_events(u64 *times, double *events_mean, double *events_stddev, u64 *mean_time)
{
	int i, n = 0;

	*events_mean = 0;
	*events_stddev = 0;
	*mean_time = 0;

	for (i = 0; i < 32; i++) {
		if (!times[i])
			break;
		*mean_time += times[i];
		*events_mean += events_from_time(times[i]);
		n += 1;
	}
	if (!n)
		return 0;

	*mean_time /= n;
	*events_mean /= n;

	if (n > 1) {
		for (i = 0; i < n; i++) {
			double events_i = *events_mean - events_from_time(times[i]);
			*events_stddev += events_i * events_i / (n - 1);
		}
		*events_stddev = sqrt(*events_stddev);
	}

	return n;
}

static void hashmap_report_final(struct bench_res res[], int res_cnt)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	double events_mean, events_stddev;
	u64 mean_time;
	int i, n;

	for (i = 0; i < nr_cpus; i++) {
		n = compute_events(ctx.skel->bss->percpu_times[i], &events_mean,
				   &events_stddev, &mean_time);
		if (n == 0)
			continue;

		if (env.quiet) {
			/* we expect only one cpu to be present */
			if (env.affinity)
				printf("%.3lf\n", events_mean);
			else
				printf("cpu%02d %.3lf\n", i, events_mean);
		} else {
			printf("cpu%02d: lookup %.3lfM ± %.3lfM events/sec"
			       " (approximated from %d samples of ~%lums)\n",
			       i, events_mean, 2*events_stddev,
			       n, mean_time / 1000000);
		}
	}
}

const struct bench bench_bpf_hashmap_lookup = {
	.name = "bpf-hashmap-lookup",
	.argp = &bench_hashmap_lookup_argp,
	.validate = validate,
	.setup = setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = NULL,
	.report_final = hashmap_report_final,
};

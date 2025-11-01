// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Cloudflare */

/*
 * All of these benchmarks operate on tries with keys in the range
 * [0, args.nr_entries), i.e. there are no gaps or partially filled
 * branches of the trie for any key < args.nr_entries.
 *
 * This gives an idea of worst-case behaviour.
 */

#include <argp.h>
#include <linux/time64.h>
#include <linux/if_ether.h>
#include "lpm_trie_bench.skel.h"
#include "lpm_trie_map.skel.h"
#include "bench.h"
#include "testing_helpers.h"
#include "progs/lpm_trie.h"

static struct ctx {
	struct lpm_trie_bench *bench;
} ctx;

static struct {
	__u32 nr_entries;
	__u32 prefixlen;
	bool random;
} args = {
	.nr_entries = 0,
	.prefixlen = 32,
	.random = false,
};

enum {
	ARG_NR_ENTRIES = 9000,
	ARG_PREFIX_LEN,
	ARG_RANDOM,
};

static const struct argp_option opts[] = {
	{ "nr_entries", ARG_NR_ENTRIES, "NR_ENTRIES", 0,
	  "Number of unique entries in the LPM trie" },
	{ "prefix_len", ARG_PREFIX_LEN, "PREFIX_LEN", 0,
	  "Number of prefix bits to use in the LPM trie" },
	{ "random", ARG_RANDOM, NULL, 0, "Access random keys during op" },
	{},
};

static error_t lpm_parse_arg(int key, char *arg, struct argp_state *state)
{
	long ret;

	switch (key) {
	case ARG_NR_ENTRIES:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > UINT_MAX) {
			fprintf(stderr, "Invalid nr_entries count.");
			argp_usage(state);
		}
		args.nr_entries = ret;
		break;
	case ARG_PREFIX_LEN:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > UINT_MAX) {
			fprintf(stderr, "Invalid prefix_len value.");
			argp_usage(state);
		}
		args.prefixlen = ret;
		break;
	case ARG_RANDOM:
		args.random = true;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

const struct argp bench_lpm_trie_map_argp = {
	.options = opts,
	.parser = lpm_parse_arg,
};

static void validate_common(void)
{
	if (env.consumer_cnt != 0) {
		fprintf(stderr, "benchmark doesn't support consumer\n");
		exit(1);
	}

	if (args.nr_entries == 0) {
		fprintf(stderr, "Missing --nr_entries parameter\n");
		exit(1);
	}

	if ((1UL << args.prefixlen) < args.nr_entries) {
		fprintf(stderr, "prefix_len value too small for nr_entries\n");
		exit(1);
	}
}

static void lpm_insert_validate(void)
{
	validate_common();

	if (env.producer_cnt != 1) {
		fprintf(stderr, "lpm-trie-insert requires a single producer\n");
		exit(1);
	}

	if (args.random) {
		fprintf(stderr, "lpm-trie-insert does not support --random\n");
		exit(1);
	}
}

static void lpm_delete_validate(void)
{
	validate_common();

	if (env.producer_cnt != 1) {
		fprintf(stderr, "lpm-trie-delete requires a single producer\n");
		exit(1);
	}

	if (args.random) {
		fprintf(stderr, "lpm-trie-delete does not support --random\n");
		exit(1);
	}
}

static void lpm_free_validate(void)
{
	validate_common();

	if (env.producer_cnt != 1) {
		fprintf(stderr, "lpm-trie-free requires a single producer\n");
		exit(1);
	}

	if (args.random) {
		fprintf(stderr, "lpm-trie-free does not support --random\n");
		exit(1);
	}
}

static struct trie_key *keys;
static __u32 *vals;

static void fill_map(int map_fd)
{
	int err;

	DECLARE_LIBBPF_OPTS(bpf_map_batch_opts, opts,
		.elem_flags = 0,
		.flags = 0,
	);

	err = bpf_map_update_batch(map_fd, keys, vals, &args.nr_entries, &opts);
	if (err) {
		fprintf(stderr, "failed to batch update keys to map: %d\n",
			-err);
		exit(1);
	}
}

static void empty_map(int map_fd)
{
	int err;

	DECLARE_LIBBPF_OPTS(bpf_map_batch_opts, opts,
		.elem_flags = 0,
		.flags = 0,
	);

	err = bpf_map_delete_batch(map_fd, keys, &args.nr_entries, &opts);
	if (err) {
		fprintf(stderr, "failed to batch delete keys for map: %d\n",
			-err);
		exit(1);
	}
}

static void attach_prog(void)
{
	int i;

	ctx.bench = lpm_trie_bench__open_and_load();
	if (!ctx.bench) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	ctx.bench->bss->nr_entries = args.nr_entries;
	ctx.bench->bss->prefixlen = args.prefixlen;
	ctx.bench->bss->random = args.random;

	if (lpm_trie_bench__attach(ctx.bench)) {
		fprintf(stderr, "failed to attach skeleton\n");
		exit(1);
	}

	keys = calloc(args.nr_entries, sizeof(*keys));
	vals = calloc(args.nr_entries, sizeof(*vals));

	for (i = 0; i < args.nr_entries; i++) {
		struct trie_key *k = &keys[i];
		__u32 *v = &vals[i];

		k->prefixlen = args.prefixlen;
		k->data = i;
		*v = 1;
	}
}

static void attach_prog_and_fill_map(void)
{
	int fd;

	attach_prog();

	fd = bpf_map__fd(ctx.bench->maps.trie_map);
	fill_map(fd);
}

static void lpm_noop_setup(void)
{
	attach_prog();
	ctx.bench->bss->op = LPM_OP_NOOP;
}

static void lpm_baseline_setup(void)
{
	attach_prog();
	ctx.bench->bss->op = LPM_OP_BASELINE;
}

static void lpm_lookup_setup(void)
{
	attach_prog_and_fill_map();
	ctx.bench->bss->op = LPM_OP_LOOKUP;
}

static void lpm_insert_setup(void)
{
	attach_prog();
	ctx.bench->bss->op = LPM_OP_INSERT;
}

static void lpm_update_setup(void)
{
	attach_prog_and_fill_map();
	ctx.bench->bss->op = LPM_OP_UPDATE;
}

static void lpm_delete_setup(void)
{
	attach_prog_and_fill_map();
	ctx.bench->bss->op = LPM_OP_DELETE;
}

static void lpm_free_setup(void)
{
	attach_prog();
	ctx.bench->bss->op = LPM_OP_FREE;
}

static void lpm_measure(struct bench_res *res)
{
	res->hits = atomic_swap(&ctx.bench->bss->hits, 0);
	res->duration_ns = atomic_swap(&ctx.bench->bss->duration_ns, 0);
}

static void bench_reinit_map(void)
{
	int fd = bpf_map__fd(ctx.bench->maps.trie_map);

	switch (ctx.bench->bss->op) {
	case LPM_OP_INSERT:
		/* trie_map needs to be emptied */
		empty_map(fd);
		break;
	case LPM_OP_DELETE:
		/* trie_map needs to be refilled */
		fill_map(fd);
		break;
	default:
		fprintf(stderr, "Unexpected REINIT return code for op %d\n",
				ctx.bench->bss->op);
		exit(1);
	}
}

/* For NOOP, BASELINE, LOOKUP, INSERT, UPDATE, and DELETE */
static void *lpm_producer(void *unused __always_unused)
{
	int err;
	char in[ETH_HLEN]; /* unused */

	LIBBPF_OPTS(bpf_test_run_opts, opts, .data_in = in,
		    .data_size_in = sizeof(in), .repeat = 1, );

	while (true) {
		int fd = bpf_program__fd(ctx.bench->progs.run_bench);
		err = bpf_prog_test_run_opts(fd, &opts);
		if (err) {
			fprintf(stderr, "failed to run BPF prog: %d\n", err);
			exit(1);
		}

		/* Check for kernel error code */
		if ((int)opts.retval < 0) {
			fprintf(stderr, "BPF prog returned error: %d\n",
				opts.retval);
			exit(1);
		}

		switch (opts.retval) {
		case LPM_BENCH_SUCCESS:
			break;
		case LPM_BENCH_REINIT_MAP:
			bench_reinit_map();
			break;
		default:
			fprintf(stderr, "Unexpected BPF prog return code %d for op %d\n",
					opts.retval, ctx.bench->bss->op);
			exit(1);
		}
	}

	return NULL;
}

static void *lpm_free_producer(void *unused __always_unused)
{
	while (true) {
		struct lpm_trie_map *skel;

		skel = lpm_trie_map__open_and_load();
		if (!skel) {
			fprintf(stderr, "failed to open skeleton\n");
			exit(1);
		}

		fill_map(bpf_map__fd(skel->maps.trie_free_map));
		lpm_trie_map__destroy(skel);
	}

	return NULL;
}

/*
 * The standard bench op_report_*() functions assume measurements are
 * taken over a 1-second interval but operations that modify the map
 * (INSERT, DELETE, and FREE) cannot run indefinitely without
 * "resetting" the map to the initial state. Depending on the size of
 * the map, this likely needs to happen before the 1-second timer fires.
 *
 * Calculate the fraction of a second over which the op measurement was
 * taken (to ignore any time spent doing the reset) and report the
 * throughput results per second.
 */
static void frac_second_report_progress(int iter, struct bench_res *res,
					long delta_ns, double rate_divisor,
					char rate)
{
	double hits_per_sec, hits_per_prod;

	hits_per_sec = res->hits / rate_divisor /
		(res->duration_ns / (double)NSEC_PER_SEC);
	hits_per_prod = hits_per_sec / env.producer_cnt;

	printf("Iter %3d (%7.3lfus): ", iter,
	       (delta_ns - NSEC_PER_SEC) / 1000.0);
	printf("hits %8.3lf%c/s (%7.3lf%c/prod)\n", hits_per_sec, rate,
	       hits_per_prod, rate);
}

static void frac_second_report_final(struct bench_res res[], int res_cnt,
				     double lat_divisor, double rate_divisor,
				     char rate, const char *unit)
{
	double hits_mean = 0.0, hits_stddev = 0.0;
	double latency = 0.0;
	int i;

	for (i = 0; i < res_cnt; i++) {
		double val = res[i].hits / rate_divisor /
			     (res[i].duration_ns / (double)NSEC_PER_SEC);
		hits_mean += val / (0.0 + res_cnt);
		latency += res[i].duration_ns / res[i].hits / (0.0 + res_cnt);
	}

	if (res_cnt > 1) {
		for (i = 0; i < res_cnt; i++) {
			double val =
				res[i].hits / rate_divisor /
				(res[i].duration_ns / (double)NSEC_PER_SEC);
			hits_stddev += (hits_mean - val) * (hits_mean - val) /
				       (res_cnt - 1.0);
		}

		hits_stddev = sqrt(hits_stddev);
	}
	printf("Summary: throughput %8.3lf \u00B1 %5.3lf %c ops/s (%7.3lf%c ops/prod), ",
	       hits_mean, hits_stddev, rate, hits_mean / env.producer_cnt,
	       rate);
	printf("latency %8.3lf %s/op\n",
	       latency / lat_divisor / env.producer_cnt, unit);
}

static void insert_ops_report_progress(int iter, struct bench_res *res,
				       long delta_ns)
{
	double rate_divisor = 1000000.0;
	char rate = 'M';

	frac_second_report_progress(iter, res, delta_ns, rate_divisor, rate);
}

static void delete_ops_report_progress(int iter, struct bench_res *res,
				       long delta_ns)
{
	double rate_divisor = 1000000.0;
	char rate = 'M';

	frac_second_report_progress(iter, res, delta_ns, rate_divisor, rate);
}

static void free_ops_report_progress(int iter, struct bench_res *res,
				     long delta_ns)
{
	double rate_divisor = 1000.0;
	char rate = 'K';

	frac_second_report_progress(iter, res, delta_ns, rate_divisor, rate);
}

static void insert_ops_report_final(struct bench_res res[], int res_cnt)
{
	double lat_divisor = 1.0;
	double rate_divisor = 1000000.0;
	const char *unit = "ns";
	char rate = 'M';

	frac_second_report_final(res, res_cnt, lat_divisor, rate_divisor, rate,
				 unit);
}

static void delete_ops_report_final(struct bench_res res[], int res_cnt)
{
	double lat_divisor = 1.0;
	double rate_divisor = 1000000.0;
	const char *unit = "ns";
	char rate = 'M';

	frac_second_report_final(res, res_cnt, lat_divisor, rate_divisor, rate,
				 unit);
}

static void free_ops_report_final(struct bench_res res[], int res_cnt)
{
	double lat_divisor = 1000000.0;
	double rate_divisor = 1000.0;
	const char *unit = "ms";
	char rate = 'K';

	frac_second_report_final(res, res_cnt, lat_divisor, rate_divisor, rate,
				 unit);
}

/* noop bench measures harness-overhead */
const struct bench bench_lpm_trie_noop = {
	.name = "lpm-trie-noop",
	.argp = &bench_lpm_trie_map_argp,
	.validate = validate_common,
	.setup = lpm_noop_setup,
	.producer_thread = lpm_producer,
	.measure = lpm_measure,
	.report_progress = ops_report_progress,
	.report_final = ops_report_final,
};

/* baseline overhead for lookup and update */
const struct bench bench_lpm_trie_baseline = {
	.name = "lpm-trie-baseline",
	.argp = &bench_lpm_trie_map_argp,
	.validate = validate_common,
	.setup = lpm_baseline_setup,
	.producer_thread = lpm_producer,
	.measure = lpm_measure,
	.report_progress = ops_report_progress,
	.report_final = ops_report_final,
};

/* measure cost of doing a lookup on existing entries in a full trie */
const struct bench bench_lpm_trie_lookup = {
	.name = "lpm-trie-lookup",
	.argp = &bench_lpm_trie_map_argp,
	.validate = validate_common,
	.setup = lpm_lookup_setup,
	.producer_thread = lpm_producer,
	.measure = lpm_measure,
	.report_progress = ops_report_progress,
	.report_final = ops_report_final,
};

/* measure cost of inserting new entries into an empty trie */
const struct bench bench_lpm_trie_insert = {
	.name = "lpm-trie-insert",
	.argp = &bench_lpm_trie_map_argp,
	.validate = lpm_insert_validate,
	.setup = lpm_insert_setup,
	.producer_thread = lpm_producer,
	.measure = lpm_measure,
	.report_progress = insert_ops_report_progress,
	.report_final = insert_ops_report_final,
};

/* measure cost of updating existing entries in a full trie */
const struct bench bench_lpm_trie_update = {
	.name = "lpm-trie-update",
	.argp = &bench_lpm_trie_map_argp,
	.validate = validate_common,
	.setup = lpm_update_setup,
	.producer_thread = lpm_producer,
	.measure = lpm_measure,
	.report_progress = ops_report_progress,
	.report_final = ops_report_final,
};

/* measure cost of deleting existing entries from a full trie */
const struct bench bench_lpm_trie_delete = {
	.name = "lpm-trie-delete",
	.argp = &bench_lpm_trie_map_argp,
	.validate = lpm_delete_validate,
	.setup = lpm_delete_setup,
	.producer_thread = lpm_producer,
	.measure = lpm_measure,
	.report_progress = delete_ops_report_progress,
	.report_final = delete_ops_report_final,
};

/* measure cost of freeing a full trie */
const struct bench bench_lpm_trie_free = {
	.name = "lpm-trie-free",
	.argp = &bench_lpm_trie_map_argp,
	.validate = lpm_free_validate,
	.setup = lpm_free_setup,
	.producer_thread = lpm_free_producer,
	.measure = lpm_measure,
	.report_progress = free_ops_report_progress,
	.report_final = free_ops_report_final,
};

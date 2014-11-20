/*
 * builtin-bench.c
 *
 * General benchmarking collections provided by perf
 *
 * Copyright (C) 2009, Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 */

/*
 * Available benchmark collection list:
 *
 *  sched ... scheduler and IPC performance
 *  mem   ... memory access performance
 *  numa  ... NUMA scheduling and MM performance
 *  futex ... Futex performance
 */
#include "perf.h"
#include "util/util.h"
#include "util/parse-options.h"
#include "builtin.h"
#include "bench/bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

typedef int (*bench_fn_t)(int argc, const char **argv, const char *prefix);

struct bench {
	const char	*name;
	const char	*summary;
	bench_fn_t	fn;
};

#ifdef HAVE_LIBNUMA_SUPPORT
static struct bench numa_benchmarks[] = {
	{ "mem",	"Benchmark for NUMA workloads",			bench_numa		},
	{ "all",	"Test all NUMA benchmarks",			NULL			},
	{ NULL,		NULL,						NULL			}
};
#endif

static struct bench sched_benchmarks[] = {
	{ "messaging",	"Benchmark for scheduling and IPC",		bench_sched_messaging	},
	{ "pipe",	"Benchmark for pipe() between two processes",	bench_sched_pipe	},
	{ "all",	"Test all scheduler benchmarks",		NULL			},
	{ NULL,		NULL,						NULL			}
};

static struct bench mem_benchmarks[] = {
	{ "memcpy",	"Benchmark for memcpy()",			bench_mem_memcpy	},
	{ "memset",	"Benchmark for memset() tests",			bench_mem_memset	},
	{ "all",	"Test all memory benchmarks",			NULL			},
	{ NULL,		NULL,						NULL			}
};

static struct bench futex_benchmarks[] = {
	{ "hash",	"Benchmark for futex hash table",               bench_futex_hash	},
	{ "wake",	"Benchmark for futex wake calls",               bench_futex_wake	},
	{ "requeue",	"Benchmark for futex requeue calls",            bench_futex_requeue	},
	{ "all",	"Test all futex benchmarks",			NULL			},
	{ NULL,		NULL,						NULL			}
};

struct collection {
	const char	*name;
	const char	*summary;
	struct bench	*benchmarks;
};

static struct collection collections[] = {
	{ "sched",	"Scheduler and IPC benchmarks",			sched_benchmarks	},
	{ "mem",	"Memory access benchmarks",			mem_benchmarks		},
#ifdef HAVE_LIBNUMA_SUPPORT
	{ "numa",	"NUMA scheduling and MM benchmarks",		numa_benchmarks		},
#endif
	{"futex",       "Futex stressing benchmarks",                   futex_benchmarks        },
	{ "all",	"All benchmarks",				NULL			},
	{ NULL,		NULL,						NULL			}
};

/* Iterate over all benchmark collections: */
#define for_each_collection(coll) \
	for (coll = collections; coll->name; coll++)

/* Iterate over all benchmarks within a collection: */
#define for_each_bench(coll, bench) \
	for (bench = coll->benchmarks; bench && bench->name; bench++)

static void dump_benchmarks(struct collection *coll)
{
	struct bench *bench;

	printf("\n        # List of available benchmarks for collection '%s':\n\n", coll->name);

	for_each_bench(coll, bench)
		printf("%14s: %s\n", bench->name, bench->summary);

	printf("\n");
}

static const char *bench_format_str;

/* Output/formatting style, exported to benchmark modules: */
int bench_format = BENCH_FORMAT_DEFAULT;
unsigned int bench_repeat = 10; /* default number of times to repeat the run */

static const struct option bench_options[] = {
	OPT_STRING('f', "format", &bench_format_str, "default", "Specify format style"),
	OPT_UINTEGER('r', "repeat",  &bench_repeat,   "Specify amount of times to repeat the run"),
	OPT_END()
};

static const char * const bench_usage[] = {
	"perf bench [<common options>] <collection> <benchmark> [<options>]",
	NULL
};

static void print_usage(void)
{
	struct collection *coll;
	int i;

	printf("Usage: \n");
	for (i = 0; bench_usage[i]; i++)
		printf("\t%s\n", bench_usage[i]);
	printf("\n");

	printf("        # List of all available benchmark collections:\n\n");

	for_each_collection(coll)
		printf("%14s: %s\n", coll->name, coll->summary);
	printf("\n");
}

static int bench_str2int(const char *str)
{
	if (!str)
		return BENCH_FORMAT_DEFAULT;

	if (!strcmp(str, BENCH_FORMAT_DEFAULT_STR))
		return BENCH_FORMAT_DEFAULT;
	else if (!strcmp(str, BENCH_FORMAT_SIMPLE_STR))
		return BENCH_FORMAT_SIMPLE;

	return BENCH_FORMAT_UNKNOWN;
}

/*
 * Run a specific benchmark but first rename the running task's ->comm[]
 * to something meaningful:
 */
static int run_bench(const char *coll_name, const char *bench_name, bench_fn_t fn,
		     int argc, const char **argv, const char *prefix)
{
	int size;
	char *name;
	int ret;

	size = strlen(coll_name) + 1 + strlen(bench_name) + 1;

	name = zalloc(size);
	BUG_ON(!name);

	scnprintf(name, size, "%s-%s", coll_name, bench_name);

	prctl(PR_SET_NAME, name);
	argv[0] = name;

	ret = fn(argc, argv, prefix);

	free(name);

	return ret;
}

static void run_collection(struct collection *coll)
{
	struct bench *bench;
	const char *argv[2];

	argv[1] = NULL;
	/*
	 * TODO:
	 *
	 * Preparing preset parameters for
	 * embedded, ordinary PC, HPC, etc...
	 * would be helpful.
	 */
	for_each_bench(coll, bench) {
		if (!bench->fn)
			break;
		printf("# Running %s/%s benchmark...\n", coll->name, bench->name);
		fflush(stdout);

		argv[1] = bench->name;
		run_bench(coll->name, bench->name, bench->fn, 1, argv, NULL);
		printf("\n");
	}
}

static void run_all_collections(void)
{
	struct collection *coll;

	for_each_collection(coll)
		run_collection(coll);
}

int cmd_bench(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct collection *coll;
	int ret = 0;

	if (argc < 2) {
		/* No collection specified. */
		print_usage();
		goto end;
	}

	argc = parse_options(argc, argv, bench_options, bench_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	bench_format = bench_str2int(bench_format_str);
	if (bench_format == BENCH_FORMAT_UNKNOWN) {
		printf("Unknown format descriptor: '%s'\n", bench_format_str);
		goto end;
	}

	if (bench_repeat == 0) {
		printf("Invalid repeat option: Must specify a positive value\n");
		goto end;
	}

	if (argc < 1) {
		print_usage();
		goto end;
	}

	if (!strcmp(argv[0], "all")) {
		run_all_collections();
		goto end;
	}

	for_each_collection(coll) {
		struct bench *bench;

		if (strcmp(coll->name, argv[0]))
			continue;

		if (argc < 2) {
			/* No bench specified. */
			dump_benchmarks(coll);
			goto end;
		}

		if (!strcmp(argv[1], "all")) {
			run_collection(coll);
			goto end;
		}

		for_each_bench(coll, bench) {
			if (strcmp(bench->name, argv[1]))
				continue;

			if (bench_format == BENCH_FORMAT_DEFAULT)
				printf("# Running '%s/%s' benchmark:\n", coll->name, bench->name);
			fflush(stdout);
			ret = run_bench(coll->name, bench->name, bench->fn, argc-1, argv+1, prefix);
			goto end;
		}

		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			dump_benchmarks(coll);
			goto end;
		}

		printf("Unknown benchmark: '%s' for collection '%s'\n", argv[1], argv[0]);
		ret = 1;
		goto end;
	}

	printf("Unknown collection: '%s'\n", argv[0]);
	ret = 1;

end:
	return ret;
}

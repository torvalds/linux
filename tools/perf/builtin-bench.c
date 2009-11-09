/*
 *
 * builtin-bench.c
 *
 * General benchmarking subsystem provided by perf
 *
 * Copyright (C) 2009, Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 *
 */

/*
 *
 * Available subsystem list:
 *  sched ... scheduler and IPC mechanism
 *
 */

#include "perf.h"
#include "util/util.h"
#include "util/parse-options.h"
#include "builtin.h"
#include "bench/bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bench_suite {
	const char *name;
	const char *summary;
	int (*fn)(int, const char **, const char *);
};

static struct bench_suite sched_suites[] = {
	{ "messaging",
	  "Benchmark for scheduler and IPC mechanisms",
	  bench_sched_messaging },
	{ "pipe",
	  "Flood of communication over pipe() between two processes",
	  bench_sched_pipe      },
	{ NULL,
	  NULL,
	  NULL                  }
};

struct bench_subsys {
	const char *name;
	const char *summary;
	struct bench_suite *suites;
};

static struct bench_subsys subsystems[] = {
	{ "sched",
	  "scheduler and IPC mechanism",
	  sched_suites },
	{ NULL,
	  NULL,
	  NULL         }
};

static void dump_suites(int subsys_index)
{
	int i;

	printf("List of available suites for %s...\n\n",
	       subsystems[subsys_index].name);

	for (i = 0; subsystems[subsys_index].suites[i].name; i++)
		printf("\t%s: %s\n",
		       subsystems[subsys_index].suites[i].name,
		       subsystems[subsys_index].suites[i].summary);

	printf("\n");
	return;
}

static char *bench_format_str;
int bench_format = BENCH_FORMAT_DEFAULT;

static const struct option bench_options[] = {
	OPT_STRING('f', "format", &bench_format_str, "default",
		    "Specify format style"),
	OPT_END()
};

static const char * const bench_usage[] = {
	"perf bench [<common options>] <subsystem> <suite> [<options>]",
	NULL
};

static void print_usage(void)
{
	int i;

	printf("Usage: \n");
	for (i = 0; bench_usage[i]; i++)
		printf("\t%s\n", bench_usage[i]);
	printf("\n");

	printf("List of available subsystems...\n\n");

	for (i = 0; subsystems[i].name; i++)
		printf("\t%s: %s\n",
		       subsystems[i].name, subsystems[i].summary);
	printf("\n");
}

static int bench_str2int(char *str)
{
	if (!str)
		return BENCH_FORMAT_DEFAULT;

	if (!strcmp(str, BENCH_FORMAT_DEFAULT_STR))
		return BENCH_FORMAT_DEFAULT;
	else if (!strcmp(str, BENCH_FORMAT_SIMPLE_STR))
		return BENCH_FORMAT_SIMPLE;

	return BENCH_FORMAT_UNKNOWN;
}

int cmd_bench(int argc, const char **argv, const char *prefix __used)
{
	int i, j, status = 0;

	if (argc < 2) {
		/* No subsystem specified. */
		print_usage();
		goto end;
	}

	argc = parse_options(argc, argv, bench_options, bench_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	bench_format = bench_str2int(bench_format_str);
	if (bench_format == BENCH_FORMAT_UNKNOWN) {
		printf("Unknown format descriptor:%s\n", bench_format_str);
		goto end;
	}

	if (argc < 1) {
		print_usage();
		goto end;
	}

	for (i = 0; subsystems[i].name; i++) {
		if (strcmp(subsystems[i].name, argv[0]))
			continue;

		if (argc < 2) {
			/* No suite specified. */
			dump_suites(i);
			goto end;
		}

		for (j = 0; subsystems[i].suites[j].name; j++) {
			if (strcmp(subsystems[i].suites[j].name, argv[1]))
				continue;

			status = subsystems[i].suites[j].fn(argc - 1,
							    argv + 1, prefix);
			goto end;
		}

		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			dump_suites(i);
			goto end;
		}

		printf("Unknown suite:%s for %s\n", argv[1], argv[0]);
		status = 1;
		goto end;
	}

	printf("Unknown subsystem:%s\n", argv[0]);
	status = 1;

end:
	return status;
}

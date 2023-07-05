// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * uprobe.c
 *
 * uprobe benchmarks
 *
 *  Copyright (C) 2023, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "../perf.h"
#include "../util/util.h"
#include <subcmd/parse-options.h>
#include "../builtin.h"
#include "bench.h"
#include <linux/time64.h>

#include <inttypes.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#define LOOPS_DEFAULT 1000
static int loops = LOOPS_DEFAULT;

static const struct option options[] = {
	OPT_INTEGER('l', "loop",	&loops,		"Specify number of loops"),
	OPT_END()
};

static const char * const bench_uprobe_usage[] = {
	"perf bench uprobe <options>",
	NULL
};

static int bench_uprobe_format__default_fprintf(const char *name, const char *unit, u64 diff, FILE *fp)
{
	static u64 baseline, previous;
	s64 diff_to_baseline = diff - baseline,
	    diff_to_previous = diff - previous;
	int printed = fprintf(fp, "# Executed %'d %s calls\n", loops, name);

	printed += fprintf(fp, " %14s: %'" PRIu64 " %ss", "Total time", diff, unit);

	if (baseline) {
		printed += fprintf(fp, " %s%'" PRId64 " to baseline", diff_to_baseline > 0 ? "+" : "", diff_to_baseline);

		if (previous != baseline)
			fprintf(stdout, " %s%'" PRId64 " to previous", diff_to_previous > 0 ? "+" : "", diff_to_previous);
	}

	printed += fprintf(fp, "\n\n %'.3f %ss/op", (double)diff / (double)loops, unit);

	if (baseline) {
		printed += fprintf(fp, " %'.3f %ss/op to baseline", (double)diff_to_baseline / (double)loops, unit);

		if (previous != baseline)
			printed += fprintf(fp, " %'.3f %ss/op to previous", (double)diff_to_previous / (double)loops, unit);
	} else {
		baseline = diff;
	}

	fputc('\n', fp);

	previous = diff;

	return printed + 1;
}

static int bench_uprobe(int argc, const char **argv)
{
	const char *name = "usleep(1000)", *unit = "usec";
	struct timespec start, end;
	u64 diff;
	int i;

	argc = parse_options(argc, argv, options, bench_uprobe_usage, 0);

	clock_gettime(CLOCK_REALTIME, &start);

	for (i = 0; i < loops; i++) {
		usleep(USEC_PER_MSEC);
	}

	clock_gettime(CLOCK_REALTIME, &end);

	diff = end.tv_sec * NSEC_PER_SEC + end.tv_nsec - (start.tv_sec * NSEC_PER_SEC + start.tv_nsec);
	diff /= NSEC_PER_USEC;

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		bench_uprobe_format__default_fprintf(name, unit, diff, stdout);
		break;

	case BENCH_FORMAT_SIMPLE:
		printf("%" PRIu64 "\n", diff);
		break;

	default:
		/* reaching here is something of a disaster */
		fprintf(stderr, "Unknown format:%d\n", bench_format);
		exit(1);
	}

	return 0;
}

int bench_uprobe_baseline(int argc, const char **argv)
{
	return bench_uprobe(argc, argv);
}

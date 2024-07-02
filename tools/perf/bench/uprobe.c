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
#include <linux/compiler.h>
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

enum bench_uprobe {
        BENCH_UPROBE__BASELINE,
        BENCH_UPROBE__EMPTY,
        BENCH_UPROBE__TRACE_PRINTK,
};

static const struct option options[] = {
	OPT_INTEGER('l', "loop",	&loops,		"Specify number of loops"),
	OPT_END()
};

static const char * const bench_uprobe_usage[] = {
	"perf bench uprobe <options>",
	NULL
};

#ifdef HAVE_BPF_SKEL
#include "bpf_skel/bench_uprobe.skel.h"

#define bench_uprobe__attach_uprobe(prog) \
	skel->links.prog = bpf_program__attach_uprobe_opts(/*prog=*/skel->progs.prog, \
							   /*pid=*/-1, \
							   /*binary_path=*/"/lib64/libc.so.6", \
							   /*func_offset=*/0, \
							   /*opts=*/&uprobe_opts); \
	if (!skel->links.prog) { \
		err = -errno; \
		fprintf(stderr, "Failed to attach bench uprobe \"%s\": %s\n", #prog, strerror(errno)); \
		goto cleanup; \
	}

struct bench_uprobe_bpf *skel;

static int bench_uprobe__setup_bpf_skel(enum bench_uprobe bench)
{
	DECLARE_LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts);
	int err;

	/* Load and verify BPF application */
	skel = bench_uprobe_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open and load uprobes bench BPF skeleton\n");
		return -1;
	}

	err = bench_uprobe_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load and verify BPF skeleton\n");
		goto cleanup;
	}

	uprobe_opts.func_name = "usleep";
	switch (bench) {
	case BENCH_UPROBE__BASELINE:							break;
	case BENCH_UPROBE__EMPTY:	 bench_uprobe__attach_uprobe(empty);		break;
	case BENCH_UPROBE__TRACE_PRINTK: bench_uprobe__attach_uprobe(trace_printk);	break;
	default:
		fprintf(stderr, "Invalid bench: %d\n", bench);
		goto cleanup;
	}

	return err;
cleanup:
	bench_uprobe_bpf__destroy(skel);
	skel = NULL;
	return err;
}

static void bench_uprobe__teardown_bpf_skel(void)
{
	if (skel) {
		bench_uprobe_bpf__destroy(skel);
		skel = NULL;
	}
}
#else
static int bench_uprobe__setup_bpf_skel(enum bench_uprobe bench __maybe_unused) { return 0; }
static void bench_uprobe__teardown_bpf_skel(void) {};
#endif

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

static int bench_uprobe(int argc, const char **argv, enum bench_uprobe bench)
{
	const char *name = "usleep(1000)", *unit = "usec";
	struct timespec start, end;
	u64 diff;
	int i;

	argc = parse_options(argc, argv, options, bench_uprobe_usage, 0);

	if (bench != BENCH_UPROBE__BASELINE && bench_uprobe__setup_bpf_skel(bench) < 0)
		return 0;

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

	if (bench != BENCH_UPROBE__BASELINE)
		bench_uprobe__teardown_bpf_skel();

	return 0;
}

int bench_uprobe_baseline(int argc, const char **argv)
{
	return bench_uprobe(argc, argv, BENCH_UPROBE__BASELINE);
}

int bench_uprobe_empty(int argc, const char **argv)
{
	return bench_uprobe(argc, argv, BENCH_UPROBE__EMPTY);
}

int bench_uprobe_trace_printk(int argc, const char **argv)
{
	return bench_uprobe(argc, argv, BENCH_UPROBE__TRACE_PRINTK);
}

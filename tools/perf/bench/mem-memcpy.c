/*
 * mem-memcpy.c
 *
 * memcpy: Simple memory copy in various ways
 *
 * Written by Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 */
#include <ctype.h>

#include "../perf.h"
#include "../util/util.h"
#include "../util/parse-options.h"
#include "../util/header.h"
#include "bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

#define K 1024

static const char	*length_str	= "1MB";
static const char	*routine	= "default";
static bool		use_clock	= false;
static int		clock_fd;

static const struct option options[] = {
	OPT_STRING('l', "length", &length_str, "1MB",
		    "Specify length of memory to copy. "
		    "available unit: B, MB, GB (upper and lower)"),
	OPT_STRING('r', "routine", &routine, "default",
		    "Specify routine to copy"),
	OPT_BOOLEAN('c', "clock", &use_clock,
		    "Use CPU clock for measuring"),
	OPT_END()
};

struct routine {
	const char *name;
	const char *desc;
	void * (*fn)(void *dst, const void *src, size_t len);
};

struct routine routines[] = {
	{ "default",
	  "Default memcpy() provided by glibc",
	  memcpy },
	{ NULL,
	  NULL,
	  NULL   }
};

static const char * const bench_mem_memcpy_usage[] = {
	"perf bench mem memcpy <options>",
	NULL
};

static struct perf_event_attr clock_attr = {
	.type		= PERF_TYPE_HARDWARE,
	.config		= PERF_COUNT_HW_CPU_CYCLES
};

static void init_clock(void)
{
	clock_fd = sys_perf_event_open(&clock_attr, getpid(), -1, -1, 0);

	if (clock_fd < 0 && errno == ENOSYS)
		die("No CONFIG_PERF_EVENTS=y kernel support configured?\n");
	else
		BUG_ON(clock_fd < 0);
}

static u64 get_clock(void)
{
	int ret;
	u64 clk;

	ret = read(clock_fd, &clk, sizeof(u64));
	BUG_ON(ret != sizeof(u64));

	return clk;
}

static double timeval2double(struct timeval *ts)
{
	return (double)ts->tv_sec +
		(double)ts->tv_usec / (double)1000000;
}

int bench_mem_memcpy(int argc, const char **argv,
		     const char *prefix __used)
{
	int i;
	void *dst, *src;
	size_t length;
	double bps = 0.0;
	struct timeval tv_start, tv_end, tv_diff;
	u64 clock_start, clock_end, clock_diff;

	clock_start = clock_end = clock_diff = 0ULL;
	argc = parse_options(argc, argv, options,
			     bench_mem_memcpy_usage, 0);

	tv_diff.tv_sec = 0;
	tv_diff.tv_usec = 0;
	length = (size_t)perf_atoll((char *)length_str);

	if ((s64)length <= 0) {
		fprintf(stderr, "Invalid length:%s\n", length_str);
		return 1;
	}

	for (i = 0; routines[i].name; i++) {
		if (!strcmp(routines[i].name, routine))
			break;
	}
	if (!routines[i].name) {
		printf("Unknown routine:%s\n", routine);
		printf("Available routines...\n");
		for (i = 0; routines[i].name; i++) {
			printf("\t%s ... %s\n",
			       routines[i].name, routines[i].desc);
		}
		return 1;
	}

	dst = zalloc(length);
	if (!dst)
		die("memory allocation failed - maybe length is too large?\n");

	src = zalloc(length);
	if (!src)
		die("memory allocation failed - maybe length is too large?\n");

	if (bench_format == BENCH_FORMAT_DEFAULT) {
		printf("# Copying %s Bytes from %p to %p ...\n\n",
		       length_str, src, dst);
	}

	if (use_clock) {
		init_clock();
		clock_start = get_clock();
	} else {
		BUG_ON(gettimeofday(&tv_start, NULL));
	}

	routines[i].fn(dst, src, length);

	if (use_clock) {
		clock_end = get_clock();
		clock_diff = clock_end - clock_start;
	} else {
		BUG_ON(gettimeofday(&tv_end, NULL));
		timersub(&tv_end, &tv_start, &tv_diff);
		bps = (double)((double)length / timeval2double(&tv_diff));
	}

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		if (use_clock) {
			printf(" %14lf Clock/Byte\n",
			       (double)clock_diff / (double)length);
		} else {
			if (bps < K)
				printf(" %14lf B/Sec\n", bps);
			else if (bps < K * K)
				printf(" %14lfd KB/Sec\n", bps / 1024);
			else if (bps < K * K * K)
				printf(" %14lf MB/Sec\n", bps / 1024 / 1024);
			else {
				printf(" %14lf GB/Sec\n",
				       bps / 1024 / 1024 / 1024);
			}
		}
		break;
	case BENCH_FORMAT_SIMPLE:
		if (use_clock) {
			printf("%14lf\n",
			       (double)clock_diff / (double)length);
		} else
			printf("%lf\n", bps);
		break;
	default:
		/* reaching this means there's some disaster: */
		die("unknown format: %d\n", bench_format);
		break;
	}

	return 0;
}

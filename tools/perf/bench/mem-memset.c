/*
 * mem-memset.c
 *
 * memset: Simple memory set in various ways
 *
 * Trivial clone of mem-memcpy.c.
 */

#include "../perf.h"
#include "../util/util.h"
#include "../util/parse-options.h"
#include "../util/header.h"
#include "../util/cloexec.h"
#include "bench.h"
#include "mem-memset-arch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

#define K 1024

static const char	*length_str	= "1MB";
static const char	*routine	= "default";
static int		iterations	= 1;
static bool		use_cycle;
static int		cycle_fd;
static bool		only_prefault;
static bool		no_prefault;

static const struct option options[] = {
	OPT_STRING('l', "length", &length_str, "1MB",
		    "Specify length of memory to set. "
		    "Available units: B, KB, MB, GB and TB (upper and lower)"),
	OPT_STRING('r', "routine", &routine, "default",
		    "Specify routine to set"),
	OPT_INTEGER('i', "iterations", &iterations,
		    "repeat memset() invocation this number of times"),
	OPT_BOOLEAN('c', "cycle", &use_cycle,
		    "Use cycles event instead of gettimeofday() for measuring"),
	OPT_BOOLEAN('o', "only-prefault", &only_prefault,
		    "Show only the result with page faults before memset()"),
	OPT_BOOLEAN('n', "no-prefault", &no_prefault,
		    "Show only the result without page faults before memset()"),
	OPT_END()
};

typedef void *(*memset_t)(void *, int, size_t);

struct routine {
	const char *name;
	const char *desc;
	memset_t fn;
};

static const struct routine routines[] = {
	{ "default",
	  "Default memset() provided by glibc",
	  memset },
#ifdef HAVE_ARCH_X86_64_SUPPORT

#define MEMSET_FN(fn, name, desc) { name, desc, fn },
#include "mem-memset-x86-64-asm-def.h"
#undef MEMSET_FN

#endif

	{ NULL,
	  NULL,
	  NULL   }
};

static const char * const bench_mem_memset_usage[] = {
	"perf bench mem memset <options>",
	NULL
};

static struct perf_event_attr cycle_attr = {
	.type		= PERF_TYPE_HARDWARE,
	.config		= PERF_COUNT_HW_CPU_CYCLES
};

static void init_cycle(void)
{
	cycle_fd = sys_perf_event_open(&cycle_attr, getpid(), -1, -1,
				       perf_event_open_cloexec_flag());

	if (cycle_fd < 0 && errno == ENOSYS)
		die("No CONFIG_PERF_EVENTS=y kernel support configured?\n");
	else
		BUG_ON(cycle_fd < 0);
}

static u64 get_cycle(void)
{
	int ret;
	u64 clk;

	ret = read(cycle_fd, &clk, sizeof(u64));
	BUG_ON(ret != sizeof(u64));

	return clk;
}

static double timeval2double(struct timeval *ts)
{
	return (double)ts->tv_sec +
		(double)ts->tv_usec / (double)1000000;
}

static void alloc_mem(void **dst, size_t length)
{
	*dst = zalloc(length);
	if (!*dst)
		die("memory allocation failed - maybe length is too large?\n");
}

static u64 do_memset_cycle(memset_t fn, size_t len, bool prefault)
{
	u64 cycle_start = 0ULL, cycle_end = 0ULL;
	void *dst = NULL;
	int i;

	alloc_mem(&dst, len);

	if (prefault)
		fn(dst, -1, len);

	cycle_start = get_cycle();
	for (i = 0; i < iterations; ++i)
		fn(dst, i, len);
	cycle_end = get_cycle();

	free(dst);
	return cycle_end - cycle_start;
}

static double do_memset_gettimeofday(memset_t fn, size_t len, bool prefault)
{
	struct timeval tv_start, tv_end, tv_diff;
	void *dst = NULL;
	int i;

	alloc_mem(&dst, len);

	if (prefault)
		fn(dst, -1, len);

	BUG_ON(gettimeofday(&tv_start, NULL));
	for (i = 0; i < iterations; ++i)
		fn(dst, i, len);
	BUG_ON(gettimeofday(&tv_end, NULL));

	timersub(&tv_end, &tv_start, &tv_diff);

	free(dst);
	return (double)((double)len / timeval2double(&tv_diff));
}

#define pf (no_prefault ? 0 : 1)

#define print_bps(x) do {					\
		if (x < K)					\
			printf(" %14lf B/Sec", x);		\
		else if (x < K * K)				\
			printf(" %14lfd KB/Sec", x / K);	\
		else if (x < K * K * K)				\
			printf(" %14lf MB/Sec", x / K / K);	\
		else						\
			printf(" %14lf GB/Sec", x / K / K / K); \
	} while (0)

int bench_mem_memset(int argc, const char **argv,
		     const char *prefix __maybe_unused)
{
	int i;
	size_t len;
	double result_bps[2];
	u64 result_cycle[2];

	argc = parse_options(argc, argv, options,
			     bench_mem_memset_usage, 0);

	if (no_prefault && only_prefault) {
		fprintf(stderr, "Invalid options: -o and -n are mutually exclusive\n");
		return 1;
	}

	if (use_cycle)
		init_cycle();

	len = (size_t)perf_atoll((char *)length_str);

	result_cycle[0] = result_cycle[1] = 0ULL;
	result_bps[0] = result_bps[1] = 0.0;

	if ((s64)len <= 0) {
		fprintf(stderr, "Invalid length:%s\n", length_str);
		return 1;
	}

	/* same to without specifying either of prefault and no-prefault */
	if (only_prefault && no_prefault)
		only_prefault = no_prefault = false;

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

	if (bench_format == BENCH_FORMAT_DEFAULT)
		printf("# Copying %s Bytes ...\n\n", length_str);

	if (!only_prefault && !no_prefault) {
		/* show both of results */
		if (use_cycle) {
			result_cycle[0] =
				do_memset_cycle(routines[i].fn, len, false);
			result_cycle[1] =
				do_memset_cycle(routines[i].fn, len, true);
		} else {
			result_bps[0] =
				do_memset_gettimeofday(routines[i].fn,
						len, false);
			result_bps[1] =
				do_memset_gettimeofday(routines[i].fn,
						len, true);
		}
	} else {
		if (use_cycle) {
			result_cycle[pf] =
				do_memset_cycle(routines[i].fn,
						len, only_prefault);
		} else {
			result_bps[pf] =
				do_memset_gettimeofday(routines[i].fn,
						len, only_prefault);
		}
	}

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		if (!only_prefault && !no_prefault) {
			if (use_cycle) {
				printf(" %14lf Cycle/Byte\n",
					(double)result_cycle[0]
					/ (double)len);
				printf(" %14lf Cycle/Byte (with prefault)\n ",
					(double)result_cycle[1]
					/ (double)len);
			} else {
				print_bps(result_bps[0]);
				printf("\n");
				print_bps(result_bps[1]);
				printf(" (with prefault)\n");
			}
		} else {
			if (use_cycle) {
				printf(" %14lf Cycle/Byte",
					(double)result_cycle[pf]
					/ (double)len);
			} else
				print_bps(result_bps[pf]);

			printf("%s\n", only_prefault ? " (with prefault)" : "");
		}
		break;
	case BENCH_FORMAT_SIMPLE:
		if (!only_prefault && !no_prefault) {
			if (use_cycle) {
				printf("%lf %lf\n",
					(double)result_cycle[0] / (double)len,
					(double)result_cycle[1] / (double)len);
			} else {
				printf("%lf %lf\n",
					result_bps[0], result_bps[1]);
			}
		} else {
			if (use_cycle) {
				printf("%lf\n", (double)result_cycle[pf]
					/ (double)len);
			} else
				printf("%lf\n", result_bps[pf]);
		}
		break;
	default:
		/* reaching this means there's some disaster: */
		die("unknown format: %d\n", bench_format);
		break;
	}

	return 0;
}

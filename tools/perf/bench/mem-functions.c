/*
 * mem-memcpy.c
 *
 * Simple memcpy() and memset() benchmarks
 *
 * Written by Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 */

#include "../perf.h"
#include "../util/util.h"
#include "../util/parse-options.h"
#include "../util/header.h"
#include "../util/cloexec.h"
#include "bench.h"
#include "mem-memcpy-arch.h"
#include "mem-memset-arch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

#define K 1024

static const char	*length_str	= "1MB";
static const char	*routine_str	= "all";
static int		iterations	= 1;
static bool		use_cycles;
static int		cycles_fd;

static const struct option options[] = {
	OPT_STRING('l', "length", &length_str, "1MB",
		    "Specify length of memory to copy. "
		    "Available units: B, KB, MB, GB and TB (upper and lower)"),
	OPT_STRING('r', "routine", &routine_str, "all",
		    "Specify the routine to run, \"all\" runs all available routines"),
	OPT_INTEGER('i', "iterations", &iterations,
		    "repeat memcpy() invocation this number of times"),
	OPT_BOOLEAN('c', "cycles", &use_cycles,
		    "Use a cycles event instead of gettimeofday() to measure performance"),
	OPT_END()
};

typedef void *(*memcpy_t)(void *, const void *, size_t);
typedef void *(*memset_t)(void *, int, size_t);

struct routine {
	const char *name;
	const char *desc;
	union {
		memcpy_t memcpy;
		memset_t memset;
	} fn;
};

struct routine memcpy_routines[] = {
	{ .name		= "default",
	  .desc		= "Default memcpy() provided by glibc",
	  .fn.memcpy	= memcpy },

#ifdef HAVE_ARCH_X86_64_SUPPORT
# define MEMCPY_FN(_fn, _name, _desc) {.name = _name, .desc = _desc, .fn.memcpy = _fn},
# include "mem-memcpy-x86-64-asm-def.h"
# undef MEMCPY_FN
#endif

	{ NULL, }
};

static const char * const bench_mem_memcpy_usage[] = {
	"perf bench mem memcpy <options>",
	NULL
};

static struct perf_event_attr cycle_attr = {
	.type		= PERF_TYPE_HARDWARE,
	.config		= PERF_COUNT_HW_CPU_CYCLES
};

static void init_cycles(void)
{
	cycles_fd = sys_perf_event_open(&cycle_attr, getpid(), -1, -1, perf_event_open_cloexec_flag());

	if (cycles_fd < 0 && errno == ENOSYS)
		die("No CONFIG_PERF_EVENTS=y kernel support configured?\n");
	else
		BUG_ON(cycles_fd < 0);
}

static u64 get_cycles(void)
{
	int ret;
	u64 clk;

	ret = read(cycles_fd, &clk, sizeof(u64));
	BUG_ON(ret != sizeof(u64));

	return clk;
}

static double timeval2double(struct timeval *ts)
{
	return (double)ts->tv_sec + (double)ts->tv_usec / (double)1000000;
}

#define print_bps(x) do {						\
		if (x < K)						\
			printf(" %14lf B/Sec\n", x);			\
		else if (x < K * K)					\
			printf(" %14lfd KB/Sec\n", x / K);		\
		else if (x < K * K * K)					\
			printf(" %14lf MB/Sec\n", x / K / K);		\
		else							\
			printf(" %14lf GB/Sec\n", x / K / K / K);	\
	} while (0)

struct bench_mem_info {
	const struct routine *routines;
	u64 (*do_cycles)(const struct routine *r, size_t len);
	double (*do_gettimeofday)(const struct routine *r, size_t len);
	const char *const *usage;
};

static void __bench_mem_routine(struct bench_mem_info *info, int r_idx, size_t len, double totallen)
{
	const struct routine *r = &info->routines[r_idx];
	double result_bps = 0.0;
	u64 result_cycles = 0;

	printf("Routine %s (%s)\n", r->name, r->desc);

	if (bench_format == BENCH_FORMAT_DEFAULT)
		printf("# Copying %s Bytes ...\n\n", length_str);

	if (use_cycles) {
		result_cycles = info->do_cycles(r, len);
	} else {
		result_bps = info->do_gettimeofday(r, len);
	}

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		if (use_cycles) {
			printf(" %14lf cycles/Byte\n", (double)result_cycles/totallen);
		} else {
			print_bps(result_bps);
		}
		break;

	case BENCH_FORMAT_SIMPLE:
		if (use_cycles) {
			printf("%lf\n", (double)result_cycles/totallen);
		} else {
			printf("%lf\n", result_bps);
		}
		break;

	default:
		BUG_ON(1);
		break;
	}
}

static int bench_mem_common(int argc, const char **argv, struct bench_mem_info *info)
{
	int i;
	size_t len;
	double totallen;

	argc = parse_options(argc, argv, options, info->usage, 0);

	if (use_cycles)
		init_cycles();

	len = (size_t)perf_atoll((char *)length_str);
	totallen = (double)len * iterations;

	if ((s64)len <= 0) {
		fprintf(stderr, "Invalid length:%s\n", length_str);
		return 1;
	}

	if (!strncmp(routine_str, "all", 3)) {
		for (i = 0; info->routines[i].name; i++)
			__bench_mem_routine(info, i, len, totallen);
		return 0;
	}

	for (i = 0; info->routines[i].name; i++) {
		if (!strcmp(info->routines[i].name, routine_str))
			break;
	}
	if (!info->routines[i].name) {
		printf("Unknown routine: %s\n", routine_str);
		printf("Available routines...\n");
		for (i = 0; info->routines[i].name; i++) {
			printf("\t%s ... %s\n",
			       info->routines[i].name, info->routines[i].desc);
		}
		return 1;
	}

	__bench_mem_routine(info, i, len, totallen);

	return 0;
}

static void memcpy_alloc_mem(void **dst, void **src, size_t length)
{
	*dst = zalloc(length);
	if (!*dst)
		die("memory allocation failed - maybe length is too large?\n");

	*src = zalloc(length);
	if (!*src)
		die("memory allocation failed - maybe length is too large?\n");

	/* Make sure to always prefault zero pages even if MMAP_THRESH is crossed: */
	memset(*src, 0, length);
}

static u64 do_memcpy_cycles(const struct routine *r, size_t len)
{
	u64 cycle_start = 0ULL, cycle_end = 0ULL;
	void *src = NULL, *dst = NULL;
	memcpy_t fn = r->fn.memcpy;
	int i;

	memcpy_alloc_mem(&dst, &src, len);

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, src, len);

	cycle_start = get_cycles();
	for (i = 0; i < iterations; ++i)
		fn(dst, src, len);
	cycle_end = get_cycles();

	free(src);
	free(dst);
	return cycle_end - cycle_start;
}

static double do_memcpy_gettimeofday(const struct routine *r, size_t len)
{
	struct timeval tv_start, tv_end, tv_diff;
	memcpy_t fn = r->fn.memcpy;
	void *src = NULL, *dst = NULL;
	int i;

	memcpy_alloc_mem(&dst, &src, len);

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, src, len);

	BUG_ON(gettimeofday(&tv_start, NULL));
	for (i = 0; i < iterations; ++i)
		fn(dst, src, len);
	BUG_ON(gettimeofday(&tv_end, NULL));

	timersub(&tv_end, &tv_start, &tv_diff);

	free(src);
	free(dst);

	return (double)(((double)len * iterations) / timeval2double(&tv_diff));
}

int bench_mem_memcpy(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct bench_mem_info info = {
		.routines		= memcpy_routines,
		.do_cycles		= do_memcpy_cycles,
		.do_gettimeofday	= do_memcpy_gettimeofday,
		.usage			= bench_mem_memcpy_usage,
	};

	return bench_mem_common(argc, argv, &info);
}

static void memset_alloc_mem(void **dst, size_t length)
{
	*dst = zalloc(length);
	if (!*dst)
		die("memory allocation failed - maybe length is too large?\n");
}

static u64 do_memset_cycles(const struct routine *r, size_t len)
{
	u64 cycle_start = 0ULL, cycle_end = 0ULL;
	memset_t fn = r->fn.memset;
	void *dst = NULL;
	int i;

	memset_alloc_mem(&dst, len);

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, -1, len);

	cycle_start = get_cycles();
	for (i = 0; i < iterations; ++i)
		fn(dst, i, len);
	cycle_end = get_cycles();

	free(dst);
	return cycle_end - cycle_start;
}

static double do_memset_gettimeofday(const struct routine *r, size_t len)
{
	struct timeval tv_start, tv_end, tv_diff;
	memset_t fn = r->fn.memset;
	void *dst = NULL;
	int i;

	memset_alloc_mem(&dst, len);

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, -1, len);

	BUG_ON(gettimeofday(&tv_start, NULL));
	for (i = 0; i < iterations; ++i)
		fn(dst, i, len);
	BUG_ON(gettimeofday(&tv_end, NULL));

	timersub(&tv_end, &tv_start, &tv_diff);

	free(dst);
	return (double)(((double)len * iterations) / timeval2double(&tv_diff));
}

static const char * const bench_mem_memset_usage[] = {
	"perf bench mem memset <options>",
	NULL
};

static const struct routine memset_routines[] = {
	{ .name		= "default",
	  .desc		= "Default memset() provided by glibc",
	  .fn.memset	= memset },

#ifdef HAVE_ARCH_X86_64_SUPPORT
# define MEMSET_FN(_fn, _name, _desc) { .name = _name, .desc = _desc, .fn.memset = _fn },
# include "mem-memset-x86-64-asm-def.h"
# undef MEMSET_FN
#endif

	{ NULL, }
};

int bench_mem_memset(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct bench_mem_info info = {
		.routines		= memset_routines,
		.do_cycles		= do_memset_cycles,
		.do_gettimeofday	= do_memset_gettimeofday,
		.usage			= bench_mem_memset_usage,
	};

	return bench_mem_common(argc, argv, &info);
}

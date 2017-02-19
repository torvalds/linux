/*
 * mem-memcpy.c
 *
 * Simple memcpy() and memset() benchmarks
 *
 * Written by Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 */

#include "debug.h"
#include "../perf.h"
#include "../util/util.h"
#include <subcmd/parse-options.h>
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
#include <linux/time64.h>

#define K 1024

static const char	*size_str	= "1MB";
static const char	*function_str	= "all";
static int		nr_loops	= 1;
static bool		use_cycles;
static int		cycles_fd;

static const struct option options[] = {
	OPT_STRING('s', "size", &size_str, "1MB",
		    "Specify the size of the memory buffers. "
		    "Available units: B, KB, MB, GB and TB (case insensitive)"),

	OPT_STRING('f', "function", &function_str, "all",
		    "Specify the function to run, \"all\" runs all available functions, \"help\" lists them"),

	OPT_INTEGER('l', "nr_loops", &nr_loops,
		    "Specify the number of loops to run. (default: 1)"),

	OPT_BOOLEAN('c', "cycles", &use_cycles,
		    "Use a cycles event instead of gettimeofday() to measure performance"),

	OPT_END()
};

typedef void *(*memcpy_t)(void *, const void *, size_t);
typedef void *(*memset_t)(void *, int, size_t);

struct function {
	const char *name;
	const char *desc;
	union {
		memcpy_t memcpy;
		memset_t memset;
	} fn;
};

static struct perf_event_attr cycle_attr = {
	.type		= PERF_TYPE_HARDWARE,
	.config		= PERF_COUNT_HW_CPU_CYCLES
};

static int init_cycles(void)
{
	cycles_fd = sys_perf_event_open(&cycle_attr, getpid(), -1, -1, perf_event_open_cloexec_flag());

	if (cycles_fd < 0 && errno == ENOSYS) {
		pr_debug("No CONFIG_PERF_EVENTS=y kernel support configured?\n");
		return -1;
	}

	return cycles_fd;
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
	return (double)ts->tv_sec + (double)ts->tv_usec / (double)USEC_PER_SEC;
}

#define print_bps(x) do {						\
		if (x < K)						\
			printf(" %14lf bytes/sec\n", x);		\
		else if (x < K * K)					\
			printf(" %14lfd KB/sec\n", x / K);		\
		else if (x < K * K * K)					\
			printf(" %14lf MB/sec\n", x / K / K);		\
		else							\
			printf(" %14lf GB/sec\n", x / K / K / K);	\
	} while (0)

struct bench_mem_info {
	const struct function *functions;
	u64 (*do_cycles)(const struct function *r, size_t size, void *src, void *dst);
	double (*do_gettimeofday)(const struct function *r, size_t size, void *src, void *dst);
	const char *const *usage;
	bool alloc_src;
};

static void __bench_mem_function(struct bench_mem_info *info, int r_idx, size_t size, double size_total)
{
	const struct function *r = &info->functions[r_idx];
	double result_bps = 0.0;
	u64 result_cycles = 0;
	void *src = NULL, *dst = zalloc(size);

	printf("# function '%s' (%s)\n", r->name, r->desc);

	if (dst == NULL)
		goto out_alloc_failed;

	if (info->alloc_src) {
		src = zalloc(size);
		if (src == NULL)
			goto out_alloc_failed;
	}

	if (bench_format == BENCH_FORMAT_DEFAULT)
		printf("# Copying %s bytes ...\n\n", size_str);

	if (use_cycles) {
		result_cycles = info->do_cycles(r, size, src, dst);
	} else {
		result_bps = info->do_gettimeofday(r, size, src, dst);
	}

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		if (use_cycles) {
			printf(" %14lf cycles/byte\n", (double)result_cycles/size_total);
		} else {
			print_bps(result_bps);
		}
		break;

	case BENCH_FORMAT_SIMPLE:
		if (use_cycles) {
			printf("%lf\n", (double)result_cycles/size_total);
		} else {
			printf("%lf\n", result_bps);
		}
		break;

	default:
		BUG_ON(1);
		break;
	}

out_free:
	free(src);
	free(dst);
	return;
out_alloc_failed:
	printf("# Memory allocation failed - maybe size (%s) is too large?\n", size_str);
	goto out_free;
}

static int bench_mem_common(int argc, const char **argv, struct bench_mem_info *info)
{
	int i;
	size_t size;
	double size_total;

	argc = parse_options(argc, argv, options, info->usage, 0);

	if (use_cycles) {
		i = init_cycles();
		if (i < 0) {
			fprintf(stderr, "Failed to open cycles counter\n");
			return i;
		}
	}

	size = (size_t)perf_atoll((char *)size_str);
	size_total = (double)size * nr_loops;

	if ((s64)size <= 0) {
		fprintf(stderr, "Invalid size:%s\n", size_str);
		return 1;
	}

	if (!strncmp(function_str, "all", 3)) {
		for (i = 0; info->functions[i].name; i++)
			__bench_mem_function(info, i, size, size_total);
		return 0;
	}

	for (i = 0; info->functions[i].name; i++) {
		if (!strcmp(info->functions[i].name, function_str))
			break;
	}
	if (!info->functions[i].name) {
		if (strcmp(function_str, "help") && strcmp(function_str, "h"))
			printf("Unknown function: %s\n", function_str);
		printf("Available functions:\n");
		for (i = 0; info->functions[i].name; i++) {
			printf("\t%s ... %s\n",
			       info->functions[i].name, info->functions[i].desc);
		}
		return 1;
	}

	__bench_mem_function(info, i, size, size_total);

	return 0;
}

static u64 do_memcpy_cycles(const struct function *r, size_t size, void *src, void *dst)
{
	u64 cycle_start = 0ULL, cycle_end = 0ULL;
	memcpy_t fn = r->fn.memcpy;
	int i;

	/* Make sure to always prefault zero pages even if MMAP_THRESH is crossed: */
	memset(src, 0, size);

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, src, size);

	cycle_start = get_cycles();
	for (i = 0; i < nr_loops; ++i)
		fn(dst, src, size);
	cycle_end = get_cycles();

	return cycle_end - cycle_start;
}

static double do_memcpy_gettimeofday(const struct function *r, size_t size, void *src, void *dst)
{
	struct timeval tv_start, tv_end, tv_diff;
	memcpy_t fn = r->fn.memcpy;
	int i;

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, src, size);

	BUG_ON(gettimeofday(&tv_start, NULL));
	for (i = 0; i < nr_loops; ++i)
		fn(dst, src, size);
	BUG_ON(gettimeofday(&tv_end, NULL));

	timersub(&tv_end, &tv_start, &tv_diff);

	return (double)(((double)size * nr_loops) / timeval2double(&tv_diff));
}

struct function memcpy_functions[] = {
	{ .name		= "default",
	  .desc		= "Default memcpy() provided by glibc",
	  .fn.memcpy	= memcpy },

#ifdef HAVE_ARCH_X86_64_SUPPORT
# define MEMCPY_FN(_fn, _name, _desc) {.name = _name, .desc = _desc, .fn.memcpy = _fn},
# include "mem-memcpy-x86-64-asm-def.h"
# undef MEMCPY_FN
#endif

	{ .name = NULL, }
};

static const char * const bench_mem_memcpy_usage[] = {
	"perf bench mem memcpy <options>",
	NULL
};

int bench_mem_memcpy(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct bench_mem_info info = {
		.functions		= memcpy_functions,
		.do_cycles		= do_memcpy_cycles,
		.do_gettimeofday	= do_memcpy_gettimeofday,
		.usage			= bench_mem_memcpy_usage,
		.alloc_src              = true,
	};

	return bench_mem_common(argc, argv, &info);
}

static u64 do_memset_cycles(const struct function *r, size_t size, void *src __maybe_unused, void *dst)
{
	u64 cycle_start = 0ULL, cycle_end = 0ULL;
	memset_t fn = r->fn.memset;
	int i;

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, -1, size);

	cycle_start = get_cycles();
	for (i = 0; i < nr_loops; ++i)
		fn(dst, i, size);
	cycle_end = get_cycles();

	return cycle_end - cycle_start;
}

static double do_memset_gettimeofday(const struct function *r, size_t size, void *src __maybe_unused, void *dst)
{
	struct timeval tv_start, tv_end, tv_diff;
	memset_t fn = r->fn.memset;
	int i;

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, -1, size);

	BUG_ON(gettimeofday(&tv_start, NULL));
	for (i = 0; i < nr_loops; ++i)
		fn(dst, i, size);
	BUG_ON(gettimeofday(&tv_end, NULL));

	timersub(&tv_end, &tv_start, &tv_diff);

	return (double)(((double)size * nr_loops) / timeval2double(&tv_diff));
}

static const char * const bench_mem_memset_usage[] = {
	"perf bench mem memset <options>",
	NULL
};

static const struct function memset_functions[] = {
	{ .name		= "default",
	  .desc		= "Default memset() provided by glibc",
	  .fn.memset	= memset },

#ifdef HAVE_ARCH_X86_64_SUPPORT
# define MEMSET_FN(_fn, _name, _desc) { .name = _name, .desc = _desc, .fn.memset = _fn },
# include "mem-memset-x86-64-asm-def.h"
# undef MEMSET_FN
#endif

	{ .name = NULL, }
};

int bench_mem_memset(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct bench_mem_info info = {
		.functions		= memset_functions,
		.do_cycles		= do_memset_cycles,
		.do_gettimeofday	= do_memset_gettimeofday,
		.usage			= bench_mem_memset_usage,
	};

	return bench_mem_common(argc, argv, &info);
}

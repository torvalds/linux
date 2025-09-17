// SPDX-License-Identifier: GPL-2.0
/*
 * mem-memcpy.c
 *
 * Simple memcpy() and memset() benchmarks
 *
 * Written by Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 */

#include "debug.h"
#include "../perf-sys.h"
#include <subcmd/parse-options.h>
#include "../util/header.h"
#include "../util/cloexec.h"
#include "../util/string2.h"
#include "bench.h"
#include "mem-memcpy-arch.h"
#include "mem-memset-arch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <errno.h>
#include <linux/time64.h>

#define K 1024

static const char	*size_str	= "1MB";
static const char	*function_str	= "all";
static unsigned int	nr_loops	= 1;
static bool		use_cycles;
static int		cycles_fd;

static const struct option options[] = {
	OPT_STRING('s', "size", &size_str, "1MB",
		    "Specify the size of the memory buffers. "
		    "Available units: B, KB, MB, GB and TB (case insensitive)"),

	OPT_STRING('f', "function", &function_str, "all",
		    "Specify the function to run, \"all\" runs all available functions, \"help\" lists them"),

	OPT_UINTEGER('l', "nr_loops", &nr_loops,
		    "Specify the number of loops to run. (default: 1)"),

	OPT_BOOLEAN('c', "cycles", &use_cycles,
		    "Use a cycles event instead of gettimeofday() to measure performance"),

	OPT_END()
};

union bench_clock {
	u64		cycles;
	struct timeval	tv;
};

struct bench_params {
	size_t		size;
	size_t		size_total;
	unsigned int	nr_loops;
};

struct bench_mem_info {
	const struct function *functions;
	int (*do_op)(const struct function *r, struct bench_params *p,
		     void *src, void *dst, union bench_clock *rt);
	const char *const *usage;
	bool alloc_src;
};

typedef bool (*mem_init_t)(struct bench_mem_info *, struct bench_params *,
			   void **, void **);
typedef void (*mem_fini_t)(struct bench_mem_info *, struct bench_params *,
			   void **, void **);
typedef void *(*memcpy_t)(void *, const void *, size_t);
typedef void *(*memset_t)(void *, int, size_t);

struct function {
	const char *name;
	const char *desc;
	struct {
		mem_init_t init;
		mem_fini_t fini;
		union {
			memcpy_t memcpy;
			memset_t memset;
		};
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

static void clock_get(union bench_clock *t)
{
	if (use_cycles)
		t->cycles = get_cycles();
	else
		BUG_ON(gettimeofday(&t->tv, NULL));
}

static union bench_clock clock_diff(union bench_clock *s, union bench_clock *e)
{
	union bench_clock t;

	if (use_cycles)
		t.cycles = e->cycles - s->cycles;
	else
		timersub(&e->tv, &s->tv, &t.tv);

	return t;
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

static void __bench_mem_function(struct bench_mem_info *info, struct bench_params *p,
				 int r_idx)
{
	const struct function *r = &info->functions[r_idx];
	double result_bps = 0.0;
	union bench_clock rt = { 0 };
	void *src = NULL, *dst = NULL;

	printf("# function '%s' (%s)\n", r->name, r->desc);

	if (r->fn.init && r->fn.init(info, p, &src, &dst))
		goto out_init_failed;

	if (bench_format == BENCH_FORMAT_DEFAULT)
		printf("# Copying %s bytes ...\n\n", size_str);

	if (info->do_op(r, p, src, dst, &rt))
		goto out_test_failed;

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		if (use_cycles) {
			printf(" %14lf cycles/byte\n", (double)rt.cycles/(double)p->size_total);
		} else {
			result_bps = (double)p->size_total/timeval2double(&rt.tv);
			print_bps(result_bps);
		}
		break;

	case BENCH_FORMAT_SIMPLE:
		if (use_cycles) {
			printf("%lf\n", (double)rt.cycles/(double)p->size_total);
		} else {
			result_bps = (double)p->size_total/timeval2double(&rt.tv);
			printf("%lf\n", result_bps);
		}
		break;

	default:
		BUG_ON(1);
		break;
	}

out_test_failed:
out_free:
	if (r->fn.fini) r->fn.fini(info, p, &src, &dst);
	return;
out_init_failed:
	printf("# Memory allocation failed - maybe size (%s) is too large?\n", size_str);
	goto out_free;
}

static int bench_mem_common(int argc, const char **argv, struct bench_mem_info *info)
{
	int i;
	struct bench_params p = { 0 };

	argc = parse_options(argc, argv, options, info->usage, 0);

	if (use_cycles) {
		i = init_cycles();
		if (i < 0) {
			fprintf(stderr, "Failed to open cycles counter\n");
			return i;
		}
	}

	p.nr_loops = nr_loops;
	p.size = (size_t)perf_atoll((char *)size_str);

	if ((s64)p.size <= 0) {
		fprintf(stderr, "Invalid size:%s\n", size_str);
		return 1;
	}
	p.size_total = p.size * p.nr_loops;

	if (!strncmp(function_str, "all", 3)) {
		for (i = 0; info->functions[i].name; i++)
			__bench_mem_function(info, &p, i);
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

	__bench_mem_function(info, &p, i);

	return 0;
}

static void memcpy_prefault(memcpy_t fn, size_t size, void *src, void *dst)
{
	/* Make sure to always prefault zero pages even if MMAP_THRESH is crossed: */
	memset(src, 0, size);

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, src, size);
}

static int do_memcpy(const struct function *r, struct bench_params *p,
		     void *src, void *dst, union bench_clock *rt)
{
	union bench_clock start, end;
	memcpy_t fn = r->fn.memcpy;

	memcpy_prefault(fn, p->size, src, dst);

	clock_get(&start);
	for (unsigned int i = 0; i < p->nr_loops; ++i)
		fn(dst, src, p->size);
	clock_get(&end);

	*rt = clock_diff(&start, &end);

	return 0;
}

static void *bench_mmap(size_t size, bool populate)
{
	void *p;
	int extra = populate ? MAP_POPULATE : 0;

	p = mmap(NULL, size, PROT_READ|PROT_WRITE,
		 extra | MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

	return p == MAP_FAILED ? NULL : p;
}

static void bench_munmap(void *p, size_t size)
{
	if (p)
		munmap(p, size);
}

static bool mem_alloc(struct bench_mem_info *info, struct bench_params *p,
		      void **src, void **dst)
{
	bool failed;

	*dst = bench_mmap(p->size, true);
	failed = *dst == NULL;

	if (info->alloc_src) {
		*src = bench_mmap(p->size, true);
		failed = failed || *src == NULL;
	}

	return failed;
}

static void mem_free(struct bench_mem_info *info __maybe_unused,
		     struct bench_params *p __maybe_unused,
		     void **src, void **dst)
{
	bench_munmap(*dst, p->size);
	bench_munmap(*src, p->size);

	*dst = *src = NULL;
}

struct function memcpy_functions[] = {
	{ .name		= "default",
	  .desc		= "Default memcpy() provided by glibc",
	  .fn.init	= mem_alloc,
	  .fn.fini	= mem_free,
	  .fn.memcpy	= memcpy },

#ifdef HAVE_ARCH_X86_64_SUPPORT
# define MEMCPY_FN(_fn, _init, _fini, _name, _desc)	\
	{.name = _name, .desc = _desc, .fn.memcpy = _fn, .fn.init = _init, .fn.fini = _fini },
# include "mem-memcpy-x86-64-asm-def.h"
# undef MEMCPY_FN
#endif

	{ .name = NULL, }
};

static const char * const bench_mem_memcpy_usage[] = {
	"perf bench mem memcpy <options>",
	NULL
};

int bench_mem_memcpy(int argc, const char **argv)
{
	struct bench_mem_info info = {
		.functions		= memcpy_functions,
		.do_op			= do_memcpy,
		.usage			= bench_mem_memcpy_usage,
		.alloc_src              = true,
	};

	return bench_mem_common(argc, argv, &info);
}

static int do_memset(const struct function *r, struct bench_params *p,
		     void *src __maybe_unused, void *dst, union bench_clock *rt)
{
	union bench_clock start, end;
	memset_t fn = r->fn.memset;

	/*
	 * We prefault the freshly allocated memory range here,
	 * to not measure page fault overhead:
	 */
	fn(dst, -1, p->size);

	clock_get(&start);
	for (unsigned int i = 0; i < p->nr_loops; ++i)
		fn(dst, i, p->size);
	clock_get(&end);

	*rt = clock_diff(&start, &end);

	return 0;
}

static const char * const bench_mem_memset_usage[] = {
	"perf bench mem memset <options>",
	NULL
};

static const struct function memset_functions[] = {
	{ .name		= "default",
	  .desc		= "Default memset() provided by glibc",
	  .fn.init	= mem_alloc,
	  .fn.fini	= mem_free,
	  .fn.memset	= memset },

#ifdef HAVE_ARCH_X86_64_SUPPORT
# define MEMSET_FN(_fn, _init, _fini, _name, _desc) \
	{.name = _name, .desc = _desc, .fn.memset = _fn, .fn.init = _init, .fn.fini = _fini },
# include "mem-memset-x86-64-asm-def.h"
# undef MEMSET_FN
#endif

	{ .name = NULL, }
};

int bench_mem_memset(int argc, const char **argv)
{
	struct bench_mem_info info = {
		.functions		= memset_functions,
		.do_op			= do_memset,
		.usage			= bench_mem_memset_usage,
	};

	return bench_mem_common(argc, argv, &info);
}

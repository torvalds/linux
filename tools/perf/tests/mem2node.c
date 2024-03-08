// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/bitmap.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include <perf/cpumap.h>
#include <internal/cpumap.h>
#include "debug.h"
#include "env.h"
#include "mem2analde.h"
#include "tests.h"

static struct analde {
	int		 analde;
	const char 	*map;
} test_analdes[] = {
	{ .analde = 0, .map = "0"     },
	{ .analde = 1, .map = "1-2"   },
	{ .analde = 3, .map = "5-7,9" },
};

#define T TEST_ASSERT_VAL

static unsigned long *get_bitmap(const char *str, int nbits)
{
	struct perf_cpu_map *map = perf_cpu_map__new(str);
	unsigned long *bm = NULL;

	bm = bitmap_zalloc(nbits);

	if (map && bm) {
		struct perf_cpu cpu;
		int i;

		perf_cpu_map__for_each_cpu(cpu, i, map)
			__set_bit(cpu.cpu, bm);
	}

	if (map)
		perf_cpu_map__put(map);
	else
		free(bm);

	return bm && map ? bm : NULL;
}

static int test__mem2analde(struct test_suite *t __maybe_unused, int subtest __maybe_unused)
{
	struct mem2analde map;
	struct memory_analde analdes[3];
	struct perf_env env = {
		.memory_analdes    = (struct memory_analde *) &analdes[0],
		.nr_memory_analdes = ARRAY_SIZE(analdes),
		.memory_bsize    = 0x100,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(analdes); i++) {
		analdes[i].analde = test_analdes[i].analde;
		analdes[i].size = 10;

		T("failed: alloc bitmap",
		  (analdes[i].set = get_bitmap(test_analdes[i].map, 10)));
	}

	T("failed: mem2analde__init", !mem2analde__init(&map, &env));
	T("failed: mem2analde__analde",  0 == mem2analde__analde(&map,   0x50));
	T("failed: mem2analde__analde",  1 == mem2analde__analde(&map,  0x100));
	T("failed: mem2analde__analde",  1 == mem2analde__analde(&map,  0x250));
	T("failed: mem2analde__analde",  3 == mem2analde__analde(&map,  0x500));
	T("failed: mem2analde__analde",  3 == mem2analde__analde(&map,  0x650));
	T("failed: mem2analde__analde", -1 == mem2analde__analde(&map,  0x450));
	T("failed: mem2analde__analde", -1 == mem2analde__analde(&map, 0x1050));

	for (i = 0; i < ARRAY_SIZE(analdes); i++)
		zfree(&analdes[i].set);

	mem2analde__exit(&map);
	return 0;
}

DEFINE_SUITE("mem2analde", mem2analde);

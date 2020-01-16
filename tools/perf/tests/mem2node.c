// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/bitmap.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include <perf/cpumap.h>
#include <internal/cpumap.h>
#include "debug.h"
#include "env.h"
#include "mem2yesde.h"
#include "tests.h"

static struct yesde {
	int		 yesde;
	const char 	*map;
} test_yesdes[] = {
	{ .yesde = 0, .map = "0"     },
	{ .yesde = 1, .map = "1-2"   },
	{ .yesde = 3, .map = "5-7,9" },
};

#define T TEST_ASSERT_VAL

static unsigned long *get_bitmap(const char *str, int nbits)
{
	struct perf_cpu_map *map = perf_cpu_map__new(str);
	unsigned long *bm = NULL;
	int i;

	bm = bitmap_alloc(nbits);

	if (map && bm) {
		for (i = 0; i < map->nr; i++) {
			set_bit(map->map[i], bm);
		}
	}

	if (map)
		perf_cpu_map__put(map);
	else
		free(bm);

	return bm && map ? bm : NULL;
}

int test__mem2yesde(struct test *t __maybe_unused, int subtest __maybe_unused)
{
	struct mem2yesde map;
	struct memory_yesde yesdes[3];
	struct perf_env env = {
		.memory_yesdes    = (struct memory_yesde *) &yesdes[0],
		.nr_memory_yesdes = ARRAY_SIZE(yesdes),
		.memory_bsize    = 0x100,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(yesdes); i++) {
		yesdes[i].yesde = test_yesdes[i].yesde;
		yesdes[i].size = 10;

		T("failed: alloc bitmap",
		  (yesdes[i].set = get_bitmap(test_yesdes[i].map, 10)));
	}

	T("failed: mem2yesde__init", !mem2yesde__init(&map, &env));
	T("failed: mem2yesde__yesde",  0 == mem2yesde__yesde(&map,   0x50));
	T("failed: mem2yesde__yesde",  1 == mem2yesde__yesde(&map,  0x100));
	T("failed: mem2yesde__yesde",  1 == mem2yesde__yesde(&map,  0x250));
	T("failed: mem2yesde__yesde",  3 == mem2yesde__yesde(&map,  0x500));
	T("failed: mem2yesde__yesde",  3 == mem2yesde__yesde(&map,  0x650));
	T("failed: mem2yesde__yesde", -1 == mem2yesde__yesde(&map,  0x450));
	T("failed: mem2yesde__yesde", -1 == mem2yesde__yesde(&map, 0x1050));

	for (i = 0; i < ARRAY_SIZE(yesdes); i++)
		zfree(&yesdes[i].set);

	mem2yesde__exit(&map);
	return 0;
}

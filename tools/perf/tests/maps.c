// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include "tests.h"
#include "map.h"
#include "maps.h"
#include "dso.h"
#include "debug.h"

struct map_def {
	const char *name;
	u64 start;
	u64 end;
};

struct check_maps_cb_args {
	struct map_def *merged;
	unsigned int i;
};

static int check_maps_cb(struct map *map, void *data)
{
	struct check_maps_cb_args *args = data;
	struct map_def *merged = &args->merged[args->i];

	if (map__start(map) != merged->start ||
	    map__end(map) != merged->end ||
	    strcmp(map__dso(map)->name, merged->name) ||
	    refcount_read(map__refcnt(map)) != 1) {
		return 1;
	}
	args->i++;
	return 0;
}

static int failed_cb(struct map *map, void *data __maybe_unused)
{
	pr_debug("\tstart: %" PRIu64 " end: %" PRIu64 " name: '%s' refcnt: %d\n",
		map__start(map),
		map__end(map),
		map__dso(map)->name,
		refcount_read(map__refcnt(map)));

	return 0;
}

static int check_maps(struct map_def *merged, unsigned int size, struct maps *maps)
{
	bool failed = false;

	if (maps__nr_maps(maps) != size) {
		pr_debug("Expected %d maps, got %d", size, maps__nr_maps(maps));
		failed = true;
	} else {
		struct check_maps_cb_args args = {
			.merged = merged,
			.i = 0,
		};
		failed = maps__for_each_map(maps, check_maps_cb, &args);
	}
	if (failed) {
		pr_debug("Expected:\n");
		for (unsigned int i = 0; i < size; i++) {
			pr_debug("\tstart: %" PRIu64 " end: %" PRIu64 " name: '%s' refcnt: 1\n",
				merged[i].start, merged[i].end, merged[i].name);
		}
		pr_debug("Got:\n");
		maps__for_each_map(maps, failed_cb, NULL);
	}
	return failed ? TEST_FAIL : TEST_OK;
}

static int test__maps__merge_in(struct test_suite *t __maybe_unused, int subtest __maybe_unused)
{
	unsigned int i;
	struct map_def bpf_progs[] = {
		{ "bpf_prog_1", 200, 300 },
		{ "bpf_prog_2", 500, 600 },
		{ "bpf_prog_3", 800, 900 },
	};
	struct map_def merged12[] = {
		{ "kcore1",     100,  200 },
		{ "bpf_prog_1", 200,  300 },
		{ "kcore1",     300,  500 },
		{ "bpf_prog_2", 500,  600 },
		{ "kcore1",     600,  800 },
		{ "bpf_prog_3", 800,  900 },
		{ "kcore1",     900, 1000 },
	};
	struct map_def merged3[] = {
		{ "kcore1",      100,  200 },
		{ "bpf_prog_1",  200,  300 },
		{ "kcore1",      300,  500 },
		{ "bpf_prog_2",  500,  600 },
		{ "kcore1",      600,  800 },
		{ "bpf_prog_3",  800,  900 },
		{ "kcore1",      900, 1000 },
		{ "kcore3",     1000, 1100 },
	};
	struct map *map_kcore1, *map_kcore2, *map_kcore3;
	int ret;
	struct maps *maps = maps__new(NULL);

	TEST_ASSERT_VAL("failed to create maps", maps);

	for (i = 0; i < ARRAY_SIZE(bpf_progs); i++) {
		struct map *map;

		map = dso__new_map(bpf_progs[i].name);
		TEST_ASSERT_VAL("failed to create map", map);

		map__set_start(map, bpf_progs[i].start);
		map__set_end(map, bpf_progs[i].end);
		TEST_ASSERT_VAL("failed to insert map", maps__insert(maps, map) == 0);
		map__put(map);
	}

	map_kcore1 = dso__new_map("kcore1");
	TEST_ASSERT_VAL("failed to create map", map_kcore1);

	map_kcore2 = dso__new_map("kcore2");
	TEST_ASSERT_VAL("failed to create map", map_kcore2);

	map_kcore3 = dso__new_map("kcore3");
	TEST_ASSERT_VAL("failed to create map", map_kcore3);

	/* kcore1 map overlaps over all bpf maps */
	map__set_start(map_kcore1, 100);
	map__set_end(map_kcore1, 1000);

	/* kcore2 map hides behind bpf_prog_2 */
	map__set_start(map_kcore2, 550);
	map__set_end(map_kcore2, 570);

	/* kcore3 map hides behind bpf_prog_3, kcore1 and adds new map */
	map__set_start(map_kcore3, 880);
	map__set_end(map_kcore3, 1100);

	ret = maps__merge_in(maps, map_kcore1);
	TEST_ASSERT_VAL("failed to merge map", !ret);

	ret = check_maps(merged12, ARRAY_SIZE(merged12), maps);
	TEST_ASSERT_VAL("merge check failed", !ret);

	ret = maps__merge_in(maps, map_kcore2);
	TEST_ASSERT_VAL("failed to merge map", !ret);

	ret = check_maps(merged12, ARRAY_SIZE(merged12), maps);
	TEST_ASSERT_VAL("merge check failed", !ret);

	ret = maps__merge_in(maps, map_kcore3);
	TEST_ASSERT_VAL("failed to merge map", !ret);

	ret = check_maps(merged3, ARRAY_SIZE(merged3), maps);
	TEST_ASSERT_VAL("merge check failed", !ret);

	maps__zput(maps);
	map__zput(map_kcore1);
	map__zput(map_kcore2);
	map__zput(map_kcore3);
	return TEST_OK;
}

DEFINE_SUITE("maps__merge_in", maps__merge_in);

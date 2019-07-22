// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/kernel.h>
#include "tests.h"
#include "map.h"
#include "map_groups.h"
#include "dso.h"
#include "debug.h"

struct map_def {
	const char *name;
	u64 start;
	u64 end;
};

static int check_maps(struct map_def *merged, unsigned int size, struct map_groups *mg)
{
	struct map *map;
	unsigned int i = 0;

	map = map_groups__first(mg);
	while (map) {
		TEST_ASSERT_VAL("wrong map start",  map->start == merged[i].start);
		TEST_ASSERT_VAL("wrong map end",    map->end == merged[i].end);
		TEST_ASSERT_VAL("wrong map name",  !strcmp(map->dso->name, merged[i].name));
		TEST_ASSERT_VAL("wrong map refcnt", refcount_read(&map->refcnt) == 2);

		i++;
		map = map_groups__next(map);

		TEST_ASSERT_VAL("less maps expected", (map && i < size) || (!map && i == size));
	}

	return TEST_OK;
}

int test__map_groups__merge_in(struct test *t __maybe_unused, int subtest __maybe_unused)
{
	struct map_groups mg;
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

	map_groups__init(&mg, NULL);

	for (i = 0; i < ARRAY_SIZE(bpf_progs); i++) {
		struct map *map;

		map = dso__new_map(bpf_progs[i].name);
		TEST_ASSERT_VAL("failed to create map", map);

		map->start = bpf_progs[i].start;
		map->end   = bpf_progs[i].end;
		map_groups__insert(&mg, map);
		map__put(map);
	}

	map_kcore1 = dso__new_map("kcore1");
	TEST_ASSERT_VAL("failed to create map", map_kcore1);

	map_kcore2 = dso__new_map("kcore2");
	TEST_ASSERT_VAL("failed to create map", map_kcore2);

	map_kcore3 = dso__new_map("kcore3");
	TEST_ASSERT_VAL("failed to create map", map_kcore3);

	/* kcore1 map overlaps over all bpf maps */
	map_kcore1->start = 100;
	map_kcore1->end   = 1000;

	/* kcore2 map hides behind bpf_prog_2 */
	map_kcore2->start = 550;
	map_kcore2->end   = 570;

	/* kcore3 map hides behind bpf_prog_3, kcore1 and adds new map */
	map_kcore3->start = 880;
	map_kcore3->end   = 1100;

	ret = map_groups__merge_in(&mg, map_kcore1);
	TEST_ASSERT_VAL("failed to merge map", !ret);

	ret = check_maps(merged12, ARRAY_SIZE(merged12), &mg);
	TEST_ASSERT_VAL("merge check failed", !ret);

	ret = map_groups__merge_in(&mg, map_kcore2);
	TEST_ASSERT_VAL("failed to merge map", !ret);

	ret = check_maps(merged12, ARRAY_SIZE(merged12), &mg);
	TEST_ASSERT_VAL("merge check failed", !ret);

	ret = map_groups__merge_in(&mg, map_kcore3);
	TEST_ASSERT_VAL("failed to merge map", !ret);

	ret = check_maps(merged3, ARRAY_SIZE(merged3), &mg);
	TEST_ASSERT_VAL("merge check failed", !ret);
	return TEST_OK;
}

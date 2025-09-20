// SPDX-License-Identifier: GPL-2.0
#include "tests.h"
#include <stdio.h>
#include "cpumap.h"
#include "event.h"
#include "util/synthetic-events.h"
#include <string.h>
#include <linux/bitops.h>
#include <internal/cpumap.h>
#include "debug.h"

struct machine;

static int process_event_mask(const struct perf_tool *tool __maybe_unused,
			 union perf_event *event,
			 struct perf_sample *sample __maybe_unused,
			 struct machine *machine __maybe_unused)
{
	struct perf_record_cpu_map *map_event = &event->cpu_map;
	struct perf_record_cpu_map_data *data;
	struct perf_cpu_map *map;
	unsigned int long_size;

	data = &map_event->data;

	TEST_ASSERT_VAL("wrong type", data->type == PERF_CPU_MAP__MASK);

	long_size = data->mask32_data.long_size;

	TEST_ASSERT_VAL("wrong long_size", long_size == 4 || long_size == 8);

	TEST_ASSERT_VAL("wrong nr",   data->mask32_data.nr == 1);

	TEST_ASSERT_VAL("wrong cpu", perf_record_cpu_map_data__test_bit(0, data));
	TEST_ASSERT_VAL("wrong cpu", !perf_record_cpu_map_data__test_bit(1, data));
	for (int i = 2; i <= 20; i++)
		TEST_ASSERT_VAL("wrong cpu", perf_record_cpu_map_data__test_bit(i, data));

	map = cpu_map__new_data(data);
	TEST_ASSERT_VAL("wrong nr",  perf_cpu_map__nr(map) == 20);

	TEST_ASSERT_VAL("wrong cpu", perf_cpu_map__cpu(map, 0).cpu == 0);
	for (int i = 2; i <= 20; i++)
		TEST_ASSERT_VAL("wrong cpu", perf_cpu_map__cpu(map, i - 1).cpu == i);

	perf_cpu_map__put(map);
	return 0;
}

static int process_event_cpus(const struct perf_tool *tool __maybe_unused,
			 union perf_event *event,
			 struct perf_sample *sample __maybe_unused,
			 struct machine *machine __maybe_unused)
{
	struct perf_record_cpu_map *map_event = &event->cpu_map;
	struct perf_record_cpu_map_data *data;
	struct perf_cpu_map *map;

	data = &map_event->data;

	TEST_ASSERT_VAL("wrong type", data->type == PERF_CPU_MAP__CPUS);

	TEST_ASSERT_VAL("wrong nr",   data->cpus_data.nr == 2);
	TEST_ASSERT_VAL("wrong cpu",  data->cpus_data.cpu[0] == 1);
	TEST_ASSERT_VAL("wrong cpu",  data->cpus_data.cpu[1] == 256);

	map = cpu_map__new_data(data);
	TEST_ASSERT_VAL("wrong nr",  perf_cpu_map__nr(map) == 2);
	TEST_ASSERT_VAL("wrong cpu", perf_cpu_map__cpu(map, 0).cpu == 1);
	TEST_ASSERT_VAL("wrong cpu", perf_cpu_map__cpu(map, 1).cpu == 256);
	TEST_ASSERT_VAL("wrong refcnt", refcount_read(perf_cpu_map__refcnt(map)) == 1);
	perf_cpu_map__put(map);
	return 0;
}

static int process_event_range_cpus(const struct perf_tool *tool __maybe_unused,
				union perf_event *event,
				struct perf_sample *sample __maybe_unused,
				struct machine *machine __maybe_unused)
{
	struct perf_record_cpu_map *map_event = &event->cpu_map;
	struct perf_record_cpu_map_data *data;
	struct perf_cpu_map *map;

	data = &map_event->data;

	TEST_ASSERT_VAL("wrong type", data->type == PERF_CPU_MAP__RANGE_CPUS);

	TEST_ASSERT_VAL("wrong any_cpu",   data->range_cpu_data.any_cpu == 0);
	TEST_ASSERT_VAL("wrong start_cpu", data->range_cpu_data.start_cpu == 1);
	TEST_ASSERT_VAL("wrong end_cpu",   data->range_cpu_data.end_cpu == 256);

	map = cpu_map__new_data(data);
	TEST_ASSERT_VAL("wrong nr",  perf_cpu_map__nr(map) == 256);
	TEST_ASSERT_VAL("wrong cpu", perf_cpu_map__cpu(map, 0).cpu == 1);
	TEST_ASSERT_VAL("wrong cpu", perf_cpu_map__max(map).cpu == 256);
	TEST_ASSERT_VAL("wrong refcnt", refcount_read(perf_cpu_map__refcnt(map)) == 1);
	perf_cpu_map__put(map);
	return 0;
}


static int test__cpu_map_synthesize(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct perf_cpu_map *cpus;

	/* This one is better stored in a mask. */
	cpus = perf_cpu_map__new("0,2-20");

	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_cpu_map(NULL, cpus, process_event_mask, NULL));

	perf_cpu_map__put(cpus);

	/* This one is better stored in cpu values. */
	cpus = perf_cpu_map__new("1,256");

	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_cpu_map(NULL, cpus, process_event_cpus, NULL));

	perf_cpu_map__put(cpus);

	/* This one is better stored as a range. */
	cpus = perf_cpu_map__new("1-256");

	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_cpu_map(NULL, cpus, process_event_range_cpus, NULL));

	perf_cpu_map__put(cpus);
	return 0;
}

static int cpu_map_print(const char *str)
{
	struct perf_cpu_map *map = perf_cpu_map__new(str);
	char buf[100];

	if (!map)
		return -1;

	cpu_map__snprint(map, buf, sizeof(buf));
	perf_cpu_map__put(map);

	return !strcmp(buf, str);
}

static int test__cpu_map_print(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	TEST_ASSERT_VAL("failed to convert map", cpu_map_print("1"));
	TEST_ASSERT_VAL("failed to convert map", cpu_map_print("1,5"));
	TEST_ASSERT_VAL("failed to convert map", cpu_map_print("1,3,5,7,9,11,13,15,17,19,21-40"));
	TEST_ASSERT_VAL("failed to convert map", cpu_map_print("2-5"));
	TEST_ASSERT_VAL("failed to convert map", cpu_map_print("1,3-6,8-10,24,35-37"));
	TEST_ASSERT_VAL("failed to convert map", cpu_map_print("1,3-6,8-10,24,35-37"));
	TEST_ASSERT_VAL("failed to convert map", cpu_map_print("1-10,12-20,22-30,32-40"));
	return 0;
}

static int __test__cpu_map_merge(const char *lhs, const char *rhs, int nr, const char *expected)
{
	struct perf_cpu_map *a = perf_cpu_map__new(lhs);
	struct perf_cpu_map *b = perf_cpu_map__new(rhs);
	char buf[100];

	perf_cpu_map__merge(&a, b);
	TEST_ASSERT_VAL("failed to merge map: bad nr", perf_cpu_map__nr(a) == nr);
	cpu_map__snprint(a, buf, sizeof(buf));
	TEST_ASSERT_VAL("failed to merge map: bad result", !strcmp(buf, expected));
	perf_cpu_map__put(b);

	/*
	 * If 'b' is a superset of 'a', 'a' points to the same map with the
	 * map 'b'. In this case, the owner 'b' has released the resource above
	 * but 'a' still keeps the ownership, the reference counter should be 1.
	 */
	TEST_ASSERT_VAL("unexpected refcnt: bad result",
			refcount_read(perf_cpu_map__refcnt(a)) == 1);

	perf_cpu_map__put(a);
	return 0;
}

static int test__cpu_map_merge(struct test_suite *test __maybe_unused,
			       int subtest __maybe_unused)
{
	int ret;

	ret = __test__cpu_map_merge("4,2,1", "4,5,7", 5, "1-2,4-5,7");
	if (ret)
		return ret;
	ret = __test__cpu_map_merge("1-8", "6-9", 9, "1-9");
	if (ret)
		return ret;
	ret = __test__cpu_map_merge("1-8,12-20", "6-9,15", 18, "1-9,12-20");
	if (ret)
		return ret;
	ret = __test__cpu_map_merge("4,2,1", "1", 3, "1-2,4");
	if (ret)
		return ret;
	ret = __test__cpu_map_merge("1", "4,2,1", 3, "1-2,4");
	if (ret)
		return ret;
	ret = __test__cpu_map_merge("1", "1", 1, "1");
	return ret;
}

static int __test__cpu_map_intersect(const char *lhs, const char *rhs, int nr, const char *expected)
{
	struct perf_cpu_map *a = perf_cpu_map__new(lhs);
	struct perf_cpu_map *b = perf_cpu_map__new(rhs);
	struct perf_cpu_map *c = perf_cpu_map__intersect(a, b);
	char buf[100];

	TEST_ASSERT_EQUAL("failed to intersect map: bad nr", perf_cpu_map__nr(c), nr);
	cpu_map__snprint(c, buf, sizeof(buf));
	TEST_ASSERT_VAL("failed to intersect map: bad result", !strcmp(buf, expected));
	perf_cpu_map__put(a);
	perf_cpu_map__put(b);
	perf_cpu_map__put(c);
	return 0;
}

static int test__cpu_map_intersect(struct test_suite *test __maybe_unused,
				   int subtest __maybe_unused)
{
	int ret;

	ret = __test__cpu_map_intersect("4,2,1", "4,5,7", 1, "4");
	if (ret)
		return ret;
	ret = __test__cpu_map_intersect("1-8", "6-9", 3, "6-8");
	if (ret)
		return ret;
	ret = __test__cpu_map_intersect("1-8,12-20", "6-9,15", 4, "6-8,15");
	if (ret)
		return ret;
	ret = __test__cpu_map_intersect("4,2,1", "1", 1, "1");
	if (ret)
		return ret;
	ret = __test__cpu_map_intersect("1", "4,2,1", 1, "1");
	if (ret)
		return ret;
	ret = __test__cpu_map_intersect("1", "1", 1, "1");
	return ret;
}

static int test__cpu_map_equal(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct perf_cpu_map *any = perf_cpu_map__new_any_cpu();
	struct perf_cpu_map *one = perf_cpu_map__new("1");
	struct perf_cpu_map *two = perf_cpu_map__new("2");
	struct perf_cpu_map *empty = perf_cpu_map__intersect(one, two);
	struct perf_cpu_map *pair = perf_cpu_map__new("1-2");
	struct perf_cpu_map *tmp;
	struct perf_cpu_map **maps[] = {&empty, &any, &one, &two, &pair};

	for (size_t i = 0; i < ARRAY_SIZE(maps); i++) {
		/* Maps equal themself. */
		TEST_ASSERT_VAL("equal", perf_cpu_map__equal(*maps[i], *maps[i]));
		for (size_t j = 0; j < ARRAY_SIZE(maps); j++) {
			/* Maps dont't equal each other. */
			if (i == j)
				continue;
			TEST_ASSERT_VAL("not equal", !perf_cpu_map__equal(*maps[i], *maps[j]));
		}
	}

	/* Maps equal made maps. */
	perf_cpu_map__merge(&two, one);
	TEST_ASSERT_VAL("pair", perf_cpu_map__equal(pair, two));

	tmp = perf_cpu_map__intersect(pair, one);
	TEST_ASSERT_VAL("one", perf_cpu_map__equal(one, tmp));
	perf_cpu_map__put(tmp);

	for (size_t i = 0; i < ARRAY_SIZE(maps); i++)
		perf_cpu_map__put(*maps[i]);

	return TEST_OK;
}

static struct test_case tests__cpu_map[] = {
	TEST_CASE("Synthesize cpu map", cpu_map_synthesize),
	TEST_CASE("Print cpu map", cpu_map_print),
	TEST_CASE("Merge cpu map", cpu_map_merge),
	TEST_CASE("Intersect cpu map", cpu_map_intersect),
	TEST_CASE("Equal cpu map", cpu_map_equal),
	{	.name = NULL, }
};

struct test_suite suite__cpu_map = {
	.desc = "CPU map",
	.test_cases = tests__cpu_map,
};

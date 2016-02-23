#include "tests.h"
#include "cpumap.h"

static int process_event_mask(struct perf_tool *tool __maybe_unused,
			 union perf_event *event,
			 struct perf_sample *sample __maybe_unused,
			 struct machine *machine __maybe_unused)
{
	struct cpu_map_event *map_event = &event->cpu_map;
	struct cpu_map_mask *mask;
	struct cpu_map_data *data;
	struct cpu_map *map;
	int i;

	data = &map_event->data;

	TEST_ASSERT_VAL("wrong type", data->type == PERF_CPU_MAP__MASK);

	mask = (struct cpu_map_mask *)data->data;

	TEST_ASSERT_VAL("wrong nr",   mask->nr == 1);

	for (i = 0; i < 20; i++) {
		TEST_ASSERT_VAL("wrong cpu", test_bit(i, mask->mask));
	}

	map = cpu_map__new_data(data);
	TEST_ASSERT_VAL("wrong nr",  map->nr == 20);

	for (i = 0; i < 20; i++) {
		TEST_ASSERT_VAL("wrong cpu", map->map[i] == i);
	}

	cpu_map__put(map);
	return 0;
}

static int process_event_cpus(struct perf_tool *tool __maybe_unused,
			 union perf_event *event,
			 struct perf_sample *sample __maybe_unused,
			 struct machine *machine __maybe_unused)
{
	struct cpu_map_event *map_event = &event->cpu_map;
	struct cpu_map_entries *cpus;
	struct cpu_map_data *data;
	struct cpu_map *map;

	data = &map_event->data;

	TEST_ASSERT_VAL("wrong type", data->type == PERF_CPU_MAP__CPUS);

	cpus = (struct cpu_map_entries *)data->data;

	TEST_ASSERT_VAL("wrong nr",   cpus->nr == 2);
	TEST_ASSERT_VAL("wrong cpu",  cpus->cpu[0] == 1);
	TEST_ASSERT_VAL("wrong cpu",  cpus->cpu[1] == 256);

	map = cpu_map__new_data(data);
	TEST_ASSERT_VAL("wrong nr",  map->nr == 2);
	TEST_ASSERT_VAL("wrong cpu", map->map[0] == 1);
	TEST_ASSERT_VAL("wrong cpu", map->map[1] == 256);
	TEST_ASSERT_VAL("wrong refcnt", atomic_read(&map->refcnt) == 1);
	cpu_map__put(map);
	return 0;
}


int test__cpu_map_synthesize(int subtest __maybe_unused)
{
	struct cpu_map *cpus;

	/* This one is better stores in mask. */
	cpus = cpu_map__new("0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19");

	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_cpu_map(NULL, cpus, process_event_mask, NULL));

	cpu_map__put(cpus);

	/* This one is better stores in cpu values. */
	cpus = cpu_map__new("1,256");

	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_cpu_map(NULL, cpus, process_event_cpus, NULL));

	cpu_map__put(cpus);
	return 0;
}

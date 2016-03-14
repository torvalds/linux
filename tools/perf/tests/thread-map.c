#include <sys/types.h>
#include <unistd.h>
#include "tests.h"
#include "thread_map.h"
#include "debug.h"

int test__thread_map(int subtest __maybe_unused)
{
	struct thread_map *map;

	/* test map on current pid */
	map = thread_map__new_by_pid(getpid());
	TEST_ASSERT_VAL("failed to alloc map", map);

	thread_map__read_comms(map);

	TEST_ASSERT_VAL("wrong nr", map->nr == 1);
	TEST_ASSERT_VAL("wrong pid",
			thread_map__pid(map, 0) == getpid());
	TEST_ASSERT_VAL("wrong comm",
			thread_map__comm(map, 0) &&
			!strcmp(thread_map__comm(map, 0), "perf"));
	TEST_ASSERT_VAL("wrong refcnt",
			atomic_read(&map->refcnt) == 1);
	thread_map__put(map);

	/* test dummy pid */
	map = thread_map__new_dummy();
	TEST_ASSERT_VAL("failed to alloc map", map);

	thread_map__read_comms(map);

	TEST_ASSERT_VAL("wrong nr", map->nr == 1);
	TEST_ASSERT_VAL("wrong pid", thread_map__pid(map, 0) == -1);
	TEST_ASSERT_VAL("wrong comm",
			thread_map__comm(map, 0) &&
			!strcmp(thread_map__comm(map, 0), "dummy"));
	TEST_ASSERT_VAL("wrong refcnt",
			atomic_read(&map->refcnt) == 1);
	thread_map__put(map);
	return 0;
}

static int process_event(struct perf_tool *tool __maybe_unused,
			 union perf_event *event,
			 struct perf_sample *sample __maybe_unused,
			 struct machine *machine __maybe_unused)
{
	struct thread_map_event *map = &event->thread_map;
	struct thread_map *threads;

	TEST_ASSERT_VAL("wrong nr",   map->nr == 1);
	TEST_ASSERT_VAL("wrong pid",  map->entries[0].pid == (u64) getpid());
	TEST_ASSERT_VAL("wrong comm", !strcmp(map->entries[0].comm, "perf"));

	threads = thread_map__new_event(&event->thread_map);
	TEST_ASSERT_VAL("failed to alloc map", threads);

	TEST_ASSERT_VAL("wrong nr", threads->nr == 1);
	TEST_ASSERT_VAL("wrong pid",
			thread_map__pid(threads, 0) == getpid());
	TEST_ASSERT_VAL("wrong comm",
			thread_map__comm(threads, 0) &&
			!strcmp(thread_map__comm(threads, 0), "perf"));
	TEST_ASSERT_VAL("wrong refcnt",
			atomic_read(&threads->refcnt) == 1);
	thread_map__put(threads);
	return 0;
}

int test__thread_map_synthesize(int subtest __maybe_unused)
{
	struct thread_map *threads;

	/* test map on current pid */
	threads = thread_map__new_by_pid(getpid());
	TEST_ASSERT_VAL("failed to alloc map", threads);

	thread_map__read_comms(threads);

	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_thread_map2(NULL, threads, process_event, NULL));

	return 0;
}

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/prctl.h>
#include "tests.h"
#include "thread_map.h"
#include "debug.h"

#define NAME	(const char *) "perf"
#define NAMEUL	(unsigned long) NAME

int test__thread_map(int subtest __maybe_unused)
{
	struct thread_map *map;

	TEST_ASSERT_VAL("failed to set process name",
			!prctl(PR_SET_NAME, NAMEUL, 0, 0, 0));

	/* test map on current pid */
	map = thread_map__new_by_pid(getpid());
	TEST_ASSERT_VAL("failed to alloc map", map);

	thread_map__read_comms(map);

	TEST_ASSERT_VAL("wrong nr", map->nr == 1);
	TEST_ASSERT_VAL("wrong pid",
			thread_map__pid(map, 0) == getpid());
	TEST_ASSERT_VAL("wrong comm",
			thread_map__comm(map, 0) &&
			!strcmp(thread_map__comm(map, 0), NAME));
	TEST_ASSERT_VAL("wrong refcnt",
			refcount_read(&map->refcnt) == 1);
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
			refcount_read(&map->refcnt) == 1);
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
	TEST_ASSERT_VAL("wrong comm", !strcmp(map->entries[0].comm, NAME));

	threads = thread_map__new_event(&event->thread_map);
	TEST_ASSERT_VAL("failed to alloc map", threads);

	TEST_ASSERT_VAL("wrong nr", threads->nr == 1);
	TEST_ASSERT_VAL("wrong pid",
			thread_map__pid(threads, 0) == getpid());
	TEST_ASSERT_VAL("wrong comm",
			thread_map__comm(threads, 0) &&
			!strcmp(thread_map__comm(threads, 0), NAME));
	TEST_ASSERT_VAL("wrong refcnt",
			refcount_read(&threads->refcnt) == 1);
	thread_map__put(threads);
	return 0;
}

int test__thread_map_synthesize(int subtest __maybe_unused)
{
	struct thread_map *threads;

	TEST_ASSERT_VAL("failed to set process name",
			!prctl(PR_SET_NAME, NAMEUL, 0, 0, 0));

	/* test map on current pid */
	threads = thread_map__new_by_pid(getpid());
	TEST_ASSERT_VAL("failed to alloc map", threads);

	thread_map__read_comms(threads);

	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_thread_map2(NULL, threads, process_event, NULL));

	return 0;
}

int test__thread_map_remove(int subtest __maybe_unused)
{
	struct thread_map *threads;
	char *str;
	int i;

	TEST_ASSERT_VAL("failed to allocate map string",
			asprintf(&str, "%d,%d", getpid(), getppid()) >= 0);

	threads = thread_map__new_str(str, NULL, 0);

	TEST_ASSERT_VAL("failed to allocate thread_map",
			threads);

	if (verbose > 0)
		thread_map__fprintf(threads, stderr);

	TEST_ASSERT_VAL("failed to remove thread",
			!thread_map__remove(threads, 0));

	TEST_ASSERT_VAL("thread_map count != 1", threads->nr == 1);

	if (verbose > 0)
		thread_map__fprintf(threads, stderr);

	TEST_ASSERT_VAL("failed to remove thread",
			!thread_map__remove(threads, 0));

	TEST_ASSERT_VAL("thread_map count != 0", threads->nr == 0);

	if (verbose > 0)
		thread_map__fprintf(threads, stderr);

	TEST_ASSERT_VAL("failed to not remove thread",
			thread_map__remove(threads, 0));

	for (i = 0; i < threads->nr; i++)
		free(threads->map[i].comm);

	free(threads);
	return 0;
}

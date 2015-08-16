#include <sys/types.h>
#include <unistd.h>
#include "tests.h"
#include "thread_map.h"
#include "debug.h"

int test__thread_map(void)
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
	thread_map__put(map);
	return 0;
}

// SPDX-License-Identifier: GPL-2.0
#include <perf/threadmap.h>
#include <stdlib.h>
#include <linux/refcount.h>
#include <internal/threadmap.h>
#include <string.h>
#include <asm/bug.h>
#include <stdio.h>

static void perf_thread_map__reset(struct perf_thread_map *map, int start, int nr)
{
	size_t size = (nr - start) * sizeof(map->map[0]);

	memset(&map->map[start], 0, size);
	map->err_thread = -1;
}

struct perf_thread_map *perf_thread_map__realloc(struct perf_thread_map *map, int nr)
{
	size_t size = sizeof(*map) + sizeof(map->map[0]) * nr;
	int start = map ? map->nr : 0;

	map = realloc(map, size);
	/*
	 * We only realloc to add more items, let's reset new items.
	 */
	if (map)
		perf_thread_map__reset(map, start, nr);

	return map;
}

#define thread_map__alloc(__nr) perf_thread_map__realloc(NULL, __nr)

void perf_thread_map__set_pid(struct perf_thread_map *map, int idx, pid_t pid)
{
	map->map[idx].pid = pid;
}

char *perf_thread_map__comm(struct perf_thread_map *map, int idx)
{
	return map->map[idx].comm;
}

struct perf_thread_map *perf_thread_map__new_array(int nr_threads, pid_t *array)
{
	struct perf_thread_map *threads = thread_map__alloc(nr_threads);
	int i;

	if (!threads)
		return NULL;

	for (i = 0; i < nr_threads; i++)
		perf_thread_map__set_pid(threads, i, array ? array[i] : -1);

	threads->nr = nr_threads;
	refcount_set(&threads->refcnt, 1);

	return threads;
}

struct perf_thread_map *perf_thread_map__new_dummy(void)
{
	return perf_thread_map__new_array(1, NULL);
}

static void perf_thread_map__delete(struct perf_thread_map *threads)
{
	if (threads) {
		int i;

		WARN_ONCE(refcount_read(&threads->refcnt) != 0,
			  "thread map refcnt unbalanced\n");
		for (i = 0; i < threads->nr; i++)
			free(perf_thread_map__comm(threads, i));
		free(threads);
	}
}

struct perf_thread_map *perf_thread_map__get(struct perf_thread_map *map)
{
	if (map)
		refcount_inc(&map->refcnt);
	return map;
}

void perf_thread_map__put(struct perf_thread_map *map)
{
	if (map && refcount_dec_and_test(&map->refcnt))
		perf_thread_map__delete(map);
}

int perf_thread_map__nr(struct perf_thread_map *threads)
{
	return threads ? threads->nr : 1;
}

pid_t perf_thread_map__pid(struct perf_thread_map *map, int idx)
{
	return map->map[idx].pid;
}

// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include "debug.h"
#include "tests.h"
#include "machine.h"
#include "thread_map.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"

#define THREADS 4

static int go_away;

struct thread_data {
	pthread_t	pt;
	pid_t		tid;
	void		*map;
	int		ready[2];
};

static struct thread_data threads[THREADS];

static int thread_init(struct thread_data *td)
{
	void *map;

	map = mmap(NULL, page_size,
		   PROT_READ|PROT_WRITE|PROT_EXEC,
		   MAP_SHARED|MAP_ANONYMOUS, -1, 0);

	if (map == MAP_FAILED) {
		perror("mmap failed");
		return -1;
	}

	td->map = map;
	td->tid = syscall(SYS_gettid);

	pr_debug("tid = %d, map = %p\n", td->tid, map);
	return 0;
}

static void *thread_fn(void *arg)
{
	struct thread_data *td = arg;
	ssize_t ret;
	int go;

	if (thread_init(td))
		return NULL;

	/* Signal thread_create thread is initialized. */
	ret = write(td->ready[1], &go, sizeof(int));
	if (ret != sizeof(int)) {
		pr_err("failed to notify\n");
		return NULL;
	}

	while (!go_away) {
		/* Waiting for main thread to kill us. */
		usleep(100);
	}

	munmap(td->map, page_size);
	return NULL;
}

static int thread_create(int i)
{
	struct thread_data *td = &threads[i];
	int err, go;

	if (pipe(td->ready))
		return -1;

	err = pthread_create(&td->pt, NULL, thread_fn, td);
	if (!err) {
		/* Wait for thread initialization. */
		ssize_t ret = read(td->ready[0], &go, sizeof(int));
		err = ret != sizeof(int);
	}

	close(td->ready[0]);
	close(td->ready[1]);
	return err;
}

static int threads_create(void)
{
	struct thread_data *td0 = &threads[0];
	int i, err = 0;

	go_away = 0;

	/* 0 is main thread */
	if (thread_init(td0))
		return -1;

	for (i = 1; !err && i < THREADS; i++)
		err = thread_create(i);

	return err;
}

static int threads_destroy(void)
{
	struct thread_data *td0 = &threads[0];
	int i, err = 0;

	/* cleanup the main thread */
	munmap(td0->map, page_size);

	go_away = 1;

	for (i = 1; !err && i < THREADS; i++)
		err = pthread_join(threads[i].pt, NULL);

	return err;
}

typedef int (*synth_cb)(struct machine *machine);

static int synth_all(struct machine *machine)
{
	return perf_event__synthesize_threads(NULL,
					      perf_event__process,
					      machine, 0, 500, 1);
}

static int synth_process(struct machine *machine)
{
	struct thread_map *map;
	int err;

	map = thread_map__new_by_pid(getpid());

	err = perf_event__synthesize_thread_map(NULL, map,
						perf_event__process,
						machine, 0, 500);

	thread_map__put(map);
	return err;
}

static int mmap_events(synth_cb synth)
{
	struct machine *machine;
	int err, i;

	/*
	 * The threads_create will not return before all threads
	 * are spawned and all created memory map.
	 *
	 * They will loop until threads_destroy is called, so we
	 * can safely run synthesizing function.
	 */
	TEST_ASSERT_VAL("failed to create threads", !threads_create());

	machine = machine__new_host();

	dump_trace = verbose > 1 ? 1 : 0;

	err = synth(machine);

	dump_trace = 0;

	TEST_ASSERT_VAL("failed to destroy threads", !threads_destroy());
	TEST_ASSERT_VAL("failed to synthesize maps", !err);

	/*
	 * All data is synthesized, try to find map for each
	 * thread object.
	 */
	for (i = 0; i < THREADS; i++) {
		struct thread_data *td = &threads[i];
		struct addr_location al;
		struct thread *thread;

		thread = machine__findnew_thread(machine, getpid(), td->tid);

		pr_debug("looking for map %p\n", td->map);

		thread__find_map(thread, PERF_RECORD_MISC_USER,
				 (unsigned long) (td->map + 1), &al);

		thread__put(thread);

		if (!al.map) {
			pr_debug("failed, couldn't find map\n");
			err = -1;
			break;
		}

		pr_debug("map %p, addr %" PRIx64 "\n", al.map, al.map->start);
	}

	machine__delete_threads(machine);
	machine__delete(machine);
	return err;
}

/*
 * This test creates 'THREADS' number of threads (including
 * main thread) and each thread creates memory map.
 *
 * When threads are created, we synthesize them with both
 * (separate tests):
 *   perf_event__synthesize_thread_map (process based)
 *   perf_event__synthesize_threads    (global)
 *
 * We test we can find all memory maps via:
 *   thread__find_map
 *
 * by using all thread objects.
 */
int test__mmap_thread_lookup(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	/* perf_event__synthesize_threads synthesize */
	TEST_ASSERT_VAL("failed with sythesizing all",
			!mmap_events(synth_all));

	/* perf_event__synthesize_thread_map synthesize */
	TEST_ASSERT_VAL("failed with sythesizing process",
			!mmap_events(synth_process));

	return 0;
}

/*
 *  sync stress test: parallelism
 *  Copyright 2015-2016 Collabora Ltd.
 *
 *  Based on the implementation from the Android Open Source Project,
 *
 *  Copyright 2012 Google, Inc
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *  OTHER DEALINGS IN THE SOFTWARE.
 */

#include <pthread.h>

#include "sync.h"
#include "sw_sync.h"
#include "synctest.h"

static struct {
	int iterations;
	int timeline;
	int counter;
} test_data_two_threads;

static int test_stress_two_threads_shared_timeline_thread(void *d)
{
	int thread_id = (long)d;
	int timeline = test_data_two_threads.timeline;
	int iterations = test_data_two_threads.iterations;
	int fence, valid, ret, i;

	for (i = 0; i < iterations; i++) {
		fence = sw_sync_fence_create(timeline, "fence",
					     i * 2 + thread_id);
		valid = sw_sync_fence_is_valid(fence);
		ASSERT(valid, "Failure allocating fence\n");

		/* Wait on the prior thread to complete */
		ret = sync_wait(fence, -1);
		ASSERT(ret > 0, "Problem occurred on prior thread\n");

		/*
		 * Confirm the previous thread's writes are visible
		 * and then increment
		 */
		ASSERT(test_data_two_threads.counter == i * 2 + thread_id,
		       "Counter got damaged!\n");
		test_data_two_threads.counter++;

		/* Kick off the other thread */
		ret = sw_sync_timeline_inc(timeline, 1);
		ASSERT(ret == 0, "Advancing timeline failed\n");

		sw_sync_fence_destroy(fence);
	}

	return 0;
}

int test_stress_two_threads_shared_timeline(void)
{
	pthread_t a, b;
	int valid;
	int timeline = sw_sync_timeline_create();

	valid = sw_sync_timeline_is_valid(timeline);
	ASSERT(valid, "Failure allocating timeline\n");

	test_data_two_threads.iterations = 1 << 16;
	test_data_two_threads.counter = 0;
	test_data_two_threads.timeline = timeline;

	/*
	 * Use a single timeline to synchronize two threads
	 * hammmering on the same counter.
	 */

	pthread_create(&a, NULL, (void *(*)(void *))
		       test_stress_two_threads_shared_timeline_thread,
		       (void *)0);
	pthread_create(&b, NULL, (void *(*)(void *))
		       test_stress_two_threads_shared_timeline_thread,
		       (void *)1);

	pthread_join(a, NULL);
	pthread_join(b, NULL);

	/* make sure the threads did not trample on one another */
	ASSERT(test_data_two_threads.counter ==
	       test_data_two_threads.iterations * 2,
	       "Counter has unexpected value\n");

	sw_sync_timeline_destroy(timeline);

	return 0;
}

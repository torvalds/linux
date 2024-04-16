/*
 *  sync stress test: producer/consumer
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

/* IMPORTANT NOTE: if you see this test failing on your system, it may be
 * due to a shortage of file descriptors. Please ensure your system has
 * a sensible limit for this test to finish correctly.
 */

/* Returns 1 on error, 0 on success */
static int busy_wait_on_fence(int fence)
{
	int error, active;

	do {
		error = sync_fence_count_with_status(fence, FENCE_STATUS_ERROR);
		ASSERT(error == 0, "Error occurred on fence\n");
		active = sync_fence_count_with_status(fence,
						      FENCE_STATUS_ACTIVE);
	} while (active);

	return 0;
}

static struct {
	int iterations;
	int threads;
	int counter;
	int consumer_timeline;
	int *producer_timelines;
	pthread_mutex_t lock;
} test_data_mpsc;

static int mpsc_producer_thread(void *d)
{
	int id = (long)d;
	int fence, valid, i;
	int *producer_timelines = test_data_mpsc.producer_timelines;
	int consumer_timeline = test_data_mpsc.consumer_timeline;
	int iterations = test_data_mpsc.iterations;

	for (i = 0; i < iterations; i++) {
		fence = sw_sync_fence_create(consumer_timeline, "fence", i);
		valid = sw_sync_fence_is_valid(fence);
		ASSERT(valid, "Failure creating fence\n");

		/*
		 * Wait for the consumer to finish. Use alternate
		 * means of waiting on the fence
		 */

		if ((iterations + id) % 8 != 0) {
			ASSERT(sync_wait(fence, -1) > 0,
			       "Failure waiting on fence\n");
		} else {
			ASSERT(busy_wait_on_fence(fence) == 0,
			       "Failure waiting on fence\n");
		}

		/*
		 * Every producer increments the counter, the consumer
		 * checks and erases it
		 */
		pthread_mutex_lock(&test_data_mpsc.lock);
		test_data_mpsc.counter++;
		pthread_mutex_unlock(&test_data_mpsc.lock);

		ASSERT(sw_sync_timeline_inc(producer_timelines[id], 1) == 0,
		       "Error advancing producer timeline\n");

		sw_sync_fence_destroy(fence);
	}

	return 0;
}

static int mpcs_consumer_thread(void)
{
	int fence, merged, tmp, valid, it, i;
	int *producer_timelines = test_data_mpsc.producer_timelines;
	int consumer_timeline = test_data_mpsc.consumer_timeline;
	int iterations = test_data_mpsc.iterations;
	int n = test_data_mpsc.threads;

	for (it = 1; it <= iterations; it++) {
		fence = sw_sync_fence_create(producer_timelines[0], "name", it);
		for (i = 1; i < n; i++) {
			tmp = sw_sync_fence_create(producer_timelines[i],
						   "name", it);
			merged = sync_merge("name", tmp, fence);
			sw_sync_fence_destroy(tmp);
			sw_sync_fence_destroy(fence);
			fence = merged;
		}

		valid = sw_sync_fence_is_valid(fence);
		ASSERT(valid, "Failure merging fences\n");

		/*
		 * Make sure we see an increment from every producer thread.
		 * Vary the means by which we wait.
		 */
		if (iterations % 8 != 0) {
			ASSERT(sync_wait(fence, -1) > 0,
			       "Producers did not increment as expected\n");
		} else {
			ASSERT(busy_wait_on_fence(fence) == 0,
			       "Producers did not increment as expected\n");
		}

		ASSERT(test_data_mpsc.counter == n * it,
		       "Counter value mismatch!\n");

		/* Release the producer threads */
		ASSERT(sw_sync_timeline_inc(consumer_timeline, 1) == 0,
		       "Failure releasing producer threads\n");

		sw_sync_fence_destroy(fence);
	}

	return 0;
}

int test_consumer_stress_multi_producer_single_consumer(void)
{
	int iterations = 1 << 12;
	int n = 5;
	long i, ret;
	int producer_timelines[n];
	int consumer_timeline;
	pthread_t threads[n];

	consumer_timeline = sw_sync_timeline_create();
	for (i = 0; i < n; i++)
		producer_timelines[i] = sw_sync_timeline_create();

	test_data_mpsc.producer_timelines = producer_timelines;
	test_data_mpsc.consumer_timeline = consumer_timeline;
	test_data_mpsc.iterations = iterations;
	test_data_mpsc.threads = n;
	test_data_mpsc.counter = 0;
	pthread_mutex_init(&test_data_mpsc.lock, NULL);

	for (i = 0; i < n; i++) {
		pthread_create(&threads[i], NULL, (void * (*)(void *))
			       mpsc_producer_thread, (void *)i);
	}

	/* Consumer thread runs here */
	ret = mpcs_consumer_thread();

	for (i = 0; i < n; i++)
		pthread_join(threads[i], NULL);

	return ret;
}

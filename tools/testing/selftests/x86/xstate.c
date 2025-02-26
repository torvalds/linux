// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <pthread.h>
#include <stdbool.h>

#include "helpers.h"
#include "xstate.h"

static struct xstate_info xstate;

struct futex_info {
	unsigned int iterations;
	struct futex_info *next;
	pthread_mutex_t mutex;
	pthread_t thread;
	bool valid;
	int nr;
};

static inline void load_rand_xstate(struct xstate_info *xstate, struct xsave_buffer *xbuf)
{
	clear_xstate_header(xbuf);
	set_xstatebv(xbuf, xstate->mask);
	set_rand_data(xstate, xbuf);
	xrstor(xbuf, xstate->mask);
}

static inline bool validate_xstate_same(struct xsave_buffer *xbuf1, struct xsave_buffer *xbuf2)
{
	int ret;

	ret = memcmp(&xbuf1->bytes[xstate.xbuf_offset],
		     &xbuf2->bytes[xstate.xbuf_offset],
		     xstate.size);
	return ret == 0;
}

static inline bool validate_xregs_same(struct xsave_buffer *xbuf1)
{
	struct xsave_buffer *xbuf2;
	bool ret;

	xbuf2 = alloc_xbuf();
	if (!xbuf2)
		ksft_exit_fail_msg("failed to allocate XSAVE buffer\n");

	xsave(xbuf2, xstate.mask);
	ret = validate_xstate_same(xbuf1, xbuf2);

	free(xbuf2);
	return ret;
}

/* Context switching test */

static void *check_xstate(void *info)
{
	struct futex_info *finfo = (struct futex_info *)info;
	struct xsave_buffer *xbuf;
	int i;

	xbuf = alloc_xbuf();
	if (!xbuf)
		ksft_exit_fail_msg("unable to allocate XSAVE buffer\n");

	/*
	 * Load random data into 'xbuf' and then restore it to the xstate
	 * registers.
	 */
	load_rand_xstate(&xstate, xbuf);
	finfo->valid = true;

	for (i = 0; i < finfo->iterations; i++) {
		pthread_mutex_lock(&finfo->mutex);

		/*
		 * Ensure the register values have not diverged from the
		 * record. Then reload a new random value.  If it failed
		 * ever before, skip it.
		 */
		if (finfo->valid) {
			finfo->valid = validate_xregs_same(xbuf);
			load_rand_xstate(&xstate, xbuf);
		}

		/*
		 * The last thread's last unlock will be for thread 0's
		 * mutex. However, thread 0 will have already exited the
		 * loop and the mutex will already be unlocked.
		 *
		 * Because this is not an ERRORCHECK mutex, that
		 * inconsistency will be silently ignored.
		 */
		pthread_mutex_unlock(&finfo->next->mutex);
	}

	free(xbuf);
	return finfo;
}

static void create_threads(uint32_t num_threads, uint32_t iterations, struct futex_info *finfo)
{
	int i;

	for (i = 0; i < num_threads; i++) {
		int next_nr;

		finfo[i].nr = i;
		finfo[i].iterations = iterations;

		/*
		 * Thread 'i' will wait on this mutex to be unlocked.
		 * Lock it immediately after initialization:
		 */
		pthread_mutex_init(&finfo[i].mutex, NULL);
		pthread_mutex_lock(&finfo[i].mutex);

		next_nr = (i + 1) % num_threads;
		finfo[i].next = &finfo[next_nr];

		if (pthread_create(&finfo[i].thread, NULL, check_xstate, &finfo[i]))
			ksft_exit_fail_msg("pthread_create() failed\n");
	}
}

static bool checkout_threads(uint32_t num_threads, struct futex_info *finfo)
{
	void *thread_retval;
	bool valid = true;
	int err, i;

	for (i = 0; i < num_threads; i++) {
		err = pthread_join(finfo[i].thread, &thread_retval);
		if (err)
			ksft_exit_fail_msg("pthread_join() failed for thread %d err: %d\n", i, err);

		if (thread_retval != &finfo[i]) {
			ksft_exit_fail_msg("unexpected thread retval for thread %d: %p\n",
					   i, thread_retval);
		}

		valid &= finfo[i].valid;
	}

	return valid;
}

static void affinitize_cpu0(void)
{
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);

	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
		ksft_exit_fail_msg("sched_setaffinity to CPU 0 failed\n");
}

void test_context_switch(uint32_t feature_num, uint32_t num_threads, uint32_t iterations)
{
	struct futex_info *finfo;

	/* Affinitize to one CPU to force context switches */
	affinitize_cpu0();

	xstate = get_xstate_info(feature_num);

	printf("[RUN]\t%s: check context switches, %d iterations, %d threads.\n",
	       xstate.name, iterations, num_threads);

	finfo = malloc(sizeof(*finfo) * num_threads);
	if (!finfo)
		ksft_exit_fail_msg("unable allocate memory\n");

	create_threads(num_threads, iterations, finfo);

	/*
	 * This thread wakes up thread 0
	 * Thread 0 will wake up 1
	 * Thread 1 will wake up 2
	 * ...
	 * The last thread will wake up 0
	 *
	 * This will repeat for the configured
	 * number of iterations.
	 */
	pthread_mutex_unlock(&finfo[0].mutex);

	/* Wait for all the threads to finish: */
	if (checkout_threads(num_threads, finfo))
		printf("[OK]\tNo incorrect case was found.\n");
	else
		printf("[FAIL]\tFailed with context switching test.\n");

	free(finfo);
}

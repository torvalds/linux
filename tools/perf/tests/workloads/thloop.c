/* SPDX-License-Identifier: GPL-2.0 */
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <linux/compiler.h>
#include "../tests.h"

static volatile sig_atomic_t done;

/* We want to check this symbol in perf report */
noinline void test_loop(void);

static void sighandler(int sig __maybe_unused)
{
	done = 1;
}

noinline void test_loop(void)
{
	while (!done);
}

static void *thfunc(void *arg)
{
	void (*loop_fn)(void) = arg;

	loop_fn();
	return NULL;
}

static int thloop(int argc, const char **argv)
{
	int nt = 2, sec = 1, err = 1;
	pthread_t *thread_list = NULL;

	if (argc > 0)
		sec = atoi(argv[0]);

	if (sec <= 0) {
		fprintf(stderr, "Error: seconds (%d) must be >= 1\n", sec);
		return 1;
	}

	if (argc > 1)
		nt = atoi(argv[1]);

	if (nt <= 0) {
		fprintf(stderr, "Error: thread count (%d) must be >= 1\n", nt);
		return 1;
	}

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);

	thread_list = calloc(nt, sizeof(pthread_t));
	if (thread_list == NULL) {
		fprintf(stderr, "Error: malloc failed for %d threads\n", nt);
		goto out;
	}
	for (int i = 1; i < nt; i++) {
		int ret = pthread_create(&thread_list[i], NULL, thfunc, test_loop);

		if (ret) {
			fprintf(stderr, "Error: failed to create thread %d\n", i);
			done = 1; // Ensure started threads terminate.
			goto out;
		}
	}
	alarm(sec);
	test_loop();
	err = 0;
out:
	for (int i = 1; i < nt; i++) {
		if (thread_list && thread_list[i])
			pthread_join(thread_list[i], /*retval=*/NULL);
	}
	free(thread_list);
	return err;
}

DEFINE_WORKLOAD(thloop);

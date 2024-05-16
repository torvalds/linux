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
	int sec = 1;
	pthread_t th;

	if (argc > 0)
		sec = atoi(argv[0]);

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);
	alarm(sec);

	pthread_create(&th, NULL, thfunc, test_loop);
	test_loop();
	pthread_join(th, NULL);

	return 0;
}

DEFINE_WORKLOAD(thloop);

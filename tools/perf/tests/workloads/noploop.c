/* SPDX-License-Identifier: GPL-2.0 */
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <linux/compiler.h>
#include "../tests.h"

static volatile sig_atomic_t done;

static void sighandler(int sig __maybe_unused)
{
	done = 1;
}

static int noploop(int argc, const char **argv)
{
	int sec = 1;

	pthread_setname_np(pthread_self(), "perf-noploop");
	if (argc > 0)
		sec = atoi(argv[0]);

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);
	alarm(sec);

	while (!done)
		continue;

	return 0;
}

DEFINE_WORKLOAD(noploop);

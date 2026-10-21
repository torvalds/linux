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
	double sec = 1.0;

	pthread_setname_np(pthread_self(), "perf-noploop");
	if (argc > 0)
		sec = atof(argv[0]);

	if (!(sec > 0.0)) {
		fprintf(stderr, "Error: seconds (%f) must be > 0\n", sec);
		return 1;
	}

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);

	if (sec < 1.0) {
		useconds_t usecs = (useconds_t)(sec * 1000000.0);

		ualarm(usecs > 0 ? usecs : 1, 0);
	} else
		alarm((unsigned int)sec);

	while (!done)
		continue;

	return 0;
}

DEFINE_WORKLOAD(noploop);

// SPDX-License-Identifier: GPL-2.0
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <linux/compiler.h>
#include "../tests.h"

extern void test_rs(uint count);

static volatile sig_atomic_t done;

static void sighandler(int sig __maybe_unused)
{
	done = 1;
}

static int code_with_type(int argc, const char **argv)
{
	int sec = 1, num_loops = 100;

	pthread_setname_np(pthread_self(), "perf-code-with-type");
	if (argc > 0)
		sec = atoi(argv[0]);

	if (argc > 1)
		num_loops = atoi(argv[1]);

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);
	alarm(sec);

	/*
	 * Rust doesn't have signal management in the standard library. To
	 * not deal with any external crates, offload signal handling to the
	 * outside code.
	 */
	while (!done) {
		test_rs(num_loops);
		continue;
	}

	return 0;
}

DEFINE_WORKLOAD(code_with_type);

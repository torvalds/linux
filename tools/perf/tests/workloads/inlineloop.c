// SPDX-License-Identifier: GPL-2.0
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <linux/compiler.h>
#include "../tests.h"

static volatile int a;
static volatile sig_atomic_t done;

static void sighandler(int sig __maybe_unused)
{
	done = 1;
}

static inline void __attribute__((always_inline)) leaf(int b)
{
again:
	a += b;
	if (!done)
		goto again;
}

static inline void __attribute__((always_inline)) middle(int b)
{
	leaf(b);
}

static noinline void parent(int b)
{
	middle(b);
}

static int inlineloop(int argc, const char **argv)
{
	int sec = 1;

	pthread_setname_np(pthread_self(), "perf-inlineloop");
	if (argc > 0)
		sec = atoi(argv[0]);

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);
	alarm(sec);

	parent(sec);

	return 0;
}

DEFINE_WORKLOAD(inlineloop);

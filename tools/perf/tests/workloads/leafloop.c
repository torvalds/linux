/* SPDX-License-Identifier: GPL-2.0 */
#include <signal.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include <unistd.h>
#include "../tests.h"

/* We want to check these symbols in perf script */
noinline void leaf(volatile int b);
noinline void parent(volatile int b);

static volatile int a;
static volatile sig_atomic_t done;

static void sighandler(int sig __maybe_unused)
{
	done = 1;
}

noinline void leaf(volatile int b)
{
	while (!done)
		a += b;
}

noinline void parent(volatile int b)
{
	leaf(b);
}

static int leafloop(int argc, const char **argv)
{
	int sec = 1;

	if (argc > 0)
		sec = atoi(argv[0]);

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);
	alarm(sec);

	parent(sec);
	return 0;
}

DEFINE_WORKLOAD(leafloop);

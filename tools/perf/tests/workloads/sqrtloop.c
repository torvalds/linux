/* SPDX-License-Identifier: GPL-2.0 */
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/compiler.h>
#include <sys/wait.h>
#include "../tests.h"

static volatile sig_atomic_t done;

static void sighandler(int sig __maybe_unused)
{
	done = 1;
}

static int __sqrtloop(int sec)
{
	signal(SIGALRM, sighandler);
	alarm(sec);

	while (!done)
		(void)sqrt(rand());
	return 0;
}

static int sqrtloop(int argc, const char **argv)
{
	int sec = 1;

	if (argc > 0)
		sec = atoi(argv[0]);

	switch (fork()) {
	case 0:
		return __sqrtloop(sec);
	case -1:
		return -1;
	default:
		wait(NULL);
	}
	return 0;
}

DEFINE_WORKLOAD(sqrtloop);

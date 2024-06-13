// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for prctl(PR_GET_TSC, ...) / prctl(PR_SET_TSC, ...)
 *
 * Tests if the control register is updated correctly
 * when set with prctl()
 *
 * Warning: this test will cause a very high load for a few seconds
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>
#include <wait.h>


#include <sys/prctl.h>
#include <linux/prctl.h>

/* Get/set the process' ability to use the timestamp counter instruction */
#ifndef PR_GET_TSC
#define PR_GET_TSC 25
#define PR_SET_TSC 26
# define PR_TSC_ENABLE		1   /* allow the use of the timestamp counter */
# define PR_TSC_SIGSEGV		2   /* throw a SIGSEGV instead of reading the TSC */
#endif

/* snippet from wikipedia :-) */

static uint64_t rdtsc(void)
{
uint32_t lo, hi;
/* We cannot use "=A", since this would use %rax on x86_64 */
__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
return (uint64_t)hi << 32 | lo;
}

int should_segv = 0;

static void sigsegv_cb(int sig)
{
	if (!should_segv)
	{
		fprintf(stderr, "FATAL ERROR, rdtsc() failed while enabled\n");
		exit(0);
	}
	if (prctl(PR_SET_TSC, PR_TSC_ENABLE) < 0)
	{
		perror("prctl");
		exit(0);
	}
	should_segv = 0;

	rdtsc();
}

static void task(void)
{
	signal(SIGSEGV, sigsegv_cb);
	alarm(10);
	for(;;)
	{
		rdtsc();
		if (should_segv)
		{
			fprintf(stderr, "FATAL ERROR, rdtsc() succeeded while disabled\n");
			exit(0);
		}
		if (prctl(PR_SET_TSC, PR_TSC_SIGSEGV) < 0)
		{
			perror("prctl");
			exit(0);
		}
		should_segv = 1;
	}
}


int main(void)
{
	int n_tasks = 100, i;

	fprintf(stderr, "[No further output means we're allright]\n");

	for (i=0; i<n_tasks; i++)
		if (fork() == 0)
			task();

	for (i=0; i<n_tasks; i++)
		wait(NULL);

	exit(0);
}


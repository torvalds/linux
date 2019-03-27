/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996 - 2000
 *	HD Associates, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by HD Associates, Inc
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

volatile int ticked;
#define CAN_USE_ALARMS

#ifdef CAN_USE_ALARMS
void tick(int arg)
{
	ticked = 1;
}
#endif

/* Fifo: Verify that fifo and round-robin scheduling seem to work.
 *
 * This tests:
 * 1. That sched_rr_get_interval seems to work;
 * 2. That FIFO scheduling doesn't seeem to be round-robin;
 * 3. That round-robin scheduling seems to work.
 * 
 */
static pid_t child;
static void tidyup(void)
{
	if (child)
		kill(child, SIGHUP);
}

static double
tvsub(const struct timeval *a, const struct timeval *b)
{
	long sdiff;
	long udiff;

	sdiff = a->tv_sec - b->tv_sec;
	udiff = a->tv_usec - b->tv_usec;

	return (double)(sdiff * 1000000 + udiff) / 1e6;
}

int fifo(int argc, char *argv[])
{
	int e = 0;
	volatile long *p, pid;
	int i;
	struct sched_param fifo_param;
	struct timespec interval;
#define MAX_RANAT 32
	struct timeval ranat[MAX_RANAT];

#ifdef CAN_USE_ALARMS
	static struct itimerval itimerval;
#endif

	/* What is the round robin interval?
	 */

	if (sched_rr_get_interval(0, &interval) == -1) {
		perror("sched_rr_get_interval");
		exit(errno);
	}

#ifdef CAN_USE_ALARMS
	signal(SIGALRM, tick);
#endif

	fifo_param.sched_priority = 1;

	p = (long *)mmap(0, sizeof(*p),
	PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);

	if (p == (long *)-1)
		err(errno, "mmap");

	*p = 0;

	if (sched_setscheduler(0, SCHED_FIFO, &fifo_param) == -1)
	{
		perror("sched_setscheduler");
		return -1;
	}

	pid = getpid();

	if ((child = fork()) == 0)
	{
		/* Child process.  Just keep setting the pointer to our
		 * PID.  The parent will kill us when it wants to.
		 */

		pid = getpid();
		while (1)
			*p = pid;
	}
	else
	{
		atexit(tidyup);
		*p = pid;


		ticked = 0;

#ifdef CAN_USE_ALARMS
		/* Set an alarm for 250 times the round-robin interval.
		 * Then we will verify that a similar priority process
		 * will not run when we are using the FIFO scheduler.
		 */
		itimerval.it_value.tv_usec = interval.tv_nsec / (1000 / 250);

		itimerval.it_value.tv_sec = itimerval.it_value.tv_usec / 1000000;
		itimerval.it_value.tv_usec %= 1000000;


		if (setitimer(ITIMER_REAL, &itimerval, 0) == -1) {
			perror("setitimer");
			exit(errno);
		}
#endif


		gettimeofday(ranat, 0);
		i = 1;
		while (!ticked && i < MAX_RANAT)
			if (*p == child) {
				gettimeofday(ranat + i, 0);
				*p = 0;
				e = -1;
				i++;
			}

		if (e) {
			int j;

			fprintf(stderr,
			"SCHED_FIFO had erroneous context switches:\n");
			for (j = 1; j < i; j++) {
				fprintf(stderr, "%d %g\n", j,
					tvsub(ranat + j, ranat + j - 1));
			}
			return e;
		}

		/* Switch to the round robin scheduler and the child
		 * should run within twice the interval.
		 */
		if (sched_setscheduler(child, SCHED_RR, &fifo_param) == -1 ||
		sched_setscheduler(0, SCHED_RR, &fifo_param) == -1)
		{
			perror("sched_setscheduler");
			return -1;
		}

		e = -1;

		ticked = 0;

#ifdef CAN_USE_ALARMS

		/* Now we do want to see it run.  But only set
		 * the alarm for twice the interval:
		 */
		itimerval.it_value.tv_usec = interval.tv_nsec / 500;

		if (setitimer(ITIMER_REAL, &itimerval, 0) == -1) {
			perror("setitimer");
			exit(errno);
		}
#endif

		for (i = 0; !ticked; i++)
			if (*p == child) {
				e = 0;
				break;
			}

		if (e)
			fprintf(stderr,"Child never ran when it should have.\n");
	}

	exit(e);
}

#ifdef STANDALONE_TESTS
int main(int argc, char *argv[]) { return fifo(argc, argv); }
#endif

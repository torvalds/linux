/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996-1999
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
 * $FreeBSD$
 *
 */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "prutil.h"

/* buzz: busy wait a random amount of time.
 */
static void buzz(int n)
{
	volatile int i;
	int m = random() & 0x0ffff;
	for (i = 0; i < m; i++)
		;
}

/* Yield: Verify that "sched_yield" works for the FIFO case.
 * This runs several processes and verifies that the yield seems
 * to permit the next one on the ready queue to run.
 */
int yield(int argc, char *argv[])
{
	volatile int *p;
	int i;
	int nslaves, n;
	int master, slave;
	pid_t youngest = !0;	/* Our youngest child */
	struct sched_param set, got;
	int nloops = 1000;

	errno = 0;

	set.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if (set.sched_priority == -1 && errno) {
		perror("sched_get_priority_max");
		exit(errno);
	}

	if (argc == 1)
		n = nslaves = 10;

	else if (argc != 2) {
		fprintf(stderr, "usage: prog [n_instances]\n");
		exit(-1);
	}
	else
		n = nslaves = atoi(argv[1]);

	p = (int *)mmap(0, sizeof(int),
	PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);

	if (p == (int *)-1)
		err(errno, "mmap");

	*p = 0;

	if (sched_setscheduler(0, SCHED_FIFO, &set) == -1)
		err(errno, "sched_setscheduler");

	/* I better still be SCHED_FIFO and RT_PRIO_MAX:
	 */
	(void)sched_is(__LINE__, &got, SCHED_FIFO);
	if (got.sched_priority != set.sched_priority) {
		fprintf(stderr, "line %d: scheduler screwup\n",
		__LINE__);
		exit(-1);
	}

	slave = 0;
	master = 1;

	/* Fork off the slaves.
	 */
	for (i = 0; i < nslaves; i++) {
		if ((youngest = fork()) == 0) {
			/* I better still be SCHED_FIFO and RT_PRIO_MAX:
			 */
			(void)sched_is(__LINE__, &got, SCHED_FIFO);

			if (got.sched_priority != set.sched_priority) {
				fprintf(stderr, "line %d: scheduler screwup\n",
				__LINE__);
				exit(-1);
			}

			master = 0;	/* I'm a slave */
			slave = i + 1;	/* With this flag */
			*p = slave;	/* And I live */
			break;
		}
	}

	if (master) {
		/* If we conform the slave processes haven't run yet.
		 * The master must yield to let the first slave run.
		 */
		if (*p != 0) {
			fprintf(stderr,
			"Error at line %d: Writer %d has run\n", __LINE__, *p);
			exit(-1);
		}
	}

	/* Now the master yields, the first slave runs, and yields,
	 * next runs, yields, ...
	 *
	 * So the master should get through this first.
	 */

	if (sched_yield() == -1)
		err(errno, "sched_yield");

	if (master) {
		int status;

		/* The final slave process should be the last one started.
		 */
		if (*p != nslaves) {
			fprintf(stderr,
			"Error at line %d: Final slave is %d not %d.\n",
			__LINE__, *p, nslaves);
			exit(-1);
		}

		/* Wait for our youngest to exit:
		 */
		waitpid(youngest, &status, 0);

		exit(WEXITSTATUS(status));	/* Let the slaves continue */
	}

	/* Now the first one has started up.
	 */
	for (i = 0; i < nloops; i++) {
		if (((*p) % nslaves) !=
		((slave + nslaves - 1) % nslaves)) {
			fprintf(stderr, "%d ran before %d on iteration %d.\n",
			*p, slave, i);
			exit(-1);
		}
		*p = slave;

		/* Delay some random amount of time.
		 */
		buzz(slave);

		if (sched_yield() == -1)
			err(errno, "sched_yield");
	}

	exit(0);
}
#ifdef STANDALONE_TESTS
int main(int argc, char *argv[]) { return yield(argc, argv); }
#endif

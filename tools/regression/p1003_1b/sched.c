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

/* XXX: The spec says that if _POSIX_C_SOURCE is defined then
 *      _POSIX_SOURCE is ignored.  However, this is similar to
 *      the code in the O'Reilly "POSIX.4" book
 */

#define _POSIX_VERSION 199309L
#define _POSIX_SOURCE
#define _POSIX_C_SOURCE 199309L

#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#define	__XSI_VISIBLE 1
#include <stdlib.h>
#undef __XSI_VISIBLE
#include <string.h>
#include <unistd.h>

#include "prutil.h"

static FILE *verbose;

static void
checkpris(int sched)
{
	int smin;
	int smax;

	errno = 0;

	if ( (smin = sched_get_priority_min(sched)) == -1 && errno)
		quit("sched_get_priority_min");

	if ( (smax = sched_get_priority_max(sched)) == -1 && errno)
		quit("sched_get_priority_max");

	if (smax - smin + 1 < 32 || smax < smin) {
		fprintf(stderr, "Illegal priority range for %s: %d to %d\n",
		sched_text(sched), smin, smax);
		exit(-1);
	}

	if (verbose)
		fprintf(verbose, "%12s: sched_min %2d sched_max %2d\n",
		sched_text(sched), smin, smax);
}

/* Set "try_anyway" to quit if you don't want to go on when
 * it doesn't look like something should work.
 */
static void try_anyway(const char *s)
{
	fputs(s, stderr);
	fprintf(stderr, "(trying anyway)\n");
	errno = 0;
}

static void q(int line, int code, const char *text)
{
	if (code == -1)
	{
		fprintf(stderr, "Error at line %d:\n", line);
		perror(text);
		exit(errno);
	}
}

int sched(int ac, char *av[])
{
	int fifo_schedmin, fifo_schedmax;
	int i;
	struct sched_param rt_param;
	int n_instances = 10;
	int sched;

	verbose = 0;

#if _POSIX_VERSION < 199309
	try_anyway("The _POSIX_VERSION predates P1003.1B\n");
#endif

#if !defined(_POSIX_PRIORITY_SCHEDULING)
	try_anyway(
	"The environment does not claim to support Posix scheduling.\n");
#endif

	/* Is priority scheduling configured?
	 */
	errno = 0;
	if (sysconf(_SC_PRIORITY_SCHEDULING) == -1) {
		if (errno != 0) {
			/* This isn't valid - may be a standard violation
			 */
			quit("(should not happen) sysconf(_SC_PRIORITY_SCHEDULING)");
		}
		else {
			try_anyway(
			"The environment does not have run-time "
			"support for Posix scheduling.\n");
		}
	}

	/* Check that the priorities seem reasonable.
	 */

	checkpris(SCHED_FIFO);
	checkpris(SCHED_RR);
	checkpris(SCHED_OTHER);

/* BSD extensions?
 */
#if defined(SCHED_IDLE)
	checkpris(SCHED_IDLE);
#endif

	fifo_schedmin = sched_get_priority_min(SCHED_FIFO);
	fifo_schedmax = sched_get_priority_max(SCHED_FIFO);

	/* Make sure we can do some basic schedule switching:
	 */
	{
		struct sched_param orig_param, shouldbe;
		int orig_scheduler = sched_is(__LINE__, &orig_param, -1);

		if (verbose)
			fprintf(verbose,
			"The original scheduler is %s and the priority is %d.\n",
			sched_text(orig_scheduler), orig_param.sched_priority);

		/* Basic check: Try to set current settings:
		 */
		q(__LINE__, sched_setscheduler(0, orig_scheduler, &orig_param),
			"sched_setscheduler: Can't set original scheduler");

		rt_param.sched_priority = fifo_schedmin;

		q(__LINE__, sched_setscheduler(0, SCHED_FIFO, &rt_param),
		"sched_setscheduler SCHED_FIFO");

		(void)sched_is(__LINE__, 0, SCHED_FIFO);

		q(__LINE__, sched_getparam(0, &shouldbe), "sched_getparam");

		if (shouldbe.sched_priority != fifo_schedmin)
			quit("sched_setscheduler wrong priority (min)");

		rt_param.sched_priority = fifo_schedmin;

		q(__LINE__, sched_setparam(0, &rt_param),
			"sched_setparam to fifo_schedmin");

		rt_param.sched_priority = fifo_schedmin + 1;

		q(__LINE__, sched_setparam(0, &rt_param),
			"sched_setparam to fifo_schedmin + 1");

		q(__LINE__, sched_getparam(0, &shouldbe),
			"sched_getparam");

		if (shouldbe.sched_priority != fifo_schedmin + 1)
			quit("sched_setscheduler wrong priority (min + 1)");

		q(__LINE__, sched_setscheduler(0, SCHED_RR, &rt_param),
			"sched_setscheduler SCHED_RR");

		(void)sched_is(__LINE__, 0, SCHED_RR);

		q(__LINE__, sched_setscheduler(0, orig_scheduler, &orig_param),
			"sched_setscheduler restoring original scheduler");

		(void)sched_is(__LINE__, 0, orig_scheduler);
	}


	{
		char nam[] = "P1003_1b_schedXXXXXX";
		int fd;
		pid_t p;
		pid_t *lastrun;

		fd = mkstemp(nam);
		if (fd == -1)
			q(__LINE__, errno, "mkstemp failed");

		(void)unlink(nam);

		p = (pid_t)0;

		write(fd, &p, sizeof(p));

		q(__LINE__,  (int)(lastrun = mmap(0, sizeof(*lastrun), PROT_READ|PROT_WRITE,
		MAP_SHARED, fd, 0)), "mmap");

		/* Set our priority at the highest:
		 */
		sched = SCHED_FIFO;
		rt_param.sched_priority = fifo_schedmax;
		q(__LINE__, sched_setscheduler(0, sched, &rt_param),
		"sched_setscheduler sched");

		for (i = 0; i < n_instances; i++)
		{
			pid_t me;

			/* XXX This is completely bogus.  The children never run.
			 */
			if ((me = fork()) != 0)
			{
				/* Parent.
				 */
				(void)sched_is(__LINE__, 0, sched);

				/* Lower our priority:
				 */
				rt_param.sched_priority--;

				q(__LINE__, sched_setscheduler(0, sched, &rt_param),
				"sched_setscheduler sched");

				while (1)
				{
					q(__LINE__, sched_getparam(0, &rt_param), "sched_getparam");

					rt_param.sched_priority--;


					if (rt_param.sched_priority < fifo_schedmin)
						exit(0);

					*lastrun = me;
					q(__LINE__, sched_setparam(0, &rt_param), "sched_setparam");

					if (*lastrun == me)
					{
						/* The child will run twice
						 * at  the end:
						 */
						if (!me || rt_param.sched_priority != 0)
						{
							fprintf(stderr,
							"ran process %ld twice at priority %d\n",
							(long)me, rt_param.sched_priority + 1);
							exit(-1);
						}
					}
				}
			}
		}
	}

	return 0;
}
#ifdef STANDALONE_TESTS
int main(int argc, char *argv[]) { return sched(argc, argv); }
#endif

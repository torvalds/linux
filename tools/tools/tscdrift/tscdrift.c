/*-
 * Copyright (c) 2014 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/cpuset.h>
#include <machine/atomic.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define	barrier()	__asm __volatile("" ::: "memory")

#define	TESTS		1024

static volatile int gate;
static volatile uint64_t thread_tsc;

/* Bind the current thread to the specified CPU. */
static void
bind_cpu(int cpu)
{
	cpuset_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(set),
	    &set) < 0)
		err(1, "cpuset_setaffinity(%d)", cpu);
}

static void *
thread_main(void *arg)
{
	int cpu, i;

	cpu = (intptr_t)arg;
	bind_cpu(cpu);
	for (i = 0; i < TESTS; i++) {
		gate = 1;
		while (gate == 1)
			cpu_spinwait();
		barrier();

		__asm __volatile("lfence");
		thread_tsc = rdtsc();

		barrier();
		gate = 3;
		while (gate == 3)
			cpu_spinwait();
	}
	return (NULL);
}

int
main(int ac __unused, char **av __unused)
{
	cpuset_t all_cpus;
	int64_t **skew, *aveskew, *minskew, *maxskew;
	float *stddev;
	double sumsq;
	pthread_t child;
	uint64_t tsc;
	int *cpus;
	int error, i, j, ncpu;

	/*
	 * Find all the CPUs this program is eligible to run on and use
	 * this as our global set.  This means you can use cpuset to
	 * restrict this program to only run on a subset of CPUs.
	 */
	if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1,
	    sizeof(all_cpus), &all_cpus) < 0)
		err(1, "cpuset_getaffinity");
	for (ncpu = 0, i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, &all_cpus))
			ncpu++;
	}
	if (ncpu < 2)
		errx(1, "Only one available CPU");
	cpus = calloc(ncpu, sizeof(*cpus));
	skew = calloc(ncpu, sizeof(*skew));
	for (i = 0; i < ncpu; i++)
		skew[i] = calloc(TESTS, sizeof(*skew[i]));
	for (i = 0, j = 0; i < CPU_SETSIZE; i++)
		if (CPU_ISSET(i, &all_cpus)) {
			assert(j < ncpu);
			cpus[j] = i;
			j++;
		}

	/*
	 * We bind this thread to the first CPU and then bind all the
	 * other threads to other CPUs in turn saving TESTS counts of
	 * skew calculations.
	 */
	bind_cpu(cpus[0]);
	for (i = 1; i < ncpu; i++) {
		error = pthread_create(&child, NULL, thread_main,
		    (void *)(intptr_t)cpus[i]);
		if (error)
			errc(1, error, "pthread_create");

		for (j = 0; j < TESTS; j++) {
			while (gate != 1)
				cpu_spinwait();
			gate = 2;
			barrier();

			tsc = rdtsc();

			barrier();
			while (gate != 3)
				cpu_spinwait();
			gate = 4;

			skew[i][j] = thread_tsc - tsc;
		}

		error = pthread_join(child, NULL);
		if (error)
			errc(1, error, "pthread_join");
	}

	/*
	 * Compute average skew for each CPU and output a summary of
	 * the results.
	 */
	aveskew = calloc(ncpu, sizeof(*aveskew));
	minskew = calloc(ncpu, sizeof(*minskew));
	maxskew = calloc(ncpu, sizeof(*maxskew));
	stddev = calloc(ncpu, sizeof(*stddev));
	stddev[0] = 0.0;
	for (i = 1; i < ncpu; i++) {
		sumsq = 0;
		minskew[i] = maxskew[i] = skew[i][0];
		for (j = 0; j < TESTS; j++) {
			aveskew[i] += skew[i][j];
			if (skew[i][j] < minskew[i])
				minskew[i] = skew[i][j];
			if (skew[i][j] > maxskew[i])
				maxskew[i] = skew[i][j];
			sumsq += (skew[i][j] * skew[i][j]);
		}
		aveskew[i] /= TESTS;
		sumsq /= TESTS;
		sumsq -= aveskew[i] * aveskew[i];
		stddev[i] = sqrt(sumsq);
	}

	printf("CPU | TSC skew (min/avg/max/stddev)\n");
	printf("----+------------------------------\n");
	for (i = 0; i < ncpu; i++)
		printf("%3d | %5jd %5jd %5jd   %6.3f\n", cpus[i],
		    (intmax_t)minskew[i], (intmax_t)aveskew[i],
		    (intmax_t)maxskew[i], stddev[i]);
	return (0);
}

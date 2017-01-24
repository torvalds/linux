/*
 * Test null syscall performance
 *
 * Copyright (C) 2009-2015 Anton Blanchard, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define NR_LOOPS 10000000

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>

static volatile int soak_done;
unsigned long long clock_frequency;
unsigned long long timebase_frequency;
double timebase_multiplier;

static inline unsigned long long mftb(void)
{
	unsigned long low;

	asm volatile("mftb %0" : "=r" (low));

	return low;
}

static void sigalrm_handler(int unused)
{
	soak_done = 1;
}

/*
 * Use a timer instead of busy looping on clock_gettime() so we don't
 * pollute profiles with glibc and VDSO hits.
 */
static void cpu_soak_usecs(unsigned long usecs)
{
	struct itimerval val;

	memset(&val, 0, sizeof(val));
	val.it_value.tv_usec = usecs;

	signal(SIGALRM, sigalrm_handler);
	setitimer(ITIMER_REAL, &val, NULL);

	while (1) {
		if (soak_done)
			break;
	}

	signal(SIGALRM, SIG_DFL);
}

/*
 * This only works with recent kernels where cpufreq modifies
 * /proc/cpuinfo dynamically.
 */
static void get_proc_frequency(void)
{
	FILE *f;
	char line[128];
	char *p, *end;
	unsigned long v;
	double d;
	char *override;

	/* Try to get out of low power/low frequency mode */
	cpu_soak_usecs(0.25 * 1000000);

	f = fopen("/proc/cpuinfo", "r");
	if (f == NULL)
		return;

	timebase_frequency = 0;

	while (fgets(line, sizeof(line), f) != NULL) {
		if (strncmp(line, "timebase", 8) == 0) {
			p = strchr(line, ':');
			if (p != NULL) {
				v = strtoull(p + 1, &end, 0);
				if (end != p + 1)
					timebase_frequency = v;
			}
		}

		if (((strncmp(line, "clock", 5) == 0) ||
		     (strncmp(line, "cpu MHz", 7) == 0))) {
			p = strchr(line, ':');
			if (p != NULL) {
				d = strtod(p + 1, &end);
				if (end != p + 1) {
					/* Find fastest clock frequency */
					if ((d * 1000000ULL) > clock_frequency)
						clock_frequency = d * 1000000ULL;
				}
			}
		}
	}

	fclose(f);

	override = getenv("FREQUENCY");
	if (override)
		clock_frequency = strtoull(override, NULL, 10);

	if (timebase_frequency)
		timebase_multiplier = (double)clock_frequency
					/ timebase_frequency;
	else
		timebase_multiplier = 1;
}

static void do_null_syscall(unsigned long nr)
{
	unsigned long i;

	for (i = 0; i < nr; i++)
		getppid();
}

#define TIME(A, STR) \

int main(void)
{
	unsigned long tb_start, tb_now;
	struct timespec tv_start, tv_now;
	unsigned long long elapsed_ns, elapsed_tb;

	get_proc_frequency();

	clock_gettime(CLOCK_MONOTONIC, &tv_start);
	tb_start = mftb();

	do_null_syscall(NR_LOOPS);

	clock_gettime(CLOCK_MONOTONIC, &tv_now);
	tb_now = mftb();

	elapsed_ns = (tv_now.tv_sec - tv_start.tv_sec) * 1000000000ULL +
			(tv_now.tv_nsec - tv_start.tv_nsec);
	elapsed_tb = tb_now - tb_start;

	printf("%10.2f ns %10.2f cycles\n", (float)elapsed_ns / NR_LOOPS,
			(float)elapsed_tb * timebase_multiplier / NR_LOOPS);

	return 0;
}

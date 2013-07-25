/*
 * Copyright (C) 2013 Red Hat, Inc., Frederic Weisbecker <fweisbec@redhat.com>
 *
 * Licensed under the terms of the GNU GPL License version 2
 *
 * Selftests for a few posix timers interface.
 *
 * Kernel loop code stolen from Steven Rostedt <srostedt@redhat.com>
 */

#include <sys/time.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#define DELAY 2
#define USECS_PER_SEC 1000000

static volatile int done;

/* Busy loop in userspace to elapse ITIMER_VIRTUAL */
static void user_loop(void)
{
	while (!done);
}

/*
 * Try to spend as much time as possible in kernelspace
 * to elapse ITIMER_PROF.
 */
static void kernel_loop(void)
{
	void *addr = sbrk(0);

	while (!done) {
		brk(addr + 4096);
		brk(addr);
	}
}

/*
 * Sleep until ITIMER_REAL expiration.
 */
static void idle_loop(void)
{
	pause();
}

static void sig_handler(int nr)
{
	done = 1;
}

/*
 * Check the expected timer expiration matches the GTOD elapsed delta since
 * we armed the timer. Keep a 0.5 sec error margin due to various jitter.
 */
static int check_diff(struct timeval start, struct timeval end)
{
	long long diff;

	diff = end.tv_usec - start.tv_usec;
	diff += (end.tv_sec - start.tv_sec) * USECS_PER_SEC;

	if (abs(diff - DELAY * USECS_PER_SEC) > USECS_PER_SEC / 2) {
		printf("Diff too high: %lld..", diff);
		return -1;
	}

	return 0;
}

static int check_itimer(int which)
{
	int err;
	struct timeval start, end;
	struct itimerval val = {
		.it_value.tv_sec = DELAY,
	};

	printf("Check itimer ");

	if (which == ITIMER_VIRTUAL)
		printf("virtual... ");
	else if (which == ITIMER_PROF)
		printf("prof... ");
	else if (which == ITIMER_REAL)
		printf("real... ");

	fflush(stdout);

	done = 0;

	if (which == ITIMER_VIRTUAL)
		signal(SIGVTALRM, sig_handler);
	else if (which == ITIMER_PROF)
		signal(SIGPROF, sig_handler);
	else if (which == ITIMER_REAL)
		signal(SIGALRM, sig_handler);

	err = gettimeofday(&start, NULL);
	if (err < 0) {
		perror("Can't call gettimeofday()\n");
		return -1;
	}

	err = setitimer(which, &val, NULL);
	if (err < 0) {
		perror("Can't set timer\n");
		return -1;
	}

	if (which == ITIMER_VIRTUAL)
		user_loop();
	else if (which == ITIMER_PROF)
		kernel_loop();
	else if (which == ITIMER_REAL)
		idle_loop();

	gettimeofday(&end, NULL);
	if (err < 0) {
		perror("Can't call gettimeofday()\n");
		return -1;
	}

	if (!check_diff(start, end))
		printf("[OK]\n");
	else
		printf("[FAIL]\n");

	return 0;
}

static int check_timer_create(int which)
{
	int err;
	timer_t id;
	struct timeval start, end;
	struct itimerspec val = {
		.it_value.tv_sec = DELAY,
	};

	printf("Check timer_create() ");
	if (which == CLOCK_THREAD_CPUTIME_ID) {
		printf("per thread... ");
	} else if (which == CLOCK_PROCESS_CPUTIME_ID) {
		printf("per process... ");
	}
	fflush(stdout);

	done = 0;
	timer_create(which, NULL, &id);
	if (err < 0) {
		perror("Can't create timer\n");
		return -1;
	}
	signal(SIGALRM, sig_handler);

	err = gettimeofday(&start, NULL);
	if (err < 0) {
		perror("Can't call gettimeofday()\n");
		return -1;
	}

	err = timer_settime(id, 0, &val, NULL);
	if (err < 0) {
		perror("Can't set timer\n");
		return -1;
	}

	user_loop();

	gettimeofday(&end, NULL);
	if (err < 0) {
		perror("Can't call gettimeofday()\n");
		return -1;
	}

	if (!check_diff(start, end))
		printf("[OK]\n");
	else
		printf("[FAIL]\n");

	return 0;
}

int main(int argc, char **argv)
{
	int err;

	printf("Testing posix timers. False negative may happen on CPU execution \n");
	printf("based timers if other threads run on the CPU...\n");

	if (check_itimer(ITIMER_VIRTUAL) < 0)
		return -1;

	if (check_itimer(ITIMER_PROF) < 0)
		return -1;

	if (check_itimer(ITIMER_REAL) < 0)
		return -1;

	if (check_timer_create(CLOCK_THREAD_CPUTIME_ID) < 0)
		return -1;

	/*
	 * It's unfortunately hard to reliably test a timer expiration
	 * on parallel multithread cputime. We could arm it to expire
	 * on DELAY * nr_threads, with nr_threads busy looping, then wait
	 * the normal DELAY since the time is elapsing nr_threads faster.
	 * But for that we need to ensure we have real physical free CPUs
	 * to ensure true parallelism. So test only one thread until we
	 * find a better solution.
	 */
	if (check_timer_create(CLOCK_PROCESS_CPUTIME_ID) < 0)
		return -1;

	return 0;
}

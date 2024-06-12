// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat, Inc., Frederic Weisbecker <fweisbec@redhat.com>
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

#include "../kselftest.h"

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
	int err = 0;

	while (!done && !err) {
		err = brk(addr + 4096);
		err |= brk(addr);
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

	if (llabs(diff - DELAY * USECS_PER_SEC) > USECS_PER_SEC / 2) {
		printf("Diff too high: %lld..", diff);
		return -1;
	}

	return 0;
}

static int check_itimer(int which)
{
	const char *name;
	int err;
	struct timeval start, end;
	struct itimerval val = {
		.it_value.tv_sec = DELAY,
	};

	if (which == ITIMER_VIRTUAL)
		name = "ITIMER_VIRTUAL";
	else if (which == ITIMER_PROF)
		name = "ITIMER_PROF";
	else if (which == ITIMER_REAL)
		name = "ITIMER_REAL";
	else
		return -1;

	done = 0;

	if (which == ITIMER_VIRTUAL)
		signal(SIGVTALRM, sig_handler);
	else if (which == ITIMER_PROF)
		signal(SIGPROF, sig_handler);
	else if (which == ITIMER_REAL)
		signal(SIGALRM, sig_handler);

	err = gettimeofday(&start, NULL);
	if (err < 0) {
		ksft_perror("Can't call gettimeofday()");
		return -1;
	}

	err = setitimer(which, &val, NULL);
	if (err < 0) {
		ksft_perror("Can't set timer");
		return -1;
	}

	if (which == ITIMER_VIRTUAL)
		user_loop();
	else if (which == ITIMER_PROF)
		kernel_loop();
	else if (which == ITIMER_REAL)
		idle_loop();

	err = gettimeofday(&end, NULL);
	if (err < 0) {
		ksft_perror("Can't call gettimeofday()");
		return -1;
	}

	ksft_test_result(check_diff(start, end) == 0, "%s\n", name);

	return 0;
}

static int check_timer_create(int which)
{
	const char *type;
	int err;
	timer_t id;
	struct timeval start, end;
	struct itimerspec val = {
		.it_value.tv_sec = DELAY,
	};

	if (which == CLOCK_THREAD_CPUTIME_ID) {
		type = "thread";
	} else if (which == CLOCK_PROCESS_CPUTIME_ID) {
		type = "process";
	} else {
		ksft_print_msg("Unknown timer_create() type %d\n", which);
		return -1;
	}

	done = 0;
	err = timer_create(which, NULL, &id);
	if (err < 0) {
		ksft_perror("Can't create timer");
		return -1;
	}
	signal(SIGALRM, sig_handler);

	err = gettimeofday(&start, NULL);
	if (err < 0) {
		ksft_perror("Can't call gettimeofday()");
		return -1;
	}

	err = timer_settime(id, 0, &val, NULL);
	if (err < 0) {
		ksft_perror("Can't set timer");
		return -1;
	}

	user_loop();

	err = gettimeofday(&end, NULL);
	if (err < 0) {
		ksft_perror("Can't call gettimeofday()");
		return -1;
	}

	ksft_test_result(check_diff(start, end) == 0,
			 "timer_create() per %s\n", type);

	return 0;
}

static pthread_t ctd_thread;
static volatile int ctd_count, ctd_failed;

static void ctd_sighandler(int sig)
{
	if (pthread_self() != ctd_thread)
		ctd_failed = 1;
	ctd_count--;
}

static void *ctd_thread_func(void *arg)
{
	struct itimerspec val = {
		.it_value.tv_sec = 0,
		.it_value.tv_nsec = 1000 * 1000,
		.it_interval.tv_sec = 0,
		.it_interval.tv_nsec = 1000 * 1000,
	};
	timer_t id;

	/* 1/10 seconds to ensure the leader sleeps */
	usleep(10000);

	ctd_count = 100;
	if (timer_create(CLOCK_PROCESS_CPUTIME_ID, NULL, &id))
		return "Can't create timer\n";
	if (timer_settime(id, 0, &val, NULL))
		return "Can't set timer\n";

	while (ctd_count > 0 && !ctd_failed)
		;

	if (timer_delete(id))
		return "Can't delete timer\n";

	return NULL;
}

/*
 * Test that only the running thread receives the timer signal.
 */
static int check_timer_distribution(void)
{
	const char *errmsg;

	signal(SIGALRM, ctd_sighandler);

	errmsg = "Can't create thread\n";
	if (pthread_create(&ctd_thread, NULL, ctd_thread_func, NULL))
		goto err;

	errmsg = "Can't join thread\n";
	if (pthread_join(ctd_thread, (void **)&errmsg) || errmsg)
		goto err;

	if (!ctd_failed)
		ksft_test_result_pass("check signal distribution\n");
	else if (ksft_min_kernel_version(6, 3))
		ksft_test_result_fail("check signal distribution\n");
	else
		ksft_test_result_skip("check signal distribution (old kernel)\n");
	return 0;
err:
	ksft_print_msg("%s", errmsg);
	return -1;
}

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(6);

	ksft_print_msg("Testing posix timers. False negative may happen on CPU execution \n");
	ksft_print_msg("based timers if other threads run on the CPU...\n");

	if (check_itimer(ITIMER_VIRTUAL) < 0)
		ksft_exit_fail();

	if (check_itimer(ITIMER_PROF) < 0)
		ksft_exit_fail();

	if (check_itimer(ITIMER_REAL) < 0)
		ksft_exit_fail();

	if (check_timer_create(CLOCK_THREAD_CPUTIME_ID) < 0)
		ksft_exit_fail();

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
		ksft_exit_fail();

	if (check_timer_distribution() < 0)
		ksft_exit_fail();

	ksft_finished();
}

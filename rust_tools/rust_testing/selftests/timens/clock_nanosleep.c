// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>

#include <sys/timerfd.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

#include "log.h"
#include "timens.h"

void test_sig(int sig)
{
	if (sig == SIGUSR2)
		pthread_exit(NULL);
}

struct thread_args {
	struct timespec *now, *rem;
	pthread_mutex_t *lock;
	int clockid;
	int abs;
};

void *call_nanosleep(void *_args)
{
	struct thread_args *args = _args;

	clock_nanosleep(args->clockid, args->abs ? TIMER_ABSTIME : 0, args->now, args->rem);
	pthread_mutex_unlock(args->lock);
	return NULL;
}

static int run_test(int clockid, int abs)
{
	struct timespec now = {}, rem;
	struct thread_args args = { .now = &now, .rem = &rem, .clockid = clockid};
	struct timespec start;
	pthread_mutex_t lock;
	pthread_t thread;
	int j, ok, ret;

	signal(SIGUSR1, test_sig);
	signal(SIGUSR2, test_sig);

	pthread_mutex_init(&lock, NULL);
	pthread_mutex_lock(&lock);

	if (clock_gettime(clockid, &start) == -1) {
		if (errno == EINVAL && check_skip(clockid))
			return 0;
		return pr_perror("clock_gettime");
	}


	if (abs) {
		now.tv_sec = start.tv_sec;
		now.tv_nsec = start.tv_nsec;
	}

	now.tv_sec += 3600;
	args.abs = abs;
	args.lock = &lock;
	ret = pthread_create(&thread, NULL, call_nanosleep, &args);
	if (ret != 0) {
		pr_err("Unable to create a thread: %s", strerror(ret));
		return 1;
	}

	/* Wait when the thread will call clock_nanosleep(). */
	ok = 0;
	for (j = 0; j < 8; j++) {
		/* The maximum timeout is about 5 seconds. */
		usleep(10000 << j);

		/* Try to interrupt clock_nanosleep(). */
		pthread_kill(thread, SIGUSR1);

		usleep(10000 << j);
		/* Check whether clock_nanosleep() has been interrupted or not. */
		if (pthread_mutex_trylock(&lock) == 0) {
			/**/
			ok = 1;
			break;
		}
	}
	if (!ok)
		pthread_kill(thread, SIGUSR2);
	pthread_join(thread, NULL);
	pthread_mutex_destroy(&lock);

	if (!ok) {
		ksft_test_result_pass("clockid: %d abs:%d timeout\n", clockid, abs);
		return 1;
	}

	if (rem.tv_sec < 3300 || rem.tv_sec > 3900) {
		pr_fail("clockid: %d abs: %d remain: %ld\n",
			clockid, abs, rem.tv_sec);
		return 1;
	}
	ksft_test_result_pass("clockid: %d abs:%d\n", clockid, abs);

	return 0;
}

int main(int argc, char *argv[])
{
	int ret, nsfd;

	ksft_print_header();

	nscheck();

	ksft_set_plan(4);

	check_supported_timers();

	if (unshare_timens())
		return 1;

	if (_settime(CLOCK_MONOTONIC, 7 * 24 * 3600))
		return 1;
	if (_settime(CLOCK_BOOTTIME, 9 * 24 * 3600))
		return 1;

	nsfd = open("/proc/self/ns/time_for_children", O_RDONLY);
	if (nsfd < 0)
		return pr_perror("Unable to open timens_for_children");

	if (setns(nsfd, CLONE_NEWTIME))
		return pr_perror("Unable to set timens");

	ret = 0;
	ret |= run_test(CLOCK_MONOTONIC, 0);
	ret |= run_test(CLOCK_MONOTONIC, 1);
	ret |= run_test(CLOCK_BOOTTIME_ALARM, 0);
	ret |= run_test(CLOCK_BOOTTIME_ALARM, 1);

	if (ret)
		ksft_exit_fail();
	ksft_exit_pass();
	return ret;
}

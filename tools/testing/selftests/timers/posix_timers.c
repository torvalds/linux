// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat, Inc., Frederic Weisbecker <fweisbec@redhat.com>
 *
 * Selftests for a few posix timers interface.
 *
 * Kernel loop code stolen from Steven Rostedt <srostedt@redhat.com>
 */
#define _GNU_SOURCE
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <include/vdso/time64.h>
#include <pthread.h>

#include "../kselftest.h"

#define DELAY 2

static void __fatal_error(const char *test, const char *name, const char *what)
{
	char buf[64];
	char *ret_str = NULL;

	ret_str = strerror_r(errno, buf, sizeof(buf));

	if (name && strlen(name) && ret_str)
		ksft_exit_fail_msg("%s %s %s %s\n", test, name, what, ret_str);
	else if (ret_str)
		ksft_exit_fail_msg("%s %s %s\n", test, what, ret_str);
	else
		ksft_exit_fail_msg("%s %s\n", test, what);

}

#define fatal_error(name, what)	__fatal_error(__func__, name, what)

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
	diff += (end.tv_sec - start.tv_sec) * USEC_PER_SEC;

	if (llabs(diff - DELAY * USEC_PER_SEC) > USEC_PER_SEC / 2) {
		printf("Diff too high: %lld..", diff);
		return -1;
	}

	return 0;
}

static void check_itimer(int which, const char *name)
{
	struct timeval start, end;
	struct itimerval val = {
		.it_value.tv_sec = DELAY,
	};

	done = 0;

	if (which == ITIMER_VIRTUAL)
		signal(SIGVTALRM, sig_handler);
	else if (which == ITIMER_PROF)
		signal(SIGPROF, sig_handler);
	else if (which == ITIMER_REAL)
		signal(SIGALRM, sig_handler);

	if (gettimeofday(&start, NULL) < 0)
		fatal_error(name, "gettimeofday()");

	if (setitimer(which, &val, NULL) < 0)
		fatal_error(name, "setitimer()");

	if (which == ITIMER_VIRTUAL)
		user_loop();
	else if (which == ITIMER_PROF)
		kernel_loop();
	else if (which == ITIMER_REAL)
		idle_loop();

	if (gettimeofday(&end, NULL) < 0)
		fatal_error(name, "gettimeofday()");

	ksft_test_result(check_diff(start, end) == 0, "%s\n", name);
}

static void check_timer_create(int which, const char *name)
{
	struct timeval start, end;
	struct itimerspec val = {
		.it_value.tv_sec = DELAY,
	};
	timer_t id;

	done = 0;

	if (timer_create(which, NULL, &id) < 0)
		fatal_error(name, "timer_create()");

	if (signal(SIGALRM, sig_handler) == SIG_ERR)
		fatal_error(name, "signal()");

	if (gettimeofday(&start, NULL) < 0)
		fatal_error(name, "gettimeofday()");

	if (timer_settime(id, 0, &val, NULL) < 0)
		fatal_error(name, "timer_settime()");

	user_loop();

	if (gettimeofday(&end, NULL) < 0)
		fatal_error(name, "gettimeofday()");

	ksft_test_result(check_diff(start, end) == 0,
			 "timer_create() per %s\n", name);
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
		fatal_error(NULL, "timer_create()");
	if (timer_settime(id, 0, &val, NULL))
		fatal_error(NULL, "timer_settime()");
	while (ctd_count > 0 && !ctd_failed)
		;

	if (timer_delete(id))
		fatal_error(NULL, "timer_delete()");

	return NULL;
}

/*
 * Test that only the running thread receives the timer signal.
 */
static void check_timer_distribution(void)
{
	if (signal(SIGALRM, ctd_sighandler) == SIG_ERR)
		fatal_error(NULL, "signal()");

	if (pthread_create(&ctd_thread, NULL, ctd_thread_func, NULL))
		fatal_error(NULL, "pthread_create()");

	if (pthread_join(ctd_thread, NULL))
		fatal_error(NULL, "pthread_join()");

	if (!ctd_failed)
		ksft_test_result_pass("check signal distribution\n");
	else if (ksft_min_kernel_version(6, 3))
		ksft_test_result_fail("check signal distribution\n");
	else
		ksft_test_result_skip("check signal distribution (old kernel)\n");
}

struct tmrsig {
	int	signals;
	int	overruns;
};

static void siginfo_handler(int sig, siginfo_t *si, void *uc)
{
	struct tmrsig *tsig = si ? si->si_ptr : NULL;

	if (tsig) {
		tsig->signals++;
		tsig->overruns += si->si_overrun;
	}
}

static void *ignore_thread(void *arg)
{
	unsigned int *tid = arg;
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, NULL))
		fatal_error(NULL, "sigprocmask(SIG_BLOCK)");

	*tid = gettid();
	sleep(100);

	if (sigprocmask(SIG_UNBLOCK, &set, NULL))
		fatal_error(NULL, "sigprocmask(SIG_UNBLOCK)");
	return NULL;
}

static void check_sig_ign(int thread)
{
	struct tmrsig tsig = { };
	struct itimerspec its;
	unsigned int tid = 0;
	struct sigaction sa;
	struct sigevent sev;
	pthread_t pthread;
	timer_t timerid;
	sigset_t set;

	if (thread) {
		if (pthread_create(&pthread, NULL, ignore_thread, &tid))
			fatal_error(NULL, "pthread_create()");
		sleep(1);
	}

	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = siginfo_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL))
		fatal_error(NULL, "sigaction()");

	/* Block the signal */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, NULL))
		fatal_error(NULL, "sigprocmask(SIG_BLOCK)");

	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR1;
	sev.sigev_value.sival_ptr = &tsig;
	if (thread) {
		sev.sigev_notify = SIGEV_THREAD_ID;
		sev._sigev_un._tid = tid;
	}

	if (timer_create(CLOCK_MONOTONIC, &sev, &timerid))
		fatal_error(NULL, "timer_create()");

	/* Start the timer to expire in 100ms and 100ms intervals */
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;
	timer_settime(timerid, 0, &its, NULL);

	sleep(1);

	/* Set the signal to be ignored */
	if (signal(SIGUSR1, SIG_IGN) == SIG_ERR)
		fatal_error(NULL, "signal(SIG_IGN)");

	sleep(1);

	if (thread) {
		/* Stop the thread first. No signal should be delivered to it */
		if (pthread_cancel(pthread))
			fatal_error(NULL, "pthread_cancel()");
		if (pthread_join(pthread, NULL))
			fatal_error(NULL, "pthread_join()");
	}

	/* Restore the handler */
	if (sigaction(SIGUSR1, &sa, NULL))
		fatal_error(NULL, "sigaction()");

	sleep(1);

	/* Unblock it, which should deliver the signal in the !thread case*/
	if (sigprocmask(SIG_UNBLOCK, &set, NULL))
		fatal_error(NULL, "sigprocmask(SIG_UNBLOCK)");

	if (timer_delete(timerid))
		fatal_error(NULL, "timer_delete()");

	if (!thread) {
		ksft_test_result(tsig.signals == 1 && tsig.overruns == 29,
				 "check_sig_ign SIGEV_SIGNAL\n");
	} else {
		ksft_test_result(tsig.signals == 0 && tsig.overruns == 0,
				 "check_sig_ign SIGEV_THREAD_ID\n");
	}
}

static void check_rearm(void)
{
	struct tmrsig tsig = { };
	struct itimerspec its;
	struct sigaction sa;
	struct sigevent sev;
	timer_t timerid;
	sigset_t set;

	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = siginfo_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL))
		fatal_error(NULL, "sigaction()");

	/* Block the signal */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, NULL))
		fatal_error(NULL, "sigprocmask(SIG_BLOCK)");

	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR1;
	sev.sigev_value.sival_ptr = &tsig;
	if (timer_create(CLOCK_MONOTONIC, &sev, &timerid))
		fatal_error(NULL, "timer_create()");

	/* Start the timer to expire in 100ms and 100ms intervals */
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;
	if (timer_settime(timerid, 0, &its, NULL))
		fatal_error(NULL, "timer_settime()");

	sleep(1);

	/* Reprogram the timer to single shot */
	its.it_value.tv_sec = 10;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	if (timer_settime(timerid, 0, &its, NULL))
		fatal_error(NULL, "timer_settime()");

	/* Unblock it, which should not deliver a signal */
	if (sigprocmask(SIG_UNBLOCK, &set, NULL))
		fatal_error(NULL, "sigprocmask(SIG_UNBLOCK)");

	if (timer_delete(timerid))
		fatal_error(NULL, "timer_delete()");

	ksft_test_result(!tsig.signals, "check_rearm\n");
}

static void check_delete(void)
{
	struct tmrsig tsig = { };
	struct itimerspec its;
	struct sigaction sa;
	struct sigevent sev;
	timer_t timerid;
	sigset_t set;

	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = siginfo_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL))
		fatal_error(NULL, "sigaction()");

	/* Block the signal */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, NULL))
		fatal_error(NULL, "sigprocmask(SIG_BLOCK)");

	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR1;
	sev.sigev_value.sival_ptr = &tsig;
	if (timer_create(CLOCK_MONOTONIC, &sev, &timerid))
		fatal_error(NULL, "timer_create()");

	/* Start the timer to expire in 100ms and 100ms intervals */
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;
	if (timer_settime(timerid, 0, &its, NULL))
		fatal_error(NULL, "timer_settime()");

	sleep(1);

	if (timer_delete(timerid))
		fatal_error(NULL, "timer_delete()");

	/* Unblock it, which should not deliver a signal */
	if (sigprocmask(SIG_UNBLOCK, &set, NULL))
		fatal_error(NULL, "sigprocmask(SIG_UNBLOCK)");

	ksft_test_result(!tsig.signals, "check_delete\n");
}

static inline int64_t calcdiff_ns(struct timespec t1, struct timespec t2)
{
	int64_t diff;

	diff = NSEC_PER_SEC * (int64_t)((int) t1.tv_sec - (int) t2.tv_sec);
	diff += ((int) t1.tv_nsec - (int) t2.tv_nsec);
	return diff;
}

static void check_sigev_none(int which, const char *name)
{
	struct timespec start, now;
	struct itimerspec its;
	struct sigevent sev;
	timer_t timerid;

	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_NONE;

	if (timer_create(which, &sev, &timerid))
		fatal_error(name, "timer_create()");

	/* Start the timer to expire in 100ms and 100ms intervals */
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;
	timer_settime(timerid, 0, &its, NULL);

	if (clock_gettime(which, &start))
		fatal_error(name, "clock_gettime()");

	do {
		if (clock_gettime(which, &now))
			fatal_error(name, "clock_gettime()");
	} while (calcdiff_ns(now, start) < NSEC_PER_SEC);

	if (timer_gettime(timerid, &its))
		fatal_error(name, "timer_gettime()");

	if (timer_delete(timerid))
		fatal_error(name, "timer_delete()");

	ksft_test_result(its.it_value.tv_sec || its.it_value.tv_nsec,
			 "check_sigev_none %s\n", name);
}

static void check_gettime(int which, const char *name)
{
	struct itimerspec its, prev;
	struct timespec start, now;
	struct sigevent sev;
	timer_t timerid;
	int wraps = 0;
	sigset_t set;

	/* Block the signal */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, NULL))
		fatal_error(name, "sigprocmask(SIG_BLOCK)");

	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR1;

	if (timer_create(which, &sev, &timerid))
		fatal_error(name, "timer_create()");

	/* Start the timer to expire in 100ms and 100ms intervals */
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;
	if (timer_settime(timerid, 0, &its, NULL))
		fatal_error(name, "timer_settime()");

	if (timer_gettime(timerid, &prev))
		fatal_error(name, "timer_gettime()");

	if (clock_gettime(which, &start))
		fatal_error(name, "clock_gettime()");

	do {
		if (clock_gettime(which, &now))
			fatal_error(name, "clock_gettime()");
		if (timer_gettime(timerid, &its))
			fatal_error(name, "timer_gettime()");
		if (its.it_value.tv_nsec > prev.it_value.tv_nsec)
			wraps++;
		prev = its;

	} while (calcdiff_ns(now, start) < NSEC_PER_SEC);

	if (timer_delete(timerid))
		fatal_error(name, "timer_delete()");

	ksft_test_result(wraps > 1, "check_gettime %s\n", name);
}

static void check_overrun(int which, const char *name)
{
	struct timespec start, now;
	struct tmrsig tsig = { };
	struct itimerspec its;
	struct sigaction sa;
	struct sigevent sev;
	timer_t timerid;
	sigset_t set;

	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = siginfo_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL))
		fatal_error(name, "sigaction()");

	/* Block the signal */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, NULL))
		fatal_error(name, "sigprocmask(SIG_BLOCK)");

	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR1;
	sev.sigev_value.sival_ptr = &tsig;
	if (timer_create(which, &sev, &timerid))
		fatal_error(name, "timer_create()");

	/* Start the timer to expire in 100ms and 100ms intervals */
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;
	if (timer_settime(timerid, 0, &its, NULL))
		fatal_error(name, "timer_settime()");

	if (clock_gettime(which, &start))
		fatal_error(name, "clock_gettime()");

	do {
		if (clock_gettime(which, &now))
			fatal_error(name, "clock_gettime()");
	} while (calcdiff_ns(now, start) < NSEC_PER_SEC);

	/* Unblock it, which should deliver a signal */
	if (sigprocmask(SIG_UNBLOCK, &set, NULL))
		fatal_error(name, "sigprocmask(SIG_UNBLOCK)");

	if (timer_delete(timerid))
		fatal_error(name, "timer_delete()");

	ksft_test_result(tsig.signals == 1 && tsig.overruns == 9,
			 "check_overrun %s\n", name);
}

#include <sys/syscall.h>

static int do_timer_create(int *id)
{
	return syscall(__NR_timer_create, CLOCK_MONOTONIC, NULL, id);
}

static int do_timer_delete(int id)
{
	return syscall(__NR_timer_delete, id);
}

#ifndef PR_TIMER_CREATE_RESTORE_IDS
# define PR_TIMER_CREATE_RESTORE_IDS		77
# define PR_TIMER_CREATE_RESTORE_IDS_OFF	 0
# define PR_TIMER_CREATE_RESTORE_IDS_ON		 1
# define PR_TIMER_CREATE_RESTORE_IDS_GET	 2
#endif

static void check_timer_create_exact(void)
{
	int id;

	if (prctl(PR_TIMER_CREATE_RESTORE_IDS, PR_TIMER_CREATE_RESTORE_IDS_ON, 0, 0, 0)) {
		switch (errno) {
		case EINVAL:
			ksft_test_result_skip("check timer create exact, not supported\n");
			return;
		default:
			ksft_test_result_skip("check timer create exact, errno = %d\n", errno);
			return;
		}
	}

	if (prctl(PR_TIMER_CREATE_RESTORE_IDS, PR_TIMER_CREATE_RESTORE_IDS_GET, 0, 0, 0) != 1)
		fatal_error(NULL, "prctl(GET) failed\n");

	id = 8;
	if (do_timer_create(&id) < 0)
		fatal_error(NULL, "timer_create()");

	if (do_timer_delete(id))
		fatal_error(NULL, "timer_delete()");

	if (prctl(PR_TIMER_CREATE_RESTORE_IDS, PR_TIMER_CREATE_RESTORE_IDS_OFF, 0, 0, 0))
		fatal_error(NULL, "prctl(OFF)");

	if (prctl(PR_TIMER_CREATE_RESTORE_IDS, PR_TIMER_CREATE_RESTORE_IDS_GET, 0, 0, 0) != 0)
		fatal_error(NULL, "prctl(GET) failed\n");

	if (id != 8) {
		ksft_test_result_fail("check timer create exact %d != 8\n", id);
		return;
	}

	/* Validate that it went back to normal mode and allocates ID 9 */
	if (do_timer_create(&id) < 0)
		fatal_error(NULL, "timer_create()");

	if (do_timer_delete(id))
		fatal_error(NULL, "timer_delete()");

	if (id == 9)
		ksft_test_result_pass("check timer create exact\n");
	else
		ksft_test_result_fail("check timer create exact. Disabling failed.\n");
}

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(19);

	ksft_print_msg("Testing posix timers. False negative may happen on CPU execution \n");
	ksft_print_msg("based timers if other threads run on the CPU...\n");

	check_timer_create_exact();

	check_itimer(ITIMER_VIRTUAL, "ITIMER_VIRTUAL");
	check_itimer(ITIMER_PROF, "ITIMER_PROF");
	check_itimer(ITIMER_REAL, "ITIMER_REAL");
	check_timer_create(CLOCK_THREAD_CPUTIME_ID, "CLOCK_THREAD_CPUTIME_ID");

	/*
	 * It's unfortunately hard to reliably test a timer expiration
	 * on parallel multithread cputime. We could arm it to expire
	 * on DELAY * nr_threads, with nr_threads busy looping, then wait
	 * the normal DELAY since the time is elapsing nr_threads faster.
	 * But for that we need to ensure we have real physical free CPUs
	 * to ensure true parallelism. So test only one thread until we
	 * find a better solution.
	 */
	check_timer_create(CLOCK_PROCESS_CPUTIME_ID, "CLOCK_PROCESS_CPUTIME_ID");
	check_timer_distribution();

	check_sig_ign(0);
	check_sig_ign(1);
	check_rearm();
	check_delete();
	check_sigev_none(CLOCK_MONOTONIC, "CLOCK_MONOTONIC");
	check_sigev_none(CLOCK_PROCESS_CPUTIME_ID, "CLOCK_PROCESS_CPUTIME_ID");
	check_gettime(CLOCK_MONOTONIC, "CLOCK_MONOTONIC");
	check_gettime(CLOCK_PROCESS_CPUTIME_ID, "CLOCK_PROCESS_CPUTIME_ID");
	check_gettime(CLOCK_THREAD_CPUTIME_ID, "CLOCK_THREAD_CPUTIME_ID");
	check_overrun(CLOCK_MONOTONIC, "CLOCK_MONOTONIC");
	check_overrun(CLOCK_PROCESS_CPUTIME_ID, "CLOCK_PROCESS_CPUTIME_ID");
	check_overrun(CLOCK_THREAD_CPUTIME_ID, "CLOCK_THREAD_CPUTIME_ID");

	ksft_finished();
}

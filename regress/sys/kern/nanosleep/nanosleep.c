/*	$OpenBSD: nanosleep.c,v 1.9 2024/02/29 21:47:02 bluhm Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <err.h>
#include <signal.h>

int invalid_time(void);
int trivial(void);
int with_signal(void);
int time_elapsed(void);
int time_elapsed_with_signal(void);

int short_time(void);

void sighandler(int);

int
main(int argc, char **argv)
{
	int ch, ret;

	ret = 0;

	while ((ch = getopt(argc, argv, "itseES")) != -1) {
		switch (ch) {
		case 'i':
			ret |= invalid_time();
			break;
		case 't':
			ret |= trivial();
			break;
		case 's':
			ret |= with_signal();
			break;
		case 'e':
			ret |= time_elapsed();
			break;
		case 'E':
			ret |= time_elapsed_with_signal();
			break;
		case 'S':
			ret |= short_time();
			break;
		default:
			fprintf(stderr, "Usage: nanosleep [-itseSE]\n");
			exit(1);
		}
	}

	return (ret);
}

void
sighandler(int signum)
{
}

int
trivial(void)
{
	struct timespec timeout, remainder;

	timeout.tv_sec = 0;
	timeout.tv_nsec = 30000000;
	remainder.tv_sec = 4711;	/* Just add to the confusion */
	remainder.tv_nsec = 4711;
	if (nanosleep(&timeout, &remainder) < 0) {
		warn("%s: nanosleep", __func__);
		return 1;
	}

	/*
	 * Just check that we don't get any leftover time if we sleep the
	 * amount of time we want to sleep.
	 * If we receive any signal, something is wrong anyway.
	 */
	if (remainder.tv_sec != 0 || remainder.tv_nsec != 0) {
		warnx("%s: non-zero time: %lld.%09ld", __func__,
		    (long long)remainder.tv_sec, remainder.tv_nsec);
		return 1;
	}

	return 0;
}

int
with_signal(void)
{
	struct timespec timeout, remainder;
	pid_t pid;
	int status;

	signal(SIGUSR1, sighandler);

	pid = getpid();

	switch(fork()) {
	case -1:
		err(1, "fork");
	case 0:
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;
		nanosleep(&timeout, NULL);
		kill(pid, SIGUSR1);
		_exit(0);
	default:
		break;
	}

	timeout.tv_sec = 10;
	timeout.tv_nsec = 0;
	remainder.tv_sec = 0;
	remainder.tv_nsec = 0;
	if (nanosleep(&timeout, &remainder) == 0) {
		warnx("%s: nanosleep", __func__);
		return 1;
	}

	if (remainder.tv_sec == 0 && remainder.tv_nsec == 0) {
		warnx("%s: zero time", __func__);
		return 1;
	}

	if (wait(&status) < 0)
		err(1, "wait");
	if (status != 0)
		errx(1, "status");

	return 0;
}

int
time_elapsed(void)
{
	struct timespec timeout;
	struct timespec start, end, duration;

	timeout.tv_sec = 0;
	timeout.tv_nsec = 500000000;

	if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
		warn("%s: clock_gettime", __func__);
		return 1;
	}

	if (nanosleep(&timeout, NULL) < 0) {
		warn("%s: nanosleep", __func__);
		return 1;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
		warn("%s: clock_gettime", __func__);
		return 1;
	}

	timespecsub(&end, &start, &duration);

	if (duration.tv_sec == 0 && duration.tv_nsec < 500000000) {
		warnx("%s: slept less than 0.5 sec: %lld.%09ld", __func__,
		    (long long)duration.tv_sec, duration.tv_nsec);
		return 1;
	}

	return 0;
}

int
time_elapsed_with_signal(void)
{
	struct timespec timeout, remainder;
	struct timespec start, end, duration;
	pid_t pid;
	int status;

	signal(SIGUSR1, sighandler);

	pid = getpid();

	switch(fork()) {
	case -1:
		err(1, "fork");
	case 0:
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;
		nanosleep(&timeout, NULL);
		kill(pid, SIGUSR1);
		_exit(0);
	default:
		break;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
		warn("%s: clock_gettime", __func__);
		return 1;
	}

	timeout.tv_sec = 10;
	timeout.tv_nsec = 0;
	remainder.tv_sec = 0;
	remainder.tv_nsec = 0;
	if (nanosleep(&timeout, &remainder) == 0) {
		warnx("%s: nanosleep", __func__);
		return 1;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
		warn("%s: clock_gettime", __func__);
		return 1;
	}

	timespecsub(&end, &start, &duration);
	timespecadd(&duration, &remainder, &timeout);
	/* XXX remainder may be one tick too small */
	remainder.tv_sec = 0;
	remainder.tv_nsec = 10000000;
	timespecadd(&timeout, &remainder, &timeout);

	if (timeout.tv_sec < 10) {
		warnx("%s: slept time + leftover time < 10 sec: %lld.%09ld",
		    __func__, (long long)timeout.tv_sec, timeout.tv_nsec);
		return 1;
	}

	if (wait(&status) < 0)
		err(1, "wait");
	if (status != 0)
		errx(1, "status");

	return 0;
}

int
short_time(void)
{
	struct timespec timeout;
	pid_t pid;
	int status;

	signal(SIGUSR1, sighandler);

	pid = getpid();

	switch(fork()) {
	case -1:
		err(1, "fork");
	case 0:
		/* Sleep two seconds, then shoot parent. */
		timeout.tv_sec = 2;
		timeout.tv_nsec = 0;
		nanosleep(&timeout, NULL);
		kill(pid, SIGUSR1);
		_exit(0);
	default:
		break;
	}

	timeout.tv_sec = 0;
	timeout.tv_nsec = 1;
	if (nanosleep(&timeout, NULL) < 0) {
		warn("%s: nanosleep", __func__);
		return 1;
	}

	if (wait(&status) < 0)
		err(1, "wait");
	if (status != 0)
		errx(1, "status");

	return 0;
}

int
invalid_time(void)
{
	struct timespec timeout[3] = { {-1, 0}, {0, -1}, {0, 1000000000L} };
	int i, status;

	for (i = 0; i < 3; i++) {
		status = nanosleep(&timeout[i], NULL);
		if (status != -1 || errno != EINVAL) {
			warnx("%s: nanosleep %lld %ld", __func__,
			    (long long)timeout[i].tv_sec, timeout[i].tv_nsec);
			return 1;
		}
	}
	return 0;
}

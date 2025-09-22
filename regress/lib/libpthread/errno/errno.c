/* $OpenBSD: errno.c,v 1.3 2021/06/23 22:39:31 kettenis Exp $ */
/* PUBLIC DOMAIN Sep 2011 <guenther@openbsd.org> */

/*
 * Verify that &errno is different for each thread and is stable across
 * context switches and in signal handlers
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"

int *main_errno, *t1_errno, *t2_errno, **handler_errno;
pthread_t main_tid, t1_tid, t2_tid;

enum state
{
	START,
	T1_START,
	T1_SIGNAL,
	T1_CHECK2,
	T1_EXIT,
} state;
 
pthread_mutex_t m;
pthread_cond_t c;
sigset_t sigusr2;

static void
set_state(enum state new_state)
{
	CHECKe(pthread_mutex_lock(&m));
	ASSERT(state == new_state - 1);
	state = new_state;
	CHECKe(pthread_cond_signal(&c));
	CHECKe(pthread_mutex_unlock(&m));
}

static void
wait_for_state(enum state new_state)
{
	CHECKe(pthread_mutex_lock(&m));
	while(state != new_state)
		CHECKe(pthread_cond_wait(&c, &m));
	CHECKe(pthread_mutex_unlock(&m));
}


/*
 * Yes, pthread_self() isn't async-signal-safe in general, but it should
 * be okay for the regress test here
 */
static void
act_handler(int signal)
{
	ASSERT(signal == SIGUSR1);
	if (handler_errno == &main_errno) {
		CHECKe(write(STDOUT_FILENO, "m", 1));
		ASSERT(&errno == main_errno);
		ASSERTe(errno, == EXDEV);
		ASSERT(pthread_equal(t1_tid, pthread_self()));
	} else if (handler_errno == &t1_errno) {
		CHECKe(write(STDOUT_FILENO, "\n", 1));
		ASSERT(&errno == t1_errno);
		ASSERTe(errno, == EXDEV);
		ASSERT(pthread_equal(t1_tid, pthread_self()));
		CHECKe(kill(getpid(), SIGUSR2));
	} else if (handler_errno == &t2_errno) {
		CHECKe(write(STDOUT_FILENO, "2", 1));
		ASSERT(&errno == t2_errno);
		ASSERTe(errno, == EXDEV);
		ASSERT(pthread_equal(t2_tid, pthread_self()));
	} else {
		PANIC("unknown thread in act_handler!");
	}
}

void *
tmain(void *arg)
{
	t1_errno = &errno;
	ASSERT(t1_errno != main_errno);
	ASSERT(*t1_errno == 0);

	/* verify preservation across switch */
	errno = EXDEV;

	wait_for_state(T1_START);

	t1_tid = pthread_self();
	ASSERT(pthread_equal(main_tid, t1_tid) == 0);
	ASSERT(&errno == t1_errno);

	ASSERTe(*t1_errno, == EXDEV);

	set_state(T1_SIGNAL);
	ASSERT(&errno == t1_errno);
	wait_for_state(T1_CHECK2);

	ASSERT(&errno == t1_errno);
	ASSERT(pthread_equal(t1_tid, pthread_self()));

	set_state(T1_EXIT);
	return (NULL);
}

int
main(int argc, char **argv)
{
	struct sigaction act;
	int r;

	pthread_mutex_init(&m, NULL);
	pthread_cond_init(&c, NULL);
	state = START;

	main_errno = &errno;
	main_tid = pthread_self();

	act.sa_handler = act_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	errno = 0;
	CHECKe(sigaction(SIGUSR1, &act, NULL));
	ASSERT(*main_errno == 0);
	ASSERT(errno == 0);

	/*
	 * we'll use SIGUSR2 for signal state change from act_handler,
	 * detecting it with sigwait(), so block it now
	 */
	CHECKe(sigaction(SIGUSR2, &act, NULL));
	sigemptyset(&sigusr2);
	sigaddset(&sigusr2, SIGUSR2);
	CHECKr(pthread_sigmask(SIG_BLOCK, &sigusr2, NULL));

	sched_yield();
	ASSERT(&errno == main_errno);

	/* do something to force an error */
	r = close(11);
	if (r != 0) {
		ASSERT(r == -1);
		ASSERTe(*main_errno, == EBADF);
		ASSERTe(errno, == EBADF);
	}
	r = write(11, "", 1);
	ASSERT(r == -1);
	ASSERTe(*main_errno, == EBADF);
	ASSERTe(errno, == EBADF);

	/* verify that a succesfull syscall doesn't change errno */
	CHECKe(write(STDOUT_FILENO, "X", 1));
	ASSERTe(*main_errno, == EBADF);
	ASSERTe(errno, == EBADF);
	ASSERT(&errno == main_errno);

	CHECKr(pthread_create(&t1_tid, NULL, tmain, NULL));
	ASSERTe(*main_errno, == EBADF);
	ASSERT(&errno == main_errno);
	ASSERT(pthread_equal(main_tid, pthread_self()));

	set_state(T1_START);
	ASSERTe(*main_errno, == EBADF);
	ASSERT(&errno == main_errno);

	wait_for_state(T1_SIGNAL);
	ASSERTe(*main_errno, == EBADF);
	ASSERT(&errno == main_errno);
	ASSERT(pthread_equal(main_tid, pthread_self()));

	handler_errno = &t1_errno;
	CHECKe(pthread_kill(t1_tid, SIGUSR1));
	ASSERT(&errno == main_errno);

	CHECKr(sigwait(&sigusr2, &r));
	ASSERTe(*main_errno, == EBADF);
	ASSERT(&errno == main_errno);
	ASSERT(pthread_equal(main_tid, pthread_self()));
	set_state(T1_CHECK2);

	wait_for_state(T1_EXIT);
	CHECKe(pthread_join(t1_tid, NULL));
	ASSERT(&errno == main_errno);
	ASSERT(pthread_equal(main_tid, pthread_self()));

	SUCCEED;
}

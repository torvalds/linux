/* $OpenBSD: sigdeliver.c,v 1.1 2002/10/12 03:39:21 marc Exp $ */
/* PUBLIC DOMAIN Oct 2002 <marc@snafu.org> */

/*
 * test signal delivery of pending signals
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "test.h"

static pthread_mutex_t	sync_mutex;

volatile sig_atomic_t	got_signal;

/*
 * sigusr1 signal handler.
 */
static void
sighandler(int signo)
{
	got_signal += 1;
}

/*
 * Install a signal handler for sigusr1 and then wait for it to
 * occur.
 */
static void *
do_nothing (void *arg)
{
	SET_NAME("nothing");

	ASSERT(signal(SIGUSR1, sighandler) != SIG_ERR);
	CHECKr(pthread_mutex_lock(&sync_mutex));
	ASSERT(got_signal != 0);
	CHECKr(pthread_mutex_unlock(&sync_mutex));
	return 0;
}

int
main (int argc, char *argv[])
{
	pthread_t pthread;

	/* Initialize and lock a mutex. */
	CHECKr(pthread_mutex_init(&sync_mutex, NULL));
	CHECKr(pthread_mutex_lock(&sync_mutex));

	/* start a thread that will wait on the mutex we now own */
	CHECKr(pthread_create(&pthread, NULL, do_nothing, NULL));

	/*
	 * Give the thread time to run and install its signal handler.
	 * The thread should be blocked waiting for the mutex we own.
	 * Give it a signal and then release the mutex and see if the
	 * signal is ever processed.
	 */
	sleep(2);
	CHECKr(pthread_kill(pthread, SIGUSR1));
	CHECKr(pthread_mutex_unlock(&sync_mutex));
	CHECKr(pthread_join(pthread, NULL));
	SUCCEED;
}

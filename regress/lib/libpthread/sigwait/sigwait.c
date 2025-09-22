/*	$OpenBSD: sigwait.c,v 1.7 2017/05/27 14:24:28 mpi Exp $	*/
/*
 * Copyright (c) 1998 Daniel M. Eischen <eischen@vigrid.com>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Daniel M. Eischen.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL M. EISCHEN AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <stdlib.h>
#include <unistd.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <pthread_np.h>
#include "test.h"

static int		sigcounts[NSIG + 1];
static sigset_t		wait_mask;
static pthread_mutex_t	waiter_mutex;


static void *
sigwaiter (void *arg)
{
	int signo;

	SET_NAME("sigwaiter");

	while (sigcounts[SIGINT] == 0) {
		printf("Sigwait waiting (thread %p)\n", pthread_self());
		CHECKe(sigwait (&wait_mask, &signo));
		sigcounts[signo]++;
		printf ("Sigwait caught signal %d (%s)\n", signo, 
		    strsignal(signo));

		/* Allow the main thread to prevent the sigwait. */
		CHECKr(pthread_mutex_lock (&waiter_mutex));
		CHECKr(pthread_mutex_unlock (&waiter_mutex));
	}

	return (arg);
}


static void
sighandler (int signo)
{
	int save_errno = errno;
	char buf[8192];

	snprintf(buf, sizeof buf,
	    "  -> Signal handler caught signal %d (%s) in thread %p\n",
	    signo, strsignal(signo), pthread_self());
	write(STDOUT_FILENO, buf, strlen(buf));

	if ((signo >= 0) && (signo <= NSIG))
		sigcounts[signo]++;
	errno = save_errno;
}

int main (int argc, char *argv[])
{
	pthread_mutexattr_t mattr;
	pthread_attr_t	pattr;
	pthread_t	tid;
	struct sigaction act;

	/* Initialize our signal counts. */
	memset ((void *) sigcounts, 0, NSIG * sizeof (int));

	/* Setup our wait mask. */
	sigemptyset (&wait_mask);		/* Default action	*/
	sigaddset (&wait_mask, SIGHUP);		/* terminate		*/
	sigaddset (&wait_mask, SIGINT);		/* terminate		*/
	sigaddset (&wait_mask, SIGQUIT);	/* create core image	*/
	sigaddset (&wait_mask, SIGURG);		/* ignore		*/
	sigaddset (&wait_mask, SIGIO);		/* ignore		*/
	sigaddset (&wait_mask, SIGUSR1);	/* terminate		*/

	/* Block all of the signals that will be waited for */
	CHECKe(sigprocmask (SIG_BLOCK, &wait_mask, NULL));

	/* Ignore signals SIGHUP and SIGIO. */
	sigemptyset (&act.sa_mask);
	sigaddset (&act.sa_mask, SIGHUP);
	sigaddset (&act.sa_mask, SIGIO);
	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	CHECKe(sigaction (SIGHUP, &act, NULL));
	CHECKe(sigaction (SIGIO, &act, NULL));

	/* Install a signal handler for SIGURG */
	sigemptyset (&act.sa_mask);
	sigaddset (&act.sa_mask, SIGURG);
	act.sa_handler = sighandler;
	act.sa_flags = SA_RESTART;
	CHECKe(sigaction (SIGURG, &act, NULL));

	/* Install a signal handler for SIGXCPU */
	sigemptyset (&act.sa_mask);
	sigaddset (&act.sa_mask, SIGXCPU);
	CHECKe(sigaction (SIGXCPU, &act, NULL));

	/*
	 * Initialize the thread attribute.
	 */
	CHECKr(pthread_attr_init (&pattr));
	CHECKr(pthread_attr_setdetachstate (&pattr, PTHREAD_CREATE_JOINABLE));

	/*
	 * Initialize and create a mutex.
	 */
	CHECKr(pthread_mutexattr_init (&mattr));
	CHECKr(pthread_mutex_init (&waiter_mutex, &mattr));

	/*
	 * Create the sigwaiter thread.
	 */
	CHECKr(pthread_create (&tid, &pattr, sigwaiter, NULL));

#if 0	/* XXX To quote POSIX 2008, XSH, from section 2.4.1
	 * (Signal Generation and Delivery) paragraph 4:
	 *	If the action associated with a blocked signal is to
	 *	ignore the signal and if that signal is generated for
	 *	the process, it is unspecified whether the signal is
	 *	discarded immediately upon generation or remains pending.
	 * So, SIGIO may remain pending here and be accepted by the sigwait()
	 * in the other thread, even though its disposition is "ignored".
	 */
	/*
	 * Verify that an ignored signal doesn't cause a wakeup.
	 * We don't have a handler installed for SIGIO.
	 */
	CHECKr(pthread_kill (tid, SIGIO));
	sleep (1);
	CHECKe(kill(getpid(), SIGIO));
	sleep (1);
	/* sigwait should not wake up for ignored signal SIGIO */
	ASSERT(sigcounts[SIGIO] == 0);
#endif

	/*
	 * Verify that a signal with a default action of ignore, for
	 * which we have a signal handler installed, will release a sigwait.
	 */
	CHECKr(pthread_kill (tid, SIGURG));
	sleep (1);
	CHECKe(kill(getpid(), SIGURG));
	sleep (1);
	/* sigwait should wake up for SIGURG */
	ASSERT(sigcounts[SIGURG] == 2);

	/*
	 * Verify that a signal with a default action that terminates
	 * the process will release a sigwait.
	 */
	CHECKr(pthread_kill (tid, SIGUSR1));
	sleep (1);
	CHECKe(kill(getpid(), SIGUSR1));
	sleep (1);
	if (sigcounts[SIGUSR1] != 2)
		printf ("FAIL: sigwait doesn't wake up for SIGUSR1.\n");

	/*
	 * Verify that if we install a signal handler for a previously
	 * ignored signal, an occurrence of this signal will release
	 * the (already waiting) sigwait.
	 */

	/* Install a signal handler for SIGHUP. */
	sigemptyset (&act.sa_mask);
	sigaddset (&act.sa_mask, SIGHUP);
	act.sa_handler = sighandler;
	act.sa_flags = SA_RESTART;
	CHECKe(sigaction (SIGHUP, &act, NULL));

	/* Sending SIGHUP should release the sigwait. */
	CHECKe(kill(getpid(), SIGHUP));
	sleep (1);
	CHECKr(pthread_kill (tid, SIGHUP));
	sleep (1);
	/* sigwait should wake up for SIGHUP */
	ASSERT(sigcounts[SIGHUP] == 2);

	/*
	 * Verify that a pending signal in the waiters mask will
	 * cause sigwait to return the pending signal.  We do this
	 * by taking the waiters mutex and signaling the waiter to
	 * release him from the sigwait.  The waiter will block
	 * on taking the mutex, and we can then send the waiter a
	 * signal which should be added to his pending signals.
	 * The next time the waiter does a sigwait, he should
	 * return with the pending signal.
	 */
	sigcounts[SIGHUP] = 0;
 	CHECKr(pthread_mutex_lock (&waiter_mutex));
	/* Release the waiter from sigwait. */
	CHECKe(kill(getpid(), SIGHUP));
	sleep (1);
	/* signal waiter should wake up for SIGHUP */
	ASSERT(sigcounts[SIGHUP] == 1);
	/* Release the waiter thread and allow him to run. */
	CHECKr(pthread_mutex_unlock (&waiter_mutex));
	sleep (1);

	/*
	 * Repeat the above test using pthread_kill and SIGUSR1
	 */
	sigcounts[SIGUSR1] = 0;
 	CHECKr(pthread_mutex_lock (&waiter_mutex));
	/* Release the waiter from sigwait. */
	CHECKr(pthread_kill (tid, SIGUSR1));
	sleep (1);
	/* sigwait should wake up for SIGUSR1 */
	ASSERT(sigcounts[SIGUSR1] == 1);
	/* Add SIGUSR1 to the waiters pending signals. */
	CHECKr(pthread_kill (tid, SIGUSR1));
	/* Release the waiter thread and allow him to run. */
	CHECKr(pthread_mutex_unlock (&waiter_mutex));
	sleep (1);
	/* sigwait should return for pending SIGUSR1 */
	ASSERT(sigcounts[SIGUSR1] == 2);

#if 0
	/*
	 * Verify that we can still kill the process for a signal
	 * not being waited on by sigwait.
	 */
	CHECKe(kill(getpid(), SIGPIPE));
	PANIC("SIGPIPE did not terminate process");

	/*
	 * Wait for the thread to finish.
	 */
	CHECKr(pthread_join (tid, NULL));
#endif

	SUCCEED;
}

/*	$OpenBSD: sigsuspend.c,v 1.5 2012/02/20 01:51:32 guenther Exp $	*/
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

static int	sigcounts[NSIG + 1];
static int	sigfifo[NSIG + 1];
static int	fifo_depth = 0;
static sigset_t suspender_mask;
static pthread_t suspender_tid;


static void *
sigsuspender (void *arg)
{
	int save_count, status, i;
	sigset_t run_mask;

	SET_NAME("sigsuspender");

	/* Run with all signals blocked. */
	sigfillset (&run_mask);
	CHECKe(sigprocmask (SIG_SETMASK, &run_mask, NULL));

	/* Allow these signals to wake us up during a sigsuspend. */
	sigfillset (&suspender_mask);		/* Default action	*/
	sigdelset (&suspender_mask, SIGINT);	/* terminate		*/
	sigdelset (&suspender_mask, SIGHUP);	/* terminate		*/
	sigdelset (&suspender_mask, SIGQUIT);	/* create core image	*/
	sigdelset (&suspender_mask, SIGURG);	/* ignore		*/
	sigdelset (&suspender_mask, SIGIO);	/* ignore		*/
	sigdelset (&suspender_mask, SIGUSR2);	/* terminate		*/
	sigdelset (&suspender_mask, SIGSTOP);	/* unblockable		*/
	sigdelset (&suspender_mask, SIGKILL);	/* unblockable		*/

	while (sigcounts[SIGINT] == 0) {
		save_count = sigcounts[SIGUSR2];

		status = sigsuspend (&suspender_mask);
		if ((status == 0) || (errno != EINTR)) {
			DIE(errno, "Unable to suspend for signals, "
				"return value %d\n",
				status);
		}
		for (i = 0; i < fifo_depth; i++)
			printf ("Sigsuspend woke up by signal %d (%s)\n",
				sigfifo[i], strsignal(sigfifo[i]));
		fifo_depth = 0;
	}

	return (arg);
}


static void
sighandler (int signo)
{
	int save_errno = errno;
	char buf[8192];
	sigset_t set;
	sigset_t tmp;
	pthread_t self;

	if ((signo >= 0) && (signo <= NSIG))
		sigcounts[signo]++;

	/*
	 * If we are running on behalf of the suspender thread,
	 * ensure that we have the correct mask set.   NOTE: per
	 * POSIX the current signo will be part of the mask unless
	 * SA_NODEFER was specified.   Since it isn't in this test
	 * add the current signal to the original suspender_mask
	 * before checking.
	 */
	self = pthread_self ();
	if (self == suspender_tid) {
		sigfifo[fifo_depth] = signo;
		fifo_depth++;
		snprintf(buf, sizeof buf,
		    "  -> Suspender thread signal handler caught "
			"signal %d (%s)\n", signo, strsignal(signo));
		write(STDOUT_FILENO, buf, strlen(buf));
		sigprocmask (SIG_SETMASK, NULL, &set);
		tmp = suspender_mask;
		sigaddset(&tmp, signo);
		sigdelset(&tmp, SIGTHR);
		sigdelset(&set, SIGTHR);
		ASSERT(set == tmp);
	} else {
		snprintf(buf, sizeof buf,
		    "  -> Main thread signal handler caught "
			"signal %d (%s)\n", signo, strsignal(signo));
		write(STDOUT_FILENO, buf, strlen(buf));
	}
	errno = save_errno;
}


int main (int argc, char *argv[])
{
	pthread_attr_t	pattr;
	struct sigaction act;
	sigset_t	oldset;
	sigset_t	newset;

	/* Initialize our signal counts. */
	memset ((void *) sigcounts, 0, NSIG * sizeof (int));

	/* Ignore signal SIGIO. */
	sigemptyset (&act.sa_mask);
	sigaddset (&act.sa_mask, SIGIO);
	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	CHECKe(sigaction (SIGIO, &act, NULL));

	/* Install a signal handler for SIGURG. */
	sigemptyset (&act.sa_mask);
	sigaddset (&act.sa_mask, SIGURG);
	act.sa_handler = sighandler;
	act.sa_flags = SA_RESTART;
	CHECKe(sigaction (SIGURG, &act, NULL));

	/* Install a signal handler for SIGXCPU */
	sigemptyset (&act.sa_mask);
	sigaddset (&act.sa_mask, SIGXCPU);
	CHECKe(sigaction (SIGXCPU, &act, NULL));

	/* Get our current signal mask. */
	CHECKe(sigprocmask (SIG_SETMASK, NULL, &oldset));

	/* Mask out SIGUSR1 and SIGUSR2. */
	newset = oldset;
	sigaddset (&newset, SIGUSR1);
	sigaddset (&newset, SIGUSR2);
	CHECKe(sigprocmask (SIG_SETMASK, &newset, NULL));

	/* Install a signal handler for SIGUSR1 and SIGUSR2 */
	sigemptyset (&act.sa_mask);
	sigaddset (&act.sa_mask, SIGUSR1);
	sigaddset (&act.sa_mask, SIGUSR2);
	act.sa_handler = sighandler;
	act.sa_flags = SA_RESTART;
	CHECKe(sigaction (SIGUSR1, &act, NULL));
	CHECKe(sigaction (SIGUSR2, &act, NULL));

	/*
	 * Initialize the thread attribute.
	 */
	CHECKr(pthread_attr_init (&pattr));
	CHECKr(pthread_attr_setdetachstate (&pattr, PTHREAD_CREATE_JOINABLE));

	/*
	 * Create the sigsuspender thread.
	 */
	CHECKr(pthread_create (&suspender_tid, &pattr, sigsuspender, NULL));

	/*
	 * Verify that an ignored signal doesn't cause a wakeup.
	 * We don't have a handler installed for SIGIO.
	 */
	CHECKr(pthread_kill (suspender_tid, SIGIO));
	sleep (1);
	CHECKe(kill (getpid (), SIGIO));
	sleep (1);
	/* sigsuspend should not wake up for ignored signal SIGIO */
	ASSERT(sigcounts[SIGIO] == 0);

	/*
	 * Verify that a signal with a default action of ignore, for
	 * which we have a signal handler installed, will release a
	 * sigsuspend.
	 */
	CHECKr(pthread_kill (suspender_tid, SIGURG));
	sleep (1);
	CHECKe(kill (getpid (), SIGURG));
	sleep (1);
	/* sigsuspend should wake up for SIGURG */
	ASSERT(sigcounts[SIGURG] == 2);

	/*
	 * Verify that a SIGUSR2 signal will release a sigsuspended
	 * thread.
	 */
	CHECKr(pthread_kill (suspender_tid, SIGUSR2));
	sleep (1);
	CHECKe(kill (getpid (), SIGUSR2));
	sleep (1);
	/* sigsuspend should wake up for SIGUSR2 */
	ASSERT(sigcounts[SIGUSR2] == 2);

	/*
	 * Verify that a signal, blocked in both the main and
	 * sigsuspender threads, does not cause the signal handler
	 * to be called.
	 */
	CHECKr(pthread_kill (suspender_tid, SIGUSR1));
	sleep (1);
	CHECKe(kill (getpid (), SIGUSR1));
	sleep (1);
	/* signal handler should not be called for USR1 */
	ASSERT(sigcounts[SIGUSR1] == 0);
#if 0
	/*
	 * Verify that we can still kill the process for a signal
	 * not being waited on by sigwait.
	 */
	CHECKe(kill (getpid (), SIGPIPE));
	PANIC("SIGPIPE did not terminate process");

	/*
	 * Wait for the thread to finish.
	 */
	CHECKr(pthread_join (suspender_tid, NULL));
#endif
	SUCCEED;
}


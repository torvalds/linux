/* $OpenBSD: pthread_kill.c,v 1.5 2016/05/10 04:04:34 guenther Exp $ */
/* PUBLIC DOMAIN Oct 2002 <marc@snafu.org> */

/*
 * Verify that pthread_kill does the right thing, i.e. the signal
 * is delivered to the correct thread and proper signal processing
 * is performed.
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "test.h"

static void
act_handler(int signal, siginfo_t *siginfo, void *context)
{
	struct sigaction sa;
	char buf[200];

	CHECKe(sigaction(SIGUSR1, NULL, &sa));
	ASSERT(sa.sa_handler == SIG_DFL);
	ASSERT(siginfo != NULL);
	snprintf(buf, sizeof buf,
	    "act_handler: signal %d, siginfo %p, context %p\n",
	    signal, siginfo, context);
	write(STDOUT_FILENO, buf, strlen(buf));
}
 
static void *
thread(void * arg)
{
	sigset_t run_mask;
	sigset_t suspender_mask;

	/* wait for sigusr1 */
	SET_NAME(arg);

	/* Run with all signals blocked, then suspend for SIGUSR1 */
	sigfillset(&run_mask);
	CHECKe(sigprocmask(SIG_SETMASK, &run_mask, NULL));
	sigfillset(&suspender_mask);
	sigdelset(&suspender_mask, SIGUSR1);
	for (;;) {
		sigsuspend(&suspender_mask);
		ASSERT(errno == EINTR);
		printf("Thread %s woke up\n", (char*) arg);
	}
		
}

int
main(int argc, char **argv)
{
	pthread_t thread1;
	pthread_t thread2;
	struct sigaction act;

	act.sa_sigaction = act_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_NODEFER;
	CHECKe(sigaction(SIGUSR1, &act, NULL));
	CHECKr(pthread_create(&thread1, NULL, thread, "T1"));
	CHECKr(pthread_create(&thread2, NULL, thread, "T2"));
	sleep(1);

	/* Signal handler should run once, both threads should awaken */
	CHECKe(kill(getpid(), SIGUSR1));
	sleep(1);

	/* Signal handler run once, only T1 should awaken */
	CHECKe(sigaction(SIGUSR1, &act, NULL));
	CHECKr(pthread_kill(thread1, SIGUSR1));
	sleep(1);

	/* Signal handler run once, only T2 should awaken */
	CHECKe(sigaction(SIGUSR1, &act, NULL));
	CHECKr(pthread_kill(thread2, SIGUSR1));
	sleep(1);

	SUCCEED;
}

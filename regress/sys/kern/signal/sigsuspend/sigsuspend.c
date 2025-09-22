/*	$OpenBSD: sigsuspend.c,v 1.1 2020/09/16 14:04:30 mpi Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2005, Public domain.
 */

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <sys/types.h>
#include <sys/wait.h>

sig_atomic_t gotusr1;
sig_atomic_t gotusr2;

void
usr1handler(int signo)
{
	gotusr1 = 1;
}

void
usr2handler(int signo)
{
	gotusr2 = 1;
}

int
main()
{
	sigset_t set, oset;
	struct sigaction sa;
	pid_t pid, ppid;
	int status;

	ppid = getpid();

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = usr1handler;
	if (sigaction(SIGUSR1, &sa, NULL))
		err(1, "sigaction(USR1)");

	sa.sa_handler = usr2handler;
	if (sigaction(SIGUSR2, &sa, NULL))
		err(1, "sigaction(USR2)");

	/*
	 * Set the procmask to mask the early USR1 the child will send us.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGUSR2);
	if (sigprocmask(SIG_BLOCK, &set, &oset))
		err(1, "sigprocmask");

	switch((pid = fork())) {
	case 0:
		/*
		 * In the child. 
		 */

		kill(ppid, SIGUSR1);	/* Tell the parent we're ready. */

		sigemptyset(&set);
		sigaddset(&set, SIGUSR2);
		sigsuspend(&set);

		/*
		 * Check that sigsuspend didn't change the signal mask.
		 */
		if (sigprocmask(SIG_SETMASK, NULL, &oset))
			err(1, "sigprocmask");
		if (!sigismember(&oset, SIGUSR1) ||
		    !sigismember(&oset, SIGUSR2))
			errx(1, "sigprocmask is bad");

		/* Check that we got the sigusr1 that we expected. */
		if (!gotusr1)
			errx(1, "didn't get usr1");
		if (gotusr2)
			errx(1, "got incorrect usr2");
		
		sigemptyset(&set);
		sigaddset(&set, SIGUSR1);
		sigsuspend(&set);

		if (!gotusr2)
			errx(1, "didn't get usr2");

		_exit(0);
	case -1:
		err(1, "fork");
	default:
		/*
		 * In the parent.
		 * Waiting for the initial USR1 that tells us the child
		 * is ready.
		 */
		while (gotusr1 == 0)
			sigsuspend(&oset);

		/*
		 * Check that sigsuspend didn't change the signal mask.
		 */
		if (sigprocmask(SIG_SETMASK, NULL, &oset))
			err(1, "sigprocmask");
		if (!sigismember(&oset, SIGUSR1) ||
		    !sigismember(&oset, SIGUSR2))
			errx(1, "sigprocmask is bad");

		/*
		 * Deliberately send USR2 first to confuse.
		 */
		kill(pid, SIGUSR2);
		kill(pid, SIGUSR1);

		if (waitpid(pid, &status, 0) != pid)
			err(1, "waitpid");

		if (WIFEXITED(status))
			exit(WEXITSTATUS(status));
		exit(1);
	}
	/* NOTREACHED */
}



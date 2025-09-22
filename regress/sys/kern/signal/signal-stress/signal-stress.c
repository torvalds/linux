/*	$OpenBSD: signal-stress.c,v 1.3 2025/03/12 06:46:35 claudio Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2004 Public Domain.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>

int nprocs, nsigs;
pid_t *pids;
pid_t next, prev;
sig_atomic_t usr1, usr2;

void
sighand(int sig)
{
	if (sig == SIGUSR1 && ++usr1 <= nsigs) {
		if (kill(next, sig))
			_exit(1);
	}
	if (sig == SIGUSR2 && ++usr2 <= nsigs) {
		if (kill(prev, sig))
			_exit(1);
	}
}

void
do_child(void)
{
	int i;
	sigset_t mask, oldmask;

	/*
	 * Step 1 - suspend and wait for SIGCONT so that all siblings have
	 * been started before the next step.
	 */
	raise(SIGSTOP);

	/* Find our neighbours. */
	for (i = 0; i < nprocs; i++) {
		if (pids[i] != getpid())
			continue;
		if (i + 1 == nprocs)
			next = pids[0];
		else
			next = pids[i + 1];
		if (i == 0)
			prev = pids[nprocs - 1];
		else
			prev = pids[i - 1];
	}

	signal(SIGUSR1, sighand);
	signal(SIGUSR2, sighand);

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);

	sigprocmask(SIG_BLOCK, &mask, &oldmask);

	/* Step 2 - wait again until everyone is ready. */
	raise(SIGSTOP);

	while (usr1 < nsigs || usr2 < nsigs)
		sigsuspend(&oldmask);

	/* Step 3 - wait again until everyone is ready. */
	raise(SIGSTOP);
}

void
wait_stopped(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, WUNTRACED) != pid)
		err(1, "waitpid");
	if (!WIFSTOPPED(status))
		errx(1, "child %d not stopped", pid);
}

void
cleanup(void)
{
	int i;

	for (i = 0; i < nprocs; i++)
		kill(pids[i], 9);
}

void
alrmhand(int sig)
{
	cleanup();
	_exit(1);
}

int
main()
{
	int i;
	pid_t pid;

	nprocs = 35;

	nsigs = 1000;

	if ((pids = mmap(NULL, getpagesize(), PROT_READ|PROT_WRITE,
	    MAP_ANON|MAP_SHARED, -1, 0)) == MAP_FAILED)
		err(1, "mmap");

	for (i = 0; i < nprocs; i++) {
		switch((pid = fork())) {
		case 0:
			do_child();
			_exit(0);
		case -1:
			err(1, "fork");
		}
		pids[i] = pid;
	}

	atexit(cleanup);
	signal(SIGALRM, alrmhand);
	alarm(120);			/* Die after two minutes. */

	/* Step 1. Wait until all children have went to sleep */
	for (i = 0; i < nprocs; i++)
		wait_stopped(pids[i]);
	/* And wake them */
	for (i = 0; i < nprocs; i++)
		kill(pids[i], SIGCONT);

	/* Step 2. Repeat. */
	for (i = 0; i < nprocs; i++)
		wait_stopped(pids[i]);
	for (i = 0; i < nprocs; i++)
		kill(pids[i], SIGCONT);

	/*
	 * Now all children are ready for action.
	 * Send the first signals and wait until they all exit.
	 */
	kill(pids[arc4random_uniform(nprocs)], SIGUSR1);
	kill(pids[arc4random_uniform(nprocs)], SIGUSR2);

	/*
	 * The signal game is running, now insert noise in the process.
	 */
	for (i = 0; i < 10 * nprocs; i++) {
		pid_t pid = pids[arc4random_uniform(nprocs)];
		kill(pid, SIGSTOP);
		wait_stopped(pid);
		kill(pid, SIGCONT);
	}
		

	/* Step 3. Repeat. */
	for (i = 0; i < nprocs; i++)
		wait_stopped(pids[i]);
	for (i = 0; i < nprocs; i++)
		kill(pids[i], SIGCONT);

	/* Wait for everyone to finish. */
	for (i = 0; i < nprocs; i++) {
		int status;

		if (waitpid(pids[i], &status, WUNTRACED) != pids[i])
			err(1, "waitpid");
		if (!WIFEXITED(status))
			errx(1, "child %d not stopped (%d)", pids[i], status);
		if (WEXITSTATUS(status) != 0)
			warnx("child %d status: %d", i, status);
	}

	return (0);
}

/*	$OpenBSD: earlysig.c,v 1.2 2014/05/20 01:25:24 guenther Exp $	*/

/*
 * Public domain.  2005, Otto Moerbeek; 2013, Philip Guenther
 *
 * Try to create the case where a signal is delivered to a process before
 * the pthread fork() wrapper can unlock the ld.so bind lock.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <err.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void dohup(int signo)
{
	struct utsname name;
	uname(&name);			/* anything that'll require binding */
}

void *tmain(void *arg)
{
	return (arg);
}

int
main()
{
	pthread_t tid;
	pid_t pid, rpid;
	int r, status;

	if (signal(SIGHUP, dohup) == SIG_ERR)
		err(1, "signal");

	/* make sure the thread library is fully active */
	if ((r = pthread_create(&tid, NULL, tmain, NULL)))
		errc(1, r, "pthread_create");
	pthread_join(tid, NULL);

	/* make sure kill() and all the symbols in fork() are bound */
	kill(0, 0);
	if ((pid = fork()) <= 0) {
		if (pid == -1)
			err(1, "fork");
		_exit(0);
	}
	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");


	switch(pid = fork()) {
	case -1:
		err(1, "fork");
		break;
	case 0:
		sleep(2);
		_exit(0);
	default:
		kill(pid, SIGHUP);
		sleep(3);
		if ((rpid = waitpid(pid, &status, WNOHANG)) == -1)
			err(1, "waitpid");
		if (rpid == 0) {
			/* took too long */
			kill(pid, SIGKILL);
			if (waitpid(pid, &status, 0) == -1)
				err(1, "waitpid");
		}
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			exit(0);
		else if (WIFEXITED(status))
			errx(1, "child exited with status %d",
			    WEXITSTATUS(status));
		else if (WTERMSIG(status) == SIGKILL)
			errx(1, "failed: child hung");
		errx(1, "child killed by signal %d", WTERMSIG(status));
	}
}

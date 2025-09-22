/*	$OpenBSD: earlysig.c,v 1.2 2007/08/01 21:32:53 miod Exp $	*/

/*
 * Public domain.  2005, Otto Moerbeek
 *
 * Try to create the case where a signal is delivered to a process before
 * fork() returns.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

void dohup(int signo)
{
}

int
main()
{
	pid_t pid;
	int status;

	signal(SIGHUP, dohup);

	switch(pid = fork()) {
	case -1:
		err(1, "fork");
		break;
	case 0:
		sleep(2);
		exit(0);
	default:
		kill(pid, SIGHUP);
		sleep(1);
		if (waitpid(pid, &status, 0) == -1)
			err(1, "waitpid");
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			exit(0);
		else
			errx(1, "child exited with status %d",
			    WEXITSTATUS(status));
	}
}

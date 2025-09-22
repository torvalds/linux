/*	$OpenBSD: sig-stop.c,v 1.2 2024/08/23 12:56:26 anton Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2007 Public Domain.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <err.h>
#include <signal.h>

int
main(int argc, char **argv)
{
	struct timespec ts;
	pid_t child;
	int status;
	int count;
	int toggle = 0;

	switch((child = fork())) {
	case -1:
		err(1, "fork");
	case 0:
		ts.tv_sec = 0;
		ts.tv_nsec = 1000;
		for (count = 0; count < 100; count++) {
			nanosleep(&ts, NULL);
		}
		exit(0);
	default:
		break;
	}

	ts.tv_sec = 1;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);

	do {
		toggle ^= 1;
		if (kill(child, toggle ? SIGSTOP : SIGCONT)) {
			if (wait(&status) < 0)
				err(1, "wait");
			break;
		}
	} while(waitpid(child, &status, WCONTINUED|WUNTRACED) > 0 &&
	    (toggle ? WIFSTOPPED(status) : WIFCONTINUED(status)));

	if (!WIFEXITED(status))
		err(1, "bad status: %d", status);

	return 0;
}

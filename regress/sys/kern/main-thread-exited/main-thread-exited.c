/* $OpenBSD: main-thread-exited.c,v 1.2 2014/05/20 01:25:24 guenther Exp $ */
/* PUBLIC DOMAIN Mar 2012 <guenther@openbsd.org> */


#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *
tmain(void *arg)
{
	sleep(1);
	printf("sending SIGKILL\n");
	kill(getpid(), SIGKILL);
	sleep(1);
	printf("still running!\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	pid_t pid;
	pthread_t t;
	int r;

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid > 0) {
		int status;

		if (waitpid(pid, &status, 0) != pid)
			err(1, "waitpid");
		exit(! WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL);
	}

	/* in child */
	if ((r = pthread_create(&t, NULL, tmain, NULL)))
		errc(1, r, "pthread_create");
	pthread_exit(NULL);
	abort();
}

/*	$OpenBSD: blocked_join.c,v 1.2 2012/08/22 22:51:27 fgsch Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2012. Public Domain.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "test.h"

void *
deadlock_detector(void *arg)
{
	sleep(10);
	PANIC("deadlock detected");
}

void *
joiner(void *arg)
{
	pthread_t mainthread = *(pthread_t *)arg;
	ASSERT(pthread_join(mainthread, NULL) == EDEADLK);
	return (NULL);
}

int
main(int argc, char **argv)
{
	pthread_t d, t, self = pthread_self();
	pid_t pid;

	switch ((pid = fork())) {
	case -1:
		PANIC("cannot fork");
		/* NOTREACHED */

	case 0:
		/* child */
		break;

	default:
		CHECKe(waitpid(pid, NULL, 0));
		_exit(0);
		/* NOTREACHED */
	}

        CHECKr(pthread_create(&d, NULL, deadlock_detector, NULL));
	CHECKr(pthread_create(&t, NULL, joiner, &self));
	CHECKr(pthread_join(t, NULL));
	SUCCEED;
}

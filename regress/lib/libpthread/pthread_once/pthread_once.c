/*	$OpenBSD: pthread_once.c,v 1.1 2017/10/16 01:46:09 guenther Exp $ */

/*
 * Scott Cheloha <scottcheloha@gmail.com>, 2017. Public Domain.
 */

#include <pthread.h>
#include <unistd.h>

#include "test.h"

pthread_once_t once_control = PTHREAD_ONCE_INIT;
int done = 0;

void
init_routine(void)
{
	static int first = 1;

	if (first) {
		first = 0;
		CHECKr(pthread_cancel(pthread_self()));
		pthread_testcancel();
	}
	done = 1;
}

void *
thread_main(void *arg)
{
	pthread_once(&once_control, init_routine);
	return NULL;
}

int
main(int argc, char **argv)
{
	pthread_t tid;

	CHECKr(pthread_create(&tid, NULL, thread_main, NULL));
	CHECKr(pthread_join(tid, NULL));
	ASSERT(!done);
	alarm(5);	/* if this rings we are probably deadlocked */
	CHECKr(pthread_once(&once_control, init_routine));
	alarm(0);
	ASSERT(done);
	SUCCEED;
}
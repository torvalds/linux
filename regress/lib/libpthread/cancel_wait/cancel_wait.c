/* $OpenBSD: cancel_wait.c,v 1.2 2015/09/14 08:36:32 guenther Exp $ */
/* PUBLIC DOMAIN <marc@snafu.org> */

/*
 * Check that a thread waiting in wait/waitpid/wait3/wait4 can be
 * cancelled.
 */

#include <sys/types.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <err.h>
#include <pthread.h>
#include <unistd.h>

#include "test.h"

pid_t child;
int status;

static void *
wait_thread(void *arg)
{
	wait(&status);
	return (arg);
}

static void *
waitpid_thread(void *arg)
{
	waitpid(child, &status, 0);
	return (arg);
}

static void *
wait3_thread(void *arg)
{
	wait3(&status, 0, NULL);
	return (arg);
}

static void *
wait4_thread(void *arg)
{
	wait4(child, &status, 0, NULL);
	return (arg);
}

int
main(int argc, char *argv[])
{
	pthread_t thread;
	void *ret = NULL;

	child = fork();
	if (child == -1)
		err(1, "fork");
	if (child == 0) {
		sleep(1000000);
		_exit(0);
	}

	status = 42;

	printf("trying wait\n");
	CHECKr(pthread_create(&thread, NULL, wait_thread, NULL));
	sleep(1);
	CHECKr(pthread_cancel(thread));
	CHECKr(pthread_join(thread, &ret));
	ASSERT(ret == PTHREAD_CANCELED);
	ASSERT(status == 42);

	printf("trying waitpid\n");
	CHECKr(pthread_create(&thread, NULL, waitpid_thread, NULL));
	sleep(1);
	CHECKr(pthread_cancel(thread));
	CHECKr(pthread_join(thread, &ret));
	ASSERT(ret == PTHREAD_CANCELED);
	ASSERT(status == 42);

	printf("trying wait3\n");
	CHECKr(pthread_create(&thread, NULL, wait3_thread, NULL));
	sleep(1);
	CHECKr(pthread_cancel(thread));
	CHECKr(pthread_join(thread, &ret));
	ASSERT(ret == PTHREAD_CANCELED);
	ASSERT(status == 42);

	printf("trying wait4\n");
	CHECKr(pthread_create(&thread, NULL, wait4_thread, NULL));
	sleep(1);
	CHECKr(pthread_cancel(thread));
	CHECKr(pthread_join(thread, &ret));
	ASSERT(ret == PTHREAD_CANCELED);
	ASSERT(status == 42);

	kill(child, SIGKILL);

	CHECKr(pthread_create(&thread, NULL, wait4_thread, NULL));
	sleep(1);
	CHECKr(pthread_join(thread, &ret));
	ASSERT(ret == NULL);
	ASSERT(WIFSIGNALED(status));
	ASSERT(WTERMSIG(status) == 9);

	SUCCEED;
}

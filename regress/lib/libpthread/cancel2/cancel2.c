/* $OpenBSD: cancel2.c,v 1.3 2015/09/14 08:35:44 guenther Exp $ */
/* PUBLIC DOMAIN <marc@snafu.org> */

/*
 * Check that a thread waiting on a select or poll without timeout can be
 * cancelled.
 */

#include <sys/types.h>
#include <sys/time.h>

#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include "test.h"

static void *
select_thread(void *arg)
{
	int read_fd = *(int*) arg;
	fd_set read_fds;
	int result;

	FD_ZERO(&read_fds);
	FD_SET(read_fd, &read_fds);
	result = select(read_fd + 1, &read_fds, NULL, NULL, NULL);
	printf("select returned %d\n", result);
	return 0;
}


static void *
pselect_thread(void *arg)
{
	int read_fd = *(int*) arg;
	fd_set read_fds;
	int result;

	FD_ZERO(&read_fds);
	FD_SET(read_fd, &read_fds);
	result = pselect(read_fd + 1, &read_fds, NULL, NULL, NULL, NULL);
	printf("pselect returned %d\n", result);
	return 0;
}

static void *
poll_thread(void *arg)
{
	int read_fd = *(int*) arg;
	struct pollfd pfd;
	int result;

	pfd.fd = read_fd;
	pfd.events = POLLIN;

	result = poll(&pfd, 1, -1);
	printf("poll returned %d\n", result);
	return arg;
}


static void *
ppoll_thread(void *arg)
{
	int read_fd = *(int*) arg;
	struct pollfd pfd;
	int result;

	pfd.fd = read_fd;
	pfd.events = POLLIN;

	result = ppoll(&pfd, 1, NULL, NULL);
	printf("ppoll returned %d\n", result);
	return arg;
}

int
main(int argc, char *argv[])
{
	pthread_t thread;
	void *result = NULL;
	int pipe_fd[2];

	CHECKe(pipe(pipe_fd));

	printf("trying select\n");
	CHECKr(pthread_create(&thread, NULL, select_thread, pipe_fd));
	sleep(1);
	CHECKr(pthread_cancel(thread));
	CHECKr(pthread_join(thread, &result));
	ASSERT(result == PTHREAD_CANCELED);

	printf("trying pselect\n");
	CHECKr(pthread_create(&thread, NULL, pselect_thread, pipe_fd));
	sleep(1);
	CHECKr(pthread_cancel(thread));
	CHECKr(pthread_join(thread, &result));
	ASSERT(result == PTHREAD_CANCELED);

	printf("trying poll\n");
	CHECKr(pthread_create(&thread, NULL, poll_thread, pipe_fd));
	sleep(1);
	CHECKr(pthread_cancel(thread));
	CHECKr(pthread_join(thread, &result));
	ASSERT(result == PTHREAD_CANCELED);

	printf("trying ppoll\n");
	CHECKr(pthread_create(&thread, NULL, ppoll_thread, pipe_fd));
	sleep(1);
	CHECKr(pthread_cancel(thread));
	CHECKr(pthread_join(thread, &result));
	ASSERT(result == PTHREAD_CANCELED);

	SUCCEED;
}

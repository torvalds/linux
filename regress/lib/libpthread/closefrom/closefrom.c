/* $OpenBSD: closefrom.c,v 1.1 2004/01/15 22:22:52 marc Exp $ */

/* PUBLIC DOMAIN: No Rights Reserved. Marco S Hyman <marc@snafu.org> */

#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include "test.h"

static void *
dummy_thread(void* arg)
{
	/* just exit */
	return NULL;
}

/*
 * Test that closefrom does the right thing in a threaded programs,
 * specifically that it doesn't kill the thread kernel signal pipe.
 */
int
main(int argc, char *argv[])
{
	pthread_t thread;
	void *status;
	int fd;
	int result;

	/* close files above stderr.   The kernel pipes shouldn't be touched */
	fd = STDERR_FILENO + 1;
	result = closefrom(fd);
	printf("closefrom(%d) == %d/%d\n", fd, result, errno);

	/* do it again: make sure that the result is -1/EBADF */
	result = closefrom(fd);
	printf("closefrom(%d) == %d/%d\n", fd, result, errno);
	ASSERT(result == -1 && errno == EBADF);

	/* start a thread to verify the thread kernel is working */
	CHECKr(pthread_create(&thread, NULL, dummy_thread, NULL));
	CHECKr(pthread_join(thread, &status));
	printf("dummy thread exited with status %p\n", status);

	SUCCEED;
	return 0;
}

/*	$OpenBSD: close_race.c,v 1.2 2006/10/06 13:11:58 kurt Exp $	*/
/*
 * Copyright (c) 2006 Kurt Miller <kurt@intricatesoftware.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Test close() racing with other threads using the same file
 * descriptor.
 */

#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include "test.h"

#define ITERATIONS	100
#define BUSY_THREADS	10

static void *
deadlock_detector(void *arg)
{
	sleep(60);
	PANIC("deadlock detected");
}

static void *
busy_thread(void *arg)
{
	int fd = *(int *)arg;

	/* loop until error */
	while(fcntl(fd, F_GETFD, NULL) != -1);

	return ((caddr_t)NULL + errno);
}

int
main(int argc, char *argv[])
{
	pthread_t busy_threads[BUSY_THREADS], deadlock_thread;
	int fd, i, j;
	void *value_ptr;
	struct timespec rqtp;

	rqtp.tv_sec = 0;
	rqtp.tv_nsec = 1000000;

	CHECKr(pthread_create(&deadlock_thread, NULL,
	    deadlock_detector, NULL));

	for (i = 0; i < ITERATIONS; i++) {
		CHECKe(fd = socket(AF_INET, SOCK_DGRAM, 0));
		for (j = 0; j < BUSY_THREADS; j++)
			CHECKr(pthread_create(&busy_threads[j], NULL,
			    busy_thread, (void *)&fd));
		nanosleep(&rqtp, NULL);
		CHECKr(close(fd));
		for (j = 0; j < 5; j++) {
			CHECKr(pthread_join(busy_threads[j], &value_ptr));
			ASSERT(value_ptr == (void *)EBADF);
		}
	}	
	SUCCEED;
}

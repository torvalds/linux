/*	$OpenBSD: blocked_dup2.c,v 1.4 2012/02/20 17:06:11 kurt Exp $	*/
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
 * Test dup2() racing with other threads using the same file
 * descriptor, with some of them blocking on data that will never
 * arrive.
 */

#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "test.h"

#define ITERATIONS	100
#define WAITING_THREADS	5

static void *
deadlock_detector(void *arg)
{
	sleep(15);
	PANIC("deadlock detected");
}

static void *
waiting_read(void *arg)
{
	int fd = *(int *)arg;
	struct sockaddr remote_addr;
	char readBuf;
	int n, remote_addr_len = sizeof(struct sockaddr);

	n = recvfrom(fd, &readBuf, 1, 0, &remote_addr, &remote_addr_len);

	if (n == -1)
		return ((caddr_t)NULL + errno);
	else
		return (NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t waiting_threads[WAITING_THREADS];
	pthread_t deadlock_thread;
	struct sockaddr_in addr;
	int fd, newfd, i, j;
	void *value_ptr;
	struct timespec rqtp;

	rqtp.tv_sec = 0;
	rqtp.tv_nsec = 1000000;

	bzero((char *) &addr, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(0);

	CHECKr(pthread_create(&deadlock_thread, NULL,
	    deadlock_detector, NULL));

	CHECKe(fd = socket(AF_INET, SOCK_DGRAM, 0));
 	CHECKr(bind(fd, (struct sockaddr *)&addr, sizeof(addr)));

	CHECKe(newfd = socket(AF_INET, SOCK_DGRAM, 0));
 	CHECKr(bind(newfd, (struct sockaddr *)&addr, sizeof(addr)));

	for (i = 0; i < ITERATIONS; i++) {
		for (j = 0; j < WAITING_THREADS; j++)
			CHECKr(pthread_create(&waiting_threads[j], NULL,
			    waiting_read, (void *)&newfd));
		nanosleep(&rqtp, NULL);
		CHECKe(dup2(fd, newfd));
		for (j = 0; j < WAITING_THREADS; j++) {
			CHECKr(pthread_join(waiting_threads[j], &value_ptr));
			ASSERT(value_ptr == (void *)EBADF);
		}
	}	
	SUCCEED;
}

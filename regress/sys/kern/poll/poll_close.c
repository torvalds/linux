/*	$OpenBSD: poll_close.c,v 1.4 2021/12/24 10:22:41 visa Exp $	*/

/*
 * Copyright (c) 2021 Visa Hankala
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
 * Test behaviour when a monitored file descriptor is closed by another thread.
 *
 * Note that this case is not defined by POSIX.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <err.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int	barrier[2];
static int	sock[2];

static void *
thread_main(void *arg)
{
	struct pollfd pfd[1];
	int ret;
	char b;

	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = sock[1];
	pfd[0].events = POLLIN;
	ret = poll(pfd, 1, INFTIM);
	assert(ret == 1);
	assert(pfd[0].revents & POLLIN);

	/* Drain data to prevent subsequent wakeups. */
	read(sock[1], &b, 1);

	/* Sync with parent thread. */
	write(barrier[1], "y", 1);
	read(barrier[1], &b, 1);

	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = sock[1];
	pfd[0].events = POLLIN;
	ret = poll(pfd, 1, INFTIM);
	assert(ret == 1);
	assert(pfd[0].revents & POLLNVAL);

	return NULL;
}

int
main(void)
{
	pthread_t t;
	int ret, saved_fd;
	char b;

	/* Enforce test timeout. */
	alarm(10);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, barrier) == -1)
		err(1, "can't create socket pair");

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock) == -1)
		err(1, "can't create socket pair");

	ret = pthread_create(&t, NULL, thread_main, NULL);
	if (ret != 0) {
		fprintf(stderr, "can't start thread: %s\n", strerror(ret));
		return 1;
	}

	/* Let the thread settle in poll(). */
	usleep(100000);

	/* Awaken poll(). */
	write(sock[0], "x", 1);

	/* Wait until the thread has left poll(). */
	read(barrier[0], &b, 1);

	/*
	 * Close and restore the fd that the thread has polled.
	 * This creates a pending badfd knote in the kernel.
	 */
	saved_fd = dup(sock[1]);
	close(sock[1]);
	dup2(saved_fd, sock[1]);
	close(saved_fd);

	/* Let the thread continue. */
	write(barrier[0], "x", 1);

	/* Let the thread settle in poll(). */
	usleep(100000);

	/* Close the fd to awaken poll(). */
	close(sock[1]);

	pthread_join(t, NULL);

	close(sock[0]);

	return 0;
}

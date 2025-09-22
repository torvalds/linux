/*	$OpenBSD: select.c,v 1.4 2018/04/26 15:55:14 guenther Exp $	*/
/*
 * Copyright (c) 1993, 1994, 1995, 1996 by Chris Provenzano and contributors, 
 * proven@mit.edu All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Chris Provenzano,
 *	the University of California, Berkeley, and contributors.
 * 4. Neither the name of Chris Provenzano, the University, nor the names of
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CHRIS PROVENZANO AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CHRIS PROVENZANO, THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */ 

/*
 * Rudimentary test of select().
 */

#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "test.h"

#define NLOOPS 10000

int ntouts = 0;

static void *
bg_routine(void *arg)
{
	char dot = '.';
	int n;

	SET_NAME("bg");

	/* Busy loop, printing dots */
	for (;;) {
		pthread_yield();
		write(STDOUT_FILENO, &dot, sizeof dot);
		pthread_yield();
		n = NLOOPS;
		while (n-- > 0)
			pthread_yield();
	}
}

static void *
fg_routine(void *arg)
{
	int	flags;
	int	n;
	fd_set	r;
	int	fd = fileno((FILE *) arg);
	int	tty = isatty(fd);
	int	maxfd;
	int	nb;
	char	buf[128];

	SET_NAME("fg");

	/* Set the file descriptor to non-blocking */
	flags = fcntl(fd, F_GETFL);
	CHECKr(fcntl(fd, F_SETFL, flags | O_NONBLOCK));

	for (;;) {

		/* Print a prompt if it's a tty: */
		if (tty) {
			printf("type something> ");
			fflush(stdout);
		}

		/* Select on the fdesc: */
		FD_ZERO(&r);
		FD_SET(fd, &r);
		maxfd = fd;
		errno = 0;
		CHECKe(n = select(maxfd + 1, &r, (fd_set *) 0, (fd_set *) 0,
				  (struct timeval *) 0));

		if (n > 0) {
			/* Something was ready for read. */
			printf("select returned %d\n", n);
			while ((nb = read(fd, buf, sizeof(buf) - 1)) > 0) {
				printf("read %d: `%.*s'\n", nb, nb, buf);
			}
			printf("last read was %d, errno = %d %s\n", nb, errno,
			       errno == 0 ? "success" : strerror(errno));
			if (nb < 0)
				ASSERTe(errno, == EWOULDBLOCK || 
				    errno == EAGAIN);
			if (nb == 0)
				break;
		} else
			ntouts++;
	}
	printf("read finished\n");
	return (NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t	bg_thread, fg_thread;
	FILE *		slpr;

	/* Create a fdesc that will block for a while on read: */
	CHECKn(slpr = popen("sleep 2; echo foo", "r"));

	/* Create a busy loop thread that yields a lot: */
	CHECKr(pthread_create(&bg_thread, NULL, bg_routine, 0));

	/* Create the thread that reads the fdesc: */
	CHECKr(pthread_create(&fg_thread, NULL, fg_routine, (void *) slpr));

	/* Wait for the reader thread to finish */
	CHECKr(pthread_join(fg_thread, NULL));

	/* Clean up*/
	CHECKe(pclose(slpr));

	SUCCEED;
}

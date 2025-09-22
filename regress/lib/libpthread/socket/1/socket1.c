/*	$OpenBSD: socket1.c,v 1.6 2017/05/30 06:38:10 mpi Exp $	*/
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

/* ==== test_sock_1.c =========================================================
 * Copyright (c) 1993 by Chris Provenzano, proven@athena.mit.edu
 *
 * Description : Test pthread_create() and pthread_exit() calls.
 *
 *  1.00 93/08/03 proven
 *      -Started coding this file.
 */

#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "test.h"
#include <sched.h>
#include <string.h>
#include <stdlib.h>

struct sockaddr_in a_sout;
int success = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_attr_t attr;

static int counter = 0;

static void *
sock_connect(void *arg)
{
	char buf[1024];
	int fd;

	/* Ensure sock_read runs first */
	CHECKr(pthread_mutex_lock(&mutex));

	a_sout.sin_addr.s_addr = htonl(0x7f000001); /* loopback */
	CHECKe(fd = socket(AF_INET, SOCK_STREAM, 0));

	ASSERT(++counter == 2);

	/* connect to the socket */
	CHECKe(connect(fd, (struct sockaddr *) &a_sout, sizeof(a_sout)));
	CHECKe(close(fd));

	CHECKe(fd = socket(AF_INET, SOCK_STREAM, 0));
	ASSERT(++counter == 3);

	CHECKr(pthread_mutex_unlock(&mutex));
	CHECKe(connect(fd, (struct sockaddr *) &a_sout, sizeof(a_sout)));

	/* Ensure sock_read runs again */
	pthread_yield();
	sleep(1);

	CHECKr(pthread_mutex_lock(&mutex));
	memset(buf, 0, sizeof(buf));
	CHECKe(read(fd, buf, 1024));

	ASSERT(++counter == atoi(buf));
	write(fd, "6", 1);

	CHECKe(close(fd));
	success++;
	CHECKr(pthread_mutex_unlock(&mutex));

	return(NULL);
}

static void *
sock_write(void *arg)
{
	int fd = *(int *)arg;

	CHECKe(write(fd, "5", 1));
	return(NULL);
}

static void *
sock_accept(void *arg)
{
	pthread_t thread;
	struct sockaddr a_sin;
	int a_sin_size, a_fd, fd;
	short port;
	char buf[1024];

	port = 3276;
	a_sout.sin_family = AF_INET;
	a_sout.sin_port = htons(port);
	a_sout.sin_addr.s_addr = INADDR_ANY;

	CHECKe(a_fd = socket(AF_INET, SOCK_STREAM, 0));

	while (1) {
		if(0 == bind(a_fd, (struct sockaddr *) &a_sout, sizeof(a_sout)))
			break;
		if (errno == EADDRINUSE) { 
			a_sout.sin_port = htons((++port));
			continue;
		}
		DIE(errno, "bind");
	}
	CHECKe(listen(a_fd, 2));

	ASSERT(++counter == 1);

	CHECKr(pthread_create(&thread, &attr, sock_connect, 
	    (void *)0xdeadbeaf));

	a_sin_size = sizeof(a_sin);
	CHECKe(fd = accept(a_fd, &a_sin, &a_sin_size));
	CHECKr(pthread_mutex_lock(&mutex));
	CHECKe(close(fd));

	ASSERT(++counter == 4);

	a_sin_size = sizeof(a_sin);
	CHECKe(fd = accept(a_fd, &a_sin, &a_sin_size));
	CHECKr(pthread_mutex_unlock(&mutex));

	/* Setup a write thread */
	CHECKr(pthread_create(&thread, &attr, sock_write, &fd));
	memset(buf, 0, sizeof(buf));
	CHECKe(read(fd, buf, 1024));

	ASSERT(++counter == atoi(buf));

	CHECKe(close(fd));

	CHECKr(pthread_mutex_lock(&mutex));
	success++;
	CHECKr(pthread_mutex_unlock(&mutex));

	CHECKr(pthread_join(thread, NULL));
	return(NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t thread;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	CHECKr(pthread_attr_init(&attr));
#if 0
	CHECKr(pthread_attr_setschedpolicy(&attr, SCHED_FIFO));
#endif
	CHECKr(pthread_create(&thread, &attr, sock_accept,
	    (void *)0xdeadbeaf));

	CHECKr(pthread_join(thread, NULL));

	ASSERT(success == 2);
	SUCCEED;
}

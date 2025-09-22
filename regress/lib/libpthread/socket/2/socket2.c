/*	$OpenBSD: socket2.c,v 1.7 2015/11/04 21:29:20 tedu Exp $	*/
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
#include <pthread_np.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "test.h"

struct sockaddr_in a_sout;

#define MESSAGE5 "This should be message #5"
#define MESSAGE6 "This should be message #6"

static void *
sock_write(void *arg)
{
	int fd = *(int *)arg;

	SET_NAME("writer");
	CHECKe(write(fd, MESSAGE5, sizeof(MESSAGE5)));
	return(NULL);
}

static pthread_mutex_t waiter_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *
waiter(void *arg)
{
	int status;
	pid_t pid;

	SET_NAME("waiter");
	CHECKr(pthread_mutex_lock(&waiter_mutex));
	printf("waiting for child\n");
	CHECKe(pid = wait(&status));
	ASSERT(WIFEXITED(status));
	ASSERT(WEXITSTATUS(status) == 0);
	printf("child exited\n");
	CHECKr(pthread_mutex_unlock(&waiter_mutex));
	return (NULL);
}

static void *
sock_accept(void *arg)
{
	pthread_t thread, wthread;
	struct sockaddr a_sin;
	int a_sin_size, a_fd, fd;
	u_int16_t port;
	char buf[1024];
	pid_t pid;

	port = 3276;
	a_sout.sin_family = AF_INET;
	a_sout.sin_port = htons(port);
	a_sout.sin_addr.s_addr = INADDR_ANY;

	CHECKe(a_fd = socket(AF_INET, SOCK_STREAM, 0));

	while(1) {
		if (bind(a_fd, (struct sockaddr *)&a_sout, sizeof(a_sout))==0)
			break;
		if (errno == EADDRINUSE) { 
			a_sout.sin_port = htons((++port));
			continue;
		}
		DIE(errno, "bind");
	}

	printf("listening on port %d\n", port);

	CHECKe(listen(a_fd, 2));
		
	printf("%d: This should be message #1\n", getpid());

	CHECKr(pthread_mutex_init(&waiter_mutex, NULL));
	CHECKr(pthread_mutex_lock(&waiter_mutex));
	CHECKr(pthread_create(&wthread, NULL, waiter, NULL));

	snprintf(buf, sizeof buf, "%d", port);

	CHECKe(pid = fork());
	switch(pid) {
	case 0:
		execl("socket2a", "socket2a", "fork okay", buf, (char *)NULL);
		DIE(errno, "execl");
	default:
		break;
	}
	CHECKr(pthread_mutex_unlock(&waiter_mutex));
	pthread_yield();

	a_sin_size = sizeof(a_sin);
	CHECKe(fd = accept(a_fd, &a_sin, &a_sin_size));
	CHECKe(close(fd)); 

	sleep(1);

	printf("%d: This should be message #4\n", getpid());

	a_sin_size = sizeof(a_sin);
	memset(&a_sin, 0, sizeof(a_sin));
	CHECKe(fd = accept(a_fd, &a_sin, &a_sin_size));

	/* Setup a write thread */

	CHECKr(pthread_create(&thread, NULL, sock_write, &fd));
	CHECKe(read(fd, buf, 1024));

	printf("%d: %s\n", getpid(), buf); /* message 6 */

	CHECKe(close(fd));

	if (pthread_mutex_trylock(&waiter_mutex) == EBUSY) {
		sleep(2);
		if (pthread_mutex_trylock(&waiter_mutex) == EBUSY) {
			/* forcibly kill child */
			CHECKe(kill(pid, SIGKILL));
			PANIC("child %d took too long to exit", pid);
		}
	}
	CHECKr(pthread_join(wthread, NULL));

	return(NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t thread;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	CHECKr(pthread_create(&thread, NULL, sock_accept, 
	    (void *)0xdeadbeaf));

	CHECKr(pthread_join(thread, NULL));

	SUCCEED;
}

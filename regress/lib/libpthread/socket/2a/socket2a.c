/*	$OpenBSD: socket2a.c,v 1.5 2015/11/04 21:29:20 tedu Exp $	*/
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
#include <string.h>
#include <stdlib.h>
#include "test.h"

struct sockaddr_in a_sout;

#define MESSAGE5 "This should be message #5"
#define MESSAGE6 "This should be message #6"

static void * 
sock_connect(void *arg)
{
	char buf[1024];
	int fd;
	short port;

	port = atoi(arg);
 	a_sout.sin_family = AF_INET;
 	a_sout.sin_port = htons(port);
	a_sout.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* loopback */

	CHECKe(fd = socket(AF_INET, SOCK_STREAM, 0));

	printf("%d: This should be message #2\n", getpid());

	CHECKe(connect(fd, (struct sockaddr *) &a_sout, sizeof(a_sout))); 
	CHECKe(close(fd)); 
		
	CHECKe(fd = socket(AF_INET, SOCK_STREAM, 0));

	printf("%d: This should be message #3\n", getpid());

	CHECKe(connect(fd, (struct sockaddr *) &a_sout, sizeof(a_sout)));

	/* Ensure sock_read runs again */

	CHECKe(read(fd, buf, 1024));
	CHECKe(write(fd, MESSAGE6, sizeof(MESSAGE6)));

	printf("%d: %s\n", getpid(), buf);

	CHECKe(close(fd));
	return (NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t thread;

	if (argc == 3 && (!strcmp(argv[1], "fork okay"))) {
		sleep(1);
		setvbuf(stdout, NULL, _IONBF, 0);
		setvbuf(stderr, NULL, _IONBF, 0);

		CHECKr(pthread_create(&thread, NULL, sock_connect, 
		    (void *)argv[2]));
		CHECKr(pthread_join(thread, NULL));
		SUCCEED;
	} else {
		fprintf(stderr, "test_sock_2a needs to be exec'ed from "
		    "test_sock_2.\n");
		fprintf(stderr, "It is not a stand alone test.\n");
		PANIC("usage");
	}
}

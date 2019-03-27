/*-
 * Copyright (c) 2009 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * When a multi-threaded application calls fork(2) from one thread while
 * another thread is blocked in accept(2), we prefer that the file descriptor
 * to be returned by accept(2) not appear in the child process.  Test this by
 * creating a thread blocked in accept(2), then forking a child and seeing if
 * the fd it would have returned is defined in the child or not.
 */

#include <sys/socket.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PORT
#define	PORT	9000
#endif

static int listen_fd;

static void *
do_accept(__unused void *arg)
{
	int accept_fd;

	accept_fd = accept(listen_fd, NULL, NULL);
	if (accept_fd < 0)
		err(1, "accept");

	close(accept_fd);
	return (NULL);
}

static void
do_fork(void)
{
	int pid;

	pid = fork();
	if (pid < 0)
		err(1, "fork");
	if (pid > 0) {
		waitpid(pid, NULL, 0);
		exit(0);
	}

	/*
	 * We will call ftruncate(2) on the next available file descriptor,
	 * listen_fd+1, and get back EBADF if it's not a valid descriptor,
	 * and EINVAL if it is.  This (currently) works fine in practice.
	 */
	if (ftruncate(listen_fd + 1, 0) < 0) {
		if (errno == EBADF)
			exit(0);
		else if (errno == EINVAL)
			errx(1, "file descriptor still open in child");
		else
			err(1, "unexpected error");
	} else
		errx(1, "ftruncate succeeded");
}

int
main(void)
{
	struct sockaddr_in sin;
	pthread_t accept_thread;

	listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0)
		err(1, "socket");
	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(PORT);
	if (bind(listen_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "bind");
	if (listen(listen_fd, -1) <0)
		err(1, "listen");
	if (pthread_create(&accept_thread, NULL, do_accept, NULL) != 0)
		err(1, "pthread_create");
	sleep(1);	/* Easier than using a CV. */
	do_fork();
	exit(0);
}

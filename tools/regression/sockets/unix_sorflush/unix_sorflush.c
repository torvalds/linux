/*-
 * Copyright (c) 2008 Robert N. M. Watson
 * All rights reserved.
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
 */

/*
 * Reproduce a race in which:
 *
 * - Process (a) is blocked in read on a socket waiting on data.
 * - Process (b) is blocked in shutdown() on a socket waiting on (a).
 * - Process (c) delivers a signal to (b) interrupting its wait.
 *
 * This race is premised on shutdown() not interrupting (a) properly, and the
 * signal to (b) causing problems in the kernel.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/socket.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
receive_and_exit(int s)
{
	ssize_t ssize;
	char ch;

	ssize = recv(s, &ch, sizeof(ch), 0);
	if (ssize < 0)
		err(-1, "receive_and_exit: recv");
	exit(0);
}

static void
shutdown_and_exit(int s)
{

	if (shutdown(s, SHUT_RD) < 0)
		err(-1, "shutdown_and_exit: shutdown");
	exit(0);
}

int
main(void)
{
	pid_t pida, pidb;
	int sv[2];

	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sv) < 0)
		err(-1, "socketpair");

	pida = fork();
	if (pida < 0)
		err(-1, "fork");
	if (pida == 0)
		receive_and_exit(sv[1]);
	sleep(1);
	pidb = fork();
	if (pidb < 0) {
		warn("fork");
		(void)kill(pida, SIGKILL);
		exit(-1);
	}
	if (pidb == 0)
		shutdown_and_exit(sv[1]);
	sleep(1);
	if (kill(pidb, SIGKILL) < 0)
		err(-1, "kill");
	sleep(1);
	printf("ok 1 - unix_sorflush\n");
	exit(0);
}

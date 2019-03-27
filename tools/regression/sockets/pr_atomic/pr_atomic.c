/*-
 * Copyright (c) 2006 Bruce M. Simpson
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
 *
 * $FreeBSD$
 */

/*
 * Regression test for uiomove in kernel; specifically for PR kern/38495.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>

static char socket_path[] = "tmp.XXXXXX";

static jmp_buf myjmpbuf;

static void handle_sigalrm(int signo __unused)
{
	longjmp(myjmpbuf, 1);
}

int
main(void)
{
	struct sockaddr_un un;
	pid_t pid;
	int s;

	if (mkstemp(socket_path) == -1)
		err(1, "mkstemp");
	s = socket(PF_LOCAL, SOCK_DGRAM, 0);
	if (s == -1)
		errx(-1, "socket");
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_LOCAL;
	unlink(socket_path);
	strcpy(un.sun_path, socket_path);
	if (bind(s, (struct sockaddr *)&un, sizeof(un)) == -1)
		errx(-1, "bind");
	pid = fork();
	if (pid == -1)
		errx(-1, "fork");
	if (pid == 0) {
		int conn;
		char buf[] = "AAAAAAAAA";

		close(s);
		conn = socket(AF_LOCAL, SOCK_DGRAM, 0);
		if (conn == -1)
			errx(-1,"socket");
		if (sendto(conn, buf, sizeof(buf), 0, (struct sockaddr *)&un,
		    sizeof(un)) != sizeof(buf))
			errx(-1,"sendto");
		close(conn);
		_exit(0);
	}

	sleep(5);

	/* Make sure the data is there when we try to receive it. */
	if (recvfrom(s, (void *)-1, 1, 0, NULL, NULL) != -1)
		errx(-1,"recvfrom succeeded when failure expected");

	(void)signal(SIGALRM, handle_sigalrm);
	if (setjmp(myjmpbuf) == 0) {
		/*
	 	 * This recvfrom will panic an unpatched system, and block
		 * a patched one.
		 */
		alarm(5);
		(void)recvfrom(s, (void *)-1, 1, 0, NULL, NULL);
	}

	/* We should reach here via longjmp() and all should be well. */

	return (0);
}

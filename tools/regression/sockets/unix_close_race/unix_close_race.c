/*-
 * Copyright (c) 2010 Mikolaj Golub
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
 * This regression test attempts to trigger a race that occurs when both
 * endpoints of a connected UNIX domain socket are closed at once.  The two
 * close paths may run concurrently leading to a call to sodisconnect() on an
 * already-closed socket in kernel.  Before it was fixed, this might lead to
 * ENOTCONN being returned improperly from close().
 *
 * This race is fairly timing-dependent, so it effectively requires SMP, and
 * may not even trigger then.
 */

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

static char socket_path[] = "tmp.XXXXXXXX";

#define	USLEEP	100
#define	LOOPS	100000

int
main(void)
{
	struct sockaddr_un servaddr;
	int listenfd, connfd, pid;
	u_int counter, ncpus;
	size_t len;

	len = sizeof(ncpus);
	if (sysctlbyname("kern.smp.cpus", &ncpus, &len, NULL, 0) < 0)
		err(1, "kern.smp.cpus");
	if (len != sizeof(ncpus))
		errx(1, "kern.smp.cpus: invalid length");
	if (ncpus < 2)
		warnx("SMP not present, test may be unable to trigger race");

	if (mkstemp(socket_path) == -1)
		err(1, "mkstemp failed");
	unlink(socket_path);

	/*
	 * Create a UNIX domain socket that the child will repeatedly
	 * accept() from, and that the parent will repeatedly connect() to.
	 */
	if ((listenfd = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0)
		err(1, "parent: socket error");
	(void)unlink(socket_path);
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sun_family = AF_LOCAL;
	strcpy(servaddr.sun_path, socket_path);
	if (bind(listenfd, (struct sockaddr *) &servaddr,
	    sizeof(servaddr)) < 0)
		err(1, "parent: bind error");
	if (listen(listenfd, 1024) < 0)
		err(1, "parent: listen error");

	pid = fork();
	if (pid == -1)
		err(1, "fork()");
	if (pid != 0) {
		/*
		 * In the parent, repeatedly connect and disconnect from the
		 * socket, attempting to induce the race.
		 */
		close(listenfd);
		sleep(1);
		bzero(&servaddr, sizeof(servaddr));
		servaddr.sun_family = AF_LOCAL;
		strcpy(servaddr.sun_path, socket_path);
		for (counter = 0; counter < LOOPS; counter++) {
			if ((connfd = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0) {
				(void)kill(pid, SIGTERM);
				err(1, "parent: socket error");
			}
			if (connect(connfd, (struct sockaddr *)&servaddr,
			    sizeof(servaddr)) < 0) {
			    	(void)kill(pid, SIGTERM);
				err(1, "parent: connect error");
			}
			if (close(connfd) < 0) {
				(void)kill(pid, SIGTERM);
				err(1, "parent: close error");
			}
			usleep(USLEEP);
		}
		(void)kill(pid, SIGTERM);
	} else {
		/*
		 * In the child, loop accepting and closing.  We may pick up
		 * the race here so report errors from close().
		 */
		for ( ; ; ) {
			if ((connfd = accept(listenfd,
			    (struct sockaddr *)NULL, NULL)) < 0)
				err(1, "child: accept error");
			if (close(connfd) < 0)
				err(1, "child: close error");
		}
	}
	printf("OK\n");
	exit(0);
}

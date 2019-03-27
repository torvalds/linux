/*-
 * Copyright (c) 2005 Robert N. M. Watson
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

#include <sys/param.h>
#include <sys/jail.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * A bug in the jail(8) code prevented processes in jail from properly
 * connecting UDP sockets.  This test program attempts to exercise that bug.
 */

static void
usage(void)
{

	fprintf(stderr, "udpconnectjail: no arguments\n");
	exit(-1);
}

static void
test(const char *context, struct sockaddr_in *sin)
{
	int sock;

	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		errx(-1, "%s: socket(PF_INET, SOCK_DGRAM, 0): %s", context,
		    strerror(errno));

	if (connect(sock, (struct sockaddr *)sin, sizeof(*sin)) < 0)
		errx(-1, "%s: connect(%s): %s", context,
		    inet_ntoa(sin->sin_addr), strerror(errno));

	if (close(sock) < 0)
		errx(-1, "%s: close(): %s", context, strerror(errno));
}

int
main(int argc, __unused char *argv[])
{
	struct sockaddr_in sin;
	struct jail thejail;
	struct in_addr ia4;

	if (argc != 1)
		usage();

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(8080);	/* Arbitrary */

	/*
	 * First run the system call test outside of a jail.
	 */
	test("not in jail", &sin);

	/*
	 * Now re-run in a jail.
	 * XXX-BZ should switch to jail_set(2).
	 */
	ia4.s_addr = htonl(INADDR_LOOPBACK);

	bzero(&thejail, sizeof(thejail));
	thejail.version = JAIL_API_VERSION;
	thejail.path = "/";
	thejail.hostname = "jail";
	thejail.jailname = "udpconnectjail";
	thejail.ip4s = 1;
	thejail.ip4 = &ia4;
	
	if (jail(&thejail) < 0)
		errx(-1, "jail: %s", strerror(errno));
	test("in jail", &sin);

	fprintf(stdout, "PASS\n");

	return (0);
}

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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * The UDP code allows transmitting zero-byte datagrams, but are they
 * received?
 */

#define	THEPORT	9543		/* Arbitrary. */

static void
usage(void)
{

	errx(-1, "no arguments allowed\n");
}

static void
test(int domain, const char *domainstr, struct sockaddr *sa, socklen_t salen)
{
	int sock_send, sock_receive;
	ssize_t size;

	sock_send = socket(domain, SOCK_DGRAM, 0);
	if (sock_send < 0)
		err(-1, "socket(%s, SOCK_DGRAM, 0)", domainstr);

	sock_receive = socket(domain, SOCK_DGRAM, 0);
	if (sock_receive < 0)
		err(-1, "socket(%s, SOCK_DGRAM, 0)", domainstr);

	if (bind(sock_receive, sa, salen) < 0)
		err(-1, "Protocol %s bind(sock_receive)", domainstr);
	if (fcntl(sock_receive, F_SETFL, O_NONBLOCK, 1) < 0)
		err(-1, "Protocll %s fcntl(sock_receive, FL_SETFL, "
		    "O_NONBLOCK)", domainstr);

	if (connect(sock_send, sa, salen) < 0)
		err(-1, "Protocol %s connect(sock_send)", domainstr);

	size = recv(sock_receive, NULL, 0, 0);
	if (size > 0)
		errx(-1, "Protocol %s recv(sock_receive, NULL, 0) before: %zd",
		    domainstr, size);
	else if (size < 0)
		err(-1, "Protocol %s recv(sock_receive, NULL, 0) before",
		    domainstr);

	size = send(sock_send, NULL, 0, 0);
	if (size < 0)
		err(-1, "Protocol %s send(sock_send, NULL, 0)", domainstr);

	(void)sleep(1);
	size = recv(sock_receive, NULL, 0, 0);
	if (size < 0)
		err(-1, "Protocol %s recv(sock_receive, NULL, 0) test",
		    domainstr);

	size = recv(sock_receive, NULL, 0, 0);
	if (size > 0)
		errx(-1, "Protocol %s recv(sock_receive, NULL, 0) after: %zd",
		    domainstr, size);
	else if (size < 0)
		err(-1, "Protocol %s recv(sock_receive, NULL, 0) after",
		    domainstr);
}

int
main(int argc, __unused char *argv[])
{
	struct sockaddr_un sun;
	struct sockaddr_in6 sin6;
	struct sockaddr_in sin;
	struct in6_addr loopback6addr = IN6ADDR_LOOPBACK_INIT;

	if (argc != 1)
		usage();

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(THEPORT);

	test(PF_INET, "PF_INET", (struct sockaddr *)&sin, sizeof(sin));

	bzero(&sin6, sizeof(sin6));
	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = loopback6addr;
	sin6.sin6_port = htons(THEPORT);

	test(PF_INET6, "PF_INET6", (struct sockaddr *)&sin6, sizeof(sin6));

	bzero(&sun, sizeof(sun));
	sun.sun_len = sizeof(sun);
	sun.sun_family = AF_LOCAL;
	strlcpy(sun.sun_path, "/tmp/udpzerosize-socket", sizeof(sun.sun_path));
	if (unlink(sun.sun_path) < 0 && errno != ENOENT)
		err(-1, "unlink: %s", sun.sun_path);

	test(PF_LOCAL, "PF_LOCAL", (struct sockaddr *)&sun, sizeof(sun));

	return (0);
}

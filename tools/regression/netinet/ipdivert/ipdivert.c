/*-
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract
 * to Juniper Networks, Inc.
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
 * This is a test tool for IP divert sockets.  For the time being, it just
 * exercise creation and binding of sockets, rather than their divert
 * behaviour.  It would be highly desirable to broaden this test tool to
 * include packet injection and diversion.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
ok(const char *test)
{

	fprintf(stderr, "%s: OK\n", test);
}

static void
fail(const char *test, const char *note)
{

	fprintf(stderr, "%s - %s: FAIL (%s)\n", test, note, strerror(errno));
	exit(1);
}

static void
failx(const char *test, const char *note)
{

	fprintf(stderr, "%s - %s: FAIL\n", test, note);
	exit(1);
}

static int
ipdivert_create(const char *test)
{
	int s;

	s = socket(PF_INET, SOCK_RAW, IPPROTO_DIVERT);
	if (s < 0)
		fail(test, "socket");
	return (s);
}

static void
ipdivert_close(const char *test, int s)
{

	if (close(s) < 0)
		fail(test, "close");
}

static void
ipdivert_bind(const char *test, int s, u_short port, int expect)
{
	struct sockaddr_in sin;
	int err;

	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(port);

	err = bind(s, (struct sockaddr *)&sin, sizeof(sin));
	if (err < 0) {
		if (expect == 0)
			fail(test, "bind");
		if (errno != expect)
			fail(test, "bind");
	} else {
		if (expect != 0)
			failx(test, "bind");
	}
}

int
main(int argc, char *argv[])
{
	const char *test;
	int s1, s2;

	/*
	 * First test: create and close an IP divert socket.
	 */
	test = "create_close";
	s1 = ipdivert_create(test);
	ipdivert_close(test, s1);
	ok(test);

	/*
	 * Second test: create, bind, and close an IP divert socket.
	 */
	test = "create_bind_close";
	s1 = ipdivert_create(test);
	ipdivert_bind(test, s1, 1000, 0);
	ipdivert_close(test, s1);
	ok(test);

	/*
	 * Third test: create two sockets, bind to different ports, and close.
	 * This should succeed due to non-conflict on the port numbers.
	 */
	test = "create2_bind2_close2";
	s1 = ipdivert_create(test);
	s2 = ipdivert_create(test);
	ipdivert_bind(test, s1, 1000, 0);
	ipdivert_bind(test, s2, 1001, 0);
	ipdivert_close(test, s1);
	ipdivert_close(test, s2);
	ok(test);

	/*
	 * Fourth test: create two sockets, bind to the *same* port, and
	 * close.  This should fail due to conflicting port numbers.
	 */
	test = "create2_bind2_conflict_close2";
	s1 = ipdivert_create(test);
	s2 = ipdivert_create(test);
	ipdivert_bind(test, s1, 1000, 0);
	ipdivert_bind(test, s2, 1000, EADDRINUSE);
	ipdivert_close(test, s1);
	ipdivert_close(test, s2);
	ok(test);

	return (0);
}

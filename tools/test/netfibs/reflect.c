/*-
 * Copyright (c) 2012 Cisco Systems, Inc.
 * All rights reserved.
 *
 * This software was developed by Bjoern Zeeb under contract to
 * Cisco Systems, Inc..
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

#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>

#include <netinet/in.h>


#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

static char *testcase;
static int accepts;
static int debug;
static u_int fib = -1;
static u_int reflectfib = -1;
static uint16_t port = 6666;
static char *addr;
static int nostart;

static int
reflect_conn(int s, char *buf, size_t buflen, ssize_t l, struct sockaddr *sa,
    socklen_t salen)
{
	ssize_t m;

	if (l == -1)
		err(EX_OSERR, "read()");
	if (l == 0)
		errx(EX_NOINPUT, "EOF");
	if ((size_t)l > (buflen - 1))
		errx(EX_DATAERR, "Input too long");
	/* Nuke the \n from echo | netcat. */
	buf[l-1] = '\0';

	/*
	 * Match three cases: (1) START, (2) DONE, (3) anything else.
	 * For anything but START and DONE we just reflect everything.
	 */
	/*
	 * We expected a "START testcase" on first connect.  Otherwise it means
	 * that we are out of sync.  Exit to not produce weird results.
	 */
	if (accepts == 0 && nostart == 0) {
		if (strncmp(buf, "START ", 6) != 0)
			errx(EX_PROTOCOL, "Not received START on first "
			    "connect: %s", buf);
		if (l < 8)
			errx(EX_PROTOCOL, "START without test case name: %s",
			    buf);
		if (strcmp(buf+6, testcase) != 0)
			errx(EX_PROTOCOL, "START test case does not match "
			    "'%s': '%s'", testcase, buf+6);
	}
	/* If debug is on, log. */
	if (debug > 0)
		fprintf(stderr, "<< %s: %s\n", testcase, buf);

	if (reflectfib != (u_int)-1)
		l = snprintf(buf, buflen, "FIB %u\n", reflectfib);

	/* If debug is on, log. */
	if (debug > 0) {
		buf[l-1] = '\0';
		fprintf(stderr, ">> %s: %s\n", testcase, buf);
	}

	/* Reflect data with \n again. */
	buf[l-1] = '\n';

	if (sa != NULL) {
		m = sendto(s, buf, l, 0, sa, salen);
	} else
		m = write(s, buf, l);
	/* XXX This is simplified handling. */
	if (m == -1 && sa != NULL && errno == EHOSTUNREACH)
		warn("ignored expected: sendto(%s, %zd)", buf, l);
	else if (m == -1 && (sa == NULL || errno != EHOSTUNREACH))
		err(EX_OSERR, "write(%s, %zd)", buf, l);
	else if (m != l)
		err(EX_OSERR, "short write(%s, %zd) %zd", buf, l, m);


	accepts++;
	
	/* See if we got an end signal. */
	if (strncmp(buf, "DONE", 4) == 0)
		return (-2);
	return (0);
}

static int
reflect_tcp6_conn(int as)
{
	char buf[1500];
	ssize_t l;
	int error, s;

	s = accept(as, NULL, NULL);
	if (s == -1)
		err(EX_OSERR, "accept()");

	l = read(s, buf, sizeof(buf));
	error = reflect_conn(s, buf, sizeof(buf), l, NULL, 0);
	close(s);

	return (error);
}

static int
reflect_udp6_conn(int s)
{
	char buf[1500];
	struct sockaddr_in6 from;
	socklen_t fromlen;
	ssize_t l;
	int error;

	fromlen = sizeof(from);
	l = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&from,
	    &fromlen);
#if 0
	if (l != -1) {
		rc = connect(s, (struct sockaddr *)&from, fromlen);
		if (rc == -1) {
			if (inet_ntop(PF_INET6, &from, buf, sizeof(buf)) == NULL)
				buf[0] = '\0';
			err(EX_OSERR, "connect(%d, %s, %u)", s, buf, fromlen);
		}
	}
#endif
	error = reflect_conn(s, buf, sizeof(buf), l, (struct sockaddr *)&from,
	    fromlen);
#if 0
	if (l != -1) {
		/* Undo the connect binding again. */
		fromlen = sizeof(from);
		bzero(&from, fromlen);
		from.sin6_len = fromlen;
		from.sin6_family = AF_INET6;
		from.sin6_port = htons(port);	/* This only gives us a ::1:port ::1:port binding */
		rc = connect(s, (struct sockaddr *)&from, fromlen);
		if (rc == -1) {
			if (inet_ntop(PF_INET6, &from.sin6_addr, buf,
			    sizeof(buf)) == NULL)
				buf[0] = '\0';
			err(EX_OSERR, "un-connect(%d, %s, %u)", s, buf, fromlen);
		}
	}
#endif

	return (error);
}

static int
reflect_6(int domain, int type)
{
	struct sockaddr_in6 sin6;
	fd_set rset;
	int i, rc, s;

	/* Get us a listen socket. */
	s = socket(domain, type, 0);
	if (s == -1)
		err(EX_OSERR, "socket()");

	/*
	 * In case a FIB was given on cmd line, set it.  Let the kernel do the
	 * the bounds check.
	 */
	if (fib != (u_int)-1) {
		rc = setsockopt(s, SOL_SOCKET, SO_SETFIB, &fib, sizeof(fib));
		if (rc == -1)
			err(EX_OSERR, "setsockopt(SO_SETFIB)");
	}

	/* Allow re-use. Otherwise restarting for the next test might error. */
	i = 1;
	rc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
	if (rc == -1)
		err(EX_OSERR, "setsockopt(SO_REUSEADDR)");
	i = 1;
	rc = setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &i, sizeof(i));
	if (rc == -1)
		err(EX_OSERR, "setsockopt(SO_REUSEPORT)");

	/* Bind address and port or just port. */
	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = htons(port);
	sin6.sin6_flowinfo = 0;
	bzero(&sin6.sin6_addr, sizeof(sin6.sin6_addr));
	if (addr != NULL) {
		rc = inet_pton(PF_INET6, addr, &sin6.sin6_addr);
		if (rc == 0)
			errx(EX_USAGE, "inet_pton()");
		else if (rc == -1)
			err(EX_OSERR, "inet_pton()");
		else if (rc != 1)
			errx(EX_SOFTWARE, "inet_pton()");
	}
	sin6.sin6_scope_id = 0;
	rc = bind(s, (struct sockaddr *)&sin6, sizeof(sin6));
	if (rc == -1)
		err(EX_OSERR, "bind(%d)", s);
	
	if (type == SOCK_STREAM) {
		rc = listen(s, port);
		if (rc == -1)
			err(EX_OSERR, "listen(%d, %u)", s, port);
	}

	/*
	 * We shall never do more than one connection in parallel so can keep
	 * it simple.
	 */
	do {
		FD_ZERO(&rset);
		FD_SET(s, &rset);
		rc = select(s + 1, &rset, NULL, NULL, NULL);
		if (rc == -1 && errno != EINTR)
			err(EX_OSERR, "select()");

		if (rc == 0 || errno == EINTR)	
			continue;

		if (rc != 1)
			errx(EX_OSERR, "select() miscounted 1 to %d", rc);
		if (!FD_ISSET(s, &rset))
			errx(EX_OSERR, "select() did not return our socket");

		if (type == SOCK_STREAM)
			rc = reflect_tcp6_conn(s);
		else if (type == SOCK_DGRAM)
			rc = reflect_udp6_conn(s);
		else
			errx(EX_SOFTWARE, "Unsupported socket type %d", type);
	} while (rc == 0);
	/* Turn end flagging into no error. */
	if (rc == -2)
		rc = 0;

	/* Close listen socket. */
	close(s);

	return (rc);
}

static int
reflect_tcp6(void)
{

	return (reflect_6(PF_INET6, SOCK_STREAM));
}

static int
reflect_udp6(void)
{

	return (reflect_6(PF_INET6, SOCK_DGRAM));
}

int
main(int argc, char *argv[])
{
	long long l;
	char *dummy, *afname;
	int ch, rc;

	afname = NULL;
	while ((ch = getopt(argc, argv, "A:dF:f:Np:t:T:")) != -1) {
		switch (ch) {
		case 'A':
			addr = optarg;
			break;
		case 'd':
			debug++;
			break;
		case 'F':
			l = strtoll(optarg, &dummy, 10);
			if (*dummy != '\0' || l < 0)
				errx(EX_USAGE, "Invalid FIB number");
			fib = (u_int)l;
			break;
		case 'f':
			l = strtoll(optarg, &dummy, 10);
			if (*dummy != '\0' || l < 0)
				errx(EX_USAGE, "Invalid FIB number");
			reflectfib = (u_int)l;
			break;
		case 'N':
			nostart=1;
			break;
		case 'p':
			l = strtoll(optarg, &dummy, 10);
			if (*dummy != '\0' || l < 0)
				errx(EX_USAGE, "Invalid port number");
			port = (uint16_t)l;
			break;
		case 't':
			testcase = optarg;
			break;
		case 'T':
			afname = optarg;
			break;
		case '?':
		default:
			errx(EX_USAGE, "Unknown command line option at '%c'",
			    optopt);
			/* NOTREACHED */
		}
	}

	if (testcase == NULL)
		errx(EX_USAGE, "Mandatory option -t <testcase> not given");
	if (afname == NULL)
		errx(EX_USAGE, "Mandatory option -T <afname> not given");

	if (strcmp(afname, "TCP6") == 0)
		rc = reflect_tcp6();
	else if (strcmp(afname, "UDP6") == 0)
		rc = reflect_udp6();
	else
		errx(EX_USAGE, "Mandatory option -T %s not a valid option",
		    afname);

	return (rc);
}

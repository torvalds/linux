/*	$OpenBSD: relay.c,v 1.2 2014/01/08 23:32:17 bluhm Exp $ */
/*
 * Copyright (c) 2013 Alexander Bluhm <bluhm@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Accept tcp or udp from client and connect to server.
 * Then copy or splice data from client to server.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	BUFSIZE		(1<<16)

__dead void	usage(void);
void		relay_copy(int, int);
void		relay_splice(int, int);
int		socket_listen(int *, struct addrinfo *, const char *,
		    const char *);
int		listen_select(const int *, int);
int		socket_accept(int);
int		socket_connect(struct addrinfo *, const char *, const char *);

__dead void
usage(void)
{
	fprintf(stderr,
	    "usage: relay copy|splice [-46tu] [-b bindaddress] listenport "
	    "hostname port\n"
	    "       copy [-46tu] [-b bindaddress] listenport hostname port\n"
	    "       splice [-46tu] [-b bindaddress] listenport hostname port\n"
	    "           -4              IPv4 only\n"
	    "           -6              IPv6 only\n"
	    "           -b bindaddress  bind listen socket to address\n"
	    "           -t              TCP (default)\n"
	    "           -u              UDP\n"
	    );
	exit(1);
}

void
relay_copy(int fdin, int fdout)
{
	char buf[BUFSIZE];
	off_t len;
	size_t off;
	ssize_t nr, nw;

	printf("copy...\n");
	len = 0;
	while (1) {
		nr = read(fdin, buf, sizeof(buf));
		if (nr == -1)
			err(1, "read");
		if (nr == 0)
			break;
		len += nr;
		off = 0;
		do {
			nw = write(fdout, buf + off, nr);
			if (nw == -1)
				err(1, "write");
			off += nw;
			nr -= nw;
		} while (nr);
	}
	printf("len %lld\n", len);
}

void
relay_splice(int fdin, int fdout)
{
	fd_set fdset;
	socklen_t optlen;
	off_t len;
	int error;

	printf("splice...\n");
	if (setsockopt(fdin, SOL_SOCKET, SO_SPLICE, &fdout, sizeof(int)) == -1)
		err(1, "setsockopt splice");
	FD_ZERO(&fdset);
	FD_SET(fdin, &fdset);
	if (select(fdin+1, &fdset, NULL, NULL, NULL) == -1)
		err(1, "select");
	optlen = sizeof(error);
	if (getsockopt(fdin, SOL_SOCKET, SO_ERROR, &error, &optlen) == -1)
		err(1, "getsockopt error");
	if (error)
		printf("error %s\n", strerror(error));
	optlen = sizeof(len);
	if (getsockopt(fdin, SOL_SOCKET, SO_SPLICE, &len, &optlen) == -1)
		err(1, "getsockopt splice");
	printf("len %lld\n", len);
}

int
socket_listen(int *ls, struct addrinfo *hints, const char *listenaddr,
    const char *listenport)
{
	char host[NI_MAXHOST], serv[NI_MAXSERV];
	struct sockaddr_storage sa;
	socklen_t salen;
	struct addrinfo *res, *res0;
	const char *cause = NULL;
	int optval, error, save_errno, nls;

	hints->ai_flags = AI_PASSIVE;
	error = getaddrinfo(listenaddr, listenport, hints, &res0);
	if (error)
		errx(1, "getaddrinfo %s %s: %s", listenaddr == NULL ? "*" :
		    listenaddr, listenport, gai_strerror(error));
	for (res = res0, nls = 0; res && nls < FD_SETSIZE; res = res->ai_next) {
		ls[nls] = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
		if (ls[nls] == -1) {
			cause = "listen socket";
			continue;
		}
		optval = 100000;
		if (setsockopt(ls[nls], SOL_SOCKET, SO_RCVBUF,
		    &optval, sizeof(optval)) == -1)
			err(1, "setsockopt rcvbuf");
		optval = 1;
		if (setsockopt(ls[nls], SOL_SOCKET, SO_REUSEADDR,
		    &optval, sizeof(optval)) == -1)
			err(1, "setsockopt reuseaddr");
		if (bind(ls[nls], res->ai_addr, res->ai_addrlen) == -1) {
			cause = "bind";
			save_errno = errno;
			close(ls[nls]);
			errno = save_errno;
			continue;
		}
		if (hints->ai_socktype == SOCK_STREAM) {
			if (listen(ls[nls], 5) == -1)
				err(1, "listen");
		}
		salen = sizeof(sa);
		if (getsockname(ls[nls], (struct sockaddr *)&sa, &salen) == -1)
			err(1, "listen getsockname");
		error = getnameinfo((struct sockaddr *)&sa, salen,
		    host, sizeof(host), serv, sizeof(serv),
		    NI_NUMERICHOST|NI_NUMERICSERV);
		if (error)
			errx(1, "listen getnameinfo: %s", gai_strerror(error));
		printf("listen %s %s\n", host, serv);
		nls++;
	}
	if (nls == 0)
		err(1, "%s", cause);
	freeaddrinfo(res0);

	return nls;
}

int
listen_select(const int *ls, int nls)
{
	fd_set fdset;
	int i, mfd;

	FD_ZERO(&fdset);
	mfd = 0;
	for (i = 0; i < nls; i++) {
		FD_SET(ls[i], &fdset);
		if (ls[i] > mfd)
			mfd = ls[i];
	}
	if (select(mfd+1, &fdset, NULL, NULL, NULL) == -1)
		err(1, "select");
	for (i = 0; i < nls; i++) {
		if (FD_ISSET(ls[i], &fdset))
			break;
	}
	if (i == nls)
		errx(1, "select: no fd set");
	return ls[i];
}

int
socket_accept(int ls)
{
	char host[NI_MAXHOST], serv[NI_MAXSERV];
	struct sockaddr_storage sa;
	socklen_t salen;
	int error, as;

	salen = sizeof(sa);
	as = accept(ls, (struct sockaddr *)&sa, &salen);
	if (as == -1)
		err(1, "accept");
	error = getnameinfo((struct sockaddr *)&sa, salen,
	    host, sizeof(host), serv, sizeof(serv),
	    NI_NUMERICHOST|NI_NUMERICSERV);
	if (error)
		errx(1, "accept getnameinfo: %s", gai_strerror(error));
	printf("accept %s %s\n", host, serv);

	return as;
}

int
socket_connect(struct addrinfo *hints, const char *hostname, const char *port)
{
	char host[NI_MAXHOST], serv[NI_MAXSERV];
	struct sockaddr_storage sa;
	socklen_t salen;
	struct addrinfo *res, *res0;
	const char *cause = NULL;
	int optval, error, save_errno, cs;

	hints->ai_flags = 0;
	error = getaddrinfo(hostname, port, hints, &res0);
	if (error)
		errx(1, "getaddrinfo %s %s: %s", hostname, port,
		    gai_strerror(error));
	cs = -1;
	for (res = res0; res; res = res->ai_next) {
		cs = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
		if (cs == -1) {
			cause = "connect socket";
			continue;
		}
		optval = 100000;
		if (setsockopt(cs, SOL_SOCKET, SO_SNDBUF,
		    &optval, sizeof(optval)) == -1)
			err(1, "setsockopt sndbuf");
		if (connect(cs, res->ai_addr, res->ai_addrlen) == -1) {
			cause = "connect";
			save_errno = errno;
			close(cs);
			errno = save_errno;
			cs = -1;
			continue;
		}
		break;
	}
	if (cs == -1)
		err(1, "%s", cause);
	salen = sizeof(sa);
	if (getpeername(cs, (struct sockaddr *)&sa, &salen) == -1)
		err(1, "connect getpeername");
	error = getnameinfo((struct sockaddr *)&sa, salen,
	    host, sizeof(host), serv, sizeof(serv),
	    NI_NUMERICHOST|NI_NUMERICSERV);
	if (error)
		errx(1, "connect getnameinfo: %s", gai_strerror(error));
	printf("connect %s %s\n", host, serv);
	freeaddrinfo(res0);

	return cs;
}

int
main(int argc, char *argv[])
{
	struct addrinfo hints;
	int ch, ls[FD_SETSIZE], nls, as, cs, optval;
	const char *listenaddr, *listenport, *hostname, *port;
	const char *relayname;
	void (*relayfunc)(int, int);

	relayname = strrchr(argv[0], '/');
	relayname = relayname ? relayname + 1 : argv[0];
	if (strcmp(relayname, "copy") == 0)
		relayfunc = relay_copy;
	else if (strcmp(relayname, "splice") == 0)
		relayfunc = relay_splice;
	else {
		argc--;
		argv++;
		if (argv[0] == NULL)
			usage();
		relayname = argv[0];
		if (strcmp(relayname, "copy") == 0)
			relayfunc = relay_copy;
		else if (strcmp(relayname, "splice") == 0)
			relayfunc = relay_splice;
		else
			usage();
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	listenaddr = NULL;
	while ((ch = getopt(argc, argv, "46b:tu")) != -1) {
		switch (ch) {
		case '4':
			hints.ai_family = PF_INET;
			break;
		case '6':
			hints.ai_family = PF_INET6;
			break;
		case 'b':
			listenaddr = optarg;
			break;
		case 't':
			hints.ai_socktype = SOCK_STREAM;
			break;
		case 'u':
			hints.ai_socktype = SOCK_DGRAM;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 3)
		usage();
	listenport = argv[0];
	hostname = argv[1];
	port = argv[2];

	nls = socket_listen(ls, &hints, listenaddr, listenport);

	while (1) {
		if (hints.ai_socktype == SOCK_STREAM) {
			as = socket_accept(listen_select(ls, nls));
			cs = socket_connect(&hints, hostname, port);
			optval = 1;
			if (setsockopt(cs, IPPROTO_TCP, TCP_NODELAY,
			    &optval, sizeof(optval)) == -1)
				err(1, "setsockopt nodelay");
		} else {
			cs = socket_connect(&hints, hostname, port);
			as = listen_select(ls, nls);
		}

		relayfunc(as, cs);

		if (close(cs) == -1)
			err(1, "connect close");
		if (hints.ai_socktype == SOCK_STREAM) {
			if (close(as) == -1)
				err(1, "accept close");
		}
		printf("close\n");
	}
}

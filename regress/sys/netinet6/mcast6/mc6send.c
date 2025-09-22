/*	$OpenBSD: mc6send.c,v 1.1.1.1 2019/09/05 01:50:34 bluhm Exp $	*/
/*
 * Copyright (c) 2019 Alexander Bluhm <bluhm@openbsd.org>
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

#include <sys/socket.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void __dead usage(void);

void __dead
usage(void)
{
	fprintf(stderr,
"mc6send [-f file] [-g group] [-i ifname] [-m message] [-p port]\n"
"    -f file         print message to log file, default stdout\n"
"    -g group        multicast group, default 224.0.0.123\n"
"    -i ifname       multicast interface address\n"
"    -m message      message in payload, maximum 255 characters, default foo\n"
"    -l loop         disable or enable loopback, 0 or 1\n"
"    -p port         destination port number, default 12345\n"
"    -t ttl          set multicast ttl\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in6 sin6;
	FILE *log;
	const char *errstr, *file, *group, *ifname, *msg;
	size_t len;
	ssize_t n;
	int ch, s, loop, port, ttl;
	unsigned int ifindex;

	log = stdout;
	file = NULL;
	group = "ff04::123";
	ifname = NULL;
	loop = -1;
	msg = "foo";
	port = 12345;
	ttl = -1;
	while ((ch = getopt(argc, argv, "f:g:i:l:m:p:t:")) != -1) {
		switch (ch) {
		case 'f':
			file = optarg;
			break;
		case 'g':
			group = optarg;
			break;
		case 'i':
			ifname = optarg;
			break;
		case 'l':
			loop = strtonum(optarg, 0, 1, &errstr);
			if (errstr != NULL)
				errx(1, "loop is %s: %s", errstr, optarg);
			break;
		case 'm':
			msg = optarg;
			break;
		case 'p':
			port = strtonum(optarg, 1, 0xffff, &errstr);
			if (errstr != NULL)
				errx(1, "port is %s: %s", errstr, optarg);
			break;
		case 't':
			ttl = strtonum(optarg, 0, 255, &errstr);
			if (errstr != NULL)
				errx(1, "ttl is %s: %s", errstr, optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc)
		usage();

	if (file != NULL) {
		log = fopen(file, "w");
		if (log == NULL)
			err(1, "fopen %s", file);
	}

	s = socket(AF_INET6, SOCK_DGRAM, 0);
	if (s == -1)
		err(1, "socket");
	if (ifname != NULL) {
		ifindex = if_nametoindex(ifname);
		if (ifindex == 0)
			err(1, "if_nametoindex %s", ifname);
		if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifindex,
		    sizeof(ifindex)) == -1)
			err(1, "setsockopt IPV6_MULTICAST_IF %s", ifname);
	}
	if (loop != -1) {
		if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop,
		    sizeof(loop)) == -1)
			err(1, "setsockopt loop %d", loop);
	}
	if (ttl != -1) {
		if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl,
		    sizeof(ttl)) == -1)
			err(1, "setsockopt ttl %d", ttl);
	}

	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = htons(port);
	if (inet_pton(AF_INET6, group, &sin6.sin6_addr) == -1)
		err(1, "inet_pton %s", group);
	if (ifname != NULL &&
	    (IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr) ||
	    IN6_IS_ADDR_MC_LINKLOCAL(&sin6.sin6_addr) ||
	    IN6_IS_ADDR_MC_INTFACELOCAL(&sin6.sin6_addr))) {
		sin6.sin6_scope_id = ifindex;
	}
	if (connect(s, (struct sockaddr *)&sin6, sizeof(sin6)) == -1)
		err(1, "connect [%s]:%d", group, port);

	len = strlen(msg);
	if (len >= 255)
		err(1, "message too long %zu", len);
	n = send(s, msg, len, 0);
	if (n == -1)
		err(1, "send");
	if ((size_t)n != len)
		errx(1, "send %zd", n);
	fprintf(log, ">>> %s\n", msg);
	fflush(log);

	return 0;
}

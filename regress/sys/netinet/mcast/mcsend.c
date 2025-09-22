/*	$OpenBSD: mcsend.c,v 1.2 2019/09/05 02:44:36 bluhm Exp $	*/
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
"mcsend [-f file] [-g group] [-i ifaddr] [-m message] [-p port]\n"
"    -f file         print message to log file, default stdout\n"
"    -g group        multicast group, default 224.0.0.123\n"
"    -i ifaddr       multicast interface address\n"
"    -m message      message in payload, maximum 255 characters, default foo\n"
"    -l loop         disable or enable loopback, 0 or 1\n"
"    -p port         destination port number, default 12345\n"
"    -t ttl          set multicast ttl\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	FILE *log;
	const char *errstr, *file, *group, *ifaddr, *msg;
	size_t len;
	ssize_t n;
	int ch, s, loop, port, ttl;

	log = stdout;
	file = NULL;
	group = "224.0.1.123";
	ifaddr = NULL;
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
			ifaddr = optarg;
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

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1)
		err(1, "socket");
	if (ifaddr != NULL) {
		struct in_addr addr;

		if (inet_pton(AF_INET, ifaddr, &addr) == -1)
			err(1, "inet_pton %s", ifaddr);
		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &addr,
		    sizeof(addr)) == -1)
			err(1, "setsockopt IP_MULTICAST_IF %s", ifaddr);
	}
	if (loop != -1) {
		unsigned char value = loop;

		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &value,
		    sizeof(value)) == -1)
			err(1, "setsockopt loop %d", loop);
	}
	if (ttl != -1) {
		unsigned char value = ttl;

		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &value,
		    sizeof(value)) == -1)
			err(1, "setsockopt ttl %d", ttl);
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	if (inet_pton(AF_INET, group, &sin.sin_addr) == -1)
		err(1, "inet_pton %s", group);
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == -1)
		err(1, "connect %s:%d", group, port);

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

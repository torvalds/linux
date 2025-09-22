/*	$OpenBSD: rip6cksum.c,v 1.1.1.1 2019/05/09 15:54:31 bluhm Exp $	*/
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

#include <errno.h>
#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>

void __dead usage(void);

void __dead
usage(void)
{
	fprintf(stderr, "rip6cksum [-ehw] [-c ckoff] [-r recvsz] [-s sendsz] "
	    "[-- scapy ...]\n"
	    "    -c ckoff   set checksum offset within rip header\n"
	    "    -e         expect error when setting ckoff\n"
	    "    -h         help, show usage\n"
	    "    -r recvsz  expected payload size from socket\n"
	    "    -s sendsz  send payload of given size to socket\n"
	    "    -w         wait for packet on socket, timeout 10 seconds\n"
	    "    scapy ...  run scapy program after socket setup\n"
	);
	exit(1);
}

const struct in6_addr loop6 = IN6ADDR_LOOPBACK_INIT;
int
main(int argc, char *argv[])
{
	int s, ch, eflag, cflag, rflag, sflag, wflag;
	int ckoff;
	size_t recvsz, sendsz;
	const char *errstr;
	struct sockaddr_in6 sin6;

	if (setvbuf(stdout, NULL, _IOLBF, 0) != 0)
		err(1, "setvbuf stdout line buffered");

	eflag = cflag = rflag = sflag = wflag = 0;
	while ((ch = getopt(argc, argv, "c:ehr:s:w")) != -1) {
		switch (ch) {
		case 'c':
			ckoff = strtonum(optarg, INT_MIN, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "ckoff is %s: %s", errstr, optarg);
			cflag = 1;
			break;
		case 'e':
			eflag = 1;
			break;
		case 'r':
			recvsz = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "recvsz is %s: %s", errstr, optarg);
			rflag = 1;
			break;
		case 's':
			sendsz = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "sendsz is %s: %s", errstr, optarg);
			sflag = 1;
			break;
		case 'w':
			wflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	printf("socket inet6 raw 255\n");
	s = socket(AF_INET6, SOCK_RAW, 255);
	if (s == -1)
		err(1, "socket raw");
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = loop6;
	printf("bind ::1\n");
	if (bind(s, (struct sockaddr *)&sin6, sizeof(sin6)) == -1)
		err(1, "bind ::1");
	printf("connect ::1\n");
	if (connect(s, (struct sockaddr *)&sin6, sizeof(sin6)) == -1)
		err(1, "connect ::1");

	if (cflag) {
		printf("setsockopt ipv6 checksum %d\n", ckoff);
		if (setsockopt(s, IPPROTO_IPV6, IPV6_CHECKSUM, &ckoff,
		     sizeof(ckoff)) == -1) {
			if (!eflag)
				err(1, "setsockopt ckoff");
			printf("setsockopt failed as expected: %s\n",
			    strerror(errno));
		} else {
			if (eflag)
				errx(1, "setsockopt succeeded unexpectedly");
		}
	}

	if (argc) {
		pid_t pid;

		printf("fork child process\n");
		pid = fork();
		if (pid == -1)
			err(1, "fork");
		if (pid == 0) {
			/* child */
			printf("execute %s\n", argv[0]);
			execvp(argv[0], argv);
			err(1, "execvp %s", argv[0]);
		}
		printf("child pid %d\n", pid);
	}

	if (wflag) {
		int n;
		ssize_t r;
		size_t rsz;
		fd_set fds;
		struct timeval to;
		char buf[1<<16];

		FD_ZERO(&fds);
		FD_SET(s, &fds);
		to.tv_sec = 10;
		to.tv_usec = 0;
		printf("select socket read\n");
		n = select(s + 1, &fds, NULL, NULL, &to);
		switch (n) {
		case -1:
			err(1, "select");
		case 0:
			errx(1, "timeout");
		default:
			printf("selected %d\n", n);
		}
		printf("recv packet\n");
		r = recv(s, buf, sizeof(buf), 0);
		if (r < 0)
			err(1, "recv");
		rsz = r;
		printf("received payload size %zd\n", rsz);
		if (rflag) {
			if (rsz != recvsz)
				errx(1, "wrong payload size, expected %zu",					    recvsz);
		}
	}

	if (sflag) {
		size_t i;
		char *buf;

		buf = malloc(sendsz);
		if (buf == NULL)
			err(1, "malloc sendsz");
		for (i = 0; i < sendsz; i++)
			buf[i] = i & 0xff;
		printf("send payload of size %zu\n", sendsz);
		if (send(s, buf, sendsz, 0) == -1)
			err(1, "send");
		free(buf);
	}

	if (argc) {
		int status;

		printf("wait for child\n");
		if (wait(&status) == -1)
			err(1, "wait");
		if (status != 0)
			errx(1, "child program %s status %d", argv[0], status);
		printf("child program %s status %d\n", argv[0], status);
	}

	return 0;
}

/*	$OpenBSD: mc6recv.c,v 1.2 2021/07/06 11:50:34 bluhm Exp $	*/
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
#include <sys/wait.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include <err.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void __dead usage(void);
void sigexit(int);

void __dead
usage(void)
{
	fprintf(stderr,
"mc6recv [-f file] [-g group] [-i ifname] [-n timeout] [-p port] [-r timeout]\n"
"    [mc6send ...]\n"
"    -f file         print message to log file, default stdout\n"
"    -g group        multicast group, default 224.0.0.123\n"
"    -i ifname       multicast interface name\n"
"    -n timeout      expect not to receive any message until timeout\n"
"    -p port         destination port number, default 12345\n"
"    -r timeout      receive timeout in seconds\n"
"    mc6send ...      after setting up receive, fork and exec send command\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in6 sin6;
	struct ipv6_mreq mreq6;
	FILE *log;
	const char *errstr, *file, *group, *ifname;
	char msg[256];
	ssize_t n;
	int ch, s, norecv, port, status;
	unsigned int timeout;
	pid_t pid;

	log = stdout;
	file = NULL;
	group = "ff04::123";
	ifname = "lo0";
	norecv = 0;
	port = 12345;
	timeout = 0;
	while ((ch = getopt(argc, argv, "f:g:i:n:p:r:")) != -1) {
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
		case 'n':
			norecv = 1;
			timeout = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "no timeout is %s: %s", errstr, optarg);
			break;
		case 'p':
			port = strtonum(optarg, 1, 0xffff, &errstr);
			if (errstr != NULL)
				errx(1, "port is %s: %s", errstr, optarg);
			break;
		case 'r':
			timeout = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "timeout is %s: %s", errstr, optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (file != NULL) {
		log = fopen(file, "w");
		if (log == NULL)
			err(1, "fopen %s", file);
	}

	s = socket(AF_INET6, SOCK_DGRAM, 0);
	if (s == -1)
		err(1, "socket");
	if (inet_pton(AF_INET6, group, &mreq6.ipv6mr_multiaddr) == -1)
		err(1, "inet_pton %s", group);
	mreq6.ipv6mr_interface = if_nametoindex(ifname);
	if (mreq6.ipv6mr_interface == 0)
		err(1, "if_nametoindex %s", ifname);
	if (setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6,
	    sizeof(mreq6)) == -1)
		err(1, "setsockopt IPV6_JOIN_GROUP %s %s", group, ifname);

	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = htons(port);
	if (inet_pton(AF_INET6, group, &sin6.sin6_addr) == -1)
		err(1, "inet_pton %s", group);
	if (IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr) ||
	    IN6_IS_ADDR_MC_LINKLOCAL(&sin6.sin6_addr) ||
	    IN6_IS_ADDR_MC_INTFACELOCAL(&sin6.sin6_addr)) {
		sin6.sin6_scope_id = mreq6.ipv6mr_interface;
	}
	if (bind(s, (struct sockaddr *)&sin6, sizeof(sin6)) == -1)
		err(1, "bind [%s]:%d", group, port);

	if (argc) {
		pid = fork();
		switch (pid) {
		case -1:
			err(1, "fork");
		case 0:
			execvp(argv[0], argv);
			err(1, "exec %s", argv[0]);
		}
	}
	if (timeout) {
		if (norecv) {
			if (signal(SIGALRM, sigexit) == SIG_ERR)
				err(1, "signal SIGALRM");
		}
		alarm(timeout);
	}
	n = recv(s, msg, sizeof(msg) - 1, 0);
	if (n == -1)
		err(1, "recv");
	msg[n] = '\0';
	fprintf(log, "<<< %s\n", msg);
	fflush(log);

	if (norecv)
		errx(1, "received %s", msg);

	if (argc) {
		if (waitpid(pid, &status, 0) == -1)
			err(1, "waitpid %d", pid);
		if (status)
			errx(1, "%s %d", argv[0], status);
	}

	return 0;
}

void
sigexit(int sig)
{
	_exit(0);
}

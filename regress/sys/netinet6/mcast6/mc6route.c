/*	$OpenBSD: mc6route.c,v 1.2 2021/07/06 11:50:34 bluhm Exp $	*/
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
/*
 * Copyright (c) 1983, 1988, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet6/ip6_mroute.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void __dead usage(void);
void sigexit(int);
size_t get_sysctl(const int *mib, u_int mcnt, char **buf);

void __dead
usage(void)
{
	fprintf(stderr,
"mc6route [-b] [-f file] [-g group] -i ifname [-n timeout] -o outname\n"
"    [-r timeout]\n"
"    -b              fork to background after setup\n"
"    -f file         print message to log file, default stdout\n"
"    -g group        multicast group, default 224.0.0.123\n"
"    -i ifname       multicast interface address\n"
"    -n timeout      expect not to receive any message until timeout\n"
"    -o outname      outgoing interface address\n"
"    -r timeout      receive timeout in seconds\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	struct mif6ctl mif;
	struct mf6cctl mfc;
	struct mif6info *minfo;
	FILE *log;
	const char *errstr, *file, *group, *ifname, *outname;
	char *buf;
	size_t needed;
	u_int64_t pktin, pktout;
	int value, ch, s, fd, background, norecv;
	unsigned int timeout;
	pid_t pid;
	int mib[] = { CTL_NET, PF_INET6, IPPROTO_IPV6, IPV6CTL_MRTMIF };

	background = 0;
	log = stdout;
	file = NULL;
	group = "ff04::123";
	ifname = NULL;
	norecv = 0;
	outname = NULL;
	timeout = 0;
	while ((ch = getopt(argc, argv, "bf:g:i:n:o:r:")) != -1) {
		switch (ch) {
		case 'b':
			background = 1;
			break;
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
		case 'o':
			outname = optarg;
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
	if (ifname == NULL)
		errx(2, "no ifname");
	if (outname == NULL)
		errx(2, "no outname");
	if (argc)
		usage();

	if (file != NULL) {
		log = fopen(file, "w");
		if (log == NULL)
			err(1, "fopen %s", file);
	}

	s = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	if (s == -1)
		err(1, "socket");
	value = 1;
	if (setsockopt(s, IPPROTO_IPV6, MRT6_INIT, &value, sizeof(value)) == -1)
		err(1, "setsockopt MRT6_INIT");

	memset(&mif, 0, sizeof(mif));
	mif.mif6c_mifi = 0;
	mif.mif6c_pifi = if_nametoindex(ifname);
	if (mif.mif6c_pifi == 0)
		err(1, "if_nametoindex %s", ifname);
	if (setsockopt(s, IPPROTO_IPV6, MRT6_ADD_MIF, &mif, sizeof(mif)) == -1)
		err(1, "setsockopt MRT6_ADD_MIF %s", ifname);

	memset(&mif, 0, sizeof(mif));
	mif.mif6c_mifi = 1;
	mif.mif6c_pifi = if_nametoindex(outname);
	if (mif.mif6c_pifi == 0)
		err(1, "if_nametoindex %s", outname);
	if (setsockopt(s, IPPROTO_IPV6, MRT6_ADD_MIF, &mif, sizeof(mif)) == -1)
		err(1, "setsockopt MRT6_ADD_MIF %s", outname);

	memset(&mfc, 0, sizeof(mfc));
	if (inet_pton(AF_INET6, group, &mfc.mf6cc_mcastgrp.sin6_addr) == -1)
		err(1, "inet_pton %s", group);
	mfc.mf6cc_parent = 0;
	IF_SET(1, &mfc.mf6cc_ifset);

	if (setsockopt(s, IPPROTO_IPV6, MRT6_ADD_MFC, &mfc, sizeof(mfc)) == -1)
		err(1, "setsockopt MRT6_ADD_MFC %s", ifname);

	if (background) {
		pid = fork();
		switch (pid) {
		case -1:
			err(1, "fork");
		case 0:
			fd = open("/dev/null", O_RDWR);
			if (fd == -1)
				err(1, "open /dev/null");
			if (dup2(fd, 0) == -1)
				err(1, "dup 0");
			if (dup2(fd, 1) == -1)
				err(1, "dup 1");
			if (dup2(fd, 2) == -1)
				err(1, "dup 2");
			break;
		default:
			_exit(0);
		}
	}

	if (timeout) {
		if (norecv) {
			if (signal(SIGALRM, sigexit) == SIG_ERR)
				err(1, "signal SIGALRM");
		}
		alarm(timeout);
	}

	buf = NULL;
	pktin = pktout = 0;
	do {
		struct timespec sleeptime = { 0, 10000000 };

		if (nanosleep(&sleeptime, NULL) == -1)
			err(1, "nanosleep");
		needed = get_sysctl(mib, sizeof(mib) / sizeof(mib[0]), &buf);
		for (minfo = (struct mif6info *)buf;
		    (char *)minfo < buf + needed;
		    minfo++) {
			switch (minfo->m6_ifindex) {
			case 0:
				if (pktin != minfo->m6_pkt_in) {
					fprintf(log, "<<< %llu\n",
					    minfo->m6_pkt_in);
					fflush(log);
				}
				pktin = minfo->m6_pkt_in;
				break;
			case 1:
				if (pktout != minfo->m6_pkt_out) {
					fprintf(log, ">>> %llu\n",
					    minfo->m6_pkt_out);
					fflush(log);
				}
				pktout = minfo->m6_pkt_out;
				break;
			}
		}
	} while (pktin == 0 || pktout == 0);
	free(buf);

	if (norecv)
		errx(1, "pktin %llu, pktout %llu", pktin, pktout);

	return 0;
}

void
sigexit(int sig)
{
	_exit(0);
}

/* from netstat(8) */
size_t
get_sysctl(const int *mib, u_int mcnt, char **buf)
{
	size_t needed;

	while (1) {
		if (sysctl(mib, mcnt, NULL, &needed, NULL, 0) == -1)
			err(1, "sysctl-estimate");
		if (needed == 0)
			break;
		if ((*buf = realloc(*buf, needed)) == NULL)
			err(1, NULL);
		if (sysctl(mib, mcnt, *buf, &needed, NULL, 0) == -1) {
			if (errno == ENOMEM)
				continue;
			err(1, "sysctl");
		}
		break;
	}

	return needed;
}

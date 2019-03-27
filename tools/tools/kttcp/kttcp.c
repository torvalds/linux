/*	$FreeBSD$	*/
/*	$NetBSD: kttcp.c,v 1.5 2002/07/11 23:32:35 simonb Exp $	*/

/*
 * Copyright (c) 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden and Jason R. Thorpe
 * for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "kttcpio.h"

#define	KTTCP_PORT		"22222"
#define	KTTCP_XMITSIZE		(10*1024*1024)
#define	KTTCP_SOCKBUF_DEFAULT	65536

#define	KTTCP_DEVICE		"/dev/kttcp"

static void
usage(void)
{
	fprintf(stderr,
	    "usage: kttcp -r [-b sockbufsize] [-p port] [-q] [-v]\n"
	    "                [-4] [-6]\n"
	    "       kttcp -t [-b sockbufsize] [-n bytes] [-q] [-v] [-p port]\n"
	    "                [-4] [-6] host\n"
	);
	exit(1);
}

static unsigned long long
get_bytes(const char *str)
{
	unsigned long long bytes;
	char *cp;

	bytes = strtoull(str, &cp, 10);
	if (bytes == ULLONG_MAX && errno == ERANGE)
		err(1, "%s", str);

	if (cp[0] != '\0') {
		if (cp[1] != '\0')
			errx(1, "invalid byte count: %s", str);
		if (cp[0] == 'k' || cp[0] == 'K')
			bytes *= 1024;
		else if (cp[0] == 'm' || cp[0] == 'M')
			bytes *= 1024 * 1024;
		else if (cp[0] == 'g' || cp[0] == 'G')
			bytes *= 1024 * 1024 * 1024;
		else
			errx(1, "invalid byte count modifier: %s", str);
	}

	return (bytes);
}

int
main(int argc, char *argv[])
{
	int c, error, s, verbose, s2, kfd;
	int xmitset, family;
	int bufsize;
	int ai_flag;
	char *host;
	char *portstr;
	struct kttcp_io_args kio;
	struct addrinfo hints, *addr, *res;
	struct sockaddr_storage ss;
	struct rusage rustart, ruend;
	struct timeval tvtmp;
	unsigned long long ull, usecs, bytespersec, bitspersec, xmitsize;
	char connecthost[NI_MAXHOST];
	socklen_t slen;
	const int one = 1;
	u_long cmd;

	cmd = 0;
	portstr = KTTCP_PORT;
	verbose = 1;
	xmitset = 0;
	bufsize = KTTCP_SOCKBUF_DEFAULT;
	xmitsize = KTTCP_XMITSIZE;
	family = PF_UNSPEC;
	while ((c = getopt(argc, argv, "46b:n:p:qrtvw:")) != -1) {
		switch (c) {
		case '4':
			if (family != PF_UNSPEC)
				usage();
			family = PF_INET;
			break;
		case '6':
			if (family != PF_UNSPEC)
				usage();
			family = PF_INET6;
			break;
		case 'b':
			ull = get_bytes(optarg);
			if (ull > INT_MAX)
				errx(1,
				    "invalid socket buffer size: %s\n", optarg);
			bufsize = ull;
			break;
		case 'n':
			xmitsize = get_bytes(optarg);
			xmitset = 1;
			break;
		case 'p':
			portstr = optarg;
			break;
		case 'q':
			verbose = 0;
			break;
		case 'r':
			if (cmd != 0)
				usage();
			cmd = KTTCP_IO_RECV;
			break;
		case 't':
			if (cmd != 0)
				usage();
			cmd = KTTCP_IO_SEND;
			break;
		case 'v':
			verbose = 2;
			break;
		case '?':
		default:
			usage();
		}
	}
	if (cmd == 0)
		usage();

	argc -= optind;
	argv += optind;

	if (cmd == KTTCP_IO_SEND) {
		if (xmitsize <= 0 || argc < 1)
			usage();
		host = argv[0];
		ai_flag = 0;
	} else {
		if (xmitset == 0)
			xmitsize = KTTCP_MAX_XMIT;
		host = NULL;
		ai_flag = AI_PASSIVE;
	}

	if ((kfd = open(KTTCP_DEVICE, O_RDWR, 666)) == -1)
		err(2, "open %s", KTTCP_DEVICE);

	memset(&hints, 0, sizeof hints);
	hints.ai_flags = ai_flag;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = family;
	error = getaddrinfo(host, portstr, &hints, &addr);

	if (error != 0)
		errx(2, "%s", gai_strerror(error));

	s = -1;
	for (res = addr; res != NULL; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s >= 0)
			break;
	}
	if (res == NULL)
		err(2, "can't create socket");

	printf("kttcp: socket buffer size: %d\n", bufsize);

	if (cmd == KTTCP_IO_SEND) {
		if (connect(s, res->ai_addr, res->ai_addrlen) < 0)
			err(2, "connect");
		if (verbose) {
			getnameinfo(res->ai_addr, res->ai_addrlen,
			    connecthost, sizeof connecthost, NULL, 0,
			    NI_NUMERICHOST);
			printf("kttcp: connected to %s\n", connecthost);
		}
		if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof (int))
		    < 0)
			err(2, "setsockopt sndbuf");
		kio.kio_socket = s;
	} else {
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one,
		    sizeof (int)) < 0)
			err(2, "setsockopt reuseaddr");
		if (bind(s, res->ai_addr, res->ai_addrlen) < 0)
			err(2, "bind");
		if (listen(s, 1) < 0)
			err(2, "listen");
		if (verbose)
			printf("kttcp: listening on port %s\n", portstr);
		slen = sizeof ss;
		s2 = accept(s, (struct sockaddr *)&ss, &slen);
		if (s2 < 0)
			err(2, "accept");
		if (verbose) {
			getnameinfo((struct sockaddr *)&ss, ss.ss_len,
			    connecthost, sizeof connecthost, NULL, 0,
			    NI_NUMERICHOST);
			printf("kttcp: connect from %s\n", connecthost);
		}
		if (setsockopt(s2, SOL_SOCKET, SO_RCVBUF, &bufsize,
		    sizeof (int)) < 0)
			err(2, "setsockopt rcvbuf");
		kio.kio_socket = s2;
	}

	kio.kio_totalsize = xmitsize;

	getrusage(RUSAGE_SELF, &rustart);
	if (ioctl(kfd, cmd, &kio) == -1)
		err(2, "kttcp i/o command");
	getrusage(RUSAGE_SELF, &ruend);

	usecs = (unsigned long long)kio.kio_elapsed.tv_sec * 1000000;
	usecs += kio.kio_elapsed.tv_usec;

	bytespersec = kio.kio_bytesdone * 1000000LL / usecs;
	bitspersec = bytespersec * NBBY;
	printf("kttcp: %llu bytes in %ld.%03ld real seconds ==> %llu bytes/sec\n",
	    kio.kio_bytesdone, kio.kio_elapsed.tv_sec,
	    kio.kio_elapsed.tv_usec / 1000, bytespersec);
	if (verbose > 1) {
		timersub(&ruend.ru_stime, &rustart.ru_stime, &tvtmp);
		bytespersec = kio.kio_bytesdone * 1000000LL /
		    (tvtmp.tv_sec * 1000000ULL + tvtmp.tv_usec);
		printf("kttcp: %llu bytes in %ld.%03ld CPU seconds ==> %llu bytes/CPU sec\n",
		    kio.kio_bytesdone, tvtmp.tv_sec, tvtmp.tv_usec / 1000, bytespersec);
	}
	printf("       %g (%g) Megabits/sec\n",
	    ((double) bitspersec / 1024.0) / 1024.0,
	    ((double) bitspersec / 1000.0) / 1000.0);

	timersub(&ruend.ru_utime, &rustart.ru_utime, &tvtmp);
	/* XXX
	 * sometimes, this ends up as -1 * hz!?
	 */
	if (tvtmp.tv_sec < 0)
		tvtmp.tv_sec = tvtmp.tv_usec = 0;
	printf("  %ld.%02lduser", tvtmp.tv_sec, tvtmp.tv_usec / 10000);
	ull = tvtmp.tv_sec * 1000000ULL + tvtmp.tv_usec;

	timersub(&ruend.ru_stime, &rustart.ru_stime, &tvtmp);
	printf(" %ld.%02ldsys", tvtmp.tv_sec, tvtmp.tv_usec / 10000);
	ull += tvtmp.tv_sec * 1000000ULL + tvtmp.tv_usec;

	printf(" %lld.%lldreal", usecs / 1000000, (usecs % 1000000) / 10000);
	printf(" %lld%%", ull * 100 / usecs);
	printf("\n");


	close(kio.kio_socket);
	if (cmd == KTTCP_IO_RECV)
		close(s);
	close(kfd);
	freeaddrinfo(addr);

	return 0;
}

/*-
 * l2ping.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: l2ping.c,v 1.5 2003/05/16 19:54:40 max Exp $
 * $FreeBSD$
 */

#include <sys/ioctl.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	usage	(void);
static void	tv_sub	(struct timeval *, struct timeval const *);
static double	tv2msec	(struct timeval const *);

#undef	min
#define	min(x, y)	(((x) > (y))? (y) : (x))

static char const		pattern[] = "1234567890-";
#define PATTERN_SIZE		(sizeof(pattern) - 1)

/* 
 * Main 
 */

int
main(int argc, char *argv[])
{
	bdaddr_t		 src, dst;
	struct hostent		*he;
	uint8_t			*echo_data;
	struct sockaddr_l2cap	 sa;
	int32_t			 n, s, count, wait, flood, echo_size, numeric;
	char			*endp, *rname;

	/* Set defaults */
	memcpy(&src, NG_HCI_BDADDR_ANY, sizeof(src));
	memcpy(&dst, NG_HCI_BDADDR_ANY, sizeof(dst));

	echo_data = (uint8_t *) calloc(NG_L2CAP_MAX_ECHO_SIZE, sizeof(uint8_t));
	if (echo_data == NULL) {
		fprintf(stderr, "Failed to allocate echo data buffer");
		exit(1);
	}

	/*
	 * Set default echo size to the NG_L2CAP_MTU_MINIMUM minus
	 * the size of the L2CAP signalling command header.
	 */

	echo_size = NG_L2CAP_MTU_MINIMUM - sizeof(ng_l2cap_cmd_hdr_t);
	count = -1; /* unimited */
	wait = 1;   /* sec */
	flood = 0;
	numeric = 0;

	/* Parse command line arguments */
	while ((n = getopt(argc, argv, "a:c:fi:nS:s:h")) != -1) {
		switch (n) {
		case 'a':
			if (!bt_aton(optarg, &dst)) {
				if ((he = bt_gethostbyname(optarg)) == NULL)
					errx(1, "%s: %s", optarg, hstrerror(h_errno));

				memcpy(&dst, he->h_addr, sizeof(dst));
			}
			break;

		case 'c':
			count = strtol(optarg, &endp, 10);
			if (count <= 0 || *endp != '\0')
				usage();
			break;

		case 'f':
			flood = 1;
			break;

		case 'i':
			wait = strtol(optarg, &endp, 10);
			if (wait <= 0 || *endp != '\0')
				usage();
			break;

		case 'n':
			numeric = 1;
			break;

		case 'S':
			if (!bt_aton(optarg, &src)) {
				if ((he = bt_gethostbyname(optarg)) == NULL)
					errx(1, "%s: %s", optarg, hstrerror(h_errno));

				memcpy(&src, he->h_addr, sizeof(src));
			}
			break;

		case 's':
                        echo_size = strtol(optarg, &endp, 10);
                        if (echo_size < sizeof(int32_t) ||
			    echo_size > NG_L2CAP_MAX_ECHO_SIZE ||
			    *endp != '\0')
				usage();
			break;

		case 'h':
		default:
			usage();
			break;
		}
	}

	if (memcmp(&dst, NG_HCI_BDADDR_ANY, sizeof(dst)) == 0)
		usage();

	he = bt_gethostbyaddr((const char *)&dst, sizeof(dst), AF_BLUETOOTH);
	if (he == NULL || he->h_name == NULL || he->h_name[0] == '\0' || numeric)
		asprintf(&rname, "%s", bt_ntoa(&dst, NULL));
	else
		rname = strdup(he->h_name);

	if (rname == NULL)
		errx(1, "Failed to create remote hostname");

	s = socket(PF_BLUETOOTH, SOCK_RAW, BLUETOOTH_PROTO_L2CAP);
	if (s < 0)
		err(2, "Could not create socket");

	memset(&sa, 0, sizeof(sa));
	sa.l2cap_len = sizeof(sa);
	sa.l2cap_family = AF_BLUETOOTH;
	memcpy(&sa.l2cap_bdaddr, &src, sizeof(sa.l2cap_bdaddr));

	if (bind(s, (struct sockaddr *) &sa, sizeof(sa)) < 0)
		err(3,
"Could not bind socket, src bdaddr=%s", bt_ntoa(&sa.l2cap_bdaddr, NULL));

	memset(&sa, 0, sizeof(sa));
	sa.l2cap_len = sizeof(sa);
	sa.l2cap_family = AF_BLUETOOTH;
	memcpy(&sa.l2cap_bdaddr, &dst, sizeof(sa.l2cap_bdaddr));

	if (connect(s, (struct sockaddr *) &sa, sizeof(sa)) < 0)
		err(4,
"Could not connect socket, dst bdaddr=%s", bt_ntoa(&sa.l2cap_bdaddr, NULL));

	/* Fill pattern */
	for (n = 0; n < echo_size; ) {
		int32_t	avail = min(echo_size - n, PATTERN_SIZE);

		memcpy(echo_data + n, pattern, avail);
		n += avail;
	}

	/* Start ping'ing */
	for (n = 0; count == -1 || count > 0; n ++) {
		struct ng_btsocket_l2cap_raw_ping	r;
		struct timeval				a, b;
		int32_t					fail;

		if (gettimeofday(&a, NULL) < 0)
			err(5, "Could not gettimeofday(a)");

		fail = 0;
		*((int32_t *) echo_data) = htonl(n);

		r.result = 0;
		r.echo_size = echo_size;
		r.echo_data = echo_data;
		if (ioctl(s, SIOC_L2CAP_L2CA_PING, &r, sizeof(r)) < 0) {
			r.result = errno;
			fail = 1;
/*
			warn("Could not ping, dst bdaddr=%s",
				bt_ntoa(&r.echo_dst, NULL));
*/
		}

		if (gettimeofday(&b, NULL) < 0)
			err(7, "Could not gettimeofday(b)");

		tv_sub(&b, &a);

		fprintf(stdout,
"%d bytes from %s seq_no=%d time=%.3f ms result=%#x %s\n",
			r.echo_size,
			rname,
			ntohl(*((int32_t *)(r.echo_data))),
			tv2msec(&b), r.result,
			((fail == 0)? "" : strerror(errno)));

		if (!flood) {
			/* Wait */
			a.tv_sec = wait;
			a.tv_usec = 0;
			select(0, NULL, NULL, NULL, &a);
		}

		if (count != -1)
			count --;
	}

	free(rname);
	free(echo_data);
	close(s);

	return (0);
} /* main */

/* 
 * a -= b, for timevals 
 */

static void
tv_sub(struct timeval *a, struct timeval const *b)
{
	if (a->tv_usec < b->tv_usec) {
		a->tv_usec += 1000000;
		a->tv_sec -= 1;
	}

	a->tv_usec -= b->tv_usec;
	a->tv_sec -= b->tv_sec;
} /* tv_sub */

/* 
 * convert tv to msec 
 */

static double
tv2msec(struct timeval const *tvp)
{
	return(((double)tvp->tv_usec)/1000.0 + ((double)tvp->tv_sec)*1000.0);
} /* tv2msec */

/* 
 * Usage 
 */

static void
usage(void)
{
	fprintf(stderr, "Usage: l2ping [-fhn] -a remote " \
		"[-c count] [-i wait] [-S source] [-s size]\n");
	fprintf(stderr, "Where:\n");
	fprintf(stderr, "  -a remote  Specify remote device to ping\n");
	fprintf(stderr, "  -c count   Number of packets to send\n");
	fprintf(stderr, "  -f         No delay between packets\n");
	fprintf(stderr, "  -h         Display this message\n");
	fprintf(stderr, "  -i wait    Delay between packets (sec)\n");
	fprintf(stderr, "  -n         Numeric output only\n");
	fprintf(stderr, "  -S source  Specify source device\n");
	fprintf(stderr, "  -s size    Packet size (bytes), " \
		"between %zd and %zd\n", sizeof(int32_t), NG_L2CAP_MAX_ECHO_SIZE);
	
	exit(255);
} /* usage */


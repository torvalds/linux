/*-
 * bthost.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: bthost.c,v 1.5 2003/05/21 20:30:01 max Exp $
 * $FreeBSD$
 */

#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int  hostmode  (char const *arg, int brief);
static int  protomode (char const *arg, int brief);
static void usage     (void);

int
main(int argc, char **argv)
{
	int	opt, brief = 0, proto = 0;

	while ((opt = getopt(argc, argv, "bhp")) != -1) {
		switch (opt) {
		case 'b':
			brief = 1;
			break;

		case 'p':
			proto = 1;
			break;

		case 'h':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	exit(proto? protomode(*argv, brief) : hostmode(*argv, brief));
}

static int
hostmode(char const *arg, int brief)
{
	struct hostent	*he = NULL;
	bdaddr_t	 ba;
	char		 bastr[32];
	int		 reverse;

	if (bt_aton(arg, &ba) == 1) {
		reverse = 1;
		he = bt_gethostbyaddr((char const *) &ba, sizeof(ba), 
					AF_BLUETOOTH);
	} else {
		reverse = 0;
		he = bt_gethostbyname(arg);
	}

	if (he == NULL) {
		herror(reverse? bt_ntoa(&ba, bastr) : arg);
		return (1);
	}

	if (brief)
		printf("%s", reverse? he->h_name :
				bt_ntoa((bdaddr_t *)(he->h_addr), bastr));
	else
		printf("Host %s has %s %s\n", 
			reverse? bt_ntoa(&ba, bastr) : arg,
			reverse? "name" : "address",
			reverse? he->h_name :
				bt_ntoa((bdaddr_t *)(he->h_addr), bastr));

	return (0);
}

static int
protomode(char const *arg, int brief)
{
	struct protoent	*pe = NULL;
	int		 proto;

	if ((proto = atoi(arg)) != 0)
		pe = bt_getprotobynumber(proto);
	else
		pe = bt_getprotobyname(arg);

	if (pe == NULL) {
		fprintf(stderr, "%s: Unknown Protocol/Service Multiplexor\n", arg);
		return (1);
	}

	if (brief) {
		if (proto)
			printf("%s", pe->p_name);
		else
			printf("%d", pe->p_proto);
	} else {
		printf("Protocol/Service Multiplexor %s has number %d\n",
			pe->p_name, pe->p_proto);
	}

	return (0);
}

static void
usage(void)
{
	fprintf(stdout, "Usage: bthost [-b -h -p] host_or_protocol\n");
	exit(255);
}


/*	$OpenBSD: print-enc.c,v 1.17 2021/12/01 18:28:45 deraadt Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/queue.h>

#include <net/if.h>
#include <netinet/ip_ipsp.h>

#include <sys/mbuf.h>
#include <net/if_enc.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include <ctype.h>
#include <netdb.h>
#include <pcap.h>
#include <signal.h>
#include <stdio.h>

#include "interface.h"
#include "addrtoname.h"

#define ENC_PRINT_TYPE(wh, xf, nam) \
	if ((wh) & (xf)) { \
		printf("%s%s", nam, (wh) == (xf) ? "): " : ","); \
		(wh) &= ~(xf); \
	}

void
enc_if_print(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int length = h->len, caplen = h->caplen;
	const struct enchdr *hdr;
	int flags;

	ts_print(&h->ts);

	if (caplen < ENC_HDRLEN) {
		printf("[|enc]");
		goto out;
	}

	/*
	 * Some printers want to get back at the link level addresses,
	 * and/or check that they're not walking off the end of the packet.
	 * Rather than pass them all the way down, we set these globals.
	 */
	packetp = p;
	snapend = p + caplen;

	hdr = (struct enchdr *)p;
	flags = hdr->flags;
	if (flags == 0)
		printf("(unprotected): ");
	else
		printf("(");
	ENC_PRINT_TYPE(flags, M_AUTH, "authentic");
	ENC_PRINT_TYPE(flags, M_CONF, "confidential");
	/* ENC_PRINT_TYPE(flags, M_TUNNEL, "tunnel"); */
	printf("SPI 0x%08x: ", ntohl(hdr->spi));

	length -= ENC_HDRLEN;
	p += ENC_HDRLEN;

	switch (hdr->af) {
	case AF_INET:
	default:
		ip_print(p, length);
		break;
	case AF_INET6:
		ip6_print(p, length);
		break;
	}

	if (xflag)
		default_print(p, caplen - ENC_HDRLEN);
out:
	putchar('\n');
}

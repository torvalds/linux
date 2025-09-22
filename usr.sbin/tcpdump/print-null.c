/*	$OpenBSD: print-null.c,v 1.25 2021/12/01 18:28:46 deraadt Exp $	*/

/*
 * Copyright (c) 1991, 1993, 1994, 1995, 1996, 1997
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

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/if_ether.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>

#include <pcap.h>
#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"

#ifndef AF_NS
#define AF_NS		6		/* XEROX NS protocols */
#endif

/*
 * The DLT_NULL packet header is 4 bytes long. It contains a host
 * order 32 bit integer that specifies the family, e.g. AF_INET
 */
#define	NULL_HDRLEN 4

static void
null_print(const u_char *p, const struct ip *ip, u_int length)
{
	u_int family;

	memcpy((char *)&family, (char *)p, sizeof(family));

	if (nflag && family != AF_LINK) {
		/* XXX just dump the header */
		return;
	}
	switch (family) {

	case AF_INET:
		printf("ip: ");
		break;

	case AF_INET6:
		printf("ip6: ");
		break;

	case AF_NS:
		printf("ns: ");
		break;

#ifdef __OpenBSD__
	case AF_LINK:
		ether_print(p + NULL_HDRLEN, length);
		break;
#endif
	case AF_MPLS:
		printf("mpls: ");
		break;

	default:
		printf("AF %d: ", family);
		break;
	}
}

void
loop_if_print(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
	*(u_int *)p = ntohl(*(u_int *)p);

	null_if_print(user, h, p);
}

void
null_if_print(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int length = h->len;
	u_int caplen = h->caplen;
	u_int family = *(u_int *)p;

#ifdef __OpenBSD__
	struct ether_header *ep;
	u_short ether_type;
	extern u_short extracted_ethertype;
#endif

	ts_print(&h->ts);

	if (caplen < NULL_HDRLEN) {
		printf("[|null]");
		goto out;
	}

	/*
	 * Some printers want to get back at the link level addresses,
	 * and/or check that they're not walking off the end of the packet.
	 * Rather than pass them all the way down, we set these globals.
	 */
	packetp = p;
	snapend = p + caplen;

	length -= NULL_HDRLEN;

	if (eflag)
		null_print(p, (struct ip *)(p + NULL_HDRLEN), length);

	switch (family) {
	case AF_INET:
		ip_print(p + NULL_HDRLEN, length);
		break;

	case AF_INET6:
		ip6_print(p + NULL_HDRLEN, length);
		break;

	case AF_MPLS:
		mpls_print(p + NULL_HDRLEN, length);
		break;

#ifdef __OpenBSD__
	case AF_LINK:
		if (caplen < sizeof(struct ether_header) + NULL_HDRLEN) {
			printf("[|ether]");
			goto out;
		}

		length -= sizeof(struct ether_header);
		caplen -= sizeof(struct ether_header);
		ep = (struct ether_header *)(p + NULL_HDRLEN);
		p += NULL_HDRLEN + sizeof(struct ether_header);
		packetp += sizeof(struct ether_header);
		ether_type = ntohs(ep->ether_type);

		extracted_ethertype = 0;
		if (ether_type <= ETHERMTU) {
			/* Try to print the LLC-layer header & higher layers */
			if (llc_print(p, length, caplen, ESRC(ep),
			    EDST(ep)) == 0) {
				/* ether_type not known, print raw packet */
				if (!eflag)
					ether_print((u_char *)ep, length);
				if (extracted_ethertype) {
					printf("(LLC %s) ",
					       etherproto_string(htons(extracted_ethertype)));
				}
				if (!xflag && !qflag)
					default_print(p, caplen - NULL_HDRLEN);
			}
		} else if (ether_encap_print(ether_type, p, length,
		           caplen) == 0) {
			/* ether_type not known, print raw packet */
			if (!eflag)
				ether_print((u_char *)ep, length +
				    sizeof(*ep));
			if (!xflag && !qflag)
				default_print(p, caplen - NULL_HDRLEN);
		}
		break;
#endif /* __OpenBSD__ */
	}

	if (xflag)
		default_print((const u_char *)(packetp + NULL_HDRLEN),
		    caplen - NULL_HDRLEN);
 out:
	putchar('\n');
}


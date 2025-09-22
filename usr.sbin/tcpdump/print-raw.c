/*	$OpenBSD: print-raw.c,v 1.10 2025/06/06 00:03:06 dlg Exp $	*/

/*
 * Copyright (c) 1996
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
#include <netinet/if_ether.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>

#include <pcap.h>
#include <stdio.h>
#include <string.h>

#include "addrtoname.h"
#include "interface.h"

#ifndef AF_NS
#define AF_NS		6		/* XEROX NS protocols */
#endif

/*
 * The DLT_RAW packet has no header. It contains a raw IP packet.
 */

void
raw_if_print(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int length = h->len;
	u_int caplen = h->caplen;
	uint8_t v;

	ts_print(&h->ts);

	/*
	 * Some printers want to get back at the link level addresses,
	 * and/or check that they're not walking off the end of the packet.
	 * Rather than pass them all the way down, we set these globals.
	 */
	packetp = p;
	snapend = p + caplen;

	if (caplen >= sizeof(v)) {
		v = *p >> 4;
		switch (v) {
		case 4:
			if (eflag)
				printf("ip: ");

			ip_print(p, length);
			break;
		case 6:
			if (eflag)
				printf("ip6: ");

			ip6_print(p, length);
			break;
		default:
			if (eflag)
				printf("v%u: ", v);
			break;
		}
	}

	if (xflag)
		default_print(p, caplen);
	putchar('\n');
}

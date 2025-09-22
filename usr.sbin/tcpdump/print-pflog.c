/*	$OpenBSD: print-pflog.c,v 1.35 2022/02/22 17:35:01 deraadt Exp $	*/

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

#ifndef NO_PID
#define NO_PID	(99999+1)
#endif

#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <net/pfvar.h>
#include <net/if_pflog.h>

#include <arpa/inet.h>

#include <ctype.h>
#include <netdb.h>
#include <pcap.h>
#include <signal.h>
#include <stdio.h>

#include "interface.h"
#include "addrtoname.h"

char *pf_reasons[PFRES_MAX+2] = PFRES_NAMES;

void
pflog_if_print(u_char *user, const struct pcap_pkthdr *h,
     const u_char *p)
{
	u_int length = h->len;
	u_int hdrlen;
	u_int caplen = h->caplen;
	const struct ip *ip;
	const struct ip6_hdr *ip6;
	const struct pfloghdr *hdr;

	ts_print(&h->ts);

	/* check length */
	if (caplen < sizeof(u_int8_t)) {
		printf("[|pflog]");
		goto out;
	}

#define MIN_PFLOG_HDRLEN	45
	hdr = (struct pfloghdr *)p;
	if (hdr->length < MIN_PFLOG_HDRLEN) {
		printf("[pflog: invalid header length!]");
		goto out;
	}
	hdrlen = (hdr->length + 3) & 0xfc;

	if (caplen < hdrlen) {
		printf("[|pflog]");
		goto out;
	}

	/*
	 * Some printers want to get back at the link level addresses,
	 * and/or check that they're not walking off the end of the packet.
	 * Rather than pass them all the way down, we set these globals.
	 */
	packetp = p;
	snapend = p + caplen;

	hdr = (struct pfloghdr *)p;
	if (eflag) {
		printf("rule ");
		if (ntohl(hdr->rulenr) == (u_int32_t) -1)
			printf("def");
		else {
			printf("%u", ntohl(hdr->rulenr));
			if (hdr->ruleset[0]) {
				printf(".%s", hdr->ruleset);
				if (ntohl(hdr->subrulenr) == (u_int32_t) -1)
					printf(".def");
				else
					printf(".%u", ntohl(hdr->subrulenr));
			}
		}
		if (hdr->reason < PFRES_MAX)
			printf("/(%s) ", pf_reasons[hdr->reason]);
		else
			printf("/(unkn %u) ", (unsigned)hdr->reason);
		if (vflag)
			printf("[uid %u, pid %u] ", (unsigned)hdr->rule_uid,
			    (unsigned)hdr->rule_pid);

		switch (hdr->action) {
		case PF_MATCH:
			printf("match");
			break;
		case PF_SCRUB:
			printf("scrub");
			break;
		case PF_PASS:
			printf("pass");
			break;
		case PF_DROP:
			printf("block");
			break;
		case PF_NAT:
		case PF_NONAT:
			printf("nat");
			break;
		case PF_BINAT:
		case PF_NOBINAT:
			printf("binat");
			break;
		case PF_RDR:
		case PF_NORDR:
			printf("rdr");
			break;
		}
		printf(" %s on %s: ",
		    hdr->dir == PF_OUT ? "out" : "in",
		    hdr->ifname);
		if (vflag && hdr->pid != NO_PID)
			printf("[uid %u, pid %u] ", (unsigned)hdr->uid,
			    (unsigned)hdr->pid);
		if (vflag && hdr->rewritten) {
			char buf[48];

			printf("[rewritten: ");
			if (inet_ntop(hdr->naf, &hdr->saddr, buf,
			    sizeof(buf)) == NULL)
				printf("src ?");
			else
				printf("src %s:%u", buf, ntohs(hdr->sport));
			printf(", ");
			if (inet_ntop(hdr->naf, &hdr->daddr, buf,
			    sizeof(buf)) == NULL)
				printf("dst ?");
			else
				printf("dst %s:%u", buf, ntohs(hdr->dport));
			printf("] ");
		}
	}
	length -= hdrlen;
	switch(hdr->af) {
	case AF_INET:
		ip = (struct ip *)(p + hdrlen);
		ip_print((const u_char *)ip, length);
		if (xflag)
			default_print((const u_char *)ip,
			    caplen - hdrlen);
		break;
	case AF_INET6:
		ip6 = (struct ip6_hdr *)(p + hdrlen);
		ip6_print((const u_char *)ip6, length);
		if (xflag)
			default_print((const u_char *)ip6,
			    caplen - hdrlen);
		break;
	default:
		printf("unknown-af %d", hdr->af);
		break;
	}

out:
	putchar('\n');
}

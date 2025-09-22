/*	$OpenBSD: print-rip.c,v 1.18 2020/01/24 22:46:37 procter Exp $	*/

/*
 * Copyright (c) 1989, 1990, 1991, 1993, 1994, 1996
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

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"			/* must come after interface.h */

struct rip {
	u_char rip_cmd;			/* request/response */
	u_char rip_vers;		/* protocol version # */
	u_short rip_zero2;		/* unused */
};
#define	RIPCMD_REQUEST		1	/* want info */
#define	RIPCMD_RESPONSE		2	/* responding to request */
#define	RIPCMD_TRACEON		3	/* turn tracing on */
#define	RIPCMD_TRACEOFF		4	/* turn it off */
#define	RIPCMD_POLL		5	/* want info from everybody */
#define	RIPCMD_POLLENTRY	6	/* poll for entry */

#define RIP_AUTHLEN 16

struct rip_netinfo {
	u_short rip_family;
	u_short rip_tag;
	u_int32_t rip_dest;
	u_int32_t rip_dest_mask;
	u_int32_t rip_router;
	u_int32_t rip_metric;		/* cost of route */
};

static void
rip_printblk(const u_char *cp, const u_char *ep)
{
	for (; cp < ep; cp += 2)
		printf(" %04x", EXTRACT_16BITS(cp));
	return;
}

static void
rip_entry_print_v1(int vers, const struct rip_netinfo *ni)
{
	u_short family;

	/* RFC 1058 */
	family = EXTRACT_16BITS(&ni->rip_family);
	if (family != AF_INET) {
		printf(" [family %d:", family);
		rip_printblk((u_char *)&ni->rip_tag,
			     (u_char *)&ni->rip_metric +
			     sizeof(ni->rip_metric));
		printf("]");
		return;
	}
	if (ni->rip_tag || ni->rip_dest_mask || ni->rip_router) {
		/* MBZ fields not zero */
		printf(" [");
		rip_printblk((u_char *)&ni->rip_family,
			     (u_char *)&ni->rip_metric +
			     sizeof(ni->rip_metric));
		printf("]");
		return;
	}
	printf(" {%s}(%d)", ipaddr_string(&ni->rip_dest),
	       EXTRACT_32BITS(&ni->rip_metric));
}

static void
rip_entry_print_v2(int vers, const struct rip_netinfo *ni)
{
	u_char *p;
	u_short family;
	char buf[RIP_AUTHLEN];

	/* RFC 1723 */
	family = EXTRACT_16BITS(&ni->rip_family);
	if (family == 0xFFFF) {
		if (EXTRACT_16BITS(&ni->rip_tag) == 2) {
			memcpy(buf, &ni->rip_dest, sizeof(buf));
			buf[sizeof(buf)-1] = '\0';
			for (p = buf; *p; p++) {
				if (!isprint(*p))
					break;
			}
			if (!*p) {
				printf(" [password %s]", buf);
			} else {
				printf(" [password: ");
				rip_printblk((u_char *)&ni->rip_dest,
					     (u_char *)&ni->rip_metric +
					     sizeof(ni->rip_metric));
				printf("]");
			}
		} else {
			printf(" [auth %d:",
			       EXTRACT_16BITS(&ni->rip_tag));
			rip_printblk((u_char *)&ni->rip_dest,
				     (u_char *)&ni->rip_metric +
				     sizeof(ni->rip_metric));
			printf("]");
		}
	} else if (family != AF_INET) {
		printf(" [family %d:", family);
		rip_printblk((u_char *)&ni->rip_tag,
			     (u_char *)&ni->rip_metric +
			     sizeof(ni->rip_metric));
		printf("]");
		return;
	} else { /* AF_INET */
		printf(" {%s", ipaddr_string(&ni->rip_dest));
		if (ni->rip_dest_mask)
			printf("/%s", ipaddr_string(&ni->rip_dest_mask));
		if (ni->rip_router)
			printf("->%s", ipaddr_string(&ni->rip_router));
		if (ni->rip_tag)
			printf(" tag %04x", EXTRACT_16BITS(&ni->rip_tag));
		printf("}(%d)", EXTRACT_32BITS(&ni->rip_metric));
	}
}

void
rip_print(const u_char *dat, u_int length)
{
	const struct rip *rp;
	const struct rip_netinfo *ni;
	int i, j, trunc;

	i = min(length, snapend - dat) - sizeof(*rp);
	if (i < 0) {
		printf("[|rip]");
		return;
	}

	rp = (struct rip *)dat;
	switch (rp->rip_vers) {
	case 0:
		/* RFC 1058 */
		printf("RIPv0: ");
		rip_printblk((u_char *)(rp + 1), snapend);
		break;
	default:
		switch (rp->rip_cmd) {
		case RIPCMD_REQUEST:
			printf("RIPv%d-req %d", rp->rip_vers, length);
			break;
		case RIPCMD_RESPONSE:
			j = length / sizeof(*ni);
			if (j * sizeof(*ni) != length - 4)
				printf("RIPv%d-resp [items %d] [%d]:",
				       rp->rip_vers, j, length);
			else
				printf("RIPv%d-resp [items %d]:",
				       rp->rip_vers, j);
			trunc = (i / sizeof(*ni)) != j;
			ni = (struct rip_netinfo *)(rp + 1);
			for (; (i -= sizeof(*ni)) >= 0; ++ni) {
				if (rp->rip_vers == 1)
					rip_entry_print_v1(rp->rip_vers, ni);
				else
					rip_entry_print_v2(rp->rip_vers, ni);
			}
			if (trunc)
				printf("[|rip]");
			break;
		case RIPCMD_TRACEON:
			printf("RIPv%d-traceon %d: \"", rp->rip_vers, length);
			(void)fn_print((const u_char *)(rp + 1), snapend);
			printf("\"");
			break;
		case RIPCMD_TRACEOFF:
			printf("RIPv%d-traceoff %d", rp->rip_vers, length);
			break;
		case RIPCMD_POLL:
			printf("RIPv%d-poll %d", rp->rip_vers, length);
			break;
		case RIPCMD_POLLENTRY:
			printf("RIPv%d-pollentry %d", rp->rip_vers, length);
			break;
		default:
			printf("RIPv%d-#%d %d", rp->rip_vers, rp->rip_cmd,
			       length);
			break;
		}
        }
}

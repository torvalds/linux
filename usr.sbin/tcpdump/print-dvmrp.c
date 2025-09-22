/*	$OpenBSD: print-dvmrp.c,v 1.9 2015/11/16 00:16:39 mmcc Exp $	*/

/*
 * Copyright (c) 1995, 1996
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
#include <netinet/tcp.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "interface.h"
#include "addrtoname.h"

/*
 * DVMRP message types and flag values shamelessly stolen from
 * mrouted/dvmrp.h.
 */
#define DVMRP_PROBE		1	/* for finding neighbors */
#define DVMRP_REPORT		2	/* for reporting some or all routes */
#define DVMRP_ASK_NEIGHBORS	3	/* sent by mapper, asking for a list */
					/*
					 * of this router's neighbors
					 */
#define DVMRP_NEIGHBORS		4	/* response to such a request */
#define DVMRP_ASK_NEIGHBORS2	5	/* as above, want new format reply */
#define DVMRP_NEIGHBORS2	6
#define DVMRP_PRUNE		7	/* prune message */
#define DVMRP_GRAFT		8	/* graft message */
#define DVMRP_GRAFT_ACK		9	/* graft acknowledgement */

/*
 * 'flags' byte values in DVMRP_NEIGHBORS2 reply.
 */
#define DVMRP_NF_TUNNEL		0x01	/* neighbors reached via tunnel */
#define DVMRP_NF_SRCRT		0x02	/* tunnel uses IP source routing */
#define DVMRP_NF_DOWN		0x10	/* kernel state of interface */
#define DVMRP_NF_DISABLED	0x20	/* administratively disabled */
#define DVMRP_NF_QUERIER	0x40	/* I am the subnet's querier */

static void print_probe(const u_char *, const u_char *, u_int);
static void print_report(const u_char *, const u_char *, u_int);
static void print_neighbors(const u_char *, const u_char *, u_int);
static void print_neighbors2(const u_char *, const u_char *, u_int);
static void print_prune(const u_char *, const u_char *, u_int);
static void print_graft(const u_char *, const u_char *, u_int);
static void print_graft_ack(const u_char *, const u_char *, u_int);

static u_int32_t target_level;

void
dvmrp_print(const u_char *bp, u_int len)
{
	const u_char *ep;
	u_char type;

	ep = (const u_char *)snapend;
	if (bp >= ep)
		return;

	type = bp[1];
	bp += 8;
	/*
	 * Skip IGMP header
	 */

	len -= 8;

	switch (type) {

	case DVMRP_PROBE:
		printf(" Probe");
		if (vflag)
			print_probe(bp, ep, len);
		break;

	case DVMRP_REPORT:
		printf(" Report");
		if (vflag)
			print_report(bp, ep, len);
		break;

	case DVMRP_ASK_NEIGHBORS:
		printf(" Ask-neighbors(old)");
		break;

	case DVMRP_NEIGHBORS:
		printf(" Neighbors(old)");
		print_neighbors(bp, ep, len);
		break;

	case DVMRP_ASK_NEIGHBORS2:
		printf(" Ask-neighbors2");
		break;

	case DVMRP_NEIGHBORS2:
		printf(" Neighbors2");
		/*
		 * extract version and capabilities from IGMP group
		 * address field
		 */
		bp -= 4;
		target_level = (bp[0] << 24) | (bp[1] << 16) |
		    (bp[2] << 8) | bp[3];
		bp += 4;
		print_neighbors2(bp, ep, len);
		break;

	case DVMRP_PRUNE:
		printf(" Prune");
		print_prune(bp, ep, len);
		break;

	case DVMRP_GRAFT:
		printf(" Graft");
		print_graft(bp, ep, len);
		break;

	case DVMRP_GRAFT_ACK:
		printf(" Graft-ACK");
		print_graft_ack(bp, ep, len);
		break;

	default:
		printf(" [type %d]", type);
		break;
	}
}

static void
print_report(const u_char *bp, const u_char *ep, u_int len)
{
	u_int32_t mask, origin;
	int metric, i, width, done;

	while (len > 0) {
		if (len < 3) {
			printf(" [|]");
			return;
		}
		mask = (u_int32_t)0xff << 24 | bp[0] << 16 | bp[1] << 8 | bp[2];
		width = 1;
		if (bp[0])
			width = 2;
		if (bp[1])
			width = 3;
		if (bp[2])
			width = 4;

		printf("\n\tMask %s", intoa(htonl(mask)));
		bp += 3;
		len -= 3;
		do {
			if (bp + width + 1 > ep) {
				printf(" [|]");
				return;
			}
			if (len < width + 1) {
				printf("\n\t  [Truncated Report]");
				return;
			}
			origin = 0;
			for (i = 0; i < width; ++i)
				origin = origin << 8 | *bp++;
			for ( ; i < 4; ++i)
				origin <<= 8;

			metric = *bp++;
			done = metric & 0x80;
			metric &= 0x7f;
			printf("\n\t  %s metric %d", intoa(htonl(origin)),
				metric);
			len -= width + 1;
		} while (!done);
	}
}

#define GET_ADDR(to) (memcpy((char *)to, (char *)bp, 4), bp += 4)

static void
print_probe(const u_char *bp, const u_char *ep, u_int len)
{
	u_int32_t genid;
	u_char neighbor[4];

	if ((len < 4) || ((bp + 4) > ep)) {
		/* { (ctags) */
		printf(" [|}");
		return;
	}
	genid = (bp[0] << 24) | (bp[1] << 16) | (bp[2] << 8) | bp[3];
	bp += 4;
	len -= 4;
	printf("\n\tgenid %u", genid);

	while ((len > 0) && (bp < ep)) {
		if ((len < 4) || ((bp + 4) > ep)) {
			printf(" [|]");
			return;
		}
		GET_ADDR(neighbor);
		len -= 4;
		printf("\n\tneighbor %s", ipaddr_string(neighbor));
	}
}

static void
print_neighbors(const u_char *bp, const u_char *ep, u_int len)
{
	u_char laddr[4], neighbor[4];
	u_char metric;
	u_char thresh;
	int ncount;

	while (len > 0 && bp < ep) {
		if (len < 7 || (bp + 7) >= ep) {
			printf(" [|]");
			return;
		}
		GET_ADDR(laddr);
		metric = *bp++;
		thresh = *bp++;
		ncount = *bp++;
		len -= 7;
		while (--ncount >= 0 && (len >= 4) && (bp + 4) < ep) {
			GET_ADDR(neighbor);
			printf(" [%s ->", ipaddr_string(laddr));
			printf(" %s, (%d/%d)]",
				   ipaddr_string(neighbor), metric, thresh);
			len -= 4;
		}
	}
}

static void
print_neighbors2(const u_char *bp, const u_char *ep, u_int len)
{
	u_char laddr[4], neighbor[4];
	u_char metric, thresh, flags;
	int ncount;

	printf(" (v %d.%d):",
	       (int)target_level & 0xff,
	       (int)(target_level >> 8) & 0xff);

	while (len > 0 && bp < ep) {
		if (len < 8 || (bp + 8) >= ep) {
			printf(" [|]");
			return;
		}
		GET_ADDR(laddr);
		metric = *bp++;
		thresh = *bp++;
		flags = *bp++;
		ncount = *bp++;
		len -= 8;
		while (--ncount >= 0 && (len >= 4) && (bp + 4) <= ep) {
			GET_ADDR(neighbor);
			printf(" [%s -> ", ipaddr_string(laddr));
			printf("%s (%d/%d", ipaddr_string(neighbor),
				     metric, thresh);
			if (flags & DVMRP_NF_TUNNEL)
				printf("/tunnel");
			if (flags & DVMRP_NF_SRCRT)
				printf("/srcrt");
			if (flags & DVMRP_NF_QUERIER)
				printf("/querier");
			if (flags & DVMRP_NF_DISABLED)
				printf("/disabled");
			if (flags & DVMRP_NF_DOWN)
				printf("/down");
			printf(")]");
			len -= 4;
		}
		if (ncount != -1) {
			printf(" [|]");
			return;
		}
	}
}

static void
print_prune(const u_char *bp, const u_char *ep, u_int len)
{
	union a {
		u_char b[4];
		u_int32_t i;
	} prune_timer;

	if (len < 12 || (bp + 12) > ep) {
		printf(" [|]");
		return;
	}
	printf(" src %s grp %s", ipaddr_string(bp), ipaddr_string(bp + 4));
	bp += 8;
	GET_ADDR(prune_timer.b);
	printf(" timer %d", (int)ntohl(prune_timer.i));
}

static void
print_graft(const u_char *bp, const u_char *ep, u_int len)
{

	if (len < 8 || (bp + 8) > ep) {
		printf(" [|]");
		return;
	}
	printf(" src %s grp %s", ipaddr_string(bp), ipaddr_string(bp + 4));
}

static void
print_graft_ack(const u_char *bp, const u_char *ep, u_int len)
{

	if (len < 8 || (bp + 8) > ep) {
		printf(" [|]");
		return;
	}
	printf(" src %s grp %s", ipaddr_string(bp), ipaddr_string(bp + 4));
}

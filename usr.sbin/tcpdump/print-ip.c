/*	$OpenBSD: print-ip.c,v 1.53 2020/01/24 22:46:36 procter Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "addrtoname.h"
#include "interface.h"
#include "extract.h"			/* must come after interface.h */

/* Compatibility */
#ifndef	IPPROTO_ND
#define	IPPROTO_ND	77
#endif

#ifndef IN_CLASSD
#define IN_CLASSD(i) (((int32_t)(i) & 0xf0000000) == 0xe0000000)
#endif

/* Definitions required for ECN
   for use if the OS running tcpdump does not have ECN */
#ifndef IPTOS_ECT
#define IPTOS_ECT	0x02	/* ECN Capable Transport in IP header*/
#endif
#ifndef IPTOS_CE
#define IPTOS_CE	0x01	/* ECN Cong. Experienced in IP header*/
#endif

/* (following from ipmulti/mrouted/prune.h) */

/*
 * The packet format for a traceroute request.
 */
struct tr_query {
	u_int  tr_src;			/* traceroute source */
	u_int  tr_dst;			/* traceroute destination */
	u_int  tr_raddr;		/* traceroute response address */
#if BYTE_ORDER == BIG_ENDIAN
	struct {
		u_int   ttl : 8;	/* traceroute response ttl */
		u_int   qid : 24;	/* traceroute query id */
	} q;
#else
	struct {
		u_int	qid : 24;	/* traceroute query id */
		u_int	ttl : 8;	/* traceroute response ttl */
	} q;
#endif
};

#define tr_rttl q.ttl
#define tr_qid  q.qid

/*
 * Traceroute response format.  A traceroute response has a tr_query at the
 * beginning, followed by one tr_resp for each hop taken.
 */
struct tr_resp {
	u_int tr_qarr;			/* query arrival time */
	u_int tr_inaddr;		/* incoming interface address */
	u_int tr_outaddr;		/* outgoing interface address */
	u_int tr_rmtaddr;		/* parent address in source tree */
	u_int tr_vifin;			/* input packet count on interface */
	u_int tr_vifout;		/* output packet count on interface */
	u_int tr_pktcnt;		/* total incoming packets for src-grp */
	u_char  tr_rproto;		/* routing proto deployed on router */
	u_char  tr_fttl;		/* ttl required to forward on outvif */
	u_char  tr_smask;		/* subnet mask for src addr */
	u_char  tr_rflags;		/* forwarding error codes */
};

/* defs within mtrace */
#define TR_QUERY 1
#define TR_RESP	2

/* fields for tr_rflags (forwarding error codes) */
#define TR_NO_ERR	0
#define TR_WRONG_IF	1
#define TR_PRUNED	2
#define TR_OPRUNED	3
#define TR_SCOPED	4
#define TR_NO_RTE	5
#define TR_NO_FWD	7
#define TR_NO_SPACE	0x81
#define TR_OLD_ROUTER	0x82

/* fields for tr_rproto (routing protocol) */
#define TR_PROTO_DVMRP	1
#define TR_PROTO_MOSPF	2
#define TR_PROTO_PIM	3
#define TR_PROTO_CBT	4

static void print_mtrace(const u_char *bp, u_int len)
{
	struct tr_query *tr = (struct tr_query *)(bp + 8);

	printf("mtrace %d: %s to %s reply-to %s", tr->tr_qid,
	    ipaddr_string(&tr->tr_src), ipaddr_string(&tr->tr_dst),
	    ipaddr_string(&tr->tr_raddr));
	if (IN_CLASSD(ntohl(tr->tr_raddr)))
		printf(" with-ttl %d", tr->tr_rttl);
}

static void print_mresp(const u_char *bp, u_int len)
{
	struct tr_query *tr = (struct tr_query *)(bp + 8);

	printf("mresp %d: %s to %s reply-to %s", tr->tr_qid,
	    ipaddr_string(&tr->tr_src), ipaddr_string(&tr->tr_dst),
	    ipaddr_string(&tr->tr_raddr));
	if (IN_CLASSD(ntohl(tr->tr_raddr)))
		printf(" with-ttl %d", tr->tr_rttl);
}

static void
igmp_print(const u_char *bp, u_int len, const u_char *bp2)
{
	const struct ip *ip;

	ip = (const struct ip *)bp2;
        printf("%s > %s: ",
	    ipaddr_string(&ip->ip_src),
	    ipaddr_string(&ip->ip_dst));

	TCHECK2(bp[0], 8);
	switch (bp[0]) {
	case 0x11:
		printf("igmp query");
		if (*(int *)&bp[4])
			printf(" [gaddr %s]", ipaddr_string(&bp[4]));
		if (len != 8)
			printf(" [len %d]", len);
		break;
	case 0x12:
		printf("igmp report %s", ipaddr_string(&bp[4]));
		if (len != 8)
			printf(" [len %d]", len);
		break;
	case 0x16:
		printf("igmp nreport %s", ipaddr_string(&bp[4]));
		break;
	case 0x17:
		printf("igmp leave %s", ipaddr_string(&bp[4]));
		break;
	case 0x13:
		printf("igmp dvmrp");
		if (len < 8)
			printf(" [len %d]", len);
		else
			dvmrp_print(bp, len);
		break;
	case 0x14:
		printf("igmp pim");
		pim_print(bp, len);
  		break;
	case 0x1e:
		print_mresp(bp, len);
		break;
	case 0x1f:
		print_mtrace(bp, len);
		break;
	default:
		printf("igmp-%d", bp[0] & 0xf);
		break;
	}
	if ((bp[0] >> 4) != 1)
		printf(" [v%d]", bp[0] >> 4);

	TCHECK2(bp[0], len);
	if (vflag) {
		/* Check the IGMP checksum */
		u_int32_t sum = 0;
		int count;
		const u_short *sp = (u_short *)bp;
		
		for (count = len / 2; --count >= 0; )
			sum += *sp++;
		if (len & 1)
			sum += ntohs(*(u_char *) sp << 8);
		while (sum >> 16)
			sum = (sum & 0xffff) + (sum >> 16);
		sum = 0xffff & ~sum;
		if (sum != 0)
			printf(" bad igmp cksum %x!", EXTRACT_16BITS(&bp[2]));
	}
	return;
trunc:
	printf("[|igmp]");
}

/*
 * print the recorded route in an IP RR, LSRR or SSRR option.
 */
static void
ip_printroute(const char *type, const u_char *cp, u_int length)
{
	u_int ptr = cp[2] - 1;
	u_int len;

	printf(" %s{", type);
	if ((length + 1) & 3)
		printf(" [bad length %d]", length);
	if (ptr < 3 || ((ptr + 1) & 3) || ptr > length + 1)
		printf(" [bad ptr %d]", cp[2]);

	type = "";
	for (len = 3; len < length; len += 4) {
		if (ptr == len)
			type = "#";
		printf("%s%s", type, ipaddr_string(&cp[len]));
		type = " ";
	}
	printf("%s}", ptr == len? "#" : "");
}

/*
 * print IP options.
 */
static void
ip_optprint(const u_char *cp, u_int length)
{
	u_int len;
	int tt;

	for (; length > 0; cp += len, length -= len) {
		TCHECK(cp[1]);
		tt = *cp;
		len = (tt == IPOPT_NOP || tt == IPOPT_EOL) ? 1 : cp[1];
		if (len <= 0) {
			printf("[|ip op len %d]", len);
			return;
		}
		if (&cp[1] >= snapend || cp + len > snapend) {
			printf("[|ip]");
			return;
		}
		switch (tt) {

		case IPOPT_EOL:
			printf(" EOL");
			if (length > 1)
				printf("-%d", length - 1);
			return;

		case IPOPT_NOP:
			printf(" NOP");
			break;

		case IPOPT_TS:
			printf(" TS{%d}", len);
			break;

		case IPOPT_SECURITY:
			printf(" SECURITY{%d}", len);
			break;

		case IPOPT_RR:
			printf(" RR{%d}=", len);
			ip_printroute("RR", cp, len);
			break;

		case IPOPT_SSRR:
			ip_printroute("SSRR", cp, len);
			break;

		case IPOPT_LSRR:
			ip_printroute("LSRR", cp, len);
			break;

		default:
			printf(" IPOPT-%d{%d}", cp[0], len);
			break;
		}
	}
	return;

trunc:
	printf("[|ip]");
}

/*
 * print an IP datagram.
 */
void
ip_print(const u_char *bp, u_int length)
{
	const struct ip *ip;
	u_int hlen, len, off;
	const u_char *cp;
	const u_char *pktp = packetp;
	const u_char *send = snapend;

	TCHECK2(bp[0], 1);
	ip = (const struct ip *)bp;

	/*
	 * If the IP header is not aligned, copy into abuf.
	 * This will never happen with BPF.  It does happen with raw packet
	 * dumps from -r.
	 */
	if ((intptr_t)ip & (sizeof(u_int32_t)-1)) {
		static u_char *abuf = NULL;
		static int didwarn = 0;
		int clen = snapend - bp;

		if (clen > snaplen)
			clen = snaplen;
		if (abuf == NULL) {
			abuf = malloc(snaplen);
			if (abuf == NULL)
				error("ip_print: malloc");
		}
		memmove((char *)abuf, (char *)ip, min(length, clen));
		snapend = abuf + clen;
		packetp = abuf;
		ip = (struct ip *)abuf;
		/* We really want libpcap to give us aligned packets */
		if (!didwarn) {
			warning("compensating for unaligned libpcap packets");
			++didwarn;
		}
	}

	TCHECK(*ip);
	if (ip->ip_v != IPVERSION) {
		printf("bad-ip-version %u", ip->ip_v);
		goto out;
	}

	len = ntohs(ip->ip_len);
	if (length < len) {
		printf("truncated-ip - %d bytes missing!",
		    len - length);
		len = length;
	}

	hlen = ip->ip_hl * 4;
	if (hlen < sizeof(struct ip) || hlen > len) {
		printf("bad-hlen %d", hlen);
		goto out;
	}

	len -= hlen;

	/*
	 * If this is fragment zero, hand it to the next higher
	 * level protocol.
	 */
	off = ntohs(ip->ip_off);
	if ((off & 0x1fff) == 0) {
		cp = (const u_char *)ip + hlen;
		if (cp > snapend)
			goto trunc;
		switch (ip->ip_p) {

		case IPPROTO_TCP:
			tcp_print(cp, len, (const u_char *)ip);
			break;

		case IPPROTO_UDP:
			udp_print(cp, len, (const u_char *)ip);
			break;

		case IPPROTO_ICMP:
			icmp_print(cp, len, (const u_char *)ip);
			break;

#ifndef IPPROTO_IGRP
#define IPPROTO_IGRP 9
#endif
		case IPPROTO_IGRP:
			igrp_print(cp, len, (const u_char *)ip);
			break;

		case IPPROTO_ND:
			printf("%s > %s:", ipaddr_string(&ip->ip_src),
			    ipaddr_string(&ip->ip_dst));
			printf(" nd %d", len);
			break;

#ifndef IPPROTO_OSPF
#define IPPROTO_OSPF 89
#endif
		case IPPROTO_OSPF:
			ospf_print(cp, len, (const u_char *)ip);
			break;

#ifndef IPPROTO_IGMP
#define IPPROTO_IGMP 2
#endif
		case IPPROTO_IGMP:
			igmp_print(cp, len, (const u_char *)ip);
			break;

#ifndef IPPROTO_IPIP
#define IPPROTO_IPIP 4
#endif
		case IPPROTO_IPIP:
			/* ip-in-ip encapsulation */
			if (vflag)
				printf("%s > %s: ",
				    ipaddr_string(&ip->ip_src),
				    ipaddr_string(&ip->ip_dst));
			ip_print(cp, len);
			if (! vflag) {
				printf(" (encap)");
				goto out;
			}
			break;

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif
		case IPPROTO_IPV6:
			/* ip6-in-ip encapsulation */
			if (vflag)
				printf("%s > %s: ",
				    ipaddr_string(&ip->ip_src),
				    ipaddr_string(&ip->ip_dst));
			ip6_print(cp, len);
			if (! vflag) {
 				printf(" (encap)");
				goto out;
 			}
 			break;

#ifndef IPPROTO_GRE
#define IPPROTO_GRE 47
#endif
		case IPPROTO_GRE:
			printf("%s > %s: ",
			    ipaddr_string(&ip->ip_src),
			    ipaddr_string(&ip->ip_dst));
			/* do it */
			gre_print(cp, len);
  			break;

#ifndef IPPROTO_ESP
#define IPPROTO_ESP 50
#endif
		case IPPROTO_ESP:
			printf("%s > %s: ",
			    ipaddr_string(&ip->ip_src),
			    ipaddr_string(&ip->ip_dst));
			esp_print(cp, len, (const u_char *)ip);
			break;

#ifndef IPPROTO_AH
#define IPPROTO_AH 51
#endif
		case IPPROTO_AH:
			ah_print(cp, len, (const u_char *)ip);
			break;

#ifndef IPPROTO_MOBILE
#define IPPROTO_MOBILE 55
#endif
		case IPPROTO_MOBILE:
			if (vflag)
				printf("mobile %s > %s: ",
				    ipaddr_string(&ip->ip_src),
				    ipaddr_string(&ip->ip_dst));
			mobile_print(cp, len);
			if (! vflag) {
				printf(" (mobile encap)");
				goto out;
			}
			break;

#ifndef IPPROTO_ETHERIP
#define IPPROTO_ETHERIP	97
#endif
		case IPPROTO_ETHERIP:
			printf("%s > %s: ",
			    ipaddr_string(&ip->ip_src),
			    ipaddr_string(&ip->ip_dst));
			etherip_print(cp, snapend - cp, len);
			break;

#ifndef	IPPROTO_IPCOMP
#define	IPPROTO_IPCOMP	108
#endif
		case IPPROTO_IPCOMP:
			ipcomp_print(cp, len, (const u_char *)ip);
			break;

#ifndef IPPROTO_CARP  
#define IPPROTO_CARP 112
#endif
		case IPPROTO_CARP:
			if (packettype == PT_VRRP) {
				if (vflag)
					printf("vrrp %s > %s: ",
					    ipaddr_string(&ip->ip_src),
					    ipaddr_string(&ip->ip_dst));
				vrrp_print(cp, len, ip->ip_ttl);
			} else {
				if (vflag)
					printf("carp %s > %s: ",
					    ipaddr_string(&ip->ip_src),
					    ipaddr_string(&ip->ip_dst));
				carp_print(cp, len, ip->ip_ttl);
			}
			break;

#ifndef IPPROTO_PFSYNC  
#define IPPROTO_PFSYNC 240
#endif
		case IPPROTO_PFSYNC:
			pfsync_ip_print(cp,
			    (int)(snapend - (u_char *)ip) - hlen,
			    (const u_char *)ip);
			break;

		default:
			printf("%s > %s:",
			    ipaddr_string(&ip->ip_src),
			    ipaddr_string(&ip->ip_dst));
			printf(" ip-proto-%d %d", ip->ip_p, len);
			break;
		}
	}
	/*
	 * for fragmented datagrams, print id:size@offset.  On all
	 * but the last stick a "+".  For unfragmented datagrams, note
	 * the don't fragment flag.
	 */
	if (off & 0x3fff) {
		/*
		 * if this isn't the first frag, we're missing the
		 * next level protocol header.  print the ip addr.
		 */
		if (off & 0x1fff)
			printf("%s > %s:",
			    ipaddr_string(&ip->ip_src),
			    ipaddr_string(&ip->ip_dst));
		printf(" (frag %d:%d@%d%s)",
		    ntohs(ip->ip_id), len,
		    (off & 0x1fff) * 8,
		    (off & IP_MF)? "+" : "");
	} 
	if (off & IP_DF)
		printf(" (DF)");

	if (ip->ip_tos) {
		printf(" [tos 0x%x", (int)ip->ip_tos);
		if (ip->ip_tos & (IPTOS_CE|IPTOS_ECT)) {
			printf(" (");
			if (ip->ip_tos & IPTOS_ECT) {
				/* ECN-capable transport */
				putchar('E');
			}
			if (ip->ip_tos & IPTOS_CE) {
				/* _C_ongestion experienced (ECN) */
				putchar('C'); 
			}
			printf(")");
  		}
		printf("]");
	}

	if (ip->ip_ttl <= 1)
		printf(" [ttl %d]", (int)ip->ip_ttl);

	if (vflag) {
		char *sep = "";

		printf(" (");
		if (ip->ip_ttl > 1) {
			printf("%sttl %d", sep, (int)ip->ip_ttl);
			sep = ", ";
		}
		if ((off & 0x3fff) == 0) {
			printf("%sid %d", sep, (int)ntohs(ip->ip_id));
			sep = ", ";
		}
		printf("%slen %u", sep, ntohs(ip->ip_len));
		sep = ", ";
		if ((u_char *)ip + hlen <= snapend) {
			u_int16_t sum, ip_sum;
			sum = in_cksum((const u_short *)ip, hlen, 0);
			if (sum != 0) {
				ip_sum = EXTRACT_16BITS(&ip->ip_sum);
				printf("%sbad ip cksum %x! -> %x", sep, ip_sum,
				    in_cksum_shouldbe(ip_sum, sum));
				sep = ", ";
			}
		}
		if (hlen > sizeof(struct ip)) {
			hlen -= sizeof(struct ip);
			printf("%soptlen=%d", sep, hlen);
			ip_optprint((u_char *)(ip + 1), hlen);
		}
		printf(")");
	}
out:
	packetp = pktp;
	snapend = send;
	return;

trunc:
	printf("[|ip]");
}

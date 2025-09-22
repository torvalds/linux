/*	$OpenBSD: print-icmp.c,v 1.27 2021/12/01 18:28:46 deraadt Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1993, 1994, 1995, 1996
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

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>

#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"			/* must come after interface.h */

/* rfc1700 */
#ifndef ICMP_UNREACH_NET_UNKNOWN
#define ICMP_UNREACH_NET_UNKNOWN	6	/* destination net unknown */
#endif
#ifndef ICMP_UNREACH_HOST_UNKNOWN
#define ICMP_UNREACH_HOST_UNKNOWN	7	/* destination host unknown */
#endif
#ifndef ICMP_UNREACH_ISOLATED
#define ICMP_UNREACH_ISOLATED		8	/* source host isolated */
#endif
#ifndef ICMP_UNREACH_NET_PROHIB
#define ICMP_UNREACH_NET_PROHIB		9	/* admin prohibited net */
#endif
#ifndef ICMP_UNREACH_HOST_PROHIB
#define ICMP_UNREACH_HOST_PROHIB	10	/* admin prohibited host */
#endif
#ifndef ICMP_UNREACH_TOSNET
#define ICMP_UNREACH_TOSNET		11	/* tos prohibited net */
#endif
#ifndef ICMP_UNREACH_TOSHOST
#define ICMP_UNREACH_TOSHOST		12	/* tos prohibited host */
#endif

/* rfc1716 */
#ifndef ICMP_UNREACH_FILTER_PROHIB
#define ICMP_UNREACH_FILTER_PROHIB	13	/* admin prohibited filter */
#endif
#ifndef ICMP_UNREACH_HOST_PRECEDENCE
#define ICMP_UNREACH_HOST_PRECEDENCE	14	/* host precedence violation */
#endif
#ifndef ICMP_UNREACH_PRECEDENCE_CUTOFF
#define ICMP_UNREACH_PRECEDENCE_CUTOFF	15	/* precedence cutoff */
#endif

/* rfc1256 */
#ifndef ICMP_ROUTERADVERT
#define ICMP_ROUTERADVERT		9	/* router advertisement */
#endif
#ifndef ICMP_ROUTERSOLICIT
#define ICMP_ROUTERSOLICIT		10	/* router solicitation */
#endif

#define ICMP_INFOTYPE(type) \
    ((type) == ICMP_ECHOREPLY || (type) == ICMP_ECHO || \
    (type) == ICMP_ROUTERADVERT || (type) == ICMP_ROUTERSOLICIT || \
    (type) == ICMP_TSTAMP || (type) == ICMP_TSTAMPREPLY || \
    (type) == ICMP_IREQ || (type) == ICMP_IREQREPLY || \
    (type) == ICMP_MASKREQ || (type) == ICMP_MASKREPLY)

/* Most of the icmp types */
static struct tok icmp2str[] = {
	{ ICMP_ECHOREPLY,		"echo reply" },
	{ ICMP_SOURCEQUENCH,		"source quench" },
	{ ICMP_ECHO,			"echo request" },
	{ ICMP_ROUTERSOLICIT,		"router solicitation" },
	{ ICMP_TSTAMP,			"time stamp request" },
	{ ICMP_TSTAMPREPLY,		"time stamp reply" },
	{ ICMP_IREQ,			"information request" },
	{ ICMP_IREQREPLY,		"information reply" },
	{ ICMP_MASKREQ,			"address mask request" },
	{ 0,				NULL }
};

/* Formats for most of the ICMP_UNREACH codes */
static struct tok unreach2str[] = {
	{ ICMP_UNREACH_NET,		"net %s unreachable" },
	{ ICMP_UNREACH_HOST,		"host %s unreachable" },
	{ ICMP_UNREACH_SRCFAIL,
	    "%s unreachable - source route failed" },
	{ ICMP_UNREACH_NET_UNKNOWN,	"net %s unreachable - unknown" },
	{ ICMP_UNREACH_HOST_UNKNOWN,	"host %s unreachable - unknown" },
	{ ICMP_UNREACH_ISOLATED,
	    "%s unreachable - source host isolated" },
	{ ICMP_UNREACH_NET_PROHIB,
	    "net %s unreachable - admin prohibited" },
	{ ICMP_UNREACH_HOST_PROHIB,
	    "host %s unreachable - admin prohibited" },
	{ ICMP_UNREACH_TOSNET,
	    "net %s unreachable - tos prohibited" },
	{ ICMP_UNREACH_TOSHOST,
	    "host %s unreachable - tos prohibited" },
	{ ICMP_UNREACH_FILTER_PROHIB,
	    "host %s unreachable - admin prohibited filter" },
	{ ICMP_UNREACH_HOST_PRECEDENCE,
	    "host %s unreachable - host precedence violation" },
	{ ICMP_UNREACH_PRECEDENCE_CUTOFF,
	    "host %s unreachable - precedence cutoff" },
	{ 0,				NULL }
};

/* Formats for the ICMP_REDIRECT codes */
static struct tok type2str[] = {
	{ ICMP_REDIRECT_NET,		"redirect %s to net %s" },
	{ ICMP_REDIRECT_HOST,		"redirect %s to host %s" },
	{ ICMP_REDIRECT_TOSNET,		"redirect-tos %s to net %s" },
	{ ICMP_REDIRECT_TOSHOST,	"redirect-tos %s to net %s" },
	{ 0,				NULL }
};

/* rfc1191 */
struct mtu_discovery {
	short unused;
	short nexthopmtu;
};

/* rfc1256 */
struct ih_rdiscovery {
	u_char ird_addrnum;
	u_char ird_addrsiz;
	u_short ird_lifetime;
};

struct id_rdiscovery {
	u_int32_t ird_addr;
	u_int32_t ird_pref;
};

void
icmp_print(const u_char *bp, u_int length, const u_char *bp2)
{
	const struct icmp *dp;
	const struct ip *ip;
	const char *str, *fmt;
	const struct ip *oip;
	const struct udphdr *ouh;
	u_int hlen, dport, mtu;
	char buf[HOST_NAME_MAX+1+256];
	char buf2[HOST_NAME_MAX+1+256];

	dp = (struct icmp *)bp;
	ip = (struct ip *)bp2;
	str = buf;

        printf("%s > %s: ",
	    ipaddr_string(&ip->ip_src),
	    ipaddr_string(&ip->ip_dst));

	TCHECK(dp->icmp_code);
	if (qflag) 
		(void) snprintf(buf, sizeof buf, "%u %u", dp->icmp_type,
		    dp->icmp_code);
	else switch (dp->icmp_type) {

	case ICMP_ECHOREPLY:
	case ICMP_ECHO:
		if (vflag) {
			TCHECK(dp->icmp_seq);
			(void)snprintf(buf, sizeof buf,
				       "echo %s (id:%04x seq:%u)",
				       (dp->icmp_type == ICMP_ECHO)?
				       "request": "reply",
				       ntohs(dp->icmp_id),
				       ntohs(dp->icmp_seq));
		} else
			str = tok2str(icmp2str, "type-#%u", dp->icmp_type);
		break;

	case ICMP_UNREACH:
		TCHECK(dp->icmp_ip.ip_dst);
		switch (dp->icmp_code) {

		case ICMP_UNREACH_PROTOCOL:
			TCHECK(dp->icmp_ip.ip_p);
			(void)snprintf(buf, sizeof buf,
				       "%s protocol %u unreachable",
				       ipaddr_string(&dp->icmp_ip.ip_dst),
				       dp->icmp_ip.ip_p);
			break;

		case ICMP_UNREACH_PORT:
			TCHECK(dp->icmp_ip.ip_p);
			oip = &dp->icmp_ip;
			hlen = oip->ip_hl * 4;
			ouh = (struct udphdr *)(((u_char *)oip) + hlen);
			TCHECK(ouh->uh_dport);
			dport = ntohs(ouh->uh_dport);
			switch (oip->ip_p) {

			case IPPROTO_TCP:
				(void)snprintf(buf, sizeof buf,
					"%s tcp port %s unreachable",
					ipaddr_string(&oip->ip_dst),
					tcpport_string(dport));
				break;

			case IPPROTO_UDP:
				(void)snprintf(buf, sizeof buf,
					"%s udp port %s unreachable",
					ipaddr_string(&oip->ip_dst),
					udpport_string(dport));
				break;

			default:
				(void)snprintf(buf, sizeof buf,
					"%s protocol %u port %u unreachable",
					ipaddr_string(&oip->ip_dst),
					oip->ip_p, dport);
				break;
			}
			break;

		case ICMP_UNREACH_NEEDFRAG:
			{
			const struct mtu_discovery *mp;

			mp = (struct mtu_discovery *)&dp->icmp_void;
                        mtu = EXTRACT_16BITS(&mp->nexthopmtu);
                        if (mtu)
			    (void)snprintf(buf, sizeof buf,
				"%s unreachable - need to frag (mtu %u)",
				ipaddr_string(&dp->icmp_ip.ip_dst), mtu);
                        else
			    (void)snprintf(buf, sizeof buf,
				"%s unreachable - need to frag",
				ipaddr_string(&dp->icmp_ip.ip_dst));
			}
			break;

		default:
			fmt = tok2str(unreach2str, "#%u %%s unreachable",
			    dp->icmp_code);
			(void)snprintf(buf, sizeof buf, fmt,
			    ipaddr_string(&dp->icmp_ip.ip_dst));
			break;
		}
		break;

	case ICMP_REDIRECT:
		TCHECK(dp->icmp_ip.ip_dst);
		fmt = tok2str(type2str, "redirect-#%u %%s to net %%s",
		    dp->icmp_code);
		(void)snprintf(buf, sizeof buf, fmt,
		    ipaddr_string(&dp->icmp_ip.ip_dst),
		    ipaddr_string(&dp->icmp_gwaddr));
		break;

	case ICMP_ROUTERADVERT:
		{
		const struct ih_rdiscovery *ihp;
		const struct id_rdiscovery *idp;
		u_int lifetime, num, size;

		(void)strlcpy(buf, "router advertisement", sizeof(buf));

		ihp = (struct ih_rdiscovery *)&dp->icmp_void;
		TCHECK(*ihp);
		(void)strlcat(buf, " lifetime ", sizeof(buf));
		lifetime = EXTRACT_16BITS(&ihp->ird_lifetime);
		if (lifetime < 60)
			(void)snprintf(buf2, sizeof(buf2), "%u", lifetime);
		else if (lifetime < 60 * 60)
			(void)snprintf(buf2, sizeof(buf2), "%u:%02u",
			    lifetime / 60, lifetime % 60);
		else
			(void)snprintf(buf2, sizeof(buf2), "%u:%02u:%02u",
			    lifetime / 3600, (lifetime % 3600) / 60,
			    lifetime % 60);
		strlcat(buf, buf2, sizeof(buf));

		num = ihp->ird_addrnum;
		(void)snprintf(buf2, sizeof(buf2), " %u:", num);
		strlcat(buf, buf2, sizeof(buf));

		size = ihp->ird_addrsiz;
		if (size != 2) {
			(void)snprintf(buf2, sizeof(buf2), " [size %u]", size);
			strlcat(buf, buf2, sizeof(buf));
			break;
		}
		idp = (struct id_rdiscovery *)&dp->icmp_data;
		while (num-- > 0) {
			TCHECK(*idp);
			(void)snprintf(buf2, sizeof(buf2), " {%s %u}",
			    ipaddr_string(&idp->ird_addr),
			    EXTRACT_32BITS(&idp->ird_pref));
			strlcat(buf, buf2, sizeof(buf));
		}
		}
		break;

	case ICMP_TIMXCEED:
		TCHECK(dp->icmp_ip.ip_dst);
		switch (dp->icmp_code) {

		case ICMP_TIMXCEED_INTRANS:
			str = "time exceeded in-transit";
			break;

		case ICMP_TIMXCEED_REASS:
			str = "ip reassembly time exceeded";
			break;

		default:
			(void)snprintf(buf, sizeof buf,
				"time exceeded-#%u", dp->icmp_code);
			break;
		}
		break;

	case ICMP_PARAMPROB:
		switch (dp->icmp_code) {
		case ICMP_PARAMPROB_OPTABSENT:
			str = "requested option absent";
			break;
		case ICMP_PARAMPROB_LENGTH:
			snprintf(buf, sizeof buf, "bad length %u", dp->icmp_pptr);
			break;
		default:
			TCHECK(dp->icmp_pptr);
			(void)snprintf(buf, sizeof buf,
				"parameter problem - octet %u",
				dp->icmp_pptr);
			break;
		}
		break;

	case ICMP_MASKREPLY:
		TCHECK(dp->icmp_mask);
		(void)snprintf(buf, sizeof buf, "address mask is 0x%08x",
		    (u_int32_t)ntohl(dp->icmp_mask));
		break;

	default:
		str = tok2str(icmp2str, "type-#%u", dp->icmp_type);
		break;
	}
	printf("icmp: %s", str);
	if (vflag) {
		if (TTEST2(dp->icmp_type, length)) {
			u_int16_t sum, icmp_sum;
			sum = in_cksum((const u_short *)dp, length, 0);
			if (sum != 0) {
				icmp_sum = EXTRACT_16BITS(&dp->icmp_cksum);
				printf(" [bad icmp cksum %x! -> %x]", icmp_sum,
				    in_cksum_shouldbe(icmp_sum, sum));
			}
			else
				printf(" [icmp cksum ok]");
		}
	}
	if (vflag > 1 && !ICMP_INFOTYPE(dp->icmp_type) &&
	    TTEST(dp->icmp_ip)) {
		printf(" for ");
		oip = &dp->icmp_ip;
		ip_print((u_char *)oip, ntohs(oip->ip_len));
	}
	return;
trunc:
	printf("[|icmp]");
}

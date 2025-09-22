/*	$OpenBSD: nametoaddr.c,v 1.25 2024/04/05 18:01:56 deraadt Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996
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
 *
 * Name to id translation routines used by the scanner.
 * These functions are not time critical.
 */

#include <sys/types.h>				/* concession to AIX */
#include <sys/socket.h>
#include <sys/time.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>

#include "pcap-int.h"

#include "gencode.h"
#include <pcap-namedb.h>

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#ifndef NTOHL
#define NTOHL(x) (x) = ntohl(x)
#define NTOHS(x) (x) = ntohs(x)
#endif

static __inline int xdtoi(int);

/*
 *  Convert host name to internet address.
 *  Return 0 upon failure.
 */
bpf_u_int32 **
pcap_nametoaddr(const char *name)
{
#ifndef h_addr
	static bpf_u_int32 *hlist[2];
#endif
	bpf_u_int32 **p;
	struct hostent *hp;

	if ((hp = gethostbyname(name)) != NULL) {
#ifndef h_addr
		hlist[0] = (bpf_u_int32 *)hp->h_addr;
		NTOHL(hp->h_addr);
		return hlist;
#else
		for (p = (bpf_u_int32 **)hp->h_addr_list; *p; ++p)
			NTOHL(**p);
		return (bpf_u_int32 **)hp->h_addr_list;
#endif
	}
	else
		return 0;
}

#ifdef INET6
struct addrinfo *
pcap_nametoaddrinfo(const char *name)
{
	struct addrinfo hints, *res;
	int error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;	/*not really*/
	error = getaddrinfo(name, NULL, &hints, &res);
	if (error)
		return NULL;
	else
		return res;
}
#endif /*INET6*/

/*
 *  Convert net name to internet address.
 *  Return 0 upon failure.
 */
bpf_u_int32
pcap_nametonetaddr(const char *name)
{
	struct netent *np;

	if ((np = getnetbyname(name)) != NULL)
		return np->n_net;
	else
		return 0;
}

/*
 * Convert a port name to its port and protocol numbers.
 * We assume only TCP or UDP.
 * Return 0 upon failure.
 */
int
pcap_nametoport(const char *name, int *port, int *proto)
{
	struct servent *sp;
	char *other;

	sp = getservbyname(name, (char *)0);
	if (sp != NULL) {
		NTOHS(sp->s_port);
		*port = sp->s_port;
		*proto = pcap_nametoproto(sp->s_proto);
		/*
		 * We need to check /etc/services for ambiguous entries.
		 * If we find the ambiguous entry, and it has the
		 * same port number, change the proto to PROTO_UNDEF
		 * so both TCP and UDP will be checked.
		 */
		if (*proto == IPPROTO_TCP)
			other = "udp";
		else
			other = "tcp";

		sp = getservbyname(name, other);
		if (sp != 0) {
			NTOHS(sp->s_port);
#ifdef notdef
			if (*port != sp->s_port)
				/* Can't handle ambiguous names that refer
				   to different port numbers. */
				warning("ambiguous port %s in /etc/services",
					name);
#endif
			*proto = PROTO_UNDEF;
		}
		return 1;
	}
#if defined(ultrix) || defined(__osf__)
	/* Special hack in case NFS isn't in /etc/services */
	if (strcmp(name, "nfs") == 0) {
		*port = 2049;
		*proto = PROTO_UNDEF;
		return 1;
	}
#endif
	return 0;
}

int
pcap_nametoproto(const char *str)
{
	struct protoent *p;

	p = getprotobyname(str);
	if (p != 0)
		return p->p_proto;
	else
		return PROTO_UNDEF;
}

#include "ethertype.h"

struct eproto {
	char *s;
	u_short p;
};

/* Static data base of ether protocol types. */
static const struct eproto _eproto_db[] = {
	{ "pup", ETHERTYPE_PUP },
	{ "xns", ETHERTYPE_NS },
	{ "ip", ETHERTYPE_IP },
#ifdef INET6
	{ "ip6", ETHERTYPE_IPV6 },
#endif
	{ "arp", ETHERTYPE_ARP },
	{ "rarp", ETHERTYPE_REVARP },
	{ "lldp", ETHERTYPE_LLDP },
	{ "slow", ETHERTYPE_SLOW },
	{ "sprite", ETHERTYPE_SPRITE },
	{ "mopdl", ETHERTYPE_MOPDL },
	{ "moprc", ETHERTYPE_MOPRC },
	{ "decnet", ETHERTYPE_DN },
	{ "lat", ETHERTYPE_LAT },
	{ "sca", ETHERTYPE_SCA },
	{ "lanbridge", ETHERTYPE_LANBRIDGE },
	{ "vexp", ETHERTYPE_VEXP },
	{ "vprod", ETHERTYPE_VPROD },
	{ "atalk", ETHERTYPE_ATALK },
	{ "atalkarp", ETHERTYPE_AARP },
	{ "loopback", ETHERTYPE_LOOPBACK },
	{ "decdts", ETHERTYPE_DECDTS },
	{ "decdns", ETHERTYPE_DECDNS },
	{ (char *)0, 0 }
};
/* Accessor for tcpdump */
const struct eproto * const eproto_db = _eproto_db;

int
pcap_nametoeproto(const char *s)
{
	const struct eproto *p = _eproto_db;

	while (p->s != 0) {
		if (strcmp(p->s, s) == 0)
			return p->p;
		p += 1;
	}
	return PROTO_UNDEF;
}

#include "llc.h"

/* Static data base of LLC values. */
static const struct eproto llc_db[] = {
	{ "stp", LLCSAP_8021D },
	{ (char *)0, 0 }
};

int
pcap_nametollc(const char *s)
{
	const struct eproto *p = llc_db;

	while (p->s != 0) {
		if (strcmp(p->s, s) == 0)
			return p->p;
		p += 1;
	}
	return PROTO_UNDEF;
}

/* Hex digit to integer. */
static __inline int
xdtoi(int c)
{
	if (isdigit(c))
		return c - '0';
	else if (islower(c))
		return c - 'a' + 10;
	else
		return c - 'A' + 10;
}

int
__pcap_atoin(const char *s, bpf_u_int32 *addr)
{
	u_int n;
	int len;

	*addr = 0;
	len = 0;
	while (1) {
		n = 0;
		while (*s && *s != '.')
			n = n * 10 + *s++ - '0';
		*addr <<= 8;
		*addr |= n & 0xff;
		len += 8;
		if (*s == '\0')
			return len;
		++s;
	}
	/* NOTREACHED */
}

int
__pcap_atodn(const char *s, bpf_u_int32 *addr)
{
#define AREASHIFT 10
#define AREAMASK 0176000
#define NODEMASK 01777

	u_int node, area;

	if (sscanf((char *)s, "%d.%d", &area, &node) != 2)
		bpf_error("malformed decnet address '%s'", s);

	*addr = (area << AREASHIFT) & AREAMASK;
	*addr |= (node & NODEMASK);

	return(32);
}

/*
 * Convert 's' which has the form "xx:xx:xx:xx:xx:xx" into a new
 * ethernet address.  Assumes 's' is well formed.
 */
u_char *
pcap_ether_aton(const char *s)
{
	u_char *ep, *e;
	u_int d;

	e = ep = malloc(6);
	if (e == NULL)
		bpf_error("malloc");

	while (*s) {
		if (*s == ':')
			s += 1;
		d = xdtoi(*s++);
		if (isxdigit((unsigned char)*s)) {
			d <<= 4;
			d |= xdtoi(*s++);
		}
		*ep++ = d;
	}

	return (e);
}

#ifndef HAVE_ETHER_HOSTTON
/* Roll our own */
u_char *
pcap_ether_hostton(const char *name)
{
	struct pcap_etherent *ep;
	u_char *ap;
	static FILE *fp = NULL;
	static init = 0;

	if (!init) {
		fp = fopen(PCAP_ETHERS_FILE, "r");
		++init;
		if (fp == NULL)
			return (NULL);
	} else if (fp == NULL)
		return (NULL);
	else
		rewind(fp);
	
	while ((ep = pcap_next_etherent(fp)) != NULL) {
		if (strcmp(ep->name, name) == 0) {
			ap = malloc(6);
			if (ap != NULL) {
				memcpy(ap, ep->addr, 6);
				return (ap);
			}
			break;
		}
	}
	return (NULL);
}
#else

/* Use the os supplied routines */
u_char *
pcap_ether_hostton(const char *name)
{
	u_char *ap;
	u_char a[6];

	ap = NULL;
	if (ether_hostton(name, (struct ether_addr *)a) == 0) {
		ap = malloc(6);
		if (ap != NULL)
			memcpy((char *)ap, (char *)a, 6);
	}
	return (ap);
}
#endif

u_short
__pcap_nametodnaddr(const char *name)
{
#ifdef	DECNETLIB
	struct nodeent *getnodebyname();
	struct nodeent *nep;
	unsigned short res;

	nep = getnodebyname(name);
	if (nep == ((struct nodeent *)0))
		bpf_error("unknown decnet host name '%s'\n", name);

	memcpy((char *)&res, (char *)nep->n_addr, sizeof(unsigned short));
	return(res);
#else
	bpf_error("decnet name support not included, '%s' cannot be translated\n",
		name);
	/* NOTREACHED */
#endif
}

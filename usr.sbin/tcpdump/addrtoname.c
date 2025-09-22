/*	$OpenBSD: addrtoname.c,v 1.40 2021/12/01 18:28:45 deraadt Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
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
 *  Internet, ethernet, port, and protocol string to address
 *  and address to string conversion routines
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip6.h>

#include <arpa/inet.h>

#include <ctype.h>
#include <inttypes.h>
#include <netdb.h>
#include <pcap.h>
#include <pcap-namedb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

#include "interface.h"
#include "addrtoname.h"
#include "llc.h"
#include "privsep.h"
#include "savestr.h"

/*
 * hash tables for whatever-to-name translations
 */

#define HASHNAMESIZE 4096

struct hnamemem {
	u_int32_t addr;
	char *name;
	struct hnamemem *nxt;
};

struct hnamemem hnametable[HASHNAMESIZE];
struct hnamemem tporttable[HASHNAMESIZE];
struct hnamemem uporttable[HASHNAMESIZE];
struct hnamemem eprototable[HASHNAMESIZE];
struct hnamemem dnaddrtable[HASHNAMESIZE];
struct hnamemem llcsaptable[HASHNAMESIZE];

struct h6namemem {
	struct in6_addr addr;
	char *name;
	struct h6namemem *nxt;
};

struct h6namemem h6nametable[HASHNAMESIZE];

struct enamemem {
	u_short e_addr0;
	u_short e_addr1;
	u_short e_addr2;
	char *e_name;
	u_char *e_nsap;			/* used only for nsaptable[] */
#define e_bs e_nsap			/* for bytestringtable */
	struct enamemem *e_nxt;
};

struct enamemem enametable[HASHNAMESIZE];
struct enamemem nsaptable[HASHNAMESIZE];
struct enamemem bytestringtable[HASHNAMESIZE];
static char *ipprototable[256];

struct protoidmem {
	u_int32_t p_oui;
	u_short p_proto;
	char *p_name;
	struct protoidmem *p_nxt;
};

struct protoidmem protoidtable[HASHNAMESIZE];

/*
 * A faster replacement for inet_ntoa().
 */
char *
intoa(u_int32_t addr)
{
	char *cp;
	u_int byte;
	int n;
	static char buf[sizeof(".xxx.xxx.xxx.xxx")];

	NTOHL(addr);
	cp = &buf[sizeof buf];
	*--cp = '\0';

	n = 4;
	do {
		byte = addr & 0xff;
		*--cp = byte % 10 + '0';
		byte /= 10;
		if (byte > 0) {
			*--cp = byte % 10 + '0';
			byte /= 10;
			if (byte > 0)
				*--cp = byte + '0';
		}
		*--cp = '.';
		addr >>= 8;
	} while (--n > 0);

	return cp + 1;
}

static u_int32_t f_netmask;
static u_int32_t f_localnet;
static u_int32_t netmask;

/*
 * Return a name for the IP address pointed to by ap.  This address
 * is assumed to be in network byte order.
 */
char *
getname(const u_char *ap)
{
	char host[HOST_NAME_MAX+1];
	u_int32_t addr;
	struct hnamemem *p;

	/*
	 * Extract 32 bits in network order, dealing with alignment.
	 */
	switch ((intptr_t)ap & (sizeof(u_int32_t)-1)) {

	case 0:
		addr = *(u_int32_t *)ap;
		break;

	case 2:
#if BYTE_ORDER == BIG_ENDIAN
		addr = ((u_int32_t)*(u_short *)ap << 16) |
			(u_int32_t)*(u_short *)(ap + 2);
#else
		addr = ((u_int32_t)*(u_short *)(ap + 2) << 16) |
			(u_int32_t)*(u_short *)ap;
#endif
		break;

	default:
#if BYTE_ORDER == BIG_ENDIAN
		addr = ((u_int32_t)ap[0] << 24) |
			((u_int32_t)ap[1] << 16) |
			((u_int32_t)ap[2] << 8) |
			(u_int32_t)ap[3];
#else
		addr = ((u_int32_t)ap[3] << 24) |
			((u_int32_t)ap[2] << 16) |
			((u_int32_t)ap[1] << 8) |
			(u_int32_t)ap[0];
#endif
		break;
	}

	p = &hnametable[addr & (HASHNAMESIZE-1)];
	for (; p->nxt; p = p->nxt) {
		if (p->addr == addr)
			return (p->name);
	}
	p->addr = addr;
	p->nxt = newhnamemem();

	/*
	 * Only print names when:
	 *	(1) -n was not given
	 *      (2) Address is foreign and -f was given. (If -f was not
	 *	    give, f_netmask and f_local are 0 and the test
	 *	    evaluates to true)
	 *      (3) -a was given or the host portion is not all ones
	 *          nor all zeros (i.e. not a network or broadcast address)
	 */
	if (!nflag &&
	    (addr & f_netmask) == f_localnet &&
	    (aflag ||
	    !((addr & ~netmask) == 0 || (addr | netmask) == 0xffffffff))) {
		size_t n = priv_gethostbyaddr((char *)&addr, sizeof(addr),
		    AF_INET, host, sizeof(host));
		if (n > 0) {
			char *dotp;

			p->name = savestr(host);
			if (Nflag) {
				/* Remove domain qualifications */
				dotp = strchr(p->name, '.');
				if (dotp)
					*dotp = '\0';
			}
			return (p->name);
		}
	}
	p->name = savestr(intoa(addr));
	return (p->name);
}

/*
 * Return a name for the IP6 address pointed to by ap.  This address
 * is assumed to be in network byte order.
 */
char *
getname6(const u_char *ap)
{
	char host[HOST_NAME_MAX+1];
	struct in6_addr addr;
	struct h6namemem *p;
	char *cp;
	char ntop_buf[INET6_ADDRSTRLEN];

	memcpy(&addr, ap, sizeof(addr));
	p = &h6nametable[*(u_int16_t *)&addr.s6_addr[14] & (HASHNAMESIZE-1)];
	for (; p->nxt; p = p->nxt) {
		if (memcmp(&p->addr, &addr, sizeof(addr)) == 0)
			return (p->name);
	}
	p->addr = addr;
	p->nxt = newh6namemem();

	/*
	 * Only print names when:
	 *	(1) -n was not given
	 *      (2) Address is foreign and -f was given. (If -f was not
	 *	    give, f_netmask and f_local are 0 and the test
	 *	    evaluates to true)
	 *      (3) -a was given or the host portion is not all ones
	 *          nor all zeros (i.e. not a network or broadcast address)
	 */
	if (!nflag
#if 0
	&&
	    (addr & f_netmask) == f_localnet &&
	    (aflag ||
	    !((addr & ~netmask) == 0 || (addr | netmask) == 0xffffffff))
#endif
	    ) {
		size_t n = priv_gethostbyaddr((char *)&addr, sizeof(addr),
		    AF_INET6, host, sizeof(host));
		if (n > 0) {
			char *dotp;

			p->name = savestr(host);
			if (Nflag) {
				/* Remove domain qualifications */
				dotp = strchr(p->name, '.');
				if (dotp)
					*dotp = '\0';
			}
			return (p->name);
		}
	}
	cp = (char *)inet_ntop(AF_INET6, &addr, ntop_buf, sizeof(ntop_buf));
	p->name = savestr(cp);
	return (p->name);
}

static char hex[] = "0123456789abcdef";


/* Find the hash node that corresponds the ether address 'ep' */

static inline struct enamemem *
lookup_emem(const u_char *ep)
{
	u_int i, j, k;
	struct enamemem *tp;

	k = (ep[0] << 8) | ep[1];
	j = (ep[2] << 8) | ep[3];
	i = (ep[4] << 8) | ep[5];

	tp = &enametable[(i ^ j) & (HASHNAMESIZE-1)];
	while (tp->e_nxt)
		if (tp->e_addr0 == i &&
		    tp->e_addr1 == j &&
		    tp->e_addr2 == k)
			return tp;
		else
			tp = tp->e_nxt;
	tp->e_addr0 = i;
	tp->e_addr1 = j;
	tp->e_addr2 = k;
	tp->e_nxt = calloc(1, sizeof(*tp));
	if (tp->e_nxt == NULL)
		error("lookup_emem: calloc");

	return tp;
}

/*
 * Find the hash node that corresponds to the bytestring 'bs' 
 * with length 'nlen'
 */

static inline struct enamemem *
lookup_bytestring(const u_char *bs, const int nlen)
{
	struct enamemem *tp;
	u_int i, j, k;

	if (nlen >= 6) {
		k = (bs[0] << 8) | bs[1];
		j = (bs[2] << 8) | bs[3];
		i = (bs[4] << 8) | bs[5];
	} else if (nlen >= 4) {
		k = (bs[0] << 8) | bs[1];
		j = (bs[2] << 8) | bs[3];
		i = 0;
	} else
		i = j = k = 0;

	tp = &bytestringtable[(i ^ j) & (HASHNAMESIZE-1)];
	while (tp->e_nxt)
		if (tp->e_addr0 == i &&
		    tp->e_addr1 == j &&
		    tp->e_addr2 == k &&
		    bcmp((char *)bs, (char *)(tp->e_bs), nlen) == 0)
			return tp;
		else
			tp = tp->e_nxt;

	tp->e_addr0 = i;
	tp->e_addr1 = j;
	tp->e_addr2 = k;

	tp->e_bs = calloc(1, nlen + 1);
	if (tp->e_bs == NULL)
		error("lookup_bytestring: calloc");
	bcopy(bs, tp->e_bs, nlen);
	tp->e_nxt = calloc(1, sizeof(*tp));
	if (tp->e_nxt == NULL)
		error("lookup_bytestring: calloc");

	return tp;
}

/* Find the hash node that corresponds the NSAP 'nsap' */

static inline struct enamemem *
lookup_nsap(const u_char *nsap)
{
	u_int i, j, k;
	int nlen = *nsap;
	struct enamemem *tp;
	const u_char *ensap = nsap + nlen - 6;

	if (nlen > 6) {
		k = (ensap[0] << 8) | ensap[1];
		j = (ensap[2] << 8) | ensap[3];
		i = (ensap[4] << 8) | ensap[5];
	}
	else
		i = j = k = 0;

	tp = &nsaptable[(i ^ j) & (HASHNAMESIZE-1)];
	while (tp->e_nxt)
		if (tp->e_addr0 == i &&
		    tp->e_addr1 == j &&
		    tp->e_addr2 == k &&
		    tp->e_nsap[0] == nlen &&
		    memcmp((char *)&(nsap[1]),
			(char *)&(tp->e_nsap[1]), nlen) == 0)
			return tp;
		else
			tp = tp->e_nxt;
	tp->e_addr0 = i;
	tp->e_addr1 = j;
	tp->e_addr2 = k;
	tp->e_nsap = malloc(nlen + 1);
	if (tp->e_nsap == NULL)
		error("lookup_nsap: malloc");
	memcpy((char *)tp->e_nsap, (char *)nsap, nlen + 1);
	tp->e_nxt = calloc(1, sizeof(*tp));
	if (tp->e_nxt == NULL)
		error("lookup_nsap: calloc");

	return tp;
}

/* Find the hash node that corresponds the protoid 'pi'. */

static inline struct protoidmem *
lookup_protoid(const u_char *pi)
{
	u_int i, j;
	struct protoidmem *tp;

	/* 5 octets won't be aligned */
	i = (((pi[0] << 8) + pi[1]) << 8) + pi[2];
	j =   (pi[3] << 8) + pi[4];
	/* XXX should be endian-insensitive, but do big-endian testing  XXX */

	tp = &protoidtable[(i ^ j) & (HASHNAMESIZE-1)];
	while (tp->p_nxt)
		if (tp->p_oui == i && tp->p_proto == j)
			return tp;
		else
			tp = tp->p_nxt;
	tp->p_oui = i;
	tp->p_proto = j;
	tp->p_nxt = calloc(1, sizeof(*tp));
	if (tp->p_nxt == NULL)
		error("lookup_protoid: calloc");

	return tp;
}

char *
etheraddr_string(const u_char *ep)
{
	struct enamemem *tp;
	struct ether_addr e;

	tp = lookup_emem(ep);
	if (tp->e_name)
		return (tp->e_name);
#ifdef HAVE_ETHER_NTOHOST
	if (!nflag) {
		char buf[HOST_NAME_MAX+1 + 1];
		if (priv_ether_ntohost(buf, sizeof(buf),
		    (struct ether_addr *)ep) > 0) {
			tp->e_name = savestr(buf);
			return (tp->e_name);
		}
	}
#endif
	memcpy(e.ether_addr_octet, ep, sizeof(e.ether_addr_octet));
	tp->e_name = savestr(ether_ntoa(&e));
	return (tp->e_name);
}

char *
linkaddr_string(const u_char *ep, const int len)
{
	u_int i, j;
	char *cp;
	struct enamemem *tp;

	if (len == 6)	/* XXX not totally correct... */
		return etheraddr_string(ep);
	
	tp = lookup_bytestring(ep, len);
	if (tp->e_name)
		return (tp->e_name);

	tp->e_name = cp = reallocarray(NULL, len, 3);
	if (tp->e_name == NULL)
		error("linkaddr_string: malloc");
	if ((j = *ep >> 4) != 0)
		*cp++ = hex[j];
	*cp++ = hex[*ep++ & 0xf];
	for (i = len-1; i > 0 ; --i) {
		*cp++ = ':';
		if ((j = *ep >> 4) != 0)
			*cp++ = hex[j];
		*cp++ = hex[*ep++ & 0xf];
	}
	*cp = '\0';
	return (tp->e_name);
}

char *
etherproto_string(u_short port)
{
	char *cp;
	struct hnamemem *tp;
	u_int32_t i = port;
	char buf[sizeof("0000")];

	for (tp = &eprototable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	tp->addr = i;
	tp->nxt = newhnamemem();

	cp = buf;
	NTOHS(port);
	*cp++ = hex[port >> 12 & 0xf];
	*cp++ = hex[port >> 8 & 0xf];
	*cp++ = hex[port >> 4 & 0xf];
	*cp++ = hex[port & 0xf];
	*cp++ = '\0';
	tp->name = savestr(buf);
	return (tp->name);
}

char *
protoid_string(const u_char *pi)
{
	u_int i, j;
	char *cp;
	struct protoidmem *tp;
	char buf[sizeof("00:00:00:00:00")];

	tp = lookup_protoid(pi);
	if (tp->p_name)
		return tp->p_name;

	cp = buf;
	if ((j = *pi >> 4) != 0)
		*cp++ = hex[j];
	*cp++ = hex[*pi++ & 0xf];
	for (i = 4; (int)--i >= 0;) {
		*cp++ = ':';
		if ((j = *pi >> 4) != 0)
			*cp++ = hex[j];
		*cp++ = hex[*pi++ & 0xf];
	}
	*cp = '\0';
	tp->p_name = savestr(buf);
	return (tp->p_name);
}

char *
llcsap_string(u_char sap)
{
	struct hnamemem *tp;
	u_int32_t i = sap;
	char buf[sizeof("sap 00")];

	for (tp = &llcsaptable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	tp->addr = i;
	tp->nxt = newhnamemem();

	snprintf(buf, sizeof(buf), "sap %02x", sap & 0xff);
	tp->name = savestr(buf);
	return (tp->name);
}

char *
isonsap_string(const u_char *nsap)
{
	u_int i, nlen = nsap[0];
	char *cp;
	struct enamemem *tp;

	tp = lookup_nsap(nsap);
	if (tp->e_name)
		return tp->e_name;

	tp->e_name = cp = malloc(nlen * 2 + 2);
	if (cp == NULL)
		error("isonsap_string: malloc");

	nsap++;
	*cp++ = '/';
	for (i = nlen; (int)--i >= 0;) {
		*cp++ = hex[*nsap >> 4];
		*cp++ = hex[*nsap++ & 0xf];
	}
	*cp = '\0';
	return (tp->e_name);
}

char *
tcpport_string(u_short port)
{
	struct hnamemem *tp;
	u_int32_t i = port;
	char buf[sizeof("00000")];

	for (tp = &tporttable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	tp->addr = i;
	tp->nxt = newhnamemem();

	(void)snprintf(buf, sizeof(buf), "%u", i);
	tp->name = savestr(buf);
	return (tp->name);
}

char *
udpport_string(u_short port)
{
	struct hnamemem *tp;
	u_int32_t i = port;
	char buf[sizeof("00000")];

	for (tp = &uporttable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	tp->addr = i;
	tp->nxt = newhnamemem();

	(void)snprintf(buf, sizeof(buf), "%u", i);
	tp->name = savestr(buf);
	return (tp->name);
}

char *
ipproto_string(u_int proto)
{
	return ipprototable[proto & 0xff];
}

static void
init_servarray(void)
{
	struct hnamemem *table;
	int i, port;
	char buf[sizeof("0000000000")];
	char service[BUFSIZ];
	char protocol[BUFSIZ];

	priv_getserventries();
	while (priv_getserventry(service, sizeof(service), &port, protocol,
	    sizeof(protocol)) != 0) {
		port = ntohs(port);
		i = port & (HASHNAMESIZE-1);
		if (strcmp(protocol, "tcp") == 0)
			table = &tporttable[i];
		else if (strcmp(protocol, "udp") == 0)
			table = &uporttable[i];
		else
			continue;

		while (table->name)
			table = table->nxt;
		if (nflag) {
			(void)snprintf(buf, sizeof(buf), "%d", port);
			table->name = savestr(buf);
		} else
			table->name = savestr(service);
		table->addr = port;
		table->nxt = newhnamemem();
	}
}

static void
init_ipprotoarray(void)
{
	int i;
	char buf[sizeof("000")];
	char prot[BUFSIZ];

	if (!nflag) {
		priv_getprotoentries();
		while (priv_getprotoentry(prot, sizeof(prot), &i) != 0)
			ipprototable[i & 0xff] = savestr(prot);
	}
	for (i = 0; i < 256; i++)
		if (ipprototable[i] == NULL) {
			(void)snprintf(buf, sizeof(buf), "%d", i);
			ipprototable[i] = savestr(buf);
		}
}

/* XXX from libpcap */
extern const struct eproto {
	char *s;
	u_short p;
} * const eproto_db;

static void
init_eprotoarray(void)
{
	int i;
	struct hnamemem *table;

	for (i = 0; eproto_db[i].s; i++) {
		int j = ntohs(eproto_db[i].p) & (HASHNAMESIZE-1);
		table = &eprototable[j];
		while (table->name)
			table = table->nxt;
		table->name = eproto_db[i].s;
		table->addr = ntohs(eproto_db[i].p);
		table->nxt = newhnamemem();
	}
}

/*
 * SNAP proto IDs with org code 0:0:0 are actually encapsulated Ethernet
 * types.
 */
static void
init_protoidarray(void)
{
	int i;
	struct protoidmem *tp;
	u_char protoid[5];

	protoid[0] = 0;
	protoid[1] = 0;
	protoid[2] = 0;
	for (i = 0; eproto_db[i].s; i++) {
		u_short etype = htons(eproto_db[i].p);

		memcpy((char *)&protoid[3], (char *)&etype, 2);
		tp = lookup_protoid(protoid);
		tp->p_name = savestr(eproto_db[i].s);
	}
}

static struct etherlist {
	u_char addr[6];
	char *name;
} etherlist[] = {
	{{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, "Broadcast" },
	{{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e }, "LLDP_Multicast" },
	{{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, NULL }
};

/*
 * Initialize the ethers hash table.  We take two different approaches
 * depending on whether or not the system provides the ethers name
 * service.  If it does, we just wire in a few names at startup,
 * and etheraddr_string() fills in the table on demand.  If it doesn't,
 * then we suck in the entire /etc/ethers file at startup.  The idea
 * is that parsing the local file will be fast, but spinning through
 * all the ethers entries via NIS & next_etherent might be very slow.
 *
 * XXX pcap_next_etherent doesn't belong in the pcap interface, but
 * since the pcap module already does name-to-address translation,
 * it's already does most of the work for the ethernet address-to-name
 * translation, so we just pcap_next_etherent as a convenience.
 */
static void
init_etherarray(void)
{
	struct etherlist *el;
	struct enamemem *tp;
#ifdef HAVE_ETHER_NTOHOST
	char name[HOST_NAME_MAX+1 + 1];
#else
	struct pcap_etherent *ep;
	FILE *fp;

	/* Suck in entire ethers file */
	fp = fopen(PCAP_ETHERS_FILE, "r");
	if (fp != NULL) {
		while ((ep = pcap_next_etherent(fp)) != NULL) {
			tp = lookup_emem(ep->addr);
			tp->e_name = savestr(ep->name);
		}
		(void)fclose(fp);
	}
#endif

	/* Hardwire some ethernet names */
	for (el = etherlist; el->name != NULL; ++el) {
		tp = lookup_emem(el->addr);
		/* Don't override existing name */
		if (tp->e_name != NULL)
			continue;

#ifdef HAVE_ETHER_NTOHOST
                /* Use yp/nis version of name if available */
                if (priv_ether_ntohost(name, sizeof(name),
		    (struct ether_addr *)el->addr) > 0) {
                        tp->e_name = savestr(name);
			continue;
		}
#endif
		tp->e_name = el->name;
	}
}

static struct tok llcsap_db[] = {
	{ LLCSAP_NULL,		"null" },
	{ LLCSAP_8021B_I,	"802.1b-gsap" },
	{ LLCSAP_8021B_G,	"802.1b-isap" },
	{ LLCSAP_IP,		"ip-sap" },
	{ LLCSAP_PROWAYNM,	"proway-nm" },
	{ LLCSAP_8021D,		"802.1d" },
	{ LLCSAP_RS511,		"eia-rs511" },
	{ LLCSAP_ISO8208,	"x.25/llc2" },
	{ LLCSAP_PROWAY,	"proway" },
	{ LLCSAP_ISONS,		"iso-clns" },
	{ LLCSAP_GLOBAL,	"global" },
	{ 0,			NULL }
};

static void
init_llcsaparray(void)
{
	int i;
	struct hnamemem *table;

	for (i = 0; llcsap_db[i].s != NULL; i++) {
		table = &llcsaptable[llcsap_db[i].v];
		while (table->name)
			table = table->nxt;
		table->name = llcsap_db[i].s;
		table->addr = llcsap_db[i].v;
		table->nxt = newhnamemem();
	}
}

/*
 * Initialize the address to name translation machinery.  We map all
 * non-local IP addresses to numeric addresses if fflag is true (i.e.,
 * to prevent blocking on the nameserver).  localnet is the IP address
 * of the local network.  mask is its subnet mask.
 */
void
init_addrtoname(u_int32_t localnet, u_int32_t mask)
{
	netmask = mask;
	if (fflag) {
		f_localnet = localnet;
		f_netmask = mask;
	}

	init_servarray();
	init_ipprotoarray();

	if (nflag)
		/*
		 * Simplest way to suppress names.
		 */
		return;

	init_etherarray();
	init_eprotoarray();
	init_llcsaparray();
	init_protoidarray();
}

char *
dnaddr_string(u_short dnaddr)
{
	struct hnamemem *tp;

	for (tp = &dnaddrtable[dnaddr & (HASHNAMESIZE-1)]; tp->nxt != 0;
	     tp = tp->nxt)
		if (tp->addr == dnaddr)
			return (tp->name);

	tp->addr = dnaddr;
	tp->nxt = newhnamemem();
	if (nflag)
		tp->name = dnnum_string(dnaddr);
	else
		tp->name = dnname_string(dnaddr);

	return(tp->name);
}

/* Return a zero'ed hnamemem struct and cuts down on calloc() overhead */
struct hnamemem *
newhnamemem(void)
{
	struct hnamemem *p;
	static struct hnamemem *ptr = NULL;
	static u_int num = 0;

	if (num  <= 0) {
		num = 64;
		ptr = calloc(num, sizeof (*ptr));
		if (ptr == NULL)
			error("newhnamemem: calloc");
	}
	--num;
	p = ptr++;
	return (p);
}

/* Return a zero'ed h6namemem struct and cuts down on calloc() overhead */
struct h6namemem *
newh6namemem(void)
{
	struct h6namemem *p;
	static struct h6namemem *ptr = NULL;
	static u_int num = 0;

	if (num  <= 0) {
		num = 64;
		ptr = calloc(num, sizeof (*ptr));
		if (ptr == NULL)
			error("newh6namemem: calloc");
	}
	--num;
	p = ptr++;
	return (p);
}

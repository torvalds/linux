/*	$OpenBSD: print-ether.c,v 1.42 2022/06/09 12:56:14 mbuhl Exp $	*/

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

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>

#include <stdio.h>
#include <stddef.h>
#include <pcap.h>


#include "interface.h"
#include "addrtoname.h"
#include "ethertype.h"
#include "extract.h"

const u_char *packetp;
const u_char *snapend;

void ether_macctl(const u_char *, u_int);
void ether_pbb_print(const u_char *, u_int, u_int);
void ether_eapol_print(const u_char *, u_int, u_int);

void
ether_print(const u_char *bp, u_int length)
{
	const struct ether_header *ep;

	ep = (const struct ether_header *)bp;
	if (qflag) {
		TCHECK2(*ep, 12);
		printf("%s %s %d: ",
		    etheraddr_string(ESRC(ep)),
		    etheraddr_string(EDST(ep)),
		    length);
	} else {
		TCHECK2(*ep, 14);
		printf("%s %s %s %d: ",
		    etheraddr_string(ESRC(ep)),
		    etheraddr_string(EDST(ep)),
		    etherproto_string(ep->ether_type),
		    length);
	}
	return;
trunc:
	printf("[|ether] ");
}

u_short extracted_ethertype;

/*
 * This is the top level routine of the printer.  'p' is the points
 * to the ether header of the packet, 'tvp' is the timestamp,
 * 'length' is the length of the packet off the wire, and 'caplen'
 * is the number of bytes actually captured.
 */
void
ether_if_print(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
	ts_print(&h->ts);

	/*
	 * Some printers want to get back at the ethernet addresses,
	 * and/or check that they're not walking off the end of the packet.
	 * Rather than pass them all the way down, we set these globals.
	 */
	snapend = p + h->caplen;

	ether_tryprint(p, h->len, 1);
}

void
ether_tryprint(const u_char *p, u_int length, int first_header)
{
	struct ether_header *ep;
	u_int caplen = snapend - p;
	u_short ether_type;

	if (caplen < sizeof(struct ether_header)) {
		printf("[|ether]");
		goto out;
	}

	if (eflag)
		ether_print(p, length);

	packetp = p;
	length -= sizeof(struct ether_header);
	caplen -= sizeof(struct ether_header);
	ep = (struct ether_header *)p;
	p += sizeof(struct ether_header);

	ether_type = ntohs(ep->ether_type);

	/*
	 * Is it (gag) an 802.3 encapsulation?
	 */
	extracted_ethertype = 0;
	if (ether_type <= ETHERMTU) {
		/* Try to print the LLC-layer header & higher layers */
		if (llc_print(p, length, caplen, ESRC(ep), EDST(ep)) == 0) {
			/* ether_type not known, print raw packet */
			if (!eflag)
				ether_print((u_char *)ep, length);
			if (extracted_ethertype) {
				printf("(LLC %s) ",
			       etherproto_string(htons(extracted_ethertype)));
			}
			if (!xflag && !qflag) {
				if (eflag)
					default_print(packetp,
					    snapend - packetp);
				else
					default_print(p, caplen);
			}
		}
	} else if (ether_encap_print(ether_type, p, length, caplen) == 0) {
		/* ether_type not known, print raw packet */
		if (!eflag)
			ether_print((u_char *)ep, length + sizeof(*ep));
		if (!xflag && !qflag) {
			if (eflag)
				default_print(packetp, snapend - packetp);
			else
				default_print(p, caplen);
		}
	}
	if (xflag && first_header) {
		if (eflag)
			default_print(packetp, snapend - packetp);
		else
			default_print(p, caplen);
	}
 out:
	if (first_header)
		putchar('\n');
}

void
ether_pbb_print(const u_char *bp, u_int length, u_int caplen)
{
	uint32_t itag;
	uint8_t pri, res;

	if (caplen < sizeof(itag))
		goto trunc;

	itag = EXTRACT_32BITS(bp);

	bp += sizeof(itag);
	length -= sizeof(itag);
	caplen -= sizeof(itag);

	pri = itag >> 29;
	if (pri <= 1)
		pri = !pri;

	res = (itag >> 24) & 0x7;

	printf("802.1Q ivid %u pri %u ", itag & 0xffffff, pri);
	if (itag & (1 << 28))
		printf("dei ");
	if (itag & (1 << 27))
		printf("uca ");
	if (res)
		printf("res %u! ", res);

	ether_tryprint(bp, length, 0);

	return;

trunc:
	printf("[|pbb] ");
}

/*
 * Prints the packet encapsulated in an Ethernet data segment
 * (or an equivalent encapsulation), given the Ethernet type code.
 *
 * Returns non-zero if it can do so, zero if the ethertype is unknown.
 *
 * Stuffs the ether type into a global for the benefit of lower layers
 * that might want to know what it is.
 */

int
ether_encap_print(u_short ethertype, const u_char *p,
    u_int length, u_int caplen)
{
	uint16_t vlan, pri, vid;
recurse:
	extracted_ethertype = ethertype;

	switch (ethertype) {

	case ETHERTYPE_IP:
		ip_print(p, length);
		return (1);

	case ETHERTYPE_IPV6:
		ip6_print(p, length);
		return (1);

	case ETHERTYPE_ARP:
	case ETHERTYPE_REVARP:
		arp_print(p, length, caplen);
		return (1);

	case ETHERTYPE_DN:
		decnet_print(p, length, caplen);
		return (1);

	case ETHERTYPE_ATALK:
		if (vflag)
			printf("et1 ");
		atalk_print_llap(p, length);
		return (1);

	case ETHERTYPE_AARP:
		aarp_print(p, length);
		return (1);

	case ETHERTYPE_8021Q:
		printf("802.1Q ");
	case ETHERTYPE_QINQ:
		if (ethertype == ETHERTYPE_QINQ)
			printf("QinQ s");

		/* XXX caplen check */

		vlan = ntohs(*(unsigned short*)p);
		vid = vlan & 0xfff;
		pri = vlan >> 13;
		if (pri <= 1)
			pri = !pri;

		printf("vid %d pri %d%s", vid, pri,
		    vlan & 0x1000 ? " dei " : " ");
		ethertype = ntohs(*(unsigned short*)(p+2));
		p += 4;
		length -= 4;
		caplen -= 4;
		if (ethertype > ETHERMTU) 
			goto recurse;

		extracted_ethertype = 0;

		if (llc_print(p, length, caplen, p-18, p-12) == 0) {
			/* ether_type not known, print raw packet */
			if (!eflag)
				ether_print(p-18, length+4);
			if (extracted_ethertype) {
				printf("(LLC %s) ",
				etherproto_string(htons(extracted_ethertype)));
			}
			if (!xflag && !qflag)
				default_print(p-18, caplen+4);
		}
		return (1);

	case ETHERTYPE_NSH:
		nsh_print(p, length);
		return (1);

	case ETHERTYPE_EAPOL:
		ether_eapol_print(p, length, caplen);
		return (1);

	case ETHERTYPE_PBB:
		ether_pbb_print(p, length, caplen);
		return (1);

	case ETHERTYPE_NHRP:
		nhrp_print(p, length);
		return (1);

#ifdef PPP
	case ETHERTYPE_PPPOEDISC:
	case ETHERTYPE_PPPOE:
		pppoe_if_print(ethertype, p, length, caplen);
		return (1);
#endif

	case ETHERTYPE_FLOWCONTROL:
		ether_macctl(p, length);
		return (1);

	case ETHERTYPE_MPLS:
	case ETHERTYPE_MPLS_MCAST:
		mpls_print(p, length);
		return (1);

	case ETHERTYPE_LLDP:
		lldp_print(p, length);
		return (1);

	case ETHERTYPE_SLOW:
		slow_print(p, length);
		return (1);

	case ETHERTYPE_LAT:
	case ETHERTYPE_SCA:
	case ETHERTYPE_MOPRC:
	case ETHERTYPE_MOPDL:
		/* default_print for now */
	default:
		return (0);
	}
}

void
ether_macctl(const u_char *p, u_int length)
{
	printf("MACCTL");

	if (length < 2)
		goto trunc;
	if (EXTRACT_16BITS(p) == 0x0001) {
		u_int plen;

		printf(" PAUSE");

		length -= 2;
		p += 2;
		if (length < 2)
			goto trunc;
		plen = 512 * EXTRACT_16BITS(p);
		printf(" quanta %u", plen);
	} else {
		printf(" unknown-opcode(0x%04x)", EXTRACT_16BITS(p));
	}
	return;

trunc:
	printf("[|MACCTL]");
}

/*
 * 802.1X EAPOL PDU
 */

struct eapol_header {
	uint8_t			version;
	uint8_t			type;
#define EAPOL_T_EAP			0x00
#define EAPOL_T_START			0x01
#define EAPOL_T_LOGOFF			0x02
#define EAPOL_T_KEY			0x03
#define EAPOL_T_ENCAP_ASF_ALERT		0x04
#define EAPOL_T_MKA			0x05
#define EAPOL_T_ANNOUNCEMENT_GENERIC	0x06
#define EAPOL_T_ANNOUNCEMENT_SPECIFIC	0x07
#define EAPOL_T_ANNOUNCEMENT_REQ	0x08
	uint16_t		length;
};

void
ether_eapol_print(const u_char *bp, u_int length, u_int caplen)
{
	struct eapol_header h;

	printf("EAPOL");

	if (caplen < sizeof(h))
		goto trunc;

	h.version = *(bp + offsetof(struct eapol_header, version));
	h.type = *(bp + offsetof(struct eapol_header, type));
	h.length = EXTRACT_16BITS(bp + offsetof(struct eapol_header, length));

	bp += sizeof(h);
	length -= sizeof(h);
	caplen -= sizeof(h);

	if (vflag)
		printf(" (v%u, len %u)", h.version, h.length);

	if (length > h.length)
		length = h.length;
	else if (length < h.length) {
		printf(" truncated-eapol - %u bytes missing!",
		    h.length - length);
	}

	switch (h.type) {
	case EAPOL_T_EAP:
		printf(" EAP");
		break;
	case EAPOL_T_START:
		printf(" Start");
		break;
	case EAPOL_T_LOGOFF:
		printf(" Logoff");
		break;
	case EAPOL_T_KEY:
		printf(" Key");
		break;
	case EAPOL_T_MKA:
		printf(" MKA");
		break;
	case EAPOL_T_ANNOUNCEMENT_GENERIC:
		printf(" Announcement (Generic)");
		break;
	case EAPOL_T_ANNOUNCEMENT_SPECIFIC:
		printf(" Announcement (Specific)");
		break;
	case EAPOL_T_ANNOUNCEMENT_REQ:
		printf(" Announcement Req");
		break;
	default:
		printf(" unknown (%u)", h.type);
		break;
	}

	return;

trunc:
	printf(" [|eapol] ");
}

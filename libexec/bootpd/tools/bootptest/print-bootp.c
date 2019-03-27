/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988-1990
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that:
 * 1. Source code distributions retain the above copyright
 *    notice and this paragraph in its entirety
 * 2. Distributions including binary code include the above copyright
 *    notice and this paragraph in its entirety in the documentation
 *    or other materials provided with the distribution, and 
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Format and print bootp packets.
 *
 * This file was copied from tcpdump-2.1.1 and modified.
 * There is an e-mail list for tcpdump: <tcpdump@ee.lbl.gov>
 *
 * $FreeBSD$
 */

#include <stdio.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <sys/time.h>	/* for struct timeval in net/if.h */
#include <net/if.h>
#include <netinet/in.h>

#include <string.h>
#include <ctype.h>

#include "bootp.h"
#include "bootptest.h"

/* These decode the vendor data. */
static void rfc1048_print(u_char *bp, int length);
static void cmu_print(u_char *bp, int length);
static void other_print(u_char *bp, int length);
static void dump_hex(u_char *bp, int len);

/*
 * Print bootp requests
 */
void
bootp_print(bp, length, sport, dport)
	struct bootp *bp;
	int length;
	u_short sport, dport;
{
	static char tstr[] = " [|bootp]";
	static unsigned char vm_cmu[4] = VM_CMU;
	static unsigned char vm_rfc1048[4] = VM_RFC1048;
	u_char *ep;
	int vdlen;

#define TCHECK(var, l) if ((u_char *)&(var) > ep - l) goto trunc

	/* Note funny sized packets */
	if (length != sizeof(struct bootp))
		(void) printf(" [len=%d]", length);

	/* 'ep' points to the end of avaible data. */
	ep = (u_char *) snapend;

	switch (bp->bp_op) {

	case BOOTREQUEST:
		/* Usually, a request goes from a client to a server */
		if (sport != IPPORT_BOOTPC || dport != IPPORT_BOOTPS)
			printf(" (request)");
		break;

	case BOOTREPLY:
		/* Usually, a reply goes from a server to a client */
		if (sport != IPPORT_BOOTPS || dport != IPPORT_BOOTPC)
			printf(" (reply)");
		break;

	default:
		printf(" bootp-#%d", bp->bp_op);
	}

	/* The usual hardware address type is 1 (10Mb Ethernet) */
	if (bp->bp_htype != 1)
		printf(" htype:%d", bp->bp_htype);

	/* The usual length for 10Mb Ethernet address is 6 bytes */
	if (bp->bp_hlen != 6)
		printf(" hlen:%d", bp->bp_hlen);

	/* Client's Hardware address */
	if (bp->bp_hlen) {
		struct ether_header *eh;
		char *e;

		TCHECK(bp->bp_chaddr[0], 6);
		eh = (struct ether_header *) packetp;
		if (bp->bp_op == BOOTREQUEST)
			e = (char *) ESRC(eh);
		else if (bp->bp_op == BOOTREPLY)
			e = (char *) EDST(eh);
		else
			e = NULL;
		if (e == NULL || bcmp((char *) bp->bp_chaddr, e, 6))
			dump_hex(bp->bp_chaddr, bp->bp_hlen);
	}
	/* Only print interesting fields */
	if (bp->bp_hops)
		printf(" hops:%d", bp->bp_hops);

	if (bp->bp_xid)
		printf(" xid:%ld", (long)ntohl(bp->bp_xid));

	if (bp->bp_secs)
		printf(" secs:%d", ntohs(bp->bp_secs));

	/* Client's ip address */
	TCHECK(bp->bp_ciaddr, sizeof(bp->bp_ciaddr));
	if (bp->bp_ciaddr.s_addr)
		printf(" C:%s", ipaddr_string(&bp->bp_ciaddr));

	/* 'your' ip address (bootp client) */
	TCHECK(bp->bp_yiaddr, sizeof(bp->bp_yiaddr));
	if (bp->bp_yiaddr.s_addr)
		printf(" Y:%s", ipaddr_string(&bp->bp_yiaddr));

	/* Server's ip address */
	TCHECK(bp->bp_siaddr, sizeof(bp->bp_siaddr));
	if (bp->bp_siaddr.s_addr)
		printf(" S:%s", ipaddr_string(&bp->bp_siaddr));

	/* Gateway's ip address */
	TCHECK(bp->bp_giaddr, sizeof(bp->bp_giaddr));
	if (bp->bp_giaddr.s_addr)
		printf(" G:%s", ipaddr_string(&bp->bp_giaddr));

	TCHECK(bp->bp_sname[0], sizeof(bp->bp_sname));
	if (*bp->bp_sname) {
		printf(" sname:");
		if (printfn(bp->bp_sname, ep)) {
			fputs(tstr + 1, stdout);
			return;
		}
	}
	TCHECK(bp->bp_file[0], sizeof(bp->bp_file));
	if (*bp->bp_file) {
		printf(" file:");
		if (printfn(bp->bp_file, ep)) {
			fputs(tstr + 1, stdout);
			return;
		}
	}
	/* Don't try to decode the vendor buffer unless we're verbose */
	if (vflag <= 0)
		return;

	vdlen = sizeof(bp->bp_vend);
	/* Vendor data can extend to the end of the packet. */
	if (vdlen < (ep - bp->bp_vend))
		vdlen = (ep - bp->bp_vend);

	TCHECK(bp->bp_vend[0], vdlen);
	printf(" vend");
	if (!bcmp(bp->bp_vend, vm_rfc1048, sizeof(u_int32)))
		rfc1048_print(bp->bp_vend, vdlen);
	else if (!bcmp(bp->bp_vend, vm_cmu, sizeof(u_int32)))
		cmu_print(bp->bp_vend, vdlen);
	else
		other_print(bp->bp_vend, vdlen);

	return;
 trunc:
	fputs(tstr, stdout);
#undef TCHECK
}

/*
 * Option description data follows.
 * These are described in: RFC-1048, RFC-1395, RFC-1497, RFC-1533
 *
 * The first char of each option string encodes the data format:
 * ?: unknown
 * a: ASCII
 * b: byte (8-bit)
 * i: inet address
 * l: int32
 * s: short (16-bit)
 */
char *
rfc1048_opts[] = {
	/* Originally from RFC-1048: */
	"?PAD",				/*  0: Padding - special, no data. */
	"iSM",				/*  1: subnet mask (RFC950)*/
	"lTZ",				/*  2: time offset, seconds from UTC */
	"iGW",				/*  3: gateways (or routers) */
	"iTS",				/*  4: time servers (RFC868) */
	"iINS",				/*  5: IEN name servers (IEN116) */
	"iDNS",				/*  6: domain name servers (RFC1035)(1034?) */
	"iLOG",				/*  7: MIT log servers */
	"iCS",				/*  8: cookie servers (RFC865) */
	"iLPR",				/*  9: lpr server (RFC1179) */
	"iIPS",				/* 10: impress servers (Imagen) */
	"iRLP",				/* 11: resource location servers (RFC887) */
	"aHN",				/* 12: host name (ASCII) */
	"sBFS",				/* 13: boot file size (in 512 byte blocks) */

	/* Added by RFC-1395: */
	"aDUMP",			/* 14: Merit Dump File */
	"aDNAM",			/* 15: Domain Name (for DNS) */
	"iSWAP",			/* 16: Swap Server */
	"aROOT",			/* 17: Root Path */

	/* Added by RFC-1497: */
	"aEXTF",			/* 18: Extensions Path (more options) */

	/* Added by RFC-1533: (many, many options...) */
#if 1	/* These might not be worth recognizing by name. */

	/* IP Layer Parameters, per-host (RFC-1533, sect. 4) */
	"bIP-forward",		/* 19: IP Forwarding flag */
	"bIP-srcroute",		/* 20: IP Source Routing Enable flag */
	"iIP-filters",		/* 21: IP Policy Filter (addr pairs) */
	"sIP-maxudp",		/* 22: IP Max-UDP reassembly size */
	"bIP-ttlive",		/* 23: IP Time to Live */
	"lIP-pmtuage",		/* 24: IP Path MTU aging timeout */
	"sIP-pmtutab",		/* 25: IP Path MTU plateau table */

	/* IP parameters, per-interface (RFC-1533, sect. 5) */
	"sIP-mtu-sz",		/* 26: IP MTU size */
	"bIP-mtu-sl",		/* 27: IP MTU all subnets local */
	"bIP-bcast1",		/* 28: IP Broadcast Addr ones flag */
	"bIP-mask-d",		/* 29: IP do mask discovery */
	"bIP-mask-s",		/* 30: IP do mask supplier */
	"bIP-rt-dsc",		/* 31: IP do router discovery */
	"iIP-rt-sa",		/* 32: IP router solicitation addr */
	"iIP-routes",		/* 33: IP static routes (dst,router) */

	/* Link Layer parameters, per-interface (RFC-1533, sect. 6) */
	"bLL-trailer",		/* 34: do tralier encapsulation */
	"lLL-arp-tmo",		/* 35: ARP cache timeout */
	"bLL-ether2",		/* 36: Ethernet version 2 (IEEE 802.3) */

	/* TCP parameters (RFC-1533, sect. 7) */
	"bTCP-def-ttl",		/* 37: default time to live */
	"lTCP-KA-tmo",		/* 38: keepalive time interval */
	"bTCP-KA-junk",		/* 39: keepalive sends extra junk */

	/* Application and Service Parameters (RFC-1533, sect. 8) */
	"aNISDOM",			/* 40: NIS Domain (Sun YP) */
	"iNISSRV",			/* 41: NIS Servers */
	"iNTPSRV",			/* 42: NTP (time) Servers (RFC 1129) */
	"?VSINFO",			/* 43: Vendor Specific Info (encapsulated) */
	"iNBiosNS",			/* 44: NetBIOS Name Server (RFC-1001,1..2) */
	"iNBiosDD",			/* 45: NetBIOS Datagram Dist. Server. */
	"bNBiosNT",			/* 46: NetBIOS Note Type */
	"?NBiosS",			/* 47: NetBIOS Scope */
	"iXW-FS",			/* 48: X Window System Font Servers */
	"iXW-DM",			/* 49: X Window System Display Managers */

	/* DHCP extensions (RFC-1533, sect. 9) */
#endif
};
#define	KNOWN_OPTIONS (sizeof(rfc1048_opts) / sizeof(rfc1048_opts[0]))

static void
rfc1048_print(bp, length)
	u_char *bp;
	int length;
{
	u_char tag;
	u_char *ep;
	int len;
	u_int32 ul;
	u_short us;
	struct in_addr ia;
	char *optstr;

	printf("-rfc1395");

	/* Step over magic cookie */
	bp += sizeof(int32);
	/* Setup end pointer */
	ep = bp + length;
	while (bp < ep) {
		tag = *bp++;
		/* Check for tags with no data first. */
		if (tag == TAG_PAD)
			continue;
		if (tag == TAG_END)
			return;
		if (tag < KNOWN_OPTIONS) {
			optstr = rfc1048_opts[tag];
			printf(" %s:", optstr + 1);
		} else {
			printf(" T%d:", tag);
			optstr = "?";
		}
		/* Now scan the length byte. */
		len = *bp++;
		if (bp + len > ep) {
			/* truncated option */
			printf(" |(%d>%td)", len, ep - bp);
			return;
		}
		/* Print the option value(s). */
		switch (optstr[0]) {

		case 'a':				/* ASCII string */
			printfn(bp, bp + len);
			bp += len;
			len = 0;
			break;

		case 's':				/* Word formats */
			while (len >= 2) {
				bcopy((char *) bp, (char *) &us, 2);
				printf("%d", ntohs(us));
				bp += 2;
				len -= 2;
				if (len) printf(",");
			}
			if (len) printf("(junk=%d)", len);
			break;

		case 'l':				/* Long words */
			while (len >= 4) {
				bcopy((char *) bp, (char *) &ul, 4);
				printf("%ld", (long)ntohl(ul));
				bp += 4;
				len -= 4;
				if (len) printf(",");
			}
			if (len) printf("(junk=%d)", len);
			break;

		case 'i':				/* INET addresses */
			while (len >= 4) {
				bcopy((char *) bp, (char *) &ia, 4);
				printf("%s", ipaddr_string(&ia));
				bp += 4;
				len -= 4;
				if (len) printf(",");
			}
			if (len) printf("(junk=%d)", len);
			break;

		case 'b':
		default:
			break;

		}						/* switch */

		/* Print as characters, if appropriate. */
		if (len) {
			dump_hex(bp, len);
			if (isascii(*bp) && isprint(*bp)) {
				printf("(");
				printfn(bp, bp + len);
				printf(")");
			}
			bp += len;
			len = 0;
		}
	} /* while bp < ep */
}

static void
cmu_print(bp, length)
	u_char *bp;
	int length;
{
	struct cmu_vend *v;

	printf("-cmu");

	v = (struct cmu_vend *) bp;
	if (length < sizeof(*v)) {
		printf(" |L=%d", length);
		return;
	}

	/* Subnet mask */
	if (v->v_flags & VF_SMASK) {
		printf(" SM:%s", ipaddr_string(&v->v_smask));
	}
	/* Default gateway */
	if (v->v_dgate.s_addr)
		printf(" GW:%s", ipaddr_string(&v->v_dgate));

	/* Domain name servers */
	if (v->v_dns1.s_addr)
		printf(" DNS1:%s", ipaddr_string(&v->v_dns1));
	if (v->v_dns2.s_addr)
		printf(" DNS2:%s", ipaddr_string(&v->v_dns2));

	/* IEN-116 name servers */
	if (v->v_ins1.s_addr)
		printf(" INS1:%s", ipaddr_string(&v->v_ins1));
	if (v->v_ins2.s_addr)
		printf(" INS2:%s", ipaddr_string(&v->v_ins2));

	/* Time servers */
	if (v->v_ts1.s_addr)
		printf(" TS1:%s", ipaddr_string(&v->v_ts1));
	if (v->v_ts2.s_addr)
		printf(" TS2:%s", ipaddr_string(&v->v_ts2));

}


/*
 * Print out arbitrary, unknown vendor data.
 */

static void
other_print(bp, length)
	u_char *bp;
	int length;
{
	u_char *ep;					/* end pointer */
	u_char *zp;					/* points one past last non-zero byte */

	/* Setup end pointer */
	ep = bp + length;

	/* Find the last non-zero byte. */
	for (zp = ep; zp > bp; zp--) {
		if (zp[-1] != 0)
			break;
	}

	/* Print the all-zero case in a compact representation. */
	if (zp == bp) {
		printf("-all-zero");
		return;
	}
	printf("-unknown");

	/* Are there enough trailing zeros to make "00..." worthwhile? */
	if (zp + 2 > ep)
		zp = ep;				/* print them all normally */

	/* Now just print all the non-zero data. */
	while (bp < zp) {
		printf(".%02X", *bp);
		bp++;
	}

	if (zp < ep)
		printf(".00...");

	return;
}

static void
dump_hex(bp, len)
	u_char *bp;
	int len;
{
	while (len > 0) {
		printf("%02X", *bp);
		bp++;
		len--;
		if (len) printf(".");
	}
}

/*
 * Local Variables:
 * tab-width: 4
 * c-indent-level: 4
 * c-argdecl-indent: 4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: -4
 * c-label-offset: -4
 * c-brace-offset: 0
 * End:
 */

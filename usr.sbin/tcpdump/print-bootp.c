/*	$OpenBSD: print-bootp.c,v 1.25 2021/12/01 18:28:45 deraadt Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996, 1997
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
 * Format and print bootp packets.
 */
#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "bootp.h"

static void rfc1048_print(const u_char *, u_int);
static void cmu_print(const u_char *, u_int);

static char tstr[] = " [|bootp]";

/*
 * Print bootp requests
 */
void
bootp_print(const u_char *cp, u_int length,
	    u_short sport, u_short dport)
{
	const struct bootp *bp;
	static u_char vm_cmu[4] = VM_CMU;
	static u_char vm_rfc1048[4] = VM_RFC1048;

	bp = (struct bootp *)cp;
	TCHECK(bp->bp_op);
	switch (bp->bp_op) {

	case BOOTREQUEST:
		/* Usually, a request goes from a client to a server */
		if (sport != IPPORT_BOOTPC || dport != IPPORT_BOOTPS)
			printf("(request)");
		break;

	case BOOTREPLY:
		/* Usually, a reply goes from a server to a client */
		if (sport != IPPORT_BOOTPS || dport != IPPORT_BOOTPC)
			printf("(reply)");
		break;

	default:
		printf("bootp-#%d", bp->bp_op);
	}

	TCHECK(bp->bp_flags);

	/* The usual hardware address type is 1 (10Mb Ethernet) */
	if (bp->bp_htype != 1)
		printf(" htype-#%d", bp->bp_htype);

	/* The usual length for 10Mb Ethernet address is 6 bytes */
	if (bp->bp_htype != 1 || bp->bp_hlen != 6)
		printf(" hlen:%d", bp->bp_hlen);

	/* Only print interesting fields */
	if (bp->bp_hops)
		printf(" hops:%d", bp->bp_hops);
	if (bp->bp_xid)
		printf(" xid:0x%x", (u_int32_t)ntohl(bp->bp_xid));
	if (bp->bp_secs)
		printf(" secs:%d", ntohs(bp->bp_secs));
	if (bp->bp_flags)
		printf(" flags:0x%x", ntohs(bp->bp_flags));

	/* Client's ip address */
	TCHECK(bp->bp_ciaddr);
	if (bp->bp_ciaddr.s_addr)
		printf(" C:%s", ipaddr_string(&bp->bp_ciaddr));

	/* 'your' ip address (bootp client) */
	TCHECK(bp->bp_yiaddr);
	if (bp->bp_yiaddr.s_addr)
		printf(" Y:%s", ipaddr_string(&bp->bp_yiaddr));

	/* Server's ip address */
	TCHECK(bp->bp_siaddr);
	if (bp->bp_siaddr.s_addr)
		printf(" S:%s", ipaddr_string(&bp->bp_siaddr));

	/* Gateway's ip address */
	TCHECK(bp->bp_giaddr);
	if (bp->bp_giaddr.s_addr)
		printf(" G:%s", ipaddr_string(&bp->bp_giaddr));

	/* Client's Ethernet address */
	if (bp->bp_htype == 1 && bp->bp_hlen == 6) {
		const struct ether_header *eh;
		const char *e;

		TCHECK2(bp->bp_chaddr[0], 6);
		eh = (struct ether_header *)packetp;
		if (bp->bp_op == BOOTREQUEST)
			e = (const char *)ESRC(eh);
		else if (bp->bp_op == BOOTREPLY)
			e = (const char *)EDST(eh);
		else
			e = NULL;
		if (e == 0 || memcmp((char *)bp->bp_chaddr, e, 6) != 0)
			printf(" ether %s", etheraddr_string(bp->bp_chaddr));
	}

	TCHECK2(bp->bp_sname[0], 1);		/* check first char only */
	if (*bp->bp_sname) {
		printf(" sname \"");
		if (fn_print(bp->bp_sname, snapend)) {
			putchar('"');
			printf("%s", tstr + 1);
			return;
		}
		putchar('"');
	}
	TCHECK2(bp->bp_file[0], 1);		/* check first char only */
	if (*bp->bp_file) {
		printf(" file \"");
		if (fn_print(bp->bp_file, snapend)) {
			putchar('"');
			printf("%s", tstr + 1);
			return;
		}
		putchar('"');
	}

	/* Decode the vendor buffer */
	TCHECK2(bp->bp_vend[0], sizeof(u_int32_t));
	length -= sizeof(*bp) - sizeof(bp->bp_vend);
	if (memcmp((char *)bp->bp_vend, (char *)vm_rfc1048,
		 sizeof(u_int32_t)) == 0)
		rfc1048_print(bp->bp_vend, length);
	else if (memcmp((char *)bp->bp_vend, (char *)vm_cmu,
		      sizeof(u_int32_t)) == 0)
		cmu_print(bp->bp_vend, length);
	else {
		u_int32_t ul;

		memcpy((char *)&ul, (char *)bp->bp_vend, sizeof(ul));
		if (ul != 0)
			printf("vend-#0x%x", ul);
	}

	return;
trunc:
	printf("%s", tstr);
}

/* The first character specifies the format to print */
static struct tok tag2str[] = {
/* RFC1048 tags */
	{ TAG_PAD,		" PAD" },
	{ TAG_SUBNET_MASK,	"iSM" },	/* subnet mask (RFC950) */
	{ TAG_TIME_OFFSET,	"lTZ" },	/* seconds from UTC */
	{ TAG_GATEWAY,		"iDG" },	/* default gateway */
	{ TAG_TIME_SERVER,	"iTS" },	/* time servers (RFC868) */
	{ TAG_NAME_SERVER,	"iIEN" },	/* IEN name servers (IEN116) */
	{ TAG_DOMAIN_SERVER,	"iNS" },	/* domain name (RFC1035) */
	{ TAG_LOG_SERVER,	"iLOG" },	/* MIT log servers */
	{ TAG_COOKIE_SERVER,	"iCS" },	/* cookie servers (RFC865) */
	{ TAG_LPR_SERVER,	"iLPR" },	/* lpr server (RFC1179) */
	{ TAG_IMPRESS_SERVER,	"iIM" },	/* impress servers (Imagen) */
	{ TAG_RLP_SERVER,	"iRL" },	/* resource location (RFC887) */
	{ TAG_HOSTNAME,		"aHN" },	/* ascii hostname */
	{ TAG_BOOTSIZE,		"sBS" },	/* 512 byte blocks */
	{ TAG_END,		" END" },
/* RFC1497 tags */
	{ TAG_DUMPPATH,		"aDP" },
	{ TAG_DOMAINNAME,	"aDN" },
	{ TAG_SWAP_SERVER,	"iSS" },
	{ TAG_ROOTPATH,		"aRP" },
	{ TAG_EXTPATH,		"aEP" },
/* RFC2132 tags */
	{ TAG_IP_FORWARD,	"BIPF" },
	{ TAG_NL_SRCRT,		"BSRT" },
	{ TAG_PFILTERS,		"pPF" },
	{ TAG_REASS_SIZE,	"sRSZ" },
	{ TAG_DEF_TTL,		"bTTL" },
	{ TAG_MTU_TIMEOUT,	"lMA" },
	{ TAG_MTU_TABLE,	"sMT" },
	{ TAG_INT_MTU,		"sMTU" },
	{ TAG_LOCAL_SUBNETS,	"BLSN" },
	{ TAG_BROAD_ADDR,	"iBR" },
	{ TAG_DO_MASK_DISC,	"BMD" },
	{ TAG_SUPPLY_MASK,	"BMS" },
	{ TAG_DO_RDISC,		"BRD" },
	{ TAG_RTR_SOL_ADDR,	"iRSA" },
	{ TAG_STATIC_ROUTE,	"pSR" },
	{ TAG_USE_TRAILERS,	"BUT" },
	{ TAG_ARP_TIMEOUT,	"lAT" },
	{ TAG_ETH_ENCAP,	"BIE" },
	{ TAG_TCP_TTL,		"bTT" },
	{ TAG_TCP_KEEPALIVE,	"lKI" },
	{ TAG_KEEPALIVE_GO,	"BKG" },
	{ TAG_NIS_DOMAIN,	"aYD" },
	{ TAG_NIS_SERVERS,	"iYS" },
	{ TAG_NTP_SERVERS,	"iNTP" },
	{ TAG_VENDOR_OPTS,	"bVO" },
	{ TAG_NETBIOS_NS,	"iWNS" },
	{ TAG_NETBIOS_DDS,	"iWDD" },
	{ TAG_NETBIOS_NODE,	"bWNT" },
	{ TAG_NETBIOS_SCOPE,	"aWSC" },
	{ TAG_XWIN_FS,		"iXFS" },
	{ TAG_XWIN_DM,		"iXDM" },
	{ TAG_NIS_P_DOMAIN,	"sN+D" },
	{ TAG_NIS_P_SERVERS,	"iN+S" },
	{ TAG_MOBILE_HOME,	"iMH" },
	{ TAG_SMPT_SERVER,	"iSMTP" },
	{ TAG_POP3_SERVER,	"iPOP3" },
	{ TAG_NNTP_SERVER,	"iNNTP" },
	{ TAG_WWW_SERVER,	"iWWW" },
	{ TAG_FINGER_SERVER,	"iFG" },
	{ TAG_IRC_SERVER,	"iIRC" },
	{ TAG_STREETTALK_SRVR,	"iSTS" },
	{ TAG_STREETTALK_STDA,	"iSTDA" },
	{ TAG_REQUESTED_IP,	"iRQ" },
	{ TAG_IP_LEASE,		"lLT" },
	{ TAG_OPT_OVERLOAD,	"bOO" },
	{ TAG_TFTP_SERVER,	"aTFTP" },
	{ TAG_BOOTFILENAME,	"aBF" },
	{ TAG_DHCP_MESSAGE,	" DHCP" },
	{ TAG_SERVER_ID,	"iSID" },
	{ TAG_PARM_REQUEST,	"bPR" },
	{ TAG_MESSAGE,		"aMSG" },
	{ TAG_MAX_MSG_SIZE,	"sMSZ" },
	{ TAG_RENEWAL_TIME,	"lRN" },
	{ TAG_REBIND_TIME,	"lRB" },
	{ TAG_VENDOR_CLASS,	"bVC" },
	{ TAG_CLIENT_ID,	"bCID" },
	{ 0,			NULL }
};

static void
rfc1048_print(const u_char *bp, u_int length)
{
	u_char tag;
	u_int len, size;
	const char *cp;
	u_char c;
	int first;
	u_int32_t ul;
	u_short us;

	printf(" vend-rfc1048");

	/* Step over magic cookie */
	bp += sizeof(int32_t);

	/* Loop while we there is a tag left in the buffer */
	while (bp + 1 < snapend) {
		tag = *bp++;
		if (tag == TAG_PAD)
			continue;
		if (tag == TAG_END)
			return;
		cp = tok2str(tag2str, "?T%d", tag);
		c = *cp++;
		printf(" %s:", cp);

		/* Get the length; check for truncation */
		if (bp + 1 >= snapend) {
			printf("%s", tstr);
			return;
		}
		len = *bp++;
		if (bp + len >= snapend) {
			printf("%s", tstr);
			return;
		}

		if (tag == TAG_DHCP_MESSAGE && len == 1) {
			c = *bp++;
			switch (c) {
			case DHCPDISCOVER:	printf("DISCOVER");	break;
			case DHCPOFFER:		printf("OFFER");	break;
			case DHCPREQUEST:	printf("REQUEST");	break;
			case DHCPDECLINE:	printf("DECLINE");	break;
			case DHCPACK:		printf("ACK");		break;
			case DHCPNAK:		printf("NACK");		break;
			case DHCPRELEASE:	printf("RELEASE");	break;
			case DHCPINFORM:	printf("INFORM");	break;
			default:		printf("%u", c);	break;
			}
			continue;
		}

		if (tag == TAG_PARM_REQUEST) {
			first = 1;
			while (len-- > 0) {
				c = *bp++;
				cp = tok2str(tag2str, "?%d", c);
				if (!first)
					putchar('+');
				printf("%s", cp + 1);
				first = 0;
			}
			continue;
		}

		/* Print data */
		size = len;
		if (c == '?') {
			/* Base default formats for unknown tags on data size */
			if (size & 1)
				c = 'b';
			else if (size & 2)
				c = 's';
			else
				c = 'l';
		}
		first = 1;
		switch (c) {

		case 'a':
			/* ascii strings */
			putchar('"');
			(void)fn_printn(bp, size, NULL);
			putchar('"');
			bp += size;
			size = 0;
			break;

		case 'i':
		case 'l':
			/* ip addresses/32-bit words */
			while (size >= sizeof(ul)) {
				if (!first)
					putchar(',');
				memcpy((char *)&ul, (char *)bp, sizeof(ul));
				if (c == 'i')
					printf("%s", ipaddr_string(&ul));
				else
					printf("%u", ntohl(ul));
				bp += sizeof(ul);
				size -= sizeof(ul);
				first = 0;
			}
			break;

		case 'p':
			/* IP address pairs */
			while (size >= 2*sizeof(ul)) {
				if (!first)
					putchar(',');
				memcpy((char *)&ul, (char *)bp, sizeof(ul));
				printf("(%s:", ipaddr_string(&ul));
				bp += sizeof(ul);
				memcpy((char *)&ul, (char *)bp, sizeof(ul));
				printf("%s)", ipaddr_string(&ul));
				bp += sizeof(ul);
				size -= 2*sizeof(ul);
				first = 0;
			}
			break;

		case 's':
			/* shorts */
			while (size >= sizeof(us)) {
				if (!first)
					putchar(',');
				memcpy((char *)&us, (char *)bp, sizeof(us));
				printf("%u", ntohs(us));
				bp += sizeof(us);
				size -= sizeof(us);
				first = 0;
			}
			break;

		case 'B':
			/* boolean */
			while (size > 0) {
				if (!first)
					putchar(',');
				switch (*bp) {
				case 0:
					putchar('N');
					break;
				case 1:
					putchar('Y');
					break;
				default:
					printf("%d?", *bp);
					break;
				}
				++bp;
				--size;
				first = 0;
			}
			break;

		case 'b':
		default:
			/* Bytes */
			while (size > 0) {
				if (!first)
					putchar('.');
				printf("%d", *bp);
				++bp;
				--size;
				first = 0;
			}
			break;
		}
		/* Data left over? */
		if (size)
			printf("[len %d]", len);
	}
}

static void
cmu_print(const u_char *bp, u_int length)
{
	const struct cmu_vend *cmu;
	static const char fmt[] = " %s:%s";

#define PRINTCMUADDR(m, s) { TCHECK(cmu->m); \
    if (cmu->m.s_addr != 0) \
	printf(fmt, s, ipaddr_string(&cmu->m.s_addr)); }

	printf(" vend-cmu");
	cmu = (struct cmu_vend *)bp;

	/* Only print if there are unknown bits */
	TCHECK(cmu->v_flags);
	if ((cmu->v_flags & ~(VF_SMASK)) != 0)
		printf(" F:0x%x", cmu->v_flags);
	PRINTCMUADDR(v_dgate, "DG");
	PRINTCMUADDR(v_smask, cmu->v_flags & VF_SMASK ? "SM" : "SM*");
	PRINTCMUADDR(v_dns1, "NS1");
	PRINTCMUADDR(v_dns2, "NS2");
	PRINTCMUADDR(v_ins1, "IEN1");
	PRINTCMUADDR(v_ins2, "IEN2");
	PRINTCMUADDR(v_ts1, "TS1");
	PRINTCMUADDR(v_ts2, "TS2");
	return;

trunc:
	printf("%s", tstr);
#undef PRINTCMUADDR
}

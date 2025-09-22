/*	$OpenBSD: print-ppp.c,v 1.37 2024/10/30 10:36:28 sthen Exp $	*/

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
 */

#ifdef PPP
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include <ctype.h>
#include <netdb.h>
#include <pcap.h>
#include <signal.h>
#include <stdio.h>

#include <netinet/if_ether.h>
#include "ethertype.h"

#include <net/ppp_defs.h>
#include "interface.h"
#include "addrtoname.h"
#include "extract.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#ifndef PPP_EAP
#define PPP_EAP 0xc227
#endif

#ifndef PPP_CDP
#define PPP_CDP 0x0207
#endif

#ifndef PPP_CDPCP
#define PPP_CDPCP 0x8207
#endif

struct protonames {
	u_short protocol;
	char *name;
};

static const struct protonames protonames[] = {
	/*
	 * Protocol field values.
	 */
	{ PPP_IP,	"IP" },		/* Internet Protocol */
	{ PPP_XNS,	"XNS" },	/* Xerox NS */
	{ PPP_IPX,	"IPX" },	/* IPX Datagram (RFC1552) */
	{ PPP_AT,	"AppleTalk" },	/* AppleTalk Protocol */
	{ PPP_VJC_COMP,	"VJC_UNCOMP" },	/* VJ compressed TCP */
	{ PPP_VJC_UNCOMP,"VJC_UNCOMP" },/* VJ uncompressed TCP */
	{ PPP_IPV6,	"IPv6" },	/* Internet Protocol version 6 */
	{ PPP_COMP,	"COMP" },	/* compressed packet */
	{ PPP_IPCP,	"IPCP" },	/* IP Control Protocol */
	{ PPP_ATCP,	"AppleTalkCP" },/* AppleTalk Control Protocol */
	{ PPP_IPXCP,	"IPXCP" },	/* IPX Control Protocol (RFC1552) */
	{ PPP_IPV6CP,	"IPV6CP" },	/* IPv6 Control Protocol */
	{ PPP_CCP,	"CCP" },	/* Compression Control Protocol */
	{ PPP_LCP,	"LCP" },	/* Link Control Protocol */
	{ PPP_PAP,	"PAP" },	/* Password Authentication Protocol */
	{ PPP_LQR,	"LQR" },	/* Link Quality Report protocol */
	{ PPP_CBCP,	"CBCP" },	/* Callback Control Protocol */
	{ PPP_CHAP,	"CHAP" },	/* Cryptographic Handshake Auth. Proto */
	{ PPP_EAP,	"EAP" },	/* Extensible Auth. Protocol */
	{ PPP_CDP,	"CDP" },
	{ PPP_CDPCP,	"CDPCP" },
};

struct ppp_control {
	uint8_t		code;
	uint8_t		id;
	uint16_t	len;
};

struct ppp_cp_type {
	const char	 *unkname;
	int		  mincode;
	int		  maxcode;
	const char 	**codes;
};

/* LCP */

#define LCP_CONF_REQ	1
#define LCP_CONF_ACK	2
#define LCP_CONF_NAK	3
#define LCP_CONF_REJ	4
#define LCP_TERM_REQ	5
#define LCP_TERM_ACK	6
#define LCP_CODE_REJ	7
#define LCP_PROT_REJ	8
#define LCP_ECHO_REQ	9
#define LCP_ECHO_RPL	10
#define LCP_DISC_REQ	11

#define LCP_MIN	LCP_CONF_REQ
#define LCP_MAX LCP_DISC_REQ

static const char *lcpcodes[] = {
	/*
	 * LCP code values (RFC1661, pp26)
	 */
	"Configure-Request",
	"Configure-Ack",
	"Configure-Nak",
	"Configure-Reject",
	"Terminate-Request",
	"Terminate-Ack",
 	"Code-Reject",
	"Protocol-Reject",
	"Echo-Request",
	"Echo-Reply",
	"Discard-Request",
};

#define LCPOPT_VEXT	0
#define LCPOPT_MRU	1
#define LCPOPT_ACCM	2
#define LCPOPT_AP	3
#define LCPOPT_QP	4
#define LCPOPT_MN	5
#define LCPOPT_PFC	7
#define LCPOPT_ACFC	8

static char *lcpconfopts[] = {
	"Vendor-Ext",
	"Max-Rx-Unit",
	"Async-Ctrl-Char-Map",
	"Auth-Prot",
	"Quality-Prot",
	"Magic-Number",
	"unassigned (6)",	
	"Prot-Field-Compr",
	"Add-Ctrl-Field-Compr",
	"FCS-Alternatives",
	"Self-Describing-Pad",
	"Numbered-Mode",
	"Multi-Link-Procedure",
	"Call-Back",
	"Connect-Time"
	"Compund-Frames",
	"Nominal-Data-Encap",
	"Multilink-MRRU",
	"Multilink-SSNHF",
	"Multilink-ED",
	"Proprietary",
	"DCE-Identifier",
	"Multilink-Plus-Proc",
	"Link-Discriminator",
	"LCP-Auth-Option",
};

/* CHAP */

#define CHAP_CHAL	1
#define CHAP_RESP	2
#define CHAP_SUCC	3
#define CHAP_FAIL	4

#define CHAP_CODEMIN 1
#define CHAP_CODEMAX 4

static const char *chapcode[] = {
	"Challenge",
	"Response",
	"Success",
	"Failure",	
};

/* PAP */

#define PAP_AREQ	1
#define PAP_AACK	2
#define PAP_ANAK	3

#define PAP_CODEMIN	1
#define PAP_CODEMAX	3

static const char *papcode[] = {
	"Authenticate-Request",
	"Authenticate-Ack",
	"Authenticate-Nak",
};

/* EAP */

#define EAP_CHAL	1
#define EAP_RESP	2
#define EAP_SUCC	3
#define EAP_FAIL	4

#define EAP_CODEMIN	EAP_CHAL
#define EAP_CODEMAX	EAP_FAIL

#define EAP_TYPE_IDENTITY	1
#define EAP_TYPE_NOTIFICATION	2
#define EAP_TYPE_NAK		3
#define EAP_TYPE_MD5_CHALLENGE	4
#define EAP_TYPE_OTP		5
#define EAP_TYPE_TOKEN		6

#define EAP_TYPEMIN		EAP_TYPE_IDENTITY
#define EAP_TYPEMAX		EAP_TYPE_TOKEN

static const char *eapcode[] = {
	"Challenge",
	"Response",
	"Success",
	"Failure",	
};

static const char *eaptype[] = {
	"Identity",
	"Notification",
	"Nak",
	"MD5-Challenge",
	"One-Time-Password",
	"Token",
};


/* IPCP */

#define IPCP_CODE_CFG_REQ	1
#define IPCP_CODE_CFG_ACK	2
#define IPCP_CODE_CFG_NAK	3
#define IPCP_CODE_CFG_REJ	4
#define IPCP_CODE_TRM_REQ	5
#define IPCP_CODE_TRM_ACK	6
#define IPCP_CODE_COD_REJ	7

#define IPCP_CODE_MIN IPCP_CODE_CFG_REQ
#define IPCP_CODE_MAX IPCP_CODE_COD_REJ

#define IPCP_2ADDR	1
#define IPCP_CP		2
#define IPCP_ADDR	3

/* IPV6CP */

#define IPV6CP_CODE_CFG_REQ	1
#define IPV6CP_CODE_CFG_ACK	2
#define IPV6CP_CODE_CFG_NAK	3
#define IPV6CP_CODE_CFG_REJ	4
#define IPV6CP_CODE_TRM_REQ	5
#define IPV6CP_CODE_TRM_ACK	6
#define IPV6CP_CODE_COD_REJ	7

#define IPV6CP_CODE_MIN IPV6CP_CODE_CFG_REQ
#define IPV6CP_CODE_MAX IPV6CP_CODE_COD_REJ

#define IPV6CP_IFID	1

static int print_lcp_config_options(const u_char *p, int);
static void handle_lcp(const u_char *, int);
static void handle_chap(const u_char *p, int);
static void handle_eap(const u_char *p, int);
static void handle_ipcp(const u_char *p, int);
static int print_ipcp_config_options(const u_char *, int);
static void handle_ipv6cp(const u_char *p, int);
static int print_ipv6cp_config_options(const u_char *, int);
static void handle_pap(const u_char *p, int);

struct pppoe_header {
	u_int8_t vertype;	/* PPPoE version/type */
	u_int8_t code;		/* PPPoE code (packet type) */
	u_int16_t sessionid;	/* PPPoE session id */
	u_int16_t len;		/* PPPoE payload length */
};
#define	PPPOE_CODE_SESSION	0x00	/* Session */
#define	PPPOE_CODE_PADO		0x07	/* Active Discovery Offer */
#define	PPPOE_CODE_PADI		0x09	/* Active Discovery Initiation */
#define	PPPOE_CODE_PADR		0x19	/* Active Discovery Request */
#define	PPPOE_CODE_PADS		0x65	/* Active Discovery Session-Confirm */
#define	PPPOE_CODE_PADT		0xa7	/* Active Discovery Terminate */
#define	PPPOE_TAG_END_OF_LIST		0x0000	/* End Of List */
#define	PPPOE_TAG_SERVICE_NAME		0x0101	/* Service Name */
#define	PPPOE_TAG_AC_NAME		0x0102	/* Access Concentrator Name */
#define	PPPOE_TAG_HOST_UNIQ		0x0103	/* Host Uniq */
#define	PPPOE_TAG_AC_COOKIE		0x0104	/* Access Concentratr Cookie */
#define	PPPOE_TAG_VENDOR_SPEC		0x0105	/* Vendor Specific */
#define	PPPOE_TAG_RELAY_SESSION		0x0110	/* Relay Session Id */
#define	PPPOE_TAG_MAX_PAYLOAD		0x0120	/* RFC 4638 Max Payload */
#define	PPPOE_TAG_SERVICE_NAME_ERROR	0x0201	/* Service Name Error */
#define	PPPOE_TAG_AC_SYSTEM_ERROR	0x0202	/* Acc. Concentrator Error */
#define	PPPOE_TAG_GENERIC_ERROR		0x0203	/* Generic Error */

static void
ppp_protoname(uint16_t proto)
{
	const struct protonames *protoname;
	int i;

	/* bsearch? */
	for (i = 0; i < nitems(protonames); i++) {
		protoname = &protonames[i];

		if (proto == protoname->protocol) {
			printf("%s ", protoname->name);
			return;
		}
	}

	printf("unknown-ppp-%04x", proto);
}

void
ppp_print(const u_char *p, u_int length)
{
	uint16_t proto;
	int l;

	l = snapend - p;

	if (l < sizeof(proto)) {
		printf("[|ppp]");
		return;
	}

	proto = EXTRACT_16BITS(p);

	p += sizeof(proto);
	l -= sizeof(proto);
	length -= sizeof(proto);

	if (eflag)
		ppp_protoname(proto);

	switch (proto) {
	case PPP_IP:
		ip_print(p, length);
		return;
	case PPP_IPV6:
		ip6_print(p, length);
		return;
	}

	if (!eflag)
		ppp_protoname(proto);

	switch(proto) {
	case PPP_LCP:
		handle_lcp(p, l);
		break;
	case PPP_CHAP:
		handle_chap(p, l);
		break;
	case PPP_EAP:
		handle_eap(p, l);
		break;
	case PPP_PAP:
		handle_pap(p, l);
		break;
	case PPP_IPCP:
		handle_ipcp(p, l);
		break;
	case PPP_IPV6CP:
		handle_ipv6cp(p, l);
		break;
	case PPP_CDP:
		cdp_print(p, length, l, 0);
		break;
	}
}

static int
ppp_cp_header(struct ppp_control *pc, const u_char *p, int l,
    const struct ppp_cp_type *t)
{
	uint8_t code;
	int off = 0;
	int len;

	len = sizeof(pc->code);
	if (l < len)
		return (-1);

	pc->code = code = *(p + off);
	if (code >= t->mincode && code <= t->maxcode)
		printf("%s ", t->codes[code - 1]);
	else
		printf("unknown-%s-%u ", t->unkname, pc->code);

	off = len;
	len += sizeof(pc->id);
	if (l < len)
		return (-1);

	pc->id = *(p + off);
	printf("Id=0x%02x:", pc->id);

	off = len;
	len += sizeof(pc->len);
	if (l < len)
		return (-1);

	pc->len = EXTRACT_16BITS(p + off);

	return (len);
}

/* print LCP frame */

static const struct ppp_cp_type ppp_cp_lcp = {
	"lcp",
	LCP_MIN, LCP_MAX,
	lcpcodes,
};

static void
handle_lcp(const u_char *p, int l)
{
	struct ppp_control pc;
	int i;

	if (ppp_cp_header(&pc, p, l, &ppp_cp_lcp) == -1)
		goto trunc;

	if (l > pc.len)
		l = pc.len;

	p += sizeof(pc);
	l -= sizeof(pc);

	switch (pc.code) {
	case LCP_CONF_REQ:
	case LCP_CONF_ACK:
	case LCP_CONF_NAK:
	case LCP_CONF_REJ:
		while (l > 0) {
			int optlen;
	
			optlen = print_lcp_config_options(p, l);
			if (optlen == -1)
				goto trunc;
			if (optlen == 0)
				break;

			p += optlen;
			l -= optlen;
		}
		break;
	case LCP_ECHO_REQ:
	case LCP_ECHO_RPL:
		if (l < 4)
			goto trunc;
		printf(" Magic-Number=%u", EXTRACT_32BITS(p));
		p += 4;
		l -= 4;

		i = sizeof(pc) + 4;
		if (i == pc.len)
			break;

		printf(" Data=");
		do {
			if (l == 0)
				goto trunc;

			printf("%02x", *p);

			p++;
			l--;
		} while (++i < pc.len);
		break;
	case LCP_TERM_REQ:
	case LCP_TERM_ACK:
	case LCP_CODE_REJ:
	case LCP_PROT_REJ:
	case LCP_DISC_REQ:
	default:
		break;
	}
	return;

trunc:
	printf("[|lcp]");
}

/* LCP config options */

static int
print_lcp_config_options(const u_char *p, int l)
{
	uint8_t type, length;
	uint16_t proto;

	if (l < sizeof(type))
		return (-1);

	type = p[0];
	if (type < nitems(lcpconfopts))
		printf(" %s", lcpconfopts[type]);
	else
		printf(" unknown-lcp-%u", type);

	if (l < sizeof(type) + sizeof(length))
		return (-1);

	length = p[1];

	if (length < sizeof(type) + sizeof(length))
		return (0);

	if (l > length)
		l = length;

	p += sizeof(type) + sizeof(length);
	l -= sizeof(type) + sizeof(length);

	switch (type) {
	case LCPOPT_MRU:
		if (length != 4)
			goto invalid;
		if (l < 2)
			return (-1);

		printf("=%u", EXTRACT_16BITS(p));
		break;
	case LCPOPT_AP:
		if (length < 4)
			goto invalid;
		if (l < sizeof(proto))
			return (-1);

		proto = EXTRACT_16BITS(p);
		switch (proto) {
		case PPP_PAP:
			printf("=PAP");
			break;
		case PPP_CHAP:
			printf("=CHAP");
			if (length < 5)
				goto invalid;

			p += sizeof(proto);
			l -= sizeof(proto);
	
			type = *p;
			switch (type) {
			case 0x05:
				printf("/MD5");
				break;
			case 0x80:
				printf("/Microsoft");
				break;
			default:
				printf("/unknown-algorithm-%02x", type);
				break;
			}
			break;
		case PPP_EAP:
			printf("=EAP");
			break;
		case 0xc027:
			printf("=SPAP");
			break;
		case 0xc127:
			printf("=Old-SPAP");
			break;
		default:
			printf("=unknown-ap-%04x", proto);
			break;
		}
		break;
	case LCPOPT_QP:
		if (length < 4)
			goto invalid;
		if (l < sizeof(proto))
			return (-1);

		proto = EXTRACT_16BITS(p);
		switch (proto) {
		case PPP_LQR:
			printf(" LQR");
			break;
		default:
			printf(" unknown-qp-%u", proto);
		}
		break;
	case LCPOPT_MN:
		if (length < 6)
			goto invalid;
		if (l < 4)
			return (-1);

		printf("=%u", EXTRACT_32BITS(p));
		break;
	case LCPOPT_PFC:
		printf(" PFC");
		break;
	case LCPOPT_ACFC:
		printf(" ACFC");
		break;
	}

	return (length);

invalid:
	printf(" invalid opt len %u", length);
	return (length);
}

/* CHAP */

static const struct ppp_cp_type ppp_cp_chap = {
	"chap",
	CHAP_CODEMIN, CHAP_CODEMAX,
	chapcode,
};

static void
handle_chap(const u_char *p, int l)
{
	struct ppp_control pc;
	uint8_t vsize;
	int i;

	if (ppp_cp_header(&pc, p, l, &ppp_cp_chap) == -1)
		goto trunc;

	if (l > pc.len)
		l = pc.len;

	p += sizeof(pc);
	l -= sizeof(pc);

	switch (pc.code) {
	case CHAP_CHAL:
	case CHAP_RESP:
		if (l < sizeof(vsize))
			goto trunc;

		vsize = *p;
		if (vsize < 1) {
			printf(" invalid Value-Size");
			return;
		}

		p += sizeof(vsize);
		l -= sizeof(vsize);

		printf(" Value=");
		for (i = 0; i < vsize; i++) {
			if (l == 0)
				goto trunc;

			printf("%02x", *p);

			p++;
			l--;
		}

		printf(" Name=");
		for (i += sizeof(pc) + sizeof(vsize); i < pc.len; i++) {
			if (l == 0)
				goto trunc;

			safeputchar(*p);

			p++;
			l--;
		}
		break;
	case CHAP_SUCC:
	case CHAP_FAIL:
		printf(" Message=");
		for (i = sizeof(pc); i < pc.len; i++) {
			if (l == 0)
				goto trunc;

			safeputchar(*p);

			p++;
			l--;
		}
		break;
	}
	return;

trunc:
	printf("[|chap]");
}

/* EAP */

static const struct ppp_cp_type ppp_cp_eap = {
	"eap",
	EAP_CODEMIN, EAP_CODEMAX,
	eapcode,
};

static void
handle_eap(const u_char *p, int l)
{
	struct ppp_control pc;
	uint8_t type, vsize;
	int i;

	if (ppp_cp_header(&pc, p, l, &ppp_cp_eap) == -1)
		goto trunc;

	if (l > pc.len)
		l = pc.len;

	p += sizeof(pc);
	l -= sizeof(pc);

	switch (pc.code) {
	case EAP_CHAL:
	case EAP_RESP:
		if (l < sizeof(type))
			goto trunc;

		type = *p;
		p += sizeof(type);
		l -= sizeof(type);

		if (type >= EAP_TYPEMIN && type <= EAP_TYPEMAX)
			printf(" %s", eaptype[type - 1]);
		else {
			printf(" unknown-eap-type-%u", type);
			return;
		}

		switch (type) {
		case EAP_TYPE_IDENTITY:
		case EAP_TYPE_NOTIFICATION:
		case EAP_TYPE_OTP:
			i = sizeof(pc) + sizeof(type);
			if (i == pc.len)
				break;

			printf("=");
			do {
				if (l == 0)
					goto trunc;

				safeputchar(*p);

				p++;
				l--;
			} while (++i < pc.len);
			break;

		case EAP_TYPE_NAK:
			if (l < sizeof(type))
				goto trunc;
			type = *p;
			if (type >= EAP_TYPEMIN && type <= EAP_TYPEMAX)
				printf(" %s", eaptype[type - 1]);
			else
				printf(" unknown-eap-type-%u", type);
			break;
		case EAP_TYPE_MD5_CHALLENGE:
			if (l < sizeof(vsize))
				goto trunc;

			vsize = *p;
			p += sizeof(vsize);
			l -= sizeof(vsize);

			printf("=");
			for (i = 0; i < vsize; i++) {
				if (l == 0)
					goto trunc;

				printf("%02x", *p);

				p++;
				l--;
			}
			break;
		}
		break;
	case CHAP_SUCC:
	case CHAP_FAIL:
		break;
	}
	return;

trunc:
	printf("[|eap]");
}

/* PAP */

static const struct ppp_cp_type ppp_cp_pap = {
	"pap",
	PAP_CODEMIN, PAP_CODEMAX,
	papcode,
};

static void
handle_pap(const u_char *p, int l)
{
	struct ppp_control pc;
	uint8_t x;
	int i;

	if (ppp_cp_header(&pc, p, l, &ppp_cp_pap) == -1)
		goto trunc;

	if (l > pc.len)
		l = pc.len;

	p += sizeof(pc);
	l -= sizeof(pc);

	switch (pc.code) {
	case PAP_AREQ:
		if (l < sizeof(x)) /* Peer-ID Length */
			goto trunc;

		x = *p;

		p += sizeof(x);
		l -= sizeof(x);
	
		printf(" Peer-Id=");
		for (i = 0; i < x; i++) {
			if (l == 0)
				goto trunc;

			safeputchar(*p);

			p++;
			l--;
		}

		if (l < sizeof(x)) /* Passwd-Length */
			goto trunc;

		x = *p;

		p += sizeof(x);
		l -= sizeof(x);
	
		printf(" Passwd=");
		for (i = 0; i < x; i++) {
			if (l == 0)
				goto trunc;

			safeputchar(*p);

			p++;
			l--;
		}
		break;

	case PAP_AACK:
	case PAP_ANAK:
		if (l < sizeof(x)) /* Msg-Length */
			goto trunc;

		x = *p;

		p += sizeof(x);
		l -= sizeof(x);
	
		printf(" Message=");
		for (i = 0; i < x; i++) {
			if (l == 0)
				goto trunc;

			safeputchar(*p);

			p++;
			l--;
		}
		break;
	}

	return;

trunc:
	printf("[|pap]");
}

/* IPCP */

#define IP_LEN 4
#define IP_FMT "%u.%u.%u.%u"
#define IP_ARG(_p) (_p)[0], (_p)[1], (_p)[2], (_p)[3]

static const struct ppp_cp_type ppp_cp_ipcp = {
	"ipcp",
	IPCP_CODE_MIN, IPCP_CODE_MAX,
	lcpcodes,
};

static void
handle_ipcp(const u_char *p, int l)
{
	struct ppp_control pc;

	if (ppp_cp_header(&pc, p, l, &ppp_cp_ipcp) == -1)
		goto trunc;

	if (l > pc.len)
		l = pc.len;

	p += sizeof(pc);
	l -= sizeof(pc);

	switch (pc.code) {
	case IPCP_CODE_CFG_REQ:
	case IPCP_CODE_CFG_ACK:
	case IPCP_CODE_CFG_NAK:
	case IPCP_CODE_CFG_REJ:
		while (l > 0) {
			int optlen;
	
			optlen = print_ipcp_config_options(p, l);
			if (optlen == -1)
				goto trunc;
			if (optlen == 0)
				break;

			p += optlen;
			l -= optlen;
		}
		break;

	case IPCP_CODE_TRM_REQ:
	case IPCP_CODE_TRM_ACK:
	case IPCP_CODE_COD_REJ:
	default:
		break;
	}

	return;

trunc:
	printf("[|ipcp]");
}

static int
print_ipcp_config_options(const u_char *p, int l)
{
	uint8_t type, length;

	if (l < sizeof(type))
		return (-1);

	type = p[0];
	switch (type) {
	case IPCP_2ADDR:
		printf(" IP-Addresses");
		break;
	case IPCP_CP:
		printf(" IP-Compression-Protocol");
		break;
	case IPCP_ADDR:
		printf(" IP-Address");
		break;
	default:
		printf(" ipcp-type-%u", type);
		break;
	}

	if (l < sizeof(type) + sizeof(length))
		return (-1);

	length = p[1];

	p += (sizeof(type) + sizeof(length));
	l -= (sizeof(type) + sizeof(length));

	switch (type) {
	case IPCP_2ADDR:
		if (length != 10)
			goto invalid;
		if (l < IP_LEN)
			return (-1);

		printf(" Src=" IP_FMT, IP_ARG(p));

		p += IP_LEN;
		l -= IP_LEN;

		if (l < IP_LEN)
			return (-1);

		printf(" Dst=" IP_FMT, IP_ARG(p));
		break;
	case IPCP_CP:
		if (length < 4)
			goto invalid;
		if (l < sizeof(type))
			return (-1);

		type = EXTRACT_16BITS(p);
		switch (type) {
		case 0x0037:
			printf(" Van Jacobsen Compressed TCP/IP");
			break;
		default:
			printf("ipcp-compression-type-%u", type);
			break;
		}
		break;
	case IPCP_ADDR:
		if (length != 6)
			goto invalid;
		if (l < IP_LEN)
			return (-1);

		printf("=" IP_FMT, IP_ARG(p));
		break;
	}

	return (length);

invalid:
	printf(" invalid opt len %u", length);
	return (length);
}

/* IPV6CP */

static const struct ppp_cp_type ppp_cp_ipv6cp = {
	"ipv6cp",
	IPV6CP_CODE_MIN, IPV6CP_CODE_MAX,
	lcpcodes,
};

static void
handle_ipv6cp(const u_char *p, int l)
{
	struct ppp_control pc;

	if (ppp_cp_header(&pc, p, l, &ppp_cp_ipv6cp) == -1)
		goto trunc;

	if (l > pc.len)
		l = pc.len;

	p += sizeof(pc);
	l -= sizeof(pc);

	switch (pc.code) {
	case IPV6CP_CODE_CFG_REQ:
	case IPV6CP_CODE_CFG_ACK:
	case IPV6CP_CODE_CFG_NAK:
	case IPV6CP_CODE_CFG_REJ:
		while (l > 0) {
			int optlen;
	
			optlen = print_ipv6cp_config_options(p, l);
			if (optlen == -1)
				goto trunc;
			if (optlen == 0)
				break;

			p += optlen;
			l -= optlen;
		}
		break;

	case IPV6CP_CODE_TRM_REQ:
	case IPV6CP_CODE_TRM_ACK:
	case IPV6CP_CODE_COD_REJ:
	default:
		break;
	}

	return;

trunc:
	printf("[|ipv6cp]");
}

static int
print_ipv6cp_config_options(const u_char *p, int l)
{
	uint8_t type, length;

	if (l < sizeof(type))
		return (-1);

	type = p[0];
	switch (type) {
	case IPV6CP_IFID:
		printf(" IPv6-Interface-Id");
		break;
	default:
		printf(" ipv6cp-type-%u", type);
		break;
	}

	if (l < sizeof(type) + sizeof(length))
		return (-1);

	length = p[1];

	p += (sizeof(type) + sizeof(length));
	l -= (sizeof(type) + sizeof(length));

	switch (type) {
	case IPV6CP_IFID:
		if (length != 10)
			goto invalid;
		if (l < 8)
			return (-1);

		printf("=%04x:%04x:%04x:%04x", EXTRACT_16BITS(p + 0),
		    EXTRACT_16BITS(p + 2), EXTRACT_16BITS(p + 4), 
		    EXTRACT_16BITS(p + 6));
		break;
	default:
		break;
	}

	return (length);
invalid:
	printf(" invalid opt len %u", length);
	return (length);
}

void
ppp_if_print(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int length = h->len;
	u_int caplen = h->caplen;

	packetp = p;
	snapend = p + caplen;

	ts_print(&h->ts);

	ppp_hdlc_print(p, length);

	if (xflag)
		default_print((const u_char *)(p + PPP_HDRLEN),
		    caplen - PPP_HDRLEN);

	putchar('\n');
}

void
ppp_ether_if_print(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int16_t pppoe_sid, pppoe_len;
	u_int l = h->caplen;
	u_int length = h->len;

	packetp = p;
	snapend = p + l;

	ts_print(&h->ts);

	if (eflag)
		printf("PPPoE ");

	if (l < sizeof(struct pppoe_header)) {
		printf("[|pppoe]");
		return;
	}

	pppoe_sid = EXTRACT_16BITS(p + 2);
	pppoe_len = EXTRACT_16BITS(p + 4);

	if (eflag) {
		printf("\n\tcode ");
		switch (p[1]) {
		case PPPOE_CODE_PADI:
			printf("Initiation");
			break;
		case PPPOE_CODE_PADO:
			printf("Offer");
			break;
		case PPPOE_CODE_PADR:
			printf("Request");
			break;
		case PPPOE_CODE_PADS:
			printf("Confirm");
			break;
		case PPPOE_CODE_PADT:
			printf("Terminate");
			break;
		case PPPOE_CODE_SESSION:
			printf("Session");
			break;
		default:
			printf("Unknown(0x%02x)", p[1]);
			break;
		}
		printf(", version %d, type %d, id 0x%04x, length %d\n\t",
		    (p[0] & 0xf), (p[0] & 0xf0) >> 4, pppoe_sid, pppoe_len);
	}

	if (length < pppoe_len) {
                printf(" truncated-pppoe - %d bytes missing!",
                    pppoe_len - length);
                pppoe_len = length;
        }

	ppp_print(p + sizeof(struct pppoe_header), pppoe_len);

	if (xflag)
		default_print(p, h->caplen);

	putchar('\n');
}

int
pppoe_if_print(u_short ethertype, const u_char *p, u_int length, u_int l)
{
	uint16_t pppoe_sid, pppoe_len;

	if (ethertype == ETHERTYPE_PPPOEDISC)
		printf("PPPoE-Discovery");
	else
		printf("PPPoE-Session");

	if (l < sizeof(struct pppoe_header))
		goto trunc;

	printf("\n\tcode ");
	switch (p[1]) {
	case PPPOE_CODE_PADI:
		printf("Initiation");
		break;
	case PPPOE_CODE_PADO:
		printf("Offer");
		break;
	case PPPOE_CODE_PADR:
		printf("Request");
		break;
	case PPPOE_CODE_PADS:
		printf("Confirm");
		break;
	case PPPOE_CODE_PADT:
		printf("Terminate");
		break;
	case PPPOE_CODE_SESSION:
		printf("Session");
		break;
	default:
		printf("Unknown(0x%02x)", p[1]);
		break;
	}

	pppoe_sid = EXTRACT_16BITS(p + 2);
	pppoe_len = EXTRACT_16BITS(p + 4);
	printf(", version %d, type %d, id 0x%04x, length %d",
	    (p[0] & 0xf), (p[0] & 0xf0) >> 4, pppoe_sid, pppoe_len);

	p += sizeof(struct pppoe_header);
	l -= sizeof(struct pppoe_header);
	length -= sizeof(struct pppoe_header);

	if (length < pppoe_len) {
                printf(" truncated-pppoe - %d bytes missing!",
                    pppoe_len - length);
                pppoe_len = length;
        }

	if (l > pppoe_len)
		l = pppoe_len;

	if (ethertype == ETHERTYPE_PPPOEDISC) {
		while (l > 0) {
			u_int16_t t_type, t_len;
			int text = 0;

			if (l < 4)
				goto trunc;
			t_type = EXTRACT_16BITS(p);
			t_len = EXTRACT_16BITS(p + 2);

			p += 4;
			l -= 4;

			if (l < t_len)
				goto trunc;

			printf("\n\ttag ");
			switch (t_type) {
			case PPPOE_TAG_END_OF_LIST:
				printf("End-Of-List");
				break;
			case PPPOE_TAG_SERVICE_NAME:
				printf("Service-Name");
				text = 1;
				break;
			case PPPOE_TAG_AC_NAME:
				printf("AC-Name");
				text = 1;
				break;
			case PPPOE_TAG_HOST_UNIQ:
				printf("Host-Uniq");
				break;
			case PPPOE_TAG_AC_COOKIE:
				printf("AC-Cookie");
				break;
			case PPPOE_TAG_VENDOR_SPEC:
				printf("Vendor-Specific");
				break;
			case PPPOE_TAG_RELAY_SESSION:
				printf("Relay-Session");
				break;
			case PPPOE_TAG_MAX_PAYLOAD:
				printf("PPP-Max-Payload");
				break;
			case PPPOE_TAG_SERVICE_NAME_ERROR:
				printf("Service-Name-Error");
				text = 1;
				break;
			case PPPOE_TAG_AC_SYSTEM_ERROR:
				printf("AC-System-Error");
				text = 1;
				break;
			case PPPOE_TAG_GENERIC_ERROR:
				printf("Generic-Error");
				text = 1;
				break;
			default:
				printf("Unknown(0x%04x)", t_type);
			}
			printf(", length %u%s", t_len, t_len ? " " : "");

			if (t_len && text == 1) {
				for (t_type = 0; t_type < t_len; t_type++) {
					if (isprint(p[t_type]))
						printf("%c", p[t_type]);
					else
						printf("\\%03o", p[t_type]);
				}
			} else if (t_len) {
				printf("0x");
				for (t_type = 0; t_type < t_len; t_type++)
					printf("%02x", p[t_type]);
			}
			p += t_len;
			l -= t_len;
		}
	} else if (ethertype == ETHERTYPE_PPPOE) {
		printf("\n\t");
		ppp_print(p, pppoe_len);
	}

	return (1);

trunc:
	printf("[|pppoe]");
	return (1);
}

void
ppp_hdlc_print(const u_char *p, u_int length)
{
	uint8_t address, control;
	int l;

	l = snapend - p;

	if (l < sizeof(address) + sizeof(control))
		goto trunc;

	address = p[0];
	control = p[1];

	p += sizeof(address) + sizeof(control);
	l -= sizeof(address) + sizeof(control);
	length -= sizeof(address) + sizeof(control);

	switch (address) {
	case 0xff: /* All-Stations */
		if (eflag)
			printf("%02x %02x %u ", address, control, length);

		if (control != 0x3) {
			printf(" discard");
			break;
		}

		ppp_print(p, length);
		break;

	default:
		printf("ppp address 0x%02x unknown", address);
		break;
	}
	return;

trunc:
	printf("[|ppp]");
}

void
ppp_hdlc_if_print(u_char *user, const struct pcap_pkthdr *h,
    const u_char *p)
{
	int l = h->caplen;

	packetp = p;
	snapend = p + l;

	ts_print(&h->ts);

	if (eflag)
		printf("PPP ");

	ppp_hdlc_print(p, h->len);

	if (xflag)
		default_print(p, l);

	printf("\n");
}

#else

#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>

#include "interface.h"
void
ppp_if_print(user, h, p)
	u_char *user;
	const struct pcap_pkthdr *h;
	const u_char *p;
{
	error("not configured for ppp");
	/* NOTREACHED */
}
#endif

/*	$OpenBSD: print-stp.c,v 1.12 2023/09/06 05:54:07 jsg Exp $	*/

/*
 * Copyright (c) 2000 Jason L. Wright (jason@thought.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Pretty print 802.1D Bridge Protocol Data Units
 */

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
#include "llc.h"

#define	STP_MSGTYPE_CBPDU	0x00
#define	STP_MSGTYPE_RSTP	0x02
#define	STP_MSGTYPE_TBPDU	0x80

#define	STP_FLAGS_STPMASK	0x81		/* strip unused STP flags */
#define	STP_FLAGS_RSTPMASK	0x7f		/* strip unused RSTP flags */
#define	STP_FLAGS_TC		0x01		/* Topology change */
#define	STP_FLAGS_P		0x02		/* Proposal flag */
#define	STP_FLAGS_ROLE		0x0c		/* Port Role */
#define	STP_FLAGS_ROLE_S	2		/* Port Role offset */
#define	STP_FLAGS_ROLE_ALT	1		/* Alt/Backup port */
#define	STP_FLAGS_ROLE_ROOT	2		/* Root port */
#define	STP_FLAGS_ROLE_DESG	3		/* Designated port */
#define	STP_FLAGS_L		0x10		/* Learning flag */
#define	STP_FLAGS_F		0x20		/* Forwarding flag */
#define	STP_FLAGS_A		0x40		/* Agreement flag */
#define	STP_FLAGS_TCA		0x80		/* Topology change ack */
#define STP_FLAGS_BITS								\
	"\20\1TC\2PROPOSAL\5LEARNING\6FORWARDING\7AGREED\10TCACK"

enum {
	STP_PROTO_STP	= 0x00,
	STP_PROTO_RSTP	= 0x02,
	STP_PROTO_SSTP	= 0x10	/* Cizzco-Eeeh */
};

static void stp_print_cbpdu(const u_char *, u_int, int);
static void stp_print_tbpdu(const u_char *, u_int);

void
stp_print(const u_char *p, u_int len)
{
	u_int16_t id;
	int proto = STP_PROTO_STP;

	if (len < 3)
		goto truncated;
	if (p[0] == LLCSAP_8021D && p[1] == LLCSAP_8021D && p[2] == LLC_UI)
		printf("802.1d");
	else if (p[0] == LLCSAP_SNAP && p[1] == LLCSAP_SNAP && p[2] == LLC_UI) {
		proto = STP_PROTO_SSTP;
		printf("SSTP");
		if (len < 8)
			goto truncated;
		p += 5;
		len -= 5;
	} else {
		printf("invalid protocol");
		return;
	}
	p += 3;
	len -= 3;

	if (len < 3)
		goto truncated;
	id = EXTRACT_16BITS(p);
	if (id != 0) {
		printf(" unknown protocol id(0x%x)", id);
		return;
	}
	switch (p[2]) {
	case STP_PROTO_STP:
		printf(" STP");
		break;
	case STP_PROTO_RSTP:
		printf(" RSTP");
		break;
	default:
		printf(" unknown protocol ver(0x%x)", p[2]);
		return;
	}
	p += 3;
	len -= 3;

	if (len < 1)
		goto truncated;
	switch (*p) {
	case STP_MSGTYPE_CBPDU:
		stp_print_cbpdu(p, len, proto);
		break;
	case STP_MSGTYPE_RSTP:
		stp_print_cbpdu(p, len, STP_PROTO_RSTP);
		break;
	case STP_MSGTYPE_TBPDU:
		stp_print_tbpdu(p, len);
		break;
	default:
		printf(" unknown message (0x%02x)", *p);
		break;
	}

	return;

truncated:
	printf("[|802.1d]");
}

static void
stp_print_cbpdu(const u_char *p, u_int len, int proto)
{
	u_int32_t cost;
	u_int16_t t;
	u_int8_t flags, role;
	int x;

	p += 1;
	len -= 1;

	printf(" config");

	if (len < 1)
		goto truncated;
	if (*p) {
		switch (proto) {
		case STP_PROTO_STP:
		case STP_PROTO_SSTP:
			flags = *p & STP_FLAGS_STPMASK;
			role = STP_FLAGS_ROLE_DESG;
			break;
		case STP_PROTO_RSTP:
		default:
			flags = *p & STP_FLAGS_RSTPMASK;
			role = (flags & STP_FLAGS_ROLE) >> STP_FLAGS_ROLE_S;
			break;
		}

		printb(" flags", flags, STP_FLAGS_BITS);
		switch (role) {
		case STP_FLAGS_ROLE_ALT:
			printf(" role=ALT/BACKUP");
			break;
		case STP_FLAGS_ROLE_ROOT:
			printf(" role=ROOT");
			break;
		case STP_FLAGS_ROLE_DESG:
			printf(" role=DESIGNATED");
			break;
		}
	}
	p += 1;
	len -= 1;

	if (len < 8)
		goto truncated;
	printf(" root=");
	printf("%x.", EXTRACT_16BITS(p));
	p += 2;
	len -= 2;
	for (x = 0; x < 6; x++) {
		printf("%s%x", (x != 0) ? ":" : "", *p);
		p++;
		len--;
	}

	if (len < 4)
		goto truncated;
	cost = EXTRACT_32BITS(p);
	printf(" rootcost=%u", cost);
	p += 4;
	len -= 4;

	if (len < 8)
		goto truncated;
	printf(" bridge=");
	printf("%x.", EXTRACT_16BITS(p));
	p += 2;
	len -= 2;
	for (x = 0; x < 6; x++) {
		printf("%s%x", (x != 0) ? ":" : "", *p);
		p++;
		len--;
	}

	if (len < 2)
		goto truncated;
	t = EXTRACT_16BITS(p);
	switch (proto) {
	case STP_PROTO_STP:
	case STP_PROTO_SSTP:
		printf(" port=%u", t & 0xff);
		printf(" ifcost=%u", t >> 8);
		break;
	case STP_PROTO_RSTP:
	default:
		printf(" port=%u", t & 0xfff);
		printf(" ifcost=%u", t >> 8);
		break;
	}
	p += 2;
	len -= 2;

	if (len < 2)
		goto truncated;
	printf(" age=%u/%u", p[0], p[1]);
	p += 2;
	len -= 2;

	if (len < 2)
		goto truncated;
	printf(" max=%u/%u", p[0], p[1]);
	p += 2;
	len -= 2;

	if (len < 2)
		goto truncated;
	printf(" hello=%u/%u", p[0], p[1]);
	p += 2;
	len -= 2;

	if (len < 2)
		goto truncated;
	printf(" fwdelay=%u/%u", p[0], p[1]);
	p += 2;
	len -= 2;

	if (proto == STP_PROTO_SSTP) {
		if (len < 7)
			goto truncated;
		p += 1;
		len -= 1;
		if (EXTRACT_16BITS(p) == 0 && EXTRACT_16BITS(p + 2) == 0x02) {
			printf(" pvid=%u", EXTRACT_16BITS(p + 4));
			p += 6;
			len -= 6;
		}
	}

	return;

truncated:
	printf("[|802.1d]");
}

static void
stp_print_tbpdu(const u_char *p, u_int len)
{
	printf(" tcn");
}

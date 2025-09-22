/*	$OpenBSD: print-vqp.c,v 1.8 2018/07/06 05:47:22 dlg Exp $	*/

/*
 * Copyright (c) 2006 Kevin Steves <stevesk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * VLAN Query Protocol (VQP)
 *
 *    0                   1                   2                   3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |    Version    |    Opcode     | Response Code |  Data Count   |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                         Transaction ID                        |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                            Type (1)                           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |             Length            |            Data               /
 *   /                                                               /
 *   /                                                               /
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                            Type (n)                           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |             Length            |            Data               /
 *   /                                                               /
 *   /                                                               /
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * VQP is layered over UDP.  The default destination port is 1589.
 *
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"

struct vqp_hdr {
	u_char version;
	u_char opcode;
	u_char rcode;
	u_char dcount;
	u_int32_t xid;
};

#define VQP_JOIN			0x01
#define VQP_JOIN_RESPONSE		0x02
#define VQP_RECONFIRM			0x03
#define VQP_RECONFIRM_RESPONSE		0x04

#define VQP_NO_ERROR			0x00
#define VQP_WRONG_VERSION		0x01
#define VQP_INSUFFICIENT_RESOURCES	0x02
#define VQP_DENY			0x03
#define VQP_SHUTDOWN			0x04
#define VQP_WRONG_MGMT_DOMAIN		0x05

/* 4 bytes struct in_addr; IP address of VQP client */
#define VQP_CLIENT_ADDR			0x00000c01
/* string */
#define VQP_PORT_NAME			0x00000c02
/* string */
#define VQP_VLAN_NAME			0x00000c03
/* string; VTP domain if set */
#define VQP_DOMAIN_NAME			0x00000c04
/* ethernet frame */
#define VQP_ETHERNET_FRAME		0x00000c05
/* 6 bytes, mac address */
#define VQP_MAC				0x00000c06
/* 2 bytes? */
#define VQP_UNKNOWN			0x00000c07
/* 6 bytes, mac address */
#define VQP_COOKIE			0x00000c08

static void
vqp_print_opcode(u_int val)
{
	switch (val) {
	case VQP_JOIN:
		printf("Join");
		break;
	case VQP_JOIN_RESPONSE:
		printf("JoinResp");
		break;
	case VQP_RECONFIRM:
		printf("Reconfirm");
		break;
	case VQP_RECONFIRM_RESPONSE:
		printf("ReconfirmResp");
		break;
	default:
		printf("unknown(%x)", val);
		break;
	}
}

static void
vqp_print_rcode(u_int val)
{
	switch (val) {
	case VQP_NO_ERROR:
		printf("NoError");
		break;
	case VQP_WRONG_VERSION:
		printf("WrongVersion");
		break;
	case VQP_INSUFFICIENT_RESOURCES:
		printf("InsufficientResources");
		break;
	case VQP_DENY:
		printf("Deny");
		break;
	case VQP_SHUTDOWN:
		printf("Shutdown");
		break;
	case VQP_WRONG_MGMT_DOMAIN:
		printf("WrongMgmtDomain");
		break;
	default:
		printf("unknown(%x)", val);
		break;
	}
}

static void
print_hex(const u_char *p, u_int len)
{
	while (len--)
		printf("%02x", *p++);
}

static void
vqp_print_type(u_int type, u_int len, const u_char *p)
{
	switch (type) {
	case VQP_CLIENT_ADDR:
		printf(" client:");
		if (len == sizeof(struct in_addr)) {
			struct in_addr in;
			memcpy(&in, p, sizeof in);
			printf("%s", inet_ntoa(in));
		} else
			print_hex(p, len);
		break;
	case VQP_PORT_NAME:
		printf(" port:");
		fn_printn(p, len, NULL);
		break;
	case VQP_VLAN_NAME:
		printf(" vlan:");
		fn_printn(p, len, NULL);
		break;
	case VQP_DOMAIN_NAME:
		printf(" domain:");
		fn_printn(p, len, NULL);
		break;
	case VQP_ETHERNET_FRAME:
		printf(" ethernet:");
		if (vflag > 1)
			print_hex(p, len);
		else if (len >= ETHER_ADDR_LEN * 2) {
			p += ETHER_ADDR_LEN;	/* skip dst mac */
			printf("%s", etheraddr_string(p)); /* src mac */
		} else
			print_hex(p, len);
		break;
	case VQP_MAC:
		printf(" mac:");
		if (len == ETHER_ADDR_LEN)
			printf("%s", etheraddr_string(p));
		else
			print_hex(p, len);
		break;
	case VQP_UNKNOWN:
		printf(" unknown:");
		print_hex(p, len);
		break;
	case VQP_COOKIE:
		printf(" cookie:");
		if (len == ETHER_ADDR_LEN)
			printf("%s", etheraddr_string(p));
		else
			print_hex(p, len);
		break;
	default:
		printf(" unknown(%x/%u)", type, len);
	}
}

void
vqp_print(const u_char *bp, u_int len)
{
	struct vqp_hdr *p = (struct vqp_hdr *)bp;
	u_int dcount;

	TCHECK(p->version);
	printf("VQPv%u", p->version);
	if (p->version != 1)
		return;
	TCHECK(p->opcode);
	printf("-");
	vqp_print_opcode(p->opcode);
	TCHECK(p->rcode);
	printf(" rcode:");
	vqp_print_rcode(p->rcode);
	TCHECK(p->xid);
	printf(" xid:0x%08x", ntohl(p->xid));
	printf(" dcount:%u", p->dcount);
	bp += sizeof(struct vqp_hdr);

	dcount = p->dcount;
	while (vflag && dcount > 0) {
		u_int type, length;

		TCHECK2(bp[0], 6);
		type = EXTRACT_32BITS(bp);
		bp += 4;
		length = EXTRACT_16BITS(bp);
		bp += 2;
		TCHECK2(bp[0], length);
		vqp_print_type(type, length, bp);
		bp += length;
		dcount--;
	}

	return;
trunc:
	printf("[|vqp]");
}

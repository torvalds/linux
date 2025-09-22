/*	$OpenBSD: print-lldp.c,v 1.11 2025/01/02 01:21:35 dlg Exp $	*/

/*
 * Copyright (c) 2006 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "addrtoname.h"
#include "extract.h"
#include "interface.h"
#include "afnum.h"

enum {
	LLDP_TLV_END			= 0,
	LLDP_TLV_CHASSIS_ID		= 1,
	LLDP_TLV_PORT_ID		= 2,
	LLDP_TLV_TTL			= 3,
	LLDP_TLV_PORT_DESCR		= 4,
	LLDP_TLV_SYSTEM_NAME		= 5,
	LLDP_TLV_SYSTEM_DESCR		= 6,
	LLDP_TLV_SYSTEM_CAP		= 7,
	LLDP_TLV_MANAGEMENT_ADDR	= 8,
	LLDP_TLV_ORG			= 127
};

enum {
	LLDP_CHASSISID_SUBTYPE_CHASSIS	= 1,
	LLDP_CHASSISID_SUBTYPE_IFALIAS	= 2,
	LLDP_CHASSISID_SUBTYPE_PORT	= 3,
	LLDP_CHASSISID_SUBTYPE_LLADDR	= 4,
	LLDP_CHASSISID_SUBTYPE_ADDR	= 5,
	LLDP_CHASSISID_SUBTYPE_IFNAME	= 6,
	LLDP_CHASSISID_SUBTYPE_LOCAL	= 7
};

enum {
	LLDP_PORTID_SUBTYPE_IFALIAS	= 1,
	LLDP_PORTID_SUBTYPE_PORT	= 2,
	LLDP_PORTID_SUBTYPE_LLADDR	= 3,
	LLDP_PORTID_SUBTYPE_ADDR	= 4,
	LLDP_PORTID_SUBTYPE_IFNAME	= 5,
	LLDP_PORTID_SUBTYPE_AGENTCID	= 6,
	LLDP_PORTID_SUBTYPE_LOCAL	= 7
};

#define LLDP_CAP_OTHER          0x01
#define LLDP_CAP_REPEATER       0x02
#define LLDP_CAP_BRIDGE         0x04
#define LLDP_CAP_WLAN           0x08
#define LLDP_CAP_ROUTER         0x10
#define LLDP_CAP_TELEPHONE      0x20
#define LLDP_CAP_DOCSIS         0x40
#define LLDP_CAP_STATION        0x80
#define LLDP_CAP_BITS								\
	"\20\01OTHER\02REPEATER\03BRIDGE\04WLAN\05ROUTER\06TELEPHONE"		\
	"\07DOCSIS\10STATION"

enum {
	LLDP_MGMT_IFACE_UNKNOWN	= 1,
	LLDP_MGMT_IFACE_IFINDEX	= 2,
	LLDP_MGMT_IFACE_SYSPORT	= 3
};

static const char *afnumber[] = AFNUM_NAME_STR;

void		 lldp_print_str(const u_int8_t *, int);
const char	*lldp_print_addr(int, const void *);
void		 lldp_print_id(int, const u_int8_t *, int);

void
lldp_print_str(const u_int8_t *str, int len)
{
	int i;
	printf("\"");
	for (i = 0; i < len; i++) {
		int ch = str[i];
		if (ch == '\r')
			continue;
		if (ch == '\n')
			ch = ' ';
		printf("%c", isprint(ch) ? ch : '.');
	}
	printf("\"");
}

const char *
lldp_print_addr(int af, const void *addr)
{
	static char buf[48];
	if (inet_ntop(af, addr, buf, sizeof(buf)) == NULL)
		return ("?");
	return (buf);
}

void
lldp_print_id(int type, const u_int8_t *ptr, int len)
{
	u_int8_t id;
	const u_int8_t *data;

	id = *(u_int8_t *)ptr;
	len -= sizeof(u_int8_t);
	data = ptr + sizeof(u_int8_t);
	if (len <= 0)
		return;

	if (type == LLDP_TLV_CHASSIS_ID) {
		switch (id) {
		case LLDP_CHASSISID_SUBTYPE_CHASSIS:
			printf("chassis ");
			lldp_print_str(data, len);
			break;
		case LLDP_CHASSISID_SUBTYPE_IFALIAS:
			printf("ifalias");
			break;
		case LLDP_CHASSISID_SUBTYPE_PORT:
			printf("port");
			break;
		case LLDP_CHASSISID_SUBTYPE_LLADDR:
			printf("lladdr %s", etheraddr_string(data));
			break;
		case LLDP_CHASSISID_SUBTYPE_ADDR:
			printf("addr");
			break;
		case LLDP_CHASSISID_SUBTYPE_IFNAME:
			printf("ifname ");
			lldp_print_str(data, len);
			break;
		case LLDP_CHASSISID_SUBTYPE_LOCAL:
			printf("local ");
			lldp_print_str(data, len);
			break;
		default:
			printf("unknown 0x%02x", id);
			break;
		}

	} else if (type == LLDP_TLV_PORT_ID) {
		switch (id) {
		case LLDP_PORTID_SUBTYPE_IFALIAS:
			printf("ifalias");
			break;
		case LLDP_PORTID_SUBTYPE_PORT:
			printf("port");
			break;
		case LLDP_PORTID_SUBTYPE_LLADDR:
			printf("lladdr %s", etheraddr_string(data));
			break;
		case LLDP_PORTID_SUBTYPE_ADDR:
			printf("addr");
			break;
		case LLDP_PORTID_SUBTYPE_IFNAME:
			printf("ifname ");
			lldp_print_str(data, len);
			break;
		case LLDP_PORTID_SUBTYPE_AGENTCID:
			printf("agentcid");
			break;
		case LLDP_PORTID_SUBTYPE_LOCAL:
			printf("local ");
			lldp_print_str(data, len);
			break;
		default:
			printf("unknown 0x%02x", id);
			break;
		}
	}
}

static void
lldp_print_mgmt_addr(const u_int8_t *ptr, u_int len)
{
	u_int alen;
	u_int afnum;
	const uint8_t *maddr;
	uint32_t ifidx;

	if (len < 1) {
		printf(" unexpected len %u", len);
		return;
	}
	alen = ptr[0];

	ptr++;
	len--;

	if (alen < 2 || alen > len) {
		printf(" unexpected address len %u", len);
		return;
	}
	afnum = ptr[0];
	maddr = ptr + 1;

	ptr += alen;
	len -= alen;

	alen--;
	switch (afnum) {
	case AFNUM_INET:
		if (alen != sizeof(struct in_addr))
			goto afnum_default;
		printf(" %s", lldp_print_addr(AF_INET, maddr));
		break;
	case AFNUM_INET6:
		if (alen != sizeof(struct in6_addr))
			goto afnum_default;
		printf(" %s", lldp_print_addr(AF_INET6, maddr));
		break;
	case AFNUM_802:
		if (alen != ETHER_ADDR_LEN)
			goto afnum_default;
		printf(" %s", etheraddr_string(maddr));
		break;
	default:
	afnum_default:
		if (afnum < AFNUM_MAX)
			printf(" %s", afnumber[afnum]);
		else
			printf(" afnum-%u", afnum);
		printf(" len %u", alen);
		break;
	}

	if (len < 5) {
		printf(" unexpected interface len %u", len);
		return;
	}

	ifidx = EXTRACT_32BITS(ptr + 1);
	if (ifidx != 0) {
		switch (*ptr) {
		case LLDP_MGMT_IFACE_UNKNOWN:
			printf(" Unknown");
			break;
		case LLDP_MGMT_IFACE_IFINDEX:
			printf(" ifIndex");
			break;
		case LLDP_MGMT_IFACE_SYSPORT:
			printf(" sysPort");
			break;
		default:
			printf(" iface-type-%u", *ptr);
			break;
		}
		printf(" %u", ifidx);
	}

	ptr += 5;
	len -= 5;

	if (len < 1) {
		printf(" unexpected oid len %u", len);
		return;
	}
	alen = ptr[0];
	ptr++;
	len--;

	if (alen != len) {
		printf(" unexpected oid len %u/%u", alen, len);
	}
	if (alen == 0)
		return;

	printf(" oid 0x");
	do {
		printf("%02X", *ptr++);
	} while (--alen > 0);
}

void
lldp_print(const u_char *p, u_int len)
{
	u_int16_t tlv;
	const u_int8_t *ptr = (u_int8_t *)p;
	int n, type, vlen;

	printf("LLDP");

	for (n = 0; n < len;) {
		TCHECK2(*ptr, sizeof(tlv));

		tlv = EXTRACT_16BITS(ptr);
		type = (tlv & 0xfe00) >> 9;
		vlen = tlv & 0x1ff;
		n += vlen;

		ptr += sizeof(tlv);
		TCHECK2(*ptr, vlen);

		switch (type) {
		case LLDP_TLV_END:
			goto done;
			break;

		case LLDP_TLV_CHASSIS_ID:
			printf(", ChassisId: ");
			lldp_print_id(type, ptr, vlen);
			break;

		case LLDP_TLV_PORT_ID:
			printf(", PortId: ");
			lldp_print_id(type, ptr, vlen);
			break;

		case LLDP_TLV_TTL:
			printf(", TTL: ");
			if (vlen != 2) {
				printf(" unexpected len %d", vlen);
				break;
			}
			printf("%ds", EXTRACT_16BITS(ptr));
			break;

		case LLDP_TLV_PORT_DESCR:
			printf(", PortDescr: ");
			lldp_print_str(ptr, vlen);
			break;

		case LLDP_TLV_SYSTEM_NAME:
			printf(", SysName: ");
			lldp_print_str(ptr, vlen);
			break;

		case LLDP_TLV_SYSTEM_DESCR:
			printf(", SysDescr: ");
			lldp_print_str(ptr, vlen);
			break;

		case LLDP_TLV_SYSTEM_CAP:
			printf(", CAP:");
			if (vlen != 4) {
				printf(" unexpected len %d", vlen);
				break;
			}
			printb(" available", EXTRACT_16BITS(ptr),
			    LLDP_CAP_BITS);
			printb(" enabled", EXTRACT_16BITS(ptr + 2),
			    LLDP_CAP_BITS);
			break;

		case LLDP_TLV_MANAGEMENT_ADDR:
			printf(", MgmtAddr:");
			lldp_print_mgmt_addr(ptr, vlen);
			break;

		case LLDP_TLV_ORG:
			printf(", Org:");
			if (vlen < 4) {
				printf(" unexpected len %d", vlen);
			}
			printf(" %02X-%02X-%02X type %02x len %u",
			    ptr[0], ptr[1], ptr[2], ptr[3], len - 4);
			break;

		default:
			printf(", type %d length %d", type, vlen);
			break;
		}
		ptr += vlen;
	}

 done:
	return;

 trunc:
	printf(" [|LLDP]");
}


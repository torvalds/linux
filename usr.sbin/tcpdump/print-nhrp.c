/*	$OpenBSD: print-nhrp.c,v 1.2 2022/12/28 21:30:19 jmc Exp $ */

/*
 * Copyright (c) 2020 Remi Locherer <remi@openbsd.org>
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
 * RFC 2332 NBMA Next Hop Resolution Protocol (NHRP)
 */

#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <net/ethertypes.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "addrtoname.h"
#include "afnum.h"
#include "interface.h"
#include "extract.h"

#define NHRP_VER_RFC2332		1

#define NHRP_PKG_RESOLUTION_REQUEST	1
#define NHRP_PKG_RESOLUTION_REPLY	2
#define NHRP_PKG_REGISTRATION_REQUEST	3
#define NHRP_PKG_REGISTRATION_REPLY	4
#define NHRP_PKG_PURGE_REQUEST		5
#define NHRP_PKG_PURGE_REPLY		6
#define NHRP_PKG_ERROR_INDICATION	7


struct nhrp_header {
	/* fixed header part */
	u_int16_t	afn;		/* link layer address */
	u_int16_t	pro_type;	/* protocol type (short form) */
	u_int8_t	pro_snap[5];	/* protocol type (long form) */
	u_int8_t	hopcnt;		/* hop count */
	u_int16_t	pktsz;		/* length of the NHRP packet (octets) */
	u_int16_t	chksum;		/* IP checksum over the entier packet */
	u_int16_t	extoff;		/* extension offset */
	u_int8_t	op_version;	/* version of address mapping and
					   management protocol */
	u_int8_t	op_type;	/* NHRP packet type */
	u_int8_t	shtl;		/* type and length of src NBMA addr */
	u_int8_t	sstl;		/* type and length of src NBMA
					   subaddress */
	/* mandatory header part */
	u_int8_t	spl;		/* src proto len */
	u_int8_t	dpl;		/* dst proto len */
	u_int16_t	flags;		/* flags */
        union {
		u_int32_t	id;	/* request id */
		struct {		/* error code */
			u_int16_t	code;
			u_int16_t	offset;
		} err;
	} u;
};

struct nhrp_cie {
	/* client information entrie */
	u_int8_t	code;
	u_int8_t	plen;
	u_int16_t	unused;
	u_int16_t	mtu;
	u_int16_t	htime;
	u_int8_t	cli_addr_tl;
	u_int8_t	cli_saddr_tl;
	u_int8_t	cli_proto_tl;
	u_int8_t	pref;
};

static const u_char *	nhrp_print_cie(const u_char *, u_int16_t, u_int16_t);


void
nhrp_print(const u_char *p, u_int length)
{
	struct nhrp_header	*hdr;
	const u_char		*nhrpext, *nhrpend;

	printf("NHRP: ");

	if ((snapend - p) < sizeof(*hdr))
		goto trunc;

	hdr = (struct nhrp_header *)p;

	if (hdr->op_version != NHRP_VER_RFC2332) {
		printf("unknown-version-%02x", hdr->op_version);
		return;

	}

	nhrpext = p + EXTRACT_16BITS(&hdr->extoff);
	nhrpend = p + EXTRACT_16BITS(&hdr->pktsz);

	switch (hdr->op_type) {
	case NHRP_PKG_RESOLUTION_REQUEST:
		printf("res request, ");
		break;
	case NHRP_PKG_RESOLUTION_REPLY:
		printf("res reply, ");
		break;
	case NHRP_PKG_REGISTRATION_REQUEST:
		printf("reg request, ");
		break;
	case NHRP_PKG_REGISTRATION_REPLY:
		printf("reg reply, ");
		break;
	case NHRP_PKG_PURGE_REQUEST:
		printf("purge request, ");
		break;
	case NHRP_PKG_PURGE_REPLY:
		printf("purge reply, ");
		break;
	case NHRP_PKG_ERROR_INDICATION:
		printf("error %u", hdr->u.err.code);
		return;
	default:
		printf("unknown-op-type-%04x, ", hdr->op_type);
		break;
	}

	printf("id %u", EXTRACT_32BITS(&hdr->u.id));

	if (vflag) {
		printf(", hopcnt %u", hdr->hopcnt);

		/* most significant bit must be 0 */
		if (hdr->shtl & 0x80)
			printf(" (shtl bit 7 set)");

		/* check 2nd most significant bit */
		if (hdr->shtl & 0x40)
			printf(" (nbma E.154)");
	}

	p += sizeof(*hdr);
	if ((snapend - p) < hdr->shtl)
		goto trunc;

	if (hdr->shtl) {
		switch (EXTRACT_16BITS(&hdr->afn)) {
		case AFNUM_INET:
			printf(", src nbma %s", ipaddr_string(p));
			break;
		case AFNUM_INET6:
			printf(", src nbma %s", ip6addr_string(p));
			break;
		case AFNUM_802:
			printf(", src nbma %s", etheraddr_string(p));
			break;
		default:
			printf(", unknown-nbma-addr-family-%04x",
			    EXTRACT_16BITS(&hdr->afn));
			break;
		}
	}

	p += hdr->shtl;
	if ((snapend - p) < (hdr->spl + hdr->dpl))
		goto trunc;

	switch (EXTRACT_16BITS(&hdr->pro_type)) {
	case ETHERTYPE_IP:
		printf(", %s -> %s",
		    ipaddr_string(p),
		    ipaddr_string(p + hdr->spl));
		break;
	case ETHERTYPE_IPV6:
		printf(", %s -> %s",
		    ip6addr_string(p),
		    ip6addr_string(p + hdr->spl));
		break;
	default:
		printf(", proto type %04x",
		    EXTRACT_16BITS(&hdr->pro_type));
		break;
	}

	p += hdr->spl + hdr->dpl;

	do {
		p = nhrp_print_cie(p, hdr->afn, hdr->pro_type);
		if (p == 0)
			goto trunc;
	} while ((hdr->extoff && (p < nhrpext)) ||
		    ((!hdr->extoff && (p < nhrpend))));

	return;

trunc:
	printf(" [|nhrp]");

}

static const u_char *
nhrp_print_cie(const u_char *data, u_int16_t afn, u_int16_t pro_type)
{
	struct nhrp_cie		*cie;
	int			 family, type;

	if ((snapend - data) < sizeof(*cie))
		return (0);

	cie = (struct nhrp_cie *)data;

	family = EXTRACT_16BITS(&afn);
	type = EXTRACT_16BITS(&pro_type);

	printf(" (code %d", cie->code);
	if (vflag)
		printf(", pl %d, mtu %d, htime %d, pref %d", cie->plen,
		    EXTRACT_16BITS(&cie->mtu), EXTRACT_16BITS(&cie->htime),
		    cie->pref);

	/* check 2nd most significant bit */
	if (cie->cli_addr_tl & 0x40)
		printf(", nbma E.154");

	data += sizeof(*cie);
	if ((snapend - data) < cie->cli_addr_tl)
		return (0);

	if (cie->cli_addr_tl) {
		switch (family) {
		case AFNUM_INET:
			printf(", nbma %s", ipaddr_string(data));
			break;
		case AFNUM_INET6:
			printf(", nbma %s", ip6addr_string(data));
			break;
		case AFNUM_802:
			printf(", nbma %s", etheraddr_string(data));
			break;
		default:
			printf(", unknown-nbma-addr-family-%04x", family);
			break;
		}
	}
	if (cie->cli_saddr_tl)
		printf(", unknown-nbma-saddr-family");

	data += cie->cli_addr_tl + cie->cli_saddr_tl;
	if ((snapend - data) < cie->cli_proto_tl)
		return (0);

	if (cie->cli_proto_tl) {
		switch (type) {
		case ETHERTYPE_IP:
			printf(", proto %s", ipaddr_string(data));
			break;
		case ETHERTYPE_IPV6:
			printf(", proto %s", ip6addr_string(data));
			break;
		default:
			printf(", unknown-proto-family-%04x", type);
			break;
		}
	}

	printf(")");

	return (data + cie->cli_proto_tl);
}

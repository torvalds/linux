/*	$OpenBSD: print-gre.c,v 1.35 2024/05/21 05:00:48 jsg Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 * tcpdump filter for GRE - Generic Routing Encapsulation
 * RFC1701 (GRE), RFC1702 (GRE IPv4), and RFC2637 (Enhanced GRE)
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

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"

#define	GRE_CP		0x8000		/* checksum present */
#define	GRE_RP		0x4000		/* routing present */
#define	GRE_KP		0x2000		/* key present */
#define	GRE_SP		0x1000		/* sequence# present */
#define	GRE_sP		0x0800		/* source routing */
#define	GRE_RECRS	0x0700		/* recursion count */
#define	GRE_AP		0x0080		/* acknowledgment# present */
#define	GRE_VERS	0x0007		/* protocol version */

/* source route entry types */
#define	GRESRE_IP	0x0800		/* IP */
#define	GRESRE_ASN	0xfffe		/* ASN */

#define NVGRE_VSID_MASK		0xffffff00U
#define NVGRE_VSID_SHIFT	8
#define NVGRE_FLOWID_MASK	0x000000ffU
#define NVGRE_FLOWID_SHIFT	0

#define GRE_WCCP	0x883e
#define ERSPAN_II	0x88be
#define ERSPAN_III	0x22eb

struct wccp_redirect {
	uint8_t		flags;
#define WCCP_D			(1 << 7)
#define WCCP_A			(1 << 6)
	uint8_t		ServiceId;
	uint8_t		AltBucket;
	uint8_t		PriBucket;
};

void gre_print_0(const u_char *, u_int);
void gre_print_1(const u_char *, u_int);
void gre_print_pptp(const u_char *, u_int, uint16_t);
void gre_print_eoip(const u_char *, u_int, uint16_t);
void gre_print_erspan(uint16_t, const u_char *, u_int);
void gre_sre_print(u_int16_t, u_int8_t, u_int8_t, const u_char *, u_int);
void gre_sre_ip_print(u_int8_t, u_int8_t, const u_char *, u_int);
void gre_sre_asn_print(u_int8_t, u_int8_t, const u_char *, u_int);

void
gre_print(const u_char *p, u_int length)
{
	uint16_t vers;
	int l;

	l = snapend - p;

	if (l < sizeof(vers)) {
		printf("[|gre]");
		return;
	}
	vers = EXTRACT_16BITS(p) & GRE_VERS;

	switch (vers) {
	case 0:
		gre_print_0(p, length);
		break;
	case 1:
		gre_print_1(p, length);
		break;
	default:
		printf("gre-unknown-version=%u", vers);
		break;
	}
}

void
gre_print_0(const u_char *p, u_int length)
{
	uint16_t flags, proto;
	u_int l;

	l = snapend - p;

	flags = EXTRACT_16BITS(p);
	p += sizeof(flags);
	l -= sizeof(flags);
	length -= sizeof(flags);

	printf("gre");

	if (vflag) {
		printf(" [%s%s%s%s%s]",
		    (flags & GRE_CP) ? "C" : "",
		    (flags & GRE_RP) ? "R" : "",
		    (flags & GRE_KP) ? "K" : "",
		    (flags & GRE_SP) ? "S" : "",
		    (flags & GRE_sP) ? "s" : "");
	}

	if (l < sizeof(proto))
		goto trunc;
	proto = EXTRACT_16BITS(p);
	p += sizeof(proto);
	l -= sizeof(proto);
	length -= sizeof(proto);

	if (vflag)
		printf(" %04x", proto);

	if ((flags & GRE_CP) | (flags & GRE_RP)) {
		if (l < 2)
			goto trunc;
		if ((flags & GRE_CP) && vflag)
			printf(" sum 0x%x", EXTRACT_16BITS(p));
		p += 2;
		l -= 2;
		length -= 2;

		if (l < 2)
			goto trunc;
		if (flags & GRE_RP)
			printf(" off 0x%x", EXTRACT_16BITS(p));
		p += 2;
		l -= 2;
		length -= 2;
	}

	if (flags & GRE_KP) {
		uint32_t key, vsid;

		if (l < sizeof(key))
			goto trunc;
		key = EXTRACT_32BITS(p);
		p += sizeof(key);
		l -= sizeof(key);
		length -= sizeof(key);

		/* maybe NVGRE, or key entropy? */
		vsid = (key & NVGRE_VSID_MASK) >> NVGRE_VSID_SHIFT;
		printf(" key=%u|%u+%02x", key, vsid,
		    (key & NVGRE_FLOWID_MASK) >> NVGRE_FLOWID_SHIFT);
	}

	if (flags & GRE_SP) {
		if (l < 4)
			goto trunc;
		printf(" seq %u", EXTRACT_32BITS(p));
		p += 4;
		l -= 4;
		length -= 4;
	}

	if (flags & GRE_RP) {
		for (;;) {
			u_int16_t af;
			u_int8_t sreoff;
			u_int8_t srelen;

			if (l < 4)
				goto trunc;
			af = EXTRACT_16BITS(p);
			sreoff = *(p + 2);
			srelen = *(p + 3);
			p += 4;
			l -= 4;
			length -= 4;

			if (af == 0 && srelen == 0)
				break;

			gre_sre_print(af, sreoff, srelen, p, l);

			if (l < srelen)
				goto trunc;
			p += srelen;
			l -= srelen;
			length -= srelen;
		}
	}

	printf(" ");

	switch (packettype) {
	case PT_ERSPAN:
		gre_print_erspan(flags, p, length);
		return;
	default:
		break;
	}

	switch (proto) {
	case 0:
		printf("keep-alive");
		break;
	case GRE_WCCP: {
		printf("wccp ");

		if (l == 0)
			return;

		if (*p >> 4 != 4) {
			struct wccp_redirect *wccp;

			if (l < sizeof(*wccp)) {
				printf("[|wccp]");
				return;
			}

			wccp = (struct wccp_redirect *)p;

			printf("D:%c A:%c SId:%u Alt:%u Pri:%u",
			    (wccp->flags & WCCP_D) ? '1' : '0',
			    (wccp->flags & WCCP_A) ? '1' : '0',
			    wccp->ServiceId, wccp->AltBucket, wccp->PriBucket);

			p += sizeof(*wccp);
			l -= sizeof(*wccp);

			printf(": ");
		}

		/* FALLTHROUGH */
	}
	case ETHERTYPE_IP:
		ip_print(p, length);
		break;
	case ETHERTYPE_IPV6:
		ip6_print(p, length);
		break;
	case ETHERTYPE_MPLS:
	case ETHERTYPE_MPLS_MCAST:
		mpls_print(p, length);
		break;
	case ETHERTYPE_TRANSETHER:
		ether_tryprint(p, length, 0);
		break;
#ifndef ETHERTYPE_NSH
#define ETHERTYPE_NSH 0x894f
#endif
	case ETHERTYPE_NSH:
		nsh_print(p, length);
		break;
	case ERSPAN_II:
		gre_print_erspan(flags, p, length);
		break;
	case 0x2000:
		cdp_print(p, length, l, 0);
		break;
#ifndef ETHERTYPE_NHRP
#define ETHERTYPE_NHRP 0x2001
#endif
	case ETHERTYPE_NHRP:
		nhrp_print(p, length);
		break;
	default:
		printf("unknown-proto-%04x", proto);
	}
	return;

trunc:
	printf("[|gre]");
}

void
gre_print_1(const u_char *p, u_int length)
{
	uint16_t flags, proto;
	int l;

	l = snapend - p;

	flags = EXTRACT_16BITS(p);
	p += sizeof(flags);
	l -= sizeof(flags);
	length -= sizeof(flags);

	if (l < sizeof(proto))
		goto trunc;

	proto = EXTRACT_16BITS(p);
	p += sizeof(proto);
	l -= sizeof(proto);
	length -= sizeof(proto);

	switch (proto) {
	case ETHERTYPE_PPP:
		gre_print_pptp(p, length, flags);
		break;
	case 0x6400:
		/* MikroTik RouterBoard Ethernet over IP (EoIP) */
		gre_print_eoip(p, length, flags);
		break;
	default:
		printf("unknown-gre1-proto-%04x", proto);
		break;
	}

	return;

trunc:
	printf("[|gre1]");
}

void
gre_print_pptp(const u_char *p, u_int length, uint16_t flags)
{
	uint16_t len;
	int l;

	l = snapend - p;

	printf("pptp");

	if (vflag) {
		printf(" [%s%s%s%s%s%s]",
		    (flags & GRE_CP) ? "C" : "",
		    (flags & GRE_RP) ? "R" : "",
		    (flags & GRE_KP) ? "K" : "",
		    (flags & GRE_SP) ? "S" : "",
		    (flags & GRE_sP) ? "s" : "",
		    (flags & GRE_AP) ? "A" : "");
	}

	if (flags & GRE_CP) {
		printf(" cpset!");
		return;
	}
	if (flags & GRE_RP) {
		printf(" rpset!");
		return;
	}
	if ((flags & GRE_KP) == 0) {
		printf(" kpunset!");
		return;
	}
	if (flags & GRE_sP) {
		printf(" spset!");
		return;
	}

	/* GRE_KP */
	if (l < sizeof(len))
		goto trunc;
	len = EXTRACT_16BITS(p);
	p += sizeof(len);
	l -= sizeof(len);
	length -= sizeof(len);

	if (vflag)
		printf(" len %u", EXTRACT_16BITS(p));

	if (l < 2)
		goto trunc;
	printf(" callid %u", EXTRACT_16BITS(p));
	p += 2;
	l -= 2;
	length -= 2;

	if (flags & GRE_SP) {
		if (l < 4)
			goto trunc;
		printf(" seq %u", EXTRACT_32BITS(p));
		p += 4;
		l -= 4;
		length -= 4;
	}

	if (flags & GRE_AP) {
		if (l < 4)
			goto trunc;
		printf(" ack %u", EXTRACT_32BITS(p));
		p += 4;
		l -= 4;
		length -= 4;
	}

	if ((flags & GRE_SP) == 0)
		return;

        if (length < len) {
		printf(" truncated-pptp - %d bytes missing!",
		    len - length);
		len = length;
	}

	printf(": ");

	ppp_hdlc_print(p, len);
	return;

trunc:
	printf("[|pptp]");
}

void
gre_print_eoip(const u_char *p, u_int length, uint16_t flags)
{
	uint16_t len, id;
	int l;

	l = snapend - p;

	printf("eoip");

	flags &= ~GRE_VERS;
	if (flags != GRE_KP) {
		printf(" unknown-eoip-flags-%04x!", flags);
		return;
	}

	if (l < sizeof(len))
		goto trunc;

	len = EXTRACT_16BITS(p);
	p += sizeof(len);
	l -= sizeof(len);
	length -= sizeof(len);

	if (l < sizeof(id))
		goto trunc;

	id = EXTRACT_LE_16BITS(p);
	p += sizeof(id);
	l -= sizeof(id);
	length -= sizeof(id);

	if (vflag)
		printf(" len=%u tunnel-id=%u", len, id);
	else
		printf(" %u", id);

        if (length < len) {
		printf(" truncated-eoip - %d bytes missing!",
		    len - length);
		len = length;
	}

	printf(": ");

	if (len == 0)
		printf("keepalive");
	else
		ether_tryprint(p, len, 0);

	return;

trunc:
	printf("[|eoip]");
}

#define ERSPAN2_VER_SHIFT	28
#define ERSPAN2_VER_MASK	(0xfU << ERSPAN2_VER_SHIFT)
#define ERSPAN2_VER		(0x1U << ERSPAN2_VER_SHIFT)
#define ERSPAN2_VLAN_SHIFT	16
#define ERSPAN2_VLAN_MASK	(0xfffU << ERSPAN2_VLAN_SHIFT)
#define ERSPAN2_COS_SHIFT	13
#define ERSPAN2_COS_MASK	(0x7U << ERSPAN2_COS_SHIFT)
#define ERSPAN2_EN_SHIFT	11
#define ERSPAN2_EN_MASK		(0x3U << ERSPAN2_EN_SHIFT)
#define ERSPAN2_EN_NONE		(0x0U << ERSPAN2_EN_SHIFT)
#define ERSPAN2_EN_ISL		(0x1U << ERSPAN2_EN_SHIFT)
#define ERSPAN2_EN_DOT1Q	(0x2U << ERSPAN2_EN_SHIFT)
#define ERSPAN2_EN_VLAN		(0x3U << ERSPAN2_EN_SHIFT)
#define ERSPAN2_T_SHIFT		10
#define ERSPAN2_T_MASK		(0x1U << ERSPAN2_T_SHIFT)
#define ERSPAN2_SID_SHIFT	0
#define ERSPAN2_SID_MASK	(0x3ffU << ERSPAN2_SID_SHIFT)

#define ERSPAN2_INDEX_SHIFT	0
#define ERSPAN2_INDEX_MASK	(0xfffffU << ERSPAN2_INDEX_SHIFT)

void
gre_print_erspan(uint16_t flags, const u_char *bp, u_int len)
{
	uint32_t hdr, ver, vlan, cos, en, sid, index;
	u_int l;

	printf("erspan");

	if (!(flags & GRE_SP)) {
		printf(" I: ");
		ether_tryprint(bp, len, 0);
		return;
	}

	l = snapend - bp;
	if (l < sizeof(hdr))
		goto trunc;

	hdr = EXTRACT_32BITS(bp);
	bp += sizeof(hdr);
	l -= sizeof(hdr);
	len -= sizeof(hdr);

	ver = hdr & ERSPAN2_VER_MASK;
	if (ver != ERSPAN2_VER) {
		ver >>= ERSPAN2_VER_SHIFT;
		printf(" erspan-unknown-version-%x", ver);
		return;
	}

	if (vflag)
		printf(" II");

	sid = (hdr & ERSPAN2_SID_MASK) >> ERSPAN2_SID_SHIFT;
	printf(" session %u", sid);

	en = hdr & ERSPAN2_EN_MASK;
	vlan = (hdr & ERSPAN2_VLAN_MASK) >> ERSPAN2_VLAN_SHIFT;
	switch (en) {
	case ERSPAN2_EN_NONE:
		break;
	case ERSPAN2_EN_ISL:
		printf(" isl %u", vlan);
		break;
	case ERSPAN2_EN_DOT1Q:
		printf(" vlan %u", vlan);
		break;
	case ERSPAN2_EN_VLAN:
		printf(" vlan payload");
		break;
	}

	if (vflag) {
		cos = (hdr & ERSPAN2_COS_MASK) >> ERSPAN2_COS_SHIFT;
		printf(" cos %u", cos);

		if (hdr & ERSPAN2_T_MASK)
			printf(" truncated");
	}

	if (l < sizeof(hdr))
		goto trunc;

	hdr = EXTRACT_32BITS(bp);
	bp += sizeof(hdr);
	l -= sizeof(hdr);
	len -= sizeof(hdr);

	if (vflag) {
		index = (hdr & ERSPAN2_INDEX_MASK) >> ERSPAN2_INDEX_SHIFT;
		printf(" index %u", index);
	}

	printf(": ");
	ether_tryprint(bp, len, 0);
	return;

trunc:
	printf(" [|erspan]");
}

void
gre_sre_print(u_int16_t af, u_int8_t sreoff, u_int8_t srelen,
    const u_char *bp, u_int len)
{
	switch (af) {
	case GRESRE_IP:
		printf(" (rtaf=ip");
		gre_sre_ip_print(sreoff, srelen, bp, len);
		printf(")");
		break;
	case GRESRE_ASN:
		printf(" (rtaf=asn");
		gre_sre_asn_print(sreoff, srelen, bp, len);
		printf(")");
		break;
	default:
		printf(" (rtaf=0x%x)", af);
	}
}
void
gre_sre_ip_print(u_int8_t sreoff, u_int8_t srelen, const u_char *bp, u_int len)
{
	struct in_addr a;
	const u_char *up = bp;

	if (sreoff & 3) {
		printf(" badoffset=%u", sreoff);
		return;
	}
	if (srelen & 3) {
		printf(" badlength=%u", srelen);
		return;
	}
	if (sreoff >= srelen) {
		printf(" badoff/len=%u/%u", sreoff, srelen);
		return;
	}

	for (;;) {
		if (len < 4 || srelen == 0)
			return;

		memcpy(&a, bp, sizeof(a));
		printf(" %s%s",
		    ((bp - up) == sreoff) ? "*" : "",
		    inet_ntoa(a));

		bp += 4;
		len -= 4;
		srelen -= 4;
	}
}

void
gre_sre_asn_print(u_int8_t sreoff, u_int8_t srelen, const u_char *bp, u_int len)
{
	const u_char *up = bp;

	if (sreoff & 1) {
		printf(" badoffset=%u", sreoff);
		return;
	}
	if (srelen & 1) {
		printf(" badlength=%u", srelen);
		return;
	}
	if (sreoff >= srelen) {
		printf(" badoff/len=%u/%u", sreoff, srelen);
		return;
	}

	for (;;) {
		if (len < 2 || srelen == 0)
			return;

		printf(" %s%x",
		    ((bp - up) == sreoff) ? "*" : "",
		    EXTRACT_16BITS(bp));

		bp += 2;
		len -= 2;
		srelen -= 2;
	}
}

/*
 * - RFC 7348 Virtual eXtensible Local Area Network (VXLAN)
 * - draft-ietf-nvo3-vxlan-gpe-08 Generic Protocol Extension for VXLAN
 */

struct vxlan_header {
	uint16_t	flags;
#define VXLAN_VER		0x3000	/* GPE */
#define VXLAN_VER_0		0x0000
#define VXLAN_I			0x0800	/* Instance Bit */
#define VXLAN_P			0x0400	/* GPE Next Protocol */
#define VXLAN_B			0x0200	/* GPE BUM Traffic */
#define VXLAN_O			0x0100	/* GPE OAM Flag */
	uint8_t		reserved;
	uint8_t		next_proto; 	/* GPE */
#define VXLAN_PROTO_RESERVED	0x00
#define VXLAN_PROTO_IPV4	0x01
#define VXLAN_PROTO_IPV6	0x02
#define VXLAN_PROTO_ETHERNET	0x03
#define VXLAN_PROTO_NSH		0x04
#define VXLAN_PROTO_MPLS	0x05
#define VXLAN_PROTO_VBNG	0x07
#define VXLAN_PROTO_GBP		0x80
#define VXLAN_PROTO_IOAM	0x82
	uint32_t	vni;
#define VXLAN_VNI_SHIFT		8
#define VXLAN_VNI_MASK		(0xffffffU << VXLAN_VNI_SHIFT)
#define VXLAN_VNI_RESERVED	(~VXLAN_VNI_MASK)
};

void
vxlan_print(const u_char *p, u_int length)
{
	const struct vxlan_header *vh;
	uint16_t flags, ver;
	uint8_t proto = VXLAN_PROTO_ETHERNET;
	int l = snapend - p;

	printf("VXLAN");

	if (l < sizeof(*vh))
		goto trunc;
	if (length < sizeof(*vh)) {
		printf(" ip truncated");
		return;
	}

	vh = (const struct vxlan_header *)p;

	p += sizeof(*vh);
	length -= sizeof(*vh);

	flags = ntohs(vh->flags);
	ver = flags & VXLAN_VER;
	if (ver != VXLAN_VER_0) {
		printf(" unknown version %u", ver >> 12);
		return;
	}

	if (flags & VXLAN_I) {
		uint32_t vni = (htonl(vh->vni) & VXLAN_VNI_MASK) >>
		    VXLAN_VNI_SHIFT;
		printf(" vni %u", vni);
	}

	if (flags & VXLAN_P)
		proto = vh->next_proto;

	if (flags & VXLAN_B)
		printf(" BUM");

	if (flags & VXLAN_O) {
		printf(" OAM (proto 0x%x, len %u)", proto, length);
		return;
	}

	printf(": ");

	switch (proto) {
	case VXLAN_PROTO_RESERVED:
		printf("Reserved");
		break;
	case VXLAN_PROTO_IPV4:
		ip_print(p, length);
		break;
	case VXLAN_PROTO_IPV6:
		ip6_print(p, length);
		break;
	case VXLAN_PROTO_ETHERNET:
		ether_tryprint(p, length, 0);
		break;
	case VXLAN_PROTO_NSH:
		nsh_print(p, length);
		break;
	case VXLAN_PROTO_MPLS:
		mpls_print(p, length);
		break;

	default:
		printf("Unassigned proto 0x%x", proto);
		break;
	}

	return;
trunc:
	printf(" [|vxlan]");
}

/*
 * Geneve: Generic Network Virtualization Encapsulation
 * draft-ietf-nvo3-geneve-16
 */

struct geneve_header {
	uint16_t	flags;
#define GENEVE_VER_SHIFT	14
#define GENEVE_VER_MASK		(0x3U << GENEVE_VER_SHIFT)
#define GENEVE_VER_0		(0x0U << GENEVE_VER_SHIFT)
#define GENEVE_OPT_LEN_SHIFT	8
#define GENEVE_OPT_LEN_MASK	(0x3fU << GENEVE_OPT_LEN_SHIFT)
#define GENEVE_OPT_LEN_UNITS	4
#define GENEVE_O		0x0080	/* Control packet */
#define GENEVE_C		0x0040	/* Critical options present */
	uint16_t		protocol;
	uint32_t	vni;
#define GENEVE_VNI_SHIFT	8
#define GENEVE_VNI_MASK		(0xffffffU << GENEVE_VNI_SHIFT)
#define GENEVE_VNI_RESERVED	(~GENEVE_VNI_MASK)
};

struct geneve_option {
	uint16_t	class;
	uint8_t		type;
	uint8_t		flags;
#define GENEVE_OPTION_LENGTH_SHIFT	0
#define GENEVE_OPTION_LENGTH_MASK	(0x1fU << GENEVE_OPTION_LENGTH_SHIFT)
};

static void
geneve_options_print(const u_char *p, u_int l)
{
	if (l == 0)
		return;

	do {
		struct geneve_option *go;
		unsigned int len, i;

		if (l < sizeof(*go))
			goto trunc;

		go = (struct geneve_option *)p;
		p += sizeof(*go);
		l -= sizeof(*go);

		printf("\n\toption class %u type %u", ntohs(go->class),
		    go->type);

		len = (go->flags & GENEVE_OPTION_LENGTH_MASK) >>
		    GENEVE_OPTION_LENGTH_SHIFT;
		if (len > 0) {
			printf(":");
			for (i = 0; i < len; i++) {
				uint32_t w;

				if (l < sizeof(w))
					goto trunc;

				w = EXTRACT_32BITS(p);
				p += sizeof(w);
				l -= sizeof(w);

				printf(" %08x", w);
			}
		}
	} while (l > 0);

	return;
trunc:
	printf("[|geneve option]");
}

void
geneve_print(const u_char *p, u_int length)
{
	const struct geneve_header *gh;
	uint16_t flags, ver, optlen, proto;
	uint32_t vni;
	int l = snapend - p;

	printf("geneve");

	if (l < sizeof(*gh))
		goto trunc;
	if (length < sizeof(*gh)) {
		printf(" ip truncated");
		return;
	}

	gh = (const struct geneve_header *)p;

	p += sizeof(*gh);
	l -= sizeof(*gh);
	length -= sizeof(*gh);

	flags = ntohs(gh->flags);
	ver = flags & GENEVE_VER_MASK;
	if (ver != GENEVE_VER_0) {
		printf(" unknown version %u", ver >> GENEVE_VER_SHIFT);
		return;
	}

	vni = (htonl(gh->vni) & GENEVE_VNI_MASK) >> GENEVE_VNI_SHIFT;
	printf(" vni %u", vni);

	if (flags & GENEVE_O)
		printf(" Control");

	if (flags & GENEVE_C)
		printf(" Critical");

	optlen = (flags & GENEVE_OPT_LEN_MASK) >> GENEVE_OPT_LEN_SHIFT;
	optlen *= GENEVE_OPT_LEN_UNITS;

	if (l < optlen)
		goto trunc;
	if (length < optlen) {
		printf(" ip truncated");
		return;
	}

	if (optlen > 0)
		geneve_options_print(p, optlen);

	p += optlen;
	length -= optlen;

	printf("\n    ");

	proto = ntohs(gh->protocol);
	switch (proto) {
	case ETHERTYPE_IP:
		ip_print(p, length);
		break;
	case ETHERTYPE_IPV6:
		ip6_print(p, length);
		break;
	case ETHERTYPE_MPLS:
	case ETHERTYPE_MPLS_MCAST:
		mpls_print(p, length);
		break;
	case ETHERTYPE_TRANSETHER:
		ether_tryprint(p, length, 0);
		break;

	default:
		printf("geneve-protocol-0x%x", proto);
		break;
	}

	return;
trunc:
	printf(" [|geneve]");
}

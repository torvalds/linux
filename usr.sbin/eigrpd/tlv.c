/*	$OpenBSD: tlv.c,v 1.17 2023/06/26 14:07:19 claudio Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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

#include <sys/types.h>
#include <sys/utsname.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "log.h"

int
gen_parameter_tlv(struct ibuf *buf, struct eigrp_iface *ei, int peerterm)
{
	struct tlv_parameter	 tp;

	tp.type = htons(TLV_TYPE_PARAMETER);
	tp.length = htons(TLV_TYPE_PARAMETER_LEN);
	if (peerterm) {
		tp.kvalues[0] = 255;
		tp.kvalues[1] = 255;
		tp.kvalues[2] = 255;
		tp.kvalues[3] = 255;
		tp.kvalues[4] = 255;
		tp.kvalues[5] = 0;
	} else
		memcpy(tp.kvalues, ei->eigrp->kvalues, 6);
	tp.holdtime = htons(ei->hello_holdtime);

	return (ibuf_add(buf, &tp, sizeof(tp)));
}

int
gen_sequence_tlv(struct ibuf *buf, struct seq_addr_head *seq_addr_list)
{
	struct tlv		 tlv;
	struct seq_addr_entry	*sa;
	uint8_t			 alen;
	uint16_t		 len = TLV_HDR_LEN;
	size_t			 off;

	tlv.type = htons(TLV_TYPE_SEQ);
	off = ibuf_size(buf) + offsetof(struct tlv, length);
	if (ibuf_add(buf, &tlv, sizeof(tlv))) {
		log_warn("%s: ibuf_add failed", __func__);
		return (-1);
	}

	TAILQ_FOREACH(sa, seq_addr_list, entry) {
		switch (sa->af) {
		case AF_INET:
			alen = sizeof (struct in_addr);
			break;
		case AF_INET6:
			alen = sizeof(struct in6_addr);
			break;
		default:
			fatalx("gen_sequence_tlv: unknown address family");
		}
		if (ibuf_add(buf, &alen, sizeof(alen)))
			return (-1);
		if (ibuf_add(buf, &sa->addr, alen)) {
			log_warn("%s: ibuf_add failed", __func__);
			return (-1);
		}

		len += (sizeof(alen) + alen);
	}

	/* adjust tlv length */
	if (ibuf_set_n16(buf, off, len) == -1)
                fatalx("gen_sequence_tlv: buf_set_n16 failed");

	return (0);
}

int
gen_sw_version_tlv(struct ibuf *buf)
{
	struct tlv_sw_version	 ts;
	struct utsname		 u;
	unsigned int		 vendor_os_major;
	unsigned int		 vendor_os_minor;

	memset(&ts, 0, sizeof(ts));
	ts.type = htons(TLV_TYPE_SW_VERSION);
	ts.length = htons(TLV_TYPE_SW_VERSION_LEN);
	if (uname(&u) >= 0) {
		if (sscanf(u.release, "%u.%u", &vendor_os_major,
		    &vendor_os_minor) == 2) {
			ts.vendor_os_major = (uint8_t) vendor_os_major;
			ts.vendor_os_minor = (uint8_t) vendor_os_minor;
		}
	}
	ts.eigrp_major = EIGRP_VERSION_MAJOR;
	ts.eigrp_minor = EIGRP_VERSION_MINOR;

	return (ibuf_add(buf, &ts, sizeof(ts)));
}

int
gen_mcast_seq_tlv(struct ibuf *buf, uint32_t seq)
{
	struct tlv_mcast_seq	 tm;

	tm.type = htons(TLV_TYPE_MCAST_SEQ);
	tm.length = htons(TLV_TYPE_MCAST_SEQ_LEN);
	tm.seq = htonl(seq);

	return (ibuf_add(buf, &tm, sizeof(tm)));
}

uint16_t
len_route_tlv(struct rinfo *ri)
{
	uint16_t		 len = TLV_HDR_LEN;

	switch (ri->af) {
	case AF_INET:
		len += sizeof(ri->nexthop.v4);
		len += PREFIX_SIZE4(ri->prefixlen);
		break;
	case AF_INET6:
		len += sizeof(ri->nexthop.v6);
		len += PREFIX_SIZE6(ri->prefixlen);
		break;
	default:
		break;
	}

	len += sizeof(ri->metric);
	if (ri->type == EIGRP_ROUTE_EXTERNAL)
		len += sizeof(ri->emetric);

	len += sizeof(ri->prefixlen);

	return (len);
}

int
gen_route_tlv(struct ibuf *buf, struct rinfo *ri)
{
	struct tlv		 tlv;
	struct in_addr		 addr;
	struct classic_metric	 metric;
	struct classic_emetric	 emetric;
	uint16_t		 tlvlen;
	uint8_t			 pflen;
	size_t			 off;

	switch (ri->af) {
	case AF_INET:
		tlv.type = TLV_PROTO_IPV4;
		break;
	case AF_INET6:
		tlv.type = TLV_PROTO_IPV6;
		break;
	default:
		fatalx("gen_route_tlv: unknown af");
	}

	switch (ri->type) {
	case EIGRP_ROUTE_INTERNAL:
		tlv.type |= TLV_ROUTE_INTERNAL;
		break;
	case EIGRP_ROUTE_EXTERNAL:
		tlv.type |= TLV_ROUTE_EXTERNAL;
		break;
	default:
		fatalx("gen_route_tlv: unknown type");
	}
	tlv.type = htons(tlv.type);

	off = ibuf_size(buf) + offsetof(struct tlv, length);
	if (ibuf_add(buf, &tlv, sizeof(tlv)))
		return (-1);
	tlvlen = TLV_HDR_LEN;

	/* nexthop */
	switch (ri->af) {
	case AF_INET:
		addr.s_addr = htonl(ri->nexthop.v4.s_addr);
		if (ibuf_add(buf, &addr, sizeof(addr)))
			return (-1);
		tlvlen += sizeof(ri->nexthop.v4);
		break;
	case AF_INET6:
		if (ibuf_add(buf, &ri->nexthop.v6, sizeof(ri->nexthop.v6)))
			return (-1);
		tlvlen += sizeof(ri->nexthop.v6);
		break;
	default:
		fatalx("gen_route_tlv: unknown af");
	}

	/* exterior metric */
	if (ri->type == EIGRP_ROUTE_EXTERNAL) {
		emetric = ri->emetric;
		emetric.routerid = htonl(emetric.routerid);
		emetric.as = htonl(emetric.as);
		emetric.tag = htonl(emetric.tag);
		emetric.metric = htonl(emetric.metric);
		emetric.reserved = htons(emetric.reserved);
		if (ibuf_add(buf, &emetric, sizeof(emetric)))
			return (-1);
		tlvlen += sizeof(emetric);
	}

	/* metric */
	metric = ri->metric;
	metric.delay = htonl(metric.delay);
	metric.bandwidth = htonl(metric.bandwidth);
	if (ibuf_add(buf, &metric, sizeof(metric)))
		return (-1);
	tlvlen += sizeof(metric);

	/* destination */
	if (ibuf_add(buf, &ri->prefixlen, sizeof(ri->prefixlen)))
		return (-1);
	switch (ri->af) {
	case AF_INET:
		pflen = PREFIX_SIZE4(ri->prefixlen);
		if (ibuf_add(buf, &ri->prefix.v4, pflen))
			return (-1);
		break;
	case AF_INET6:
		pflen = PREFIX_SIZE6(ri->prefixlen);
		if (ibuf_add(buf, &ri->prefix.v6, pflen))
			return (-1);
		break;
	default:
		fatalx("gen_route_tlv: unknown af");
	}
	tlvlen += sizeof(pflen) + pflen;

	/* adjust tlv length */
	if (ibuf_set_n16(buf, off, tlvlen) == -1)
                fatalx("gen_route_tlv: buf_set_n16 failed");

	return (0);
}

struct tlv_parameter *
tlv_decode_parameter(struct tlv *tlv, char *buf)
{
	struct tlv_parameter	*tp;

	if (ntohs(tlv->length) != TLV_TYPE_PARAMETER_LEN) {
		log_debug("%s: malformed tlv (bad length)", __func__);
		return (NULL);
	}
	tp = (struct tlv_parameter *)buf;
	return (tp);
}

int
tlv_decode_seq(int af, struct tlv *tlv, char *buf,
    struct seq_addr_head *seq_addr_list)
{
	uint16_t		 len;
	uint8_t			 alen;
	struct seq_addr_entry	*sa;

	len = ntohs(tlv->length);
	if (len < TLV_HDR_LEN) {
		log_debug("%s: malformed tlv (bad length)", __func__);
		return (-1);
	}
	buf += TLV_HDR_LEN;
	len -= TLV_HDR_LEN;

	while (len > 0) {
		memcpy(&alen, buf, sizeof(alen));
		buf += sizeof(alen);
		len -= sizeof(alen);
		if (alen > len) {
			log_debug("%s: malformed tlv (bad length)", __func__);
			return (-1);
		}

		switch (af) {
		case AF_INET:
			if (alen != sizeof (struct in_addr)) {
				log_debug("%s: invalid address length",
				    __func__);
				return (-1);
			}
			break;
		case AF_INET6:
			if (alen != sizeof (struct in6_addr)) {
				log_debug("%s: invalid address length",
				    __func__);
				return (-1);
			}
			break;
		default:
			fatalx("tlv_decode_seq: unknown af");
		}
		if ((sa = calloc(1, sizeof(*sa))) == NULL)
			fatal("tlv_decode_seq");
		sa->af = af;
		memcpy(&sa->addr, buf, alen);
		TAILQ_INSERT_TAIL(seq_addr_list, sa, entry);

		buf += alen;
		len -= alen;
	}

	return (0);
}

struct tlv_sw_version *
tlv_decode_sw_version(struct tlv *tlv, char *buf)
{
	struct tlv_sw_version	*tv;

	if (ntohs(tlv->length) != TLV_TYPE_SW_VERSION_LEN) {
		log_debug("%s: malformed tlv (bad length)", __func__);
		return (NULL);
	}
	tv = (struct tlv_sw_version *)buf;
	return (tv);
}

struct tlv_mcast_seq *
tlv_decode_mcast_seq(struct tlv *tlv, char *buf)
{
	struct tlv_mcast_seq	*tm;

	if (ntohs(tlv->length) != TLV_TYPE_MCAST_SEQ_LEN) {
		log_debug("%s: malformed tlv (bad length)", __func__);
		return (NULL);
	}
	tm = (struct tlv_mcast_seq *)buf;
	return (tm);
}

int
tlv_decode_route(int af, struct tlv *tlv, char *buf, struct rinfo *ri)
{
	unsigned int	 tlv_len, min_len, max_plen, plen, offset;

	ri->af = af;
	switch (ri->af) {
	case AF_INET:
		min_len = TLV_TYPE_IPV4_INT_MIN_LEN;
		max_plen = sizeof(ri->prefix.v4);
		break;
	case AF_INET6:
		min_len = TLV_TYPE_IPV6_INT_MIN_LEN;
		max_plen = sizeof(ri->prefix.v6);
		break;
	default:
		fatalx("tlv_decode_route: unknown af");
	}

	switch (ntohs(tlv->type) & TLV_TYPE_MASK) {
	case TLV_ROUTE_INTERNAL:
		ri->type = EIGRP_ROUTE_INTERNAL;
		break;
	case TLV_ROUTE_EXTERNAL:
		ri->type = EIGRP_ROUTE_EXTERNAL;
		min_len += sizeof(struct classic_emetric);
		break;
	default:
		fatalx("tlv_decode_route: unknown type");
	}

	tlv_len = ntohs(tlv->length);
	if (tlv_len < min_len) {
		log_debug("%s: malformed tlv (bad length)", __func__);
		return (-1);
	}

	/* nexthop */
	offset = TLV_HDR_LEN;
	switch (af) {
	case AF_INET:
		memcpy(&ri->nexthop.v4, buf + offset, sizeof(ri->nexthop.v4));
		offset += sizeof(ri->nexthop.v4);
		break;
	case AF_INET6:
		memcpy(&ri->nexthop.v6, buf + offset, sizeof(ri->nexthop.v6));
		offset += sizeof(ri->nexthop.v6);
		break;
	default:
		fatalx("tlv_decode_route: unknown af");
	}

	/* exterior metric */
	if (ri->type == EIGRP_ROUTE_EXTERNAL) {
		memcpy(&ri->emetric, buf + offset, sizeof(ri->emetric));
		ri->emetric.routerid = ntohl(ri->emetric.routerid);
		ri->emetric.as = ntohl(ri->emetric.as);
		ri->emetric.tag = ntohl(ri->emetric.tag);
		ri->emetric.metric = ntohl(ri->emetric.metric);
		ri->emetric.reserved = ntohs(ri->emetric.reserved);
		offset += sizeof(ri->emetric);
	}

	/* metric */
	memcpy(&ri->metric, buf + offset, sizeof(ri->metric));
	ri->metric.delay = ntohl(ri->metric.delay);
	ri->metric.bandwidth = ntohl(ri->metric.bandwidth);
	offset += sizeof(ri->metric);

	/* prefixlen */
	memcpy(&ri->prefixlen, buf + offset, sizeof(ri->prefixlen));
	offset += sizeof(ri->prefixlen);

	/*
	 * Different versions of IOS can use a different number of bytes to
	 * encode the same IPv6 prefix. This sucks but we have to deal with it.
	 * Instead of calculating the number of bytes based on the value of the
	 * prefixlen field, let's get this number by subtracting the size of all
	 * other fields from the total size of the TLV. It works because all
	 * the other fields have a fixed length.
	 */
	plen = tlv_len - min_len;

	/* safety check */
	if (plen > max_plen) {
		log_debug("%s: malformed tlv", __func__);
		return (-1);
	}

	/* destination */
	switch (af) {
	case AF_INET:
		memset(&ri->prefix.v4, 0, sizeof(ri->prefix.v4));
		memcpy(&ri->prefix.v4, buf + offset, plen);
		break;
	case AF_INET6:
		memset(&ri->prefix.v6, 0, sizeof(ri->prefix.v6));
		memcpy(&ri->prefix.v6, buf + offset, plen);
		break;
	default:
		fatalx("tlv_decode_route: unknown af");
	}

	/* check if the network is valid */
	if (bad_addr(af, &ri->prefix) ||
	   (af == AF_INET6 && IN6_IS_SCOPE_EMBED(&ri->prefix.v6))) {
		log_debug("%s: malformed tlv (invalid prefix): %s", __func__,
		    log_addr(af, &ri->prefix));
		return (-1);
	}

	/* just in case... */
	eigrp_applymask(af, &ri->prefix, &ri->prefix, ri->prefixlen);

	return (0);
}

void
metric_encode_mtu(uint8_t *dst, int mtu)
{
	dst[0] = (mtu & 0x00FF0000) >> 16;
	dst[1] = (mtu & 0x0000FF00) >> 8;
	dst[2] = (mtu & 0x000000FF);
}

int
metric_decode_mtu(uint8_t *mtu)
{
	return ((mtu[0] << 16) + (mtu[1] << 8) + mtu[2]);
}

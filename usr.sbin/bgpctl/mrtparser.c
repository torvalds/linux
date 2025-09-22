/*	$OpenBSD: mrtparser.c,v 1.22 2024/02/01 11:37:10 claudio Exp $ */
/*
 * Copyright (c) 2011 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mrt.h"
#include "mrtparser.h"

struct mrt_peer	*mrt_parse_v2_peer(struct mrt_hdr *, struct ibuf *);
struct mrt_rib	*mrt_parse_v2_rib(struct mrt_hdr *, struct ibuf *, int);
int	mrt_parse_dump(struct mrt_hdr *, struct ibuf *, struct mrt_peer **,
	    struct mrt_rib **);
int	mrt_parse_dump_mp(struct mrt_hdr *, struct ibuf *, struct mrt_peer **,
	    struct mrt_rib **, int);
int	mrt_extract_attr(struct mrt_rib_entry *, struct ibuf *, uint8_t, int);

void	mrt_free_peers(struct mrt_peer *);
void	mrt_free_rib(struct mrt_rib *);

u_char *mrt_aspath_inflate(struct ibuf *, uint16_t *);
int	mrt_extract_addr(struct ibuf *, struct bgpd_addr *, uint8_t);
int	mrt_extract_prefix(struct ibuf *, uint8_t, struct bgpd_addr *,
	    uint8_t *, int);

int	mrt_parse_state(struct mrt_bgp_state *, struct mrt_hdr *,
	    struct ibuf *, int);
int	mrt_parse_msg(struct mrt_bgp_msg *, struct mrt_hdr *,
	    struct ibuf *, int);

static size_t
mrt_read_buf(int fd, void *buf, size_t len)
{
	char *b = buf;
	ssize_t n;

	while (len > 0) {
		if ((n = read(fd, b, len)) == -1) {
			if (errno == EINTR)
				continue;
			err(1, "read");
		}
		if (n == 0)
			break;
		b += n;
		len -= n;
	}

	return (b - (char *)buf);
}

static struct ibuf *
mrt_read_msg(int fd, struct mrt_hdr *hdr)
{
	struct ibuf *buf;
	size_t len;

	memset(hdr, 0, sizeof(*hdr));
	if (mrt_read_buf(fd, hdr, sizeof(*hdr)) != sizeof(*hdr))
		return (NULL);

	len = ntohl(hdr->length);
	if ((buf = ibuf_open(len)) == NULL)
		err(1, "ibuf_open(%zu)", len);

	if (mrt_read_buf(fd, ibuf_reserve(buf, len), len) != len) {
		ibuf_free(buf);
		return (NULL);
	}
	return (buf);
}

void
mrt_parse(int fd, struct mrt_parser *p, int verbose)
{
	struct mrt_hdr		h;
	struct mrt_bgp_state	s;
	struct mrt_bgp_msg	m;
	struct mrt_peer		*pctx = NULL;
	struct mrt_rib		*r;
	struct ibuf		*msg;

	while ((msg = mrt_read_msg(fd, &h)) != NULL) {
		if (ibuf_size(msg) != ntohl(h.length))
			errx(1, "corrupt message, %zu vs %u", ibuf_size(msg),
			    ntohl(h.length));
		switch (ntohs(h.type)) {
		case MSG_NULL:
		case MSG_START:
		case MSG_DIE:
		case MSG_I_AM_DEAD:
		case MSG_PEER_DOWN:
		case MSG_PROTOCOL_BGP:
		case MSG_PROTOCOL_IDRP:
		case MSG_PROTOCOL_BGP4PLUS:
		case MSG_PROTOCOL_BGP4PLUS1:
			if (verbose)
				printf("deprecated MRT type %d\n",
				    ntohs(h.type));
			break;
		case MSG_PROTOCOL_RIP:
		case MSG_PROTOCOL_RIPNG:
		case MSG_PROTOCOL_OSPF:
		case MSG_PROTOCOL_ISIS_ET:
		case MSG_PROTOCOL_ISIS:
		case MSG_PROTOCOL_OSPFV3_ET:
		case MSG_PROTOCOL_OSPFV3:
			if (verbose)
				printf("unsupported MRT type %d\n",
				    ntohs(h.type));
			break;
		case MSG_TABLE_DUMP:
			switch (ntohs(h.subtype)) {
			case MRT_DUMP_AFI_IP:
			case MRT_DUMP_AFI_IPv6:
				if (p->dump == NULL)
					break;
				if (mrt_parse_dump(&h, msg, &pctx, &r) == 0) {
					if (p->dump)
						p->dump(r, pctx, p->arg);
					mrt_free_rib(r);
				}
				break;
			default:
				if (verbose)
					printf("unknown AFI %d in table dump\n",
					    ntohs(h.subtype));
				break;
			}
			break;
		case MSG_TABLE_DUMP_V2:
			switch (ntohs(h.subtype)) {
			case MRT_DUMP_V2_PEER_INDEX_TABLE:
				if (p->dump == NULL)
					break;
				if (pctx)
					mrt_free_peers(pctx);
				pctx = mrt_parse_v2_peer(&h, msg);
				break;
			case MRT_DUMP_V2_RIB_IPV4_UNICAST:
			case MRT_DUMP_V2_RIB_IPV4_MULTICAST:
			case MRT_DUMP_V2_RIB_IPV6_UNICAST:
			case MRT_DUMP_V2_RIB_IPV6_MULTICAST:
			case MRT_DUMP_V2_RIB_GENERIC:
			case MRT_DUMP_V2_RIB_IPV4_UNICAST_ADDPATH:
			case MRT_DUMP_V2_RIB_IPV4_MULTICAST_ADDPATH:
			case MRT_DUMP_V2_RIB_IPV6_UNICAST_ADDPATH:
			case MRT_DUMP_V2_RIB_IPV6_MULTICAST_ADDPATH:
			case MRT_DUMP_V2_RIB_GENERIC_ADDPATH:
				if (p->dump == NULL)
					break;
				r = mrt_parse_v2_rib(&h, msg, verbose);
				if (r) {
					if (p->dump)
						p->dump(r, pctx, p->arg);
					mrt_free_rib(r);
				}
				break;
			default:
				if (verbose)
					printf("unhandled DUMP_V2 subtype %d\n",
					    ntohs(h.subtype));
				break;
			}
			break;
		case MSG_PROTOCOL_BGP4MP_ET:
		case MSG_PROTOCOL_BGP4MP:
			switch (ntohs(h.subtype)) {
			case BGP4MP_STATE_CHANGE:
			case BGP4MP_STATE_CHANGE_AS4:
				if (mrt_parse_state(&s, &h, msg,
				    verbose) != -1) {
					if (p->state)
						p->state(&s, p->arg);
				}
				break;
			case BGP4MP_MESSAGE:
			case BGP4MP_MESSAGE_AS4:
			case BGP4MP_MESSAGE_LOCAL:
			case BGP4MP_MESSAGE_AS4_LOCAL:
			case BGP4MP_MESSAGE_ADDPATH:
			case BGP4MP_MESSAGE_AS4_ADDPATH:
			case BGP4MP_MESSAGE_LOCAL_ADDPATH:
			case BGP4MP_MESSAGE_AS4_LOCAL_ADDPATH:
				if (mrt_parse_msg(&m, &h, msg, verbose) != -1) {
					if (p->message)
						p->message(&m, p->arg);
				}
				break;
			case BGP4MP_ENTRY:
				if (p->dump == NULL)
					break;
				if (mrt_parse_dump_mp(&h, msg, &pctx, &r,
				    verbose) == 0) {
					if (p->dump)
						p->dump(r, pctx, p->arg);
					mrt_free_rib(r);
				}
				break;
			default:
				if (verbose)
					printf("unhandled BGP4MP subtype %d\n",
					    ntohs(h.subtype));
				break;
			}
			break;
		default:
			if (verbose)
				printf("unknown MRT type %d\n", ntohs(h.type));
			break;
		}
		ibuf_free(msg);
	}
	if (pctx)
		mrt_free_peers(pctx);
}

static int
mrt_afi2aid(int afi, int safi, int verbose)
{
	switch (afi) {
	case MRT_DUMP_AFI_IP:
		if (safi == -1 || safi == 1 || safi == 2)
			return AID_INET;
		else if (safi == 128)
			return AID_VPN_IPv4;
		break;
	case MRT_DUMP_AFI_IPv6:
		if (safi == -1 || safi == 1 || safi == 2)
			return AID_INET6;
		else if (safi == 128)
			return AID_VPN_IPv6;
		break;
	default:
		break;
	}
	if (verbose)
		printf("unhandled AFI/SAFI %d/%d\n", afi, safi);
	return AID_UNSPEC;
}

struct mrt_peer *
mrt_parse_v2_peer(struct mrt_hdr *hdr, struct ibuf *msg)
{
	struct mrt_peer_entry	*peers = NULL;
	struct mrt_peer	*p;
	uint32_t	bid;
	uint16_t	cnt, i;

	if (ibuf_size(msg) < 8)	/* min msg size */
		return NULL;

	p = calloc(1, sizeof(struct mrt_peer));
	if (p == NULL)
		err(1, "calloc");

	/* collector bgp id */
	if (ibuf_get_n32(msg, &bid) == -1 ||
	    ibuf_get_n16(msg, &cnt) == -1)
		goto fail;

	/* view name */
	if (cnt != 0) {
		if ((p->view = malloc(cnt + 1)) == NULL)
			err(1, "malloc");
		if (ibuf_get(msg, p->view, cnt) == -1)
			goto fail;
		p->view[cnt] = 0;
	} else
		if ((p->view = strdup("")) == NULL)
			err(1, "strdup");

	/* peer_count */
	if (ibuf_get_n16(msg, &cnt) == -1)
		goto fail;

	/* peer entries */
	if ((peers = calloc(cnt, sizeof(struct mrt_peer_entry))) == NULL)
		err(1, "calloc");
	for (i = 0; i < cnt; i++) {
		uint8_t type;

		if (ibuf_get_n8(msg, &type) == -1 ||
		    ibuf_get_n32(msg, &peers[i].bgp_id) == -1)
			goto fail;

		if (type & MRT_DUMP_V2_PEER_BIT_I) {
			if (mrt_extract_addr(msg, &peers[i].addr,
			    AID_INET6) == -1)
				goto fail;
		} else {
			if (mrt_extract_addr(msg, &peers[i].addr,
			    AID_INET) == -1)
				goto fail;
		}

		if (type & MRT_DUMP_V2_PEER_BIT_A) {
			if (ibuf_get_n32(msg, &peers[i].asnum) == -1)
				goto fail;
		} else {
			uint16_t as2;

			if (ibuf_get_n16(msg, &as2) == -1)
				goto fail;
			peers[i].asnum = as2;
		}
	}
	p->peers = peers;
	p->npeers = cnt;
	return (p);
fail:
	mrt_free_peers(p);
	free(peers);
	return (NULL);
}

struct mrt_rib *
mrt_parse_v2_rib(struct mrt_hdr *hdr, struct ibuf *msg, int verbose)
{
	struct mrt_rib_entry *entries = NULL;
	struct mrt_rib	*r;
	uint16_t	i, afi;
	uint8_t		safi, aid;

	r = calloc(1, sizeof(struct mrt_rib));
	if (r == NULL)
		err(1, "calloc");

	/* seq_num */
	if (ibuf_get_n32(msg, &r->seqnum) == -1)
		goto fail;

	switch (ntohs(hdr->subtype)) {
	case MRT_DUMP_V2_RIB_IPV4_UNICAST_ADDPATH:
	case MRT_DUMP_V2_RIB_IPV4_MULTICAST_ADDPATH:
		r->add_path = 1;
		/* FALLTHROUGH */
	case MRT_DUMP_V2_RIB_IPV4_UNICAST:
	case MRT_DUMP_V2_RIB_IPV4_MULTICAST:
		/* prefix */
		if (mrt_extract_prefix(msg, AID_INET, &r->prefix,
		    &r->prefixlen, verbose) == -1)
			goto fail;
		break;
	case MRT_DUMP_V2_RIB_IPV6_UNICAST_ADDPATH:
	case MRT_DUMP_V2_RIB_IPV6_MULTICAST_ADDPATH:
		r->add_path = 1;
		/* FALLTHROUGH */
	case MRT_DUMP_V2_RIB_IPV6_UNICAST:
	case MRT_DUMP_V2_RIB_IPV6_MULTICAST:
		/* prefix */
		if (mrt_extract_prefix(msg, AID_INET6, &r->prefix,
		    &r->prefixlen, verbose) == -1)
			goto fail;
		break;
	case MRT_DUMP_V2_RIB_GENERIC_ADDPATH:
		/*
		 * RFC8050 handling for add-path has special handling for
		 * RIB_GENERIC_ADDPATH but nobody implements it that way.
		 * So just use the same way as for the other _ADDPATH types.
		 */
		r->add_path = 1;
		/* FALLTHROUGH */
	case MRT_DUMP_V2_RIB_GENERIC:
		/* fetch AFI/SAFI pair */
		if (ibuf_get_n16(msg, &afi) == -1 ||
		    ibuf_get_n8(msg, &safi) == -1)
			goto fail;

		if ((aid = mrt_afi2aid(afi, safi, verbose)) == AID_UNSPEC)
			goto fail;

		/* prefix */
		if (mrt_extract_prefix(msg, aid, &r->prefix,
		    &r->prefixlen, verbose) == -1)
			goto fail;
		break;
	default:
		errx(1, "unknown subtype %hd", ntohs(hdr->subtype));
	}

	/* entries count */
	if (ibuf_get_n16(msg, &r->nentries) == -1)
		goto fail;

	/* entries */
	if ((entries = calloc(r->nentries, sizeof(struct mrt_rib_entry))) ==
	    NULL)
		err(1, "calloc");
	for (i = 0; i < r->nentries; i++) {
		struct ibuf	abuf;
		uint32_t	otm;
		uint16_t	alen;

		/* peer index */
		if (ibuf_get_n16(msg, &entries[i].peer_idx) == -1)
			goto fail;

		/* originated */
		if (ibuf_get_n32(msg, &otm) == -1)
			goto fail;
		entries[i].originated = otm;

		if (r->add_path) {
			if (ibuf_get_n32(msg, &entries[i].path_id) == -1)
				goto fail;
		}

		/* attr_len */
		if (ibuf_get_n16(msg, &alen) == -1 ||
		    ibuf_get_ibuf(msg, alen, &abuf) == -1)
			goto fail;

		/* attr */
		if (mrt_extract_attr(&entries[i], &abuf, r->prefix.aid,
		    1) == -1)
			goto fail;
	}
	r->entries = entries;
	return (r);
fail:
	mrt_free_rib(r);
	free(entries);
	return (NULL);
}

int
mrt_parse_dump(struct mrt_hdr *hdr, struct ibuf *msg, struct mrt_peer **pp,
    struct mrt_rib **rp)
{
	struct ibuf		 abuf;
	struct mrt_peer		*p;
	struct mrt_rib		*r;
	struct mrt_rib_entry	*re;
	uint32_t		 tmp32;
	uint16_t		 tmp16, alen;

	if (*pp == NULL) {
		*pp = calloc(1, sizeof(struct mrt_peer));
		if (*pp == NULL)
			err(1, "calloc");
		(*pp)->peers = calloc(1, sizeof(struct mrt_peer_entry));
		if ((*pp)->peers == NULL)
			err(1, "calloc");
		(*pp)->npeers = 1;
	}
	p = *pp;

	*rp = r = calloc(1, sizeof(struct mrt_rib));
	if (r == NULL)
		err(1, "calloc");
	re = calloc(1, sizeof(struct mrt_rib_entry));
	if (re == NULL)
		err(1, "calloc");
	r->nentries = 1;
	r->entries = re;

	if (ibuf_skip(msg, sizeof(uint16_t)) == -1 ||	/* view */
	    ibuf_get_n16(msg, &tmp16) == -1)		/* seqnum */
		goto fail;
	r->seqnum = tmp16;

	switch (ntohs(hdr->subtype)) {
	case MRT_DUMP_AFI_IP:
		if (mrt_extract_addr(msg, &r->prefix, AID_INET) == -1)
			goto fail;
		break;
	case MRT_DUMP_AFI_IPv6:
		if (mrt_extract_addr(msg, &r->prefix, AID_INET6) == -1)
			goto fail;
		break;
	}
	if (ibuf_get_n8(msg, &r->prefixlen) == -1 ||	/* prefixlen */
	    ibuf_skip(msg, 1) == -1 ||			/* status */
	    ibuf_get_n32(msg, &tmp32) == -1)		/* originated */
		goto fail;
	re->originated = tmp32;
	/* peer ip */
	switch (ntohs(hdr->subtype)) {
	case MRT_DUMP_AFI_IP:
		if (mrt_extract_addr(msg, &p->peers->addr, AID_INET) == -1)
			goto fail;
		break;
	case MRT_DUMP_AFI_IPv6:
		if (mrt_extract_addr(msg, &p->peers->addr, AID_INET6) == -1)
			goto fail;
		break;
	}
	if (ibuf_get_n16(msg, &tmp16) == -1)
		goto fail;
	p->peers->asnum = tmp16;

	if (ibuf_get_n16(msg, &alen) == -1 ||
	    ibuf_get_ibuf(msg, alen, &abuf) == -1)
		goto fail;

	/* attr */
	if (mrt_extract_attr(re, &abuf, r->prefix.aid, 0) == -1)
		goto fail;
	return (0);
fail:
	mrt_free_rib(r);
	return (-1);
}

int
mrt_parse_dump_mp(struct mrt_hdr *hdr, struct ibuf *msg, struct mrt_peer **pp,
    struct mrt_rib **rp, int verbose)
{
	struct ibuf		 abuf;
	struct mrt_peer		*p;
	struct mrt_rib		*r;
	struct mrt_rib_entry	*re;
	uint32_t		 tmp32;
	uint16_t		 asnum, alen, afi;
	uint8_t			 safi, nhlen, aid;

	if (*pp == NULL) {
		*pp = calloc(1, sizeof(struct mrt_peer));
		if (*pp == NULL)
			err(1, "calloc");
		(*pp)->peers = calloc(1, sizeof(struct mrt_peer_entry));
		if ((*pp)->peers == NULL)
			err(1, "calloc");
		(*pp)->npeers = 1;
	}
	p = *pp;

	*rp = r = calloc(1, sizeof(struct mrt_rib));
	if (r == NULL)
		err(1, "calloc");
	re = calloc(1, sizeof(struct mrt_rib_entry));
	if (re == NULL)
		err(1, "calloc");
	r->nentries = 1;
	r->entries = re;

	/* just ignore the microsec field for _ET header for now */
	if (ntohs(hdr->type) == MSG_PROTOCOL_BGP4MP_ET) {
		if (ibuf_skip(msg, sizeof(uint32_t)) == -1)
			goto fail;
	}

	if (ibuf_skip(msg, sizeof(uint16_t)) == -1 ||	/* source AS */
	    ibuf_get_n16(msg, &asnum) == -1 ||		/* dest AS */
	    ibuf_skip(msg, sizeof(uint16_t)) == -1 ||	/* iface index */
	    ibuf_get_n16(msg, &afi) == -1)
		goto fail;
	p->peers->asnum = asnum;

	/* source + dest ip */
	switch (afi) {
	case MRT_DUMP_AFI_IP:
		/* source IP */
		if (ibuf_skip(msg, sizeof(struct in_addr)) == -1)
			goto fail;
		/* dest IP */
		if (mrt_extract_addr(msg, &p->peers->addr, AID_INET) == -1)
			goto fail;
		break;
	case MRT_DUMP_AFI_IPv6:
		/* source IP */
		if (ibuf_skip(msg, sizeof(struct in6_addr)) == -1)
			goto fail;
		/* dest IP */
		if (mrt_extract_addr(msg, &p->peers->addr, AID_INET6) == -1)
			goto fail;
		break;
	}

	if (ibuf_skip(msg, sizeof(uint16_t)) == -1 ||	/* view */
	    ibuf_skip(msg, sizeof(uint16_t)) == -1 ||	/* status */
	    ibuf_get_n32(msg, &tmp32) == -1)		/* originated */
		goto fail;
	re->originated = tmp32;

	if (ibuf_get_n16(msg, &afi) == -1 ||		/* afi */
	    ibuf_get_n8(msg, &safi) == -1)		/* safi */
		goto fail;
	if ((aid = mrt_afi2aid(afi, safi, verbose)) == AID_UNSPEC)
		goto fail;

	if (ibuf_get_n8(msg, &nhlen) == -1)		/* nhlen */
		goto fail;

	/* nexthop */
	if (mrt_extract_addr(msg, &re->nexthop, aid) == -1)
		goto fail;

	/* prefix */
	if (mrt_extract_prefix(msg, aid, &r->prefix, &r->prefixlen,
	    verbose) == -1)
		goto fail;

	if (ibuf_get_n16(msg, &alen) == -1 ||
	    ibuf_get_ibuf(msg, alen, &abuf) == -1)
		goto fail;
	if (mrt_extract_attr(re, &abuf, r->prefix.aid, 0) == -1)
		goto fail;

	return (0);
fail:
	mrt_free_rib(r);
	return (-1);
}

int
mrt_extract_attr(struct mrt_rib_entry *re, struct ibuf *buf, uint8_t aid,
    int as4)
{
	struct ibuf	abuf;
	struct mrt_attr	*ap;
	size_t		alen, hlen;
	uint8_t		type, flags;

	do {
		ibuf_from_ibuf(&abuf, buf);
		if (ibuf_get_n8(&abuf, &flags) == -1 ||
		    ibuf_get_n8(&abuf, &type) == -1)
			return (-1);

		if (flags & MRT_ATTR_EXTLEN) {
			uint16_t tmp16;
			if (ibuf_get_n16(&abuf, &tmp16) == -1)
				return (-1);
			alen = tmp16;
			hlen = 4;
		} else {
			uint8_t tmp8;
			if (ibuf_get_n8(&abuf, &tmp8) == -1)
				return (-1);
			alen = tmp8;
			hlen = 3;
		}
		if (ibuf_truncate(&abuf, alen) == -1)
			return (-1);
		/* consume the attribute in buf before moving forward */
		if (ibuf_skip(buf, hlen + alen) == -1)
			return (-1);

		switch (type) {
		case MRT_ATTR_ORIGIN:
			if (alen != 1)
				return (-1);
			if (ibuf_get_n8(&abuf, &re->origin) == -1)
				return (-1);
			break;
		case MRT_ATTR_ASPATH:
			if (as4) {
				re->aspath_len = alen;
				if ((re->aspath = malloc(alen)) == NULL)
					err(1, "malloc");
				if (ibuf_get(&abuf, re->aspath, alen) == -1)
					return (-1);
			} else {
				re->aspath = mrt_aspath_inflate(&abuf,
				    &re->aspath_len);
				if (re->aspath == NULL)
					return (-1);
			}
			break;
		case MRT_ATTR_NEXTHOP:
			if (alen != 4)
				return (-1);
			if (aid != AID_INET)
				break;
			if (ibuf_get(&abuf, &re->nexthop.v4,
			    sizeof(re->nexthop.v4)) == -1)
				return (-1);
			re->nexthop.aid = AID_INET;
			break;
		case MRT_ATTR_MED:
			if (alen != 4)
				return (-1);
			if (ibuf_get_n32(&abuf, &re->med) == -1)
				return (-1);
			break;
		case MRT_ATTR_LOCALPREF:
			if (alen != 4)
				return (-1);
			if (ibuf_get_n32(&abuf, &re->local_pref) == -1)
				return (-1);
			break;
		case MRT_ATTR_MP_REACH_NLRI:
			/*
			 * XXX horrible hack:
			 * Once again IETF and the real world differ in the
			 * implementation. In short the abbreviated MP_NLRI
			 * hack in the standard is not used in real life.
			 * Detect the two cases by looking at the first byte
			 * of the payload (either the nexthop addr length (RFC)
			 * or the high byte of the AFI (old form)). If the
			 * first byte matches the expected nexthop length it
			 * is expected to be the RFC 6396 encoding.
			 *
			 * Checking for the hack skips over the nhlen.
			 */
			{
				uint8_t	hack;
				if (ibuf_get_n8(&abuf, &hack) == -1)
					return (-1);
				if (hack != alen - 1) {
					if (ibuf_skip(&abuf, 3) == -1)
						return (-1);
				}
			}
			switch (aid) {
			case AID_INET6:
				if (ibuf_get(&abuf, &re->nexthop.v6,
				    sizeof(re->nexthop.v6)) == -1)
					return (-1);
				re->nexthop.aid = aid;
				break;
			case AID_VPN_IPv4:
				if (ibuf_skip(&abuf, sizeof(uint64_t)) == -1 ||
				    ibuf_get(&abuf, &re->nexthop.v4,
				    sizeof(re->nexthop.v4)) == -1)
					return (-1);
				re->nexthop.aid = aid;
				break;
			case AID_VPN_IPv6:
				if (ibuf_skip(&abuf, sizeof(uint64_t)) == -1 ||
				    ibuf_get(&abuf, &re->nexthop.v6,
				    sizeof(re->nexthop.v6)) == -1)
					return (-1);
				re->nexthop.aid = aid;
				break;
			}
			break;
		case MRT_ATTR_AS4PATH:
			if (!as4) {
				free(re->aspath);
				re->aspath_len = alen;
				if ((re->aspath = malloc(alen)) == NULL)
					err(1, "malloc");
				if (ibuf_get(&abuf, re->aspath, alen) == -1)
					return (-1);
				break;
			}
			/* FALLTHROUGH */
		default:
			re->nattrs++;
			if (re->nattrs >= UCHAR_MAX)
				err(1, "too many attributes");
			ap = reallocarray(re->attrs,
			    re->nattrs, sizeof(struct mrt_attr));
			if (ap == NULL)
				err(1, "realloc");
			re->attrs = ap;
			ap = re->attrs + re->nattrs - 1;
			ibuf_rewind(&abuf);
			ap->attr_len = ibuf_size(&abuf);
			if ((ap->attr = malloc(ap->attr_len)) == NULL)
				err(1, "malloc");
			if (ibuf_get(&abuf, ap->attr, ap->attr_len) == -1)
				return (-1);
			break;
		}
	} while (ibuf_size(buf) > 0);

	return (0);
}

void
mrt_free_peers(struct mrt_peer *p)
{
	free(p->peers);
	free(p->view);
	free(p);
}

void
mrt_free_rib(struct mrt_rib *r)
{
	uint16_t	i, j;

	for (i = 0; i < r->nentries && r->entries; i++) {
		for (j = 0; j < r->entries[i].nattrs; j++)
			 free(r->entries[i].attrs[j].attr);
		free(r->entries[i].attrs);
		free(r->entries[i].aspath);
	}

	free(r->entries);
	free(r);
}

u_char *
mrt_aspath_inflate(struct ibuf *buf, uint16_t *newlen)
{
	struct ibuf *asbuf;
	u_char *data;
	size_t len;

	*newlen = 0;
	asbuf = aspath_inflate(buf);
	if (asbuf == NULL)
		return NULL;

	len = ibuf_size(asbuf);
	if ((data = malloc(len)) == NULL)
		err(1, "malloc");
	if (ibuf_get(asbuf, data, len) == -1) {
		ibuf_free(asbuf);
		return (NULL);
	}
	ibuf_free(asbuf);
	*newlen = len;
	return (data);
}

int
mrt_extract_addr(struct ibuf *msg, struct bgpd_addr *addr, uint8_t aid)
{
	memset(addr, 0, sizeof(*addr));
	switch (aid) {
	case AID_INET:
		if (ibuf_get(msg, &addr->v4, sizeof(addr->v4)) == -1)
			return (-1);
		break;
	case AID_INET6:
		if (ibuf_get(msg, &addr->v6, sizeof(addr->v6)) == -1)
			return (-1);
		break;
	case AID_VPN_IPv4:
		/* XXX labelstack and rd missing */
		if (ibuf_skip(msg, sizeof(uint64_t)) == -1 ||
		    ibuf_get(msg, &addr->v4, sizeof(addr->v4)) == -1)
			return (-1);
		break;
	case AID_VPN_IPv6:
		/* XXX labelstack and rd missing */
		if (ibuf_skip(msg, sizeof(uint64_t)) == -1 ||
		    ibuf_get(msg, &addr->v6, sizeof(addr->v6)) == -1)
			return (-1);
		break;
	default:
		return (-1);
	}
	addr->aid = aid;
	return 0;
}

int
mrt_extract_prefix(struct ibuf *msg, uint8_t aid, struct bgpd_addr *prefix,
    uint8_t *prefixlen, int verbose)
{
	int r;

	switch (aid) {
	case AID_INET:
		r = nlri_get_prefix(msg, prefix, prefixlen);
		break;
	case AID_INET6:
		r = nlri_get_prefix6(msg, prefix, prefixlen);
		break;
	case AID_VPN_IPv4:
		r = nlri_get_vpn4(msg, prefix, prefixlen, 0);
		break;
	case AID_VPN_IPv6:
		r = nlri_get_vpn6(msg, prefix, prefixlen, 0);
		break;
	default:
		if (verbose)
			printf("unknown prefix AID %d\n", aid);
		return -1;
	}
	if (r == -1 && verbose)
		printf("failed to parse prefix of AID %d\n", aid);
	return r;
}

int
mrt_parse_state(struct mrt_bgp_state *s, struct mrt_hdr *hdr, struct ibuf *msg,
    int verbose)
{
	struct timespec		 t;
	uint32_t		 sas, das, usec;
	uint16_t		 sas16, das16, afi;
	uint8_t			 aid;

	t.tv_sec = ntohl(hdr->timestamp);
	t.tv_nsec = 0;

	/* handle the microsec field for _ET header */
	if (ntohs(hdr->type) == MSG_PROTOCOL_BGP4MP_ET) {
		if (ibuf_get_n32(msg, &usec) == -1)
			return (-1);
		t.tv_nsec = usec * 1000;
	}

	switch (ntohs(hdr->subtype)) {
	case BGP4MP_STATE_CHANGE:
		if (ibuf_get_n16(msg, &sas16) == -1 ||	/* source as */
		    ibuf_get_n16(msg, &das16) == -1 ||	/* dest as */
		    ibuf_skip(msg, 2) == -1 ||		/* if_index */
		    ibuf_get_n16(msg, &afi) == -1)	/* afi */
			return (-1);
		sas = sas16;
		das = das16;
		break;
	case BGP4MP_STATE_CHANGE_AS4:
		if (ibuf_get_n32(msg, &sas) == -1 ||	/* source as */
		    ibuf_get_n32(msg, &das) == -1 ||	/* dest as */
		    ibuf_skip(msg, 2) == -1 ||		/* if_index */
		    ibuf_get_n16(msg, &afi) == -1)	/* afi */
			return (-1);
		break;
	default:
		errx(1, "mrt_parse_state: bad subtype");
	}

	/* src & dst addr */
	if ((aid = mrt_afi2aid(afi, -1, verbose)) == AID_UNSPEC)
		return (-1);

	memset(s, 0, sizeof(*s));
	s->time = t;
	s->src_as = sas;
	s->dst_as = das;

	if (mrt_extract_addr(msg, &s->src, aid) == -1)
		return (-1);
	if (mrt_extract_addr(msg, &s->dst, aid) == -1)
		return (-1);

	/* states */
	if (ibuf_get_n16(msg, &s->old_state) == -1 ||
	    ibuf_get_n16(msg, &s->new_state) == -1)
		return (-1);

	return (0);
}

int
mrt_parse_msg(struct mrt_bgp_msg *m, struct mrt_hdr *hdr, struct ibuf *msg,
    int verbose)
{
	struct timespec		 t;
	uint32_t		 sas, das, usec;
	uint16_t		 sas16, das16, afi;
	int			 addpath = 0;
	uint8_t			 aid;

	t.tv_sec = ntohl(hdr->timestamp);
	t.tv_nsec = 0;

	/* handle the microsec field for _ET header */
	if (ntohs(hdr->type) == MSG_PROTOCOL_BGP4MP_ET) {
		if (ibuf_get_n32(msg, &usec) == -1)
			return (-1);
		t.tv_nsec = usec * 1000;
	}

	switch (ntohs(hdr->subtype)) {
	case BGP4MP_MESSAGE_ADDPATH:
	case BGP4MP_MESSAGE_LOCAL_ADDPATH:
		addpath = 1;
		/* FALLTHROUGH */
	case BGP4MP_MESSAGE:
	case BGP4MP_MESSAGE_LOCAL:
		if (ibuf_get_n16(msg, &sas16) == -1 ||	/* source as */
		    ibuf_get_n16(msg, &das16) == -1 ||	/* dest as */
		    ibuf_skip(msg, 2) == -1 ||		/* if_index */
		    ibuf_get_n16(msg, &afi) == -1)	/* afi */
			return (-1);
		sas = sas16;
		das = das16;
		break;
	case BGP4MP_MESSAGE_AS4_ADDPATH:
	case BGP4MP_MESSAGE_AS4_LOCAL_ADDPATH:
		addpath = 1;
		/* FALLTHROUGH */
	case BGP4MP_MESSAGE_AS4:
	case BGP4MP_MESSAGE_AS4_LOCAL:
		if (ibuf_get_n32(msg, &sas) == -1 ||	/* source as */
		    ibuf_get_n32(msg, &das) == -1 ||	/* dest as */
		    ibuf_skip(msg, 2) == -1 ||		/* if_index */
		    ibuf_get_n16(msg, &afi) == -1)	/* afi */
			return (-1);
		break;
	default:
		errx(1, "mrt_parse_msg: bad subtype");
	}

	/* src & dst addr */
	if ((aid = mrt_afi2aid(afi, -1, verbose)) == AID_UNSPEC)
		return (-1);

	memset(m, 0, sizeof(*m));
	m->time = t;
	m->src_as = sas;
	m->dst_as = das;
	m->add_path = addpath;

	if (mrt_extract_addr(msg, &m->src, aid) == -1 ||
	    mrt_extract_addr(msg, &m->dst, aid) == -1)
		return (-1);

	/* msg */
	ibuf_from_ibuf(&m->msg, msg);
	return (0);
}

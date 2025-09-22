/*	$OpenBSD: rde_prefix.c,v 1.58 2025/02/27 14:03:32 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/queue.h>

#include <endian.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bgpd.h"
#include "rde.h"
#include "log.h"

/*
 * Prefix Table functions:
 * pt_add:    create new prefix and link it into the prefix table
 * pt_remove: Checks if there is no bgp prefix linked to the prefix,
 *            unlinks from the prefix table and frees the pt_entry.
 * pt_get:    get a prefix/prefixlen entry. While pt_lookup searches for the
 *            best matching prefix pt_get only finds the prefix/prefixlen
 *            entry. The speed of pt_get is important for the bgp updates.
 * pt_getaddr: convert the address into a struct bgpd_addr.
 * pt_lookup: lookup a IP in the prefix table. Mainly for "show ip bgp".
 * pt_empty:  returns true if there is no bgp prefix linked to the pt_entry.
 * pt_init:   initialize prefix table.
 * pt_alloc: allocate a AF specific pt_entry. Internal function.
 * pt_free:   free a pt_entry. Internal function.
 */

/* internal prototypes */
static struct pt_entry	*pt_alloc(struct pt_entry *, int len);
static void		 pt_free(struct pt_entry *);

struct pt_entry4 {
	RB_ENTRY(pt_entry)		pt_e;
	uint8_t				aid;
	uint8_t				prefixlen;
	uint16_t			len;
	uint32_t			refcnt;
	struct in_addr			prefix4;
};

struct pt_entry6 {
	RB_ENTRY(pt_entry)		pt_e;
	uint8_t				aid;
	uint8_t				prefixlen;
	uint16_t			len;
	uint32_t			refcnt;
	struct in6_addr			prefix6;
};

struct pt_entry_vpn4 {
	RB_ENTRY(pt_entry)		pt_e;
	uint8_t				aid;
	uint8_t				prefixlen;
	uint16_t			len;
	uint32_t			refcnt;
	uint64_t			rd;
	struct in_addr			prefix4;
	uint8_t				labelstack[21];
	uint8_t				labellen;
	uint8_t				pad1;
	uint8_t				pad2;
};

struct pt_entry_vpn6 {
	RB_ENTRY(pt_entry)		pt_e;
	uint8_t				aid;
	uint8_t				prefixlen;
	uint16_t			len;
	uint32_t			refcnt;
	uint64_t			rd;
	struct in6_addr			prefix6;
	uint8_t				labelstack[21];
	uint8_t				labellen;
	uint8_t				pad1;
	uint8_t				pad2;
};

struct pt_entry_evpn {
	RB_ENTRY(pt_entry)		pt_e;
	uint8_t				aid;
	uint8_t				prefixlen;
	uint16_t			len;
	uint32_t			refcnt;
	uint64_t			rd;
	uint32_t			ethtag;
	uint8_t				esi[ESI_ADDR_LEN];
	uint8_t				mac[ETHER_ADDR_LEN];
	uint8_t				labelstack[6];
	uint8_t				labellen;
	uint8_t				type;
	uint8_t				vpnaid;
	union {
		struct in_addr	prefix4;
		struct in6_addr	prefix6;
	};
};

struct pt_entry_flow {
	RB_ENTRY(pt_entry)		pt_e;
	uint8_t				aid;
	uint8_t				prefixlen;	/* unused ??? */
	uint16_t			len;
	uint32_t			refcnt;
	uint64_t			rd;
	uint8_t				flow[1];	/* NLRI */
};

#define PT_FLOW_SIZE		(offsetof(struct pt_entry_flow, flow))

RB_HEAD(pt_tree, pt_entry);
RB_PROTOTYPE(pt_tree, pt_entry, pt_e, pt_prefix_cmp);
RB_GENERATE(pt_tree, pt_entry, pt_e, pt_prefix_cmp);

struct pt_tree	pttable;

void
pt_init(void)
{
	RB_INIT(&pttable);
}

void
pt_shutdown(void)
{
	if (!RB_EMPTY(&pttable))
		log_debug("pt_shutdown: tree is not empty.");
}

void
pt_getaddr(struct pt_entry *pte, struct bgpd_addr *addr)
{
	struct pt_entry_flow	*pflow;
	struct pt_entry_evpn	*evpn;

	memset(addr, 0, sizeof(struct bgpd_addr));
	addr->aid = pte->aid;
	switch (addr->aid) {
	case AID_INET:
		addr->v4 = ((struct pt_entry4 *)pte)->prefix4;
		break;
	case AID_INET6:
		addr->v6 = ((struct pt_entry6 *)pte)->prefix6;
		/* XXX scope_id ??? */
		break;
	case AID_VPN_IPv4:
		addr->v4 = ((struct pt_entry_vpn4 *)pte)->prefix4;
		addr->rd = ((struct pt_entry_vpn4 *)pte)->rd;
		addr->labellen = ((struct pt_entry_vpn4 *)pte)->labellen;
		memcpy(addr->labelstack,
		    ((struct pt_entry_vpn4 *)pte)->labelstack,
		    addr->labellen);
		break;
	case AID_VPN_IPv6:
		addr->v6 = ((struct pt_entry_vpn6 *)pte)->prefix6;
		addr->rd = ((struct pt_entry_vpn6 *)pte)->rd;
		addr->labellen = ((struct pt_entry_vpn6 *)pte)->labellen;
		memcpy(addr->labelstack,
		    ((struct pt_entry_vpn6 *)pte)->labelstack,
		    addr->labellen);
		break;
	case AID_EVPN:
		evpn = (struct pt_entry_evpn *)pte;
		addr->evpn.type = evpn->type;
		addr->rd = evpn->rd;
		addr->evpn.ethtag = evpn->ethtag;
		addr->labellen = evpn->labellen;
		addr->evpn.aid = evpn->vpnaid;
		memcpy(addr->labelstack, evpn->labelstack, addr->labellen);
		memcpy(addr->evpn.esi, evpn->esi, sizeof(evpn->esi));
		memcpy(addr->evpn.mac, evpn->mac, sizeof(evpn->mac));
		switch (evpn->vpnaid) {
		case AID_INET:
			addr->evpn.v4 = evpn->prefix4;
			break;
		case AID_INET6:
			addr->evpn.v6 = evpn->prefix6;
			break;
		}
		break;
	case AID_FLOWSPECv4:
	case AID_FLOWSPECv6:
		pflow = (struct pt_entry_flow *)pte;
		flowspec_get_addr(pflow->flow, pflow->len - PT_FLOW_SIZE,
		    FLOWSPEC_TYPE_DEST, addr->aid == AID_FLOWSPECv6,
		    addr, &pflow->prefixlen, NULL);
		break;
	default:
		fatalx("pt_getaddr: unknown af");
	}
}

int
pt_getflowspec(struct pt_entry *pte, uint8_t **flow)
{
	struct pt_entry_flow	*pflow;

	switch (pte->aid) {
	case AID_FLOWSPECv4:
	case AID_FLOWSPECv6:
		pflow = (struct pt_entry_flow *)pte;
		*flow = pflow->flow;
		return pflow->len - PT_FLOW_SIZE;
	default:
		fatalx("pt_getflowspec: unknown af");
	}
}

struct pt_entry *
pt_fill(struct bgpd_addr *prefix, int prefixlen)
{
	static struct pt_entry4		pte4;
	static struct pt_entry6		pte6;
	static struct pt_entry_vpn4	pte_vpn4;
	static struct pt_entry_vpn6	pte_vpn6;
	static struct pt_entry_evpn	pte_evpn;

	switch (prefix->aid) {
	case AID_INET:
		memset(&pte4, 0, sizeof(pte4));
		pte4.len = sizeof(pte4);
		pte4.refcnt = UINT32_MAX;
		pte4.aid = prefix->aid;
		if (prefixlen > 32)
			fatalx("pt_fill: bad IPv4 prefixlen");
		inet4applymask(&pte4.prefix4, &prefix->v4, prefixlen);
		pte4.prefixlen = prefixlen;
		return ((struct pt_entry *)&pte4);
	case AID_INET6:
		memset(&pte6, 0, sizeof(pte6));
		pte6.len = sizeof(pte6);
		pte6.refcnt = UINT32_MAX;
		pte6.aid = prefix->aid;
		if (prefixlen > 128)
			fatalx("pt_fill: bad IPv6 prefixlen");
		inet6applymask(&pte6.prefix6, &prefix->v6, prefixlen);
		pte6.prefixlen = prefixlen;
		return ((struct pt_entry *)&pte6);
	case AID_VPN_IPv4:
		memset(&pte_vpn4, 0, sizeof(pte_vpn4));
		pte_vpn4.len = sizeof(pte_vpn4);
		pte_vpn4.refcnt = UINT32_MAX;
		pte_vpn4.aid = prefix->aid;
		if (prefixlen > 32)
			fatalx("pt_fill: bad IPv4 prefixlen");
		inet4applymask(&pte_vpn4.prefix4, &prefix->v4, prefixlen);
		pte_vpn4.prefixlen = prefixlen;
		pte_vpn4.rd = prefix->rd;
		pte_vpn4.labellen = prefix->labellen;
		memcpy(pte_vpn4.labelstack, prefix->labelstack,
		    prefix->labellen);
		return ((struct pt_entry *)&pte_vpn4);
	case AID_VPN_IPv6:
		memset(&pte_vpn6, 0, sizeof(pte_vpn6));
		pte_vpn6.len = sizeof(pte_vpn6);
		pte_vpn6.refcnt = UINT32_MAX;
		pte_vpn6.aid = prefix->aid;
		if (prefixlen > 128)
			fatalx("pt_fill: bad IPv6 prefixlen");
		inet6applymask(&pte_vpn6.prefix6, &prefix->v6, prefixlen);
		pte_vpn6.prefixlen = prefixlen;
		pte_vpn6.rd = prefix->rd;
		pte_vpn6.labellen = prefix->labellen;
		memcpy(pte_vpn6.labelstack, prefix->labelstack,
		    prefix->labellen);
		return ((struct pt_entry *)&pte_vpn6);
	case AID_EVPN:
		memset(&pte_evpn, 0, sizeof(pte_evpn));
		pte_evpn.len = sizeof(pte_evpn);
		pte_evpn.refcnt = UINT32_MAX;
		switch (prefix->evpn.aid) {
		case AID_UNSPEC:
			/* See rfc7432 section 7.2 */
			break;
		case AID_INET:
			pte_evpn.prefix4 = prefix->evpn.v4;
			break;
		case AID_INET6:
			pte_evpn.prefix6 = prefix->evpn.v6;
			break;
		default:
			fatalx("pt_fill: bad EVPN prefixlen");
		}
		pte_evpn.aid = prefix->aid;
		pte_evpn.vpnaid = prefix->evpn.aid;
		pte_evpn.prefixlen = prefixlen;
		pte_evpn.type = prefix->evpn.type;
		pte_evpn.rd = prefix->rd;
		pte_evpn.ethtag = prefix->evpn.ethtag;
		pte_evpn.labellen = prefix->labellen;
		memcpy(pte_evpn.labelstack, prefix->labelstack,
		    pte_evpn.labellen);
		memcpy(pte_evpn.esi, prefix->evpn.esi,
		    sizeof(prefix->evpn.esi));
		memcpy(pte_evpn.mac, prefix->evpn.mac,
		    sizeof(prefix->evpn.mac));
		return ((struct pt_entry *)&pte_evpn);
	default:
		fatalx("pt_fill: unknown af");
	}
}

struct pt_entry *
pt_get(struct bgpd_addr *prefix, int prefixlen)
{
	struct pt_entry	*pte;

	pte = pt_fill(prefix, prefixlen);
	return RB_FIND(pt_tree, &pttable, pte);
}

struct pt_entry *
pt_add(struct bgpd_addr *prefix, int prefixlen)
{
	struct pt_entry		*p = NULL;

	p = pt_fill(prefix, prefixlen);
	p = pt_alloc(p, p->len);

	if (RB_INSERT(pt_tree, &pttable, p) != NULL)
		fatalx("pt_add: insert failed");

	return (p);
}

struct pt_entry *
pt_get_flow(struct flowspec *f)
{
	struct pt_entry *needle;
	union {
		struct pt_entry_flow	flow;
		uint8_t			buf[4096];
	} x;

	needle = (struct pt_entry *)&x.flow;

	memset(needle, 0, PT_FLOW_SIZE);
	needle->aid = f->aid;
	needle->len = f->len + PT_FLOW_SIZE;
	memcpy(((struct pt_entry_flow *)needle)->flow, f->data, f->len);

	return RB_FIND(pt_tree, &pttable, (struct pt_entry *)needle);
}

struct pt_entry *
pt_add_flow(struct flowspec *f)
{
	struct pt_entry *p;
	int len = f->len + PT_FLOW_SIZE;

	p = malloc(len);
	if (p == NULL)
		fatal(__func__);
	rdemem.pt_cnt[f->aid]++;
	rdemem.pt_size[f->aid] += len;
	memset(p, 0, PT_FLOW_SIZE);

	p->len = len;
	p->aid = f->aid;
	memcpy(((struct pt_entry_flow *)p)->flow, f->data, f->len);

	if (RB_INSERT(pt_tree, &pttable, p) != NULL)
		fatalx("pt_add: insert failed");

	return (p);
}

void
pt_remove(struct pt_entry *pte)
{
	if (pte->refcnt != 0)
		fatalx("pt_remove: entry still holds references");

	if (RB_REMOVE(pt_tree, &pttable, pte) == NULL)
		log_warnx("pt_remove: remove failed.");
	pt_free(pte);
}

struct pt_entry *
pt_lookup(struct bgpd_addr *addr)
{
	struct pt_entry	*p;
	int		 i;

	switch (addr->aid) {
	case AID_INET:
	case AID_VPN_IPv4:
		i = 32;
		break;
	case AID_INET6:
	case AID_VPN_IPv6:
		i = 128;
		break;
	default:
		fatalx("pt_lookup: unknown af");
	}
	for (; i >= 0; i--) {
		p = pt_get(addr, i);
		if (p != NULL)
			return (p);
	}
	return (NULL);
}

int
pt_prefix_cmp(const struct pt_entry *a, const struct pt_entry *b)
{
	const struct pt_entry4		*a4, *b4;
	const struct pt_entry6		*a6, *b6;
	const struct pt_entry_vpn4	*va4, *vb4;
	const struct pt_entry_vpn6	*va6, *vb6;
	const struct pt_entry_flow	*af, *bf;
	const struct pt_entry_evpn	*ea, *eb;
	int				 i;

	if (a->aid > b->aid)
		return (1);
	if (a->aid < b->aid)
		return (-1);

	switch (a->aid) {
	case AID_INET:
		a4 = (const struct pt_entry4 *)a;
		b4 = (const struct pt_entry4 *)b;
		if (ntohl(a4->prefix4.s_addr) > ntohl(b4->prefix4.s_addr))
			return (1);
		if (ntohl(a4->prefix4.s_addr) < ntohl(b4->prefix4.s_addr))
			return (-1);
		if (a4->prefixlen > b4->prefixlen)
			return (1);
		if (a4->prefixlen < b4->prefixlen)
			return (-1);
		return (0);
	case AID_INET6:
		a6 = (const struct pt_entry6 *)a;
		b6 = (const struct pt_entry6 *)b;

		i = memcmp(&a6->prefix6, &b6->prefix6, sizeof(struct in6_addr));
		if (i > 0)
			return (1);
		if (i < 0)
			return (-1);
		if (a6->prefixlen < b6->prefixlen)
			return (-1);
		if (a6->prefixlen > b6->prefixlen)
			return (1);
		return (0);
	case AID_VPN_IPv4:
		va4 = (const struct pt_entry_vpn4 *)a;
		vb4 = (const struct pt_entry_vpn4 *)b;
		if (be64toh(va4->rd) > be64toh(vb4->rd))
			return (1);
		if (be64toh(va4->rd) < be64toh(vb4->rd))
			return (-1);
		if (ntohl(va4->prefix4.s_addr) > ntohl(vb4->prefix4.s_addr))
			return (1);
		if (ntohl(va4->prefix4.s_addr) < ntohl(vb4->prefix4.s_addr))
			return (-1);
		if (va4->prefixlen > vb4->prefixlen)
			return (1);
		if (va4->prefixlen < vb4->prefixlen)
			return (-1);
		return (0);
	case AID_VPN_IPv6:
		va6 = (const struct pt_entry_vpn6 *)a;
		vb6 = (const struct pt_entry_vpn6 *)b;
		if (be64toh(va6->rd) > be64toh(vb6->rd))
			return (1);
		if (be64toh(va6->rd) < be64toh(vb6->rd))
			return (-1);
		i = memcmp(&va6->prefix6, &vb6->prefix6,
		    sizeof(struct in6_addr));
		if (i > 0)
			return (1);
		if (i < 0)
			return (-1);
		if (va6->prefixlen > vb6->prefixlen)
			return (1);
		if (va6->prefixlen < vb6->prefixlen)
			return (-1);
		return (0);
	case AID_EVPN:
		/* XXXX Need different comparator for different types */
		ea = (const struct pt_entry_evpn *)a;
		eb = (const struct pt_entry_evpn *)b;
		if (ea->ethtag > eb->ethtag)
			return (1);
		if (ea->ethtag < eb->ethtag)
			return (-1);
		/* MAC length is always 48 */
		i = memcmp(&ea->mac, &eb->mac, sizeof(ea->mac));
		if (i > 0)
			return (1);
		if (i < 0)
			return (-1);
		if (ea->prefixlen > eb->prefixlen)
			return (1);
		if (ea->prefixlen < eb->prefixlen)
			return (-1);
		switch (ea->vpnaid) {
		case AID_UNSPEC:
			break;
		case AID_INET:
			i = memcmp(&ea->prefix4, &eb->prefix4,
			    sizeof(struct in_addr));
			if (i > 0)
				return (1);
			if (i < 0)
				return (-1);
			break;
		case AID_INET6:
			i = memcmp(&ea->prefix6, &eb->prefix6,
			    sizeof(struct in6_addr));
			if (i > 0)
				return (1);
			if (i < 0)
				return (-1);
			break;
		default:
			fatalx("pt_prefix_cmp: unknown evpn af %d", ea->vpnaid);
		}
		return (0);
	case AID_FLOWSPECv4:
	case AID_FLOWSPECv6:
		af = (const struct pt_entry_flow *)a;
		bf = (const struct pt_entry_flow *)b;
		return flowspec_cmp(af->flow, af->len - PT_FLOW_SIZE,
		    bf->flow, bf->len - PT_FLOW_SIZE,
		    a->aid == AID_FLOWSPECv6);
	default:
		fatalx("pt_prefix_cmp: unknown af %d", a->aid);
	}
	return (-1);
}

/*
 * Returns a pt_entry cloned from the one passed in.
 * Function may not return on failure.
 */
static struct pt_entry *
pt_alloc(struct pt_entry *op, int len)
{
	struct pt_entry		*p;

	p = malloc(len);
	if (p == NULL)
		fatal("pt_alloc");
	rdemem.pt_cnt[op->aid]++;
	rdemem.pt_size[op->aid] += len;
	memcpy(p, op, len);
	p->refcnt = 0;

	return (p);
}

static void
pt_free(struct pt_entry *pte)
{
	rdemem.pt_cnt[pte->aid]--;
	rdemem.pt_size[pte->aid] -= pte->len;
	free(pte);
}

/* dump a prefix into specified buffer */
int
pt_writebuf(struct ibuf *buf, struct pt_entry *pte, int withdraw,
    int add_path, uint32_t pathid)
{
	struct pt_entry_vpn4	*pvpn4 = (struct pt_entry_vpn4 *)pte;
	struct pt_entry_vpn6	*pvpn6 = (struct pt_entry_vpn6 *)pte;
	struct pt_entry_flow	*pflow = (struct pt_entry_flow *)pte;
	struct pt_entry_evpn	*pevpn = (struct pt_entry_evpn *)pte;
	struct ibuf		*tmp;
	int			 flowlen, psize;
	uint16_t		 plen;

	if ((tmp = ibuf_dynamic(32, UINT16_MAX)) == NULL)
		goto fail;

	if (add_path) {
		if (ibuf_add_n32(tmp, pathid) == -1)
			goto fail;
	}

	switch (pte->aid) {
	case AID_INET:
	case AID_INET6:
		plen = pte->prefixlen;
		if (ibuf_add_n8(tmp, plen) == -1)
			goto fail;
		if (ibuf_add(tmp, pte->data, PREFIX_SIZE(plen) - 1) == -1)
			goto fail;
		break;
	case AID_VPN_IPv4:
		plen = pvpn4->prefixlen;
		psize = PREFIX_SIZE(plen) - 1;
		plen += sizeof(pvpn4->rd) * 8;
		if (withdraw) {
			/* withdraw have one compat label as placeholder */
			plen += 3 * 8;
		} else {
			plen += pvpn4->labellen * 8;
		}

		if (ibuf_add_n8(tmp, plen) == -1)
			goto fail;
		if (withdraw) {
			/* magic compatibility label as per rfc8277 */
			if (ibuf_add_n8(tmp, 0x80) == -1 ||
			    ibuf_add_zero(tmp, 2) == -1)
				goto fail;
		} else {
			if (ibuf_add(tmp, &pvpn4->labelstack,
			    pvpn4->labellen) == -1)
				goto fail;
		}
		if (ibuf_add(tmp, &pvpn4->rd, sizeof(pvpn4->rd)) == -1 ||
		    ibuf_add(tmp, &pvpn4->prefix4, psize) == -1)
			goto fail;
		break;
	case AID_VPN_IPv6:
		plen = pvpn6->prefixlen;
		psize = PREFIX_SIZE(plen) - 1;
		plen += sizeof(pvpn6->rd) * 8;
		if (withdraw) {
			/* withdraw have one compat label as placeholder */
			plen += 3 * 8;
		} else {
			plen += pvpn6->labellen * 8;
		}

		if (ibuf_add_n8(tmp, plen) == -1)
			goto fail;
		if (withdraw) {
			/* magic compatibility label as per rfc8277 */
			if (ibuf_add_n8(tmp, 0x80) == -1 ||
			    ibuf_add_zero(tmp, 2) == -1)
				goto fail;
		} else {
			if (ibuf_add(tmp, &pvpn6->labelstack,
			    pvpn6->labellen) == -1)
				goto fail;
		}
		if (ibuf_add(tmp, &pvpn6->rd, sizeof(pvpn6->rd)) == -1 ||
		    ibuf_add(tmp, &pvpn6->prefix6, psize) == -1)
			goto fail;
		break;
	case AID_EVPN:
		if (ibuf_add_n8(tmp, pevpn->type) == -1)
			goto fail;
		switch (pevpn->type) {
		case EVPN_ROUTE_TYPE_2:
			plen = sizeof(pevpn->rd) * 8;
			plen += sizeof(pevpn->esi) * 8;
			plen += sizeof(pevpn->ethtag) * 8;
			plen += 8;	/* MAC length */
			plen += sizeof(pevpn->mac) * 8;
			plen += 8;	/* IP length */
			plen += pevpn->prefixlen;
			plen += pevpn->labellen * 8;
			if (ibuf_add_n8(tmp, PREFIX_SIZE(plen) - 1) == -1)
				goto fail;
			if (ibuf_add_h64(tmp, pevpn->rd) == -1 ||
			    ibuf_add(tmp, pevpn->esi,
			    sizeof(pevpn->esi)) == -1 ||
			    ibuf_add_h32(tmp, pevpn->ethtag) == -1)
				goto fail;
			if (ibuf_add_n8(tmp, sizeof(pevpn->mac) * 8) == -1 ||
			    ibuf_add(tmp, pevpn->mac, sizeof(pevpn->mac)) == -1)
				goto fail;
			if (ibuf_add_n8(tmp, pevpn->prefixlen) == -1)
				goto fail;
			switch (pevpn->vpnaid) {
			case AID_UNSPEC:
				/* See rfc7432 section 7.2 */
				break;
			case AID_INET:
				if (ibuf_add(tmp, &pevpn->prefix4,
				    sizeof(pevpn->prefix4)) == -1)
					goto fail;
				break;
			case AID_INET6:
				if (ibuf_add(tmp, &pevpn->prefix6,
				sizeof(pevpn->prefix6)) == -1)
					goto fail;
				break;
			default:
				goto fail;
			}
			if (ibuf_add(tmp, pevpn->labelstack,
			    pevpn->labellen) == -1)
				goto fail;
			break;
		case EVPN_ROUTE_TYPE_3:
			plen = sizeof(pevpn->rd) * 8;
			plen += sizeof(pevpn->ethtag) * 8;
			plen += 8;	/* IP length */
			plen += pevpn->prefixlen;
			if (ibuf_add_n8(tmp, PREFIX_SIZE(plen) - 1) == -1)
				goto fail;
			if (ibuf_add_h64(tmp, pevpn->rd) == -1 ||
			    ibuf_add_h32(tmp, pevpn->ethtag) == -1)
				goto fail;
			if (ibuf_add_n8(tmp, pevpn->prefixlen) == -1)
				goto fail;
			switch (pevpn->vpnaid) {
			case AID_INET:
				if (ibuf_add(tmp, &pevpn->prefix4,
				    sizeof(pevpn->prefix4)) == -1)
					goto fail;
				break;
			case AID_INET6:
				if (ibuf_add(tmp, &pevpn->prefix6,
				    sizeof(pevpn->prefix6)) == -1)
					goto fail;
				break;
			default:
				goto fail;
			}
		}
		break;
	case AID_FLOWSPECv4:
	case AID_FLOWSPECv6:
		flowlen = pflow->len - PT_FLOW_SIZE;
		if (flowlen < FLOWSPEC_LEN_LIMIT) {
			if (ibuf_add_n8(tmp, flowlen) == -1)
				goto fail;
		} else {
			if (ibuf_add_n8(tmp, 0xf0 | (flowlen >> 8)) == -1 ||
			    ibuf_add_n8(tmp, flowlen) == -1)
				goto fail;
		}
		if (ibuf_add(tmp, &pflow->flow, flowlen) == -1)
			goto fail;
		break;
	default:
		fatalx("%s: unknown aid %d", __func__, pte->aid);
	}

	/* keep 2 bytes reserved in the withdraw case for IPv4 encoding */
	if (withdraw && ibuf_left(buf) < ibuf_size(tmp) + 2)
		goto fail;
	if (ibuf_add_ibuf(buf, tmp) == -1)
		goto fail;
	ibuf_free(tmp);
	return 0;

 fail:
	ibuf_free(tmp);
	return -1;
}

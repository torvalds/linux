/*-
 * Copyright (c) 2016 Yandex LLC
 * Copyright (c) 2016 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/eventhandler.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rmlock.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <net/pfil.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/ip_fw.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>

#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/ipfw/nptv6/nptv6.h>

VNET_DEFINE_STATIC(uint16_t, nptv6_eid) = 0;
#define	V_nptv6_eid	VNET(nptv6_eid)
#define	IPFW_TLV_NPTV6_NAME	IPFW_TLV_EACTION_NAME(V_nptv6_eid)

static eventhandler_tag nptv6_ifaddr_event;

static struct nptv6_cfg *nptv6_alloc_config(const char *name, uint8_t set);
static void nptv6_free_config(struct nptv6_cfg *cfg);
static struct nptv6_cfg *nptv6_find(struct namedobj_instance *ni,
    const char *name, uint8_t set);
static int nptv6_rewrite_internal(struct nptv6_cfg *cfg, struct mbuf **mp,
    int offset);
static int nptv6_rewrite_external(struct nptv6_cfg *cfg, struct mbuf **mp,
    int offset);

#define	NPTV6_LOOKUP(chain, cmd)	\
    (struct nptv6_cfg *)SRV_OBJECT((chain), (cmd)->arg1)

#ifndef IN6_MASK_ADDR
#define IN6_MASK_ADDR(a, m)	do { \
	(a)->s6_addr32[0] &= (m)->s6_addr32[0]; \
	(a)->s6_addr32[1] &= (m)->s6_addr32[1]; \
	(a)->s6_addr32[2] &= (m)->s6_addr32[2]; \
	(a)->s6_addr32[3] &= (m)->s6_addr32[3]; \
} while (0)
#endif
#ifndef IN6_ARE_MASKED_ADDR_EQUAL
#define IN6_ARE_MASKED_ADDR_EQUAL(d, a, m)	(	\
	(((d)->s6_addr32[0] ^ (a)->s6_addr32[0]) & (m)->s6_addr32[0]) == 0 && \
	(((d)->s6_addr32[1] ^ (a)->s6_addr32[1]) & (m)->s6_addr32[1]) == 0 && \
	(((d)->s6_addr32[2] ^ (a)->s6_addr32[2]) & (m)->s6_addr32[2]) == 0 && \
	(((d)->s6_addr32[3] ^ (a)->s6_addr32[3]) & (m)->s6_addr32[3]) == 0 )
#endif

#if 0
#define	NPTV6_DEBUG(fmt, ...)	do {			\
	printf("%s: " fmt "\n", __func__, ## __VA_ARGS__);	\
} while (0)
#define	NPTV6_IPDEBUG(fmt, ...)	do {			\
	char _s[INET6_ADDRSTRLEN], _d[INET6_ADDRSTRLEN];	\
	printf("%s: " fmt "\n", __func__, ## __VA_ARGS__);	\
} while (0)
#else
#define	NPTV6_DEBUG(fmt, ...)
#define	NPTV6_IPDEBUG(fmt, ...)
#endif

static int
nptv6_getlasthdr(struct nptv6_cfg *cfg, struct mbuf *m, int *offset)
{
	struct ip6_hdr *ip6;
	struct ip6_hbh *hbh;
	int proto, hlen;

	hlen = (offset == NULL) ? 0: *offset;
	if (m->m_len < hlen)
		return (-1);
	ip6 = mtodo(m, hlen);
	hlen += sizeof(*ip6);
	proto = ip6->ip6_nxt;
	while (proto == IPPROTO_HOPOPTS || proto == IPPROTO_ROUTING ||
	    proto == IPPROTO_DSTOPTS) {
		hbh = mtodo(m, hlen);
		if (m->m_len < hlen)
			return (-1);
		proto = hbh->ip6h_nxt;
		hlen += (hbh->ip6h_len + 1) << 3;
	}
	if (offset != NULL)
		*offset = hlen;
	return (proto);
}

static int
nptv6_translate_icmpv6(struct nptv6_cfg *cfg, struct mbuf **mp, int offset)
{
	struct icmp6_hdr *icmp6;
	struct ip6_hdr *ip6;
	struct mbuf *m;

	m = *mp;
	if (offset > m->m_len)
		return (-1);
	icmp6 = mtodo(m, offset);
	NPTV6_DEBUG("ICMPv6 type %d", icmp6->icmp6_type);
	switch (icmp6->icmp6_type) {
	case ICMP6_DST_UNREACH:
	case ICMP6_PACKET_TOO_BIG:
	case ICMP6_TIME_EXCEEDED:
	case ICMP6_PARAM_PROB:
		break;
	case ICMP6_ECHO_REQUEST:
	case ICMP6_ECHO_REPLY:
		/* nothing to translate */
		return (0);
	default:
		/*
		 * XXX: We can add some checks to not translate NDP and MLD
		 * messages. Currently user must explicitly allow these message
		 * types, otherwise packets will be dropped.
		 */
		return (-1);
	}
	offset += sizeof(*icmp6);
	if (offset + sizeof(*ip6) > m->m_pkthdr.len)
		return (-1);
	if (offset + sizeof(*ip6) > m->m_len)
		*mp = m = m_pullup(m, offset + sizeof(*ip6));
	if (m == NULL)
		return (-1);
	ip6 = mtodo(m, offset);
	NPTV6_IPDEBUG("offset %d, %s -> %s %d", offset,
	    inet_ntop(AF_INET6, &ip6->ip6_src, _s, sizeof(_s)),
	    inet_ntop(AF_INET6, &ip6->ip6_dst, _d, sizeof(_d)),
	    ip6->ip6_nxt);
	if (IN6_ARE_MASKED_ADDR_EQUAL(&ip6->ip6_src,
	    &cfg->external, &cfg->mask))
		return (nptv6_rewrite_external(cfg, mp, offset));
	else if (IN6_ARE_MASKED_ADDR_EQUAL(&ip6->ip6_dst,
	    &cfg->internal, &cfg->mask))
		return (nptv6_rewrite_internal(cfg, mp, offset));
	/*
	 * Addresses in the inner IPv6 header doesn't matched to
	 * our prefixes.
	 */
	return (-1);
}

static int
nptv6_search_index(struct nptv6_cfg *cfg, struct in6_addr *a)
{
	int idx;

	if (cfg->flags & NPTV6_48PLEN)
		return (3);

	/* Search suitable word index for adjustment */
	for (idx = 4; idx < 8; idx++)
		if (a->s6_addr16[idx] != 0xffff)
			break;
	/*
	 * RFC 6296 p3.7: If an NPTv6 Translator discovers a datagram with
	 * an IID of all-zeros while performing address mapping, that
	 * datagram MUST be dropped, and an ICMPv6 Parameter Problem error
	 * SHOULD be generated.
	 */
	if (idx == 8 ||
	    (a->s6_addr32[2] == 0 && a->s6_addr32[3] == 0))
		return (-1);
	return (idx);
}

static void
nptv6_copy_addr(struct in6_addr *src, struct in6_addr *dst,
    struct in6_addr *mask)
{
	int i;

	for (i = 0; i < 8 && mask->s6_addr8[i] != 0; i++) {
		dst->s6_addr8[i] &=  ~mask->s6_addr8[i];
		dst->s6_addr8[i] |= src->s6_addr8[i] & mask->s6_addr8[i];
	}
}

static int
nptv6_rewrite_internal(struct nptv6_cfg *cfg, struct mbuf **mp, int offset)
{
	struct in6_addr *addr;
	struct ip6_hdr *ip6;
	int idx, proto;
	uint16_t adj;

	ip6 = mtodo(*mp, offset);
	NPTV6_IPDEBUG("offset %d, %s -> %s %d", offset,
	    inet_ntop(AF_INET6, &ip6->ip6_src, _s, sizeof(_s)),
	    inet_ntop(AF_INET6, &ip6->ip6_dst, _d, sizeof(_d)),
	    ip6->ip6_nxt);
	if (offset == 0)
		addr = &ip6->ip6_src;
	else {
		/*
		 * When we rewriting inner IPv6 header, we need to rewrite
		 * destination address back to external prefix. The datagram in
		 * the ICMPv6 payload should looks like it was send from
		 * external prefix.
		 */
		addr = &ip6->ip6_dst;
	}
	idx = nptv6_search_index(cfg, addr);
	if (idx < 0) {
		/*
		 * Do not send ICMPv6 error when offset isn't zero.
		 * This means we are rewriting inner IPv6 header in the
		 * ICMPv6 error message.
		 */
		if (offset == 0) {
			icmp6_error2(*mp, ICMP6_DST_UNREACH,
			    ICMP6_DST_UNREACH_ADDR, 0, (*mp)->m_pkthdr.rcvif);
			*mp = NULL;
		}
		return (IP_FW_DENY);
	}
	adj = addr->s6_addr16[idx];
	nptv6_copy_addr(&cfg->external, addr, &cfg->mask);
	adj = cksum_add(adj, cfg->adjustment);
	if (adj == 0xffff)
		adj = 0;
	addr->s6_addr16[idx] = adj;
	if (offset == 0) {
		/*
		 * We may need to translate addresses in the inner IPv6
		 * header for ICMPv6 error messages.
		 */
		proto = nptv6_getlasthdr(cfg, *mp, &offset);
		if (proto < 0 || (proto == IPPROTO_ICMPV6 &&
		    nptv6_translate_icmpv6(cfg, mp, offset) != 0))
			return (IP_FW_DENY);
		NPTV6STAT_INC(cfg, in2ex);
	}
	return (0);
}

static int
nptv6_rewrite_external(struct nptv6_cfg *cfg, struct mbuf **mp, int offset)
{
	struct in6_addr *addr;
	struct ip6_hdr *ip6;
	int idx, proto;
	uint16_t adj;

	ip6 = mtodo(*mp, offset);
	NPTV6_IPDEBUG("offset %d, %s -> %s %d", offset,
	    inet_ntop(AF_INET6, &ip6->ip6_src, _s, sizeof(_s)),
	    inet_ntop(AF_INET6, &ip6->ip6_dst, _d, sizeof(_d)),
	    ip6->ip6_nxt);
	if (offset == 0)
		addr = &ip6->ip6_dst;
	else {
		/*
		 * When we rewriting inner IPv6 header, we need to rewrite
		 * source address back to internal prefix. The datagram in
		 * the ICMPv6 payload should looks like it was send from
		 * internal prefix.
		 */
		addr = &ip6->ip6_src;
	}
	idx = nptv6_search_index(cfg, addr);
	if (idx < 0) {
		/*
		 * Do not send ICMPv6 error when offset isn't zero.
		 * This means we are rewriting inner IPv6 header in the
		 * ICMPv6 error message.
		 */
		if (offset == 0) {
			icmp6_error2(*mp, ICMP6_DST_UNREACH,
			    ICMP6_DST_UNREACH_ADDR, 0, (*mp)->m_pkthdr.rcvif);
			*mp = NULL;
		}
		return (IP_FW_DENY);
	}
	adj = addr->s6_addr16[idx];
	nptv6_copy_addr(&cfg->internal, addr, &cfg->mask);
	adj = cksum_add(adj, ~cfg->adjustment);
	if (adj == 0xffff)
		adj = 0;
	addr->s6_addr16[idx] = adj;
	if (offset == 0) {
		/*
		 * We may need to translate addresses in the inner IPv6
		 * header for ICMPv6 error messages.
		 */
		proto = nptv6_getlasthdr(cfg, *mp, &offset);
		if (proto < 0 || (proto == IPPROTO_ICMPV6 &&
		    nptv6_translate_icmpv6(cfg, mp, offset) != 0))
			return (IP_FW_DENY);
		NPTV6STAT_INC(cfg, ex2in);
	}
	return (0);
}

/*
 * ipfw external action handler.
 */
static int
ipfw_nptv6(struct ip_fw_chain *chain, struct ip_fw_args *args,
    ipfw_insn *cmd, int *done)
{
	struct ip6_hdr *ip6;
	struct nptv6_cfg *cfg;
	ipfw_insn *icmd;
	int ret;

	*done = 0; /* try next rule if not matched */
	ret = IP_FW_DENY;
	icmd = cmd + 1;
	if (cmd->opcode != O_EXTERNAL_ACTION ||
	    cmd->arg1 != V_nptv6_eid ||
	    icmd->opcode != O_EXTERNAL_INSTANCE ||
	    (cfg = NPTV6_LOOKUP(chain, icmd)) == NULL ||
	    (cfg->flags & NPTV6_READY) == 0)
		return (ret);
	/*
	 * We need act as router, so when forwarding is disabled -
	 * do nothing.
	 */
	if (V_ip6_forwarding == 0 || args->f_id.addr_type != 6)
		return (ret);
	/*
	 * NOTE: we expect ipfw_chk() did m_pullup() up to upper level
	 * protocol's headers. Also we skip some checks, that ip6_input(),
	 * ip6_forward(), ip6_fastfwd() and ipfw_chk() already did.
	 */
	ip6 = mtod(args->m, struct ip6_hdr *);
	NPTV6_IPDEBUG("eid %u, oid %u, %s -> %s %d",
	    cmd->arg1, icmd->arg1,
	    inet_ntop(AF_INET6, &ip6->ip6_src, _s, sizeof(_s)),
	    inet_ntop(AF_INET6, &ip6->ip6_dst, _d, sizeof(_d)),
	    ip6->ip6_nxt);
	if (IN6_ARE_MASKED_ADDR_EQUAL(&ip6->ip6_src,
	    &cfg->internal, &cfg->mask)) {
		/*
		 * XXX: Do not translate packets when both src and dst
		 * are from internal prefix.
		 */
		if (IN6_ARE_MASKED_ADDR_EQUAL(&ip6->ip6_dst,
		    &cfg->internal, &cfg->mask))
			return (ret);
		ret = nptv6_rewrite_internal(cfg, &args->m, 0);
	} else if (IN6_ARE_MASKED_ADDR_EQUAL(&ip6->ip6_dst,
	    &cfg->external, &cfg->mask))
		ret = nptv6_rewrite_external(cfg, &args->m, 0);
	else
		return (ret);
	/*
	 * If address wasn't rewrited - free mbuf and terminate the search.
	 */
	if (ret != 0) {
		if (args->m != NULL) {
			m_freem(args->m);
			args->m = NULL; /* mark mbuf as consumed */
		}
		NPTV6STAT_INC(cfg, dropped);
		*done = 1;
	} else {
		/* Terminate the search if one_pass is set */
		*done = V_fw_one_pass;
		/* Update args->f_id when one_pass is off */
		if (*done == 0) {
			ip6 = mtod(args->m, struct ip6_hdr *);
			args->f_id.src_ip6 = ip6->ip6_src;
			args->f_id.dst_ip6 = ip6->ip6_dst;
		}
	}
	return (ret);
}

static struct nptv6_cfg *
nptv6_alloc_config(const char *name, uint8_t set)
{
	struct nptv6_cfg *cfg;

	cfg = malloc(sizeof(struct nptv6_cfg), M_IPFW, M_WAITOK | M_ZERO);
	COUNTER_ARRAY_ALLOC(cfg->stats, NPTV6STATS, M_WAITOK);
	cfg->no.name = cfg->name;
	cfg->no.etlv = IPFW_TLV_NPTV6_NAME;
	cfg->no.set = set;
	strlcpy(cfg->name, name, sizeof(cfg->name));
	return (cfg);
}

static void
nptv6_free_config(struct nptv6_cfg *cfg)
{

	COUNTER_ARRAY_FREE(cfg->stats, NPTV6STATS);
	free(cfg, M_IPFW);
}

static void
nptv6_export_config(struct ip_fw_chain *ch, struct nptv6_cfg *cfg,
    ipfw_nptv6_cfg *uc)
{

	uc->internal = cfg->internal;
	if (cfg->flags & NPTV6_DYNAMIC_PREFIX)
		memcpy(uc->if_name, cfg->if_name, IF_NAMESIZE);
	else
		uc->external = cfg->external;
	uc->plen = cfg->plen;
	uc->flags = cfg->flags & NPTV6_FLAGSMASK;
	uc->set = cfg->no.set;
	strlcpy(uc->name, cfg->no.name, sizeof(uc->name));
}

struct nptv6_dump_arg {
	struct ip_fw_chain *ch;
	struct sockopt_data *sd;
};

static int
export_config_cb(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{
	struct nptv6_dump_arg *da = (struct nptv6_dump_arg *)arg;
	ipfw_nptv6_cfg *uc;

	uc = (ipfw_nptv6_cfg *)ipfw_get_sopt_space(da->sd, sizeof(*uc));
	nptv6_export_config(da->ch, (struct nptv6_cfg *)no, uc);
	return (0);
}

static struct nptv6_cfg *
nptv6_find(struct namedobj_instance *ni, const char *name, uint8_t set)
{
	struct nptv6_cfg *cfg;

	cfg = (struct nptv6_cfg *)ipfw_objhash_lookup_name_type(ni, set,
	    IPFW_TLV_NPTV6_NAME, name);

	return (cfg);
}

static void
nptv6_calculate_adjustment(struct nptv6_cfg *cfg)
{
	uint16_t i, e;
	uint16_t *p;

	/* Calculate checksum of internal prefix */
	for (i = 0, p = (uint16_t *)&cfg->internal;
	    p < (uint16_t *)(&cfg->internal + 1); p++)
		i = cksum_add(i, *p);

	/* Calculate checksum of external prefix */
	for (e = 0, p = (uint16_t *)&cfg->external;
	    p < (uint16_t *)(&cfg->external + 1); p++)
		e = cksum_add(e, *p);

	/* Adjustment value for Int->Ext direction */
	cfg->adjustment = cksum_add(~e, i);
}

static int
nptv6_check_prefix(const struct in6_addr *addr)
{

	if (IN6_IS_ADDR_MULTICAST(addr) ||
	    IN6_IS_ADDR_LINKLOCAL(addr) ||
	    IN6_IS_ADDR_LOOPBACK(addr) ||
	    IN6_IS_ADDR_UNSPECIFIED(addr))
		return (EINVAL);
	return (0);
}

static void
nptv6_set_external(struct nptv6_cfg *cfg, struct in6_addr *addr)
{

	cfg->external = *addr;
	IN6_MASK_ADDR(&cfg->external, &cfg->mask);
	nptv6_calculate_adjustment(cfg);
	cfg->flags |= NPTV6_READY;
}

/*
 * Try to determine what prefix to use as external for
 * configured interface name.
 */
static void
nptv6_find_prefix(struct ip_fw_chain *ch, struct nptv6_cfg *cfg,
    struct ifnet *ifp)
{
	struct ifaddr *ifa;
	struct in6_ifaddr *ia;

	MPASS(cfg->flags & NPTV6_DYNAMIC_PREFIX);
	IPFW_UH_WLOCK_ASSERT(ch);

	if (ifp == NULL) {
		ifp = ifunit_ref(cfg->if_name);
		if (ifp == NULL)
			return;
	}
	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		ia = (struct in6_ifaddr *)ifa;
		if (nptv6_check_prefix(&ia->ia_addr.sin6_addr) ||
		    IN6_ARE_MASKED_ADDR_EQUAL(&ia->ia_addr.sin6_addr,
		    &cfg->internal, &cfg->mask))
			continue;
		/* Suitable address is found. */
		nptv6_set_external(cfg, &ia->ia_addr.sin6_addr);
		break;
	}
	if_addr_runlock(ifp);
	if_rele(ifp);
}

struct ifaddr_event_args {
	struct ifnet *ifp;
	const struct in6_addr *addr;
	int event;
};

static int
ifaddr_cb(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{
	struct ifaddr_event_args *args;
	struct ip_fw_chain *ch;
	struct nptv6_cfg *cfg;

	ch = &V_layer3_chain;
	cfg = (struct nptv6_cfg *)SRV_OBJECT(ch, no->kidx);
	if ((cfg->flags & NPTV6_DYNAMIC_PREFIX) == 0)
		return (0);

	args = arg;
	/* If interface name doesn't match, ignore */
	if (strncmp(args->ifp->if_xname, cfg->if_name, IF_NAMESIZE))
		return (0);
	if (args->ifp->if_flags & IFF_DYING) { /* XXX: is it possible? */
		cfg->flags &= ~NPTV6_READY;
		return (0);
	}
	if (args->event == IFADDR_EVENT_DEL) {
		/* If instance is not ready, ignore */
		if ((cfg->flags & NPTV6_READY) == 0)
			return (0);
		/* If address does not match the external prefix, ignore */
		if (IN6_ARE_MASKED_ADDR_EQUAL(&cfg->external, args->addr,
		    &cfg->mask) != 0)
			return (0);
		/* Otherwise clear READY flag */
		cfg->flags &= ~NPTV6_READY;
	} else {/* IFADDR_EVENT_ADD */
		/* If instance is already ready, ignore */
		if (cfg->flags & NPTV6_READY)
			return (0);
		/* If address is not suitable for prefix, ignore */
		if (nptv6_check_prefix(args->addr) ||
		    IN6_ARE_MASKED_ADDR_EQUAL(args->addr, &cfg->internal,
		    &cfg->mask))
			return (0);
		/* FALLTHROUGH */
	}
	MPASS(!(cfg->flags & NPTV6_READY));
	/* Try to determine the prefix */
	if_ref(args->ifp);
	nptv6_find_prefix(ch, cfg, args->ifp);
	return (0);
}

static void
nptv6_ifaddrevent_handler(void *arg __unused, struct ifnet *ifp,
    struct ifaddr *ifa, int event)
{
	struct ifaddr_event_args args;
	struct ip_fw_chain *ch;

	if (ifa->ifa_addr->sa_family != AF_INET6)
		return;

	args.ifp = ifp;
	args.addr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
	args.event = event;

	ch = &V_layer3_chain;
	IPFW_UH_WLOCK(ch);
	ipfw_objhash_foreach_type(CHAIN_TO_SRV(ch), ifaddr_cb, &args,
	    IPFW_TLV_NPTV6_NAME);
	IPFW_UH_WUNLOCK(ch);
}

/*
 * Creates new NPTv6 instance.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ipfw_nptv6_cfg ]
 *
 * Returns 0 on success
 */
static int
nptv6_create(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct in6_addr mask;
	ipfw_obj_lheader *olh;
	ipfw_nptv6_cfg *uc;
	struct namedobj_instance *ni;
	struct nptv6_cfg *cfg;

	if (sd->valsize != sizeof(*olh) + sizeof(*uc))
		return (EINVAL);

	olh = (ipfw_obj_lheader *)sd->kbuf;
	uc = (ipfw_nptv6_cfg *)(olh + 1);
	if (ipfw_check_object_name_generic(uc->name) != 0)
		return (EINVAL);
	if (uc->plen < 8 || uc->plen > 64 || uc->set >= IPFW_MAX_SETS)
		return (EINVAL);
	if (nptv6_check_prefix(&uc->internal))
		return (EINVAL);
	in6_prefixlen2mask(&mask, uc->plen);
	if ((uc->flags & NPTV6_DYNAMIC_PREFIX) == 0 && (
	    nptv6_check_prefix(&uc->external) ||
	    IN6_ARE_MASKED_ADDR_EQUAL(&uc->external, &uc->internal, &mask)))
		return (EINVAL);

	ni = CHAIN_TO_SRV(ch);
	IPFW_UH_RLOCK(ch);
	if (nptv6_find(ni, uc->name, uc->set) != NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (EEXIST);
	}
	IPFW_UH_RUNLOCK(ch);

	cfg = nptv6_alloc_config(uc->name, uc->set);
	cfg->plen = uc->plen;
	cfg->flags = uc->flags & NPTV6_FLAGSMASK;
	if (cfg->plen <= 48)
		cfg->flags |= NPTV6_48PLEN;
	cfg->mask = mask;
	cfg->internal = uc->internal;
	IN6_MASK_ADDR(&cfg->internal, &mask);
	if (cfg->flags & NPTV6_DYNAMIC_PREFIX)
		memcpy(cfg->if_name, uc->if_name, IF_NAMESIZE);
	else
		nptv6_set_external(cfg, &uc->external);

	if ((uc->flags & NPTV6_DYNAMIC_PREFIX) != 0 &&
	    nptv6_ifaddr_event == NULL)
		nptv6_ifaddr_event = EVENTHANDLER_REGISTER(
		    ifaddr_event_ext, nptv6_ifaddrevent_handler, NULL,
		    EVENTHANDLER_PRI_ANY);

	IPFW_UH_WLOCK(ch);
	if (ipfw_objhash_alloc_idx(ni, &cfg->no.kidx) != 0) {
		IPFW_UH_WUNLOCK(ch);
		nptv6_free_config(cfg);
		return (ENOSPC);
	}
	ipfw_objhash_add(ni, &cfg->no);
	SRV_OBJECT(ch, cfg->no.kidx) = cfg;
	if (cfg->flags & NPTV6_DYNAMIC_PREFIX)
		nptv6_find_prefix(ch, cfg, NULL);
	IPFW_UH_WUNLOCK(ch);

	return (0);
}

/*
 * Destroys NPTv6 instance.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ]
 *
 * Returns 0 on success
 */
static int
nptv6_destroy(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_header *oh;
	struct nptv6_cfg *cfg;

	if (sd->valsize != sizeof(*oh))
		return (EINVAL);

	oh = (ipfw_obj_header *)sd->kbuf;
	if (ipfw_check_object_name_generic(oh->ntlv.name) != 0)
		return (EINVAL);

	IPFW_UH_WLOCK(ch);
	cfg = nptv6_find(CHAIN_TO_SRV(ch), oh->ntlv.name, oh->ntlv.set);
	if (cfg == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}
	if (cfg->no.refcnt > 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EBUSY);
	}

	ipfw_reset_eaction_instance(ch, V_nptv6_eid, cfg->no.kidx);
	SRV_OBJECT(ch, cfg->no.kidx) = NULL;
	ipfw_objhash_del(CHAIN_TO_SRV(ch), &cfg->no);
	ipfw_objhash_free_idx(CHAIN_TO_SRV(ch), cfg->no.kidx);
	IPFW_UH_WUNLOCK(ch);

	nptv6_free_config(cfg);
	return (0);
}

/*
 * Get or change nptv6 instance config.
 * Request: [ ipfw_obj_header [ ipfw_nptv6_cfg ] ]
 */
static int
nptv6_config(struct ip_fw_chain *chain, ip_fw3_opheader *op,
    struct sockopt_data *sd)
{

	return (EOPNOTSUPP);
}

/*
 * Lists all NPTv6 instances currently available in kernel.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ]
 * Reply: [ ipfw_obj_lheader ipfw_nptv6_cfg x N ]
 *
 * Returns 0 on success
 */
static int
nptv6_list(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_lheader *olh;
	struct nptv6_dump_arg da;

	/* Check minimum header size */
	if (sd->valsize < sizeof(ipfw_obj_lheader))
		return (EINVAL);

	olh = (ipfw_obj_lheader *)ipfw_get_sopt_header(sd, sizeof(*olh));

	IPFW_UH_RLOCK(ch);
	olh->count = ipfw_objhash_count_type(CHAIN_TO_SRV(ch),
	    IPFW_TLV_NPTV6_NAME);
	olh->objsize = sizeof(ipfw_nptv6_cfg);
	olh->size = sizeof(*olh) + olh->count * olh->objsize;

	if (sd->valsize < olh->size) {
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}
	memset(&da, 0, sizeof(da));
	da.ch = ch;
	da.sd = sd;
	ipfw_objhash_foreach_type(CHAIN_TO_SRV(ch), export_config_cb,
	    &da, IPFW_TLV_NPTV6_NAME);
	IPFW_UH_RUNLOCK(ch);

	return (0);
}

#define	__COPY_STAT_FIELD(_cfg, _stats, _field)	\
	(_stats)->_field = NPTV6STAT_FETCH(_cfg, _field)
static void
export_stats(struct ip_fw_chain *ch, struct nptv6_cfg *cfg,
    struct ipfw_nptv6_stats *stats)
{

	__COPY_STAT_FIELD(cfg, stats, in2ex);
	__COPY_STAT_FIELD(cfg, stats, ex2in);
	__COPY_STAT_FIELD(cfg, stats, dropped);
}

/*
 * Get NPTv6 statistics.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ]
 * Reply: [ ipfw_obj_header ipfw_obj_ctlv [ uint64_t x N ]]
 *
 * Returns 0 on success
 */
static int
nptv6_stats(struct ip_fw_chain *ch, ip_fw3_opheader *op,
    struct sockopt_data *sd)
{
	struct ipfw_nptv6_stats stats;
	struct nptv6_cfg *cfg;
	ipfw_obj_header *oh;
	ipfw_obj_ctlv *ctlv;
	size_t sz;

	sz = sizeof(ipfw_obj_header) + sizeof(ipfw_obj_ctlv) + sizeof(stats);
	if (sd->valsize % sizeof(uint64_t))
		return (EINVAL);
	if (sd->valsize < sz)
		return (ENOMEM);
	oh = (ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);
	if (oh == NULL)
		return (EINVAL);
	if (ipfw_check_object_name_generic(oh->ntlv.name) != 0 ||
	    oh->ntlv.set >= IPFW_MAX_SETS)
		return (EINVAL);
	memset(&stats, 0, sizeof(stats));

	IPFW_UH_RLOCK(ch);
	cfg = nptv6_find(CHAIN_TO_SRV(ch), oh->ntlv.name, oh->ntlv.set);
	if (cfg == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (ESRCH);
	}
	export_stats(ch, cfg, &stats);
	IPFW_UH_RUNLOCK(ch);

	ctlv = (ipfw_obj_ctlv *)(oh + 1);
	memset(ctlv, 0, sizeof(*ctlv));
	ctlv->head.type = IPFW_TLV_COUNTERS;
	ctlv->head.length = sz - sizeof(ipfw_obj_header);
	ctlv->count = sizeof(stats) / sizeof(uint64_t);
	ctlv->objsize = sizeof(uint64_t);
	ctlv->version = 1;
	memcpy(ctlv + 1, &stats, sizeof(stats));
	return (0);
}

/*
 * Reset NPTv6 statistics.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ]
 *
 * Returns 0 on success
 */
static int
nptv6_reset_stats(struct ip_fw_chain *ch, ip_fw3_opheader *op,
    struct sockopt_data *sd)
{
	struct nptv6_cfg *cfg;
	ipfw_obj_header *oh;

	if (sd->valsize != sizeof(*oh))
		return (EINVAL);
	oh = (ipfw_obj_header *)sd->kbuf;
	if (ipfw_check_object_name_generic(oh->ntlv.name) != 0 ||
	    oh->ntlv.set >= IPFW_MAX_SETS)
		return (EINVAL);

	IPFW_UH_WLOCK(ch);
	cfg = nptv6_find(CHAIN_TO_SRV(ch), oh->ntlv.name, oh->ntlv.set);
	if (cfg == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}
	COUNTER_ARRAY_ZERO(cfg->stats, NPTV6STATS);
	IPFW_UH_WUNLOCK(ch);
	return (0);
}

static struct ipfw_sopt_handler	scodes[] = {
	{ IP_FW_NPTV6_CREATE, 0,	HDIR_SET,	nptv6_create },
	{ IP_FW_NPTV6_DESTROY,0,	HDIR_SET,	nptv6_destroy },
	{ IP_FW_NPTV6_CONFIG, 0,	HDIR_BOTH,	nptv6_config },
	{ IP_FW_NPTV6_LIST,   0,	HDIR_GET,	nptv6_list },
	{ IP_FW_NPTV6_STATS,  0,	HDIR_GET,	nptv6_stats },
	{ IP_FW_NPTV6_RESET_STATS,0,	HDIR_SET,	nptv6_reset_stats },
};

static int
nptv6_classify(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype)
{
	ipfw_insn *icmd;

	icmd = cmd - 1;
	NPTV6_DEBUG("opcode %d, arg1 %d, opcode0 %d, arg1 %d",
	    cmd->opcode, cmd->arg1, icmd->opcode, icmd->arg1);
	if (icmd->opcode != O_EXTERNAL_ACTION ||
	    icmd->arg1 != V_nptv6_eid)
		return (1);

	*puidx = cmd->arg1;
	*ptype = 0;
	return (0);
}

static void
nptv6_update_arg1(ipfw_insn *cmd, uint16_t idx)
{

	cmd->arg1 = idx;
	NPTV6_DEBUG("opcode %d, arg1 -> %d", cmd->opcode, cmd->arg1);
}

static int
nptv6_findbyname(struct ip_fw_chain *ch, struct tid_info *ti,
    struct named_object **pno)
{
	int err;

	err = ipfw_objhash_find_type(CHAIN_TO_SRV(ch), ti,
	    IPFW_TLV_NPTV6_NAME, pno);
	NPTV6_DEBUG("uidx %u, type %u, err %d", ti->uidx, ti->type, err);
	return (err);
}

static struct named_object *
nptv6_findbykidx(struct ip_fw_chain *ch, uint16_t idx)
{
	struct namedobj_instance *ni;
	struct named_object *no;

	IPFW_UH_WLOCK_ASSERT(ch);
	ni = CHAIN_TO_SRV(ch);
	no = ipfw_objhash_lookup_kidx(ni, idx);
	KASSERT(no != NULL, ("NPT with index %d not found", idx));

	NPTV6_DEBUG("kidx %u -> %s", idx, no->name);
	return (no);
}

static int
nptv6_manage_sets(struct ip_fw_chain *ch, uint16_t set, uint8_t new_set,
    enum ipfw_sets_cmd cmd)
{

	return (ipfw_obj_manage_sets(CHAIN_TO_SRV(ch), IPFW_TLV_NPTV6_NAME,
	    set, new_set, cmd));
}

static struct opcode_obj_rewrite opcodes[] = {
	{
		.opcode	= O_EXTERNAL_INSTANCE,
		.etlv = IPFW_TLV_EACTION /* just show it isn't table */,
		.classifier = nptv6_classify,
		.update = nptv6_update_arg1,
		.find_byname = nptv6_findbyname,
		.find_bykidx = nptv6_findbykidx,
		.manage_sets = nptv6_manage_sets,
	},
};

static int
destroy_config_cb(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{
	struct nptv6_cfg *cfg;
	struct ip_fw_chain *ch;

	ch = (struct ip_fw_chain *)arg;
	IPFW_UH_WLOCK_ASSERT(ch);

	cfg = (struct nptv6_cfg *)SRV_OBJECT(ch, no->kidx);
	SRV_OBJECT(ch, no->kidx) = NULL;
	ipfw_objhash_del(ni, &cfg->no);
	ipfw_objhash_free_idx(ni, cfg->no.kidx);
	nptv6_free_config(cfg);
	return (0);
}

int
nptv6_init(struct ip_fw_chain *ch, int first)
{

	V_nptv6_eid = ipfw_add_eaction(ch, ipfw_nptv6, "nptv6");
	if (V_nptv6_eid == 0)
		return (ENXIO);
	IPFW_ADD_SOPT_HANDLER(first, scodes);
	IPFW_ADD_OBJ_REWRITER(first, opcodes);
	return (0);
}

void
nptv6_uninit(struct ip_fw_chain *ch, int last)
{

	if (last && nptv6_ifaddr_event != NULL)
		EVENTHANDLER_DEREGISTER(ifaddr_event_ext, nptv6_ifaddr_event);
	IPFW_DEL_OBJ_REWRITER(last, opcodes);
	IPFW_DEL_SOPT_HANDLER(last, scodes);
	ipfw_del_eaction(ch, V_nptv6_eid);
	/*
	 * Since we already have deregistered external action,
	 * our named objects become unaccessible via rules, because
	 * all rules were truncated by ipfw_del_eaction().
	 * So, we can unlink and destroy our named objects without holding
	 * IPFW_WLOCK().
	 */
	IPFW_UH_WLOCK(ch);
	ipfw_objhash_foreach_type(CHAIN_TO_SRV(ch), destroy_config_cb, ch,
	    IPFW_TLV_NPTV6_NAME);
	V_nptv6_eid = 0;
	IPFW_UH_WUNLOCK(ch);
}


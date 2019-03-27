/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2019 Yandex LLC
 * Copyright (c) 2015 Alexander V. Chernikov <melifaro@FreeBSD.org>
 * Copyright (c) 2016-2019 Andrey V. Elsukov <ae@FreeBSD.org>
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
#include <sys/ck.h>
#include <sys/epoch.h>
#include <sys/errno.h>
#include <sys/hash.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_pflog.h>
#include <net/pfil.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_fw.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#include <netinet6/ip_fw_nat64.h>

#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/pf/pf.h>

#include "nat64lsn.h"

MALLOC_DEFINE(M_NAT64LSN, "NAT64LSN", "NAT64LSN");

#define	NAT64LSN_EPOCH_ENTER(et)  NET_EPOCH_ENTER(et)
#define	NAT64LSN_EPOCH_EXIT(et)   NET_EPOCH_EXIT(et)
#define	NAT64LSN_EPOCH_ASSERT()   NET_EPOCH_ASSERT()
#define	NAT64LSN_EPOCH_CALL(c, f) epoch_call(net_epoch_preempt, (c), (f))

static uma_zone_t nat64lsn_host_zone;
static uma_zone_t nat64lsn_pgchunk_zone;
static uma_zone_t nat64lsn_pg_zone;
static uma_zone_t nat64lsn_aliaslink_zone;
static uma_zone_t nat64lsn_state_zone;
static uma_zone_t nat64lsn_job_zone;

static void nat64lsn_periodic(void *data);
#define	PERIODIC_DELAY		4
#define	NAT64_LOOKUP(chain, cmd)	\
	(struct nat64lsn_cfg *)SRV_OBJECT((chain), (cmd)->arg1)
/*
 * Delayed job queue, used to create new hosts
 * and new portgroups
 */
enum nat64lsn_jtype {
	JTYPE_NEWHOST = 1,
	JTYPE_NEWPORTGROUP,
	JTYPE_DESTROY,
};

struct nat64lsn_job_item {
	STAILQ_ENTRY(nat64lsn_job_item)	entries;
	enum nat64lsn_jtype	jtype;

	union {
		struct { /* used by JTYPE_NEWHOST, JTYPE_NEWPORTGROUP */
			struct mbuf		*m;
			struct nat64lsn_host	*host;
			struct nat64lsn_state	*state;
			uint32_t		src6_hval;
			uint32_t		state_hval;
			struct ipfw_flow_id	f_id;
			in_addr_t		faddr;
			uint16_t		port;
			uint8_t			proto;
			uint8_t			done;
		};
		struct { /* used by JTYPE_DESTROY */
			struct nat64lsn_hosts_slist	hosts;
			struct nat64lsn_pg_slist	portgroups;
			struct nat64lsn_pgchunk		*pgchunk;
			struct epoch_context		epoch_ctx;
		};
	};
};

static struct mtx jmtx;
#define	JQUEUE_LOCK_INIT()	mtx_init(&jmtx, "qlock", NULL, MTX_DEF)
#define	JQUEUE_LOCK_DESTROY()	mtx_destroy(&jmtx)
#define	JQUEUE_LOCK()		mtx_lock(&jmtx)
#define	JQUEUE_UNLOCK()		mtx_unlock(&jmtx)

static int nat64lsn_alloc_host(struct nat64lsn_cfg *cfg,
    struct nat64lsn_job_item *ji);
static int nat64lsn_alloc_pg(struct nat64lsn_cfg *cfg,
    struct nat64lsn_job_item *ji);
static struct nat64lsn_job_item *nat64lsn_create_job(
    struct nat64lsn_cfg *cfg, int jtype);
static void nat64lsn_enqueue_job(struct nat64lsn_cfg *cfg,
    struct nat64lsn_job_item *ji);
static void nat64lsn_job_destroy(epoch_context_t ctx);
static void nat64lsn_destroy_host(struct nat64lsn_host *host);
static void nat64lsn_destroy_pg(struct nat64lsn_pg *pg);

static int nat64lsn_translate4(struct nat64lsn_cfg *cfg,
    const struct ipfw_flow_id *f_id, struct mbuf **mp);
static int nat64lsn_translate6(struct nat64lsn_cfg *cfg,
    struct ipfw_flow_id *f_id, struct mbuf **mp);
static int nat64lsn_translate6_internal(struct nat64lsn_cfg *cfg,
    struct mbuf **mp, struct nat64lsn_state *state, uint8_t flags);

#define	NAT64_BIT_TCP_FIN	0	/* FIN was seen */
#define	NAT64_BIT_TCP_SYN	1	/* First syn in->out */
#define	NAT64_BIT_TCP_ESTAB	2	/* Packet with Ack */
#define	NAT64_BIT_READY_IPV4	6	/* state is ready for translate4 */
#define	NAT64_BIT_STALE		7	/* state is going to be expired */

#define	NAT64_FLAG_FIN		(1 << NAT64_BIT_TCP_FIN)
#define	NAT64_FLAG_SYN		(1 << NAT64_BIT_TCP_SYN)
#define	NAT64_FLAG_ESTAB	(1 << NAT64_BIT_TCP_ESTAB)
#define	NAT64_FLAGS_TCP	(NAT64_FLAG_SYN|NAT64_FLAG_ESTAB|NAT64_FLAG_FIN)

#define	NAT64_FLAG_READY	(1 << NAT64_BIT_READY_IPV4)
#define	NAT64_FLAG_STALE	(1 << NAT64_BIT_STALE)

static inline uint8_t
convert_tcp_flags(uint8_t flags)
{
	uint8_t result;

	result = flags & (TH_FIN|TH_SYN);
	result |= (flags & TH_RST) >> 2; /* Treat RST as FIN */
	result |= (flags & TH_ACK) >> 2; /* Treat ACK as estab */

	return (result);
}

static void
nat64lsn_log(struct pfloghdr *plog, struct mbuf *m, sa_family_t family,
    struct nat64lsn_state *state)
{

	memset(plog, 0, sizeof(*plog));
	plog->length = PFLOG_REAL_HDRLEN;
	plog->af = family;
	plog->action = PF_NAT;
	plog->dir = PF_IN;
	plog->rulenr = htonl(state->ip_src);
	plog->subrulenr = htonl((uint32_t)(state->aport << 16) |
	    (state->proto << 8) | (state->ip_dst & 0xff));
	plog->ruleset[0] = '\0';
	strlcpy(plog->ifname, "NAT64LSN", sizeof(plog->ifname));
	ipfw_bpf_mtap2(plog, PFLOG_HDRLEN, m);
}

#define	HVAL(p, n, s)	jenkins_hash32((const uint32_t *)(p), (n), (s))
#define	HOST_HVAL(c, a)	HVAL((a),\
    sizeof(struct in6_addr) / sizeof(uint32_t), (c)->hash_seed)
#define	HOSTS(c, v)	((c)->hosts_hash[(v) & ((c)->hosts_hashsize - 1)])

#define	ALIASLINK_HVAL(c, f)	HVAL(&(f)->dst_ip6,\
    sizeof(struct in6_addr) * 2 / sizeof(uint32_t), (c)->hash_seed)
#define	ALIAS_BYHASH(c, v)	\
    ((c)->aliases[(v) & ((1 << (32 - (c)->plen4)) - 1)])
static struct nat64lsn_aliaslink*
nat64lsn_get_aliaslink(struct nat64lsn_cfg *cfg __unused,
    struct nat64lsn_host *host, const struct ipfw_flow_id *f_id __unused)
{

	/*
	 * We can implement some different algorithms how
	 * select an alias address.
	 * XXX: for now we use first available.
	 */
	return (CK_SLIST_FIRST(&host->aliases));
}

#define	STATE_HVAL(c, d)	HVAL((d), 2, (c)->hash_seed)
#define	STATE_HASH(h, v)	\
    ((h)->states_hash[(v) & ((h)->states_hashsize - 1)])
#define	STATES_CHUNK(p, v)	\
    ((p)->chunks_count == 1 ? (p)->states : \
	((p)->states_chunk[CHUNK_BY_FADDR(p, v)]))

#ifdef __LP64__
#define	FREEMASK_FFSLL(pg, faddr)		\
    ffsll(*FREEMASK_CHUNK((pg), (faddr)))
#define	FREEMASK_BTR(pg, faddr, bit)	\
    ck_pr_btr_64(FREEMASK_CHUNK((pg), (faddr)), (bit))
#define	FREEMASK_BTS(pg, faddr, bit)	\
    ck_pr_bts_64(FREEMASK_CHUNK((pg), (faddr)), (bit))
#define	FREEMASK_ISSET(pg, faddr, bit)	\
    ISSET64(*FREEMASK_CHUNK((pg), (faddr)), (bit))
#define	FREEMASK_COPY(pg, n, out)	\
    (out) = ck_pr_load_64(FREEMASK_CHUNK((pg), (n)))
#else
static inline int
freemask_ffsll(uint32_t *freemask)
{
	int i;

	if ((i = ffsl(freemask[0])) != 0)
		return (i);
	if ((i = ffsl(freemask[1])) != 0)
		return (i + 32);
	return (0);
}
#define	FREEMASK_FFSLL(pg, faddr)		\
    freemask_ffsll(FREEMASK_CHUNK((pg), (faddr)))
#define	FREEMASK_BTR(pg, faddr, bit)	\
    ck_pr_btr_32(FREEMASK_CHUNK((pg), (faddr)) + (bit) / 32, (bit) % 32)
#define	FREEMASK_BTS(pg, faddr, bit)	\
    ck_pr_bts_32(FREEMASK_CHUNK((pg), (faddr)) + (bit) / 32, (bit) % 32)
#define	FREEMASK_ISSET(pg, faddr, bit)	\
    ISSET32(*(FREEMASK_CHUNK((pg), (faddr)) + (bit) / 32), (bit) % 32)
#define	FREEMASK_COPY(pg, n, out)	\
    (out) = ck_pr_load_32(FREEMASK_CHUNK((pg), (n))) | \
	((uint64_t)ck_pr_load_32(FREEMASK_CHUNK((pg), (n)) + 1) << 32)
#endif /* !__LP64__ */


#define	NAT64LSN_TRY_PGCNT	32
static struct nat64lsn_pg*
nat64lsn_get_pg(uint32_t *chunkmask, uint32_t *pgmask,
    struct nat64lsn_pgchunk **chunks, struct nat64lsn_pg **pgptr,
    uint32_t *pgidx, in_addr_t faddr)
{
	struct nat64lsn_pg *pg, *oldpg;
	uint32_t idx, oldidx;
	int cnt;

	cnt = 0;
	/* First try last used PG */
	oldpg = pg = ck_pr_load_ptr(pgptr);
	idx = oldidx = ck_pr_load_32(pgidx);
	/* If pgidx is out of range, reset it to the first pgchunk */
	if (!ISSET32(*chunkmask, idx / 32))
		idx = 0;
	do {
		ck_pr_fence_load();
		if (pg != NULL && FREEMASK_BITCOUNT(pg, faddr) > 0) {
			/*
			 * If last used PG has not free states,
			 * try to update pointer.
			 * NOTE: it can be already updated by jobs handler,
			 *	 thus we use CAS operation.
			 */
			if (cnt > 0)
				ck_pr_cas_ptr(pgptr, oldpg, pg);
			return (pg);
		}
		/* Stop if idx is out of range */
		if (!ISSET32(*chunkmask, idx / 32))
			break;

		if (ISSET32(pgmask[idx / 32], idx % 32))
			pg = ck_pr_load_ptr(
			    &chunks[idx / 32]->pgptr[idx % 32]);
		else
			pg = NULL;

		idx++;
	} while (++cnt < NAT64LSN_TRY_PGCNT);

	/* If pgidx is out of range, reset it to the first pgchunk */
	if (!ISSET32(*chunkmask, idx / 32))
		idx = 0;
	ck_pr_cas_32(pgidx, oldidx, idx);
	return (NULL);
}

static struct nat64lsn_state*
nat64lsn_get_state6to4(struct nat64lsn_cfg *cfg, struct nat64lsn_host *host,
    const struct ipfw_flow_id *f_id, uint32_t hval, in_addr_t faddr,
    uint16_t port, uint8_t proto)
{
	struct nat64lsn_aliaslink *link;
	struct nat64lsn_state *state;
	struct nat64lsn_pg *pg;
	int i, offset;

	NAT64LSN_EPOCH_ASSERT();

	/* Check that we already have state for given arguments */
	CK_SLIST_FOREACH(state, &STATE_HASH(host, hval), entries) {
		if (state->proto == proto && state->ip_dst == faddr &&
		    state->sport == port && state->dport == f_id->dst_port)
			return (state);
	}

	link = nat64lsn_get_aliaslink(cfg, host, f_id);
	if (link == NULL)
		return (NULL);

	switch (proto) {
	case IPPROTO_TCP:
		pg = nat64lsn_get_pg(
		    &link->alias->tcp_chunkmask, link->alias->tcp_pgmask,
		    link->alias->tcp, &link->alias->tcp_pg,
		    &link->alias->tcp_pgidx, faddr);
		break;
	case IPPROTO_UDP:
		pg = nat64lsn_get_pg(
		    &link->alias->udp_chunkmask, link->alias->udp_pgmask,
		    link->alias->udp, &link->alias->udp_pg,
		    &link->alias->udp_pgidx, faddr);
		break;
	case IPPROTO_ICMP:
		pg = nat64lsn_get_pg(
		    &link->alias->icmp_chunkmask, link->alias->icmp_pgmask,
		    link->alias->icmp, &link->alias->icmp_pg,
		    &link->alias->icmp_pgidx, faddr);
		break;
	default:
		panic("%s: wrong proto %d", __func__, proto);
	}
	if (pg == NULL)
		return (NULL);

	/* Check that PG has some free states */
	state = NULL;
	i = FREEMASK_BITCOUNT(pg, faddr);
	while (i-- > 0) {
		offset = FREEMASK_FFSLL(pg, faddr);
		if (offset == 0) {
			/*
			 * We lost the race.
			 * No more free states in this PG.
			 */
			break;
		}

		/* Lets try to atomically grab the state */
		if (FREEMASK_BTR(pg, faddr, offset - 1)) {
			state = &STATES_CHUNK(pg, faddr)->state[offset - 1];
			/* Initialize */
			state->flags = proto != IPPROTO_TCP ? 0 :
			    convert_tcp_flags(f_id->_flags);
			state->proto = proto;
			state->aport = pg->base_port + offset - 1;
			state->dport = f_id->dst_port;
			state->sport = port;
			state->ip6_dst = f_id->dst_ip6;
			state->ip_dst = faddr;
			state->ip_src = link->alias->addr;
			state->hval = hval;
			state->host = host;
			SET_AGE(state->timestamp);

			/* Insert new state into host's hash table */
			HOST_LOCK(host);
			CK_SLIST_INSERT_HEAD(&STATE_HASH(host, hval),
			    state, entries);
			host->states_count++;
			/*
			 * XXX: In case if host is going to be expired,
			 * reset NAT64LSN_DEADHOST flag.
			 */
			host->flags &= ~NAT64LSN_DEADHOST;
			HOST_UNLOCK(host);
			NAT64STAT_INC(&cfg->base.stats, screated);
			/* Mark the state as ready for translate4 */
			ck_pr_fence_store();
			ck_pr_bts_32(&state->flags, NAT64_BIT_READY_IPV4);
			break;
		}
	}
	return (state);
}

/*
 * Inspects icmp packets to see if the message contains different
 * packet header so we need to alter @addr and @port.
 */
static int
inspect_icmp_mbuf(struct mbuf **mp, uint8_t *proto, uint32_t *addr,
    uint16_t *port)
{
	struct icmp *icmp;
	struct ip *ip;
	int off;
	uint8_t inner_proto;

	ip = mtod(*mp, struct ip *); /* Outer IP header */
	off = (ip->ip_hl << 2) + ICMP_MINLEN;
	if ((*mp)->m_len < off)
		*mp = m_pullup(*mp, off);
	if (*mp == NULL)
		return (ENOMEM);

	ip = mtod(*mp, struct ip *); /* Outer IP header */
	icmp = L3HDR(ip, struct icmp *);
	switch (icmp->icmp_type) {
	case ICMP_ECHO:
	case ICMP_ECHOREPLY:
		/* Use icmp ID as distinguisher */
		*port = ntohs(icmp->icmp_id);
		return (0);
	case ICMP_UNREACH:
	case ICMP_TIMXCEED:
		break;
	default:
		return (EOPNOTSUPP);
	}
	/*
	 * ICMP_UNREACH and ICMP_TIMXCEED contains IP header + 64 bits
	 * of ULP header.
	 */
	if ((*mp)->m_pkthdr.len < off + sizeof(struct ip) + ICMP_MINLEN)
		return (EINVAL);
	if ((*mp)->m_len < off + sizeof(struct ip) + ICMP_MINLEN)
		*mp = m_pullup(*mp, off + sizeof(struct ip) + ICMP_MINLEN);
	if (*mp == NULL)
		return (ENOMEM);
	ip = mtodo(*mp, off); /* Inner IP header */
	inner_proto = ip->ip_p;
	off += ip->ip_hl << 2; /* Skip inner IP header */
	*addr = ntohl(ip->ip_src.s_addr);
	if ((*mp)->m_len < off + ICMP_MINLEN)
		*mp = m_pullup(*mp, off + ICMP_MINLEN);
	if (*mp == NULL)
		return (ENOMEM);
	switch (inner_proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		/* Copy source port from the header */
		*port = ntohs(*((uint16_t *)mtodo(*mp, off)));
		*proto = inner_proto;
		return (0);
	case IPPROTO_ICMP:
		/*
		 * We will translate only ICMP errors for our ICMP
		 * echo requests.
		 */
		icmp = mtodo(*mp, off);
		if (icmp->icmp_type != ICMP_ECHO)
			return (EOPNOTSUPP);
		*port = ntohs(icmp->icmp_id);
		return (0);
	};
	return (EOPNOTSUPP);
}

static struct nat64lsn_state*
nat64lsn_get_state4to6(struct nat64lsn_cfg *cfg, struct nat64lsn_alias *alias,
    in_addr_t faddr, uint16_t port, uint8_t proto)
{
	struct nat64lsn_state *state;
	struct nat64lsn_pg *pg;
	int chunk_idx, pg_idx, state_idx;

	NAT64LSN_EPOCH_ASSERT();

	if (port < NAT64_MIN_PORT)
		return (NULL);
	/*
	 * Alias keeps 32 pgchunks for each protocol.
	 * Each pgchunk has 32 pointers to portgroup.
	 * Each portgroup has 64 states for ports.
	 */
	port -= NAT64_MIN_PORT;
	chunk_idx = port / 2048;

	port -= chunk_idx * 2048;
	pg_idx = port / 64;
	state_idx = port % 64;

	/*
	 * First check in proto_chunkmask that we have allocated PG chunk.
	 * Then check in proto_pgmask that we have valid PG pointer.
	 */
	pg = NULL;
	switch (proto) {
	case IPPROTO_TCP:
		if (ISSET32(alias->tcp_chunkmask, chunk_idx) &&
		    ISSET32(alias->tcp_pgmask[chunk_idx], pg_idx)) {
			pg = alias->tcp[chunk_idx]->pgptr[pg_idx];
			break;
		}
		return (NULL);
	case IPPROTO_UDP:
		if (ISSET32(alias->udp_chunkmask, chunk_idx) &&
		    ISSET32(alias->udp_pgmask[chunk_idx], pg_idx)) {
			pg = alias->udp[chunk_idx]->pgptr[pg_idx];
			break;
		}
		return (NULL);
	case IPPROTO_ICMP:
		if (ISSET32(alias->icmp_chunkmask, chunk_idx) &&
		    ISSET32(alias->icmp_pgmask[chunk_idx], pg_idx)) {
			pg = alias->icmp[chunk_idx]->pgptr[pg_idx];
			break;
		}
		return (NULL);
	default:
		panic("%s: wrong proto %d", __func__, proto);
	}
	if (pg == NULL)
		return (NULL);

	if (FREEMASK_ISSET(pg, faddr, state_idx))
		return (NULL);

	state = &STATES_CHUNK(pg, faddr)->state[state_idx];
	ck_pr_fence_load();
	if (ck_pr_load_32(&state->flags) & NAT64_FLAG_READY)
		return (state);
	return (NULL);
}

static int
nat64lsn_translate4(struct nat64lsn_cfg *cfg,
    const struct ipfw_flow_id *f_id, struct mbuf **mp)
{
	struct pfloghdr loghdr, *logdata;
	struct in6_addr src6;
	struct nat64lsn_state *state;
	struct nat64lsn_alias *alias;
	uint32_t addr, flags;
	uint16_t port, ts;
	int ret;
	uint8_t proto;

	addr = f_id->dst_ip;
	port = f_id->dst_port;
	proto = f_id->proto;
	if (addr < cfg->prefix4 || addr > cfg->pmask4) {
		NAT64STAT_INC(&cfg->base.stats, nomatch4);
		return (cfg->nomatch_verdict);
	}

	/* Check if protocol is supported */
	switch (proto) {
	case IPPROTO_ICMP:
		ret = inspect_icmp_mbuf(mp, &proto, &addr, &port);
		if (ret != 0) {
			if (ret == ENOMEM) {
				NAT64STAT_INC(&cfg->base.stats, nomem);
				return (IP_FW_DENY);
			}
			NAT64STAT_INC(&cfg->base.stats, noproto);
			return (cfg->nomatch_verdict);
		}
		if (addr < cfg->prefix4 || addr > cfg->pmask4) {
			NAT64STAT_INC(&cfg->base.stats, nomatch4);
			return (cfg->nomatch_verdict);
		}
		/* FALLTHROUGH */
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		break;
	default:
		NAT64STAT_INC(&cfg->base.stats, noproto);
		return (cfg->nomatch_verdict);
	}

	alias = &ALIAS_BYHASH(cfg, addr);
	MPASS(addr == alias->addr);

	/* Check that we have state for this port */
	state = nat64lsn_get_state4to6(cfg, alias, f_id->src_ip,
	    port, proto);
	if (state == NULL) {
		NAT64STAT_INC(&cfg->base.stats, nomatch4);
		return (cfg->nomatch_verdict);
	}

	/* TODO: Check flags to see if we need to do some static mapping */

	/* Update some state fields if need */
	SET_AGE(ts);
	if (f_id->proto == IPPROTO_TCP)
		flags = convert_tcp_flags(f_id->_flags);
	else
		flags = 0;
	if (state->timestamp != ts)
		state->timestamp = ts;
	if ((state->flags & flags) != flags)
		state->flags |= flags;

	port = htons(state->sport);
	src6 = state->ip6_dst;

	if (cfg->base.flags & NAT64_LOG) {
		logdata = &loghdr;
		nat64lsn_log(logdata, *mp, AF_INET, state);
	} else
		logdata = NULL;

	/*
	 * We already have src6 with embedded address, but it is possible,
	 * that src_ip is different than state->ip_dst, this is why we
	 * do embedding again.
	 */
	nat64_embed_ip4(&src6, cfg->base.plat_plen, htonl(f_id->src_ip));
	ret = nat64_do_handle_ip4(*mp, &src6, &state->host->addr, port,
	    &cfg->base, logdata);
	if (ret == NAT64SKIP)
		return (cfg->nomatch_verdict);
	if (ret == NAT64RETURN)
		*mp = NULL;
	return (IP_FW_DENY);
}

/*
 * Check if particular state is stale and should be deleted.
 * Return 1 if true, 0 otherwise.
 */
static int
nat64lsn_check_state(struct nat64lsn_cfg *cfg, struct nat64lsn_state *state)
{
	int age, ttl;

	/* State was marked as stale in previous pass. */
	if (ISSET32(state->flags, NAT64_BIT_STALE))
		return (1);

	/* State is not yet initialized, it is going to be READY */
	if (!ISSET32(state->flags, NAT64_BIT_READY_IPV4))
		return (0);

	age = GET_AGE(state->timestamp);
	switch (state->proto) {
	case IPPROTO_TCP:
		if (ISSET32(state->flags, NAT64_BIT_TCP_FIN))
			ttl = cfg->st_close_ttl;
		else if (ISSET32(state->flags, NAT64_BIT_TCP_ESTAB))
			ttl = cfg->st_estab_ttl;
		else if (ISSET32(state->flags, NAT64_BIT_TCP_SYN))
			ttl = cfg->st_syn_ttl;
		else
			ttl = cfg->st_syn_ttl;
		if (age > ttl)
			return (1);
		break;
	case IPPROTO_UDP:
		if (age > cfg->st_udp_ttl)
			return (1);
		break;
	case IPPROTO_ICMP:
		if (age > cfg->st_icmp_ttl)
			return (1);
		break;
	}
	return (0);
}

static int
nat64lsn_maintain_pg(struct nat64lsn_cfg *cfg, struct nat64lsn_pg *pg)
{
	struct nat64lsn_state *state;
	struct nat64lsn_host *host;
	uint64_t freemask;
	int c, i, update_age;

	update_age = 0;
	for (c = 0; c < pg->chunks_count; c++) {
		FREEMASK_COPY(pg, c, freemask);
		for (i = 0; i < 64; i++) {
			if (ISSET64(freemask, i))
				continue;
			state = &STATES_CHUNK(pg, c)->state[i];
			if (nat64lsn_check_state(cfg, state) == 0) {
				update_age = 1;
				continue;
			}
			/*
			 * Expire state:
			 * 1. Mark as STALE and unlink from host's hash.
			 * 2. Set bit in freemask.
			 */
			if (ISSET32(state->flags, NAT64_BIT_STALE)) {
				/*
				 * State was marked as STALE in previous
				 * pass. Now it is safe to release it.
				 */
				state->flags = 0;
				ck_pr_fence_store();
				FREEMASK_BTS(pg, c, i);
				NAT64STAT_INC(&cfg->base.stats, sdeleted);
				continue;
			}
			MPASS(state->flags & NAT64_FLAG_READY);

			host = state->host;
			HOST_LOCK(host);
			CK_SLIST_REMOVE(&STATE_HASH(host, state->hval),
			    state, nat64lsn_state, entries);
			host->states_count--;
			HOST_UNLOCK(host);

			/* Reset READY flag */
			ck_pr_btr_32(&state->flags, NAT64_BIT_READY_IPV4);
			/* And set STALE flag */
			ck_pr_bts_32(&state->flags, NAT64_BIT_STALE);
			ck_pr_fence_store();
			/*
			 * Now translate6 will not use this state, wait
			 * until it become safe for translate4, then mark
			 * state as free.
			 */
		}
	}

	/*
	 * We have some alive states, update timestamp.
	 */
	if (update_age)
		SET_AGE(pg->timestamp);

	if (GET_AGE(pg->timestamp) < cfg->pg_delete_delay)
		return (0);

	return (1);
}

static void
nat64lsn_expire_portgroups(struct nat64lsn_cfg *cfg,
    struct nat64lsn_pg_slist *portgroups)
{
	struct nat64lsn_alias *alias;
	struct nat64lsn_pg *pg, *tpg, *firstpg, **pgptr;
	uint32_t *pgmask, *pgidx;
	int i, idx;

	for (i = 0; i < 1 << (32 - cfg->plen4); i++) {
		alias = &cfg->aliases[i];
		CK_SLIST_FOREACH_SAFE(pg, &alias->portgroups, entries, tpg) {
			if (nat64lsn_maintain_pg(cfg, pg) == 0)
				continue;
			/* Always keep first PG */
			if (pg->base_port == NAT64_MIN_PORT)
				continue;
			/*
			 * PG is expired, unlink it and schedule for
			 * deferred destroying.
			 */
			idx = (pg->base_port - NAT64_MIN_PORT) / 64;
			switch (pg->proto) {
			case IPPROTO_TCP:
				pgmask = alias->tcp_pgmask;
				pgptr = &alias->tcp_pg;
				pgidx = &alias->tcp_pgidx;
				firstpg = alias->tcp[0]->pgptr[0];
				break;
			case IPPROTO_UDP:
				pgmask = alias->udp_pgmask;
				pgptr = &alias->udp_pg;
				pgidx = &alias->udp_pgidx;
				firstpg = alias->udp[0]->pgptr[0];
				break;
			case IPPROTO_ICMP:
				pgmask = alias->icmp_pgmask;
				pgptr = &alias->icmp_pg;
				pgidx = &alias->icmp_pgidx;
				firstpg = alias->icmp[0]->pgptr[0];
				break;
			}
			/* Reset the corresponding bit in pgmask array. */
			ck_pr_btr_32(&pgmask[idx / 32], idx % 32);
			ck_pr_fence_store();
			/* If last used PG points to this PG, reset it. */
			ck_pr_cas_ptr(pgptr, pg, firstpg);
			ck_pr_cas_32(pgidx, idx, 0);
			/* Unlink PG from alias's chain */
			ALIAS_LOCK(alias);
			CK_SLIST_REMOVE(&alias->portgroups, pg,
			    nat64lsn_pg, entries);
			alias->portgroups_count--;
			ALIAS_UNLOCK(alias);
			/* And link to job's chain for deferred destroying */
			NAT64STAT_INC(&cfg->base.stats, spgdeleted);
			CK_SLIST_INSERT_HEAD(portgroups, pg, entries);
		}
	}
}

static void
nat64lsn_expire_hosts(struct nat64lsn_cfg *cfg,
    struct nat64lsn_hosts_slist *hosts)
{
	struct nat64lsn_host *host, *tmp;
	int i;

	for (i = 0; i < cfg->hosts_hashsize; i++) {
		CK_SLIST_FOREACH_SAFE(host, &cfg->hosts_hash[i],
		    entries, tmp) {
			/* Is host was marked in previous call? */
			if (host->flags & NAT64LSN_DEADHOST) {
				if (host->states_count > 0) {
					host->flags &= ~NAT64LSN_DEADHOST;
					continue;
				}
				/*
				 * Unlink host from hash table and schedule
				 * it for deferred destroying.
				 */
				CFG_LOCK(cfg);
				CK_SLIST_REMOVE(&cfg->hosts_hash[i], host,
				    nat64lsn_host, entries);
				cfg->hosts_count--;
				CFG_UNLOCK(cfg);
				CK_SLIST_INSERT_HEAD(hosts, host, entries);
				continue;
			}
			if (GET_AGE(host->timestamp) < cfg->host_delete_delay)
				continue;
			if (host->states_count > 0)
				continue;
			/* Mark host as going to be expired in next pass */
			host->flags |= NAT64LSN_DEADHOST;
			ck_pr_fence_store();
		}
	}
}

static struct nat64lsn_pgchunk*
nat64lsn_expire_pgchunk(struct nat64lsn_cfg *cfg)
{
#if 0
	struct nat64lsn_alias *alias;
	struct nat64lsn_pgchunk *chunk;
	uint32_t pgmask;
	int i, c;

	for (i = 0; i < 1 << (32 - cfg->plen4); i++) {
		alias = &cfg->aliases[i];
		if (GET_AGE(alias->timestamp) < cfg->pgchunk_delete_delay)
			continue;
		/* Always keep single chunk allocated */
		for (c = 1; c < 32; c++) {
			if ((alias->tcp_chunkmask & (1 << c)) == 0)
				break;
			chunk = ck_pr_load_ptr(&alias->tcp[c]);
			if (ck_pr_load_32(&alias->tcp_pgmask[c]) != 0)
				continue;
			ck_pr_btr_32(&alias->tcp_chunkmask, c);
			ck_pr_fence_load();
			if (ck_pr_load_32(&alias->tcp_pgmask[c]) != 0)
				continue;
		}
	}
#endif
	return (NULL);
}

#if 0
static void
nat64lsn_maintain_hosts(struct nat64lsn_cfg *cfg)
{
	struct nat64lsn_host *h;
	struct nat64lsn_states_slist *hash;
	int i, j, hsize;

	for (i = 0; i < cfg->hosts_hashsize; i++) {
		CK_SLIST_FOREACH(h, &cfg->hosts_hash[i], entries) {
			 if (h->states_count / 2 < h->states_hashsize ||
			     h->states_hashsize >= NAT64LSN_MAX_HSIZE)
				 continue;
			 hsize = h->states_hashsize * 2;
			 hash = malloc(sizeof(*hash)* hsize, M_NOWAIT);
			 if (hash == NULL)
				 continue;
			 for (j = 0; j < hsize; j++)
				CK_SLIST_INIT(&hash[i]);

			 ck_pr_bts_32(&h->flags, NAT64LSN_GROWHASH);
		}
	}
}
#endif

/*
 * This procedure is used to perform various maintance
 * on dynamic hash list. Currently it is called every 4 seconds.
 */
static void
nat64lsn_periodic(void *data)
{
	struct nat64lsn_job_item *ji;
	struct nat64lsn_cfg *cfg;

	cfg = (struct nat64lsn_cfg *) data;
	CURVNET_SET(cfg->vp);
	if (cfg->hosts_count > 0) {
		ji = uma_zalloc(nat64lsn_job_zone, M_NOWAIT);
		if (ji != NULL) {
			ji->jtype = JTYPE_DESTROY;
			CK_SLIST_INIT(&ji->hosts);
			CK_SLIST_INIT(&ji->portgroups);
			nat64lsn_expire_hosts(cfg, &ji->hosts);
			nat64lsn_expire_portgroups(cfg, &ji->portgroups);
			ji->pgchunk = nat64lsn_expire_pgchunk(cfg);
			NAT64LSN_EPOCH_CALL(&ji->epoch_ctx,
			    nat64lsn_job_destroy);
		} else
			NAT64STAT_INC(&cfg->base.stats, jnomem);
	}
	callout_schedule(&cfg->periodic, hz * PERIODIC_DELAY);
	CURVNET_RESTORE();
}

#define	ALLOC_ERROR(stage, type)	((stage) ? 10 * (type) + (stage): 0)
#define	HOST_ERROR(stage)		ALLOC_ERROR(stage, 1)
#define	PG_ERROR(stage)			ALLOC_ERROR(stage, 2)
static int
nat64lsn_alloc_host(struct nat64lsn_cfg *cfg, struct nat64lsn_job_item *ji)
{
	char a[INET6_ADDRSTRLEN];
	struct nat64lsn_aliaslink *link;
	struct nat64lsn_host *host;
	struct nat64lsn_state *state;
	uint32_t hval, data[2];
	int i;

	/* Check that host was not yet added. */
	NAT64LSN_EPOCH_ASSERT();
	CK_SLIST_FOREACH(host, &HOSTS(cfg, ji->src6_hval), entries) {
		if (IN6_ARE_ADDR_EQUAL(&ji->f_id.src_ip6, &host->addr)) {
			/* The host was allocated in previous call. */
			ji->host = host;
			goto get_state;
		}
	}

	host = ji->host = uma_zalloc(nat64lsn_host_zone, M_NOWAIT);
	if (ji->host == NULL)
		return (HOST_ERROR(1));

	host->states_hashsize = NAT64LSN_HSIZE;
	host->states_hash = malloc(sizeof(struct nat64lsn_states_slist) *
	    host->states_hashsize, M_NAT64LSN, M_NOWAIT);
	if (host->states_hash == NULL) {
		uma_zfree(nat64lsn_host_zone, host);
		return (HOST_ERROR(2));
	}

	link = uma_zalloc(nat64lsn_aliaslink_zone, M_NOWAIT);
	if (link == NULL) {
		free(host->states_hash, M_NAT64LSN);
		uma_zfree(nat64lsn_host_zone, host);
		return (HOST_ERROR(3));
	}

	/* Initialize */
	HOST_LOCK_INIT(host);
	SET_AGE(host->timestamp);
	host->addr = ji->f_id.src_ip6;
	host->hval = ji->src6_hval;
	host->flags = 0;
	host->states_count = 0;
	host->states_hashsize = NAT64LSN_HSIZE;
	CK_SLIST_INIT(&host->aliases);
	for (i = 0; i < host->states_hashsize; i++)
		CK_SLIST_INIT(&host->states_hash[i]);

	/* Determine alias from flow hash. */
	hval = ALIASLINK_HVAL(cfg, &ji->f_id);
	link->alias = &ALIAS_BYHASH(cfg, hval);
	CK_SLIST_INSERT_HEAD(&host->aliases, link, host_entries);

	ALIAS_LOCK(link->alias);
	CK_SLIST_INSERT_HEAD(&link->alias->hosts, link, alias_entries);
	link->alias->hosts_count++;
	ALIAS_UNLOCK(link->alias);

	CFG_LOCK(cfg);
	CK_SLIST_INSERT_HEAD(&HOSTS(cfg, ji->src6_hval), host, entries);
	cfg->hosts_count++;
	CFG_UNLOCK(cfg);

get_state:
	data[0] = ji->faddr;
	data[1] = (ji->f_id.dst_port << 16) | ji->port;
	ji->state_hval = hval = STATE_HVAL(cfg, data);
	state = nat64lsn_get_state6to4(cfg, host, &ji->f_id, hval,
	    ji->faddr, ji->port, ji->proto);
	/*
	 * We failed to obtain new state, used alias needs new PG.
	 * XXX: or another alias should be used.
	 */
	if (state == NULL) {
		/* Try to allocate new PG */
		if (nat64lsn_alloc_pg(cfg, ji) != PG_ERROR(0))
			return (HOST_ERROR(4));
		/* We assume that nat64lsn_alloc_pg() got state */
	} else
		ji->state = state;

	ji->done = 1;
	DPRINTF(DP_OBJ, "ALLOC HOST %s %p",
	    inet_ntop(AF_INET6, &host->addr, a, sizeof(a)), host);
	return (HOST_ERROR(0));
}

static int
nat64lsn_find_pg_place(uint32_t *data)
{
	int i;

	for (i = 0; i < 32; i++) {
		if (~data[i] == 0)
			continue;
		return (i * 32 + ffs(~data[i]) - 1);
	}
	return (-1);
}

static int
nat64lsn_alloc_proto_pg(struct nat64lsn_cfg *cfg,
    struct nat64lsn_alias *alias, uint32_t *chunkmask,
    uint32_t *pgmask, struct nat64lsn_pgchunk **chunks,
    struct nat64lsn_pg **pgptr, uint8_t proto)
{
	struct nat64lsn_pg *pg;
	int i, pg_idx, chunk_idx;

	/* Find place in pgchunk where PG can be added */
	pg_idx = nat64lsn_find_pg_place(pgmask);
	if (pg_idx < 0)	/* no more PGs */
		return (PG_ERROR(1));
	/* Check that we have allocated pgchunk for given PG index */
	chunk_idx = pg_idx / 32;
	if (!ISSET32(*chunkmask, chunk_idx)) {
		chunks[chunk_idx] = uma_zalloc(nat64lsn_pgchunk_zone,
		    M_NOWAIT);
		if (chunks[chunk_idx] == NULL)
			return (PG_ERROR(2));
		ck_pr_bts_32(chunkmask, chunk_idx);
		ck_pr_fence_store();
	}
	/* Allocate PG and states chunks */
	pg = uma_zalloc(nat64lsn_pg_zone, M_NOWAIT);
	if (pg == NULL)
		return (PG_ERROR(3));
	pg->chunks_count = cfg->states_chunks;
	if (pg->chunks_count > 1) {
		pg->freemask_chunk = malloc(pg->chunks_count *
		    sizeof(uint64_t), M_NAT64LSN, M_NOWAIT);
		if (pg->freemask_chunk == NULL) {
			uma_zfree(nat64lsn_pg_zone, pg);
			return (PG_ERROR(4));
		}
		pg->states_chunk = malloc(pg->chunks_count *
		    sizeof(struct nat64lsn_states_chunk *), M_NAT64LSN,
		    M_NOWAIT | M_ZERO);
		if (pg->states_chunk == NULL) {
			free(pg->freemask_chunk, M_NAT64LSN);
			uma_zfree(nat64lsn_pg_zone, pg);
			return (PG_ERROR(5));
		}
		for (i = 0; i < pg->chunks_count; i++) {
			pg->states_chunk[i] = uma_zalloc(
			    nat64lsn_state_zone, M_NOWAIT);
			if (pg->states_chunk[i] == NULL)
				goto states_failed;
		}
		memset(pg->freemask_chunk, 0xff,
		    sizeof(uint64_t) * pg->chunks_count);
	} else {
		pg->states = uma_zalloc(nat64lsn_state_zone, M_NOWAIT);
		if (pg->states == NULL) {
			uma_zfree(nat64lsn_pg_zone, pg);
			return (PG_ERROR(6));
		}
		memset(&pg->freemask64, 0xff, sizeof(uint64_t));
	}

	/* Initialize PG and hook it to pgchunk */
	SET_AGE(pg->timestamp);
	pg->proto = proto;
	pg->base_port = NAT64_MIN_PORT + 64 * pg_idx;
	ck_pr_store_ptr(&chunks[chunk_idx]->pgptr[pg_idx % 32], pg);
	ck_pr_fence_store();
	ck_pr_bts_32(&pgmask[pg_idx / 32], pg_idx % 32);
	ck_pr_store_ptr(pgptr, pg);

	ALIAS_LOCK(alias);
	CK_SLIST_INSERT_HEAD(&alias->portgroups, pg, entries);
	SET_AGE(alias->timestamp);
	alias->portgroups_count++;
	ALIAS_UNLOCK(alias);
	NAT64STAT_INC(&cfg->base.stats, spgcreated);
	return (PG_ERROR(0));

states_failed:
	for (i = 0; i < pg->chunks_count; i++)
		uma_zfree(nat64lsn_state_zone, pg->states_chunk[i]);
	free(pg->freemask_chunk, M_NAT64LSN);
	free(pg->states_chunk, M_NAT64LSN);
	uma_zfree(nat64lsn_pg_zone, pg);
	return (PG_ERROR(7));
}

static int
nat64lsn_alloc_pg(struct nat64lsn_cfg *cfg, struct nat64lsn_job_item *ji)
{
	struct nat64lsn_aliaslink *link;
	struct nat64lsn_alias *alias;
	int ret;

	link = nat64lsn_get_aliaslink(cfg, ji->host, &ji->f_id);
	if (link == NULL)
		return (PG_ERROR(1));

	/*
	 * TODO: check that we did not already allocated PG in
	 *	 previous call.
	 */

	ret = 0;
	alias = link->alias;
	/* Find place in pgchunk where PG can be added */
	switch (ji->proto) {
	case IPPROTO_TCP:
		ret = nat64lsn_alloc_proto_pg(cfg, alias,
		    &alias->tcp_chunkmask, alias->tcp_pgmask,
		    alias->tcp, &alias->tcp_pg, ji->proto);
		break;
	case IPPROTO_UDP:
		ret = nat64lsn_alloc_proto_pg(cfg, alias,
		    &alias->udp_chunkmask, alias->udp_pgmask,
		    alias->udp, &alias->udp_pg, ji->proto);
		break;
	case IPPROTO_ICMP:
		ret = nat64lsn_alloc_proto_pg(cfg, alias,
		    &alias->icmp_chunkmask, alias->icmp_pgmask,
		    alias->icmp, &alias->icmp_pg, ji->proto);
		break;
	default:
		panic("%s: wrong proto %d", __func__, ji->proto);
	}
	if (ret == PG_ERROR(1)) {
		/*
		 * PG_ERROR(1) means that alias lacks free PGs
		 * XXX: try next alias.
		 */
		printf("NAT64LSN: %s: failed to obtain PG\n",
		    __func__);
		return (ret);
	}
	if (ret == PG_ERROR(0)) {
		ji->state = nat64lsn_get_state6to4(cfg, ji->host, &ji->f_id,
		    ji->state_hval, ji->faddr, ji->port, ji->proto);
		if (ji->state == NULL)
			ret = PG_ERROR(8);
		else
			ji->done = 1;
	}
	return (ret);
}

static void
nat64lsn_do_request(void *data)
{
	struct epoch_tracker et;
	struct nat64lsn_job_head jhead;
	struct nat64lsn_job_item *ji, *ji2;
	struct nat64lsn_cfg *cfg;
	int jcount;
	uint8_t flags;

	cfg = (struct nat64lsn_cfg *)data;
	if (cfg->jlen == 0)
		return;

	CURVNET_SET(cfg->vp);
	STAILQ_INIT(&jhead);

	/* Grab queue */
	JQUEUE_LOCK();
	STAILQ_SWAP(&jhead, &cfg->jhead, nat64lsn_job_item);
	jcount = cfg->jlen;
	cfg->jlen = 0;
	JQUEUE_UNLOCK();

	/* TODO: check if we need to resize hash */

	NAT64STAT_INC(&cfg->base.stats, jcalls);
	DPRINTF(DP_JQUEUE, "count=%d", jcount);

	/*
	 * TODO:
	 * What we should do here is to build a hash
	 * to ensure we don't have lots of duplicate requests.
	 * Skip this for now.
	 *
	 * TODO: Limit per-call number of items
	 */

	NAT64LSN_EPOCH_ENTER(et);
	STAILQ_FOREACH(ji, &jhead, entries) {
		switch (ji->jtype) {
		case JTYPE_NEWHOST:
			if (nat64lsn_alloc_host(cfg, ji) != HOST_ERROR(0))
				NAT64STAT_INC(&cfg->base.stats, jhostfails);
			break;
		case JTYPE_NEWPORTGROUP:
			if (nat64lsn_alloc_pg(cfg, ji) != PG_ERROR(0))
				NAT64STAT_INC(&cfg->base.stats, jportfails);
			break;
		default:
			continue;
		}
		if (ji->done != 0) {
			flags = ji->proto != IPPROTO_TCP ? 0 :
			    convert_tcp_flags(ji->f_id._flags);
			nat64lsn_translate6_internal(cfg, &ji->m,
			    ji->state, flags);
			NAT64STAT_INC(&cfg->base.stats, jreinjected);
		}
	}
	NAT64LSN_EPOCH_EXIT(et);

	ji = STAILQ_FIRST(&jhead);
	while (ji != NULL) {
		ji2 = STAILQ_NEXT(ji, entries);
		/*
		 * In any case we must free mbuf if
		 * translator did not consumed it.
		 */
		m_freem(ji->m);
		uma_zfree(nat64lsn_job_zone, ji);
		ji = ji2;
	}
	CURVNET_RESTORE();
}

static struct nat64lsn_job_item *
nat64lsn_create_job(struct nat64lsn_cfg *cfg, int jtype)
{
	struct nat64lsn_job_item *ji;

	/*
	 * Do not try to lock possibly contested mutex if we're near the
	 * limit. Drop packet instead.
	 */
	ji = NULL;
	if (cfg->jlen >= cfg->jmaxlen)
		NAT64STAT_INC(&cfg->base.stats, jmaxlen);
	else {
		ji = uma_zalloc(nat64lsn_job_zone, M_NOWAIT);
		if (ji == NULL)
			NAT64STAT_INC(&cfg->base.stats, jnomem);
	}
	if (ji == NULL) {
		NAT64STAT_INC(&cfg->base.stats, dropped);
		DPRINTF(DP_DROPS, "failed to create job");
	} else {
		ji->jtype = jtype;
		ji->done = 0;
	}
	return (ji);
}

static void
nat64lsn_enqueue_job(struct nat64lsn_cfg *cfg, struct nat64lsn_job_item *ji)
{

	JQUEUE_LOCK();
	STAILQ_INSERT_TAIL(&cfg->jhead, ji, entries);
	NAT64STAT_INC(&cfg->base.stats, jrequests);
	cfg->jlen++;

	if (callout_pending(&cfg->jcallout) == 0)
		callout_reset(&cfg->jcallout, 1, nat64lsn_do_request, cfg);
	JQUEUE_UNLOCK();
}

static void
nat64lsn_job_destroy(epoch_context_t ctx)
{
	struct nat64lsn_job_item *ji;
	struct nat64lsn_host *host;
	struct nat64lsn_pg *pg;
	int i;

	ji = __containerof(ctx, struct nat64lsn_job_item, epoch_ctx);
	MPASS(ji->jtype == JTYPE_DESTROY);
	while (!CK_SLIST_EMPTY(&ji->hosts)) {
		host = CK_SLIST_FIRST(&ji->hosts);
		CK_SLIST_REMOVE_HEAD(&ji->hosts, entries);
		if (host->states_count > 0) {
			/*
			 * XXX: The state has been created
			 * during host deletion.
			 */
			printf("NAT64LSN: %s: destroying host with %d "
			    "states\n", __func__, host->states_count);
		}
		nat64lsn_destroy_host(host);
	}
	while (!CK_SLIST_EMPTY(&ji->portgroups)) {
		pg = CK_SLIST_FIRST(&ji->portgroups);
		CK_SLIST_REMOVE_HEAD(&ji->portgroups, entries);
		for (i = 0; i < pg->chunks_count; i++) {
			if (FREEMASK_BITCOUNT(pg, i) != 64) {
				/*
				 * XXX: The state has been created during
				 * PG deletion.
				 */
				printf("NAT64LSN: %s: destroying PG %p "
				    "with non-empty chunk %d\n", __func__,
				    pg, i);
			}
		}
		nat64lsn_destroy_pg(pg);
	}
	uma_zfree(nat64lsn_pgchunk_zone, ji->pgchunk);
	uma_zfree(nat64lsn_job_zone, ji);
}

static int
nat64lsn_request_host(struct nat64lsn_cfg *cfg,
    const struct ipfw_flow_id *f_id, struct mbuf **mp, uint32_t hval,
    in_addr_t faddr, uint16_t port, uint8_t proto)
{
	struct nat64lsn_job_item *ji;

	ji = nat64lsn_create_job(cfg, JTYPE_NEWHOST);
	if (ji != NULL) {
		ji->m = *mp;
		ji->f_id = *f_id;
		ji->faddr = faddr;
		ji->port = port;
		ji->proto = proto;
		ji->src6_hval = hval;

		nat64lsn_enqueue_job(cfg, ji);
		NAT64STAT_INC(&cfg->base.stats, jhostsreq);
		*mp = NULL;
	}
	return (IP_FW_DENY);
}

static int
nat64lsn_request_pg(struct nat64lsn_cfg *cfg, struct nat64lsn_host *host,
    const struct ipfw_flow_id *f_id, struct mbuf **mp, uint32_t hval,
    in_addr_t faddr, uint16_t port, uint8_t proto)
{
	struct nat64lsn_job_item *ji;

	ji = nat64lsn_create_job(cfg, JTYPE_NEWPORTGROUP);
	if (ji != NULL) {
		ji->m = *mp;
		ji->f_id = *f_id;
		ji->faddr = faddr;
		ji->port = port;
		ji->proto = proto;
		ji->state_hval = hval;
		ji->host = host;

		nat64lsn_enqueue_job(cfg, ji);
		NAT64STAT_INC(&cfg->base.stats, jportreq);
		*mp = NULL;
	}
	return (IP_FW_DENY);
}

static int
nat64lsn_translate6_internal(struct nat64lsn_cfg *cfg, struct mbuf **mp,
    struct nat64lsn_state *state, uint8_t flags)
{
	struct pfloghdr loghdr, *logdata;
	int ret;
	uint16_t ts;

	/* Update timestamp and flags if needed */
	SET_AGE(ts);
	if (state->timestamp != ts)
		state->timestamp = ts;
	if ((state->flags & flags) != 0)
		state->flags |= flags;

	if (cfg->base.flags & NAT64_LOG) {
		logdata = &loghdr;
		nat64lsn_log(logdata, *mp, AF_INET6, state);
	} else
		logdata = NULL;

	ret = nat64_do_handle_ip6(*mp, htonl(state->ip_src),
	    htons(state->aport), &cfg->base, logdata);
	if (ret == NAT64SKIP)
		return (cfg->nomatch_verdict);
	if (ret == NAT64RETURN)
		*mp = NULL;
	return (IP_FW_DENY);
}

static int
nat64lsn_translate6(struct nat64lsn_cfg *cfg, struct ipfw_flow_id *f_id,
    struct mbuf **mp)
{
	struct nat64lsn_state *state;
	struct nat64lsn_host *host;
	struct icmp6_hdr *icmp6;
	uint32_t addr, hval, data[2];
	int offset, proto;
	uint16_t port;
	uint8_t flags;

	/* Check if protocol is supported */
	port = f_id->src_port;
	proto = f_id->proto;
	switch (f_id->proto) {
	case IPPROTO_ICMPV6:
		/*
		 * For ICMPv6 echo reply/request we use icmp6_id as
		 * local port.
		 */
		offset = 0;
		proto = nat64_getlasthdr(*mp, &offset);
		if (proto < 0) {
			NAT64STAT_INC(&cfg->base.stats, dropped);
			DPRINTF(DP_DROPS, "mbuf isn't contigious");
			return (IP_FW_DENY);
		}
		if (proto == IPPROTO_ICMPV6) {
			icmp6 = mtodo(*mp, offset);
			if (icmp6->icmp6_type == ICMP6_ECHO_REQUEST ||
			    icmp6->icmp6_type == ICMP6_ECHO_REPLY)
				port = ntohs(icmp6->icmp6_id);
		}
		proto = IPPROTO_ICMP;
		/* FALLTHROUGH */
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		break;
	default:
		NAT64STAT_INC(&cfg->base.stats, noproto);
		return (cfg->nomatch_verdict);
	}

	/* Extract IPv4 from destination IPv6 address */
	addr = nat64_extract_ip4(&f_id->dst_ip6, cfg->base.plat_plen);
	if (addr == 0 || nat64_check_private_ip4(&cfg->base, addr) != 0) {
		char a[INET_ADDRSTRLEN];

		NAT64STAT_INC(&cfg->base.stats, dropped);
		DPRINTF(DP_DROPS, "dropped due to embedded IPv4 address %s",
		    inet_ntop(AF_INET, &addr, a, sizeof(a)));
		return (IP_FW_DENY); /* XXX: add extra stats? */
	}

	/* Try to find host */
	hval = HOST_HVAL(cfg, &f_id->src_ip6);
	CK_SLIST_FOREACH(host, &HOSTS(cfg, hval), entries) {
		if (IN6_ARE_ADDR_EQUAL(&f_id->src_ip6, &host->addr))
			break;
	}
	/* We use IPv4 address in host byte order */
	addr = ntohl(addr);
	if (host == NULL)
		return (nat64lsn_request_host(cfg, f_id, mp,
		    hval, addr, port, proto));

	flags = proto != IPPROTO_TCP ? 0 : convert_tcp_flags(f_id->_flags);

	data[0] = addr;
	data[1] = (f_id->dst_port << 16) | port;
	hval = STATE_HVAL(cfg, data);
	state = nat64lsn_get_state6to4(cfg, host, f_id, hval, addr,
	    port, proto);
	if (state == NULL)
		return (nat64lsn_request_pg(cfg, host, f_id, mp, hval, addr,
		    port, proto));
	return (nat64lsn_translate6_internal(cfg, mp, state, flags));
}

/*
 * Main dataplane entry point.
 */
int
ipfw_nat64lsn(struct ip_fw_chain *ch, struct ip_fw_args *args,
    ipfw_insn *cmd, int *done)
{
	struct nat64lsn_cfg *cfg;
	ipfw_insn *icmd;
	int ret;

	IPFW_RLOCK_ASSERT(ch);

	*done = 0;	/* continue the search in case of failure */
	icmd = cmd + 1;
	if (cmd->opcode != O_EXTERNAL_ACTION ||
	    cmd->arg1 != V_nat64lsn_eid ||
	    icmd->opcode != O_EXTERNAL_INSTANCE ||
	    (cfg = NAT64_LOOKUP(ch, icmd)) == NULL)
		return (IP_FW_DENY);

	*done = 1;	/* terminate the search */

	switch (args->f_id.addr_type) {
	case 4:
		ret = nat64lsn_translate4(cfg, &args->f_id, &args->m);
		break;
	case 6:
		/*
		 * Check that destination IPv6 address matches our prefix6.
		 */
		if ((cfg->base.flags & NAT64LSN_ANYPREFIX) == 0 &&
		    memcmp(&args->f_id.dst_ip6, &cfg->base.plat_prefix,
		    cfg->base.plat_plen / 8) != 0) {
			ret = cfg->nomatch_verdict;
			break;
		}
		ret = nat64lsn_translate6(cfg, &args->f_id, &args->m);
		break;
	default:
		ret = cfg->nomatch_verdict;
	}

	if (ret != IP_FW_PASS && args->m != NULL) {
		m_freem(args->m);
		args->m = NULL;
	}
	return (ret);
}

static int
nat64lsn_state_ctor(void *mem, int size, void *arg, int flags)
{
	struct nat64lsn_states_chunk *chunk;
	int i;

	chunk = (struct nat64lsn_states_chunk *)mem;
	for (i = 0; i < 64; i++)
		chunk->state[i].flags = 0;
	return (0);
}

void
nat64lsn_init_internal(void)
{

	nat64lsn_host_zone = uma_zcreate("NAT64LSN hosts",
	    sizeof(struct nat64lsn_host), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	nat64lsn_pgchunk_zone = uma_zcreate("NAT64LSN portgroup chunks",
	    sizeof(struct nat64lsn_pgchunk), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	nat64lsn_pg_zone = uma_zcreate("NAT64LSN portgroups",
	    sizeof(struct nat64lsn_pg), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	nat64lsn_aliaslink_zone = uma_zcreate("NAT64LSN links",
	    sizeof(struct nat64lsn_aliaslink), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	nat64lsn_state_zone = uma_zcreate("NAT64LSN states",
	    sizeof(struct nat64lsn_states_chunk), nat64lsn_state_ctor,
	    NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	nat64lsn_job_zone = uma_zcreate("NAT64LSN jobs",
	    sizeof(struct nat64lsn_job_item), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	JQUEUE_LOCK_INIT();
}

void
nat64lsn_uninit_internal(void)
{

	/* XXX: epoch_task drain */
	JQUEUE_LOCK_DESTROY();
	uma_zdestroy(nat64lsn_host_zone);
	uma_zdestroy(nat64lsn_pgchunk_zone);
	uma_zdestroy(nat64lsn_pg_zone);
	uma_zdestroy(nat64lsn_aliaslink_zone);
	uma_zdestroy(nat64lsn_state_zone);
	uma_zdestroy(nat64lsn_job_zone);
}

void
nat64lsn_start_instance(struct nat64lsn_cfg *cfg)
{

	CALLOUT_LOCK(cfg);
	callout_reset(&cfg->periodic, hz * PERIODIC_DELAY,
	    nat64lsn_periodic, cfg);
	CALLOUT_UNLOCK(cfg);
}

struct nat64lsn_cfg *
nat64lsn_init_instance(struct ip_fw_chain *ch, in_addr_t prefix, int plen)
{
	struct nat64lsn_cfg *cfg;
	struct nat64lsn_alias *alias;
	int i, naddr;

	cfg = malloc(sizeof(struct nat64lsn_cfg), M_NAT64LSN,
	    M_WAITOK | M_ZERO);

	CFG_LOCK_INIT(cfg);
	CALLOUT_LOCK_INIT(cfg);
	STAILQ_INIT(&cfg->jhead);
	cfg->vp = curvnet;
	COUNTER_ARRAY_ALLOC(cfg->base.stats.cnt, NAT64STATS, M_WAITOK);

	cfg->hash_seed = arc4random();
	cfg->hosts_hashsize = NAT64LSN_HOSTS_HSIZE;
	cfg->hosts_hash = malloc(sizeof(struct nat64lsn_hosts_slist) *
	    cfg->hosts_hashsize, M_NAT64LSN, M_WAITOK | M_ZERO);
	for (i = 0; i < cfg->hosts_hashsize; i++)
		CK_SLIST_INIT(&cfg->hosts_hash[i]);

	naddr = 1 << (32 - plen);
	cfg->prefix4 = prefix;
	cfg->pmask4 = prefix | (naddr - 1);
	cfg->plen4 = plen;
	cfg->aliases = malloc(sizeof(struct nat64lsn_alias) * naddr,
	    M_NAT64LSN, M_WAITOK | M_ZERO);
	for (i = 0; i < naddr; i++) {
		alias = &cfg->aliases[i];
		alias->addr = prefix + i; /* host byte order */
		CK_SLIST_INIT(&alias->hosts);
		ALIAS_LOCK_INIT(alias);
	}

        callout_init_mtx(&cfg->periodic, &cfg->periodic_lock, 0);
        callout_init(&cfg->jcallout, CALLOUT_MPSAFE);

	return (cfg);
}

static void
nat64lsn_destroy_pg(struct nat64lsn_pg *pg)
{
	int i;

	if (pg->chunks_count == 1) {
		uma_zfree(nat64lsn_state_zone, pg->states);
	} else {
		for (i = 0; i < pg->chunks_count; i++)
			uma_zfree(nat64lsn_state_zone, pg->states_chunk[i]);
		free(pg->states_chunk, M_NAT64LSN);
		free(pg->freemask_chunk, M_NAT64LSN);
	}
	uma_zfree(nat64lsn_pg_zone, pg);
}

static void
nat64lsn_destroy_alias(struct nat64lsn_cfg *cfg,
    struct nat64lsn_alias *alias)
{
	struct nat64lsn_pg *pg;
	int i;

	while (!CK_SLIST_EMPTY(&alias->portgroups)) {
		pg = CK_SLIST_FIRST(&alias->portgroups);
		CK_SLIST_REMOVE_HEAD(&alias->portgroups, entries);
		nat64lsn_destroy_pg(pg);
	}
	for (i = 0; i < 32; i++) {
		if (ISSET32(alias->tcp_chunkmask, i))
			uma_zfree(nat64lsn_pgchunk_zone, alias->tcp[i]);
		if (ISSET32(alias->udp_chunkmask, i))
			uma_zfree(nat64lsn_pgchunk_zone, alias->udp[i]);
		if (ISSET32(alias->icmp_chunkmask, i))
			uma_zfree(nat64lsn_pgchunk_zone, alias->icmp[i]);
	}
	ALIAS_LOCK_DESTROY(alias);
}

static void
nat64lsn_destroy_host(struct nat64lsn_host *host)
{
	struct nat64lsn_aliaslink *link;

	while (!CK_SLIST_EMPTY(&host->aliases)) {
		link = CK_SLIST_FIRST(&host->aliases);
		CK_SLIST_REMOVE_HEAD(&host->aliases, host_entries);

		ALIAS_LOCK(link->alias);
		CK_SLIST_REMOVE(&link->alias->hosts, link,
		    nat64lsn_aliaslink, alias_entries);
		link->alias->hosts_count--;
		ALIAS_UNLOCK(link->alias);

		uma_zfree(nat64lsn_aliaslink_zone, link);
	}
	HOST_LOCK_DESTROY(host);
	free(host->states_hash, M_NAT64LSN);
	uma_zfree(nat64lsn_host_zone, host);
}

void
nat64lsn_destroy_instance(struct nat64lsn_cfg *cfg)
{
	struct nat64lsn_host *host;
	int i;

	CALLOUT_LOCK(cfg);
	callout_drain(&cfg->periodic);
	CALLOUT_UNLOCK(cfg);
	callout_drain(&cfg->jcallout);

	for (i = 0; i < cfg->hosts_hashsize; i++) {
		while (!CK_SLIST_EMPTY(&cfg->hosts_hash[i])) {
			host = CK_SLIST_FIRST(&cfg->hosts_hash[i]);
			CK_SLIST_REMOVE_HEAD(&cfg->hosts_hash[i], entries);
			nat64lsn_destroy_host(host);
		}
	}

	for (i = 0; i < (1 << (32 - cfg->plen4)); i++)
		nat64lsn_destroy_alias(cfg, &cfg->aliases[i]);

	CALLOUT_LOCK_DESTROY(cfg);
	CFG_LOCK_DESTROY(cfg);
	COUNTER_ARRAY_FREE(cfg->base.stats.cnt, NAT64STATS);
	free(cfg->hosts_hash, M_NAT64LSN);
	free(cfg->aliases, M_NAT64LSN);
	free(cfg, M_NAT64LSN);
}


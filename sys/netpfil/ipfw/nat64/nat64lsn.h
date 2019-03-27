/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2019 Yandex LLC
 * Copyright (c) 2015 Alexander V. Chernikov <melifaro@FreeBSD.org>
 * Copyright (c) 2015-2019 Andrey V. Elsukov <ae@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef	_IP_FW_NAT64LSN_H_
#define	_IP_FW_NAT64LSN_H_

#include "ip_fw_nat64.h"
#include "nat64_translate.h"

#define	NAT64_MIN_PORT		1024
struct nat64lsn_host;
struct nat64lsn_alias;

struct nat64lsn_state {
	/* IPv6 host entry keeps hash table to speedup state lookup */
	CK_SLIST_ENTRY(nat64lsn_state)	entries;
	struct nat64lsn_host	*host;

	struct in6_addr	ip6_dst;	/* Destination IPv6 address */

	in_addr_t	ip_src;		/* Alias IPv4 address */
	in_addr_t	ip_dst;		/* Destination IPv4 address */
	uint16_t	dport;		/* Destination port */
	uint16_t	sport;		/* Source port */

	uint32_t	hval;
	uint32_t	flags;		/* Internal flags */
	uint16_t	aport;
	uint16_t	timestamp;	/* last used */
	uint8_t		proto;
	uint8_t		_spare[7];
};

struct nat64lsn_states_chunk {
	struct nat64lsn_state	state[64];
};

#define	ISSET64(mask, bit)	((mask) & ((uint64_t)1 << (bit)))
#define	ISSET32(mask, bit)	((mask) & ((uint32_t)1 << (bit)))
struct nat64lsn_pg {
	CK_SLIST_ENTRY(nat64lsn_pg)	entries;

	uint16_t		base_port;
	uint16_t		timestamp;
	uint8_t			proto;
	uint8_t			chunks_count;
	uint8_t			spare[2];

	union {
		uint64_t	freemask64;
		uint32_t	freemask32[2];
		uint64_t	*freemask64_chunk;
		uint32_t	*freemask32_chunk;
		void		*freemask_chunk;
	};
	union {
		struct nat64lsn_states_chunk *states;
		struct nat64lsn_states_chunk **states_chunk;
	};
};

#define	CHUNK_BY_FADDR(p, a)	((a) & ((p)->chunks_count - 1))

#ifdef __LP64__
#define	FREEMASK_CHUNK(p, v)	\
    ((p)->chunks_count == 1 ? &(p)->freemask64 : \
	&(p)->freemask64_chunk[CHUNK_BY_FADDR(p, v)])
#define	FREEMASK_BITCOUNT(pg, faddr)	\
    bitcount64(*FREEMASK_CHUNK((pg), (faddr)))
#else
#define	FREEMASK_CHUNK(p, v)	\
    ((p)->chunks_count == 1 ? &(p)->freemask32[0] : \
	&(p)->freemask32_chunk[CHUNK_BY_FADDR(p, v) * 2])
#define	FREEMASK_BITCOUNT(pg, faddr)	\
    bitcount64(*(uint64_t *)FREEMASK_CHUNK((pg), (faddr)))
#endif /* !__LP64__ */

struct nat64lsn_pgchunk {
	struct nat64lsn_pg	*pgptr[32];
};

struct nat64lsn_aliaslink {
	CK_SLIST_ENTRY(nat64lsn_aliaslink)	alias_entries;
	CK_SLIST_ENTRY(nat64lsn_aliaslink)	host_entries;
	struct nat64lsn_alias	*alias;
};

CK_SLIST_HEAD(nat64lsn_aliaslink_slist, nat64lsn_aliaslink);
CK_SLIST_HEAD(nat64lsn_states_slist, nat64lsn_state);
CK_SLIST_HEAD(nat64lsn_hosts_slist, nat64lsn_host);
CK_SLIST_HEAD(nat64lsn_pg_slist, nat64lsn_pg);

struct nat64lsn_alias {
	struct nat64lsn_aliaslink_slist	hosts;
	struct nat64lsn_pg_slist	portgroups;

	struct mtx		lock;
	in_addr_t		addr;	/* host byte order */
	uint32_t		hosts_count;
	uint32_t		portgroups_count;
	uint32_t		tcp_chunkmask;
	uint32_t		udp_chunkmask;
	uint32_t		icmp_chunkmask;

	uint32_t		tcp_pgidx;
	uint32_t		udp_pgidx;
	uint32_t		icmp_pgidx;
	uint16_t		timestamp;
	uint16_t		spare;

	uint32_t		tcp_pgmask[32];
	uint32_t		udp_pgmask[32];
	uint32_t		icmp_pgmask[32];
	struct nat64lsn_pgchunk	*tcp[32];
	struct nat64lsn_pgchunk	*udp[32];
	struct nat64lsn_pgchunk	*icmp[32];

	/* pointer to PG that can be used for faster state allocation */
	struct nat64lsn_pg	*tcp_pg;
	struct nat64lsn_pg	*udp_pg;
	struct nat64lsn_pg	*icmp_pg;
};
#define	ALIAS_LOCK_INIT(p)	\
	mtx_init(&(p)->lock, "alias_lock", NULL, MTX_DEF)
#define	ALIAS_LOCK_DESTROY(p)	mtx_destroy(&(p)->lock)
#define	ALIAS_LOCK(p)		mtx_lock(&(p)->lock)
#define	ALIAS_UNLOCK(p)		mtx_unlock(&(p)->lock)

#define	NAT64LSN_HSIZE		256
#define	NAT64LSN_MAX_HSIZE	4096
#define	NAT64LSN_HOSTS_HSIZE	1024

struct nat64lsn_host {
	struct in6_addr		addr;
	struct nat64lsn_aliaslink_slist	aliases;
	struct nat64lsn_states_slist	*states_hash;
	CK_SLIST_ENTRY(nat64lsn_host)	entries;
	uint32_t		states_count;
	uint32_t		hval;
	uint32_t		flags;
#define	NAT64LSN_DEADHOST	1
#define	NAT64LSN_GROWHASH	2
	uint16_t		states_hashsize;
	uint16_t		timestamp;
	struct mtx		lock;
};

#define	HOST_LOCK_INIT(p)	\
	mtx_init(&(p)->lock, "host_lock", NULL, MTX_DEF|MTX_NEW)
#define	HOST_LOCK_DESTROY(p)	mtx_destroy(&(p)->lock)
#define	HOST_LOCK(p)		mtx_lock(&(p)->lock)
#define	HOST_UNLOCK(p)		mtx_unlock(&(p)->lock)

VNET_DECLARE(uint16_t, nat64lsn_eid);
#define	V_nat64lsn_eid		VNET(nat64lsn_eid)
#define	IPFW_TLV_NAT64LSN_NAME	IPFW_TLV_EACTION_NAME(V_nat64lsn_eid)

/* Timestamp macro */
#define	_CT		((int)time_uptime % 65536)
#define	SET_AGE(x)	(x) = _CT
#define	GET_AGE(x)	((_CT >= (x)) ? _CT - (x): (int)65536 + _CT - (x))

STAILQ_HEAD(nat64lsn_job_head, nat64lsn_job_item);

struct nat64lsn_cfg {
	struct named_object	no;

	struct nat64lsn_hosts_slist	*hosts_hash;
	struct nat64lsn_alias	*aliases;	/* array of aliases */

	struct mtx	lock;
	uint32_t	hosts_hashsize;
	uint32_t	hash_seed;

	uint32_t	prefix4;	/* IPv4 prefix */
	uint32_t	pmask4;		/* IPv4 prefix mask */
	uint8_t		plen4;
	uint8_t		nomatch_verdict;/* Return value on no-match */

	uint32_t	hosts_count;	/* Number of items in host hash */
	uint32_t	states_chunks;	/* Number of states chunks per PG */
	uint32_t	jmaxlen;	/* Max jobqueue length */
	uint16_t	host_delete_delay;	/* Stale host delete delay */
	uint16_t	pgchunk_delete_delay;
	uint16_t	pg_delete_delay;	/* Stale portgroup del delay */
	uint16_t	st_syn_ttl;	/* TCP syn expire */
	uint16_t	st_close_ttl;	/* TCP fin expire */
	uint16_t	st_estab_ttl;	/* TCP established expire */
	uint16_t	st_udp_ttl;	/* UDP expire */
	uint16_t	st_icmp_ttl;	/* ICMP expire */

	struct nat64_config	base;
#define	NAT64LSN_FLAGSMASK	(NAT64_LOG | NAT64_ALLOW_PRIVATE)
#define	NAT64LSN_ANYPREFIX	0x00000100

	struct mtx		periodic_lock;
	struct callout		periodic;
	struct callout		jcallout;
	struct vnet		*vp;
	struct nat64lsn_job_head	jhead;
	int			jlen;
	char			name[64];	/* Nat instance name */
};

/* CFG_LOCK protects cfg->hosts_hash from modification */
#define	CFG_LOCK_INIT(p)	\
	mtx_init(&(p)->lock, "cfg_lock", NULL, MTX_DEF)
#define	CFG_LOCK_DESTROY(p)	mtx_destroy(&(p)->lock)
#define	CFG_LOCK(p)		mtx_lock(&(p)->lock)
#define	CFG_UNLOCK(p)		mtx_unlock(&(p)->lock)

#define	CALLOUT_LOCK_INIT(p)	\
	mtx_init(&(p)->periodic_lock, "periodic_lock", NULL, MTX_DEF)
#define	CALLOUT_LOCK_DESTROY(p)	mtx_destroy(&(p)->periodic_lock)
#define	CALLOUT_LOCK(p)		mtx_lock(&(p)->periodic_lock)
#define	CALLOUT_UNLOCK(p)	mtx_unlock(&(p)->periodic_lock)

struct nat64lsn_cfg *nat64lsn_init_instance(struct ip_fw_chain *ch,
    in_addr_t prefix, int plen);
void nat64lsn_destroy_instance(struct nat64lsn_cfg *cfg);
void nat64lsn_start_instance(struct nat64lsn_cfg *cfg);
void nat64lsn_init_internal(void);
void nat64lsn_uninit_internal(void);
int ipfw_nat64lsn(struct ip_fw_chain *ch, struct ip_fw_args *args,
    ipfw_insn *cmd, int *done);

#endif /* _IP_FW_NAT64LSN_H_ */
